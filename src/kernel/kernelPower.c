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
//  kernelPower.c
//

#include "kernelPower.h"
#include "kernelError.h"
#include <string.h>

static kernelDevice *systemPower = NULL;
static kernelPowerOps *ops = NULL;


/////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////
//
//  Below here, the functions are exported for external use
//
/////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////


int kernelPowerInitialize(kernelDevice *dev)
{
	// This function initializes the power management functions.

	int status = 0;

	// Check params
	if (!dev)
	{
		kernelError(kernel_error, "NULL parameter");
		return (status = ERR_NOTINITIALIZED);
	}

	systemPower = dev;

	if (!systemPower->driver || !systemPower->driver->ops)
	{
		kernelError(kernel_error, "The power driver or ops are NULL");
		return (status = ERR_NULLPARAMETER);
	}

	ops = systemPower->driver->ops;

	// Return success
	return (status = 0);
}


int kernelPowerOff(void)
{
	// Try to turn off system power.

	int status = 0;

	if (!systemPower)
		return (status = ERR_NOTINITIALIZED);

	// Make sure the device driver 'power off' function has been installed
	if (!ops->driverPowerOff)
	{
		kernelError(kernel_error, "The device driver function is NULL");
		return (status = ERR_NOSUCHFUNCTION);
	}

	// Call the driver function
	status = ops->driverPowerOff(systemPower);
	return (status);
}

