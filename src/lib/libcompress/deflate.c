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
//  deflate.c
//

// This is common library code for the DEFLATE algorithm.

#include "libcompress.h"
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/deflate.h>
#include <sys/api.h>

#ifdef DEBUG
	#define DEBUG_OUTMAX	160
	int debugDeflate = 0;
	static char debugOutput[DEBUG_OUTMAX];

	static void DEBUGMSG(const char *message, ...)
	{
		va_list list;

		if (debugDeflate)
		{
			va_start(list, message);
			vsnprintf(debugOutput, DEBUG_OUTMAX, message, list);
			printf("%s", debugOutput);
		}
	}
#else
	#define DEBUGMSG(message, arg...) do { } while (0)
#endif


static void makeHuffmanCodes(unsigned char *codeLens, huffmanTable *table)
{
	// Derived from section 3.2.2 of RFC 1951.  Given a series of code lengths
	// for elements of an alphabet, generate the codes.

	unsigned short lenCounts[DEFLATE_CODELEN_CODES];
	unsigned short lenCodes[DEFLATE_CODELEN_CODES];
	unsigned short code = 0;
	huffmanCode *ptr = NULL;
	int bits, count1, count2;

	memset(lenCounts, 0, (DEFLATE_CODELEN_CODES * sizeof(unsigned short)));
	memset(lenCodes, 0, (DEFLATE_CODELEN_CODES * sizeof(unsigned short)));

	// Count the number of codes with each length (1-18)
	for (bits = 1; bits <= 18; bits ++)
	{
		for (count1 = 0; count1 < table->numCodes; count1 ++)
		{
			if (codeLens[count1] == bits)
			{
				lenCounts[bits] += 1;

				if (!table->leastBits || (bits < table->leastBits))
					table->leastBits = bits;
				if (bits > table->mostBits)
					table->mostBits = bits;
			}
		}

		table->len[bits].numCodes = lenCounts[bits];
	}

	DEBUGMSG("Code length counts (numCodes=%d, leastBits=%d, mostBits=%d):\n",
		table->numCodes, table->leastBits, table->mostBits);
	for (bits = table->leastBits; bits <= table->mostBits; bits ++)
	{
		table->len[bits].first = (table->len[bits - 1].first +
			lenCounts[bits - 1]);
		DEBUGMSG("%d codes of length %d first=%d\n", lenCounts[bits], bits,
			table->len[bits].first);
	}

	DEBUGMSG("Starting codes:\n");
	// Calculate the starting code value for each length
	for (bits = table->leastBits; bits <= table->mostBits; bits ++)
	{
		code = ((code + lenCounts[bits - 1]) << 1);
		table->len[bits].startCode = lenCodes[bits] = code;

		if (lenCounts[bits])
		{
			DEBUGMSG("%d: ", bits);
			for (count1 = bits; count1 >= 1; count1 --)
				DEBUGMSG("%d", ((lenCodes[bits] & (1 << (count1 - 1)))?
					1 : 0));
			DEBUGMSG("\n");
		}
	}

	DEBUGMSG("Actual codes:\n");
	// Now create the actual codes
	for (count1 = 0; count1 < table->numCodes; count1 ++)
	{
		if (codeLens[count1])
		{
			table->codes[count1].num = count1;
			table->codes[count1].len = codeLens[count1];
			table->codes[count1].code = lenCodes[codeLens[count1]]++;
			table->ordered[table->len[codeLens[count1]].first +
				(table->codes[count1].code -
					table->len[codeLens[count1]].startCode)] =
						&table->codes[count1];

			DEBUGMSG("%d (len %d): ", count1, table->codes[count1].len);
			for (count2 = codeLens[count1]; count2 >= 1; count2 --)
				DEBUGMSG("%d", ((table->codes[count1].code &
					(1 << (count2 - 1)))? 1 : 0));
			DEBUGMSG("\n");
		}
	}

	DEBUGMSG("Ordered:\n");
	for (bits = table->leastBits; bits <= table->mostBits; bits ++)
	{
		for (count1 = 0; count1 < table->len[bits].numCodes; count1 ++)
		{
			ptr = table->ordered[table->len[bits].first + count1];

			DEBUGMSG("len %d: ", ptr->len);
			for (count2 = ptr->len; count2 >= 1; count2 --)
				DEBUGMSG("%d", ((ptr->code & (1 << (count2 - 1)))? 1 : 0));
			DEBUGMSG("\n");
		}
	}

	return;
}


/////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////
//
// Below here, the functions are exported for external use
//
/////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////

int deflateCompressFileData(deflateState *deflate, FILE *inStream,
	FILE *outStream, progress *prog)
{
	int status = 0;
	unsigned totalBytes = inStream->f.size;
	unsigned maxInBytes = 0;
	unsigned maxOutBytes = 0;
	unsigned doneBytes = 0;

	maxInBytes = min(totalBytes, COMPRESS_MAX_BUFFERSIZE);
	maxInBytes = max(maxInBytes, 1); // Makes dealing with empty files easier

	// Worst case scenario, DEFLATE expands to 5 extra bytes per 32K block,
	// but give it a bit of extra working space in any case.
	maxOutBytes = (maxInBytes +
		max((((maxInBytes + (DEFLATE_MAX_INBUFFERSIZE - 1)) /
			DEFLATE_MAX_INBUFFERSIZE) * 5),
		(maxInBytes / 10)));
	maxOutBytes = max(maxOutBytes, 5);

	memset(deflate, 0, sizeof(deflateState));
	deflate->inBuffer = calloc(maxInBytes, 1);
	deflate->outBuffer = calloc(maxOutBytes, 1);

	if (!deflate->inBuffer || !deflate->outBuffer)
	{
		fprintf(stderr, "Memory error\n");
		return (status = ERR_MEMORY);
	}

	if (prog)
	{
		memset((void *) prog, 0, sizeof(progress));
		prog->numTotal = totalBytes;
	}

	do
	{
		maxInBytes = min((totalBytes - doneBytes), maxInBytes);

		if (doneBytes < totalBytes)
		{
			DEBUGMSG("Reading %u bytes\n", maxInBytes);
			if (prog && (lockGet(&prog->progLock) >= 0))
			{
				sprintf((char *) prog->statusMessage, "Reading %u bytes",
					maxInBytes);
				lockRelease(&prog->progLock);
			}

			if (fread((void *)(deflate->inBuffer + deflate->inByte), 1,
				maxInBytes, inStream) < maxInBytes)
			{
				fprintf(stderr, "Error reading %s\n", inStream->f.name);
				status = ERR_IO;
				break;
			}
		}

		deflate->inBytes = maxInBytes;
		deflate->outBytes = maxOutBytes;
		deflate->outByte = 0;

		DEBUGMSG("Compressing %u bytes\n", deflate->inBytes);
		if (prog && (lockGet(&prog->progLock) >= 0))
		{
			sprintf((char *) prog->statusMessage, "Compressing %u bytes",
				deflate->inBytes);
			lockRelease(&prog->progLock);
		}

		status = deflateCompress(deflate);
		if (status < 0)
		{
			fprintf(stderr, "Error compressing %s\n", inStream->f.name);
			break;
		}

		DEBUGMSG("Writing %u bytes\n", deflate->outByte);
		if (prog && (lockGet(&prog->progLock) >= 0))
		{
			sprintf((char *) prog->statusMessage, "Writing %u bytes",
				deflate->outByte);
			lockRelease(&prog->progLock);
		}

		if (fwrite((void *) deflate->outBuffer, 1, deflate->outByte,
			outStream) < deflate->outByte)
		{
			fprintf(stderr, "Error writing %s\n", outStream->f.name);
			status = ERR_IO;
			break;
		}

		doneBytes += maxInBytes;

		if (prog && (lockGet(&prog->progLock) >= 0))
		{
			prog->numFinished = doneBytes;
			if (totalBytes)
				prog->percentFinished = ((doneBytes * 100) / totalBytes);
			else
				prog->percentFinished = 100;
			lockRelease(&prog->progLock);
		}

		if (!deflate->final)
		{
			// This is not mandatory for the DEFLATE compression code, but for
			// maximum compression, we should keep the last
			// DEFLATE_MAX_DISTANCE (32K) bytes at the top of the input buffer
			// for more matches.
			memmove((void *) deflate->inBuffer,
				(deflate->inBuffer + (deflate->inByte - DEFLATE_MAX_DISTANCE)),
				DEFLATE_MAX_DISTANCE);
			deflate->inByte = DEFLATE_MAX_DISTANCE;
			maxInBytes -= DEFLATE_MAX_DISTANCE;

			// If the previous round produced an incomplete output byte,
			// preserve it for the next round in byte 0, and clear the rest.
			// Otherwise, just clear.
			if (deflate->bitOut.bit)
			{
				deflate->outBuffer[0] = deflate->outBuffer[deflate->outByte];
				memset((deflate->outBuffer + 1), 0, deflate->outByte);
			}
			else
			{
				memset(deflate->outBuffer, 0, deflate->outByte);
			}
		}

	} while (!deflate->final);

	if (deflate->outBuffer)
		free(deflate->outBuffer);

	if (deflate->inBuffer)
		free((void *) deflate->inBuffer);

	return (status);
}


int deflateDecompressFileData(deflateState *deflate, FILE *inStream,
	FILE *outStream, progress *prog)
{
	int status = 0;
	unsigned totalBytes = inStream->f.size;
	unsigned maxInBytes = 0;
	unsigned maxOutBytes = 0;
	unsigned doneBytes = 0;
	unsigned skipOutBytes = 0;

	maxInBytes = min(totalBytes, COMPRESS_MAX_BUFFERSIZE);
	maxOutBytes = COMPRESS_MAX_BUFFERSIZE;

	memset(deflate, 0, sizeof(deflateState));
	deflate->inBuffer = calloc(maxInBytes, 1);
	deflate->outBuffer = calloc(maxOutBytes, 1);

	if (!deflate->inBuffer || !deflate->outBuffer)
	{
		fprintf(stderr, "Memory error\n");
		return (status = ERR_MEMORY);
	}

	if (prog)
	{
		memset((void *) prog, 0, sizeof(progress));
		prog->numTotal = totalBytes;
	}

	while (doneBytes < totalBytes)
	{
		maxInBytes = (min((totalBytes - doneBytes), COMPRESS_MAX_BUFFERSIZE) -
			deflate->inBytes);

		DEBUGMSG("Reading %u bytes\n", maxInBytes);
		if (prog && (lockGet(&prog->progLock) >= 0))
		{
			sprintf((char *) prog->statusMessage, "Reading %u bytes",
				maxInBytes);
			lockRelease(&prog->progLock);
		}

		maxInBytes = (fread((void *)(deflate->inBuffer + deflate->inBytes), 1,
			maxInBytes, inStream) + deflate->inBytes);

		if (!maxInBytes)
		{
			fprintf(stderr, "Error reading %s\n", inStream->f.name);
			status = ERR_IO;
			break;
		}

		deflate->inBytes = maxInBytes;
		deflate->inByte = 0;
		deflate->outBytes = maxOutBytes;
		deflate->outByte = skipOutBytes;

		DEBUGMSG("Decompressing %u bytes\n", deflate->inBytes);
		if (prog && (lockGet(&prog->progLock) >= 0))
		{
			sprintf((char *) prog->statusMessage, "Decompressing %u bytes",
				deflate->inBytes);
			lockRelease(&prog->progLock);
		}

		status = deflateDecompress(deflate);
		if (status < 0)
		{
			fprintf(stderr, "Error decompressing %s\n", inStream->f.name);
			break;
		}

		if (outStream)
		{
			DEBUGMSG("Writing %u bytes\n", (deflate->outByte - skipOutBytes));
			if (prog && (lockGet(&prog->progLock) >= 0))
			{
				sprintf((char *) prog->statusMessage, "Writing %u bytes",
					(deflate->outByte - skipOutBytes));
				lockRelease(&prog->progLock);
			}

			if (fwrite((void *)(deflate->outBuffer + skipOutBytes), 1,
				(deflate->outByte - skipOutBytes), outStream) <
				(deflate->outByte - skipOutBytes))
			{
				fprintf(stderr, "Error writing %s\n", outStream->f.name);
				status = ERR_IO;
				break;
			}
		}

		doneBytes += (maxInBytes - deflate->inBytes);

		if (prog && (lockGet(&prog->progLock) >= 0))
		{
			prog->numFinished = doneBytes;
			prog->percentFinished = ((doneBytes * 100) / totalBytes);
			lockRelease(&prog->progLock);
		}

		if (deflate->final)
			break;

		// If there are unprocessed bytes remaining in the input buffer, we
		// need to copy them to the top before we start the next loop.
		if (deflate->inBytes)
		{
			memcpy((void *) deflate->inBuffer, (deflate->inBuffer +
				deflate->inByte), deflate->inBytes);
		}

		// We must keep the last DEFLATE_MAX_DISTANCE (32K) bytes at the top
		// of the output buffer.
		memmove(deflate->outBuffer,
			(deflate->outBuffer + (deflate->outByte - DEFLATE_MAX_DISTANCE)),
			DEFLATE_MAX_DISTANCE);

		if (!skipOutBytes)
		{
			skipOutBytes = DEFLATE_MAX_DISTANCE;
			maxOutBytes -= skipOutBytes;
		}

		// Clear the rest of the output buffer
		memset((deflate->outBuffer + skipOutBytes), 0,
			(deflate->outByte - skipOutBytes));
	}

	// Seek backwards to the start of any un-processed input bytes
	if (deflate->inBytes)
	{
		DEBUGMSG("Rewinding %u bytes\n", deflate->inBytes);
		fseek(inStream, -((long) deflate->inBytes), SEEK_CUR);
	}

	if (deflate->outBuffer)
		free(deflate->outBuffer);

	if (deflate->inBuffer)
		free((void *) deflate->inBuffer);

	return (status);
}


void deflateMakeHuffmanTable(huffmanTable *table, int numCodes,
	unsigned char *codeLens)
{
	// Given a list of code lengths, allocate and construct a (dynamic)
	// Huffman table

	memset(table, 0, sizeof(huffmanTable));
	table->numCodes = numCodes;

	// Make the Huffman codes for the code lengths
	makeHuffmanCodes(codeLens, table);
}

