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
//  reboot.c
//

// This is the UNIX-style command for rebooting the system

/* This is the text that appears when a user requests help about this program
<help>

 -- reboot --

A command for rebooting the computer.

Usage:
  reboot [-e] [-f]

This command causes the system to reboot.  If the (optional) '-e' parameter
is supplied, then 'reboot' will attempt to eject the boot medium (if
applicable, such as a CD-ROM).  If the (optional) '-f' parameter is
supplied, then it will attempt to ignore errors and reboot regardless.
Use this flag with caution if filesystems do not appear to be unmounting
correctly; you may need to back up unsaved data before rebooting.

Options:
-e  : Eject the boot medium.
-f  : Force reboot and ignore errors.

</help>
*/

#include <libintl.h>
#include <locale.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/api.h>
#include <sys/env.h>

#define _(string) gettext(string)

static disk sysDisk;


static void doEject(void)
{
	int status = 0;

	printf("%s", _("\nEjecting, please wait... "));

	if (diskSetLockState(sysDisk.name, 0) < 0)
		printf("%s", _("\n\nUnable to unlock the media door\n"));
	else
	{
		status = diskSetDoorState(sysDisk.name, 1);
		if (status < 0)
		{
			// Try a second time.  Sometimes 2 attempts seems to help.
			status = diskSetDoorState(sysDisk.name, 1);

			if (status < 0)
				printf("%s", _("\n\nCan't seem to eject.  Try pushing the "
					"'eject' button now.\n"));
		}
		else
			printf("\n");
	}
}


int main(int argc, char *argv[])
{
	// There's a nice system function for doing this.

	int status = 0;
	char opt;
	int eject = 0;
	int force = 0;

	setlocale(LC_ALL, getenv(ENV_LANG));
	textdomain("reboot");

	// Check options
	while (strchr("ef?", (opt = getopt(argc, argv, "ef"))))
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

			default:
				fprintf(stderr, _("Unknown option '%c'\n"), optopt);
				return (status = ERR_INVALID);
		}
	}

	// Get the system disk
	memset(&sysDisk, 0, sizeof(disk));
	fileGetDisk("/", &sysDisk);

	if (eject && (sysDisk.type & DISKTYPE_CDROM))
		doEject();

	status = shutdown(1, force);
	if (status < 0)
	{
		if (!force)
			printf(_("Use \"%s -f\" to force.\n"), argv[0]);
		return (status);
	}

	// Wait for death
	while (1);
}

