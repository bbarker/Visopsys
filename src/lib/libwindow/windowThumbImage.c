//
//  Visopsys
//  Copyright (C) 1998-2018 J. Andrew McLaughlin
//
//  This library is free software; you can redistribute it and/or modify it
//  under the terms of the GNU Lesser General Public License as published by
//  the Free Software Foundation; either version 2.1 of the License, or (at
//  your option) any later version.
//
//  This library is distributed in the hope that it will be useful, but
//  WITHOUT ANY WARRANTY; without even the implied warranty of
//  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU Lesser
//  General Public License for more details.
//
//  You should have received a copy of the GNU Lesser General Public License
//  along with this library; if not, write to the Free Software Foundation,
//  Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
//
//  windowThumbImage.c
//

// This contains functions for user programs to operate GUI components.

#include <errno.h>
#include <sys/api.h>
#include <sys/window.h>

extern int libwindow_initialized;
extern void libwindowInitialize(void);


static int getImage(const char *fileName, image *imageData, unsigned maxWidth,
	unsigned maxHeight, int stretch, color *background)
{
	int status = 0;
	image loadImage;
	float scale = 0;
	unsigned thumbWidth = 0;
	unsigned thumbHeight = 0;

	memset(&loadImage, 0, sizeof(image));

	status = imageNew(imageData, maxWidth, maxHeight);
	if (status < 0)
		return (status);

	if (!stretch && background)
	{
		status = imageFill(imageData, background);
		if (status < 0)
			goto out;
	}

	if (fileName)
	{
		status = imageLoad(fileName, 0, 0, &loadImage);
		if (status < 0)
			goto out;

		// Scale the image
		thumbWidth = loadImage.width;
		thumbHeight = loadImage.height;

		// Presumably we need to shrink it?
		if (stretch)
		{
			thumbWidth = maxWidth;
			thumbHeight = maxHeight;
		}
		else
		{
			if (thumbWidth > maxWidth)
			{
				scale = ((float) maxWidth / (float) thumbWidth);
				thumbWidth = (unsigned)((float) thumbWidth * scale);
				thumbHeight = (unsigned)((float) thumbHeight * scale);
			}

			if (thumbHeight > maxHeight)
			{
				scale = ((float) maxHeight / (float) thumbHeight);
				thumbWidth = (unsigned)((float) thumbWidth * scale);
				thumbHeight = (unsigned)((float) thumbHeight * scale);
			}
		}

		if ((thumbWidth != loadImage.width) ||
			(thumbHeight != loadImage.height))
		{
			status = imageResize(&loadImage, thumbWidth, thumbHeight);
			if (status < 0)
				goto out;
		}

		status = imagePaste(&loadImage, imageData,
			((maxWidth - loadImage.width) / 2),
			((maxHeight - loadImage.height) / 2));
		if (status < 0)
			goto out;
	}

	status = 0;

out:
	if (loadImage.data)
		imageFree(&loadImage);

	if ((status < 0) && imageData->data)
		imageFree(imageData);

	return (status);
}


/////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////
//
// Below here, the functions are exported for external use
//
/////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////

_X_ objectKey windowNewThumbImage(objectKey parent, const char *fileName, unsigned maxWidth, unsigned maxHeight, int stretch, componentParameters *params)
{
	// Desc: Create a new window image component from the supplied image file name 'fileName', with the given 'parent' window or container, and component parameters 'params'.  Dimension values 'maxWidth' and 'maxHeight' constrain the maximum image size.  The resulting image will be scaled down, if necessary, with the aspect ratio intact, unless 'stretch' is non-zero, in which case the thumbnail image will be resized to 'maxWidth' and 'maxHeight'.  If 'params' specifies a background color, any empty space will be filled with that color.  If 'fileName' is NULL, an empty image will be created.

	int status = 0;
	color *background = NULL;
	image imageData;
	objectKey thumbImage = NULL;

	if (!libwindow_initialized)
		libwindowInitialize();

	// Check params.  File name can be NULL.
	if (!parent || !maxWidth || !maxHeight || !params)
	{
		errno = ERR_NULLPARAMETER;
		return (thumbImage = NULL);
	}

	if (params->flags & WINDOW_COMPFLAG_CUSTOMBACKGROUND)
		background = &params->background;

	status = getImage(fileName, &imageData, maxWidth, maxHeight, stretch,
		background);

	if (status >= 0)
	{
		thumbImage = windowNewImage(parent, &imageData, draw_normal, params);
		if (!thumbImage)
			errno = ERR_NOCREATE;
	}

	imageFree(&imageData);
	return (thumbImage);
}


_X_ int windowThumbImageUpdate(objectKey thumbImage, const char *fileName, unsigned maxWidth, unsigned maxHeight, int stretch, color *background)
{
	// Desc: Update an existing window image component 'thumbImage', previously created with a call to windowNewThumbImage(), from the supplied image file name 'fileName'.  Dimension values 'maxWidth' and 'maxHeight' constrain the maximum image size.  The resulting image will be scaled down, if necessary, with the aspect ratio intact.  If 'fileName' is NULL, the image will become blank.

	int status = 0;
	image imageData;

	if (!libwindow_initialized)
		libwindowInitialize();

	// Check params.  File name and background can be NULL.
	if (!thumbImage || !maxWidth || !maxHeight)
		return (status = ERR_NULLPARAMETER);

	status = getImage(fileName, &imageData, maxWidth, maxHeight, stretch,
		background);

	if (status >= 0)
	{
		status = windowComponentSetData(thumbImage, &imageData, sizeof(image),
			1 /* redraw */);
	}

	imageFree(&imageData);
	return (status);
}

