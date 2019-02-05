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
//  kernelWindowTextField.c
//

// This code is for managing kernelWindowTextField components.
// These are just kernelWindowTextArea that consist of a single line, but
// they have slightly different behaviour; for example, they don't scroll
// vertically, but have to be able to scroll horizontally.

#include "kernelWindow.h"	// Our prototypes are here
#include "kernelMalloc.h"
#include <stdlib.h>
#include <string.h>

static int (*saveFocus)(kernelWindowComponent *, int) = NULL;
static int (*saveSetData)(kernelWindowComponent *, void *, int) = NULL;
static int (*saveDestroy)(kernelWindowComponent *component) = NULL;


static int focus(kernelWindowComponent *component, int yesNo)
{
	// This gets called when a component gets or loses the focus

	int status = 0;
	kernelWindowTextArea *textArea = component->data;
	kernelTextArea *area = textArea->area;

	if (saveFocus)
	{
		// Call the 'focus' function of the underlying text area
		status = saveFocus(component, yesNo);
		if (status < 0)
			return (status);
	}

	kernelTextStreamSetCursor(area->outputStream, yesNo);

	return (status = 0);
}


static int getData(kernelWindowComponent *component, void *buffer, int size)
{
	// Copy the text (up to size bytes) from our private buffer to the
	// supplied buffer.
	kernelWindowTextArea *textArea = component->data;

	size = min(size, (MAXSTRINGLENGTH - 1));

	memcpy(buffer, textArea->fieldBuffer, size);

	return (0);
}


static int showScrolled(kernelWindowComponent *component)
{
	kernelWindowTextArea *textArea = component->data;
	kernelTextArea *area = textArea->area;
	int bufferChars = strlen(textArea->fieldBuffer);
	char *bufferPtr = NULL;

	// Do we need to do any horizontal scrolling?
	if (bufferChars >= (area->columns - 1))
	{
		bufferPtr = (textArea->fieldBuffer + (bufferChars -
			area->columns) + 1);
	}
	else
	{
		bufferPtr = textArea->fieldBuffer;
	}

	if (saveSetData)
		return (saveSetData(component, bufferPtr, strlen(bufferPtr)));
	else
		return (0);
}


static int setData(kernelWindowComponent *component, void *buffer, int size)
{
	// Copy the text (up to size bytes) from the supplied buffer to the
	// text area.
	kernelWindowTextArea *textArea = component->data;

	size = min(size, (MAXSTRINGLENGTH - 1));

	memcpy(textArea->fieldBuffer, buffer, size);
	textArea->fieldBuffer[size] = '\0';

	return (showScrolled(component));
}


static int keyEvent(kernelWindowComponent *component, windowEvent *event)
{
	kernelWindowTextArea *textArea = component->data;
	kernelTextArea *area = textArea->area;
	int bufferChars = strlen(textArea->fieldBuffer);

	if (event->type == EVENT_KEY_DOWN)
	{
		if (event->key == keyBackSpace)
		{
			if (bufferChars <= 0)
				return (0);

			textArea->fieldBuffer[--bufferChars] = NULL;
			kernelTextStreamBackSpace(area->outputStream);

			// Do we need to do any horizontal scrolling?
			if (bufferChars >= (area->columns - 1))
				showScrolled(component);
		}

		else if (event->ascii >= ASCII_SPACE)
		{
			if (bufferChars >= (MAXSTRINGLENGTH - 1))
				return (0);

			textArea->fieldBuffer[bufferChars++] = event->ascii;
			textArea->fieldBuffer[bufferChars++] = '\0';
			kernelTextStreamPutc(area->outputStream, event->ascii);

			// Do we need to do any horizontal scrolling?
			if (bufferChars >= (area->columns - 1))
				showScrolled(component);
		}
	}

	return (0);
}


static int destroy(kernelWindowComponent *component)
{
	kernelWindowTextArea *textArea = component->data;

	if (textArea)
	{
		if (textArea->fieldBuffer)
		{
			kernelFree(textArea->fieldBuffer);
			textArea->fieldBuffer = NULL;
		}
	}

	if (saveDestroy)
		return (saveDestroy(component));
	else
		return (0);
}


/////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////
//
// Below here, the functions are exported for external use
//
/////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////

kernelWindowComponent *kernelWindowNewTextField(objectKey parent, int columns,
	componentParameters *params)
{
	// Just returns a kernelWindowTextArea with only one row, but there are
	// a couple of other things we do as well.

	kernelWindowComponent *component = NULL;
	kernelWindowTextArea *textArea = NULL;
	kernelTextArea *area = NULL;
	componentParameters newParams;

	memcpy(&newParams, params, sizeof(componentParameters));
	params = &newParams;

	component = kernelWindowNewTextArea(parent, columns, 1, 0, params);
	if (!component)
		return (component);

	textArea = component->data;
	area = textArea->area;

	// Allocate our private buffer for the line contents
	textArea->fieldBuffer = kernelMalloc(MAXSTRINGLENGTH);
	if (!textArea->fieldBuffer)
	{
		if (component->destroy)
			component->destroy(component);
		return (component = NULL);
	}

	// Only X-resizable
	component->flags &= ~WINFLAG_RESIZABLEY;

	// Turn off the cursor until we get the focus
	area->cursorState = 0;

	// Turn echo off
	area->inputStream->attrs.echo = 0;

	// We want different focus behaviour than a text area
	if (!saveFocus)
		saveFocus = component->focus;
	component->focus = focus;

	// Override the setData function (we'll save the data in our private
	// buffer)
	if (!saveSetData)
		saveSetData = component->setData;
	component->setData = setData;

	// Override the getData function (we'll return the data from our private
	// buffer)
	component->getData = getData;

	// Override the key event handler (we output directly into the text area)
	component->keyEvent = keyEvent;

	// Override the destructor
	if (!saveDestroy)
		saveDestroy = component->destroy;
	component->destroy = destroy;

	return (component);
}

