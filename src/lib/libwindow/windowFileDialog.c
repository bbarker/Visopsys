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
//  windowFileDialog.c
//

// This contains functions for user programs to operate GUI components.

#include <libgen.h>
#include <libintl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/api.h>
#include <sys/window.h>

#define _(string) gettext(string)
#define MAX_IMAGE_DIMENSION 128

extern int libwindow_initialized;
extern void libwindowInitialize(void);

static char cwd[MAX_PATH_LENGTH];
static fileType selectType = unknownT;
static int doImageThumbs = 0;
static objectKey dialogWindow = NULL;
static objectKey thumbImage = NULL;
static objectKey locationField = NULL;
static objectKey textField = NULL;


static void doFileSelection(file *theFile, char *fullName,
	loaderFileClass *loaderClass)
{
	// Is this an image we're supposed to show a thumbnail for?
	if (doImageThumbs)
	{
		if ((theFile->type == fileT) &&
			(loaderClass->class & LOADERFILECLASS_IMAGE))
		{
			windowSwitchPointer(dialogWindow, MOUSE_POINTER_BUSY);
			windowThumbImageUpdate(thumbImage, fullName, MAX_IMAGE_DIMENSION,
				MAX_IMAGE_DIMENSION, 0 /* no stretch */, &COLOR_WHITE);
			windowSwitchPointer(dialogWindow, MOUSE_POINTER_DEFAULT);
		}
		else
		{
			windowThumbImageUpdate(thumbImage, NULL, MAX_IMAGE_DIMENSION,
				MAX_IMAGE_DIMENSION, 0 /* no stretch */, &COLOR_WHITE);
		}
	}

	if (theFile->type == selectType)
		windowComponentSetData(textField, theFile->name,
			(strlen(theFile->name) + 1), 1 /* redraw */);

	// Did we change directory?
	if (theFile->type == dirT)
	{
		strncpy(cwd, fullName, MAX_PATH_LENGTH);
		windowComponentSetData(locationField, cwd, strlen(cwd),
			1 /* redraw */);
	}
}


/////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////
//
// Below here, the functions are exported for external use
//
/////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////

_X_ int windowNewFileDialog(objectKey parentWindow, const char *title, const char *message, const char *startDir, char *fileName, unsigned maxLength, fileType type, int thumb)
{
	// Desc: Create a 'file' dialog box, with the parent window 'parentWindow', and the given titlebar text and main message.  If 'startDir' is a non-NULL directory name, the dialog will initially display the contents of that directory.  If 'fileName' contains data (i.e. the string's first character is non-NULL), the file name field of the dialog will contain that string.  The 'type' argument specifies whether the user is expected to select a file (fileT) or directory (dirT).  If 'thumb' is non-zero, an area will display image thumbnails when image files are clicked.  The dialog will have a file selection area, a file name field, an 'OK' button and a 'CANCEL' button.  If the user presses OK or ENTER, the function returns the value 1 and copies the file name into the fileName buffer.  Otherwise it returns 0 and puts a NULL string into fileName.  If 'parentWindow' is NULL, the dialog box is actually created as an independent window that looks the same as a dialog.  This is a blocking call that returns when the user closes the dialog window (i.e. the dialog is 'modal').

	int status = 0;
	componentParameters params;
	objectKey textLabel = NULL;
	windowFileList *fileList = NULL;
	char *baseName = NULL;
	objectKey okButton = NULL;
	objectKey cancelButton = NULL;
	windowEvent event;

	if (!libwindow_initialized)
		libwindowInitialize();

	// Check params.  It's okay for parentWindow to be NULL.
	if (!title || !message || !fileName)
		return (status = ERR_NULLPARAMETER);

	if (startDir)
		strncpy(cwd, startDir, MAX_PATH_LENGTH);
	else
		multitaskerGetCurrentDirectory(cwd, MAX_PATH_LENGTH);

	// Create the dialog.
	if (parentWindow)
		dialogWindow = windowNewDialog(parentWindow, title);
	else
		dialogWindow = windowNew(multitaskerGetCurrentProcessId(), title);

	if (!dialogWindow)
		return (status = ERR_NOCREATE);

	// Record the type of file the user is supposed to select
	selectType = type;

	memset(&params, 0, sizeof(componentParameters));
	params.gridWidth = 1;
	params.gridHeight = 4;
	params.padLeft = 5;
	params.padRight = 5;
	params.padTop = 5;
	params.orientationX = orient_center;
	params.orientationY = orient_middle;
	params.flags = WINDOW_COMPFLAG_FIXEDHEIGHT;

	if (thumb)
	{
		params.flags |= (WINDOW_COMPFLAG_CUSTOMBACKGROUND |
			WINDOW_COMPFLAG_HASBORDER);
		params.background = COLOR_WHITE;
		thumbImage = windowNewThumbImage(dialogWindow, NULL,
			MAX_IMAGE_DIMENSION, MAX_IMAGE_DIMENSION, 0 /* no stretch */,
			&params);
		if (!thumbImage)
			return (status = ERR_NOCREATE);

		doImageThumbs = 1;
	}

	params.gridX++;
	params.gridWidth = 2;
	params.gridHeight = 1;
	params.orientationX = orient_left;
	params.orientationY = orient_top;
	params.flags &= ~(WINDOW_COMPFLAG_CUSTOMBACKGROUND |
		WINDOW_COMPFLAG_HASBORDER);
	textLabel = windowNewTextLabel(dialogWindow, message, &params);
	if (!textLabel)
	{
		status = ERR_NOCREATE;
		goto out;
	}

	// Create the location text field
	params.gridY++;
	locationField = windowNewTextField(dialogWindow, 30, &params);
	if (!locationField)
	{
		status = ERR_NOCREATE;
		goto out;
	}

	windowComponentSetData(locationField, cwd, strlen(cwd), 1 /* redraw */);
	windowComponentSetEnabled(locationField, 0); // For now

	// Create the file list widget
	params.gridY++;
	params.flags &= ~WINDOW_COMPFLAG_FIXEDHEIGHT;
	fileList = windowNewFileList(dialogWindow, windowlist_icononly, 3, 4,
		cwd, WINFILEBROWSE_CAN_CD, doFileSelection, &params);
	if (!fileList)
	{
		status = ERR_NOCREATE;
		goto out;
	}

	windowComponentFocus(fileList->key);

	// Create the text field for the user to type.
	params.gridY++;
	params.flags |= WINDOW_COMPFLAG_FIXEDHEIGHT;
	textField = windowNewTextField(dialogWindow, 30, &params);
	if (!textField)
	{
		status = ERR_NOCREATE;
		goto out;
	}

	if (fileName[0])
	{
		baseName = basename(fileName);
		if (baseName)
		{
			windowComponentSetData(textField, baseName, MAX_PATH_LENGTH,
				1 /* redraw */);
			free(baseName);
		}
	}

	// Create the OK button
	params.gridY++;
	params.gridWidth = 1;
	params.padLeft = 2;
	params.padRight = 2;
	params.padBottom = 5;
	params.orientationX = orient_right;
	params.flags |= WINDOW_COMPFLAG_FIXEDWIDTH;
	okButton = windowNewButton(dialogWindow, _("OK"), NULL, &params);
	if (!okButton)
	{
		status = ERR_NOCREATE;
		goto out;
	}

	// Create the Cancel button
	params.gridX++;
	params.orientationX = orient_left;
	cancelButton = windowNewButton(dialogWindow, _("Cancel"), NULL, &params);
	if (!cancelButton)
	{
		status = ERR_NOCREATE;
		goto out;
	}

	if (parentWindow)
		windowCenterDialog(parentWindow, dialogWindow);

	windowSetVisible(dialogWindow, 1);

	while (1)
	{
		// Check for events to be passed to the file list widget
		if (windowComponentEventGet(fileList->key, &event) > 0)
			fileList->eventHandler(fileList, &event);

		// Check for the OK button, or 'enter' in the text field
		if (((windowComponentEventGet(okButton, &event) > 0) &&
				(event.type == EVENT_MOUSE_LEFTUP)) ||
			((windowComponentEventGet(textField, &event) > 0) &&
				(event.type == EVENT_KEY_DOWN) && (event.key == keyEnter)))
		{
			windowComponentGetData(textField, fileName, maxLength);
			if (fileName[0] == '\0')
			{
				status = 0;
			}
			else
			{
				if (strcmp(cwd, "/"))
					snprintf(fileName, maxLength, "%s/", cwd);
				else
					strncpy(fileName, cwd, maxLength);

				if (type == fileT)
					windowComponentGetData(textField,
						(fileName + strlen(fileName)),
						(maxLength - strlen(fileName)));
				status = 1;
			}

			break;
		}

		// Check for the Cancel button
		status = windowComponentEventGet(cancelButton, &event);
		if ((status < 0) ||
			((status > 0) && (event.type == EVENT_MOUSE_LEFTUP)))
		{
			fileName[0] = '\0';
			status = 0;
			break;
		}

		// Check for window close events
		status = windowComponentEventGet(dialogWindow, &event);
		if ((status < 0) ||
			((status > 0) && (event.type == EVENT_WINDOW_CLOSE)))
		{
			fileName[0] = '\0';
			status = 0;
			break;
		}

		// Not finished yet
		multitaskerYield();
	}

out:

	if (fileList)
		fileList->destroy(fileList);

	if (dialogWindow)
		windowDestroy(dialogWindow);

	return (status);
}

