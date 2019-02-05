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
//  kernelPs2KeyboardDriver.c
//

// Driver for standard PS/2 PC keyboards

#include "kernelDriver.h" // Contains my prototypes
#include "kernelCpu.h"
#include "kernelDebug.h"
#include "kernelDevice.h"
#include "kernelError.h"
#include "kernelInterrupt.h"
#include "kernelKeyboard.h"
#include "kernelMalloc.h"
#include "kernelPage.h"
#include "kernelParameters.h"
#include "kernelPic.h"
#include <string.h>
#include <sys/processor.h>
#include <sys/window.h>

#define KEYTIMEOUT			20 // ms

// Some special scan values that we care about
#define EXTENDED			0xE0
#define KEY_RELEASE			0x80
#define LEFT_CONTROL		0x1D

// Flags for keyboard state and lights
#define CAPSLOCK			0x04
#define NUMLOCK				0x02
#define SCROLLLOCK			0x01

static kernelKeyboard keyboard;

// Mapping of PC scan codes to EFI scan codes
static keyScan ps2Scan2Scan[] = { 0,
	// ~ top row
	keyEsc, keyE1, keyE2, keyE3, keyE4, keyE5, keyE6,			// 01-0E
	keyE7, keyE8, keyE9, keyE10, keyE11, keyE12, keyBackSpace,
	// ~ second row
	keyTab, keyD1, keyD2, keyD3, keyD4, keyD5, keyD6,			// 0F-1C
	keyD7, keyD8, keyD9, keyD10, keyD11, keyD12, keyEnter,
	// ~ third row
	keyLCtrl, keyC1, keyC2, keyC3, keyC4, keyC5, keyC6,			// 1D-28
	keyC7, keyC8, keyC9, keyC10, keyC11,
	// ~ fourth row
	keyE0, keyLShift, keyC12, keyB1, keyB2, keyB3, keyB4,		// 29-36
	keyB5, keyB6, keyB7, keyB8, keyB9, keyB10, keyRShift,
	// special keys, keypad, function keys
	keyAsterisk, keyLAlt, keySpaceBar, keyCapsLock, keyF1, 		// 37-44
	keyF2, keyF3, keyF4, keyF5, keyF6, keyF7, keyF8,
	keyF9, keyF10,
	// keypad
	keyNLck, keySLck, keySeven, keyEight, keyNine, keyMinus,	// 45-51
	keyFour, keyFive, keySix, keyPlus, keyOne, keyTwo, keyThree,
	// keypad, function keys, bottom row
	keyZero, keyDel, keyDel /*?*/, 0, keyB0, keyF11,			// 52-5D
	keyF12, 0, 0, keyA0, keyA3, keyA4
};


static inline int isData(void)
{
	// Return 1 if there's data waiting

	unsigned char status = 0;

	processorInPort8(0x64, status);

	if ((status & 0x21) == 0x01)
		return (1);
	else
		return (0);
}


static int inPort60(unsigned char *data)
{
	// Input a value from the keyboard controller's data port, after checking
	// to make sure that there's some data of the correct type waiting for us
	// (port 0x60).

	unsigned char status = 0;
	uquad_t currTime = kernelCpuGetMs();
	uquad_t endTime = (currTime + KEYTIMEOUT);

	// Wait until the controller says it's got data of the requested type
	while (currTime <= endTime)
	{
		if (isData())
		{
			processorInPort8(0x60, *data);
			return (0);
		}

		processorDelay();
		currTime = kernelCpuGetMs();
	}

	processorInPort8(0x64, status);
	kernelError(kernel_error, "Timeout reading port 60, port 64=%02x", status);
	return (ERR_TIMEOUT);
}


static int waitControllerReady(void)
{
	// Wait for the controller to be ready

	unsigned char status = 0;
	uquad_t currTime = kernelCpuGetMs();
	uquad_t endTime = (currTime + KEYTIMEOUT);

	while (currTime <= endTime)
	{
		processorInPort8(0x64, status);

		if (!(status & 0x02))
			return (0);

		currTime = kernelCpuGetMs();
	}

	kernelError(kernel_error, "Controller not ready timeout, port 64=%02x",
		status);
	return (ERR_TIMEOUT);
}


static int waitCommandReceived(void)
{
	unsigned char status = 0;
	uquad_t currTime = kernelCpuGetMs();
	uquad_t endTime = (currTime + KEYTIMEOUT);

	while (currTime <= endTime)
	{
		processorInPort8(0x64, status);

		if (status & 0x08)
			return (0);

		currTime = kernelCpuGetMs();
	}

	kernelError(kernel_error, "Controller receive command timeout, port "
		"64=%02x", status);
	return (ERR_TIMEOUT);
}


static int outPort60(unsigned char data)
{
	// Output a value to the keyboard controller's data port, after checking
	// that it's able to receive data (port 0x60).

	int status = 0;

	status = waitControllerReady();
	if (status < 0)
		return (status);

	processorOutPort8(0x60, data);

	return (status = 0);
}


static int outPort64(unsigned char data)
{
	// Output a value to the keyboard controller's command port, after checking
	// that it's able to receive data (port 0x64).

	int status = 0;

	status = waitControllerReady();
	if (status < 0)
		return (status);

	processorOutPort8(0x64, data);

	// Wait until the controller believes it has received it.
	status = waitCommandReceived();
	if (status < 0)
		return (status);

	return (status = 0);
}


static void setLight(int whichLight, int on)
{
	// Turns the keyboard lights on/off

	unsigned char data = 0;

	// Tell the keyboard we want to change the light status
	outPort60(0xED);

	if (on)
		keyboard.lights |= whichLight;
	else
		keyboard.lights &= ~whichLight;

	// Tell the keyboard to change the lights
	outPort60((unsigned char) keyboard.lights);

	// Read the ACK
	inPort60(&data);

	return;
}


static void readData(void)
{
	// This function reads the keyboard data and returns it to the keyboard
	// console text input stream

	int status = 0;
	unsigned char data = 0;
	keyScan scan = 0;
	int release = 0;
	static int extended = 0;
	static int extended1 = 0;

	// Read the data from port 60h
	status = inPort60(&data);

	kernelPicEndOfInterrupt(INTERRUPT_NUM_KEYBOARD);

	if (status < 0)
		return;

	// If an extended scan code is coming next...
	if (data == EXTENDED)
	{
		// The next thing coming is an extended scan code.  Set the flag
		// so it can be collected next time
		extended = 1;
		return;
	}

	// If a Pause/Break sequence (0xE1, 0x1D, 0x45 (press) or 0xE1, 0x9D,
	// 0xC5 (release)) is coming next...
	if (data == (EXTENDED + 1))
	{
		extended1 = 1;
		return;
	}

	if (extended1 && ((data & ~KEY_RELEASE) == LEFT_CONTROL))
		return;

	// Something else, other than a scan code?
	if ((data & ~KEY_RELEASE) > 0x5D)
		return;

	// Key press or key release?
	if (data & KEY_RELEASE)
	{
		scan = ps2Scan2Scan[data - KEY_RELEASE];
		release = 1;
	}
	else
		scan = ps2Scan2Scan[data];

	// Some special cases of extended scan codes and/or funny PS2 combinations
	if (extended)
	{
		switch (scan)
		{
			case keyLCtrl:
				// Really right CTRL
				scan = keyRCtrl;
				break;

			case keyLAlt:
				// Really right ALT
				scan = keyA2;
				break;

			case keyZero:
				// Really insert
				scan = keyIns;
				break;

			case keyB10:
				// Really numpad /
				scan = keySlash;
				break;

			case keyOne:
				// Really end
				scan = keyEnd;
				break;

			case keyTwo:
				// Really down cursor
				scan = keyDownArrow;
				break;

			case keyThree:
				// Really page down
				scan = keyPgDn;
				break;

			case keyFour:
				// Really left cursor
				scan = keyLeftArrow;
				break;

			case keySix:
				// Really right cursor
				scan = keyRightArrow;
				break;

			case keySeven:
				// Really home
				scan = keyHome;
				break;

			case keyEight:
				// Really up cursor
				scan = keyUpArrow;
				break;

			case keyNine:
				// Really page up
				scan = keyPgUp;
				break;

			case keyNLck:
				// Really Pause/Break
				scan = keyPause;
				break;

			case keyAsterisk:
				// Really PrtScn/SysRq
				scan = keyPrint;
				break;

			case keyLShift:
				// Ignore extended left shift.  Precedes PrtScn/SysRq.
				extended = 0;
				return;

			default:
				break;
		}
	}
	else
	{
 		if (scan == keyDel)
			// Really numpad .
			scan = keyPeriod;

		else if (extended1 && (scan == keyNLck))
		{
			// Really Pause/Break
			scan = keyPause;
			extended1 = 0;
		}
	}

	if (!release)
	{
		// Update keyboard lights, if applicable
		if (scan == keyCapsLock)
			setLight(CAPSLOCK,
				!(keyboard.state.toggleState & KEYBOARD_CAPS_LOCK_ACTIVE));
		else if (scan == keyNLck)
			setLight(NUMLOCK,
				!(keyboard.state.toggleState & KEYBOARD_NUM_LOCK_ACTIVE));
		else if (scan == keySLck)
			setLight(SCROLLLOCK,
				!(keyboard.state.toggleState & KEYBOARD_SCROLL_LOCK_ACTIVE));
	}

	if (release)
		kernelKeyboardInput(&keyboard, EVENT_KEY_UP, scan);
	else
		kernelKeyboardInput(&keyboard, EVENT_KEY_DOWN, scan);

	// Clear the extended flag
	extended = 0;
	return;
}


static void interrupt(void)
{
	// This is the PS/2 interrupt handler.

	void *address = NULL;

	processorIsrEnter(address);
	kernelInterruptSetCurrent(INTERRUPT_NUM_KEYBOARD);

	kernelDebug(debug_io, "Ps2Key keyboard interrupt");
	readData();

	kernelInterruptClearCurrent();
	processorIsrExit(address);
}


static int driverDetect(void *parent, kernelDriver *driver)
{
	// This function is used to detect a PS/2 keyboard and initialize it, as
	// well as registering it with the higher-level device functions.

	int status = 0;
	void *biosData = NULL;
	unsigned char flags;
	kernelDevice *dev = NULL;

	memset(&keyboard, 0, sizeof(kernelKeyboard));
	keyboard.type = keyboard_ps2;

	kernelDebug(debug_io, "Ps2Key get flags data from BIOS");

	// Map the BIOS data area into our memory so we can get hardware
	// information from it.
	status = kernelPageMapToFree(KERNELPROCID, 0, &biosData, 0x1000);
	if (status < 0)
		goto out;

	// Get the flags from the BIOS data area
	flags = (unsigned) *((unsigned char *)(biosData + 0x417));

	// Unmap BIOS data
	kernelPageUnmap(KERNELPROCID, biosData, 0x1000);

	// Record the keyboard state
	if (flags & (CAPSLOCK << 4))
		keyboard.state.toggleState |= KEYBOARD_CAPS_LOCK_ACTIVE;
	if (flags & (NUMLOCK << 4))
		keyboard.state.toggleState |= KEYBOARD_NUM_LOCK_ACTIVE;
	if (flags & (SCROLLLOCK << 4))
		keyboard.state.toggleState |= KEYBOARD_SCROLL_LOCK_ACTIVE;

	// Set the keyboard lights
	setLight(CAPSLOCK, (keyboard.state.toggleState &
		KEYBOARD_CAPS_LOCK_ACTIVE));
	setLight(NUMLOCK, (keyboard.state.toggleState &
		KEYBOARD_NUM_LOCK_ACTIVE));
	setLight(SCROLLLOCK, (keyboard.state.toggleState &
		KEYBOARD_SCROLL_LOCK_ACTIVE));

	// Add this keyboard to the keyboard subsystem
	status = kernelKeyboardAdd(&keyboard);
	if (status < 0)
		goto out;

	// Don't save any old handler for the dedicated keyboard interrupt, but if
	// there is one, we want to know about it.
	if (kernelInterruptGetHandler(INTERRUPT_NUM_KEYBOARD))
		kernelError(kernel_warn, "Not chaining unexpected existing handler "
			"for keyboard int %d", INTERRUPT_NUM_KEYBOARD);

	kernelDebug(debug_io, "Ps2Key hook interrupt");

	// Register our interrupt handler
	status = kernelInterruptHook(INTERRUPT_NUM_KEYBOARD, &interrupt, NULL);
	if (status < 0)
		goto out;

	kernelDebug(debug_io, "Ps2Key turn on keyboard interrupt");

	// Turn on the interrupt
	status = kernelPicMask(INTERRUPT_NUM_KEYBOARD, 1);
	if (status < 0)
		goto out;

	kernelDebug(debug_io, "Ps2Key enable keyboard");

	// Tell the keyboard to enable
	outPort64(0xAE);

	// Allocate memory for the device
	dev = kernelMalloc(sizeof(kernelDevice));
	if (!dev)
		return (status = ERR_MEMORY);

	dev->device.class = kernelDeviceGetClass(DEVICECLASS_KEYBOARD);
	dev->device.subClass = kernelDeviceGetClass(DEVICESUBCLASS_KEYBOARD_PS2);
	dev->driver = driver;

	// Add the kernel device
	kernelDebug(debug_io, "Ps2Key adding device");
	status = kernelDeviceAdd(parent, dev);
	if (status < 0)
		goto out;

	kernelDebug(debug_io, "Ps2Key finished PS/2 keyboard detection/setup");
	status = 0;

out:
	if (status < 0)
	{
		if (dev)
			kernelFree(dev);
	}

	return (status);
}


/////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////
//
// Below here, the functions are exported for external use
//
/////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////

void kernelPs2KeyboardDriverRegister(kernelDriver *driver)
{
	// Device driver registration.

	driver->driverDetect = driverDetect;

	return;
}

