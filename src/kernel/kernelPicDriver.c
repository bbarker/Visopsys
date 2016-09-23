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
//  kernelPicDriver.c
//

// Driver for standard Programmable Interrupt Controllers (PIC)

#include "kernelDriver.h" // Contains my prototypes
#include "kernelDebug.h"
#include "kernelDevice.h"
#include "kernelError.h"
#include "kernelInterrupt.h"
#include "kernelMalloc.h"
#include "kernelPic.h"
#include <string.h>
#include <sys/processor.h>


/////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////
//
//  Standard PIC driver functions
//
/////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////

static int driverGetVector(kernelPic *pic __attribute__((unused)),
	int intNumber)
{
	// All vector numbers are sequential from INTERRUPT_VECTORSTART
	return (INTERRUPT_VECTORSTART + intNumber);
}


static int driverEndOfInterrupt(kernelPic *pic __attribute__((unused)),
	int intNumber)
{
	// Sends end of interrupt (EOI) commands to one or both of the PICs.
	// Our parameter should be the number of the interrupt.  If the number
	// is greater than 7, we will issue EOI to both the slave and master
	// controllers.  Otherwise, just the master.

	if (intNumber > 0x07)
		// Issue an end-of-interrupt (EOI) to the slave PIC
		processorOutPort8(0xA0, 0x20);

	// Issue an end-of-interrupt (EOI) to the master PIC
	processorOutPort8(0x20, 0x20);

	return (0);
}


static int driverMask(kernelPic *pic __attribute__((unused)), int intNumber,
	int on)
{
	// This masks or unmasks an interrupt.  Our parameters should be the number
	// of the interrupt vector, and an on/off value.

	unsigned char data = 0;

	if (intNumber <= 0x07)
	{
		intNumber = (0x01 << intNumber);

		// Get the current mask value
		processorInPort8(0x21, data);

		// An enabled interrupt has its mask bit off
		if (on)
			data &= ~intNumber;
		else
			data |= intNumber;

		processorOutPort8(0x21, data);
	}
	else
	{
		intNumber = (0x01 << (intNumber - 0x08));

		// Get the current mask value
		processorInPort8(0xA1, data);

		// An enabled interrupt has its mask bit off
		if (on)
			data &= ~intNumber;
		else
			data |= intNumber;

		processorOutPort8(0xA1, data);
	}

	return (0);
}


static int driverGetActive(kernelPic *pic __attribute__((unused)))
{
	// Returns the number of the active interrupt

	unsigned char data = 0;
	int intNumber = 0;

	// First ask the master pic
	processorOutPort8(0x20, 0x0B);
	processorInPort8(0x20, data);

	if (!data)
		return (intNumber = ERR_NODATA);

	while (!((data >> intNumber) & 1))
		intNumber += 1;

	// Is it actually the slave PIC?
	if (intNumber == 2)
	{
		// Ask the slave PIC which interrupt
		processorOutPort8(0xA0, 0x0B);
		processorInPort8(0xA0, data);

		if (!data)
			return (intNumber = ERR_NODATA);

		intNumber = 8;
		while (!((data >> (intNumber - 8)) & 1))
			intNumber += 1;
	}

	return (intNumber);
}


static int driverDisable(kernelPic *pic)
{
	// Disable the PICs by masking everything off.  This gets called when we
	// are using I/O APICs instead.

	kernelDebug(debug_io, "PIC disabling 8259s");

	processorOutPort8(0xA1, 0xFF);
	processorOutPort8(0x21, 0xFF);

	pic->enabled = 0;

	return (0);
}


static int driverDetect(void *parent, kernelDriver *driver)
{
	// Normally, this routine is used to detect and initialize each device,
	// as well as registering each one with any higher-level interfaces.  Since
	// we can assume that there's a PIC, just initialize it.

	int status = 0;
	kernelPic *pic = NULL;
	kernelDevice *dev = NULL;

	// The master controller

	// Initialization byte 1 - init
	processorOutPort8(0x20, 0x11);
	// Initialization byte 2 - starting vector
	processorOutPort8(0x21, INTERRUPT_VECTORSTART);
	// Initialization byte 3 - slave at IRQ2
	processorOutPort8(0x21, 0x04);
	// Initialization byte 4 - 8086/88 mode
	processorOutPort8(0x21, 0x01);
	// Normal operation, normal priorities
	processorOutPort8(0x20, 0x20);
	// Mask all ints off initially, except for 2 (the slave controller)
	processorOutPort8(0x21, 0xFB);

	// The slave controller

	// Initialization byte 1 - init
	processorOutPort8(0xA0, 0x11);
	// Initialization byte 2 - starting vector
	processorOutPort8(0xA1, (INTERRUPT_VECTORSTART + 8));
	// Initialization byte 3 - cascade ID
	processorOutPort8(0xA1, 0x02);
	// Initialization byte 4 - 8086/88 mode
	processorOutPort8(0xA1, 0x01);
	// Normal operation, normal priorities
	processorOutPort8(0xA0, 0x20);
	// Mask all ints off initially
	processorOutPort8(0xA1, 0xFF);

	// Allocate memory for the PIC
	pic = kernelMalloc(sizeof(kernelPic));
	if (!pic)
		return (status = ERR_MEMORY);

	pic->type = pic_8259;
	pic->enabled = 1;
	pic->startIrq = 0;
	pic->numIrqs = 16;
	pic->driver = driver;

	// Add the PIC
	status = kernelPicAdd(pic);
	if (status < 0)
	{
		kernelFree(dev);
		return (status);
	}

	// Allocate memory for the device
	dev = kernelMalloc(sizeof(kernelDevice));
	if (!dev)
		return (status = ERR_MEMORY);

	dev->device.class = kernelDeviceGetClass(DEVICECLASS_INTCTRL);
	dev->device.subClass = kernelDeviceGetClass(DEVICESUBCLASS_INTCTRL_PIC);
	dev->driver = driver;

	// Add the kernel device
	return (status = kernelDeviceAdd(parent, dev));
}


static kernelPicOps picOps = {
	NULL,	// driverGetIntNumber
	driverGetVector,
	driverEndOfInterrupt,
	driverMask,
	driverGetActive,
	driverDisable
};


/////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////
//
//  Below here, the functions are exported for external use
//
/////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////

void kernelPicDriverRegister(kernelDriver *driver)
{
	 // Device driver registration.

	driver->driverDetect = driverDetect;
	driver->ops = &picOps;

	return;
}

