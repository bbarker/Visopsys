//
//  Visopsys
//  Copyright (C) 1998-2018 J. Andrew McLaughlin
//
//  This program is free software; you can redistribute it and/or modify it
//  under the terms of the GNU General Public License as published by the Free
//  Software Foundation; either version 2 of the License, or (at your option)
//  any later version.
//
//  This program is distributed in the hope that it will be useful, but
//  WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
//  or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License
//  for more details.
//
//  You should have received a copy of the GNU General Public License along
//  with this program; if not, write to the Free Software Foundation, Inc.,
//  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
//
//  kernelImageJpg.c
//

// This file contains code for loading, saving, and converting images in
// the JPEG (.jpg) format.

// Contains code copyright (C) 1996, MPEG Software Simulation Group.
// All Rights Reserved.  See the function inverseDctBlock() for more
// information.

#include "kernelDebug.h"
#include "kernelError.h"
#include "kernelImage.h"
#include "kernelImageJpg.h"
#include "kernelLoader.h"
#include "kernelMalloc.h"
#include "kernelMemory.h"
#include "kernelText.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/processor.h>

#define H_Y_BLOCKSPERMCU	jpg->hvBlocksPerMcu[0]
#define V_Y_BLOCKSPERMCU	jpg->hvBlocksPerMcu[1]
#define H_CB_BLOCKSPERMCU	jpg->hvBlocksPerMcu[2]
#define V_CB_BLOCKSPERMCU	jpg->hvBlocksPerMcu[3]
#define H_CR_BLOCKSPERMCU	jpg->hvBlocksPerMcu[4]
#define V_CR_BLOCKSPERMCU	jpg->hvBlocksPerMcu[5]
#define Y_BLOCKSPERMCU		jpg->blocksPerMcu[0]
#define CB_BLOCKSPERMCU		jpg->blocksPerMcu[1]
#define CR_BLOCKSPERMCU		jpg->blocksPerMcu[2]

// YCbCr->RGB, float versions.  Generally better, but slower.
//#define rgbR(y, cr) (y + (1.402 * (cr - 128)))
//#define rgbG(y, cb, cr) (y - (0.34414 * (cb - 128)) - (0.71414 * (cr - 128)))
//#define rgbB(y, cb) (y + (1.772 * (cb - 128)))
// YCbCr->RGB, integer versions.  Not as good, but faster.
#define rgbR(y, cr) (((y << 16) + (91881 * (cr - 128))) >> 16)
#define rgbG(y, cb, cr) (((y << 16) - (22544 * (cb - 128)) - \
	(46793 * (cr - 128))) >> 16)
#define rgbB(y, cb) (((y << 16) + (116129 * (cb - 128))) >> 16)

// Default huffman table size and value arrays
static const unsigned char defaultHuffDcLumSizes[16] = {
	0, 1, 5, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0
};

static const unsigned char defaultHuffDcLumValues[] = {
	0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11
};

static const unsigned char defaultHuffDcChromSizes[16] = {
	0, 3, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0
};

static const unsigned char defaultHuffDcChromValues[] = {
	0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11
};

static const unsigned char defaultHuffAcLumSizes[16] = {
	0, 2, 1, 3, 3, 2, 4, 3, 5, 5, 4, 4, 0, 0, 1, 125
};

static const unsigned char defaultHuffAcLumValues[] = {
	0x01, 0x02, 0x03, 0x00, 0x04, 0x11, 0x05, 0x12, 0x21, 0x31, 0x41, 0x06,
	0x13, 0x51, 0x61, 0x07, 0x22, 0x71, 0x14, 0x32, 0x81, 0x91, 0xA1, 0x08,
	0x23, 0x42, 0xB1, 0xC1, 0x15, 0x52, 0xD1, 0xF0, 0x24, 0x33, 0x62, 0x72,
	0x82, 0x09, 0x0A, 0x16, 0x17, 0x18, 0x19, 0x1A, 0x25, 0x26, 0x27, 0x28,
	0x29, 0x2A, 0x34, 0x35,	0x36, 0x37, 0x38, 0x39, 0x3A, 0x43, 0x44, 0x45,
	0x46, 0x47, 0x48, 0x49, 0x4A, 0x53, 0x54, 0x55, 0x56, 0x57, 0x58, 0x59,
	0x5A, 0x63, 0x64, 0x65, 0x66, 0x67,	0x68, 0x69, 0x6A, 0x73, 0x74, 0x75,
	0x76, 0x77, 0x78, 0x79, 0x7A, 0x83, 0x84, 0x85, 0x86, 0x87, 0x88, 0x89,
	0x8A, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97, 0x98,	0x99, 0x9A, 0xA2, 0xA3,
	0xA4, 0xA5, 0xA6, 0xA7, 0xA8, 0xA9, 0xAA, 0xB2, 0xB3, 0xB4, 0xB5, 0xB6,
	0xB7, 0xB8, 0xB9, 0xBA, 0xC2, 0xC3, 0xC4, 0xC5, 0xC6, 0xC7,	0xC8, 0xC9,
	0xCA, 0xD2, 0xD3, 0xD4, 0xD5, 0xD6, 0xD7, 0xD8, 0xD9, 0xDA, 0xE1, 0xE2,
	0xE3, 0xE4, 0xE5, 0xE6, 0xE7, 0xE8, 0xE9, 0xEA, 0xF1, 0xF2, 0xF3, 0xF4,
	0xF5, 0xF6, 0xF7, 0xF8, 0xF9, 0xFA
};

static const unsigned char defaultHuffAcChromSizes[16] = {
	0, 2, 1, 2, 4, 4, 3, 4, 7, 5, 4, 4, 0, 1, 2, 119
};

static const unsigned char defaultHuffAcChromValues[] = {
	0x00, 0x01, 0x02, 0x03, 0x11, 0x04, 0x05, 0x21, 0x31, 0x06, 0x12, 0x41,
	0x51, 0x07, 0x61, 0x71, 0x13, 0x22, 0x32, 0x81, 0x08, 0x14, 0x42, 0x91,
	0xA1, 0xB1,	0xC1, 0x09, 0x23, 0x33, 0x52, 0xF0, 0x15, 0x62, 0x72, 0xD1,
	0x0A, 0x16, 0x24, 0x34, 0xE1, 0x25, 0xF1, 0x17, 0x18, 0x19, 0x1A, 0x26,
	0x27, 0x28, 0x29, 0x2A,	0x35, 0x36, 0x37, 0x38, 0x39, 0x3A, 0x43, 0x44,
	0x45, 0x46, 0x47, 0x48, 0x49, 0x4A, 0x53, 0x54, 0x55, 0x56, 0x57, 0x58,
	0x59, 0x5A, 0x63, 0x64, 0x65, 0x66,	0x67, 0x68, 0x69, 0x6A, 0x73, 0x74,
	0x75, 0x76, 0x77, 0x78, 0x79, 0x7A, 0x82, 0x83, 0x84, 0x85, 0x86, 0x87,
	0x88, 0x89, 0x8A, 0x92, 0x93, 0x94, 0x95, 0x96,	0x97, 0x98, 0x99, 0x9A,
	0xA2, 0xA3, 0xA4, 0xA5, 0xA6, 0xA7, 0xA8, 0xA9, 0xAA, 0xB2, 0xB3, 0xB4,
	0xB5, 0xB6, 0xB7, 0xB8, 0xB9, 0xBA, 0xC2, 0xC3, 0xC4, 0xC5,	0xC6, 0xC7,
	0xC8, 0xC9, 0xCA, 0xD2, 0xD3, 0xD4, 0xD5, 0xD6, 0xD7, 0xD8, 0xD9, 0xDA,
	0xE2, 0xE3, 0xE4, 0xE5, 0xE6, 0xE7, 0xE8, 0xE9, 0xEA, 0xF2, 0xF3, 0xF4,
	0xF5, 0xF6, 0xF7, 0xF8, 0xF9, 0xFA
};

static int zigZag[64] = {
	0, 1, 5, 6, 14, 15, 27, 28,
	2, 4, 7, 13, 16, 26, 29, 42,
	3, 8, 12, 17, 25, 30, 41, 43,
	9, 11, 18, 24, 31, 40, 44, 53,
	10, 19, 23, 32, 39, 45, 52, 54,
	20, 22, 33, 38, 46, 51, 55, 60,
	21, 34, 37, 47, 50, 56, 59, 61,
	35, 36, 48, 49, 57, 58, 62, 63
};


static int detect(const char *fileName, void *dataPtr, unsigned dataLength,
	loaderFileClass *class)
{
	// This function returns 1 and fills the fileClass structure if the data
	// points to an JPEG file.

	jpgJfifHeader *header = NULL;

	if (!fileName || !dataPtr || !dataLength || !class)
		return (0);

	// Make sure there's enough data here for our detection
	if (dataLength < (sizeof(JFIF_START) + sizeof(jpgJfifHeader)))
		return (0);

	// See whether this file claims to be a JPEG file

	if (memcmp(dataPtr, JFIF_START, sizeof(JFIF_START)) &&
		memcmp(dataPtr, EXIF_START, sizeof(EXIF_START)))
	{
		//kernelDebug(debug_misc, "JFIF_START or EXIF_START missing");
		return (0);
	}

	header = (dataPtr + sizeof(JFIF_START));

	if (strncmp(header->identifier, JFIF_MAGIC, sizeof(JFIF_MAGIC)) &&
		strncmp(header->identifier, EXIF_MAGIC, sizeof(EXIF_MAGIC)))
	{
		kernelDebug(debug_misc, "Magic number not %s or %s (%s)", JFIF_MAGIC,
			EXIF_MAGIC, header->identifier);
		return (0);
	}

	// We will say this is a JPG file.
	sprintf(class->name, "%s %s", FILECLASS_NAME_JPG, FILECLASS_NAME_IMAGE);
	class->type = (LOADERFILECLASS_BIN | LOADERFILECLASS_IMAGE);
	return (1);
}


#if defined(DEBUG)
static inline void printHuffTable(jpgHuffTable *table)
{
	int count;

	kernelDebug(debug_misc, "Huff table numCodes=%d", table->numCodes);
	kernelTextPrint("Sizes: ");
	for (count = 0; count < 16; count ++)
		kernelTextPrint("%d ", table->sizes[count]);

	kernelTextNewline();

	kernelTextPrint("Codes: ");
	for (count = 0; count < table->numCodes; count ++)
	{
		kernelTextPrint("%04x=%d ", table->huffCodes[count].code,
			table->huffCodes[count].value);
	}

	kernelTextNewline();
}


static inline void printQuantTable(jpgQuantTable *table)
{
	int count1, count2;

	kernelDebug(debug_misc, "Quant table precision=%d ident=%d:",
		table->precision, table->ident);

	for (count1 = 0; count1 < 8; count1 ++)
	{
		for (count2 = 0; count2 < 8; count2 ++)
		{
			if (table->precision == 8)
			{
				kernelTextPrint("%d ", table->values.val8[(count1 * 8) +
					count2]);
			}
			else
			{
				kernelTextPrint("%d ", table->values.val16[(count1 * 8) +
					count2]);
			}
		}

		kernelTextNewline();
	}
}


static inline void printBlock(short *coeff)
{
	int count1, count2;

	kernelDebug(debug_misc, "Coefficient block:");
	for (count1 = 0; count1 < 8; count1 ++)
	{
		for (count2 = 0; count2 < 8; count2 ++)
			kernelTextPrint("%d, ", coeff[(count1 * 8) + count2]);

		kernelTextNewline();
	}
}
#else
	#define printHuffTable(table)  do {} while (0)
	#define printQuantTable(table)  do {} while (0)
	#define printBlock(coeff) do {} while (0)
#endif


static void genHuffTable(const unsigned char *sizes,
	const unsigned char *values, jpgHuffTable *table)
{
	// Given pointers to arrays of bit size counts and values, generate the
	// values for the huffman table.

	int code = 0;
	int count1, count2;

	for (count1 = 0; count1 < 16; count1++)
	{
		table->sizes[count1] = sizes[count1];
		table->sizedCodes[count1] = &table->huffCodes[table->numCodes];

		if (table->sizes[count1])
		{
			kernelDebug(debug_misc, "table->sizes[%d]=%d", count1,
				table->sizes[count1]);

			//kernelDebug(debug_misc, "%d bits: ", (count1 + 1));
			for (count2 = 0; count2 < table->sizes[count1]; count2++)
			{
				//kernelDebug(debug_misc, "%04x=%d ", code,
				//	values[table->numCodes]);
				table->huffCodes[table->numCodes].code = code;
				table->huffCodes[table->numCodes].value =
					values[table->numCodes];
				table->numCodes += 1;
				code += 1;
			}
		}

		code <<= 1;
	}
}


static int genQuantTable(int precision, int ident, unsigned char *values,
	jpgQuantTable *table)
{
	// Given the number of bits per value and a pointer to the array of
	// values, construct a quantization table

	int status = 0;

	if (!precision)
		table->precision = 8;
	else
		table->precision = 16;

	table->ident = ident;

	if (table->precision == 8)
	{
		memcpy(table->values.val8, values, (64 * sizeof(unsigned char)));
	}
	else if (table->precision == 16)
	{
		memcpy(table->values.val16, values, (64 * sizeof(unsigned short)));
	}
	else
	{
		kernelError(kernel_error, "Quantization tables of precision %d are "
			"not supported", precision);
		return (status = ERR_NOTIMPLEMENTED);
	}

	return (status = 0);
}


static unsigned short readBits(unsigned char *dataPointer,
	unsigned *bitPosition, int bits, int consume)
{
	// Given a pointer to the start of data, return the requested number of
	// bits (up to 16) from the stream into the variable pointer.  If
	// 'consume' is non-zero, the bit position will be moved forward.

	unsigned byteOffset = (*bitPosition / 8);
	unsigned bitOffset = (*bitPosition % 8);
	unsigned char *bytes = NULL;
	int numBytes = 0;
	int gotBytes = 0;
	int nullBytes[4];
	unsigned value = 0;
	int count;

	if (consume)
	{
		numBytes = ((bitOffset + bits + 7) / 8);

		for (count = 0; count < numBytes; count ++)
			nullBytes[count] = 0;
	}

	// Grab 4 bytes from the current position and put them into the 'value'
	// variable.
	bytes = (unsigned char *)(dataPointer + byteOffset);
	for (count = 0; gotBytes < 4; count ++)
	{
		// Watch out for 0xFF00 sequences
		if ((bytes[count] == 0x00) && byteOffset &&
			(bytes[count - 1] == 0xFF))
		{
			// This is 0x00, and the previous byte was 0xFF, so we skip this
			// byte and make a note of it.
			nullBytes[gotBytes] = 1;
			continue;
		}
		else
		{
			value = ((value << 8) | bytes[count]);
			gotBytes += 1;
		}
	}

	// Adjust the value so that it's just the requested number of bits,
	// shifted to the right.
	value &= (0xFFFFFFFF >> bitOffset);
	value >>= (32 - (bits + bitOffset));

	if (consume)
	{
		// We need to move the bit position forward.  First the number that
		// are actually requested.
		*bitPosition += bits;

		// Now add for any 0xFF00 sequences where we skipped the 0x00 part.
		for (count = 0; count < numBytes; count ++)
		{
			if (nullBytes[count])
				*bitPosition += 8;
		}
	}

	return ((unsigned short)(value & 0xFFFF));
}


static inline int getHuffValue(jpgHuffTable *table, int bits,
	unsigned short code, unsigned char *value)
{
	// Given a huffman table and a bit code, return the appropriate value

	int status = 0;
	int count;

	for (count = 0; count < table->sizes[bits - 1]; count ++)
	{
		if (table->sizedCodes[bits - 1][count].code == code)
		{
			*value = table->sizedCodes[bits - 1][count].value;
			return (status = 0);
		}
	}

	//kernelDebug(debug_misc, "%d-bit code %04x not found", bits, code);
	return (status = ERR_NODATA);
}


static int readHuffValue(unsigned char *dataPointer, unsigned *bitPosition,
	jpgHuffTable *table, unsigned char *value)
{
	// Query bits from the stream until we get a valid huffman code

	int status = 0;
	unsigned raw = 0;
	int bits = 0;

	// Peek the maximum possible, 16 bits
	raw = readBits(dataPointer, bitPosition, 16, 0);

	// While we have only leading ones, shift them up, since there are no
	// valid codes with all ones.
	while (raw & 0x8000)
	{
		raw <<= 1;
		bits += 1;
	}

	if (bits < 16)
	{
		// Then shift one more.
		raw <<= 1;
		bits += 1;
	}

	for ( ; bits <= 16; bits += 1)
	{
		status = getHuffValue(table, bits, (raw >> 16), value);
		if (status >= 0)
		{
			// Consume the relevant bits
			readBits(dataPointer, bitPosition, bits, 1);
			return (status);
		}

		raw <<= 1;
	}

	// If we fall through, we didn't find a valid code.
	return (status = ERR_NODATA);
}


static inline short bitcode2Value(unsigned char bits, unsigned short code)
{
	// Given a category (number of bits) and a bitcode value, calculate the
	// actual value.

	int msb = 0;
	short value = 0;

	msb = (1 << (bits - 1));

	if (code & msb)
		value = code;
	else
		value = ((-msb << 1) + 1 + code);

	return (value);
}


static unsigned char markerCheck(unsigned char *dataPointer,
	unsigned *bitPosition)
{
	// Peeks ahead in the bitstream for any markers (on the next byte
	// boundary, if applicable), and if a marker is found, discards any
	// stuffed bits that come before it.

	unsigned bytePosition;
	unsigned char marker = 0;
	unsigned short tmpValue = 0;
	int stuffBits = 0;

	// Take a peek ahead.
	tmpValue = readBits(dataPointer, bitPosition, 16, 0);

	// Starts with 0xFF?
	if ((tmpValue & 0xFF00) != 0xFF00)
		return (marker = 0);

	// Are we on a byte boundary?
	if (*bitPosition % 8)
	{
		// Peek ahead to the byte boundary
		stuffBits = (8 - (*bitPosition % 8));
		bytePosition = (*bitPosition + stuffBits);
		tmpValue = readBits(dataPointer, &bytePosition, 16, 0);
	}

	if (stuffBits)
	{
		// There was bitstuffing.  Move ahead to the byte boundary.
		kernelDebug(debug_misc, "Discard %d stuff bits", stuffBits);
		*bitPosition += stuffBits;
		//kernelDebugBinary((dataPointer + (*bitPosition / 8)), 4);
	}

	marker = (tmpValue & 0xFF);
	kernelDebug(debug_misc, "Marker %02x", marker);
	*bitPosition += 16;
	return (marker);
}


static int readBlock(unsigned char *dataPointer, unsigned *bitPosition,
	jpgHuffTable *dcTable, jpgHuffTable *acTable, short *dcValue,
	short *coeff)
{
	// Reads an 8x8 image block.  Given a pointer to the data and the current
	// bit position, and DC and AC huffman tables, and the current DC value,
	// fill in the array of 64 coefficients.

	int status = 0;
	unsigned char category;
	unsigned char zeros;
	short dcDiff = 0;
	unsigned short tmpValue = 0;
	int count;

	//kernelDebug(debug_misc, "Start block at %u:%d", (*bitPosition / 8),
	//	(*bitPosition % 8));
	//kernelDebugBinary((dataPointer + (*bitPosition / 8)), 16);

	// Get the category of the DC coefficient
	status = readHuffValue(dataPointer, bitPosition, dcTable, &category);
	if (status < 0)
	{
		kernelDebugError("Can't decode DC category for bits %04x offset "
			"%u:%d", readBits(dataPointer, bitPosition, 16, 0),
			(*bitPosition / 8), (*bitPosition % 8));
		kernelDebugBinary((dataPointer + (*bitPosition / 8)), 4);
		return (status);
	}

	//kernelDebug(debug_misc, "DC category %d", category);

	if (category)
	{
		// Read 'category' bits of the DC bitcode.
		tmpValue = readBits(dataPointer, bitPosition, category, 1);

		//kernelDebug(debug_misc, "DC bitcode %d", tmpValue);

		// Get the value for the DC bitcode.
		dcDiff = bitcode2Value(category, tmpValue);
		*dcValue += dcDiff;

		//kernelDebug(debug_misc, "DC diff %d, value %d", dcDiff, *dcValue);
	}

	coeff[0] = *dcValue;

	// Now 63 AC coefficients
	for (count = 1; count < 64; count ++)
	{
		// Get a huffman code for the byte that contains the number of zeros
		// and the category of the next value.
		status = readHuffValue(dataPointer, bitPosition, acTable, &category);
		if (status < 0)
		{
			kernelDebugError("Can't decode AC zeros/category for bits %04x "
				"offset %d", readBits(dataPointer, bitPosition, 16, 0),
				(*bitPosition % 8));
			kernelDebugBinary((dataPointer + (*bitPosition / 8)), 4);
			return (status);
		}

		// Check for EOB
		if (!category)
			break;

		zeros = (category >> 4);
		category &= 0x0F;

		//kernelDebug(debug_misc, "AC zeros %d category %d", zeros, category);

		// Check for runs of zeros
		if (zeros)
		{
			count += zeros;

			if (!category)
				continue;
		}

		// Manually read 'category' bits of the bitcode
		tmpValue = readBits(dataPointer, bitPosition, category, 1);

		//kernelDebug(debug_misc, "AC bitcode %d", tmpValue);

		// Get the value for the AC bitcode.
		coeff[count] = bitcode2Value(category, tmpValue);
		//kernelDebug(debug_misc, "AC value %d", coeff[count]);
	}

	return (status = 0);
}


static void deQuantBlock(short *coeff, jpgQuantTable *table)
{
	// De-quantize an 8x8 data block, given the (zig-zagged) raw component and
	// the appropriate quantization table.  Returns de-quantized, de-zigzagged
	// values in the same array of coefficients.

	short tmpCoeff[64];
	int count1;

	// De-quantize
	for (count1 = 0; count1 < 64; count1 ++)
	{
		if (table->precision == 8)
			tmpCoeff[count1] = (coeff[count1] * table->values.val8[count1]);
		else
			tmpCoeff[count1] = (coeff[count1] * table->values.val16[count1]);
	}

	// De-zig-zag
	for (count1 = 0; count1 < 64; count1 ++)
		coeff[count1] = tmpCoeff[zigZag[count1]];
}


static void inverseDctBlock(short *coeff)
{
	// Perform IDCT (inverse discrete cosine transform) on an 8x8 block of
	// coefficients, and level-shift (add 128).  This is performed on a
	// de-quantized, de-zig-zagged array as returned by the deQuantBlock()
	// function, above.
	//
	// Information about the algorithm:
	//   Inverse two dimensional DCT, Chen-Wang algorithm
	//   (cf. IEEE ASSP-32, pp. 803-816, Aug. 1984)
	//   32-bit integer arithmetic (16 bit coefficients)
	//   11 mults, 29 adds per DCT
	//
	// This function is a (lightly) modified version of the function
	// idct_int32() in idct.c found in mpeg2avi_016B34.zip
	// at http://www.geocities.com/liaor2/mpeg2avi/mpeg2avi.htm.  The
	// copyright notice is as follows:
	//
	// Copyright (C) 1996, MPEG Software Simulation Group.  All Rights
	// Reserved.
	//
	// Disclaimer of Warranty
	//
	// These software programs are available to the user without any license
	// fee or royalty on an "as is" basis.  The MPEG Software Simulation Group
	// disclaims any and all warranties, whether express, implied, or
	// statuary, including any implied warranties or merchantability or of
	// fitness for a particular purpose.  In no event shall the copyright-
	// holder be liable for any incidental, punitive, or consequential damages
	// of any kind whatsoever arising from the use of these programs.
	//
	// This disclaimer of warranty extends to the user of these programs and
	// user's customers, employees, agents, transferees, successors, and
	// assigns.
	//
	// The MPEG Software Simulation Group does not represent or warrant that
	// the programs furnished hereunder are free of infringement of any third-
	// party patents.
	//
	// Commercial implementations of MPEG-1 and MPEG-2 video, including
	// shareware, are subject to royalty fees to patent holders.  Many of
	// these patents are general enough such that they are unavoidable
	// regardless of implementation design.

	#define W1 2841 // 2048*sqrt(2)*cos(1*pi/16)
	#define W2 2676 // 2048*sqrt(2)*cos(2*pi/16)
	#define W3 2408 // 2048*sqrt(2)*cos(3*pi/16)
	#define W5 1609 // 2048*sqrt(2)*cos(5*pi/16)
	#define W6 1108 // 2048*sqrt(2)*cos(6*pi/16)
	#define W7 565  // 2048*sqrt(2)*cos(7*pi/16)

	static short *iclip = NULL; // Clipping table
	static short *iclp = NULL;
	short *co;
	int i;
	int X0, X1, X2, X3, X4, X5, X6, X7, X8;

	if (!iclip)
	{
		iclip = kernelMalloc(1024 * sizeof(short));
		iclp = (iclip + 512);
		for (i = -512; i < 512; i ++)
			iclp[i] = ((i < -256)? -256 : ((i > 255)? 255 : i));
	}

	for (i = 0; i < 8; i ++) // IDCT rows
	{
		co = (coeff + (i << 3));
		if (!((X1 = co[4] << 11) | (X2 = co[6]) | (X3 = co[2]) |
			(X4 = co[1]) | (X5 = co[7]) | (X6 = co[5]) | (X7 = co[3])))
		{
			co[0] = co[1] = co[2] = co[3] = co[4] = co[5] = co[6] = co[7] =
				(co[0] << 3);
			continue;
		}

		X0 = ((co[0] << 11) + 128); // For proper rounding in the fourth stage

		// First stage
		X8 = (W7 * (X4 + X5));
		X4 = (X8 + (W1 - W7) * X4);
		X5 = (X8 - (W1 + W7) * X5);
		X8 = (W3 * (X6 + X7));
		X6 = (X8 - (W3 - W5) * X6);
		X7 = (X8 - (W3 + W5) * X7);

		// Second stage
		X8 = (X0 + X1);
		X0 -= X1;
		X1 = (W6 * (X3 + X2));
		X2 = (X1 - (W2 + W6) * X2);
		X3 = (X1 + (W2 - W6) * X3);
		X1 = (X4 + X6);
		X4 -= X6;
		X6 = (X5 + X7);
		X5 -= X7;

		// Third stage
		X7 = (X8 + X3);
		X8 -= X3;
		X3 = (X0 + X2);
		X0 -= X2;
		X2 = ((181 * (X4 + X5) + 128) >> 8);
		X4 = ((181 * (X4 - X5) + 128) >> 8);

		// Fourth stage

		co[0] = ((X7 + X1) >> 8);
		co[1] = ((X3 + X2) >> 8);
		co[2] = ((X0 + X4) >> 8);
		co[3] = ((X8 + X6) >> 8);
		co[4] = ((X8 - X6) >> 8);
		co[5] = ((X0 - X4) >> 8);
		co[6] = ((X3 - X2) >> 8);
		co[7] = ((X7 - X1) >> 8);

	} // End for (i = 0; i < 8; ++i) IDCT-rows

	for (i = 0; i < 8; i++) // IDCT columns
	{
		co = (coeff + i);
		// Shortcut
		if (!((X1 = (co[8 * 4] << 8)) | (X2 = co[8 * 6]) | (X3 = co[8 * 2]) |
			(X4 = co[8 * 1]) | (X5 = co[8 * 7]) | (X6 = co[8 * 5]) |
			(X7 = co[8 * 3])))
		{
			co[8 * 0] = co[8 * 1] = co[8 * 2] = co[8 * 3] = co[8 * 4] =
			co[8 * 5] = co[8 * 6] = co[8 * 7] = iclp[(co[8 * 0] + 32) >> 6];
			continue;
		}

		X0 = ((co[8 * 0] << 8) + 8192);

		// First stage
		X8 = (W7 * (X4 + X5) + 4);
		X4 = ((X8 + (W1 - W7) * X4) >> 3);
		X5 = ((X8 - (W1 + W7) * X5) >> 3);
		X8 = (W3 * (X6 + X7) + 4);
		X6 = ((X8 - (W3 - W5) * X6) >> 3);
		X7 = ((X8 - (W3 + W5) * X7) >> 3);

		// Second stage
		X8 = (X0 + X1);
		X0 -= X1;
		X1 = (W6 * (X3 + X2) + 4);
		X2 = ((X1 - (W2 + W6) * X2) >> 3);
		X3 = ((X1 + (W2 - W6) * X3) >> 3);
		X1 = (X4 + X6);
		X4 -= X6;
		X6 = (X5 + X7);
		X5 -= X7;

		// Third stage
		X7 = (X8 + X3);
		X8 -= X3;
		X3 = (X0 + X2);
		X0 -= X2;
		X2 = ((181 * (X4 + X5) + 128) >> 8);
		X4 = ((181 * (X4 - X5) + 128) >> 8);

		// Fourth stage
		co[8 * 0] = iclp[(X7 + X1) >> 14];
		co[8 * 1] = iclp[(X3 + X2) >> 14];
		co[8 * 2] = iclp[(X0 + X4) >> 14];
		co[8 * 3] = iclp[(X8 + X6) >> 14];
		co[8 * 4] = iclp[(X8 - X6) >> 14];
		co[8 * 5] = iclp[(X0 - X4) >> 14];
		co[8 * 6] = iclp[(X3 - X2) >> 14];
		co[8 * 7] = iclp[(X7 - X1) >> 14];
	}

	// Level shift (add 128)
	for (i = 0; i < 64; i ++)
		coeff[i] += 128;
}


static void arrangeMcu(int hBlocks, int vBlocks, short *coeff)
{
	// Given a sequential array of blocks, arrange them into an MCU (suitable
	// for upsampling).

	short *tmpCoeff = NULL;
	int srcIndex = 0;
	int destIndex = 0;
	int count1, count2, count3;

	tmpCoeff = kernelMalloc(hBlocks * vBlocks * 64 * sizeof(short));
	if (!tmpCoeff)
		return;

	for (count1 = 0; count1 < vBlocks; count1 ++)
	{
		for (count2 = 0; count2 < hBlocks; count2 ++)
		{
			destIndex = ((count1 * hBlocks * 64) + (count2 * 8));
			for (count3 = 0; count3 < 64; count3 ++)
			{
				tmpCoeff[destIndex++] = coeff[srcIndex++];
				if (!(destIndex % 8))
					destIndex += ((hBlocks - 1) * 8);
			}
		}
	}

	memcpy(coeff, tmpCoeff, (hBlocks * vBlocks * 64 * sizeof(short)));
	kernelFree(tmpCoeff);
}


static void upsampleMcu(int hyBlocks, int vyBlocks, int hcBlocks,
	int vcBlocks, short *cCoeff)
{
	// Up-samples a subsampled array of chroma coefficients so that it matches
	// the size of the luma coefficients.

	int hRatio = (hyBlocks / hcBlocks);
	int vRatio = (vyBlocks / vcBlocks);
	int srcIdx = (((hcBlocks * 8) * (vcBlocks * 8)) - 1);
	int destIdx = (((hyBlocks * 8) * (vyBlocks * 8)) - 1);
	int count1, count2, count3, count4;

	//kernelDebug(debug_misc, "hRatio=%d vRatio=%d srcIdx=%d destIdx=%d",
	//	hRatio, vRatio, srcIdx, destIdx);

	//kernelDebug(debug_misc, "Before upsample:");
	//for (count1 = 0; count1 < (vcBlocks * 8); count1 ++)
	//{
	//	for (count2 = 0; count2 < (hcBlocks * 8); count2 ++)
	//	{
	//		kernelTextPrint("%d ", cCoeff[(count1 * (hcBlocks * 8)) +
	//			count2]);
	//	}
	//	kernelTextNewline();
	//}

	for (count1 = 0; count1 < (vyBlocks * 8); count1 += vRatio)
	{
		for (count2 = 0; count2 < (hyBlocks * 8); count2 += hRatio)
		{
			for (count3 = 0; count3 < vRatio; count3 ++)
			{
				for (count4 = 0; count4 < hRatio; count4 ++)
				{
					cCoeff[destIdx - (count3 * hyBlocks * 8) - count4] =
						cCoeff[srcIdx];
				}
			}

			srcIdx -= 1;
			destIdx -= hRatio;
		}

		destIdx -= ((vRatio - 1) * hyBlocks * 8);
	}

	//kernelDebug(debug_misc, "After upsample:");
	//for (count1 = 0; count1 < (vyBlocks * 8); count1 ++)
	//{
	//	for (count2 = 0; count2 < (hyBlocks * 8); count2 ++)
	//	{
	//		kernelTextPrint("%d ", cCoeff[(count1 * (hyBlocks * 8)) +
	//			count2]);
	//	}
	//	kernelTextNewline();
	//}
}


static void mcuToRgb(jpgData *jpg, short *yCoeff, short *cbCoeff,
	short *crCoeff, int xCoord, int yCoord, pixel *pixels)
{
	// Transforms 3 processed (Y, Cb, Cr) coefficient arrays for an MCU into
	// the supplied pixel array.  The xCoord and yCoord parameters are the
	// starting coordinates of the resulting 8x8 blocks.

	unsigned pixelIndex = 0;
	int mcuHeight = 0;
	int mcuWidth = 0;
	int coeffIndex = 0;
	short red = 0, green = 0, blue = 0;
	int count1, count2;

	pixelIndex = ((yCoord * jpg->frameHeader->width) + xCoord);
	mcuHeight = min((V_Y_BLOCKSPERMCU * 8), (jpg->frameHeader->height -
		yCoord));
	mcuWidth = min((H_Y_BLOCKSPERMCU * 8), (jpg->frameHeader->width -
		xCoord));

	//kernelDebug(debug_misc, "Start compToRgb (%d,%d)... ", xCoord, yCoord);

	for (count1 = 0; count1 < mcuHeight; count1 ++)
	{
		for (count2 = 0; count2 < mcuWidth; count2 ++)
		{
			coeffIndex = ((count1 * H_Y_BLOCKSPERMCU * 8) + count2);

			red = rgbR(yCoeff[coeffIndex], crCoeff[coeffIndex]);
			red = min(red, 255);
			red = max(red, 0);
			pixels[pixelIndex].red = (unsigned char) red;

			green = rgbG(yCoeff[coeffIndex], cbCoeff[coeffIndex],
				crCoeff[coeffIndex]);
			green = min(green, 255);
			green = max(green, 0);
			pixels[pixelIndex].green = (unsigned char) green;

			blue = rgbB(yCoeff[coeffIndex], cbCoeff[coeffIndex]);
			blue = min(blue, 255);
			blue = max(blue, 0);
			pixels[pixelIndex].blue = (unsigned char) blue;

			//kernelDebug(debug_misc, "Value %d (%d,%d,%d)", coeffIndex,
			//	pixels[pixelIndex].red, pixels[pixelIndex].green,
			//	pixels[pixelIndex].blue);
			pixelIndex += 1;
		}

		pixelIndex += (jpg->frameHeader->width - mcuWidth);
	}

	//kernelDebug(debug_misc, "...End compToRgb");
}


static int decode(jpgData *jpg, pixel *imageData)
{
	int status = 0;
	short *yCoeff = NULL;
	short *cbCoeff = NULL;
	short *crCoeff = NULL;
	int xCoord = 0;
	int yCoord = 0;
	int mcuCount = 0;
	int restartCount = 0;
	unsigned char marker = 0;
	int count;

	yCoeff = kernelMalloc((Y_BLOCKSPERMCU * 64) * sizeof(short));
	cbCoeff = kernelMalloc((Y_BLOCKSPERMCU * 64) * sizeof(short));
	crCoeff = kernelMalloc((Y_BLOCKSPERMCU * 64) * sizeof(short));
	if (!yCoeff || !cbCoeff || !crCoeff)
		return (status = ERR_MEMORY);

	kernelDebug(debug_misc, "Y_BLOCKSPERMCU=%d CB_BLOCKSPERMCU=%d "
		"CR_BLOCKSPERMCU=%d", Y_BLOCKSPERMCU, CB_BLOCKSPERMCU,
		CR_BLOCKSPERMCU);

	// Process each component's blocks (generally Y, Cb, Cr)
	for (yCoord = 0; yCoord < jpg->frameHeader->height;
		yCoord += (V_Y_BLOCKSPERMCU * 8))
	{
		for (xCoord = 0; xCoord < jpg->frameHeader->width;
			xCoord += (H_Y_BLOCKSPERMCU * 8), mcuCount += 1)
		{
			if (jpg->restartHeader && jpg->restartHeader->interval)
			{
				// See whether we are expecting a restart marker to start this
				// MCU
				//kernelDebug(debug_misc, "mcuCount=%d interval=%d", mcuCount,
				//	jpg->restartHeader->interval);
				if (mcuCount && !(mcuCount % jpg->restartHeader->interval))
				{
					// Expect a restart marker here, with possibly some bit
					// stuffing in front of it.  markerCheck() will do all of
					// that for us.
					marker = markerCheck(jpg->dataPointer, &jpg->bitPosition);
					marker &= 0x0F;

					kernelDebug(debug_misc, "Restart marker %d", marker);
					jpg->yDcValue = 0;
					jpg->cbDcValue = 0;
					jpg->crDcValue = 0;

					// Is it the correct marker?
					if (marker != restartCount)
					{
						kernelDebugError("Expected restart marker %d, got %d",
							restartCount, marker);
						restartCount = marker;
					}

					// Increment, but they cycle 0-7.
					restartCount = ((restartCount + 1) % 8);
				}
			}

			//kernelDebug(debug_misc, "Start MCU %d (%d,%d)", mcuCount,
			//	(xCoord / (H_Y_BLOCKSPERMCU * 8)),
			//	(yCoord / (V_Y_BLOCKSPERMCU * 8)));
			//kernelDebugBinary((jpg->dataPointer + (jpg->bitPosition / 8)),
			//	16);

			memset(yCoeff, 0, ((Y_BLOCKSPERMCU * 64) * sizeof(short)));
			memset(cbCoeff, 0, ((Y_BLOCKSPERMCU * 64) * sizeof(short)));
			memset(crCoeff, 0, ((Y_BLOCKSPERMCU * 64) * sizeof(short)));

			// Read the Y (luminance) blocks
			for (count = 0; count < Y_BLOCKSPERMCU; count ++)
			{
				//kernelDebug(debug_misc, "Start Y%d at %u:%d", count,
				//	(jpg->bitPosition / 8), (jpg->bitPosition % 8));
				status = readBlock(jpg->dataPointer, &jpg->bitPosition,
					&jpg->huffTable[JPG_HUFF_DC_LUM],
					&jpg->huffTable[JPG_HUFF_AC_LUM], &jpg->yDcValue,
					(yCoeff + (count * 64)));
				if (status < 0)
				{
					kernelDebugError("Error decoding Y block at offset %u:%d "
						"MCU %d (%d,%d)", (jpg->bitPosition / 8),
						(jpg->bitPosition % 8), mcuCount,
						(xCoord / (H_Y_BLOCKSPERMCU * 8)),
						(yCoord / (V_Y_BLOCKSPERMCU * 8)));
					goto out;
				}

				deQuantBlock((yCoeff + (count * 64)),
					&jpg->quantTable[jpg->frameHeader->comp[0].quantTable]);
				inverseDctBlock((yCoeff + (count * 64)));
			}

			// Read the Cb (blue chrominance) blocks
			for (count = 0; count < CB_BLOCKSPERMCU; count ++)
			{
				//kernelDebug(debug_misc, "Start Cb at %u:%d",
				//	(jpg->bitPosition / 8), (jpg->bitPosition % 8));
				status = readBlock(jpg->dataPointer, &jpg->bitPosition,
					&jpg->huffTable[JPG_HUFF_DC_CHROM],
					&jpg->huffTable[JPG_HUFF_AC_CHROM], &jpg->cbDcValue,
					(cbCoeff + (count * 64)));
				if (status < 0)
				{
					kernelDebugError("Error decoding Cb block at offset "
						"%u:%d MCU %d (%d,%d)", (jpg->bitPosition / 8),
						(jpg->bitPosition % 8), mcuCount,
						(xCoord / (H_Y_BLOCKSPERMCU * 8)),
						(yCoord / (V_Y_BLOCKSPERMCU * 8)));
					goto out;
				}

				deQuantBlock((cbCoeff + (count * 64)),
					&jpg->quantTable[jpg->frameHeader->comp[1].quantTable]);
				inverseDctBlock((cbCoeff + (count * 64)));
			}

			// Read the Cr (red chrominance) blocks
			for (count = 0; count < CR_BLOCKSPERMCU; count ++)
			{
				//kernelDebug(debug_misc, "Start Cr at %u:%d",
				//	(jpg->bitPosition / 8), (jpg->bitPosition % 8));
				status = readBlock(jpg->dataPointer, &jpg->bitPosition,
					&jpg->huffTable[JPG_HUFF_DC_CHROM],
					&jpg->huffTable[JPG_HUFF_AC_CHROM], &jpg->crDcValue,
					(crCoeff + (count * 64)));
				if (status < 0)
				{
					kernelDebugError("Error decoding Cr block at offset "
						"%u:%d MCU %d (%d,%d)", (jpg->bitPosition / 8),
						(jpg->bitPosition % 8), mcuCount,
						(xCoord / (H_Y_BLOCKSPERMCU * 8)),
						(yCoord / (V_Y_BLOCKSPERMCU * 8)));
					goto out;
				}

				deQuantBlock((crCoeff + (count * 64)),
					&jpg->quantTable[jpg->frameHeader->comp[2].quantTable]);
				inverseDctBlock((crCoeff + (count * 64)));
			}

			// If the chroma coefficients are subsampled, expand the arrays.
			if (Y_BLOCKSPERMCU != 1)
			{
				arrangeMcu(H_Y_BLOCKSPERMCU, V_Y_BLOCKSPERMCU, yCoeff);

				if (CB_BLOCKSPERMCU != 1)
					arrangeMcu(H_CB_BLOCKSPERMCU, V_CB_BLOCKSPERMCU, cbCoeff);

				if (CB_BLOCKSPERMCU != Y_BLOCKSPERMCU)
				{
					upsampleMcu(H_Y_BLOCKSPERMCU, V_Y_BLOCKSPERMCU,
						H_CB_BLOCKSPERMCU, V_CB_BLOCKSPERMCU, cbCoeff);
				}

				if (CR_BLOCKSPERMCU != 1)
					arrangeMcu(H_CR_BLOCKSPERMCU, V_CR_BLOCKSPERMCU, crCoeff);

				if (CR_BLOCKSPERMCU != Y_BLOCKSPERMCU)
				{
					upsampleMcu(H_Y_BLOCKSPERMCU, V_Y_BLOCKSPERMCU,
						H_CR_BLOCKSPERMCU, V_CR_BLOCKSPERMCU, crCoeff);
				}
			}

			// We finished reading all the blocks for this MCU.  Now turn them
			// into RGB image data.
			mcuToRgb(jpg, yCoeff, cbCoeff, crCoeff, xCoord, yCoord,
				imageData);
		}

		//kernelDebug(debug_misc, "Decoded lines %d-%d", yCoord,
		//	(yCoord + (V_Y_BLOCKSPERMCU * 8) - 1));
	}

	status = 0;

out:
	kernelFree(yCoeff);
	kernelFree(cbCoeff);
	kernelFree(crCoeff);
	return (status);
}


static int load(unsigned char *imageFileData, int dataLength,
	int reqWidth __attribute__((unused)),
	int reqHeight __attribute__((unused)), image *loadImage)
{
	// Loads a .jpg file and returns it as an image.  The memory for this and
	// its data must be freed by the caller.

	int status = 0;
	jpgData *jpg = NULL;
	jpgJfifHeader *jfifHeader = NULL;
	jpgExifHeader *exifHeader = NULL;
	jpgHuffHeader *huffHeader = NULL;
	jpgHuffTableHeader *huffTableHeader = NULL;
	jpgHuffTable *huffTable = NULL;
	jpgQuantHeader *quantHeader = NULL;
	pixel *imageData = NULL;
	int count1, count2;

	// Check params
	if (!imageFileData || !dataLength || !loadImage)
		return (status = ERR_NULLPARAMETER);

	// Get memory for the JPEG data
	jpg = kernelMalloc(sizeof(jpgData));
	if (!jpg)
		return (status = ERR_MEMORY);

	// Loop through the file data and get pointers to the various tables we
	// need.
	for (count1 = 0; ((count1 < (dataLength - 1)) && !jpg->dataPointer);
		count1 ++)
	{
		// Each marker consists of two bytes: an FF byte followed by a byte
		// which is not equal to 0 or FF and specifies the type of the marker
		if ((imageFileData[count1] != 0xFF) || !imageFileData[count1 + 1] ||
			(imageFileData[count1 + 1] == 0xFF))
		{
			continue;
		}

		count1 += 1;

		switch (imageFileData[count1])
		{
			case JPG_DHT:
			{
				// At least one huffman table is here
				huffHeader = (jpgHuffHeader *)(imageFileData + count1 + 1);
				huffHeader->length = processorSwap16(huffHeader->length);
				kernelDebug(debug_misc, "Hufftable(s) %d bytes at %d",
					huffHeader->length, count1);

				huffTableHeader = &huffHeader->table[0];

				for (count2 = 0; (((void *) huffTableHeader +
					sizeof(jpgHuffTableHeader)) < ((void *) huffHeader +
					huffHeader->length)); count2 ++)
				{
					// Construct the huffman table from the huffman header
					if (huffTableHeader->classIdent == JPG_HUFF_DC_LUM_ID)
					{
						kernelDebug(debug_misc, "Hufftable %d "
							"JPG_HUFF_DC_LUM", count2);
						huffTable = &jpg->huffTable[JPG_HUFF_DC_LUM];
					}
					else if (huffTableHeader->classIdent ==
						JPG_HUFF_AC_LUM_ID)
					{
						kernelDebug(debug_misc, "Hufftable %d "
							"JPG_HUFF_AC_LUM", count2);
						huffTable = &jpg->huffTable[JPG_HUFF_AC_LUM];
					}
					else if (huffTableHeader->classIdent ==
						JPG_HUFF_DC_CHROM_ID)
					{
						kernelDebug(debug_misc, "Hufftable %d "
							"JPG_HUFF_DC_CHROM", count2);
						huffTable = &jpg->huffTable[JPG_HUFF_DC_CHROM];
					}
					else if (huffTableHeader->classIdent ==
						JPG_HUFF_AC_CHROM_ID)
					{
						kernelDebug(debug_misc, "Hufftable %d "
							"JPG_HUFF_AC_CHROM", count2);
						huffTable = &jpg->huffTable[JPG_HUFF_AC_CHROM];
					}
					else
					{
						kernelError(kernel_error, "Unknown Huffman table "
							"ident %d", huffTableHeader->classIdent);
						break;
					}

					genHuffTable(huffTableHeader->sizes,
						huffTableHeader->values, huffTable);

					huffTableHeader = ((void *) huffTableHeader +
						sizeof(jpgHuffTableHeader) + huffTable->numCodes);
				}

				count1 += huffHeader->length;
				break;
			}

			case JPG_SOS:
			{
				// A start-of-scan marker is here
				jpg->scanHeader = (jpgScanHeader *)(imageFileData + count1 +
					1);
				jpg->scanHeader->length =
					processorSwap16(jpg->scanHeader->length);
				jpg->dataPointer = (imageFileData + count1 + 1 +
					jpg->scanHeader->length);
				kernelDebug(debug_misc, "Start-of-scan at %d length %u",
					count1, jpg->scanHeader->length);

				count1 += jpg->scanHeader->length;
				break;
			}

			case JPG_DQT:
			{
				// At least one quantization table is here
				quantHeader = (jpgQuantHeader *)(imageFileData + count1 + 1);
				quantHeader->length = processorSwap16(quantHeader->length);
				kernelDebug(debug_misc, "Quanttable %d bytes at %d",
					quantHeader->length, count1);

				count2 = (count1 + 3);
				while (jpg->numQuantTables < JPG_QUANT_TABLES)
				{
					// Construct the quantization table from the quantization
					// header
					status = genQuantTable((imageFileData[count2] >> 4),
						(imageFileData[count2] & 0xF),
						&imageFileData[count2 + 1],
						&jpg->quantTable[jpg->numQuantTables]);
					if (status < 0)
						break;

					//printQuantTable(&jpg->quantTable[jpg->numQuantTables]);
					count2 += ((((imageFileData[count2] >> 4) + 1) * 64) + 1);
					jpg->numQuantTables += 1;

					if ((count2 - count1) >= quantHeader->length)
						break;
				}

				count1 += quantHeader->length;
				break;
			}

			case JPG_DRI:
			{
				// A restart header is here
				jpg->restartHeader = (jpgRestartHeader *)(imageFileData +
					count1 + 1);
				jpg->restartHeader->length =
					processorSwap16(jpg->restartHeader->length);
				jpg->restartHeader->interval =
					processorSwap16(jpg->restartHeader->interval);
				kernelDebug(debug_misc, "Restart interval %d at %d",
					jpg->restartHeader->interval, count1);

				count1 += jpg->restartHeader->length;
				break;
			}

			case JPG_SOF:
			{
				// A frame header is here
				jpg->frameHeader = (jpgFrameHeader *)(imageFileData + count1 +
					1);
				jpg->frameHeader->length =
					processorSwap16(jpg->frameHeader->length);

				if (jpg->frameHeader->precision != 8)
				{
					kernelError(kernel_error, "Only 8bpp JPEGs are supported "
						"(this is %d)", jpg->frameHeader->precision);
					status = ERR_NOTIMPLEMENTED;
					goto err_out;
				}

				jpg->frameHeader->height =
					processorSwap16(jpg->frameHeader->height);
				jpg->frameHeader->width =
					processorSwap16(jpg->frameHeader->width);

				if (jpg->frameHeader->numComps != 3)
				{
					kernelError(kernel_error, "Only 3-component JPEGs are "
						"supported");
					status = ERR_NOTIMPLEMENTED;
					goto err_out;
				}

				count1 += jpg->frameHeader->length;
				break;
			}

			case JPG_SOF1:
			case JPG_SOF2:
			case JPG_SOF3:
			case JPG_SOF5:
			case JPG_SOF6:
			case JPG_SOF7:
			case JPG_SOF9:
			case JPG_SOF10:
			case JPG_SOF11:
			case JPG_SOF13:
			case JPG_SOF14:
			case JPG_SOF15:
			{
				// All of these frame types represent unsupported compression
				// formats
				kernelError(kernel_error, "Unsupported JPEG format "
					"(SOF=%02x)", imageFileData[count1]);
				status = ERR_NOTIMPLEMENTED;
				goto err_out;
			}

			case JPG_SOI:
			{
				kernelDebug(debug_misc, "JPEG SOI at %d", count1);
				break;
			}

			case JPG_APP0:
			{
				// A JFIF header is here
				jfifHeader = (jpgJfifHeader *)(imageFileData + count1 + 1);
				jfifHeader->length = processorSwap16(jfifHeader->length);
				kernelDebug(debug_misc, "JPEG APP0 at %d length %d", count1,
					jfifHeader->length);

				// Check the version.
				if (jfifHeader->versionMajor != 0x01)
				{
					kernelError(kernel_error, "Unsupported JPEG version "
						"%02x%02x", jfifHeader->versionMajor,
						jfifHeader->versionMinor);
					status = ERR_NOTIMPLEMENTED;
				}

				count1 += jfifHeader->length;
				break;
			}

			case JPG_APP1:
			{
				// An EXIF header is here
				exifHeader = (jpgExifHeader *)(imageFileData + count1 + 1);
				exifHeader->length = processorSwap16(exifHeader->length);
				kernelDebug(debug_misc, "EXIF APP1 header at %d length %d",
					count1, exifHeader->length);

				// For the moment we ignore the EXIF data.
				count1 += exifHeader->length;
				break;
			}

			case JPG_APP2:
			case JPG_APP3:
			case JPG_APP4:
			case JPG_APP5:
			case JPG_APP6:
			case JPG_APP7:
			case JPG_APP8:
			case JPG_APP9:
			case JPG_APP10:
			case JPG_APP11:
			case JPG_APP12:
			case JPG_APP13:
			case JPG_APP14:
			case JPG_APP15:
			{
				// Some other application-specific marker we'll ignore.
				jfifHeader = (jpgJfifHeader *)(imageFileData + count1 + 1);
				jfifHeader->length = processorSwap16(jfifHeader->length);
				kernelDebug(debug_misc, "APP%d marker at %d length %d",
					(imageFileData[count1] & 0xF), count1,
					jfifHeader->length);

				count1 += jfifHeader->length;
				break;
			}

			case JPG_EOI:
			{
				kernelDebug(debug_misc, "JPEG EOI at %d", count1);
				break;
			}

			default:
			{
				// Dont care/not supported
				kernelDebug(debug_misc, "Unsupported JPEG marker %02x at %d",
					imageFileData[count1], count1);
				break;
			}
		}
	}

	// Generate default huffman tables for any that weren't defined in the
	// file.
	if (!jpg->huffTable[JPG_HUFF_DC_LUM].numCodes)
	{
		kernelDebug(debug_misc, "Generate hufftable JPG_HUFF_DC_LUM");
		genHuffTable(defaultHuffDcLumSizes, defaultHuffDcLumValues,
			&jpg->huffTable[JPG_HUFF_DC_LUM]);
	}
	if (!jpg->huffTable[JPG_HUFF_AC_LUM].numCodes)
	{
		kernelDebug(debug_misc, "Generate hufftable JPG_HUFF_AC_LUM");
		genHuffTable(defaultHuffAcLumSizes, defaultHuffAcLumValues,
			&jpg->huffTable[JPG_HUFF_AC_LUM]);
	}
	if (!jpg->huffTable[JPG_HUFF_DC_CHROM].numCodes)
	{
		kernelDebug(debug_misc, "Generate hufftable JPG_HUFF_DC_CHROM");
		genHuffTable(defaultHuffDcChromSizes, defaultHuffDcChromValues,
			&jpg->huffTable[JPG_HUFF_DC_CHROM]);
	}
	if (!jpg->huffTable[JPG_HUFF_AC_CHROM].numCodes)
	{
		kernelDebug(debug_misc, "Generate hufftable JPG_HUFF_AC_CHROM");
		genHuffTable(defaultHuffAcChromSizes, defaultHuffAcChromValues,
			&jpg->huffTable[JPG_HUFF_AC_CHROM]);
	}

	if ((jpg->numQuantTables != JPG_QUANT_TABLES) || !jpg->frameHeader)
	{
		kernelError(kernel_error, "Image table data missing");
		status = ERR_BADDATA;
		goto err_out;
	}

	// Figure out how many Y blocks, Cb blocks, and Cr blocks there will be
	// in each MCU (Minimum Coded Unit).
	for (count1 = 0; count1 < 3; count1 ++)
	{
		jpg->hvBlocksPerMcu[count1 * 2] =
			((jpg->frameHeader->comp[count1].samplingFactor & 0xF0) >> 4);
		jpg->hvBlocksPerMcu[(count1 * 2) + 1] =
			(jpg->frameHeader->comp[count1].samplingFactor & 0x0F);
		jpg->blocksPerMcu[count1] = (jpg->hvBlocksPerMcu[count1 * 2] *
			jpg->hvBlocksPerMcu[(count1 * 2) + 1]);
	}

	if ((Y_BLOCKSPERMCU > 4) || (CB_BLOCKSPERMCU > 2) ||
		(CR_BLOCKSPERMCU > 2))
	{
		kernelError(kernel_error, "Y/Cb/Cr blocks per MCU (%d/%d/%d) is not "
			"supported", Y_BLOCKSPERMCU, CB_BLOCKSPERMCU, CR_BLOCKSPERMCU);
		status = ERR_NOTIMPLEMENTED;
		goto err_out;
	}

	// If the values are the same (for example 4:4:4) then they are 1:1:1.
	if ((Y_BLOCKSPERMCU == CB_BLOCKSPERMCU) &&
		(CB_BLOCKSPERMCU == CR_BLOCKSPERMCU))
	{
		for (count1 = 0; count1 < 6; count1 ++)
			jpg->hvBlocksPerMcu[count1] = 1;
		for (count1 = 0; count1 < 3; count1 ++)
			jpg->blocksPerMcu[count1] = 1;
	}

	for (count1 = 0; count1 < jpg->frameHeader->numComps; count1 ++)
	{
		kernelDebug(debug_misc, "Frame comp %d id=%d sampFact=%dx%d "
			"quantTab=%d", count1, jpg->frameHeader->comp[count1].compId,
			jpg->hvBlocksPerMcu[count1 * 2],
			jpg->hvBlocksPerMcu[(count1 * 2) + 1],
			jpg->frameHeader->comp[count1].quantTable);
	}

	// Figure out how much memory we need for the array of pixels that we'll
	// attach to the image, and allocate it.  The size is a product of the
	// image height and width.
	loadImage->pixels = (jpg->frameHeader->width * jpg->frameHeader->height);
	loadImage->dataLength = (loadImage->pixels * sizeof(pixel));

	imageData = kernelMemoryGet(loadImage->dataLength, "image data");
	if (!imageData)
	{
		status = ERR_MEMORY;
		goto err_out;
	}

	kernelDebug(debug_misc, "Jpeg image %dx%d", jpg->frameHeader->width,
		jpg->frameHeader->height);

	// Decode the image scan data
	status = decode(jpg, imageData);

	// Set the image's info fields
	loadImage->width = jpg->frameHeader->width;
	loadImage->height = jpg->frameHeader->height;

	// Assign the image data to the image
	loadImage->data = imageData;

	// Release memory
	if (jpg)
		kernelFree(jpg);

	// Return the status of the 'decode' call
	return (status);

err_out:

	// Release memory
	if (jpg)
		kernelFree(jpg);
	if (imageData)
		kernelMemoryRelease(imageData);

	return (status);
}


kernelFileClass jpgFileClass = {
	FILECLASS_NAME_JPG,
	&detect,
	{}
};


/////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////
//
// Below here, the functions are exported for external use
//
/////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////

kernelFileClass *kernelFileClassJpg(void)
{
	// The loader will call this function so that we can return a structure
	// for managing JPEG files

	static int filled = 0;

	if (!filled)
	{
		jpgFileClass.image.load = &load;
		filled = 1;
	}

	return (&jpgFileClass);
}

