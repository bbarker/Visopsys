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
//  kernelWindowComponent.c
//

// This code implements a generic window component, including a default
// constructor and default functions that can be overridden.

#include "kernelWindow.h"	// Our prototypes are here
#include "kernelDebug.h"
#include "kernelError.h"
#include "kernelMalloc.h"
#include "kernelMultitasker.h"
#include "kernelWindowEventStream.h"
#include <stdlib.h>
#include <string.h>
#include <sys/env.h>

extern kernelWindowVariables *windowVariables;


static int drawBorder(kernelWindowComponent *component, int draw)
{
	// Draw or erase a simple little border around the supplied component

	if (draw)
		kernelGraphicDrawRect(component->buffer,
			(color *) &component->params.foreground,
			draw_normal, (component->xCoord - 2), (component->yCoord - 2),
			(component->width + 4), (component->height + 4), 1, 0);
	else
		kernelGraphicDrawRect(component->buffer,
			(color *) &component->window->background,
			draw_normal, (component->xCoord - 2), (component->yCoord - 2),
			(component->width + 4),	(component->height + 4), 1, 0);

	return (0);
}


static int erase(kernelWindowComponent *component)
{
	// Simple erasure of a component, by drawing a filled rectangle of the
	// window's background color over the component's area

	kernelGraphicDrawRect(component->buffer,
		(color *) &component->window->background, draw_normal,
		component->xCoord, component->yCoord, component->width,
		component->height, 1, 1);

	return (0);
}


static int grey(kernelWindowComponent *component)
{
	// Filter the component with the default background color

	// If the component has a draw function (stored in its 'grey' pointer)
	// call it first.
	if (component->grey)
		component->grey(component);

	kernelGraphicFilter(component->buffer,
		(color *) &component->params.background, component->xCoord,
		component->yCoord, component->width, component->height);

	return (0);
}


static void renderComponent(kernelWindowComponent *component)
{
	kernelWindow *window = component->window;

	kernelDebug(debug_gui, "WindowComponent render type %s in '%s'",
		componentTypeString(component->type), window->title);

	// Redraw a clip of that part of the window
	if (window->drawClip)
		window->drawClip(window, (component->xCoord - 2),
			(component->yCoord - 2), (component->width + 4),
			(component->height + 4));
}


/////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////
//
// Below here, the functions are exported for external use
//
/////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////

kernelWindowComponent *kernelWindowComponentNew(objectKey parent,
	componentParameters *params)
{
	// Creates a new component and adds it to the main container of the
	// parent window, or to the parent itself if it's a container.

	int status = 0;
	kernelWindowComponent *parentComponent = NULL;
	kernelWindowComponent *component = NULL;
	const char *charSet = NULL;

	// Check params
	if (!parent || !params)
		return (component = NULL);

	// Get memory for the basic component
	component = kernelMalloc(sizeof(kernelWindowComponent));
	if (!component)
		return (component);

	component->type = genericComponentType;
	component->subType = genericComponentType;
	component->window = getWindow(parent);

	// Use the window's character set by default
	if (component->window)
		strncpy((char *) component->charSet,
			(char *) component->window->charSet, CHARSET_NAME_LEN);

	// Use the window's buffer by default
	if (component->window)
		component->buffer = &component->window->buffer;

	// Visible and enabled by default
	component->flags |= (WINFLAG_VISIBLE | WINFLAG_ENABLED);

	// If the parameter flags indicate the component should be focusable,
	// set the appropriate component flag
	if (params->flags & WINDOW_COMPFLAG_CANFOCUS)
		component->flags |= WINFLAG_CANFOCUS;

	// Copy the parameters into the component
	memcpy((void *) &component->params, params, sizeof(componentParameters));

	// If the default colors are requested, copy them into the component
	// parameters
	if (!(component->params.flags & WINDOW_COMPFLAG_CUSTOMFOREGROUND))
	{
		memcpy((void *) &component->params.foreground,
			&windowVariables->color.foreground, sizeof(color));
	}

	if (!(component->params.flags & WINDOW_COMPFLAG_CUSTOMBACKGROUND))
	{
		memcpy((void *) &component->params.background,
			&windowVariables->color.background, sizeof(color));
	}

	// Try to make sure we have the required character set.
	if (params->font && kernelCurrentProcess)
	{
		charSet = kernelVariableListGet(kernelCurrentProcess->environment,
			ENV_CHARSET);

		if (charSet)
			kernelWindowComponentSetCharSet(component, charSet);
	}

	// Initialize the event stream
	status = kernelWindowEventStreamNew(&component->events);
	if (status < 0)
	{
		kernelFree((void *) component);
		return (component = NULL);
	}

	// Default functions
	component->drawBorder = &drawBorder;
	component->erase = &erase;
	component->grey = &grey;

	// Now we need to add the component somewhere.

	if (((kernelWindow *) parent)->type == windowType)
	{
		// The parent is a window, so we use the window's main container.
		parentComponent = ((kernelWindow *) parent)->mainContainer;
	}
	else if (((kernelWindowComponent *) parent)->add)
	{
		// Not a window but a component with an 'add' function.
		parentComponent = parent;
	}
	else
	{
		kernelError(kernel_error, "Invalid parent object for new component");
		kernelFree((void *) component);
		return (component = NULL);
	}

	if (parentComponent && parentComponent->add)
		status = parentComponent->add(parentComponent, component);

	if (status < 0)
	{
		kernelFree((void *) component);
		return (component = NULL);
	}

	if (!component->container)
		component->container = parentComponent;

	return (component);
}


void kernelWindowComponentDestroy(kernelWindowComponent *component)
{
	extern kernelWindow *consoleWindow;
	extern kernelWindowComponent *consoleTextArea;

	// Check params.
	if (!component)
		return;

	// Make sure the component is removed from any containers it's in
	removeFromContainer(component);

	// Never destroy the console text area.  If this is the console text area,
	// move it back to our console window
	if (component == consoleTextArea)
	{
		kernelWindowMoveConsoleTextArea(component->window, consoleWindow);
		return;
	}

	// Call the component's own destroy function
	if (component->destroy)
		component->destroy(component);

	component->data = NULL;

	// If this component's window is referencing it in any special way,
	// we have to remove the reference
	if (component->window)
	{
		if (component->window->titleBar == component)
			component->window->titleBar = NULL;
		if (component->window->borders[0] == component)
			component->window->borders[0] = NULL;
		if (component->window->borders[1] == component)
			component->window->borders[1] = NULL;
		if (component->window->borders[2] == component)
			component->window->borders[2] = NULL;
		if (component->window->borders[3] == component)
			component->window->borders[3] = NULL;
		if (component->window->menuBar == component)
			component->window->menuBar = NULL;
		if (component->window->sysContainer == component)
			component->window->sysContainer = NULL;
		if (component->window->mainContainer == component)
			component->window->mainContainer = NULL;
		if (component->window->focusComponent == component)
			component->window->focusComponent = NULL;
		if (component->window->mouseInComponent == component)
			component->window->mouseInComponent = NULL;
	}

	// Deallocate generic things

	// Free the component's event stream
	kernelStreamDestroy(&component->events);

	// Free the component itself
	kernelFree((void *) component);

	return;
}


int kernelWindowComponentSetCharSet(kernelWindowComponent *component,
	const char *charSet)
{
	// Sets the character set for a component

	int status = 0;

	if (!component)
		return (status = ERR_NOSUCHENTRY);

	// Set the character set
	strncpy((char *) component->charSet, charSet, CHARSET_NAME_LEN);

	// Try to make sure we have the required character set.
	if (component->params.font &&
		!kernelFontHasCharSet((kernelFont *) component->params.font, charSet))
	{
		kernelFontGet(((kernelFont *) component->params.font)->family,
			((kernelFont *) component->params.font)->flags,
			((kernelFont *) component->params.font)->points, charSet);
	}

	// Return success
	return (status = 0);
}


int kernelWindowComponentSetVisible(kernelWindowComponent *component,
	int visible)
{
	// Set a component visible or not visible

	int status = 0;
	kernelWindow *window = NULL;
	int numComponents = 0;
	kernelWindowComponent **array = NULL;
	kernelWindowComponent *tmpComponent = NULL;
	int count;

	// Check params
	if (!component)
		return (status = ERR_NULLPARAMETER);

	window = component->window;

	if (component->numComps)
		numComponents = component->numComps(component);

	// One for the component itself.
	numComponents += 1;

	array = kernelMalloc(numComponents * sizeof(kernelWindowComponent *));
	if (!array)
		return (status = ERR_MEMORY);

	array[0] = component;
	numComponents = 1;

	if (component->flatten)
		component->flatten(component, array, &numComponents, 0);

	for (count = 0; count < numComponents; count ++)
	{
		tmpComponent = array[count];

		if (visible)
		{
			tmpComponent->flags |= WINFLAG_VISIBLE;
			if (tmpComponent->draw)
				tmpComponent->draw(tmpComponent);
		}
		else // Not visible
		{
			if (window->focusComponent == tmpComponent)
			{
				// Make sure it doesn't have the focus
				tmpComponent->flags &= ~WINFLAG_HASFOCUS;
				window->focusComponent = NULL;
			}

			tmpComponent->flags &= ~WINFLAG_VISIBLE;
			if (tmpComponent->erase)
				tmpComponent->erase(tmpComponent);
		}
	}

	kernelFree(array);

	renderComponent(component);

	return (status = 0);
}


int kernelWindowComponentSetEnabled(kernelWindowComponent *component,
	int enabled)
{
	// Set a component enabled or not enabled.  What we do is swap the 'draw'
	// and 'grey' functions of the component and any sub-components, if
	// applicable

	int status = 0;
	kernelWindow *window = NULL;
	kernelWindowComponent **array = NULL;
	int numComponents = 0;
	int (*tmpDraw)(kernelWindowComponent *) = NULL;
	kernelWindowComponent *tmpComponent = NULL;
	int count;

	// Check params
	if (!component)
		return (status = ERR_NULLPARAMETER);

	window = component->window;

	kernelDebug(debug_gui, "WindowComponent set type %s in '%s' %sabled",
		componentTypeString(component->type), window->title,
		(enabled? "en" : "dis"));

	if (component->numComps)
		numComponents = component->numComps(component);

	// One for the component itself.
	numComponents += 1;

	array = kernelMalloc(numComponents * sizeof(kernelWindowComponent *));
	if (!array)
		return (status = ERR_MEMORY);

	array[0] = component;
	numComponents = 1;

	if (component->flatten)
		component->flatten(component, array, &numComponents, 0);

	for (count = 0; count < numComponents; count ++)
	{
		tmpComponent = array[count];

		if ((enabled && !(tmpComponent->flags & WINFLAG_ENABLED)) ||
			(!enabled && (tmpComponent->flags & WINFLAG_ENABLED)))
		{
			// Swap the 'draw' and 'grey' function pointers
			tmpDraw = tmpComponent->grey;
			tmpComponent->grey = tmpComponent->draw;
			tmpComponent->draw = tmpDraw;
		}

		if (enabled)
		{
			tmpComponent->flags |= WINFLAG_ENABLED;
		}
		else // disabled
		{
			tmpComponent->flags &= ~WINFLAG_ENABLED;

			if (window->focusComponent == tmpComponent)
			{
				// Make sure it doesn't have the focus
				tmpComponent->flags &= ~WINFLAG_HASFOCUS;
				window->focusComponent = NULL;
			}
		}
	}

	kernelFree(array);

	if (component->flags & WINFLAG_VISIBLE)
		renderComponent(component);

	return (status = 0);
}


int kernelWindowComponentGetWidth(kernelWindowComponent *component)
{
	// Return the width parameter of the component
	if (!component)
		return (0);
	else
		return (component->width);
}


int kernelWindowComponentSetWidth(kernelWindowComponent *component, int width)
{
	// Set the width parameter of the component

	int status = 0;

	if (!component)
		return (status = ERR_NULLPARAMETER);

	// If the component wants to know about resize events...
	if (component->resize)
		status = component->resize(component, width, component->height);

	if ((width < component->width) && (component->flags & WINFLAG_VISIBLE))
	{
		component->flags &= ~WINFLAG_VISIBLE;
		renderComponent(component);
		component->flags |= WINFLAG_VISIBLE;
	}

	component->width = width;

	renderComponent(component);

	return (status);
}


int kernelWindowComponentGetHeight(kernelWindowComponent *component)
{
	// Return the height parameter of the component
	if (!component)
		return (0);
	else
		return (component->height);
}


int kernelWindowComponentSetHeight(kernelWindowComponent *component,
	int height)
{
	// Set the width parameter of the component

	int status = 0;

	if (!component)
		return (status = ERR_NULLPARAMETER);

	// If the component wants to know about resize events...
	if (component->resize)
		status = component->resize(component, component->width, height);

	if ((height < component->height) && (component->flags & WINFLAG_VISIBLE))
	{
		component->flags &= ~WINFLAG_VISIBLE;
		renderComponent(component);
		component->flags |= WINFLAG_VISIBLE;
	}

	component->height = height;

	renderComponent(component);

	return (status);
}


int kernelWindowComponentFocus(kernelWindowComponent *component)
{
	// Gives the supplied component the focus, puts it on top of any other
	// components it intersects, etc.

	int status = 0;
	kernelWindow *window = NULL;

	// Check params
	if (!component)
		return (status = ERR_NULLPARAMETER);

	// Get the window
	window = component->window;
	if (!window)
	{
		kernelError(kernel_error, "Component to focus has no window");
		return (status = ERR_NODATA);
	}

	return (window->changeComponentFocus(window, component));
}


int kernelWindowComponentUnfocus(kernelWindowComponent *component)
{
	// Removes the focus from the supplied component

	int status = 0;
	kernelWindow *window = NULL;

	// Check params
	if (!component)
		return (status = ERR_NULLPARAMETER);

	// Get the window
	window = component->window;
	if (!window)
	{
		kernelError(kernel_error, "Component to unfocus has no window");
		return (status = ERR_NODATA);
	}

	return (window->changeComponentFocus(window, NULL));
}


int kernelWindowComponentDraw(kernelWindowComponent *component)
{
	// Draw  a component

	int status = 0;

	// Check params
	if (!component)
		return (status = ERR_NULLPARAMETER);

	if (!component->draw)
		return (status = ERR_NOTIMPLEMENTED);

	return (component->draw(component));
}


int kernelWindowComponentGetData(kernelWindowComponent *component,
	void *buffer, int size)
{
	// Get (generic) data from a component

	int status = 0;

	// Check params
	if (!component || !buffer)
		return (status = ERR_NULLPARAMETER);

	if (!component->getData)
		return (status = ERR_NOTIMPLEMENTED);

	return (component->getData(component, buffer, size));
}


int kernelWindowComponentSetData(kernelWindowComponent *component,
	void *buffer, int size, int render)
{
	// Set (generic) data in a component

	int status = 0;

	// Check params.  buffer can only be NULL if size is NULL
	if (!component || (!buffer && size))
		return (status = ERR_NULLPARAMETER);

	if (!component->setData)
		return (status = ERR_NOTIMPLEMENTED);

	status = component->setData(component, buffer, size);

	if (render)
		renderComponent(component);

	return (status);
}


int kernelWindowComponentGetSelected(kernelWindowComponent *component,
	int *selection)
{
	// Calls the 'get selected' method of the component, if applicable

	// Check parameters
	if (!component || !selection)
		return (ERR_NULLPARAMETER);

	if (!component->getSelected)
		return (ERR_NOSUCHFUNCTION);

	return (component->getSelected(component, selection));
}


int kernelWindowComponentSetSelected(kernelWindowComponent *component,
	int selected)
{
	// Calls the 'set selected' method of the component, if applicable

	// Check parameters
	if (!component)
		return (ERR_NULLPARAMETER);

	if (!component->setSelected)
		return (ERR_NOSUCHFUNCTION);

	return (component->setSelected(component, selected));
}

