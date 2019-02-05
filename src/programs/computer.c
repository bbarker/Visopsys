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
//  computer.c
//

// This is a graphical program for navigating the resources of the computer.
// It displays the disks, etc., and if they are clicked, mounts them (if
// necessary) and launches a file browser at the mount point.

/* This is the text that appears when a user requests help about this program
<help>

 -- computer --

A graphical program for navigating the resources of the computer.

Usage:
  computer

The computer program is interactive, and may only be used in graphics mode.
It displays a window with icons representing media resources such as floppy
disks, hard disks, CD-ROMs, and flash disks.  Clicking on an icon will cause
the system to attempt to mount (if necessary) the volume and open a file
browser window for that filesystem.

</help>
*/

#include <errno.h>
#include <libintl.h>
#include <locale.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/api.h>
#include <sys/ascii.h>
#include <sys/env.h>
#include <sys/paths.h>
#include <sys/window.h>

#define _(string) gettext(string)
#define gettext_noop(string) (string)

#define WINDOW_TITLE			_("Computer")
#define DEFAULT_ROWS			3
#define DEFAULT_COLUMNS			5
#define MAX_VARIABLE_LEN		128
#define FLOPPYDISK_ICONFILE		PATH_SYSTEM_ICONS "/floppy.ico"
#define HARDDISK_ICONFILE		PATH_SYSTEM_ICONS "/harddisk.ico"
#define CDROMDISK_ICONFILE		PATH_SYSTEM_ICONS "/cdrom.ico"
#define FLASHDISK_ICONFILE		PATH_SYSTEM_ICONS "/usbthumb.ico"
#define FILE_BROWSER			PATH_PROGRAMS "/filebrowse"
#define FORMAT					PATH_PROGRAMS "/format"

// Right-click menu

#define DISKMENU_BROWSE			0
#define DISKMENU_MOUNTAS		1
#define DISKMENU_UNMOUNT		2
#define DISKMENU_PROPERTIES		3

static windowMenuContents diskMenuContents = {
	4,
	{
		{ gettext_noop("Browse"), NULL },
		{ gettext_noop("Mount as..."), NULL },
		{ gettext_noop("Unmount"), NULL },
		{ gettext_noop("Properties"), NULL }
	}
};

static struct {
	unsigned diskType;
	const char *fileName;
	image *iconImage;
} icons[] = {
	{ DISKTYPE_FLOPPY, FLOPPYDISK_ICONFILE, NULL },
	{ DISKTYPE_CDROM, CDROMDISK_ICONFILE, NULL },
	{ DISKTYPE_FLASHDISK, FLASHDISK_ICONFILE, NULL },
	{ DISKTYPE_HARDDISK, HARDDISK_ICONFILE, NULL },
	{ 0, NULL, NULL }
};

static int processId = 0;
static int privilege = 0;
static int numDisks = 0;
static disk *disks = NULL;
static listItemParameters *iconParams = NULL;
static objectKey window = NULL;
static objectKey diskMenu = NULL;
static objectKey iconList = NULL;
static int stop = 0;


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


static void deallocateMemory(void)
{
	if (disks)
	{
		free(disks);
		disks = NULL;
	}

	if (iconParams)
	{
		free(iconParams);
		iconParams = NULL;
	}
}


static int getAndScanSelection(void)
{
	int status = 0;
	int selected = 0;

	// Get the selected item
	windowComponentGetSelected(iconList, &selected);
	if (selected < 0)
		return (status = selected);

	// Re-scan disk info
	status = diskGet(disks[selected].name, &disks[selected]);
	if (status < 0)
		error("%s", _("Error re-reading disk info"));

	return (selected);
}


static int getMountPoint(const char *diskName, char *mountPoint)
{
	// Given a disk name, see if we can't find the mount point.

	int status = 0;
	char variable[MAX_VARIABLE_LEN];
	char tmpMountPoint[MAX_VARIABLE_LEN];

	snprintf(variable, MAX_VARIABLE_LEN, "%s.mountpoint", diskName);

	// Try getting the mount point from the configuration file
	status = configGet(DISK_MOUNT_CONFIG, variable, tmpMountPoint,
		MAX_VARIABLE_LEN);
	if (status < 0)
		return (status);

	if (tmpMountPoint[0] == '\0')
		return (status = ERR_NODATA);

	strncpy(mountPoint, tmpMountPoint, MAX_VARIABLE_LEN);

	return (status = 0);
}


static void setMountPoint(const char *diskName, char *mountPoint)
{
	// Given a disk name, try to set the mount point in the mount configuration
	// file.

	int status = 0;
	variableList mountConfig;
	disk theDisk;
	char variable[MAX_VARIABLE_LEN];

	// Don't attempt to set the mount point if the config file's filesystem
	// is read-only
	if (!fileGetDisk(DISK_MOUNT_CONFIG, &theDisk) && theDisk.readOnly)
		return;

	// Try reading the mount configuration file
	status = configRead(DISK_MOUNT_CONFIG, &mountConfig);
	if (status < 0)
		return;

	// Set the mount point
	snprintf(variable, MAX_VARIABLE_LEN, "%s.mountpoint", diskName);
	status = variableListSet(&mountConfig, variable, mountPoint);
	if (status < 0)
	{
		variableListDestroy(&mountConfig);
		return;
	}

	// Set automount to off
	snprintf(variable, MAX_VARIABLE_LEN, "%s.automount", diskName);
	status = variableListSet(&mountConfig, variable, "no");
	if (status < 0)
	{
		variableListDestroy(&mountConfig);
		return;
	}

	configWrite(DISK_MOUNT_CONFIG, &mountConfig);
	variableListDestroy(&mountConfig);
	return;
}


static int mount(disk *theDisk, const char *mountPoint)
{
	int status = 0;

	status = filesystemMount(theDisk->name, mountPoint);
	if (status < 0)
	{
		if (status == ERR_NOSUCHFUNCTION)
			error(_("Filesystem on %s is not supported for browsing"),
				theDisk->name);
		else
			error(_("Can't mount %s on %s"), theDisk->name, mountPoint);
		return (status);
	}

	// Try to re-read disk info
	diskGet(theDisk->name, theDisk);

	return (status = 0);
}


static void browseThread(void)
{
	// The user has clicked on a disk, and wants to browse it.  This thread
	// will mount the filesystem, if necessary, and launch a file browser
	// program for it.

	int status = 0;
	int selected = -1;
	disk theDisk;
	char *command = NULL;

	windowSwitchPointer(window, MOUSE_POINTER_BUSY);

	selected = getAndScanSelection();
	if (selected < 0)
		goto out;

	// Make a private copy of the selected disk structure.  The main program
	// keeps scanning, and can clobber the list when something changes (for
	// example, when we mount it)
	memcpy(&theDisk, &disks[selected], sizeof(disk));

	// See whether we have to mount the corresponding disk, and launch
	// an appropriate file browser

	// Disk mounted?
	if (!theDisk.mounted)
	{
		// If it's removable, see if there is any media present
		if ((theDisk.type & DISKTYPE_REMOVABLE) &&
			!diskMediaPresent(theDisk.name))
		{
			error(_("No media in disk %s"), theDisk.name);
			status = ERR_INVALID;
			goto out;
		}

		if (getMountPoint(theDisk.name, theDisk.mountPoint) < 0)
			snprintf(theDisk.mountPoint, MAX_PATH_LENGTH, "/%s", theDisk.name);

		status = mount(&theDisk, theDisk.mountPoint);
		if (status < 0)
			goto out;
	}

	command = calloc(MAXSTRINGLENGTH, 1);
	if (!command)
	{
		status = ERR_MEMORY;
		goto out;
	}

	// Launch a file browser
	snprintf(command, MAXSTRINGLENGTH, "%s %s", FILE_BROWSER,
		theDisk.mountPoint);

	// Exec the file browser command, no block
	status = loaderLoadAndExec(command, privilege, 0);

	free(command);

	if (status < 0)
	{
		error("%s", _("Error launching file browser"));
		goto out;
	}

	status = 0;

out:
	windowSwitchPointer(window, MOUSE_POINTER_DEFAULT);
	multitaskerTerminate(status);
}


static void mountAsThread(void)
{
	// The user has right-clicked on a disk, and wants to specify a mount point
	// and browse it.  This thread will query for the mount point, mount the
	// filesystem, if necessary, and launch a file browser program for it.

	int status = 0;
	int selected = -1;
	disk theDisk;
	int saveConfiguration = 0;
	char *command = NULL;

	selected = getAndScanSelection();
	if (selected < 0)
		goto out;

	// Make a private copy of the selected disk structure.  The main program
	// keeps scanning, and can clobber the list when something changes (for
	// example, when we mount it)
	memcpy(&theDisk, &disks[selected], sizeof(disk));

	// See whether we have to mount the corresponding disk, and launch
	// an appropriate file browser

	// Disk mounted?
	if (theDisk.mounted)
	{
		error(_("Disk is already mounted as %s"), theDisk.mountPoint);
		goto out;
	}

	windowSwitchPointer(window, MOUSE_POINTER_BUSY);

	// If it's removable, see if there is any media present
	if ((theDisk.type & DISKTYPE_REMOVABLE) &&
		!diskMediaPresent(theDisk.name))
	{
		error(_("No media in disk %s"), theDisk.name);
		status = ERR_INVALID;
		goto out;
	}

	windowSwitchPointer(window, MOUSE_POINTER_DEFAULT);

	// See if there's a mount point specified in the mount configuration
	if (getMountPoint(theDisk.name, theDisk.mountPoint) < 0)
		saveConfiguration = 1;

	// Ask the user for the mount point
	status = windowNewPromptDialog(window, _("Mount point"),
		_("Please enter the mount point"), 1, 40, theDisk.mountPoint);
	if (status <= 0)
		goto out;

	windowSwitchPointer(window, MOUSE_POINTER_BUSY);

	status = mount(&theDisk, theDisk.mountPoint);
	if (status < 0)
		goto out;

	command = calloc(MAXSTRINGLENGTH, 1);
	if (!command)
	{
		status = ERR_MEMORY;
		goto out;
	}

	// Launch a file browser
	snprintf(command, MAXSTRINGLENGTH, "%s %s", FILE_BROWSER,
		theDisk.mountPoint);

	// Exec the file browser command, no block
	status = loaderLoadAndExec(command, privilege, 0);

	free(command);

	if (status < 0)
	{
		error("%s", _("Error launching file browser"));
		goto out;
	}

	if (saveConfiguration)
		// Try to save the specified mount point in the mount configuration
		setMountPoint(theDisk.name, theDisk.mountPoint);

	status = 0;

out:
	windowSwitchPointer(window, MOUSE_POINTER_DEFAULT);
	multitaskerTerminate(status);
}


static void unmountThread(void)
{
	int status = 0;
	int selected = -1;

	selected = getAndScanSelection();
	if (selected < 0)
		goto out;

	// Disk mounted?
	if (!disks[selected].mounted)
	{
		error(_("Disk %s is not mounted"), disks[selected].name);
		goto out;
	}

	windowSwitchPointer(window, MOUSE_POINTER_BUSY);

	// Unmount it
	status = filesystemUnmount(disks[selected].mountPoint);
	if (status < 0)
		error(_("Error unmounting %s"), disks[selected].name);

out:
	windowSwitchPointer(window, MOUSE_POINTER_DEFAULT);
	multitaskerTerminate(status);
}


static void propertiesThread(void)
{
	int status = 0;
	int selected = -1;
	char *buff = NULL;

	selected = getAndScanSelection();
	if (selected < 0)
		goto out;

	buff = malloc(1024);
	if (!buff)
	{
		status = ERR_MEMORY;
		goto out;
	}

	if (disks[selected].type & DISKTYPE_FIXED)
		strcat(buff, _("Fixed "));
	else if (disks[selected].type & DISKTYPE_REMOVABLE)
		strcat(buff, _("Removable "));

	if (disks[selected].type & DISKTYPE_FLOPPY)
		strcat(buff, _("floppy\n"));
	else if (disks[selected].type & DISKTYPE_CDROM)
		strcat(buff, _("CD-ROM\n"));
	else if (disks[selected].type & DISKTYPE_FLASHDISK)
		strcat(buff, _("flash disk\n"));
	else if (disks[selected].type & DISKTYPE_HARDDISK)
		strcat(buff, _("hard disk\n"));

	sprintf((buff + strlen(buff)), _("%u cylinders, %u heads, %u sectors\n"),
		disks[selected].cylinders, disks[selected].heads,
		disks[selected].sectorsPerCylinder);

	sprintf((buff + strlen(buff)), _("Size: %llu MB\n"),
		(disks[selected].numSectors /
		(1048576 / disks[selected].sectorSize)));

	sprintf((buff + strlen(buff)), _("Partition type: %s\n"),
		disks[selected].partType);

	sprintf((buff + strlen(buff)), _("Filesystem type: %s\n"),
		disks[selected].fsType);

	if (disks[selected].mounted)
		sprintf((buff + strlen(buff)), _("Mounted as %s%s\n"),
		disks[selected].mountPoint,
		(disks[selected].readOnly? _(" (read only)") : ""));

	windowNewInfoDialog(window, _("Properties"), buff);
	status = 0;

out:
	multitaskerTerminate(status);
}


static void initMenuContents(windowMenuContents *contents)
{
	int count;

	for (count = 0; count < contents->numItems; count ++)
	{
		strncpy(contents->items[count].text, _(contents->items[count].text),
			WINDOW_MAX_LABEL_LENGTH);
		contents->items[count].text[WINDOW_MAX_LABEL_LENGTH - 1] = '\0';
	}
}


static void refreshMenuContents(void)
{
	int count;

	initMenuContents(&diskMenuContents);

	for (count = 0; count < diskMenuContents.numItems; count ++)
	{
		windowComponentSetData(diskMenuContents.items[count].key,
			diskMenuContents.items[count].text,
			strlen(diskMenuContents.items[count].text),
			(count == (diskMenuContents.numItems - 1)));
	}
}


static void refreshWindow(void)
{
	// We got a 'window refresh' event (probably because of a language
	// switch), so we need to update things

	// Re-get the language setting
	setlocale(LC_ALL, getenv(ENV_LANG));
	textdomain("computer");

	// Re-get the character set
	if (getenv(ENV_CHARSET))
		windowSetCharSet(window, getenv(ENV_CHARSET));

	// Refresh the menu contents
	refreshMenuContents();

	// Refresh the window title
	windowSetTitle(window, WINDOW_TITLE);
}


static void eventHandler(objectKey key, windowEvent *event)
{
	int selectedIndex = -1;

	// Check for window events.
	if (key == window)
	{
		// Check for window refresh
		if (event->type == EVENT_WINDOW_REFRESH)
			refreshWindow();

		// Check for the window being closed
		else if (event->type == EVENT_WINDOW_CLOSE)
			stop = 1;
	}

	// Check for disk menu events.

	else if (key == diskMenuContents.items[DISKMENU_BROWSE].key)
	{
		if (event->type & EVENT_SELECTION)
		{
			// Launch the 'browse' thread
			if (multitaskerSpawn(&browseThread, "browse thread", 0, NULL) < 0)
			{
				error("%s", _("Error spawning browser thread"));
			}
		}
	}

	else if (key == diskMenuContents.items[DISKMENU_MOUNTAS].key)
	{
		if (event->type & EVENT_SELECTION)
		{
			// Launch the "mount as" thread
			if (multitaskerSpawn(&mountAsThread, "mount as thread", 0,
				NULL) < 0)
			{
				error("%s", _("Error spawning mount as thread"));
			}
		}
	}

	else if (key == diskMenuContents.items[DISKMENU_UNMOUNT].key)
	{
		if (event->type & EVENT_SELECTION)
		{
			// Launch the 'unmount' thread
			if (multitaskerSpawn(&unmountThread, "unmount thread", 0, NULL) < 0)
			{
				error("%s", _("Error spawning unmount thread"));
			}
		}
	}

	else if (key == diskMenuContents.items[DISKMENU_PROPERTIES].key)
	{
		if (event->type & EVENT_SELECTION)
		{
			// Launch the 'properties' thread
			if (multitaskerSpawn(&propertiesThread, "properties thread",
				0, NULL) < 0)
			{
				error("%s", _("Error spawning properties thread"));
			}
		}
	}

	// Check for events in our icon list.
	else if (key == iconList)
	{
		if (event->type & EVENT_SELECTION)
		{
			// We consider the icon 'clicked' if it is a mouse left-up,
			// or an ENTER key press
			if ((event->type & EVENT_MOUSE_LEFTUP) ||
				((event->type & EVENT_KEY_DOWN) && (event->key == keyEnter)))
			{
				// Launch the browse thread
				if (multitaskerSpawn(&browseThread, "browse thread", 0,
					NULL) < 0)
				{
					error("%s", _("Error spawning browser thread"));
				}
			}
			else
			{
				// The icon was just selected.  Update our context menu based
				// on the particular selection.
				selectedIndex = getAndScanSelection();
				if (selectedIndex >= 0)
				{
					windowComponentSetEnabled(
						diskMenuContents.items[DISKMENU_MOUNTAS].key,
						(disks[selectedIndex].mounted? 0 : 1));
					windowComponentSetEnabled(
						diskMenuContents.items[DISKMENU_UNMOUNT].key,
						(disks[selectedIndex].mounted? 1 : 0));
				}
			}
		}
	}
}


static void setContextMenus(void)
{
	objectKey *listComponents = NULL;
	int count;

	// Try to set a the context menu for each icon
	listComponents = malloc(numDisks * sizeof(objectKey));
	if (listComponents)
	{
		if (windowComponentGetData(iconList, listComponents,
			(numDisks * sizeof(objectKey))) >= 0)
		{
			for (count = 0; count < numDisks; count ++)
				windowContextSet(listComponents[count], diskMenu);
		}

		free(listComponents);
	}
}


static int scanComputer(void)
{
	// This gets the list of disks and/or other hardware we're interested in
	// and creates icon parameters for them

	int status = 0;
	int newNumDisks = 0;
	int newDisksSize = 0;
	disk *newDisks = NULL;
	listItemParameters *newIconParams = NULL;
	int count1, count2;

	// Call the kernel to give us the number of available disks
	newNumDisks = diskGetCount();
	if (newNumDisks <= 0)
		return (status = ERR_NOSUCHENTRY);

	newDisksSize = (newNumDisks * sizeof(disk));

	newDisks = malloc(newDisksSize);
	newIconParams = malloc(newNumDisks * sizeof(listItemParameters));
	if (!newDisks || !newIconParams)
	{
		error("%s", _("Memory allocation error"));
		return (status = ERR_MEMORY);
	}

	// Read disk info
	status = diskGetAll(newDisks, newDisksSize);
	if (status < 0)
		// Eek.  Problem getting disk info
		return (status);

	// Any change?
	if (!disks || (newNumDisks != numDisks) ||
		memcmp(disks, newDisks, newDisksSize))
	{
		windowSwitchPointer(window, MOUSE_POINTER_BUSY);

		// Get the icon image and label for each disk.
		for (count1 = 0; count1 < newNumDisks; count1 ++)
		{
			for (count2 = 0; icons[count2].fileName; count2 ++)
			{
				if (newDisks[count1].type & icons[count2].diskType)
				{
					memcpy(&newIconParams[count1].iconImage,
						icons[count2].iconImage, sizeof(image));
					break;
				}
			}

			// Copy the disk name, but add the filesystem label, if available.
			if (newDisks[count1].label[0])
			{
				// Don't let the label be too long
				if (strlen(newDisks[count1].label) > 20)
					strcpy((newDisks[count1].label + 17), "...");

				snprintf(newIconParams[count1].text, WINDOW_MAX_LABEL_LENGTH,
					"%s (%s)", newDisks[count1].name, newDisks[count1].label);
			}
			else
			{
				strncpy(newIconParams[count1].text, newDisks[count1].name,
					WINDOW_MAX_LABEL_LENGTH);
			}
		}

		deallocateMemory();

		numDisks = newNumDisks;
		disks = newDisks;
		iconParams = newIconParams;

		if (iconList)
		{
			windowComponentSetData(iconList, iconParams, numDisks,
				1 /* redraw */);
			setContextMenus();
		}

		windowSwitchPointer(window, MOUSE_POINTER_DEFAULT);
	}
	else
	{
		free(newDisks);
		free(newIconParams);
	}

	return (status = 0);
}


static void handleMenuEvents(windowMenuContents *contents)
{
	int count;

	for (count = 0; count < contents->numItems; count ++)
		windowRegisterEventHandler(contents->items[count].key, &eventHandler);
}


static int constructWindow(void)
{
	int status = 0;
	componentParameters params;

	// Create a new window, with small, arbitrary size and location
	window = windowNew(processId, WINDOW_TITLE);
	if (!window)
		return (status = ERR_NOTINITIALIZED);

	memset(&params, 0, sizeof(componentParameters));

	// Create a context menu for disks
	initMenuContents(&diskMenuContents);
	diskMenu = windowNewMenu(window, NULL, _("Disk"), &diskMenuContents,
		&params);
	handleMenuEvents(&diskMenuContents);

	params.gridWidth = 1;
	params.gridHeight = 1;
	params.padTop = 5;
	params.padBottom = 5;
	params.padLeft = 5;
	params.padRight = 5;
	params.orientationX = orient_center;
	params.orientationY = orient_middle;

	// Create a window list to hold the icons
	iconList = windowNewList(window, windowlist_icononly, DEFAULT_ROWS,
		DEFAULT_COLUMNS, 0, iconParams, numDisks, &params);
	windowRegisterEventHandler(iconList, &eventHandler);
	setContextMenus();
	windowComponentFocus(iconList);

	// Register an event handler to catch window close events
	windowRegisterEventHandler(window, &eventHandler);

	windowSetVisible(window, 1);

	return (status = 0);
}


int main(int argc, char *argv[])
{
	int status = 0;
	int guiThreadPid = 0;
	int seconds = 0;
	int count;

	setlocale(LC_ALL, getenv(ENV_LANG));
	textdomain("computer");

	// Only work in graphics mode
	if (!graphicsAreEnabled())
	{
		fprintf(stderr, _("\nThe \"%s\" command only works in graphics "
			"mode\n"), (argc? argv[0] : ""));
		return (errno = ERR_NOTINITIALIZED);
	}

	// What is my process id?
	processId = multitaskerGetCurrentProcessId();

	// What is my privilege level?
	privilege = multitaskerGetProcessPrivilege(processId);

	// Load our icon images
	for (count = 0; icons[count].fileName; count ++)
	{
		icons[count].iconImage = malloc(sizeof(image));
		if (!icons[count].iconImage)
		{
			status = ERR_MEMORY;
			goto out;
		}

		status = imageLoad(icons[count].fileName, 64, 64,
			icons[count].iconImage);
		if (status < 0)
		{
			error(_("Can't load icon image %s"), icons[count].fileName);
			free(icons[count].iconImage);
			icons[count].iconImage = NULL;
		}
	}

	// Get information about all the disks, and make icon parameters for them.
	status = scanComputer();
	if (status < 0)
		goto out;

	status = constructWindow();
	if (status < 0)
		goto out;

	// Run the GUI as a separate thread
	guiThreadPid = windowGuiThread();

	while (!stop && multitaskerProcessIsAlive(guiThreadPid))
	{
		// Wait about 1 second between updates
		if (rtcReadSeconds() != seconds)
		{
			scanComputer();
			seconds = rtcReadSeconds();
		}

		multitaskerYield();
	}

	// We're back.
	windowGuiStop();
	windowDestroy(window);

	status = 0;

out:
	// Deallocate memory
	deallocateMemory();

	for (count = 0; icons[count].fileName; count ++)
	{
		if (icons[count].iconImage)
		{
			if (icons[count].iconImage->data)
				imageFree(icons[count].iconImage);
			free(icons[count].iconImage);
		}
	}

	return (status);
}

