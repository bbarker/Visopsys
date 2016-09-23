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
//  kernelImageIco.c
//

// This file contains code for manipulating windows .ico format icon files.

#include "kernelDebug.h"
#include "kernelError.h"
#include "kernelGraphic.h"
#include "kernelImage.h"
#include "kernelLoader.h"
#include "kernelMalloc.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/bmp.h>
#include <sys/ico.h>
#include <sys/png.h>


#ifdef DEBUG
static void debugIcoHeader(icoHeader *header)
{
	kernelDebug(debug_misc, "ICO header:\n"
		"  reserved=%d\n"
		"  type=%d\n"
		"  numIcons=%d", header->reserved, header->type, header->numIcons);
}

static void debugIcoEntry(icoEntry *entry)
{
	kernelDebug(debug_misc, "ICO entry:\n"
		"  width=%d\n"
		"  height=%d\n"
		"  colorCount=%d\n"
		"  reserved=%d\n"
		"  planes=%d\n"
		"  bitCount=%d\n"
		"  size=%u\n"
		"  fileOffset=%u", (entry->width? entry->width : 256),
		(entry->height? entry->height : 256), entry->colorCount,
		entry->reserved, entry->planes, entry->bitCount, entry->size,
		entry->fileOffset);
}

static void debugIcoInfoHeader(icoInfoHeader *info)
{
	kernelDebug(debug_misc, "ICO entry info header:\n"
		"  headerSize=%u\n"
		"  width=%u\n"
		"  height=%u\n"
		"  planes=%d\n"
		"  bitsPerPixel=%d\n"
		"  compression=%u\n"
		"  dataSize=%u\n"
		"  hResolution=%u\n"
		"  vResolution=%u\n"
		"  colors=%u\n"
		"  importantColors=%u", info->headerSize, info->width, info->height,
		info->planes, info->bitsPerPixel, info->compression, info->dataSize,
		info->hResolution, info->vResolution, info->colors,
		info->importantColors);
}
#else
	#define debugIcoHeader(header) do { } while (0)
	#define debugIcoEntry(entry) do { } while (0)
	#define debugIcoInfoHeader(info) do { } while (0)
#endif


static int detect(const char *fileName, void *dataPtr, unsigned dataSize,
	loaderFileClass *class)
{
	// This function returns 1 and fills the fileClass structure if the data
	// points to an ICO file.

	icoHeader *header = dataPtr;
	icoEntry *entry = NULL;
	int count;

	if (!fileName || !dataPtr || !class)
		return (0);

	kernelDebug(debug_misc, "ICO detect %s", fileName);

	// Make sure there's enough data here for our detection
	if (dataSize < (sizeof(icoHeader) + sizeof(icoEntry)))
		return (0);

	// See whether this file seems to be an .ico file
	if (!header->reserved && (header->type == 1) && header->numIcons)
	{
		debugIcoHeader(header);

		// We will search up to 3 entries for valid BMP values
		for (count = 0; count < min(2, header->numIcons); count ++)
		{
			entry = &header->entries[count];

			debugIcoEntry(entry);

			if (!entry->reserved &&
				((entry->planes == 0) || (entry->planes == 1)) &&
				((entry->bitCount == BMP_BPP_MONO) ||
					(entry->bitCount == BMP_BPP_16) ||
					(entry->bitCount == BMP_BPP_256) ||
					(entry->bitCount == BMP_BPP_16BIT) ||
					(entry->bitCount == BMP_BPP_24BIT) ||
					(entry->bitCount == BMP_BPP_32BIT)))
			{
				// We will say this is an ICO file.
				sprintf(class->className, "%s %s", FILECLASS_NAME_ICO,
					FILECLASS_NAME_IMAGE);
				class->class = (LOADERFILECLASS_BIN | LOADERFILECLASS_IMAGE);
				class->subClass = LOADERFILESUBCLASS_ICO;
				return (1);
			}
		}
	}

	return (0);
}


static int load(unsigned char *imageFileData, int dataSize, int reqWidth,
	int reqHeight, image *loadImage)
{
	// Processes the data from a raw .ico file and returns it as an image in
	// the closest possible dimensions to those requested (or else, the biggest
	// image if no dimensions are specified).  The memory  must be freed by
	// the caller.

	int status = 0;
	icoHeader *fileHeader = NULL;
	icoEntry *entry = NULL;
	unsigned width = 0;
	unsigned height = 0;
	unsigned size = 0;
	icoInfoHeader *info = NULL;
	unsigned dataStart = 0;
	unsigned colorCount = 0;
	unsigned char *palette = NULL;
	unsigned char *xorBitmap = NULL;
	unsigned char *andBitmap = NULL;
	pixel *imageData = NULL;
	unsigned fileOffset = 0;
	unsigned fileLineWidth = 0;
	unsigned pixelRowCounter = 0;
	unsigned char colorIndex = 0;
	unsigned pixelCounter = 0;
	unsigned tmpWidth, tmpHeight;
	int count;

	if (!imageFileData || !dataSize || !loadImage)
		return (status = ERR_NULLPARAMETER);

	kernelDebug(debug_misc, "ICO load, dataSize=%d", dataSize);

	// Point our header pointer at the start of the file
	fileHeader = (icoHeader *) imageFileData;

	if (!fileHeader->numIcons)
		return (status = ERR_NODATA);

	// Loop through the icon entries.  If a desired height and width was
	// specified, pick the one that's closest.  Otherwise, pick the largest
	// ("highest res") one
	for (count = 0; count < fileHeader->numIcons; count ++)
	{
		// If it's a PNG image rather than a BMP, skip it
		if (*((unsigned *)(imageFileData +
			fileHeader->entries[count].fileOffset)) == PNG_MAGIC1)
		{
			continue;
		}

		tmpWidth = 256;
		if (fileHeader->entries[count].width)
			tmpWidth = fileHeader->entries[count].width;
		tmpHeight = 256;
		if (fileHeader->entries[count].height)
			tmpHeight = fileHeader->entries[count].height;

		if ((reqWidth && reqHeight &&
				(abs((tmpWidth * tmpHeight) - (reqWidth * reqHeight)) <
					abs((width * height) - (reqWidth * reqHeight)))) ||
			(!reqWidth && !reqHeight && ((tmpWidth * tmpHeight) > size)))
		{
			kernelDebug(debug_misc, "ICO choosing entry %d", count);
			entry = &fileHeader->entries[count];
			width = tmpWidth;
			height = tmpHeight;
			size = (width * height);
		}
	}

	if (!entry)
		return (status = ERR_NOSUCHENTRY);

	debugIcoEntry(entry);

	info = ((void *) imageFileData + entry->fileOffset);

	debugIcoInfoHeader(info);

	dataStart = (entry->fileOffset + sizeof(icoInfoHeader) + (colorCount * 4));

	if (info->bitsPerPixel == BMP_BPP_256)
	{
		colorCount = 256;
		if (entry->colorCount)
			colorCount = entry->colorCount;
	}

	palette = ((void *) info + info->headerSize);
	xorBitmap = (palette + (colorCount * 4));
	andBitmap = (xorBitmap + ((size * info->bitsPerPixel) / 8) +
		(((size * info->bitsPerPixel) % 8)? 1 : 0));

	// Get a blank image of sufficient size
	status = kernelImageNew(loadImage, width, height);
	if (status < 0)
		return (status);

	imageData = loadImage->data;

	// Ok.  Now we need to loop through the bitmap data and turn each bit of
	// data into a pixel.  Note that bitmap data is "upside down" in the file.

	if (info->bitsPerPixel == BMP_BPP_32BIT)
	{
		// 32-bit bitmap.  Pretty simple, since our image structure's data
		// is a 24-bit bitmap (but the right way up).

		loadImage->alpha = kernelMalloc(size * sizeof(float));
		if (!loadImage->alpha)
			return (status = ERR_MEMORY);

		fileLineWidth = (width * 4);

		// This outer loop is repeated once for each row of pixels
		for (count = (height - 1); count >= 0; count --)
		{
			fileOffset = (dataStart + (count * fileLineWidth));

			// This inner loop is repeated for each pixel in a row
			for (pixelRowCounter = 0; pixelRowCounter < width;
				pixelRowCounter++)
			{
				imageData[pixelCounter].blue =
					imageFileData[fileOffset + (pixelRowCounter * 4)];
				imageData[pixelCounter].green =
					imageFileData[fileOffset + (pixelRowCounter * 4) + 1];
				imageData[pixelCounter].red =
					imageFileData[fileOffset + (pixelRowCounter * 4) + 2];
				loadImage->alpha[pixelCounter++] = ((float)
					imageFileData[fileOffset + (pixelRowCounter * 4) + 3] /
					(float) 0xFF);
			}
		}
	}
	else if (info->bitsPerPixel == BMP_BPP_24BIT)
	{
		// 24-bit bitmap.  Very simple, since our image structure's data
		// is a 24-bit bitmap (but the right way up).

		// There might be padding bytes at the end of a line in the file to
		// make each one have a multiple of 4 bytes
		fileLineWidth = (width * 3);
		if (fileLineWidth % 4)
			fileLineWidth = (fileLineWidth + (4 - (fileLineWidth % 4)));

		// This outer loop is repeated once for each row of pixels
		for (count = (height - 1); count >= 0; count --)
		{
			fileOffset = (dataStart + (count * fileLineWidth));

			// Copy a line of data from the file to our image
			memcpy((((void *) imageData) + ((height - count - 1) *
				(width * 3))), ((void *) imageFileData + fileOffset),
				(width * 3));
		}
	}
	else if (info->bitsPerPixel == BMP_BPP_256)
	{
		// 8-bit bitmap.  (256 colors)

		if (info->compression == BMP_COMP_NONE)
		{
			// No compression.  Each sequential byte of data in the file is an
			// index into the color palette (at the end of the header)

			// There might be padding bytes at the end of a line in the file to
			// make each one have a multiple of 4 bytes
			fileLineWidth = width;
			if (fileLineWidth % 4)
				fileLineWidth = (fileLineWidth + (4 - (fileLineWidth % 4)));

			// This outer loop is repeated once for each row of pixels
			for (count = (height - 1); count >= 0; count --)
			{
				fileOffset = (dataStart + (count * fileLineWidth));

				// This inner loop is repeated for each pixel in a row
				for (pixelRowCounter = 0; pixelRowCounter < width;
					pixelRowCounter++)
				{
					// Get the byte that indexes the color
					colorIndex = imageFileData[fileOffset + pixelRowCounter];

					if (colorIndex >= colorCount)
					{
						kernelError(kernel_error, "Illegal color index %d",
							colorIndex);
						kernelImageFree((image *) &loadImage);
						return (status = ERR_INVALID);
					}

					// Convert it to a pixel
					imageData[pixelCounter].blue = palette[colorIndex * 4];
					imageData[pixelCounter].green =
						palette[(colorIndex * 4) + 1];
					imageData[pixelCounter++].red =
						palette[(colorIndex * 4) + 2];
				}
			}
		}
		else
		{
			// Not supported.  Release the image data memory
			kernelError(kernel_error, "RLE compression not supported");
			kernelImageFree((image *) &loadImage);
			return (status = ERR_INVALID);
		}
	}
	else
	{
		// Not supported.  Release the image data memory
		kernelError(kernel_error, "Unsupported bit depth %d",
			info->bitsPerPixel);
		kernelImageFree((image *) &loadImage);
		return (status = ERR_INVALID);
	}

	if ((info->bitsPerPixel == BMP_BPP_24BIT) ||
		(info->bitsPerPixel == BMP_BPP_256))
	{
		// Process the XOR-bitmap for BPP < 32
	}

	// Process the AND-bitmap (for transparency)
	fileLineWidth = (((width + 31) / 32) * 4);
	for (pixelCounter = 0, count = (height - 1); count >= 0; count --)
	{
		for (pixelRowCounter = 0; pixelRowCounter < width; pixelRowCounter ++,
			pixelCounter ++)
		{
			if (andBitmap[(count * fileLineWidth) + (pixelRowCounter / 8)] &
				(0x80 >> (pixelRowCounter % 8)))
			{
				imageData[pixelCounter].blue = 0;
				imageData[pixelCounter].green = 0xFF;
				imageData[pixelCounter].red = 0;
			}
		}
	}

	// Set the image's info fields
	loadImage->width = width;
	loadImage->height = height;

	// Assign the image data to the image
	loadImage->data = imageData;

	return (0);
}


kernelFileClass icoFileClass = {
	FILECLASS_NAME_ICO,
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

kernelFileClass *kernelFileClassIco(void)
{
	// The loader will call this function so that we can return a structure
	// for managing ICO files

	static int filled = 0;

	if (!filled)
	{
		icoFileClass.image.load = &load;
		filled = 1;
	}

	return (&icoFileClass);
}

