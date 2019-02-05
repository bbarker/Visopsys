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
//  console.c
//

// This command will display/close the console window

/* This is the text that appears when a user requests help about this program
<help>

 -- console --

Launch a console window.

Usage:
  console

(Only available in graphics mode)

This command will launch a window in which console messages are displayed.
This is useful for viewing logging or error messages that do not appear
in other windows.

</help>
*/

#include <libintl.h>
#include <locale.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/window.h>
#include <sys/api.h>
#include <sys/env.h>

#define _(string) gettext(string)

#define WINDOW_TITLE	_("Console Window")

objectKey window = NULL;


static void refreshWindow(void)
{
	// We got a 'window refresh' event (probably because of a language
	// switch), so we need to update things

	// Re-get the language setting
	setlocale(LC_ALL, getenv(ENV_LANG));
	textdomain("console");

	// Re-get the character set
	if (getenv(ENV_CHARSET))
		windowSetCharSet(window, getenv(ENV_CHARSET));

	// Refresh the window title
	windowSetTitle(window, WINDOW_TITLE);
}


static void eventHandler(objectKey key, windowEvent *event)
{
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
}


int main(int argc, char *argv[])
{
	int status = 0;

	setlocale(LC_ALL, getenv(ENV_LANG));
	textdomain("console");

	// Only work in graphics mode
	if (!graphicsAreEnabled())
	{
		fprintf(stderr, _("\nThe \"%s\" command only works in graphics "
			"mode\n"), (argc? argv[0] : ""));
		errno = ERR_NOTINITIALIZED;
		return (status = errno);
	}

	// Create a new window, with small, arbitrary size and location
	window = windowNew(multitaskerGetCurrentProcessId(), WINDOW_TITLE);

	// Put the console text area in the window
	status = windowAddConsoleTextArea(window);
	if (status < 0)
	{
		if (status == ERR_ALREADY)
		{
			// There's already a console window open somewhere
			windowNewErrorDialog(NULL, _("Error"), _("Cannot open more than "
				"one console window!"));
		}
		else
		{
			windowNewErrorDialog(NULL, _("Error"), _("Error opening the "
				"console window!"));
		}

		windowDestroy(window);
		return (status);
	}

	// Not resizable
	windowSetResizable(window, 0);

	// Register an event handler to catch window close events
	windowRegisterEventHandler(window, &eventHandler);

	// Make it visible
	windowSetVisible(window, 1);

	// Run the GUI
	windowGuiRun();

	// Destroy the window
	windowDestroy(window);

	// Done
	return (status);
}

