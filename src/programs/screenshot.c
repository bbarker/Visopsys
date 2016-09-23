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
//  screenshot.c
//

// Saves a screen shot, either with the supplied filename, or else it
// will query the user for one.

/* This is the text that appears when a user requests help about this program
<help>

 -- screenshot --

Save the current screen to an image file.

Usage:
  screenshot [file_name]

(Only available in graphics mode)

The (optional) file name can be an absolute pathname under which the
screenshot should be saved.  If no file name is specified, the program
will present a dialog box asking for one.

Currently only the uncompressed, 24-bit bitmap format is supported.

</help>
*/

#include <errno.h>
#include <libintl.h>
#include <locale.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/api.h>
#include <sys/env.h>
#include <sys/window.h>

#define _(string) gettext(string)


int main(int argc, char *argv[])
{
	int status = 0;
	char fileName[MAX_PATH_NAME_LENGTH];

	setlocale(LC_ALL, getenv(ENV_LANG));
	textdomain("screenshot");

	// Only work in graphics mode
	if (!graphicsAreEnabled())
	{
		printf(_("\nThe \"%s\" command only works in graphics mode\n"),
			argv[0]);
		errno = ERR_NOTINITIALIZED;
		return (status = errno);
	}

	// Did the user supply a file name?
	if (argc > 1)
		strncpy(fileName, argv[argc - 1], MAX_PATH_NAME_LENGTH);

	else
	{
		// Prompt for a file name
		status = windowNewFileDialog(NULL, _("Enter file name"),
			_("Please enter the file name to use:"), NULL, fileName,
			MAX_PATH_NAME_LENGTH, fileT, 1 /* show thumbnails */);
		if (status != 1)
		{
			errno = status;
			if (errno)
				perror(argv[0]);
			return (errno);
		}
	}
	fileName[MAX_PATH_NAME_LENGTH - 1] = '\0';

	status = windowSaveScreenShot(fileName);
	if (status < 0)
	{
		windowNewErrorDialog(NULL, _("Error"),
			_("Couldn't save the screenshot.\n"
			"I'm sure it would have been nice."));
		errno = status;
		perror(argv[0]);
	}

	return (status);
}

