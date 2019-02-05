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
//  kernelBus.c
//

#include "kernelBus.h"
#include "kernelDebug.h"
#include "kernelError.h"
#include "kernelLinkedList.h"
#include "kernelMalloc.h"
#include <string.h>

static kernelLinkedList buses;
static int initialized = 0;

#ifdef DEBUG
static inline const char *busType2String(kernelBusType type)
{
	switch (type)
	{
		case bus_pci:
			return "PCI";
		case bus_usb:
			return "USB";
		default:
			return "unknown";
	}
}
#else
	#define busType2String(type) do { } while (0)
#endif


/////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////
//
//  Below here, the functions are exported for external use
//
/////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////

int kernelBusRegister(kernelBus *bus)
{
	int status = 0;

	// Check params
	if (!bus)
	{
		kernelError(kernel_error, "NULL parameter");
		return (status = ERR_NULLPARAMETER);
	}

	kernelDebug(debug_io, "BUS register new %s bus %p",
		busType2String(bus->type), bus);

	if (!initialized)
	{
		memset(&buses, 0, sizeof(kernelLinkedList));
		initialized = 1;
	}

	// Add the supplied device to our list of buses
	status = kernelLinkedListAdd(&buses, (void *) bus);
	if (status < 0)
		return (status);

	return (status = 0);
}


int kernelBusGetTargets(kernelBusType type, kernelBusTarget **pointer)
{
	// This is a wrapper for the bus-specific driver functions, but it will
	// aggregate a list of targets from all buses of the requested type.

	int status = 0;
	kernelLinkedListItem *iter = NULL;
	kernelBus *bus = NULL;
	kernelBusTarget *tmpTargets = NULL;
	int numTargets = 0;

	if (!initialized)
	{
		kernelDebugError("Bus functions not initialized");
		return (status = ERR_NOTINITIALIZED);
	}

	// Check params
	if (!pointer)
	{
		kernelError(kernel_error, "NULL parameter");
		return (status = ERR_NULLPARAMETER);
	}

	kernelDebug(debug_io, "BUS get %s targets", busType2String(type));

	*pointer = NULL;

	// Loop through all our buses and collect all the targets for buses
	// of the requested type
	bus = kernelLinkedListIterStart(&buses, &iter);
	while (bus)
	{
		if (bus->type == type)
		{
			kernelDebug(debug_io, "BUS found %s bus %p",
				busType2String(bus->type), bus);

			// Operation supported?
			if (!bus->ops->driverGetTargets)
			{
				kernelDebug(debug_io, "BUS %p doesn't support "
					"driverGetTargets()", bus);
				continue;
			}

			status = bus->ops->driverGetTargets(bus, &tmpTargets);
			if (status > 0)
			{
				kernelDebug(debug_io, "BUS found %d targets", status);

				*pointer = kernelRealloc(*pointer, ((numTargets + status) *
					sizeof(kernelBusTarget)));
				if (!(*pointer))
					return (status = ERR_MEMORY);

				memcpy(&((*pointer)[numTargets]), tmpTargets,
					(status * sizeof(kernelBusTarget)));

				numTargets += status;
				kernelFree(tmpTargets);
			}
		}

		bus = kernelLinkedListIterNext(&buses, &iter);
	}

	return (numTargets);
}


kernelBusTarget *kernelBusGetTarget(kernelBusType type, int id)
{
	// Get the target for the specified type and ID

	int numTargets = 0;
	kernelBusTarget *targets = NULL;
	kernelBusTarget *target = NULL;
	int count;

	if (!initialized)
	{
		kernelDebugError("Bus functions not initialized");
		return (target = NULL);
	}

	kernelDebug(debug_io, "BUS get %s target, id=0x%08x", busType2String(type),
		id);

	numTargets = kernelBusGetTargets(type, &targets);
	if (numTargets <= 0)
		return (target = NULL);

	for (count = 0; count < numTargets; count ++)
		kernelDebug(debug_io, "BUS target id=0x%08x", targets[count].id);

	for (count = 0; count < numTargets; count ++)
	{
		kernelDebug(debug_io, "BUS target id=0x%08x", targets[count].id);

		if (targets[count].id == id)
		{
			kernelDebug(debug_io, "BUS target found");

			target = kernelMalloc(sizeof(kernelBusTarget));
			if (!target)
				break;

			memcpy(target, &targets[count], sizeof(kernelBusTarget));
			break;
		}
	}

	kernelFree(targets);
	return (target);
}


int kernelBusGetTargetInfo(kernelBusTarget *target, void *pointer)
{
	// This is a wrapper for the bus-specific driver function

	int status = 0;

	if (!initialized)
	{
		kernelDebugError("Bus functions not initialized");
		return (status = ERR_NOTINITIALIZED);
	}

	// Check params
	if (!target || !pointer)
	{
		kernelError(kernel_error, "NULL parameter");
		return (status = ERR_NULLPARAMETER);
	}

	kernelDebug(debug_io, "BUS get target id=0x%08x info", target->id);

	if (!target->bus || !target->bus->ops)
	{
		kernelError(kernel_error, "Target bus pointer (%p) or ops (%p) is NULL",
			target->bus, (target->bus? target->bus->ops : NULL));
		return (status = ERR_NODATA);
	}

	// Operation supported?
	if (!target->bus->ops->driverGetTargetInfo)
	{
		kernelError(kernel_error, "Bus type %d doesn't support this function",
			target->bus->type);
		return (status = ERR_NOSUCHFUNCTION);
	}

	status = target->bus->ops->driverGetTargetInfo(target, pointer);
	return (status);
}


unsigned kernelBusReadRegister(kernelBusTarget *target, int reg, int bitWidth)
{
	// This is a wrapper for the bus-specific driver function

	unsigned contents = 0;

	if (!initialized)
	{
		kernelDebugError("Bus functions not initialized");
		return (0);
	}

	// Check params
	if (!target)
	{
		kernelError(kernel_error, "NULL parameter");
		return (contents = 0);
	}

	if (!target->bus || !target->bus->ops)
	{
		kernelError(kernel_error, "Target bus pointer (%p) or ops (%p) is NULL",
			target->bus, (target->bus? target->bus->ops : NULL));
		return (contents = 0);
	}

	// Operation supported?
	if (!target->bus->ops->driverReadRegister)
	{
		kernelError(kernel_error, "Bus type %d doesn't support this function",
			target->bus->type);
		return (contents = 0);
	}

	contents = target->bus->ops->driverReadRegister(target, reg, bitWidth);
	return (contents);
}


int kernelBusWriteRegister(kernelBusTarget *target, int reg, int bitWidth,
	unsigned contents)
{
	// This is a wrapper for the bus-specific driver function

	int status = 0;

	if (!initialized)
	{
		kernelDebugError("Bus functions not initialized");
		return (status = ERR_NOTINITIALIZED);
	}

	// Check params
	if (!target)
	{
		kernelError(kernel_error, "NULL parameter");
		return (status = ERR_NULLPARAMETER);
	}

	if (!target->bus || !target->bus->ops)
	{
		kernelError(kernel_error, "Target bus pointer (%p) or ops (%p) is NULL",
			target->bus, (target->bus? target->bus->ops : NULL));
		return (status = ERR_NODATA);
	}

	// Operation supported?
	if (!target->bus->ops->driverWriteRegister)
	{
		kernelError(kernel_error, "Bus type %d doesn't support this function",
			target->bus->type);
		return (status = ERR_NOSUCHFUNCTION);
	}

	status = target->bus->ops->driverWriteRegister(target, reg, bitWidth,
		contents);
	return (status);
}


void kernelBusDeviceClaim(kernelBusTarget *target, kernelDriver *driver)
{
	// This is a wrapper for the bus-specific driver function, called by a
	// device driver that wants to lay claim to a specific device.  This is
	// advisory-only.

	if (!initialized)
	{
		kernelDebugError("Bus functions not initialized");
		return;
	}

	// Check params
	if (!target || !driver)
	{
		kernelError(kernel_error, "NULL parameter");
		return;
	}

	if (!target->bus || !target->bus->ops)
	{
		kernelError(kernel_error, "Target bus pointer (%p) or ops (%p) is NULL",
			target->bus, (target->bus? target->bus->ops : NULL));
		return;
	}

	// Operation supported?
	if (!target->bus->ops->driverDeviceClaim)
	{
		kernelError(kernel_error, "Bus type %d doesn't support this function",
			target->bus->type);
		return;
	}

	target->bus->ops->driverDeviceClaim(target, driver);
	return;
}


int kernelBusDeviceEnable(kernelBusTarget *target, int enable)
{
	// This is a wrapper for the bus-specific driver function

	int status = 0;

	if (!initialized)
	{
		kernelDebugError("Bus functions not initialized");
		return (status = ERR_NOTINITIALIZED);
	}

	// Check params
	if (!target)
	{
		kernelError(kernel_error, "NULL parameter");
		return (status = ERR_NULLPARAMETER);
	}

	if (!target->bus || !target->bus->ops)
	{
		kernelError(kernel_error, "Target bus pointer (%p) or ops (%p) is NULL",
			target->bus, (target->bus? target->bus->ops : NULL));
		return (status = ERR_NODATA);
	}

	// Operation supported?
	if (!target->bus->ops->driverDeviceEnable)
	{
		kernelError(kernel_error, "Bus type %d doesn't support this function",
			target->bus->type);
		return (status = ERR_NOSUCHFUNCTION);
	}

	status = target->bus->ops->driverDeviceEnable(target, enable);
	return (status);
}


int kernelBusSetMaster(kernelBusTarget *target, int master)
{
	// This is a wrapper for the bus-specific driver function

	int status = 0;

	if (!initialized)
	{
		kernelDebugError("Bus functions not initialized");
		return (status = ERR_NOTINITIALIZED);
	}

	// Check params
	if (!target)
	{
		kernelError(kernel_error, "NULL parameter");
		return (status = ERR_NULLPARAMETER);
	}

	if (!target->bus || !target->bus->ops)
	{
		kernelError(kernel_error, "Target bus pointer (%p) or ops (%p) is NULL",
			target->bus, (target->bus? target->bus->ops : NULL));
		return (status = ERR_NODATA);
	}

	// Operation supported?
	if (!target->bus->ops->driverSetMaster)
	{
		kernelError(kernel_error, "Bus type %d doesn't support this function",
			target->bus->type);
		return (status = ERR_NOSUCHFUNCTION);
	}

	status = target->bus->ops->driverSetMaster(target, master);
	return (status);
}


int kernelBusRead(kernelBusTarget *target, unsigned size, void *buffer)
{
	// This is a wrapper for the bus-specific driver function

	int status = 0;

	if (!initialized)
	{
		kernelDebugError("Bus functions not initialized");
		return (status = ERR_NOTINITIALIZED);
	}

	// Check params
	if (!target || !buffer)
	{
		kernelError(kernel_error, "NULL parameter");
		return (status = ERR_NULLPARAMETER);
	}

	if (!target->bus || !target->bus->ops)
	{
		kernelError(kernel_error, "Target bus pointer (%p) or ops (%p) is NULL",
			target->bus, (target->bus? target->bus->ops : NULL));
		return (status = ERR_NODATA);
	}

	// Operation supported?
	if (!target->bus->ops->driverRead)
	{
		kernelError(kernel_error, "Bus type %d doesn't support this function",
			target->bus->type);
		return (status = ERR_NOSUCHFUNCTION);
	}

	return (target->bus->ops->driverRead(target, size, buffer));
}


int kernelBusWrite(kernelBusTarget *target, unsigned size, void *buffer)
{
	// This is a wrapper for the bus-specific driver function

	int status = 0;

	if (!initialized)
	{
		kernelDebugError("Bus functions not initialized");
		return (status = ERR_NOTINITIALIZED);
	}

	// Check params
	if (!target || !buffer)
	{
		kernelError(kernel_error, "NULL parameter");
		return (status = ERR_NULLPARAMETER);
	}

	if (!target->bus || !target->bus->ops)
	{
		kernelError(kernel_error, "Target bus pointer (%p) or ops (%p) is NULL",
			target->bus, (target->bus? target->bus->ops : NULL));
		return (status = ERR_NODATA);
	}

	// Operation supported?
	if (!target->bus->ops->driverWrite)
	{
		kernelError(kernel_error, "Bus type %d doesn't support this function",
			target->bus->type);
		return (status = ERR_NOSUCHFUNCTION);
	}

	return (target->bus->ops->driverWrite(target, size, buffer));
}

