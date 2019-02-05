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
//  kernelUsbDriver.c
//

#include "kernelUsbDriver.h"
#include "kernelCpu.h"
#include "kernelDebug.h"
#include "kernelError.h"
#include "kernelInterrupt.h"
#include "kernelLinkedList.h"
#include "kernelLocale.h"
#include "kernelLog.h"
#include "kernelMalloc.h"
#include "kernelMultitasker.h"
#include "kernelPic.h"
#include "kernelVariableList.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/processor.h>

#define _(string) kernelGetText(string)

#ifdef DEBUG
static inline void debugDeviceDesc(usbDeviceDesc *deviceDesc)
{
	kernelDebug(debug_usb, "USB debug device descriptor:\n"
		"  descLength=%d\n"
		"  descType=%d\n"
		"  usbVersion=%d.%d\n"
		"  deviceClass=0x%02x\n"
		"  deviceSubClass=0x%02x\n"
		"  deviceProtocol=0x%02x\n"
		"  maxPacketSize0=%d\n"
		"  vendorId=0x%04x\n"
		"  productId=0x%04x\n"
		"  deviceVersion=%d.%d\n"
		"  manuStringIdx=%d\n"
		"  prodStringIdx=%d\n"
		"  serStringIdx=%d\n"
		"  numConfigs=%d", deviceDesc->descLength,
		deviceDesc->descType, ((deviceDesc->usbVersion & 0xFF00) >> 8),
		(deviceDesc->usbVersion & 0xFF), deviceDesc->deviceClass,
		deviceDesc->deviceSubClass, deviceDesc->deviceProtocol,
		deviceDesc->maxPacketSize0, deviceDesc->vendorId,
		deviceDesc->productId,
		((deviceDesc->deviceVersion & 0xFF00) >> 8),
		(deviceDesc->deviceVersion & 0xFF), deviceDesc->manuStringIdx,
		deviceDesc->prodStringIdx, deviceDesc->serStringIdx,
		deviceDesc->numConfigs);
}

static inline void debugDevQualDesc(usbDevQualDesc *devQualDesc)
{
	kernelDebug(debug_usb, "USB debug device qualifier descriptor:\n"
		"  descLength=%d\n"
		"  descType=%d\n"
		"  usbVersion=%d.%d\n"
		"  deviceClass=0x%02x\n"
		"  deviceSubClass=0x%02x\n"
		"  deviceProtocol=0x%02x\n"
		"  maxPacketSize0=%d\n"
		"  numConfigs=%d", devQualDesc->descLength,
		devQualDesc->descType, ((devQualDesc->usbVersion & 0xFF00) >> 8),
		(devQualDesc->usbVersion & 0xFF), devQualDesc->deviceClass,
		devQualDesc->deviceSubClass, devQualDesc->deviceProtocol,
		devQualDesc->maxPacketSize0, devQualDesc->numConfigs);
}

static inline void debugConfigDesc(usbConfigDesc *configDesc)
{
	kernelDebug(debug_usb, "USB debug config descriptor:\n"
		"  descLength=%d\n"
		"  descType=%d\n"
		"  totalLength=%d\n"
		"  numInterfaces=%d\n"
		"  confValue=%d\n"
		"  confStringIdx=%d\n"
		"  attributes=%d\n"
		"  maxPower=%d", configDesc->descLength, configDesc->descType,
		configDesc->totalLength, configDesc->numInterfaces,
		configDesc->confValue, configDesc->confStringIdx,
		configDesc->attributes, configDesc->maxPower);
}

static inline void debugInterDesc(usbInterDesc *interDesc)
{
	kernelDebug(debug_usb, "USB debug inter descriptor:\n"
		"  descLength=%d\n"
		"  descType=%d\n"
		"  interNum=%d\n"
		"  altSetting=%d\n"
		"  numEndpoints=%d\n"
		"  interClass=0x%02x\n"
		"  interSubClass=0x%02x\n"
		"  interProtocol=0x%02x\n"
		"  interStringIdx=%d", interDesc->descLength,
		interDesc->descType, interDesc->interNum, interDesc->altSetting,
		interDesc->numEndpoints, interDesc->interClass,
		interDesc->interSubClass, interDesc->interProtocol,
		interDesc->interStringIdx);
}

static inline void debugSuperEndpCompDesc(usbSuperEndpCompDesc *superComp)
{
	kernelDebug(debug_usb, "USB debug superspeed endpoint companion "
		"descriptor:\n"
		"  descLength=%d\n"
		"  descType=%d\n"
		"  maxBurst=%d", superComp->descLength, superComp->descType,
		superComp->maxBurst);
}

static inline void debugEndpointDesc(usbEndpointDesc *endpointDesc)
{
	kernelDebug(debug_usb, "USB debug endpoint descriptor:\n"
		"  descLength=%d\n"
		"  descType=%d\n"
		"  endpntAddress=0x%02x\n"
		"  attributes=%d\n"
		"  maxPacketSize=%d\n"
		"  interval=%d", endpointDesc->descLength,
		endpointDesc->descType, endpointDesc->endpntAddress,
		endpointDesc->attributes, endpointDesc->maxPacketSize,
		endpointDesc->interval);

	if (endpointDesc->superComp.descType == USB_DESCTYPE_SSENDPCOMP)
		debugSuperEndpCompDesc(&endpointDesc->superComp);
}

static inline void debugUsbDevice(usbDevice *usbDev)
{
	kernelDebug(debug_usb, "USB debug device:\n"
		"  device=%p\n"
		"  controller=%p (%d)\n"
		"  rootPort=%d\n"
		"  hubPort=%d\n"
		"  speed=%s\n"
		"  address=%d\n"
		"  usbVersion=%d.%d\n"
		"  classcode=0x%02x\n"
		"  subClassCode=0x%02x\n"
		"  protocol=0x%02x\n"
		"  vendorId=0x%04x\n"
		"  deviceId=0x%04x", usbDev, usbDev->controller,
		usbDev->controller->num, usbDev->rootPort, usbDev->hubPort,
		usbDevSpeed2String(usbDev->speed), usbDev->address,
		((usbDev->usbVersion & 0xFF00) >> 8),
		(usbDev->usbVersion & 0xFF), usbDev->classCode,
		usbDev->subClassCode, usbDev->protocol, usbDev->vendorId,
		usbDev->deviceId);
}
#else
	#define debugDeviceDesc(desc) do { } while (0)
	#define debugDevQualDesc(desc) do { } while (0)
	#define debugConfigDesc(desc) do { } while (0)
	#define debugInterDesc(desc) do { } while (0)
	#define debugSuperEndpCompDesc(desc) do { } while (0)
	#define debugEndpointDesc(desc) do { } while (0)
	#define debugUsbDevice(usbDev) do { } while (0)
#endif // DEBUG

static usbSubClass subclass_hid[] = {
	{ 0x01, "keyboard", DEVICECLASS_KEYBOARD, DEVICESUBCLASS_KEYBOARD_USB },
	{ 0x02, "mouse", DEVICECLASS_MOUSE, DEVICESUBCLASS_MOUSE_USB },
	{ USB_INVALID_SUBCLASSCODE, "", DEVICECLASS_NONE, DEVICESUBCLASS_NONE }
};

static usbSubClass subclass_massstorage[] = {
	{ 0x01, "flash", DEVICECLASS_STORAGE, DEVICESUBCLASS_STORAGE_FLASH },
	{ 0x02, "CD/DVD", DEVICECLASS_DISK, DEVICESUBCLASS_DISK_CDDVD },
	{ 0x03, "tape", DEVICECLASS_STORAGE, DEVICESUBCLASS_STORAGE_TAPE },
	{ 0x04, "floppy", DEVICECLASS_DISK, DEVICESUBCLASS_DISK_FLOPPY },
	{ 0x05, "floppy", DEVICECLASS_DISK, DEVICESUBCLASS_DISK_FLOPPY },
	{ 0x06, "SCSI", DEVICECLASS_DISK, DEVICESUBCLASS_DISK_SCSI },
	{ USB_INVALID_SUBCLASSCODE, "", DEVICECLASS_NONE, DEVICESUBCLASS_NONE }
};

static usbSubClass subclass_hub[] = {
	{ 0x00, "USB", DEVICECLASS_HUB, DEVICESUBCLASS_HUB_USB },
	{ USB_INVALID_SUBCLASSCODE, "", DEVICECLASS_NONE, DEVICESUBCLASS_NONE }
};

static usbSubClass subclass_vendor[] = {
	{ 0xFF, "unknown", DEVICECLASS_UNKNOWN, DEVICESUBCLASS_UNKNOWN_USB },
	{ USB_INVALID_SUBCLASSCODE, "", DEVICECLASS_NONE, DEVICESUBCLASS_NONE }
};

static usbClass usbClasses[] = {
	{ 0x01, "audio", NULL },
	{ 0x02, "CDC-control", NULL },
	{ 0x03, "human interface device", subclass_hid },
	{ 0x05, "physical", NULL },
	{ 0x06, "image", NULL },
	{ 0x07, "printer", NULL },
	{ 0x08, "mass storage", subclass_massstorage },
	{ 0x09, "hub", subclass_hub },
	{ 0x0A, "CDC-data", NULL },
	{ 0x0B, "chip/smart card", NULL },
	{ 0x0D, "content-security", NULL },
	{ 0x0E, "video", NULL },
	{ 0x0F, "personal healthcare", NULL },
	{ 0xDC, "diagnostic", NULL },
	{ 0xE0, "wireless controller", NULL },
	{ 0xEF, "miscellaneous", NULL },
	{ 0xFE, "application-specific", NULL },
	{ 0xFF, "vendor-specific", subclass_vendor },
	{ USB_INVALID_CLASSCODE, "", NULL }
};

static kernelLinkedList controllerList;
static kernelLinkedList hubList;
static kernelLinkedList deviceList;
static int usbThreadId = 0;

// Saved old interrupt handlers
static void **oldIntHandlers = NULL;
static int numOldHandlers = 0;


static void usbInterrupt(void)
{
	// This is the USB interrupt handler.

	void *address = NULL;
	int interruptNum = 0;
	usbController *controller = NULL;
	kernelLinkedListItem *iter = NULL;
	int serviced = 0;

	processorIsrEnter(address);

	// Which interrupt number is active?
	interruptNum = kernelPicGetActive();
	if (interruptNum < 0)
		goto out;

	kernelInterruptSetCurrent(interruptNum);

	//kernelDebug(debug_usb, "USB interrupt %d", interruptNum);

	// Search for controllers registered with this interrupt number.
	controller = kernelLinkedListIterStart(&controllerList, &iter);
	while (controller && !serviced)
	{
		if (controller->interruptNum == interruptNum)
		{
			//kernelDebug(debug_usb, "USB try controller %d", controller->num);
			if (controller->interrupt)
			{
				// See whether this controller is interrupting.  If not, it
				// must return the 'no data' error code.
				if (controller->interrupt(controller) != ERR_NODATA)
				{
					//kernelDebug(debug_usb, "USB interrupt serviced");
					serviced = 1;
				}
			}
		}

		controller = kernelLinkedListIterNext(&controllerList, &iter);
	}

	if (serviced)
		kernelPicEndOfInterrupt(interruptNum);

	kernelInterruptClearCurrent();

	if (!serviced)
	{
		if (oldIntHandlers[interruptNum])
		{
			// We didn't service this interrupt, and we're sharing this PCI
			// interrupt with another device whose handler we saved.  Call it.
			kernelDebug(debug_usb, "USB interrupt not serviced - chaining");
			processorIsrCall(oldIntHandlers[interruptNum]);
		}
		else
		{
			// We'd better acknowledge the interrupt, or else it wouldn't be
			// cleared, and our controllers using this vector wouldn't receive
			// any more.
			kernelDebugError("Interrupt not serviced and no saved ISR");
			kernelPicEndOfInterrupt(interruptNum);
		}
	}

out:
	processorIsrExit(address);
}


__attribute__((noreturn))
static void usbThread(void)
{
	kernelLinkedListItem *iter = NULL;
	usbHub *hub = NULL;

	while (1)
	{
		kernelMultitaskerYield();

		// Call applicable thread calls for all the hubs
		hub = kernelLinkedListIterStart(&hubList, &iter);
		while (hub)
		{
			if (hub->threadCall)
				hub->threadCall(hub);

			hub = kernelLinkedListIterNext(&hubList, &iter);
		}
	}
}


static void deviceInfo2BusTarget(usbDevice *usbDev, int interface,
	kernelBusTarget *target)
{
	// Translate a device to a bus target listing

	usbClass *class = NULL;
	usbSubClass *subClass = NULL;

	class = kernelUsbGetClass(usbDev->classCode);
	if (!class)
	{
		kernelDebugError("Target %p - no device class", target);
		return;
	}

	subClass = kernelUsbGetSubClass(class, usbDev->subClassCode,
		usbDev->protocol);

	target->bus = usbDev->controller->bus;
	target->id = usbMakeTargetCode(usbDev->controller->num, usbDev->address,
		interface);
	if (subClass)
	{
		target->class = kernelDeviceGetClass(subClass->systemClassCode);
		target->subClass = kernelDeviceGetClass(subClass->systemSubClassCode);
	}

	target->claimed = usbDev->interface[interface].claimed;
}


static int driverGetTargets(kernelBus *bus, kernelBusTarget **pointer)
{
	// Generate the list of targets that reside on the given bus (controller).

	kernelLinkedListItem *iter = NULL;
	usbDevice *usbDev = NULL;
	int targetCount = 0;
	kernelBusTarget *busTargets = NULL;
	int interCount;

	// Count the number of USB devices attached to the controller that owns
	// this bus
	usbDev = kernelLinkedListIterStart(&deviceList, &iter);
	while (usbDev)
	{
		if (usbDev->controller && (usbDev->controller->bus == bus))
		{
			for (interCount = 0; interCount < usbDev->numInterfaces;
				interCount ++)
			{
				// (Re-)allocate memory for the targets list
				busTargets = kernelRealloc(busTargets, ((targetCount + 1) *
					sizeof(kernelBusTarget)));
				if (!busTargets)
					return (targetCount = ERR_MEMORY);

				deviceInfo2BusTarget(usbDev, interCount,
					&busTargets[targetCount++]);
			}
		}

		usbDev = kernelLinkedListIterNext(&deviceList, &iter);
	}

	*pointer = busTargets;
	return (targetCount);
}


static int driverGetTargetInfo(kernelBusTarget *target, void *pointer)
{
	// Given a target number, copy the device's USB device info into the
	// supplied memory pointer

	int status = ERR_NOSUCHENTRY;
	kernelLinkedListItem *iter = NULL;
	usbDevice *usbDev = NULL;
	int count;

	usbDev = kernelLinkedListIterStart(&deviceList, &iter);
	while (usbDev)
	{
		for (count = 0; count < usbDev->numInterfaces; count ++)
		{
			if (usbMakeTargetCode(usbDev->controller->num, usbDev->address,
				count) == target->id)
			{
				memcpy(pointer, (void *) usbDev, sizeof(usbDevice));
				return (status = 0);
			}
		}

		usbDev = kernelLinkedListIterNext(&deviceList, &iter);
	}

	return (status);
}


static int transaction(int target, usbTransaction *trans, int numTrans)
{
	int status = 0;
	usbDevice *usbDev = NULL;

	kernelDebug(debug_usb, "USB do transaction for target 0x%08x", target);

	usbDev = kernelUsbGetDevice(target);
	if (!usbDev)
	{
		kernelError(kernel_error, "No such device");
		return (status = ERR_NOSUCHENTRY);
	}

#ifdef DEBUG
	char *className = NULL, *subClassName = NULL;
	if (kernelUsbGetClassName(usbDev->classCode, usbDev->subClassCode,
		usbDev->protocol, &className, &subClassName) >= 0)
	{
		kernelDebug(debug_usb, "USB (%s %s)", subClassName, className);
	}
#endif // DEBUG

	if (!usbDev->controller)
	{
		kernelError(kernel_error, "Device controller is NULL");
		return (status = ERR_NULLPARAMETER);
	}

	if (!usbDev->controller->queue)
	{
		kernelError(kernel_error, "Controller driver cannot queue "
			"transactions");
		return (status = ERR_NOTIMPLEMENTED);
	}

	status = usbDev->controller->queue(usbDev->controller, usbDev, trans,
		numTrans);

	return (status);
}


static int addController(kernelDevice *dev, int numControllers,
	kernelDriver *driver)
{
	int status = 0;
	usbController *controller = NULL;
	char value[32];

	controller = dev->data;
	controller->dev = dev;
	controller->num = numControllers;

	// Add any values we want in the attributes list
	sprintf(value, "%d", controller->interruptNum);
	kernelVariableListSet(&dev->device.attrs, "controller.interrupt", value);
	snprintf(value, 32, "%d.%d", ((controller->usbVersion & 0xF0) >> 4),
		(controller->usbVersion & 0xF));
	kernelVariableListSet(&dev->device.attrs, "controller.usbVersion", value);

	// Add it to our list of controllers
	status = kernelLinkedListAdd(&controllerList, (void *) controller);
	if (status < 0)
		return (status);

	kernelDebug(debug_usb, "USB %d controllers, %d hubs, %d devices",
		controllerList.numItems, hubList.numItems, deviceList.numItems);

	// Get memory for the bus service
	controller->bus = kernelMalloc(sizeof(kernelBus));
	if (!controller->bus)
		return (status = ERR_MEMORY);

	controller->bus->type = bus_usb;
	controller->bus->dev = dev;
	controller->bus->ops = driver->ops;

	// Register the bus service
	status = kernelBusRegister(controller->bus);
	if (status < 0)
		return (status);

	// Do we have an interrupt number?
	if (controller->interruptNum != 0xFF)
	{
		// Save any existing handler for the interrupt we're hooking

		if (numOldHandlers <= controller->interruptNum)
		{
			numOldHandlers = (controller->interruptNum + 1);

			oldIntHandlers = kernelRealloc(oldIntHandlers,
				(numOldHandlers * sizeof(void *)));
			if (!oldIntHandlers)
				return (status = ERR_MEMORY);
		}

		if (!oldIntHandlers[controller->interruptNum] &&
			(kernelInterruptGetHandler(controller->interruptNum) !=
				usbInterrupt))
		{
			oldIntHandlers[controller->interruptNum] =
				kernelInterruptGetHandler(controller->interruptNum);
		}

		// Register the interrupt handler
		status = kernelInterruptHook(controller->interruptNum, &usbInterrupt,
			NULL);
		if (status < 0)
			return (status);
	}
	else
	{
		// No interrupt number.  Inconvenient, but not necessarily fatal.
		kernelDebugError("No interrupt number for controller %d",
			controller->num);
	}

	return (status = 0);
}


static int driverDetect(void *parent __attribute__((unused)),
	kernelDriver *driver)
{
	// This function is called to detect USB buses.  There are a few different
	// types so we call further detection functions to do the actual hardware
	// interaction.

	int status = 0;
	kernelBusTarget *pciTargets = NULL;
	int numPciTargets = 0;
	int numControllers = 0;
	int deviceCount = 0;
	kernelDevice *dev = NULL;
	kernelLinkedListItem *iter = NULL;
	usbController *controller = NULL;

	memset(&controllerList, 0, sizeof(kernelLinkedList));
	memset(&hubList, 0, sizeof(kernelLinkedList));
	memset(&deviceList, 0, sizeof(kernelLinkedList));

	// See if there are any USB controllers on the PCI bus.  This obviously
	// depends upon PCI hardware detection occurring before USB detection.

	// Search the PCI bus(es) for devices
	numPciTargets = kernelBusGetTargets(bus_pci, &pciTargets);
	if (numPciTargets <= 0)
		return (status = numPciTargets);

	// Search the PCI bus targets for USB controllers

	// We must do EHCI controllers before UHCI controllers, as they need to
	// detect non-high-speed devices and release port ownership, so that the
	// UHCI companion controllers will detect them afterwards.
	for (deviceCount = 0; deviceCount < numPciTargets; deviceCount ++)
	{
		// If it's not a USB controller, skip it
		if (!pciTargets[deviceCount].class ||
			(pciTargets[deviceCount].class->class != DEVICECLASS_BUS) ||
			!pciTargets[deviceCount].subClass ||
			(pciTargets[deviceCount].subClass->class !=
				DEVICESUBCLASS_BUS_USB))
		{
			continue;
		}

		// See if it's an EHCI controller
		if ((dev = kernelUsbEhciDetect(&pciTargets[deviceCount], driver)))
		{
			// Add the controller
			status = addController(dev, numControllers++, driver);
			if (status < 0)
				kernelError(kernel_warn, "Couldn't add USB controller");
		}
	}

	// Now do the rest
	for (deviceCount = 0; deviceCount < numPciTargets; deviceCount ++)
	{
		// If it's not a USB controller, skip it
		if (!pciTargets[deviceCount].class ||
			(pciTargets[deviceCount].class->class != DEVICECLASS_BUS) ||
			!pciTargets[deviceCount].subClass ||
			(pciTargets[deviceCount].subClass->class !=
				DEVICESUBCLASS_BUS_USB))
		{
			continue;
		}

		// See if it's an XHCI controller
		if ((dev = kernelUsbXhciDetect(&pciTargets[deviceCount], driver)))
			; // empty

		// See if it's a UHCI controller
		else if ((dev = kernelUsbUhciDetect(&pciTargets[deviceCount], driver)))
			; // empty

		// See if it's an OHCI controller
		else if ((dev = kernelUsbOhciDetect(&pciTargets[deviceCount], driver)))
			; // empty

		else
			// Not a supported USB controller
			continue;

		// Add the controller
		status = addController(dev, numControllers++, driver);
		if (status < 0)
			kernelError(kernel_warn, "Couldn't add USB controller");
	}

	kernelFree(pciTargets);

	// For each detected controller, enable its interrupt and register its
	// root hub
	controller = kernelLinkedListIterStart(&controllerList, &iter);
	while (controller)
	{
		if (controller->interruptNum != 0xFF)
			// Turn on the interrupt
			kernelPicMask(controller->interruptNum, 1);

		// Add the controller's root hub to our list of hubs.  This is the
		// last step, and will trigger cold-plugged device detection
		kernelUsbAddHub(&controller->hub, 0 /* no hotplug */);

		controller = kernelLinkedListIterNext(&controllerList, &iter);
	}

	return (status);
}


static void driverDeviceClaim(kernelBusTarget *target, kernelDriver *driver)
{
	// Allows a driver to claim a USB bus device

	usbDevice *usbDev = NULL;
	int controller __attribute__((unused));
	int address __attribute__((unused));
	int interface = 0;

	if (!target || !driver)
	{
		kernelError(kernel_error, "NULL parameter");
		return;
	}

	// Find the USB device using the ID
	usbDev = kernelUsbGetDevice(target->id);
	if (!usbDev)
		return;

	usbMakeContAddrIntr(target->id, controller, address, interface)

	kernelDebug(debug_usb, "USB target 0x%08x claimed", target->id);
	usbDev->interface[interface].claimed = driver;

	return;
}


static int driverWrite(kernelBusTarget *target, unsigned size, void *params)
{
	// A wrapper for the 'transaction' function

	kernelDebug(debug_usb, "USB driver write");

	if (!target || !params)
	{
		kernelError(kernel_error, "Target or params are NULL");
		return (ERR_NULLPARAMETER);
	}

	return (transaction(target->id, (usbTransaction *) params,
		(size / sizeof(usbTransaction))));
}


static void removeDeviceRecursive(usbController *controller, usbHub *hub,
	usbDevice *usbDev)
{
	usbHub *removedHub = NULL;
	usbDevice *connectedDev = NULL;
	kernelLinkedListItem *iter = NULL;
	usbClass *class = NULL;
	usbSubClass *subClass = NULL;
	int count;

	// If the device is a hub, recurse to remove attached devices first
	if ((usbDev->classCode == 0x09) && !usbDev->subClassCode)
	{
		// Hubs only have one interface
		removedHub = usbDev->interface[0].data;

		connectedDev = kernelLinkedListIterStart((kernelLinkedList *)
			&removedHub->devices, &iter);

		while (connectedDev)
		{
			removeDeviceRecursive(controller, removedHub, connectedDev);

			connectedDev = kernelLinkedListIterNext((kernelLinkedList *)
				&removedHub->devices, &iter);
		}
	}

	class = kernelUsbGetClass(usbDev->classCode);
	subClass = kernelUsbGetSubClass(class, usbDev->subClassCode,
		usbDev->protocol);

	kernelDebug(debug_usb, "USB device %d disconnected (%s %s)",
		usbDev->address, (subClass? subClass->name : ""),
		(class? class->name : ""));

	if (controller->deviceRemoved)
		// Tell the controller that the device has disconnected
		controller->deviceRemoved(controller, usbDev);

	if (subClass)
	{
		for (count = 0; count < usbDev->numInterfaces; count ++)
		{
			// Tell the device hotplug function that the device has
			// disconnected
			kernelDeviceHotplug(controller->dev, subClass->systemSubClassCode,
				bus_usb, usbMakeTargetCode(controller->num, usbDev->address,
					count), 0 /* disconnected */);
		}
	}

	// Remove the device from the device list.
	kernelLinkedListRemove(&deviceList, (void *) usbDev);

	// Remove the device from the hub's list
	kernelLinkedListRemove((kernelLinkedList *) &hub->devices,
		(void *) usbDev);

	// If the device was a hub, remove it from our list of hubs
	if ((usbDev->classCode == 0x09) && !usbDev->subClassCode)
		kernelLinkedListRemove(&hubList, (void *) usbDev->interface[0].data);

	// Free the device memory
	if (usbDev->configDesc)
		kernelFree(usbDev->configDesc);

	kernelFree((void *) usbDev);

	return;
}


// Our bus operations structure.
static kernelBusOps usbOps = {
	driverGetTargets,
	driverGetTargetInfo,
	NULL, // driverReadRegister
	NULL, // driverWriteRegister
	driverDeviceClaim,
	NULL, // driverDeviceEnable
	NULL, // driverSetMaster
	NULL, // driverRead  (All USB transactions are 'write' transactions)
	driverWrite
};


/////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////
//
//  Below here, the functions are exported for external use
//
/////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////

void kernelUsbDriverRegister(kernelDriver *driver)
{
	// Device driver registration.

	driver->driverDetect = driverDetect;
	driver->ops = &usbOps;

	return;
}


int kernelUsbInitialize(void)
{
	// This gets called after multitasking is enabled.

	kernelLinkedListItem *iter = NULL;
	usbDevice *usbDev = NULL;

	// Loop through the devices that were detected at boot time, and see
	// whether we have any that weren't claimed by a driver
	usbDev = kernelLinkedListIterStart(&deviceList, &iter);
	while (usbDev)
	{
		kernelDebug(debug_usb, "USB device %p class=0x%02x sub=0x%02x "
			"proto=0x%02x %sclaimed", usbDev, usbDev->classCode,
			usbDev->subClassCode, usbDev->protocol,
			((usbDev->interface[0].claimed)? "" : "not "));

		usbDev = kernelLinkedListIterNext(&deviceList, &iter);
	}

	// Spawn the USB thread
	if (controllerList.numItems)
	{
		usbThreadId = kernelMultitaskerSpawnKernelThread(usbThread,
			"usb thread", 0, NULL);
	}

	return (0);
}


int kernelUsbShutdown(void)
{
	// Called at shutdown.  We do a reset of all registered controllers so there
	// won't be remnants of transactions on the buses messing things up (for
	// example if we're doing a soft reboot)

	kernelLinkedListItem *iter = NULL;
	usbController *controller = NULL;

	controller = kernelLinkedListIterStart(&controllerList, &iter);
	while (controller)
	{
		if (controller->reset)
			controller->reset(controller);

		controller = kernelLinkedListIterNext(&controllerList, &iter);
	}

	return (0);
}


usbClass *kernelUsbGetClass(int classCode)
{
	// Return the USB class, given the class code

	int count;

	for (count = 0; count < 256; count++)
	{
		// If no more classcodes are in the list
		if (usbClasses[count].classCode == USB_INVALID_CLASSCODE)
			return (NULL);

		// If valid classcode is found
		if (usbClasses[count].classCode == classCode)
			return (&usbClasses[count]);
	}

	return (NULL);
}


usbSubClass *kernelUsbGetSubClass(usbClass *class, int subClassCode,
	int protocol)
{
	// Return the USB subclass, given the class and subclass code

	int count;

	if (!class)
		return (NULL);

	// Some things are classified by protocol rather than subclass code
	if (class->classCode == 3)
		subClassCode = protocol;

	if (class->subClasses)
	{
		for (count = 0; count < 256; count++)
		{
			// If no more subclass codes are in the list
			if (class->subClasses[count].subClassCode ==
				USB_INVALID_SUBCLASSCODE)
			{
				return (NULL);
			}

			if (class->subClasses[count].subClassCode == subClassCode)
				return (&class->subClasses[count]);
		}
	}

	return (NULL);
}


int kernelUsbGetClassName(int classCode, int subClassCode, int protocol,
	char **className, char **subClassName)
{
	// Returns name of the class and the subclass in human readable format.
	// Buffers className and subClassName have to be provided.

	int status = 0;
	usbClass *class = NULL;
	usbSubClass *subClass = NULL;

	// Check params
	if (!className || !subClassName)
	{
		kernelError(kernel_error, "NULL parameter");
		return (status = ERR_NULLPARAMETER);
	}

	class = kernelUsbGetClass(classCode);
	if (!class)
	{
		*className = "unknown device";
		*subClassName = "";
		return (status = USB_INVALID_CLASSCODE);
	}

	*className = (char *) class->name;

	subClass = kernelUsbGetSubClass(class, subClassCode, protocol);
	if (!subClass)
	{
		*subClassName = "USB";
		return (status = USB_INVALID_SUBCLASSCODE);
	}

	*subClassName = (char *) subClass->name;
	return (status = 0);
}


void kernelUsbAddHub(usbHub *hub, int hotplug)
{
	if (kernelLinkedListAdd(&hubList, (void *) hub) < 0)
	{
		kernelDebugError("Couldn't add hub to list");
		return;
	}

	kernelDebug(debug_usb, "USB %d controllers, %d hubs, %d devices",
		controllerList.numItems, hubList.numItems, deviceList.numItems);

	// Do an initial device detection.  We can't assume it's OK for
	// USB devices to simply be added later when the first thread
	// call comes (for example, if we're booting from a USB stick,
	// it needs to be registered immediately)
	if (hub->detectDevices)
		hub->detectDevices(hub, hotplug);
}


int kernelUsbDevConnect(usbController *controller, usbHub *hub, int port,
	usbDevSpeed speed, int hotplug)
{
	// Enumerate a new device in respose to a port connection by assigning the
	// address, various descriptors, and setting the configuration.

	int status = 0;
	usbDevice *usbDev = NULL;
	unsigned bytes = 0;
	usbConfigDesc *tmpConfigDesc = NULL;
	usbInterDesc *interDesc = NULL;
	usbEndpointDesc *endpDesc = NULL;
	char *className = NULL;
	char *subClassName = NULL;
	usbClass *class = NULL;
	usbSubClass *subClass = NULL;
	void *ptr = NULL;
	int count1, count2, count3;

	// Check params
	if (!controller || !hub)
	{
		kernelError(kernel_error, "NULL parameter");
		return (status = ERR_NULLPARAMETER);
	}

	if (hub->usbDev)
	{
		kernelDebug(debug_usb, "USB device connection on controller %d, "
			"hub %d, port %d", controller->num, hub->usbDev->address, port);
	}
	else
	{
		kernelDebug(debug_usb, "USB device connection on controller %d, "
			"root hub port %d", controller->num, port);
	}

	// Get memory for the USB device
	usbDev = kernelMalloc(sizeof(usbDevice));
	if (!usbDev)
		return (status = ERR_MEMORY);

	usbDev->controller = controller;

	usbDev->hub = hub;
	if (hub->usbDev)
	{
		usbDev->rootPort = hub->usbDev->rootPort;
		usbDev->hubDepth = (hub->usbDev->hubDepth + 1);
		usbDev->hubPort = port;
		usbDev->routeString = ((((port + 1) & 0xF) <<
			(hub->usbDev->hubDepth * 4)) | hub->usbDev->routeString);

		kernelDebug(debug_usb, "USB hub depth=%d, route string=0x%05x",
			usbDev->hubDepth, usbDev->routeString);
	}
	else
	{
		usbDev->rootPort = port;
	}

	usbDev->speed = speed;

	// Set a default maximum packet size for control endpoint 0
	usbDev->endpoint0.maxPacketSize = 8;

	// Try getting a device descriptor of only 8 bytes.  Thereafter we will
	// *know* the supported packet size.
	kernelDebug(debug_usb, "USB get short device descriptor for new device");
	status = kernelUsbControlTransfer(usbDev, USB_GET_DESCRIPTOR,
		(USB_DESCTYPE_DEVICE << 8), 0, USB_PID_IN,
		usbDev->endpoint0.maxPacketSize, (void *) &usbDev->deviceDesc, NULL);
	if (status < 0)
	{
		kernelError(kernel_error, "Error getting short device descriptor");
		goto err_out;
	}

	// Do it again.  Some devices need this.
	kernelDebug(debug_usb, "USB get short device descriptor for new device");
	status = kernelUsbControlTransfer(usbDev, USB_GET_DESCRIPTOR,
		(USB_DESCTYPE_DEVICE << 8), 0, USB_PID_IN,
		usbDev->endpoint0.maxPacketSize, (void *) &usbDev->deviceDesc, NULL);
	if (status < 0)
	{
		kernelError(kernel_error, "Error getting short device descriptor");
		goto err_out;
	}

	debugDeviceDesc((usbDeviceDesc *) &usbDev->deviceDesc);

	usbDev->usbVersion = usbDev->deviceDesc.usbVersion;
	usbDev->classCode = usbDev->deviceDesc.deviceClass;
	usbDev->subClassCode = usbDev->deviceDesc.deviceSubClass;
	usbDev->protocol = usbDev->deviceDesc.deviceProtocol;

	// Save the maximum packet size for control endpoint 0
	usbDev->endpoint0.maxPacketSize = usbDev->deviceDesc.maxPacketSize0;
	if (!usbDev->endpoint0.maxPacketSize)
	{
		kernelError(kernel_error, "New device max packet size is 0");
		status = ERR_INVALID;
		goto err_out;
	}

	// Try to set a device address.

	kernelDebug(debug_usb, "USB set address %d for new device %p",
		(controller->addressCounter + 1), usbDev);

	status = kernelUsbControlTransfer(usbDev, USB_SET_ADDRESS,
		(controller->addressCounter + 1), 0, 0, 0, NULL, NULL);
	if (status < 0)
	{
		// No device waiting for an address, we guess
		kernelError(kernel_error, "Error setting device address");
		goto err_out;
	}

	// The device is now in the 'addressed' state.
	// 	N.B: The XHCI controller chooses its own address value, and our XHCI
	//	driver sets it in the device structure.
	if (controller->type != usb_xhci)
		usbDev->address = ++controller->addressCounter;

	// We're supposed to allow a 2ms delay for the device after the set
	// address command.
	kernelDebug(debug_usb, "USB delay after set_address");
	kernelCpuSpinMs(2);

	// Now get the whole device descriptor
	kernelDebug(debug_usb, "USB get full device descriptor for new device %d",
		usbDev->address);
	status = kernelUsbControlTransfer(usbDev, USB_GET_DESCRIPTOR,
		(USB_DESCTYPE_DEVICE << 8), 0, USB_PID_IN, sizeof(usbDeviceDesc),
		(void *) &usbDev->deviceDesc, NULL);
	if (status < 0)
	{
		kernelError(kernel_error, "Error getting device descriptor");
		goto err_out;
	}

	debugDeviceDesc((usbDeviceDesc *) &usbDev->deviceDesc);

	// Vendor and product IDs from the full descriptor
	usbDev->deviceId = usbDev->deviceDesc.productId;
	usbDev->vendorId = usbDev->deviceDesc.vendorId;

	// Get the short first configuration, the minimum size, which will tell
	// us how big the full descriptor will be

	tmpConfigDesc = kernelMalloc(sizeof(usbConfigDesc));
	if (!tmpConfigDesc)
	{
		status = ERR_MEMORY;
		goto err_out;
	}

	kernelDebug(debug_usb, "USB get short first configuration for new device "
		"%d", usbDev->address);
	bytes = 0;
	status = kernelUsbControlTransfer(usbDev, USB_GET_DESCRIPTOR,
		(USB_DESCTYPE_CONFIG << 8), 0, USB_PID_IN, sizeof(usbConfigDesc),
		tmpConfigDesc, &bytes);
	if ((status < 0) || (bytes < sizeof(usbConfigDesc)))
	{
		kernelError(kernel_error, "Error getting short configuration "
			"descriptor");
		goto err_out;
	}

	// Now that we know the total size of the configuration information, do
	// a second request that gets all of the data.

	usbDev->configDesc = kernelMalloc(tmpConfigDesc->totalLength);
	if (!usbDev->configDesc)
	{
		status = ERR_MEMORY;
		goto err_out;
	}

	kernelDebug(debug_usb, "USB get full first configuration for new device "
		"%d", usbDev->address);

	bytes = 0;
	status = kernelUsbControlTransfer(usbDev, USB_GET_DESCRIPTOR,
		(USB_DESCTYPE_CONFIG << 8), 0, USB_PID_IN, tmpConfigDesc->totalLength,
		usbDev->configDesc, &bytes);
	if (status < 0)
	{
		kernelError(kernel_error, "Error getting configuration descriptor");
		goto err_out;
	}

	// Don't bail if the device shorted us - it happens (not sure why)
	if (bytes < tmpConfigDesc->totalLength)
	{
		kernelDebugError("Config descriptor is %u bytes, but only got %u",
			tmpConfigDesc->totalLength, bytes);
		usbDev->configDesc->totalLength = bytes;
	}

	kernelFree(tmpConfigDesc);

	debugConfigDesc(usbDev->configDesc);

	usbDev->numInterfaces = usbDev->configDesc->numInterfaces;

	// Loop through the configuration descriptor and record information about
	// the interfaces and endpoints
	ptr = ((void *) usbDev->configDesc + usbDev->configDesc->descLength);
	for (count1 = 0; ((count1 < usbDev->numInterfaces) &&
		(count1 < USB_MAX_INTERFACES)); )
	{
		if (ptr >= ((void *) usbDev->configDesc +
			usbDev->configDesc->totalLength))
		{
			break;
		}

		interDesc = ptr;
		if (interDesc->descType != USB_DESCTYPE_INTERFACE)
		{
			ptr += interDesc->descLength;
			continue;
		}

		//debugInterDesc(interDesc);

		// Save the interface info we care about
		usbDev->interface[count1].classCode = interDesc->interClass;
		usbDev->interface[count1].subClassCode = interDesc->interSubClass;
		usbDev->interface[count1].protocol = interDesc->interProtocol;

		kernelDebug(debug_usb, "USB interface %d class=0x%02x subClass=0x%02x "
			"proto=0x%02x", count1, usbDev->interface[count1].classCode,
			usbDev->interface[count1].subClassCode,
			usbDev->interface[count1].protocol);

		// If the device descriptor was missing class/subclass/protocol info,
		// use what's in the first interface descriptor
		if (!count1 && !usbDev->classCode)
		{
			usbDev->classCode = usbDev->interface[count1].classCode;
			usbDev->subClassCode = usbDev->interface[count1].subClassCode;
			usbDev->protocol = usbDev->interface[count1].protocol;
		}

		// Move to the endoint descriptors
		ptr += interDesc->descLength;

		for (count2 = 0; ((count2 < interDesc->numEndpoints) &&
			(count2 < USB_MAX_ENDPOINTS)); )
		{
			if (ptr >= ((void *) usbDev->configDesc +
				usbDev->configDesc->totalLength))
			{
				break;
			}

			endpDesc = ptr;
			if (endpDesc->descType != USB_DESCTYPE_ENDPOINT)
			{
				ptr += endpDesc->descLength;
				continue;
			}

			//debugEndpointDesc(endpDesc);

			// Save the endpoint info we care about
			usbDev->interface[count1].endpoint[count2].number =
				endpDesc->endpntAddress;
			usbDev->interface[count1].endpoint[count2].attributes =
				endpDesc->attributes;
			usbDev->interface[count1].endpoint[count2].maxPacketSize =
				endpDesc->maxPacketSize;
			usbDev->interface[count1].endpoint[count2].interval =
				endpDesc->interval;

			kernelDebug(debug_usb, "USB endpoint 0x%02x attributes=0x%02x "
				"maxPacketSize=%d",
				usbDev->interface[count1].endpoint[count2].number,
				usbDev->interface[count1].endpoint[count2].attributes,
				usbDev->interface[count1].endpoint[count2].maxPacketSize);

			// USB3 superspeed endpoints only
			if ((usbDev->speed == usbspeed_super) &&
				(endpDesc->superComp.descType == USB_DESCTYPE_SSENDPCOMP))
			{
				usbDev->interface[count1].endpoint[count2].maxBurst =
					endpDesc->superComp.maxBurst;
			}

			ptr += endpDesc->descLength;
			usbDev->interface[count1].numEndpoints += 1;
			count2 += 1;
		}

		count1 += 1;
	}

	// After we've enumerated the interfaces and endpoints, construct an
	// easy-to-iterate list of unique endpoints
	kernelDebug(debug_usb, "USB construct endpoint list");
	usbDev->endpoint[usbDev->numEndpoints++] =
		(usbEndpoint *) &usbDev->endpoint0;
	for (count1 = 0; count1 < usbDev->numInterfaces; count1 ++)
	{
		for (count2 = 0; count2 < usbDev->interface[count1].numEndpoints;
			count2 ++)
		{
			for (count3 = 0; ; count3 ++)
			{
				if (count3 >= usbDev->numEndpoints)
				{
					usbDev->endpoint[usbDev->numEndpoints++] = (usbEndpoint *)
						&usbDev->interface[count1].endpoint[count2];
					break;
				}

				if (usbDev->interface[count1].endpoint[count2].number ==
					usbDev->endpoint[count3]->number)
				{
					break;
				}
			}
		}
	}

	kernelDebug(debug_usb, "USB %d unique endpoints", usbDev->numEndpoints);
	for (count1 = 0; count1 < usbDev->numEndpoints; count1 ++)
	{
		kernelDebug(debug_usb, "USB endpoint 0x%02x attributes=0x%02x "
			"maxPacketSize=%d", usbDev->endpoint[count1]->number,
			usbDev->endpoint[count1]->attributes,
			usbDev->endpoint[count1]->maxPacketSize);
	}

	debugUsbDevice(usbDev);

	// Ok, we will add this device.

	kernelDebug(debug_usb, "USB add device");
	status = kernelLinkedListAdd(&deviceList, (void *) usbDev);
	if (status < 0)
		goto err_out;

	kernelDebug(debug_usb, "USB %d controllers, %d hubs, %d devices",
		controllerList.numItems, hubList.numItems, deviceList.numItems);

	status = kernelLinkedListAdd((kernelLinkedList *) &hub->devices,
		(void *) usbDev);
	if (status < 0)
		goto err_out;

	kernelUsbGetClassName(usbDev->classCode, usbDev->subClassCode,
		usbDev->protocol, &className, &subClassName);

	kernelLog("USB: %s %s %u:%u dev:%04x, vend:%04x, class:%02x, "
		"sub:%02x proto:%02x usb:%d.%d", subClassName, className,
		usbDev->controller->num, usbDev->address, usbDev->deviceId,
		usbDev->vendorId, usbDev->classCode, usbDev->subClassCode,
		usbDev->protocol, (usbDev->usbVersion >> 8),
		((((usbDev->usbVersion >> 4) & 0xF) * 10) +
			(usbDev->usbVersion & 0xF)));

	if (hotplug)
	{
		for (count1 = 0; count1 < usbDev->numInterfaces; count1 ++)
		{
			// See about calling the appropriate hotplug detection functions
			// of the appropriate drivers

			class = kernelUsbGetClass((usbDev->interface[count1].classCode?
				usbDev->interface[count1].classCode : usbDev->classCode));

			subClass = kernelUsbGetSubClass(class,
				(usbDev->interface[count1].subClassCode?
					usbDev->interface[count1].subClassCode :
					usbDev->subClassCode),
				(usbDev->interface[count1].protocol?
					usbDev->interface[count1].protocol : usbDev->protocol));

			if (subClass)
			{
				status = kernelDeviceHotplug(controller->dev,
					subClass->systemSubClassCode, bus_usb,
					usbMakeTargetCode(usbDev->controller->num,
						usbDev->address, count1), 1 /* connected */);
				if (status < 0)
					return (status);
			}
		}
	}

	return (status = 0);

err_out:
	if (tmpConfigDesc)
		kernelFree(tmpConfigDesc);

	if (usbDev)
		removeDeviceRecursive(controller, hub, usbDev);

	return (status);
}


void kernelUsbDevDisconnect(usbController *controller, usbHub *hub, int port)
{
	// If the port status(es) indicate that a device has disconnected, figure
	// out which one it is and remove it from the root hub's list

	kernelLinkedListItem *iter = NULL;
	usbDevice *usbDev = NULL;

	kernelDebug(debug_usb, "USB device disconnection on controller %d hub %p "
		"port %d", controller->num, hub, port);
	kernelDebug(debug_usb, "USB hub %p has %d devices", hub,
		hub->devices.numItems);

	// Try to find the device
	usbDev = kernelLinkedListIterStart((kernelLinkedList *) &hub->devices,
		&iter);

	while (usbDev)
	{
		if ((!hub->usbDev && (usbDev->rootPort == port)) ||
			(hub->usbDev && (usbDev->hubPort == port)))
		{
			removeDeviceRecursive(controller, hub, usbDev);
			break;
		}

		usbDev = kernelLinkedListIterNext((kernelLinkedList *) &hub->devices,
			&iter);
	}

	if (usbDev)
	{
		kernelDebug(debug_usb, "USB %d controllers, %d hubs, %d devices",
			controllerList.numItems, hubList.numItems, deviceList.numItems);
	}
	else
	{
		kernelDebug(debug_usb, "USB no matching device");
	}

	return;
}


usbDevice *kernelUsbGetDevice(int target)
{
	usbDevice *usbDev = NULL;
	int controllerNum = 0;
	int address = 0;
	int interface __attribute__((unused));
	kernelLinkedListItem *iter = NULL;
	usbDevice *tmpUsbDev = NULL;

	// Break out the target information
	usbMakeContAddrIntr(target, controllerNum, address, interface);

	// Try to find the device
	tmpUsbDev = kernelLinkedListIterStart(&deviceList, &iter);
	while (tmpUsbDev)
	{
		if ((tmpUsbDev->controller->num == controllerNum) &&
			(tmpUsbDev->address == address))
		{
			usbDev = tmpUsbDev;
			break;
		}

		tmpUsbDev = kernelLinkedListIterNext(&deviceList, &iter);
	}

	if (!usbDev)
		kernelError(kernel_error, "No such device %d", address);

	return (usbDev);
}


usbEndpoint *kernelUsbGetEndpoint(usbDevice *usbDev, unsigned char number)
{
	// Searches through the list of endpoints for an endpoint with the
	// requested number.

	int count;

	// Check params
	if (!usbDev)
	{
		kernelError(kernel_error, "NULL parameter");
		return (NULL);
	}

	if (!number)
		// Endpoint 0 is created before the rest of the list is populated
		return ((usbEndpoint *) &usbDev->endpoint0);

	for (count = 0; count < usbDev->numEndpoints; count ++)
	{
		if (usbDev->endpoint[count]->number == number)
			return (usbDev->endpoint[count]);
	}

	// Not found
	return (NULL);
}


volatile unsigned char *kernelUsbGetEndpointDataToggle(usbDevice *usbDev,
	unsigned char number)
{
	// Try to find the endpoint data toggle for the given endpoint.

	volatile unsigned char *toggle = NULL;
	usbEndpoint *endpoint = NULL;

	// Check params
	if (!usbDev)
	{
		kernelError(kernel_error, "NULL parameter");
		return (toggle = NULL);
	}

	endpoint = kernelUsbGetEndpoint(usbDev, number);

	if (endpoint)
		toggle = &endpoint->dataToggle;

	return (toggle);
}


int kernelUsbSetDeviceConfig(usbDevice *usbDev)
{
	// Set the device configuration

	int status = 0;

	// Check params
	if (!usbDev || !usbDev->configDesc)
	{
		kernelError(kernel_error, "NULL parameter");
		return (status = ERR_NULLPARAMETER);
	}

	// Only set configuration once per physical device
	if (!usbDev->configured)
	{
		kernelDebug(debug_usb, "USB set configuration %d for new device %d",
			usbDev->configDesc->confValue, usbDev->address);

		status = kernelUsbControlTransfer(usbDev, USB_SET_CONFIGURATION,
			usbDev->configDesc->confValue, 0, 0, 0, NULL, NULL);

		if (status >= 0)
			usbDev->configured = 1;
	}

	return (status);
}


int kernelUsbSetupDeviceRequest(usbTransaction *trans, usbDeviceRequest *req)
{
	// Create a USB device request from the supplied USB transaction structure.

	int status = 0;
	const char *opString __attribute__((unused)) = NULL;

	// Check params
	if (!trans || !req)
	{
		kernelError(kernel_error, "NULL parameter");
		return (status = ERR_NULLPARAMETER);
	}

	kernelDebug(debug_usb, "USB set up device request");

	memset(req, 0, sizeof(usbDeviceRequest));

	req->requestType = trans->control.requestType;

	// Does the request go to an endpoint?
	if (trans->endpoint)
	{
		req->requestType |= USB_DEVREQTYPE_ENDPOINT;
		// trans->endpoint = 0;
	}

	req->request = trans->control.request;
	req->value = trans->control.value;
	req->index = trans->control.index;
	req->length = trans->length;

	if (req->requestType & (USB_DEVREQTYPE_CLASS | USB_DEVREQTYPE_VENDOR))
	{
		// The request is class- or vendor-specific
		opString = "class/vendor-specific control transfer";
	}
	else
	{
		// What request are we doing?  Determine the correct requestType and
		// whether there will be a data phase, etc.
		switch (trans->control.request)
		{
			case USB_GET_STATUS:
				opString = "USB_GET_STATUS";
				req->requestType |= USB_DEVREQTYPE_DEV2HOST;
				break;
			case USB_CLEAR_FEATURE:
				opString = "USB_CLEAR_FEATURE";
				req->requestType |= USB_DEVREQTYPE_HOST2DEV;
				break;
			case USB_SET_FEATURE:
				opString = "USB_SET_FEATURE";
				req->requestType |= USB_DEVREQTYPE_HOST2DEV;
				break;
			case USB_SET_ADDRESS:
				opString = "USB_SET_ADDRESS";
				req->requestType |= USB_DEVREQTYPE_HOST2DEV;
				break;
			case USB_GET_DESCRIPTOR:
				opString = "USB_GET_DESCRIPTOR";
				req->requestType |= USB_DEVREQTYPE_DEV2HOST;
				break;
			case USB_SET_DESCRIPTOR:
				opString = "USB_SET_DESCRIPTOR";
				req->requestType |= USB_DEVREQTYPE_HOST2DEV;
				break;
			case USB_GET_CONFIGURATION:
				opString = "USB_GET_CONFIGURATION";
				req->requestType |= USB_DEVREQTYPE_DEV2HOST;
				break;
			case USB_SET_CONFIGURATION:
				opString = "USB_SET_CONFIGURATION";
				req->requestType |= USB_DEVREQTYPE_HOST2DEV;
				break;
			case USB_GET_INTERFACE:
				opString = "USB_GET_INTERFACE";
				req->requestType |= USB_DEVREQTYPE_DEV2HOST;
				break;
			case USB_SET_INTERFACE:
				opString = "USB_SET_INTERFACE";
				req->requestType |= USB_DEVREQTYPE_HOST2DEV;
				break;
			case USB_SYNCH_FRAME:
				opString = "USB_SYNCH_FRAME";
				req->requestType |= USB_DEVREQTYPE_DEV2HOST;
				break;
			// Device-class-specific ones
			case USB_MASSSTORAGE_RESET:
				opString = "USB_MASSSTORAGE_RESET";
				req->requestType |= (USB_DEVREQTYPE_HOST2DEV |
					USB_DEVREQTYPE_CLASS |
					USB_DEVREQTYPE_INTERFACE);
			break;
			default:
				// Perhaps some thing we don't know about.  Try to proceed
				// anyway.
				opString = "unknown control transfer";
				break;
		}
	}

	if (req->requestType & USB_DEVREQTYPE_DEV2HOST)
		trans->pid = USB_PID_IN;
	else
		trans->pid = USB_PID_OUT;

	kernelDebug(debug_usb, "USB do %s for address %d:%02x", opString,
		trans->address, trans->endpoint);
	kernelDebug(debug_usb, "USB type=0x%02x, req=0x%02x, value=0x%02x, "
		"index=0x%02x, length=%d", req->requestType, req->request,
		req->value, req->index, req->length);

	return (status = 0);
}


int kernelUsbControlTransfer(usbDevice *usbDev, unsigned char request,
	unsigned short value, unsigned short index, unsigned char pid,
	unsigned short length, void *buffer, unsigned *bytes)
{
	// This is a convenience function for doing a control transfer, so that
	// callers (i.e. device drivers) don't have to construct a usbTransaction
	// structure manually.

	int status = 0;
	usbTransaction trans;

	// Check params
	if (!usbDev || !usbDev->controller)
	{
		kernelError(kernel_error, "NULL parameter");
		return (status = ERR_NULLPARAMETER);
	}

	if (!usbDev->controller->queue)
	{
		kernelError(kernel_error, "Controller cannot queue transactions");
		return (status = ERR_NOTIMPLEMENTED);
	}

	kernelDebug(debug_usb, "USB control transfer of %d bytes for address %d",
		length, usbDev->address);

	memset((void *) &trans, 0, sizeof(usbTransaction));
	trans.type = usbxfer_control;
	trans.address = usbDev->address;
	// trans.endpoint = 0; <- no need to re-clear it
	trans.control.request = request;
	trans.control.value = value;
	trans.control.index = index;
	trans.length = length;
	trans.buffer = buffer;
	trans.pid = pid;
	trans.timeout = USB_STD_TIMEOUT_MS;

	status = usbDev->controller->queue(usbDev->controller, usbDev, &trans, 1);

	if (bytes)
		*bytes = trans.bytes;

	return (status);
}


int kernelUsbScheduleInterrupt(usbDevice *usbDev, int interface,
	unsigned char endpoint, int interval, unsigned maxLen,
	void (*callback)(usbDevice *, int, void *, unsigned))
{
	// This is a function for scheduling a periodic interrupt transfer for a
	// device, with a callback to the caller (i.e. a device driver).

	int status = 0;

	// Check params
	if (!usbDev)
	{
		kernelError(kernel_error, "NULL parameter");
		return (status = ERR_NULLPARAMETER);
	}

	if (!usbDev->controller)
	{
		kernelError(kernel_error, "Device controller is NULL");
		return (status = ERR_NULLPARAMETER);
	}

	if (!usbDev->controller->schedInterrupt)
	{
		kernelError(kernel_error, "Controller cannot schedule interrupts");
		return (status = ERR_NOTIMPLEMENTED);
	}

	if (interval < 1)
	{
		kernelError(kernel_error, "Interrupt intervals must be 1 or greater");
		return (status = ERR_RANGE);
	}

	status = usbDev->controller->schedInterrupt(usbDev->controller, usbDev,
		interface, endpoint, interval, maxLen, callback);

	return (status);
}


int kernelUsbSetDeviceAttrs(usbDevice *usbDev, int interface,
	kernelDevice *dev)
{
	// Initialize the variable list for device attributes, and add a few
	// generic things.

	int status = 0;
	unsigned char class;
	unsigned char subClass;
	unsigned char protocol;
	char *className = NULL;
	char *subClassName = NULL;
	char value[80];

	status = kernelVariableListCreate(&dev->device.attrs);
	if (status < 0)
		return (status);

	class = (usbDev->interface[interface].classCode?
		usbDev->interface[interface].classCode : usbDev->classCode);
	subClass = (usbDev->interface[interface].subClassCode?
		usbDev->interface[interface].subClassCode : usbDev->subClassCode);
	protocol = (usbDev->interface[interface].protocol?
		usbDev->interface[interface].protocol : usbDev->protocol);

	kernelUsbGetClassName(class, subClass, protocol, &className,
		&subClassName);

	snprintf(value, 80, "0x%02x (%s)", class, className);
	kernelVariableListSet(&dev->device.attrs, "usb.class", value);

	snprintf(value, 80, "0x%02x (%s)", subClass, subClassName);
	kernelVariableListSet(&dev->device.attrs, "usb.subclass", value);

	snprintf(value, 80, "0x%02x", protocol);
	kernelVariableListSet(&dev->device.attrs, "usb.protocol", value);

	snprintf(value, 80, "%d", (usbDev->rootPort + 1));
	kernelVariableListSet(&dev->device.attrs, "usb.rootport", value);

	if (usbDev->hub->usbDev)
	{
		snprintf(value, 80, "%d", (usbDev->hubPort + 1));
		kernelVariableListSet(&dev->device.attrs, "usb.hubport", value);
	}

	snprintf(value, 80, "%d", usbDev->address);
	kernelVariableListSet(&dev->device.attrs, "usb.address", value);

	kernelVariableListSet(&dev->device.attrs, "usb.speed",
		usbDevSpeed2String(usbDev->speed));

	snprintf(value, 80, "%d.%d", (usbDev->usbVersion >> 8),
		((((usbDev->usbVersion >> 4) & 0xF) * 10) +
			(usbDev->usbVersion & 0xF)));
	kernelVariableListSet(&dev->device.attrs, "usb.version", value);

	return (status = 0);
}

