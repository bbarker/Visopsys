//
//  Visopsys
//  Copyright (C) 1998-2018 J. Andrew McLaughlin
//
//  This library is free software; you can redistribute it and/or modify it
//  under the terms of the GNU Lesser General Public License as published by
//  the Free Software Foundation; either version 2.1 of the License, or (at
//  your option) any later version.
//
//  This library is distributed in the hope that it will be useful, but
//  WITHOUT ANY WARRANTY; without even the implied warranty of
//  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU Lesser
//  General Public License for more details.
//
//  You should have received a copy of the GNU Lesser General Public License
//  along with this library; if not, write to the Free Software Foundation,
//  Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
//
//  deflate_compress.c
//

// This is the compression half of the library code for the DEFLATE algorithm.

#include "libcompress.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/api.h>
#include <sys/compress.h>
#include <sys/deflate.h>

#ifdef DEBUG
	#define DEBUG_OUTMAX	160
	int debugDeflateCompress = 0;
	static char debugOutput[DEBUG_OUTMAX];

	static void DEBUGMSG(const char *message, ...)
	{
		va_list list;

		if (debugDeflateCompress)
		{
			va_start(list, message);
			vsnprintf(debugOutput, DEBUG_OUTMAX, message, list);
			printf("%s", debugOutput);
		}
	}
#else
	#define DEBUGMSG(message, arg...) do { } while (0)
#endif


static void writeBitField(deflateState *state, int bits,
	unsigned short data)
{
	// Write bitfield bits, in LSB order.

	int written = 0;
	int count;

	while (bits)
	{
		if ((data >> written) & 1)
			state->bitOut.data[state->bitOut.byte] |= (1 << state->bitOut.bit);

		bits -= 1; written += 1; state->bitOut.bit += 1;

		if (state->bitOut.bit >= 8)
		{
			state->bitOut.byte += 1;
			state->bitOut.bit = 0;
		}
	}

	DEBUGMSG("[");
	for (count = (written - 1); count >= 0; count --)
	{
		if (data & (1 << count))
			DEBUGMSG("1");
		else
			DEBUGMSG("0");
	}
	DEBUGMSG("] ");
}


static inline void writeBits(deflateState *state, int bits,
	unsigned short data)
{
	// Write Huffman code bits, in MSB order.

	DEBUGMSG("%dx[", bits);
	while (bits)
	{
		if ((data >> (bits - 1)) & 1)
		{
			DEBUGMSG("1");
			state->bitOut.data[state->bitOut.byte] |= (1 << state->bitOut.bit);
		}
		else
		{
			DEBUGMSG("0");
		}

		bits -= 1; state->bitOut.bit += 1;

		if (state->bitOut.bit >= 8)
		{
			state->bitOut.byte += 1;
			state->bitOut.bit = 0;
		}
	}
	DEBUGMSG("] ");
}


static void skipOutputBits(deflateState *state)
{
	// Skip the remaining bits of the byte.  Write zeros.

	if (state->bitOut.bit)
	{
		DEBUGMSG("Skip %d bits\n", (8 - state->bitOut.bit));
		state->bitOut.byte += 1;
		state->bitOut.bit = 0;
	}
}


static inline int addHashNode(deflateState *state, const unsigned char *data)
{
	int status = 0;
	hashNode **start = NULL;
	hashNode *newNode = NULL;
	hashNode *nextNode = NULL;

	DEBUGMSG("Add hash node '%02x %02x %02x' offset %d\n", data[0], data[1],
		data[2], (data - state->byteIn.data));

	if (state->hash.numFreeNodes <= 0)
	{
		DEBUGMSG("No free hash nodes\n");
		return (status = ERR_NOFREE);
	}

	// Take a new hash node from the list of free nodes
	newNode = state->hash.freeNodes;
	state->hash.freeNodes = newNode->next;
	state->hash.numFreeNodes -= 1;

	// Set up the new node
	newNode->generation = state->hash.generation;
	newNode->data = data;
	newNode->prev = NULL;

	// Add the node to the appropriate bucket
	start = &state->hash.buckets[data[0]].sub[data[1]];
	nextNode = *start;
	newNode->next = nextNode;
	if (nextNode)
		nextNode->prev = newNode;
	*start = newNode;

	return (status = 0);
}


static inline void removeHashNode(deflateState *state, int bucket,
	int subBucket, hashNode *removeNode)
{
	hashNode **start = NULL;

	//DEBUGMSG("Remove hash node\n");

	// Remove the node from the appropriate bucket
	start = &state->hash.buckets[bucket].sub[subBucket];
	if (*start == removeNode)
		*start = removeNode->next;
	else
		removeNode->prev->next = removeNode->next;
	if (removeNode->next)
		removeNode->next->prev = removeNode->prev;

	// Add it back to the list of free nodes
	removeNode->next = state->hash.freeNodes;
	state->hash.freeNodes = removeNode;
	state->hash.numFreeNodes += 1;
}


static void initHashTable(deflateState *state)
{
	unsigned ptrAdjust = 0;
	hashNode *node = NULL;
	hashNode *next = NULL;
	int count1, count2;

	DEBUGMSG("Init hash table\n");

	if (!state->inByte)
	{
		DEBUGMSG("Start new hash table\n");

		// We're starting at the beginning of the buffer, so initialize
		// everything in the hash table.

		memset(&state->hash, 0, sizeof(hashTable));
		state->hash.freeNodes = state->hash.nodeMemory;
		state->hash.numFreeNodes = DEFLATE_HASH_NODES;

		for (count1 = 0; count1 < DEFLATE_HASH_NODES; count1 ++)
		{
			// No need to set the 'prev' link in the free node list
			if (count1 < (DEFLATE_HASH_NODES - 1))
				state->hash.freeNodes[count1].next =
					&state->hash.freeNodes[count1 + 1];
		}
	}
	else
	{
		DEBUGMSG("Keep existing hash table\n");

		// There is previous data in the buffer, so adjust the pointers in the
		// hash table, and prune any hash nodes that have moved outside our
		// DEFLATE_MAX_DISTANCE (32k) distance window (more than one generation
		// ago).
		if (state->inByte < state->hash.byte)
			ptrAdjust = (state->hash.byte - state->inByte);

		DEBUGMSG("%sdjust pointers\n", (ptrAdjust? "A" : "Don't a"));

		state->hash.generation += 1;

		for (count1 = 0; count1 < DEFLATE_HASH_BUCKETS; count1 ++)
		{
			for (count2 = 0; count2 < DEFLATE_HASH_BUCKETS; count2 ++)
			{
				node = state->hash.buckets[count1].sub[count2];

				while (node)
				{
					next = node->next;
					if (node->generation < (state->hash.generation - 1))
						removeHashNode(state, count1, count2, node);
					else if (ptrAdjust)
						node->data -= ptrAdjust;

					node = next;
				}
			}
		}
	}
}


static int processInput(deflateState *state)
{
	// Examines all of the input data, builds hash chains and searches for
	// matches, and outputs a combination of literal values and length/distance
	// (match) values.

	int status = 0;
	const unsigned char *ptr = (state->byteIn.data + state->byteIn.byte);
	int remainingBytes = state->byteIn.bufferedBytes;
	int bucket = 0, subBucket = 0;
	hashNode *searchNode = NULL;
	hashNode *nextNode = NULL;
	int nodeMatch = 0;
	int nodeDistance = 0;
	int matchLength = 0;
	int distance = 0;

	DEBUGMSG("Process %d bytes of input data\n", remainingBytes);

	memset(&state->processed, 0, sizeof(processedInput));

	// Initialize the hash table
	initHashTable(state);

	// Loop through the input
	while (remainingBytes >= 3)
	{
		// Hash buckets and sub-buckets are indexed by the first two data
		// bytes, so that we only have to search for a matching third byte.
		// Thus, it's not a true hashing function at all.  It's done this
		// way for speed, at the expense of expanded memory usage, to reduce
		// search iterations and improve performance.
		bucket = ptr[0];
		subBucket = ptr[1];

		//DEBUGMSG("Offset %d, search for match for '%02x %02x %02x' in "
		//	"bucket %d sub %d\n", (ptr - state->byteIn.data), ptr[0], ptr[1],
		//	ptr[2], bucket, subBucket);

		searchNode = state->hash.buckets[bucket].sub[subBucket];
		matchLength = 0;
		distance = 0;

		while (searchNode)
		{
			nextNode = searchNode->next;

			if (ptr[2] == searchNode->data[2])
			{
				// We found a hash table match.  Is it within the permissable
				// distance?
				nodeDistance = (ptr - searchNode->data);
				if (nodeDistance <= DEFLATE_MAX_DISTANCE)
				{
					// Initially, we've matched 3 bytes.
					nodeMatch = 3;

					// Continue looking for more matching bytes
					while ((nodeMatch < min(remainingBytes, 258)) &&
						(ptr[nodeMatch] == searchNode->data[nodeMatch]))
					{
						nodeMatch += 1;
					}

					DEBUGMSG("Found hash match of length %d at distance %d\n",
						nodeMatch, nodeDistance);

					// Prefer less distant nodes (occur sooner in the bucket
					// list) by requiring a more distant match to be longer.
					if (nodeMatch > matchLength)
					{
						matchLength = nodeMatch;
						distance = nodeDistance;
					}
				}
				else
				{
					// Out of range; remove it from the table to reduce search
					// times.
					removeHashNode(state, bucket, subBucket, searchNode);
				}
			}

			// Look at the next one.
			searchNode = nextNode;
		}

		// Add the 3 bytes to the hash table
		status = addHashNode(state, ptr);
		if (status < 0)
			return (status);

		if (matchLength)
		{
			// Output the length and distance codes
			state->processed.codes[state->processed.numCodes++] =
				(0x8000 | matchLength);
			state->processed.codes[state->processed.numCodes++] = distance;
			ptr += matchLength;
			remainingBytes -= matchLength;
		}
		else
		{
			// Output the current byte as a literal
			state->processed.codes[state->processed.numCodes++] = *(ptr++);
			remainingBytes -= 1;
		}
	}

	// Copy the last byte(s) as literals
	while (remainingBytes--)
		state->processed.codes[state->processed.numCodes++] = *(ptr++);

	// Add end-of-block (256)
	state->processed.codes[state->processed.numCodes++] = DEFLATE_CODE_EOB;

	// Remember where in the input buffer we ended this round.
	state->hash.byte = (state->inByte + state->byteIn.bufferedBytes);

	return (status = 0);
}


static int copyUncompressedOutputBlock(deflateState *state)
{
	// Given some data in the input buffer that we don't want to compress, copy
	// it straight to the output stream.

	int status = 0;
	unsigned short length = state->byteIn.bufferedBytes;
	unsigned short nLength = (unsigned short) ~length;

	DEBUGMSG("Uncompressed block of %d bytes\n", length);

	// Write out the compression method
	DEBUGMSG("Compression method: ");
	writeBitField(state, 2, DEFLATE_BTYPE_NONE);
	DEBUGMSG("\n");

	// Discard the remaining bits of the current output byte
	if (state->bitOut.bit)
		skipOutputBits(state);

	// Write the length value and its complementary value
	DEBUGMSG("Length words (%04x %04x): ", length, nLength);
	writeBitField(state, 16, length);
	writeBitField(state, 16, nLength);
	DEBUGMSG("\n");

	// Output the data
	memcpy((state->bitOut.data + state->bitOut.byte),
		state->byteIn.data, length);
	state->bitOut.byte += length;

	return (status = 0);
}


static void makeStaticHuffmanLitLenTable(deflateState *state)
{
	// Construct a table of static Huffman codes

	int count;

	memset(&state->staticTable, 0, sizeof(huffmanTable));
	state->staticTable.leastBits = 7;
	state->staticTable.mostBits = 9;
	state->staticTable.numCodes = DEFLATE_LITLEN_CODES;

	// Add 8-bit codes 0-143
	for (count = 0; count <= 143; count ++)
	{
		state->staticTable.codes[count].len = 8;
		state->staticTable.codes[count].code = (0x30 + count);
	}

	// Add 9-bit codes 144-255
	for (count = 144; count <= 255; count ++)
	{
		state->staticTable.codes[count].len = 9;
		state->staticTable.codes[count].code = (0x190 + (count - 144));
	}

	// Add 7-bit codes 256-279
	for (count = 256; count <= 279; count ++)
	{
		state->staticTable.codes[count].len = 7;
		state->staticTable.codes[count].code = count;
	}

	// Add 8-bit codes 280-287
	for (count = 280; count <= 287; count ++)
	{
		state->staticTable.codes[count].len = 8;
		state->staticTable.codes[count].code = (0xC0 + (count - 280));
	}
}


static void calcLenExtra(unsigned short len, unsigned short *newCode,
	int *retNumExtraBits, unsigned short *retExtraBits)
{
	int numExtraBits = 0;
	unsigned short extraBits = 0;
	int p2 = 8;

	if (len <= 10)
	{
		*newCode = (254 + len);
	}
	else if (len == 258)
	{
		*newCode = 285;
	}
	else
	{
		len -= 3;

		while (len & ~(p2 - 1))
		{
			p2 <<= 1;
			numExtraBits += 1;
		}

		*newCode = (261 + (numExtraBits << 2) +
			((len - (p2 >> 1)) / (1 << numExtraBits)));
		extraBits = (len % (1 << numExtraBits));
	}

	if (retNumExtraBits)
		*retNumExtraBits = numExtraBits;
	if (retExtraBits)
		*retExtraBits = extraBits;
}


static void calcDistExtra(unsigned short dist, unsigned short *newCode,
	int *retNumExtraBits, unsigned short *retExtraBits)
{
	int numExtraBits = 0;
	unsigned short extraBits = 0;
	int p2 = 4;

	dist -= 1;

	if (dist <= 3)
	{
		*newCode = dist;
	}
	else
	{
		while (dist & ~(p2 - 1))
		{
			p2 <<= 1;
			numExtraBits += 1;
		}

		*newCode = (2 + (numExtraBits << 1) +
			((dist - (p2 >> 1)) / (1 << numExtraBits)));
		extraBits = (dist % (1 << numExtraBits));
	}

	if (retNumExtraBits)
		*retNumExtraBits = numExtraBits;
	if (retExtraBits)
		*retExtraBits = extraBits;
}


static int compressStaticBlock(deflateState *state)
{
	// Compress a block of data using static Huffman codes.

	int status = 0;
	unsigned short code = 0;
	int numExtraBits = 0;
	unsigned short extraBits = 0;
	int count;

	DEBUGMSG("Static block of %d bytes\n", state->byteIn.bufferedBytes);

	// Write out the compression method
	DEBUGMSG("Compression method: ");
	writeBitField(state, 2, DEFLATE_BTYPE_FIXED);
	DEBUGMSG("\n");

	// Write the compressed data
	for (count = 0; count < state->processed.numCodes; count ++)
	{
		code = state->processed.codes[count];

		if (code & 0x8000)
		{
			// This is a distance-length combo.  Figure out which distance/
			// length codes and extra bits to output.
			calcLenExtra((code & 0x7FFF), &code, &numExtraBits, &extraBits);

			DEBUGMSG("Write length code %d %dx%04x ", code,
				state->staticTable.codes[code].len,
				state->staticTable.codes[code].code);
			writeBits(state, state->staticTable.codes[code].len,
				state->staticTable.codes[code].code);

			if (numExtraBits)
			{
				DEBUGMSG("extra bits ");
				writeBitField(state, numExtraBits, extraBits);
			}

			DEBUGMSG("\n");

			code = state->processed.codes[++count];

			calcDistExtra(code, &code, &numExtraBits, &extraBits);

			DEBUGMSG("Write distance code %d %dx%02x ", code, 5, code);
			writeBits(state, 5, code);

			if (numExtraBits)
			{
				DEBUGMSG("extra bits ");
				writeBitField(state, numExtraBits, extraBits);
			}

			DEBUGMSG("\n");
		}
		else
		{
			// This is a literal byte
			DEBUGMSG("Write literal code %d %dx%04x ", code,
				state->staticTable.codes[code].len,
				state->staticTable.codes[code].code);
			writeBits(state, state->staticTable.codes[code].len,
				state->staticTable.codes[code].code);

			DEBUGMSG("\n");
		}
	}

	return (status = 0);
}


static int getLeastFrequentCode(unsigned short *codeCounts, int numCodeCounts)
{
	int leastFrequentCode = -1;
	unsigned short smallestCount = 0xFFFF;
	int count;

	for (count = 0; count < numCodeCounts; count ++)
	{
		if (codeCounts[count] && (codeCounts[count] < smallestCount))
		{
			smallestCount = codeCounts[count];
			leastFrequentCode = count;
		}
	}

	if (leastFrequentCode >= 0)
	{
		DEBUGMSG("Least frequent code=%d, count=%u\n", leastFrequentCode,
			codeCounts[leastFrequentCode]);
	}

	return (leastFrequentCode);
}


static void insertHuffmanNode(huffmanNode *nodeQueue[], int *numQueued,
	huffmanNode *node)
{
	// Insert an internal node into the queue.  Largest go at the back.

	int count;

	for (count = *numQueued; count; count --)
	{
		if (nodeQueue[count - 1]->weight <= node->weight)
			break;

		nodeQueue[count] = nodeQueue[count - 1];
	}

	nodeQueue[count] = node;
	*numQueued += 1;
}


static void makeHuffmanTree(huffmanTree *tree, unsigned short *codeCounts,
	int numCodeCounts, int balance)
{
	// Build a Huffman tree from the supplied list of code counts.  If the
	// 'balance' parameter is set, the algorithm will attempt to keep the tree
	// balanced as it goes, by combining internal nodes instead of always
	// using remaining leaves.  Usually this produces the shortest average code
	// lengths.  However, other times, it produces unacceptably long codes, and
	// we do better by always using remaining leaves, and rebalancing the
	// tree afterwards.

	unsigned short tmpCodeCounts[DEFLATE_LITLEN_CODES];
	int leastFrequentCode = 0;
	huffmanLeaf *leafQueue[DEFLATE_LITLEN_CODES];
	int numQueuedLeaves = 0;
	huffmanNode *nodeQueue[DEFLATE_LITLEN_CODES];
	int numQueuedNodes = 0;
	huffmanNode *newNode = NULL;

	DEBUGMSG("Build Huffman tree (%d counts)\n", numCodeCounts);

	memset(tree, 0, sizeof(huffmanTree));

	// Assign the counts to leaf nodes in our tree, and add them to our leaf
	// queue
	memcpy(tmpCodeCounts, codeCounts, (numCodeCounts *
		sizeof(unsigned short)));
	while ((leastFrequentCode = getLeastFrequentCode(tmpCodeCounts,
		numCodeCounts)) >= 0)
	{
		leafQueue[numQueuedLeaves] = &tree->leafMem[tree->numLeaves++];
		leafQueue[numQueuedLeaves]->weight = tmpCodeCounts[leastFrequentCode];
		leafQueue[numQueuedLeaves]->value = leastFrequentCode;
		numQueuedLeaves += 1;
		tmpCodeCounts[leastFrequentCode] = 0;
	}

	// Loop while there are leaf items in our leaf queue, or more than 1 item
	// in our node queue.
	while (numQueuedLeaves || (numQueuedNodes > 1))
	{
		// Pick 2 smallest items from the front of the leaf and node queues.
		// Preference is given to the leaf queue.

		// We always create a new node
		newNode = &tree->nodeMem[tree->numNodes++];

		if ((numQueuedLeaves > 1) &&
			(!numQueuedNodes ||
				((leafQueue[0]->weight <= nodeQueue[0]->weight) &&
				(leafQueue[1]->weight <= nodeQueue[0]->weight))))
		{
			// Take first 2 items from the leaf queue, add them to a new node,
			// and add that to the node queue.

			newNode->weight = (leafQueue[0]->weight + leafQueue[1]->weight);
			newNode->left = leafQueue[0];
			newNode->leftIsLeaf = 1;
			newNode->right = leafQueue[1];
			newNode->rightIsLeaf = 1;

			DEBUGMSG("Combine leaves (value=%d, weight=%d), (value=%d, "
				"weight=%d) = node weight %d\n",
				leafQueue[0]->value, leafQueue[0]->weight, leafQueue[1]->value,
				leafQueue[1]->weight, newNode->weight);

			// Remove 2 leaves from the front of the leaf queue
			numQueuedLeaves -= 2;
			memmove(leafQueue, &leafQueue[2],
				(numQueuedLeaves * sizeof(huffmanLeaf *)));
		}

		else if ((numQueuedNodes > 1) &&
			(!numQueuedLeaves ||
				(balance && (nodeQueue[0]->weight < leafQueue[0]->weight) &&
				(nodeQueue[1]->weight < leafQueue[0]->weight))))
		{
			// Take first 2 items from the node queue, add them to a new node,
			// and add that to the node queue.

			newNode->weight = (nodeQueue[0]->weight + nodeQueue[1]->weight);
			newNode->left = nodeQueue[0];
			newNode->right = nodeQueue[1];

			DEBUGMSG("Combine nodes weight=%d,%d = node weight=%d\n",
				nodeQueue[0]->weight, nodeQueue[1]->weight, newNode->weight);

			// Remove 2 nodes from the front of the node queue
			numQueuedNodes -= 2;
			memmove(nodeQueue, &nodeQueue[2],
				(numQueuedNodes * sizeof(huffmanNode *)));
		}

		else
		{
			// Take the first item from the leaf queue, and the first item
			// from the node queue, add them to a new node, and add that to
			// the node queue.

			newNode->weight = (nodeQueue[0]->weight + leafQueue[0]->weight);
			newNode->left = nodeQueue[0];
			newNode->right = leafQueue[0];
			newNode->rightIsLeaf = 1;

			DEBUGMSG("Combine leaf (value=%d, weight=%d), node weight=%d = "
				"node weight=%d\n",leafQueue[0]->value, leafQueue[0]->weight,
				nodeQueue[0]->weight, newNode->weight);

			// Remove a leaf from the front of the leaf queue, and a node from
			// the front of the node queue
			numQueuedLeaves -= 1;
			memmove(leafQueue, &leafQueue[1],
				(numQueuedLeaves * sizeof(huffmanLeaf *)));
			numQueuedNodes -= 1;
			memmove(nodeQueue, &nodeQueue[1],
				(numQueuedNodes * sizeof(huffmanNode *)));
		}

		// Queue the new node
		if (balance)
			// New node is always the biggest
			nodeQueue[numQueuedNodes++] = newNode;
		else
			// Need to insert it into the correct queue position.
			insertHuffmanNode(nodeQueue, &numQueuedNodes, newNode);
	}

	tree->rootNode = nodeQueue[0];
}


static void balanceTree(huffmanNode *node)
{
	// Attempt to re-balance the tree, to shorten the length of codes.  Doesn't
	// update node weights (they're not needed after the tree is built).
	// Assumes that makeHuffmanTree() creates all nodes with 2 children, and
	// that 'mixed' nodes (one leaf child and one non-) are built with the leaf
	// on the right.

	huffmanNode *moveNode = NULL;

	if (!node->leftIsLeaf && node->rightIsLeaf)
	{
		moveNode = node->left;

		if (!moveNode->leftIsLeaf && moveNode->rightIsLeaf)
		{
			DEBUGMSG("move node weight %d right\n", moveNode->weight);
			node->left = moveNode->left;
			node->leftIsLeaf = 0;
			moveNode->left = moveNode->right;
			moveNode->leftIsLeaf = 1;
			moveNode->right = node->right;
			moveNode->rightIsLeaf = 1;
			node->right = moveNode;
			node->rightIsLeaf = 0;
		}
	}

	if (!node->leftIsLeaf)
		balanceTree(node->left);

	if (!node->rightIsLeaf)
		balanceTree(node->right);
}


static int recurseHuffmanNodes(huffmanNode *node, int depth, int maxDepth,
	unsigned char *codeLengths)
{
	// Calculate code bit-lengths from the tree.  Assumes that
	// makeHuffmanTree() creates all nodes with 2 children.

	int status = 0;
	huffmanLeaf *leaf = NULL;

	if (node->leftIsLeaf)
	{
		leaf = node->left;
		codeLengths[leaf->value] = (depth + 1);
		DEBUGMSG("value=%d, bits=%d\n", leaf->value, codeLengths[leaf->value]);
	}
	else
	{
		status = recurseHuffmanNodes(node->left, (depth + 1), maxDepth,
			codeLengths);
		if (status < 0)
			return (status);
	}

	if (node->rightIsLeaf)
	{
		leaf = node->right;
		codeLengths[leaf->value] = (depth + 1);
		DEBUGMSG("value=%d, bits=%d\n", leaf->value, codeLengths[leaf->value]);
	}
	else
	{
		status = recurseHuffmanNodes(node->right, (depth + 1), maxDepth,
			codeLengths);
		if (status < 0)
			return (status);
	}

	if (depth >= maxDepth)
	{
		DEBUGMSG("Tree depth (%d) exceeds maximum bits (%d)\n", (depth + 1),
			maxDepth);
		return (status = ERR_RANGE);
	}

	return (status = 0);
}


static int makeCodeLengths(huffmanTree *tree, unsigned short *codeCounts,
	int numCodeCounts, unsigned char *codeLengths, int maxBits)
{
	// Produce the list of code lengths from the list of code counts.

	int status = 0;

	// Turn the code counts into a Huffman tree.
	makeHuffmanTree(tree, codeCounts, numCodeCounts, 1 /* balance */);

	DEBUGMSG("Attempt to balance Huffman tree\n");
	balanceTree(tree->rootNode);

	// Walk the tree to determine the code lengths.
	DEBUGMSG("Calculate tree code lengths\n");
	status = recurseHuffmanNodes(tree->rootNode, 0, maxBits, codeLengths);

	if (status < 0)
	{
		// The codes were too long.  Try again, relying on our post-balancing
		// to make the codes short enough.

		DEBUGMSG("Retry without inline balancing\n");
		makeHuffmanTree(tree, codeCounts, numCodeCounts, 0 /* no balance */);

		DEBUGMSG("Attempt to balance Huffman tree\n");
		balanceTree(tree->rootNode);

		DEBUGMSG("Calculate tree code lengths\n");
		status = recurseHuffmanNodes(tree->rootNode, 0, maxBits, codeLengths);
	}

	return (status);
}


static int makeRleCodeLens(unsigned char *comboCodeLens, int numCodeLens,
	unsigned char *rleCodeLens, unsigned char *repeatVals)
{
	// Look for runs of repeated values in the combined list of literal-length
	// and distance code counts.

	int numRleCodeLens = 0;
	int lastLoop = 0;
	int haveLastLen = 0;
	unsigned char lastLen = 0;
	int repeats = 0;
	int numRepeatVals = 0;
	int count;

	DEBUGMSG("Combo code lengths (%d): ", numCodeLens);

	for (count = 0; count < numCodeLens; count ++)
	{
		// If we're not at the end, and the current code is 0, or the same as
		// the previous one
		//     Count the repeat.
		//     If the repeated code is not zero, and the code is now repeated
		//         6 times, output the run length.  Repeats = 0
		//     Else if zeros are repeated 138 times, output the run length.
		//         Repeats = 0
		//
		// Else (the code is different)
		//     If we have been storing up repeats
		//         If the number of repeats is less than 3, just output the
		//             individual bytes.  Repeats = 0
		//         Else output the run length.  Repeats = 0
		//     Output the current byte

		DEBUGMSG("%d ", comboCodeLens[count]);

		if (count >= (numCodeLens - 1))
			lastLoop = 1;

		// If we're not at the end, is this length code zero or the same as the
		// last one?
		if (!lastLoop && haveLastLen && (comboCodeLens[count] == lastLen))
		{
			// Count the repeat
			repeats += 1;

			// Have we maxed out the run length?
			if ((lastLen && (repeats >= 6)) || (!lastLen && (repeats >= 138)))
			{
				// Output the run length.
				if (lastLen)
					rleCodeLens[numRleCodeLens++] = 16;
				else
					rleCodeLens[numRleCodeLens++] = 18;

				repeatVals[numRepeatVals++] = repeats;

				// Forget this code
				haveLastLen = 0;
				repeats = 0;
			}
		}
		else
		{
			// This code is different, or we're at the end.  Were we counting
			// up a series of repeats?
			if (repeats)
			{
				if (repeats < 3)
				{
					// Output the last length code 'repeats' times.
					while (repeats--)
						rleCodeLens[numRleCodeLens++] = lastLen;
				}
				else
				{
					// Output the run length.
					if (lastLen)
					{
						rleCodeLens[numRleCodeLens++] = 16;
					}
					else
					{
						if (repeats <= 10)
							rleCodeLens[numRleCodeLens++] = 17;
						else
							rleCodeLens[numRleCodeLens++] = 18;
					}

					repeatVals[numRepeatVals++] = repeats;
				}

				repeats = 0;
			}

			if (lastLoop || comboCodeLens[count])
				// Output the current code
				rleCodeLens[numRleCodeLens++] = comboCodeLens[count];
			else if (!comboCodeLens[count])
				repeats += 1;

			// Remember this code for the next loop.
			lastLen = comboCodeLens[count];
			haveLastLen = 1;
		}
	}

	DEBUGMSG("\n");

#ifdef DEBUG_COMPRESS
	numRepeatVals = 0; repeats = 0;
	DEBUGMSG("RLE code lengths (%d): ", numRleCodeLens);
	for (count = 0; count < numRleCodeLens; count ++)
	{
		DEBUGMSG("%d ", rleCodeLens[count]);
		if (rleCodeLens[count] < 16)
		{
			repeats += 1;
		}
		else
		{
			repeats += repeatVals[numRepeatVals];
			DEBUGMSG("(%d) ", repeatVals[numRepeatVals++]);
		}
	}
	DEBUGMSG("\n");
#endif

	return (numRleCodeLens);
}


static int compressDynamicBlock(deflateState *state)
{
	// Compress a block of data using dynamically-generated Huffman codes.

	int status = 0;
	unsigned short code = 0;
	unsigned short litLenCounts[DEFLATE_LITLEN_CODES];
	int numLitLenCodes = DEFLATE_LITERAL_CODES;
	unsigned short distCounts[DEFLATE_DIST_CODES];
	int numDistCodes = 0;
	unsigned char litLenCodeLens[DEFLATE_LITLEN_CODES];
	unsigned char distCodeLens[DEFLATE_DIST_CODES];
	unsigned char comboCodeLens[DEFLATE_LITLEN_CODES + DEFLATE_DIST_CODES];
	unsigned char rleCodeLens[DEFLATE_LITLEN_CODES + DEFLATE_DIST_CODES];
	int numRleCodeLens = 0;
	unsigned char repeatVals[DEFLATE_LITLEN_CODES + DEFLATE_DIST_CODES];
	unsigned short codeLenCounts[DEFLATE_CODELEN_CODES];
	unsigned char codeLenCodeLens[DEFLATE_CODELEN_CODES];
	unsigned char codeLenCodeOrder[DEFLATE_CODELEN_CODES] =
		{ 16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15 };
	int numCodeLenCodes = 0;
	huffmanCode *outputCode = NULL;
	int numRepeatVals = 0;
	int numExtraBits = 0;
	unsigned short extraBits = 0;
	int count;

	DEBUGMSG("Dynamic block of %d bytes\n", state->byteIn.bufferedBytes);

	// Write out the compression method
	DEBUGMSG("Compression method: ");
	writeBitField(state, 2, DEFLATE_BTYPE_DYN);
	DEBUGMSG("\n");

	// Count the codes
	memset(litLenCounts, 0, sizeof(litLenCounts));
	memset(distCounts, 0, sizeof(distCounts));
	for (count = 0; count < state->processed.numCodes; count ++)
	{
		code = state->processed.codes[count];

		if (code & 0x8000)
		{
			// This is a distance-length combo.

			// Get the length code
			calcLenExtra((code & 0x7FFF), &code, NULL /* numExtraBits*/,
				NULL /* extraBits */);

			litLenCounts[code] += 1;
			if (code >= numLitLenCodes)
				numLitLenCodes = (code + 1);

			// Get the distance code
			code = state->processed.codes[++count];
			calcDistExtra(code, &code, NULL /* numExtraBits */,
				NULL /* extraBits */);

			distCounts[code] += 1;
			if (code >= numDistCodes)
				numDistCodes = (code + 1);
		}
		else
		{
			// This is a literal byte
			litLenCounts[code] += 1;
		}
	}

	DEBUGMSG("Literal-length code counts:\n");
	for (count = 0; count < numLitLenCodes; count ++)
	{
		if (litLenCounts[count])
			DEBUGMSG("code=%d, count=%u\n", count, litLenCounts[count]);
	}

	// Calculate the literal-length code lengths
	memset(litLenCodeLens, 0, sizeof(litLenCodeLens));
	if (numLitLenCodes)
	{
		status = makeCodeLengths(&state->litLenTree, litLenCounts,
			numLitLenCodes, litLenCodeLens, 15);
		if (status < 0)
			return (status);
	}

	DEBUGMSG("Literal-length code lengths:\n");
	for (count = 0; count < numLitLenCodes; count ++)
	{
		if (litLenCodeLens[count])
			DEBUGMSG("code=%d, length=%u\n", count, litLenCodeLens[count]);
	}

	// If only one distance code is used, it is encoded using 1 bit, not 0
	// bits (a single code length of 1, with 1 unused code).  1 distance
	// code of 0 bits means that there are no distance codes - the data is
	// all literals.
	memset(distCodeLens, 0, sizeof(distCodeLens));
	if (numDistCodes)
	{
		if (numDistCodes == 1)
		{
			distCodeLens[0] = 1;
		}
		else
		{
			// Calculate the distance code lengths
			status = makeCodeLengths(&state->distTree, distCounts,
				numDistCodes, distCodeLens, 15);
			if (status < 0)
				return (status);
		}
	}
	else
	{
		numDistCodes = 1;
	}

	DEBUGMSG("Distance code lengths:\n");
	for (count = 0; count < numDistCodes; count ++)
	{
		if (distCodeLens[count])
			DEBUGMSG("code=%d, length=%u\n", count, distCodeLens[count]);
	}

	// Generate the RLE-encoded sequence of combined literal-length and
	// distance code lengths
	for (count = 0; count < numLitLenCodes; count ++)
		comboCodeLens[numRleCodeLens++] = litLenCodeLens[count];
	for (count = 0; count < numDistCodes; count ++)
		comboCodeLens[numRleCodeLens++] = distCodeLens[count];

	numRleCodeLens = makeRleCodeLens(comboCodeLens, numRleCodeLens,
		rleCodeLens, repeatVals);

	// Count the RLE-encoded literal-length and distance code lengths.
	memset(codeLenCounts, 0, sizeof(codeLenCounts));
	for (count = 0; count < numRleCodeLens; count ++)
		codeLenCounts[rleCodeLens[count]] += 1;

	DEBUGMSG("Code length counts:\n");
	for (count = 0; count < DEFLATE_CODELEN_CODES; count ++)
		if (codeLenCounts[count])
			DEBUGMSG("code length=%d, count=%u\n", count,
				codeLenCounts[count]);

	// Calculate the code length code lengths
	memset(codeLenCodeLens, 0, sizeof(codeLenCodeLens));
	status = makeCodeLengths(&state->codeLenTree, codeLenCounts,
		DEFLATE_CODELEN_CODES, codeLenCodeLens, 7);
	if (status < 0)
		return (status);

	DEBUGMSG("Code length code lengths:\n");
	for (count = 0; count < DEFLATE_CODELEN_CODES; count ++)
		if (codeLenCodeLens[count])
			DEBUGMSG("code length=%d, code length code length=%d\n", count,
				codeLenCodeLens[count]);

	// Calculate the number of code length codes
	for (count = 0; count < DEFLATE_CODELEN_CODES; count ++)
		if (codeLenCodeLens[codeLenCodeOrder[count]])
			numCodeLenCodes = (count + 1);

	DEBUGMSG("Number of code length code lengths = %d\n", numCodeLenCodes);

	// Output the number of literal-length codes - 257
	DEBUGMSG("Output number of literal-length codes (%d) - 257 = %d: ",
		numLitLenCodes, (numLitLenCodes - DEFLATE_LITERAL_CODES));
	writeBitField(state, 5, (numLitLenCodes - DEFLATE_LITERAL_CODES));
	DEBUGMSG("\n");

	// Output the number of distance codes - 1
	DEBUGMSG("Output number of distance codes (%d) - 1 = %d: ",
		numDistCodes, (numDistCodes - 1));
	writeBitField(state, 5, (numDistCodes - 1));
	DEBUGMSG("\n");

	// Output the number of code length codes - 4
	DEBUGMSG("Output number of code length codes (%d) - 4 = %d: ",
		numCodeLenCodes, (numCodeLenCodes - 4));
	writeBitField(state, 4, (numCodeLenCodes - 4));
	DEBUGMSG("\n");

	// Output the code length Huffman codes for all literal-length and
	// distance codes.
	for (count = 0; count < numCodeLenCodes; count ++)
	{
		DEBUGMSG("%d(%d):", count, codeLenCodeOrder[count]);
		writeBitField(state, 3, codeLenCodeLens[codeLenCodeOrder[count]]);
		DEBUGMSG("\n");
	}

	// Make the Huffman table for the code counts
	deflateMakeHuffmanTable(&state->codeLenTable, DEFLATE_CODELEN_CODES,
		codeLenCodeLens);

	// Output the RLE-and-Huffman-coded code counts
	DEBUGMSG("Output code length codes:\n");
	for (count = 0; count < numRleCodeLens; count ++)
	{
		outputCode = &state->codeLenTable.codes[rleCodeLens[count]];

		// Output the code
		DEBUGMSG("%d:", count);
		writeBits(state, outputCode->len, outputCode->code);

		// If the code length value was >= 16, then it describes a run length,
		// and we need to output the length bits that follow
		if (rleCodeLens[count] >= 16)
		{
			DEBUGMSG("len: ");
			if (rleCodeLens[count] ==  16)
				writeBitField(state, 2, (repeatVals[numRepeatVals++] - 3));
			else if (rleCodeLens[count] ==  17)
				writeBitField(state, 3, (repeatVals[numRepeatVals++] - 3));
			else if (rleCodeLens[count] ==  18)
				writeBitField(state, 7, (repeatVals[numRepeatVals++] - 11));
		}

		DEBUGMSG("\n");
	}

	// Make the Huffman table for the literal-length codes
	deflateMakeHuffmanTable(&state->litLenTable, numLitLenCodes,
		litLenCodeLens);

	// Make the Huffman table for the distance codes
	deflateMakeHuffmanTable(&state->distTable, numDistCodes, distCodeLens);

	// Write the compressed data
	for (count = 0; count < state->processed.numCodes; count ++)
	{
		code = state->processed.codes[count];

		if (code & 0x8000)
		{
			// This is a distance-length combo.  Figure out which distance/
			// length codes and extra bits to output.
			calcLenExtra((code & 0x7FFF), &code, &numExtraBits, &extraBits);

			outputCode = &state->litLenTable.codes[code];

			DEBUGMSG("Write length code %d %dx%04x ", code, outputCode->len,
				outputCode->code);
			writeBits(state, outputCode->len, outputCode->code);

			if (numExtraBits)
			{
				DEBUGMSG("extra bits ");
				writeBitField(state, numExtraBits, extraBits);
			}

			DEBUGMSG("\n");

			code = state->processed.codes[++count];

			calcDistExtra(code, &code, &numExtraBits, &extraBits);

			outputCode = &state->distTable.codes[code];

			DEBUGMSG("Write distance code %d %dx%02x ", code, outputCode->len,
				outputCode->code);
			writeBits(state, outputCode->len, outputCode->code);

			if (numExtraBits)
			{
				DEBUGMSG("extra bits ");
				writeBitField(state, numExtraBits, extraBits);
			}

			DEBUGMSG("\n");
		}
		else
		{
			// This is a literal byte

			outputCode = &state->litLenTable.codes[code];

			DEBUGMSG("Write literal code %d %dx%04x ", code, outputCode->len,
				outputCode->code);
			writeBits(state, outputCode->len, outputCode->code);
		}

		DEBUGMSG("\n");
	}

	return (status = 0);
}


static void restart(deflateState *state, int startBit)
{
	// Reset the input and output buffers, so that we're back to the
	// start of the current block.

	DEBUGMSG("Restart the block\n");

	state->bitOut.data[0] &= (0xFF >> (8 - startBit));
	memset((state->bitOut.data + 1), 0, state->bitOut.byte);
	state->byteIn.byte = 0;
	state->bitOut.byte = 0;
	state->bitOut.bit = startBit;

	// Write out the 'final' flag
	DEBUGMSG("Final flag: ");
	writeBitField(state, 1, state->final);
	DEBUGMSG("\n");
}


/////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////
//
// Below here, the functions are exported for external use
//
/////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////

int deflateCompress(deflateState *state)
{
	int status = 0;
	int startBit = 0;

	// Check params
	if (!state || !state->inBuffer || !state->outBuffer)
	{
		DEBUGMSG("NULL parameter\n");
		status = ERR_NULLPARAMETER;
		goto out;
	}

	// Set up our input and output buffers
	state->byteIn.data = (state->inBuffer + state->inByte);
	state->bitOut.data = (state->outBuffer + state->outByte);

	if (!state->inBytes)
	{
		// Empty file.  Do it anyway.
		writeBitField(state, 1, (state->final = 1));
		copyUncompressedOutputBlock(state);
		state->outBytes -= state->bitOut.byte;
		state->outByte += state->bitOut.byte;
		if (state->bitOut.bit)
		{
			skipOutputBits(state);
			state->outBytes -= 1;
			state->outByte += 1;
		}
	}

	while (state->inBytes)
	{
		state->byteIn.bufferedBytes = min(state->inBytes,
			DEFLATE_MAX_INBUFFERSIZE);
		state->byteIn.byte = 0;
		state->bitOut.byte = 0;
		startBit = state->bitOut.bit;

		// Calculate the CRC32 of the uncompressed data
		state->crc32Sum = crc32((void *) state->byteIn.data,
			state->byteIn.bufferedBytes, &state->crc32Sum);

		// If the buffer isn't full, this is the final block
		if (state->byteIn.bufferedBytes < DEFLATE_MAX_INBUFFERSIZE)
			state->final = 1;

		// Write out the 'final' flag
		DEBUGMSG("Final flag: ");
		writeBitField(state, 1, state->final);
		DEBUGMSG("\n");

		// Process the input data
		status = processInput(state);
		if (status < 0)
			goto out;

		// First try compressing using dynamic Huffman codes
		status = compressDynamicBlock(state);
		if (status < 0)
			goto out;

		// Did we inadvertently expand the data?
		if (state->bitOut.byte >= state->byteIn.bufferedBytes)
		{
			DEBUGMSG("Dyamic compression expanded the data\n");

			// Restart
			restart(state, startBit);

			if (!state->staticTable.numCodes)
				// Make the table of static Huffman codes
				makeStaticHuffmanLitLenTable(state);

			// Compress using static Huffman codes
			status = compressStaticBlock(state);
			if (status < 0)
				goto out;

			// Did we still expand the data?
			if (state->bitOut.byte >= state->byteIn.bufferedBytes)
			{
				DEBUGMSG("Static compression expanded the data\n");

				// Restart
				restart(state, startBit);

				// Copy data without compression
				status = copyUncompressedOutputBlock(state);
				if (status < 0)
					goto out;
			}
		}

		state->inBytes -= state->byteIn.bufferedBytes;
		state->inByte += state->byteIn.bufferedBytes;
		state->outBytes -= state->bitOut.byte;
		state->outByte += state->bitOut.byte;

		if (state->final)
		{
			// Discard any remaining bits of the current output byte
			if (state->bitOut.bit)
			{
				skipOutputBits(state);
				state->outBytes -= 1;
				state->outByte += 1;
			}

			break;
		}

		state->byteIn.data += state->byteIn.bufferedBytes;
		state->bitOut.data += state->bitOut.byte;
	}

	// Return success
	status = 0;

out:
	if (status < 0)
		errno = status;

	return (status);
}

