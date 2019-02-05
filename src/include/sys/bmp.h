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
//  bmp.h
//

// This defines things used by kernelImageBmp.c for manipulating windows/os2
// format bitmap files

#if !defined(_BMP_H)

// Constants

#define BMP_MAGIC		"BM"

#define BMP_BPP_MONO	1
#define BMP_BPP_16		4
#define BMP_BPP_256		8
#define BMP_BPP_16BIT	16
#define BMP_BPP_24BIT	24
#define BMP_BPP_32BIT	32

#define BMP_COMP_NONE	0
#define BMP_COMP_RLE8	1
#define BMP_COMP_RLE4	2
#define BMP_COMP_BITF	3

// The .bmp header

typedef struct {
	unsigned size;
	unsigned reserved;
	unsigned dataStart;
	unsigned headerSize;
	unsigned width;
	unsigned height;
	short planes;
	short bitsPerPixel;
	unsigned compression;
	unsigned dataSize;
	unsigned hResolution;
	unsigned vResolution;
	unsigned colors;
	unsigned importantColors;

} bmpHeader;

#define _BMP_H
#endif

