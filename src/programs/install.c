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
//  install.c
//

// This is a program for installing the system on a target disk (filesystem).

/* This is the text that appears when a user requests help about this program
<help>

 -- install --

This program will install a copy of Visopsys on another disk.

Usage:
  install [-T] [disk_name]

The 'install' program is interactive, but a logical disk parameter can
(optionally) be specified on the command line.  If no disk is specified,
then the user will be prompted to choose from a menu.  Use the 'disks'
command to list the available disks.

Options:
-T  : Force text mode operation

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
#include <sys/ascii.h>
#include <sys/env.h>
#include <sys/kernconf.h>
#include <sys/lang.h>
#include <sys/paths.h>
#include <sys/user.h>
#include <sys/vsh.h>

#define _(string) gettext(string)
#define gettext_noop(string) (string)

#define WINDOW_TITLE		_("Install")
#define TITLE_STRING		_("Visopsys Installer\nCopyright (C) 1998-2018 " \
	"J. Andrew McLaughlin")
#define INSTALL_DISK		_("[ Installing on disk %s ]")
#define BASIC_INSTALL		_("Basic install")
#define FULL_INSTALL		_("Full install")
#define FORMAT_DISK			_("Format %s (erases all data!)")
#define CHOOSE_FILESYSTEM	_("Choose filesystem type")
#define LANGUAGE			_("Language")
#define INSTALL				_("Install")
#define QUIT				_("Quit")
#define MOUNTPOINT			"/tmp_install"
#define BASICINSTALL		PATH_SYSTEM "/install-files.basic"
#define FULLINSTALL			PATH_SYSTEM "/install-files.full"

typedef enum { install_basic, install_full } install_type;

static int processId = 0;
static char rootDisk[DISK_MAX_NAMELENGTH];
static int numberDisks = 0;
static disk diskInfo[DISK_MAXDEVICES];
static char *diskName = NULL;
static char *chooseVolumeString =
	gettext_noop("Please choose the volume on which to install:");
static char *setPasswordString =
	gettext_noop("Please choose a password for the 'admin' account");
static char *partitionString = gettext_noop("Partition disks...");
static char *cancelString = gettext_noop("Installation cancelled.");
static install_type installType;
static unsigned bytesToCopy = 0;
static unsigned bytesCopied = 0;
static progress prog;
static int doFormat = 1;
static int chooseFsType = 0;
static char formatFsType[16];
static char installLanguage[6];
static textScreen screen;

// GUI stuff
static int graphics = 0;
static objectKey window = NULL;
static objectKey titleLabel = NULL;
static objectKey installDiskLabel = NULL;
static objectKey installTypeRadio = NULL;
static objectKey formatCheckbox = NULL;
static objectKey fsTypeCheckbox = NULL;
static objectKey langImage = NULL;
static objectKey langButton = NULL;
static objectKey statusLabel = NULL;
static objectKey progressBar = NULL;
static objectKey installButton = NULL;
static objectKey quitButton = NULL;
static image flagImage;


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

	va_start(list, format);
	vsnprintf(output, MAXSTRINGLENGTH, format, list);
	va_end(list);

	if (graphics)
		windowNewErrorDialog(window, _("Error"), output);
	else
		printf(_("\n\nERROR: %s\n\n"), output);
}


__attribute__((format(printf, 2, 3))) __attribute__((noreturn))
static void quit(int status, const char *message, ...)
{
	// Shut everything down

	va_list list;
	char output[MAXSTRINGLENGTH];

	if (message)
	{
		va_start(list, message);
		vsnprintf(output, MAXSTRINGLENGTH, message, list);
		va_end(list);
	}

	if (graphics)
		windowGuiStop();
	else
		textScreenRestore(&screen);

	if (message)
	{
		if (status < 0)
		{
			error(_("%s  Quitting."), output);
		}
		else
		{
			if (graphics)
				windowNewInfoDialog(window, _("Complete"), output);
			else
				printf("\n%s\n", output);
		}
	}

	if (graphics && window)
		windowDestroy(window);

	errno = status;

	if (screen.data)
		memoryRelease(screen.data);

	exit(status);
}


static void makeDiskList(void)
{
	// Make a list of disks on which we can install

	int status = 0;
	int tmpNumberDisks = diskGetCount();
	disk *tmpDiskInfo = NULL;
	int count;

	numberDisks = 0;
	memset(diskInfo, 0, (DISK_MAXDEVICES * sizeof(disk)));

	tmpDiskInfo = malloc(DISK_MAXDEVICES * sizeof(disk));
	if (!tmpDiskInfo)
		quit(status, "%s", _("Memory allocation error."));

	status = diskGetAll(tmpDiskInfo, (DISK_MAXDEVICES * sizeof(disk)));
	if (status < 0)
		// Eek.  Problem getting disk info
		quit(status, "%s", _("Unable to get disk information."));

	// Loop through the list we got.  Copy any valid disks (disks to which
	// we might be able to install) into the main array
	for (count = 0; count < tmpNumberDisks; count ++)
	{
		// Make sure it's not the root disk; that would be pointless and
		// possibly dangerous
		if (!strcmp(rootDisk, tmpDiskInfo[count].name))
			continue;

		// Skip CD-ROMS
		if (tmpDiskInfo[count].type & DISKTYPE_CDROM)
			continue;

		// Otherwise, we will put this in the list
		memcpy(&diskInfo[numberDisks++], &tmpDiskInfo[count], sizeof(disk));
	}

	free(tmpDiskInfo);
}


static int loadFlagImage(const char *lang, image *img)
{
	int status = 0;
	char path[MAX_PATH_LENGTH];

	sprintf(path, "%s/flag-%s.bmp", PATH_SYSTEM_LOCALE, lang);

	status = fileFind(path, NULL);
	if (status < 0)
		return (status);

	return (status = imageLoad(path, 30, 20, img));
}


static void chooseLanguage(void)
{
	char pickedLanguage[sizeof(installLanguage)];

	if (windowNewLanguageDialog(window, pickedLanguage) >= 0)
	{
		strncpy(installLanguage, pickedLanguage,
			(sizeof(installLanguage - 1)));

		if (flagImage.data)
			imageFree(&flagImage);

		if (loadFlagImage(installLanguage, &flagImage) >= 0)
			windowComponentSetData(langImage, &flagImage, sizeof(image),
				1 /* redraw */);
	}
}


static void eventHandler(objectKey key, windowEvent *event)
{

	// Check for window events.
	if (key == window)
	{
		// Check for the window being closed
		if (event->type == EVENT_WINDOW_CLOSE)
			quit(0, NULL);
	}

	else if ((key == formatCheckbox) && (event->type & EVENT_SELECTION))
	{
		windowComponentGetSelected(formatCheckbox, &doFormat);
		if (!doFormat)
			windowComponentSetSelected(fsTypeCheckbox, 0);
		windowComponentSetEnabled(fsTypeCheckbox, doFormat);
	}

	else if ((key == fsTypeCheckbox) && (event->type & EVENT_SELECTION))
	{
		windowComponentGetSelected(fsTypeCheckbox, &chooseFsType);
	}

	// Check the the 'language' button
	else if ((key == langButton) && (event->type == EVENT_MOUSE_LEFTUP))
	{
		chooseLanguage();
	}

	// Check for the 'install' button
	else if ((key == installButton) && (event->type == EVENT_MOUSE_LEFTUP))
	{
		// Stop the GUI here and the installation will commence
		windowGuiStop();
	}

	// Check for the 'quit' button
	else if ((key == quitButton) && (event->type == EVENT_MOUSE_LEFTUP))
	{
		quit(0, NULL);
	}
}


static void constructWindow(void)
{
	// If we are in graphics mode, make a window rather than operating on the
	// command line.

	objectKey container1 = NULL;
	file langDir;
	componentParameters params;
	char tmp[40];

	// Create a new window
	window = windowNew(processId, WINDOW_TITLE);
	if (!window)
		quit(ERR_NOCREATE, "%s", _("Can't create window!"));

	memset(&params, 0, sizeof(componentParameters));
	params.gridWidth = 2;
	params.gridHeight = 1;
	params.padTop = params.padLeft = params.padRight = 5;
	params.orientationX = orient_left;
	params.orientationY = orient_middle;
	titleLabel = windowNewTextLabel(window, TITLE_STRING, &params);

	params.gridY++;
	sprintf(tmp, INSTALL_DISK, diskName);
	installDiskLabel = windowNewTextLabel(window, tmp, &params);

	params.gridY++;
	installTypeRadio = windowNewRadioButton(window, 2, 1, (char *[])
		{ BASIC_INSTALL, FULL_INSTALL }, 2 , &params);
	windowComponentSetEnabled(installTypeRadio, 0);

	params.gridY++;
	sprintf(tmp, FORMAT_DISK, diskName);
	formatCheckbox = windowNewCheckbox(window, tmp, &params);
	windowComponentSetSelected(formatCheckbox, 1);
	windowComponentSetEnabled(formatCheckbox, 0);
	windowRegisterEventHandler(formatCheckbox, &eventHandler);

	params.gridY++;
	fsTypeCheckbox = windowNewCheckbox(window, CHOOSE_FILESYSTEM, &params);
	windowComponentSetEnabled(fsTypeCheckbox, 0);
	windowRegisterEventHandler(fsTypeCheckbox, &eventHandler);

	params.gridY++;
	container1 = windowNewContainer(window, "container1", &params);
	if (container1)
	{
		params.gridWidth = 1;
		params.flags |= WINDOW_COMPFLAG_FIXEDWIDTH;
		if (loadFlagImage(installLanguage, &flagImage) >= 0)
		{
			langImage = windowNewImage(container1, &flagImage, draw_normal,
				&params);
		}

		params.gridX++;
		langButton = windowNewButton(container1, LANGUAGE, NULL, &params);
		windowRegisterEventHandler(langButton, &eventHandler);

		if (fileFind(PATH_SYSTEM_LOCALE, &langDir) < 0)
		{
			if (langImage)
				windowComponentSetEnabled(langImage, 0);

			windowComponentSetEnabled(langButton, 0);
		}
	}

	params.gridX = 0;
	params.gridY++;
	params.gridWidth = 2;
	params.flags &= ~WINDOW_COMPFLAG_FIXEDWIDTH;
	statusLabel = windowNewTextLabel(window, "", &params);
	windowComponentSetWidth(statusLabel, windowComponentGetWidth(titleLabel));

	params.gridY++;
	params.orientationX = orient_center;
	progressBar = windowNewProgressBar(window, &params);

	params.gridY++;
	params.gridWidth = 1;
	params.padBottom = 5;
	params.orientationX = orient_right;
	params.flags |= WINDOW_COMPFLAG_FIXEDWIDTH;
	installButton = windowNewButton(window, INSTALL, NULL, &params);
	windowRegisterEventHandler(installButton, &eventHandler);
	windowComponentSetEnabled(installButton, 0);

	params.gridX++;
	params.orientationX = orient_left;
	quitButton = windowNewButton(window, QUIT, NULL, &params);
	windowRegisterEventHandler(quitButton, &eventHandler);
	windowComponentSetEnabled(quitButton, 0);

	// Register an event handler to catch window close events
	windowRegisterEventHandler(window, &eventHandler);

	// Go
	windowSetVisible(window, 1);
}


static void printBanner(void)
{
	// Print a message
	textScreenClear();
	printf("\n%s\n\n", _(TITLE_STRING));
}


static int yesOrNo(char *question)
{
	char character;

	if (graphics)
	{
		return (windowNewQueryDialog(window, _("Confirmation"), question));
	}

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


static int chooseDisk(void)
{
	// This is where the user chooses the disk on which to install

	int status = 0;
	int diskNumber = -1;
	objectKey chooseWindow = NULL;
	componentParameters params;
	objectKey diskList = NULL;
	objectKey okButton = NULL;
	objectKey partButton = NULL;
	objectKey cancelButton = NULL;
	listItemParameters diskListParams[DISK_MAXDEVICES];
	char *diskStrings[DISK_MAXDEVICES];
	windowEvent event;
	int count;

	// We jump back to this position if the user repartitions the disks
start:

	memset(&params, 0, sizeof(componentParameters));
	params.gridX = 0;
	params.gridY = 0;
	params.gridWidth = 3;
	params.gridHeight = 1;
	params.padTop = 5;
	params.padLeft = 5;
	params.padRight = 5;
	params.orientationX = orient_center;
	params.orientationY = orient_middle;

	memset(diskListParams, 0, (numberDisks * sizeof(listItemParameters)));
	for (count = 0; count < numberDisks; count ++)
	{
		snprintf(diskListParams[count].text, WINDOW_MAX_LABEL_LENGTH,
			"%s  [ %s ]", diskInfo[count].name, diskInfo[count].partType);
	}

	if (graphics)
	{
		chooseWindow = windowNew(processId, _("Choose Installation Disk"));
		windowNewTextLabel(chooseWindow, _(chooseVolumeString), &params);

		// Make a window list with all the disk choices
		params.gridY = 1;
		diskList = windowNewList(chooseWindow, windowlist_textonly, 5, 1, 0,
			diskListParams, numberDisks, &params);
		windowComponentFocus(diskList);

		// Make 'OK', 'partition', and 'cancel' buttons
		params.gridY = 2;
		params.gridWidth = 1;
		params.padBottom = 5;
		params.padRight = 0;
		params.orientationX = orient_right;
		okButton = windowNewButton(chooseWindow, _("OK"), NULL, &params);

		params.gridX = 1;
		params.padRight = 5;
		params.orientationX = orient_center;
		partButton = windowNewButton(chooseWindow, _(partitionString), NULL,
			&params);

		params.gridX = 2;
		params.padLeft = 0;
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

			// Check for our 'partition' button
			status = windowComponentEventGet(partButton, &event);
			if ((status < 0) || ((status > 0) &&
				(event.type == EVENT_MOUSE_LEFTUP)))
			{
				// The user wants to repartition the disks.  Get rid of this
				// window, run the disk manager, and start again
				windowDestroy(chooseWindow);
				chooseWindow = NULL;

				// Privilege zero, no args, block
				loaderLoadAndExec(PATH_PROGRAMS "/fdisk", 0, 1);

				// Remake our disk list
				makeDiskList();

				// Start again.
				goto start;
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

		diskStrings[numberDisks] = _(partitionString);

		diskNumber = vshCursorMenu(_(chooseVolumeString), diskStrings,
			(numberDisks + 1), 10 /* max rows */, 0 /* selected */);

		if (diskNumber == numberDisks)
		{
			// The user wants to repartition the disks.  Run the disk
			// manager, and start again

			// Privilege zero, no args, block
			loaderLoadAndExec(PATH_PROGRAMS "/fdisk", 0, 1);

			// Remake our disk list
			makeDiskList();

			// Start again.
			printBanner();
			goto start;
		}
	}

	return (diskNumber);
}


static unsigned getInstallSize(const char *installFileName)
{
	// Given the name of an install file, calculate the number of bytes
	// of disk space the installation will require.

	#define BUFFSIZE 160

	int status = 0;
	fileStream installFile;
	file theFile;
	char buffer[BUFFSIZE];
	unsigned bytes = 0;

	// Clear stack data
	memset(&installFile, 0, sizeof(fileStream));
	memset(&theFile, 0, sizeof(file));
	memset(buffer, 0, BUFFSIZE);

	// See if the install file exists
	status = fileFind(installFileName, NULL);
	if (status < 0)
		// Doesn't exist
		return (bytes = 0);

	// Open the install file
	status = fileStreamOpen(installFileName, OPENMODE_READ, &installFile);
	if (status < 0)
		// Can't open the install file.
		return (bytes = 0);

	// Read it line by line
	while (1)
	{
		status = fileStreamReadLine(&installFile, BUFFSIZE, buffer);
		if (status < 0)
		{
			// End of file
			break;
		}
		else if (!status || (buffer[0] == '#'))
		{
			// Ignore blank lines and comments
			continue;
		}
		else
		{
			// Use the line of data as the name of a file.  We try to find the
			// file and add its size to the number of bytes
			status = fileFind(strtok(buffer, "="), &theFile);
			if (status < 0)
			{
				error(_("Can't open source file \"%s\""), buffer);
				continue;
			}

			bytes += theFile.size;
		}
	}

	fileStreamClose(&installFile);

	// Add 1K for a little buffer space
	return (bytes + 1024);
}


static int askFsType(void)
{
	// Query the user for the FAT filesystem type

	int status = 0;
	int selectedType = 0;
	char *fsTypes[] = { _("Default"), _("FAT12"), _("FAT16"), _("FAT32") };

	if (graphics)
	{
		selectedType = windowNewRadioDialog(window,
			_("Choose Filesystem Type"), _("Supported types:"), fsTypes, 4, 0);
	}
	else
	{
		selectedType = vshCursorMenu(_("Choose the filesystem type:"),
			fsTypes, 4, 0 /* no max rows */, 0 /* selected */);
	}

	if (selectedType < 0)
		return (status = selectedType);

	if (!strcasecmp(fsTypes[selectedType], _("Default")))
		strcpy(formatFsType, "fat");
	else
		strcpy(formatFsType, fsTypes[selectedType]);

	return (status = 0);
}


static void updateStatus(const char *message)
{
	// Updates progress messages.

	int statusLength = 0;

	if (lockGet(&prog.progLock) >= 0)
	{
		if (strlen((char *) prog.statusMessage) &&
			(prog.statusMessage[strlen((char *) prog.statusMessage) - 1] !=
				'\n'))
		{
			strcat((char *) prog.statusMessage, message);
		}
		else
		{
			strcpy((char *) prog.statusMessage, message);
		}

		statusLength = strlen((char *) prog.statusMessage);
		if (statusLength >= PROGRESS_MAX_MESSAGELEN)
		{
			statusLength = (PROGRESS_MAX_MESSAGELEN - 1);
			prog.statusMessage[statusLength] = '\0';
		}

		if (prog.statusMessage[statusLength - 1] == '\n')
			statusLength -= 1;

		if (graphics)
		{
			windowComponentSetData(statusLabel, (char *) prog.statusMessage,
				statusLength, 1 /* redraw */);
		}

		lockRelease(&prog.progLock);
	}
}


static int mountedCheck(disk *theDisk)
{
	// If the disk is mounted, ask if we can unmount it

	int status = 0;
	char tmpChar[160];

	if (!theDisk->mounted)
		return (status = 0);

	sprintf(tmpChar, _("The disk is mounted as %s.  It must be unmounted\n"
		"before continuing.  Unmount?"), theDisk->mountPoint);

	if (!yesOrNo(tmpChar))
		return (status = ERR_CANCELLED);

	// Try to unmount the filesystem
	status = filesystemUnmount(theDisk->mountPoint);
	if (status < 0)
	{
		error(_("Unable to unmount %s"), theDisk->mountPoint);
		return (status);
	}

	return (status = 0);
}


static int copyBootSector(disk *theDisk)
{
	// Overlay the boot sector from the root disk onto the boot sector of
	// the target disk

	int status = 0;
	char bootSectFilename[MAX_PATH_NAME_LENGTH];
	char command[MAX_PATH_NAME_LENGTH];

	updateStatus(_("Copying boot sector...  "));

	// Make sure we know the filesystem type
	status = diskGetFilesystemType(theDisk->name, theDisk->fsType,
		FSTYPE_MAX_NAMELENGTH);
	if (status < 0)
	{
		error(_("Unable to determine the filesystem type on disk \"%s\""),
			theDisk->name);
		return (status);
	}

	// Determine which boot sector we should be using
	if (strncmp(theDisk->fsType, "fat", 3))
	{
		error(_("Can't install a boot sector for filesystem type \"%s\""),
			theDisk->fsType);
		return (status = ERR_INVALID);
	}

	strcpy(bootSectFilename, PATH_SYSTEM_BOOT "/bootsect.fat");
	if (!strcasecmp(theDisk->fsType, "fat32"))
		strcat(bootSectFilename, "32");

	// Find the boot sector
	status = fileFind(bootSectFilename, NULL);
	if (status < 0)
	{
		error(_("Unable to find the boot sector file \"%s\""),
			bootSectFilename);
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

	updateStatus(_("Done\n"));

	return (status = 0);
}


static int copyFiles(const char *installFileName)
{
	#define BUFFSIZE 160

	int status = 0;
	fileStream installFile;
	file theFile;
	unsigned percent = 0;
	char buffer[BUFFSIZE];
	char *srcFile = NULL;
	char *destFile = NULL;
	char tmpFileName[128];

	// Clear stack data
	memset(&installFile, 0, sizeof(fileStream));
	memset(&theFile, 0, sizeof(file));
	memset(buffer, 0, BUFFSIZE);
	memset(tmpFileName, 0, 128);

	// Open the install file
	status = fileStreamOpen(installFileName, OPENMODE_READ, &installFile);
	if (status < 0)
	{
		error(_("Can't open install file \"%s\""),  installFileName);
		return (status);
	}

	sprintf(buffer, _("Copying %s files...  "),
		(!strcmp(installFileName, BASICINSTALL)? _("basic") : _("extra")));
	updateStatus(buffer);

	// Read it line by line
	while (1)
	{
		status = fileStreamReadLine(&installFile, BUFFSIZE, buffer);
		if (status < 0)
		{
			// End of file
			break;
		}
		else if (!status || (buffer[0] == '#'))
		{
			// Ignore blank lines and comments
			continue;
		}

		// Get the source filename, and the destination filename if it
		// follows (separated by an '=')

		srcFile = strtok(buffer, "=");
		destFile = strtok(NULL, "=");
		if (!destFile)
			destFile = srcFile;

		// Find the source file
		status = fileFind(srcFile, &theFile);
		if (status < 0)
		{
			// Later we should do something here to make a message listing
			// the names of any missing files
			error(_("Missing file \"%s\""), buffer);
			continue;
		}

		strcpy(tmpFileName, MOUNTPOINT);
		strcat(tmpFileName, destFile);

		if (theFile.type == dirT)
		{
			// It's a directory, create it in the desination
			if (fileFind(tmpFileName, NULL) < 0)
				status = fileMakeDir(tmpFileName);
		}
		else
		{
			// It's a file.  Copy it to the destination.
			status = fileCopy(srcFile, tmpFileName);
		}

		if (status < 0)
			goto done;

		bytesCopied += theFile.size;

		// Sync periodically
		if ((((bytesCopied * 100) / bytesToCopy) % 10) > (percent % 10))
			diskSyncAll();

		percent = ((bytesCopied * 100) / bytesToCopy);

		if (graphics)
		{
			windowComponentSetData(progressBar, (void *) percent, 1,
				1 /* redraw */);
		}
		else if (lockGet(&prog.progLock) >= 0)
		{
			prog.percentFinished = percent;
			lockRelease(&prog.progLock);
		}
	}

	status = 0;

done:
	fileStreamClose(&installFile);

	diskSyncAll();

	updateStatus(_("Done\n"));

	return (status);
}


static void setAdminPassword(void)
{
	// Show a 'set password' dialog box for the admin user

	int status = 0;
	objectKey dialogWindow = NULL;
	componentParameters params;
	objectKey passwordField1 = NULL;
	objectKey passwordField2 = NULL;
	objectKey noMatchLabel = NULL;
	objectKey okButton = NULL;
	objectKey cancelButton = NULL;
	windowEvent event;
	char confirmPassword[USER_MAX_PASSWDLENGTH + 1];
	char newPassword[USER_MAX_PASSWDLENGTH + 1];

	if (graphics)
	{
		// Create the dialog
		dialogWindow = windowNewDialog(window,
			_("Set Administrator Password"));

		memset(&params, 0, sizeof(componentParameters));
		params.gridWidth = 2;
		params.gridHeight = 1;
		params.padLeft = 5;
		params.padRight = 5;
		params.padTop = 5;
		params.orientationX = orient_center;
		params.orientationY = orient_middle;
		windowNewTextLabel(dialogWindow, _(setPasswordString), &params);

		params.gridY = 1;
		params.gridWidth = 1;
		params.padRight = 0;
		params.orientationX = orient_right;
		windowNewTextLabel(dialogWindow, _("New password:"), &params);

		params.gridX = 1;
		params.padRight = 5;
		params.orientationX = orient_left;
		passwordField1 = windowNewPasswordField(dialogWindow,
			(USER_MAX_PASSWDLENGTH + 1), &params);
		windowComponentFocus(passwordField1);

		params.gridX = 0;
		params.gridY = 2;
		params.padRight = 0;
		params.orientationX = orient_right;
		windowNewTextLabel(dialogWindow, _("Confirm password:"), &params);

		params.gridX = 1;
		params.orientationX = orient_left;
		params.padRight = 5;
		passwordField2 = windowNewPasswordField(dialogWindow,
			(USER_MAX_PASSWDLENGTH + 1), &params);

		params.gridX = 0;
		params.gridY = 3;
		params.gridWidth = 2;
		params.orientationX = orient_center;
		noMatchLabel = windowNewTextLabel(dialogWindow, _("Passwords do not "
			"match"), &params);
		windowComponentSetVisible(noMatchLabel, 0);

		// Create the OK button
		params.gridY = 4;
		params.gridWidth = 1;
		params.padBottom = 5;
		params.padRight = 0;
		params.orientationX = orient_right;
		params.flags |= WINDOW_COMPFLAG_FIXEDWIDTH;
		okButton = windowNewButton(dialogWindow, _("OK"), NULL, &params);

		// Create the Cancel button
		params.gridX = 1;
		params.padRight = 5;
		params.orientationX = orient_left;
		cancelButton = windowNewButton(dialogWindow, _("Cancel"), NULL,
			&params);

		windowCenterDialog(window, dialogWindow);
		windowSetVisible(dialogWindow, 1);

		graphicsRestart:
		while (1)
		{
			// Check for window close events
			status = windowComponentEventGet(dialogWindow, &event);
			if ((status < 0) || ((status > 0) &&
				(event.type == EVENT_WINDOW_CLOSE)))
			{
				error("%s", _("No password set.  It will be blank."));
				windowDestroy(dialogWindow);
				return;
			}

			// Check for the OK button
			status = windowComponentEventGet(okButton, &event);
			if ((status >= 0) && (event.type == EVENT_MOUSE_LEFTUP))
				break;

			// Check for the Cancel button
			status = windowComponentEventGet(cancelButton, &event);
			if ((status < 0) || ((status > 0) &&
				(event.type == EVENT_MOUSE_LEFTUP)))
			{
				error("%s", _("No password set.  It will be blank."));
				windowDestroy(dialogWindow);
				return;
			}

			if (((windowComponentEventGet(passwordField1, &event) > 0) &&
					(event.type == EVENT_KEY_DOWN)) ||
				((windowComponentEventGet(passwordField2, &event) > 0) &&
					(event.type == EVENT_KEY_DOWN)))
			{
				if (event.key == keyEnter)
				{
					break;
				}
				else
				{
					windowComponentGetData(passwordField1, newPassword,
						USER_MAX_PASSWDLENGTH);
					windowComponentGetData(passwordField2, confirmPassword,
						USER_MAX_PASSWDLENGTH);
					if (strncmp(newPassword, confirmPassword,
						USER_MAX_PASSWDLENGTH))
					{
						windowComponentSetVisible(noMatchLabel, 1);
						windowComponentSetEnabled(okButton, 0);
					}
					else
					{
						windowComponentSetVisible(noMatchLabel, 0);
						windowComponentSetEnabled(okButton, 1);
					}
				}
			}

			// Done
			multitaskerYield();
		}

		windowComponentGetData(passwordField1, newPassword,
			USER_MAX_PASSWDLENGTH);
		windowComponentGetData(passwordField2, confirmPassword,
			USER_MAX_PASSWDLENGTH);
	}
	else
	{
		textRestart:
		printf("\n%s\n", _(setPasswordString));

		// Turn keyboard echo off
		textInputSetEcho(0);

		vshPasswordPrompt(_("New password: "), newPassword);
		vshPasswordPrompt(_("Confirm password: "), confirmPassword);
	}

	// Make sure the new password and confirm passwords match
	if (strncmp(newPassword, confirmPassword, USER_MAX_PASSWDLENGTH))
	{
		error("%s", _("Passwords do not match"));
		if (graphics)
		{
			windowComponentSetData(passwordField1, "", 0, 1 /* redraw */);
			windowComponentSetData(passwordField2, "", 0, 1 /* redraw */);
			goto graphicsRestart;
		}
		else
		{
			goto textRestart;
		}
	}

	if (graphics)
		windowDestroy(dialogWindow);
	else
		printf("\n");

	// We have a password.  Copy the blank password file for the new system
	// password file
	status = fileCopy(MOUNTPOINT USER_PASSWORDFILE_BLANK,
		MOUNTPOINT USER_PASSWORDFILE);
	if (status < 0)
	{
		error("%s", _("Unable to create the password file"));
		return;
	}

	status = userFileSetPassword(MOUNTPOINT USER_PASSWORDFILE, USER_ADMIN, "",
		newPassword);
	if (status < 0)
	{
		error("%s", _("Unable to set the \"admin\" password"));
		return;
	}

	return;
}


int main(int argc, char *argv[])
{
	int status = 0;
	char opt;
	int diskNumber = -1;
	char tmpChar[80];
	unsigned diskSize = 0;
	unsigned basicInstallSize = 0xFFFFFFFF;
	unsigned fullInstallSize = 0xFFFFFFFF;
	const char *message = NULL;
	objectKey progressDialog = NULL;
	int selected = 0;
	int count;

	setlocale(LC_ALL, getenv(ENV_LANG));
	textdomain("install");

	memset((void *) &prog, 0, sizeof(progress));

	// Set English as the default install language, unless some language is
	// currently set
	strcpy(installLanguage, LANG_ENGLISH);
	if (getenv(ENV_LANG))
		strcpy(installLanguage, getenv(ENV_LANG));

	processId = multitaskerGetCurrentProcessId();

	// Are graphics enabled?
	graphics = graphicsAreEnabled();

	// Check options
	while (strchr("T?", (opt = getopt(argc, argv, "T"))))
	{
		switch (opt)
		{
			case 'T':
				// Force text mode
				graphics = 0;
				break;

			default:
				quit(ERR_INVALID, _("Unknown option '%c'"), optopt);
		}
	}

	// Check privilege level
	if (multitaskerGetProcessPrivilege(processId))
	{
		quit(ERR_PERMISSION, "%s", _("You must be a privileged user to use "
			"this command.\n(Try logging in as user \"admin\")."));
	}

	// Get the root disk
	status = diskGetBoot(rootDisk);
	if (status < 0)
		// Couldn't get the root disk name
		quit(status, "%s", _("Can't determine the root disk."));

	makeDiskList();

	if (!graphics)
	{
		textScreenSave(&screen);
		printBanner();
	}

	// The user can specify the disk name as an argument.  Try to see
	// whether they did so.
	if (argc > 1)
	{
		for (count = 0; count < numberDisks; count ++)
		{
			if (!strcmp(diskInfo[count].name, argv[argc - 1]))
			{
				diskNumber = count;
				break;
			}
		}
	}

	if (diskNumber < 0)
		// The user has not specified a disk number.  We need to display the
		// list of available disks and prompt them.
		diskNumber = chooseDisk();

	if (diskNumber < 0)
		quit(diskNumber, NULL);

	diskName = diskInfo[diskNumber].name;

	if (graphics)
		constructWindow();

	// Make sure the disk isn't mounted
	status = mountedCheck(&diskInfo[diskNumber]);
	if (status < 0)
		quit(0, "%s", _(cancelString));

	// Calculate the number of bytes that will be consumed by the various
	// types of install
	basicInstallSize = getInstallSize(BASICINSTALL);
	fullInstallSize = getInstallSize(FULLINSTALL);

	// How much space is available on the raw disk?
	diskSize = (diskInfo[diskNumber].numSectors *
		diskInfo[diskNumber].sectorSize);

	// Make sure there's at least room for a basic install
	if (diskSize < basicInstallSize)
	{
		quit((status = ERR_NOFREE), _("Disk %s is too small (%dK) to install "
			"Visopsys\n(%dK required)"), diskInfo[diskNumber].name,
			(diskSize / 1024), (basicInstallSize / 1024));
	}

	// Show basic/full install choices based on whether there's enough space
	// to do both
	if (fullInstallSize && ((basicInstallSize + fullInstallSize) < diskSize))
	{
		if (graphics)
		{
			windowComponentSetSelected(installTypeRadio, 1);
			windowComponentSetEnabled(installTypeRadio, 1);
			windowComponentSetEnabled(formatCheckbox, 1);
			windowComponentSetEnabled(fsTypeCheckbox, 1);
		}
	}

	// Wait for the user to select any options and commence the installation
	if (graphics)
	{
		// We're ready to go, enable the buttons
		windowComponentSetEnabled(installButton, 1);
		windowComponentSetEnabled(quitButton, 1);

		// Focus the 'install' button by default
		windowComponentFocus(installButton);

		windowGuiRun();

		// Disable some components which are no longer usable
		windowComponentSetEnabled(installButton, 0);
		windowComponentSetEnabled(quitButton, 0);
		windowComponentSetEnabled(installTypeRadio, 0);
		windowComponentSetEnabled(formatCheckbox, 0);
		windowComponentSetEnabled(fsTypeCheckbox, 0);
		windowComponentSetEnabled(langButton, 0);
	}

	// Find out what type of installation to do
	installType = install_basic;
	if (graphics)
	{
		windowComponentGetSelected(installTypeRadio, &selected);
		if (selected == 1)
			installType = install_full;
	}
	else if (fullInstallSize && ((basicInstallSize + fullInstallSize) <
		diskSize))
	{
		status = vshCursorMenu(_("Please choose the install type:"),
			(char *[]){ _("Basic"), _("Full") }, 2, 0 /* no max rows */,
			1 /* selected */);

		if (status < 0)
		{
			textScreenRestore(&screen);
			return (status);
		}

		if (status == 1)
			installType = install_full;
	}

	bytesToCopy = basicInstallSize;
	if (installType == install_full)
		bytesToCopy += fullInstallSize;

	sprintf(tmpChar, _("Installing on disk %s.  Are you SURE?"), diskName);
	if (!yesOrNo(tmpChar))
		quit(0, "%s", _(cancelString));

	// Default filesystem formatting type is optimal/default FAT.
	strcpy(formatFsType, "fat");

	// In text mode, ask whether to format
	if (!graphics)
	{
		sprintf(tmpChar, "Format disk %s? (erases all data!)", diskName);
		doFormat = yesOrNo(tmpChar);
	}

	if (doFormat)
	{
		if (!graphics || chooseFsType)
		{
			if (askFsType() < 0)
				quit(0, "%s", _(cancelString));
		}

		updateStatus(_("Formatting... "));

		if (graphics)
		{
			progressDialog = windowNewProgressDialog(NULL, _("Formatting..."),
				&prog);
		}
		else
		{
			printf("%s", _("\nFormatting...\n"));
			vshProgressBar(&prog);
		}

		status = filesystemFormat(diskName, formatFsType, "Visopsys", 0,
			&prog);

		if (graphics)
			windowProgressDialogDestroy(progressDialog);
		else
			vshProgressBarDestroy(&prog);

		if (status < 0)
			quit(status, "%s", _("Errors during format."));

		// Rescan the disk info so we get the new filesystem type, etc.
		status = diskGet(diskName, &diskInfo[diskNumber]);
		if (status < 0)
			quit(status, "%s", _("Error rescanning disk after format."));

		updateStatus(_("Done\n"));
		memset((void *) &prog, 0, sizeof(progress));
	}

	// Copy the boot sector to the destination disk
	status = copyBootSector(&diskInfo[diskNumber]);
	if (status < 0)
		quit(status, "%s", _("Couldn't copy the boot sector."));

	// Mount the target filesystem
	updateStatus(_("Mounting target disk...  "));
	status = filesystemMount(diskName, MOUNTPOINT);
	if (status < 0)
		quit(status, "%s", _("Unable to mount the target disk."));
	updateStatus(_("Done\n"));

	// Rescan the disk info so we get the available free space, etc.
	status = diskGet(diskName, &diskInfo[diskNumber]);
	if (status < 0)
		quit(status, "%s", _("Error rescanning disk after mount."));

	// Try to make sure the filesystem contains enough free space
	if (diskInfo[diskNumber].freeBytes < bytesToCopy)
	{
		if (doFormat)
		{
			// We formatted, so we're pretty sure we won't succeed here.
			if (filesystemUnmount(MOUNTPOINT) < 0)
				error("%s", _("Unable to unmount the target disk."));
			quit((status = ERR_NOFREE),
				_("The filesystem on disk %s is too small "
				"(%lluK) for\nthe selected Visopsys installation (%uK "
				"required)."), diskName,
				(diskInfo[diskNumber].freeBytes / 1024), (bytesToCopy / 1024));
		}
		else
		{
			sprintf(tmpChar, _("There MAY not be enough free space on disk %s "
				"(%lluK) for the\nselected Visopsys installation (%uK "
				"required).  Continue?"), diskName,
				(diskInfo[diskNumber].freeBytes / 1024), (bytesToCopy / 1024));
			if (!yesOrNo(tmpChar))
			{
				if (filesystemUnmount(MOUNTPOINT) < 0)
					error("%s", _("Unable to unmount the target disk."));
				quit(0, "%s", _(cancelString));
			}
		}
	}

	if (!graphics)
	{
		memset((void *) &prog, 0, sizeof(progress));
		printf("%s", _("\nInstalling...\n"));
		vshProgressBar(&prog);
	}

	// Copy the files

	status = copyFiles(BASICINSTALL);
	if ((status >= 0) && (installType == install_full))
		status = copyFiles(FULLINSTALL);

	if (!graphics)
		vshProgressBarDestroy(&prog);

	if (status >= 0)
	{
		// Set the start program of the target installation to be the login
		// program
		configSet(MOUNTPOINT KERNEL_DEFAULT_CONFIG, KERNELVAR_START_PROGRAM,
			PATH_PROGRAMS "/login");

		// Set the system language variable
		configSet(MOUNTPOINT PATH_SYSTEM_CONFIG "/environment.conf", ENV_LANG,
			installLanguage);

		// Prompt the user to set the admin password
		setAdminPassword();
	}

	// Unmount the target filesystem
	updateStatus(_("Unmounting target disk...  "));
	if (filesystemUnmount(MOUNTPOINT) < 0)
		error("%s", _("Unable to unmount the target disk."));
	updateStatus(_("Done\n"));

	if (status < 0)
	{
		// Couldn't copy the files
		message = _("Unable to copy files.");
		if (graphics)
			quit(status, "%s", message);
		else
			error("%s", message);
	}
	else
	{
		message = _("Installation successful.");
		if (graphics)
			quit(status, "%s", message);
		else
			printf("\n%s\n", message);
	}

	// Only text mode should fall through to here
	pause();
	quit(status, NULL);

	// Make the compiler happy
	return (status);
}

