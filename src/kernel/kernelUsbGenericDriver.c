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
//  kernelUsbGenericDriver.c
//

// Driver for USB devices that aren't claimed by any real driver

#include "kernelDriver.h"	// Contains my prototypes
#include "kernelDebug.h"
#include "kernelError.h"
#include "kernelMalloc.h"
#include "kernelUsbDriver.h"
#include "kernelVariableList.h"
#include <stdlib.h>


static int detectTarget(void *parent, int target, void *driver)
{
	int status = 0;
	kernelBusTarget *busTarget = NULL;
	usbDevice *usbDev = NULL;
	int controller __attribute__((unused));
	int address __attribute__((unused));
	int interNum = 0;
	usbInterface *interface = NULL;
	kernelDevice *dev = NULL;

	// Get the bus target
	busTarget = kernelBusGetTarget(bus_usb, target);
	if (!busTarget)
	{
		status = ERR_NOSUCHENTRY;
		goto out;
	}

	// Get the USB device
	usbDev = kernelUsbGetDevice(target);
	if (!usbDev)
	{
		status = ERR_NOSUCHENTRY;
		goto out;
	}

	// Get the interface number
	usbMakeContAddrIntr(target, controller, address, interNum);

	interface = (usbInterface *) &usbDev->interface[interNum];

	kernelDebug(debug_usb, "USB generic class=0x%02x subclass=0x%02x "
		"protocol=0x%02x", interface->classCode, interface->subClassCode,
		interface->protocol);

	// Get a device structure
	dev = kernelMalloc(sizeof(kernelDevice));
	if (!dev)
	{
		status = ERR_MEMORY;
		goto out;
	}

	interface->data = dev;

	// Tell USB that we're claiming this device.
	kernelBusDeviceClaim(busTarget, driver);

	// Set up the kernel device
	dev->device.class = kernelDeviceGetClass(DEVICECLASS_UNKNOWN);
	dev->device.subClass = kernelDeviceGetClass(DEVICESUBCLASS_UNKNOWN_USB);
	kernelUsbSetDeviceAttrs(usbDev, interNum, dev);
	dev->driver = driver;

	// Add the kernel device
	status = kernelDeviceAdd(parent, dev);

out:
	if (busTarget)
		kernelFree(busTarget);

	if (status < 0)
	{
		kernelVariableListDestroy(&dev->device.attrs);

		if (dev)
			kernelFree(dev);
	}

 	return (status);
}


static int detect(void *parent __attribute__((unused)), kernelDriver *driver)
{
	// Detect unclaimed USB device interfaces, and register kernel devices
	// for them.

	int status = 0;
	kernelBusTarget *busTargets = NULL;
	int numBusTargets = 0;
	int deviceCount = 0;
	usbDevice usbDev;

	// Search the USB bus(es) for devices
	numBusTargets = kernelBusGetTargets(bus_usb, &busTargets);
	if (numBusTargets <= 0)
		return (status = 0);

	// Search the bus targets for USB HID devices
	for (deviceCount = 0; deviceCount < numBusTargets; deviceCount ++)
	{
		// Try to get the USB information about the target
		status = kernelBusGetTargetInfo(&busTargets[deviceCount],
			(void *) &usbDev);
		if (status < 0)
			continue;

		// Claimed?
		if (busTargets[deviceCount].claimed)
			continue;

		detectTarget(usbDev.controller->dev, busTargets[deviceCount].id,
			driver);
	}

	kernelFree(busTargets);
	return (status = 0);
}


static int hotplug(void *parent, int busType __attribute__((unused)),
	int target, int connected, kernelDriver *driver)
{
	// An unclaimed USB device has been added or removed.

	int status = 0;
	usbDevice *usbDev = NULL;
	int controller __attribute__((unused));
	int address __attribute__((unused));
	int interface = 0;
	kernelDevice *dev = NULL;

	if (connected)
	{
		status = detectTarget(parent, target, driver);
		if (status < 0)
			return (status);
	}
	else
	{
		usbDev = kernelUsbGetDevice(target);
		if (!usbDev)
		{
			kernelError(kernel_error, "No such USB device 0x%08x", target);
			return (status = ERR_NOSUCHENTRY);
		}

		usbMakeContAddrIntr(target, controller, address, interface);

		dev = usbDev->interface[interface].data;
		if (!dev)
		{
			kernelError(kernel_error, "No such device 0x%08x", target);
			return (status = ERR_NOSUCHENTRY);
		}

		// Found it.
		kernelDebug(debug_usb, "USB generic device removed");

		// Remove it from the device tree
		kernelDeviceRemove(dev);

		// Free the device's attributes list
		kernelVariableListDestroy(&dev->device.attrs);

		kernelFree(dev);
	}

	return (status = 0);
}


/////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////
//
// Below here, the functions are exported for external use
//
/////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////

void kernelUsbGenericDriverRegister(kernelDriver *driver)
{
	// Device driver registration.

	driver->driverDetect = detect;
	driver->driverHotplug = hotplug;

	return;
}

