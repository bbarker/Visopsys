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
//  kernelFontVbf.c
//

// This file contains code for loading, saving, and converting fonts
// in the Visopsys Bitmap Font (.vbf) format.  VBF is a very simple,
// proprietary format that allows for simple bitmapped fonts in a 'sparse'
// list (i.e. the list of glyph codes can contain as many or as few entries
// as desired, in any order, etc.).  Existing popular bitmap formats are much
// more rigid and complicated, don't allow for sparseness, and thus aren't
// amenable to our usual disk-space stinginess.

#include "kernelFontVbf.h"
#include "kernelDebug.h"
#include "kernelFile.h"
#include "kernelFont.h"
#include "kernelError.h"
#include "kernelLoader.h"
#include "kernelMalloc.h"
#include "kernelMemory.h"
#include <stdio.h>
#include <string.h>


static int readHeader(const char *fileName, vbfFileHeader *vbfHeader)
{
	// Read the header of a VBF file

	int status = 0;
	file theFile;
	vbfMultiVerHeader *multiHeader = NULL;

	kernelDebug(debug_font, "VBF read %s header", fileName);

	// Initialize the file structure we're going to use
	memset(&theFile, 0, sizeof(file));

	status = kernelFileOpen(fileName, OPENMODE_READ, &theFile);
	if (status < 0)
		return (status);

	multiHeader = kernelMalloc(theFile.blockSize);
	if (!multiHeader)
	{
		kernelFileClose(&theFile);
		return (status = ERR_MEMORY);
	}

	status = kernelFileRead(&theFile, 0 /* block */, 1 /* blocks */,
		multiHeader);

	kernelFileClose(&theFile);

	if (status < 0)
	{
		kernelFree(multiHeader);
		return (status);
	}

	if (strncmp(multiHeader->common.magic, VBF_MAGIC, VBF_MAGIC_LEN))
	{
		kernelDebugError("VBF signature not found");
		kernelFree(multiHeader);
		return (status = ERR_INVALID);
	}

	if (multiHeader->common.version == VBF_VERSION2)
	{
		kernelDebug(debug_font, "VBF version 2");
		memcpy(vbfHeader, multiHeader, sizeof(vbfFileHeader));
		kernelFree(multiHeader);
		return (status = 0);
	}
	else
	{
		kernelError(kernel_error, "Unsupported VBF version %d.%d",
			(multiHeader->common.version >> 16),
			(multiHeader->common.version & 0xFFFF));
		kernelFree(multiHeader);
		return (status = ERR_NOTIMPLEMENTED);
	}
}


/////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////
//
// Standard font driver functions
//
/////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////

static int detect(const char *fileName, void *dataPtr, unsigned size,
	loaderFileClass *class)
{
	// This function returns 1 and fills the fileClass structure if the data
	// points to a VBF file.

	vbfMultiVerHeader *vbfHeader = dataPtr;

	// Check params
	if (!fileName || !dataPtr || !class)
		return (0);

	// Make sure there's enough data here for our detection
	if ((size < sizeof(vbfFileHeaderV1)) && (size < sizeof(vbfFileHeader)))
		return (0);

	// See whether this file claims to be a VBF file.
	if (!strncmp(vbfHeader->common.magic, VBF_MAGIC, VBF_MAGIC_LEN))
	{
		// We'll accept that.
		sprintf(class->name, "%s %s", FILECLASS_NAME_VBF,
			FILECLASS_NAME_FONT);
		class->type = (LOADERFILECLASS_BIN | LOADERFILECLASS_FONT);
		class->subType = LOADERFILESUBCLASS_VBF;
		return (1);
	}
	else
	{
		return (0);
	}
}


static int getInfo(const char *fileName, kernelFont *font)
{
	int status = 0;
	vbfFileHeader vbfHeader;

	status = readHeader(fileName, &vbfHeader);
	if (status < 0)
		return (status);

	memset(font, 0, sizeof(kernelFont));
	strncpy(font->family, vbfHeader.family, FONT_FAMILY_LEN);
	font->flags = vbfHeader.flags;
	font->points = vbfHeader.points;
	font->numCharSets = 1;
	strncpy(font->charSet[0], vbfHeader.charSet, FONT_CHARSET_LEN);

	return (status = 0);
}


static int load(unsigned char *fileData, int dataLength, kernelFont *font,
	int fixedWidth)
{
	// Loads a VBF file and adds its data to a font.  The memory for this and
	// its data must be freed by the caller.

	int status = 0;
	vbfFileHeader *vbfHeader = (vbfFileHeader *) fileData;
	int glyphBytes = 0;
	unsigned char *fontData = NULL;
	kernelGlyph *glyph = NULL;
	unsigned char *glyphData = 0;
	int firstOnPixel = 0, lastOnPixel = 0, currentPixel = 0;
	int count1, count2, count3;

	kernelDebug(debug_font, "VBF load");

	// Check params
	if (!fileData || !dataLength || !font)
		return (status = ERR_NULLPARAMETER);

	// Copy/add the basic font info
	strncpy(font->family, vbfHeader->family, FONT_FAMILY_LEN);
	font->flags = vbfHeader->flags;
	font->points = vbfHeader->points;
	strncpy(font->charSet[font->numCharSets++], vbfHeader->charSet,
		FONT_CHARSET_LEN);
	font->glyphWidth = vbfHeader->glyphWidth;
	font->glyphHeight = vbfHeader->glyphHeight;

	// How many bytes per glyph?
	glyphBytes = (((vbfHeader->glyphWidth * vbfHeader->glyphHeight) + 7) / 8);

	kernelDebug(debug_font, "VBF font %s flags=%02x points=%d charset=%s "
		"glyphWidth=%d glyphHeight=%d", font->family, font->flags,
		font->points, font->charSet[font->numCharSets - 1], font->glyphWidth,
		font->glyphHeight);

	// Get memory for the font structure and the images data.
	font->glyphs = kernelRealloc(font->glyphs,
		((font->numGlyphs + vbfHeader->numGlyphs) * sizeof(kernelGlyph)));
	fontData = kernelMalloc(glyphBytes * vbfHeader->numGlyphs);

	if (!font->glyphs || !fontData)
	{
		kernelError(kernel_error, "Unable to get memory to hold the font "
			"data");
		return (status = ERR_MEMORY);
	}

	// Copy the bitmap data directory from the file into the font memory
	memcpy(fontData, &vbfHeader->codes[vbfHeader->numGlyphs],
		(glyphBytes * vbfHeader->numGlyphs));

	// Loop through the all the images
	for (count1 = 0; count1 < vbfHeader->numGlyphs; count1 ++)
	{
		glyph = &font->glyphs[font->numGlyphs + count1];

		glyph->unicode = vbfHeader->codes[count1];

		// Stuff that won't change in the rest of the code for this character,
		// below (things like width can change -- see below)
		glyph->img.type = IMAGETYPE_MONO;
		glyph->img.width = font->glyphWidth;
		glyph->img.height = font->glyphHeight;
		glyph->img.pixels = (font->glyphWidth * font->glyphHeight);
		glyph->img.dataLength = glyphBytes;
		glyph->img.data = (fontData + (count1 * glyphBytes));

		// If a variable-width font has been requested, then we need to do some
		// bit-bashing to remove surplus space on either side of each
		// character.
		if (!fixedWidth)
		{
			glyphData = glyph->img.data;

			// These allow us to keep track of the leftmost and rightmost 'on'
			// pixels for this character.  We can use these for narrowing the
			// image if we want a variable-width font
			firstOnPixel = (font->glyphWidth - 1);
			lastOnPixel = 0;

			for (count2 = 0; count2 < font->glyphHeight; count2 ++)
			{
				// Find the first-on pixel
				for (count3 = 0; count3 < firstOnPixel; count3 ++)
				{
					if (glyphData[((count2 * font->glyphWidth) + count3) / 8] &
						(0x80 >> (((count2 * font->glyphWidth) + count3) % 8)))
					{
						firstOnPixel = count3;
						break;
					}
				}

				// Find the last-on pixel
				for (count3 = (font->glyphWidth - 1); count3 > lastOnPixel;
					count3 --)
				{
					if (glyphData[((count2 * font->glyphWidth) + count3) / 8] &
						(0x80 >> (((count2 * font->glyphWidth) + count3) % 8)))
					{
						lastOnPixel = count3;
						break;
					}
				}
			}

			// For variable-width fonts, we want no empty columns before the
			// character data, and only one after.  Make sure we don't get
			// buggered up by anything with no 'on' pixels such as the space
			// character

			if ((firstOnPixel > 0) || (lastOnPixel < (font->glyphWidth - 2)))
			{
				if (firstOnPixel > lastOnPixel)
				{
					// This has no pixels.  Probably a space character.  Give
					// it a width of approximately 1/5th the char width
					firstOnPixel = 0;
					lastOnPixel = ((font->glyphWidth / 5) - 1);
				}

				// We will strip bits from each row of the character image.
				// This is the little bit of bit bashing.  The count2 counter
				// counts through all of the bits.  The count3 one only counts
				// bits that aren't being skipped, and sets/clears them.

				for (count2 = 0, count3 = 0;
					count2 < (font->glyphWidth * font->glyphHeight); count2 ++)
				{
					currentPixel = (count2 % font->glyphWidth);
					if ((currentPixel < firstOnPixel) ||
						(currentPixel > (lastOnPixel + 1)))
					{
						// Skip this pixel.  It's from a column we're deleting.
						continue;
					}

					if (glyphData[count2 / 8] & (0x80 >> (count2 % 8)))
					{
						// The bit is on
						glyphData[count3 / 8] |= (0x80 >> (count3 % 8));
					}
					else
					{
						// The bit is off
						glyphData[count3 / 8] &= ~(0x80 >> (count3 % 8));
					}

					count3++;
				}

				// Adjust the character image information
				glyph->img.width -= (firstOnPixel + (((font->glyphWidth - 2) -
					lastOnPixel)));
				glyph->img.pixels = (glyph->img.width * font->glyphHeight);
			}
		}
	}

	font->numGlyphs += vbfHeader->numGlyphs;
	return (status = 0);
}


kernelFileClass vbfFileClass = {
	FILECLASS_NAME_VBF,
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

kernelFileClass *kernelFileClassVbf(void)
{
	// The loader will call this function so that we can return a structure
	// for managing VBF files

	static int filled = 0;

	if (!filled)
	{
		vbfFileClass.font.getInfo = &getInfo;
		vbfFileClass.font.load = &load;
		filled = 1;
	}

	return (&vbfFileClass);
}

