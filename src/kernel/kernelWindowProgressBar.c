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
//  kernelWindowProgressBar.c
//

// This code is for managing kernelWindowProgressBar objects.

#include "kernelWindow.h"	// Our prototypes are here
#include "kernelError.h"
#include "kernelFont.h"
#include "kernelGraphic.h"
#include "kernelMalloc.h"
#include <stdio.h>

extern kernelWindowVariables *windowVariables;


static int draw(kernelWindowComponent *component)
{
	// Draw the progress bar component

	kernelWindowProgressBar *progressBar = component->data;
	int thickness = windowVariables->border.thickness;
	int shadingIncrement = windowVariables->border.shadingIncrement;
	kernelFont *font = (kernelFont *) component->params.font;
	char prog[5];

	// Draw the background of the progress bar
	kernelGraphicDrawRect(component->buffer,
		(color *) &component->params.background, draw_normal,
		(component->xCoord + thickness), (component->yCoord + thickness),
		(component->width - (thickness * 2)),
		(component->height - (thickness * 2)), 1, 1);

	// Draw the border
	kernelGraphicDrawGradientBorder(component->buffer,
		component->xCoord, component->yCoord, component->width,
		component->height, thickness, (color *) &component->params.background,
		shadingIncrement, draw_reverse, border_all);

	// Draw the slider
	progressBar->sliderWidth = (((component->width - (thickness * 2)) *
		progressBar->progressPercent) / 100);
	if (progressBar->sliderWidth < (thickness * 2))
		progressBar->sliderWidth = (thickness * 2);

	kernelGraphicDrawGradientBorder(component->buffer,
		(component->xCoord + thickness), (component->yCoord + thickness),
		progressBar->sliderWidth, (component->height - (thickness * 2)),
		thickness, (color *) &component->params.background,
		shadingIncrement, draw_normal, border_all);

	if (font)
	{
		// Print the progress percent
		sprintf(prog, "%d%%", progressBar->progressPercent);
		kernelGraphicDrawText(component->buffer,
			(color *) &component->params.foreground,
			(color *) &component->params.background, font,
			(char *) component->charSet, prog, draw_translucent,
			(component->xCoord + ((component->width -
				 kernelFontGetPrintedWidth(font, (char *) component->charSet,
					prog)) / 2)),
			(component->yCoord +
				((component->height - font->glyphHeight) / 2)));
	}

	return (0);
}


static int setData(kernelWindowComponent *component, void *data, int length)
{
	// Set the progress percentage.  Our 'data' parameter is just an
	// integer value

	int status = 0;
	kernelWindowProgressBar *progressBar = component->data;

	// We ignore 'length'.  This keeps the compiler happy
	if (!length)
		return (status = ERR_NULLPARAMETER);

	if (component->erase)
		component->erase(component);

	progressBar->progressPercent = (int) data;

	if (progressBar->progressPercent < 0)
		progressBar->progressPercent = 0;
	if (progressBar->progressPercent > 100)
		progressBar->progressPercent = 100;

	if (component->draw)
		status = component->draw(component);

	component->window->update(component->window, component->xCoord,
		component->yCoord, component->width, component->height);

	return (0);
}


static int destroy(kernelWindowComponent *component)
{
	// Release all our memory
	if (component->data)
	{
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

kernelWindowComponent *kernelWindowNewProgressBar(objectKey parent,
	componentParameters *params)
{
	// Formats a kernelWindowComponent as a kernelWindowProgressBar

	kernelWindowComponent *component = NULL;
	kernelWindowProgressBar *progressBar = NULL;

	// Check params
	if (!parent || !params)
	{
		kernelError(kernel_error, "NULL parameter");
		return (component = NULL);
	}

	// Get the basic component structure
	component = kernelWindowComponentNew(parent, params);
	if (!component)
		return (component);

	// If font is NULL, use the default
	if (!component->params.font)
		component->params.font = windowVariables->font.varWidth.small.font;

	component->type = progressBarComponentType;

	// Set the functions
	component->draw = &draw;
	component->setData = &setData;
	component->destroy = &destroy;

	component->width = 200;
	component->height = 25;
	component->minWidth = component->width;
	component->minHeight = component->height;

	progressBar = kernelMalloc(sizeof(kernelWindowProgressBar));
	if (!progressBar)
	{
		kernelWindowComponentDestroy(component);
		return (component = NULL);
	}

	component->data = (void *) progressBar;

	progressBar->progressPercent = 0;

	return (component);
}

