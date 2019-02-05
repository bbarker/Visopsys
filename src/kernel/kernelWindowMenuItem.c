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
//  kernelWindowMenuItem.c
//

// This code is for managing kernelWindowMenuItem objects.  These are
// selectable items that occur inside of kernelWindowMenu components.

#include "kernelWindow.h"	// Our prototypes are here
#include "kernelDebug.h"
#include "kernelError.h"
#include <stdlib.h>
#include <string.h>

static int (*saveListItemSetData)(kernelWindowComponent *, void *, int) = NULL;
extern kernelWindowVariables *windowVariables;


static int menuWidth(kernelWindow *menu)
{
	// Return the width of the widest menu item.

	int width = 0;
	kernelWindowContainer *container = (kernelWindowContainer *)
		menu->mainContainer->data;
	int count;

	for (count = 0; count < container->numComponents; count ++)
	{
		if (container->components[count]->width > width)
			width = container->components[count]->width;
	}

	return (width + (windowVariables->border.thickness * 6));
}


static int menuHeight(kernelWindow *menu)
{
	// Return the cumulative height of the menu items.

	int height = 0;
	kernelWindowContainer *container = (kernelWindowContainer *)
		menu->mainContainer->data;
	int count;

	for (count = 0; count < container->numComponents; count ++)
		height += container->components[count]->height;

	return (height + (windowVariables->border.thickness * 4));
}


static int setData(kernelWindowComponent *component, void *text, int length)
{
	// Set the menu item text

	int status = 0;
	kernelWindow *menu = component->window;
	listItemParameters itemParams;

	if (saveListItemSetData)
	{
		memset(&itemParams, 0, sizeof(listItemParameters));

		strncpy(itemParams.text, text, min(length, WINDOW_MAX_LABEL_LENGTH));

		status = saveListItemSetData(component, &itemParams,
			sizeof(listItemParameters));

		// Redo the menu layout
		menu->mainContainer->layout(menu->mainContainer);

		// If the menu is visible, draw it
		if (menu->flags & WINFLAG_VISIBLE)
			menu->draw(menu);
	}
	else
	{
		status = ERR_NOTINITIALIZED;
	}

	return (status);
}


/////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////
//
// Below here, the functions are exported for external use
//
/////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////

kernelWindowComponent *kernelWindowNewMenuItem(kernelWindow *menu,
	const char *text, componentParameters *userParams)
{
	// Formats a kernelWindowComponent as a kernelWindowMenuItem

	kernelWindowComponent *component = NULL;
	int numComponents = 0;
	componentParameters params;
	listItemParameters itemParams;

	// Check params
	if (!menu || !text || !userParams)
	{
		kernelError(kernel_error, "NULL parameter");
		return (component = NULL);
	}

	numComponents =
		((kernelWindowContainer *) menu->mainContainer->data)->numComponents;

	kernelDebug(debug_gui, "WindowMenuItem new menu item %d %s", numComponents,
		text);

	memcpy(&params, userParams, sizeof(componentParameters));
	params.gridX = 0;
	params.gridY = numComponents;
	params.gridWidth = params.gridHeight = 1;
	params.padLeft = params.padRight = windowVariables->border.thickness;
	params.padTop = (numComponents? 0 : windowVariables->border.thickness);
	params.padBottom = 0;
	params.orientationX = orient_left;
	params.orientationY = orient_top;

	if (!params.font)
		params.font = windowVariables->font.varWidth.small.font;

	memset(&itemParams, 0, sizeof(listItemParameters));
	strncpy(itemParams.text, text, WINDOW_MAX_LABEL_LENGTH);

	// Get the superclass list item component
	component = kernelWindowNewListItem(menu, windowlist_textonly, &itemParams,
		&params);
	if (!component)
		return (component);

	// If we don't have the list item's setData() function pointer saved, save
	// it now
	if (!saveListItemSetData)
		saveListItemSetData = component->setData;

	// Set the functions
	component->setData = &setData;

	// We use a different default background color than the window list
	// item component that the menu item is based upon
	if (!(params.flags & WINDOW_COMPFLAG_CUSTOMBACKGROUND))
	{
		memcpy((color *) &component->params.background,
			&windowVariables->color.background, sizeof(color));
	}

	// Set the new size of the menu
	kernelWindowSetSize(menu, menuWidth(menu), menuHeight(menu));

	// Redo the layout
	menu->mainContainer->layout(menu->mainContainer);

	// If the menu is visible, draw it
	if (menu->flags & WINFLAG_VISIBLE)
		menu->draw(menu);

	return (component);
}

