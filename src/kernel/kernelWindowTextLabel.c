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
//  kernelWindowTextLabel.c
//

// This code is for managing kernelWindowTextLabel objects.
// These are just lines of text that occur inside windows


#include "kernelWindow.h"	// Our prototypes are here
#include "kernelError.h"
#include "kernelFont.h"
#include "kernelGraphic.h"
#include "kernelMalloc.h"
#include <string.h>

extern kernelWindowVariables *windowVariables;


static int setText(kernelWindowComponent *component, const char *text,
	int length)
{
	// Set the label text

	int status = 0;
	kernelWindowTextLabel *label = component->data;
	kernelFont *font = (kernelFont *) component->params.font;
	int width = 0;
	int count;

	// Set the text
	if (label->text)
	{
		kernelFree((void *) label->text);
		label->text = NULL;
	}

	label->text = kernelMalloc(length + 1);
	if (!label->text)
		return (status = ERR_NOCREATE);

	strncpy((char *) label->text, text, length);

	// How many lines?  We replace any newlines with NULLS and count them
	label->lines = 1;
	for (count = 0; count < length; count ++)
	{
		if (label->text[count] == '\n')
		{
			label->text[count] = '\0';
			label->lines += 1;
		}
	}

	// Set the width and height of the component based on the widest line and
	// the number of lines, respectively
	char *tmp  = label->text;
	for (count = 0; count < label->lines; count ++)
	{
		width = 0;
		if (font)
			width = kernelFontGetPrintedWidth(font,
				(char *) component->charSet, tmp);
		if (width > component->width)
			component->width = width;

		tmp += (strlen(tmp) + 1);
	}

	if (font)
		component->height = (font->glyphHeight * label->lines);
	component->minWidth = component->width;
	component->minHeight = component->height;

	return (status = 0);
}


static int draw(kernelWindowComponent *component)
{
	// Draw the label component

	int status = 0;
	kernelWindowTextLabel *label = component->data;
	kernelFont *font = (kernelFont *) component->params.font;
	int count;

	char *tmp = label->text;
	for (count = 0; count < label->lines; count ++)
	{
		if (font)
		{
			status = kernelGraphicDrawText(component->buffer,
				(color *) &component->params.foreground,
				(color *) &component->params.background, font,
				(char *) component->charSet, tmp, draw_normal,
				component->xCoord, (component->yCoord +
					(font->glyphHeight * count)));
			if (status < 0)
				break;
		}

		tmp += (strlen(tmp) + 1);
	}

	if (component->params.flags & WINDOW_COMPFLAG_HASBORDER)
		component->drawBorder(component, 1);

	return (status);
}


static int setData(kernelWindowComponent *component, void *text, int length)
{
	// Set the label text

	int status = 0;

	if (component->erase)
		component->erase(component);

	status = setText(component, text, length);
	if (status < 0)
		return (status);

	draw(component);

	component->window
		->update(component->window, component->xCoord, component->yCoord,
			component->width, component->height);

	return (status = 0);
}


static int destroy(kernelWindowComponent *component)
{
	kernelWindowTextLabel *label = component->data;

	// Release all our memory
	if (label)
	{
		if (label->text)
		{
			kernelFree((void *) label->text);
			label->text = NULL;
		}

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

kernelWindowComponent *kernelWindowNewTextLabel(objectKey parent,
	const char *text, componentParameters *params)
{
	// Formats a kernelWindowComponent as a kernelWindowTextLabel

	int status = 0;
	kernelWindowComponent *component = NULL;
	kernelWindowTextLabel *textLabel = NULL;

	// Check params
	if (!parent || !text || !params)
	{
		kernelError(kernel_error, "NULL parameter");
		return (component = NULL);
	}

	// Get the basic component structure
	component = kernelWindowComponentNew(parent, params);
	if (!component)
		return (component);

	component->type = textLabelComponentType;

	// Set the functions
	component->draw = &draw;
	component->setData = &setData;
	component->destroy = &destroy;

	// If font is NULL, use the default
	if (!component->params.font)
		component->params.font = windowVariables->font.varWidth.medium.font;

	// Get the label component
	textLabel = kernelMalloc(sizeof(kernelWindowTextLabel));
	if (!textLabel)
	{
		kernelWindowComponentDestroy(component);
		return (component = NULL);
	}

	component->data = (void *) textLabel;

	// Set the label data
	status = setText(component, text, strlen(text));
	if (status < 0)
	{
		kernelWindowComponentDestroy(component);
		return (component = NULL);
	}

	return (component);
}

