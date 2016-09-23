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
//  kernelDevice.c
//

#include "kernelDevice.h"
#include "kernelDebug.h"
#include "kernelError.h"
#include "kernelLog.h"
#include "kernelMalloc.h"
#include "kernelText.h"
#include "kernelVariableList.h"
#include <stdio.h>
#include <string.h>

// An array of device classes, with names
static kernelDeviceClass allClasses[] = {
	{ DEVICECLASS_CPU,		"CPU"						},
	{ DEVICECLASS_MEMORY,	"memory"					},
	{ DEVICECLASS_SYSTEM,	"system"					},
	{ DEVICECLASS_POWER,	"power management"			},
	{ DEVICECLASS_BUS,		"bus controller"			},
	{ DEVICECLASS_BRIDGE,	"bridge"					},
	{ DEVICECLASS_INTCTRL,	"interrupt controller"		},
	{ DEVICECLASS_SYSTIMER,	"system timer"				},
	{ DEVICECLASS_RTC,		"real-time clock (RTC)"		},
	{ DEVICECLASS_DMA,		"DMA controller"			},
	{ DEVICECLASS_DISKCTRL,	"disk controller"			},
	{ DEVICECLASS_KEYBOARD,	"keyboard"					},
	{ DEVICECLASS_MOUSE,	"mouse"						},
	{ DEVICECLASS_TOUCHSCR,	"touchscreen"				},
	{ DEVICECLASS_DISK,		"disk"						},
	{ DEVICECLASS_GRAPHIC,	"graphic adapter"			},
	{ DEVICECLASS_NETWORK,	"network adapter"			},
	{ DEVICECLASS_HUB,		"hub"						},
	{ DEVICECLASS_UNKNOWN,	"unknown"					},
	{ 0, NULL											}
};

// An array of device subclasses, with names
static kernelDeviceClass allSubClasses[] = {
	{ DEVICESUBCLASS_CPU_X86,			"x86"					},
	{ DEVICESUBCLASS_SYSTEM_BIOS,		"BIOS"					},
	{ DEVICESUBCLASS_SYSTEM_BIOS32,		"BIOS (32-bit)"			},
	{ DEVICESUBCLASS_SYSTEM_BIOSPNP,	"BIOS (Plug and Play)"	},
	{ DEVICESUBCLASS_SYSTEM_MULTIPROC,	"multiprocessor"		},
	{ DEVICESUBCLASS_POWER_ACPI,		"ACPI"					},
	{ DEVICESUBCLASS_BUS_PCI,			"PCI"					},
	{ DEVICESUBCLASS_BUS_USB,			"USB"					},
	{ DEVICESUBCLASS_BRIDGE_PCI,		"PCI"					},
	{ DEVICESUBCLASS_BRIDGE_ISA,		"ISA"					},
	{ DEVICESUBCLASS_INTCTRL_PIC,		"PIC"					},
	{ DEVICESUBCLASS_INTCTRL_APIC,		"APIC"					},
	{ DEVICESUBCLASS_DISKCTRL_IDE,		"IDE"					},
	{ DEVICESUBCLASS_DISKCTRL_SATA,		"SATA"					},
	{ DEVICESUBCLASS_KEYBOARD_PS2,		"PS/2"					},
	{ DEVICESUBCLASS_KEYBOARD_USB,		"USB"					},
	{ DEVICESUBCLASS_MOUSE_PS2,			"PS/2"					},
	{ DEVICESUBCLASS_MOUSE_SERIAL,		"serial"				},
	{ DEVICESUBCLASS_MOUSE_USB,			"USB"					},
	{ DEVICESUBCLASS_TOUCHSCR_USB,		"USB"					},
	{ DEVICESUBCLASS_DISK_FLOPPY,		"floppy"				},
	{ DEVICESUBCLASS_DISK_IDE,			"IDE"					},
	{ DEVICESUBCLASS_DISK_SATA,			"SATA"					},
	{ DEVICESUBCLASS_DISK_SCSI,			"SCSI"					},
	{ DEVICESUBCLASS_DISK_CDDVD,		"CD/DVD"				},
	{ DEVICESUBCLASS_GRAPHIC_FRAMEBUFFER, "framebuffer"			},
	{ DEVICESUBCLASS_NETWORK_ETHERNET,	"ethernet"				},
	{ DEVICESUBCLASS_HUB_USB,			"USB"					},
	{ DEVICESUBCLASS_UNKNOWN_USB,		"USB"					},
	{ 0, NULL													}
};

// Our static list of built-in display drivers
static kernelDriver displayDrivers[] = {
	{ DEVICECLASS_GRAPHIC, DEVICESUBCLASS_GRAPHIC_FRAMEBUFFER,
		kernelFramebufferGraphicDriverRegister, NULL, NULL, NULL	},
	{ 0, 0, NULL, NULL, NULL, NULL									}
};

// Our static list of built-in drivers
static kernelDriver deviceDrivers[] = {
	{ DEVICECLASS_CPU, DEVICESUBCLASS_CPU_X86,
		kernelCpuDriverRegister, NULL, NULL, NULL						},
	{ DEVICECLASS_MEMORY, 0,
		kernelMemoryDriverRegister, NULL, NULL, NULL					},
	{ DEVICECLASS_SYSTEM, DEVICESUBCLASS_SYSTEM_BIOS,
		kernelBios32DriverRegister, NULL, NULL, NULL					},
	{ DEVICECLASS_SYSTEM, DEVICESUBCLASS_SYSTEM_BIOS,
		kernelBiosPnpDriverRegister, NULL, NULL, NULL					},
	{ DEVICECLASS_SYSTEM, DEVICESUBCLASS_SYSTEM_MULTIPROC,
		kernelMultiProcDriverRegister, NULL, NULL, NULL					},
	{ DEVICECLASS_POWER, DEVICESUBCLASS_POWER_ACPI,
		kernelAcpiDriverRegister, NULL, NULL, NULL						},
	// Do motherboard-type devices.  The PICs must be before most drivers,
	// specifically anything that uses interrupts (which is almost
	// everything)
	{ DEVICECLASS_INTCTRL, DEVICESUBCLASS_INTCTRL_PIC,
		kernelPicDriverRegister, NULL, NULL, NULL						},
	{ DEVICECLASS_INTCTRL, DEVICESUBCLASS_INTCTRL_APIC,
		kernelApicDriverRegister, NULL, NULL, NULL						},
	{ DEVICECLASS_SYSTIMER, 0,
		kernelSysTimerDriverRegister, NULL, NULL, NULL					},
	{ DEVICECLASS_RTC, 0, kernelRtcDriverRegister, NULL, NULL, NULL		},
	{ DEVICECLASS_DMA, 0, kernelDmaDriverRegister, NULL, NULL, NULL		},
	// Do buses before other non-motherboard devices, so that drivers can
	// find their devices on the buses.
	{ DEVICECLASS_BUS, DEVICESUBCLASS_BUS_PCI,
		kernelPciDriverRegister, NULL, NULL, NULL						},
	{ DEVICECLASS_BUS, DEVICESUBCLASS_BUS_USB,
		kernelUsbDriverRegister, NULL, NULL, NULL						},
	// Bridges should come right after buses, we guess
	{ DEVICECLASS_BRIDGE, DEVICESUBCLASS_BRIDGE_ISA,
		kernelIsaBridgeDriverRegister, NULL, NULL, NULL					},
	// Also do hubs before most other devices (same reason as above)
	{ DEVICECLASS_HUB, DEVICESUBCLASS_HUB_USB,
		kernelUsbHubDriverRegister, NULL, NULL, NULL					},
	// Do keyboards.  We do these fairly early in case we have a problem
	// and we need to interact with the user (even if it's just "boot
	// failed, press any key", etc)
	{ DEVICECLASS_KEYBOARD, DEVICESUBCLASS_KEYBOARD_PS2,
		kernelPs2KeyboardDriverRegister, NULL, NULL, NULL				},
	{ DEVICECLASS_KEYBOARD, DEVICESUBCLASS_KEYBOARD_USB,
		kernelUsbKeyboardDriverRegister, NULL, NULL, NULL				},
	// Then do disks and disk controllers
	{ DEVICECLASS_DISK, DEVICESUBCLASS_DISK_RAMDISK,
		kernelRamDiskDriverRegister, NULL, NULL, NULL					},
	{ DEVICECLASS_DISK, DEVICESUBCLASS_DISK_FLOPPY,
		kernelFloppyDriverRegister, NULL, NULL, NULL					},
	{ DEVICECLASS_DISK, DEVICESUBCLASS_DISK_SCSI,
		kernelScsiDiskDriverRegister, NULL, NULL, NULL					},
	{ DEVICECLASS_DISK, DEVICESUBCLASS_DISK_CDDVD,
		kernelUsbAtapiDriverRegister, NULL, NULL, NULL					},
	{ DEVICECLASS_DISKCTRL, DEVICESUBCLASS_DISKCTRL_SATA,
		kernelSataAhciDriverRegister, NULL, NULL, NULL					},
	{ DEVICECLASS_DISKCTRL, DEVICESUBCLASS_DISKCTRL_IDE,
		kernelIdeDriverRegister, NULL, NULL, NULL						},
	// Do the pointer devices after the graphic device so we can get screen
	// parameters, etc.  Also needs to be after the keyboard driver since
	// PS2 mouses use the keyboard controller.
	{ DEVICECLASS_MOUSE, DEVICESUBCLASS_MOUSE_PS2,
		kernelPs2MouseDriverRegister, NULL, NULL, NULL					},
	// USB mice and touchscreens can look very much alike in their HID
	// descriptors, but the mouse driver at least restricts itself to
	// claiming interfaces that declare themselves as using "boot mouse"
	// protocol.  The touchscreen driver is more promiscuous, so do mouse
	// first.
	{ DEVICECLASS_MOUSE, DEVICESUBCLASS_MOUSE_USB,
		kernelUsbMouseDriverRegister, NULL, NULL, NULL					},
	{ DEVICECLASS_TOUCHSCR, DEVICESUBCLASS_TOUCHSCR_USB,
		kernelUsbTouchscreenDriverRegister, NULL, NULL, NULL			},
	// Network and other non-critical (for basic operation) devices follow
	{ DEVICECLASS_NETWORK, DEVICESUBCLASS_NETWORK_ETHERNET,
		kernelPcNetDriverRegister, NULL, NULL, NULL						},
	// For creating kernel devices for unsupported things
	{ DEVICECLASS_UNKNOWN, DEVICESUBCLASS_UNKNOWN_USB,
		kernelUsbGenericDriverRegister, NULL, NULL, NULL				},
	{ 0, 0, NULL, NULL, NULL, NULL										}
};

// Our device tree
static kernelDevice *deviceTree = NULL;
static int numTreeDevices = 0;


static int isDevInTree(kernelDevice *root, kernelDevice *dev)
{
	// This is for checking device pointers passed in from user space to make
	// sure that they point to devices in our tree.

	while (root)
	{
		if (root == dev)
			return (1);

		if (root->device.firstChild)
		{
			if (isDevInTree(root->device.firstChild, dev) == 1)
				return (1);
		}

		root = root->device.next;
	}

	return (0);
}


static int findDeviceType(kernelDevice *dev, kernelDeviceClass *class,
	kernelDeviceClass *subClass, kernelDevice *devPointers[], int maxDevices,
	int numDevices)
{
	// Recurses through the device tree rooted at the supplied device and
	// returns the all instances of devices of the requested type

	while (dev)
	{
		if (numDevices >= maxDevices)
			return (numDevices);

		if ((dev->device.class == class) &&
			(!subClass || (dev->device.subClass == subClass)))
		{
			devPointers[numDevices++] = dev;
		}

		if (dev->device.firstChild)
		{
			numDevices += findDeviceType(dev->device.firstChild, class,
				subClass, devPointers, maxDevices, numDevices);
		}

		dev = dev->device.next;
	}

	return (numDevices);
}


static void device2user(kernelDevice *kernel, device *user)
{
	// Convert a kernelDevice structure to the user version

	const char *variable = NULL;
	int count;

	// Check params
	if (!kernel || !user)
	{
		kernelError(kernel_error, "NULL parameter");
		return;
	}

	kernelVariableListDestroy(&user->attrs);
	memset(user, 0, sizeof(device));

	if (kernel->device.class)
	{
		user->devClass.classNum = kernel->device.class->class;
		strncpy(user->devClass.name, kernel->device.class->name,
			DEV_CLASSNAME_MAX);
	}

	if (kernel->device.subClass)
	{
		user->subClass.classNum = kernel->device.subClass->class;
		strncpy(user->subClass.name, kernel->device.subClass->name,
			DEV_CLASSNAME_MAX);
	}

	kernelVariableListCreate(&user->attrs);
	for (count = 0; count < kernel->device.attrs.numVariables; count ++)
	{
		variable = kernelVariableListGetVariable(&kernel->device.attrs, count);
		if (variable)
			kernelVariableListSet(&user->attrs, variable,
				kernelVariableListGet(&kernel->device.attrs, variable));
	}

	user->parent = kernel->device.parent;
	user->firstChild = kernel->device.firstChild;
	user->previous = kernel->device.previous;
	user->next = kernel->device.next;
}


/////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////
//
//  Below here, the functions are exported for external use
//
/////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////

int kernelDeviceInitialize(void)
{
	// This function is called during startup so we can call the
	// driverRegister() functions of all our drivers

	int status = 0;
	int driverCount = 0;

	// Allocate a NULL 'system' device to build our device tree from
	deviceTree = kernelMalloc(sizeof(kernelDevice));
	if (!deviceTree)
		return (status = ERR_MEMORY);

	deviceTree->device.class = kernelDeviceGetClass(DEVICECLASS_SYSTEM);
	numTreeDevices = 1;

	// Loop through our static structures of built-in device drivers and
	// initialize them.

	for (driverCount = 0; displayDrivers[driverCount].class; driverCount ++)
	{
		if (displayDrivers[driverCount].driverRegister)
			displayDrivers[driverCount]
				.driverRegister(&displayDrivers[driverCount]);
	}

	for (driverCount = 0; deviceDrivers[driverCount].class; driverCount ++)
	{
		if (deviceDrivers[driverCount].driverRegister)
			deviceDrivers[driverCount]
				.driverRegister(&deviceDrivers[driverCount]);
	}

	return (status = 0);
}


int kernelDeviceDetectDisplay(void)
{
	// This function is called during startup so we can call the detect()
	// functions of all our display drivers

	int status = 0;
	kernelDeviceClass *class = NULL;
	kernelDeviceClass *subClass = NULL;
	char driverString[128];
	int driverCount = 0;

	// Loop for each hardware driver, and see if it has any devices for us
	for (driverCount = 0; displayDrivers[driverCount].class; driverCount ++)
	{
		class = kernelDeviceGetClass(displayDrivers[driverCount].class);

		subClass = NULL;
		if (displayDrivers[driverCount].subClass)
		{
			subClass =
				kernelDeviceGetClass(displayDrivers[driverCount].subClass);
		}

		driverString[0] = '\0';
		if (subClass)
			sprintf(driverString, "%s ", subClass->name);
		if (class)
			strcat(driverString, class->name);

		if (!displayDrivers[driverCount].driverDetect)
		{
			kernelError(kernel_error, "Device driver for \"%s\" has no "
				"'detect' function", driverString);
			continue;
		}

		status = displayDrivers[driverCount].driverDetect(deviceTree,
			&displayDrivers[driverCount]);
		if (status < 0)
		{
			kernelError(kernel_error, "Error %d detecting \"%s\" devices",
				status, driverString);
		}
	}

	return (status = 0);
}


int kernelDeviceDetect(void)
{
	// This function is called during startup so we can call the detect()
	// functions of all our general drivers

	int status = 0;
	int textNumColumns = 0;
	kernelDeviceClass *class = NULL;
	kernelDeviceClass *subClass = NULL;
	char driverString[128];
	int driverCount = 0;
	int count;

	kernelTextPrint("\n");
	textNumColumns = kernelTextGetNumColumns();

	// Loop for each hardware driver, and see if it has any devices for us
	for (driverCount = 0; deviceDrivers[driverCount].class; driverCount ++)
	{
		class = kernelDeviceGetClass(deviceDrivers[driverCount].class);

		subClass = NULL;
		if (deviceDrivers[driverCount].subClass)
		{
			subClass =
				kernelDeviceGetClass(deviceDrivers[driverCount].subClass);
		}

		driverString[0] = '\0';
		if (subClass)
			sprintf(driverString, "%s ", subClass->name);
		if (class)
			strcat(driverString, class->name);

		// Clear the current line
		kernelTextSetColumn(0);
		for (count = 0; count < (textNumColumns - 1); count ++)
			kernelTextPutc(' ');

		// Print a message
		kernelTextSetColumn(0);
		kernelTextPrint("Detecting hardware: %s ", driverString);

		if (!deviceDrivers[driverCount].driverDetect)
		{
			kernelError(kernel_error, "Device driver for \"%s\" has no "
				"'detect' function", driverString);
			continue;
		}

		status = deviceDrivers[driverCount].driverDetect(deviceTree,
			&deviceDrivers[driverCount]);
		if (status < 0)
		{
			kernelError(kernel_error, "Error %d detecting \"%s\" devices",
				status, driverString);
		}
	}

	// Clear our text
	kernelTextSetColumn(0);
	for (count = 0; count < (textNumColumns - 1); count ++)
		kernelTextPutc(' ');
	kernelTextSetColumn(0);

	return (status = 0);
}


kernelDeviceClass *kernelDeviceGetClass(int classNum)
{
	// Given a device (sub)class number, return a pointer to the static class
	// description

	kernelDeviceClass *classList = allClasses;
	int count;

	// Looking for a subclass?
	if (classNum & DEVICESUBCLASS_MASK)
		classList = allSubClasses;

	// Loop through the list
	for (count = 0; classList[count].class; count ++)
	{
		if (classList[count].class == classNum)
			return (&classList[count]);
	}

	// Not found
	return (NULL);
}


int kernelDeviceFindType(kernelDeviceClass *class, kernelDeviceClass *subClass,
	kernelDevice *devPointers[], int maxDevices)
{
	// Calls findDevice to return the first device it finds, with the
	// requested device class and subclass

	int status = 0;

	// Check params.  subClass can be NULL.
	if (!class || !devPointers)
		return (status = ERR_NULLPARAMETER);

	status = findDeviceType(deviceTree, class, subClass, devPointers,
		maxDevices, 0);

	return (status);
}


int kernelDeviceHotplug(kernelDevice *parent, int classNum, int busType,
	int target, int connected)
{
	// Call the hotplug detection routine for any driver that matches the
	// supplied class (and subclass).  This was added to support, for example,
	// USB devices that can be added or removed at any time.

	int status = 0;
	int count;

	kernelDebug(debug_device, "Device hotplug %sconnection",
		(connected? "" : "dis"));

	for (count = 0; deviceDrivers[count].class; count ++)
	{
		if ((classNum & DEVICECLASS_MASK) == deviceDrivers[count].class)
		{
			if (!(classNum & DEVICESUBCLASS_MASK) ||
				(classNum == deviceDrivers[count].subClass))
			{
				if (deviceDrivers[count].driverHotplug)
				{
					status = deviceDrivers[count].driverHotplug(parent,
						busType, target, connected, &deviceDrivers[count]);
				}
			}
		}
	}

	return (status);
}


int kernelDeviceAdd(kernelDevice *parent, kernelDevice *new)
{
	// Given a parent device, add the new device as a child

	int status = 0;
	kernelDevice *listPointer = NULL;
	const char *vendor = NULL;
	const char *model = NULL;
	char driverString[128];

	kernelDebug(debug_device, "Device add %p parent=%p", new, parent);

	// Check params
	if (!new)
	{
		kernelError(kernel_error, "NULL parameter");
		return (status = ERR_NULLPARAMETER);
	}

	// Make sure it isn't already here
	if (isDevInTree(deviceTree, new))
	{
		kernelError(kernel_error, "Device %p has already been added", new);
		return (status = ERR_ALREADY);
	}

	// NULL parent means use the root system device.
	if (!parent)
		parent = deviceTree;

	new->device.parent = parent;

	driverString[0] = '\0';

	vendor = kernelVariableListGet(&new->device.attrs, DEVICEATTRNAME_VENDOR);
	model = kernelVariableListGet(&new->device.attrs, DEVICEATTRNAME_MODEL);
	if (vendor || model)
	{
		if (vendor && model)
			sprintf(driverString, "\"%s %s\" ", vendor, model);
		else if (vendor)
			sprintf(driverString, "\"%s\" ", vendor);
		else if (model)
			sprintf(driverString, "\"%s\" ", model);
	}

	if (new->device.subClass)
	{
		sprintf((driverString + strlen(driverString)), "%s ",
			new->device.subClass->name);
	}

	if (new->device.class)
		strcat(driverString, new->device.class->name);

	new->device.firstChild = new->device.previous = new->device.next = NULL;

	// If the parent has no children, make this the first one.
	if (!parent->device.firstChild)
		parent->device.firstChild = new;

	else
	{
		// The parent has at least one child.  Follow the linked list to the
		// last child.
		listPointer = parent->device.firstChild;
		while (listPointer->device.next)
			listPointer = listPointer->device.next;

		// listPointer points to the last child.
		listPointer->device.next = new;
		new->device.previous = listPointer;
	}

	kernelLog("%s device detected", driverString);

	numTreeDevices += 1;
	return (status = 0);
}


int kernelDeviceRemove(kernelDevice *old)
{
	// Given a device, remove it from our tree

	int status = 0;
	kernelDevice *parent = NULL;
	kernelDevice *previous = NULL;
	kernelDevice *next = NULL;

	kernelDebug(debug_device, "Device remove %p", old);

	// Check params
	if (!old)
	{
		kernelError(kernel_error, "NULL parameter");
		return (status = ERR_NULLPARAMETER);
	}

	// Cannot remove devices that have children
	if (old->device.firstChild)
	{
		kernelError(kernel_error, "Cannot remove devices that have children");
		return (status = ERR_NULLPARAMETER);
	}

	parent = old->device.parent;
	previous = old->device.previous;
	next = old->device.next;

	// If this is the parent's first child, substitute the next device pointer
	// (whether or not it's NULL)
	if (parent && (parent->device.firstChild == old))
		parent->device.firstChild = next;

	// Connect our 'previous' and 'next' devices, as applicable.
	if (previous)
		previous->device.next = next;
	if (next)
		next->device.previous = previous;

	numTreeDevices -= 1;
	return (status = 0);
}


int kernelDeviceTreeGetRoot(device *rootDev)
{
	// Returns the user-space portion of the device tree root device

	int status = 0;

	// Are we initialized?
	if (!deviceTree)
		return (status = ERR_NOTINITIALIZED);

	// Check params
	if (!rootDev)
	{
		kernelError(kernel_error, "NULL parameter");
		return (status = ERR_NULLPARAMETER);
	}

	device2user(&deviceTree[0], rootDev);
	return (status = 0);
}


int kernelDeviceTreeGetChild(device *parentDev, device *childDev)
{
	// Returns the user-space portion of the supplied device's first child
	// device

	int status = 0;

	// Are we initialized?
	if (!deviceTree)
		return (status = ERR_NOTINITIALIZED);

	// Check params
	if (!parentDev || !childDev)
	{
		kernelError(kernel_error, "NULL parameter");
		return (status = ERR_NULLPARAMETER);
	}

	if (!parentDev->firstChild ||
		!isDevInTree(deviceTree, parentDev->firstChild))
	{
		return (status = ERR_NOSUCHENTRY);
	}

	device2user(parentDev->firstChild, childDev);
	return (status = 0);
}


int kernelDeviceTreeGetNext(device *dev)
{
	// Returns the user-space portion of the supplied device's 'next' (sibling)
	// device

	int status = 0;

	// Are we initialized?
	if (!deviceTree)
		return (status = ERR_NOTINITIALIZED);

	// Check params
	if (!dev)
	{
		kernelError(kernel_error, "NULL parameter");
		return (status = ERR_NULLPARAMETER);
	}

	if (!dev->next || !isDevInTree(deviceTree, dev->next))
		return (status = ERR_NOSUCHENTRY);

	device2user(dev->next, dev);
	return (status = 0);
}

