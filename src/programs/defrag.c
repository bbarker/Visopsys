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
//  defrag.c
//

// This is a program for defragmenting a disk

/* This is the text that appears when a user requests help about this program
<help>

 -- defrag --

This command will defragment a filesystem.

Usage:
  defrag [-s] [-T] [disk_name]

The 'defrag' program is interactive, and operates in both text and graphics
modes.  The -T option forces defrag to operate in text-only mode.  The -s
option forces 'silent' mode (i.e. no unnecessary output or status messages
are printed/displayed).

The last (optional) parameter is the name of a (logical) disk to defragment
(use the 'disks' command to list the disks).  A defrag can only proceed if
the driver for the requested filesystem type supports this functionality.

Options:
-s         : Silent mode
-T         : Force text mode operation

</help>
*/

#include <errno.h>
#include <libintl.h>
#include <locale.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/api.h>
#include <sys/env.h>
#include <sys/vsh.h>

#define _(string) gettext(string)

static int graphics = 0;
static int processId = 0;
static disk diskInfo[DISK_MAXDEVICES];
static int numberDisks = 0;
static int silentMode = 0;


static int yesOrNo(char *question)
{
	char character;

	if (graphics)
		return (windowNewQueryDialog(NULL, _("Confirmation"), question));
	else
	{
		printf("\n%s (y/n): ", question);
		textInputSetEcho(0);

		while (1)
		{
			character = getchar();

			if ((character == 'y') || (character == 'Y'))
			{
				printf("Yes\n");
				textInputSetEcho(1);
				return (1);
			}
			else if ((character == 'n') || (character == 'N'))
			{
				printf("No\n");
				textInputSetEcho(1);
				return (0);
			}
		}
	}
}


static void pause(void)
{
	printf("%s", _("\nPress any key to continue. "));
	getchar();
	printf("\n");
}


__attribute__((format(printf, 1, 2)))
static void error(const char *format, ...)
{
	// Generic error message code for either text or graphics modes

	va_list list;
	char output[MAXSTRINGLENGTH];

	if (silentMode)
		return;

	va_start(list, format);
	vsnprintf(output, MAXSTRINGLENGTH, format, list);
	va_end(list);

	if (graphics)
	{
		windowNewErrorDialog(NULL, _("Error"), output);
	}
	else
	{
		printf("\n\n%s\n", output);
		pause();
	}
}


static int chooseDisk(void)
{
	// This is where the user chooses the disk on which to install

	int status = 0;
	int diskNumber = -1;
	objectKey chooseWindow = NULL;
	componentParameters params;
	objectKey diskList = NULL;
	objectKey okButton = NULL;
	objectKey cancelButton = NULL;
	listItemParameters diskListParams[DISK_MAXDEVICES];
	char *diskStrings[DISK_MAXDEVICES];
	windowEvent event;
	int count;

	#define CHOOSEDISK_STRING _("Please choose the disk to defragment:")

	memset(&params, 0, sizeof(componentParameters));
	params.gridX = 0;
	params.gridY = 0;
	params.gridWidth = 2;
	params.gridHeight = 1;
	params.padTop = 5;
	params.padLeft = 5;
	params.padRight = 5;
	params.orientationX = orient_center;
	params.orientationY = orient_middle;

	memset(diskListParams, 0, (numberDisks * sizeof(listItemParameters)));
	for (count = 0; count < numberDisks; count ++)
		snprintf(diskListParams[count].text, WINDOW_MAX_LABEL_LENGTH,
			"%s  [ %s ]", diskInfo[count].name, diskInfo[count].partType);

	if (graphics)
	{
		chooseWindow = windowNew(processId, _("Choose Disk"));
		windowNewTextLabel(chooseWindow, CHOOSEDISK_STRING, &params);

		// Make a window list with all the disk choices
		params.gridY = 1;
		diskList = windowNewList(chooseWindow, windowlist_textonly, 5, 1, 0,
			diskListParams, numberDisks, &params);
		windowComponentFocus(diskList);

		// Make 'OK' and 'cancel' buttons
		params.gridY = 2;
		params.gridWidth = 1;
		params.padBottom = 5;
		params.orientationX = orient_right;
		params.flags |= WINDOW_COMPFLAG_FIXEDWIDTH;
		okButton = windowNewButton(chooseWindow, _("OK"), NULL, &params);

		params.gridX = 1;
		params.orientationX = orient_left;
		cancelButton = windowNewButton(chooseWindow, _("Cancel"), NULL,
			&params);

		// Make the window visible
		windowRemoveMinimizeButton(chooseWindow);
		windowRemoveCloseButton(chooseWindow);
		windowSetResizable(chooseWindow, 0);
		windowSetVisible(chooseWindow, 1);

		while (1)
		{
			// Check for our OK button
			status = windowComponentEventGet(okButton, &event);
			if ((status < 0) || ((status > 0) &&
				(event.type == EVENT_MOUSE_LEFTUP)))
			{
				windowComponentGetSelected(diskList, &diskNumber);
				break;
			}

			// Check for our Cancel button
			status = windowComponentEventGet(cancelButton, &event);
			if ((status < 0) || ((status > 0) &&
				(event.type == EVENT_MOUSE_LEFTUP)))
			{
				break;
			}

			// Done
			multitaskerYield();
		}

		windowDestroy(chooseWindow);
		chooseWindow = NULL;
	}

	else
	{
		for (count = 0; count < numberDisks; count ++)
			diskStrings[count] = diskListParams[count].text;
		diskNumber = vshCursorMenu(CHOOSEDISK_STRING, diskStrings, numberDisks,
			10 /* max rows */, 0 /* selected */);
	}

	return (diskNumber);
}


static int mountedCheck(disk *theDisk)
{
	// If the disk is mounted, query whether to ignore, unmount, or cancel

	int status = 0;
	int choice = 0;
	char tmpChar[160];
	char character;

	if (!(theDisk->mounted))
		return (status = 0);
	else if (silentMode)
		return (status = ERR_CANCELLED);

	sprintf(tmpChar, _("The disk is mounted as %s.  It is STRONGLY "
		"recommended\nthat you unmount before continuing"),
		theDisk->mountPoint);

	if (graphics)
		choice = windowNewChoiceDialog(NULL, _("Disk is mounted"), tmpChar,
			(char *[]){ _("Ignore"), _("Unmount"), _("Cancel") }, 3, 1);
	else
	{
		printf(_("\n%s (I)gnore/(U)nmount/(C)ancel?: "), tmpChar);
		textInputSetEcho(0);

		while (1)
		{
			character = getchar();

			if ((character == 'i') || (character == 'I'))
			{
				printf("%s", _("Ignore\n"));
				choice = 0;
				break;
			}
			else if ((character == 'u') || (character == 'U'))
			{
				printf("%s", _("Unmount\n"));
				choice = 1;
				break;
			}
			else if ((character == 'c') || (character == 'C'))
			{
				printf("%s", _("Cancel\n"));
				choice = 2;
				break;
			}
		}

		textInputSetEcho(1);
	}

	if ((choice < 0) || (choice == 2))
		// Cancelled
		return (status = ERR_CANCELLED);

	else if (choice == 1)
	{
		// Try to unmount the filesystem
		status = filesystemUnmount(theDisk->mountPoint);
		if (status < 0)
		{
			error(_("Unable to unmount %s"), theDisk->mountPoint);
			return (status);
		}
	}

	return (status = 0);
}


int main(int argc, char *argv[])
{
	int status = 0;
	char opt;
	int diskNumber = -1;
	progress prog;
	objectKey progressDialog = NULL;
	char tmpChar[240];
	int count;

	setlocale(LC_ALL, getenv(ENV_LANG));
	textdomain("defrag");

	// Are graphics enabled?
	graphics = graphicsAreEnabled();

	// Check options
	while (strchr("sT?", (opt = getopt(argc, argv, "sT"))))
	{
		switch (opt)
		{
			case 's':
				// Operate in silent/script mode
				silentMode = 1;
				break;

			case 'T':
				// Force text mode
				graphics = 0;
				break;

			default:
				error(_("Unknown option '%c'"), optopt);
				return (status = ERR_INVALID);
		}
	}

	// Call the kernel to give us the number of available disks
	numberDisks = diskGetCount();

	status = diskGetAll(diskInfo, (DISK_MAXDEVICES * sizeof(disk)));
	if (status < 0)
	{
		// Eek.  Problem getting disk info
		error("%s", _("Error getting disks info"));
		return (errno = status);
	}

	if (!graphics && !silentMode)
		// Print a message
		printf("%s", _("\nVisopsys DEFRAG Utility\nCopyright (C) 1998-2016 J. "
			"Andrew McLaughlin\n"));

	if (argc > 1)
	{
		// The user can specify the disk name as an argument.  Try to see
		// whether they did so.
		for (count = 0; count < numberDisks; count ++)
		{
			if (!strcmp(diskInfo[count].name, argv[argc - 1]))
			{
				diskNumber = count;
				break;
			}
		}
	}

	processId = multitaskerGetCurrentProcessId();

	// Check privilege level
	if (multitaskerGetProcessPrivilege(processId))
	{
		error("%s", _("You must be a privileged user to use this command.\n"
			"(Try logging in as user \"admin\")"));
		return (errno = ERR_PERMISSION);
	}

	if (diskNumber == -1)
	{
		if (silentMode)
			// Can't prompt for a disk in silent mode
			return (errno = ERR_INVALID);

		// The user has not specified a disk name.  We need to display the
		// list of available disks and prompt them.

		diskNumber = chooseDisk();
		if (diskNumber < 0)
			return (status = 0);
	}

	// Make sure we know the filesystem type
	if (!strcmp(diskInfo[diskNumber].fsType, "unknown"))
	{
		// Scan for it explicitly
		status = diskGetFilesystemType(diskInfo[diskNumber].name,
			diskInfo[diskNumber].fsType, FSTYPE_MAX_NAMELENGTH);
		if ((status < 0) || !strcmp(diskInfo[diskNumber].fsType, "unknown"))
		{
			error(_("Unknown filesystem type on disk \"%s\""),
				diskInfo[diskNumber].name);
			return (errno = ERR_NOTIMPLEMENTED);
		}
	}

	// Make sure things are up to date
	status = diskGet(diskInfo[diskNumber].name, &diskInfo[diskNumber]);
	if (status < 0)
	{
		error(_("Error getting info for disk \"%s\""),
			diskInfo[diskNumber].name);
		return (errno = status);
	}

	// Make sure that the defragment operation is supported for the selected
	// disk
	if (!(diskInfo[diskNumber].opFlags & FS_OP_DEFRAG))
	{
		error(_("Defragmenting the filesystem type \"%s\" is not supported"),
			diskInfo[diskNumber].fsType);
		return (errno = ERR_NOTIMPLEMENTED);
	}

	if (!silentMode)
	{
		sprintf(tmpChar, _("Defragmenting disk %s.  Are you sure?"),
			diskInfo[diskNumber].name);
		if (!yesOrNo(tmpChar))
		{
			printf("%s", _("\nQuitting.\n"));
			return (status = 0);
		}
	}

	// Make sure it's not mounted
	status = mountedCheck(&diskInfo[diskNumber]);
	if (status < 0)
		return (errno = status);

	memset((void *) &prog, 0, sizeof(progress));
	if (graphics)
		progressDialog = windowNewProgressDialog(NULL, _("Defragmenting..."),
			&prog);
	else
		vshProgressBar(&prog);

	status = filesystemDefragment(diskInfo[diskNumber].name, &prog);

	if (!graphics)
		vshProgressBarDestroy(&prog);

	if ((status >= 0) && !silentMode)
	{
		sprintf(tmpChar, "%s", _("Defragmentation complete"));
		if (graphics)
			windowNewInfoDialog(NULL, _("Success"), tmpChar);
		else
			printf("\n%s\n", tmpChar);
	}

	if (graphics)
		windowProgressDialogDestroy(progressDialog);

	return (errno = status);
}

