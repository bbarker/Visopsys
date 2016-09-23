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
//  kernelShutdown.c
//

// This code is responsible for an orderly shutdown and/or reboot of
// the kernel

#include "kernelShutdown.h"
#include "kernelCpu.h"
#include "kernelEnvironment.h"
#include "kernelError.h"
#include "kernelFilesystem.h"
#include "kernelFont.h"
#include "kernelGraphic.h"
#include "kernelLocale.h"
#include "kernelLog.h"
#include "kernelMultitasker.h"
#include "kernelNetwork.h"
#include "kernelPower.h"
#include "kernelUsbDriver.h"
#include "kernelWindow.h"
#include <stdio.h>
#include <string.h>
#include <sys/processor.h>

#define _(string) kernelGetText(string)
#define SHUTDOWN_MSG1			_("Shutting down Visopsys, please wait...")
#define SHUTDOWN_MSG2			_("[ Wait for \"OK to power off\" message ]")
#define SHUTDOWN_MSG_REBOOT		_("Rebooting.")
#define SHUTDOWN_MSG_POWER		_("OK to power off now.")


static void messageBox(kernelFont *font, int numLines, char *message[])
{
	// Prints a blue box, with lines of white text centered in it.
	// Useful for final shutdown messages.

	const char *charSet = NULL;
	unsigned screenWidth = kernelGraphicGetScreenWidth();
	unsigned screenHeight = kernelGraphicGetScreenHeight();
	unsigned messageWidth = 0;
	unsigned messageHeight, boxWidth, boxHeight, tmp;
	int count;

	// The default desktop color
	extern color kernelDefaultDesktop;

	if (kernelCurrentProcess)
		charSet = kernelVariableListGet(kernelCurrentProcess->environment,
			ENV_CHARSET);

	if (!charSet)
		charSet = CHARSET_NAME_DEFAULT;

	for (count = 0; count < numLines; count ++)
	{
		tmp = kernelFontGetPrintedWidth(font, charSet, message[count]);
		if (tmp > messageWidth)
			messageWidth = tmp;
	}

	messageHeight = (font->glyphHeight * numLines);
	boxWidth = (messageWidth + 30);
	boxHeight = (messageHeight + (font->glyphHeight * 2));

	// The box
	kernelGraphicDrawRect(NULL, &kernelDefaultDesktop, draw_normal,
		((screenWidth - boxWidth) / 2), ((screenHeight - boxHeight) / 2),
		boxWidth, boxHeight, 1, 1);

	// Nice white border
	kernelGraphicDrawRect(NULL, &COLOR_WHITE, draw_normal,
		((screenWidth - boxWidth) / 2), ((screenHeight - boxHeight) / 2),
		boxWidth, boxHeight, 2, 0);

	// The message
	for (count = 0; count < numLines; count ++)
	{
		kernelGraphicDrawText(NULL, &COLOR_WHITE, &kernelDefaultDesktop, font,
			charSet, message[count], draw_normal,
			((screenWidth -
				kernelFontGetPrintedWidth(font, charSet, message[count])) / 2),
			(((screenHeight - messageHeight) / 2) +
				(font->glyphHeight * count)));
	}
}


/////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////
//
//  Below here, the functions are exported for external use
//
/////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////

int kernelShutdown(int reboot, int force)
{
	// This function will shut down the kernel, and reboot the computer
	// if the 'reboot' argument dictates.  This function must include
	// shutdown invocations for all of the major subsystems that support
	// and/or require it.

	int status = 0;
	int graphics = 0;
	static int shutdownInProgress = 0;
	char *finalMessage = NULL;

	// We only use these if grapics are enabled
	kernelFont *font = NULL;
	kernelWindow *window = NULL;
	componentParameters params;
	unsigned screenWidth = 0;
	unsigned screenHeight = 0;
	int windowWidth, windowHeight;

	if (shutdownInProgress && !force)
	{
		kernelError(kernel_error, "The system is already shutting down");
		return (status = ERR_ALREADY);
	}

	shutdownInProgress = 1;

	// Are grapics enabled?  If so, we will try to output our initial message
	// to a window
	graphics = kernelGraphicsAreEnabled();

	if (graphics)
	{
		screenWidth = kernelGraphicGetScreenWidth();
		screenHeight = kernelGraphicGetScreenHeight();

		// Try to load a nice-looking font
		font = kernelFontGet(WINDOW_DEFAULT_VARFONT_MEDIUM_FAMILY,
			WINDOW_DEFAULT_VARFONT_MEDIUM_FLAGS,
			WINDOW_DEFAULT_VARFONT_MEDIUM_POINTS, NULL);

		if (!font)
			// Font's not there, we suppose.  Use the system one.
			kernelFontGetSystem(&font);

		window = kernelWindowNew(kernelMultitaskerGetCurrentProcessId(),
			_("Shutting down"));
		if (window)
		{
			memset(&params, 0, sizeof(componentParameters));
			params.gridWidth = 1;
			params.gridHeight = 1;
			params.padLeft = 10;
			params.padRight = 10;
			params.padTop = 5;
			params.padBottom = 5;
			params.orientationX = orient_center;
			params.orientationY = orient_top;
			kernelWindowNewTextLabel(window, SHUTDOWN_MSG1, &params);

			if (!reboot)
			{
				params.gridY = 1;
				params.padTop = 0;
				kernelWindowNewTextLabel(window, SHUTDOWN_MSG2, &params);
			}

			kernelWindowRemoveMinimizeButton(window);
			kernelWindowRemoveCloseButton(window);
			kernelWindowGetSize(window, &windowWidth, &windowHeight);
			kernelWindowSetLocation(window, ((screenWidth - windowWidth) / 2),
				((screenHeight - windowHeight) / 3));
			kernelWindowSetVisible(window, 1);
		}
	}

	// Echo the appropriate message(s) to the console [as well]
	kernelTextPrintLine("\n%s", SHUTDOWN_MSG1);
	if (!reboot)
		kernelTextPrintLine("%s", SHUTDOWN_MSG2);

	// Stop networking
	status = kernelNetworkShutdown();
	if (status < 0)
		kernelError(kernel_error, "Network shutdown failed");

	// Shut down kernel logging
	kernelLog("Stopping kernel logging");
	status = kernelLogShutdown();
	if (status < 0)
		kernelError(kernel_error, "The kernel logger could not be stopped.");

	// Detach from our parent process, if applicable, so we won't get killed
	// when our parent gets killed
	kernelMultitaskerDetach();

	// Kill all the processes, except this one and the kernel.
	kernelLog("Stopping all processes");
	status = kernelMultitaskerKillAll();

	if ((status < 0) && (!force))
	{
		// Eek.  We couldn't kill the processes nicely
		kernelError(kernel_error, "Unable to stop processes nicely.  "
			"Shutdown aborted.");
		shutdownInProgress = 0;
		return (status);
	}

	// Unmount all filesystems and synchronize/shut down the disks
	kernelLog("Unmounting filesystems, synchronizing disks");
	status = kernelDiskShutdown();
	if (status < 0)
	{
		// Eek.  We couldn't synchronize the filesystems.  We should
		// stop and allow the user to try to save their data
		kernelError(kernel_error, "Unable to syncronize disks.  Shutdown "
			"aborted.");
		shutdownInProgress = 0;
		return (status);
	}

	// After this point, don't abort.  We're running the show.

	// Shut down the multitasker
	status = kernelMultitaskerShutdown(1 /* nice shutdown */);

	if (status < 0)
	{
		if (!force)
		{
			// Abort the shutdown
			kernelError(kernel_error, "Unable to stop multitasker.  Shutdown "
				"aborted.");
			shutdownInProgress = 0;
			return (status);
		}
		else
		{
			// Attempt to shutdown the multitasker without the 'nice' flag.
			// We won't bother to check whether this was successful
			kernelMultitaskerShutdown(0 /* NOT nice shutdown */);
		}
	}

	// Only shut down USB if we're rebooting ('cause it takes the time to reset
	// the controller(s), etc.)
	if (reboot)
	{
		status = kernelUsbShutdown();
		if (status < 0)
			// Not a fatal error
			kernelError(kernel_error, "The USB system could not be stopped.");
	}

	// Don't want the user moving the mousie over our message stuff.
	kernelMouseShutdown();

	// What final message will we be displaying today?
	if (reboot)
		finalMessage = SHUTDOWN_MSG_REBOOT;
	else
		finalMessage = SHUTDOWN_MSG_POWER;

	// Last words.
	kernelTextPrintLine("\n%s", finalMessage);

	if (graphics)
	{
		// Get rid of the window we showed.  No need to properly destroy it.
		if (window)
			kernelWindowSetVisible(window, 0);

		// Draw a box with the final message
		messageBox(font, 1, (char *[]){ finalMessage } );
	}

	// Finally, we either halt or reboot the computer
	if (reboot)
	{
		kernelCpuSpinMs(1000); // Wait 1 second
		// Disable interrupts
		processorDisableInts();
		processorReboot();
	}
	else
	{
		// Try to power off, if the appropriate power management routines are
		// installed
		kernelPowerOff();

		// Default to processor stop.
		processorStop();
	}

	// Just for good form
	return (status);
}


void kernelPanicOutput(const char *fileName, const char *function, int line,
	const char *message, ...)
{
	// This is a quick shutdown for kernel panic which puts nice messages
	// on the screen in graphics mode as well.

	kernelFont *font = NULL;
	char panicMessage[MAX_ERRORTEXT_LENGTH];
	char errorText[MAX_ERRORTEXT_LENGTH];
	va_list list;

	processorDisableInts();

	snprintf(panicMessage, MAX_ERRORTEXT_LENGTH,
		_("SYSTEM HALTED: Panic at %s:%s(%d)"), fileName, function, line);

	// Expand the message if there were any parameters
	va_start(list, message);
	vsnprintf(errorText, MAX_ERRORTEXT_LENGTH, message, list);
	va_end(list);

	if (kernelGraphicsAreEnabled())
	{
		// Draw a box with the panic message
		kernelFontGetSystem(&font);
		messageBox(font, 2, (char *[]){ panicMessage, errorText } );
	}
	else
	{
		kernelTextPrintLine("%s", panicMessage);
		kernelTextPrintLine("%s", errorText);
	}

	processorStop();
}

