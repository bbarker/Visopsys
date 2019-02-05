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
//  kernelFont.h
//

#if !defined(_KERNELFONT_H)

#include <sys/charset.h>
#include <sys/font.h>

#define FONTS_MAX			32
#define FONT_FAMILY_LEN		32
#define FONT_MAX_CHARSETS	16
#define FONT_CHARSET_LEN	16

typedef struct {
	unsigned unicode;
	image img;

} kernelGlyph;

typedef struct {
	char family[FONT_FAMILY_LEN];		// Font family (e.g. arial, xterm, ...)
	unsigned flags;						// See FONT_STYLEFLAG_* in <sys/font.h>
	int points;							// Size in points (e.g. 10, 12, 20)
	int numCharSets;					// Number of character sets loaded
	char charSet[FONT_MAX_CHARSETS]		// e.g. ASCII, ISO-8859-15, etc.
		[FONT_CHARSET_LEN];
	int numGlyphs;						// Number of glyphs in file
	int glyphWidth;						// Fixed width of all glyphs
	int glyphHeight;					// Fixed height of all glyphs
	kernelGlyph *glyphs;

} kernelFont;

// Functions exported from kernelFont.c
int kernelFontInitialize(void);
int kernelFontGetSystem(kernelFont **);
int kernelFontHasCharSet(kernelFont *, const char *);
kernelFont *kernelFontGet(const char *, unsigned, int, const char *);
int kernelFontGetPrintedWidth(kernelFont *, const char *, const char *);
int kernelFontGetWidth(kernelFont *);
int kernelFontGetHeight(kernelFont *);

#define _KERNELFONT_H
#endif

