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
//  kernelFramebufferGraphicDriver.c
//

// This is the simple graphics driver for a LFB (Linear Framebuffer)
// -equipped graphics adapter

#include "kernelGraphic.h"
#include "kernelError.h"
#include "kernelImage.h"
#include "kernelMalloc.h"
#include "kernelMain.h"
#include "kernelPage.h"
#include "kernelParameters.h"
#include <stdlib.h>
#include <string.h>
#include <sys/processor.h>

static kernelGraphicAdapter *adapter = NULL;
static graphicBuffer wholeScreen;


static inline void alphaBlend32(pixel *pix, float alpha, pixel *buf)
{
	// Given a pixel from an image with an alpha channel value, blend it into
	// the buffer.

	buf->red = (((1.0 - alpha) * buf->red) + (alpha * pix->red));
	buf->green = (((1.0 - alpha) * buf->green) + (alpha * pix->green));
	buf->blue = (((1.0 - alpha) * buf->blue) + (alpha * pix->blue));
}


static inline void alphaBlend16(pixel *pix, float alpha, short *buf)
{
	// Given a pixel from an image with an alpha channel value, blend it into
	// the buffer.

	short pixRed, pixGreen, pixBlue;
	short bufRed, bufGreen, bufBlue;

	pixRed = ((short)(alpha * pix->red) >> 3);
	pixBlue = ((short)(alpha * pix->blue) >> 3);
	bufBlue = ((short)((1.0 - alpha) * ((*buf & 0x1F) << 3)) >> 3);

	if (adapter->bitsPerPixel == 16)
	{
		pixGreen = ((short)(alpha * pix->green) >> 2);
		bufRed = ((short)((1.0 - alpha) * ((*buf & 0xF800) >> 8)) >> 3);
		bufGreen = ((short)((1.0 - alpha) * ((*buf & 0x07E0) >> 3)) >> 2);

		*buf = (((bufRed + pixRed) << 11) | ((bufGreen + pixGreen) << 5) |
			(bufBlue + pixBlue));
	}
	else
	{
		pixGreen = ((short)(alpha * pix->green) >> 3);
		bufRed = ((short)((1.0 - alpha) * ((*buf & 0x7C00) >> 7)) >> 3);
		bufGreen = ((short)((1.0 - alpha) * ((*buf & 0x03E0) >> 2)) >> 3);

		*buf = (((bufRed + pixRed) << 10) | ((bufGreen + pixGreen) << 5) |
			(bufBlue + pixBlue));
	}
}


static int driverClearScreen(color *background)
{
	// Resets the whole screen to the supplied background color

	int status = 0;
	int lineCount, count;

	if (adapter->bitsPerPixel == 32)
	{
		unsigned pix = ((background->red << 16) | (background->green << 8) |
			background->blue);

		for (lineCount = 0; lineCount < adapter->yRes; lineCount ++)
		{
			processorWriteDwords(pix, (adapter->framebuffer + (lineCount *
				adapter->scanLineBytes)), adapter->xRes);
		}
	}

	else if (adapter->bitsPerPixel == 24)
	{
		char *linePointer = (char *) adapter->framebuffer;

		for (lineCount = 0; lineCount < adapter->yRes; lineCount ++)
		{
			for (count = 0; count < (adapter->xRes *
				adapter->bytesPerPixel); )
			{
				linePointer[count++] = background->blue;
				linePointer[count++] = background->green;
				linePointer[count++] = background->red;
			}

			linePointer += adapter->scanLineBytes;
		}
	}

	else if ((adapter->bitsPerPixel == 16) || (adapter->bitsPerPixel == 15))
	{
		short pix = 0;

		if (adapter->bitsPerPixel == 16)
		{
			pix = (((background->red >> 3) << 11) |
				((background->green >> 2) << 5) | (background->blue >> 3));
		}
		else
		{
			pix = (((background->red >> 3) << 10) |
				((background->green >> 3) << 5) | (background->blue >> 3));
		}

		for (lineCount = 0; lineCount < adapter->yRes; lineCount ++)
		{
			processorWriteWords(pix, (adapter->framebuffer + (lineCount *
				adapter->scanLineBytes)), adapter->xRes);
		}
	}

	return (status = 0);
}


static int driverDrawPixel(graphicBuffer *buffer, color *foreground,
	drawMode mode, int xCoord, int yCoord)
{
	// Draws a single pixel to the graphic buffer using the supplied
	// foreground color

	int status = 0;
	int scanLineBytes = 0;
	unsigned char *bufferPointer = NULL;
	short pix = 0;

	// If the supplied graphicBuffer is NULL, we draw directly to the whole
	// screen
	if (!buffer)
		buffer = &wholeScreen;

	// Make sure the pixel is in the buffer
	if ((xCoord < 0) || (xCoord >= buffer->width) ||
		(yCoord < 0) || (yCoord >= buffer->height))
	{
		// Don't make an error condition, just skip it
		return (status = 0);
	}

	scanLineBytes = (buffer->width * adapter->bytesPerPixel);
	if (buffer->data == adapter->framebuffer)
		scanLineBytes = adapter->scanLineBytes;

	// Draw the pixel using the supplied color
	bufferPointer = (buffer->data + (yCoord * scanLineBytes) + (xCoord *
		adapter->bytesPerPixel));

	if ((adapter->bitsPerPixel == 32) || (adapter->bitsPerPixel == 24))
	{
		if (mode == draw_normal)
		{
			bufferPointer[0] = foreground->blue;
			bufferPointer[1] = foreground->green;
			bufferPointer[2] = foreground->red;
		}
		else if (mode == draw_or)
		{
			bufferPointer[0] |= foreground->blue;
			bufferPointer[1] |= foreground->green;
			bufferPointer[2] |= foreground->red;
		}
		else if (mode == draw_xor)
		{
			bufferPointer[0] ^= foreground->blue;
			bufferPointer[1] ^= foreground->green;
			bufferPointer[2] ^= foreground->red;
		}
	}

	else if ((adapter->bitsPerPixel == 16) || (adapter->bitsPerPixel == 15))
	{
		if (adapter->bitsPerPixel == 16)
		{
			pix = (((foreground->red >> 3) << 11) |
				((foreground->green >> 2) << 5) | (foreground->blue >> 3));
		}
		else
		{
			pix = (((foreground->red >> 3) << 10) |
				((foreground->green >> 3) << 5) | (foreground->blue >> 3));
		}

		if (mode == draw_normal)
			*((short *) bufferPointer) = pix;
		else if (mode == draw_or)
			*((short *) bufferPointer) |= pix;
		else if (mode == draw_xor)
			*((short *) bufferPointer) ^= pix;
	}

	return (status = 0);
}


static int driverDrawLine(graphicBuffer *buffer, color *foreground,
	drawMode mode, int startX, int startY, int endX, int endY)
{
	// Draws a line on the screen using the supplied foreground color

	int status = 0;
	int scanLineBytes = 0;
	int lineLength = 0;
	int lineBytes = 0;
	unsigned char *bufferPointer = NULL;
	int count;

	#define SWAP(a, b) do { int tmp = a; a = b; b = tmp; } while (0)

	// If the supplied graphicBuffer is NULL, we draw directly to the whole
	// screen
	if (!buffer)
		buffer = &wholeScreen;

	scanLineBytes = (buffer->width * adapter->bytesPerPixel);
	if (buffer->data == adapter->framebuffer)
		scanLineBytes = adapter->scanLineBytes;

	// Is it a horizontal line?
	if (startY == endY)
	{
		// This is an easy line to draw.

		// If the Y location is off the buffer, skip it
		if ((startY < 0) || (startY >= buffer->height))
			return (status = 0);

		// Make sure startX < endX
		if (startX > endX)
			SWAP(startX, endX);

		// If the line goes off the edge of the buffer, only attempt to
		// display what will fit
		if (startX < 0)
			startX = 0;
		if (endX >= buffer->width)
			endX = (buffer->width - 1);
		lineLength = ((endX - startX) + 1);

		// Nothing to do?
		if (lineLength <= 0)
			return (status = 0);

		// How many bytes in the line?
		lineBytes = (adapter->bytesPerPixel * lineLength);

		bufferPointer = (buffer->data + (startY * scanLineBytes) + (startX *
			adapter->bytesPerPixel));

		// Do a loop through the line, copying the color values consecutively

		if ((adapter->bitsPerPixel == 32) || (adapter->bitsPerPixel == 24))
		{
			if ((adapter->bitsPerPixel == 24) || (mode == draw_or) ||
				(mode == draw_xor))
			{
				for (count = 0; count < lineBytes; )
				{
					if (mode == draw_normal)
					{
						bufferPointer[count] = foreground->blue;
						bufferPointer[count + 1] = foreground->green;
						bufferPointer[count + 2] = foreground->red;
					}
					else if (mode == draw_or)
					{
						bufferPointer[count] |= foreground->blue;
						bufferPointer[count + 1] |= foreground->green;
						bufferPointer[count + 2] |= foreground->red;
					}
					else if (mode == draw_xor)
					{
						bufferPointer[count] ^= foreground->blue;
						bufferPointer[count + 1] ^= foreground->green;
						bufferPointer[count + 2] ^= foreground->red;
					}

					count += 3;
					if (adapter->bitsPerPixel == 32)
						count++;
				}
			}
			else
			{
				unsigned pix = ((foreground->red << 16) |
					(foreground->green << 8) | foreground->blue);

				processorWriteDwords(pix, bufferPointer, lineLength);
			}
		}

		else if ((adapter->bitsPerPixel == 16) ||
			(adapter->bitsPerPixel == 15))
		{
			short pix = 0;

			if (adapter->bitsPerPixel == 16)
			{
				pix = (((foreground->red >> 3) << 11) |
					((foreground->green >> 2) << 5) |
					(foreground->blue >> 3));
			}
			else
			{
				pix = (((foreground->red >> 3) << 10) |
					((foreground->green >> 3) << 5) |
					(foreground->blue >> 3));
			}

			for (count = 0; count < lineLength; count ++)
			{
				if (mode == draw_normal)
					((short *) bufferPointer)[count] = pix;
				else if (mode == draw_or)
					((short *) bufferPointer)[count] |= pix;
				else if (mode == draw_xor)
					((short *) bufferPointer)[count] ^= pix;
			}
		}
	}

	// Is it a vertical line?
	else if (startX == endX)
	{
		// This is an easy line to draw.

		// If the X location is off the buffer, skip it
		if ((startX < 0) || (startX >= buffer->width))
			return (status = 0);

		// Make sure startY < endY
		if (startY > endY)
			SWAP(startY, endY);

		// If the line goes off the bottom edge of the buffer, only attempt to
		// display what will fit
		if (startY < 0)
			startY = 0;
		if (endY >= buffer->height)
			endY = (buffer->height - 1);
		lineLength = ((endY - startY) + 1);

		// Nothing to do?
		if (lineLength <= 0)
			return (status = 0);

		bufferPointer = (buffer->data + (startY * scanLineBytes) + (startX *
			adapter->bytesPerPixel));

		// Do a loop through the line, copying the color values into each row

		if ((adapter->bitsPerPixel == 32) || (adapter->bitsPerPixel == 24))
		{
			for (count = 0; count < lineLength; count ++)
			{
				if (mode == draw_normal)
				{
					bufferPointer[0] = foreground->blue;
					bufferPointer[1] = foreground->green;
					bufferPointer[2] = foreground->red;
				}
				else if (mode == draw_or)
				{
					bufferPointer[0] |= foreground->blue;
					bufferPointer[1] |= foreground->green;
					bufferPointer[2] |= foreground->red;
				}
				else if (mode == draw_xor)
				{
					bufferPointer[0] ^= foreground->blue;
					bufferPointer[1] ^= foreground->green;
					bufferPointer[2] ^= foreground->red;
				}

				bufferPointer += scanLineBytes;
			}
		}

		else if ((adapter->bitsPerPixel == 16) ||
			(adapter->bitsPerPixel == 15))
		{
			short pix = 0;

			if (adapter->bitsPerPixel == 16)
			{
				pix = (((foreground->red >> 3) << 11) |
					((foreground->green >> 2) << 5) |
					(foreground->blue >> 3));
			}
			else
			{
				pix = (((foreground->red >> 3) << 10) |
					((foreground->green >> 3) << 5) |
					(foreground->blue >> 3));
			}

			for (count = 0; count < lineLength; count ++)
			{
				if (mode == draw_normal)
					*((short *) bufferPointer) = pix;
				else if (mode == draw_or)
					*((short *) bufferPointer) |= pix;
				else if (mode == draw_xor)
					*((short *) bufferPointer) ^= pix;

				bufferPointer += scanLineBytes;
			}
		}
	}

	// It's not horizontal or vertical.  We will use a Bresenham algorithm to
	// make the line
	else
	{
		int deltaX = 0, deltaY = 0, yStep = 0, x = 0, y = 0;
		float error = 0.0, deltaError = 0.0;
		int steep = (abs(endY - startY) > abs(endX - startX));

		if (steep)
		{
			SWAP(startX, startY);
			SWAP(endX, endY);
		}

		if (startX > endX)
		{
			SWAP(startX, endX);
			SWAP(startY, endY);
		}

		deltaX = (endX - startX);
		deltaY = abs(endY - startY);

		deltaError = ((float) deltaY / (float) deltaX);

		y = startY;

		if (startY < endY)
			yStep = 1;
		else
			yStep = -1;

		for (x = startX; x <= endX; x ++)
		{
			if (steep)
				driverDrawPixel(buffer, foreground, mode, y, x);
			else
				driverDrawPixel(buffer, foreground, mode, x, y);

			error += deltaError;
			if (error >= 0.5)
			{
				y += yStep;
				error -= 1.0;
			}
		}
	}

	return (status = 0);
}


static int driverDrawRect(graphicBuffer *buffer, color *foreground,
	drawMode mode, int xCoord, int yCoord, int width, int height,
	int thickness, int fill)
{
	// Draws a rectangle into the buffer using the supplied foreground color

	int status = 0;
	int scanLineBytes = 0;
	int endX = (xCoord + (width - 1));
	int endY = (yCoord + (height - 1));
	int lineBytes = 0;
	unsigned char *lineBuffer = NULL;
	void *bufferPointer = NULL;
	int count;

	// If the supplied graphicBuffer is NULL, we draw directly to the whole
	// screen
	if (!buffer)
	{
		buffer = &wholeScreen;

		// For performance reasons, we don't want to use the framebuffer
		// memory itself as a linebuffer
		lineBuffer = adapter->lineBuffer;
	}

	// Out of the buffer entirely?
	if (((xCoord + width) <= 0) || (xCoord >= buffer->width) ||
		((yCoord + height) <= 0) || (yCoord >= buffer->height))
	{
		return (status = ERR_BOUNDS);
	}

	scanLineBytes = (buffer->width * adapter->bytesPerPixel);
	if (buffer->data == adapter->framebuffer)
		scanLineBytes = adapter->scanLineBytes;

	// See whether the thickness makes it equivalent to a fill.  I.e. more
	// than half (rounded down) the width or height.
	if ((thickness > (width >> 1)) || (thickness > (height >> 1)))
		fill = 1;

	if (fill)
	{
		// Off the left edge of the buffer?
		if (xCoord < 0)
		{
			width += xCoord;
			xCoord = 0;
		}

		// Off the top of the buffer?
		if (yCoord < 0)
		{
			height += yCoord;
			yCoord = 0;
		}

		// Off the right edge of the buffer?
		if ((xCoord + width) >= buffer->width)
			width = (buffer->width - xCoord);

		// Off the bottom of the buffer?
		if ((yCoord + height) >= buffer->height)
			height = (buffer->height - yCoord);

		// Re-set these values
		endX = (xCoord + (width - 1));
		endY = (yCoord + (height - 1));

		if ((mode == draw_or) || (mode == draw_xor))
		{
			// Just draw a series of lines, since every pixel needs to be
			// dealt with individually and we can't really do that better than
			// the line drawing function does already.
			for (count = yCoord; count <= endY; count ++)
			{
				driverDrawLine(buffer, foreground, mode, xCoord, count, endX,
					count);
			}
		}
		else
		{
			// Draw the box manually

			lineBytes = (width * adapter->bytesPerPixel);

			// Do the first line manually

			// Point to the starting place
			bufferPointer = (buffer->data + (yCoord * scanLineBytes) +
				(xCoord * adapter->bytesPerPixel));

			// If we're not using the adapter's linebuffer, use the first line
			// of the target buffer
			if (!lineBuffer)
				lineBuffer = (unsigned char *) bufferPointer;

			// Do a loop through the line, copying the color values
			// consecutively

			if ((adapter->bitsPerPixel == 32) ||
				(adapter->bitsPerPixel == 24))
			{
				for (count = 0; count < lineBytes; )
				{
					lineBuffer[count++] = foreground->blue;
					lineBuffer[count++] = foreground->green;
					lineBuffer[count++] = foreground->red;
					if (adapter->bitsPerPixel == 32)
						count++;
				}
			}

			else if ((adapter->bitsPerPixel == 16) ||
				(adapter->bitsPerPixel == 15))
			{
				short pix = 0;

				if (adapter->bitsPerPixel == 16)
				{
					pix = (((foreground->red >> 3) << 11) |
						((foreground->green >> 2) << 5) |
						(foreground->blue >> 3));
				}
				else
				{
					pix = (((foreground->red >> 3) << 10) |
						((foreground->green >> 3) << 5) |
						(foreground->blue >> 3));
				}

				for (count = 0; count < width; count ++)
					((short *) lineBuffer)[count] = pix;
			}

			// If we're using the adapter's linebuffer, copy the first line
			if (lineBuffer != bufferPointer)
				processorCopyBytes(lineBuffer, bufferPointer, lineBytes);

			// Copy the line 'height' -1 times
			for (count = 1; count < height; count ++)
			{
				bufferPointer += scanLineBytes;
				processorCopyBytes(lineBuffer, bufferPointer, lineBytes);
			}
		}
	}
	else if (thickness)
	{
		// Draw the top line 'thickness' times
		for (count = (yCoord + thickness - 1); count >= yCoord; count --)
		{
			driverDrawLine(buffer, foreground, mode, xCoord, count, endX,
				count);
		}

		// Draw the left line 'thickness' times
		for (count = (xCoord + thickness - 1); count >= xCoord; count --)
		{
			driverDrawLine(buffer, foreground, mode, count, (yCoord +
				thickness), count, (endY - thickness));
		}

		// Draw the bottom line 'thickness' times
		for (count = (endY - thickness + 1); count <= endY; count ++)
		{
			driverDrawLine(buffer, foreground, mode, xCoord, count, endX,
				count);
		}

		// Draw the right line 'thickness' times
		for (count = (endX - thickness + 1); count <= endX; count ++)
		{
			driverDrawLine(buffer, foreground, mode, count, (yCoord +
				thickness), count, (endY - thickness));
		}
	}

	return (status = 0);
}


static int driverDrawOval(graphicBuffer *buffer, color *foreground,
	drawMode mode, int xCoord, int yCoord, int width, int height,
	int thickness, int fill)
{
	// Draws an oval into the buffer using the supplied foreground color.  We
	// use a version of the Bresenham circle algorithm, but in the case of an
	// (unfilled) oval with (thickness > 1) we calculate inner and outer
	// ovals, and draw lines between the inner and outer X coordinates of
	// both, for any given Y coordinate.

	int status = 0;
	int centerX = (xCoord + (width / 2));
	int centerY = (yCoord + (height / 2));
	int outerRadius = (width >> 1);
	int outerD = (3 - (outerRadius << 1));
	int outerX = 0;
	int outerY = outerRadius;
	int innerRadius = 0, innerD = 0, innerX = 0, innerY = 0;
	int *outerBitmap = NULL;
	int *innerBitmap = NULL;

	// If the supplied graphicBuffer is NULL, we draw directly to the whole
	// screen
	if (!buffer)
		buffer = &wholeScreen;

	// For now, we only support circles
	if (width != height)
	{
		kernelError(kernel_error, "The framebuffer driver only supports "
			"circular ovals");
		return (status = ERR_NOTIMPLEMENTED);
	}

	outerBitmap = kernelMalloc((outerRadius + 1) * sizeof(int));
	if (!outerBitmap)
		return (status = ERR_MEMORY);

	if ((thickness > 1) && !fill)
	{
		innerRadius = (outerRadius - thickness + 1);
		innerD = (3 - (innerRadius << 1));
		innerY = innerRadius;

		innerBitmap = kernelMalloc((innerRadius + 1) * sizeof(int));
		if (!innerBitmap)
			return (status = ERR_MEMORY);
	}

	while (outerX <= outerY)
	{
		if (!fill && (thickness == 1))
		{
			driverDrawPixel(buffer, foreground, mode, (centerX + outerX),
				(centerY + outerY));
			driverDrawPixel(buffer, foreground, mode, (centerX + outerX),
				(centerY - outerY));
			driverDrawPixel(buffer, foreground, mode, (centerX - outerX),
				(centerY + outerY));
			driverDrawPixel(buffer, foreground, mode, (centerX - outerX),
				(centerY - outerY));
			driverDrawPixel(buffer, foreground, mode, (centerX + outerY),
				(centerY + outerX));
			driverDrawPixel(buffer, foreground, mode, (centerX + outerY),
				(centerY - outerX));
			driverDrawPixel(buffer, foreground, mode, (centerX - outerY),
				(centerY + outerX));
			driverDrawPixel(buffer, foreground, mode, (centerX - outerY),
				(centerY - outerX));
		}

		if (outerY > outerBitmap[outerX])
			outerBitmap[outerX] = outerY;
		if (outerX > outerBitmap[outerY])
			outerBitmap[outerY] = outerX;

		if (outerD < 0)
		{
			outerD += ((outerX << 2) + 6);
		}
		else
		{
			outerD += (((outerX - outerY) << 2) + 10);
			outerY -= 1;
		}
		outerX += 1;

		if ((thickness > 1) && !fill)
		{
			if (!innerBitmap[innerX] || (innerY < innerBitmap[innerX]))
				innerBitmap[innerX] = innerY;
			if (!innerBitmap[innerY] || (innerX < innerBitmap[innerY]))
				innerBitmap[innerY] = innerX;

			if (innerD < 0)
			{
				innerD += ((innerX << 2) + 6);
			}
			else
			{
				innerD += (((innerX - innerY) << 2) + 10);
				innerY -= 1;
			}
			innerX += 1;
		}
	}

	if ((thickness > 1) || fill)
	{
		for (outerY = 0; outerY <= outerRadius; outerY ++)
		{
			if ((outerY > innerRadius) || fill)
			{
				driverDrawLine(buffer, foreground, mode,
					(centerX - outerBitmap[outerY]), (centerY - outerY),
					(centerX + outerBitmap[outerY]), (centerY - outerY));
				driverDrawLine(buffer, foreground, mode,
					(centerX - outerBitmap[outerY]), (centerY + outerY),
					(centerX + outerBitmap[outerY]), (centerY + outerY));
			}
			else
			{
				driverDrawLine(buffer, foreground, mode,
					(centerX - outerBitmap[outerY]), (centerY - outerY),
					(centerX - innerBitmap[outerY]), (centerY - outerY));
				driverDrawLine(buffer, foreground, mode,
					(centerX + innerBitmap[outerY]), (centerY - outerY),
					(centerX + outerBitmap[outerY]), (centerY - outerY));
				driverDrawLine(buffer, foreground, mode,
					(centerX - outerBitmap[outerY]), (centerY + outerY),
					(centerX - innerBitmap[outerY]), (centerY + outerY));
				driverDrawLine(buffer, foreground, mode,
					(centerX + innerBitmap[outerY]), (centerY + outerY),
					(centerX + outerBitmap[outerY]), (centerY + outerY));
			}
		}
	}

	kernelFree(outerBitmap);
	if ((thickness > 1) && !fill)
		kernelFree(innerBitmap);

	return (status = 0);
}


static int driverDrawMonoImage(graphicBuffer *buffer, image *drawImage,
	drawMode mode, color *foreground, color *background, int xCoord,
	int yCoord)
{
	// Draws the supplied image into the buffer at the requested coordinates

	int status = 0;
	int lineLength = 0;
	int numberLines = 0;
	int xOffset = 0, yOffset = 0;
	int lineBytes = 0;
	int scanLineBytes = 0;
	unsigned char *bufferPointer = NULL;
	unsigned char *monoImageData = NULL;
	register unsigned pixelCounter = 0;
	int lineCounter = 0;
	short onPixel, offPixel;
	int count;

	// If the supplied graphicBuffer is NULL, we draw directly to the whole
	// screen
	if (!buffer)
		buffer = &wholeScreen;

	// Make sure it's a mono image
	if (drawImage->type != IMAGETYPE_MONO)
		return (status = ERR_INVALID);

	lineLength = drawImage->width;
	numberLines = drawImage->height;

	// If the image is outside the buffer entirely, skip it
	if (((xCoord + lineLength) <= 0) || (xCoord >= buffer->width) ||
		((yCoord + numberLines) <= 0) || (yCoord >= buffer->height))
	{
		return (status = ERR_BOUNDS);
	}

	// If the image goes off the sides of the buffer, only attempt to display
	// the pixels that will fit

	if (xCoord < 0)
	{
		lineLength += xCoord;
		xOffset -= xCoord;
		xCoord = 0;
	}

	if ((xCoord + lineLength) >= buffer->width)
		lineLength -= ((xCoord + lineLength) - buffer->width);

	// If the image goes off the top or bottom of the buffer, only show the
	// lines that will fit

	if (yCoord < 0)
	{
		numberLines += yCoord;
		yOffset -= yCoord;
		yCoord = 0;
	}

	if ((yCoord + numberLines) >= buffer->height)
		numberLines -= ((yCoord + numberLines) - buffer->height);

	// Images are lovely little data structures that give us image data in the
	// most convenient form we can imagine.

	// How many bytes in a line of data?
	lineBytes = (adapter->bytesPerPixel * lineLength);

	// How many bytes in a line of buffer?
	scanLineBytes = (buffer->width * adapter->bytesPerPixel);
	if (buffer->data == adapter->framebuffer)
		scanLineBytes = adapter->scanLineBytes;

	bufferPointer = (buffer->data + (yCoord * scanLineBytes) + (xCoord *
		adapter->bytesPerPixel));

	// A mono image has a bitmap of 'on' bits and 'off' bits.  We will draw
	// all 'on' bits using the current foreground color.
	monoImageData = (unsigned char *) drawImage->data;

	pixelCounter = ((yOffset * drawImage->width) + xOffset);

	// Loop for each line

	for (lineCounter = 0; lineCounter < numberLines; lineCounter++)
	{
		// Do a loop through the line, copying either the foreground color
		// value or the background color into buffer

		if ((adapter->bitsPerPixel == 32) || (adapter->bitsPerPixel == 24))
		{
			for (count = 0; count < lineBytes; pixelCounter ++)
			{
				// Isolate the bit from the bitmap
				if (monoImageData[pixelCounter / 8] &
					(0x80 >> (pixelCounter % 8)))
				{
					// 'on' bit.
					bufferPointer[count++] = foreground->blue;
					bufferPointer[count++] = foreground->green;
					bufferPointer[count++] = foreground->red;
					if (adapter->bitsPerPixel == 32)
						count++;
				}
				else
				{
					if (mode == draw_translucent)
					{
						count += adapter->bytesPerPixel;
					}
					else
					{
						// 'off' bit.
						bufferPointer[count++] = background->blue;
						bufferPointer[count++] = background->green;
						bufferPointer[count++] = background->red;
						if (adapter->bitsPerPixel == 32)
							count++;
					}
				}
			}
		}

		else if ((adapter->bitsPerPixel == 16) ||
			(adapter->bitsPerPixel == 15))
		{
			if (adapter->bitsPerPixel == 16)
			{
				onPixel = (((foreground->red >> 3) << 11) |
					((foreground->green >> 2) << 5) |
					(foreground->blue >> 3));
				offPixel = (((background->red >> 3) << 11) |
					((background->green >> 2) << 5) |
					(background->blue >> 3));
			}
			else
			{
				onPixel = (((foreground->red >> 3) << 10) |
					((foreground->green >> 3) << 5) |
					(foreground->blue >> 3));
				offPixel = (((background->red >> 3) << 10) |
					((background->green >> 3) << 5) |
					(background->blue >> 3));
			}

			for (count = 0; count < lineLength; count ++, pixelCounter ++)
			{
				// Isolate the bit from the bitmap
				if (monoImageData[pixelCounter / 8] &
					(0x80 >> (pixelCounter % 8)))
				{
					// 'on' bit.
					((short *) bufferPointer)[count] = onPixel;
				}

				else if (mode != draw_translucent)
				{
					// 'off' bit
					((short *) bufferPointer)[count] = offPixel;
				}
			}
		}

		// Move to the next line in the buffer
		bufferPointer += scanLineBytes;

		// Are we skipping any of this line because it's off the buffer?
		if (drawImage->width > (unsigned) lineLength)
			pixelCounter += (drawImage->width - lineLength);
	}

	// Success
	return (status = 0);
}


static int driverDrawImage(graphicBuffer *buffer, image *drawImage,
	drawMode mode, int xCoord, int yCoord, int xOffset, int yOffset,
	int width, int height)
{
	// Draws the requested width and height of the supplied image into the
	// buffer at the requested coordinates, with the requested offset, width,
	// and height

	int status = 0;
	int lineLength = 0;
	int numberLines = 0;
	int lineBytes = 0;
	int scanLineBytes = 0;
	unsigned char *bufferPointer = NULL;
	pixel *imageData = NULL;
	register unsigned pixelCounter = 0;
	int lineCounter = 0;
	short pix = 0;
	int count;

	// If the supplied graphicBuffer is NULL, we draw directly to the whole
	// screen
	if (!buffer)
		buffer = &wholeScreen;

	// Make sure it's a color image
	if (drawImage->type == IMAGETYPE_MONO)
		return (status = ERR_INVALID);

	lineLength = drawImage->width;
	if (width)
		lineLength = width;

	numberLines = drawImage->height;
	if (height)
		numberLines = height;

	// If the image is outside the buffer entirely, skip it
	if (((xCoord + lineLength) <= 0) || (xCoord >= buffer->width) ||
		((yCoord + numberLines) <= 0) || (yCoord >= buffer->height))
	{
		return (status = ERR_BOUNDS);
	}

	// If the image goes off the sides of the buffer, only attempt to display
	// the pixels that will fit

	if (xCoord < 0)
	{
		lineLength += xCoord;
		xOffset -= xCoord;
		xCoord = 0;
	}

	if ((xCoord + lineLength) >= buffer->width)
		lineLength -= ((xCoord + lineLength) - buffer->width);
	if ((unsigned)(xOffset + lineLength) >= drawImage->width)
		lineLength -= ((xOffset + lineLength) - drawImage->width);

	// If the image goes off the top or bottom of the buffer, only show the
	// lines that will fit

	if (yCoord < 0)
	{
		numberLines += yCoord;
		yOffset -= yCoord;
		yCoord = 0;
	}

	if ((yCoord + numberLines) >= buffer->height)
		numberLines -= ((yCoord + numberLines) - buffer->height);
	if ((unsigned)(yOffset + numberLines) >= drawImage->height)
		numberLines -= ((yOffset + numberLines) - drawImage->height);

	// Images are lovely little data structures that give us image data in the
	// most convenient form we can imagine.

	// How many bytes in a line of data?
	lineBytes = (adapter->bytesPerPixel * lineLength);

	// How many bytes in a line of buffer?
	scanLineBytes = (buffer->width * adapter->bytesPerPixel);
	if (buffer->data == adapter->framebuffer)
		scanLineBytes = adapter->scanLineBytes;

	bufferPointer = (buffer->data + (yCoord * scanLineBytes) + (xCoord *
		adapter->bytesPerPixel));

	imageData = (pixel *) drawImage->data;

	pixelCounter = ((yOffset * drawImage->width) + xOffset);

	// Loop for each line

	for (lineCounter = 0; lineCounter < numberLines; lineCounter++)
	{
		// Do a loop through the line, copying the color values from the
		// image into the buffer

		if ((adapter->bitsPerPixel == 32) || (adapter->bitsPerPixel == 24))
		{
			for (count = 0; count < lineBytes; pixelCounter ++)
			{
				if ((mode == draw_translucent) &&
					PIXELS_EQ(&imageData[pixelCounter],
						&drawImage->transColor))
				{
					// Translucent pixel, just skip it.
					count += adapter->bytesPerPixel;
				}

				else if ((mode == draw_alphablend) && drawImage->alpha &&
					 (drawImage->alpha[pixelCounter] < 1.0))
				{
					if (drawImage->alpha[pixelCounter] > 0)
					{
						// Partially-opaque pixel.  Alpha blend it with the
						// contents of the buffer.
						alphaBlend32(&imageData[pixelCounter],
							drawImage->alpha[pixelCounter],
							(pixel *) &bufferPointer[count]);
					}

					count += adapter->bytesPerPixel;
				}

				else
				{
					bufferPointer[count++] = imageData[pixelCounter].blue;
					bufferPointer[count++] = imageData[pixelCounter].green;
					bufferPointer[count++] = imageData[pixelCounter].red;
					if (adapter->bitsPerPixel == 32)
						count++;
				}
			}
		}

		else if ((adapter->bitsPerPixel == 16) ||
			(adapter->bitsPerPixel == 15))
		{
			for (count = 0; count < lineLength; count ++, pixelCounter ++)
			{
				if ((mode == draw_translucent) &&
					PIXELS_EQ(&imageData[pixelCounter],
						&drawImage->transColor))
				{
					// Translucent pixel, just skip it.
					continue;
				}

				else if ((mode == draw_alphablend) && drawImage->alpha &&
					 (drawImage->alpha[pixelCounter] < 1.0))
				{
					if (drawImage->alpha[pixelCounter] > 0)
					{
						// Partially-opaque pixel.  Alpha blend it with the
						// contents of the buffer.
						alphaBlend16(&imageData[pixelCounter],
							drawImage->alpha[pixelCounter],
							(short *)(bufferPointer + (count * 2)));
					}
				}

				else
				{
					if (adapter->bitsPerPixel == 16)
					{
						pix = (((imageData[pixelCounter].red >> 3) << 11) |
							((imageData[pixelCounter].green >> 2) << 5) |
							(imageData[pixelCounter].blue >> 3));
					}
					else
					{
						pix = (((imageData[pixelCounter].red >> 3) << 10) |
							((imageData[pixelCounter].green >> 3) << 5) |
							(imageData[pixelCounter].blue >> 3));
					}

					((short *) bufferPointer)[count] = pix;
				}
			}
		}

		// Move to the next line in the buffer
		bufferPointer += scanLineBytes;

		// Are we skipping any of this line because it's off the buffer?
		if (drawImage->width > (unsigned) lineLength)
			pixelCounter += (drawImage->width - lineLength);
	}

	// Success
	return (status = 0);
}


static int driverGetImage(graphicBuffer *buffer, image *theImage, int xCoord,
	int yCoord, int width, int height)
{
	// From a clip of the supplied buffer, make an image from its contents.

	int status = 0;
	int scanLineBytes = 0;
	int lineLength = 0;
	int numberLines = 0;
	int lineBytes = 0;
	unsigned char *bufferPointer = NULL;
	pixel *imageData = NULL;
	int lineCounter = 0;
	unsigned pixelCounter = 0;
	int count;

	// If the supplied graphicBuffer is NULL, we read directly from the whole
	// screen
	if (!buffer)
		buffer = &wholeScreen;

	// Check params
	if ((xCoord < 0) || (xCoord >= buffer->width) ||
		(yCoord < 0) || (yCoord >= buffer->height))
	{
		return (status = ERR_BOUNDS);
	}

	scanLineBytes = (buffer->width * adapter->bytesPerPixel);
	if (buffer->data == adapter->framebuffer)
		scanLineBytes = adapter->scanLineBytes;

	// If the clip goes off the right edge of the buffer, only grab what
	// exists.
	if ((xCoord + width) < buffer->width)
		lineLength = width;
	else
		lineLength = (buffer->width - xCoord);

	// If the clip goes off the bottom of the buffer, only grab what exists.
	if ((height + yCoord) < buffer->height)
		numberLines = height;
	else
		numberLines = (buffer->height - yCoord);

	// Get an image
	status = kernelImageNew(theImage, lineLength, numberLines);
	if (status < 0)
		return (status);

	// How many bytes in a line of data?
	lineBytes = (adapter->bytesPerPixel * lineLength);

	// Figure out the starting memory location in the buffer
	bufferPointer = (buffer->data + (yCoord * scanLineBytes) + (xCoord *
		adapter->bytesPerPixel));

	imageData = (pixel *) theImage->data;

	// Now loop through each line of the buffer, filling the image data from
	// the screen

	for (lineCounter = 0; lineCounter < numberLines; lineCounter++)
	{
		// Do a loop through the line, copying the color values from the
		// buffer into the image data

		if ((adapter->bitsPerPixel == 32) || (adapter->bitsPerPixel == 24))
		{
			for (count = 0; count < lineBytes; pixelCounter ++)
			{
				imageData[pixelCounter].blue = bufferPointer[count++];
				imageData[pixelCounter].green = bufferPointer[count++];
				imageData[pixelCounter].red = bufferPointer[count++];
				if (adapter->bitsPerPixel == 32)
					count++;
			}
		}

		else if ((adapter->bitsPerPixel == 16) ||
			(adapter->bitsPerPixel == 15))
		{
			for (count = 0; count < lineLength; count ++, pixelCounter ++)
			{
				short pix = ((short *) bufferPointer)[count];

				if (adapter->bitsPerPixel == 16)
				{
					imageData[pixelCounter].red = (unsigned char)
						(((pix & 0xF800) >> 11) * 8.225806452);
					imageData[pixelCounter].green = (unsigned char)
						(((pix & 0x07E0) >> 5) * 4.047619048);
					imageData[pixelCounter].blue = (unsigned char)
						((pix & 0x001F) * 8.225806452);
				}
				else
				{
					imageData[pixelCounter].red = (unsigned char)
						(((pix & 0x7C00) >> 10) * 8.225806452);
					imageData[pixelCounter].green = (unsigned char)
						(((pix & 0x03E0) >> 5) * 8.225806452);
					imageData[pixelCounter].blue = (unsigned char)
						((pix & 0x001F) * 8.225806452);
				}
			}
		}

		// Move to the next line in the buffer
		bufferPointer += scanLineBytes;
	}

	return (status = 0);
}


static int driverCopyArea(graphicBuffer *buffer, int xCoord1, int yCoord1,
	int width, int height, int xCoord2, int yCoord2)
{
	// Copy a clip of data from one area of the buffer to another

	int status = 0;
	int scanLineBytes = 0;
	unsigned char *srcPointer = NULL;
	unsigned char *destPointer = NULL;
	int count;

	// If the supplied graphicBuffer is NULL, we draw directly to the whole
	// screen
	if (!buffer)
		buffer = &wholeScreen;

	scanLineBytes = (buffer->width * adapter->bytesPerPixel);
	if (buffer->data == adapter->framebuffer)
		scanLineBytes = adapter->scanLineBytes;

	// Make sure we're not going outside the buffer
	if (xCoord1 < 0)
	{
		width += xCoord1;
		xCoord1 = 0;
	}
	if (yCoord1 < 0)
	{
		height += yCoord1;
		yCoord1 = 0;
	}

	if ((xCoord1 + width) >= buffer->width)
		width -= ((xCoord1 + width) - buffer->width);
	if ((yCoord1 + height) >= buffer->height)
		height -= ((yCoord1 + height) - buffer->height);

	if (xCoord2 < 0)
	{
		width += xCoord2;
		xCoord2 = 0;
	}
	if (yCoord2 < 0)
	{
		height += yCoord2;
		yCoord2 = 0;
	}

	if ((xCoord2 + width) >= buffer->width)
		width -= ((xCoord2 + width) - buffer->width);
	if ((yCoord2 + height) >= buffer->height)
		height -= ((yCoord2 + height) - buffer->height);

	// Anything to do?
	if ((width <= 0) || (height <= 0))
		return (status = 0);

	srcPointer = (buffer->data + (yCoord1 * scanLineBytes) + (xCoord1 *
		adapter->bytesPerPixel));
	destPointer = (buffer->data + (yCoord2 * scanLineBytes) + (xCoord2 *
		adapter->bytesPerPixel));

	for (count = yCoord1; count <= (yCoord1 + height - 1); count ++)
	{
		memcpy(destPointer, srcPointer, (width * adapter->bytesPerPixel));
		srcPointer += scanLineBytes;
		destPointer += scanLineBytes;
	}

	return (status = 0);
}


static int driverRenderBuffer(graphicBuffer *buffer, int drawX, int drawY,
	int clipX, int clipY, int width, int height)
{
	// Take the supplied graphic buffer and render it onto the screen.

	int status = 0;
	void *bufferPointer = NULL;
	void *screenPointer = NULL;

	// This function is the single instance where a NULL buffer is not
	// allowed, since we are drawing the buffer to the screen this time
	if (!buffer)
		return (status = ERR_NULLPARAMETER);

	// Not allowed to specify a clip that is not fully inside the buffer
	if ((clipX < 0) || ((clipX + width) > buffer->width) ||
		(clipY < 0) || ((clipY + height) > buffer->height))
	{
		return (status = ERR_RANGE);
	}

	// Get the line length of each line that we want to draw and cut them
	// off if the area will extend past the screen boundaries.
	if ((drawX + clipX) < 0)
	{
		width += (drawX + clipX);
		clipX -= (drawX + clipX);
	}

	if ((drawX + clipX + width) >= wholeScreen.width)
		width = (wholeScreen.width - (drawX + clipX));

	if ((drawY + clipY) < 0)
	{
		height += (drawY + clipY);
		clipY -= (drawY + clipY);
	}

	if ((drawY + clipY + height) >= wholeScreen.height)
		height = (wholeScreen.height - (drawY + clipY));

	// Don't draw if the whole area is off the screen
	if (((drawX + clipX) >= wholeScreen.width) ||
		((drawY + clipY) >= wholeScreen.height))
	{
		return (status = 0);
	}

	// Calculate the starting offset inside the buffer
	bufferPointer = (buffer->data + (clipY * (buffer->width *
		adapter->bytesPerPixel)) + (clipX * adapter->bytesPerPixel));

	// Calculate the starting offset on the screen
	screenPointer = (wholeScreen.data + ((drawY + clipY) *
		adapter->scanLineBytes) + ((drawX + clipX) * adapter->bytesPerPixel));

	// Start copying lines
	for ( ; height > 0; height --)
	{
		memcpy(screenPointer, bufferPointer, (width *
			adapter->bytesPerPixel));
		bufferPointer += (buffer->width * adapter->bytesPerPixel);
		screenPointer += adapter->scanLineBytes;
	}

	return (status = 0);
}


static int driverFilter(graphicBuffer *buffer, color *filterColor, int xCoord,
	int yCoord, int width, int height)
{
	// Take an area of a buffer and average it with the supplied color

	int status = 0;
	int scanLineBytes = 0;
	int lineBytes = 0;
	unsigned char *bufferPointer = NULL;
	int red, green, blue;
	int lineCount, count;

	// If the supplied graphicBuffer is NULL, we draw directly to the whole
	// screen
	if (!buffer)
		buffer = &wholeScreen;

	// Out of the buffer entirely?
	if ((xCoord >= buffer->width) || (yCoord >= buffer->height))
		return (status = ERR_BOUNDS);

	scanLineBytes = (buffer->width * adapter->bytesPerPixel);
	if (buffer->data == adapter->framebuffer)
		scanLineBytes = adapter->scanLineBytes;

	// Off the left edge of the buffer?
	if (xCoord < 0)
	{
		width += xCoord;
		xCoord = 0;
	}

	// Off the top of the buffer?
	if (yCoord < 0)
	{
		height += yCoord;
		yCoord = 0;
	}

	// Off the right edge of the buffer?
	if ((xCoord + width) >= buffer->width)
		width = (buffer->width - xCoord);

	// Off the bottom of the buffer?
	if ((yCoord + height) >= buffer->height)
		height = (buffer->height - yCoord);

	// How many bytes in the line?
	lineBytes = (adapter->bytesPerPixel * width);

	bufferPointer = (buffer->data + (yCoord * scanLineBytes) + (xCoord *
		adapter->bytesPerPixel));

	// Do a loop through each line, copying the color values consecutively
	for (lineCount = 0; lineCount < height; lineCount ++)
	{
		if ((adapter->bitsPerPixel == 32) || (adapter->bitsPerPixel == 24))
		{
			for (count = 0; count < lineBytes; )
			{
				bufferPointer[count] = ((bufferPointer[count] +
					filterColor->blue) / 2);
				bufferPointer[count + 1] = ((bufferPointer[count + 1] +
					filterColor->green) / 2);
				bufferPointer[count + 2] = ((bufferPointer[count + 2] +
					filterColor->red) / 2);

				count += 3;
				if (adapter->bitsPerPixel == 32)
					count++;
			}
		}

		else if ((adapter->bitsPerPixel == 16) ||
			(adapter->bitsPerPixel == 15))
		{
			for (count = 0; count < width; count ++)
			{
				short *ptr = (short *) bufferPointer;

				blue = ((((ptr[count] & 0x001F) +
					(filterColor->blue >> 3)) >> 1) & 0x001F);

				if (adapter->bitsPerPixel == 16)
				{
					red = (((((ptr[count] & 0xF800) >> 11) +
						(filterColor->red >> 3)) >> 1) & 0x001F);
					green = (((((ptr[count] & 0x07E0) >> 5) +
						(filterColor->green >> 2)) >> 1) & 0x003F);
					ptr[count] = (short)((red << 11) | (green << 5) | blue);
				}
				else
				{
					red = (((((ptr[count] & 0x7C00) >> 10) +
						(filterColor->red >> 3)) >> 1) & 0x001F);
					green = (((((ptr[count] & 0x03E0) >> 5) +
						(filterColor->green >> 3)) >> 1) & 0x001F);
					ptr[count] = (short)((red << 10) | (green << 5) | blue);
				}
			}
		}

		bufferPointer += scanLineBytes;
	}

	return (status = 0);
}


static int driverDetect(void *parent, kernelDriver *driver)
{
	// This function is used to detect and initialize each device, as well as
	// registering each one with any higher-level interfaces

	int status = 0;
	kernelDevice *dev = NULL;

	// Allocate memory for the device
	dev = kernelMalloc(sizeof(kernelDevice) + sizeof(kernelGraphicAdapter));
	if (!dev)
		return (status = 0);

	adapter = ((void *) dev + sizeof(kernelDevice));

	// Set up the device parameters
	adapter->videoMemory = kernelOsLoaderInfo->graphicsInfo.videoMemory;
	adapter->framebuffer = (void *)
		kernelOsLoaderInfo->graphicsInfo.framebuffer;
	adapter->mode = kernelOsLoaderInfo->graphicsInfo.mode;
	adapter->xRes = kernelOsLoaderInfo->graphicsInfo.xRes;
	adapter->yRes = kernelOsLoaderInfo->graphicsInfo.yRes;
	adapter->bitsPerPixel = kernelOsLoaderInfo->graphicsInfo.bitsPerPixel;
	if (adapter->bitsPerPixel == 15)
		adapter->bytesPerPixel = 2;
	else
		adapter->bytesPerPixel = (adapter->bitsPerPixel / 8);
	adapter->scanLineBytes = kernelOsLoaderInfo->graphicsInfo.scanLineBytes;
	adapter->numberModes = kernelOsLoaderInfo->graphicsInfo.numberModes;
	memcpy(&adapter->supportedModes,
		&kernelOsLoaderInfo->graphicsInfo.supportedModes,
		(sizeof(videoMode) * MAXVIDEOMODES));

	dev->device.class = kernelDeviceGetClass(DEVICECLASS_GRAPHIC);
	dev->device.subClass =
		kernelDeviceGetClass(DEVICESUBCLASS_GRAPHIC_FRAMEBUFFER);
	dev->driver = driver;
	dev->data = adapter;

	// If we are in a graphics mode, initialize the graphics functions
	if (adapter->mode)
	{
		// Map the supplied physical linear framebuffer address into kernel
		// memory
		status = kernelPageMapToFree(KERNELPROCID, (unsigned)
			adapter->framebuffer, &adapter->framebuffer, (adapter->yRes *
			adapter->scanLineBytes));
		if (status < 0)
		{
			kernelError(kernel_error, "Unable to map linear framebuffer");
			return (status);
		}

		status = kernelGraphicInitialize(dev);
		if (status < 0)
			return (status);
	}

	// Set up the graphicBuffer that represents the whole screen
	wholeScreen.width = adapter->xRes;
	wholeScreen.height = adapter->yRes;
	wholeScreen.data = adapter->framebuffer;

	adapter->lineBuffer = kernelMalloc(adapter->scanLineBytes);
	if (!adapter->lineBuffer)
		return (status = ERR_MEMORY);

	// Add the kernel device
	return (status = kernelDeviceAdd(parent, dev));
}


static kernelGraphicOps framebufferOps = {
	driverClearScreen,
	driverDrawPixel,
	driverDrawLine,
	driverDrawRect,
	driverDrawOval,
	driverDrawMonoImage,
	driverDrawImage,
	driverGetImage,
	driverCopyArea,
	driverRenderBuffer,
	driverFilter,
};


/////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////
//
// Below here, the functions are exported for external use
//
/////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////

void kernelFramebufferGraphicDriverRegister(kernelDriver *driver)
{
	 // Device driver registration.

	driver->driverDetect = driverDetect;
	driver->ops = &framebufferOps;

	return;
}

