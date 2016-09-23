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
//  format.c
//

// This is a program for formatting a disk

/* This is the text that appears when a user requests help about this program
<help>

 -- format --

This command will create a new, empty filesystem.

Usage:
  format [-l] [-n name] [-s] [-t type] [-T] [disk_name]

The 'format' program is interactive, and operates in both text and graphics
mode.  The -l option forces a 'long' format (if supported), which clears the
entire data area of the filesystem.  The -n option sets the volume name
(label).  The -s option forces 'silent' mode (i.e. no unnecessary output
or status messages are printed/displayed).  The -T option forces format to
operate in text-only mode.

The -t option is the desired filesystem type.  Currently the default type,
if none is specified, is FAT.  The names of supported filesystem types are
dependent upon the names allowed by particular filesystem drivers.  For
example, the FAT filesystem driver will accept the generic type name 'fat',
in which case it will then choose the most appropriate FAT subtype for the
size of the disk.  Otherwise it will accept the explicit subtypes 'fat12',
'fat16' or 'fat32'.  Other filesystem types can be expected to exhibit the
same sorts of behaviour as they are developed.

Some currently-supported arguments to the -t option are:

  none        : Erases all known filesystem types
  fat         : DOS/Windows FAT
    fat12     : 12-bit FAT
    fat16     : 16-bit FAT
    fat32     : 32-bit FAT, or VFAT
  ext         : Linux EXT
    ext2      : Linux EXT2 (EXT3 not yet supported)
  linux-swap  : Linux swap
  ntfs        : Windows NTFS

The third (optional) parameter is the name of a (logical) disk to format
(use the 'disks' command to list the disks).  A format can only proceed if
the driver for the requested filesystem type supports this functionality.

Options:
-l         : Long format
-n <name>  : Set the volume name (label)
-s         : Silent mode
-t <type>  : Format as this filesystem type.
-T         : Force text mode operation

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
#include <sys/ntfs.h>
#include <sys/paths.h>
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
		printf(_("\n%s (y/n): "), question);
		textInputSetEcho(0);

		while (1)
		{
			character = getchar();

			if ((character == 'y') || (character == 'Y'))
			{
				printf("%s", _("Yes\n"));
				textInputSetEcho(1);
				return (1);
			}
			else if ((character == 'n') || (character == 'N'))
			{
				printf("%s", _("No\n"));
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

	#define CHOOSEDISK_STRING _("Please choose the disk to format:")

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

	sprintf(tmpChar, _("The disk is mounted as %s.  It is STRONGLY "
		"recommended\nthat you unmount before continuing"),
		theDisk->mountPoint);

	if (graphics)
	{
		choice =
			windowNewChoiceDialog(NULL, _("Disk is mounted"), tmpChar,
				(char *[]){ _("Ignore"), _("Unmount"), _("Cancel") }, 3, 1);
	}
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


static int copyBootSector(disk *theDisk, const char *fsType)
{
	// Overlay the "no boot" code from a /system/boot/ boot sector onto the
	// boot sector of the target disk

	int status = 0;
	char bootSectFilename[MAX_PATH_NAME_LENGTH];
	char command[MAX_PATH_NAME_LENGTH];

	if (!strncasecmp(fsType, "fat", 3))
	{
		strcpy(bootSectFilename, PATH_SYSTEM_BOOT "/bootsect.fatnoboot");
		if (!strcasecmp(fsType, "fat32"))
			strcat(bootSectFilename, "32");
	}
	else
		// No non-FAT boot sectors at the moment.
		return (status = 0);

	// Find the boot sector
	status = fileFind(bootSectFilename, NULL);
	if (status < 0)
	{
		// Isn't one.  No worries.
		printf(_("No boot sector available for filesystem type %s\n"), fsType);
		return (status);
	}

	// Use our companion program to do the work
	sprintf(command, PATH_PROGRAMS "/copy-boot %s %s", bootSectFilename,
		theDisk->name);
	status = system(command);

	diskSync(theDisk->name);

	if (status < 0)
	{
		error(_("Error %d copying boot sector \"%s\" to disk %s"), status,
			bootSectFilename, theDisk->name);
		return (status);
	}

	return (status = 0);
}


static void usage(char *name)
{
	printf("%s", _("usage:\n"));
	printf(_("%s [-l] [-s] [-t type] [-T] [disk_name]\n"), name);
	return;
}


int main(int argc, char *argv[])
{
	int status = 0;
	char opt;
	int diskNumber = -1;
	char rootDisk[DISK_MAX_NAMELENGTH];
	char type[16];
	char volName[MAX_NAME_LENGTH];
	int longFormat = 0;
	progress prog;
	objectKey progressDialog = NULL;
	char tmpChar[240];
	int count;

	setlocale(LC_ALL, getenv(ENV_LANG));
	textdomain("format");

	// Clear stack data
	memset(volName, 0, MAX_NAME_LENGTH);

	// Are graphics enabled?
	graphics = graphicsAreEnabled();

	// By default, we do 'generic' (i.e. let the driver make decisions) FAT.
	strcpy(type, "fat");

	// Check options
	while (strchr("lnstT:?", (opt = getopt(argc, argv, "ln:st:T"))))
	{
		switch (opt)
		{
			case 'l':
				// Long format
				longFormat = 1;
				break;

			case 'n':
				// Volume name
				if (!optarg)
				{
					error("%s", _("Missing volume name argument for '-n' "
						"option"));
					usage(argv[0]);
					return (status = ERR_NULLPARAMETER);
				}
				strcpy(volName, optarg);
				break;

			case 's':
				// Operate in silent/script mode
				silentMode = 1;
				break;

			case 't':
				// Desired filesystem type
				if (!optarg)
				{
					error("%s", _("Missing type argument for '-t' option"));
					usage(argv[0]);
					return (status = ERR_NULLPARAMETER);
				}
				strcpy(type, optarg);
				break;

			case 'T':
				// Force text mode
				graphics = 0;
				break;

			case ':':
				error(_("Missing parameter for %s option"), argv[optind - 1]);
				usage(argv[0]);
				return (status = ERR_NULLPARAMETER);

			default:
				error(_("Unknown option '%c'"), optopt);
				usage(argv[0]);
				return (status = ERR_INVALID);
		}
	}

	// Call the kernel to give us the number of available disks
	numberDisks = diskGetCount();

	status = diskGetAll(diskInfo, (DISK_MAXDEVICES * sizeof(disk)));
	if (status < 0)
		// Eek.  Problem getting disk info
		return (status);

	if (!graphics && !silentMode)
		// Print a message
		printf("%s", _("\nVisopsys FORMAT Utility\nCopyright (C) 1998-2016 J. "
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
		return (status = ERR_PERMISSION);
	}

	if (diskNumber == -1)
	{
		if (silentMode)
			// Can't prompt for a disk in silent mode
			return (status = ERR_INVALID);

		// The user has not specified a disk name.  We need to display the
		// list of available disks and prompt them.

		diskNumber = chooseDisk();
		if (diskNumber < 0)
			return (status = 0);

		sprintf(tmpChar, _("Formatting disk %s as %s.  All data currenly on "
			"the disk will be lost.\nAre you sure?"),
			diskInfo[diskNumber].name, type);
		if (!yesOrNo(tmpChar))
		{
			printf("%s", _("\nQuitting.\n"));
			return (status = 0);
		}
	}

	// Make sure it's not mounted
	status = mountedCheck(&diskInfo[diskNumber]);
	if (status < 0)
		return (status);

	// Get the root disk
	status = diskGetBoot(rootDisk);
	if (status >= 0)
	{
		if (!strcmp(rootDisk, diskInfo[diskNumber].name))
		{
			if (!silentMode)
			{
				sprintf(tmpChar, "%s", _("\nYOU HAVE REQUESTED TO FORMAT YOUR "
					"ROOT DISK.  I probably shouldn't let you\ndo this.  "
					"After format is complete, you should shut down the "
					"computer.\nAre you SURE you want to proceed?"));
				if (!yesOrNo(tmpChar))
				{
					printf("%s", _("\nQuitting.\n"));
					return (status = 0);
				}
			}
		}
	}

	memset((void *) &prog, 0, sizeof(progress));
	if (graphics)
	{
		progressDialog = windowNewProgressDialog(NULL, _("Formatting..."),
			&prog);
	}
	else if (!silentMode)
	{
		vshProgressBar(&prog);
	}

	if (!strcasecmp(type, "none"))
	{
		status = filesystemClobber(diskInfo[diskNumber].name);
		prog.percentFinished = 100;
		prog.complete = 1;
	}
	else if (!strcasecmp(type, "ntfs"))
	{
		status = ntfsFormat(diskInfo[diskNumber].name, volName, longFormat,
			&prog);
		filesystemScan(diskInfo[diskNumber].name);
	}
	else
	{
		status = filesystemFormat(diskInfo[diskNumber].name, type, volName,
			longFormat, &prog);
	}

	if (!graphics && !silentMode)
		vshProgressBarDestroy(&prog);

	if (status >= 0)
	{
		// The kernel's format code creates a 'dummy' boot sector.  If we have
		// a proper one stored in the /system/boot directory, copy it to the
		// disk.
		copyBootSector(&diskInfo[diskNumber], type);

		if (!silentMode)
		{
			sprintf(tmpChar, "Format complete");
			if (graphics)
				windowNewInfoDialog(progressDialog, _("Success"), tmpChar);
			else
				printf("\n%s\n", tmpChar);
		}
	}

	if (graphics)
		windowProgressDialogDestroy(progressDialog);

	return (status);
}

