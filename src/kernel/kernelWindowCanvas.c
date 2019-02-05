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
//  kernelWindowCanvas.c
//

// This code is for managing kernelWindowCanvas objects.
// These are just kernelWindowImage components that can be drawn upon.

#include "kernelWindow.h"	// Our prototypes are here
#include "kernelDebug.h"
#include "kernelGraphic.h"
#include "kernelImage.h"
#include "kernelMalloc.h"


static void drawFocus(kernelWindowComponent *component, int focus)
{
	color *drawColor = NULL;

	if (focus)
		drawColor = (color *) &component->params.foreground;
	else
		drawColor = (color *) &component->window->background;

	kernelGraphicDrawRect(component->buffer, drawColor, draw_normal,
		(component->xCoord - 1), (component->yCoord - 1),
		(component->width + 2),	(component->height + 2), 1, 0);

	return;
}


static int draw(kernelWindowComponent *component)
{
	// First draw the underlying image component, and then if it has the focus,
	// draw another border

	int status = 0;
	kernelWindowCanvas *canvas = component->data;

	kernelDebug(debug_gui, "WindowCanvas draw");

	kernelGraphicCopyBuffer(&canvas->buffer, component->buffer,
		component->xCoord, component->yCoord);

	if (component->flags & WINFLAG_HASFOCUS)
		drawFocus(component, 1);

	return (status = 0);
}


static int resize(kernelWindowComponent *component, int width, int height)
{
	int status = 0;
	kernelWindowCanvas *canvas = component->data;
	image tmpImage;

	kernelDebug(debug_gui, "WindowCanvas resize from %d,%d to %d,%d",
		component->width, component->height, width, height);

	// Get an image from the canvas buffer
	status = kernelGraphicGetImage(&canvas->buffer, &tmpImage, 0, 0,
		component->width, component->height);
	if (status < 0)
		return (status);

	// Re-allocate the canvas buffer
	canvas->buffer.width = width;
	canvas->buffer.height = height;
	canvas->buffer.data = kernelRealloc(canvas->buffer.data,
		kernelGraphicCalculateAreaBytes(width, height));
	if (!canvas->buffer.data)
	{
		status = ERR_MEMORY;
		goto out;
	}

	// Resize the canvas image
	status = kernelImageResize(&tmpImage, width, height);
	if (status < 0)
		goto out;

	// Draw the resized image in our new buffer
	status = kernelGraphicDrawImage(&canvas->buffer, &tmpImage, draw_normal,
		0, 0, 0, 0, width, height);

out:
	kernelImageFree(&tmpImage);
	return (status = 0);
}


static int focus(kernelWindowComponent *component, int yesNo)
{
	kernelDebug(debug_gui, "WindowCanvas focus");

	drawFocus(component, yesNo);

	component->window->update(component->window, (component->xCoord - 1),
		(component->yCoord - 1), (component->width + 2),
		(component->height + 2));

	return (0);
}


static int setData(kernelWindowComponent *component, void *data, int size
	__attribute__((unused)))
{
	// This is where we implement drawing on the canvas.  Our parameter
	// is a structure that specifies the drawing operation and parameters

	int status = 0;
	kernelWindowCanvas *canvas = component->data;
	windowDrawParameters *params = data;

	kernelDebug(debug_gui, "WindowCanvas set data");

	switch (params->operation)
	{
		case draw_pixel:
			status = kernelGraphicDrawPixel(&canvas->buffer,
				&params->foreground, params->mode, params->xCoord1,
				params->yCoord1);
			break;

		case draw_line:
			status = kernelGraphicDrawLine(&canvas->buffer,
				&params->foreground, params->mode, params->xCoord1,
				params->yCoord1, params->xCoord2, params->yCoord2);
			break;

		case draw_rect:
			status = kernelGraphicDrawRect(&canvas->buffer,
				&params->foreground, params->mode, params->xCoord1,
				params->yCoord1, params->width, params->height,
				params->thickness, params->fill);
			break;

		case draw_oval:
			status = kernelGraphicDrawOval(&canvas->buffer,
				&params->foreground, params->mode, params->xCoord1,
				params->yCoord1, params->width, params->height,
				params->thickness, params->fill);
			break;

		case draw_image:
			status = kernelGraphicDrawImage(&canvas->buffer,
				(image *) params->data, params->mode, params->xCoord1,
				params->yCoord1, params->xCoord2, params->yCoord2,
				params->width, params->height);
			break;

		case draw_text:
			if (params->font)
			{
				// Try to make sure we have the required character set.
				if (!kernelFontHasCharSet((kernelFont *) params->font,
					(char *) component->charSet))
				{
					kernelFontGet(((kernelFont *) params->font)->family,
						((kernelFont *) params->font)->flags,
						((kernelFont *) params->font)->points,
						(char *) component->charSet);
				}

				status = kernelGraphicDrawText(&canvas->buffer,
					&params->foreground, &params->background,
					(kernelFont *) params->font, (char *) component->charSet,
					params->data, params->mode, params->xCoord1,
					params->yCoord1);
			}
			break;

		default:
			break;
	}

	if (!params->buffer)
		component->window->update(component->window, component->xCoord,
			component->yCoord, component->width, component->height);

	return (status);
}


static int destroy(kernelWindowComponent *component)
{
	kernelWindowCanvas *canvas = component->data;

	kernelDebug(debug_gui, "WindowCanvas detroy");

	// Release all our memory
	if (canvas)
	{
		if (canvas->buffer.data)
			kernelFree(canvas->buffer.data);

		kernelFree(component->data);
		component->data = NULL;
	}

	return (0);
}


/////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////
//
// Below here, the functions are exported for external use
//
/////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////

kernelWindowComponent *kernelWindowNewCanvas(objectKey parent, int width,
	int height, componentParameters *params)
{
	// Formats a kernelWindowComponent as a kernelWindowCanvas.  A
	// kernelWindowCanvas is a type of kernelWindowImage, but we allow
	// drawing operations on it.

	kernelWindowComponent *component = NULL;
	kernelWindowCanvas *canvas = NULL;

	// Check params
	if (!parent || !width || !height || !params)
		return (component = NULL);

	// Get the basic component structure
	component = kernelWindowComponentNew(parent, params);
	if (!component)
		return (component);

	// Now populate it
	component->type = canvasComponentType;
	component->width = width;
	component->height = height;
	component->minWidth = component->width;
	component->minHeight = component->height;
	component->flags |= WINFLAG_RESIZABLE;

	// Set the functions
	component->draw = &draw;
	component->resize = &resize;
	component->focus = &focus;
	component->setData = &setData;
	component->destroy = &destroy;

	// Get the kernelWindowCanvas memory
	canvas = kernelMalloc(sizeof(kernelWindowCanvas));
	if (!canvas)
	{
		kernelWindowComponentDestroy(component);
		return (component = NULL);
	}

	// Get a graphic buffer
	canvas->buffer.width = width;
	canvas->buffer.height = height;
	canvas->buffer.data = kernelMalloc(kernelGraphicCalculateAreaBytes(width,
		height));
	if (!canvas->buffer.data)
	{
		kernelWindowComponentDestroy(component);
		return (component = NULL);
	}

	component->data = (void *) canvas;

	// If a custom background was specified, fill it with that color
	if (params->flags & WINDOW_COMPFLAG_CUSTOMBACKGROUND)
	{
		kernelGraphicDrawRect(&canvas->buffer, &params->background,
			draw_normal, 0, 0, width, height, 1 /* thickness */,
			1 /* fill */);
	}

	return (component);
}

