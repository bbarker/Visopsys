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
//  kernelWindowCheckbox.c
//

// This code is for managing kernelWindowCheckbox objects.

#include "kernelWindow.h"	// Our prototypes are here
#include "kernelError.h"
#include "kernelFont.h"
#include "kernelMalloc.h"
#include <string.h>
#include <stdlib.h>

extern kernelWindowVariables *windowVariables;


static void setSize(kernelWindowComponent *component)
{
	kernelWindowCheckbox *checkbox = component->data;

	// The width of the checkbox is the width of the checkbox, plus a bit
	// of padding, plus the printed width of the text
	component->width = (windowVariables->checkbox.size + 3);
	if (component->params.font)
		component->width +=
			kernelFontGetPrintedWidth((kernelFont *) component->params.font,
				(char *) component->charSet, checkbox->text);

	// The height of the checkbox is the height of the font, or the height
	// of the checkbox, whichever is greater
	component->height = windowVariables->checkbox.size;
	if (component->params.font)
		component->height = max(((kernelFont *) component->params.font)
			->glyphHeight, windowVariables->checkbox.size);

	component->minWidth = component->width;
	component->minHeight = component->height;
}


static int draw(kernelWindowComponent *component)
{
	// Draw the checkbox component

	int status = 0;
	kernelWindowCheckbox *checkbox = component->data;
	int checkboxSize = windowVariables->checkbox.size;
	int yCoord = 0;

	yCoord = (component->yCoord + ((component->height - checkboxSize) / 2));

	// Draw the white center of the check box
	kernelGraphicDrawRect(component->buffer, &COLOR_WHITE, draw_normal,
		component->xCoord, yCoord, checkboxSize, checkboxSize, 1, 1);

	// Draw a border around it
	kernelGraphicDrawGradientBorder(component->buffer, component->xCoord,
		yCoord, checkboxSize, checkboxSize,	windowVariables->border.thickness,
		(color *) &component->params.background,
		windowVariables->border.shadingIncrement, draw_reverse, border_all);

	if (checkbox->selected)
	{
		// Draw a cross in the box
		kernelGraphicDrawLine(component->buffer,
			&COLOR_BLACK, draw_normal,
			(component->xCoord + windowVariables->border.thickness + 1),
			(yCoord + windowVariables->border.thickness + 1),
			(component->xCoord +
				(checkboxSize - windowVariables->border.thickness - 1)),
			(yCoord + (checkboxSize - windowVariables->border.thickness - 1)));
		kernelGraphicDrawLine(component->buffer,
			&COLOR_BLACK, draw_normal,
			(component->xCoord + windowVariables->border.thickness + 1),
			(yCoord + (checkboxSize - windowVariables->border.thickness - 1)),
			(component->xCoord +
				(checkboxSize - windowVariables->border.thickness - 1)),
			(yCoord + windowVariables->border.thickness + 1));
	}

	if (component->params.font)
	{
		// Now draw the text next to the box
		kernelGraphicDrawText(component->buffer,
			(color *) &component->params.foreground,
			(color *) &component->params.background,
			(kernelFont *) component->params.font, (char *) component->charSet,
			checkbox->text, draw_normal,
			(component->xCoord + checkboxSize + 3), (component->yCoord));
	}

	if (component->params.flags & WINDOW_COMPFLAG_HASBORDER)
		component->drawBorder(component, 1);

	return (status = 0);
}


static int focus(kernelWindowComponent *component, int yesNo)
{
	component->drawBorder(component, yesNo);
	component->window->update(component->window, (component->xCoord - 2),
		(component->yCoord - 2), (component->width + 4),
		(component->height + 4));
	return (0);
}


static int setData(kernelWindowComponent *component, void *text, int length)
{
	// Set the checkbox text

	kernelWindowCheckbox *checkbox = component->data;

	if (checkbox->text)
		kernelFree(checkbox->text);

	checkbox->text = kernelMalloc(length + 1);
	if (!checkbox->text)
		return (ERR_MEMORY);

	strncpy(checkbox->text, text, length);

	// Set the new size
	setSize(component);

	// Re-draw
	if (component->draw)
		draw(component);

	component->window
		->update(component->window, component->xCoord, component->yCoord,
			component->width, component->height);

	return (0);
}


static int getSelected(kernelWindowComponent *component, int *selection)
{
	kernelWindowCheckbox *checkbox = component->data;
	*selection = checkbox->selected;
	return (0);
}


static int setSelected(kernelWindowComponent *component, int selected)
{
	kernelWindowCheckbox *checkbox = component->data;

	checkbox->selected = selected;

	// Re-draw
	if (component->draw)
		component->draw(component);

	component->window
		->update(component->window, component->xCoord, component->yCoord,
		 	component->width, component->height);

	return (0);
}


static int mouseEvent(kernelWindowComponent *component, windowEvent *event)
{
	// When mouse events happen to list components, we pass them on to the
	// appropriate kernelWindowListItem component

	int status = 0;
	kernelWindowCheckbox *checkbox = component->data;

	if (event->type == EVENT_MOUSE_LEFTDOWN)
	{
		if (checkbox->selected)
			setSelected(component, 0);
		else
			setSelected(component, 1);

		// Make this also a 'selection' event
		event->type |= EVENT_SELECTION;
	}

	return (status = 0);
}


static int keyEvent(kernelWindowComponent *component, windowEvent *event)
{
	// We allow the user to control the checkbox widget with 'space bar' key
	// presses, to select or deselect the item.

	int status = 0;

	// Translate this into a mouse event.
	if ((event->type & EVENT_MASK_KEY) && (event->key == keySpaceBar))
	{
		if (event->type == EVENT_KEY_DOWN)
			event->type = EVENT_MOUSE_LEFTDOWN;
		if (event->type == EVENT_KEY_UP)
			event->type = EVENT_MOUSE_LEFTUP;
		status = mouseEvent(component, event);
	}

	return (status);
}


static int destroy(kernelWindowComponent *component)
{
	kernelWindowCheckbox *checkbox = component->data;

	// Release all our memory
	if (checkbox)
	{
		if (checkbox->text)
		{
			kernelFree((void *) checkbox->text);
			checkbox->text = NULL;
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

kernelWindowComponent *kernelWindowNewCheckbox(objectKey parent,
	const char *text, componentParameters *params)
{
	// Formats a kernelWindowComponent as a kernelWindowCheckbox

	kernelWindowComponent *component = NULL;
	kernelWindowCheckbox *checkbox = NULL;

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

	component->type = checkboxComponentType;
	component->flags |= WINFLAG_CANFOCUS;

	// Set the functions
	component->draw = &draw;
	component->focus = &focus;
	component->setData = &setData;
	component->getSelected = &getSelected;
	component->setSelected = &setSelected;
	component->mouseEvent = &mouseEvent;
	component->keyEvent = &keyEvent;
	component->destroy = &destroy;

	// If font is NULL, use the default
	if (!component->params.font)
		component->params.font = windowVariables->font.varWidth.small.font;

	// Get the checkbox
	checkbox = kernelMalloc(sizeof(kernelWindowCheckbox));
	if (!checkbox)
	{
		kernelWindowComponentDestroy(component);
		return (component = NULL);
	}

	component->data = (void *) checkbox;

	// Try to get memory for the text
	checkbox->text = kernelMalloc(strlen(text) + 1);
	if (!checkbox->text)
	{
		kernelWindowComponentDestroy(component);
		return (component = NULL);
	}

	strcpy(checkbox->text, text);

	// Set the component size
	setSize(component);

	return (component);
}

