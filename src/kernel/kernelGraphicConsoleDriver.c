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
//  kernelGraphicConsoleDriver.c
//

// This is the graphic console screen driver.  Manipulates character images
// using the kernelGraphic functions.

#include "kernelError.h"
#include "kernelFont.h"
#include "kernelMalloc.h"
#include "kernelMemory.h"
#include "kernelWindow.h"
#include <stdlib.h>
#include <string.h>


static inline void updateComponent(kernelTextArea *area)
{
	kernelWindowComponent *component =
		(kernelWindowComponent *) area->windowComponent;

	if (component && component->update)
		component->update(area->windowComponent);
}


static void scrollBuffer(kernelTextArea *area, int lines)
{
	// Scrolls back everything in the area's buffer

	int dataLength = (lines * area->columns);

	// Increasing the stored scrollback lines?
	if ((area->rows + area->scrollBackLines) < area->maxBufferLines)
	{
		area->scrollBackLines +=
			min(lines, (area->maxBufferLines -
				(area->rows + area->scrollBackLines)));

		updateComponent(area);
	}

	memcpy(TEXTAREA_FIRSTSCROLLBACK(area),
		(TEXTAREA_FIRSTSCROLLBACK(area) + dataLength),
		((area->rows + area->scrollBackLines) * area->columns));
}


static void setCursor(kernelTextArea *area, int onOff)
{
	// Draws or erases the cursor at the current position

	int cursorPosition = (area->cursorRow * area->columns) + area->cursorColumn;
	graphicBuffer *buffer =
		((kernelWindowComponent *) area->windowComponent)->buffer;
	char string[2];

	string[0] = area->visibleData[cursorPosition];
	string[1] = '\0';

	if (onOff)
	{
		kernelGraphicDrawRect(buffer,
			(color *) &area->foreground, draw_normal,
			(area->xCoord + (area->cursorColumn * area->font->glyphWidth)),
			(area->yCoord + (area->cursorRow * area->font->glyphHeight)),
			area->font->glyphWidth, area->font->glyphHeight, 1, 1);
		kernelGraphicDrawText(buffer, (color *) &area->background,
			(color *) &area->foreground, area->font, area->charSet, string,
			draw_normal,
			(area->xCoord + (area->cursorColumn * area->font->glyphWidth)),
			(area->yCoord + (area->cursorRow * area->font->glyphHeight)));
	}
	else
	{
		// Clear out the position and redraw the character
		kernelGraphicClearArea(buffer, (color *) &area->background,
			(area->xCoord + (area->cursorColumn * area->font->glyphWidth)),
			(area->yCoord + (area->cursorRow * area->font->glyphHeight)),
			area->font->glyphWidth, area->font->glyphHeight);
		kernelGraphicDrawText(buffer, (color *) &area->foreground,
			(color *) &area->background, area->font, area->charSet, string,
			draw_normal,
			(area->xCoord + (area->cursorColumn * area->font->glyphWidth)),
			(area->yCoord + (area->cursorRow * area->font->glyphHeight)));
	}

	// Tell the window manager to update the graphic buffer
	kernelWindowUpdateBuffer(buffer,
		(area->xCoord + (area->cursorColumn * area->font->glyphWidth)),
		(area->yCoord + (area->cursorRow * area->font->glyphHeight)),
		area->font->glyphWidth, area->font->glyphHeight);

	area->cursorState = onOff;

	return;
}


static int scrollLine(kernelTextArea *area)
{
	// Scrolls the text by 1 line in the text area provided.

	kernelWindowComponent *component = area->windowComponent;
	kernelWindowTextArea *windowTextArea = component->data;
	graphicBuffer *buffer = component->buffer;
	int maxWidth = 0;
	int longestLine = 0;
	int lineWidth = 0;
	int count;

	if (windowTextArea)
		maxWidth = windowTextArea->areaWidth;
	else if (component->width)
		maxWidth = component->width;
	else
		maxWidth = buffer->width;

	// Figure out the length of the longest line
	for (count = 0; count < area->rows; count ++)
	{
		lineWidth = (strlen((char *)(area->visibleData +
			(count * area->columns))) * area->font->glyphWidth);

		if (lineWidth > maxWidth)
		{
			longestLine = maxWidth;
			break;
		}

		if (lineWidth > longestLine)
			longestLine = lineWidth;
	}

	if (buffer->height > area->font->glyphHeight)
	{
		// Copy everything up by one line
		kernelGraphicCopyArea(buffer, area->xCoord,
			(area->yCoord + area->font->glyphHeight), longestLine,
			((area->rows - 1) * area->font->glyphHeight),
			area->xCoord, area->yCoord);
	}

	// Erase the last line
	kernelGraphicClearArea(buffer, (color *) &area->background, area->xCoord,
		(area->yCoord + ((area->rows - 1) * area->font->glyphHeight)),
		longestLine, area->font->glyphHeight);

	// Tell the window manager to update the whole graphic buffer
	kernelWindowUpdateBuffer(buffer, area->xCoord, area->yCoord,
		longestLine, (area->rows * area->font->glyphHeight));

	// Move the buffer up by one
	scrollBuffer(area, 1);

	// Clear out the bottom row
	memset(TEXTAREA_LASTVISIBLE(area), 0, area->columns);

	// Copy our buffer data to the visible area
	memcpy(area->visibleData, TEXTAREA_FIRSTVISIBLE(area),
		(area->rows * area->columns));

	// The cursor position is now 1 row up from where it was.
	area->cursorRow -= 1;

	return (0);
}


static int getCursorAddress(kernelTextArea *area)
{
	// Returns the cursor address as an integer
	return ((area->cursorRow * area->columns) + area->cursorColumn);
}


static int screenDraw(kernelTextArea *area)
{
	// Yup, draws the text area as currently specified

	graphicBuffer *buffer =
		((kernelWindowComponent *) area->windowComponent)->buffer;
	unsigned char *bufferAddress = NULL;
	char *lineBuffer = NULL;
	int count;

	lineBuffer = kernelMalloc(area->columns + 1);
	if (!lineBuffer)
		return (ERR_MEMORY);

	// Clear the area
	kernelGraphicClearArea(buffer, (color *) &area->background, area->xCoord,
		area->yCoord, (area->columns * area->font->glyphWidth),
		(area->rows * area->font->glyphHeight));

	// Copy from the buffer to the visible area, minus any scrollback lines
	bufferAddress = TEXTAREA_FIRSTVISIBLE(area);
	bufferAddress -= (area->scrolledBackLines * area->columns);

	for (count = 0; count < area->rows; count ++)
	{
		strncpy(lineBuffer, (char *) bufferAddress, area->columns);
		lineBuffer[area->columns] = '\0';
		kernelGraphicDrawText(buffer, (color *) &area->foreground,
			(color *) &area->background, area->font, area->charSet, lineBuffer,
			draw_normal, area->xCoord, (area->yCoord +
				(count * area->font->glyphHeight)));
		bufferAddress += area->columns;
	}

	kernelFree(lineBuffer);

	// Tell the window manager to update the whole area buffer
	kernelWindowUpdateBuffer(buffer, area->xCoord, area->yCoord,
		(area->columns * area->font->glyphWidth),
		(area->rows * area->font->glyphHeight));

	// If we aren't scrolled back, show the cursor again
	if (area->cursorState && !(area->scrolledBackLines))
		setCursor(area, 1);

	return (0);
}


static int setCursorAddress(kernelTextArea *area, int row, int col)
{
	// Moves the cursor

	int cursorState = area->cursorState;
	char *line = NULL;

	// If we are currently scrolled back, this puts us back to normal
	if (area->scrolledBackLines)
	{
		area->scrolledBackLines = 0;
		screenDraw(area);

		updateComponent(area);
	}

	if (cursorState)
		setCursor(area, 0);

	area->cursorRow = row;
	area->cursorColumn = col;

	// If any of the preceding spots have NULLS in them, fill those with
	// spaces instead
	line = ((char *) TEXTAREA_FIRSTVISIBLE(area) +
		TEXTAREA_CURSORPOS(area) - col);
	for ( ; col >= 0; col --)
		if (line[col] == '\0')
			line[col] = ' ';

	if (cursorState)
		setCursor(area, 1);

	return (0);
}


static int print(kernelTextArea *area, const char *text, textAttrs *attrs)
{
	// Prints text to the text area.

	graphicBuffer *buffer =
		((kernelWindowComponent *) area->windowComponent)->buffer;
	int cursorState = area->cursorState;
	int length = 0;
	color *foreground = (color *) &area->foreground;
	color *background = (color *) &area->background;
	char *lineBuffer = NULL;
	int inputCounter = 0;
	int bufferCounter = 0;
	unsigned tabChars = 0;
	unsigned count;

	lineBuffer = kernelMalloc(area->columns + 1);
	if (!lineBuffer)
		return (ERR_MEMORY);

	// See whether we're printing with special attributes
	if (attrs)
	{
		if (attrs->flags & TEXT_ATTRS_FOREGROUND)
			foreground = &attrs->foreground;
		if (attrs->flags & TEXT_ATTRS_BACKGROUND)
			background = &attrs->background;
		if (attrs->flags & TEXT_ATTRS_REVERSE)
		{
			color *tmpColor = foreground;
			foreground = background;
			background = tmpColor;
		}
	}

	// If we are currently scrolled back, this puts us back to normal
	if (area->scrolledBackLines)
	{
		area->scrolledBackLines = 0;
		screenDraw(area);

		updateComponent(area);
	}

	if (cursorState)
		// Turn off da cursor
		setCursor(area, 0);

	// How long is the string?
	length = strlen(text);

	// Loop through the input string, adding characters to our line buffer.
	// If we reach the end of a line or encounter a newline character, do
	// a newline
	for (inputCounter = 0; inputCounter < length; inputCounter++)
	{
		// Add this character to the lineBuffer
		lineBuffer[bufferCounter++] = (unsigned char) text[inputCounter];

		if (text[inputCounter] == '\t')
		{
			tabChars =
				((TEXT_DEFAULT_TAB - (bufferCounter % TEXT_DEFAULT_TAB)) - 1);
			for (count = 0; count < tabChars; count ++)
				lineBuffer[bufferCounter++] = ' ';
		}

		// Is this the completion of the line?
		if ((inputCounter >= (length - 1)) ||
			((area->cursorColumn + bufferCounter) >= area->columns) ||
			(text[inputCounter] == '\n'))
		{
			lineBuffer[bufferCounter] = '\0';

			// Add it to our buffers
			strncpy((char *)(TEXTAREA_FIRSTVISIBLE(area) +
				TEXTAREA_CURSORPOS(area)), lineBuffer,
				(area->columns - area->cursorColumn));

			if (area->hidden)
			{
				for (count = 0; count < strlen(lineBuffer); count ++)
					lineBuffer[count] = '*';
				strncpy((char *)(area->visibleData + TEXTAREA_CURSORPOS(area)),
					lineBuffer, (area->columns - area->cursorColumn));
			}
			else
			{
				strncpy((char *)(area->visibleData + TEXTAREA_CURSORPOS(area)),
					(char *)(TEXTAREA_FIRSTVISIBLE(area) +
						TEXTAREA_CURSORPOS(area)),
					(area->columns - area->cursorColumn));
			}

			// Draw it
			kernelGraphicDrawText(buffer, foreground, background, area->font,
				area->charSet, lineBuffer, draw_normal,
				(area->xCoord + (area->cursorColumn * area->font->glyphWidth)),
				(area->yCoord + (area->cursorRow * area->font->glyphHeight)));

			kernelWindowUpdateBuffer(buffer, (area->xCoord +
					(area->cursorColumn * area->font->glyphWidth)),
				(area->yCoord + (area->cursorRow * area->font->glyphHeight)),
				(bufferCounter * area->font->glyphWidth),
				area->font->glyphHeight);

			if (((area->cursorColumn + bufferCounter) >= area->columns) ||
				(text[inputCounter] == '\n'))
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
					area->cursorRow += 1;
				area->cursorColumn = 0;

				bufferCounter = 0;
			}
			else
				area->cursorColumn += bufferCounter;
		}
	}

	kernelFree(lineBuffer);

	if (cursorState)
		// Turn on the cursor
		setCursor(area, 1);

	return (0);
}


static int delete(kernelTextArea *area)
{
	// Erase the character at the current position

	graphicBuffer *buffer =
		((kernelWindowComponent *) area->windowComponent)->buffer;
	int cursorState = area->cursorState;
	int position = TEXTAREA_CURSORPOS(area);

	// If we are currently scrolled back, this puts us back to normal
	if (area->scrolledBackLines)
	{
		area->scrolledBackLines = 0;
		screenDraw(area);

		updateComponent(area);
	}

	if (cursorState)
		// Turn off the cursor
		setCursor(area, 0);

	// Delete the character in our buffers
	*(TEXTAREA_FIRSTVISIBLE(area) + position) = '\0';
	*(area->visibleData + position) = '\0';

	kernelWindowUpdateBuffer(buffer,
		(area->xCoord + (area->cursorColumn * area->font->glyphWidth)),
		(area->yCoord + (area->cursorRow * area->font->glyphHeight)),
		area->font->glyphWidth, area->font->glyphHeight);

	if (cursorState)
		// Turn on the cursor
		setCursor(area, 1);

	return (0);
}


static int screenClear(kernelTextArea *area)
{
	// Yup, clears the text area

	graphicBuffer *buffer =
		((kernelWindowComponent *) area->windowComponent)->buffer;

	// Clear the area
	kernelGraphicClearArea(buffer, (color *) &area->background,
		area->xCoord, area->yCoord, (area->columns * area->font->glyphWidth),
		(area->rows * area->font->glyphHeight));

	// Tell the window manager to update the whole area buffer
	kernelWindowUpdateBuffer(buffer, area->xCoord, area->yCoord,
		(area->columns * area->font->glyphWidth),
		(area->rows * area->font->glyphHeight));

	// Empty all the data
	memset(TEXTAREA_FIRSTVISIBLE(area), 0, (area->columns * area->rows));

	// Copy to the visible area
	memcpy(area->visibleData, TEXTAREA_FIRSTVISIBLE(area),
		(area->rows * area->columns));

	// Cursor to the top right
	area->cursorColumn = 0;
	area->cursorRow = 0;

	if (area->cursorState)
		// Turn on the cursor
		setCursor(area, 1);

	updateComponent(area);

	return (0);
}


static int screenSave(kernelTextArea *area, textScreen *screen)
{
	// This routine saves the current contents of the screen

	// Get memory for a new save area
	screen->data = kernelMemoryGet(area->columns * area->rows,
		"text screen data");
	if (!screen->data)
		return (ERR_MEMORY);

	memcpy(screen->data, TEXTAREA_FIRSTVISIBLE(area),
		(area->rows * area->columns));

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
			(area->rows * area->columns));

		// Copy to the visible area
		memcpy(area->visibleData, screen->data, (area->rows * area->columns));
	}

	area->cursorColumn = screen->column;
	area->cursorRow = screen->row;

	screenDraw(area);

	updateComponent(area);

	return (0);
}


static kernelTextOutputDriver graphicModeDriver = {
	setCursor,
	getCursorAddress,
	setCursorAddress,
	NULL,	// setForeground
	NULL,	// setBackground
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

int kernelGraphicConsoleInitialize(void)
{
	// Called before the first use of the text console.

	// Register our driver
	return (kernelSoftwareDriverRegister(graphicConsoleDriver,
		&graphicModeDriver));
}

