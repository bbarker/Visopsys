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
//  kernelImageBmp.c
//

// This file contains code for loading, saving, and converting images
// in the "device independent bitmap" (.bmp) format, commonly used in
// MS(R)-Windows(R), etc.

#include "kernelError.h"
#include "kernelFile.h"
#include "kernelGraphic.h"
#include "kernelImage.h"
#include "kernelLoader.h"
#include "kernelMalloc.h"
#include "kernelMemory.h"
#include <stdio.h>
#include <string.h>
#include <sys/bmp.h>


static int detect(const char *fileName, void *dataPtr, unsigned size,
	loaderFileClass *class)
{
	// This function returns 1 and fills the fileClass structure if the data
	// points to an BMP file.

	// Check params
	if (!fileName || !dataPtr || !class)
		return (0);

	if (size < 2)
		return (0);

	// See whether this file claims to be a bitmap file
	if (!strncmp(dataPtr, BMP_MAGIC, 2))
	{
		// We will say this is a BMP file.
		sprintf(class->className, "%s %s", FILECLASS_NAME_BMP,
			FILECLASS_NAME_IMAGE);
		class->class = (LOADERFILECLASS_BIN | LOADERFILECLASS_IMAGE);

		return (1);
	}
	else
	{
		return (0);
	}
}


static int load(unsigned char *imageFileData, int dataLength,
	int reqWidth __attribute__((unused)),
	int reqHeight __attribute__((unused)), image *loadImage)
{
	// Loads a .bmp file and returns it as an image.  The memory for this and
	// its data must be freed by the caller.

	int status = 0;
	bmpHeader *header = NULL;
	unsigned width = 0;
	unsigned height = 0;
	unsigned dataStart = 0;
	int compression = 0;
	int colors = 0;
	unsigned char *palette = NULL;
	unsigned fileOffset = 0;
	unsigned fileLineWidth = 0;
	unsigned pixelCounter = 0;
	unsigned pixelRowCounter = 0;
	unsigned char colorIndex = 0;
	pixel *imageData = NULL;
	int count1, count2;

	// Check params
	if (!imageFileData || !dataLength || !loadImage)
		return (status = ERR_NULLPARAMETER);

	// Point our header pointer at the start of the file
	header = (bmpHeader *)(imageFileData + 2);

	width = header->width;
	height = header->height;
	dataStart = header->dataStart;
	compression = header->compression;
	colors = header->colors;

	palette = (imageFileData + sizeof(bmpHeader) + 2);

	// Get a blank image of sufficient size
	status = kernelImageNew(loadImage, width, height);
	if (status < 0)
		return (status);

	imageData = loadImage->data;

	// Ok.  Now we need to loop through the bitmap data and turn each bit
	// of data into a pixel.  The method we use will depend on whether
	// the image is compressed, and if so, the method used.  Note that bitmap
	// data is "upside down" in the file.

	if (header->bitsPerPixel == BMP_BPP_32BIT)
	{
		// 32-bit bitmap.  Pretty simple, since our image structure's data
		// is a 24-bit bitmap (but the right way up).

		fileLineWidth = (width * 4);

		// This outer loop is repeated once for each row of pixels
		for (count1 = (height - 1); count1 >= 0; count1 --)
		{
			fileOffset = (dataStart + (count1 * fileLineWidth));

			// This inner loop is repeated for each pixel in a row
			for (pixelRowCounter = 0; pixelRowCounter < width;
				pixelRowCounter++)
			{
				imageData[pixelCounter].blue =
					imageFileData[fileOffset + (pixelRowCounter * 4)];
				imageData[pixelCounter].green =
					imageFileData[fileOffset + (pixelRowCounter * 4) + 1];
				imageData[pixelCounter++].red =
					imageFileData[fileOffset + (pixelRowCounter * 4) + 2];
			}
		}
	}

	else if (header->bitsPerPixel == BMP_BPP_24BIT)
	{
		// 24-bit bitmap.  Very simple, since our image structure's data
		// is a 24-bit bitmap (but the right way up).

		// There might be padding bytes at the end of a line in the file to
		// make each one have a multiple of 4 bytes
		fileLineWidth = (width * 3);
		if (fileLineWidth % 4)
			fileLineWidth = (fileLineWidth + (4 - (fileLineWidth % 4)));

		// This outer loop is repeated once for each row of pixels
		for (count1 = (height - 1); count1 >= 0; count1 --)
		{
			fileOffset = (dataStart + (count1 * fileLineWidth));

			// Copy a line of data from the file to our image
			memcpy((((void *) imageData) + ((height - count1 - 1) *
				(width * 3))), (imageFileData + fileOffset), (width * 3));
		}
	}

	else if (header->bitsPerPixel == BMP_BPP_256)
	{
		// 8-bit bitmap.  (256 colors)

		if (compression == BMP_COMP_NONE)
		{
			// No compression.  Each sequential byte of data in the file is an
			// index into the color palette (at the end of the header)

			// There might be padding bytes at the end of a line in the file to
			// make each one have a multiple of 4 bytes
			fileLineWidth = width;
			if (fileLineWidth % 4)
				fileLineWidth = (fileLineWidth + (4 - (fileLineWidth % 4)));

			// This outer loop is repeated once for each row of pixels
			for (count1 = (height - 1); count1 >= 0; count1 --)
			{
				fileOffset = (dataStart + (count1 * fileLineWidth));

				// This inner loop is repeated for each pixel in a row
				for (pixelRowCounter = 0; pixelRowCounter < width;
					pixelRowCounter++)
				{
					// Get the byte that indexes the color
					colorIndex = imageFileData[fileOffset + pixelRowCounter];

					if (colorIndex >= colors)
					{
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

		else if (compression == BMP_COMP_RLE8)
		{
			// 8-bit RLE compression.

			fileOffset = dataStart;

			// This outer loop is repeated once for each row of pixels
			for (count1 = (height - 1); count1 >= 0; count1 --)
			{
				int endOfBitmap = 0;

				pixelCounter = (count1 * width);

				for (pixelRowCounter = 0; pixelRowCounter < width;
					pixelRowCounter++)
				{
					int absoluteMode = 0;
					int endOfLine = 0;

					// Check for an escape code.
					if (!imageFileData[fileOffset])
					{
						fileOffset += 1;

						switch (imageFileData[fileOffset])
						{
							case 0:
								// Code for end-of-line.
								endOfLine = 1;
								break;

							case 1:
								// Code for end-of-bitmap.
								endOfBitmap = 1;
								break;

							case 2:
								// Code for delta.
								kernelError(kernel_error, "RLE bitmap deltas "
									"not yet supported");
								kernelImageFree((image *) &loadImage);
								return (status = ERR_NOTIMPLEMENTED);

							default:
								absoluteMode = 1;
								break;
						}

						if (endOfLine || endOfBitmap)
						{
							fileOffset += 1;
							break;
						}
					}

					int numBytes = imageFileData[fileOffset];

					if (absoluteMode)
					{
						// This is 'absolute mode'.

						for (count2 = 0; count2 < numBytes; count2++)
						{
							fileOffset += 1;

							// Get the byte that indexes the color
							colorIndex = imageFileData[fileOffset];

							if (colorIndex >= colors)
							{
								kernelError(kernel_error, "Illegal color "
									"index %d", colorIndex);
								kernelImageFree((image *) &loadImage);
								return (status = ERR_INVALID);
							}

							// Convert it to a pixel
							imageData[pixelCounter].blue =
								palette[colorIndex * 4];
							imageData[pixelCounter].green =
								palette[(colorIndex * 4) + 1];
							imageData[pixelCounter++].red =
								palette[(colorIndex * 4) + 2];
						}

						// If the number of bytes was not word-aligned, skip
						// padding bytes
						if (numBytes % 2)
							fileOffset += 1;
					}
					else
					{
						// This is 'encoded mode', a normal run.

						fileOffset += 1;

						// Get the byte that indexes the color
						colorIndex = imageFileData[fileOffset];

						if (colorIndex >= colors)
						{
							kernelError(kernel_error, "Illegal color index %d",
								colorIndex);
							kernelImageFree((image *) &loadImage);
							return (status = ERR_INVALID);
						}

						for (count2 = 0; count2 < numBytes; count2++)
						{
							// Convert it to a pixel
							imageData[pixelCounter].blue =
								palette[colorIndex * 4];
							imageData[pixelCounter].green =
								palette[(colorIndex * 4) + 1];
							imageData[pixelCounter++].red =
								palette[(colorIndex * 4) + 2];
						}
					}

					fileOffset += 1;
				}

				if (endOfBitmap)
					break;
			}
		}
		else
		{
			// Not supported.  Release the file data and image data memory
			kernelError(kernel_error, "Unsupported compression type %d",
				compression);
			kernelImageFree((image *) &loadImage);
			return (status = ERR_INVALID);
		}
	}

	else if (header->bitsPerPixel == BMP_BPP_MONO)
	{
		// Monochrome bitmap.  The palette contains 2 values.  Each bit of data
		// in the file is an index into the color palette (at the end of the
		// header).

		// There might be padding bytes at the end of a line in the file to
		// make each one have a multiple of 4 bytes
		fileLineWidth = ((width + 7) / 8);
		if (fileLineWidth % 4)
			fileLineWidth = (fileLineWidth + (4 - (fileLineWidth % 4)));

		// This outer loop is repeated once for each row of pixels
		for (count1 = (height - 1); count1 >= 0; count1 --)
		{
			fileOffset = (dataStart + (count1 * fileLineWidth));

			// This inner loop is repeated for each pixel in a row
			for (pixelRowCounter = 0; pixelRowCounter < width;
				pixelRowCounter++)
			{
				// Get the byte that indexes the color
				colorIndex =
					((imageFileData[fileOffset + (pixelRowCounter / 8)] &
					(0x80 >> (pixelRowCounter % 8))) > 0);

				if (colorIndex >= colors)
				{
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
		// Not supported.  Release the file data and image data memory
		kernelError(kernel_error, "Unsupported bits per pixel value %d",
			header->bitsPerPixel);
		kernelImageFree((image *) &loadImage);
		return (status = ERR_INVALID);
	}

	// Success
	return (status = 0);
}


static int save(const char *fileName, image *saveImage)
{
	// Saves a kernel image format to a .bmp file

	int status = 0;
	int padBytes = 0;
	bmpHeader header;
	unsigned dataSize = 0;
	unsigned char *fileData = NULL;
	unsigned char *imageData = NULL;
	file theFile;
	int count;

	// Do we need to pad each line of the image with extra bytes?  The file
	// data needs to be on doubleword boundaries.
	if ((saveImage->width * 3) % 4)
		padBytes = 4 - ((saveImage->width * 3) % 4);

	// The data size is number of lines, times line width + pad bytes
	dataSize = ((saveImage->width * 3) + padBytes) * saveImage->height;

	// Start filling in the bitmap header using the image information

	header.size = 2 + sizeof(bmpHeader) + dataSize;
	header.reserved = 0;
	header.dataStart = 2 + sizeof(bmpHeader);
	header.headerSize = 0x28;
	header.width = saveImage->width;
	header.height = saveImage->height;
	header.planes = 1;
	header.bitsPerPixel = BMP_BPP_24BIT;
	header.compression = BMP_COMP_NONE;
	header.dataSize = dataSize;
	header.hResolution = 7800;  // ?!? Whatever
	header.vResolution = 7800;  // ?!? Whatever
	header.importantColors = 0;

	// Get memory for the file
	fileData = kernelMalloc(header.size);
	if (!fileData)
	{
		kernelError(kernel_error, "Unable to allocate memory for bitmap file");
		return (status = ERR_MEMORY);
	}

	// Set a pointer to the start of the image data
	imageData = fileData + header.dataStart;

	for (count = (saveImage->height - 1); count >= 0 ; count --)
	{
		memcpy(imageData, (saveImage->data + (count * (saveImage->width * 3))),
			(saveImage->width * 3));
		memset((imageData + (saveImage->width * 3)), 0, padBytes);

		// Move to the next line
		imageData += ((saveImage->width * 3) + padBytes);
	}

	// This needs to be set after we've processed the data
	header.colors = 0;

	// Now copy the 'magic number' into the file area
	fileData[0] = 'B'; fileData[1] = 'M';

	// Copy the header data into the file area
	memcpy((fileData + 2), &header, sizeof(bmpHeader));

	// Now create/open the file stream for writing
	status = kernelFileOpen(fileName, (OPENMODE_WRITE | OPENMODE_TRUNCATE |
		OPENMODE_CREATE), &theFile);
	if (status < 0)
	{
		kernelError(kernel_error, "Unable to open %s for writing", fileName);
		// Free the file data
		kernelFree(fileData);
		return (status);
	}

	// Write the file
	status = kernelFileWrite(&theFile, 0, (header.size / theFile.blockSize) +
		((header.size % theFile.blockSize)? 1 : 0), fileData);

	// Free the file data
	kernelFree(fileData);

	if (status < 0)
	{
		kernelError(kernel_error, "Unable to write %s", fileName);
		return (status);
	}

	// Close the file
	kernelFileClose(&theFile);

	// Done
	return (status = 0);
}


kernelFileClass bmpFileClass = {
	FILECLASS_NAME_BMP,
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

kernelFileClass *kernelFileClassBmp(void)
{
	// The loader will call this function so that we can return a structure
	// for managing BMP files

	static int filled = 0;

	if (!filled)
	{
		bmpFileClass.image.load = &load;
		bmpFileClass.image.save = &save;
		filled = 1;
	}

	return (&bmpFileClass);
}

