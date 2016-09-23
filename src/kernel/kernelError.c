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
//  kernelError.c
//

#include "kernelError.h"
#include "kernelImage.h"
#include "kernelInterrupt.h"
#include "kernelLog.h"
#include "kernelMultitasker.h"
#include "kernelPic.h"
#include "kernelWindow.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

static char *panicConst = "PANIC";
static char *errorConst = "Error";
static char *warningConst = "Warning";
static char *messageConst = "Message";


static int errorDialogDetails(kernelWindow *parent, const char *details)
{
	int status = 0;
	kernelWindow *dialog = NULL;
	kernelWindowComponent *okButton = NULL;
	kernelWindowComponent *detailsArea = NULL;
	componentParameters params;
	windowEvent event;

	memset(&params, 0, sizeof(componentParameters));

	dialog = kernelWindowNewDialog(parent, "Error details");
	if (!dialog)
	{
		status = ERR_NOCREATE;
		goto out;
	}

	params.gridWidth = 1;
	params.gridHeight = 1;
	params.padLeft = 5;
	params.padRight = 5;
	params.padTop = 5;
	params.flags = (WINDOW_COMPFLAG_FIXEDWIDTH | WINDOW_COMPFLAG_FIXEDHEIGHT);
	params.orientationX = orient_center;
	params.orientationY = orient_middle;

	// Create the details area
	detailsArea = kernelWindowNewTextArea(dialog, 60, 25, 200, &params);
	if (!detailsArea)
	{
		status = ERR_NOCREATE;
		goto out;
	}

	kernelWindowComponentSetData(detailsArea, (void *) details,
		strlen(details), 1 /* redraw */);
	kernelTextStreamSetCursor(((kernelWindowTextArea *) detailsArea->data)
		->area->outputStream, 0);

	// Create the OK button
	params.gridY += 1;
	params.padBottom = 5;
	okButton = kernelWindowNewButton(dialog, "OK", NULL, &params);
	if (!okButton)
	{
		status = ERR_NOCREATE;
		goto out;
	}

	kernelWindowComponentFocus(okButton);
	kernelWindowSetVisible(dialog, 1);

	while (1)
	{
		// Check for our OK button
		status = kernelWindowComponentEventGet((objectKey) okButton, &event);
		if ((status > 0) && (event.type == EVENT_MOUSE_LEFTUP))
			break;

		// Check for window close events
		status = kernelWindowComponentEventGet((objectKey) dialog, &event);
		if ((status > 0) && (event.type == EVENT_WINDOW_CLOSE))
			break;

		// Done
		kernelMultitaskerYield();
	}

	status = 0;

out:
	if (dialog)
		kernelWindowDestroy(dialog);

	return (status);
}


static void errorDialogThread(int argc, void *argv[])
{
	int status = 0;
	const char *title = NULL;
	const char *message = NULL;
	const char *details = NULL;
	kernelWindow *window = NULL;
	image errorImage;
	kernelWindowComponent *messageContainer = NULL;
	kernelWindowComponent *buttonContainer = NULL;
	kernelWindowComponent *okButton = NULL;
	kernelWindowComponent *detailsButton = NULL;
	componentParameters params;
	windowEvent event;

	if (argc < 4)
	{
		status = ERR_ARGUMENTCOUNT;
		goto exit;
	}

	memset(&errorImage, 0, sizeof(image));
	memset(&params, 0, sizeof(componentParameters));

	title = argv[1];
	message = argv[2];
	details = argv[3];

	// Create the dialog.
	window = kernelWindowNew(kernelCurrentProcess->processId, title);
	if (!window)
	{
		status = ERR_NOCREATE;
		goto exit;
	}

	// Create the container for the message
	params.gridWidth = 1;
	params.gridHeight = 1;
	params.padLeft = 5;
	params.padRight = 5;
	params.padTop = 5;
	params.flags = (WINDOW_COMPFLAG_FIXEDWIDTH | WINDOW_COMPFLAG_FIXEDHEIGHT);
	params.orientationX = orient_center;
	params.orientationY = orient_middle;
	messageContainer = kernelWindowNewContainer(window, "messageContainer",
		&params);
	if (!messageContainer)
	{
		status = ERR_NOCREATE;
		goto exit;
	}

	params.orientationX = orient_right;
	status = kernelImageLoad(ERRORIMAGE_NAME, 64, 64, &errorImage);
	if (!status)
	{
		errorImage.transColor.green = 0xFF;
		kernelWindowNewImage(messageContainer, &errorImage, draw_alphablend,
			&params);
		kernelImageFree(&errorImage);
	}

	// Create the label
	params.gridX += 1;
	params.orientationX = orient_left;
	kernelWindowNewTextLabel(messageContainer, message, &params);

	// Create the container for the buttons
	params.gridX = 0;
	params.gridY += 1;
	params.padBottom = 5;
	params.orientationX = orient_center;
	buttonContainer =
		kernelWindowNewContainer(window, "buttonContainer", &params);
	if (!messageContainer)
	{
		status = ERR_NOCREATE;
		goto exit;
	}

	// Create the OK button
	params.padBottom = 0;
	params.orientationX = orient_right;
	okButton = kernelWindowNewButton(buttonContainer, "OK", NULL, &params);
	if (!okButton)
	{
		status = ERR_NOCREATE;
		goto exit;
	}

	// Create the details button
	params.gridX += 1;
	params.orientationX = orient_left;
	detailsButton =
		kernelWindowNewButton(buttonContainer, "Details", NULL, &params);
	if (!detailsButton)
	{
		status = ERR_NOCREATE;
		goto exit;
	}
	kernelWindowComponentSetEnabled(detailsButton, (details != NULL));

	kernelWindowComponentFocus(okButton);
	kernelWindowSetVisible(window, 1);

	while (1)
	{
		// Check for our OK button
		status = kernelWindowComponentEventGet((objectKey) okButton, &event);
		if ((status > 0) && (event.type == EVENT_MOUSE_LEFTUP))
			break;

		// Check for our details button
		status =
			kernelWindowComponentEventGet((objectKey) detailsButton, &event);
		if ((status > 0) && (event.type == EVENT_MOUSE_LEFTUP))
		{
			status = errorDialogDetails(window, details);
			if (status < 0)
				kernelWindowComponentSetEnabled(detailsButton, 0);
			else
				break;
		}

		// Check for window close events
		status = kernelWindowComponentEventGet((objectKey) window, &event);
		if ((status > 0) && (event.type == EVENT_WINDOW_CLOSE))
			break;

		// Done
		kernelMultitaskerYield();
	}

	status = 0;

exit:
	if (window)
		kernelWindowDestroy(window);

	kernelMultitaskerTerminate(status);
}


/////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////
//
//  Below here, the functions are exported for external use
//
/////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////

void kernelErrorOutput(const char *fileName, const char *function, int line,
	kernelErrorKind kind, const char *message, ...)
{
	// This routine takes a bunch of parameters and outputs a kernel error
	// which is output to the text console and the kernel log.

	int printErrors = !kernelLogGetToConsole();
	va_list list;
	const char *errorType = NULL;
	char processName[MAX_PROCNAME_LENGTH];
	char errorText[MAX_ERRORTEXT_LENGTH];

	// Copy the kind of the error
	switch(kind)
	{
		case kernel_panic:
			errorType = panicConst;
			break;
		case kernel_error:
			errorType = errorConst;
			break;
		case kernel_warn:
			errorType = warningConst;
			break;
		default:
			errorType= messageConst;
			break;
	}

	processName[0] = '\0';
	if (kernelProcessingInterrupt())
		sprintf(processName, "interrupt %d", kernelInterruptGetCurrent());
	else if (kernelCurrentProcess)
		strncpy(processName, (char *) kernelCurrentProcess->name,
			MAX_PROCNAME_LENGTH);

	sprintf(errorText, "%s:%s:%s:%s(%d):", errorType, processName, fileName,
		function, line);

	// Log the context of the message
	kernelLog("%s", errorText);

	if (printErrors)
		// Output the context of the message to the screen
		kernelTextPrintLine("%s", errorText);

	// Initialize the argument list
	va_start(list, message);

	// Expand the message if there were any parameters
	vsnprintf(errorText, MAX_ERRORTEXT_LENGTH, message, list);

	va_end(list);

	// Log the message
	kernelLog("%s", errorText);

	if (printErrors)
		// Output the message to the screen
		kernelTextPrintLine("%s", errorText);

	return;
}


void kernelErrorDialog(const char *title, const char *message,
	const char *details)
{
	// This will make a simple error dialog message, and wait until the button
	// has been pressed.

	void *args[] = {
		(void *) title,
		(void *) message,
		(void *) details
	};

	// Check params.  Details can be NULL.
	if (!title || !message)
		return;

	kernelMultitaskerSpawnKernelThread(&errorDialogThread,
		ERRORDIALOG_THREADNAME, 3, args);
	return;
}

