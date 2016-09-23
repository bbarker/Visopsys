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
//  kernelTouch.c
//

// This contains utility functions for managing touchscreens.

#include "kernelTouch.h"
#include "kernelError.h"
#include "kernelGraphic.h"
#include "kernelMouse.h"
#include "kernelMultitasker.h"
#include "kernelWindow.h"
#include <string.h>

// The graphics environment
static int screenWidth = 0;
static int screenHeight = 0;

static int threadPid = 0;
static int threadStop = 0;
static int initialized = 0;
static int detected = 0;

// Keeps touch pointer status and location data
static volatile struct {
	int changed;
	int xPosition;
	int newXPosition;
	int yPosition;
	int newYPosition;
	int touch;
	int newTouch;

} touchStatus;


static inline void draw(void)
{
	// Draw the touch pointer
	kernelGraphicDrawOval(NULL, &COLOR_BLACK, draw_normal,
		(touchStatus.xPosition - (TOUCH_POINTER_SIZE / 2)),
		(touchStatus.yPosition - (TOUCH_POINTER_SIZE / 2)), TOUCH_POINTER_SIZE,
		TOUCH_POINTER_SIZE, 1 /* thickness */, 1 /* fill */);
}


static inline void erase(void)
{
	// Redraw whatever the touch pointer was covering
	kernelWindowRedrawArea((touchStatus.xPosition - (TOUCH_POINTER_SIZE / 2)),
		(touchStatus.yPosition - (TOUCH_POINTER_SIZE / 2)), TOUCH_POINTER_SIZE,
		TOUCH_POINTER_SIZE);
}


static inline void status2event(int eventType, windowEvent *event)
{
	event->type = eventType;
	event->xPosition = touchStatus.xPosition;
	event->yPosition = touchStatus.yPosition;
}


static void touchThread(void)
{
	// Check for finger movement, draw updates, and pass events to the
	// window manager

	int eventType = 0;
	windowEvent event;

	memset(&event, 0, sizeof(windowEvent));

	while (!threadStop)
	{
		if (!touchStatus.changed)
		{
			kernelMultitaskerYield();
			continue;
		}

		if (!touchStatus.newTouch)
		{
			erase();

			// Set up our event
			eventType = EVENT_MOUSE_LEFTUP;
		}
		else
		{
			if (touchStatus.touch)
				erase();
			else
				kernelMouseHide();

			touchStatus.xPosition = touchStatus.newXPosition;
			touchStatus.yPosition = touchStatus.newYPosition;

			draw();

			// Set up our event
			if (touchStatus.touch)
				eventType = EVENT_MOUSE_DRAG;
			else
				eventType = EVENT_MOUSE_LEFTDOWN;
		}

		// Tell the window manager
		status2event(eventType, &event);
		kernelWindowProcessEvent(&event);

		touchStatus.touch = touchStatus.newTouch;
		touchStatus.changed = 0;
	}

	kernelMultitaskerTerminate(0);
}


/////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////
//
// Below here, the functions are exported for external use
//
/////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////

int kernelTouchInitialize(void)
{
	// Initialize the touch functions

	int status = 0;

	memset((void *) &touchStatus, 0, sizeof(touchStatus));

	screenWidth = kernelGraphicGetScreenWidth();
	screenHeight = kernelGraphicGetScreenHeight();

	initialized = 1;

	// Spawn the touch thread
	threadPid = kernelMultitaskerSpawn(touchThread, "touch thread", 0, NULL);
	if (threadPid < 0)
		kernelError(kernel_error, "Unable to start touch thread");

	return (status = 0);
}


int kernelTouchShutdown(void)
{
	// Stop processing touchscreen stuff.

	// Don't accept more input data
	initialized = 0;

	// Tell the thread to stop
	threadStop = 1;

	// Wait for the thread to terminate
	while (kernelMultitaskerProcessIsAlive(threadPid));

	// Erase the touch pointer
	erase();

	return (0);
}


void kernelTouchDetected(void)
{
	// This is called by the device drivers to tell us that some touch device
	// has been detected.
	detected = 1;
}


int kernelTouchAvailable(void)
{
	// This is called by the rest of the system to determine whether some touch
	// device has been detected.
	return (detected);
}


void kernelTouchInput(kernelTouchReport *report)
{
	// Called by the device drivers to register touch input

	int screenX = 0;
	int screenY = 0;

	// Check params
	if (!report)
		return;

	// Make sure we've been initialized
	if (!initialized)
		return;

	// Figure out the screen coordinates of the touch

	screenX = ((report->x << TOUCH_SCALING_FACTOR) /
		((report->maxX << TOUCH_SCALING_FACTOR) / screenWidth));

	screenY = ((report->y << TOUCH_SCALING_FACTOR) /
		((report->maxY << TOUCH_SCALING_FACTOR) / screenHeight));

	touchStatus.newXPosition = screenX;
	touchStatus.newYPosition = screenY;
	touchStatus.newTouch = (report->flags & 1);
	touchStatus.changed = 1;

	kernelMultitaskerSetProcessState(threadPid, proc_ioready);

	return;
}

