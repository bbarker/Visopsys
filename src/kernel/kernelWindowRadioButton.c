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
//  kernelWindowRadioButton.c
//

// This code is for managing kernelWindowRadioButton objects.

#include "kernelWindow.h"	// Our prototypes are here
#include "kernelError.h"
#include "kernelFont.h"
#include "kernelGraphic.h"
#include "kernelMalloc.h"
#include <string.h>

extern kernelWindowVariables *windowVariables;


static int draw(kernelWindowComponent *component)
{
	// Draw the radio button component

	int status = 0;
	kernelWindowRadioButton *radio = component->data;
	kernelFont *font = (kernelFont *) component->params.font;
	int xCoord = 0, yCoord = 0;
	color tmpColor;
	int count1, count2;

	char *tmp = radio->text;
	for (count1 = 0; count1 < radio->numItems; count1 ++)
	{
		xCoord = component->xCoord;
		yCoord = component->yCoord;
		if (font)
			yCoord += (font->glyphHeight * count1);

		memcpy(&tmpColor, (color *) &component->window->background,
			sizeof(color));
		for (count2 = 0; count2 < 3; count2 ++)
		{
			tmpColor.red -= windowVariables->border.shadingIncrement;
			tmpColor.green -= windowVariables->border.shadingIncrement;
			tmpColor.blue -= windowVariables->border.shadingIncrement;
			kernelGraphicDrawOval(component->buffer, &tmpColor, draw_normal,
				(xCoord + count2), (yCoord + count2),
				(windowVariables->radioButton.size - (count2 * 2)),
				(windowVariables->radioButton.size - (count2 * 2)), 1, 0);
		}

		kernelGraphicDrawOval(component->buffer, &COLOR_WHITE, draw_normal,
			(xCoord + 3), (yCoord + 3),
			(windowVariables->radioButton.size - 6),
			(windowVariables->radioButton.size - 6), 1, 1);

		if (radio->selectedItem == count1)
		{
			kernelGraphicDrawOval(component->buffer,
				(color *) &component->params.foreground, draw_normal,
				(xCoord + 3), (yCoord + 3),
				(windowVariables->radioButton.size - 6),
				(windowVariables->radioButton.size - 6), 1, 1);
		}

		if (font)
		{
			status = kernelGraphicDrawText(component->buffer,
				(color *) &component->params.foreground,
				(color *) &component->window->background, font,
				(char *) component->charSet, tmp, draw_normal,
				(component->xCoord + windowVariables->radioButton.size + 2),
				(component->yCoord + (font->glyphHeight * count1)));
			if (status < 0)
				break;
		}

		tmp += (strlen(tmp) + 1);
	}

	if (component->params.flags & WINDOW_COMPFLAG_HASBORDER)
		component->drawBorder(component, 1);

	return (status);
}


static int focus(kernelWindowComponent *component, int yesNo)
{
	if (component->drawBorder)
		component->drawBorder(component, yesNo);

	component->window->update(component->window, (component->xCoord - 2),
		(component->yCoord - 2), (component->width + 4),
		(component->height + 4));

	return (0);
}


static int setData(kernelWindowComponent *component, void *data, int numItems)
{
	// Set the radio button text labels

	kernelWindowRadioButton *radio = component->data;
	const char **items = data;
	int textMemorySize = 0;
	char *tmp = NULL;
	int count;

	// If no items, nothing to do
	if (!numItems)
		return (ERR_NODATA);

	// Calculate how much memory we need for our text data
	for (count = 0; count < numItems; count ++)
		textMemorySize += (strlen(items[count]) + 1);

	// Free any old memory
	if (radio->text)
		kernelFree(radio->text);
	radio->numItems = 0;

	// Try to get memory
	radio->text = kernelMalloc(textMemorySize);
	if (!radio->text)
		return (ERR_MEMORY);

	// Loop through the strings (items) and add them to our text memory
	tmp = radio->text;
	for (count = 0; count < numItems; count ++)
	{
		strcpy(tmp, items[count]);
		tmp += (strlen(items[count]) + 1);

		if (component->params.font &&
			((kernelFontGetPrintedWidth((kernelFont *) component->params.font,
				(char *) component->charSet, items[count]) +
				windowVariables->radioButton.size + 3) > component->width))
		{
			component->width = (kernelFontGetPrintedWidth((kernelFont *)
				component->params.font, (char *) component->charSet,
				items[count]) + windowVariables->radioButton.size + 3);
		}

		radio->numItems += 1;
	}

	// The height of the radio button is the height of the font times the number
	// of items.
	if (component->params.font)
		component->height = (numItems *
			((kernelFont *) component->params.font)->glyphHeight);

	component->minWidth = component->width;
	component->minHeight = component->height;

	return (0);
}


static int getSelected(kernelWindowComponent *component, int *selection)
{
	kernelWindowRadioButton *radio = component->data;
	*selection = radio->selectedItem;
	return (0);
}


static int setSelected(kernelWindowComponent *component, int selected)
{
	int status = 0;
	kernelWindowRadioButton *radio = component->data;

	// Check params
	if ((selected < 0) || (selected >= radio->numItems))
	{
		kernelError(kernel_error, "Illegal component number %d", selected);
		return (status = ERR_BOUNDS);
	}

	radio->selectedItem = selected;

	// Re-draw
	draw(component);
	component->window
		->update(component->window, component->xCoord, component->yCoord,
			component->width, component->height);

	return (status = 0);
}


static int mouseEvent(kernelWindowComponent *component, windowEvent *event)
{
	// When mouse events happen to list components, we pass them on to the
	// appropriate kernelWindowListItem component

	kernelWindowRadioButton *radio = component->data;
	int clickedItem = 0;

	if (radio->numItems && (event->type == EVENT_MOUSE_LEFTDOWN))
	{
		// Figure out which item was clicked based on the coordinates of the
		// event
		clickedItem =
			(event->yPosition - (component->window->yCoord + component->yCoord));
		if (component->params.font)
			clickedItem /=
				((kernelFont *) component->params.font)->glyphHeight;

		// Is this item different from the currently selected item?
		if (clickedItem != radio->selectedItem)
		{
			radio->selectedItem = clickedItem;

			if (component->draw)
				component->draw(component);

			component->window->update(component->window, component->xCoord,
				component->yCoord, component->width, component->height);

			// Make this also a 'selection' event
			event->type |= EVENT_SELECTION;
		}
	}

	return (0);
}


static int keyEvent(kernelWindowComponent *component, windowEvent *event)
{
	// We allow the user to control the list widget with key presses, such
	// as cursor movements.  The radio button accepts cursor up and cursor
	// down movements.

	kernelWindowRadioButton *radio = component->data;

	if ((event->type == EVENT_KEY_DOWN) &&
		((event->key == keyUpArrow) || (event->key == keyDownArrow)))
	{
		if (event->key == keyUpArrow)
		{
			// UP cursor
			if (radio->selectedItem > 0)
				radio->selectedItem -= 1;
		}
		else
		{
			// DOWN cursor
			if (radio->selectedItem < (radio->numItems - 1))
				radio->selectedItem += 1;
		}

		if (component->draw)
			component->draw(component);

		component->window
			->update(component->window, component->xCoord, component->yCoord,
				component->width, component->height);

		// Make this also a 'selection' event
		event->type |= EVENT_SELECTION;
	}

	return (0);
}


static int destroy(kernelWindowComponent *component)
{
	kernelWindowRadioButton *radio = component->data;

	// Release all our memory
	if (radio)
	{
		if (radio->text)
		{
			kernelFree((void *) radio->text);
			radio->text = NULL;
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

kernelWindowComponent *kernelWindowNewRadioButton(objectKey parent,
	int rows, int columns, const char **items, int numItems,
	componentParameters *params)
{
	// Formats a kernelWindowComponent as a kernelWindowRadioButton

	kernelWindowComponent *component = NULL;
	kernelWindowRadioButton *radioButton = NULL;

	// Check parameters.
	if (!parent || !items || !params)
	{
		kernelError(kernel_error, "NULL parameter");
		return (component = NULL);
	}

	// We ignore 'rows' and 'columns' for now.  This keeps the compiler happy.
	if (!rows || !columns)
		return (component = NULL);

	// Get the basic component structure
	component = kernelWindowComponentNew(parent, params);
	if (!component)
		return (component);

	component->type = radioButtonComponentType;
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

	// Get the radio button
	radioButton = kernelMalloc(sizeof(kernelWindowRadioButton));
	if (!radioButton)
	{
		kernelWindowComponentDestroy(component);
		return (component = NULL);
	}

	component->data = (void *) radioButton;

	radioButton->selectedItem = 0;

	// Set the data
	if (setData(component, items, numItems) < 0)
	{
		kernelWindowComponentDestroy(component);
		return (component = NULL);
	}

	return (component);
}

