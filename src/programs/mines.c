//
//  Mines
//  Copyright (C) 2004 Graeme McLaughlin
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
//  mines.c
//

// Written by Graeme McLaughlin
// Mods by Andy McLaughlin

/* This is the text that appears when a user requests help about this program
<help>

 -- mines --

A mine sweeper game.

Usage:
  mines

</help>
*/

#include <errno.h>
#include <libintl.h>
#include <locale.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/api.h>
#include <sys/env.h>
#include <sys/paths.h>
#include <sys/window.h>

#define _(string) gettext(string)

#define WINDOW_TITLE	_("Mines")
#define MINE_IMAGE		PATH_PROGRAMS "/mines.dir/mine.bmp"
#define NUM_MINES		10
#define GRID_DIM		8

static objectKey window = NULL;
static objectKey gridButtons[GRID_DIM][GRID_DIM];
static objectKey gridOtherStuff[GRID_DIM][GRID_DIM];
static int mineField[GRID_DIM][GRID_DIM];
static image mineImage;
static int numUncovered = 0;


static inline void uncover(int x, int y)
{
	if (gridButtons[x][y])
	{
		windowComponentSetVisible(gridButtons[x][y], 0);
		if (gridOtherStuff[x][y])
			windowComponentSetVisible(gridOtherStuff[x][y], 1);
		gridButtons[x][y] = NULL;
		numUncovered += 1;
	}
}


static void uncoverAll(void)
{
	int x, y;

	for (x = 0; x < GRID_DIM; x++)
		for (y = 0; y < GRID_DIM; y++)
			uncover(x, y);
}


static void clickEmpties(int x, int y)
{
	// Recursive function which uncovers empty squares

	if (mineField[x][y])
	{
		uncover(x, y);

		if (mineField[x][y] == -1)
		{
			mineField[x][y] = 0;

			// Start from top left corner and make my way around

			if ((x >= 1) && (y >= 1) && (mineField[x - 1][y - 1] != 9))
				clickEmpties((x - 1), (y - 1));

			if ((y >= 1) && (mineField[x][y - 1] != 9))
				clickEmpties(x, (y - 1));

			if ((x < (GRID_DIM - 1)) && (y >= 1) &&
				(mineField[x + 1][y - 1] != 9))
			{
				clickEmpties((x + 1), (y - 1));
			}

			if ((x < (GRID_DIM - 1)) &&	(mineField[x + 1][y] != 9))
				clickEmpties((x + 1), y);

			if ((x < (GRID_DIM - 1)) && (y < (GRID_DIM - 1)) &&
				(mineField[x + 1][y + 1] != 9))
			{
				clickEmpties((x + 1), (y + 1));
			}

			if ((y < (GRID_DIM - 1)) &&	(mineField[x][y + 1] != 9))
				clickEmpties(x, (y + 1));

			if ((x >= 1) && (y < (GRID_DIM - 1)) &&
				(mineField[x - 1][y + 1] != 9))
			{
				clickEmpties((x - 1), (y + 1));
			}

			if ((x >= 1) &&	(mineField[x - 1][y] != 9))
				clickEmpties((x - 1), y);
		}
	}

	return;
}


static void gameOver(int win)
{
	uncoverAll();

	windowNewInfoDialog(window, _("Game over"),
		(win? _("You win!") : _("You lose.")));

	return;
}


static void refreshWindow(void)
{
	// We got a 'window refresh' event (probably because of a language switch),
	// so we need to update things

	// Re-get the language setting
	setlocale(LC_ALL, getenv(ENV_LANG));
	textdomain("mines");

	// Re-get the character set
	if (getenv(ENV_CHARSET))
		windowSetCharSet(window, getenv(ENV_CHARSET));

	// Refresh the window title
	windowSetTitle(window, WINDOW_TITLE);
}


static void eventHandler(objectKey key, windowEvent *event)
{
	int x = 0;
	int y = 0;

	// Check for window events.
	if (key == window)
	{
		// Check for window refresh
		if (event->type == EVENT_WINDOW_REFRESH)
			refreshWindow();

		// Check for the window being closed
		else if (event->type == EVENT_WINDOW_CLOSE)
			windowGuiStop();
	}

	// Only go through the array of buttons if the event was a mouse click
	else if (event->type == EVENT_MOUSE_LEFTUP)
	{
		for (x = 0; x < GRID_DIM; x++)
		{
			for (y = 0; y < GRID_DIM; y++)
			{
				if (key == gridButtons[x][y])
				{
					// If this spot is empty, invoke the clickEmpties function
					if (mineField[x][y] == -1)
					{
						clickEmpties(x, y);
					}
					else
					{
						if (mineField[x][y] == 9)
						{
							gameOver(0);
						}
						else
						{
							uncover(x, y);

							if (numUncovered >=
								((GRID_DIM * GRID_DIM) - NUM_MINES))
							{
								gameOver(1);
							}
						}
					}

					return;
				}
			}
		}
	}
}


static void initialize(void)
{
	// Zeros out the array, assigns mines to random squares, and figures out
	// how many mines adjacent

	int x = 0;
	int y = 0;
	int randomX = 0;    // X and Y coords for random mines
	int randomY = 0;
	int mineCount = 0;  // Holds the running total of surrounding mines
	componentParameters params;
	char tmpChar[2];

	// First, let's zero it out
	for (x = 0; x < GRID_DIM; x++)
		for (y = 0; y < GRID_DIM; y++)
			mineField[x][y] = -1;

	// Now we randomly scatter the mines
	for (x = 0; x < NUM_MINES; )
	{
		randomX = (randomUnformatted() % GRID_DIM);
		randomY = (randomUnformatted() % GRID_DIM);

		// If this one's already a mine, we won't count it
		if (mineField[randomX][randomY] == 9)
			continue;

		mineField[randomX][randomY] = 9;
		x += 1;
	}

	memset(&params, 0, sizeof(componentParameters));
	params.gridWidth = 1;
	params.gridHeight = 1;
	params.orientationX = orient_center;
	params.orientationY = orient_middle;

	// Now that there are some mines scattered, we can establish the values of
	// the elements which have mines surrounding them
	for (x = 0; x < GRID_DIM; x++)
	{
		for (y = 0; y < GRID_DIM; y++)
		{
			params.gridX = x;
			params.gridY = y;

			// We don't want to count mines if this position is a mine itself
			if (mineField[x][y] == 9)
			{
			// Place an image of a mine there
			gridOtherStuff[x][y] =
			windowNewImage(window, &mineImage, draw_translucent, &params);
			windowComponentSetVisible(gridOtherStuff[x][y], 0);
			}
			else
			{
			mineCount = 0;

			// Ok, we'll go clockwise starting from the mine to the immediate
			// left
			if (((y - 1) >= 0) && (mineField[x][y - 1] == 9))
			mineCount++;
			if (((x - 1) >= 0) && ((y - 1) >= 0) &&
				(mineField[x - 1][y - 1] == 9))
			mineCount++;
			if (((x - 1) >= 0) && (mineField[x - 1][y] == 9))
			mineCount++;
			if (((x - 1) >= 0) && ((y + 1) <= 7) &&
				(mineField[x - 1][y + 1] == 9))
			mineCount++;
			if (((y + 1) <= 7) && (mineField[x][y + 1] == 9))
			mineCount++;
			if (((x + 1) <= 7) && ((y + 1) <= 7) &&
				(mineField[x + 1][y + 1] == 9))
			mineCount++;
			if (((x + 1) <= 7) && (mineField[x + 1][y] == 9))
			mineCount++;
			if (((x + 1) <= 7) && ((y - 1) >= 0) &&
				(mineField[x + 1][y - 1] == 9))
			mineCount++;

			if ((mineCount > 0) && (mineCount < 9))
			{
				sprintf(tmpChar, "%d", mineCount);
				gridOtherStuff[x][y] =
				windowNewTextLabel(window, tmpChar, &params);
				windowComponentSetVisible(gridOtherStuff[x][y], 0);
			}

			// Finally, we can assign a value to the current position
			if (mineCount)
				mineField[x][y] = mineCount;
			}
		}
	}

	// Set up the buttons.  We set up an array, just like the actual minefield.
	for (y = 0; y < GRID_DIM; y++)
	{
		params.gridY = y;

		for (x = 0; x < GRID_DIM; x++)
		{
			params.gridX = x;

			gridButtons[x][y] = windowNewButton(window, "   ", NULL, &params);
			windowRegisterEventHandler(gridButtons[x][y], &eventHandler);
		}
	}
}


int main(int argc __attribute__((unused)), char *argv[])
{
	int status = 0;

	setlocale(LC_ALL, getenv(ENV_LANG));
	textdomain("mines");

	// Only work in graphics mode
	if (!graphicsAreEnabled())
	{
		printf(_("\nThe \"%s\" command only works in graphics mode\n"),
			argv[0]);
		return (errno = ERR_NOTINITIALIZED);
	}

	// Load our images
	status = imageLoad(MINE_IMAGE, 0, 0, &mineImage);
	if (status < 0)
	{
		printf(_("\nCan't load %s\n"), MINE_IMAGE);
		return (errno = status);
	}
	mineImage.transColor.green = 255;

	// Create a new window
	window = windowNew(multitaskerGetCurrentProcessId(), WINDOW_TITLE);

	// Register an event handler to catch window events.
	windowRegisterEventHandler(window, &eventHandler);

	// Generate mine field
	initialize();

	// Go live.
	windowSetVisible(window, 1);

	// Run the GUI
	windowGuiRun();

	// Destroy the window
	windowDestroy(window);

	imageFree(&mineImage);

	// Done
	return (0);
}

