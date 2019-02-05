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
//  windowRadioDialog.c
//

// This contains functions for user programs to operate GUI components.

#include <libintl.h>
#include <string.h>
#include <sys/api.h>
#include <sys/errors.h>
#include <sys/window.h>

#define _(string) gettext(string)

extern int libwindow_initialized;
extern void libwindowInitialize(void);


/////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////
//
// Below here, the functions are exported for external use
//
/////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////

_X_ int windowNewRadioDialog(objectKey parentWindow, const char *title, const char *message, char *choiceStrings[], int numChoices, int defaultChoice)
{
	// Desc: Create a dialog window with a radio button widget with the parent window 'parentWindow', the given titlebar text and main message, and 'numChoices' choices, as specified by the 'choiceStrings'.  'default' is the default focussed selection.  The dialog's radio button widget will have items for each choice.  If the user chooses one of the choices, the function returns the 0-based index of the choice.  Otherwise it returns negative.  If 'parentWindow' is NULL, the dialog box is actually created as an independent window that looks the same as a dialog.  This is a blocking call that returns when the user closes the dialog window (i.e. the dialog is 'modal').

	int status = 0;
	objectKey dialogWindow = NULL;
	objectKey container = NULL;
	image iconImage;
	objectKey radioButton = NULL;
	objectKey buttonContainer = NULL;
	objectKey okButton = NULL;
	objectKey cancelButton = NULL;
	componentParameters params;
	windowEvent event;
	int choice = ERR_INVALID;

	if (!libwindow_initialized)
		libwindowInitialize();

	// Check params.  It's okay for parentWindow to be NULL.
	if (!title || !message || !choiceStrings)
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
		windowDestroy(dialogWindow);
		return (status = ERR_NOCREATE);
	}

	params.gridHeight = 2;
	params.padLeft = 0;
	params.padTop = 0;
	params.orientationX = orient_right;
	params.orientationY = orient_top;
	params.flags = (WINDOW_COMPFLAG_FIXEDWIDTH | WINDOW_COMPFLAG_FIXEDHEIGHT);

	// Try to load the 'question' image
	status = imageLoad(QUESTIMAGE_NAME, 64, 64, &iconImage);
	if (!status && iconImage.data)
	{
		iconImage.transColor.green = 0xFF;
		windowNewImage(container, &iconImage, draw_alphablend, &params);
		imageFree(&iconImage);
	}

	// Create the label
	params.gridX++;
	params.gridHeight = 1;
	params.padRight = 0;
	params.orientationX = orient_left;
	if (!windowNewTextLabel(container, message, &params))
	{
		windowDestroy(dialogWindow);
		return (status = ERR_NOCREATE);
	}

	// Create the radio button
	params.gridY++;
	radioButton = windowNewRadioButton(container, numChoices, 1, choiceStrings,
		numChoices, &params);
	if (!radioButton)
	{
		windowDestroy(dialogWindow);
		return (status = ERR_NOCREATE);
	}

	// Create the container for the buttons
	params.gridX = 0;
	params.gridY++;
	params.gridWidth = 2;
	params.padLeft = 0;
	params.padRight = 0;
	params.padTop = 0;
	params.padBottom = 0;
	params.orientationX = orient_center;
	buttonContainer = windowNewContainer(container, "buttonContainer",
		&params);
	if (!buttonContainer)
	{
		windowDestroy(dialogWindow);
		return (status = ERR_NOCREATE);
	}

	// Create the OK button
	params.gridWidth = 1;
	params.padLeft = 2;
	params.padRight = 2;
	params.orientationX = orient_right;
	okButton = windowNewButton(buttonContainer, _("OK"), NULL, &params);
	if (!okButton)
	{
		windowDestroy(dialogWindow);
		return (status = ERR_NOCREATE);
	}

	windowComponentFocus(okButton);

	// Create the Cancel button
	params.gridX++;
	params.orientationX = orient_left;
	cancelButton = windowNewButton(buttonContainer, _("Cancel"), NULL,
		&params);
	if (!cancelButton)
	{
		windowDestroy(dialogWindow);
		return (status = ERR_NOCREATE);
	}

	if ((defaultChoice >= 0) && (defaultChoice < numChoices))
		windowComponentSetSelected(radioButton, defaultChoice);

	if (parentWindow)
		windowCenterDialog(parentWindow, dialogWindow);

	windowSetVisible(dialogWindow, 1);

	while (1)
	{
		// Check for the OK button
		if ((windowComponentEventGet(okButton, &event) > 0) &&
			(event.type == EVENT_MOUSE_LEFTUP))
		{
			status = windowComponentGetSelected(radioButton, &choice);
			if (status < 0)
				choice = status;
			break;
		}

		// Check for our Cancel button or window close events
		if (((windowComponentEventGet(dialogWindow, &event) > 0) &&
				(event.type == EVENT_WINDOW_CLOSE)) ||
			((windowComponentEventGet(cancelButton, &event) > 0) &&
				(event.type == EVENT_MOUSE_LEFTUP)))
		{
			choice = ERR_CANCELLED;
			break;
		}

		// Not finished yet
		multitaskerYield();
	}

	windowDestroy(dialogWindow);
	return (choice);
}

