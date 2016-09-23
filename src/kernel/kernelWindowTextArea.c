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
//  kernelWindowTextArea.c
//

// This code is for managing kernelWindowTextArea objects.
// These are just textareas that appear inside windows and buttons, etc

#include "kernelWindow.h"	// Our prototypes are here
#include "kernelError.h"
#include "kernelFont.h"
#include "kernelGraphic.h"
#include "kernelMalloc.h"
#include "kernelMultitasker.h"
#include "kernelWindowEventStream.h"
#include <stdlib.h>
#include <string.h>

extern kernelWindowVariables *windowVariables;


static inline int isMouseInScrollBar(windowEvent *event,
	kernelWindowComponent *component)
{
	// We use this to determine whether a mouse event is inside the scroll bar

	kernelWindowScrollBar *scrollBar = component->data;

	if (scrollBar->dragging ||
		(event->xPosition >= (component->window->xCoord + component->xCoord)))
	{
		return (1);
	}
	else
	{
		return (0);
	}
}


static inline void updateScrollBar(kernelWindowTextArea *textArea)
{
	scrollBarState state;

	if (textArea->scrollBar->setData)
	{
		state.displayPercent = 100;
		if (textArea->area->rows + textArea->area->scrollBackLines)
			state.displayPercent =
				((textArea->area->rows * 100) /
					(textArea->area->rows + textArea->area->scrollBackLines));
		state.positionPercent = 100;
		if (textArea->area->scrollBackLines)
			state.positionPercent -=
				((textArea->area->scrolledBackLines * 100) /
					textArea->area->scrollBackLines);
		textArea->scrollBar->setData(textArea->scrollBar, &state,
			sizeof(scrollBarState));
	}
}


static int numComps(kernelWindowComponent *component)
{
	kernelWindowTextArea *textArea = component->data;

	if (textArea->scrollBar)
		// Return 1 for our scrollbar,
		return (1);
	else
		return (0);
}


static int flatten(kernelWindowComponent *component,
	kernelWindowComponent **array, int *numItems, unsigned flags)
{
	kernelWindowTextArea *textArea = component->data;

	if (textArea->scrollBar && ((textArea->scrollBar->flags & flags) == flags))
	{
		// Add our scrollbar
		array[*numItems] = textArea->scrollBar;
		*numItems += 1;
	}

	return (0);
}


static int setBuffer(kernelWindowComponent *component, graphicBuffer *buffer)
{
	// Set the graphics buffer for the component's subcomponents.

	int status = 0;
	kernelWindowTextArea *textArea = component->data;

	if (textArea->scrollBar && textArea->scrollBar->setBuffer)
	{
		// Do our scrollbar
		status = textArea->scrollBar->setBuffer(textArea->scrollBar, buffer);
		textArea->scrollBar->buffer = buffer;
	}

	return (status);
}


static int draw(kernelWindowComponent *component)
{
	// Draw the textArea component

	kernelWindowTextArea *textArea = component->data;
	kernelTextArea *area = textArea->area;

	// Draw a gradient border
	kernelGraphicDrawGradientBorder(component->buffer,
		component->xCoord, component->yCoord, component->width,
		component->height, windowVariables->border.thickness,
		(color *) &component->window->background,
		windowVariables->border.shadingIncrement, draw_reverse, border_all);

	// Tell the text area to draw itself
	area->outputStream->outputDriver->screenDraw(area);

	// If there's a scroll bar, draw it too
	if (textArea->scrollBar && textArea->scrollBar->draw)
		textArea->scrollBar->draw(textArea->scrollBar);

	if (component->params.flags & WINDOW_COMPFLAG_HASBORDER)
		component->drawBorder(component, 1);

	return (0);
}


static int update(kernelWindowComponent *component)
{
	// This gets called when the text area has done something, and we use
	// it to update the scroll bar.

	kernelWindowTextArea *textArea = component->data;

	if (textArea->scrollBar)
		updateScrollBar(textArea);

	return (0);
}


static int move(kernelWindowComponent *component, int xCoord, int yCoord)
{
	kernelWindowTextArea *textArea = component->data;
	kernelTextArea *area = textArea->area;
	int scrollBarX = 0;

	area->xCoord = (xCoord + windowVariables->border.thickness);
	area->yCoord = (yCoord + windowVariables->border.thickness);

	// If we have a scroll bar, move it too
	if (textArea->scrollBar)
	{
		scrollBarX = (xCoord + textArea->areaWidth +
			(windowVariables->border.thickness * 2));

		if (textArea->scrollBar->move)
			textArea->scrollBar->move(textArea->scrollBar, scrollBarX, yCoord);

		textArea->scrollBar->xCoord = scrollBarX;
		textArea->scrollBar->yCoord = yCoord;
	}

	return (0);
}


static int resize(kernelWindowComponent *component, int width, int height)
{
	int status = 0;
	kernelWindowTextArea *textArea = component->data;
	kernelTextArea *area = textArea->area;
	int newColumns = 0, newRows = 0;
	int scrollBarX = 0;

	textArea->areaWidth = (width - (windowVariables->border.thickness * 2));
	if (textArea->scrollBar)
		textArea->areaWidth -= textArea->scrollBar->width;

	if (area->font)
	{
		// Calculate the new columns and rows.
		newColumns = (textArea->areaWidth / area->font->glyphWidth);
		newRows = (height / area->font->glyphHeight);
	}

	if ((newColumns != area->columns) || (newRows != area->rows))
	{
		status = kernelTextAreaResize(area, newColumns, newRows);
		if (status < 0)
			return (status);
	}

	// If we have a scroll bar, move/resize it too
	if (textArea->scrollBar)
	{
		if (width != component->width)
		{
			scrollBarX = (component->xCoord + textArea->areaWidth +
				(windowVariables->border.thickness * 2));

			if (textArea->scrollBar->move)
				textArea->scrollBar->move(textArea->scrollBar, scrollBarX,
					component->yCoord);

			textArea->scrollBar->xCoord = scrollBarX;
		}

		if (height != component->height)
		{
			if (textArea->scrollBar->resize)
				textArea->scrollBar->resize(textArea->scrollBar,
					textArea->scrollBar->width, height);

			textArea->scrollBar->height = height;
		}
	}

	return (status = 0);
}


static int focus(kernelWindowComponent *component, int yesNo)
{
	kernelWindowTextArea *textArea = component->data;
	kernelTextArea *area = textArea->area;

	if (yesNo)
	{
		kernelTextSetCurrentInput(area->inputStream);
		kernelTextSetCurrentOutput(area->outputStream);
	}

	return (0);
}


static int getData(kernelWindowComponent *component, void *buffer, int size)
{
	// Copy the text (up to size bytes) from the text area to the supplied
	// buffer.
	kernelWindowTextArea *textArea = component->data;
	kernelTextArea *area = textArea->area;

	if (size > (area->columns * area->rows))
		size = (area->columns * area->rows);

	memcpy(buffer, area->visibleData, size);

	return (0);
}


static int setData(kernelWindowComponent *component, void *buffer, int size)
{
	// Copy the text (up to size bytes) from the supplied buffer to the
	// text area.
	kernelWindowTextArea *textArea = component->data;
	kernelTextArea *area = textArea->area;

	kernelTextStreamScreenClear(area->outputStream);

	if (size)
		kernelTextStreamPrint(area->outputStream, buffer);

	if (component->draw)
		component->draw(component);

	return (0);
}


static int mouseEvent(kernelWindowComponent *component, windowEvent *event)
{
	int status = 0;
	kernelWindowTextArea *textArea = component->data;
	kernelWindowScrollBar *scrollBar = NULL;
	int scrolledBackLines = 0;
	windowEvent cursorEvent;
	int cursorColumn = 0, cursorRow = 0;

	// Is the event in one of our scroll bars?
	if (textArea->scrollBar && isMouseInScrollBar(event, textArea->scrollBar))
	{
		scrollBar = textArea->scrollBar->data;

		// First, pass on the event to the scroll bar
		if (textArea->scrollBar->mouseEvent)
			textArea->scrollBar->mouseEvent(textArea->scrollBar, event);

		scrolledBackLines = (((100 - scrollBar->state.positionPercent) *
			textArea->area->scrollBackLines) / 100);

		if (scrolledBackLines != textArea->area->scrolledBackLines)
		{
			// Adjust the scrollback values of the text area based on the
			// positioning of the scroll bar.
			textArea->area->scrolledBackLines = scrolledBackLines;
			component->draw(component);
		}
	}
	else if ((event->type == EVENT_MOUSE_LEFTDOWN) &&
		(component->params.flags & WINDOW_COMPFLAG_CLICKABLECURSOR))
	{
		// The event was a click in the text area.  Move the cursor to the
		// clicked location.

		if (textArea->area->font)
			cursorColumn = ((event->xPosition - (component->window->xCoord +
				textArea->area->xCoord)) / textArea->area->font->glyphWidth);
		cursorColumn = min(cursorColumn, textArea->area->columns);

		if (textArea->area->font)
			cursorRow = ((event->yPosition - (component->window->yCoord +
				textArea->area->yCoord)) / textArea->area->font->glyphHeight);
		cursorRow = min(cursorRow, textArea->area->rows);

		if (textArea->area && textArea->area->outputStream &&
			textArea->area->font)
		{
			kernelTextStreamSetColumn(textArea->area->outputStream,
				cursorColumn);
			kernelTextStreamSetRow(textArea->area->outputStream, cursorRow);

			// Write a 'cursor moved' event to the component event stream
			memset(&cursorEvent, 0, sizeof(windowEvent));
			cursorEvent.type = EVENT_CURSOR_MOVE;
			kernelWindowEventStreamWrite(&component->events, &cursorEvent);
		}
	}

	return (status);
}


static int keyEvent(kernelWindowComponent *component, windowEvent *event)
{
	// Puts window key events into the input stream of the text area

	kernelWindowTextArea *textArea = component->data;
	kernelTextInputStream *inputStream = textArea->area->inputStream;

	if ((event->type == EVENT_KEY_DOWN) && inputStream &&
		inputStream->s.append && event->ascii)
	{
		inputStream->s.append(&inputStream->s, (char) event->ascii);
	}

	if (textArea->scrollBar)
		updateScrollBar(textArea);

	return (0);
}


static int destroy(kernelWindowComponent *component)
{
	kernelWindowTextArea *textArea = component->data;
	int processId = kernelCurrentProcess->processId;

	if (textArea)
	{
		if (textArea->area)
		{
			// If the current input/output streams are currently pointing at
			// our input/output streams, set the current ones to NULL
			if (kernelTextGetCurrentInput() == textArea->area->inputStream)
				kernelTextSetCurrentInput(NULL);
			if (kernelTextGetCurrentOutput() == textArea->area->outputStream)
				kernelTextSetCurrentOutput(NULL);

			if (kernelMultitaskerGetTextInput() == textArea->area->inputStream)
				kernelMultitaskerSetTextInput(processId, NULL);
			if (kernelMultitaskerGetTextOutput() ==
				textArea->area->outputStream)
			{
				kernelMultitaskerSetTextOutput(processId, NULL);
			}

			kernelTextAreaDestroy(textArea->area);
			textArea->area = NULL;
		}

		if (textArea->scrollBar)
		{
			kernelWindowComponentDestroy(textArea->scrollBar);
			textArea->scrollBar = NULL;
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

kernelWindowComponent *kernelWindowNewTextArea(objectKey parent, int columns,
	int rows, int bufferLines, componentParameters *params)
{
	// Formats a kernelWindowComponent as a kernelWindowTextArea

	kernelWindowComponent *component = NULL;
	kernelWindowTextArea *textArea = NULL;
	componentParameters subParams;

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

	component->type = textAreaComponentType;
	component->flags |= (WINFLAG_CANFOCUS | WINFLAG_RESIZABLE);

	// Set the functions
	component->numComps = &numComps;
	component->flatten = &flatten;
	component->setBuffer = &setBuffer;
	component->draw = &draw;
	component->update = &update;
	component->move = &move;
	component->resize = &resize;
	component->focus = &focus;
	component->getData = &getData;
	component->setData = &setData;
	component->mouseEvent = &mouseEvent;
	component->keyEvent = &keyEvent;
	component->destroy = &destroy;

	// If the user wants the default colors, we set them to the default for a
	// text area
	if (!(component->params.flags & WINDOW_COMPFLAG_CUSTOMBACKGROUND))
		memcpy((color *) &component->params.background, &COLOR_WHITE,
			sizeof(color));

	// If font is NULL, user the default small fixed-width font
	if (!component->params.font)
	{
		// Try to make sure we've got the required character set
		kernelFontGet(windowVariables->font.fixWidth.small.family,
			windowVariables->font.fixWidth.small.flags,
			windowVariables->font.fixWidth.small.points,
			(char *) component->charSet);

		component->params.font = windowVariables->font.fixWidth.small.font;
	}

	// Get memory for the kernelWindowTextArea
	textArea = kernelMalloc(sizeof(kernelWindowTextArea));
	if (!textArea)
	{
		kernelWindowComponentDestroy(component);
		return (component = NULL);
	}

	component->data = (void *) textArea;

	// Create the text area inside it
	textArea->area = kernelTextAreaNew(columns, rows, 1, bufferLines);
	if (!textArea->area)
	{
		kernelWindowComponentDestroy(component);
		return (component = NULL);
	}

	// Set some values
	textArea->area->xCoord = windowVariables->border.thickness;
	textArea->area->yCoord = windowVariables->border.thickness;
	memcpy((color *) &textArea->area->foreground,
		(color *) &component->params.foreground, sizeof(color));
	memcpy((color *) &textArea->area->background,
		(color *) &component->params.background, sizeof(color));
	textArea->area->font = (kernelFont *) component->params.font;
	textArea->area->charSet = (char *) component->charSet;
	textArea->area->windowComponent = (void *) component;
	textArea->areaWidth = (columns *
		((kernelFont *) component->params.font)->glyphWidth);

	// Populate the rest of the component fields
	component->width = (textArea->areaWidth +
		(windowVariables->border.thickness * 2));
	component->height = ((rows *
		((kernelFont *) component->params.font)->glyphHeight) +
			(windowVariables->border.thickness * 2));

	// If there are any buffer lines, we need a scroll bar as well.
	if (bufferLines)
	{
		// Standard parameters for a scroll bar
		memcpy(&subParams, params, sizeof(componentParameters));
		subParams.flags &= ~(WINDOW_COMPFLAG_CUSTOMFOREGROUND |
			WINDOW_COMPFLAG_CUSTOMBACKGROUND);

		textArea->scrollBar =
			kernelWindowNewScrollBar(parent, scrollbar_vertical, 0,
				component->height, &subParams);
		if (!textArea->scrollBar)
		{
			kernelWindowComponentDestroy(component);
			return (component = NULL);
		}

		// Remove the scrollbar from the parent container
		removeFromContainer(textArea->scrollBar);

		textArea->scrollBar->xCoord = component->width;
		component->width += textArea->scrollBar->width;
	}

	// After our width and height are finalized...
	component->minWidth = component->width;
	component->minHeight = component->height;

	return (component);
}

