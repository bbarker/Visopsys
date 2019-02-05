//
//  Visopsys
//  Copyright (C) 1998-2018 J. Andrew McLaughlin
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
//  windowKeyboard.c
//

// This contains functions for user programs to operate GUI components.

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/api.h>
#include <sys/charset.h>
#include <sys/keyboard.h>
#include <sys/window.h>

extern int libwindow_initialized;
extern void libwindowInitialize(void);

static int rowKeys[] = {
	WINDOWKEYBOARD_ROW0_KEYS,
	WINDOWKEYBOARD_ROW1_KEYS,
	WINDOWKEYBOARD_ROW2_KEYS,
	WINDOWKEYBOARD_ROW3_KEYS,
	WINDOWKEYBOARD_ROW4_KEYS,
	WINDOWKEYBOARD_ROW5_KEYS
};

static int p0RowKeys[] = {
	WINDOWKEYBOARD_ROW0_P0_KEYS,
	WINDOWKEYBOARD_ROW1_P0_KEYS,
	WINDOWKEYBOARD_ROW2_P0_KEYS,
	WINDOWKEYBOARD_ROW3_P0_KEYS,
	WINDOWKEYBOARD_ROW4_P0_KEYS,
	WINDOWKEYBOARD_ROW5_P0_KEYS
};

static int p1RowKeys[] = {
	WINDOWKEYBOARD_ROW0_P1_KEYS,
	WINDOWKEYBOARD_ROW1_P1_KEYS,
	WINDOWKEYBOARD_ROW2_P1_KEYS,
	WINDOWKEYBOARD_ROW3_P1_KEYS,
	WINDOWKEYBOARD_ROW4_P1_KEYS,
	WINDOWKEYBOARD_ROW5_P1_KEYS
};

// Mapping out the virtual keyboard scan codes
static keyScan scans[][WINDOWKEYBOARD_MAX_ROWKEYS] = {
	// Function key row ROW0
	{ keyEsc, keyF1, keyF2, keyF3, keyF4, keyF5, keyF6, keyF7, keyF8, keyF9,
		keyF10, keyF11, keyF12, keyPrint, keySLck, keyPause },
	// Number key row ROW1
	{ keyE0, keyE1, keyE2, keyE3, keyE4, keyE5, keyE6, keyE7, keyE8, keyE9,
		keyE10, keyE11, keyE12, keyBackSpace, keyIns, keyHome, keyPgUp },
	// Top letter row (QWERTY...) ROW2
	{ keyTab, keyD1, keyD2, keyD3, keyD4, keyD5, keyD6, keyD7, keyD8, keyD9,
		keyD10, keyD11, keyD12, keyD13, keyDel, keyEnd, keyPgDn },
	// Middle letter row (ASDF...) ROW3
	{ keyCapsLock, keyC1, keyC2, keyC3, keyC4, keyC5, keyC6, keyC7, keyC8,
		keyC9, keyC10, keyC11, keyC12, keyEnter },
	// Bottom letter row (ZXCV...) ROW4
	{ keyLShift, keyB0, keyB1, keyB2, keyB3, keyB4, keyB5, keyB6, keyB7, keyB8,
		keyB9, keyB10, keyRShift, keyUpArrow },
	// Bottom spacebar row ROW5
	{ keyLCtrl, keyA0, keyLAlt, keySpaceBar, keyA2, keyA3, keyA4, keyRCtrl,
		keyLeftArrow, keyDownArrow, keyRightArrow }
};

// Keys with special strings
static struct {
	keyScan scan;
	const char *string1;
	const char *string2;
} keyStrings[] = {
	{ keyEsc, "Esc", NULL },
	{ keyF1, "F1", NULL },
	{ keyF2, "F2", NULL },
	{ keyF3, "F3", NULL },
	{ keyF4, "F4", NULL },
	{ keyF5, "F5", NULL },
	{ keyF6, "F6", NULL },
	{ keyF7, "F7", NULL },
	{ keyF8, "F8", NULL },
	{ keyF9, "F9", NULL },
	{ keyF10, "F10", NULL },
	{ keyF11, "F11", NULL },
	{ keyF12, "F12", NULL },
	{ keyPrint, "Prt", "Scn" },
	{ keySLck, "Scr", "Lck" },
	{ keyPause, "Pse", NULL },
	{ keyIns, "Ins", NULL },
	{ keyHome, "Hom", NULL },
	{ keyPgUp, "Pg", "Up" },
	{ keyDel, "Del", NULL },
	{ keyEnd, "End", NULL },
	{ keyPgDn, "Pg", "Dn" },
	{ keyCapsLock, "Caps", "Lck" },
	{ keyLCtrl, "Ctrl", NULL },
	{ keyLAlt, "Alt", NULL },
	{ keyA2, "Alt", "Gr" },
	{ keyRCtrl, "Ctrl", NULL },
	{ 0, NULL, NULL }
};

// Keys with special 'weights' when drawing
static struct {
	keyScan scan;
	int weight;
} weights[] = {
	{ keyBackSpace, 100 },
	{ keyTab, 50 },
	{ keyD13, 50 },
	{ keyCapsLock, 40 },
	{ keyEnter, 60 },
	{ keyLShift, 25 },
	{ keyRShift, 75 },
	{ keyLCtrl, 8 },
	{ keyA0, 5 },
	{ keyLAlt, 5 },
	{ keySpaceBar, 60 },
	{ keyA2, 5 },
	{ keyA3, 5 },
	{ keyA4, 5 },
	{ keyRCtrl, 7 },
	{ 0, 0 }
};


static int getKeyHeight(windowKeyboard *keyboard)
{
	return (((keyboard->height - 2) - (WINDOWKEYBOARD_GAP +
		(WINDOWKEYBOARD_KEYROWS - 2))) / WINDOWKEYBOARD_KEYROWS);
}


static objectKey pickFont(int maxHeight)
{
	objectKey font = NULL;
	objectKey tmpFont = NULL;
	int height;
	int count;

	struct {
		const char *family;
		unsigned flags;
		int points;
	} tryFonts[] = {
		{ FONT_FAMILY_ARIAL, FONT_STYLEFLAG_BOLD, 20 },
		{ FONT_FAMILY_ARIAL, FONT_STYLEFLAG_BOLD, 12 },
		{ FONT_FAMILY_ARIAL, FONT_STYLEFLAG_BOLD, 10 },
		{ FONT_FAMILY_LIBMONO, FONT_STYLEFLAG_BOLD, 10 },
		{ FONT_FAMILY_XTERM, FONT_STYLEFLAG_NORMAL, 10 },
		{ FONT_FAMILY_LIBMONO, FONT_STYLEFLAG_BOLD, 8 },
		{ NULL, 0, 0 }
	};

	for (count = 0; tryFonts[count].family; count ++)
	{
		tmpFont = fontGet(tryFonts[count].family, tryFonts[count].flags,
			tryFonts[count].points, NULL);

		if (!tmpFont)
			continue;

		height = fontGetHeight(tmpFont);
		if ((height > 0) && (height <= maxHeight))
		{
			font = tmpFont;
			break;
		}
	}

	return (font);
}


static void makeRow(windowKeyboard *keyboard, int rowCount, int yCoord,
	int rowHeight)
{
	int stdKeyWidth = 0;
	int rowKeyGaps = 0;
	int panelGapWidth = 0;
	int extraWidth = 0;
	int topGapWidth = 0;
	windowKey *key = NULL;
	keyScan scan = 0;
	int xCoord = 1;
	int keyCount, count;

	// How many total keys in this row
	keyboard->rows[rowCount].numKeys = rowKeys[rowCount];

	// Calculate the standard key width.
	stdKeyWidth = ((keyboard->width - 2) / 18);

	// Calculate the number of standard inter-key gaps
	rowKeyGaps = (p0RowKeys[rowCount] - 1);
	if (p1RowKeys[rowCount])
		rowKeyGaps += (p1RowKeys[rowCount] - 1);

	// Calculate the size of the gap separating the panels
	panelGapWidth = max(WINDOWKEYBOARD_GAP, (stdKeyWidth / 3));

	// Calculate the spare width in panel 0, to be allocated to 'weighted'
	// keys
	extraWidth = (keyboard->width - ((stdKeyWidth *
		(p0RowKeys[rowCount] + 3)) + rowKeyGaps + panelGapWidth));

	// Top function key row has 3 additional gaps
	if (!rowCount)
		topGapWidth = (extraWidth / 3);

	for (keyCount = 0; keyCount < rowKeys[rowCount]; keyCount ++)
	{
		key = &keyboard->rows[rowCount].keys[keyCount];
		scan = scans[rowCount][keyCount];

		key->xCoord = xCoord;
		key->yCoord = yCoord;
		key->width = stdKeyWidth;
		key->height = rowHeight;
		key->scan = scan;

		for (count = 0; keyStrings[count].string1; count ++)
		{
			if (keyStrings[count].scan == scan)
			{
				key->string1 = keyStrings[count].string1;
				key->string2 = keyStrings[count].string2;
				break;
			}
		}

		for (count = 0; weights[count].weight; count ++)
		{
			if (weights[count].scan == scan)
			{
				key->width += ((weights[count].weight * extraWidth) / 100);
				break;
			}
		}

		// There are a few keys we want to remember
		if (scan == keyLShift)
			keyboard->leftShift = key;
		else if (scan == keyRShift)
			keyboard->rightShift = key;
		else if (scan == keyLCtrl)
			keyboard->leftControl = key;
		else if (scan == keyRCtrl)
			keyboard->rightControl = key;

		xCoord += (key->width + 1);

		if (!rowCount && (!keyCount || (keyCount == 4) || (keyCount == 8)))
			// Extra gaps in top function key row
			xCoord += topGapWidth;

		if (keyCount == (p0RowKeys[rowCount] - 1))
			// Gap for 2nd panel
			xCoord = (keyboard->width - ((stdKeyWidth * 3) + 3));

		if (scan == keyRShift)
			// Key-sized gap beween right-shift and up-arrow
			xCoord += (stdKeyWidth + 1);
	}
}


static void makeKeyboard(windowKeyboard *keyboard)
{
	int yCoord = 1;
	int rowHeight = getKeyHeight(keyboard);
	int rowCount;

	// Make each keyboard row
	for (rowCount = 0; rowCount < WINDOWKEYBOARD_KEYROWS; rowCount ++)
	{
		makeRow(keyboard, rowCount, yCoord, rowHeight);

		// Make a little gap after the function key row
		if (!rowCount)
			yCoord += WINDOWKEYBOARD_GAP;

		yCoord += (rowHeight + 1);
	}
}


static int isPictureKey(keyScan scan)
{
	switch (scan)
	{
		case keyBackSpace:
		case keyTab:
		case keyEnter:
		case keyLShift:
		case keyRShift:
		case keyUpArrow:
		case keyA0:
		case keyA3:
		case keyA4:
		case keyLeftArrow:
		case keyDownArrow:
		case keyRightArrow:
			return (1);

		default:
			return (0);
	}
}


static void drawHorizArrow(windowKeyboard *keyboard, windowKey *key, int left,
	int right, int stop, int tail)
{
	int totalWidth = (key->width / 3);
	int totalHeight = ((key->height / 5) & ~1);
	int arrowHeadWidth = (totalWidth / 4);
	windowDrawParameters params;

	// Clear our drawing parameters
	memset(&params, 0, sizeof(windowDrawParameters));

	params.operation = draw_line;
	params.mode = draw_normal;
	params.xCoord1 = params.xCoord2 = (key->xCoord + 3);
	params.yCoord1 = params.yCoord2 = (key->yCoord + 3);
	params.thickness = max(1, (key->height / 10));
	memcpy(&params.foreground, &keyboard->foreground, sizeof(color));

	if (left)
	{
		if (stop)
		{
			// Stop (for TAB key)
			params.yCoord2 += totalHeight;
			windowComponentSetData(keyboard->canvas, &params, 1,
				0 /* no redraw */);
			params.yCoord2 -= totalHeight;
		}

		// Draw the arrow head.  First the top up-slope.
		params.yCoord1 += (totalHeight / 2);
		params.xCoord2 += arrowHeadWidth;
		windowComponentSetData(keyboard->canvas, &params, 1,
			0 /* no redraw */);

		// Back of the arrow head
		params.xCoord1 += arrowHeadWidth;
		params.yCoord1 += (totalHeight / 2);
		windowComponentSetData(keyboard->canvas, &params, 1,
			0 /* no redraw */);

		// Bottom down-slope
		params.xCoord2 -= arrowHeadWidth;
		params.yCoord2 += (totalHeight / 2);
		windowComponentSetData(keyboard->canvas, &params, 1,
			0 /* no redraw */);

		// Arrow body
		params.yCoord1 -= (totalHeight / 2);
		params.xCoord2 += totalWidth;
		windowComponentSetData(keyboard->canvas, &params, 1, 1 /* redraw */);

		params.xCoord1 = params.xCoord2;
		params.yCoord1 -= (totalHeight / 2);

		if (tail)
			// Tail (for ENTER key)
			windowComponentSetData(keyboard->canvas, &params, 1,
				1 /* redraw */);

		// If there's also a right arrow, prepare to draw it underneath
		params.yCoord1 = params.yCoord2 = ((key->yCoord + 3) + totalHeight);
	}

	if (right)
	{
		params.xCoord1 = params.xCoord2 = ((key->xCoord + 3) + totalWidth);

		if (stop)
		{
			// Stop (for TAB key)
			params.yCoord2 += totalHeight;
			windowComponentSetData(keyboard->canvas, &params, 1,
				0 /* no redraw */);
			params.yCoord2 -= totalHeight;
		}

		// Draw the arrow head.  First the top down-slope.
		params.yCoord1 += (totalHeight / 2);
		params.xCoord2 -= arrowHeadWidth;
		windowComponentSetData(keyboard->canvas, &params, 1,
			0 /* no redraw */);

		// Back of the arrow head
		params.xCoord1 -= arrowHeadWidth;
		params.yCoord1 += (totalHeight / 2);
		windowComponentSetData(keyboard->canvas, &params, 1,
			0 /* no redraw */);

		// Bottom up-slope
		params.xCoord2 += arrowHeadWidth;
		params.yCoord2 += (totalHeight / 2);
		windowComponentSetData(keyboard->canvas, &params, 1,
			0 /* no redraw */);

		// Arrow body
		params.yCoord1 -= (totalHeight / 2);
		params.xCoord2 -= totalWidth;
		windowComponentSetData(keyboard->canvas, &params, 1, 1 /* redraw */);
	}
}


static void drawShiftArrow(windowKeyboard *keyboard, windowKey *key)
{
	int totalWidth = ((key->height / 3) & ~3); // Yes, use height
	int totalHeight = totalWidth;
	windowDrawParameters params;

	// Clear our drawing parameters
	memset(&params, 0, sizeof(windowDrawParameters));

	params.operation = draw_line;
	params.mode = draw_normal;
	params.thickness = max(1, (key->height / 10));
	memcpy(&params.foreground, &keyboard->foreground, sizeof(color));

	// Draw clockwise from the top.  First the right down-slope of the arrow
	// head
	params.xCoord1 = ((key->xCoord + 3) + (totalWidth / 2));
	params.yCoord1 = (key->yCoord + 3);
	params.xCoord2 = (params.xCoord1 + (totalWidth / 2));
	params.yCoord2 = (params.yCoord1 + (totalHeight / 2));
	windowComponentSetData(keyboard->canvas, &params, 1, 0 /* no redraw */);

	// Bottom-right of the arrow head
	params.xCoord1 += (totalWidth / 4);
	params.yCoord1 = params.yCoord2;
	windowComponentSetData(keyboard->canvas, &params, 1, 0 /* no redraw */);

	// Right side of the arrow body
	params.xCoord2 = params.xCoord1;
	params.yCoord2 += (totalHeight / 2);
	windowComponentSetData(keyboard->canvas, &params, 1, 0 /* no redraw */);

	// Bottom of the arrow body
	params.xCoord1 -= (totalWidth / 2);
	params.yCoord1 = params.yCoord2;
	windowComponentSetData(keyboard->canvas, &params, 1, 0 /* no redraw */);

	// Right side of the arrow body
	params.xCoord2 = params.xCoord1;
	params.yCoord2 -= (totalHeight / 2);
	windowComponentSetData(keyboard->canvas, &params, 1, 0 /* no redraw */);

	// Bottom-left of the arrow head
	params.xCoord1 -= (totalWidth / 4);
	params.yCoord1 = params.yCoord2;
	windowComponentSetData(keyboard->canvas, &params, 1, 0 /* no redraw */);

	// Left up-slope of the arrow head
	params.xCoord2 += (totalWidth / 4);
	params.yCoord2 -= (totalHeight / 2);
	windowComponentSetData(keyboard->canvas, &params, 1, 1 /* redraw */);
}


static void drawVertArrow(windowKeyboard *keyboard, windowKey *key, int up)
{
	int totalWidth = ((key->height / 5) & ~1); // Yes, use height
	int totalHeight = (key->height / 3);
	int arrowHeadHeight = (totalHeight / 4);
	windowDrawParameters params;

	// Clear our drawing parameters
	memset(&params, 0, sizeof(windowDrawParameters));

	params.operation = draw_line;
	params.mode = draw_normal;
	params.xCoord1 = params.xCoord2 = (key->xCoord + 3);
	params.yCoord1 = params.yCoord2 = (key->yCoord + 3);
	params.thickness = max(1, (key->height / 10));
	memcpy(&params.foreground, &keyboard->foreground, sizeof(color));

	if (up)
	{
		// Draw the arrow head.  First the right down-slope.
		params.xCoord1 += (totalWidth / 2);
		params.xCoord2 += totalWidth;
		params.yCoord2 += arrowHeadHeight;
		windowComponentSetData(keyboard->canvas, &params, 1,
			0 /* no redraw */);

		// Base of the arrow head
		params.xCoord1 -= (totalWidth / 2);
		params.yCoord1 += arrowHeadHeight;
		windowComponentSetData(keyboard->canvas, &params, 1,
			0 /* no redraw */);

		// Left up-slope of the arrow head
		params.xCoord2 -= (totalWidth / 2);
		params.yCoord2 -= arrowHeadHeight;
		windowComponentSetData(keyboard->canvas, &params, 1,
			0 /* no redraw */);

		// Arrow body
		params.xCoord1 += (totalWidth / 2);
		params.yCoord2 += totalHeight;
		windowComponentSetData(keyboard->canvas, &params, 1, 1 /* redraw */);
	}
	else
	{
		// Draw the arrow head.  First the right up-slope.
		params.xCoord1 += (totalWidth / 2);
		params.yCoord1 += totalHeight;
		params.xCoord2 += totalWidth;
		params.yCoord2 += (totalHeight - arrowHeadHeight);
		windowComponentSetData(keyboard->canvas, &params, 1,
			0 /* no redraw */);

		// Base of the arrow head
		params.xCoord1 -= (totalWidth / 2);
		params.yCoord1 -= arrowHeadHeight;
		windowComponentSetData(keyboard->canvas, &params, 1,
			0 /* no redraw */);

		// Left down-slope of the arrow head
		params.xCoord2 -= (totalWidth / 2);
		params.yCoord2 += arrowHeadHeight;
		windowComponentSetData(keyboard->canvas, &params, 1,
			0 /* no redraw */);

		// Arrow body
		params.xCoord1 += (totalWidth / 2);
		params.yCoord2 -= totalHeight;
		windowComponentSetData(keyboard->canvas, &params, 1, 1 /* redraw */);
	}
}


static void drawWinIcon(windowKeyboard *keyboard, windowKey *key)
{
	int totalWidth = (key->width / 2);
	int totalHeight = (key->height / 2);
	int menuWidth = (totalWidth / 2);
	int menuHeight = ((totalHeight * 2) / 3);
	windowDrawParameters params;

	// Clear our drawing parameters
	memset(&params, 0, sizeof(windowDrawParameters));

	params.operation = draw_rect;
	params.mode = draw_normal;
	params.xCoord1 = (key->xCoord + ((key->width - totalWidth) / 2));
	params.yCoord1 = (key->yCoord + ((key->height - totalHeight) / 2));
	params.width = totalWidth;
	params.height = totalHeight;
	params.thickness = 1;
	memcpy(&params.foreground, &keyboard->foreground, sizeof(color));
	windowComponentSetData(keyboard->canvas, &params, 1, 0 /* no redraw */);

	// Draw the title bar
	params.height = max(3, (totalHeight / 10));
	params.fill = 1;
	windowComponentSetData(keyboard->canvas, &params, 1, 0 /* no redraw */);

	// Draw the window menu
	params.xCoord1 += 2;
	params.yCoord1 += params.height;
	params.width = menuWidth;
	params.height = (menuHeight - params.height);
	params.fill = 0;
	windowComponentSetData(keyboard->canvas, &params, 1, 0 /* no redraw */);

	// Draw a selected item
	params.yCoord1 += max(1, (menuHeight / 5));
	params.height = max(1, (menuHeight / 5));
	params.fill = 1;
	windowComponentSetData(keyboard->canvas, &params, 1, 1 /* redraw */);
}


static void drawMenuIcon(windowKeyboard *keyboard, windowKey *key)
{
	int totalWidth = (key->width / 3);
	int totalHeight = ((key->height * 2) / 3);
	int itemHeight = max(2, (totalHeight / 4));
	windowDrawParameters params;
	int count;

	// Clear our drawing parameters
	memset(&params, 0, sizeof(windowDrawParameters));

	params.operation = draw_rect;
	params.mode = draw_normal;
	params.xCoord1 = (key->xCoord + ((key->width - totalWidth) / 2));
	params.yCoord1 = (key->yCoord + ((key->height - totalHeight) / 2));
	params.width = totalWidth;
	params.height = itemHeight;
	params.thickness = 1;
	memcpy(&params.foreground, &keyboard->foreground, sizeof(color));

	params.height = itemHeight;
	for (count = 0; count < 4; count ++)
	{
		params.fill = 0;
		if (count == 2)
			params.fill = 1;

		windowComponentSetData(keyboard->canvas, &params, 1, (count >= 3));

		params.yCoord1 += itemHeight;
	}
}


static void drawPictureKey(windowKeyboard *keyboard, windowKey *key)
{
	switch (key->scan)
	{
		case keyBackSpace:
			drawHorizArrow(keyboard, key, 1 /* left */, 0 /* no right */,
				0 /* no stop */, 0 /* no tail */);
			break;

		case keyTab:
			drawHorizArrow(keyboard, key, 1 /* left */, 1 /* right */,
				1 /* stop */, 0 /* no tail */);
			break;

		case keyEnter:
			drawHorizArrow(keyboard, key, 1 /* left */, 0 /* no right */,
				0 /* no stop */, 1 /* tail */);
			break;

		case keyLShift:
		case keyRShift:
			drawShiftArrow(keyboard, key);
			break;

		case keyUpArrow:
			drawVertArrow(keyboard, key, 1 /* up */);
			break;

		case keyA0:
		case keyA3:
			drawWinIcon(keyboard, key);
			break;

		case keyA4:
			drawMenuIcon(keyboard, key);
			break;

		case keyLeftArrow:
			drawHorizArrow(keyboard, key, 1 /* left */, 0 /* no right */,
				0 /* no stop */, 0 /* no tail */);
			break;

		case keyDownArrow:
			drawVertArrow(keyboard, key, 0 /* not up */);
			break;

		case keyRightArrow:
			drawHorizArrow(keyboard, key, 0 /* no left */, 1 /* right */,
				0 /* no stop */, 0 /* no tail */);
			break;

		default:
			break;
	}
}


static unsigned getKeyChar(windowKeyboard *keyboard, keyScan scan)
{
	unsigned unicode = 0;

	if (keyboard->shiftState & KEYBOARD_RIGHT_ALT_PRESSED)
	{
		if (keyboard->shiftState & KEYBOARD_SHIFT_PRESSED)
			unicode = keyboard->map.shiftAltGrMap[scan];
		else
			unicode = keyboard->map.altGrMap[scan];
	}

	else if (keyboard->shiftState & KEYBOARD_CONTROL_PRESSED)
	{
		unicode = keyboard->map.controlMap[scan];
	}

	else if (keyboard->toggleState & KEYBOARD_CAPS_LOCK_ACTIVE)
	{
		if (keyboard->shiftState & KEYBOARD_SHIFT_PRESSED)
			unicode = tolower(keyboard->map.shiftMap[scan]);
		else
			unicode = toupper(keyboard->map.regMap[scan]);
	}

	else if (keyboard->shiftState & KEYBOARD_SHIFT_PRESSED)
	{
		unicode = keyboard->map.shiftMap[scan];
	}

	else
	{
		unicode = keyboard->map.regMap[scan];
	}

	return (charsetFromUnicode(keyboard->charsetName, unicode));
}


static void drawKeyMapping(windowKeyboard *keyboard, windowKey *key, int clear)
{
	char keyChar[2];
	windowDrawParameters params;

	// Clear our drawing parameters
	memset(&params, 0, sizeof(windowDrawParameters));

	params.mode = draw_normal;
	params.xCoord1 = (key->xCoord + ((key->width - keyboard->fontWidth) / 2));
	params.yCoord1 = (key->yCoord + ((key->height - keyboard->fontHeight) /
		2));
	memcpy(&params.foreground, &keyboard->background, sizeof(color));
	params.foreground.red = ((params.foreground.red * 8) / 10);
	params.foreground.green = ((params.foreground.green * 8) / 10);
	params.foreground.blue = ((params.foreground.blue * 8) / 10);

	if (clear)
	{
		// Clear any existing character first
		params.operation = draw_rect;
		params.width = keyboard->fontWidth;
		params.height = keyboard->fontHeight;
		params.fill = 1;
		windowComponentSetData(keyboard->canvas, &params, 1,
			0 /* no redraw */);
	}

	params.operation = draw_text;
	params.font = keyboard->font;
	memcpy(&params.background, &params.foreground, sizeof(color));
	memcpy(&params.foreground, &keyboard->foreground, sizeof(color));

	sprintf(keyChar, "%c", getKeyChar(keyboard, key->scan));
	params.data = keyChar;

	windowComponentSetData(keyboard->canvas, &params, 1, 1 /* redraw */);
}


static void drawKey(windowKeyboard *keyboard, windowKey *key)
{
	windowDrawParameters params;

	// Clear our drawing parameters
	memset(&params, 0, sizeof(windowDrawParameters));

	// Draw a border
	params.operation = draw_rect;
	params.mode = draw_normal;
	params.xCoord1 = key->xCoord;
	params.yCoord1 = key->yCoord;
	params.width = key->width;
	params.height = key->height;
	params.thickness = 1;
	memcpy(&params.foreground, &keyboard->foreground, sizeof(color));
	windowComponentSetData(keyboard->canvas, &params, 1, 0 /* no redraw */);

	// Draw a slightly darker, inner body for the key
	params.xCoord1 += 1;
	params.yCoord1 += 1;
	params.width -= 2;
	params.height -= 2;
	params.fill = 1;
	memcpy(&params.foreground, &keyboard->background, sizeof(color));
	params.foreground.red = ((params.foreground.red * 8) / 10);
	params.foreground.green = ((params.foreground.green * 8) / 10);
	params.foreground.blue = ((params.foreground.blue * 8) / 10);
	windowComponentSetData(keyboard->canvas, &params, 1, 0 /* no redraw */);

	if (isPictureKey(key->scan))
	{
		drawPictureKey(keyboard, key);
	}
	else
	{
		// Draw the text
		if (key->string1)
		{
			params.operation = draw_text;
			params.xCoord1 += 2;
			params.yCoord1 += 1;
			params.font = keyboard->smallFont;
			params.data = (char *) key->string1;
			memcpy(&params.background, &params.foreground, sizeof(color));
			memcpy(&params.foreground, &keyboard->foreground, sizeof(color));

			windowComponentSetData(keyboard->canvas, &params, 1,
				1 /* redraw */);

			if (key->string2)
			{
				params.yCoord1 += keyboard->smallFontHeight;
				params.data = (char *) key->string2;
				windowComponentSetData(keyboard->canvas, &params, 1,
					1 /* redraw */);
			}
		}
		else
		{
			drawKeyMapping(keyboard, key, 0 /* no clear */);
		}
	}
}


static void draw(windowKeyboard *keyboard)
{
	windowDrawParameters params;
	int rowCount, keyCount;

	// Clear our drawing parameters
	memset(&params, 0, sizeof(windowDrawParameters));

	// Clear the whole keyboard area with the background color
	params.operation = draw_rect;
	params.mode = draw_normal;
	params.width = keyboard->width;
	params.height = keyboard->height;
	params.thickness = 1;
	params.fill = 1;
	memcpy(&params.foreground, &keyboard->background, sizeof(color));
	windowComponentSetData(keyboard->canvas, &params, 1, 1 /* redraw */);

	// Draw the keys
	for (rowCount = 0; rowCount < WINDOWKEYBOARD_KEYROWS; rowCount ++)
	{
		for (keyCount = 0; keyCount < keyboard->rows[rowCount].numKeys;
			keyCount ++)
		{
			drawKey(keyboard, &keyboard->rows[rowCount].keys[keyCount]);
		}
	}
}


static int isShiftModifierKey(keyScan scan)
{
	switch (scan)
	{
		case keyLShift:
		case keyRShift:
		case keyLCtrl:
		case keyLAlt:
		case keyA2:
		case keyRCtrl:
			return (1);

		default:
			return (0);
	}
}


static int isToggleModifierKey(keyScan scan)
{
	switch (scan)
	{
		case keySLck:
		case keyNLck:
		case keyCapsLock:
			return (1);

		default:
			return (0);
	}
}


static inline int isModifierKey(keyScan scan)
{
	return (isShiftModifierKey(scan) || isToggleModifierKey(scan));
}


static void togglePressed(windowKeyboard *keyboard, windowKey *key)
{
	windowDrawParameters params;

	// Clear our drawing parameters
	memset(&params, 0, sizeof(windowDrawParameters));

	// Set drawing values
	params.operation = draw_rect;
	params.mode = draw_xor;
	params.xCoord1 = (key->xCoord + 1);
	params.yCoord1 = (key->yCoord + 1);
	params.width = (key->width - 2);
	params.height = (key->height - 2);
	params.thickness = 1;
	params.fill = 1;
	memcpy(&params.foreground, &keyboard->foreground, sizeof(color));

	windowComponentSetData(keyboard->canvas, &params, 1, 1 /* redraw */);
}


static int isShiftModifierRelease(windowKeyboard *keyboard, keyScan scan)
{
	return ((((scan == keyLShift) || (scan == keyRShift)) &&
			(keyboard->shiftState & KEYBOARD_SHIFT_PRESSED)) ||
		(((scan == keyLCtrl) || (scan == keyRCtrl)) &&
			(keyboard->shiftState & KEYBOARD_CONTROL_PRESSED)) ||
		(((scan == keyLAlt) || (scan == keyA2)) &&
			(keyboard->shiftState & KEYBOARD_ALT_PRESSED)));
}


static void redrawKeyMappings(windowKeyboard *keyboard)
{
	windowKey *key = NULL;
	int rowCount, keyCount;

	// Draw the keys
	for (rowCount = 0; rowCount < WINDOWKEYBOARD_KEYROWS; rowCount ++)
	{
		for (keyCount = 0; keyCount < keyboard->rows[rowCount].numKeys;
			keyCount ++)
		{
			key = &keyboard->rows[rowCount].keys[keyCount];

			if (!isPictureKey(key->scan) && !key->string1)
				drawKeyMapping(keyboard, key, 1 /* clear */);
		}
	}
}


static void processModifier(windowKeyboard *keyboard, keyScan scan)
{
	switch (scan)
	{
		case keySLck:
			keyboard->toggleState ^= KEYBOARD_SCROLL_LOCK_ACTIVE;
			break;

		case keyNLck:
			keyboard->toggleState ^= KEYBOARD_NUM_LOCK_ACTIVE;
			break;

		case keyCapsLock:
			keyboard->toggleState ^= KEYBOARD_CAPS_LOCK_ACTIVE;
			break;

		case keyLShift:
		case keyRShift:
			keyboard->shiftState ^= KEYBOARD_SHIFT_PRESSED;
			break;

		case keyLCtrl:
		case keyRCtrl:
			keyboard->shiftState ^= KEYBOARD_CONTROL_PRESSED;
			break;

		case keyLAlt:
			keyboard->shiftState ^= KEYBOARD_LEFT_ALT_PRESSED;
			break;

		case keyA2:
			keyboard->shiftState ^= KEYBOARD_RIGHT_ALT_PRESSED;
			break;

		default:
			return;
	}

	// Redraw just the key mappings
	redrawKeyMappings(keyboard);

	return;
}


static int eventHandler(windowKeyboard *keyboard, windowEvent *event)
{
	windowKey *key = NULL;
	int found = 0;
	keyScan scan = 0;
	int rowCount, keyCount;

	// Check params.
	if (!keyboard || !event)
		return (errno = ERR_NULLPARAMETER);

	if (event->type & EVENT_MOUSE_LEFT)
	{
		// Loop through the keys, looking for one that receives this event
		for (rowCount = 0; rowCount < WINDOWKEYBOARD_KEYROWS; rowCount ++)
		{
			for (keyCount = 0; keyCount < keyboard->rows[rowCount].numKeys;
				keyCount ++)
			{
				key = &keyboard->rows[rowCount].keys[keyCount];

				if ((event->xPosition >= key->xCoord) &&
					(event->xPosition < (key->xCoord + key->width)) &&
					(event->yPosition >= key->yCoord) &&
					(event->yPosition < (key->yCoord + key->height)))
				{
					found = 1;
					break;
				}
			}

			if (found)
				break;
		}

		// If some key was previously pressed, un-press it unless it's one
		// of the modifier keys
		if (keyboard->pressedKey)
		{
			scan = keyboard->pressedKey->scan;

			if (!isModifierKey(scan))
				togglePressed(keyboard, keyboard->pressedKey);

			// Shift, Control, and Alt don't unpress until pressed again
			if (!isShiftModifierKey(scan) && keyboard->callback)
				keyboard->callback(EVENT_KEY_UP, scan);

			keyboard->pressedKey = NULL;
		}

		// Was some new key pressed?
		if (found && (event->type & EVENT_MOUSE_LEFTDOWN))
		{
			int isModRel = isShiftModifierRelease(keyboard, key->scan);

			// Shift, Control, and Alt unpress when pressed again
			if (isModRel && keyboard->callback)
				keyboard->callback(EVENT_KEY_UP, key->scan);

			togglePressed(keyboard, key);

			// The control and shift keys are linked with their peers on the
			// other side of the keyboard
			if (key == keyboard->leftShift)
				togglePressed(keyboard, keyboard->rightShift);
			else if (key == keyboard->rightShift)
				togglePressed(keyboard, keyboard->leftShift);
			else if (key == keyboard->leftControl)
				togglePressed(keyboard, keyboard->rightControl);
			else if (key == keyboard->rightControl)
				togglePressed(keyboard, keyboard->leftControl);

			// See whether this keypress changed our state
			processModifier(keyboard, key->scan);

			if (!isModRel && keyboard->callback)
				keyboard->callback(EVENT_KEY_DOWN, key->scan);

			keyboard->pressedKey = key;
		}
	}

	return (0);
}


static int setMap(windowKeyboard *keyboard, keyMap *map)
{
	// Replace the existing map with the one supplied, and re-draw

	// Check params.
	if (!keyboard || !map)
		return (errno = ERR_NULLPARAMETER);

	memcpy(&keyboard->map, map, sizeof(keyMap));
	redrawKeyMappings(keyboard);

	return (0);
}


static int setCharset(windowKeyboard *keyboard, const char *charsetName)
{
	// Replace the existing character set name with the one supplied, and
	// re-draw

	int status = 0;

	// Check params.
	if (!keyboard || !charsetName)
		return (errno = ERR_NULLPARAMETER);

	status = windowComponentSetCharSet(keyboard->canvas, charsetName);
	if (status < 0)
		return (errno = status);

	// Copy the name of the character set
	strncpy(keyboard->charsetName, charsetName, CHARSET_NAME_LEN);
	redrawKeyMappings(keyboard);

	return (status = 0);
}


/////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////
//
// Below here, the functions are exported for external use
//
/////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////

_X_ windowKeyboard *windowNewKeyboard(objectKey parent, int width, int height, void *callback, componentParameters *params)
{
	// Desc: Create a 'virtual keyboard' widget with the parent window 'parent', the given width and height in pixels, and an optional function pointer 'callback' for when virtual keys are pressed.

	int status = 0;
	windowKeyboard *keyboard = NULL;
	int keyHeight = 0;
	color foreground = { 255, 255, 255 };

	if (!libwindow_initialized)
		libwindowInitialize();

	// Check params.
	if (!parent || !params)
	{
		errno = ERR_NULLPARAMETER;
		return (keyboard = NULL);
	}

	// We have minimum width and height
	width = max(500, width);
	height = max(200, height);

	// Allocate memory for our keyboard structure
	keyboard = malloc(sizeof(windowKeyboard));
	if (!keyboard)
	{
		errno = ERR_MEMORY;
		return (keyboard = NULL);
	}

	// Create the keyboard's main canvas
	keyboard->canvas = windowNewCanvas(parent, width, height, params);
	if (!keyboard->canvas)
	{
		status = errno = ERR_NOCREATE;
		goto out;
	}

	// Get the current keyboard map
	status = keyboardGetMap(&keyboard->map);
	if (status < 0)
	{
		errno = status;
		goto out;
	}

	// Try to get the character set for the keymap language
	if (configGet(PATH_SYSTEM_CONFIG "/charset.conf", keyboard->map.language,
		keyboard->charsetName, CHARSET_NAME_LEN) < 0)
	{
		strncpy(keyboard->charsetName, CHARSET_NAME_ISO_8859_15,
			CHARSET_NAME_LEN);
	}

	keyboard->width = width;
	keyboard->height = height;

	// Was a foreground color specified?
	if (params->flags & WINDOW_COMPFLAG_CUSTOMFOREGROUND)
		// Use the one we were given
		memcpy(&keyboard->foreground, &params->foreground, sizeof(color));
	else
		// Use our default
		keyboard->foreground = foreground;

	// Was a background color specified?
	if (params->flags & WINDOW_COMPFLAG_CUSTOMBACKGROUND)
		// Use the one we were given
		memcpy(&keyboard->background, &params->background, sizeof(color));
	else
		// Use the desktop color
		windowGetColor(COLOR_SETTING_DESKTOP, &keyboard->background);

	// Try to load an appropriate fonts
	keyHeight = getKeyHeight(keyboard);

	// Pick a large font for keymap characters
	keyboard->font = pickFont(keyHeight - 4);
	if (!keyboard->font)
		goto out;

	keyboard->fontWidth = fontGetPrintedWidth(keyboard->font, NULL, "@");

	keyboard->fontHeight = fontGetHeight(keyboard->font);

	// Pick a small font for key strings
	keyboard->smallFont = pickFont((keyHeight - 4) / 2);
	if (!keyboard->font)
		goto out;

	keyboard->smallFontHeight = fontGetHeight(keyboard->smallFont);

	// Set our function pointers
	keyboard->eventHandler = &eventHandler;
	keyboard->setMap = &setMap;
	keyboard->setCharset = &setCharset;
	keyboard->callback = callback;

	makeKeyboard(keyboard);
	draw(keyboard);

	status = 0;

out:
	if (status < 0)
	{
		if (keyboard)
		{
			free(keyboard);
			keyboard = NULL;
		}
	}

	return (keyboard);
}

