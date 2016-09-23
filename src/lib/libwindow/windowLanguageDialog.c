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
//  windowColorDialog.c
//

// This contains functions for user programs to operate GUI components.

#include <libintl.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/api.h>
#include <sys/env.h>
#include <sys/errors.h>
#include <sys/file.h>
#include <sys/lang.h>
#include <sys/paths.h>
#include <sys/window.h>

#define _(string) gettext(string)
#define TITLE			_("Language Chooser")

extern int libwindow_initialized;
extern void libwindowInitialize(void);

static listItemParameters *langs = NULL;
static int numLangs = 0;


static int getLanguages(void)
{
	int status = 0;
	file langDir;
	file f;
	char path[MAX_PATH_LENGTH];
	char codes[32][6];
	int count;

	// We always have English
	strcpy(codes[0], LANG_ENGLISH);
	numLangs = 1;

	// Does the 'locale' directory exist?  Anything in it?
	status = fileFirst(PATH_SYSTEM_LOCALE, &langDir);
	if (status < 0)
		return (status);

	// Loop through any directories
	while (numLangs < 32)
	{
		if (langDir.type == dirT)
		{
			sprintf(path, "%s/%s/LC_MESSAGES", PATH_SYSTEM_LOCALE,
				langDir.name);

			if ((fileFind(path, &f) >= 0) && (f.type == dirT))
				strncpy(codes[numLangs++], langDir.name, 6);
		}

		status = fileNext(PATH_SYSTEM_LOCALE, &langDir);
		if (status < 0)
			break;
	}

	langs = malloc(numLangs * sizeof(listItemParameters));
	if (!langs)
		return (status = ERR_MEMORY);

	memset(langs, 0, (numLangs * sizeof(listItemParameters)));

	for (count = 0; count < numLangs; count ++)
	{
		// Copy the language codes, and try to load flag images

		strncpy(langs[count].text, codes[count], 6);

		sprintf(path, "%s/flag-%s.bmp", PATH_SYSTEM_LOCALE, langs[count].text);

		if (fileFind(path, &f) >= 0)
			imageLoad(path, 0, 0, &langs[count].iconImage);
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

_X_ int windowNewLanguageDialog(objectKey parentWindow, char *pickedLanguage)
{
	// Desc: Create a 'language chooser' dialog box, with the parent window 'parentWindow', and a pointer to the string buffer 'pickedLanguage', which should be at least 6 bytes in length.  The initial language selected will be the value of the LANG environment variable, if possible.  If 'parentWindow' is NULL, the dialog box is actually created as an independent window that looks the same as a dialog.  This is a blocking call that returns when the user closes the dialog window (i.e. the dialog is 'modal').

	int status = 0;
	objectKey dialogWindow = NULL;
	componentParameters params;
	objectKey langList = NULL;
	objectKey buttonContainer = NULL;
	objectKey okButton = NULL;
	objectKey cancelButton = NULL;
	int selected = 0;
	windowEvent event;
	int count;

	if (!libwindow_initialized)
		libwindowInitialize();

	// Check params.  It's okay for parentWindow to be NULL.
	if (!pickedLanguage)
		return (status = ERR_NULLPARAMETER);

	// See what languages are available
	status = getLanguages();
	if (status < 0)
		return (status);

	// Create the dialog.  Arbitrary size and coordinates
	if (parentWindow)
		dialogWindow = windowNewDialog(parentWindow, TITLE);
	else
		dialogWindow = windowNew(multitaskerGetCurrentProcessId(), TITLE);

	if (!dialogWindow)
	{
		status = ERR_NOCREATE;
		goto out;
	}

	memset(&params, 0, sizeof(componentParameters));
	params.gridWidth = 1;
	params.gridHeight = 1;
	params.padTop = 7;
	params.padLeft = 5;
	params.padRight = 3;
	params.orientationX = orient_center;
	params.orientationY = orient_middle;

	// Create the list of languages
	langList = windowNewList(dialogWindow, windowlist_icononly,
		((numLangs > 3)? 2 : 1) /* rows */,	3 /* columns */,
		0 /* multiple */, langs, numLangs, &params);
	if (!langList)
	{
		status = ERR_NOCREATE;
		goto out;
	}

	// Try to select the current language
	for (count = 0; count < numLangs; count ++)
	{
		if (!strcmp(langs[count].text, getenv(ENV_LANG)))
		{
			selected = count;
			break;
		}
	}

	windowComponentSetSelected(langList, selected);

	// Make a container for the buttons
	params.gridY++;
	params.padTop = 1;
	params.padBottom = 4;
	params.flags = (WINDOW_COMPFLAG_FIXEDWIDTH | WINDOW_COMPFLAG_FIXEDHEIGHT);
	buttonContainer = windowNewContainer(dialogWindow, "buttonContainer",
		&params);
	if (!buttonContainer)
	{
		status = ERR_NOCREATE;
		goto out;
	}

	// Create the OK button
	params.gridY = 0;
	params.padTop = params.padBottom = 0;
	params.padLeft = 2;
	params.padRight = 2;
	params.orientationX = orient_right;
	okButton = windowNewButton(buttonContainer, _("OK"), NULL, &params);
	if (!okButton)
	{
		status = ERR_NOCREATE;
		goto out;
	}

	// Create the Cancel button
	params.gridX++;
	params.orientationX = orient_left;
	cancelButton = windowNewButton(buttonContainer, _("Cancel"), NULL,
		&params);
	if (!cancelButton)
	{
		status = ERR_NOCREATE;
		goto out;
	}

	// Focus 'Cancel' by default
	windowComponentFocus(cancelButton);

	// Center the dialog on the parent, if there is a parent.
	if (parentWindow)
		windowCenterDialog(parentWindow, dialogWindow);

	windowSetVisible(dialogWindow, 1);

	while (1)
	{
		// Check for our OK button
		status = windowComponentEventGet(okButton, &event);
		if ((status > 0) && (event.type == EVENT_MOUSE_LEFTUP))
		{
			status = windowComponentGetSelected(langList, &selected);
			if (status >= 0)
			{
				strncpy(pickedLanguage, langs[selected].text, 6);
				status = 0;
			}
			break;
		}

		// Check for the cancel button
		status = windowComponentEventGet(cancelButton, &event);
		if ((status < 0) ||
			((status > 0) && (event.type == EVENT_MOUSE_LEFTUP)))
		{
			status = ERR_CANCELLED;
			break;
		}

		// Check for window events
		status = windowComponentEventGet(dialogWindow, &event);
		if (status > 0)
		{
			if (event.type == EVENT_WINDOW_CLOSE)
			{
				status = ERR_CANCELLED;
				break;
			}
		}

		// Not finished yet
		multitaskerYield();
	}

out:
	if (langs)
	{
		for (count = 0; count < numLangs; count ++)
			imageFree(&langs[count].iconImage);

		free(langs);
	}

	if (dialogWindow)
		windowDestroy(dialogWindow);

	return (status);
}

