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
//  kernelPic.c
//

#include "kernelPic.h"
#include "kernelDebug.h"
#include "kernelError.h"
#include <string.h>
#include <sys/processor.h>

static kernelPic *pics[MAX_PICS];
static int numPics = 0;


static void apicSetup(void)
{
	kernelPicOps *ops = NULL;
	int count;

	kernelDebug(debug_io, "PIC setting up for I/O APIC");

	// Disable any 8259 PICs
	for (count = 0; count < numPics; count ++)
	{
		if (pics[count]->enabled && (pics[count]->type == pic_8259))
		{
			ops = pics[count]->driver->ops;

			// Call the driver function
			if (ops->driverDisable)
				ops->driverDisable(pics[count]);
		}
	}
}


static kernelPic *findPic(int intNumber)
{
	int count;

	//kernelDebug(debug_io, "PIC find PIC for interrupt %d", intNumber);

	for (count = 0; count < numPics; count ++)
	{
		if (pics[count]->enabled &&
			(intNumber >= pics[count]->startIrq) &&
			(intNumber < (pics[count]->startIrq + pics[count]->numIrqs)))
		{
			//kernelDebug(debug_io, "PIC found - PIC %d", count);
			return (pics[count]);
		}
	}

	// Not found
	kernelDebug(debug_io, "PIC not found");
	return (NULL);
}


/////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////
//
//  Below here, the functions are exported for external use
//
/////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////

int kernelPicAdd(kernelPic *pic)
{
	int status = 0;

	// Check params
	if (!pic || !pic->driver || !pic->driver->ops)
		return (status = ERR_NULLPARAMETER);

	if (numPics >= MAX_PICS)
	{
		kernelError(kernel_error, "Max PICs (%d) has been reached", MAX_PICS);
		return (status = ERR_NOFREE);
	}

	// If the PIC is an (enabled) I/O APIC, we will disable any 8259 PIC and do
	// some additional setup
	if ((pic->type == pic_ioapic) && pic->enabled)
		apicSetup();

	// Add it to our list
	pics[numPics++] = pic;

	// Enable interrupts as soon as a good PIC is online.
	processorEnableInts();

	return (status = 0);
}


int kernelPicGetIntNumber(unsigned char busId, unsigned char busIrq)
{
	// This function will attempt to return the IRQ number assigned to the
	// supplied device, specified by bus ID and bus IRQ in the format
	// defined by the multiprocessor specification.

	int intNumber = 0;
	kernelPic *pic = NULL;
	kernelPicOps *ops = NULL;
	int count;

	kernelDebug(debug_io, "PIC request IRQ of device %d:%d", busId, busIrq);

	if (!numPics)
		return (intNumber = ERR_NOTINITIALIZED);

	for (count = 0; count < numPics; count ++)
	{
		if (pics[count]->enabled)
		{
			pic = pics[count];
			ops = pic->driver->ops;

			// Call the driver function
			if (ops->driverGetIntNumber)
			{
				intNumber = ops->driverGetIntNumber(pic, busId, busIrq);

				if (intNumber >= 0)
					return (intNumber);
			}
		}
	}

	// Nothing found
	return (intNumber = ERR_NODATA);
}


int kernelPicGetVector(int intNumber)
{
	// Different PIC types (5259, APIC) use different schemes for prioritizing
	// interrupts.  When a device driver hooks an interrupt, the generic
	// interrupt code will ask us which vector number to use.  We allow the
	// PIC driver code to provide the answer.

	int status = 0;
	kernelPic *pic = NULL;
	kernelPicOps *ops = NULL;

	kernelDebug(debug_io, "PIC get vector for interrupt %d", intNumber);

	if (!numPics)
		return (status = ERR_NOTINITIALIZED);

	pic = findPic(intNumber);
	if (!pic)
		return (status = ERR_NOSUCHENTRY);

	ops = pic->driver->ops;

	// Call the driver function
	if (ops->driverGetVector)
		status = ops->driverGetVector(pic, intNumber);

	return (status);
}


int kernelPicEndOfInterrupt(int intNumber)
{
	// This instructs the PIC to end the current interrupt.  Note that the
	// interrupt number parameter is merely so that the driver can determine
	// which controller(s) to send the command to.

	int status = 0;
	kernelPic *pic = NULL;
	kernelPicOps *ops = NULL;

	//kernelDebug(debug_io, "PIC EOI for interrupt %d", intNumber);

	if (!numPics)
		return (status = ERR_NOTINITIALIZED);

	pic = findPic(intNumber);
	if (!pic)
		return (status = ERR_NOSUCHENTRY);

	ops = pic->driver->ops;

	// Call the driver function
	if (ops->driverEndOfInterrupt)
		status = ops->driverEndOfInterrupt(pic, intNumber);

	return (status);
}


int kernelPicMask(int intNumber, int on)
{
	// This instructs the PIC to enable (on) or mask the interrupt.

	int status = 0;
	kernelPic *pic = NULL;
	kernelPicOps *ops = NULL;

	kernelDebug(debug_io, "PIC mask interrupt %d %s", intNumber,
		(on? "on" : "off"));

	if (!numPics)
		return (status = ERR_NOTINITIALIZED);

	pic = findPic(intNumber);
	if (!pic)
		return (status = ERR_NOSUCHENTRY);

	ops = pic->driver->ops;

	// Call the driver function
	if (ops->driverMask)
		status = ops->driverMask(pic, intNumber, on);

	return (status);
}


int kernelPicGetActive(void)
{
	// This asks the PIC for the currently-active interrupt

	int intNumber = 0;
	kernelPic *pic = NULL;
	kernelPicOps *ops = NULL;
	int count;

	kernelDebug(debug_io, "PIC active interrupt requested");

	if (!numPics)
		return (intNumber = ERR_NOTINITIALIZED);

	for (count = 0; count < numPics; count ++)
	{
		if (pics[count]->enabled)
		{
			pic = pics[count];
			ops = pic->driver->ops;

			// Call the driver function
			if (ops->driverGetActive)
				intNumber = ops->driverGetActive(pic);

			if (intNumber >= 0)
				return (intNumber);
		}
	}

	// Nothing found
	return (intNumber = ERR_NODATA);
}

