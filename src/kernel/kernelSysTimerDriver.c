//
//  Visopsys
//  Copyright (C) 1998-2018 J. Andrew McLaughlin
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
//  kernelSysTimerDriver.c
//

// Driver for standard PC system timer chip

#include "kernelDriver.h" // Contains my prototypes
#include "kernelDebug.h"
#include "kernelError.h"
#include "kernelMalloc.h"
#include "kernelSysTimer.h"
#include <string.h>
#include <sys/processor.h>

static int portNumber[] = { 0x40, 0x41, 0x42 };
static char timerMode[3] = { -1, -1, -1 };
static int timerTicks = 0;


static unsigned char readBackStatus(int counter)
{
	unsigned char data, commandByte;

	// Calculate the command to use (read-back, 'status' value)
	commandByte = (0xE0 | (0x02 << counter));

	// Send the command to the general command port
	processorOutPort8(0x43, commandByte);

	// Read the status byte
	processorInPort8(portNumber[counter], data);

	return (data);
}


static void driverTick(void)
{
	// This updates the count of the system timer

	// Add one to the timer 0 tick counter
	timerTicks += 1;

	kernelDebug(debug_misc, "PIT interrupt %d", timerTicks);

	return;
}


static int driverRead(void)
{
	// Returns the value of the system timer tick counter.
	return (timerTicks);
}


static int driverReadValue(int counter)
{
	// This function is used to select and read one of the system
	// timer counters

	int timerValue = 0;
	unsigned char commandByte = 0, data = 0;

	// Make sure the timer number is not greater than 2.  This driver only
	// supports timers 0 through 2 (since that's all most systems will have)
	if (counter > 2)
		return (timerValue = ERR_BOUNDS);

	// Make sure we've set the timer at some point previously
	if (timerMode[counter] < 0)
		return (timerValue = 0);

	// Calculate the command to use (read-back, 'count' value)
	commandByte = (0xD0 | (0x02 << counter));

	// Send the command to the general command port
	processorOutPort8(0x43, commandByte);

	// The counter will now be expecting us to read two bytes from
	// the applicable port.

	// Read the low byte first, followed by the high byte
	processorInPort8(portNumber[counter], data);
	timerValue = data;

	processorInPort8(portNumber[counter], data);
	timerValue |= (data << 8);

	kernelDebug(debug_misc, "PIT read counter %d count=%d", counter,
		timerValue);

	return (timerValue);
}


static int driverSetupTimer(int counter, int mode, int count)
{
	// This function is used to select, set the mode and count of one
	// of the system timer counters

	int status = 0;
	unsigned char data, commandByte;

	kernelDebug(debug_misc, "PIT setting counter %d mode=%d count=%d", counter,
		mode, count);

	// Make sure the timer number is not greater than 2.  This driver only
	// supports timers 0 through 2 (since that's all most systems will have)
	if (counter > 2)
		return (status = ERR_BOUNDS);

	// Make sure the mode is legal
	if (mode > 5)
		return (status = ERR_BOUNDS);

	// Calculate the command to use
	commandByte = ((counter << 6) | 0x30 | (mode << 1));

	// Send the command to the general command port
	processorOutPort8(0x43, commandByte);

	// The timer is now expecting us to send two bytes which represent the
	// initial count of the timer.

	// Send low byte first, followed by the high byte to the data
	data = (unsigned char)(count & 0xFF);
	processorOutPort8(portNumber[counter], data);

	data = (unsigned char)((count >> 8) & 0xFF);
	processorOutPort8(portNumber[counter], data);

	// Wait until the count is loaded (NULL count is zero)
	while (readBackStatus(counter) & 0x40);

	timerMode[counter] = mode;

	kernelDebug(debug_misc, "PIT set counter cmd=0x%02x", commandByte);

	return (status = 0);
}


static int driverGetOutput(int counter)
{
	// This function is used to read the output pin of one of the system timer
	// counters

	// Make sure the timer number is not greater than 2.  This driver only
	// supports timers 0 through 2 (since that's all most systems will have)
	if (counter > 2)
		return (ERR_RANGE);

	if (readBackStatus(counter) & 0x80)
		return (1);
	else
		return (0);
}


static int driverDetect(void *parent, kernelDriver *driver)
{
	// Normally, this function is used to detect and initialize each device,
	// as well as registering each one with any higher-level interfaces.
	// Since we can assume that there's a system timer, just initialize it.

	int status = 0;
	kernelDevice *dev = NULL;

	// Allocate memory for the device
	dev = kernelMalloc(sizeof(kernelDevice));
	if (!dev)
		return (status = 0);

	dev->device.class = kernelDeviceGetClass(DEVICECLASS_SYSTIMER);
	dev->driver = driver;

	// Reset the counter we use to count the number of timer 0 (system timer)
	// interrupts we've encountered
	timerTicks = 0;

	// Make sure that counter 0 is set to operate in mode 3
	// (some systems erroneously use mode 2) with an initial value of 0
	driverSetupTimer(0, 3, 0);

	// Initialize system timer operations
	status = kernelSysTimerInitialize(dev);
	if (status < 0)
	{
		kernelFree(dev);
		return (status);
	}

	// Add the kernel device
	return (status = kernelDeviceAdd(parent, dev));
}


static kernelSysTimerOps sysTimerOps = {
	driverTick,
	driverRead,
	driverReadValue,
	driverSetupTimer,
	driverGetOutput
};


/////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////
//
// Below here, the functions are exported for external use
//
/////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////

void kernelSysTimerDriverRegister(kernelDriver *driver)
{
	 // Device driver registration.

	driver->driverDetect = driverDetect;
	driver->ops = &sysTimerOps;

	return;
}

