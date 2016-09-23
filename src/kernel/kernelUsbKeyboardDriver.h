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
//  kernelUsbKeyboardDriver.h
//

#if !defined(_KERNELUSBKEYBOARDDRIVER_H)

#include "kernelKeyboard.h"
#include "kernelUsbDriver.h"

// Bit positions for the keyboard modifier byte
#define USB_HID_KEYBOARD_RIGHTGUI	0x80
#define USB_HID_KEYBOARD_RIGHTALT	0x40
#define USB_HID_KEYBOARD_RIGHTSHIFT	0x20
#define USB_HID_KEYBOARD_RIGHTCTRL	0x10
#define USB_HID_KEYBOARD_LEFTGUI	0x08
#define USB_HID_KEYBOARD_LEFTALT	0x04
#define USB_HID_KEYBOARD_LEFTSHIFT	0x02
#define USB_HID_KEYBOARD_LEFTCTRL	0x01

// The size of the boot protocol keyboard buffer
#define USB_HID_KEYBOARD_BUFFSIZE	6

typedef struct {
	unsigned char modifier;
	unsigned char res;
	unsigned char code[USB_HID_KEYBOARD_BUFFSIZE];

} __attribute__((packed)) usbKeyboardData;

typedef struct {
	kernelBusTarget *busTarget;
	usbDevice *usbDev;
	kernelDevice dev;
	int interface;
	usbKeyboardData oldKeyboardData;
	kernelKeyboard keyboard;

} usbKeyboard;

#define _KERNELUSBKEYBOARDDRIVER_H
#endif

