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
//  jpg.h
//

// This defines things for manipulating JPG image files

#if !defined(_JPG_H)

// Constants

// SOF (Start Of Frame) markers.
#define JPG_SOF					0xC0  // Baseline DCT
// The rest are unused except to recognise unsupported formats.
#define JPG_SOF1				0xC1  // Etended sequential DCT
#define JPG_SOF2				0xC2  // Progressive DCT
#define JPG_SOF3				0xC3  // Lossless (sequential)
#define JPG_SOF5				0xC5  // Diff. sequential DCT
#define JPG_SOF6				0xC6  // Diff. progressive DCT
#define JPG_SOF7				0xC7  // Diff. lossless (sequential)
#define JPG_SOF8				0xC8  // Reserved
#define JPG_SOF9				0xC9  // Extended sequential DCT (arith)
#define JPG_SOF10				0xCA  // Progressive DCT (arith)
#define JPG_SOF11				0xCB  // Lossless (sequential arith)
#define JPG_SOF13				0xCD  // Diff. sequential DCT (arith)
#define JPG_SOF14				0xCE  // Diff. progressive DCT (arith)
#define JPG_SOF15				0xCF  // Diff. lossless (sequential arith)
// End unsupported SOFs.  Other markers:
#define JPG_DHT					0xC4
#define JPG_RST0				0xD0
#define JPG_RST7				0xD7
#define JPG_SOI					0xD8
#define JPG_EOI					0xD9
#define JPG_SOS					0xDA
#define JPG_DQT					0xDB
#define JPG_DRI					0xDD
#define JPG_APP0				0xE0
#define JPG_APP1				0xE1
#define JPG_APP2				0xE2
#define JPG_APP3				0xE3
#define JPG_APP4				0xE4
#define JPG_APP5				0xE5
#define JPG_APP6				0xE6
#define JPG_APP7				0xE7
#define JPG_APP8				0xE8
#define JPG_APP9				0xE9
#define JPG_APP10				0xEA
#define JPG_APP11				0xEB
#define JPG_APP12				0xEC
#define JPG_APP13				0xED
#define JPG_APP14				0xEE
#define JPG_APP15				0xEF
#define JPG_COM					0xFE

#define JFIF_START				((char[]){ 0xFF, JPG_SOI, 0xFF, JPG_APP0 })
#define JFIF_MAGIC				"JFIF"
#define EXIF_START				((char[]){ 0xFF, JPG_SOI, 0xFF, JPG_APP1 })
#define EXIF_MAGIC				"Exif"

#define JPG_UNITS_NONE			0
#define JPG_UNITS_DPI			1
#define JPG_UNITS_DPCM			2

#define JPG_HUFF_TABLES			4
#define JPG_HUFF_VALUES			256
#define JPG_QUANT_TABLES		2

// The 4 Huffman table types and their order in our our array
#define JPG_HUFF_DC_LUM_ID		0x00 // DC of luminance (Y)
#define JPG_HUFF_AC_LUM_ID		0x10 // AC of luminance (Y)
#define JPG_HUFF_DC_CHROM_ID	0x01 // DC of chrominance (Cb, Cr)
#define JPG_HUFF_AC_CHROM_ID	0x11 // AC of chrominance (Cb, Cr)
#define JPG_HUFF_DC_LUM			0
#define JPG_HUFF_AC_LUM			1
#define JPG_HUFF_DC_CHROM		2
#define JPG_HUFF_AC_CHROM		3

// The .jpg APP0 header

// This is the on-disk JFIF file header
typedef struct {
	unsigned short length;
	char identifier[5];
	unsigned char versionMajor;
	unsigned char versionMinor;
	unsigned char units;
	unsigned short densityX;
	unsigned short densityY;
	unsigned char thumbX;
	unsigned char thumbY;
	unsigned char thumbData[];

} __attribute__((packed)) jpgJfifHeader;

// This is the on-disk EXIF file header
typedef struct {
	unsigned short length;
	char identifier[5];
	unsigned char junk[];

} __attribute__((packed)) jpgExifHeader;

// This is the on-disk Huffman table header
typedef struct {
	unsigned short length;
	struct {
		unsigned char classIdent;
		unsigned char sizes[16];
		unsigned char values[JPG_HUFF_VALUES];
	} table[];

} __attribute__((packed)) jpgHuffHeader;

// This is the on-disk quantization table
typedef struct {
	unsigned short length;
	unsigned char precisionIdent;
	unsigned char values[];

} __attribute__((packed)) jpgQuantHeader;

// This is the on-disk restart interval header
typedef struct {
	unsigned short length;
	unsigned short interval;

} __attribute__((packed)) jpgRestartHeader;

// This is the on-disk frame (image data) header
typedef struct {
	unsigned short length;
	unsigned char precision;
	unsigned short height;
	unsigned short width;
	unsigned char numComps;

	struct {
		unsigned char compId;
		unsigned char samplingFactor;
		unsigned char quantTable;
	} comp[4]; // Up to 4, most often 3 (Y, Cb, Cr)

} __attribute__((packed)) jpgFrameHeader;

typedef struct {
	unsigned short length;
	unsigned char numComps;
	struct {
		unsigned char compId;
		unsigned char huffTable;
	} comp[];

} __attribute__((packed)) jpgScanHeader;

#define _JPG_H
#endif

