//
//  Visopsys
//  Copyright (C) 1998-2018 J. Andrew McLaughlin
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

typedef struct {
	objectKey window;
	char cwd[MAX_PATH_LENGTH];
	fileType selectType;
	objectKey thumbImage;
	int doImageThumbs;
	objectKey textField;
	windowFileList *fileList;
	objectKey locationField;

} fileDialog;

static fileDialog *dialog = NULL;


static void doFileSelection(windowFileList *fileList, file *theFile,
	char *fullName, loaderFileClass *loaderClass)
{
	if (fileList != dialog->fileList)
		return;

	// Is this an image we're supposed to show a thumbnail for?
	if (dialog->doImageThumbs)
	{
		if ((theFile->type == fileT) &&
			(loaderClass->type & LOADERFILECLASS_IMAGE))
		{
			windowSwitchPointer(dialog->window, MOUSE_POINTER_BUSY);
			windowThumbImageUpdate(dialog->thumbImage, fullName,
				MAX_IMAGE_DIMENSION, MAX_IMAGE_DIMENSION, 0 /* no stretch */,
				&COLOR_WHITE);
			windowSwitchPointer(dialog->window, MOUSE_POINTER_DEFAULT);
		}
		else
		{
			windowThumbImageUpdate(dialog->thumbImage, NULL,
				MAX_IMAGE_DIMENSION, MAX_IMAGE_DIMENSION, 0 /* no stretch */,
				&COLOR_WHITE);
		}
	}

	if (((dialog->selectType == unknownT) && (theFile->type != dirT)) ||
		(theFile->type == dialog->selectType))
	{
		windowComponentSetData(dialog->textField, theFile->name,
			(strlen(theFile->name) + 1), 1 /* redraw */);
	}

	// Did we change directory?
	if (theFile->type == dirT)
	{
		windowComponentSetData(dialog->textField, "", 1, 1 /* redraw */);

		strncpy(dialog->cwd, fullName, MAX_PATH_LENGTH);
		windowComponentSetData(dialog->locationField, dialog->cwd,
			strlen(dialog->cwd), 1 /* redraw */);
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
	// Desc: Create a 'file' dialog box, with the parent window 'parentWindow', and the given titlebar text and main message.  If 'startDir' is a non-NULL directory name, the dialog will initially display the contents of that directory.  If 'fileName' contains data (i.e. the string's first character is non-NULL), the file name field of the dialog will contain that string.  The 'type' argument specifies whether the user is expected to select a file (fileT) or directory (dirT) or any (unknownT).  If 'thumb' is non-zero, an area will display image thumbnails when image files are clicked.  The dialog will have a file selection area, a file name field, an 'OK' button and a 'CANCEL' button.  If the user presses OK or ENTER, the function returns the value 1 and copies the file name into the fileName buffer.  Otherwise it returns 0 and puts a NULL string into fileName.  If 'parentWindow' is NULL, the dialog box is actually created as an independent window that looks the same as a dialog.  This is a blocking call that returns when the user closes the dialog window (i.e. the dialog is 'modal').

	int status = 0;
	componentParameters params;
	objectKey textLabel = NULL;
	char *baseName = NULL;
	objectKey okButton = NULL;
	objectKey cancelButton = NULL;
	windowEvent event;

	if (!libwindow_initialized)
		libwindowInitialize();

	// Check params.  It's okay for parentWindow to be NULL.
	if (!title || !message || !fileName)
		return (status = ERR_NULLPARAMETER);

	if (dialog)
		return (status = ERR_ALREADY);

	dialog = calloc(1, sizeof(fileDialog));
	if (!dialog)
		return (status = ERR_MEMORY);

	// Create the dialog.
	if (parentWindow)
		dialog->window = windowNewDialog(parentWindow, title);
	else
		dialog->window = windowNew(multitaskerGetCurrentProcessId(), title);

	if (!dialog->window)
	{
		status = ERR_NOCREATE;
		goto out;
	}

	if (startDir)
		strncpy(dialog->cwd, startDir, MAX_PATH_LENGTH);
	else
		multitaskerGetCurrentDirectory(dialog->cwd, MAX_PATH_LENGTH);

	// Record the type of file the user is supposed to select
	dialog->selectType = type;

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
		dialog->thumbImage = windowNewThumbImage(dialog->window, NULL,
			MAX_IMAGE_DIMENSION, MAX_IMAGE_DIMENSION, 0 /* no stretch */,
			&params);
		if (!dialog->thumbImage)
		{
			status = ERR_NOCREATE;
			goto out;
		}

		dialog->doImageThumbs = 1;
	}

	params.gridX++;
	params.gridWidth = 2;
	params.gridHeight = 1;
	params.orientationX = orient_left;
	params.orientationY = orient_top;
	params.flags &= ~(WINDOW_COMPFLAG_CUSTOMBACKGROUND |
		WINDOW_COMPFLAG_HASBORDER);
	textLabel = windowNewTextLabel(dialog->window, message, &params);
	if (!textLabel)
	{
		status = ERR_NOCREATE;
		goto out;
	}

	// Create the location text field
	params.gridY++;
	dialog->locationField = windowNewTextField(dialog->window, 30, &params);
	if (!dialog->locationField)
	{
		status = ERR_NOCREATE;
		goto out;
	}

	windowComponentSetData(dialog->locationField, dialog->cwd,
		strlen(dialog->cwd), 0 /* no redraw */);
	windowComponentSetEnabled(dialog->locationField, 0); // For now

	// Create the file list widget
	params.gridY++;
	params.flags &= ~WINDOW_COMPFLAG_FIXEDHEIGHT;
	dialog->fileList = windowNewFileList(dialog->window, windowlist_icononly,
		3, 4, dialog->cwd, WINFILEBROWSE_CAN_CD, doFileSelection, &params);
	if (!dialog->fileList)
	{
		status = ERR_NOCREATE;
		goto out;
	}

	windowComponentFocus(dialog->fileList->key);

	// Create the text field for the user to type.
	params.gridY++;
	params.flags |= WINDOW_COMPFLAG_FIXEDHEIGHT;
	dialog->textField = windowNewTextField(dialog->window, 30, &params);
	if (!dialog->textField)
	{
		status = ERR_NOCREATE;
		goto out;
	}

	if (fileName[0])
	{
		baseName = basename(fileName);
		if (baseName)
		{
			windowComponentSetData(dialog->textField, baseName,
				MAX_PATH_LENGTH, 0 /* no redraw */);
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
	okButton = windowNewButton(dialog->window, _("OK"), NULL, &params);
	if (!okButton)
	{
		status = ERR_NOCREATE;
		goto out;
	}

	// Create the Cancel button
	params.gridX++;
	params.orientationX = orient_left;
	cancelButton = windowNewButton(dialog->window, _("Cancel"), NULL,
		&params);
	if (!cancelButton)
	{
		status = ERR_NOCREATE;
		goto out;
	}

	windowCenterDialog(parentWindow, dialog->window);

	windowSetVisible(dialog->window, 1);

	while (1)
	{
		// Check for events to be passed to the file list widget
		if (windowComponentEventGet(dialog->fileList->key, &event) > 0)
			dialog->fileList->eventHandler(dialog->fileList, &event);

		// Check for the OK button, or 'enter' in the text field
		if (((windowComponentEventGet(okButton, &event) > 0) &&
				(event.type == EVENT_MOUSE_LEFTUP)) ||
			((windowComponentEventGet(dialog->textField, &event) > 0) &&
				(event.type == EVENT_KEY_DOWN) && (event.key == keyEnter)))
		{
			windowComponentGetData(dialog->textField, fileName, maxLength);

			if ((type == fileT) && !fileName[0])
			{
				status = 0;
			}
			else
			{
				if (strcmp(dialog->cwd, "/"))
					snprintf(fileName, maxLength, "%s/", dialog->cwd);
				else
					strncpy(fileName, dialog->cwd, maxLength);

				if ((type == unknownT) || (type == fileT))
				{
					windowComponentGetData(dialog->textField, (fileName +
						strlen(fileName)), (maxLength - strlen(fileName)));
				}

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
		status = windowComponentEventGet(dialog->window, &event);
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
	if (dialog->fileList && dialog->fileList->destroy)
		dialog->fileList->destroy(dialog->fileList);

	if (dialog->window)
		windowDestroy(dialog->window);

	if (dialog)
	{
		free(dialog);
		dialog = NULL;
	}

	return (status);
}

