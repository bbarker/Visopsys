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
//  filesys.c
//

// This is a program for editing the mount.conf file in a user-friendly way.

/* This is the text that appears when a user requests help about this program
<help>

 -- filesys --

Program for specifying mounting characteristics of file systems.

Usage:
  filesys

The 'filesys' (File Systems) program is interactive, and may only be used
in graphics mode.  It can be used to edit the mount configuration file to
specify mount points of file systems, and whether or not to auto-mount
them at boot time.

</help>
*/

#include <libintl.h>
#include <locale.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/api.h>
#include <sys/env.h>
#include <sys/paths.h>

#define _(string) gettext(string)

#define WINDOW_TITLE			_("File systems")
#define MOUNT_POINT				_("Mount point:")
#define MOUNT_AUTOMATICALLY		_("Mount automatically at boot")
#define UNSAVED_CHANGES			_("Unsaved changes")
#define QUIT_WITHOUT_WRITE		_("Quit without writing changes?")
#define QUIT					_("Quit")
#define CANCEL					_("Cancel")
#define SAVE					_("Save")

static int readOnly = 1;
static int processId = 0;
static int privilege = 0;
static int numberDisks = 0;
static disk *diskInfo = NULL;
static listItemParameters *diskListParams = NULL;
static variableList mountConfig;
static int changesPending = 0;
static objectKey window = NULL;
static objectKey diskList = NULL;
static objectKey mountPointLabel = NULL;
static objectKey mountPointField = NULL;
static objectKey autoMountCheckbox = NULL;
static objectKey saveButton = NULL;
static objectKey quitButton = NULL;


static void freeMemory(void)
{
	if (diskInfo)
		free(diskInfo);

	if (diskListParams)
		free(diskListParams);

	variableListDestroy(&mountConfig);
}


__attribute__((format(printf, 1, 2)))
static void error(const char *format, ...)
{
	// Generic error message code

	va_list list;
	char output[MAXSTRINGLENGTH];

	va_start(list, format);
	vsnprintf(output, MAXSTRINGLENGTH, format, list);
	va_end(list);

	windowNewErrorDialog(window, _("Error"), output);
}


__attribute__((format(printf, 2, 3))) __attribute__((noreturn))
static void quit(int status, const char *message, ...)
{
	// Shut everything down

	va_list list;
	char output[MAXSTRINGLENGTH];

	va_start(list, message);
	vsnprintf(output, MAXSTRINGLENGTH, message, list);
	va_end(list);
	strcat(output, _("  Quitting."));

	if (status < 0)
		windowNewErrorDialog(window, _("Error"), output);

	windowGuiStop();

	if (window)
		windowDestroy(window);

	freeMemory();
	exit(status);
}


static void getDiskList(void)
{
	// Make a list of disks on which we can install

	int status = 0;
	int count;

	// Call the kernel to give us the number of available disks
	numberDisks = diskGetCount();
	if (numberDisks <= 0)
		// No disks
		quit(status, "%s", _("No disks to work with."));

	diskInfo = malloc(numberDisks * sizeof(disk));
	diskListParams = malloc(numberDisks * sizeof(listItemParameters));
	if (!diskInfo || !diskListParams)
		quit(status, "%s", _("Memory allocation error."));

	status = diskGetAll(diskInfo, (DISK_MAXDEVICES * sizeof(disk)));
	if (status < 0)
		// Eek.  Problem getting disk info
		quit(status, "%s", _("Unable to get disk information."));

	memset(diskListParams, 0, (numberDisks * sizeof(listItemParameters)));
	for (count = 0; count < numberDisks; count ++)
		snprintf(diskListParams[count].text, WINDOW_MAX_LABEL_LENGTH,
			"%s  [ %s ]", diskInfo[count].name, diskInfo[count].partType);
}


static void getMountConfig(void)
{
	int status = 0;

	// Try reading the mount configuration file
	status = configRead(DISK_MOUNT_CONFIG, &mountConfig);
	if (status < 0)
	{
		// Maybe the file doesn't exist.  Try to make an empty variable list
		// for it.
		status = variableListCreate(&mountConfig);
		if (status < 0)
			quit(status, "%s", _("Can't read/create the mount configuration"));
	}
}


static int saveMountConfig(void)
{
	int status = 0;

	// Try writing the mount configuration file
	status = configWrite(DISK_MOUNT_CONFIG, &mountConfig);
	if (status < 0)
		error("%s", _("Can't write the mount configuration"));
	else
	{
		changesPending = 0;
		windowComponentSetEnabled(saveButton, 0);
	}

	return (status);
}


static int getAutoMount(int diskNumber)
{
	char variable[128];
	const char *value = NULL;

	snprintf(variable, 128, "%s.automount", diskInfo[diskNumber].name);
	value = variableListGet(&mountConfig, variable);
	if (!value)
		return (0);

	if (!strncmp(value, "yes", 128))
		return (1);
	else
		return (0);
}


static void setAutoMount(int diskNumber, int autoMount)
{
	char variable[128];

	snprintf(variable, 128, "%s.automount", diskInfo[diskNumber].name);
	variableListSet(&mountConfig, variable, (autoMount? "yes" : "no"));

	changesPending = 1;
	windowComponentSetEnabled(saveButton, 1);
}


static void getMountPoint(int diskNumber, char *mountPoint)
{
	char variable[128];
	const char *value = NULL;

	snprintf(variable, 128, "%s.mountpoint", diskInfo[diskNumber].name);
	value = variableListGet(&mountConfig, variable);

	if (value)
		strncpy(mountPoint, value, MAX_PATH_LENGTH);
	else
		mountPoint[0] = '\0';
}


static void setMountPoint(int diskNumber, char *mountPoint)
{
	char variable[128];
	const char *value = NULL;
	int makeAutoMount = 0;

	snprintf(variable, 128, "%s.mountpoint", diskInfo[diskNumber].name);
	value = variableListGet(&mountConfig, variable);

	// If there's nothing for this disk currently, also add an automount entry
	if (!value)
		makeAutoMount = 1;

	variableListSet(&mountConfig, variable, mountPoint);

	if (makeAutoMount)
		setAutoMount(diskNumber, 0);

	changesPending = 1;
	windowComponentSetEnabled(saveButton, 1);
}


static void select(int diskNumber)
{
	char mountPoint[MAX_PATH_LENGTH];

	getMountPoint(diskNumber, mountPoint);
	windowComponentSetData(mountPointField, mountPoint, MAX_PATH_LENGTH,
		1 /* redraw */);
	windowComponentSetSelected(autoMountCheckbox, getAutoMount(diskNumber));
}


static void refreshWindow(void)
{
	// We got a 'window refresh' event (probably because of a language switch),
	// so we need to update things

	// Re-get the language setting
	setlocale(LC_ALL, getenv(ENV_LANG));
	textdomain("filesys");

	// Re-get the character set
	if (getenv(ENV_CHARSET))
		windowSetCharSet(window, getenv(ENV_CHARSET));

	// Refresh the 'mount point' label
	windowComponentSetData(mountPointLabel, MOUNT_POINT, strlen(MOUNT_POINT),
		1 /* redraw */);

	// Refresh the 'mount automatically' checkbox
	windowComponentSetData(autoMountCheckbox, MOUNT_AUTOMATICALLY,
		strlen(MOUNT_AUTOMATICALLY), 1 /* redraw */);

	// Refresh the 'save' button
	windowComponentSetData(saveButton, SAVE, strlen(SAVE), 1 /* redraw */);

	// Refresh the 'quit' button
	windowComponentSetData(quitButton, QUIT, strlen(QUIT), 1 /* redraw */);

	// Refresh the window title
	windowSetTitle(window, WINDOW_TITLE);
}


static void eventHandler(objectKey key, windowEvent *event)
{
	int selectedDisk = 0;
	int selected = 0;
	char mountPoint[MAX_PATH_LENGTH];

	// Check for window events.
	if (key == window)
	{
		// Check for window refresh
		if (event->type == EVENT_WINDOW_REFRESH)
			refreshWindow();

		// Check for the window being closed
		else if (event->type == EVENT_WINDOW_CLOSE)
		{
			if (changesPending &&
				windowNewChoiceDialog(window, UNSAVED_CHANGES,
					QUIT_WITHOUT_WRITE, (char *[]){ QUIT, CANCEL }, 2, 0))
			{
				return;
			}

			windowGuiStop();
		}
	}

	else if ((key == diskList) && ((event->type & EVENT_MOUSE_DOWN) ||
		(event->type & EVENT_KEY_DOWN)))
	{
		windowComponentGetSelected(diskList, &selectedDisk);
		if (selectedDisk >= 0)
			select(selectedDisk);
	}

	else if ((key == mountPointField) && (event->type & EVENT_KEY_DOWN))
	{
		windowComponentGetSelected(diskList, &selectedDisk);
		if (selectedDisk >= 0)
		{
			if (windowComponentGetData(mountPointField, mountPoint,
				MAX_PATH_LENGTH) >= 0)
			{
				setMountPoint(selectedDisk, mountPoint);
			}
		}
	}

	else if ((key == autoMountCheckbox) && (event->type & EVENT_SELECTION))
	{
		windowComponentGetSelected(diskList, &selectedDisk);
		if (selectedDisk >= 0)
		{
			windowComponentGetSelected(autoMountCheckbox, &selected);
			if (selected >= 0)
				setAutoMount(selectedDisk, selected);
		}
	}

	else if ((key == saveButton) && (event->type == EVENT_MOUSE_LEFTUP))
	{
		saveMountConfig();
	}

	else if ((key == quitButton) && (event->type == EVENT_MOUSE_LEFTUP))
	{
		if (changesPending &&
			windowNewChoiceDialog(window, UNSAVED_CHANGES, QUIT_WITHOUT_WRITE,
				(char *[]){ QUIT, CANCEL }, 2, 0))
		{
			return;
		}

		windowGuiStop();
	}

}


static void constructWindow(void)
{
	int numRows = 0;
	componentParameters params;

	memset(&params, 0, sizeof(componentParameters));
	params.gridWidth = 2;
	params.gridHeight = 1;
	params.padTop = 10;
	params.padLeft = 5;
	params.padRight = 5;
	params.orientationX = orient_left;
	params.orientationY = orient_middle;

	window = windowNew(processId, WINDOW_TITLE);

	numRows = numberDisks;
	numRows = min(numRows, 10);
	numRows = max(numRows, 5);

	// Make a window list with all the disk choices
	diskList = windowNewList(window, windowlist_textonly, numRows, 1, 0,
		diskListParams, numberDisks, &params);
	windowRegisterEventHandler(diskList, &eventHandler);
	windowComponentFocus(diskList);

	params.gridY += 1;
	params.padTop = 5;
	mountPointLabel = windowNewTextLabel(window, MOUNT_POINT, &params);

	// Make a text field for the mount point.
	params.gridY += 1;
	params.padTop = 10;
	mountPointField = windowNewTextField(window, 30, &params);
	windowRegisterEventHandler(mountPointField, &eventHandler);
	if (privilege || readOnly)
		windowComponentSetEnabled(mountPointField, 0);

	// Make a checkbox for automounting.
	params.gridY += 1;
	params.padTop = 5;
	autoMountCheckbox = windowNewCheckbox(window, MOUNT_AUTOMATICALLY,
		&params);
	windowRegisterEventHandler(autoMountCheckbox, &eventHandler);
	if (privilege || readOnly)
		windowComponentSetEnabled(autoMountCheckbox, 0);

	// Make 'save', and 'quit' buttons
	params.gridY += 1;
	params.gridWidth = 1;
	params.padBottom = 5;
	params.orientationX = orient_right;
	params.flags |= WINDOW_COMPFLAG_FIXEDWIDTH;
	saveButton = windowNewButton(window, SAVE, NULL, &params);
	windowRegisterEventHandler(saveButton, &eventHandler);
	windowComponentSetEnabled(saveButton, 0);

	params.gridX += 1;
	params.orientationX = orient_left;
	quitButton = windowNewButton(window, QUIT, NULL, &params);
	windowRegisterEventHandler(quitButton, &eventHandler);

	// Select the first disk
	select(0);

	// Make the window visible
	windowRegisterEventHandler(window, &eventHandler);
	windowSetVisible(window, 1);

	return;
}


int main(int argc __attribute__((unused)), char *argv[])
{
	int status = 0;
	disk sysDisk;

	setlocale(LC_ALL, getenv(ENV_LANG));
	textdomain("filesys");

	// Only work in graphics mode
	if (!graphicsAreEnabled())
	{
		fprintf(stderr, _("\nThe \"%s\" command only works in graphics mode\n"),
			argv[0]);
		return (status = ERR_NOTINITIALIZED);
	}

	// Find out whether we are currently running on a read-only filesystem
	memset(&sysDisk, 0, sizeof(disk));
	if (!fileGetDisk(PATH_SYSTEM, &sysDisk))
		readOnly = sysDisk.readOnly;

	processId = multitaskerGetCurrentProcessId();
	privilege = multitaskerGetProcessPrivilege(processId);

	// Get our disk list
	getDiskList();

	// Get our list of mount variables
	getMountConfig();

	// Make our window
	constructWindow();

	// Run the GUI
	windowGuiRun();
	windowDestroy(window);

	// Done
	freeMemory();
	return (status = 0);
}

