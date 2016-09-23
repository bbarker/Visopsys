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
//  windowOkDialog.c
//

// This contains functions for user programs to operate GUI components.

#include <libintl.h>
#include <string.h>
#include <sys/window.h>
#include <sys/api.h>
#include <sys/errors.h>

#define _(string) gettext(string)

typedef enum {
	infoDialog, errorDialog

} dialogType;

extern int libwindow_initialized;
extern void libwindowInitialize(void);


static int okDialog(dialogType type, objectKey parentWindow, const char *title,
	const char *message)
{
	// This will make a simple "OK" dialog message, and wait until the button
	// has been pressed.

	int status = 0;
	objectKey dialogWindow = NULL;
	objectKey container = NULL;
	image iconImage;
	objectKey mainLabel = NULL;
	objectKey okButton = NULL;
	componentParameters params;
	windowEvent event;

	if (!libwindow_initialized)
		libwindowInitialize();

	// Check params.  It's okay for parentWindow to be NULL.
	if (!title || !message)
		return (status = ERR_NULLPARAMETER);

	// Create the dialog.  Arbitrary size and coordinates
	if (parentWindow)
		dialogWindow = windowNewDialog(parentWindow, title);
	else
		dialogWindow = windowNew(multitaskerGetCurrentProcessId(), title);

	if (!dialogWindow)
		return (status = ERR_NOCREATE);

	memset(&params, 0, sizeof(componentParameters));
	params.gridWidth = 1;
	params.gridHeight = 1;
	params.padLeft = 5;
	params.padRight = 5;
	params.padTop = 5;
	params.padBottom = 5;
	params.orientationX = orient_center;
	params.orientationY = orient_middle;

	// Get a container to pack everything into
	container = windowNewContainer(dialogWindow, "container", &params);
	if (!container)
	{
		status = ERR_NOCREATE;
		goto out;
	}

	params.padLeft = 0;
	params.padTop = 0;
	params.orientationX = orient_right;
	params.orientationY = orient_top;
	params.flags = (WINDOW_COMPFLAG_FIXEDWIDTH | WINDOW_COMPFLAG_FIXEDHEIGHT);

	// If our 'info' or 'error' image hasn't been loaded, try to load it
	if (type == infoDialog)
		status = imageLoad(INFOIMAGE_NAME, 64, 64, &iconImage);
	else
		status = imageLoad(ERRORIMAGE_NAME, 64, 64, &iconImage);

	if (!status && iconImage.data)
	{
		iconImage.transColor.green = 0xFF;
		windowNewImage(container, &iconImage, draw_alphablend, &params);
	}

	// Create the label
	params.gridX++;
	params.padRight = 0;
	params.orientationX = orient_left;
	mainLabel = windowNewTextLabel(container, message, &params);
	if (!mainLabel)
	{
		status = ERR_NOCREATE;
		goto out;
	}

	// Create the button
	params.gridX = 0;
	params.gridY++;
	params.gridWidth = 2;
	params.padBottom = 0;
	params.orientationX = orient_center;
	okButton = windowNewButton(container, _("OK"), NULL, &params);
	if (!okButton)
	{
		status = ERR_NOCREATE;
		goto out;
	}

	windowComponentFocus(okButton);

	// If there's no parentWindow, the dialog will center on the screen
	windowCenterDialog(parentWindow, dialogWindow);

	windowSetVisible(dialogWindow, 1);

	while (1)
	{
		// Check for our OK button
		status = windowComponentEventGet(okButton, &event);
		if ((status < 0) || ((status > 0) &&
			(event.type == EVENT_MOUSE_LEFTUP)))
		{
			break;
		}

		// Check for window close events
		status = windowComponentEventGet(dialogWindow, &event);
		if ((status < 0) || ((status > 0) &&
			(event.type == EVENT_WINDOW_CLOSE)))
		{
			break;
		}

		// Not finished yet
		multitaskerYield();
	}

	status = 0;

out:
	if (dialogWindow)
		windowDestroy(dialogWindow);

	return (status);
}


/////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////
//
// Below here, the functions are exported for external use
//
/////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////

_X_ int windowNewInfoDialog(objectKey parentWindow, const char *title, const char *message)
{
	// Desc: Create an 'info' dialog box, with the parent window 'parentWindow', and the given titlebar text and main message.  The dialog will have a single 'OK' button for the user to acknowledge.  If 'parentWindow' is NULL, the dialog box is actually created as an independent window that looks the same as a dialog.  This is a blocking call that returns when the user closes the dialog window (i.e. the dialog is 'modal').
	return (okDialog(infoDialog, parentWindow, title, message));
}


_X_ int windowNewErrorDialog(objectKey parentWindow, const char *title, const char *message)
{
	// Desc: Create an 'error' dialog box, with the parent window 'parentWindow', and the given titlebar text and main message.  The dialog will have a single 'OK' button for the user to acknowledge.  If 'parentWindow' is NULL, the dialog box is actually created as an independent window that looks the same as a dialog.  This is a blocking call that returns when the user closes the dialog window (i.e. the dialog is 'modal').
	return (okDialog(errorDialog, parentWindow, title, message));
}

