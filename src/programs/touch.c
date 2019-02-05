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
//  touch.c
//

// This is the UNIX-style command for touching files

/* This is the text that appears when a user requests help about this program
<help>

 -- touch --

Timestamp a file or directory.

Usage:
  touch <file1> [file2] [...]

This command has a dual purpose; it is used either to create one or more new,
empty files, or to update the time/date stamp on one or more existing files
or directories.

</help>
*/

#include <errno.h>
#include <libintl.h>
#include <locale.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/api.h>
#include <sys/env.h>
#include <sys/vsh.h>

#define _(string) gettext(string)


static void usage(char *name)
{
	printf("%s", _("usage:\n"));
	printf(_("%s <file1> [file2] [...]\n"), name);
	return;
}


int main(int argc, char *argv[])
{
	// This command is the "touch" command.  It does one of two things based
	// on the filename argument.  If the file does not exist, it creates a
	// new, empty file.  If the file does exist, it updates the date and time
	// of the file to the current date and time.

	int status = 0;
	file theFile;
	int count;

	setlocale(LC_ALL, getenv(ENV_LANG));
	textdomain("touch");

	if (argc < 2)
	{
		usage(argv[0]);
		return (status = ERR_ARGUMENTCOUNT);
	}

	// Loop through all of our file name arguments
	for (count = 1; count < argc; count ++)
	{
		// Make sure the name isn't NULL
		if (!argv[count])
			return (status = ERR_NULLPARAMETER);

		// Initialize the file structure
		memset(&theFile, 0, sizeof(file));

		// Call the "find file" function to see if the file exists
		status = fileFind(argv[count], &theFile);

		// Now, either the file exists or it doesn't...

		if (status < 0)
		{
			// The file doesn't exist.  We will create the file.
			status = fileOpen(argv[count], (OPENMODE_WRITE | OPENMODE_CREATE),
				&theFile);
			if (status < 0)
			{
				errno = status;
				perror(argv[0]);
				return (status);
			}

			// Now close the file
			fileClose(&theFile);
		}
		else
		{
			// The file exists.  We need to update the date and time of the
			// file
			status = fileTimestamp(argv[count]);
			if (status < 0)
			{
				errno = status;
				perror(argv[0]);
				return (status);
			}
		}
	}

	// Return success
	return (status = 0);
}

