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
//  kernelImageJpg.h
//

// This defines things used by kernelImageJpg.c for manipulating JPEG format
// image files

#if !defined(_KERNELIMAGEJPG_H)

#include <sys/jpg.h>

// This pairs a Huffman code with a value from the on-disk Huffman table
typedef struct {
	unsigned short code;
	char value;

} jpgHuffCode;

// This is a moderately efficient way of storing information from the on-disk
// Huffman tables.
typedef struct {
	int numCodes;
	unsigned char sizes[16];
	jpgHuffCode *sizedCodes[16];
	jpgHuffCode huffCodes[JPG_HUFF_VALUES];

} jpgHuffTable;

// The data from a quantization table
typedef struct {
	int precision;
	int ident;
	union {
		unsigned char val8[64];
		unsigned short val16[64];
	} values;

} jpgQuantTable;

// This contains all our metadata for working with JPEGs.
typedef struct {
	jpgHuffTable huffTable[JPG_HUFF_TABLES];
	jpgQuantTable quantTable[JPG_QUANT_TABLES];
	int numQuantTables;
	jpgFrameHeader *frameHeader;
	jpgScanHeader *scanHeader;
	jpgRestartHeader *restartHeader;
	unsigned char *dataPointer;
	unsigned bitPosition;
	short yDcValue;
	short cbDcValue;
	short crDcValue;
	int hvBlocksPerMcu[6];
	int blocksPerMcu[3];

} jpgData;

#define _KERNELIMAGEJPG_H
#endif

