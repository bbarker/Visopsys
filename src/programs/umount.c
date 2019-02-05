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
//  mount.c
//

// This is the UNIX-style command for mounting filesystems

/* This is the text that appears when a user requests help about this program
<help>

 -- umount --

Unmount a filesystem.

Usage:
  umount <mount_point>

This command will unmount (disconnect, make unusable) the filesystem mounted
at the mount point specified as a parameter.

Example:
  umount /C

This will synchronize and unmount the logical disk mounted at /C.

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
	printf(_("%s <mount point>\n"), name);
	return;
}


int main(int argc, char *argv[])
{
	// Attempts to unmount the named filesystem from the named mount point

	int status = 0;

	setlocale(LC_ALL, getenv(ENV_LANG));
	textdomain("umount");

	if (argc < 2)
	{
		usage(argv[0]);
		return (status = ERR_ARGUMENTCOUNT);
	}

	status = filesystemUnmount(argv[1]);
	if (status < 0)
	{
		printf(_("Error unmounting %s\n"), argv[1]);
		errno = status;
		perror(argv[0]);
		return (status = errno);
	}

	// Finished
	return (status = 0);
}

