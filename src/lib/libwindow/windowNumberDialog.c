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
//  windowNumberDialog.c
//

// This contains functions for user programs to operate GUI components.

#include <libintl.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/api.h>
#include <sys/ascii.h>
#include <sys/errors.h>
#include <sys/window.h>

#define _(string) gettext(string)

typedef enum {
	promptDialog, passwordDialog
} dialogType;


extern int libwindow_initialized;
extern void libwindowInitialize(void);


static inline int decDigits(int max)
{
	int digits = 0;

	if (max < 0)
		digits += 1;

	while (max)
	{
		max /= 10;
		digits += 1;
	}

	return (digits);
}


/////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////
//
// Below here, the functions are exported for external use
//
/////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////

_X_ int windowNewNumberDialog(objectKey parentWindow, const char *title, const char *message, int minVal, int maxVal, int defaultVal, int *value)
{
	// Desc: Create a 'number' dialog box, with the parent window 'parentWindow', and the given titlebar text and main message.  The dialog will have a text field for the user to enter data using the keyboard, and a slider component for adjusting it with the mouse.  Minimum, maximum, and default values should be supplied.  If 'parentWindow' is NULL, the dialog box is actually created as an independent window that looks the same as a dialog.  This is a blocking call that returns when the user closes the dialog window (i.e. the dialog is 'modal').

	int status = 0;
	int columns = 0;
	char *buffer = NULL;
	objectKey dialogWindow = NULL;
	objectKey container = NULL;
	objectKey field = NULL;
	objectKey slider = NULL;
	scrollBarState sliderState;
	objectKey okButton = NULL;
	objectKey cancelButton = NULL;
	componentParameters params;
	windowEvent event;

	if (!libwindow_initialized)
		libwindowInitialize();

	// Check params.  It's okay for parentWindow to be NULL.
	if (!title || !message || !value)
		return (status = ERR_NULLPARAMETER);

	if ((minVal > maxVal) || (defaultVal < minVal) || (defaultVal > maxVal))
		return (status = ERR_RANGE);

	// How many columns do we need for our text field?
	columns = (max(max(2, decDigits(minVal)), decDigits(maxVal)) + 1);

	buffer = malloc(columns + 1);
	if (!buffer)
		return (status = ERR_MEMORY);

	// Create the dialog.
	if (parentWindow)
		dialogWindow = windowNewDialog(parentWindow, title);
	else
		dialogWindow = windowNew(multitaskerGetCurrentProcessId(), title);

	if (!dialogWindow)
	{
		status = ERR_NOCREATE;
		goto out;
	}

	memset(&params, 0, sizeof(componentParameters));
	params.gridWidth = 1;
	params.gridHeight = 1;
	params.padLeft = 5;
	params.padRight = 5;
	params.padTop = 5;
	params.padBottom = 5;
	params.orientationX = orient_center;
	params.orientationY = orient_top;

	// Get a container to pack everything into
	container = windowNewContainer(dialogWindow, "container", &params);
	if (!container)
	{
		status = ERR_NOCREATE;
		goto out;
	}

	// Add a label with the prompt
	params.gridWidth = 2;
	params.padLeft = 0;
	params.padRight = 0;
	params.padTop = 0;
	params.orientationX = orient_left;
	params.flags = (WINDOW_COMPFLAG_FIXEDWIDTH | WINDOW_COMPFLAG_FIXEDHEIGHT);
	windowNewTextLabel(container, message, &params);

	// Add a text field for the value
	params.gridY++;
	params.flags = WINDOW_COMPFLAG_FIXEDHEIGHT;
	field = windowNewTextField(container, columns, &params);
	sprintf(buffer, "%d", defaultVal);
	windowComponentSetData(field, buffer, columns, 1 /* redraw */);
	windowComponentFocus(field);

	// Add a slider to adjust the value mouse-ly
	params.gridY++;
	params.flags = 0;
	slider = windowNewSlider(container, scrollbar_horizontal, 0, 0, &params);
	sliderState.displayPercent = 20; // Size of slider 20%
	sliderState.positionPercent = ((maxVal - minVal)?
		(((defaultVal - minVal) * 100) / (maxVal - minVal)) : 50);
	windowComponentSetData(slider, &sliderState, sizeof(scrollBarState),
		1 /* redraw */);

	// Create the OK button
	params.gridY++;
	params.gridWidth = 1;
	params.padLeft = 2;
	params.padRight = 2;
	params.padBottom = 0;
	params.orientationX = orient_right;
	params.flags = (WINDOW_COMPFLAG_FIXEDWIDTH | WINDOW_COMPFLAG_FIXEDHEIGHT);
	okButton = windowNewButton(container, _("OK"), NULL, &params);

	// Create the Cancel button
	params.gridX++;
	params.orientationX = orient_left;
	cancelButton = windowNewButton(container, _("Cancel"), NULL, &params);

	if (parentWindow)
		windowCenterDialog(parentWindow, dialogWindow);

	windowSetVisible(dialogWindow, 1);

	while (1)
	{
		while (1)
		{
			// Check for keyboard events
			if ((windowComponentEventGet(field, &event) > 0) &&
				(event.type == EVENT_KEY_DOWN))
			{
				if (event.key == keyEnter)
				{
					status = 0;
					break;
				}

				// See if we can apply a newly-typed number to the slider
				buffer[0] = '\0';
				windowComponentGetData(field, buffer, columns);
				*value = atoi(buffer);
				if ((*value >= minVal) && (*value <= maxVal))
				{
					sliderState.positionPercent =
						(((*value - minVal) * 100) / (maxVal - minVal));
					windowComponentSetData(slider, &sliderState,
						sizeof(scrollBarState), 1 /* redraw */);
				}
			}

			// Check for slider changes
			if (windowComponentEventGet(slider, &event) > 0)
			{
				if (event.type & (EVENT_MOUSE_LEFTDOWN | EVENT_MOUSE_DRAG |
					EVENT_KEY_DOWN))
				{
					windowComponentGetData(slider, &sliderState,
						sizeof(scrollBarState));
					sprintf(buffer, "%d", (((sliderState.positionPercent *
						(maxVal - minVal)) / 100) + minVal));
					windowComponentSetData(field, buffer, columns,
						1 /* redraw */);
				}
			}

			// Check for the OK button
			if ((windowComponentEventGet(okButton, &event) > 0) &&
				(event.type == EVENT_MOUSE_LEFTUP))
			{
				status = 0;
				break;
			}

			// Check for the Cancel button and window close events
			if (((windowComponentEventGet(cancelButton, &event) > 0) &&
					(event.type == EVENT_MOUSE_LEFTUP)) ||
				((windowComponentEventGet(dialogWindow, &event) > 0) &&
					(event.type == EVENT_WINDOW_CLOSE)))
			{
				status = ERR_CANCELLED;
				break;
			}

			// Not finished yet
			multitaskerYield();
		}

		if (status < 0)
			break;

		status = windowComponentGetData(field, buffer, columns);
		if (status < 0)
			break;

		buffer[columns] = '\0';
		*value = atoi(buffer);

		if ((*value >= minVal) && (*value <= maxVal))
			break;
	}

out:
	if (buffer)
		free(buffer);

	if (dialogWindow)
		windowDestroy(dialogWindow);

	return (status);
}

