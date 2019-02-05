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
//  kernelUsbMouseDriver.c
//

// Driver for USB mice.

#include "kernelDriver.h"	// Contains my prototypes
#include "kernelUsbMouseDriver.h"
#include "kernelDebug.h"
#include "kernelError.h"
#include "kernelMalloc.h"
#include "kernelMouse.h"
#include "kernelVariableList.h"
#include <string.h>


static void interrupt(usbDevice *usbDev, int interface, void *buffer,
	unsigned length)
{
	usbMouse *mouseDev = usbDev->interface[interface].data;
	usbMouseData *mouseData = NULL;

	kernelDebug(debug_usb, "USB mouse interrupt %u bytes", length);

	if (length < sizeof(usbMouseData))
		return;

	mouseData = buffer;

	kernelDebug(debug_usb, "USB mouse buttons=%02x xChange=%d "
		"yChange=%d", mouseData->buttons, mouseData->xChange,
		mouseData->yChange);

	if (mouseData->buttons != mouseDev->oldMouseButtons)
	{
		// Look for changes in mouse button states

		// Left button; button 1
		if ((mouseData->buttons & USB_HID_MOUSE_LEFTBUTTON) !=
			(mouseDev->oldMouseButtons & USB_HID_MOUSE_LEFTBUTTON))
		{
			kernelMouseButtonChange(1, (mouseData->buttons &
				USB_HID_MOUSE_LEFTBUTTON));
		}

		// Middle button; button 2
		if ((mouseData->buttons & USB_HID_MOUSE_MIDDLEBUTTON) !=
			(mouseDev->oldMouseButtons & USB_HID_MOUSE_MIDDLEBUTTON))
		{
			kernelMouseButtonChange(2, (mouseData->buttons &
				USB_HID_MOUSE_MIDDLEBUTTON));
		}

		// Right button; button 3
		if ((mouseData->buttons & USB_HID_MOUSE_RIGHTBUTTON) !=
			(mouseDev->oldMouseButtons & USB_HID_MOUSE_RIGHTBUTTON))
		{
			kernelMouseButtonChange(3, (mouseData->buttons &
				USB_HID_MOUSE_RIGHTBUTTON));
		}

		// Save the current state
		mouseDev->oldMouseButtons = mouseData->buttons;
	}

	// Mouse movement.
	if (mouseData->xChange || mouseData->yChange)
		kernelMouseMove((int) mouseData->xChange, (int) mouseData->yChange);

	// Scroll wheel
	if ((length >= 4) && mouseData->devSpec[0])
		kernelMouseScroll((int) -((char) mouseData->devSpec[0]));
}


static int setBootProtocol(usbMouse *mouseDev, int interNum,
	kernelBusTarget *busTarget)
{
	usbTransaction usbTrans;

	kernelDebug(debug_usb, "USB mouse set boot protocol");

	// Tell the mouse to use the boot protocol.
	memset((void *) &usbTrans, 0, sizeof(usbTrans));
	usbTrans.type = usbxfer_control;
	usbTrans.address = mouseDev->usbDev->address;
	usbTrans.control.requestType = (USB_DEVREQTYPE_CLASS |
		USB_DEVREQTYPE_INTERFACE);
	usbTrans.control.request = USB_HID_SET_PROTOCOL;
	usbTrans.control.index = interNum;
	usbTrans.timeout = USB_STD_TIMEOUT_MS;

	// Write the command
	return (kernelBusWrite(busTarget, sizeof(usbTransaction),
		(void *) &usbTrans));
}


static int detectTarget(void *parent, int target, void *driver)
{
	int status = 0;
	usbMouse *mouseDev = NULL;
	kernelBusTarget *busTarget = NULL;
	int controller __attribute__((unused));
	int address __attribute__((unused));
	int interNum = 0;
	usbInterface *interface = NULL;
	usbEndpoint *endpoint = NULL;
	usbEndpoint *intrInEndp = NULL;
	int supported = 0;
	int count;

	// Get a mouse device structure
	mouseDev = kernelMalloc(sizeof(usbMouse));
	if (!mouseDev)
		return (status = ERR_MEMORY);

	// Get the bus target
	busTarget = kernelBusGetTarget(bus_usb, target);
	if (!busTarget)
	{
		status = ERR_NOSUCHENTRY;
		goto out;
	}

	// Get the USB device
	mouseDev->usbDev = kernelUsbGetDevice(target);
	if (!mouseDev->usbDev)
	{
		status = ERR_NOSUCHENTRY;
		goto out;
	}

	// Get the interface number
	usbMakeContAddrIntr(target, controller, address, interNum);

	interface = (usbInterface *) &mouseDev->usbDev->interface[interNum];

	// We support an an interface that indicates a USB class of 0x03 (HID),
	// protocol 0x02 (mouse), and is using the boot protocol (subclass 0x01).

	kernelDebug(debug_usb, "USB mouse HID device has %d interfaces",
		mouseDev->usbDev->numInterfaces);

	kernelDebug(debug_usb, "USB mouse checking interface %d", interNum);

	kernelDebug(debug_usb, "USB mouse class=0x%02x subclass=0x%02x "
		"protocol=0x%02x", interface->classCode, interface->subClassCode,
		interface->protocol);

	if ((interface->classCode != 0x03) || (interface->subClassCode != 0x01) ||
		(interface->protocol != 0x02))
	{
		// Not a supported interface
		goto out;
	}

	// Look for an interrupt-in endpoint
	for (count = 0; count < interface->numEndpoints; count ++)
	{
		endpoint = (usbEndpoint *) &interface->endpoint[count];

		if (((endpoint->attributes & USB_ENDP_ATTR_MASK) ==
			USB_ENDP_ATTR_INTERRUPT) && (endpoint->number & 0x80))
		{
			intrInEndp = endpoint;
			kernelDebug(debug_usb, "USB mouse got interrupt endpoint %02x",
				intrInEndp->number);
			break;
		}
	}

	// We *must* have an interrupt in endpoint.
	if (!intrInEndp)
	{
		kernelError(kernel_error, "Mouse device 0x%08x has no interrupt "
			"endpoint",	target);
		goto out;
	}

	// Set the device configuration
	status = kernelUsbSetDeviceConfig(mouseDev->usbDev);
	if (status < 0)
		goto out;

	interface->data = mouseDev;
	supported = 1;

	// Make sure it's set to use boot protocol.  Some devices, such as my
	// Microsoft composite wireless keyboard/mouse, actually do need this.
	status = setBootProtocol(mouseDev, interNum, busTarget);
	if (status < 0)
		goto out;

	// Schedule the regular interrupt.
	kernelUsbScheduleInterrupt(mouseDev->usbDev, interNum, intrInEndp->number,
		intrInEndp->interval, intrInEndp->maxPacketSize, &interrupt);

	// Tell USB that we're claiming this device.
	kernelBusDeviceClaim(busTarget, driver);

	// Set up the kernel device
	mouseDev->dev.device.class = kernelDeviceGetClass(DEVICECLASS_MOUSE);
	mouseDev->dev.device.subClass =
		kernelDeviceGetClass(DEVICESUBCLASS_MOUSE_USB);
	kernelUsbSetDeviceAttrs(mouseDev->usbDev, interNum, &mouseDev->dev);
	mouseDev->dev.driver = driver;

	// Add the kernel device
	status = kernelDeviceAdd(parent, &mouseDev->dev);

out:
	if (busTarget)
		kernelFree(busTarget);

	if ((status < 0) || !supported)
	{
		if (mouseDev)
			kernelFree(mouseDev);
	}
	else
	{
		kernelDebug(debug_usb, "USB mouse device detected");
	}

 	return (status);
}


static int detect(void *parent __attribute__((unused)), kernelDriver *driver)
{
	// This function is used to detect and initialize each device, as well as
	// registering each one with any higher-level interfaces.

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

		// Must be HID class
		if (usbDev.classCode != 0x03)
			continue;

		// Already claimed?
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
	// This function is used to detect whether a newly-connected, hotplugged
	// device is supported by this driver during runtime, and if so to do the
	// appropriate device setup and registration.  Alternatively if the device
	// is disconnected a call to this function lets us know to stop trying
	// to communicate with it.

	int status = 0;
	usbDevice *usbDev = NULL;
	int controller __attribute__((unused));
	int address __attribute__((unused));
	int interface = 0;
	usbMouse *mouseDev = NULL;

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

		mouseDev = usbDev->interface[interface].data;
		if (!mouseDev)
		{
			kernelError(kernel_error, "No such mouse device 0x%08x", target);
			return (status = ERR_NOSUCHENTRY);
		}

		// Found it.
		kernelDebug(debug_usb, "USB mouse device removed");

		// Remove it from the device tree
		kernelDeviceRemove(&mouseDev->dev);

		// Free the device's attributes list
		kernelVariableListDestroy(&mouseDev->dev.device.attrs);

		// Free the memory.
		kernelFree(mouseDev);
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

void kernelUsbMouseDriverRegister(kernelDriver *driver)
{
	// Device driver registration.

	driver->driverDetect = detect;
	driver->driverHotplug = hotplug;

	return;
}

