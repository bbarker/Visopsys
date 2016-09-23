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
//  vshCursorMenu.c
//

// This contains some useful functions written for the shell

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/api.h>
#include <sys/ascii.h>
#include <sys/vsh.h>


static int display(int startRow, int maxColumns, int maxRows, char *items[],
	int numItems, int selected, int *firstVisible, int *lastVisible)
{
	char *buffer = NULL;
	textAttrs attrs;
	int count1, count2;

	memset(&attrs, 0, sizeof(textAttrs));

	if (numItems > maxRows)
	{
		if (selected < *firstVisible)
		{
			*firstVisible = selected;
			*lastVisible = (selected + (maxRows - 1));
		}
		else if (selected > *lastVisible)
		{
			*firstVisible = (selected - (maxRows - 1));
			*lastVisible = selected;
		}
	}

	buffer = malloc(maxColumns + 3);
	if (!buffer)
		return (ERR_MEMORY);

	textSetColumn(0);
	textSetRow(startRow);
	textSetCursor(0);

	for (count1 = *firstVisible; count1 <= *lastVisible; count1 ++)
	{
		printf(" ");

		if (numItems > maxRows)
		{
			if (count1 == *firstVisible)
				printf("^");
			else if (count1 < *lastVisible)
				printf("|");
			else
				printf("v");
		}

		sprintf(buffer, " %s ", items[count1]);

		// Fill the nest of the buffer with spaces
		for (count2 = 0; count2 < (maxColumns - (int) strlen(items[count1]));
			count2 ++)
		{
			strcat(buffer, " ");
		}

		if (count1 == selected)
			attrs.flags = TEXT_ATTRS_REVERSE;
		else
			attrs.flags = 0;

		textPrintAttrs(&attrs, buffer);
		printf("\n");
	}

	printf("\n  [Cursor up/down to change, Enter to select, 'Q' to quit]\n");

	free(buffer);
	return (0);
}


/////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////
//
// Below here, the functions are exported for external use
//
/////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////

_X_ int vshCursorMenu(const char *prompt, char *items[], int numItems, int maxRows, int defaultSelection)
{
	// Desc: This will create a cursor-selectable text menu with the supplied 'prompt' string at the stop.  The caller supplies a list of possible choices, an optional maximum number of rows to display on the screen, and the default selection.  If maxRows is set, and the number of choices is greater than maxRows, then the menu will be scrollable.  Returns the integer (zero-based) selected item number, or else negative on error or no selection.

	int displayRows = 0;
	int firstVisible = 0, lastVisible = 0;
	int itemWidth = 0;
	int selected = defaultSelection;
	char c = '\0';
	int count;

	// Check params
	if (!prompt || !items)
		return (errno = ERR_NULLPARAMETER);

	// Calculate the number of display rows we're going to use
	displayRows = numItems;
	if (maxRows && (maxRows < displayRows))
		displayRows = maxRows;

	if (!displayRows)
		return (errno = ERR_NULLPARAMETER);

	lastVisible = (displayRows - 1);

	// Get the width of the widest item and set our item width
	for (count = 0; count < numItems; count ++)
	{
		if ((int) strlen(items[count]) > itemWidth)
			itemWidth = strlen(items[count]);
	}

	itemWidth = min(itemWidth, textGetNumColumns());

	// If we need to scroll, add a character to the width for a scroll
	// indicator
	if (displayRows < numItems)
		itemWidth += 1;

	// Print prompt message
	printf("\n%s\n", prompt);

	// Now, print 'displayRows' newlines before calculating the current row so
	// that we don't get messed up if the screen scrolls
	for (count = 0; count < (displayRows + 3); count ++)
		printf("\n");

	int row = (textGetRow() - (displayRows + 3));

	while (1)
	{
		display(row, itemWidth, displayRows, items, numItems, selected,
			&firstVisible, &lastVisible);

		textInputSetEcho(0);
		c = getchar();
		textInputSetEcho(1);

		switch (c)
		{
			case (char) ASCII_CRSRUP:
				// Cursor up.
				if (selected > 0)
					selected -= 1;
				break;

			case (char) ASCII_CRSRDOWN:
				// Cursor down.
				if (selected < (numItems - 1))
					selected += 1;
				break;

			case (char) ASCII_ENTER:
				// Enter
				textSetCursor(1);
				return (selected);

			case 'Q':
			case 'q':
				// Cancel
				textSetCursor(1);
				return (errno = ERR_CANCELLED);
		}
	}
}

