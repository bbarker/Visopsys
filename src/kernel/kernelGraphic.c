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
//  kernelGraphic.c
//

// This file contains abstracted functions for drawing raw graphics on
// the screen

#include "kernelGraphic.h"
#include "kernelCharset.h"
#include "kernelDebug.h"
#include "kernelError.h"
#include "kernelFile.h"
#include "kernelFont.h"
#include "kernelImage.h"
#include "kernelLog.h"
#include "kernelMalloc.h"
#include "kernelMultitasker.h"
#include "kernelPage.h"
#include "kernelParameters.h"
#include "kernelText.h"
#include "kernelVariableList.h"
#include "kernelWindow.h"
#include <stdlib.h>
#include <string.h>
#include <sys/color.h>
#include <sys/env.h>
#include <sys/image.h>

// The global default colors
color kernelDefaultForeground = COLOR_DEFAULT_FOREGROUND;
color kernelDefaultBackground = COLOR_DEFAULT_BACKGROUND;
color kernelDefaultDesktop = COLOR_DEFAULT_DESKTOP;

static kernelDevice *systemAdapter = NULL;
static kernelGraphicAdapter *adapterDevice = NULL;
static kernelGraphicOps *ops = NULL;

#define VBE_PMINFOBLOCK_SIG "PMID"

typedef struct {
	char signature[4];
	unsigned short entryOffset;
	unsigned short initOffset;
	unsigned short dataSelector;
	unsigned short A0000Selector;
	unsigned short B0000Selector;
	unsigned short B8000Selector;
	unsigned short codeSelector;
	unsigned char protMode;
	unsigned char checksum;

} __attribute__((packed)) vbePmInfoBlock;


static int detectVbe(void)
{
	int status = 0;
	void *biosOrig = NULL;
	vbePmInfoBlock *pmInfo = NULL;
	char checkSum = 0;
	char *tmp = NULL;
	int count1, count2;

	kernelDebug(debug_io, "VBE: detecting VBE protected mode interface");

	// Map the video BIOS image into memory.  Starts at 0xC0000 and 'normally'
	// is 32Kb according to the VBE 3.0 spec (but not really in my experience)
	status = kernelPageMapToFree(KERNELPROCID, VIDEO_BIOS_MEMORY, &biosOrig,
		VIDEO_BIOS_MEMORY_SIZE);
	if (status < 0)
		return (status);

	// Scan the video BIOS memory for the "protected mode info block"
	// structure
	kernelDebug(debug_io, "VBE: searching for VBE BIOS pmInfo signature");
	for (count1 = 0; count1 < VIDEO_BIOS_MEMORY_SIZE; count1 ++)
	{
		tmp = (biosOrig + count1);

		if (strncmp((char *) tmp, VBE_PMINFOBLOCK_SIG, 4))
			continue;

		// Maybe we found it
		kernelDebug(debug_io, "VBE: found possible pmInfo signature at %x",
			count1);

		// Check the checksum
		for (count2 = 0; count2 < (int) sizeof(vbePmInfoBlock); count2 ++)
			checkSum += tmp[count2];
		if (checkSum)
		{
			kernelDebug(debug_io, "VBE: pmInfo checksum failed (%d)",
				checkSum);
			continue;
		}

		// Found it
		pmInfo = (vbePmInfoBlock *) tmp;
		kernelLog("VBE: VESA BIOS extension signature found at %x", count1);
		break;
	}

	if (!pmInfo)
	{
		kernelDebug(debug_io, "VBE: pmInfo signature not found");
		status = 0;
		goto out;
	}

	status = 0;

out:
	// Unmap the video BIOS
	kernelPageUnmap(KERNELPROCID, biosOrig, VIDEO_BIOS_MEMORY_SIZE);

	return (status);
}


/////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////
//
// Below here, the functions are exported for external use
//
/////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////

int kernelGraphicInitialize(kernelDevice *dev)
{
	// This function initializes the graphic functions.

	int status = 0;
	// This is data for a temporary console when we first arrive in a
	// graphical mode
	kernelTextArea *tmpConsole = NULL;
	kernelTextInputStream *inputStream = NULL;
	kernelWindowComponent *component = NULL;
	graphicBuffer *buffer = NULL;

	if (!dev)
	{
		kernelError(kernel_error, "NULL parameter");
		return (status = ERR_NOTINITIALIZED);
	}

	systemAdapter = dev;

	if (!systemAdapter->data || !systemAdapter->driver ||
		!systemAdapter->driver->ops)
	{
		kernelError(kernel_error, "The graphic adapter, driver or ops are "
			"NULL");
		return (status = ERR_NULLPARAMETER);
	}

	adapterDevice = (kernelGraphicAdapter *) systemAdapter->data;
	ops = systemAdapter->driver->ops;

	// Are we in a graphics mode?
	if (!adapterDevice->mode)
		return (status = ERR_INVALID);

	// Get a temporary text area for console output, and use the graphic
	// screen as a temporary output
	tmpConsole = kernelTextAreaNew(80, 50, 1, TEXT_DEFAULT_SCROLLBACKLINES);
	if (!tmpConsole)
		// Better not try to print any error messages...
		return (status = ERR_NOTINITIALIZED);

	// Assign some extra things to the text area
	tmpConsole->foreground.blue = 255;
	tmpConsole->foreground.green = 255;
	tmpConsole->foreground.red = 255;
	memcpy((color *) &tmpConsole->background, &kernelDefaultDesktop,
		sizeof(color));

	// Change the input and output streams to the console
	inputStream = tmpConsole->inputStream;
	if (inputStream)
	{
		if (inputStream->s.buffer)
		{
			kernelFree((void *) inputStream->s.buffer);
			inputStream->s.buffer = NULL;
		}

		kernelFree((void *) tmpConsole->inputStream);
		tmpConsole->inputStream = kernelTextGetConsoleInput();
	}

	if (tmpConsole->outputStream)
	{
		kernelFree((void *) tmpConsole->outputStream);
		tmpConsole->outputStream = kernelTextGetConsoleOutput();
	}

	// Get a NULL kernelWindowComponent to attach the graphic buffer to
	component = kernelMalloc(sizeof(kernelWindowComponent));
	if (!component)
		// Better not try to print any error messages...
		return (status = ERR_NOTINITIALIZED);
	tmpConsole->windowComponent = component;

	// Get a graphic buffer and attach it to the component
	buffer = kernelMalloc(sizeof(graphicBuffer));
	if (!buffer)
	{
		// Better not try to print any error messages...
		kernelFree((void *) component);
		return (status = ERR_NOTINITIALIZED);
	}

	buffer->width = adapterDevice->xRes;
	buffer->height = adapterDevice->yRes;
	buffer->data = adapterDevice->framebuffer;

	component->buffer = buffer;

	// Initialize the font functions
	status = kernelFontInitialize();
	if (status < 0)
	{
		kernelError(kernel_error, "Font initialization failed");
		return (status);
	}

	// Assign the built-in system font to our console text area
	kernelFontGetSystem((kernelFont **) &tmpConsole->font);

	// Switch the console
	kernelTextSwitchToGraphics(tmpConsole);

	// Clear the screen with our default background color
	ops->driverClearScreen(&kernelDefaultDesktop);

	// Try to detect VBE BIOS extensions
	detectVbe();

	// Return success
	return (status = 0);
}


int kernelGraphicsAreEnabled(void)
{
	// Returns 1 if graphics are enabled, 0 otherwise

	if (systemAdapter)
		return (1);
	else
		return (0);
}


int kernelGraphicGetModes(videoMode *modeBuffer, unsigned size)
{
	// Return the list of graphics modes supported by the adapter

	size = max(size, (sizeof(videoMode) * MAXVIDEOMODES));
	memcpy(modeBuffer, &adapterDevice->supportedModes, size);

	return (adapterDevice->numberModes);
}


int kernelGraphicGetMode(videoMode *mode)
{
	// Get the current graphics mode

	mode->mode = adapterDevice->mode;
	mode->xRes = adapterDevice->xRes;
	mode->yRes = adapterDevice->yRes;
	mode->bitsPerPixel = adapterDevice->bitsPerPixel;

	return (0);
}


int kernelGraphicSetMode(videoMode *mode)
{
	// Set the preferred graphics mode for the next reboot.  We create a
	// little binary file that the loader can easily understand

	int status = 0;
	file modeFile;
	int buffer[4];

	kernelFileOpen("/grphmode", (OPENMODE_WRITE | OPENMODE_CREATE |
		OPENMODE_TRUNCATE), &modeFile);

	buffer[0] = mode->xRes;
	buffer[1] = mode->yRes;
	buffer[2] = mode->bitsPerPixel;
	buffer[3] = 0;

	status = kernelFileWrite(&modeFile, 0, 1, (unsigned char *) buffer);

	kernelFileSetSize(&modeFile, 16);
	kernelFileClose(&modeFile);

	return (status);
}


int kernelGraphicGetScreenWidth(void)
{
	// Yup, returns the screen width

	// Make sure we've been initialized
	if (!systemAdapter)
		return (ERR_NOTINITIALIZED);

	return (adapterDevice->xRes);
}


int kernelGraphicGetScreenHeight(void)
{
	// Yup, returns the screen height

	// Make sure we've been initialized
	if (!systemAdapter)
		return (ERR_NOTINITIALIZED);

	return (adapterDevice->yRes);
}


int kernelGraphicCalculateAreaBytes(int width, int height)
{
	// Return the number of bytes needed to store a graphicBuffer's data that
	// can be drawn on the current display (this varies depending on the
	// bytes-per-pixel, etc, that higher-level code shouldn't have to know
	// about)

	return (width * height * adapterDevice->bytesPerPixel);
}


int kernelGraphicClearScreen(color *background)
{
	// Clears the whole screen to the requested color

	int status = 0;

	// Make sure we've been initialized
	if (!systemAdapter)
		return (status = ERR_NOTINITIALIZED);

	// Check params.
	if (!background)
		return (status = ERR_NULLPARAMETER);

	// Now make sure the device driver clearScreen function has been installed
	if (!ops->driverClearScreen)
	{
		kernelError(kernel_error, "The driver function is NULL");
		return (status = ERR_NOSUCHFUNCTION);
	}

	// Call the driver function
	status = ops->driverClearScreen(background);

	return (status);
}


int kernelGraphicDrawPixel(graphicBuffer *buffer, color *foreground,
	drawMode mode, int xCoord, int yCoord)
{
	// This is a generic function for drawing a single pixel

	int status = 0;

	// Make sure we've been initialized
	if (!systemAdapter)
		return (status = ERR_NOTINITIALIZED);

	// Check parameters.  'buffer' is allowed to be NULL.
	if (!foreground)
		return (status = ERR_NULLPARAMETER);

	// Now make sure the device driver drawPixel function has been installed
	if (!ops->driverDrawPixel)
	{
		kernelError(kernel_error, "The driver function is NULL");
		return (status = ERR_NOSUCHFUNCTION);
	}

	// Call the driver function
	status = ops->driverDrawPixel(buffer, foreground, mode, xCoord, yCoord);

	return (status);
}


int kernelGraphicDrawLine(graphicBuffer *buffer, color *foreground,
	drawMode mode, int xCoord1, int yCoord1, int xCoord2, int yCoord2)
{
	// This is a generic function for drawing a simple line

	int status = 0;

	// Make sure we've been initialized
	if (!systemAdapter)
		return (status = ERR_NOTINITIALIZED);

	// Check parameters.  'buffer' is allowed to be NULL.
	if (!foreground)
		return (status = ERR_NULLPARAMETER);

	// NULL size?
	if ((xCoord1 == xCoord2) && (yCoord1 == yCoord2))
	{
		return (kernelGraphicDrawPixel(buffer, foreground, mode, xCoord1,
			yCoord1));
	}

	// Now make sure the device driver drawLine function has been installed
	if (!ops->driverDrawLine)
	{
		kernelError(kernel_error, "The driver function is NULL");
		return (status = ERR_NOSUCHFUNCTION);
	}

	// Call the driver function
	status = ops->driverDrawLine(buffer, foreground, mode, xCoord1, yCoord1,
		xCoord2, yCoord2);

	return (status);
}


int kernelGraphicDrawRect(graphicBuffer *buffer, color *foreground,
	drawMode mode, int xCoord, int yCoord, int width, int height,
	int thickness, int fill)
{
	// This is a generic function for drawing a rectangle

	int status = 0;

	// Make sure we've been initialized
	if (!systemAdapter)
		return (status = ERR_NOTINITIALIZED);

	// Check parameters.  'buffer' is allowed to be NULL.
	if (!foreground)
		return (status = ERR_NULLPARAMETER);

	// NULL size?
	if (!width || !height)
	{
		return (kernelGraphicDrawPixel(buffer, foreground, mode, xCoord,
			yCoord));
	}

	// If the thickness would effectively fill the rectangle, just fill
	// instead
	if (thickness >= (min(width, height) / 2))
	{
		thickness = 1;
		fill = 1;
	}

	// Now make sure the device driver drawRect function has been installed
	if (!ops->driverDrawRect)
	{
		kernelError(kernel_error, "The driver function is NULL");
		return (status = ERR_NOSUCHFUNCTION);
	}

	// Call the driver function
	status = ops->driverDrawRect(buffer, foreground, mode, xCoord, yCoord,
		width, height, thickness, fill);

	return (status);
}


int kernelGraphicDrawOval(graphicBuffer *buffer, color *foreground,
	drawMode mode, int xCoord, int yCoord, int width, int height,
	int thickness, int fill)
{
	// This is a generic function for drawing an oval

	int status = 0;

	// Make sure we've been initialized
	if (!systemAdapter)
		return (status = ERR_NOTINITIALIZED);

	// Check parameters.  'buffer' is allowed to be NULL.
	if (!foreground)
		return (status = ERR_NULLPARAMETER);

	// NULL size?
	if (!width || !height)
	{
		return (kernelGraphicDrawPixel(buffer, foreground, mode, xCoord,
			yCoord));
	}

	// If the thickness would effectively fill the oval, just fill instead
	if (thickness >= (min(width, height) / 2))
	{
		thickness = 1;
		fill = 1;
	}

	// Now make sure the device driver drawOval function has been installed
	if (!ops->driverDrawOval)
	{
		kernelError(kernel_error, "The driver function is NULL");
		return (status = ERR_NOSUCHFUNCTION);
	}

	// Call the driver function
	status = ops->driverDrawOval(buffer, foreground, mode, xCoord, yCoord,
		width, height, thickness, fill);

	return (status);
}


int kernelGraphicGetImage(graphicBuffer *buffer, image *getImage, int xCoord,
	int yCoord, int width, int height)
{
	// This is a generic function for getting an image from a buffer.  The
	// image memory returned is in the application space of the current
	// process.

	int status = 0;

	// Make sure we've been initialized
	if (!systemAdapter)
		return (status = ERR_NOTINITIALIZED);

	// Check parameters.  'buffer' is allowed to be NULL.
	if (!getImage)
		return (status = ERR_NULLPARAMETER);

	// Now make sure the device driver getImage function has been installed
	if (!ops->driverGetImage)
	{
		kernelError(kernel_error, "The driver function is NULL");
		return (status = ERR_NOSUCHFUNCTION);
	}

	// Call the driver function
	status = ops->driverGetImage(buffer, getImage, xCoord, yCoord, width,
		height);

	return (status);
}


int kernelGraphicDrawImage(graphicBuffer *buffer, image *drawImage,
	drawMode mode, int xCoord, int yCoord, int xOffset, int yOffset,
	int width, int height)
{
	// This is a generic function for drawing an image

	int status = 0;

	// Make sure we've been initialized
	if (!systemAdapter)
		return (status = ERR_NOTINITIALIZED);

	// Check parameters.  'buffer' is allowed to be NULL.
	if (!drawImage)
		return (status = ERR_NULLPARAMETER);

	// Now make sure the device driver drawImage function has been installed
	if (!ops->driverDrawImage)
	{
		kernelError(kernel_error, "The driver function is NULL");
		return (status = ERR_NOSUCHFUNCTION);
	}

	// Do we need to gather alpha channel data?
	if ((mode == draw_alphablend) && !drawImage->alpha)
	{
		status = kernelImageGetAlpha(drawImage);
		if (status < 0)
			return (status);
	}

	// Call the driver function
	status = ops->driverDrawImage(buffer, drawImage, mode, xCoord, yCoord,
		xOffset, yOffset, width, height);

	return (status);
}


int kernelGraphicDrawText(graphicBuffer *buffer, color *foreground,
	color *background, kernelFont *font, const char *charSet,
	const char *text, drawMode mode, int xCoord, int yCoord)
{
	// Draws a line of text using the supplied font at the requested
	// coordinates, with the supplied foreground and background colors.

	int status = 0;
	int length = 0;
	unsigned unicode = 0;
	int count1, count2;

	// Make sure we've been initialized
	if (!systemAdapter)
		return (status = ERR_NOTINITIALIZED);

	// Check parameters.  'buffer' and 'charSet' are allowed to be NULL.
	if (!foreground || !background || !font || !text)
		return (status = ERR_NULLPARAMETER);

	// Now make sure the device driver drawMonoImage function has been
	// installed
	if (!ops->driverDrawMonoImage)
	{
		kernelError(kernel_error, "The driver function is NULL");
		return (status = ERR_NOSUCHFUNCTION);
	}

	// How long is the string?
	length = strlen(text);

	// What character set are we using?
	if (!charSet)
		charSet = CHARSET_NAME_DEFAULT;

	// Loop through the string
	for (count1 = 0; count1 < length; count1 ++)
	{
		if ((unsigned char) text[count1] < CHARSET_IDENT_CODES)
		{
			unicode = text[count1];
		}
		else
		{
			unicode = kernelCharsetToUnicode(charSet, (unsigned char)
				text[count1]);
		}

		for (count2 = 0; count2 < font->numGlyphs; count2 ++)
		{
			if (font->glyphs[count2].unicode == unicode)
			{
				if (font->glyphs[count2].img.data)
				{
					// Call the driver function to draw the character
					status = ops->driverDrawMonoImage(buffer,
						&font->glyphs[count2].img, mode, foreground,
						background, xCoord, yCoord);

					xCoord += font->glyphs[count2].img.width;
				}

				break;
			}
		}
	}

	return (status = 0);
}


int kernelGraphicCopyArea(graphicBuffer *buffer, int xCoord1, int yCoord1,
	int width, int height, int xCoord2, int yCoord2)
{
	// Copies the requested area of the screen to the new location.

	int status = 0;

	// Make sure we've been initialized
	if (!systemAdapter)
		return (status = ERR_NOTINITIALIZED);

	// Now make sure the device driver copyArea function has been installed
	if (!ops->driverCopyArea)
	{
		kernelError(kernel_error, "The driver function is NULL");
		return (status = ERR_NOSUCHFUNCTION);
	}

	// Call the driver function to copy the area
	status = ops->driverCopyArea(buffer, xCoord1, yCoord1, width, height,
		xCoord2, yCoord2);

	return (status);
}


int kernelGraphicClearArea(graphicBuffer *buffer, color *background,
	int xCoord, int yCoord, int width, int height)
{
	// Clears the requested area of the screen.  This is just a convenience
	// function that draws a filled rectangle over the spot using the
	// background color

	// The called function will check its parameters.
	return (kernelGraphicDrawRect(buffer, background, draw_normal, xCoord,
		yCoord, width, height, 1, 1));
}


int kernelGraphicCopyBuffer(graphicBuffer *srcBuffer,
	graphicBuffer *destBuffer, int xCoord, int yCoord)
{
	// Copy the source graphicBuffer into the destination graphicBuffer at the
	// specified destination coordinates.

	int status = 0;
	int srcWidth = 0;
	int destWidth = 0;
	void *srcPointer = NULL;
	void *destPointer = NULL;
	int rowCount;

	// Make sure we've been initialized
	if (!systemAdapter)
		return (status = ERR_NOTINITIALIZED);

	srcWidth = (srcBuffer->width * adapterDevice->bytesPerPixel);
	destWidth = (destBuffer->width * adapterDevice->bytesPerPixel);

	srcPointer = srcBuffer->data;
	destPointer = (destBuffer->data + (yCoord * destWidth) + (xCoord *
		adapterDevice->bytesPerPixel));

	for (rowCount = 0; rowCount < srcBuffer->height; rowCount ++)
	{
		memcpy(destPointer, srcPointer, srcWidth);
		srcPointer += srcWidth;
		destPointer += destWidth;
	}

	return (status = 0);
}


int kernelGraphicRenderBuffer(graphicBuffer *buffer, int drawX, int drawY,
	int clipX, int clipY, int clipWidth, int clipHeight)
{
	// Take a graphicBuffer and render it on the screen

	int status = 0;

	// Make sure we've been initialized
	if (!systemAdapter)
		return (status = ERR_NOTINITIALIZED);

	// Buffer is not allowed to be NULL this time
	if (!buffer)
	{
		kernelError(kernel_error, "NULL parameter");
		return (status = ERR_NULLPARAMETER);
	}

	// Make sure the clip is fully inside the buffer
	if (clipX < 0)
	{
		clipWidth += clipX;
		clipX = 0;
	}

	if (clipY < 0)
	{
		clipHeight += clipY;
		clipY = 0;
	}

	if ((clipX + clipWidth) >= buffer->width)
		clipWidth = (buffer->width - clipX);

	if ((clipY + clipHeight) >= buffer->height)
		clipHeight = (buffer->height - clipY);

	if ((clipWidth <= 0) || (clipHeight <= 0))
		return (status = 0);

	// Now make sure the device driver renderBuffer function has been
	// installed
	if (!ops->driverRenderBuffer)
	{
		kernelError(kernel_error, "The driver function is NULL");
		return (status = ERR_NOSUCHFUNCTION);
	}

	// Call the driver function to render the buffer
	status = ops->driverRenderBuffer(buffer, drawX, drawY, clipX, clipY,
		clipWidth, clipHeight);

	return (status);
}


int kernelGraphicFilter(graphicBuffer *buffer, color *filterColor, int xCoord,
	int yCoord, int width, int height)
{
	// Take an area of a buffer and average it with the supplied color

	int status = 0;

	// Make sure we've been initialized
	if (!systemAdapter)
		return (status = ERR_NOTINITIALIZED);

	// Color not NULL.  'buffer' is allowed to be NULL.
	if (!filterColor)
		return (status = ERR_NULLPARAMETER);

	// Zero size?
	if (!width || !height)
		return (status = 0);

	// Now make sure the device driver filter function has been installed
	if (!ops->driverFilter)
	{
		kernelError(kernel_error, "The driver function is NULL");
		return (status = ERR_NOSUCHFUNCTION);
	}

	// Call the driver function
	status = ops->driverFilter(buffer, filterColor, xCoord, yCoord, width,
		height);

	return (status);
}


void kernelGraphicDrawGradientBorder(graphicBuffer *buffer, int drawX,
	int drawY, int width, int height, int thickness, color *drawColor,
	int shadingIncrement, drawMode mode, borderType type)
{
	// Draws a gradient border

	color tmpColor;
	int drawRed = 0, drawGreen = 0, drawBlue = 0;
	int count;

	if (drawColor)
		memcpy(&tmpColor, drawColor, sizeof(color));
	else
		memcpy(&tmpColor, &kernelDefaultBackground, sizeof(color));

	drawColor = &tmpColor;

	// These are the starting points of the 'inner' border lines
	int leftX = (drawX + thickness);
	int rightX = (drawX + width - thickness - 1);
	int topY = (drawY + thickness);
	int bottomY = (drawY + height - thickness - 1);

	if (mode == draw_reverse)
		shadingIncrement *= -1;

	// The top and left
	for (count = thickness; count > 0; count --)
	{
		drawRed = (drawColor->red + (count * shadingIncrement));
		if (drawRed > 255)
			drawRed = 255;
		if (drawRed < 0)
			drawRed = 0;

		drawGreen = (drawColor->green + (count * shadingIncrement));
		if (drawGreen > 255)
			drawGreen = 255;
		if (drawGreen < 0)
			drawGreen = 0;

		drawBlue = (drawColor->blue + (count * shadingIncrement));
		if (drawBlue > 255)
			drawBlue = 255;
		if (drawBlue < 0)
			drawBlue = 0;

		// Top
		if (type & border_top)
		{
			kernelGraphicDrawLine(buffer, &((color){ drawBlue, drawGreen,
				drawRed }), draw_normal, (leftX - count), (topY - count),
				(rightX + count), (topY - count));
		}
		// Left
		if (type & border_left)
		{
			kernelGraphicDrawLine(buffer, &((color){ drawBlue, drawGreen,
				drawRed }), draw_normal, (leftX - count), (topY - count),
				(leftX - count), (bottomY + count));
		}
	}

	shadingIncrement *= -1;

	// The bottom and right
	for (count = thickness; count > 0; count --)
	{
		drawRed = (drawColor->red + (count * shadingIncrement));
		if (drawRed > 255)
			drawRed = 255;
		if (drawRed < 0)
			drawRed = 0;

		drawGreen = (drawColor->green + (count * shadingIncrement));
		if (drawGreen > 255)
			drawGreen = 255;
		if (drawGreen < 0)
			drawGreen = 0;

		drawBlue = (drawColor->blue + (count * shadingIncrement));
		if (drawBlue > 255)
			drawBlue = 255;
		if (drawBlue < 0)
			drawBlue = 0;

		// Bottom
		if (type & border_bottom)
		{
			kernelGraphicDrawLine(buffer, &((color){ drawBlue, drawGreen,
				drawRed }), draw_normal, (leftX - count), (bottomY + count),
				(rightX + count), (bottomY + count));
		}
		// Right
		if (type & border_right)
		{
			kernelGraphicDrawLine(buffer, &((color){ drawBlue, drawGreen,
				drawRed }), draw_normal, (rightX + count), (topY - count),
				(rightX + count), (bottomY + count));
		}
	}
}


void kernelGraphicConvexShade(graphicBuffer *buffer, color *drawColor,
	int drawX, int drawY, int width, int height, shadeType type)
{
	// Given an buffer, area, color, and shading mode, shade the area as a
	// 3D-like, convex object.

	color tmpColor;
	int outerDiff = 30;
	int centerDiff = 10;
	int increment = ((outerDiff - centerDiff) / (height / 2));
	int limit = 0;
	int count;

	if (drawColor)
		memcpy(&tmpColor, drawColor, sizeof(color));
	else
		memcpy(&tmpColor, &kernelDefaultBackground, sizeof(color));

	drawColor = &tmpColor;

	if ((type == shade_fromtop) || (type == shade_frombottom))
		limit = height;
	else
		limit = width;

	increment = max(((outerDiff - centerDiff) / (limit / 2)), 3);
	outerDiff = max(outerDiff, (centerDiff + (increment * (limit / 2))));

	if ((type == shade_fromtop) || (type == shade_fromleft))
	{
		drawColor->red = min((drawColor->red + outerDiff), 0xFF);
		drawColor->green = min((drawColor->green + outerDiff), 0xFF);
		drawColor->blue = min((drawColor->blue + outerDiff), 0xFF);
	}
	else
	{
		drawColor->red = max((drawColor->red - outerDiff), 0);
		drawColor->green = max((drawColor->green - outerDiff), 0);
		drawColor->blue = max((drawColor->blue - outerDiff), 0);
	}

	for (count = 0; count < limit; count ++)
	{
		if ((type == shade_fromtop) || (type == shade_frombottom))
		{
			kernelGraphicDrawLine(buffer, drawColor, draw_normal, drawX,
				(drawY + count), (drawX + width - 1), (drawY + count));
		}
		else
		{
			kernelGraphicDrawLine(buffer, drawColor, draw_normal, (drawX +
				count), drawY, (drawX + count), (drawY + height - 1));
		}

		if ((type == shade_fromtop) || (type == shade_fromleft))
		{
			if (count == ((limit / 2) - 1))
			{
				drawColor->red = max((drawColor->red - (centerDiff * 2)), 0);
				drawColor->green = max((drawColor->green -
					(centerDiff * 2)), 0);
				drawColor->blue = max((drawColor->blue -
					(centerDiff * 2)), 0);
			}
			else
			{
				drawColor->red = max((drawColor->red - increment), 0);
				drawColor->green = max((drawColor->green - increment), 0);
				drawColor->blue = max((drawColor->blue - increment), 0);
			}
		}
		else
		{
			if (count == ((limit / 2) - 1))
			{
				drawColor->red = min((drawColor->red + (centerDiff  * 2)),
					0xFF);
				drawColor->green = min((drawColor->green + (centerDiff * 2)),
					0xFF);
				drawColor->blue = min((drawColor->blue + (centerDiff * 2)),
					0xFF);
			}
			else
			{
				drawColor->red = min((drawColor->red + increment), 0xFF);
				drawColor->green = min((drawColor->green + increment), 0xFF);
				drawColor->blue = min((drawColor->blue + increment), 0xFF);
			}
		}
	}
}

