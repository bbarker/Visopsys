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
//  vbf.h
//

#if !defined(_VBF_H)

#include <sys/font.h>

#define VBF_MAGIC			"VBF"
#define VBF_MAGIC_LEN		4
#define VBF_VERSION1		0x00010000
#define VBF_VERSION2		0x00020000
#define VBF_NAME_LEN		32
#define VBF_CHARSET_LEN		16
#define VBF_FAMILY_LEN		32

// Older (version 1) header
typedef struct {
	char magic[VBF_MAGIC_LEN];		// VBF_MAGIC
	int version;					// VBF_VERSION (bcd VBF_VERSION1)
	char name[VBF_NAME_LEN];		// Font name
	int points;						// Size in points (e.g. 10, 12, 20)
	char charSet[VBF_CHARSET_LEN];	// e.g. ISO-8859-15
	int numGlyphs;					// Number of glyphs in file
	int glyphWidth;					// Fixed width of all glyphs
	int glyphHeight;				// Fixed height of all glyphs
	int codes[];					// List of codepage values
	// unsigned char data[];		// Bitmap follows codes.  Each glyph is
	// padded to a byte boundary, so the size of the bitmap is:
	// numGlyphs * (((glyphWidth * glyphHeight) + 7) / 8)

} __attribute__((packed)) vbfFileHeaderV1;

// Current (version 2) header
typedef struct {
	char magic[VBF_MAGIC_LEN];		// VBF_MAGIC
	unsigned version;				// VBF_VERSION (bcd VBF_VERSION2)
	char family[VBF_FAMILY_LEN];	// Font family (e.g. arial, courier, ...)
	unsigned flags;					// See FONT_STYLEFLAG_* in <sys/font.h>
	int points;						// Size in points (e.g. 10, 12, 20)
	char charSet[VBF_CHARSET_LEN];	// e.g. ASCII, ISO-8859-15, etc.
	int numGlyphs;					// Number of glyphs in file
	int glyphWidth;					// Fixed width of all glyphs
	int glyphHeight;				// Fixed height of all glyphs
	unsigned codes[];				// List of Unicode values
	// unsigned char data[];		// Bitmap follows codes.  Each glyph is
	// padded to a byte boundary, so the size of the bitmap is:
	// numGlyphs * (((glyphWidth * glyphHeight) + 7) / 8)

} __attribute__((packed)) vbfFileHeader;

// For safe version determination
typedef union {
	struct {
		char magic[4];
		unsigned version;
	} common;
	vbfFileHeaderV1 v1;
	vbfFileHeader v2;

} __attribute__((packed)) vbfMultiVerHeader;

#define _VBF_H
#endif

