//
//  Visopsys
//  Copyright (C) 1998-2016 J. Andrew McLaughlin
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
//  ico.h
//

// This defines things for manipulating windows .ico format icon files.
// Reference: http://www.daubnet.com/en/file-format-ico

#if !defined(_ICO_H)

typedef struct {
	unsigned headerSize;		// Size of icoInfoHeader = 40
	unsigned width;				// Icon width
	unsigned height;			// Icon height (XOR-bitmap and AND-bitmap)
	short planes;				// Number of planes = 1
	short bitsPerPixel;			// Bits per pixel (1, 4, 8, 24)
	unsigned compression;		// Type of compression = 0
	unsigned dataSize;			// Size of image in bytes = 0 (uncompressed)
	unsigned hResolution;		// unused = 0
	unsigned vResolution;		// unused = 0
	unsigned colors;			// unused = 0
	unsigned importantColors;	// unused = 0

} __attribute__((packed)) icoInfoHeader;

typedef struct {
	unsigned char width;		// (16, 32 or 64)
	unsigned char height;		// (16, 32 or 64.  Most commonly = width)
	unsigned char colorCount;	// Number of colors (2, 16, 0=256)
	unsigned char reserved;		// = 0
	unsigned short planes;		// = 1
	unsigned short bitCount;	// Bits per pixel (1, 4, 8, 24)
	unsigned size;				// icoInfoHeader + ANDbitmap + XORbitmap
	unsigned fileOffset;		// Where icoInfoHeader starts

} __attribute__((packed)) icoEntry;

typedef struct {
	unsigned short reserved;	// = 0
	unsigned short type;		// = 1
	unsigned short numIcons;	// Number of icons in this file
	icoEntry entries[];			// List of icons

} __attribute__((packed)) icoHeader;

#define _ICO_H
#endif

