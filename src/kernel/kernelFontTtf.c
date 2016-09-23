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
//  kernelFontTtf.c
//

// This file contains code for loading, saving, and converting fonts
// in the "True Type" (.ttf) format, one of the most common font formats
// used by Windows, Linux, and other operating systems.

#include "kernelFontTtf.h"
#include "kernelDebug.h"
#include "kernelError.h"
#include "kernelLoader.h"
#include "kernelMalloc.h"
#include "kernelMisc.h"
#include <stdio.h>
#include <stdlib.h>
#include <sys/processor.h>


/*
#ifdef DEBUG
static inline void debugEBLC(ttfEblcTable *table)
{
	unsigned count;

	kernelDebug(debug_misc, "Debug EBLC table:\n"
		"  version=%04x\n"
		"  numSizes=%d\n", processorSwap32(table->version),
		processorSwap32(table->numSizes));

	for (count = 0; count < processorSwap32(table->numSizes); count ++)
	{
		kernelDebug(debug_misc, "Debug EBLC size table %d:\n"
			"  indexSubTableOffset=%d\n"
			"  indexTablesSize=%d\n"
			"  numIndexSubTables=%d\n"
			"  colorRef=%d\n"
			"  hori.ascender=%d\n"
			"  hori.descender=%d\n"
			"  hori.widthMax=%d\n"
			"  hori.caretSlopeNum=%d\n"
			"  hori.caretSlopeDen=%d\n"
			"  hori.caretOffset=%d\n"
			"  hori.minOriginSB=%d\n"
			"  hori.minAdvanceSB=%d\n"
			"  hori.maxBeforeBL=%d\n"
			"  hori.minAfterBL=%d\n"
			"  vert.ascender=%d\n"
			"  vert.descender=%d\n"
			"  vert.widthMax=%d\n"
			"  vert.caretSlopeNum=%d\n"
			"  vert.caretSlopeDen=%d\n"
			"  vert.caretOffset=%d\n"
			"  vert.minOriginSB=%d\n"
			"  vert.minAdvanceSB=%d\n"
			"  vert.maxBeforeBL=%d\n"
			"  vert.minAfterBL=%d\n"
			"  startGlyphIndex=%d\n"
			"  endGlyphIndex=%d\n"
			"  ppemX=%d\n"
			"  ppemY=%d\n"
			"  bitDepth=%d\n"
			"  flags=%02x\n",
			count,
			processorSwap32(table->sizeTables[count].indexSubTableOffset),
			processorSwap32(table->sizeTables[count].indexTablesSize),
			processorSwap32(table->sizeTables[count].numIndexSubTables),
			processorSwap32(table->sizeTables[count].colorRef),
			table->sizeTables[count].hori.ascender,
			table->sizeTables[count].hori.descender,
			table->sizeTables[count].hori.widthMax,
			table->sizeTables[count].hori.caretSlopeNum,
			table->sizeTables[count].hori.caretSlopeDen,
			table->sizeTables[count].hori.caretOffset,
			table->sizeTables[count].hori.minOriginSB,
			table->sizeTables[count].hori.minAdvanceSB,
			table->sizeTables[count].hori.maxBeforeBL,
			table->sizeTables[count].hori.minAfterBL,
			table->sizeTables[count].vert.ascender,
			table->sizeTables[count].vert.descender,
			table->sizeTables[count].vert.widthMax,
			table->sizeTables[count].vert.caretSlopeNum,
			table->sizeTables[count].vert.caretSlopeDen,
			table->sizeTables[count].vert.caretOffset,
			table->sizeTables[count].vert.minOriginSB,
			table->sizeTables[count].vert.minAdvanceSB,
			table->sizeTables[count].vert.maxBeforeBL,
			table->sizeTables[count].vert.minAfterBL,
			processorSwap16(table->sizeTables[count].startGlyphIndex),
			processorSwap16(table->sizeTables[count].endGlyphIndex),
			table->sizeTables[count].ppemX,
			table->sizeTables[count].ppemY,
			table->sizeTables[count].bitDepth,
			table->sizeTables[count].flags);
	}
}

static inline void debugIndexSubTable(ttfIndexSubTable *subTable)
{
	kernelDebug(debug_misc, "Debug index subtable:\n"
		"  firstGlyphIndex=%d\n"
		"  lastGlyphIndex=%d\n"
		"  indexFormat=%d\n"
		"  imageFormat=%d\n"
		"  imageDataOffset=%d\n"
		"  data=%p\n",
		subTable->firstGlyphIndex,
		subTable->lastGlyphIndex,
		subTable->indexFormat,
		subTable->imageFormat,
		subTable->imageDataOffset,
		subTable->data);
}
#endif // DEBUG
*/


static inline ttfTableDirEntry *findTableDirEntry(ttfTableDirEntry *array,
	unsigned short numEntries, unsigned tag)
{
	// Given an array of table directory entries, the number of entries, and
	// the desired tag,return a pointer to the entry.

	unsigned short count;

	for (count = 0; count < numEntries; count ++)
		if (array[count].tag == tag)
			return (&array[count]);

	return (NULL);
}


static int detect(const char *fileName, void *dataPtr, unsigned size,
	loaderFileClass *class)
{
	// This function returns 1 and fills the fileClass structure if the data
	// points to an TTF file.

	ttfOffsetSubtable *offSub = dataPtr;
	ttfTableDirEntry *tableDir = (dataPtr + sizeof(ttfOffsetSubtable));
	unsigned short tableEntries = 0;

	// Check params
	if (!fileName || !dataPtr || !class)
		return (0);

	// Make sure there's enough data here for our detection
	if (size < sizeof(ttfOffsetSubtable))
		return (0);

	// See whether this file claims to be a TTF file.  First look for a couple
	// of known magic number values
	if ((offSub->scalerType == TTF_MAGIC1) || (offSub->scalerType == TTF_MAGIC2))
		// We'll accept that hint.
		return (1);

	// Otherwise we will see if there is a 'cmap' table tag in a table directory.
	tableEntries = min(processorSwap16(offSub->numTables),
		((size - sizeof(ttfOffsetSubtable)) / sizeof(ttfTableDirEntry)));

	if (findTableDirEntry(tableDir, tableEntries, TTF_TABLETAG_CMAP))
	{
		// Found a 'cmap' table entry.  This is probably an "SFNT-housed"
		// font of some kind.
		sprintf(class->className, "%s %s", FILECLASS_NAME_TTF,
			FILECLASS_NAME_FONT);
		class->class = (LOADERFILECLASS_BIN | LOADERFILECLASS_FONT);
		class->subClass = LOADERFILESUBCLASS_TTF;
		return (1);
	}
	else
		return (0);
}


/*
static int readTables(unsigned char *fontFileData, ttfFont *font)
{
	int status = 0;
	ttfOffsetSubtable *offSub = (void *) fontFileData;
	ttfTableDirEntry *tableDir =
		((void *) fontFileData + sizeof(ttfOffsetSubtable));
	unsigned short numTables = 0;
	ttfTableDirEntry *tableDirEntry = NULL;
	ttfHeadTable *headTable = NULL;
	ttfMaxpTable *maxpTable = NULL;
	//ttfCmapTable *cmapTable = NULL;
	ttfEbdtTable *ebdtTable = NULL;
	ttfEblcTable *eblcTable = NULL;
	ttfSizeTable *sizeTable = NULL;
	ttfIndexSubTableArrayElement *indexSubTableArray = NULL;
	ttfIndexSubTableHeader *indexSubTableHeader = NULL;
	int count;

	numTables = processorSwap16(offSub->numTables);

	// Find the 'head' or 'bhed' table
	tableDirEntry = findTableDirEntry(tableDir, numTables, TTF_TABLETAG_BHED);
	if (!tableDirEntry)
	{
		kernelDebug(debug_misc, "TTF has no BHED, looking for HEAD");
		tableDirEntry =
			findTableDirEntry(tableDir, numTables, TTF_TABLETAG_HEAD);
	}
	if (!tableDirEntry)
	{
		// Doesn't seem like a font we can load
		kernelDebugError("TTF font file has no 'head' or 'bhed' table");
		return (status = ERR_NODATA);
	}

	headTable =
		((void *) fontFileData + processorSwap32(tableDirEntry->offset));

	// Check the version and magic number
	if ((headTable->version != 0x00000100) || (headTable->magic != 0xF53C0F5F))
	{
		// Doesn't seem like a font we can load
		kernelDebugError("TTF header table has unknown version (%x) or magic "
			"(%x)", processorSwap32(headTable->version),
			processorSwap32(headTable->magic));
		return (status = ERR_NOTIMPLEMENTED);
	}

	// Find the 'maxp' table
	tableDirEntry = findTableDirEntry(tableDir, numTables, TTF_TABLETAG_MAXP);
	if (!tableDirEntry)
	{
		// Doesn't seem like a font we can load
		kernelDebugError("TTF font file has no 'maxp' table");
		return (status = ERR_NODATA);
	}

	maxpTable =
		((void *) fontFileData + processorSwap32(tableDirEntry->offset));

	// Check the version
	if (maxpTable->version != 0x00000100)
	{
		// Doesn't seem like a font we can load
		kernelDebugError("TTF maximum profile table has unknown version (%x)",
			 processorSwap32(maxpTable->version));
		return (status = ERR_NOTIMPLEMENTED);
	}

	font->numGlyphs = processorSwap16(maxpTable->numGlyphs);
	font->maxPoints = processorSwap16(maxpTable->maxPoints);
	kernelDebug(debug_misc, "TTF numGlyphs=%d maxPoints=%d", font->numGlyphs,
		font->maxPoints);

	// Find the EBDT table
	tableDirEntry = findTableDirEntry(tableDir, numTables, TTF_TABLETAG_EBDT);
	if (!tableDirEntry)
	{
		// Doesn't seem like a font we can load
		kernelDebugError("TTF font file has no 'EBDT' table");
		return (status = ERR_NODATA);
	}

	ebdtTable =
		((void *) fontFileData + processorSwap32(tableDirEntry->offset));

	// Check the version
	if (ebdtTable->version != 0x00000200)
	{
		// Doesn't seem like a font we can load
		kernelDebugError("TTF embedded bitmap data table has unknown version "
			"(%x)", processorSwap32(ebdtTable->version));
		return (status = ERR_NOTIMPLEMENTED);
	}

	font->bitmapData = (unsigned char *) ebdtTable;

	// Find the EBLC table
	tableDirEntry = findTableDirEntry(tableDir, numTables, TTF_TABLETAG_EBLC);
	if (!tableDirEntry)
	{
		// Doesn't seem like a font we can load
		kernelDebugError("TTF font file has no 'EBLC' table");
		return (status = ERR_NODATA);
	}

	eblcTable =
		((void *) fontFileData + processorSwap32(tableDirEntry->offset));

	// Check the version
	if (eblcTable->version != 0x00000200)
	{
		// Doesn't seem like a font we can load
		kernelDebugError("TTF embedded bitmap location table has unknown "
			"version (%x)", processorSwap32(eblcTable->version));
		return (status = ERR_NOTIMPLEMENTED);
	}

	debugEBLC(eblcTable);

	// We only use the first 'strike' (i.e. the first size) for now.
	sizeTable = &eblcTable->sizeTables[0];

	font->charWidth = eblcTable->sizeTables[0].hori.widthMax;
	font->charHeight =
		(abs(sizeTable->hori.ascender) + abs(sizeTable->hori.descender));
	font->charBytes = ((font->charWidth * font->charHeight) / 8);
	if ((font->charWidth * font->charHeight) % 8)
		font->charBytes += 1;

	font->numIndexSubTables = processorSwap32(sizeTable->numIndexSubTables);

	font->indexSubTables =
		kernelMalloc(font->numIndexSubTables * sizeof(ttfIndexSubTable));
	if (!font->indexSubTables)
		return (status = ERR_MEMORY);

	indexSubTableArray = ((void *) eblcTable +
		processorSwap32(sizeTable->indexSubTableOffset));

	for (count = 0; count < font->numIndexSubTables; count ++)
	{
		font->indexSubTables[count].firstGlyphIndex =
			processorSwap16(indexSubTableArray[count].firstGlyphIndex);
		font->indexSubTables[count].lastGlyphIndex =
			processorSwap16(indexSubTableArray[count].lastGlyphIndex);

		indexSubTableHeader = ((void *) eblcTable +
			processorSwap32(sizeTable->indexSubTableOffset) +
			processorSwap32(indexSubTableArray[count].offset));

		font->indexSubTables[count].indexFormat =
			processorSwap16(indexSubTableHeader->indexFormat);
		font->indexSubTables[count].imageFormat =
			processorSwap16(indexSubTableHeader->imageFormat);
		font->indexSubTables[count].imageDataOffset =
			processorSwap32(indexSubTableHeader->imageDataOffset);

		font->indexSubTables[count].data = indexSubTableHeader;

		debugIndexSubTable(&font->indexSubTables[count]);
	}

	return (status = 0);
}


static int load(unsigned char *fontFileData, int dataLength,
	kernelAsciiFont **pointer, int fixedWidth __attribute__((unused)))
{
	// Loads a TTF file and returns it as a font.  The memory for this and
	// its data must be freed by the caller.

	int status = 0;
	ttfFont font;
	kernelAsciiFont *newFont = NULL;
	unsigned char *fontData = NULL;
	int count;

	// Check params
	if (!fontFileData || !dataLength || !pointer)
		return (status = ERR_NULLPARAMETER);

	kernelMemClear(&font, sizeof(ttfFont));

	kernelDebug(debug_misc, "TTF load font, file size %u", dataLength);

	status = readTables(fontFileData, &font);
	if (status < 0)
		goto out;

	kernelDebug(debug_misc, "TTF bitmap font %dx%d", font.charWidth,
		font.charHeight);

	// Get memory for the font structure and the images data.
	newFont = kernelMalloc(sizeof(kernelAsciiFont));
	fontData = kernelMalloc(font.charBytes * ASCII_PRINTABLES);
	if (!newFont || !fontData)
	{
		kernelError(kernel_error, "Unable to get memory to hold the font data");
		status = ERR_MEMORY;
		goto out;
	}

	// Set some values in the new font
	newFont->charWidth = font.charWidth;
	newFont->charHeight = font.charHeight;

	for (count = 0; count < font.numIndexSubTables; count ++)
	{
		ttfIndexSubTable1 *indexSubTable1 = NULL;
		ttfIndexSubTable2 *indexSubTable2 = NULL;

		switch (font.indexSubTables[count].indexFormat)
		{
			case TTF_IDXSUBFMT_VM4BYTE:
				// Variable metrics glyphs with 4 byte offsets
				indexSubTable1 =
					(ttfIndexSubTable1 *) font.indexSubTables[count].data;
				break;

			case TTF_IDXSUBFMT_CM:
				// All glyphs have identical metrics
				indexSubTable2 =
					(ttfIndexSubTable2 *) font.indexSubTables[count].data;
				break;

			case TTF_IDXSUBFMT_VM2BYTE:
				// Variable metrics glyphs with 2 byte offsets
			case TTF_IDXSUBFMT_VMSPGC:
				// Variable metrics glyphs with sparse glyph codes
			case TTF_IDXSUBFMT_CMSPGC:
				// Constant metrics glyphs with sparse glyph codes
			default:
				kernelDebugError("TTF index subtable format %d not supported",
					font.indexSubTables[count].indexFormat);
				status = ERR_NOTIMPLEMENTED;
				goto out;
		}
	}

	kernelTextPrintLine("SUCCESS");
	status = ERR_INVALID;
	goto out;

	// Loop through all of the bitmaps, turning them into mono bitmaps as we go.
	for (count = 0; count < ASCII_PRINTABLES; count ++)
	{
		// Stuff that won't change in the rest of the code for this character,
		// below (things like width can change -- see below)
		newFont->chars[count].type = IMAGETYPE_MONO;
		newFont->chars[count].width = charWidth;
		newFont->chars[count].height = charHeight;
		newFont->chars[count].pixels = (charWidth * charHeight);
		newFont->chars[count].dataLength = charBytes;
		newFont->chars[count].data = (fontData + (count * charBytes));
	}

	// Success
	status = 0;

out:
	if (font.indexSubTables)
		kernelFree(font.indexSubTables);

	while (1);

	return (status);
}
*/


kernelFileClass ttfFileClass = {
	FILECLASS_NAME_TTF,
	&detect,
	{ }
};


/////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////
//
// Below here, the functions are exported for external use
//
/////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////

kernelFileClass *kernelFileClassTtf(void)
{
	// The loader will call this function so that we can return a structure
	// for managing TTF files

	static int filled = 0;

	if (!filled)
	{
		//ttfFileClass.font.load = &load;
		filled = 1;
	}

	return (&ttfFileClass);
}

