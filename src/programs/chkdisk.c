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
//  chkdisk.c
//

// This is a program for performing filesystem scans

/* This is the text that appears when a user requests help about this program
<help>

 -- chkdisk --

This command can be used to perform a filesystem integrity check on a
logical disk.

Usage:
  chkdisk <disk_name>

The first parameter is the name of a disk (use the 'disks' command to list
the disks).  A check will be performed if the disk's filesystem is of a
recognized type, and the applicable filesystem driver supports a checking
function.

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
#include <sys/paths.h>

#define _(string) gettext(string)


static void usage(char *name)
{
	printf("%s", _("usage:\n"));
	printf(_("%s <disk_name>\n"), name);
	return;
}


int main(int argc, char *argv[])
{
	int status = 0;
	char *diskName = NULL;
	int force = 0;
	int repair = 0;
	char yesNo = '\0';

	setlocale(LC_ALL, getenv(ENV_LANG));
	textdomain("chkdisk");

	// Our argument is the disk number
	if (argc < 2)
	{
		usage(argv[0]);

		// Try to list the disks in the system
		loaderLoadAndExec(PATH_PROGRAMS "/disks", 3 /* user */, 1 /* block */);
		printf("\n");

		errno = ERR_ARGUMENTCOUNT;
		return (status = errno);
	}

	diskName = argv[1];

	// Print a message
	printf("%s", _("\nVisopsys CHKDISK Utility\nCopyright (C) 1998-2018 J. "
		"Andrew McLaughlin\n\n"));

	status = filesystemCheck(diskName, force, repair, NULL);

	if ((status < 0) && !repair)
	{
		// It's possible that the filesystem driver has no 'check' function.
		if (status != ERR_NOSUCHFUNCTION)
		{
			// The filesystem may contain errors.  Before we fail the whole
			// operation, ask whether the user wants to try and repair it.
			printf("%s", _("\nThe filesystem may contain errors.\nDo you want "
				"to try to repair it? (y/n): "));
			yesNo = getchar();
			printf("\n");

			if ((yesNo == 'y') || (yesNo == 'Y'))
				// Force, repair
				status = filesystemCheck(diskName, force, 1 /*repair*/, NULL);
		}

		if (status < 0)
		{
			// Make the error
			printf("%s", _("Filesystem consistency check failed.\n"));
			errno = status;
			return (status);
		}
	}

	errno = 0;
	return (status = errno);
}

