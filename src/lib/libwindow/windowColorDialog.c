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
//  windowColorDialog.c
//

// This contains functions for user programs to operate GUI components.

#include <libintl.h>
#include <string.h>
#include <stdio.h>
#include <sys/api.h>
#include <sys/errors.h>
#include <sys/window.h>

#define _(string) gettext(string)
#define TITLE			_("Color Chooser")
#define CANVAS_WIDTH	35
#define CANVAS_HEIGHT	100
#define SLIDER_WIDTH	100

extern int libwindow_initialized;
extern void libwindowInitialize(void);


static void drawColor(objectKey canvas, objectKey redLabel,
	objectKey greenLabel, objectKey blueLabel, color *draw)
{
	// Draw the current color on the canvas

	windowDrawParameters params;
	char colorString[4];

	// Clear our drawing parameters
	memset(&params, 0, sizeof(windowDrawParameters));
	params.operation = draw_rect;
	params.mode = draw_normal;
	params.foreground.red = draw->red;
	params.foreground.green = draw->green;
	params.foreground.blue = draw->blue;
	params.xCoord1 = 0;
	params.yCoord1 = 0;
	params.width = windowComponentGetWidth(canvas);
	params.height = windowComponentGetHeight(canvas);
	params.thickness = 1;
	params.fill = 1;
	windowComponentSetData(canvas, &params, sizeof(windowDrawParameters),
		1 /* redraw */);

	sprintf(colorString, "%03d", draw->red);
	windowComponentSetData(redLabel, colorString, 4, 1 /* redraw */);
	sprintf(colorString, "%03d", draw->green);
	windowComponentSetData(greenLabel, colorString, 4, 1 /* redraw */);
	sprintf(colorString, "%03d", draw->blue);
	windowComponentSetData(blueLabel, colorString, 4, 1 /* redraw */);
}


/////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////
//
// Below here, the functions are exported for external use
//
/////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////

_X_ int windowNewColorDialog(objectKey parentWindow, color *pickedColor)
{
	// Desc: Create an 'color chooser' dialog box, with the parent window 'parentWindow', and a pointer to the color structure 'pickedColor'.  Currently the window consists of red/green/blue sliders and a canvas displaying the current color.  The initial color displayed will be whatever is supplied in 'pickedColor'.  If 'parentWindow' is NULL, the dialog box is actually created as an independent window that looks the same as a dialog.  This is a blocking call that returns when the user closes the dialog window (i.e. the dialog is 'modal').

	int status = 0;
	objectKey dialogWindow = NULL;
	objectKey canvas = NULL;
	objectKey sliderContainer = NULL;
	objectKey redLabel = NULL;
	objectKey redSlider = NULL;
	objectKey greenLabel = NULL;
	objectKey greenSlider = NULL;
	objectKey blueLabel = NULL;
	objectKey blueSlider = NULL;
	objectKey buttonContainer = NULL;
	objectKey okButton = NULL;
	objectKey cancelButton = NULL;
	componentParameters params;
	windowEvent event;
	color tmpColor;
	scrollBarState scrollState;

	if (!libwindow_initialized)
		libwindowInitialize();

	// Check params.  It's okay for parentWindow to be NULL.
	if (!pickedColor)
		return (status = ERR_NULLPARAMETER);

	// Create the dialog.  Arbitrary size and coordinates
	if (parentWindow)
		dialogWindow = windowNewDialog(parentWindow, TITLE);
	else
		dialogWindow = windowNew(multitaskerGetCurrentProcessId(), TITLE);

	if (!dialogWindow)
		return (status = ERR_NOCREATE);

	// Copy the current color into the temporary color
	memcpy(&tmpColor, pickedColor, sizeof(color));

	memset(&params, 0, sizeof(componentParameters));
	params.gridWidth = 1;
	params.gridHeight = 1;
	params.padLeft = 5;
	params.padRight = 5;
	params.padTop = 5;
	params.orientationX = orient_left;
	params.orientationY = orient_top;
	params.flags = WINDOW_COMPFLAG_HASBORDER;

	// A canvas for drawing the color
	canvas = windowNewCanvas(dialogWindow, CANVAS_WIDTH, CANVAS_HEIGHT,
		&params);

	// Make a container for the sliders and their labels
	params.gridX++;
	sliderContainer = windowNewContainer(dialogWindow, "sliderContainer",
		&params);
	if (!sliderContainer)
	{
		status = ERR_NOCREATE;
		goto out;
	}

	// Red label, slider, and value label
	params.padLeft = 0;
	params.padTop = 0;
	params.padBottom = 5;
	params.flags = WINDOW_COMPFLAG_FIXEDHEIGHT;
	windowNewTextLabel(sliderContainer, _("Red"), &params);

	params.gridY++;
	params.flags = 0;
	redSlider = windowNewSlider(sliderContainer, scrollbar_horizontal,
		SLIDER_WIDTH, 0, &params);
	if (!redSlider)
	{
		status = ERR_NOCREATE;
		goto out;
	}

	scrollState.displayPercent = 20;
	scrollState.positionPercent = ((tmpColor.red * 100) / 255);
	windowComponentSetData(redSlider, &scrollState, sizeof(scrollBarState),
		1 /* redraw */);

	params.gridX++;
	params.orientationY = orient_middle;
	params.flags = (WINDOW_COMPFLAG_FIXEDWIDTH | WINDOW_COMPFLAG_FIXEDHEIGHT);
	redLabel = windowNewTextLabel(sliderContainer, "000", &params);
	if (!redLabel)
	{
		status = ERR_NOCREATE;
		goto out;
	}

	// Green label, slider, and value label

	params.gridX--;
	params.gridY++;
	params.orientationY = orient_top;
	windowNewTextLabel(sliderContainer, _("Green"), &params);

	params.gridY++;
	params.flags = 0;
	greenSlider = windowNewSlider(sliderContainer, scrollbar_horizontal,
		SLIDER_WIDTH, 0, &params);
	if (!greenSlider)
	{
		status = ERR_NOCREATE;
		goto out;
	}

	scrollState.positionPercent = ((tmpColor.green * 100) / 255);
	windowComponentSetData(greenSlider, &scrollState, sizeof(scrollBarState),
		1 /* redraw */);

	params.gridX++;
	params.orientationY = orient_middle;
	params.flags = (WINDOW_COMPFLAG_FIXEDWIDTH | WINDOW_COMPFLAG_FIXEDHEIGHT);
	greenLabel = windowNewTextLabel(sliderContainer, "000", &params);
	if (!greenLabel)
	{
		status = ERR_NOCREATE;
		goto out;
	}

	// Blue label, slider, and value label

	params.gridX--;
	params.gridY++;
	params.orientationY = orient_top;
	windowNewTextLabel(sliderContainer, _("Blue"), &params);

	params.gridY++;
	params.padBottom = 0;
	params.flags = 0;
	blueSlider = windowNewSlider(sliderContainer, scrollbar_horizontal,
		SLIDER_WIDTH, 0, &params);
	if (!blueSlider)
	{
		status = ERR_NOCREATE;
		goto out;
	}

	scrollState.positionPercent = ((tmpColor.blue * 100) / 255);
	windowComponentSetData(blueSlider, &scrollState, sizeof(scrollBarState),
		1 /* redraw */);

	params.gridX++;
	params.orientationY = orient_middle;
	params.flags = (WINDOW_COMPFLAG_FIXEDWIDTH | WINDOW_COMPFLAG_FIXEDHEIGHT);
	blueLabel = windowNewTextLabel(sliderContainer, "000", &params);
	if (!blueLabel)
	{
		status = ERR_NOCREATE;
		goto out;
	}

	// Make a container for the buttons
	params.gridX = 0;
	params.gridY++;
	params.gridWidth = 2;
	params.padRight = 0;
	params.padTop = 10;
	params.padBottom = 5;
	params.orientationX = orient_center;
	params.orientationY = orient_top;
	params.flags |= WINDOW_COMPFLAG_FIXEDWIDTH;
	buttonContainer = windowNewContainer(dialogWindow, "buttonContainer",
		&params);
	if (!buttonContainer)
	{
		status = ERR_NOCREATE;
		goto out;
	}

	// Create the OK button
	params.gridY = 0;
	params.gridWidth = 1;
	params.padLeft = 2;
	params.padRight = 2;
	params.padTop = 0;
	params.padBottom = 0;
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

	windowComponentFocus(cancelButton);

	if (parentWindow)
		windowCenterDialog(parentWindow, dialogWindow);

	windowSetVisible(dialogWindow, 1);

	// Draw the current color on the canvas
	drawColor(canvas, redLabel, greenLabel, blueLabel, &tmpColor);

	while (1)
	{
		// Check for sliders
		status = windowComponentEventGet(redSlider, &event);
		if (status > 0)
		{
			if (event.type & (EVENT_MOUSE_LEFTDOWN | EVENT_MOUSE_DRAG |
				EVENT_KEY_DOWN))
			{
				windowComponentGetData(redSlider, &scrollState,
					sizeof(scrollBarState));
				tmpColor.red = ((scrollState.positionPercent * 255) / 100);
				drawColor(canvas, redLabel, greenLabel, blueLabel, &tmpColor);
			}
		}

		status = windowComponentEventGet(greenSlider, &event);
		if (status > 0)
		{
			if (event.type & (EVENT_MOUSE_LEFTDOWN | EVENT_MOUSE_DRAG |
				EVENT_KEY_DOWN))
			{
				windowComponentGetData(greenSlider, &scrollState,
					sizeof(scrollBarState));
				tmpColor.green = ((scrollState.positionPercent * 255) / 100);
				drawColor(canvas, redLabel, greenLabel, blueLabel, &tmpColor);
			}
		}

		status = windowComponentEventGet(blueSlider, &event);
		if (status > 0)
		{
			if (event.type & (EVENT_MOUSE_LEFTDOWN | EVENT_MOUSE_DRAG |
				EVENT_KEY_DOWN))
			{
				windowComponentGetData(blueSlider, &scrollState,
					sizeof(scrollBarState));
				tmpColor.blue = ((scrollState.positionPercent * 255) / 100);
				drawColor(canvas, redLabel, greenLabel, blueLabel, &tmpColor);
			}
		}

		// Check for our OK button
		status = windowComponentEventGet(okButton, &event);
		if ((status > 0) && (event.type == EVENT_MOUSE_LEFTUP))
		{
			// Copy the temporary color into picked color
			memcpy(pickedColor, &tmpColor, sizeof(color));
			break;
		}

		// Check for the cancel button
		status = windowComponentEventGet(cancelButton, &event);
		if ((status < 0) || ((status > 0) &&
			(event.type == EVENT_MOUSE_LEFTUP)))
		{
			break;
		}

		// Check for window events
		status = windowComponentEventGet(dialogWindow, &event);
		if (status > 0)
		{
			if (event.type == EVENT_WINDOW_CLOSE)
				break;
			else if (event.type == EVENT_WINDOW_RESIZE)
				drawColor(canvas, redLabel, greenLabel, blueLabel, &tmpColor);
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

