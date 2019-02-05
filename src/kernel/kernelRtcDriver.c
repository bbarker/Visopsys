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
//  kernelRtcDriver.c
//

// Driver for standard Real-Time Clock (RTC) chips.

#include "kernelDriver.h" // Contains my prototypes
#include "kernelError.h"
#include "kernelRtc.h"
#include "kernelMalloc.h"
#include <sys/processor.h>

// Register numbers
#define SECSREG 0  // Seconds register
#define MINSREG 2  // Minutes register
#define HOURREG 4  // Hours register
// #define DOTWREG 6  // (unused) Day of week register
#define DOTMREG 7  // Day of month register
#define MNTHREG 8  // Month register
#define YEARREG 9  // Year register


static inline int waitReady(void)
{
	// This returns when the RTC is ready to be read or written.  Make sure
	// to disable interrupts before calling this function.

	// Read the clock's "update in progress" bit from Register A.  If it is
	// set, do a loop until the clock has finished doing the update.  This is
	// so we know the data we're getting from the clock is reasonable.

	unsigned char data = 0x80;
	int count;

	for (count = 0; count < 10000; count ++)
	{
		processorOutPort8(0x70, 0x0A);
		processorDelay();
		processorInPort8(0x71, data);

		if (!(data & 0x80))
			break;
	}

	if (data & 0x80)
	{
		kernelError(kernel_error, "RTC not ready");
		return (ERR_BUSY);
	}
	else
	{
		return (0);
	}
}


static int readRegister(int regNum)
{
	// This takes a register number, does the necessary probe of the RTC,
	// and returns the data in EAX

	int interrupts = 0;
	unsigned char data = 0;

	// Suspend interrupts
	processorSuspendInts(interrupts);

	// Wait until the clock is stable
	if (waitReady())
	{
		processorRestoreInts(interrupts);
		return (0);
	}

	// Now we have 244 us to read the data we want.  We'd better stop
	// talking and do it.  Disable NMI at the same time.
	data = (unsigned char)(regNum | 0x80);
	processorOutPort8(0x70, data);

	// Now read the data
	processorInPort8(0x71, data);

	// Reenable NMI
	processorOutPort8(0x70, 0x00);

	// Restore interrupts
	processorRestoreInts(interrupts);

	// The data is in BCD format.  Convert it to decimal.
	return ((int)((((data & 0xF0) >> 4) * 10) + (data & 0x0F)));
}


static int driverReadSeconds(void)
{
	// This function returns the seconds value from the Real-Time clock.
	return (readRegister(SECSREG));
}


static int driverReadMinutes(void)
{
	// This function returns the minutes value from the Real-Time clock.
	return (readRegister(MINSREG));
}


static int driverReadHours(void)
{
	// This function returns the hours value from the Real-Time clock.
	return (readRegister(HOURREG));
}


static int driverReadDayOfMonth(void)
{
	// This function returns the day-of-month value from the Real-Time clock.
	return (readRegister(DOTMREG));
}


static int driverReadMonth(void)
{
	// This function returns the month value from the Real-Time clock.
	return (readRegister(MNTHREG));
}


static int driverReadYear(void)
{
	// This function returns the year value from the Real-Time clock.
	return (readRegister(YEARREG));
}


static int driverDetect(void *parent, kernelDriver *driver)
{
	// Normally, this function is used to detect and initialize each device,
	// as well as registering each one with any higher-level interfaces.
	// Since we can assume that there's an RTC, just initialize it.

	int status = 0;
	kernelDevice *dev = NULL;

	// Allocate memory for the device
	dev = kernelMalloc(sizeof(kernelDevice));
	if (!dev)
		return (status = 0);

	dev->device.class = kernelDeviceGetClass(DEVICECLASS_RTC);
	dev->driver = driver;

	// Initialize RTC operations
	status = kernelRtcInitialize(dev);
	if (status < 0)
	{
		kernelFree(dev);
		return (status);
	}

	// Add the kernel device
	return (status = kernelDeviceAdd(parent, dev));
}


static kernelRtcOps rtcOps = {
	driverReadSeconds,
	driverReadMinutes,
	driverReadHours,
	driverReadDayOfMonth,
	driverReadMonth,
	driverReadYear
};


/////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////
//
// Below here, the functions are exported for external use
//
/////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////

void kernelRtcDriverRegister(kernelDriver *driver)
{
	// Device driver registration.

	driver->driverDetect = driverDetect;
	driver->ops = &rtcOps;

	return;
}

