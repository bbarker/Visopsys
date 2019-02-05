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
//  wallpaper.c
//

// Calls the kernel's window manager to change the background image

/* This is the text that appears when a user requests help about this program
<help>

 -- wallpaper --

Set the background wallpaper image.

Usage:
  wallpaper [image_file]

(Only available in graphics mode)

This command will set the background wallpaper image from the (optional)
image file name parameter or, if no image file name is supplied, the program
will prompt the user.

Currently, bitmap (.bmp) and JPEG (.jpg) image formats are supported.

</help>
*/

#include <libintl.h>
#include <locale.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/api.h>
#include <sys/deskconf.h>
#include <sys/env.h>
#include <sys/paths.h>
#include <sys/user.h>
#include <sys/vsh.h>
#include <sys/window.h>

#define _(string) gettext(string)

static int readOnly = 1;
static char currentUser[USER_MAX_NAMELENGTH + 1];


static int writeFileConfig(const char *configName, const char *imageName)
{
	// Set the wallpaper in the requested config file.

	int status = 0;
	file f;

	memset(&f, 0, sizeof(file));

	status = fileFind(configName, NULL);
	if (status < 0)
	{
		// The file doesn't exist.  Try to create it.
		status = fileOpen(configName, (OPENMODE_WRITE | OPENMODE_CREATE), &f);
		if (status < 0)
			return (status);

		// Now close the file
		fileClose(&f);
	}

	// Save the wallpaper variable
	if (imageName)
		status = configSet(configName, DESKVAR_BACKGROUND_IMAGE, imageName);
	else
		status = configUnset(configName, DESKVAR_BACKGROUND_IMAGE);

	return (status);
}


static int writeConfig(const char *imageName)
{
	// Set the wallpaper in the desktop config.

	int status = 0;
	char configName[MAX_PATH_NAME_LENGTH];

	if (readOnly)
		return (status = ERR_NOWRITE);

	if (!strcmp(currentUser, USER_ADMIN))
	{
		// The user 'admin' doesn't have user settings.  Use the system one.
		sprintf(configName, PATH_SYSTEM_CONFIG "/" DESKTOP_CONFIG);
	}
	else
	{
		// Does the user have a config dir?
		sprintf(configName, PATH_USERS_CONFIG, currentUser);

		status = fileFind(configName, NULL);
		if (status < 0)
		{
			// No, try to create it.
			status = fileMakeDir(configName);
			if (status < 0)
				return (status);
		}

		// Use the user's window config file?
		sprintf(configName, PATH_USERS_CONFIG "/" DESKTOP_CONFIG,
			currentUser);
	}

	status = writeFileConfig(configName, imageName);

	return (status);
}


int main(int argc, char *argv[])
{
	int status = 0;
	disk sysDisk;
	char fileName[MAX_PATH_NAME_LENGTH];

	setlocale(LC_ALL, getenv(ENV_LANG));
	textdomain("wallpaper");

	// Only work in graphics mode
	if (!graphicsAreEnabled())
	{
		fprintf(stderr, _("\nThe \"%s\" command only works in graphics "
			"mode\n"), (argc? argv[0] : ""));
		return (status = ERR_NOTINITIALIZED);
	}

	memset(&sysDisk, 0, sizeof(disk));
	memset(fileName, 0, MAX_PATH_NAME_LENGTH);

	// Find out whether we are currently running on a read-only filesystem
	if (!fileGetDisk(PATH_SYSTEM, &sysDisk))
		readOnly = sysDisk.readOnly;

	// Need the user name for saving settings
	userGetCurrent(currentUser, USER_MAX_NAMELENGTH);

	if (argc < 2)
	{
		// The user did not specify a file.  We will prompt them.
		status = windowNewFileDialog(NULL, _("Enter filename"),
			_("Please choose the background image:"), PATH_SYSTEM_WALLPAPER,
			fileName, MAX_PATH_NAME_LENGTH, fileT, 1 /* show thumbnails */);
		if (status != 1)
		{
			if (!status)
				return (status);

			printf("%s", _("No filename specified\n"));
			return (status);
		}
	}
	else
	{
		strncpy(fileName, argv[1], MAX_PATH_NAME_LENGTH);
	}

	if (strncmp(fileName, DESKVAR_BACKGROUND_NONE, MAX_PATH_NAME_LENGTH))
	{
		status = fileFind(fileName, NULL);
		if (status < 0)
		{
			printf("%s", _("File not found\n"));
			return (status);
		}

		windowShellTileBackground(fileName);
	}
	else
	{
		windowShellTileBackground(NULL);
	}

	status = writeConfig(fileName);

	return (status);
}

