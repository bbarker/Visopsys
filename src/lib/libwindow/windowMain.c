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
//  windowMain.c
//

// This contains functions for user programs to operate GUI components.

#include <errno.h>
#include <libintl.h>
#include <string.h>
#include <stdlib.h>
#include <sys/window.h>
#include <sys/api.h>


typedef volatile struct {
	objectKey key;
	unsigned eventMask;
	void (*function)(objectKey, windowEvent *);

} callBack;

int libwindow_initialized = 0;
void libwindowInitialize(void);

static callBack *callBacks = NULL;
static volatile int numCallBacks = 0;
static volatile int run = 0;
static volatile int guiThreadPid = 0;


static void guiRun(void)
{
	// This is the thread that runs for each user GUI program polling
	// components' event queues for events.

	objectKey key = NULL;
	windowEvent event;
	int count;

	run = 1;

	while (run)
	{
		// Loop through all of the registered callbacks looking for components
		// with pending events
		for (count = 0; (run && (count < numCallBacks)); count ++)
		{
			key = callBacks[count].key;

			// Any events pending?
			if (key && (windowComponentEventGet(key, &event) > 0))
			{
				if (callBacks[count].function)
					callBacks[count].function(key, &event);
			}
		}

		// Done
		multitaskerYield();
	}
}


static void guiRunThread(void)
{
	guiRun();
	multitaskerTerminate(0);
}


/////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////
//
// Below here, the functions are exported for external use
//
/////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////

void libwindowInitialize(void)
{
	bindtextdomain("libwindow", GETTEXT_LOCALEDIR_PREFIX);
	libwindow_initialized = 1;
}


_X_ int windowClearEventHandlers(void)
{
	// Desc: Remove all the callback event handlers registered with the windowRegisterEventHandler() function.

	numCallBacks = 0;

	if (callBacks)
		memset((void *) callBacks, 0, (WINDOW_MAX_EVENTHANDLERS *
			sizeof(callBack)));

	return (0);
}


_X_ int windowRegisterEventHandler(objectKey key, void (*function)(objectKey, windowEvent *))
{
	// Desc: Register a callback function as an event handler for the GUI object 'key'.  The GUI object can be a window component, or a window for example.  GUI components are typically the target of mouse click or key press events, whereas windows typically receive 'close window' events.  For example, if you create a button component in a window, you should call windowRegisterEventHandler() to receive a callback when the button is pushed by a user.  You can use the same callback function for all your objects if you wish -- the objectKey of the target component can always be found in the windowEvent passed to your callback function.  It is necessary to use one of the 'run' functions, below, such as windowGuiRun() or windowGuiThread() in order to receive the callbacks.

	int status = 0;

	// Check parameters
	if (!key || !function)
		return (status = ERR_NULLPARAMETER);

	if (!callBacks)
	{
		// Get memory for our callbacks
		callBacks = malloc(WINDOW_MAX_EVENTHANDLERS * sizeof(callBack));
		if (!callBacks)
		{
			errno = ERR_MEMORY;
			return (status = errno);
		}

		numCallBacks = 0;
	}

	if (numCallBacks >= WINDOW_MAX_EVENTHANDLERS)
		return (status = ERR_NOFREE);

	callBacks[numCallBacks].key = key;
	callBacks[numCallBacks].function = function;
	numCallBacks += 1;

	return (status = 0);
}


_X_ int windowClearEventHandler(objectKey key)
{
	// Desc: Remove a callback event handler registered with the windowRegisterEventHandler() function.

	int callBackIndex = -1;
	int count;

	for (count = 0; count < numCallBacks; count ++)
	{
		if (callBacks[count].key == key)
		{
			callBackIndex = count;
			break;
		}
	}

	if (callBackIndex < 0)
		return (ERR_NOSUCHENTRY);

	if ((numCallBacks > 1) && (callBackIndex < (numCallBacks - 1)))
		memcpy((void *) &callBacks[callBackIndex],
			(void *) &callBacks[numCallBacks - 1], sizeof(callBack));
	numCallBacks -= 1;

	return (0);
}


_X_ void windowGuiRun(void)
{
	// Desc: Run the GUI windowEvent polling as a blocking call.  In other words, use this function when your program has completed its setup code, and simply needs to watch for GUI events such as mouse clicks, key presses, and window closures.  If your program needs to do other processing (independently of windowEvents) you should use the windowGuiThread() function instead.

	guiRun();
	return;
}


_X_ int windowGuiThread(void)
{
	// Desc: Run the GUI windowEvent polling as a non-blocking call.  In other words, this function will launch a separate thread to monitor for GUI events and return control to your program.  Your program can then continue execution -- independent of GUI windowEvents.  If your program doesn't need to do any processing after setting up all its window components and event callbacks, use the windowGuiRun() function instead.

	if (!guiThreadPid || !multitaskerProcessIsAlive(guiThreadPid))
		guiThreadPid = multitaskerSpawn(&guiRunThread, "gui thread", 0, NULL);

	return (guiThreadPid);
}


_X_ int windowGuiThreadPid(void)
{
	// Desc: Returns the current GUI thread PID, if applicable, or else 0.
	return (guiThreadPid);
}


_X_ void windowGuiStop(void)
{
	// Desc: Stop GUI event polling which has been started by a previous call to one of the 'run' functions, such as windowGuiRun() or windowGuiThread().

	run = 0;

	if (guiThreadPid && (multitaskerGetCurrentProcessId() != guiThreadPid))
		multitaskerKillProcess(guiThreadPid, 0);

	multitaskerYield();

	guiThreadPid = 0;

	return;
}

