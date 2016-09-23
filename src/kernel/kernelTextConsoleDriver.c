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
//  kernelTextConsoleDriver.c
//

// This is the text console screen driver.

#include "kernelText.h"
#include "kernelError.h"
#include "kernelMemory.h"
#include <stdlib.h>
#include <string.h>
#include <sys/processor.h>


static void scrollBuffer(kernelTextArea *area, int lines)
{
	// Scrolls back everything in the area's buffer

	int dataLength = (lines * area->columns * 2);

	// Increasing the stored scrollback lines?
	if ((area->rows + area->scrollBackLines) < area->maxBufferLines)
		area->scrollBackLines += min(lines, (area->maxBufferLines -
			(area->rows + area->scrollBackLines)));

	memcpy(TEXTAREA_FIRSTSCROLLBACK(area),
		(TEXTAREA_FIRSTSCROLLBACK(area) + dataLength),
		((area->rows + area->scrollBackLines) * (area->columns * 2)));
}


static unsigned char getPcColor(color *realColor)
{
	// Convert a real color into a PC's text-mode color code.  This
	// will be necessarily approximate, except that our approximation must
	// equal the pre-defined values such as COLOR_WHITE, COLOR_RED, etc. in
	// <sys/color.h>.

	unsigned char pcColor = 0;
	int intense = 0;

	if (realColor->blue > 85)
	{
		pcColor |= 1;
		if (realColor->blue > 170)
			intense = 1;
	}
	if (realColor->green > 85)
	{
		pcColor |= 2;
		if (realColor->green > 170)
			intense = 1;
	}
	if (realColor->red > 85)
	{
		pcColor |= 4;
		if (realColor->red > 170)
			intense = 1;
	}

	// Dark gray is a special case.  It has non-intense values but needs the
	// 'intense' bit (it is represented as "intense black")
	if ((realColor->blue && (realColor->blue <= 85)) &&
		(realColor->green && (realColor->green <= 85)) &&
		(realColor->red && (realColor->red <= 85)))
	{
		intense = 1;
	}

	if (intense)
		pcColor |= 8;

	return (pcColor);
}


static void setCursor(kernelTextArea *area, int onOff)
{
	// This sets the cursor on or off at the requested cursor position

	int idx = (TEXTAREA_CURSORPOS(area) * 2);

	if (onOff)
		area->visibleData[idx + 1] = ((area->pcColor & 0x0F) << 4) |
			((area->pcColor & 0xF0) >> 4);
	else
		area->visibleData[idx + 1] = area->pcColor;

	area->cursorState = onOff;

	return;
}


static void scrollLine(kernelTextArea *area)
{
	// This will scroll the screen by 1 line

	int cursorState = area->cursorState;
	int lineLength = (area->columns * area->bytesPerChar);
	char *lastRow = NULL;
	int count;

	if (cursorState)
		// Temporarily, cursor off
		setCursor(area, 0);

	// Move the buffer up by one
	scrollBuffer(area, 1);

	// Clear out the bottom row
	lastRow = (char *) TEXTAREA_LASTVISIBLE(area);
	for (count = 0; count < lineLength; )
	{
		lastRow[count++] = '\0';
		lastRow[count++] = area->pcColor;
	}

	// Copy our buffer data to the visible area
	memcpy(area->visibleData, TEXTAREA_FIRSTVISIBLE(area),
		(area->rows * lineLength));

	// Move the cursor up by one row.
	area->cursorRow -= 1;

	if (cursorState)
		// Cursor back on
		setCursor(area, 1);

	return;
}


static int getCursorAddress(kernelTextArea *area)
{
	// Returns the cursor address as an integer
	return ((area->cursorRow * area->columns) + area->cursorColumn);
}


static int screenDraw(kernelTextArea *area)
{
	// Draws the current screen as specified by the area data

	unsigned char *bufferAddress = NULL;

	// Copy from the buffer to the visible area, minus any scrollback lines
	bufferAddress = TEXTAREA_FIRSTVISIBLE(area);
	bufferAddress -= (area->scrolledBackLines * area->columns * 2);

	memcpy(area->visibleData, bufferAddress, (area->rows * area->columns * 2));

	// If we aren't scrolled back, show the cursor again
	if (area->cursorState && !(area->scrolledBackLines))
		setCursor(area, 1);

	return (0);
}


static int setCursorAddress(kernelTextArea *area, int row, int col)
{
	// Moves the cursor

	int cursorState = area->cursorState;

	// If we are currently scrolled back, this puts us back to normal
	if (area->scrolledBackLines)
	{
		area->scrolledBackLines = 0;
		screenDraw(area);
	}

	if (cursorState)
		setCursor(area, 0);

	area->cursorRow = row;
	area->cursorColumn = col;

	if (cursorState)
		setCursor(area, 1);

	return (0);
}


static int setForeground(kernelTextArea *area, color *foreground)
{
	// Sets a new foreground color

	area->pcColor &= 0xF0;
	area->pcColor |= (getPcColor(foreground) & 0x0F);
	return (0);
}


static int setBackground(kernelTextArea *area, color *background)
{
	// Sets a new background color

	area->pcColor &= 0x0F;
	area->pcColor |= ((getPcColor(background) & 0x07) << 4);
	return (0);
}


static int print(kernelTextArea *area, const char *string, textAttrs *attrs)
{
	// Prints ascii text strings to the text console.

	unsigned char pcColor = area->pcColor;
	int cursorState = area->cursorState;
	unsigned char *bufferAddress = NULL;
	unsigned char *visibleAddress = NULL;
	int length = 0;
	int tabChars = 0;
	int count1, count2;

	// See whether we're printing with special attributes
	if (attrs)
	{
		if (attrs->flags & TEXT_ATTRS_FOREGROUND)
		{
			pcColor &= 0xF0;
			pcColor |= (getPcColor(&attrs->foreground) & 0x0F);
		}

		if (attrs->flags & TEXT_ATTRS_BACKGROUND)
		{
			pcColor &= 0x0F;
			pcColor |= ((getPcColor(&attrs->background) & 0x07) << 4);
		}

		if (attrs->flags & TEXT_ATTRS_REVERSE)
		{
			pcColor = (((pcColor & 0x07) << 4) | ((pcColor & 0x70) >> 4));
		}

		if (attrs->flags & TEXT_ATTRS_BLINKING)
		{
			pcColor |= 0x80;
		}
	}

	// If we are currently scrolled back, this puts us back to normal
	if (area->scrolledBackLines)
	{
		area->scrolledBackLines = 0;
		screenDraw(area);
	}

	if (cursorState)
		// Turn off the cursor
		setCursor(area, 0);

	bufferAddress = (TEXTAREA_FIRSTVISIBLE(area) +
		(TEXTAREA_CURSORPOS(area) * 2));
	visibleAddress = (area->visibleData + (TEXTAREA_CURSORPOS(area) * 2));

	// How long is the string?
	length = strlen(string);

	// Loop through the string, putting one byte into every even-numbered
	// screen address.  Put the color byte into every odd address
	for (count1 = 0; count1 < length; count1 ++)
	{
		if ((string[count1] != '\t') && (string[count1] != '\n'))
		{
			*(bufferAddress++) = string[count1];
			*(visibleAddress++) = string[count1];
			*(bufferAddress++) = pcColor;
			*(visibleAddress++) = pcColor;
			area->cursorColumn += 1;
		}

		if (string[count1] == '\t')
		{
			tabChars = (TEXT_DEFAULT_TAB - (area->cursorColumn %
				TEXT_DEFAULT_TAB));
			for (count2 = 0; count2 < tabChars; count2 ++)
			{
				*(bufferAddress++) = ' ';
				*(visibleAddress++) = ' ';
				*(bufferAddress++) = pcColor;
				*(visibleAddress++) = pcColor;
				area->cursorColumn += 1;
			}
		}

		// Newline, or otherwise scrolling?
		if ((string[count1] == '\n') || (area->cursorColumn >= area->columns))
		{
			// Will this cause a scroll?
			if (area->cursorRow >= (area->rows - 1))
			{
				if (!area->noScroll)
				{
					scrollLine(area);
					area->cursorRow += 1;
				}
			}
			else
			{
				area->cursorRow += 1;
			}

			area->cursorColumn = 0;
			bufferAddress = (TEXTAREA_FIRSTVISIBLE(area) +
				(TEXTAREA_CURSORPOS(area) * 2));
			visibleAddress = (area->visibleData +
				(TEXTAREA_CURSORPOS(area) * 2));
		}
	}

	if (cursorState)
		// Turn the cursor back on
		setCursor(area, 1);

	return (0);
}


static int delete(kernelTextArea *area)
{
	// Erase the character at the current position

	int cursorState = area->cursorState;
	int position = (TEXTAREA_CURSORPOS(area) * 2);

	// If we are currently scrolled back, this puts us back to normal
	if (area->scrolledBackLines)
	{
		area->scrolledBackLines = 0;
		screenDraw(area);
	}

	if (cursorState)
		// Turn off da cursor
		setCursor(area, 0);

	// Delete the character in our buffers
	*(TEXTAREA_FIRSTVISIBLE(area) + position) = '\0';
	*(TEXTAREA_FIRSTVISIBLE(area) + position + 1) = area->pcColor;
	*(area->visibleData + position) = '\0';
	*(area->visibleData + position + 1) = area->pcColor;

	if (cursorState)
		// Turn on the cursor
		setCursor(area, 1);

	return (0);
}


static int screenClear(kernelTextArea *area)
{
	// Clears the screen, and puts the cursor in the top left (starting)
	// position

	unsigned tmpData = 0;
	int dwords = 0;

	// Construct the dword of data that we will replicate all over the screen.
	// It consists of the NULL character twice, plus the color byte twice
	tmpData = ((area->pcColor << 24) | (area->pcColor << 8));

	// Calculate the number of dwords that make up the screen
	// Formula is ((COLS * ROWS) / 2)
	dwords = (area->columns * area->rows) / 2;

	processorWriteDwords(tmpData, TEXTAREA_FIRSTVISIBLE(area), dwords);

	// Copy to the visible area
	memcpy(area->visibleData, TEXTAREA_FIRSTVISIBLE(area),
		(area->rows * area->columns * 2));

	// Make the cursor go to the top left
	area->cursorColumn = 0;
	area->cursorRow = 0;

	if (area->cursorState)
		setCursor(area, 1);

	return (0);
}


static int screenSave(kernelTextArea *area, textScreen *screen)
{
	// This routine saves the current contents of the screen

	// Get memory for a new save area
	screen->data = kernelMemoryGet((area->columns * area->rows * 2),
		"text screen data");
	if (!screen->data)
		return (ERR_MEMORY);

	memcpy(screen->data, TEXTAREA_FIRSTVISIBLE(area),
		(area->rows * area->columns * 2));

	screen->column = area->cursorColumn;
	screen->row = area->cursorRow;

	return (0);
}


static int screenRestore(kernelTextArea *area, textScreen *screen)
{
	// This routine restores the saved contents of the screen

	if (screen->data)
	{
		memcpy(TEXTAREA_FIRSTVISIBLE(area), screen->data,
			(area->rows * area->columns * 2));

		// Copy to the visible area
		memcpy(area->visibleData, screen->data,
			(area->rows * area->columns * 2));
	}

	area->cursorColumn = screen->column;
	area->cursorRow = screen->row;

	return (0);
}


// Our kernelTextOutputDriver structure
static kernelTextOutputDriver textModeDriver = {
	setCursor,
	getCursorAddress,
	setCursorAddress,
	setForeground,
	setBackground,
	print,
	delete,
	screenDraw,
	screenClear,
	screenSave,
	screenRestore
};


/////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////
//
// Below here, the functions are exported for external use
//
/////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////

int kernelTextConsoleInitialize(void)
{
	// Called before the first use of the text console.

	// Register our driver
	return (kernelSoftwareDriverRegister(textConsoleDriver, &textModeDriver));
}

