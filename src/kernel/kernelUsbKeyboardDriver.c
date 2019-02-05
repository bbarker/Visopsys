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
//  kernelUsbKeyboardDriver.c
//

// Driver for USB keyboards.

#include "kernelDriver.h"	// Contains my prototypes
#include "kernelUsbKeyboardDriver.h"
#include "kernelCpu.h"
#include "kernelDebug.h"
#include "kernelError.h"
#include "kernelKeyboard.h"
#include "kernelMalloc.h"
#include "kernelVariableList.h"
#include <stdlib.h>
#include <string.h>
#include <sys/window.h>

// Flags for keyboard shift state
#define RALT_FLAG			0x0040
#define RSHIFT_FLAG			0x0020
#define RCONTROL_FLAG		0x0010
#define LALT_FLAG			0x0004
#define LSHIFT_FLAG			0x0002
#define LCONTROL_FLAG		0x0001

// Flags for keyboard toggle state
#define SCROLLLOCK_FLAG		0x04
#define CAPSLOCK_FLAG		0x02
#define NUMLOCK_FLAG		0x01

// Some USB keyboard scan codes we're interested in
#define CAPSLOCK_KEY		57
#define SCROLLLOCK_KEY		71
#define NUMLOCK_KEY			83

// Mapping of USB keyboard scan codes to EFI scan codes
static keyScan usbScan2Scan[] = { 0, 0, 0, 0,
	// A-G
	keyC1, keyB5, keyB3, keyC3, keyD3, keyC4, keyC5,			// 04-0A
	// H-N
	keyC6, keyD8, keyC7, keyC8, keyC9, keyB7, keyB6,			// 0B-11
	// O-U
	keyD9, keyD10, keyD1, keyD4, keyC2, keyD5, keyD7,			// 12-18
	// V-Z, 1-2
	keyB4, keyD2, keyB2, keyD6, keyB1, keyE1, keyE2, 			// 19-1F
	// 3-9
	keyE3, keyE4, keyE5, keyE6,	keyE7, keyE8, keyE9,			// 20-26
	// 0 Enter Esc Bs Tab
	keyE10, keyEnter, keyEsc, keyBackSpace, keyTab,				// 27-2B
	// Spc - = [ ] Bs
	keySpaceBar, keyE11, keyE12, keyD11, keyD12, keyB0,			// 2C-32
	// (INT 2) ; ' ` , .
	keyC12, keyC10, keyC11, keyE0, keyB8, keyB9,				// 32-37
	// / Caps, F1-F4
	keyB10, keyCapsLock, keyF1, keyF2, keyF3, keyF4,			// 38-3D
	// F5-F11
	keyF5, keyF6, keyF7, keyF8, keyF9, keyF10, keyF11,			// 3E-44
	// F12, PrtScn ScrLck PauseBrk Ins Home
	keyF12, keyPrint, keySLck, keyPause, keyIns, keyHome,		// 45-4A
	// PgUp Del End PgDn CurR
	keyPgUp, keyDel, keyEnd, keyPgDn, keyRightArrow,			// 4B-4F
	// CurL CurD CurU NumLck
	keyLeftArrow, keyDownArrow, keyUpArrow, keyNLck,			// 50-53
	// / * - + Enter
	keySlash, keyAsterisk, keyMinus, keyPlus, keyEnter,			// 54-58
	// End CurD PgDn CurL CurR
	keyOne, keyTwo, keyThree, keyFour, keyFive, keySix,			// 59-5E
	// Home CurU PgUp Ins Del (INT 1)
	keySeven, keyEight, keyNine, keyZero, keyDel, keyB0,		// 5F-64
	// Win
	keyA3, 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0	// 68-7F
};


static void setLights(usbKeyboard *keyDev)
{
	// This function is called to update the state of the keyboard lights

	usbTransaction usbTrans;
	unsigned char report = keyDev->keyboard.lights;

	kernelDebug(debug_usb, "USB keyboard set HID report %02x for target "
		"0x%08x, interface %d", report, keyDev->busTarget->id,
		keyDev->interface);

	// Send a "set report" command to the keyboard with the LED status.
	memset((void *) &usbTrans, 0, sizeof(usbTrans));
	usbTrans.type = usbxfer_control;
	usbTrans.address = keyDev->usbDev->address;
	usbTrans.control.requestType = (USB_DEVREQTYPE_HOST2DEV |
		USB_DEVREQTYPE_CLASS | USB_DEVREQTYPE_INTERFACE);
	usbTrans.control.request = USB_HID_SET_REPORT;
	usbTrans.control.value = (2 << 8);
	usbTrans.control.index = keyDev->interface;
	usbTrans.length = 1;
	usbTrans.buffer = &report;
	usbTrans.pid = USB_PID_OUT;
	usbTrans.timeout = USB_STD_TIMEOUT_MS;

	// Write the command
	kernelBusWrite(keyDev->busTarget, sizeof(usbTransaction),
		(void *) &usbTrans);
}


static void keyboardThreadCall(kernelKeyboard *keyboard)
{
	// Update keyboard lights to reflect the keyboard state

	usbKeyboard *keyDev = keyboard->data;
	unsigned lights = 0;
	uquad_t currentTime = 0;

	if (keyDev->keyboard.state.toggleState & KEYBOARD_SCROLL_LOCK_ACTIVE)
		lights |= SCROLLLOCK_FLAG;
	if (keyDev->keyboard.state.toggleState & KEYBOARD_CAPS_LOCK_ACTIVE)
		lights |= CAPSLOCK_FLAG;
	if (keyDev->keyboard.state.toggleState & KEYBOARD_NUM_LOCK_ACTIVE)
		lights |= NUMLOCK_FLAG;

	if (lights != keyboard->lights)
	{
		keyboard->lights = lights;
		setLights(keyDev);
	}

	if (keyboard->repeatKey)
	{
		currentTime = kernelCpuGetMs();

		if (currentTime >= keyboard->repeatTime)
		{
			kernelKeyboardInput(&keyDev->keyboard, EVENT_KEY_DOWN,
				keyboard->repeatKey);
			keyboard->repeatTime = (currentTime + 32);
		}
	}
}


static void interrupt(usbDevice *usbDev, int interface, void *buffer,
	unsigned length)
{
	usbKeyboard *keyDev = usbDev->interface[interface].data;
	usbKeyboardData *keyboardData = NULL;
	keyScan scan = 0;
	int count1, count2;

	//kernelDebug(debug_usb, "USB keyboard interrupt %u bytes", length);

	keyboardData = buffer;

	//kernelDebug(debug_usb, "USB keyboard mod=%02x codes %02x %02x %02x %02x "
	//	"%02x %02x", keyboardData->modifier, keyboardData->code[0],
	//	keyboardData->code[1], keyboardData->code[2], keyboardData->code[3],
	//	keyboardData->code[4], keyboardData->code[5]);

	// If the modifier flags have changed, we will need to send fake key
	// presses or releases for CTRL, ALT, or shift
	if (keyboardData->modifier != keyDev->oldKeyboardData.modifier)
	{
		if ((keyboardData->modifier & RALT_FLAG) !=
			(keyDev->oldKeyboardData.modifier & RALT_FLAG))
		{
			kernelKeyboardInput(&keyDev->keyboard,
				((keyboardData->modifier & RALT_FLAG)?
					EVENT_KEY_DOWN : EVENT_KEY_UP), keyA2);
		}

		if ((keyboardData->modifier & RSHIFT_FLAG) !=
			(keyDev->oldKeyboardData.modifier & RSHIFT_FLAG))
		{
			kernelKeyboardInput(&keyDev->keyboard,
				((keyboardData->modifier & RSHIFT_FLAG)?
					EVENT_KEY_DOWN : EVENT_KEY_UP), keyRShift);
		}

		if ((keyboardData->modifier & RCONTROL_FLAG) !=
			(keyDev->oldKeyboardData.modifier & RCONTROL_FLAG))
		{
			kernelKeyboardInput(&keyDev->keyboard,
				((keyboardData->modifier & RCONTROL_FLAG)?
					EVENT_KEY_DOWN : EVENT_KEY_UP), keyRCtrl);
		}

		if ((keyboardData->modifier & LALT_FLAG) !=
			(keyDev->oldKeyboardData.modifier & LALT_FLAG))
		{
			kernelKeyboardInput(&keyDev->keyboard,
				((keyboardData->modifier & LALT_FLAG)?
					EVENT_KEY_DOWN : EVENT_KEY_UP), keyLAlt);
		}

		if ((keyboardData->modifier & LSHIFT_FLAG) !=
			(keyDev->oldKeyboardData.modifier & LSHIFT_FLAG))
		{
			kernelKeyboardInput(&keyDev->keyboard,
				((keyboardData->modifier & LSHIFT_FLAG)?
					EVENT_KEY_DOWN : EVENT_KEY_UP), keyLShift);
		}

		if ((keyboardData->modifier & LCONTROL_FLAG) !=
			(keyDev->oldKeyboardData.modifier & LCONTROL_FLAG))
		{
			kernelKeyboardInput(&keyDev->keyboard,
				((keyboardData->modifier & LCONTROL_FLAG)?
					EVENT_KEY_DOWN : EVENT_KEY_UP), keyLCtrl);
		}
	}

	// Find key releases
	for (count1 = 0; count1 < USB_HID_KEYBOARD_BUFFSIZE; count1 ++)
	{
		if ((keyDev->oldKeyboardData.code[count1] < 4) ||
			(keyDev->oldKeyboardData.code[count1] > 231))
		{
			// Empty, or not a code that we are interested in
			continue;
		}

		// Does the old key press exist in the new data?
		for (count2 = 0; count2 < USB_HID_KEYBOARD_BUFFSIZE; count2 ++)
		{
			if (keyDev->oldKeyboardData.code[count1] ==
				keyboardData->code[count2])
			{
				break;
			}
		}

		if (count2 < USB_HID_KEYBOARD_BUFFSIZE)
			// The key is still pressed
			continue;

		scan = usbScan2Scan[keyDev->oldKeyboardData.code[count1]];

		kernelKeyboardInput(&keyDev->keyboard, EVENT_KEY_UP, scan);

		if (keyDev->keyboard.repeatKey == scan)
			keyDev->keyboard.repeatKey = 0;
	}

	// Find new keypresses
	for (count1 = 0; count1 < USB_HID_KEYBOARD_BUFFSIZE; count1 ++)
	{
		if ((keyboardData->code[count1] < 4) ||
			(keyboardData->code[count1] > 231))
		{
			// Empty, or not a code that we are interested in
			continue;
		}

		// Does the new key press exist in the old data?
		for (count2 = 0; count2 < USB_HID_KEYBOARD_BUFFSIZE; count2 ++)
		{
			if (keyboardData->code[count1] ==
				keyDev->oldKeyboardData.code[count2])
			{
				break;
			}
		}

		if (count2 < USB_HID_KEYBOARD_BUFFSIZE)
			// The key was already pressed
			continue;

		scan = usbScan2Scan[keyboardData->code[count1]];

		kernelKeyboardInput(&keyDev->keyboard, EVENT_KEY_DOWN, scan);

		keyDev->keyboard.repeatKey = scan;
		keyDev->keyboard.repeatTime = (kernelCpuGetMs() + 500);
	}

	if (length < sizeof(usbKeyboardData))
		memset(&keyDev->oldKeyboardData, 0, sizeof(usbKeyboardData));

	// Copy the new data to the 'old' buffer
	memcpy(&keyDev->oldKeyboardData, keyboardData,
		min(length, sizeof(usbKeyboardData)));
}


static int setBootProtocol(usbKeyboard *keyDev)
{
	usbTransaction usbTrans;

	kernelDebug(debug_usb, "USB keyboard set boot protocol");

	// Tell the keyboard to use the boot protocol.
	memset((void *) &usbTrans, 0, sizeof(usbTrans));
	usbTrans.type = usbxfer_control;
	usbTrans.address = keyDev->usbDev->address;
	usbTrans.control.requestType =
		(USB_DEVREQTYPE_CLASS | USB_DEVREQTYPE_INTERFACE);
	usbTrans.control.request = USB_HID_SET_PROTOCOL;
	usbTrans.control.index = keyDev->interface;
	usbTrans.timeout = USB_STD_TIMEOUT_MS;

	// Write the command
	return (kernelBusWrite(keyDev->busTarget, sizeof(usbTransaction),
		(void *) &usbTrans));
}


static int detectTarget(void *parent, int target, void *driver)
{
	int status = 0;
	usbKeyboard *keyDev = NULL;
	int controller __attribute__((unused));
	int address __attribute__((unused));
	int interNum = 0;
	usbInterface *interface = NULL;
	usbEndpoint *endpoint = NULL;
	usbEndpoint *intrInEndp = NULL;
	int supported = 0;
	int count;

	// Get a keyboard device structure
	keyDev = kernelMalloc(sizeof(usbKeyboard));
	if (!keyDev)
		return (status = ERR_MEMORY);

	// Get the bus target
	keyDev->busTarget = kernelBusGetTarget(bus_usb, target);
	if (!keyDev->busTarget)
	{
		status = ERR_NOSUCHENTRY;
		goto out;
	}

	// Get the USB device
	keyDev->usbDev = kernelUsbGetDevice(target);
	if (!keyDev->usbDev)
	{
		status = ERR_NOSUCHENTRY;
		goto out;
	}

	// Get the interface number
	usbMakeContAddrIntr(target, controller, address, interNum);

	interface = (usbInterface *) &keyDev->usbDev->interface[interNum];

	// We support an an interface that indicates a USB class of 0x03 (HID),
	// protocol 0x01 (keyboard), and is using the boot protocol (subclass
	// 0x01).

	kernelDebug(debug_usb, "USB keyboard HID device has %d interfaces",
		keyDev->usbDev->numInterfaces);

	kernelDebug(debug_usb, "USB keyboard checking interface %d", interNum);

	kernelDebug(debug_usb, "USB keyboard class=0x%02x subclass=0x%02x "
		"protocol=0x%02x", interface->classCode, interface->subClassCode,
		interface->protocol);

	if ((interface->classCode != 0x03) || (interface->subClassCode != 0x01) ||
		(interface->protocol != 0x01))
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
			kernelDebug(debug_usb, "USB keyboard got interrupt endpoint %02x",
				intrInEndp->number);
			break;
		}
	}

	// We *must* have an interrupt in endpoint.
	if (!intrInEndp)
	{
		kernelError(kernel_error, "Keyboard device 0x%08x has no interrupt "
			"endpoint",	target);
		goto out;
	}

	// Set the device configuration
	status = kernelUsbSetDeviceConfig(keyDev->usbDev);
	if (status < 0)
		goto out;

	keyDev->interface = interNum;
	interface->data = keyDev;
	supported = 1;

	// Make sure it's set to use boot protocol.  Some devices, such as my
	// Microsoft composite wireless keyboard/mouse, actually do need this.
	status = setBootProtocol(keyDev);
	if (status < 0)
		goto out;

	// Schedule the regular interrupt.
	kernelUsbScheduleInterrupt(keyDev->usbDev, interNum, intrInEndp->number,
		intrInEndp->interval, intrInEndp->maxPacketSize, &interrupt);

	// Tell USB that we're claiming this device.
	kernelBusDeviceClaim(keyDev->busTarget, driver);

	// Set up the kernel device
	keyDev->dev.device.class = kernelDeviceGetClass(DEVICECLASS_KEYBOARD);
	keyDev->dev.device.subClass =
		kernelDeviceGetClass(DEVICESUBCLASS_KEYBOARD_USB);
	kernelUsbSetDeviceAttrs(keyDev->usbDev, interNum, &keyDev->dev);
	keyDev->dev.driver = driver;

	// Set up the keyboard structure
	keyDev->keyboard.type = keyboard_usb;
	keyDev->keyboard.data = keyDev;
	keyDev->keyboard.threadCall = &keyboardThreadCall;

	// Add this keyboard to the keyboard subsystem
	status = kernelKeyboardAdd(&keyDev->keyboard);
	if (status < 0)
		goto out;

	// Add the kernel device
	status = kernelDeviceAdd(parent, &keyDev->dev);

out:
	if ((status < 0) || !supported)
	{
		if (keyDev)
		{
			if (keyDev->busTarget)
				kernelFree(keyDev->busTarget);

			kernelFree(keyDev);
		}
	}
	else
	{
		kernelDebug(debug_usb, "USB keyboard device detected");
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
	usbKeyboard *keyDev = NULL;

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

		keyDev = usbDev->interface[interface].data;
		if (!keyDev)
		{
			kernelError(kernel_error, "No such keyboard device 0x%08x",
				target);
			return (status = ERR_NOSUCHENTRY);
		}

		// Found it.
		kernelDebug(debug_usb, "USB keyboard device removed");

		// Remove it from the device tree
		kernelDeviceRemove(&keyDev->dev);

		// Free the device's attributes list
		kernelVariableListDestroy(&keyDev->dev.device.attrs);

		// Free the memory.
		if (keyDev->busTarget)
			kernelFree(keyDev->busTarget);

		kernelFree(keyDev);
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

void kernelUsbKeyboardDriverRegister(kernelDriver *driver)
{
	// Device driver registration.

	driver->driverDetect = detect;
	driver->driverHotplug = hotplug;

	return;
}

