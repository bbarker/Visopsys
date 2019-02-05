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
//  deflate_decompress.c
//

// This is the decompression half of the library code for the DEFLATE
// algorithm.

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
	int debugDeflateDecompress = 0;
	static char debugOutput[DEBUG_OUTMAX];

	static void DEBUGMSG(const char *message, ...)
	{
		va_list list;

		if (debugDeflateDecompress)
		{
			va_start(list, message);
			vsnprintf(debugOutput, DEBUG_OUTMAX, message, list);
			printf("%s", debugOutput);
		}
	}
#else
	#define DEBUGMSG(message, arg...) do { } while (0)
#endif


static void readBitField(deflateState *state, int bits, unsigned short *data)
{
	// Read bitfield bits, in LSB order.

	int returned = 0;
	int count;

	*data = 0;

	while (bits)
	{
		if (state->bitIn.data[state->bitIn.byte] & (1 << state->bitIn.bit))
			*data |= (1 << returned);

		bits -=1; returned +=1; state->bitIn.bit += 1;

		if (state->bitIn.bit >= 8)
		{
			state->bitIn.byte += 1;
			state->bitIn.bit = 0;
		}
	}

	DEBUGMSG("[");
	for (count = (returned - 1); count >= 0; count --)
	{
		if (*data & (1 << count))
			DEBUGMSG("1");
		else
			DEBUGMSG("0");
	}
	DEBUGMSG("] ");
}


static void readBits(deflateState *state, int bits, unsigned short *data)
{
	// Get Huffman code bits, in MSB order.  This can be called repeatedly with
	// the same value in 'data' to add more bits to what's already there.

	DEBUGMSG("%dx[", bits);
	while (bits)
	{
		*data <<= 1;

		if (state->bitIn.data[state->bitIn.byte] & (1 << state->bitIn.bit))
		{
			DEBUGMSG("1");
			*data |= 1;
		}
		else
		{
			DEBUGMSG("0");
		}

		bits -=1; state->bitIn.bit += 1;

		if (state->bitIn.bit >= 8)
		{
			state->bitIn.byte += 1;
			state->bitIn.bit = 0;
		}
	}

	DEBUGMSG("] ");
}


static void skipInputBits(deflateState *state)
{
	// Skip the remaining bits of the byte.

	if (state->bitIn.bit)
	{
		DEBUGMSG("Skip %d bits\n", (8 - state->bitIn.bit));
		state->bitIn.byte += 1;
		state->bitIn.bit = 0;
	}
}


static int copyUncompressedInputBlock(deflateState *state)
{
	// Given an uncompressed block, copy the data from the input stream to the
	// output stream.

	int status = 0;
	unsigned short length = 0;
	unsigned short nLength = 0;

	// Discard the remaining bits of this byte
	if (state->bitIn.bit)
		skipInputBits(state);

	// Get the length value and its complementary value
	readBitField(state, 16, &length);
	readBitField(state, 16, &nLength);

	DEBUGMSG("\nUncompressed block of %d bytes\n", length);

	if (length != (unsigned short) ~nLength)
	{
		DEBUGMSG("length (%04x) != ~nLength (%04x)\n", length,
			(unsigned short) ~nLength);
		return (status = ERR_BADDATA);
	}

	// Output the data
	memcpy((state->byteOut.data + state->byteOut.byte), (state->bitIn.data +
		state->bitIn.byte), length);
	state->bitIn.byte += length;
	state->byteOut.byte += length;

	return (status = 0);
}


static int getLength(deflateState *state, unsigned short code,
	unsigned short *length)
{
	// Given a code representing a literal-length alphabet code, read any
	// applicable extra bits from the stream, and return the length value.

	int status = 0;
	int extraBits = 0;
	unsigned short extraData = 0;

	// Get the 'length' value and/or the number of extra bits, if
	// applicable
	extraBits = 0;
	if ((code >= 257) && (code <= 264))
		*length = (code - 254);
	else if ((code >= 265) && (code <= 284))
		extraBits = (1 + ((code - 265) / 4));
	else
		*length = 258;

	if (extraBits)
	{
		// Get the extra length bits
		readBitField(state, extraBits, &extraData);

		*length = (((((code - 1) & 3) + 4) << extraBits) + 3 + extraData);
	}

	DEBUGMSG("Repeat of length=%d\n", *length);

	return (status = 0);
}


static int getDistance(deflateState *state, unsigned short code,
	unsigned short *distance)
{
	// Given a code representing a distance alphabet code, read any
	// applicable extra bits from the stream, and return the distance value.

	int status = 0;
	int extraBits = 0;
	unsigned short extraData = 0;

	// Do we need to read any extra bits?
	if (code <= 3)
		*distance = (code + 1);
	else
	{
		// Get the extra distance bits
		extraBits = (1 + ((code - 4) / 2));

		readBitField(state, extraBits, &extraData);

		*distance = (((2 << extraBits) + 1) +
			(((code - 4) % 2) * (1 << extraBits)) + extraData);
	}

	DEBUGMSG("Repeat at distance=%d\n", *distance);

	return (status = 0);
}


static int repeatBytes(deflateState *state, unsigned short length,
	unsigned short distance)
{
	int status = 0;
	unsigned short doBytes = 0;

	if ((distance <= 0) || (distance > (state->outByte + state->byteOut.byte)))
	{
		DEBUGMSG("Distance value %d is out of range (%d in buffer)\n",
			distance, (state->outByte + state->byteOut.byte));
		return (status = ERR_RANGE);
	}

	while (length)
	{
		// Copy the data from our buffer

		doBytes = min(length, distance);

		memcpy((state->byteOut.data + state->byteOut.byte),
			(state->byteOut.data + (state->byteOut.byte - distance)), doBytes);

		state->byteOut.byte += doBytes;
		length -= doBytes;
	}

	return (status = 0);
}


static int decompressStaticBlock(deflateState *state)
{
	// Decompress a block of data compressed with the 'deflate' algorithm and
	// static Huffman codes.

	int status = 0;
	unsigned short data = 0;
	unsigned short code = 0;
	unsigned short length = 0;
	unsigned short distance = 0;

	// Loop for one compressed data block
	while (1)
	{
		// New piece of data
		data = 0;

		readBits(state, 7, &data);

		DEBUGMSG("data=%d ", data);

		// Does this fall into our 7-bit range of values?
		if (data <= 0x17)
		{
			// This is a length-distance pair, or end-of-block.
			code = (data + DEFLATE_CODE_EOB); // Codes 256-279

			DEBUGMSG("7-bit 0x%02x code=%d\n", data, code);

			if (code == DEFLATE_CODE_EOB)
				// End of block
				break;

			// Decode the length value
			status = getLength(state, code, &length);
			if (status < 0)
				return (status);

			// Read the 'distance' value
			code = 0;
			readBits(state, 5, &code);

			// Decode the distance value
			status = getDistance(state, code, &distance);
			if (status < 0)
				return (status);

			// Repeat the data
			status = repeatBytes(state, length, distance);
			if (status < 0)
				return (status);
		}
		else
		{
			// Get one more bit
			readBits(state, 1, &data);

			DEBUGMSG("data=%d ", data);

			// Does this fall into either of our 8-bit ranges of codes?
			if ((data >= 0x30) && (data <= 0xBF))
			{
				// This is a literal data code
				code = (data - 0x30); // Codes 0-143

				if ((code >= 0x20) && (code <= 0x7E))
					DEBUGMSG("8-bit 0x%02x code=%d (%c)\n", data, code,
						code);
				else
					DEBUGMSG("8-bit 0x%02x code=%d\n", data, code);

				// Write it to the output
				state->byteOut.data[state->byteOut.byte++] = code;
			}

			else if ((data >= 0xC0) && (data <= 0xC7))
			{
				// This is a length-distance pair
				code = (data += 0x58); // Codes 280-287

				DEBUGMSG("8-bit 0x%02x code=%d\n", data, code);

				// Decode the length value
				status = getLength(state, code, &length);
				if (status < 0)
					return (status);

				// Read the 'distance' value
				code = 0;
				readBits(state, 5, &code);

				// Decode the distance value
				status = getDistance(state, code, &distance);
				if (status < 0)
					return (status);

				// Repeat the data
				status = repeatBytes(state, length, distance);
				if (status < 0)
					return (status);
			}

			else
			{
				// Get one more bit
				readBits(state, 1, &data);

				DEBUGMSG("data=%d ", data);

				// Does this fall into our 9-bit range of codes?
				if ((data >= 0x190) && (data <= 0x1FF))
				{
					// This is a literal data code
					code = (data - DEFLATE_CODE_EOB); // Codes 144-255

					DEBUGMSG("9-bit 0x%03x code=%d\n", data, code);

					// Write it to the output
					state->byteOut.data[state->byteOut.byte++] = code;
				}
				else
				{
					DEBUGMSG("Invalid data code %03x\n", data);
					return (status = ERR_BADDATA);
				}
			}
		}
	}

	return (status = 0);
}


static int readHuffmanCode(deflateState *state, huffmanTable *table,
	unsigned short *code)
{
	// Try to match a Huffman code from the stream, and return the value

	int status = 0;
	unsigned short bits = 0;
	unsigned short data = 0;

	readBits(state, table->leastBits, &data);

	for (bits = table->leastBits; bits <= table->mostBits; bits ++)
	{
		if ((data >= table->len[bits].startCode) &&
			(data < (table->len[bits].startCode + table->len[bits].numCodes)))
		{
			*code = table->ordered[table->len[bits].first +
				(data - table->len[bits].startCode)]->num;
			DEBUGMSG("found code %d (%02x)\n", *code, *code);
			return (status = 0);
		}

		readBits(state, 1, &data);
	}

	// No match
	DEBUGMSG("Code not recognized\n");
	return (status = ERR_NOSUCHENTRY);
}


static int readCodeLengths(deflateState *state, int numCodes,
	unsigned char *codeLens)
{
	int status = 0;
	unsigned short data = 0;
	unsigned short length = 0;
	int count1, count2;

	for (count1 = 0; count1 < numCodes; )
	{
		DEBUGMSG("Read code %d of %d\n", (count1 + 1), numCodes);

		status = readHuffmanCode(state, &state->codeLenTable, &data);
		if (status < 0)
			goto out;

		if (data < 16)
		{
			codeLens[count1++] = data;
		}

		else if (data == 16)
		{
			// Copy the previous code length 3-6 times.  The next 2 bits
			// indicate repeat length

			length = 0;
			readBitField(state, 2, &length);

			length += 3;
			data = codeLens[count1 - 1];

			DEBUGMSG("Repeat previous value %d times %d-%d=%d\n",
				length, count1, (count1 + length - 1), data);

			for (count2 = 0; count2 < length; count2 ++)
				codeLens[count1++] = data;
		}

		else if (data == 17)
		{
			// Repeat a code length of 0 for 3-10 times.  The next 3 bits
			// indicate repeat length

			length = 0;
			readBitField(state, 3, &length);

			length += 3;
			DEBUGMSG("Repeat 0 %d times %d-%d=0\n", length, count1,
				(count1 + length - 1));

			for (count2 = 0; count2 < length; count2 ++)
				codeLens[count1++] = 0;
		}

		else if (data == 18)
		{
			// Repeat a code length of 0 for 11-138 times.  The next 7 bits
			// indicate repeat length

			length = 0;
			readBitField(state, 7, &length);

			length += 11;
			DEBUGMSG("Repeat 0 %d times %d-%d=0\n", length, count1,
				(count1 + length - 1));

			for (count2 = 0; count2 < length; count2 ++)
				codeLens[count1++] = 0;
		}
	}

	status = 0;

out:
	return (status);
}


static int decompressDynamicBlock(deflateState *state)
{
	// Decompress a block of data compressed with the 'deflate' algorithm and
	// dynamic Huffman codes.

	int status = 0;
	unsigned short data = 0;
	unsigned short numLitLenCodes = 0;
	unsigned short numDistCodes = 0;
	unsigned short numCodeLenCodes = 0;
	unsigned char codeLenCodeOrder[DEFLATE_CODELEN_CODES] =
		{ 16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15 };
	unsigned char codeLenCodeLens[DEFLATE_CODELEN_CODES];
	unsigned char comboCodeLens[DEFLATE_LITLEN_CODES + DEFLATE_DIST_CODES];
	unsigned char *litLenCodeLens = NULL;
	unsigned char *distCodeLens = NULL;
	unsigned short length = 0, distance = 0;
	int count;

	memset(codeLenCodeLens, 0, sizeof(codeLenCodeLens));
	memset(comboCodeLens, 0, sizeof(comboCodeLens));

	// Get the number of literal-length alphabet codes, the number of distance
	// codes, and the number of code length codes
	readBitField(state, 5, &numLitLenCodes);
	readBitField(state, 5, &numDistCodes);
	readBitField(state, 4, &numCodeLenCodes);

	// Adjust the numbers according to the spec
	numLitLenCodes += DEFLATE_LITERAL_CODES;
	numDistCodes += 1;
	numCodeLenCodes += 4;

	DEBUGMSG("\nnumLitLenCodes=%d, numDistCodes=%d, numCodeLenCodes=%d\n",
		numLitLenCodes, numDistCodes, numCodeLenCodes);

	// Get the code lengths for the code length codes (does that make
	// sense? ;-)
	for (count = 0; count < numCodeLenCodes; count ++)
	{
		data = 0;
		readBitField(state, 3, &data);

		codeLenCodeLens[codeLenCodeOrder[count]] = data;
	}

	DEBUGMSG("\nCode lengths:\n");
	for (count = 0; count < DEFLATE_CODELEN_CODES; count ++)
		DEBUGMSG("%d=%d ", codeLenCodeOrder[count],
			codeLenCodeLens[codeLenCodeOrder[count]]);

	DEBUGMSG("\nReordered:\n");
	for (count = 0; count < DEFLATE_CODELEN_CODES; count ++)
		DEBUGMSG("%d=%d ", count, codeLenCodeLens[count]);

	DEBUGMSG("\n");

	// Make the Huffman table for the code lengths
	deflateMakeHuffmanTable(&state->codeLenTable, DEFLATE_CODELEN_CODES,
		codeLenCodeLens);

	// Now we construct the list of code lengths for the literal-length and
	// distance codes, in a single go
	status = readCodeLengths(state, (numLitLenCodes + numDistCodes),
		comboCodeLens);
	if (status < 0)
		return (status);

	litLenCodeLens = comboCodeLens;

	// Make the Huffman table for the literal-length alphabet
	deflateMakeHuffmanTable(&state->litLenTable, numLitLenCodes,
		litLenCodeLens);

	distCodeLens = ((void *) comboCodeLens + numLitLenCodes);

	// Make the Huffman table for the distance alphabet
	deflateMakeHuffmanTable(&state->distTable, numDistCodes, distCodeLens);

	// Decompress the data
	DEBUGMSG("Decompress data\n");

	// Loop for one compressed data block
	while (1)
	{
		// Read a literal-length code
		status = readHuffmanCode(state, &state->litLenTable, &data);
		if (status < 0)
			return (status);

		// Is it a literal or a length?
		if (data < DEFLATE_CODE_EOB)
		{
			// This is a literal value; write it to the output
			state->byteOut.data[state->byteOut.byte++] = data;
		}

		else if (data == DEFLATE_CODE_EOB)
		{
			// End of block
			break;
		}

		else
		{
			// Get the length value
			status = getLength(state, data, &length);
			if (status < 0)
				return (status);

			// Read the distance code
			status = readHuffmanCode(state, &state->distTable, &data);
			if (status < 0)
				return (status);

			// Get the distance value
			status = getDistance(state, data, &distance);
			if (status < 0)
				return (status);

			// Repeat the data
			status = repeatBytes(state, length, distance);
			if (status < 0)
				return (status);
		}
	}

	return (status = 0);
}


/////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////
//
// Below here, the functions are exported for external use
//
/////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////

int deflateDecompress(deflateState *state)
{
	int status = 0;
	unsigned short method = 0;

	// Check params
	if (!state || !state->inBuffer || !state->outBuffer)
	{
		DEBUGMSG("NULL parameter\n");
		status = ERR_NULLPARAMETER;
		goto out;
	}

	// Set up our input and output buffers
	state->bitIn.data = (state->inBuffer + state->inByte);
	state->byteOut.data = (state->outBuffer + state->outByte);

	while (!state->final && state->inBytes &&
		(state->outBytes >= DEFLATE_MAX_OUTBUFFERSIZE))
	{
		state->bitIn.byte = 0;
		state->byteOut.byte = 0;

		// Find out whether this is the final block, by reading the first bit
		DEBUGMSG("Final flag: ");
		readBitField(state, 1, &state->final);
		DEBUGMSG("\n");

		// Read the 2-bit compression method of the block
		DEBUGMSG("Compression method: ");
		readBitField(state, 2, &method);
		DEBUGMSG("\n");

		if (method == DEFLATE_BTYPE_NONE)
		{
			DEBUGMSG("No compression\n");
			status = copyUncompressedInputBlock(state);
			if (status < 0)
				goto out;
		}

		else if (method == DEFLATE_BTYPE_FIXED)
		{
			DEBUGMSG("Static Huffman codes\n");
			status = decompressStaticBlock(state);
			if (status < 0)
				goto out;
		}

		else if (method == DEFLATE_BTYPE_DYN)
		{
			DEBUGMSG("Dynamic Huffman codes\n");
			status = decompressDynamicBlock(state);
			if (status < 0)
				goto out;
		}

		else
		{
			DEBUGMSG("Unsupported compression method %x\n", method);
			status = ERR_NOTIMPLEMENTED;
			goto out;
		}

		// Calculate the CRC32 of the decompressed data
		state->crc32Sum = crc32(state->byteOut.data, state->byteOut.byte,
			&state->crc32Sum);

		state->inBytes -= state->bitIn.byte;
		state->inByte += state->bitIn.byte;
		state->outBytes -= state->byteOut.byte;
		state->outByte += state->byteOut.byte;

		if (state->final)
		{
			// Discard any remaining bits of the current input byte
			if (state->bitIn.bit)
			{
				skipInputBits(state);
				state->inBytes -= 1;
				state->inByte += 1;
			}

			break;
		}

		state->bitIn.data += state->bitIn.byte;
		state->byteOut.data += state->byteOut.byte;
	}

	// Return success
	status = 0;

out:
	if (status < 0)
		errno = status;

	return (status);
}

