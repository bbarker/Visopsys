//
//  Visopsys
//  Copyright (C) 1998-2016 J. Andrew McLaughlin
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
#include <string.h>
#include <sys/deflate.h>

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

