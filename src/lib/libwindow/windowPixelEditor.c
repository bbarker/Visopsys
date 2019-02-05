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
//  windowPixelEditor.c
//

// This contains functions for user programs to operate GUI components.

#include <errno.h>
#include <stdlib.h>
#include <sys/api.h>
#include <sys/image.h>
#include <sys/window.h>

#define MIN_PIXELSIZE	5

extern int libwindow_initialized;
extern void libwindowInitialize(void);


static void calcNumPixels(windowPixelEditor *editor)
{
	// Calculate initial numbers of pixels for each axis, using the current
	// pixel size.
	editor->horizPixels = min(((editor->width - 1) / editor->pixelSize),
		(int) editor->img->width);
	editor->vertPixels = min(((editor->height - 1) / editor->pixelSize),
		(int) editor->img->height);
}


static void calcDisplayPercentage(windowPixelEditor *editor)
{
	// Calculate the scroll bars' display percentage
	editor->horiz.displayPercent = ((editor->horizPixels * 100) /
		(int) editor->img->width);
	editor->vert.displayPercent = ((editor->vertPixels * 100) /
		(int) editor->img->height);
}


static void drawGrid(windowPixelEditor *editor)
{
	// Draw the grid

	windowDrawParameters params;
	int rowCount, columnCount;

	// Clear our drawing parameters
	memset(&params, 0, sizeof(windowDrawParameters));

	params.operation = draw_line;
	params.mode = draw_xor;
	params.thickness = 1;
	params.buffer = 1;
	memcpy(&params.foreground, &editor->background, sizeof(color));

	// Horizontal lines
	params.xCoord2 = ((editor->horizPixels * editor->pixelSize) - 1);
	for (rowCount = 0; rowCount <= editor->vertPixels; rowCount ++)
	{
		windowComponentSetData(editor->canvas, &params, 1, 0 /* no redraw */);
		params.yCoord1 += editor->pixelSize;
		params.yCoord2 = params.yCoord1;
	}

	// Vertical lines
	params.xCoord1 = params.yCoord1 = params.xCoord2 = 0;
	params.yCoord2 = ((editor->vertPixels * editor->pixelSize) - 1);
	for (columnCount = 0; columnCount <= editor->horizPixels; columnCount ++)
	{
		windowComponentSetData(editor->canvas, &params, 1,
			(columnCount == editor->horizPixels));
		params.xCoord1 += editor->pixelSize;
		params.xCoord2 = params.xCoord1;

		if (columnCount == editor->horizPixels)
			params.buffer = 0;
	}
}


static void draw(windowPixelEditor *editor)
{
	pixel *pixels = editor->img->data;
	int current = 0;
	windowDrawParameters params;
	int rowCount, columnCount;

	// Calculate the starting horizonal and vertical pixel, based on the scroll
	// bars' position percentage.
	editor->startHoriz = ((((int) editor->img->width - editor->horizPixels) *
		editor->horiz.positionPercent) / 100);
	editor-> startVert = ((((int) editor->img->height - editor->vertPixels) *
		editor->vert.positionPercent) / 100);

	// Calculate a new pixel size.
	while (((editor->horizPixels * editor->pixelSize) > (editor->width - 1)) ||
		((editor->vertPixels * editor->pixelSize) > (editor->height - 1)))
	{
		editor->pixelSize -= 1;
	}
	while (((editor->horizPixels * (editor->pixelSize + 1)) <=
			(editor->width - 1)) &&
		((editor->vertPixels * (editor->pixelSize + 1)) <=
			(editor->height - 1)))
	{
		editor->pixelSize += 1;
	}

	// Clear our drawing parameters
	memset(&params, 0, sizeof(windowDrawParameters));

	// Clear the background
	params.operation = draw_rect;
	params.mode = draw_normal;
	params.width = editor->width;
	params.height = editor->height;
	params.fill = 1;
	params.buffer = 1;
	memcpy(&params.foreground, &editor->background, sizeof(color));
	windowComponentSetData(editor->canvas, &params, 1, 0 /* no redraw */);

	// Draw the image pixels

	params.height = editor->pixelSize;

	for (rowCount = 0; rowCount < editor->vertPixels; rowCount ++)
	{
		params.xCoord1 = 0;
		params.width = editor->pixelSize;

		for (columnCount = 0; columnCount < editor->horizPixels;
			columnCount ++)
		{
			current = (((editor->startVert + rowCount) * editor->img->width) +
				(editor->startHoriz + columnCount));

			if ((columnCount < (editor->horizPixels - 1)) &&
				!memcmp(&pixels[current], &pixels[current + 1], sizeof(pixel)))
			{
				params.width += editor->pixelSize;
			}
			else
			{
				memcpy(&params.foreground, &pixels[current], sizeof(pixel));
				windowComponentSetData(editor->canvas, &params, 1,
					0 /* no redraw */);
				params.xCoord1 += params.width;
				params.width = editor->pixelSize;
			}
		}

		params.yCoord1 += editor->pixelSize;
	}

	// Draw the grid
	drawGrid(editor);
}


static int resize(windowPixelEditor *editor)
{
	// Check params.
	if (!editor)
		return (errno = ERR_NULLPARAMETER);

	// Get the current canvas size
	editor->width = windowComponentGetWidth(editor->canvas);
	editor->height = windowComponentGetHeight(editor->canvas);

	editor->maxPixelSize = ((min(editor->width, editor->height) / 2) - 1);
	calcNumPixels(editor);
	calcDisplayPercentage(editor);
	draw(editor);

	return (0);
}


static int eventHandler(windowPixelEditor *editor, windowEvent *event)
{
	int pixelX = 0;
	int pixelY = 0;
	int width = 0;
	int drew = 0;
	static int clickX, clickY, xoring = 0;
	pixel *pixels = (pixel *) editor->img->data;
	windowDrawParameters params;

	// Check params.
	if (!editor || !event)
		return (errno = ERR_NULLPARAMETER);

	// Calculate which pixel this event is happening in
	pixelX = ((event->xPosition / editor->pixelSize) + editor->startHoriz);
	pixelY = ((event->yPosition / editor->pixelSize) + editor->startVert);

	if (editor->mode == pixedmode_draw)
	{
		if (editor->drawing.operation == draw_pixel)
		{
			if (event->type & (EVENT_MOUSE_LEFTDOWN | EVENT_MOUSE_DRAG))
			{
				graphicDrawPixel(&editor->buffer, &editor->drawing.foreground,
					editor->drawing.mode, pixelX, pixelY);
				drew = 1;
			}
		}
		else if (editor->drawing.operation == draw_line)
		{
			if (event->type == EVENT_MOUSE_LEFTDOWN)
			{
				editor->drawing.xCoord1 = pixelX;
				editor->drawing.yCoord1 = pixelY;

				clickX = event->xPosition;
				clickY = event->yPosition;
			}
			else if ((event->type == EVENT_MOUSE_DRAG) ||
				(event->type == EVENT_MOUSE_LEFTUP))
			{
				memset(&params, 0, sizeof(windowDrawParameters));
				params.operation = editor->drawing.operation;
				params.mode = draw_xor;
				params.xCoord1 = clickX;
				params.yCoord1 = clickY;
				params.thickness = 1;
				memcpy(&params.foreground, &editor->background, sizeof(color));

				if (xoring)
				{
					params.xCoord2 = editor->drawing.xCoord2;
					params.yCoord2 = editor->drawing.yCoord2;
					windowComponentSetData(editor->canvas, &params, 1,
						1 /* redraw */);
					xoring = 0;
				}

				if (event->type == EVENT_MOUSE_DRAG)
				{
					params.xCoord2 = event->xPosition;
					params.yCoord2 = event->yPosition;
					windowComponentSetData(editor->canvas, &params, 1,
						1 /* redraw */);

					editor->drawing.xCoord2 = event->xPosition;
					editor->drawing.yCoord2 = event->yPosition;
					xoring = 1;
				}
				else
				{
					graphicDrawLine(&editor->buffer,
						&editor->drawing.foreground, editor->drawing.mode,
						editor->drawing.xCoord1, editor->drawing.yCoord1,
						pixelX, pixelY);
					xoring = 0;
					drew = 1;
				}
			}
		}
		else if (editor->drawing.operation == draw_rect)
		{
			if (event->type == EVENT_MOUSE_LEFTDOWN)
			{
				editor->drawing.xCoord1 = pixelX;
				editor->drawing.yCoord1 = pixelY;

				clickX = event->xPosition;
				clickY = event->yPosition;
			}
			else if ((event->type == EVENT_MOUSE_DRAG) ||
				(event->type == EVENT_MOUSE_LEFTUP))
			{
				memset(&params, 0, sizeof(windowDrawParameters));
				params.operation = editor->drawing.operation;
				params.mode = draw_xor;
				params.thickness = 1;
				memcpy(&params.foreground, &editor->background, sizeof(color));

				if (xoring)
				{
					params.xCoord1 = min(clickX, editor->drawing.xCoord2);
					params.yCoord1 = min(clickY, editor->drawing.yCoord2);
					params.width = (abs(clickX - editor->drawing.xCoord2) + 1);
					params.height = (abs(clickY -
						editor->drawing.yCoord2) + 1);
					windowComponentSetData(editor->canvas, &params, 1,
						1 /* redraw */);
					xoring = 0;
				}

				if (event->type == EVENT_MOUSE_DRAG)
				{
					params.xCoord1 = min(clickX, event->xPosition);
					params.yCoord1 = min(clickY, event->yPosition);
					params.width = (abs(clickX - event->xPosition) + 1);
					params.height = (abs(clickY - event->yPosition) + 1);
					windowComponentSetData(editor->canvas, &params, 1,
						1 /* redraw */);

					editor->drawing.xCoord2 = event->xPosition;
					editor->drawing.yCoord2 = event->yPosition;
					xoring = 1;
				}
				else
				{
					graphicDrawRect(&editor->buffer,
						&editor->drawing.foreground, editor->drawing.mode,
						min(editor->drawing.xCoord1, pixelX),
						min(editor->drawing.yCoord1, pixelY),
						(abs(editor->drawing.xCoord1 - pixelX) + 1),
						(abs(editor->drawing.yCoord1 - pixelY) + 1),
						editor->drawing.thickness, editor->drawing.fill);
					xoring = 0;
					drew = 1;
				}
			}
		}
		else if (editor->drawing.operation == draw_oval)
		{
			if (event->type == EVENT_MOUSE_LEFTDOWN)
			{
				editor->drawing.xCoord1 = pixelX;
				editor->drawing.yCoord1 = pixelY;

				clickX = event->xPosition;
				clickY = event->yPosition;
			}
			else if ((event->type == EVENT_MOUSE_DRAG) ||
				(event->type == EVENT_MOUSE_LEFTUP))
			{
				memset(&params, 0, sizeof(windowDrawParameters));
				params.operation = editor->drawing.operation;
				params.mode = draw_xor;
				params.thickness = 1;
				memcpy(&params.foreground, &editor->background, sizeof(color));

				if (xoring)
				{
					// The framebuffer graphics driver currently only supports
					// circles
					width = (((abs(clickX - editor->drawing.xCoord2) + 1) +
						(abs(clickY - editor->drawing.yCoord2) + 1)) / 2);

					params.xCoord1 = min(clickX, editor->drawing.xCoord2);
					params.yCoord1 = min(clickY, editor->drawing.yCoord2);
					params.width = width;
					params.height = width;
					windowComponentSetData(editor->canvas, &params, 1,
						1 /* redraw */);
					xoring = 0;
				}

				if (event->type == EVENT_MOUSE_DRAG)
				{
					// The framebuffer graphics driver currently only supports
					// circles
					width = (((abs(clickX - event->xPosition) + 1) +
						(abs(clickY - event->yPosition) + 1)) / 2);

					params.xCoord1 = min(clickX, event->xPosition);
					params.yCoord1 = min(clickY, event->yPosition);
					params.width = width;
					params.height = width;
					windowComponentSetData(editor->canvas, &params, 1,
						1 /* redraw */);

					editor->drawing.xCoord2 = event->xPosition;
					editor->drawing.yCoord2 = event->yPosition;
					xoring = 1;
				}
				else
				{
					// The framebuffer graphics driver currently only supports
					// circles
					width = (((abs(editor->drawing.xCoord1 - pixelX) + 1) +
						(abs(editor->drawing.yCoord1 - pixelY) + 1)) / 2);

					graphicDrawOval(&editor->buffer,
						&editor->drawing.foreground, editor->drawing.mode,
						min(editor->drawing.xCoord1, pixelX),
						min(editor->drawing.yCoord1, pixelY),
						width, width, editor->drawing.thickness,
						editor->drawing.fill);
					xoring = 0;
					drew = 1;
				}
			}
		}

		if (drew)
		{
			if (editor->img->data)
				imageFree(editor->img);
			graphicGetImage(&editor->buffer, editor->img, 0 /* xCoord */,
				0 /* yCoord */, editor->buffer.width, editor->buffer.height);
			editor->changed += 1;
			draw(editor);
		}
	}

	else if (editor->mode == pixedmode_pick)
	{
		if (event->type & (EVENT_MOUSE_LEFTDOWN | EVENT_MOUSE_DRAG))
		{
			memcpy(&editor->drawing.foreground,
				&pixels[(pixelY * editor->img->width) + pixelX],
				sizeof(color));
		}
	}

	else if (editor->mode == pixedmode_select)
	{
	}

	return (0);
}


static int zoom(windowPixelEditor *editor, int value)
{
	int origHorizPixels = 0;
	int origVertPixels = 0;

	// Check params.
	if (!editor || !value)
		return (ERR_NULLPARAMETER);

	if (((editor->pixelSize + value) < editor->minPixelSize) ||
		((editor->pixelSize + value) > editor->maxPixelSize))
	{
		return (ERR_RANGE);
	}

	origHorizPixels = editor->horizPixels;
	origVertPixels = editor->vertPixels;

	editor->pixelSize += value;
	calcNumPixels(editor);

	// Try to ensure that some actual zoom effect happens.
	if ((editor->horizPixels == origHorizPixels) &&
		(editor->vertPixels == origVertPixels))
	{
		// Increase or decrease the pixel size by a little bit until the
		// numbers of pixels change.

		if (value > 0)
			value = 1;
		else
			value = -1;

		while ((editor->horizPixels == origHorizPixels) &&
			(editor->vertPixels == origVertPixels) &&
			((editor->pixelSize + value) >= editor->minPixelSize) &&
			((editor->pixelSize + value) <= editor->maxPixelSize))
		{
			editor->pixelSize += value;
			calcNumPixels(editor);
		}
	}

	calcDisplayPercentage(editor);
	draw(editor);

	return (0);
}


static int scrollHoriz(windowPixelEditor *editor, int percent)
{
	// Check params.
	if (!editor)
		return (ERR_NULLPARAMETER);

	percent = max(0, min(100, percent));

	editor->horiz.positionPercent = percent;

	draw(editor);

	return (0);
}


static int scrollVert(windowPixelEditor *editor, int percent)
{
	// Check params.
	if (!editor)
		return (ERR_NULLPARAMETER);

	percent = max(0, min(100, percent));

	editor->vert.positionPercent = percent;

	draw(editor);

	return (0);
}


static int destroy(windowPixelEditor *editor)
{
	// Detroy and deallocate the file list.

	// Check params.
	if (!editor)
		return (ERR_NULLPARAMETER);

	if (editor->buffer.data)
		free(editor->buffer.data);

	free(editor);

	return (0);
}


/////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////
//
// Below here, the functions are exported for external use
//
/////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////

_X_ windowPixelEditor *windowNewPixelEditor(objectKey parent, int width, int height, image *img, componentParameters *params)
{
	// Desc: Create a new pixel editor widget with the parent window 'parent', with the required width and height, a pointer to the image data 'img', and component parameters 'params'.

	int status = 0;
	windowPixelEditor *editor = NULL;

	if (!libwindow_initialized)
		libwindowInitialize();

	// Check params.
	if (!parent || !width || !height || !img || !params)
	{
		errno = ERR_NULLPARAMETER;
		return (editor = NULL);
	}

	// Allocate memory for our editor structure
	editor = malloc(sizeof(windowPixelEditor));
	if (!editor)
	{
		errno = ERR_MEMORY;
		return (editor = NULL);
	}

	// Create the editor's main canvas
	editor->canvas = windowNewCanvas(parent, width, height, params);
	if (!editor->canvas)
	{
		status = errno = ERR_NOCREATE;
		goto out;
	}

	editor->width = width;
	editor->height = height;
	editor->img = img;

	// Allocate a graphic buffer to draw in
	editor->buffer.width = img->width;
	editor->buffer.height = img->height;
	editor->buffer.data = malloc(graphicCalculateAreaBytes(img->width,
		img->height));
	if (!editor->buffer.data)
	{
		status = ERR_MEMORY;
		goto out;
	}

	// Draw the image into our buffer
	status = graphicDrawImage(&editor->buffer, img, draw_normal,
		0 /* xCoord */, 0 /* yCoord */, 0 /* xOffset */, 0 /* yOffset */,
		img->width, img->height);
	if (status < 0)
		goto out;

	editor->maxPixelSize = ((min(width, height) / 2) - 1);
	editor->minPixelSize = max(MIN_PIXELSIZE,
		min((width / img->width), (height / img->height)));
	editor->pixelSize = editor->minPixelSize;

	// Calculate initial numbers of pixels for each axis, starting with the
	// default minimum pixel size, and increase the pixel size until the
	// canvas is visually filled on both axes.
	calcNumPixels(editor);
	if (editor->horizPixels < editor->vertPixels)
	{
		while ((editor->horizPixels * (editor->pixelSize + 1)) <=
			(editor->width - 1))
		{
			editor->pixelSize += 1;
			calcNumPixels(editor);
		}
	}
	else
	{
		while ((editor->vertPixels * (editor->pixelSize + 1)) <=
			(editor->height - 1))
		{
			editor->pixelSize += 1;
			calcNumPixels(editor);
		}
	}

	calcDisplayPercentage(editor);

	// Was a foreground color specified?
	if (params->flags & WINDOW_COMPFLAG_CUSTOMFOREGROUND)
	{
		// Use the one we were given
		memcpy(&editor->foreground, &params->foreground, sizeof(color));
	}
	else
	{
		// Use our default
		editor->foreground = COLOR_BLACK;
	}

	// Was a background color specified?
	if (params->flags & WINDOW_COMPFLAG_CUSTOMBACKGROUND)
	{
		// Use the one we were given
		memcpy(&editor->background, &params->background, sizeof(color));
	}
	else
	{
		// Use our default
		editor->background = COLOR_WHITE;
	}

	// Set some defaults for drawing
	editor->mode = pixedmode_draw;
	editor->drawing.operation = draw_pixel;
	editor->drawing.mode = draw_normal;
	memcpy(&editor->drawing.foreground, &editor->foreground, sizeof(color));
	memcpy(&editor->drawing.background, &editor->background, sizeof(color));
	editor->drawing.thickness = 1;

	// Set our function pointers
	editor->resize = &resize;
	editor->eventHandler = &eventHandler;
	editor->zoom = &zoom;
	editor->scrollHoriz = &scrollHoriz;
	editor->scrollVert = &scrollVert;
	editor->destroy = &destroy;

	draw(editor);

	status = 0;

out:
	if (status < 0)
	{
		if (editor)
		{
			destroy(editor);
			editor = NULL;
		}
	}

	return (editor);
}

