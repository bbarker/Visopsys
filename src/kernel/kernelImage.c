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
//  kernelImage.c
//

// This file contains code for loading, saving, and converting images
// with various file formats

#include "kernelImage.h"
#include "kernelDebug.h"
#include "kernelError.h"
#include "kernelGraphic.h"
#include "kernelLoader.h"
#include "kernelMalloc.h"
#include "kernelMemory.h"
#include "kernelParameters.h"
#include <stdlib.h>
#include <string.h>

extern color kernelDefaultBackground;


static inline void bilinearInterpolation(double distanceX, double distanceY,
	pixel **src, float **srcAlpha, pixel *dest, float *destAlpha)
{
	double row0red = (((1.0 - distanceX) * src[0]->red) + (distanceX *
		src[1]->red));
	double row0green = (((1.0 - distanceX) * src[0]->green) + (distanceX *
		src[1]->green));
	double row0blue = (((1.0 - distanceX) * src[0]->blue) + (distanceX *
		src[1]->blue));
	double row1red = (((1.0 - distanceX) * src[1]->red) + (distanceX *
		src[2]->red));
	double row1green = (((1.0 - distanceX) * src[1]->green) + (distanceX *
		src[2]->green));
	double row1blue = (((1.0 - distanceX) * src[1]->blue) + (distanceX *
		src[2]->blue));

	dest->red = (((1.0 - distanceY) * row0red) + (distanceY * row1red));
	dest->green = (((1.0 - distanceY) * row0green) + (distanceY * row1green));
	dest->blue = (((1.0 - distanceY) * row0blue) + (distanceY * row1blue));

	// Are we also interpolating the alpha channel?
	if (srcAlpha && destAlpha)
	{
		double row0alpha = (((1.0 - distanceX) * *srcAlpha[0]) + (distanceX *
			*srcAlpha[1]));
		double row1alpha = (((1.0 - distanceX) * *srcAlpha[1]) + (distanceX *
			*srcAlpha[2]));
		*destAlpha = (((1.0 - distanceY) * row0alpha) + (distanceY *
			row1alpha));
	}
}


static int imageCopy(image *srcImage, image *destImage, int kernel)
{
	// Given an image, make a copy of it.  If 'kernel' is non-zero, use kernel
	// memory for the new image.

	int status = 0;

	// Copy the image structure
	memcpy(destImage, srcImage, sizeof(image));

	// Get new memory
	if (kernel)
	{
		destImage->data = kernelMalloc(destImage->dataLength);
		destImage->isMalloc = 1;
	}
	else
	{
		destImage->data = kernelMemoryGet(destImage->dataLength,
			"image data");
	}

	if (!destImage->data)
		return (status = ERR_MEMORY);

	// Copy the data
	memcpy(destImage->data, srcImage->data, destImage->dataLength);

	// Make a copy of the alpha channel data, if it exists
	if (srcImage->alpha)
	{
		destImage->alpha = kernelMalloc(destImage->pixels * sizeof(float));
		if (destImage->alpha)
		{
			// Copy the data
			memcpy(destImage->alpha, srcImage->alpha, (destImage->pixels *
				sizeof(float)));
		}
	}

	return (status = 0);
}


/////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////
//
// Below here, the functions are exported for external use
//
/////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////

int kernelImageNew(image *blankImage, unsigned width, unsigned height)
{
	// This allocates a new image of the specified size, with the default
	// background color.

	int status = 0;
	pixel *p = NULL;
	unsigned count;

	// Check parameters
	if (!blankImage)
		return (status = ERR_NULLPARAMETER);

	memset(blankImage, 0, sizeof(image));
	blankImage->type = IMAGETYPE_COLOR;
	blankImage->pixels = (width * height);
	blankImage->width = width;
	blankImage->height = height;
	blankImage->dataLength = (blankImage->pixels * sizeof(pixel));

	blankImage->data = kernelMemoryGet(blankImage->dataLength, "image data");
	if (!blankImage->data)
		return (status = ERR_MEMORY);

	// Make each pixel be our background color
	p = blankImage->data;
	for (count = 0; count < blankImage->pixels; count ++)
	{
		p[count].red = kernelDefaultBackground.red;
		p[count].green = kernelDefaultBackground.green;
		p[count].blue = kernelDefaultBackground.blue;
	}

	return (status = 0);
}


int kernelImageFree(image *freeImage)
{
	// Frees memory allocated for image data (but does not deallocate the
	// image structure itself).

	int status = 0;

	// Check parameters
	if (!freeImage)
		return (status = ERR_NULLPARAMETER);

	if (freeImage->data)
	{
		if (freeImage->isMalloc)
			kernelFree(freeImage->data);
		else
			kernelMemoryRelease(freeImage->data);

		freeImage->data = NULL;
	}

	if (freeImage->alpha)
	{
		kernelFree(freeImage->alpha);
		freeImage->alpha = NULL;
	}

	return (status = 0);
}


int kernelImageLoad(const char *fileName, unsigned reqWidth,
	unsigned reqHeight, image *loadImage)
{
	int status = 0;
	file theFile;
	unsigned char *imageFileData = NULL;
	loaderFileClass loaderClass;
	kernelFileClass *fileClassDriver = NULL;

	// Check params
	if (!fileName || !loadImage)
		return (status = ERR_NULLPARAMETER);

	memset(loadImage, 0, sizeof(image));

	// Load the image file into memory
	imageFileData = kernelLoaderLoad(fileName, &theFile);
	if (!imageFileData)
		return (status = ERR_NOSUCHENTRY);

	// Get the file class of the file.
	fileClassDriver = kernelLoaderClassify(fileName, imageFileData,
		theFile.size, &loaderClass);
	if (!fileClassDriver)
	{
		kernelError(kernel_error, "File type of %s is unknown", fileName);
		status = ERR_INVALID;
		goto out;
	}

	// Is it an image?
	if (!(loaderClass.type & LOADERFILECLASS_IMAGE))
	{
		kernelError(kernel_error, "%s is not a recognized image format",
			fileName);
		status = ERR_INVALID;
		goto out;
	}

	if (!fileClassDriver->image.load)
	{
		status = ERR_NOTIMPLEMENTED;
		goto out;
	}

	// Call the appropriate 'load' function
	status = fileClassDriver->image.load(imageFileData, theFile.size,
		reqWidth, reqHeight, loadImage);

	if (status >= 0)
	{
		if ((reqWidth && (loadImage->width != reqWidth)) ||
			(reqHeight && (loadImage->height != reqHeight)))
		{
			if (!reqWidth)
				reqWidth = loadImage->width;
			if (!reqHeight)
				reqHeight = loadImage->height;

			kernelImageResize(loadImage, reqWidth, reqHeight);
		}
	}

out:
	kernelMemoryRelease(imageFileData);
	return (status);
}


int kernelImageSave(const char *fileName, int format, image *saveImage)
{
	int status = 0;
	const char *fileClassName = NULL;
	kernelFileClass *fileClassDriver = NULL;

	// Check params
	if (!fileName || !saveImage)
		return (status = ERR_NULLPARAMETER);

	switch (format)
	{
		case IMAGEFORMAT_BMP:
			fileClassName = FILECLASS_NAME_BMP;
			break;

		case IMAGEFORMAT_ICO:
			fileClassName = FILECLASS_NAME_ICO;
			break;

		case IMAGEFORMAT_JPG:
			fileClassName = FILECLASS_NAME_JPG;
			break;

		case IMAGEFORMAT_PPM:
			fileClassName = FILECLASS_NAME_PPM;
			break;

		default:
			kernelError(kernel_error, "Image format %d is unknown", format);
			return (status = ERR_NULLPARAMETER);
	}

	// Get the file class for the specified format
	fileClassDriver = kernelLoaderGetFileClass(fileClassName);
	if (!fileClassDriver)
		return (status = ERR_INVALID);

	if (!fileClassDriver->image.save)
	{
		kernelError(kernel_error, "Saving as type \"%s\" is not supported",
			fileClassName);
		return (status = ERR_NOTIMPLEMENTED);
	}

	// Call the appropriate 'save' function
	return (fileClassDriver->image.save(fileName, saveImage));
}


int kernelImageResize(image *resizeImage, unsigned width, unsigned height)
{
	// Given an image and new width and height values, resize it using a
	// bilinear interpolation algorithm.

	int status = 0;
	image newImage;
	double ratioX = 0;
	double ratioY = 0;
	unsigned destX = 0;
	unsigned destY = 0;
	double srcXdbl = 0;
	double srcYdbl = 0;
	int srcX = 0;
	int srcY = 0;
	int srcIndex = 0;
	int destIndex = 0;
	double distanceX = 0;
	double distanceY = 0;
	pixel *srcPixels = NULL;
	pixel *srcArea[4];
	float *srcAlpha[4];
	pixel *destPixels = NULL;
	int kernImage = 0;

	// Check parameters
	if (!resizeImage)
		return (status = ERR_NULLPARAMETER);

	kernelDebug(debug_misc, "Image resize %ux%u -> %ux%u", resizeImage->width,
		resizeImage->height, width, height);

	if ((resizeImage->width == width) && (resizeImage->height == height))
		return (status = 0);

	// Get an image of the new size
	status = kernelImageNew(&newImage, width, height);
	if (status < 0)
		return (status);

	if (resizeImage->alpha)
	{
		newImage.alpha = kernelMalloc(newImage.pixels * sizeof(float));
		if (!newImage.alpha)
			return (status = ERR_MEMORY);
	}

	newImage.type = resizeImage->type;
	PIXEL_COPY(&resizeImage->transColor, &newImage.transColor);

	// Determine the width and height ratios of the new size.
	ratioX = ((double) resizeImage->width / (double) width);
	ratioY = ((double) resizeImage->height / (double) height);

	kernelDebug(debug_misc, "Image resize ratio %fx%f", ratioX, ratioY);
	if ((ratioX < 0) || (ratioY < 0))
		kernelDebugError("Ratio seems strange");

	srcPixels = (pixel *) resizeImage->data;
	destPixels = (pixel *) newImage.data;

	for (destY = 0; destY < height; destY ++)
	{
		for (destX = 0; destX < width; destX ++)
		{
			srcXdbl = ((ratioX * (((double) destX) + 0.5)) + 0.5);
			srcYdbl = ((ratioY * (((double) destY) + 0.5)) + 0.5);
			srcX = (int) srcXdbl;
			srcY = (int) srcYdbl;

			// Don't sample outside the bounds of the source image.
			srcX = min(srcX, ((int) resizeImage->width - 2));
			srcY = min(srcY, ((int) resizeImage->height - 2));

			srcIndex = ((srcY * resizeImage->width) + srcX);
			destIndex = ((destY * width) + destX);

			distanceX = (srcXdbl - (double) srcX);
			distanceY = (srcYdbl - (double) srcY);

			srcArea[0] = &srcPixels[srcIndex];
			srcArea[1] = &srcPixels[srcIndex + 1];
			srcArea[2] = &srcPixels[srcIndex + resizeImage->width];
			srcArea[3] = &srcPixels[srcIndex + resizeImage->width + 1];

			if (resizeImage->alpha)
			{
				srcAlpha[0] = &resizeImage->alpha[srcIndex];
				srcAlpha[1] = &resizeImage->alpha[srcIndex + 1];
				srcAlpha[2] = &resizeImage->alpha[srcIndex +
					resizeImage->width];
				srcAlpha[3] = &resizeImage->alpha[srcIndex +
					resizeImage->width + 1];

				bilinearInterpolation(distanceX, distanceY, srcArea,
					srcAlpha, &destPixels[destIndex],
					&newImage.alpha[destIndex]);

				// For now we only use alpha channel values of 0 or 1, so do
				// simple rounding.
				if (newImage.alpha[destIndex] > 0.5)
				{
					newImage.alpha[destIndex] = 1.0;
				}
				else
				{
					newImage.alpha[destIndex] = 0;
					PIXEL_COPY(&resizeImage->transColor,
						&destPixels[destIndex]);
				}
			}
			else
			{
				bilinearInterpolation(distanceX, distanceY, srcArea, NULL,
					&destPixels[destIndex], NULL);
			}
		}
	}

	// Is the old image in kernel memory?
	if ((unsigned) resizeImage->data >= KERNEL_VIRTUAL_ADDRESS)
		kernImage = 1;

	// Free the old image
	kernelImageFree(resizeImage);

	// Copy the new image to the old image
	if (kernImage)
	{
		imageCopy(&newImage, resizeImage, 1);
		kernelImageFree(&newImage);
	}
	else
	{
		memcpy(resizeImage, &newImage, sizeof(image));
	}

	return (status = 0);
}


int kernelImageCopy(image *srcImage, image *destImage)
{
	// Given an image, make a copy of it.

	int status = 0;

	// Check parameters
	if (!srcImage || !destImage || !srcImage->data || !srcImage->dataLength)
		return (status = ERR_NULLPARAMETER);

	return (status = imageCopy(srcImage, destImage, 0));
}


int kernelImageCopyToKernel(image *srcImage, image *destImage)
{
	// Given an image, make a copy of it using globally-accessible kernel
	// memory

	int status = 0;

	// Check parameters
	if (!srcImage || !destImage || !srcImage->data || !srcImage->dataLength)
		return (status = ERR_NULLPARAMETER);

	return (status = imageCopy(srcImage, destImage, 1));
}


int kernelImageFill(image *fillImage, color *fillColor)
{
	// Given an image, fill it with the given color

	int status = 0;
	pixel *p = NULL;
	unsigned count;

	// Check parameters
	if (!fillImage || !fillColor)
		return (status = ERR_NULLPARAMETER);

	p = fillImage->data;
	for (count = 0; count < fillImage->pixels; count ++)
	{
		p[count].red = fillColor->red;
		p[count].green = fillColor->green;
		p[count].blue = fillColor->blue;
	}

	return (status = 0);
}


int kernelImagePaste(image *srcImage, image *destImage, int xCoord,
	int yCoord)
{
	// Given source and destination images, paste the source into the
	// destination at the given X and Y coordinates.

	int status = 0;
	void *srcPixel = 0;
	void *destPixel = 0;
	void *srcAlpha = NULL;
	void *destAlpha = NULL;
	int maxLines = 0;
	int lineWidth = 0;
	int lineCount;

	// Check parameters
	if (!srcImage || !destImage)
		return (status = ERR_NULLPARAMETER);

	if (xCoord < 0)
		xCoord = 0;
	if (yCoord < 0)
		yCoord = 0;

	srcPixel = srcImage->data;
	destPixel = (destImage->data + (((yCoord * destImage->width) + xCoord) *
		sizeof(pixel)));

	srcAlpha = (void *) srcImage->alpha;
	if (srcAlpha && !destImage->alpha)
		kernelImageGetAlpha(destImage);

	if (destImage->alpha)
	{
		destAlpha = ((void *) destImage->alpha + (((yCoord *
			destImage->width) + xCoord) * sizeof(float)));
	}

	maxLines = min(srcImage->height, (destImage->height - yCoord));
	lineWidth = min(srcImage->width, (destImage->width - xCoord));

	for (lineCount = 0; lineCount < maxLines; lineCount ++)
	{
		memcpy(destPixel, srcPixel, (lineWidth * sizeof(pixel)));
		srcPixel += (srcImage->width * sizeof(pixel));
		destPixel += (destImage->width * sizeof(pixel));

		if (srcAlpha && destAlpha)
		{
			memcpy(destAlpha, srcAlpha, (lineWidth * sizeof(float)));
			srcAlpha += (srcImage->width * sizeof(float));
			destAlpha += (destImage->width * sizeof(float));
		}
	}

	return (status = 0);
}


int kernelImageGetAlpha(image *alphaImage)
{
	// Given an image with a transparency color, allocate memory for the alpha
	// channel information and make all non-transparent pixels have an alpha
	// value of 1.0 (transparent pixels have an alpha value of 0).

	int status = 0;
	pixel *p = NULL;
	float *a = NULL;
	unsigned count;

	if (!alphaImage->alpha)
	{
		alphaImage->alpha = kernelMalloc(alphaImage->pixels * sizeof(float));
		if (!alphaImage->alpha)
			return (status = ERR_MEMORY);

		// Calculate it.

		p = alphaImage->data;
		a = alphaImage->alpha;

		for (count = 0; count < alphaImage->pixels; count ++)
		{
			if (!PIXELS_EQ(&p[count], &alphaImage->transColor))
				a[count] = 1.0;
		}
	}

	return (status = 0);
}

