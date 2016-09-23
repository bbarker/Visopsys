//
//  Visopsys
//  Copyright (C) 1998-2016 J. Andrew McLaughlin
//
//  This library is free software; you can redistribute it and/or modify it
//  under the terms of the GNU Lesser General Public License as published by
//  the Free Software Foundation; either version 2.1 of the License, or (at
//  your option) any later version.
//
//  This library is distributed in the hope that it will be useful, but
//  WITHOUT ANY WARRANTY; without even the implied warranty of
//  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU Lesser
//  General Public License for more details.
//
//  You should have received a copy of the GNU Lesser General Public License
//  along with this library; if not, write to the Free Software Foundation,
//  Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
//
//  windowArchiveList.c
//

// This contains functions for user programs to operate GUI components.

#include <errno.h>
#include <libintl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/api.h>
#include <sys/window.h>

#define _(string) gettext(string)

extern int libwindow_initialized;
extern void libwindowInitialize(void);


__attribute__((format(printf, 1, 2)))
static void error(const char *format, ...)
{
	// Generic error message code

	va_list list;
	char *output = NULL;

	output = malloc(MAXSTRINGLENGTH);
	if (!output)
		return;

	va_start(list, format);
	vsnprintf(output, MAXSTRINGLENGTH, format, list);
	va_end(list);

	windowNewErrorDialog(NULL, _("Error"), output);
	free(output);
}


static listItemParameters *allocateIconParameters(windowArchiveList *archList)
{
	listItemParameters *newIconParams = NULL;
	int count;

	if (archList->numMembers)
	{
		newIconParams = calloc(archList->numMembers,
			sizeof(listItemParameters));
	}
	else
	{
		// Can't create a list component with a NULL parameter here
		newIconParams = calloc(1, sizeof(listItemParameters));
	}

	if (!newIconParams)
	{
		error("%s", _("Memory allocation error creating icon parameters"));
		return (newIconParams);
	}

	// Fill in an array of list item parameters structures for our archive
	// members.  It will get passed to the window list creation function
	// in a moment
	for (count = 0; count < archList->numMembers; count ++)
	{
		strncpy(newIconParams[count].text, archList->members[count].name,
			WINDOW_MAX_LABEL_LENGTH);
	}

	return (newIconParams);
}


static int update(windowArchiveList *archList, archiveMemberInfo *members,
	int numMembers)
{
	// Update the archive list from the supplied member list.

	listItemParameters *iconParams = NULL;

	archList->members = members;
	archList->numMembers = numMembers;

	// Get our array of icon parameters
	iconParams = allocateIconParameters(archList);

	// Clear the list
	windowComponentSetData(archList->key, NULL, 0, 0 /* no redraw */);
	windowComponentSetData(archList->key, iconParams, archList->numMembers,
		1 /* redraw */);
	windowComponentSetSelected(archList->key, 0);

	if (iconParams)
		free(iconParams);

	return (0);
}


static int destroy(windowArchiveList *archList)
{
	// Detroy and deallocate the archive list.

	free(archList);

	return (0);
}


static int eventHandler(windowArchiveList *archList, windowEvent *event)
{
	int status = 0;
	int selected = -1;

	// Get the selected item
	windowComponentGetSelected(archList->key, &selected);
	if (selected < 0)
		return (status = selected);

	// Check for events in our icon list.  We consider the icon 'clicked'
	// if it is a mouse click selection, or an ENTER key selection
	if ((event->type & EVENT_SELECTION) &&
		((event->type & EVENT_MOUSE_LEFTUP) ||
		((event->type & EVENT_KEY_DOWN) && (event->key == keyEnter))))
	{
		if (archList->selectionCallback)
			archList->selectionCallback(selected);
	}

	return (status = 0);
}


/////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////
//
// Below here, the functions are exported for external use
//
/////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////

_X_ windowArchiveList *windowNewArchiveList(objectKey parent, windowListType type, int rows, int columns, archiveMemberInfo *members, int numMembers, void (*callback)(int), componentParameters *params)
{
	// Desc: Create a new archive list widget with the parent window 'parent', the window list type 'type' (windowlist_textonly or windowlist_icononly is currently supported), of height 'rows' and width 'columns', an array of archive member info and number of members, a function 'callback' for when the status changes, and component parameters 'params'.

	windowArchiveList *archList = NULL;
	listItemParameters *iconParams = NULL;

	if (!libwindow_initialized)
		libwindowInitialize();

	// Check params.  Members and callback can be NULL.
	if (!parent || !params)
	{
		errno = ERR_NULLPARAMETER;
		return (archList = NULL);
	}

	// Allocate memory for our archive list
	archList = malloc(sizeof(windowArchiveList));
	if (!archList)
		return (archList);

	archList->members = members;
	archList->numMembers = numMembers;
	archList->selectionCallback = callback;

	// Get our array of icon parameters
	iconParams = allocateIconParameters(archList);

	// Create a window list to hold the icons
	archList->key = windowNewList(parent, type, rows, columns, 0, iconParams,
		archList->numMembers, params);

	if (iconParams)
		free(iconParams);

	archList->eventHandler = &eventHandler;
	archList->update = &update;
	archList->destroy = &destroy;

	return (archList);
}

