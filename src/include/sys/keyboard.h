//
//  Visopsys
//  Copyright (C) 1998-2016 J. Andrew McLaughlin
//
//  This library is free software; you can redistribute it and/or modify it
//  under the terms of the GNU Lesser General Public License as published by
//  the Free Software Foundation; either version 2.1 of the License, or (at
//  your option) any later version.
//
//  This library is distributed in the hope that it will be useful, but
//  WITHOUT ANY WARRANTY; without even the implied warranty of
//  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU Lesser
//  General Public License for more details.
//
//  You should have received a copy of the GNU Lesser General Public License
//  along with this library; if not, write to the Free Software Foundation,
//  Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
//
//  keyboard.h
//

// This file contains definitions and structures for using and manipulating
// keyboards and keymaps in Visopsys.

#if !defined(_KEYBOARD_H)

#define KEYMAP_MAGIC					"keymap"
#define KEYMAP_NAMELEN					32

#define KEYBOARD_SCAN_CODES				105

// Keyboard state flags

#define KEYBOARD_LEFT_ALT_PRESSED		0x00000020
#define KEYBOARD_RIGHT_ALT_PRESSED		0x00000010
#define KEYBOARD_ALT_PRESSED \
	(KEYBOARD_LEFT_ALT_PRESSED | KEYBOARD_RIGHT_ALT_PRESSED)

#define KEYBOARD_LEFT_CONTROL_PRESSED	0x00000008
#define KEYBOARD_RIGHT_CONTROL_PRESSED	0x00000004
#define KEYBOARD_CONTROL_PRESSED \
	(KEYBOARD_LEFT_CONTROL_PRESSED | KEYBOARD_RIGHT_CONTROL_PRESSED)

#define KEYBOARD_LEFT_SHIFT_PRESSED		0x00000002
#define KEYBOARD_RIGHT_SHIFT_PRESSED	0x00000001
#define KEYBOARD_SHIFT_PRESSED \
	(KEYBOARD_LEFT_SHIFT_PRESSED | KEYBOARD_RIGHT_SHIFT_PRESSED)

// Keyboard toggle state flags
#define KEYBOARD_CAPS_LOCK_ACTIVE		0x04
#define KEYBOARD_NUM_LOCK_ACTIVE		0x02
#define KEYBOARD_SCROLL_LOCK_ACTIVE		0x01

// Hardware-neutral names for physical keyboard keys.  These are derived
// from the ones in the UEFI standard.
typedef enum {

	// |Esc|   |F1 |F2 |F3 |F4 |F5 |F6 |F7 |F8 |F9 |F10|F11|F12| |Psc|Slk|Pse|
	// |E0 |E1 |E2 |E3 |E4 |E5 |E6 |E7 |E8 |E9 |E10|E11|E12|Bks| |Ins|Hom|Pgu|
	// |Tab|D1 |D2 |D3 |D4 |D5 |D6 |D7 |D8 |D9 |D10|D11|D12|D13| |Del|End|Pgd|
	// |Cap|C1 |C2 |C3 |C4 |C5 |C6 |C7 |C8 |C9 |C10|C11|C12|Ent|
	// |Lsh|B0 |B1 |B2 |B3 |B4 |B5 |B6 |B7 |B8 |B9 |B10|Rsh    |
	// |Lct|A0 |Lal|           Spc             |A2 |A3 |A4 |Rct|

	// 6th row
	keyLCtrl = 0,				// 00
	keyA0,						// 01
	keyLAlt,					// 02
	keySpaceBar,				// 03
	keyA2,						// 04
	keyA3,						// 05
	keyA4,						// 06
	keyRCtrl,					// 07
	// Cursor/numpad keys
	keyLeftArrow,				// 08
	keyDownArrow,				// 09
	keyRightArrow,				// 0A
	keyZero,					// 0B
	keyPeriod,					// 0C
	keyEnter,					// 0D

	// 5th row
	keyLShift,					// 0E
	keyB0,						// 0F
	keyB1,						// 10
	keyB2,						// 11
	keyB3,						// 12
	keyB4,						// 13
	keyB5,						// 14
	keyB6,						// 15
	keyB7,						// 16
	keyB8,						// 17
	keyB9,						// 18
	keyB10,						// 19
	keyRShift,					// 1A
	// Cursor/numpad keys
	keyUpArrow,					// 1B
	keyOne,						// 1C
	keyTwo,						// 1D
	keyThree,					// 1E

	// 4th row
	keyCapsLock,				// 1F
	keyC1,						// 20
	keyC2,						// 21
	keyC3,						// 22
	keyC4,						// 23
	keyC5,						// 24
	keyC6,						// 25
	keyC7,						// 26
	keyC8,						// 27
	keyC9,						// 28
	keyC10,						// 29
	keyC11,						// 2A
	keyC12,						// 2B
	// Numpad keys
	keyFour,					// 2C
	keyFive,					// 2D
	keySix,						// 2E
	keyPlus,					// 2F

	// 3rd row
	keyTab,						// 30
	keyD1,						// 31
	keyD2,						// 32
	keyD3,						// 33
	keyD4,						// 34
	keyD5,						// 35
	keyD6,						// 36
	keyD7,						// 37
	keyD8,						// 38
	keyD9,						// 39
	keyD10,						// 3A
	keyD11,						// 3B
	keyD12,						// 3C
	keyD13,						// 3D
	// Editing/numpad keys
	keyDel,						// 3E
	keyEnd,						// 3F
	keyPgDn,					// 40
	keySeven,					// 41
	keyEight,					// 42
	keyNine,					// 43

	// 2nd row
	keyE0,						// 44
	keyE1,						// 45
	keyE2,						// 46
	keyE3,						// 47
	keyE4,						// 48
	keyE5,						// 49
	keyE6,						// 4A
	keyE7,						// 4B
	keyE8,						// 4C
	keyE9,						// 4D
	keyE10,						// 4E
	keyE11,						// 4F
	keyE12,						// 50
	keyBackSpace,				// 51
	// Editing keys
	keyIns,						// 52
	keyHome,					// 53
	keyPgUp,					// 54
	keyNLck,					// 55
	keySlash,					// 56
	keyAsterisk,				// 57
	keyMinus,					// 58

	// 1st row
	keyEsc,						// 59
	keyF1,						// 5A
	keyF2,						// 5B
	keyF3,						// 5C
	keyF4,						// 5D
	keyF5,						// 5E
	keyF6,						// 5F
	keyF7,						// 60
	keyF8,						// 61
	keyF9,						// 62
	keyF10,						// 63
	keyF11,						// 64
	keyF12,						// 65
	keyPrint,					// 66
	keySLck,					// 67
	keyPause					// 68

} keyScan;

// Old version 1 structure for keyboard key mappings
typedef struct {
	char magic[8];
	char name[KEYMAP_NAMELEN];
	unsigned char regMap[KEYBOARD_SCAN_CODES];
	unsigned char shiftMap[KEYBOARD_SCAN_CODES];
	unsigned char controlMap[KEYBOARD_SCAN_CODES];
	unsigned char altGrMap[KEYBOARD_SCAN_CODES];

} keyMapV1;

// A structure for holding keyboard key mappings
typedef struct {
	char magic[8];
	unsigned short version;
	char name[KEYMAP_NAMELEN];
	char language[2];
	unsigned regMap[KEYBOARD_SCAN_CODES];
	unsigned shiftMap[KEYBOARD_SCAN_CODES];
	unsigned controlMap[KEYBOARD_SCAN_CODES];
	unsigned altGrMap[KEYBOARD_SCAN_CODES];
	unsigned shiftAltGrMap[KEYBOARD_SCAN_CODES];

} keyMap;

#define _KEYBOARD_H
#endif

