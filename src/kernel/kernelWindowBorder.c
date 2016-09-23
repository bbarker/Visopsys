//
//	Visopsys
//	Copyright (C) 1998-2016 J. Andrew McLaughlin
//
//	This program is free software; you can redistribute it and/or modify it
//	under the terms of the GNU General Public License as published by the Free
//	Software Foundation; either version 2 of the License, or (at your option)
//	any later version.
//
//	This program is distributed in the hope that it will be useful, but
//	WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
//	or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License
//	for more details.
//
//	You should have received a copy of the GNU General Public License along
//	with this program; if not, write to the Free Software Foundation, Inc.,
//	51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
//
//	kernelWindowBorder.c
//

// This code is for managing kernelWindowBorder objects.
// These are just images that appear inside windows and buttons, etc

#include "kernelWindow.h"	// Our prototypes are here
#include "kernelDebug.h"
#include "kernelError.h"
#include "kernelMalloc.h"
#include "kernelMultitasker.h"
#include "kernelParameters.h"
#include "kernelWindowEventStream.h"
#include <string.h>

static int newWindowX = 0;
static int newWindowY = 0;
static int newWindowWidth = 0;
static int newWindowHeight = 0;

extern kernelWindowVariables *windowVariables;


static void resizeWindow(kernelWindowComponent *component, windowEvent *event)
{
	// This gets called by the window manager thread when a window resize
	// has been requested

	if (event->type == EVENT_WINDOW_RESIZE)
	{
		component->window->xCoord = newWindowX;
		component->window->yCoord = newWindowY;

		kernelWindowSetSize(component->window, newWindowWidth, newWindowHeight);

		kernelWindowSetVisible(component->window, 1);

		// Transfer this event into the window's event stream
		kernelWindowEventStreamWrite(&component->window->events, event);
	}

	return;
}


static int draw(kernelWindowComponent *component)
{
	// Draws all the border components on the window.	Really we should implement
	// this so that each border gets drawn individually, but this is faster

	kernelWindowBorder *border = (kernelWindowBorder *) component->data;

	kernelDebug(debug_gui, "WindowBorder \"%s\" draw", component->window->title);

	kernelGraphicDrawGradientBorder(component->buffer, 0, 0,
		component->window->buffer.width, component->window->buffer.height,
		windowVariables->border.thickness,
		(color *) &component->window->background,
		windowVariables->border.shadingIncrement, draw_normal, border->type);

	return (0);
}


static int mouseEvent(kernelWindowComponent *component, windowEvent *event)
{
	// When dragging mouse events happen to border components, we resize the
	// window.

	kernelWindowBorder *border = (kernelWindowBorder *) component->data;
	windowEvent resizeEvent;
	int diff = 0;
	int tmpWindowX = newWindowX;
	int tmpWindowY = newWindowY;
	int tmpWindowWidth = newWindowWidth;
	int tmpWindowHeight = newWindowHeight;
	static int dragging = 0;

	kernelDebug(debug_gui, "WindowBorder \"%s\" mouse event",
		component->window->title);

	if (dragging)
	{
		if (event->type == EVENT_MOUSE_DRAG)
		{
			// The window is still being resized

			// Erase the xor'ed outline
			kernelWindowRedrawArea(newWindowX, newWindowY, newWindowWidth, 1);
			kernelWindowRedrawArea(newWindowX, newWindowY, 1, newWindowHeight);
			kernelWindowRedrawArea((newWindowX + newWindowWidth - 1),
				newWindowY, 1, newWindowHeight);
			kernelWindowRedrawArea(newWindowX,
				(newWindowY + newWindowHeight - 1), newWindowWidth, 1);

			// Set the new size
			if ((border->type == border_top) &&
				(event->yPosition < (newWindowY + newWindowHeight)))
			{
				diff = (event->yPosition - newWindowY);
				tmpWindowY += diff;
				tmpWindowHeight -= diff;
			}
			else if ((border->type == border_bottom) &&
				(event->yPosition > newWindowY))
			{
				diff = (event->yPosition - (newWindowY + newWindowHeight));
				tmpWindowHeight += diff;
			}
			else if ((border->type == border_left) &&
				(event->xPosition < (newWindowX + newWindowWidth)))
			{
				diff = (event->xPosition - newWindowX);
				tmpWindowX += diff;
				tmpWindowWidth -= diff;
			}
			else if ((border->type == border_right) &&
				(event->xPosition > newWindowX))
			{
				diff = (event->xPosition - (newWindowX + newWindowWidth));
				tmpWindowWidth += diff;
			}

			// Don't resize below reasonable minimums
			if (tmpWindowWidth < windowVariables->window.minWidth)
			{
				newWindowWidth = windowVariables->window.minWidth;
			}
			else
			{
				newWindowX = tmpWindowX;
				newWindowWidth = tmpWindowWidth;
			}

			if (tmpWindowHeight < windowVariables->window.minHeight)
			{
				newWindowHeight = windowVariables->window.minHeight;
			}
			else
			{
				newWindowY = tmpWindowY;
				newWindowHeight = tmpWindowHeight;
			}

			// Draw an xor'ed outline
			kernelGraphicDrawRect(NULL, &((color){ 255, 255, 255 }),
				draw_xor, newWindowX, newWindowY, newWindowWidth,
				newWindowHeight, 1, 0);
		}
		else
		{
			// The resize is finished

			kernelDebug(debug_gui, "WindowBorder \"%s\" drag finished",
				component->window->title);

			// Erase the xor'ed outline
			kernelGraphicDrawRect(NULL, &((color){ 255, 255, 255 }),
				draw_xor, newWindowX, newWindowY, newWindowWidth,
				newWindowHeight, 1, 0);

			// Write a resize event to the component event stream
			memset(&resizeEvent, 0, sizeof(windowEvent));
			resizeEvent.type = EVENT_WINDOW_RESIZE;
			kernelWindowEventStreamWrite(&component->events, &resizeEvent);

			dragging = 0;
		}
	}

	else if ((event->type == EVENT_MOUSE_DRAG) &&
		(component->window->flags & (WINFLAG_RESIZABLEX | WINFLAG_RESIZABLEY)))
	{
		// The user has started to drag the border, to resize the window.

		kernelDebug(debug_gui, "WindowBorder \"%s\" drag start",
			component->window->title);

		// Don't show it while it's being resized
		kernelWindowSetVisible(component->window, 0);

		// Draw an xor'ed outline
		kernelGraphicDrawRect(NULL, &((color){ 255, 255, 255 }),
			draw_xor, component->window->xCoord, component->window->yCoord,
			component->window->buffer.width, component->window->buffer.height,
			1, 0);

		newWindowX = component->window->xCoord;
		newWindowY = component->window->yCoord;
		newWindowWidth = component->window->buffer.width;
		newWindowHeight = component->window->buffer.height;
		dragging = 1;
	}

	return (0);
}


static int destroy(kernelWindowComponent *component)
{
	int count;

	if (component->data)
	{
		for (count = 0; count < 4; count ++)
		{
			if (component->window->borders[count] == component)
			{
				component->window->borders[count] = NULL;
				break;
			}
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

kernelWindowComponent *kernelWindowNewBorder(objectKey parent, borderType type,
	componentParameters *params)
{
	// Formats a kernelWindowComponent as a kernelWindowBorder

	kernelWindowComponent *component = NULL;
	kernelWindowBorder *borderComponent = NULL;
	kernelWindow *window = NULL;

	// Check parameters
	if (!parent || !params)
	{
		kernelError(kernel_error, "NULL parameter");
		return (component = NULL);
	}

	// Get the basic component structure
	component = kernelWindowComponentNew(parent, params);
	if (!component)
		return (component);

	component->type = borderComponentType;

	// Set the functions
	component->draw = &draw;
	component->mouseEvent = &mouseEvent;
	component->destroy = &destroy;

	// Get the border component
	borderComponent = kernelMalloc(sizeof(kernelWindowBorder));
	if (!borderComponent)
	{
		kernelWindowComponentDestroy(component);
		return (component = NULL);
	}

	component->data = (void *) borderComponent;

	borderComponent->type = type;

	window = getWindow(parent);

	if ((type == border_left) || (type == border_right))
	{
		component->width = windowVariables->border.thickness;
		component->height = window->buffer.height;
		component->pointer = kernelMouseGetPointer(MOUSE_POINTER_RESIZEH);
	}
	else
	{
		component->width = window->buffer.width;
		component->height = windowVariables->border.thickness;
		component->pointer = kernelMouseGetPointer(MOUSE_POINTER_RESIZEV);
	}

	component->minWidth = component->width;
	component->minHeight = component->height;

	kernelWindowRegisterEventHandler(component, &resizeWindow);

	return (component);
}

