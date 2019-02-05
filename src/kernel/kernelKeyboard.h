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
//  kernelKeyboard.h
//

#if !defined(_KERNELKEYBOARD_H)

#include <sys/keyboard.h>
#include <sys/stream.h>
#include <sys/types.h>

#define MAX_KEYBOARDS				8
#define KEYBOARD_MAX_BUFFERSIZE		16

typedef enum {
	keyboard_virtual, keyboard_ps2, keyboard_usb

} kernelKeyboardType;

typedef volatile struct {
	unsigned shiftState;
	unsigned toggleState;

} kernelKeyboardState;

typedef struct _kernelKeyboard {
	kernelKeyboardType type;
	kernelKeyboardState state;
	unsigned lights;
	keyScan repeatKey;
	uquad_t repeatTime;
	void *data;

	// A call to the keyboard driver, to be made periodically by the
	// keyboard thread
	void (*threadCall)(struct _kernelKeyboard *);

} kernelKeyboard;

// Functions exported by kernelKeyboard.c
int kernelKeyboardInitialize(void);
int kernelKeyboardAdd(kernelKeyboard *);
int kernelKeyboardGetMap(keyMap *);
int kernelKeyboardSetMap(const char *);
int kernelKeyboardSetStream(stream *);
int kernelKeyboardInput(kernelKeyboard *, int, keyScan);
int kernelKeyboardVirtualInput(int, keyScan);

#define _KERNELKEYBOARD_H
#endif

