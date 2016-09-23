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
//  kernelFontTtf.h
//

// This defines things used by kernelFontTtf.c for manipulating TTF format
// font files

#if !defined(_KERNELFONTTTF_H)

#include <sys/types.h>

// Constants

// These magic numbers aren't really magic.  They are a couple of the
// acceptable values for the 'scalerType' field in the offset subtable.
#define TTF_MAGIC1				0x65757274	// 'true' (big-endian)
#define TTF_MAGIC2				0x31707974	// 'typ1'

#define TTF_TABLETAG_HEAD		0x64616568	// 'head' (big-endian)
#define TTF_TABLETAG_BHED		0x64656862	// 'bhed'
#define TTF_TABLETAG_MAXP		0x7078616D	// 'maxp'
#define TTF_TABLETAG_CMAP		0x70616D63	// 'cmap'
#define TTF_TABLETAG_EBDT		0x54444245	// 'EBDT'
#define TTF_TABLETAG_EBLC		0x434C4245	// 'EBLC'

// Types for the index subtables
#define TTF_IDXSUBFMT_VM4BYTE	1
#define TTF_IDXSUBFMT_CM		2
#define TTF_IDXSUBFMT_VM2BYTE	3
#define TTF_IDXSUBFMT_VMSPGC	4
#define TTF_IDXSUBFMT_CMSPGC	5

// Types of bitmap image formats
#define TTF_IDXIMGFMT_SMBYTE	1
#define TTF_IDXIMGFMT_SMBIT		2
#define TTF_IDXIMGFMT_NMBIT		5
#define TTF_IDXIMGFMT_BMBYTE	6
#define TTF_IDXIMGFMT_BMBIT		7


// Structures

typedef struct {
	unsigned scalerType;
	unsigned short numTables;
	unsigned short searchRange;
	unsigned short entrySelector;
	unsigned short rangeShift;

} __attribute__((packed)) ttfOffsetSubtable;

typedef struct {
	unsigned tag;
	unsigned checkSum;
	unsigned offset;
	unsigned length;

} __attribute__((packed)) ttfTableDirEntry;

typedef struct {
	unsigned version;
	unsigned revision;
	unsigned checkSumAdj;
	unsigned magic;
	unsigned short flags;
	unsigned short unitsPerEm;
	quad_t created;
	quad_t modified;
	short xMin;
	short yMin;
	short xMax;
	short yMax;
	unsigned short macStyle;
	unsigned short lowestRecSize;
	short directionHint;
	short indexToLocFormat;
	short glyphDataFormat;

} __attribute__((packed)) ttfHeadTable;

typedef struct {
	unsigned version;
	unsigned short numGlyphs;
	unsigned short maxPoints;
	unsigned short maxContours;
	unsigned short maxCompPoints;
	unsigned short maxCompConts;
	unsigned short maxZones;
	unsigned short maxTwiPoints;
	unsigned short maxStorage;
	unsigned short maxFuncDefs;
	unsigned short maxInstDefs;
	unsigned short maxStackElems;
	unsigned short maxInstSize;
	unsigned short maxCompElems;
	unsigned short maxCompDepth;

} __attribute__((packed)) ttfMaxpTable;

typedef struct {
	unsigned short version;
	unsigned short numTables;
	struct {
		unsigned short platformId;
		unsigned short encodingId;
		unsigned offset;
	} encodingTables[];

} __attribute__((packed)) ttfCmapTable;

typedef struct {
	unsigned version;
	unsigned char data[];

} __attribute__((packed)) ttfEbdtTable;

typedef struct {
	char ascender;
	char descender;
	unsigned char widthMax;
	char caretSlopeNum;
	char caretSlopeDen;
	char caretOffset;
	char minOriginSB;
	char minAdvanceSB;
	char maxBeforeBL;
	char minAfterBL;
	char pad[2];

}  __attribute__((packed)) ttfSbitLineMetrics;

typedef struct {
	unsigned indexSubTableOffset;
	unsigned indexTablesSize;
	unsigned numIndexSubTables;
	unsigned colorRef;
	ttfSbitLineMetrics hori;
	ttfSbitLineMetrics vert;
	unsigned short startGlyphIndex;
	unsigned short endGlyphIndex;
	unsigned char ppemX;
	unsigned char ppemY;
	unsigned char bitDepth;
	char flags;

} __attribute__((packed)) ttfSizeTable;

typedef struct {
	unsigned version;
	unsigned numSizes;
	ttfSizeTable sizeTables[];

} __attribute__((packed)) ttfEblcTable;

typedef struct {
	unsigned short firstGlyphIndex;
	unsigned short lastGlyphIndex;
	unsigned offset;

} __attribute__((packed)) ttfIndexSubTableArrayElement;

typedef struct {
	unsigned short indexFormat;
	unsigned short imageFormat;
	unsigned imageDataOffset;

} __attribute__((packed)) ttfIndexSubTableHeader;

typedef struct {
	unsigned char height;
	unsigned char width;
	char horiBearingX;
	char horiBearingY;
	unsigned char horiAdvance;
	char vertBearingX;
	char vertBearingY;
	unsigned char vertAdvance;

} __attribute__((packed)) ttfBigGlyphMetrics;

typedef struct {
	unsigned short glyphCode;
	unsigned short offset;

} __attribute__((packed)) ttfCodeOffsetPair;

// Variable metrics glyphs with 4 byte offsets
typedef struct {
	ttfIndexSubTableHeader header;
	unsigned offsetArray[];

} __attribute__((packed)) ttfIndexSubTable1;

// All glyphs have identical metrics
typedef struct {
	ttfIndexSubTableHeader header;
	unsigned imageSize;
	ttfBigGlyphMetrics bigMetrics;

} __attribute__((packed)) ttfIndexSubTable2;

// Variable metrics glyphs with 2 byte offsets
typedef struct {
	ttfIndexSubTableHeader header;
	unsigned short offsetArray[];

} __attribute__((packed)) ttfIndexSubTable3;

// Variable metrics glyphs with sparse glyph codes
typedef struct {
	ttfIndexSubTableHeader header;
	unsigned numGlyphs;
	ttfCodeOffsetPair glyphArray[];

} __attribute__((packed)) ttfIndexSubTable4;

// Constant metrics glyphs with sparse glyph codes
typedef struct {
	ttfIndexSubTableHeader header;
	unsigned imageSize;
	ttfBigGlyphMetrics bigMetrics;
	unsigned numGlyphs;
	unsigned short glyphCodeArray[];

} __attribute__((packed)) ttfIndexSubTable5;

typedef struct {
	int firstGlyphIndex;
	int lastGlyphIndex;
	int indexFormat;
	int imageFormat;
	unsigned imageDataOffset;
	void *data;

} ttfIndexSubTable;

typedef struct {
	int numGlyphs;
	int maxPoints;
	int charWidth;
	int charHeight;
	int charBytes;
	int numIndexSubTables;
	ttfIndexSubTable *indexSubTables;
	unsigned char *bitmapData;

} ttfFont;

#define _KERNELFONTTTF_H
#endif

