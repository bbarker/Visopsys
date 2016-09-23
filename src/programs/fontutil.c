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
//  fontutil.c
//

// A program for editing and converting Visopsys fonts.

/* This is the text that appears when a user requests help about this program
<help>

 -- fontutil --

A program for editing and converting Visopsys fonts.

Usage:
  fontutil [options] [VBF_file]

Examples:
  fontutil -a 32 -f space.bmp xterm-normal-10.vbf
    - Imports space.bmp as character code 32 (space)

  fontutil -c ISO-8859-15 arial-bold-10-iso-8859-15.vbf
    - Sets the character set to ISO-8859-15

  fontutil -e myfont.bmp myfont.vbf
    - Exports myfont.vbf to a bitmap image file named myfont.bmp

  fontutil -i myfont.bmp myfont.vbf
    - Imports the bitmap image file myfont.bmp to myfont.vbf

This command is used for viewing and modifying Visopsys font files.  In
graphics mode, the program ignores command-line parameters and operates
interactively, unless text mode is requested with -T.

Options:
-T              : Force text mode operation

Text mode options:
-a <code>       : Add a glyph to the font using the supplied code number
                  Use with a mandatory -f flag to specify the image file
-c <charset>    : Set the character set.  Also updates the code values.
-d [code]       : Dump (print) the font data, or else a representation of
                : the glyph with the supplied code number
-e <img_file>   : Export the font to the specified image file, which will
                : consist of a 16x6 grid of glyphs, representing either ASCII
                : codes 32-127, or charset codes 160-255, organized
                : left-to-right and top-to-bottom.
-i <img_file>   : Import a new font from the specified image file, which will
                : represent a table of glyphs representing either ASCII codes
                : 32-127, or charset codes 160-255, arranged in a 16x6 grid
                : read left-to-right and top-to-bottom.
-f <file_name>  : Used for supplying an extra file name to commands that
                : require one
-n <family>     : Set the font family name
-p <points>     : Set the number of points
-r <code>       : Remove the glyph with the supplied code number
-s <style>      : Add a style flag to the font, such as "bold" or "italic"
-v              : Verbose; print out more information about what's happening
-x <font_file>  : Convert an older VBF font file to the current version

</help>
*/

#include <errno.h>
#include <libgen.h>
#include <libintl.h>
#include <locale.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/api.h>
#include <sys/env.h>
#include <sys/image.h>
#include <sys/vbf.h>

#define _(string) gettext(string)

#define WINDOW_TITLE	_("Font Editor")

typedef enum {
	operation_none, operation_dump, operation_update, operation_import,
	operation_export, operation_add, operation_remove, operation_convert

} operation_type;

typedef struct {
	vbfFileHeader header;
	unsigned *codes;
	unsigned char *data;

} vbf;

static int graphics = 0;
static int processId = 0;
static int privilege = 0;
static const char *cmdName = NULL;
static vbf *selectedFont = NULL;
static int verbose = 0;
static char *fontDir = PATH_SYSTEM_FONTS;

// Graphics mode things
static objectKey window = NULL;
static listItemParameters *fontListParams = NULL;
static int numFontNames = 0;
static objectKey fontList = NULL;
static listItemParameters *glyphListParams = NULL;
static objectKey glyphList = NULL;
static objectKey saveButton = NULL;


static void usage(void)
{
	fprintf(stderr, "%s", _("usage:\n"));
	fprintf(stderr, _("  %s [options] [VBF_file]\n"), cmdName);
	fprintf(stderr, _("  (type 'help %s' for options help)\n"), cmdName);
	return;
}


__attribute__((format(printf, 1, 2)))
static void error(const char *format, ...)
{
	// Generic error message code for either text or graphics modes

	va_list list;
	char output[MAXSTRINGLENGTH];

	va_start(list, format);
	vsnprintf(output, MAXSTRINGLENGTH, format, list);
	va_end(list);

	if (graphics)
		windowNewErrorDialog(window, _("Error"), output);
	else
		fprintf(stderr, _("\n\nERROR: %s\n\n"), output);
}


static inline int glyphPosition(unsigned *codes, int numGlyphs, unsigned code)
{
	int pos = ERR_NOSUCHENTRY;
	int count;

	for (count = 0; count < numGlyphs; count ++)
	{
		if (codes[count] == code)
		{
			pos = count;
			break;
		}
	}

	return (pos);
}


static int readHeader(FILE *vbfFile, vbfMultiVerHeader *vbfHeader)
{
	// Read the header of a VBF file

	int status = 0;
	unsigned size = 0;

	// Go to the beginning of the file
	status = fseek(vbfFile, 0, SEEK_SET);
	if (status < 0)
	{
		perror(cmdName);
		error(_("Can't seek %s"), vbfFile->f.name);
		return (status = errno);
	}

	// Just read the first common bytes to determine that it's a VBF file, and
	// which version
	status = fread(vbfHeader, 1, sizeof(vbfHeader->common), vbfFile);
	if (status != sizeof(vbfHeader->common))
	{
		perror(cmdName);
		error(_("Can't read %s"), vbfFile->f.name);
		return (status = errno);
	}

	if (strncmp(vbfHeader->common.magic, VBF_MAGIC, VBF_MAGIC_LEN))
	{
		error(_("%s is not a VBF font file"), vbfFile->f.name);
		return (status = ERR_INVALID);
	}

	if (vbfHeader->common.version == VBF_VERSION1)
	{
		size = sizeof(vbfFileHeaderV1);
	}
	else if (vbfHeader->common.version == VBF_VERSION2)
	{
		size = sizeof(vbfFileHeader);
	}
	else
	{
		error(_("Unsupported VBF version %d.%d"),
			(vbfHeader->common.version >> 16),
			(vbfHeader->common.version & 0xFFFF));
		return (status = ERR_NOTIMPLEMENTED);
	}

	// Go to the beginning of the file
	status = fseek(vbfFile, 0, SEEK_SET);
	if (status < 0)
	{
		perror(cmdName);
		error(_("Can't seek %s"), vbfFile->f.name);
		return (status = errno);
	}

	// Read the whole header
	status = fread(vbfHeader, size, 1, vbfFile);
	if (status != 1)
	{
		perror(cmdName);
		error(_("Can't read %s"), vbfFile->f.name);
		return (status = errno);
	}

	return (status = 0);
}


static int writeHeader(FILE *vbfFile, vbfFileHeader *vbfHeader)
{
	// Write the header of a VBF file

	int status = 0;

	status = fseek(vbfFile, 0, SEEK_SET);
	if (status < 0)
	{
		perror(cmdName);
		error(_("Can't seek %s"), vbfFile->f.name);
		return (status = errno);
	}

	status = fwrite(vbfHeader, sizeof(vbfFileHeader), 1, vbfFile);
	if (status != 1)
	{
		perror(cmdName);
		error(_("Can't write %s"), vbfFile->f.name);
		return (status = errno);
	}

	return (status = 0);
}


static int updateHeader(const char *vbfFileName)
{
	int status = 0;
	FILE *vbfFile = NULL;
	vbfMultiVerHeader vbfHeader;
	unsigned *codes = NULL;
	int count;

	printf(_("Update VBF header of %s\n"), vbfFileName);

	memset(&vbfHeader, 0, sizeof(vbfHeader));

	vbfFile = fopen(vbfFileName, "r+");
	if (!vbfFile)
	{
		perror(cmdName);
		error(_("Can't open %s for reading/writing"), vbfFileName);
		return (status = errno);
	}

	status = readHeader(vbfFile, &vbfHeader);
	if (status < 0)
	{
		fclose(vbfFile);
		return (status);
	}

	if (vbfHeader.common.version != VBF_VERSION2)
	{
		error("%s", _("Can't update an older VBF file.  Convert with -x?"));
		status = ERR_NOTIMPLEMENTED;
		goto out;
	}

	if (selectedFont->header.family[0])
	{
		printf(_("Font family: now %s (was %s)\n"),
			selectedFont->header.family, vbfHeader.v2.family);
		memset(vbfHeader.v2.family, 0, VBF_FAMILY_LEN);
		strncpy(vbfHeader.v2.family, selectedFont->header.family,
			VBF_FAMILY_LEN);
	}

	if (selectedFont->header.flags)
	{
		printf(_("Flags: now 0x%x (was 0x%x)\n"),
			(selectedFont->header.flags | vbfHeader.v2.flags),
			vbfHeader.v2.flags);
		vbfHeader.v2.flags |= selectedFont->header.flags;
	}

	if (selectedFont->header.points)
	{
		printf(_("Points: now %d (was %d)\n"), selectedFont->header.points,
			vbfHeader.v2.points);
		vbfHeader.v2.points = selectedFont->header.points;
	}

	if (selectedFont->header.charSet[0])
	{
		printf(_("Character set: now %s (was %s)\n"),
			selectedFont->header.charSet, vbfHeader.v2.charSet);
		memset(vbfHeader.v2.charSet, 0, VBF_CHARSET_LEN);
		strncpy(vbfHeader.v2.charSet, selectedFont->header.charSet,
			VBF_CHARSET_LEN);

		codes = calloc(vbfHeader.v2.numGlyphs, sizeof(unsigned));
		if (!codes)
		{
			perror(cmdName);
			error("%s", _("Memory error"));
			status = errno;
			goto out;
		}

		// Read the code map
		status = fread(codes, sizeof(unsigned), vbfHeader.v2.numGlyphs,
			vbfFile);
		if (status < vbfHeader.v2.numGlyphs)
		{
			perror(cmdName);
			error(_("Couldn't read character codes of %s"), vbfFile->f.name);
			status = errno;
			goto out;
		}

		// Loop through and put in the codes.
		for (count = 0; count < ASCII_PRINTABLES; count ++)
		{
			if (!strcmp(vbfHeader.v2.charSet, CHARSET_NAME_ASCII))
			{
				codes[count] = (CHARSET_CTRL_CODES + count);
			}
			else
			{
				codes[count] = charsetToUnicode(vbfHeader.v2.charSet,
					(CHARSET_NUM_CODES + CHARSET_CTRL_CODES + count));
			}
		}
	}

	status = writeHeader(vbfFile, (vbfFileHeader *) &vbfHeader);

	if (codes)
	{
		// Write the code map
		status = fwrite(codes, sizeof(unsigned), vbfHeader.v2.numGlyphs,
			vbfFile);
		if (status < vbfHeader.v2.numGlyphs)
		{
			perror(cmdName);
			error(_("Couldn't write character codes for %s"), vbfFile->f.name);
			return (status = errno);
		}
	}

out:
	if (codes)
		free(codes);

	fclose(vbfFile);

	return (status);
}


static int readFontV1(FILE *vbfFile, vbfFileHeaderV1 *vbfHeader, int **codes,
	unsigned char **data)
{
	int status = 0;
	int glyphBytes = 0;

	// Read the header
	status = readHeader(vbfFile, (vbfMultiVerHeader *) vbfHeader);
	if (status < 0)
		goto out;

	// Make sure it's version 1
	if (((vbfMultiVerHeader *) vbfHeader)->common.version != VBF_VERSION1)
	{
		error("%s", _("Not a version 1 VBF file"));
		status = ERR_NOTIMPLEMENTED;
		goto out;
	}

	glyphBytes = (((vbfHeader->glyphWidth * vbfHeader->glyphHeight) + 7) / 8);

	// Get memory for the codes and data
	*codes = malloc(vbfHeader->numGlyphs * sizeof(int));
	*data = malloc(vbfHeader->numGlyphs * glyphBytes);
	if (!(*codes) || !(*data))
	{
		perror(cmdName);
		error("%s", _("Couldn't get memory for character codes or data"));
		status = errno;
		goto out;
	}

	// Read the code map
	status = fread(*codes, sizeof(int), vbfHeader->numGlyphs, vbfFile);
	if (status < vbfHeader->numGlyphs)
	{
		perror(cmdName);
		error(_("Couldn't read character codes of %s"), vbfFile->f.name);
		status = errno;
		goto out;
	}

	// Read the glyph data
	status = fread(*data, glyphBytes, vbfHeader->numGlyphs, vbfFile);
	if (status < vbfHeader->numGlyphs)
	{
		perror(cmdName);
		error(_("Couldn't read glyph data of %s"), vbfFile->f.name);
		status = errno;
		goto out;
	}

	// Return success
	status = 0;

out:
	if (status)
	{
		if (*codes)
		{
			free(*codes);
			*codes = NULL;
		}

		if (*data)
		{
			free(*data);
			*data = NULL;
		}
	}

	return (status);
}


static int readFont(FILE *vbfFile, vbfFileHeader *vbfHeader,
	unsigned **codes, unsigned char **data)
{
	int status = 0;
	int glyphBytes = 0;

	// Read the header
	status = readHeader(vbfFile, (vbfMultiVerHeader *) vbfHeader);
	if (status < 0)
		goto out;

	// Make sure it's a supported version
	if (((vbfMultiVerHeader *) vbfHeader)->common.version != VBF_VERSION2)
	{
		error("%s", _("Can't read an older VBF file.  Convert with -x?"));
		status = ERR_NOTIMPLEMENTED;
		goto out;
	}

	glyphBytes = (((vbfHeader->glyphWidth * vbfHeader->glyphHeight) + 7) / 8);

	// Get memory for the codes and data
	*codes = malloc(vbfHeader->numGlyphs * sizeof(unsigned));
	*data = malloc(vbfHeader->numGlyphs * glyphBytes);
	if (!(*codes) || !(*data))
	{
		perror(cmdName);
		error("%s", _("Couldn't get memory for character codes or data"));
		status = errno;
		goto out;
	}

	// Read the code map
	status = fread(*codes, sizeof(unsigned), vbfHeader->numGlyphs, vbfFile);
	if (status < vbfHeader->numGlyphs)
	{
		perror(cmdName);
		error(_("Couldn't read character codes of %s"), vbfFile->f.name);
		status = errno;
		goto out;
	}

	// Read the character data
	status = fread(*data, glyphBytes, vbfHeader->numGlyphs, vbfFile);
	if (status < vbfHeader->numGlyphs)
	{
		perror(cmdName);
		error(_("Couldn't read character data of %s"), vbfFile->f.name);
		status = errno;
		goto out;
	}

	// Return success
	status = 0;

out:
	if (status)
	{
		if (*codes)
		{
			free(*codes);
			*codes = NULL;
		}

		if (*data)
		{
			free(*data);
			*data = NULL;
		}
	}

	return (status);
}


static int writeFont(FILE *vbfFile, vbfFileHeader *vbfHeader,
	unsigned *codes, unsigned char *data)
{
	int status = 0;
	int glyphBytes = 0;

	// Write the header
	status = writeHeader(vbfFile, vbfHeader);
	if (status < 0)
		return (status);

	glyphBytes = (((vbfHeader->glyphWidth * vbfHeader->glyphHeight) + 7) / 8);

	// Write the code map
	status = fwrite(codes, sizeof(unsigned), vbfHeader->numGlyphs, vbfFile);
	if (status < vbfHeader->numGlyphs)
	{
		perror(cmdName);
		error(_("Couldn't write character codes for %s"), vbfFile->f.name);
		return (status = errno);
	}

	// Write the glyph data
	status = fwrite(data, glyphBytes, vbfHeader->numGlyphs, vbfFile);
	if (status < vbfHeader->numGlyphs)
	{
		perror(cmdName);
		error(_("Couldn't write glyph data for %s"), vbfFile->f.name);
		return (status = errno);
	}

	// Return success
	return (status = 0);
}


static void dumpHeader(vbfFileHeader *vbfHeader)
{
	char tmp[33];

	printf("VBF file header:\n");
	strncpy(tmp, vbfHeader->magic, VBF_MAGIC_LEN);
	tmp[VBF_MAGIC_LEN] = '\0';
	printf(" magic=%s\n", tmp);
	printf(" version=%d.%d\n", (vbfHeader->version >> 16),
		(vbfHeader->version & 0xFFFF));
	strncpy(tmp, vbfHeader->family, VBF_FAMILY_LEN);
	tmp[VBF_FAMILY_LEN] = '\0';
	printf(" family=%s\n", tmp);
	printf(" flags=%08x\n", vbfHeader->flags);
	printf(" points=%d\n", vbfHeader->points);
	strncpy(tmp, vbfHeader->charSet, VBF_CHARSET_LEN);
	tmp[VBF_CHARSET_LEN] = '\0';
	printf(" charSet=%s\n", tmp);
	printf(" numGlyphs=%d\n", vbfHeader->numGlyphs);
	printf(" glyphWidth=%d\n", vbfHeader->glyphWidth);
	printf(" glyphHeight=%d\n", vbfHeader->glyphHeight);
	if (vbfHeader->numGlyphs)
		printf(" first code=%u\n", vbfHeader->codes[0]);
}


static int dumpGlyph(unsigned code, vbfFileHeader *vbfHeader,
	unsigned *codes, unsigned char *data)
{
	int status = 0;
	int pos = 0;
	int glyphPixels = 0;
	int glyphBytes = 0;
	int count;

	pos = glyphPosition(codes, vbfHeader->numGlyphs, code);
	if (pos < 0)
	{
		error(_("Glyph %d does not exist in font file"), code);
		return (status = ERR_NOSUCHENTRY);
	}

	glyphPixels = (vbfHeader->glyphWidth * vbfHeader->glyphHeight);
	glyphBytes = ((glyphPixels + 7) / 8);
	data += (pos * glyphBytes);

	for (count = 0; count < glyphPixels; count ++)
	{
		if (!(count % vbfHeader->glyphWidth))
			printf("\n");

		if (data[count / 8] & (0x80 >> (count % 8)))
			printf("#");
		else
			printf("_");
	}

	printf("\n");
	return (status = 0);
}


static int dump(const char *vbfFileName, unsigned code)
{
	int status = 0;
	FILE *vbfFile = NULL;
	vbfFileHeader vbfHeader;
	unsigned *codes = NULL;
	unsigned char *data = NULL;

	memset(&vbfHeader, 0, sizeof(vbfHeader));

	vbfFile = fopen(vbfFileName, "r");
	if (!vbfFile)
	{
		perror(cmdName);
		error(_("Can't open %s for reading"), vbfFileName);
		return (status = errno);
	}

	status = readFont(vbfFile, &vbfHeader, &codes, &data);
	if (status < 0)
		goto out;

	status = 0;

	if (code != (unsigned) -1)
		status = dumpGlyph(code, &vbfHeader, codes, data);
	else
		dumpHeader(&vbfHeader);

out:
	if (codes)
		free(codes);
	if (data)
		free(data);
	if (vbfFile)
		fclose(vbfFile);

	return (status);
}


static void image2Bitmap(pixel *srcPix, int imageWidth, int glyphWidth,
	int glyphHeight, unsigned char *destBytes)
{
	int glyphPixels = 0;
	int pixelCount = 0;
	int count;

	glyphPixels = (glyphWidth * glyphHeight);

	// Loop through the image data, setting bitmap bits for each black pixel
	// in the image.

	for (count = 0; count < glyphPixels; count ++)
	{
		// If it's black, set the corresponding bit in the new bitmap
		if (PIXELS_EQ(&srcPix[pixelCount], &COLOR_BLACK))
		{
			destBytes[count / 8] |= (0x80 >> (count % 8));

			if (verbose)
				printf("#");
		}
		else
		{
			destBytes[count / 8] &= ~(0x80 >> (count % 8));

			if (verbose)
				printf("_");
		}

		pixelCount += 1;
		if (!(pixelCount % glyphWidth))
		{
			pixelCount += (imageWidth - glyphWidth);
			if (verbose)
				printf("\n");
		}
	}
}


static int import(const char *imageFileName, const char *vbfFileName)
{
	int status = 0;
	image importImage;
	FILE *vbfFile = NULL;
	vbfFileHeader vbfHeader;
	static int glyphColumns = 16;
	static int glyphRows = 6;
	int glyphBytes = 0;
	unsigned *codes = NULL;
	unsigned char *data = NULL;
	int startPixel = 0;
	int startByte = 0;
	int count, colCount, rowCount;

	printf(_("Import font from %s to VBF file %s\n"), imageFileName,
		vbfFileName);

	memset(&importImage, 0, sizeof(image));
	memset(&vbfHeader, 0, sizeof(vbfHeader));

	// Try to get the kernel to load the image
	status = imageLoad(imageFileName, 0, 0, &importImage);
	if (status < 0)
	{
		errno = status;
		perror(cmdName);
		error(_("Couldn't load font image file %s"), imageFileName);
		goto out;
	}

	if (importImage.width % glyphColumns)
	{
		error(_("Image width (%d) of %s is not a multiple of %d"),
			importImage.width, imageFileName, glyphColumns);
		status = ERR_INVALID;
		goto out;
	}

	if (importImage.height % glyphRows)
	{
		error(_("Image height (%d) of %s is not a multiple of %d"),
			importImage.height, imageFileName, glyphRows);
		status = ERR_INVALID;
		goto out;
	}

	// Open our output file
	vbfFile = fopen(vbfFileName, "w");
	if (!vbfFile)
	{
		perror(cmdName);
		error(_("Can't open font file %s for writing"), vbfFileName);
		status = errno;
		goto out;
	}

	strncpy(vbfHeader.magic, VBF_MAGIC, VBF_MAGIC_LEN);
	vbfHeader.version = VBF_VERSION2;

	if (selectedFont->header.family[0])
		strncpy(vbfHeader.family, selectedFont->header.family, VBF_FAMILY_LEN);
	else
		strncpy(vbfHeader.family, imageFileName, VBF_FAMILY_LEN);

	if (selectedFont->header.flags)
		vbfHeader.flags = selectedFont->header.flags;

	if (selectedFont->header.points)
		vbfHeader.points = selectedFont->header.points;

	if (selectedFont->header.charSet[0])
		strncpy(vbfHeader.charSet, selectedFont->header.charSet,
			VBF_CHARSET_LEN);
	else
		strcpy(vbfHeader.charSet, CHARSET_NAME_ASCII);

	vbfHeader.numGlyphs = (glyphColumns * glyphRows);
	vbfHeader.glyphWidth = (importImage.width / glyphColumns);
	vbfHeader.glyphHeight = (importImage.height / glyphRows);

	if (verbose)
		printf(_("%d glyphs size %dx%d\n"), vbfHeader.numGlyphs,
			vbfHeader.glyphWidth, vbfHeader.glyphHeight);

	glyphBytes = (((vbfHeader.glyphWidth * vbfHeader.glyphHeight) + 7) / 8);

	// Allocate memory for the codes and data
	codes = malloc(vbfHeader.numGlyphs * sizeof(unsigned));
	data = malloc(vbfHeader.numGlyphs * glyphBytes);
	if (!codes || !data)
	{
		perror(cmdName);
		error("%s", _("Couldn't allocate memory"));
		status = errno;
		goto out;
	}

	// Loop through and put in the codes.  We assume ASCII until the user
	// updates the font with the -c operation.
	for (count = 0; count < ASCII_PRINTABLES; count ++)
	{
		if (!strcmp(vbfHeader.charSet, CHARSET_NAME_ASCII))
		{
			codes[count] = (CHARSET_CTRL_CODES + count);
		}
		else
		{
			codes[count] = charsetToUnicode(vbfHeader.charSet,
				(CHARSET_NUM_CODES + CHARSET_CTRL_CODES + count));
		}
	}

	// Loop through the characters in the image and add them
	for (rowCount = 0; rowCount < glyphRows; rowCount ++)
	{
		for (colCount = 0; colCount < glyphColumns; colCount ++)
		{
			// Calculate the starting pixel number of the image we're working
			// from
			startPixel = ((rowCount * glyphColumns * vbfHeader.glyphWidth *
				vbfHeader.glyphHeight) + (colCount * vbfHeader.glyphWidth));

			startByte = (((rowCount * glyphColumns) + colCount) *
				glyphBytes);

			// Convert it to bitmap data
			image2Bitmap((importImage.data + (startPixel * sizeof(pixel))),
				importImage.width, vbfHeader.glyphWidth,
				vbfHeader.glyphHeight, (data + startByte));
		}
	}

	// Write out the font
	status = writeFont(vbfFile, &vbfHeader, codes, data);

out:
	if (codes)
		free(codes);
	if (data)
		free(data);
	if (vbfFile)
		fclose(vbfFile);

	return (status);
}


static int bitmap2Image(int index, vbfFileHeader *vbfHeader,
	unsigned char *data, image *glyphImage)
{
	int status = 0;
	int glyphPixels = 0;
	int glyphBytes = 0;
	int count;

	status = imageNew(glyphImage, vbfHeader->glyphWidth,
		vbfHeader->glyphHeight);
	if (status < 0)
	{
		error("%s", _("Couldn't get a new image"));
		return (status);
	}

	glyphPixels = (vbfHeader->glyphWidth * vbfHeader->glyphHeight);
	glyphBytes = ((glyphPixels + 7) / 8);
	data += (index * glyphBytes);

	for (count = 0; count < glyphPixels; count ++)
	{
		if (data[count / 8] & (0x80 >> (count % 8)))
			((pixel *) glyphImage->data)[count] = COLOR_BLACK;
		else
			((pixel *) glyphImage->data)[count] = COLOR_WHITE;
	}

	return (status = 0);
}


static int export(const char *imageFileName, const char *vbfFileName)
{
	int status = 0;
	FILE *vbfFile = NULL;
	vbfFileHeader vbfHeader;
	unsigned *codes = NULL;
	unsigned char *data = NULL;
	image exportImage;
	static int glyphColumns = 16;
	static int glyphRows = 6;
	image glyphImage;
	int colCount, rowCount;

	printf(_("Export font from VBF file %s to %s\n"), vbfFileName,
		imageFileName);

	memset(&vbfHeader, 0, sizeof(vbfHeader));
	memset(&exportImage, 0, sizeof(image));
	memset(&glyphImage, 0, sizeof(image));

	// Open our font file
	vbfFile = fopen(vbfFileName, "r");
	if (!vbfFile)
	{
		perror(cmdName);
		error(_("Can't open font file %s for reading"), vbfFileName);
		return (status = errno);
	}

	// Read in the font
	status = readFont(vbfFile, &vbfHeader, &codes, &data);

	fclose(vbfFile);

	if (status < 0)
		goto out;

	// Create a new image
	status = imageNew(&exportImage, (vbfHeader.glyphWidth * 16),
		(vbfHeader.glyphHeight * 6));
	if (status < 0)
	{
		errno = status;
		perror(cmdName);
		error("%s", _("Couldn't get a new image"));
		goto out;
	}

	if (verbose)
		printf(_("%d glyphs size %dx%d\n"), vbfHeader.numGlyphs,
			vbfHeader.glyphWidth, vbfHeader.glyphHeight);

	// Loop through the glyphs in the font and add them
	for (rowCount = 0; rowCount < glyphRows; rowCount ++)
	{
		for (colCount = 0; colCount < glyphColumns; colCount ++)
		{
			// Create an image from the glyph bitmap
			status = bitmap2Image(((rowCount * glyphColumns) + colCount),
				&vbfHeader, data, &glyphImage);
			if (status >= 0)
			{
				// Paste it into our main image
				status = imagePaste(&glyphImage, &exportImage, (colCount *
					vbfHeader.glyphWidth), (rowCount * vbfHeader.glyphHeight));

				imageFree(&glyphImage);
			}
		}
	}

	// Try to get the kernel to save the image
	status = imageSave(imageFileName, IMAGEFORMAT_BMP, &exportImage);
	if (status < 0)
	{
		errno = status;
		perror(cmdName);
		error(_("Couldn't save font image file %s"), imageFileName);
		goto out;
	}

out:
	imageFree(&exportImage);

	if (codes)
		free(codes);

	if (data)
		free(data);

	return (status);
}


static int addGlyph(unsigned code, const char *addFileName,
	const char *vbfFileName)
{
	int status = 0;
	image addImage;
	FILE *destFile = NULL;
	vbfFileHeader vbfHeader;
	unsigned *oldCodes = NULL;
	unsigned char *oldData = NULL;
	int glyphBytes = 0;
	int newNumGlyphs = 0;
	unsigned *newCodes = NULL;
	unsigned char *newData = NULL;
	int pos = 0;
	int count;

	printf(_("Add glyph %u from %s to VBF file %s\n"), code, addFileName,
		vbfFileName);

	memset(&addImage, 0, sizeof(image));
	memset(&vbfHeader, 0, sizeof(vbfHeader));

	// Try to get the kernel to load the image
	status = imageLoad(addFileName, 0, 0, &addImage);
	if (status < 0)
	{
		errno = status;
		perror(cmdName);
		error(_("Couldn't load glyph image file %s"), addFileName);
		return (status);
	}

	// Open our output file
	destFile = fopen(vbfFileName, "r+");
	if (!destFile)
	{
		perror(cmdName);
		error(_("Can't open destination file %s for writing"), vbfFileName);
		return (status = errno);
	}

	// Read in the font
	status = readFont(destFile, &vbfHeader, &oldCodes, &oldData);
	if (status < 0)
		goto out;

	glyphBytes = (((vbfHeader.glyphWidth * vbfHeader.glyphHeight) + 7) / 8);

	newNumGlyphs = vbfHeader.numGlyphs;
	newCodes = oldCodes;
	newData = oldData;

	// Does the glyph already appear in the font, or are we replacing it?
	if ((pos = glyphPosition(oldCodes, vbfHeader.numGlyphs, code)) < 0)
	{
		// The glyph doesn't appear in the font.  Make space for it.

		newNumGlyphs += 1;

		// Get memory for the new codes and new data
		newCodes = malloc(newNumGlyphs * sizeof(int));
		newData = malloc(newNumGlyphs * glyphBytes);
		if (!newCodes || !newData)
		{
			perror(cmdName);
			error("%s", _("Couldn't get memory for character codes or data"));
			status = errno;
			goto out;
		}

		// Find the correct (sorted) place in the map
		pos = vbfHeader.numGlyphs;
		for (count = 0; count < vbfHeader.numGlyphs; count ++)
		{
			if (oldCodes[count] > code)
			{
				pos = count;
				break;
			}
		}

		// Copy the codes from the 'before' part of the map
		memcpy(newCodes, oldCodes, (pos * sizeof(int)));
		// Copy the codes from the 'after' part of the map
		memcpy((newCodes + ((pos + 1) * sizeof(int))),
			(oldCodes + (pos * sizeof(int))),
			((vbfHeader.numGlyphs - pos) * sizeof(int)));

		// Copy the data from the 'before' glyphs
		memcpy(newData, oldData, (pos * glyphBytes));
		// Copy the data from the 'after' glyphs
		memcpy((newData + ((pos + 1) * glyphBytes)),
			(oldData + (pos * glyphBytes)),
			((vbfHeader.numGlyphs - pos) * glyphBytes));

		vbfHeader.numGlyphs = newNumGlyphs;
	}
	else
	{
		// Clear the existing data
		memset((newData + (pos * glyphBytes)), 0, glyphBytes);
	}

	// Set the code value in the map
	newCodes[pos] = code;

	// Convert the image data into font bitmap data
	image2Bitmap(addImage.data, addImage.width, vbfHeader.glyphWidth,
		vbfHeader.glyphHeight, (newData + (pos * glyphBytes)));

	// Write the font back to disk
	status = writeFont(destFile, &vbfHeader, newCodes, newData);

out:
	if (destFile)
		fclose(destFile);
	if (oldCodes)
		free(oldCodes);
	if (oldData)
		free(oldData);
	if (newCodes && (newCodes != oldCodes))
		free(newCodes);
	if (newData && (newData != oldData))
		free(newData);

	return (status);
}


static int removeGlyph(unsigned code, const char *vbfFileName)
{
	int status = 0;
	FILE *destFile = NULL;
	vbfFileHeader vbfHeader;
	unsigned *codes = NULL;
	unsigned char *data = NULL;
	int pos = 0;
	int glyphBytes = 0;
	unsigned newFileSize = 0;

	printf(_("Remove glyph %d from VBF file %s\n"), code, vbfFileName);

	memset(&vbfHeader, 0, sizeof(vbfHeader));

	// Open our output file
	destFile = fopen(vbfFileName, "r+");
	if (!destFile)
	{
		perror(cmdName);
		error(_("Can't open destination file %s for writing"), vbfFileName);
		return (status = errno);
	}

	// Read in the font
	status = readFont(destFile, &vbfHeader, &codes, &data);
	if (status < 0)
		goto out;

	// Find the position of the glyph in the map
	pos = glyphPosition(codes, vbfHeader.numGlyphs, code);
	if (pos < 0)
	{
		error(_("Glyph %d does not exist in font file %s"), code, vbfFileName);
		status = ERR_NOSUCHENTRY;
		goto out;
	}

	if (pos < (vbfHeader.numGlyphs - 1))
	{
		glyphBytes =
			(((vbfHeader.glyphWidth * vbfHeader.glyphHeight) + 7) / 8);

		// 'Erase' the code by copying the following ones over it
		memcpy(&codes[pos], &codes[pos + 1],
			((vbfHeader.numGlyphs - pos) * sizeof(int)));
		// 'Erase' the data
		memcpy((data + (pos * glyphBytes)), (data + ((pos + 1) * glyphBytes)),
			((vbfHeader.numGlyphs - pos) * glyphBytes));
	}

	vbfHeader.numGlyphs -= 1;

	// Write the font back to disk
	status = writeFont(destFile, &vbfHeader, codes, data);

out:
	if (destFile)
	{
		newFileSize = destFile->offset;

		fclose(destFile);

		if (!status)
			// Truncate the file to the current file offset
			truncate(vbfFileName, newFileSize);
	}

	return (status);
}


static int convert(const char *v1FileName, const char *v2FileName)
{
	// Given a version 1 VBF font file, convert it to version 2.

	int status = 0;
	FILE *v1File = NULL;
	vbfFileHeaderV1 v1Header;
	int *v1Codes = NULL;
	unsigned char *v1Data = NULL;
	int v2NumAsciiGlyphs = 0;
	int v2NumCharSetGlyphs = 0;
	int glyphBytes = 0;
	unsigned v2AsciiFileSize = 0;
	unsigned v2CharSetFileSize = 0;
	vbfFileHeader *v2AsciiHeader = NULL;
	vbfFileHeader *v2CharSetHeader = NULL;
	unsigned char *v2AsciiData = NULL;
	unsigned char *v2CharSetData = NULL;
	char v2AsciiFileName[MAX_PATH_NAME_LENGTH];
	char v2CharSetFileName[MAX_PATH_NAME_LENGTH];
	FILE *v2AsciiFile = NULL;
	FILE *v2CharSetFile = NULL;
	size_t written = 0;
	int count;

	printf(_("Convert VBF V1 file %s to VBF V2 (%s)\n"), v1FileName,
		v2FileName);

	memset(&v1Header, 0, sizeof(vbfFileHeaderV1));

	// Open the input file
	v1File = fopen(v1FileName, "r");
	if (!v1File)
	{
		perror(cmdName);
		error(_("Can't open source file %s for reading"), v1FileName);
		return (status = errno);
	}

	// Read the V1 font
	status = readFontV1(v1File, &v1Header, &v1Codes, &v1Data);

	fclose(v1File);

	if (status < 0)
		return (status);

	if (verbose)
	{
		printf(_("Glyph size %dx%d\n"), v1Header.glyphWidth,
			v1Header.glyphHeight);
	}

	// How many glyphs will go into each output file?
	for (count = 0; count < v1Header.numGlyphs; count ++)
	{
		if (v1Codes[count] <= ASCII_DEL)
			v2NumAsciiGlyphs += 1;
		else
			v2NumCharSetGlyphs += 1;
	}

	if (verbose)
	{
		printf(_("%d ASCII glyphs, %d %s glyphs\n"), v2NumAsciiGlyphs,
			v2NumCharSetGlyphs, v1Header.charSet);
	}

	// How many bytes per glyph in the bitmap?
	glyphBytes = (((v1Header.glyphWidth * v1Header.glyphHeight) + 7) / 8);

	// Calculate the output file sizes
	v2AsciiFileSize = (sizeof(vbfFileHeader) + (v2NumAsciiGlyphs *
		sizeof(unsigned)) + (v2NumAsciiGlyphs * glyphBytes));
	v2CharSetFileSize = (sizeof(vbfFileHeader) + (v2NumCharSetGlyphs *
		sizeof(unsigned)) + (v2NumCharSetGlyphs * glyphBytes));

	// Get memory for the output files
	v2AsciiHeader = calloc(1, v2AsciiFileSize);
	v2CharSetHeader = calloc(1, v2CharSetFileSize);
	if (!v2AsciiHeader || !v2CharSetHeader)
	{
		perror(cmdName);
		error("%s", _("Can't get memory for output files"));
		status = errno;
		goto out;
	}

	// Set the data pointers
	v2AsciiData = ((unsigned char *) v2AsciiHeader + sizeof(vbfFileHeader) +
		(v2NumAsciiGlyphs * sizeof(unsigned)));
	v2CharSetData = ((unsigned char *) v2CharSetHeader +
		sizeof(vbfFileHeader) + (v2NumCharSetGlyphs * sizeof(unsigned)));

	// Set up the output file names
	sprintf(v2AsciiFileName, "%s.%s", v2FileName, CHARSET_NAME_ASCII);
	sprintf(v2CharSetFileName, "%s.%s", v2FileName, v1Header.charSet);

	if (verbose)
	{
		printf(_("Creating %s (%u bytes)\n"), v2AsciiFileName,
			v2AsciiFileSize);
		printf(_("Creating %s (%u bytes)\n"), v2CharSetFileName,
			v2CharSetFileSize);
	}

	// Open the output files
	v2AsciiFile = fopen(v2AsciiFileName, "w+");
	v2CharSetFile = fopen(v2CharSetFileName, "w+");
	if (!v2AsciiFile || !v2CharSetFile)
	{
		perror(cmdName);
		error("%s", _("Can't open destination file(s) for writing"));
		status = errno;
		goto out;
	}

	// Set the header magic numbers
	strncpy(v2AsciiHeader->magic, VBF_MAGIC, VBF_MAGIC_LEN);
	strncpy(v2CharSetHeader->magic, VBF_MAGIC, VBF_MAGIC_LEN);

	// Set the header version numbers
	v2AsciiHeader->version = VBF_VERSION2;
	v2CharSetHeader->version = VBF_VERSION2;

	// Copy the old 'name' field into the new 'family' fields.  The user should
	// set this properly with another command.
	strncpy(v2AsciiHeader->family, v1Header.name, VBF_NAME_LEN);
	strncpy(v2CharSetHeader->family, v1Header.name, VBF_NAME_LEN);

	// Flags will remain empty until the user sets them with another command.

	// Set the points fields
	v2AsciiHeader->points = v1Header.points;
	v2CharSetHeader->points = v1Header.points;

	// Set the character set fields
	strcpy(v2AsciiHeader->charSet, CHARSET_NAME_ASCII);
	strncpy(v2CharSetHeader->charSet, v1Header.charSet, VBF_CHARSET_LEN);

	// Set the glyph width and height fields
	v2AsciiHeader->glyphWidth = v1Header.glyphWidth;
	v2AsciiHeader->glyphHeight = v1Header.glyphHeight;
	v2CharSetHeader->glyphWidth = v1Header.glyphWidth;
	v2CharSetHeader->glyphHeight = v1Header.glyphHeight;

	// Copy the codes and glyphs
	for (count = 0; count < v1Header.numGlyphs; count ++)
	{
		if (v1Codes[count] <= ASCII_DEL)
		{
			v2AsciiHeader->codes[v2AsciiHeader->numGlyphs] = v1Codes[count];
			memcpy((v2AsciiData + (v2AsciiHeader->numGlyphs * glyphBytes)),
				(v1Data + (count * glyphBytes)), glyphBytes);
			v2AsciiHeader->numGlyphs += 1;
		}
		else
		{
			v2CharSetHeader->codes[v2CharSetHeader->numGlyphs] =
				charsetToUnicode(v1Header.charSet, v1Codes[count]);
			memcpy((v2CharSetData + (v2CharSetHeader->numGlyphs *
				glyphBytes)), (v1Data + (count * glyphBytes)), glyphBytes);
			v2CharSetHeader->numGlyphs += 1;
		}
	}

	// Write out the ASCII file
	written = 0;
	while (written < v2AsciiFileSize)
	{
		status = fwrite(((unsigned char *) v2AsciiHeader + written), 1,
			(v2AsciiFileSize - written), v2AsciiFile);
		if (status <= 0)
		{
			perror(cmdName);
			error(_("Can't write destination file %s"), v2AsciiFileName);
			status = errno;
			goto out;
		}

		written += status;
	}

	// Write out the charset file
	written = 0;
	while (written < v2CharSetFileSize)
	{
		status = fwrite(((unsigned char *) v2CharSetHeader + written), 1,
			(v2CharSetFileSize - written), v2CharSetFile);
		if (status <= 0)
		{
			perror(cmdName);
			error(_("Can't write destination file %s"), v2CharSetFileName);
			status = errno;
			goto out;
		}

		written += status;
	}

	status = 0;

out:
	if (v2AsciiHeader)
		free(v2AsciiHeader);
	if (v2CharSetHeader)
		free(v2CharSetHeader);

	fclose(v2AsciiFile);
	fclose(v2CharSetFile);

	return (status);
}


static int getFontName(const char *fileName, char *nameBuffer)
{
	int status = 0;
	loaderFileClass class;
	FILE *vbfFile = NULL;
	vbfMultiVerHeader vbfHeader;
	char *shortName = NULL;

	loaderClassifyFile(fileName, &class);

	if (!(class.class & LOADERFILECLASS_FONT) ||
		!(class.subClass & LOADERFILESUBCLASS_VBF))
	{
		return (status = ERR_INVALID);
	}

	// Make sure it's a supported one

	vbfFile = fopen(fileName, "r");
	if (!vbfFile)
		return (status = ERR_IO);

	status = readHeader(vbfFile, &vbfHeader);

	fclose(vbfFile);

	if (status < 0)
		return (status);

	if (vbfHeader.common.version != VBF_VERSION2)
		return (status = ERR_NOTIMPLEMENTED);

	shortName = basename((char *) fileName);
	if (!shortName)
		return (status = ERR_NOSUCHFILE);

	strcpy(nameBuffer, shortName);

	free(shortName);

	numFontNames += 1;

	return (status = 0);
}


static int getFontNames(char *nameBuffer)
{
	// Look in the font directory for font files.

	int status = 0;
	char *fileName = NULL;
	file theFile;
	int bufferChar = 0;
	int count;

	fileName = malloc(MAX_PATH_NAME_LENGTH);
	if (!fileName)
		return (status = ERR_MEMORY);

	nameBuffer[0] = '\0';
	numFontNames = 0;

	// Loop through the files in the font directory
	for (count = 0; ; count ++)
	{
		if (count)
			status = fileNext(fontDir, &theFile);
		else
			status = fileFirst(fontDir, &theFile);

		if (status < 0)
			break;

		if (theFile.type != fileT)
			continue;

		snprintf(fileName, MAX_PATH_NAME_LENGTH, "%s/%s", fontDir,
			theFile.name);

		status = getFontName(fileName, (nameBuffer + bufferChar));
		if (status < 0)
			continue;

		bufferChar += (strlen(nameBuffer + bufferChar) + 1);
	}

	free(fileName);
	return (status = 0);
}


static int getFontListParams(const char *vbfFileName)
{
	// Put the list of font file names into an array of list parameters

	int status = 0;
	char *nameBuffer = NULL;
	file theFile;
	char *buffPtr = NULL;
	int count;

	nameBuffer = malloc(1024);
	if (!nameBuffer)
		return (status = ERR_MEMORY);

	if (vbfFileName)
	{
		status = fileFind(vbfFileName, &theFile);
		if (status < 0)
			goto out;

		if (theFile.type != fileT)
		{
			status = ERR_INVALID;
			goto out;
		}

		status = getFontName(vbfFileName, nameBuffer);
		if (status < 0)
			goto out;

		fontDir = dirname((char *) vbfFileName);
		if (!fontDir)
		{
			status = ERR_NOSUCHDIR;
			goto out;
		}
	}
	else
	{
		status = getFontNames(nameBuffer);
		if (status < 0)
			goto out;
	}

	if (!numFontNames)
		goto out;

	if (fontListParams)
		free(fontListParams);

	fontListParams = malloc(numFontNames * sizeof(listItemParameters));
	if (!fontListParams)
	{
		status = ERR_MEMORY;
		goto out;
	}

	buffPtr = nameBuffer;

	for (count = 0; count < numFontNames; count ++)
	{
		strncpy(fontListParams[count].text, buffPtr, WINDOW_MAX_LABEL_LENGTH);
		buffPtr += (strlen(fontListParams[count].text) + 1);
	}

	status = 0;

out:
	free(nameBuffer);
	return (status);
}


static void freeGlyphListParams(void)
{
	// Free any previously-allocated array of list parameters

	int count;

	if (glyphListParams)
	{
		for (count = 0; count < selectedFont->header.numGlyphs; count ++)
		{
			if (glyphListParams[count].iconImage.data)
				imageFree(&glyphListParams[count].iconImage);
		}

		free(glyphListParams);
		glyphListParams = NULL;
	}
}


static int getGlyphListParams(void)
{
	// Put the font glyphs into an array of list parameters

	int status = 0;
	int count;

	if (!selectedFont->header.numGlyphs)
		goto out;

	glyphListParams = malloc(selectedFont->header.numGlyphs *
		sizeof(listItemParameters));
	if (!glyphListParams)
	{
		status = ERR_MEMORY;
		goto out;
	}

	for (count = 0; count < selectedFont->header.numGlyphs; count ++)
	{
		sprintf(glyphListParams[count].text, "%04x\n%d",
			selectedFont->codes[count],
			charsetFromUnicode(selectedFont->header.charSet,
				selectedFont->codes[count]));

		status = bitmap2Image(count, &selectedFont->header, selectedFont->data,
			&glyphListParams[count].iconImage);
		if (status < 0)
			goto out;
	}

	status = 0;

out:
	return (status);
}


static int selectListFont(int selected)
{
	int status = 0;
	char *fileName = NULL;
	FILE *fontFile = NULL;

	freeGlyphListParams();

	fileName = malloc(MAX_PATH_NAME_LENGTH);
	if (!fileName)
		return (status = ERR_MEMORY);

	sprintf(fileName, "%s/%s", fontDir, fontListParams[selected].text);

	fontFile = fopen(fileName, "r");
	if (!fontFile)
	{
		error(_("Can't open %s for reading"), fileName);
		status = errno;
		goto out;
	}

	status = readFont(fontFile, &selectedFont->header, &selectedFont->codes,
		&selectedFont->data);

	fclose(fontFile);

	if (status < 0)
		goto out;

	getGlyphListParams();

	status = 0;

out:
	if (fileName)
		free(fileName);

	return (status);
}


static void updateGlyphList(void)
{
	windowComponentSetData(glyphList, glyphListParams,
		selectedFont->header.numGlyphs, 1 /* redraw */);
}


static int editGlyph(int selected)
{
	int status = 0;
	file imageFile;
	char *imageFileName = NULL;
	char *command = NULL;

	status = fileGetTemp(&imageFile);
	if (status < 0)
		return (status);

	fileClose(&imageFile);

	imageFileName = malloc(MAX_PATH_NAME_LENGTH);
	command = malloc(MAX_PATH_NAME_LENGTH);
	if (!imageFileName || !command)
	{
		status = ERR_MEMORY;
		goto out;
	}

	status = fileGetFullPath(&imageFile, imageFileName, MAX_PATH_NAME_LENGTH);
	if (status < 0)
		goto out;

	status = imageSave(imageFileName, IMAGEFORMAT_BMP,
		&glyphListParams[selected].iconImage);
	if (status < 0)
		goto out;

	sprintf(command, PATH_PROGRAMS "/imgedit -s %s", imageFileName);

	status = loaderLoadAndExec(command, privilege, 1 /* block */);

	if (status >= 0)
	{
		if (glyphListParams[selected].iconImage.data)
			imageFree(&glyphListParams[selected].iconImage);

		status = imageLoad(imageFileName, 0, 0,
			&glyphListParams[selected].iconImage);

		if (status >= 0)
			updateGlyphList();
	}

	fileDelete(imageFileName);

out:
	if (command)
		free(command);

	if (imageFileName)
		free(imageFileName);

	return (status);
}


static int save(int selected)
{
	int status = 0;
	int glyphBytes = 0;
	char *fileName = NULL;
	FILE *fontFile = NULL;
	int count;

	// Update the font data from the images
	glyphBytes = (((selectedFont->header.glyphWidth *
		selectedFont->header.glyphHeight) + 7) / 8);

	for (count = 0; count < selectedFont->header.numGlyphs; count ++)
	{
		// Convert it to bitmap data
		image2Bitmap((pixel *) glyphListParams[count].iconImage.data,
			glyphListParams[count].iconImage.width,
			selectedFont->header.glyphWidth, selectedFont->header.glyphHeight,
			(selectedFont->data + (count * glyphBytes)));
	}

	fileName = malloc(MAX_PATH_NAME_LENGTH);
	if (!fileName)
		return (status = ERR_MEMORY);

	sprintf(fileName, "%s/%s", fontDir, fontListParams[selected].text);

	fontFile = fopen(fileName, "w");
	if (!fontFile)
	{
		error(_("Can't open %s for writing"), fileName);
		status = errno;
		goto out;
	}

	// Write the font back to disk
	status = writeFont(fontFile, &selectedFont->header, selectedFont->codes,
		selectedFont->data);

	fclose(fontFile);

out:
	if (fileName)
		free(fileName);

	return (status);
}


static void refreshWindow(void)
{
	// We got a 'window refresh' event (probably because of a language switch),
	// so we need to update things

	// Re-get the language setting
	setlocale(LC_ALL, getenv("LANG"));
	textdomain("fontutil");

	// Re-get the character set
	if (getenv(ENV_CHARSET))
		windowSetCharSet(window, getenv(ENV_CHARSET));

	// Refresh the window title
	windowSetTitle(window, WINDOW_TITLE);
}


static void eventHandler(objectKey key, windowEvent *event)
{
	int selected = 0;

	// Check for window events.
	if (key == window)
	{
		// Check for window refresh
		if (event->type == EVENT_WINDOW_REFRESH)
			refreshWindow();

		// Check for the window being closed
		else if (event->type == EVENT_WINDOW_CLOSE)
			windowGuiStop();
	}

	else if ((key == fontList) && (event->type & EVENT_SELECTION))
	{
		if (windowComponentGetSelected(fontList, &selected) < 0)
			return;

		selectListFont(selected);
		updateGlyphList();
		windowComponentSetSelected(glyphList, 0);
	}

	else if (key == glyphList)
	{
		if ((event->type & EVENT_SELECTION) &&
			((event->type & EVENT_MOUSE_LEFTUP) ||
			((event->type & EVENT_KEY_DOWN) && (event->key == keyEnter))))
		{
			if (windowComponentGetSelected(glyphList, &selected) < 0)
				return;

			editGlyph(selected);
		}
	}

	else if (key == saveButton)
	{
		if (event->type & EVENT_MOUSE_LEFTUP)
		{
			if (windowComponentGetSelected(fontList, &selected) < 0)
				return;

			save(selected);
		}
	}
}


static int constructWindow(const char *vbfFileName)
{
	// If we are in graphics mode, make a window rather than operating on the
	// command line.

	int status = 0;
	objectKey buttonContainer = NULL;
	image buttonImage;
	componentParameters params;

	// We don't want to create the window before we know that there are some
	// fonts we can use
	status = getFontListParams(vbfFileName);
	if (status < 0)
		return (status);

	if (!numFontNames)
	{
		error("%s", _("No supported font files found"));
		return (status = ERR_NOSUCHFILE);
	}

	status = selectListFont(0);
	if (status < 0)
		return (status);

	// Create a new window
	window = windowNew(multitaskerGetCurrentProcessId(), WINDOW_TITLE);
	if (!window)
		return (status = ERR_NOCREATE);

	memset(&params, 0, sizeof(componentParameters));
	params.gridWidth = 1;
	params.gridHeight = 1;
	params.padTop = 5;
	params.padBottom = 5;
	params.padLeft = 5;
	params.orientationX = orient_left;
	params.orientationY = orient_top;
	params.flags = WINDOW_COMPFLAG_FIXEDWIDTH;

	// Create a list component for the font names
	fontList = windowNewList(window, windowlist_textonly, 10, 1, 0,
		fontListParams, numFontNames, &params);
	if (!fontList)
		return (status = ERR_NOCREATE);

	windowRegisterEventHandler(fontList, &eventHandler);
	windowComponentFocus(fontList);
	windowComponentSetSelected(fontList, 0);

	// Create a list component for the glyph names
	params.gridX += 1;
	params.flags = 0;
	glyphList = windowNewList(window, windowlist_icononly, 8, 8, 0,
		glyphListParams, selectedFont->header.numGlyphs, &params);
	if (!glyphList)
		return (status = ERR_NOCREATE);

	windowRegisterEventHandler(glyphList, &eventHandler);

	// Make a container for the buttons
	params.gridX += 1;
	params.padRight = 5;
	params.flags = (WINDOW_COMPFLAG_FIXEDWIDTH | WINDOW_COMPFLAG_FIXEDHEIGHT);
	buttonContainer = windowNewContainer(window, "buttonContainer", &params);
	if (!buttonContainer)
		return (status = ERR_NOCREATE);

	// Create a save button
	params.gridX = 0;
	params.padLeft = params.padRight = params.padTop = params.padBottom = 0;
	params.gridHeight = 1;
	params.flags = 0;
	imageLoad(PATH_SYSTEM_ICONS "/save.ico" , 0, 0, &buttonImage);
	saveButton = windowNewButton(buttonContainer,
		(buttonImage.data? NULL : _("Save")),
		(buttonImage.data? &buttonImage : NULL), &params);
	if (buttonImage.data)
		imageFree(&buttonImage);
	if (!saveButton)
		return (status = ERR_NOCREATE);

	windowRegisterEventHandler(saveButton, &eventHandler);

	// Register an event handler to catch window close events
	windowRegisterEventHandler(window, &eventHandler);

	windowSetVisible(window, 1);

	return (status = 0);
}


int main(int argc, char *argv[])
{
	int status = 0;
	const char *options = "acdefinprsTvx:?";
	const char *optSpec = "a:c:d:e:f:i:n:p:r:s:Tvx:";
	char opt;
	const char *vbfFileName = NULL;
	operation_type operation = operation_none;
	int code = -1;
	const char *convertFileName = NULL;
	const char *otherFileName = NULL;

	setlocale(LC_ALL, getenv(ENV_LANG));
	textdomain("fontutil");

	// Graphics enabled?
	graphics = graphicsAreEnabled();

	// What is my process id?
	processId = multitaskerGetCurrentProcessId();

	// What is my privilege level?
	privilege = multitaskerGetProcessPrivilege(processId);

	cmdName = argv[0];

	selectedFont = calloc(1, sizeof(vbf));
	if (!selectedFont)
	{
		error("%s", _("Couldn't get working memory"));
		status = ERR_MEMORY;
		goto out;
	}

	// If graphics are enabled, was text mode requested anyway?
	if (graphics)
	{
		// Check for text mode option
		while (strchr(options, (opt = getopt(argc, argv, optSpec))))
		{
			switch (opt)
			{
				case 'T':
					// Force text mode
					graphics = 0;
					break;

				case ':':
					error(_("Missing parameter for %s option"),
						argv[optind - 1]);
					usage();
					return (status = ERR_NULLPARAMETER);

				case '?':
					error(_("Unknown option '%c'"), optopt);
					usage();
					return (status = ERR_INVALID);
			}
		}

		// Reset getopt()
		optind = 0;
	}

	if (!graphics)
	{
		if (argc < 2)
		{
			usage();
			errno = ERR_ARGUMENTCOUNT;
			return (status = errno);
		}

		vbfFileName = argv[argc - 1];

		// Check more options
		while (strchr(options, (opt = getopt(argc, argv, optSpec))))
		{
			switch (opt)
			{
				case 'a':
					// Add a glyph
					if (!optarg)
					{
						error("%s", _("Missing code argument for '-a' "
							"option"));
						usage();
						return (status = ERR_NULLPARAMETER);
					}
					code = atoi(optarg);
					operation = operation_add;
					break;

				case 'c':
					// Set the charset
					if (!optarg)
					{
						error("%s", _("Missing charset argument for '-c' "
							"option"));
						usage();
						return (status = ERR_NULLPARAMETER);
					}
					strncpy(selectedFont->header.charSet, optarg,
						sizeof(selectedFont->header.charSet));
					if (operation == operation_none)
						operation = operation_update;
					break;

				case 'd':
					// Just dump out the font data
					if (optarg && (optarg != vbfFileName))
						code = atoi(optarg);
					operation = operation_dump;
					break;

				case 'e':
					// Export a font to an image file.
					if (!optarg)
					{
						error("%s", _("Missing image filename argument for "
							"'-e' option"));
						usage();
						return (status = ERR_NULLPARAMETER);
					}
					otherFileName = optarg;
					operation = operation_export;
					break;

				case 'f':
					// Another file name (depends on the operation)
					if (!optarg)
					{
						error("%s", _("Missing filename argument for '-f' "
							"option"));
						usage();
						return (status = ERR_NULLPARAMETER);
					}
					otherFileName = optarg;
					break;

				case 'i':
					// Import a new font from an image file.
					if (!optarg)
					{
						error("%s", _("Missing image filename argument for "
							"'-i' option"));
						usage();
						return (status = ERR_NULLPARAMETER);
					}
					otherFileName = optarg;
					operation = operation_import;
					break;

				case 'n':
					// Set the family
					if (!optarg)
					{
						error("%s", _("Missing family name argument for '-n' "
							"option"));
						usage();
						return (status = ERR_NULLPARAMETER);
					}
					strncpy(selectedFont->header.family, optarg,
						sizeof(selectedFont->header.family));
					if (operation == operation_none)
						operation = operation_update;
					break;

				case 'p':
					// Set the number of points
					if (!optarg)
					{
						error("%s", _("Missing points argument for '-p' "
							"option"));
						usage();
						return (status = ERR_NULLPARAMETER);
					}
					selectedFont->header.points = atoi(optarg);
					if (operation == operation_none)
						operation = operation_update;
					break;

				case 'r':
					// Remove a glyph
					if (!optarg)
					{
						error("%s", _("Missing code argument for '-r' "
							"option"));
						usage();
						return (status = ERR_NULLPARAMETER);
					}
					code = atoi(optarg);
					operation = operation_remove;
					break;

				case 's':
					// Add a style flag
					if (!optarg)
					{
						error("%s", _("Missing style argument for '-s' "
							"option"));
						usage();
						return (status = ERR_NULLPARAMETER);
					}
					if (!strcasecmp(optarg, "bold"))
					{
						selectedFont->header.flags |= FONT_STYLEFLAG_BOLD;
					}
					else if (!strcasecmp(optarg, "italic"))
					{
						selectedFont->header.flags |= FONT_STYLEFLAG_ITALIC;
					}
					else if (!strcasecmp(optarg, "fixed"))
					{
						selectedFont->header.flags |= FONT_STYLEFLAG_FIXED;
					}
					else
					{
						error(_("Unknown style argument %s"), optarg);
						usage();
						return (status = ERR_INVALID);
					}
					if (operation == operation_none)
						operation = operation_update;
					break;

				case 'T':
					break;

				case 'v':
					verbose = 1;
					break;

				case 'x':
					// Convert
					if (!optarg)
					{
						error("%s", _("Missing filename argument for '-x' "
							"option"));
						usage();
						return (status = ERR_NULLPARAMETER);
					}
					operation = operation_convert;
					convertFileName = optarg;
					break;

				case ':':
					error(_("Missing parameter for %s option"),
						argv[optind - 1]);
					usage();
					return (status = ERR_NULLPARAMETER);

				default:
					error(_("Unknown option '%c'"), optopt);
					usage();
					return (status = ERR_INVALID);
			}
		}

		switch (operation)
		{
			case operation_dump:
				status = dump(vbfFileName, code);
				break;

			case operation_convert:
				status = convert(convertFileName, vbfFileName);
				break;

			case operation_update:
				status = updateHeader(vbfFileName);
				break;

			case operation_import:
				status = import(otherFileName, vbfFileName);
				break;

			case operation_export:
				status = export(otherFileName, vbfFileName);
				break;

			case operation_add:
				if (!otherFileName)
				{
					error("%s", _("Missing image file (-f) argument to add "
						"(-a) operation"));
					usage();
					return (status = ERR_NULLPARAMETER);
				}
				status = addGlyph(code, otherFileName, vbfFileName);
				break;

			case operation_remove:
				status = removeGlyph(code, vbfFileName);
				break;

			default:
			case operation_none:
				error("%s", _("No operation specified"));
				status = ERR_INVALID;
				break;
		}
	}
	else // graphics
	{
		if (argc >= 2)
			vbfFileName = argv[argc - 1];

		// Make our window
		status = constructWindow(vbfFileName);
		if (status < 0)
			return (status);

		// Run the GUI
		windowGuiRun();

		// ...and when we come back...
		windowDestroy(window);

		status = 0;
	}

out:
	if (glyphListParams)
		freeGlyphListParams();

	if (strcmp(fontDir, PATH_SYSTEM_FONTS))
		free(fontDir);

	if (fontListParams)
		free(fontListParams);

	if (selectedFont)
	{
		if (selectedFont->codes)
			free(selectedFont->codes);

		if (selectedFont->data)
			free(selectedFont->data);

		free(selectedFont);
	}

	return (status);
}

