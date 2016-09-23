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
//  kernelSysTimer.c
//

// These are the generic C "wrapper" functions for the functions which
// reside in the system timer driver.  Most of them basically just call
// their associated functions, but there will be extra functionality here
// as well.

#include "kernelSysTimer.h"
#include "kernelError.h"
#include "kernelInterrupt.h"
#include "kernelPic.h"
#include <string.h>
#include <sys/processor.h>

static kernelDevice *systemTimer = NULL;
static kernelSysTimerOps *ops = NULL;


static void timerInterrupt(void)
{
	// This is the system timer interrupt handler.  It calls the timer driver
	// to actually read data from the device.

	void *address = NULL;

	processorIsrEnter(address);
	kernelInterruptSetCurrent(INTERRUPT_NUM_SYSTIMER);

	// Call the driver function
	if (ops->driverTick)
		ops->driverTick();

	kernelPicEndOfInterrupt(INTERRUPT_NUM_SYSTIMER);
	kernelInterruptClearCurrent();
	processorIsrExit(address);
}


/////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////
//
//  Below here, the functions are exported for external use
//
/////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////

int kernelSysTimerInitialize(kernelDevice *dev)
{
	// This function initializes the system timer.

	int status = 0;

	// Check params
	if (!dev)
	{
		kernelError(kernel_error, "NULL parameter");
		return (status = ERR_NOTINITIALIZED);
	}

	systemTimer = dev;

	if (!systemTimer->driver || !systemTimer->driver->ops)
	{
		kernelError(kernel_error, "The system timer driver or ops are NULL");
		return (status = ERR_NULLPARAMETER);
	}

	ops = systemTimer->driver->ops;

	// Set up initial, default values
	kernelSysTimerSetupTimer(0 /* timer */, 3 /* mode */,
		0 /* count 0x10000 */);

	// Don't save any old handler for the dedicated system timer interrupt,
	// but if there is one, we want to know about it.
	if (kernelInterruptGetHandler(INTERRUPT_NUM_SYSTIMER))
		kernelError(kernel_warn, "Not chaining unexpected existing handler "
			"for system timer int %d", INTERRUPT_NUM_SYSTIMER);

	// Register our interrupt handler
	status = kernelInterruptHook(INTERRUPT_NUM_SYSTIMER, &timerInterrupt,
		NULL);
	if (status < 0)
		return (status);

	// Turn on the interrupt
	status = kernelPicMask(INTERRUPT_NUM_SYSTIMER, 1);
	if (status < 0)
		return (status);

	// Return success
	return (status = 0);
}


void kernelSysTimerTick(void)
{
	// This registers a tick of the system timer.

	if (!systemTimer)
		return;

	// Call the driver function
	if (ops->driverTick)
		ops->driverTick();

	return;
}


unsigned kernelSysTimerRead(void)
{
	// This returns the value of the number of system timer ticks.

	unsigned timer = 0;
	int status = 0;

	if (!systemTimer)
		return (status = ERR_NOTINITIALIZED);

	// Now make sure the device driver timer tick function has been
	// installed
	if (!ops->driverRead)
	{
		kernelError(kernel_error, "The device driver function is NULL");
		return (status = ERR_NOSUCHFUNCTION);
	}

	// Call the driver function
	timer = ops->driverRead();

	// Return the result from the driver call
	return (timer);
}


int kernelSysTimerReadValue(int timer)
{
	// This returns the current value of the requested timer.

	int value = 0;
	int status = 0;

	if (!systemTimer)
		return (status = ERR_NOTINITIALIZED);

	// Now make sure the device driver read value function has been
	// installed
	if (!ops->driverReadValue)
	{
		kernelError(kernel_error, "The device driver function is NULL");
		return (status = ERR_NOSUCHFUNCTION);
	}

	// Call the driver function
	value = ops->driverReadValue(timer);

	// Return the result from the driver call
	return (value);
}


int kernelSysTimerSetupTimer(int timer, int mode, int startCount)
{
	// This sets up the operation of the requested timer.

	int status = 0;

	if (!systemTimer)
		return (status = ERR_NOTINITIALIZED);

	// Now make sure the device driver setup timer function has been
	// installed
	if (!ops->driverSetupTimer)
	{
		kernelError(kernel_error, "The device driver function is NULL");
		return (status = ERR_NOSUCHFUNCTION);
	}

	// Call the driver function
	status = ops->driverSetupTimer(timer, mode, startCount);

	// Return the result from the driver call
	return (status);
}


int kernelSysTimerGetOutput(int timer)
{
	// This returns the current value of the requested timer.

	int value = 0;
	int status = 0;

	if (!systemTimer)
		return (status = ERR_NOTINITIALIZED);

	// Now make sure the device driver get output function has been
	// installed
	if (!ops->driverGetOutput)
	{
		kernelError(kernel_error, "The device driver function is NULL");
		return (status = ERR_NOSUCHFUNCTION);
	}

	// Call the driver function
	value = ops->driverGetOutput(timer);

	// Return the result from the driver call
	return (value);
}


void kernelSysTimerWaitTicks(int waitTicks)
{
	// This function waits for a specified number of timer ticks to occur.

	int targetTime = 0;

	if (!systemTimer)
		return;

	// Now make sure the device driver read timer function has been
	// installed
	if (!ops->driverRead)
	{
		kernelError(kernel_error, "The device driver function is NULL");
		return;
	}

	// One more thing: make sure the number to wait is reasonable.
	if (waitTicks < 0)
		// Not possible in this dimension.  Assume zero.
		waitTicks = 0;

	// Now we can call the driver function safely.

	// Find out the current time
	targetTime = ops->driverRead();

	// Add the ticks-to-wait to that number
	targetTime += waitTicks;

	// Now loop until the time reaches the specified mark
	while (targetTime >= ops->driverRead());

	return;
}

