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
//  kernelImagePpm.c
//

// Code in this file is (c) 2014 Giuseppe Gatta

// This file contains code for loading, saving, and converting images
// in the "portable pixmap format" (.ppm) format.

#include "kernelError.h"
#include "kernelFile.h"
#include "kernelImage.h"
#include "kernelLoader.h"
#include "kernelMalloc.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>


static int detect(const char *fileName, void *dataPtr, unsigned size,
	loaderFileClass *class)
{
	// This function returns 1 and fills the fileClass structure if the data
	// points to an PPM file.

	if (!fileName || !dataPtr || !class)
		return (0);

	if (size < 2)
		return (0);

	// See whether this file claims to be a PPM file
	if (!strncmp(dataPtr, "P3", 2) || !strncmp(dataPtr, "P6", 2))
	{
		// We will say this is a PPM file.
		sprintf(class->className, "%s %s", FILECLASS_NAME_PPM,
			FILECLASS_NAME_IMAGE);
		class->class = (LOADERFILECLASS_BIN | LOADERFILECLASS_IMAGE);

		return (1);
	}

	return (0);
}


static void skipWhiteSpace(unsigned char *p, int *pos, int maxLen)
{
	while (*pos < maxLen)
	{
		if (p[*pos] == '#')
		{
			while ((*pos < maxLen) && (p[*pos] != '\n'))
				(*pos)++;
		}
		else if (isspace(p[*pos]))
			(*pos)++;
		else
			break;;
	}
}


static int getNextValue(unsigned char *p, int *pos, int maxLen)
{
	int l = 0;
	char buf[16];

	skipWhiteSpace(p, pos, maxLen);

	while ((*pos < maxLen) && isdigit(p[*pos]) && (l < 15))
		buf[l++] = p[(*pos)++];

	buf[l] = '\0';

	if (!strlen(buf))
		return (0);

	return (atoi(buf));
}


static unsigned char getByte(unsigned char *p, int *pos, int maxLen)
{
	if (*pos < maxLen)
		return (p[(*pos)++]);

	return (0);
}


static unsigned short getShort(unsigned char *p, int *pos, int maxLen)
{
	unsigned short r = 0;

	if (*pos < maxLen)
		r |= (p[(*pos)++] << 8);
	if (*pos < maxLen)
		r |= p[(*pos)++];

	return (r);
}


static int load(unsigned char *imageFileData, int dataLength,
	int reqWidth __attribute__((unused)),
	int reqHeight __attribute__((unused)), image *loadImage)
{
	int status = 0;
	unsigned int x, y;
	unsigned int r, g, b;
	pixel *imageData = NULL;
	unsigned int width, height, maxCompValue;
	int pos = 0;
	 // If magic is 'P6', this is a binary PPM
	int binary = (imageFileData[1] == '6');
	double factor;

	// Check params
	if (!imageFileData || !dataLength || !loadImage)
		return (status = ERR_NULLPARAMETER);

	pos = 2;

	width = getNextValue(imageFileData, &pos, dataLength);
	height = getNextValue(imageFileData, &pos, dataLength);
	maxCompValue = getNextValue(imageFileData, &pos, dataLength);

	if (!width || !height || !maxCompValue || (maxCompValue > 0xFFFF))
		return (status = ERR_BADDATA);

	status = kernelImageNew(loadImage, width, height);
	if (status < 0)
		return (status);

	imageData = loadImage->data;

	factor = (255 / maxCompValue);

	// Skip a single whitespace character
	if (isspace(imageFileData[pos]))
		pos += 1;

	for (y = 0; y < height; y++)
	{
		for (x = 0; ((x < width) && (pos < dataLength)); x++, imageData++)
		{
			if (binary)
			{
				if (maxCompValue > 255)
				{
					r = getShort(imageFileData, &pos, dataLength);
					g = getShort(imageFileData, &pos, dataLength);
					b = getShort(imageFileData, &pos, dataLength);
				}
				else
				{
					r = getByte(imageFileData, &pos, dataLength);
					g = getByte(imageFileData, &pos, dataLength);
					b = getByte(imageFileData, &pos, dataLength);
				}
			}
			else
			{
				r = getNextValue(imageFileData, &pos, dataLength);
				g = getNextValue(imageFileData, &pos, dataLength);
				b = getNextValue(imageFileData, &pos, dataLength);
			}

			if (r)
				r = (((r + 1) * factor) - 1);
			if (g)
				g = (((g + 1) * factor) - 1);
			if (b)
				b = (((b + 1) * factor) - 1);

			imageData->red = r;
			imageData->green = g;
			imageData->blue = b;
		}
	}

	return (status = 0);
}


static int save(const char *fileName, image *saveImage)
{
	// Saves a kernel image format to a .ppm file
	file theFile;
	unsigned char *fileData;
	unsigned int dataPtr = 0;
	unsigned int x, px;
	pixel *savePixels;
	int status = 0;

	fileData = kernelMalloc((saveImage->width * saveImage->height * 3) + 512);
	if (!fileData)
	{
		kernelError(kernel_error, "Unable to allocate memory for PPM file");
		return (status = ERR_MEMORY);
	}

	fileData[dataPtr++] = 'P';
	fileData[dataPtr++] = '6';
	fileData[dataPtr++] = '\n';
	itoa(saveImage->width, (char*) &fileData[dataPtr]);
	while (fileData[++dataPtr]);
	fileData[dataPtr++] = ' ';
	itoa(saveImage->height, (char*) &fileData[dataPtr]);
	while (fileData[++dataPtr]);
	fileData[dataPtr++] = '\n';
	fileData[dataPtr++] = '2';
	fileData[dataPtr++] = '5';
	fileData[dataPtr++] = '5';
	fileData[dataPtr++] = '\n';

	for (x = 0, px = (saveImage->width * saveImage->height),
		savePixels = saveImage->data; x < px; x++)
	{
		fileData[dataPtr++] = savePixels[x].red;
		fileData[dataPtr++] = savePixels[x].green;
		fileData[dataPtr++] = savePixels[x].blue;
	}

	status = kernelFileOpen(fileName, (OPENMODE_WRITE | OPENMODE_TRUNCATE |
		OPENMODE_CREATE), &theFile);
	if (status < 0)
	{
		kernelError(kernel_error, "Unable to open %s for writing", fileName);
		kernelFree(fileData);
		return (status);
	}

	status = kernelFileWrite(&theFile, 0, (dataPtr / theFile.blockSize) +
		((dataPtr % theFile.blockSize)? 1 : 0), fileData);

	kernelFree(fileData);

	if (status < 0)
	{
		kernelError(kernel_error, "Unable to write %s", fileName);
		kernelFileClose(&theFile);
		return (status);
	}

	status = kernelFileSetSize(&theFile, dataPtr);

	if (status < 0)
	{
		kernelError(kernel_error, "Cannot set size for %s", fileName);
		kernelFileClose(&theFile);
		return (status);
	}

	kernelFileClose(&theFile);

	return (status = 0);
}


kernelFileClass ppmFileClass = {
	FILECLASS_NAME_PPM,
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


kernelFileClass *kernelFileClassPpm(void)
{
	// The loader will call this function so that we can return a structure
	// for managing PPM files

	static int filled = 0;

	if (!filled)
	{
		ppmFileClass.image.load = &load;
		ppmFileClass.image.save = &save;
		filled = 1;
	}

	return (&ppmFileClass);
}

