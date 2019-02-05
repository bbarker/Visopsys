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
//  kernelDma.c
//

// This file contains functions for DMA access, and functions for managing
// the installed DMA driver.

#include "kernelDma.h"
#include "kernelError.h"
#include <string.h>

static kernelDevice *systemDma = NULL;
static kernelDmaOps *ops = NULL;


/////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////
//
//  Below here, the functions are exported for external use
//
/////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////


int kernelDmaInitialize(kernelDevice *dev)
{
	// This function initializes the DMA controller.

	int status = 0;

	if (!dev)
	{
		kernelError(kernel_error, "NULL parameter");
		return (status = ERR_NOTINITIALIZED);
	}

	systemDma = dev;

	if (!systemDma->driver || !systemDma->driver->ops)
	{
		kernelError(kernel_error, "The DMA driver or ops are NULL");
		return (status = ERR_NULLPARAMETER);
	}

	ops = systemDma->driver->ops;

	return (status);
}


int kernelDmaOpenChannel(int channelNumber, void *address, int count, int mode)
{
	// This function is used to set up a DMA channel and prepare it to
	// read or write data.  It is a generic function which calls the
	// specific associated device driver function.

	int status = 0;

	if (!systemDma)
		return (status = ERR_NOTINITIALIZED);

	// Make sure the driver's "open channel" function has been initialized
	if (!ops->driverOpenChannel)
	{
		// Ooops.  Driver function is NULL.
		kernelError(kernel_error, "Driver function is NULL");
		return (status = ERR_NOSUCHFUNCTION);
	}

	status = ops->driverOpenChannel(channelNumber, address, count, mode);
	return (status);
}


int kernelDmaCloseChannel(int channelNumber)
{
	// This function is used to close a DMA channel after the desired
	// "read" or "write" operation has been completed.  It is a generic
	// function which calls the specific associated device driver function.

	int status = 0;

	if (!systemDma)
		return (status = ERR_NOTINITIALIZED);

	// Make sure the driver's "close channel" function has been initialized
	if (!ops->driverCloseChannel)
	{
		// Ooops.  Driver function is NULL.
		kernelError(kernel_error, "Driver function is NULL");
		return (status = ERR_NOSUCHFUNCTION);
	}

	status = ops->driverCloseChannel(channelNumber);
	return (status);
}

