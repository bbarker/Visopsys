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
//  shutdown.c
//

// This is the UNIX-style command for shutting down the system

/* This is the text that appears when a user requests help about this program
<help>

 -- shutdown --

A command for shutting down (and/or rebooting) the computer.

Usage:
  shutdown [-T] [-e] [-f] [-h] [-r]

This command causes the system to shut down.

If the (optional) '-e' parameter is supplied, then 'shutdown' will attempt
to eject the boot medium (if applicable, such as a CD-ROM).

If the (optional) '-f' parameter is supplied, then it will attempt to ignore
errors and shut down regardless.  Use this with caution if filesystems do not
appear to be unmounting correctly; you may need to back up unsaved data before
shutting down.

If the (optional) '-h' parameter is supplied, then it will shut down and power
off the system, if possible.  This is the default in text mode.

If the (optional) '-r' parameter is supplied, then it will reboot the computer
rather than simply halting.

In graphics mode, if the '-h' and '-r' parameters are not supplied, the
program prompts the user to select 'reboot' or 'shut down'.  If the system is
currently booted from a CD-ROM, the dialog box also offers a checkbox to eject
the disc.  If the '-h' or '-r' parameters are used, the dialog will not appear
and the computer will halt or reboot.

Options:
-T  : Force text mode operation.
-e  : Eject the boot medium.
-f  : Force shutdown and ignore errors.
-h  : Halt.
-r  : Reboot.

</help>
*/

#include <libintl.h>
#include <locale.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/api.h>
#include <sys/env.h>
#include <sys/paths.h>
#include <sys/window.h>

#define _(string) gettext(string)

#define REBOOT			_("Reboot")
#define SHUTDOWN		_("Shut down")
#define EJECT			_("Eject CD-ROM")
#define WINDOW_TITLE	SHUTDOWN
#define EJECT_MESS		_("Ejecting, please wait...")
#define NOUNLOCK_MESS	_("Unable to unlock the media door")
#define NOEJECT_MESS	_("Can't seem to eject.  Try pushing\nthe 'eject' " \
	"button now.")

static int graphics = 0;
static int eject = 0;
static objectKey window = NULL;
static objectKey rebootIcon = NULL;
static objectKey shutdownIcon = NULL;
static objectKey ejectCheckbox = NULL;
static disk sysDisk;


static void doEject(void)
{
	int status = 0;
	objectKey bannerDialog = NULL;

	if (graphics)
	{
		bannerDialog = windowNewBannerDialog(window, _("Ejecting"),
			EJECT_MESS);
	}
	else
	{
		printf("\n%s ", EJECT_MESS);
	}

	if (diskSetLockState(sysDisk.name, 0) < 0)
	{
		if (graphics)
		{
			if (bannerDialog)
				windowDestroy(bannerDialog);

			windowNewErrorDialog(window, _("Error"), NOUNLOCK_MESS);
		}
		else
		{
			printf("\n\n%s\n", NOUNLOCK_MESS);
		}
	}
	else
	{
		status = diskSetDoorState(sysDisk.name, 1);
		if (status < 0)
		{
			// Try a second time.  Sometimes 2 attempts seems to help.
			status = diskSetDoorState(sysDisk.name, 1);

			if (status < 0)
			{
				if (graphics)
				{
					if (bannerDialog)
						windowDestroy(bannerDialog);

					windowNewInfoDialog(window, "Hmm", NOEJECT_MESS);
				}
				else
				{
					printf("\n\n%s\n", NOEJECT_MESS);
				}
			}
		}
		else
		{
			if (graphics)
			{
				if (bannerDialog)
					windowDestroy(bannerDialog);
			}
			else
			{
				printf("\n");
			}
		}
	}
}


static void refreshWindow(void)
{
	// We got a 'window refresh' event (probably because of a language
	// switch), so we need to update things

	// Re-get the language setting
	setlocale(LC_ALL, getenv(ENV_LANG));
	textdomain("shutdown");

	// Re-get the character set
	if (getenv(ENV_CHARSET))
		windowSetCharSet(window, getenv(ENV_CHARSET));

	// Refresh the 'reboot' icon
	windowComponentSetData(rebootIcon, REBOOT, strlen(REBOOT), 1 /* redraw */);

	// Refresh the 'shutdown' icon
	windowComponentSetData(shutdownIcon, SHUTDOWN, strlen(SHUTDOWN),
		1 /* redraw */);

	if (ejectCheckbox)
		// Refresh the 'eject' checkbox
		windowComponentSetData(ejectCheckbox, EJECT, strlen(EJECT),
			1 /* redraw */);

	// Refresh the window title
	windowSetTitle(window, WINDOW_TITLE);
}


static void eventHandler(objectKey key, windowEvent *event)
{
	int selected = 0;

	// Check for window events.
	if (key == window)
	{
		// Check for window refresh
		if (event->type == EVENT_WINDOW_REFRESH)
			refreshWindow();

		// Check for the window being closed
		else if (event->type == EVENT_WINDOW_CLOSE)
		{
			windowGuiStop();
			windowDestroy(window);
			exit(0);
		}
	}

	else if (((key == rebootIcon) || (key == shutdownIcon)) &&
		(event->type == EVENT_MOUSE_LEFTUP))
	{
		windowGuiStop();

		if (ejectCheckbox)
		{
			windowComponentGetSelected(ejectCheckbox, &selected);

			if (eject || (selected == 1))
				doEject();
		}

		windowDestroy(window);

		systemShutdown((key == rebootIcon), 0);
		while (1);
	}
}


static void constructWindow(void)
{
	// If we are in graphics mode, make a window rather than operating on the
	// command line.

	componentParameters params;
	image iconImage;

	// Create a new window, with small, arbitrary size and location
	window = windowNew(multitaskerGetCurrentProcessId(), WINDOW_TITLE);
	if (!window)
		return;

	memset(&params, 0, sizeof(componentParameters));
	params.gridWidth = 1;
	params.gridHeight = 1;
	params.padTop = 20;
	params.padBottom = 20;
	params.padLeft = 20;
	params.padRight = 20;
	params.orientationX = orient_center;
	params.orientationY = orient_middle;
	params.flags = (WINDOW_COMPFLAG_CUSTOMFOREGROUND |
		WINDOW_COMPFLAG_CUSTOMBACKGROUND | WINDOW_COMPFLAG_CANFOCUS);
	params.foreground = COLOR_WHITE;
	windowGetColor("desktop", &params.background);

	// Create a reboot icon
	memset(&iconImage, 0, sizeof(image));
	if (imageLoad(PATH_SYSTEM_ICONS "/reboot.ico", 64, 64, &iconImage) >= 0)
	{
		rebootIcon = windowNewIcon(window, &iconImage, REBOOT, &params);
		windowRegisterEventHandler(rebootIcon, &eventHandler);
		imageFree(&iconImage);
	}

	// Create a shut down icon
	memset(&iconImage, 0, sizeof(image));
	if (imageLoad(PATH_SYSTEM_ICONS "/shutdown.ico", 64, 64, &iconImage) >= 0)
	{
		params.gridX = 1;
		shutdownIcon =
			windowNewIcon(window, &iconImage, SHUTDOWN, &params);
		windowRegisterEventHandler(shutdownIcon, &eventHandler);
		imageFree(&iconImage);
	}

	// Find out whether we are currently running from a CD-ROM
	if (sysDisk.type & DISKTYPE_CDROM)
	{
		// Yes.  Make an 'eject cd' checkbox.
		params.gridX = 0;
		params.gridY = 1;
		params.gridWidth = 2;
		params.padTop = 0;
		ejectCheckbox = windowNewCheckbox(window, EJECT, &params);
	}

	// Register an event handler to catch window close events
	windowRegisterEventHandler(window, &eventHandler);

	windowSetBackgroundColor(window, &params.background);
	windowSetVisible(window, 1);

	return;
}


int main(int argc, char *argv[])
{
	int status = 0;
	char opt;
	int force = 0;
	int reboot = 0;

	setlocale(LC_ALL, getenv(ENV_LANG));
	textdomain("shutdown");

	// Are graphics enabled?
	graphics = graphicsAreEnabled();

	// Check options
	while (strchr("efhrT?", (opt = getopt(argc, argv, "efhrT"))))
	{
		switch (opt)
		{
			case 'e':
				// Eject boot media
				eject = 1;
				break;

			case 'f':
				// Shut down forcefully
				force = 1;
				break;

			case 'h':
				// Halt
				graphics = 0;
				break;

			case 'r':
				// Reboot
				graphics = 0;
				reboot = 1;
				break;

			case 'T':
				// Force text mode
				graphics = 0;
				break;

			default:
				fprintf(stderr, _("Unknown option '%c'\n"), optopt);
				return (status = ERR_INVALID);
		}
	}

	// Get the system disk
	memset(&sysDisk, 0, sizeof(disk));
	fileGetDisk("/", &sysDisk);

	// If graphics are enabled, show a query dialog asking whether to shut
	// down or reboot
	if (graphics)
	{
		constructWindow();

		// Run the GUI
		windowGuiRun();
	}
	else
	{
		if (eject && (sysDisk.type & DISKTYPE_CDROM))
			doEject();

		// There's a nice system function for doing this.
		status = systemShutdown(reboot, force);
		if (status < 0)
		{
			if (!force)
				printf(_("Use \"%s -f\" to force.\n"), argv[0]);
			return (status);
		}

		// Wait for death
		while (1);
	}

	return (status = 0);
}

