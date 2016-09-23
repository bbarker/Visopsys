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
//  ramdisk.c
//

// Creates and destroys RAM disks
// - Originally contributed by Davide Airaghi
// - Modified by Andy McLaughlin.

/* This is the text that appears when a user requests help about this program
<help>

 -- ramdisk --

Create or destroy RAM disks

Usage:
  ramdisk <create> <bytes>[unit]
  ramdisk <destroy> <name>

Create or destroy a RAM disk.  A RAM disk is an area of memory which behaves
as a physical disk drive.

When creating a RAM disk, the size argument may be given in bytes, or
(optionally) with a unit such as K (kilobytes), M (megabytes), or G
(gigabytes).  For example:

<bytes = 1> and [unit = K] ==> total size 1 KB = 1024 bytes
<bytes = 1> and [unit = M] ==> total size 1 MB = 1,048,576 bytes
<bytes = 1> and [unit = G] ==> total size 1 GB = 1,073,741,824 bytes

When destroying a RAM disk, the name is given.

Examples:
  ramdisk create 1048576
  ramdisk create 1024K
  ramdisk create 1M
  ramdisk destroy ram0

</help>
*/

#include <errno.h>
#include <libintl.h>
#include <locale.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/api.h>
#include <sys/disk.h>
#include <sys/env.h>

#define _(string) gettext(string)

typedef enum { bytes, kilobytes, megabytes, gigabytes } unit;


static void usage(char *name)
{
	printf("%s", _("usage:\n"));
	printf(_("%s <create> <bytes>[unit]\n-or-\n"), name);
	printf(_("%s <destroy> <name>\n"), name);
	return;
}


int main(int argc, char *argv[])
{
	int status = 0;
	int create = 0;
	int destroy = 0;
	char *sizeArg = NULL;
	unit units = bytes;
	unsigned size = 0;
	char name[DISK_MAX_NAMELENGTH];

	setlocale(LC_ALL, getenv(ENV_LANG));
	textdomain("ramdisk");

	// There need to be at least 2 arguments
	if ((argc < 3) || (argc > 4))
	{
		usage(argv[0]);
		return (status = ERR_ARGUMENTCOUNT);
	}

	// Arg 1 must be either 'create' or 'destroy'
	if (!strcasecmp(argv[1], "create"))
		create = 1;
	if (!strcasecmp(argv[1], "destroy"))
		destroy = 1;

	if (!create && !destroy)
	{
		usage(argv[0]);
		return (status = ERR_INVALID);
	}

	if (create)
	{
		sizeArg = argv[argc - 1];

		// Do we have units specified?
		switch (sizeArg[strlen(sizeArg) - 1])
		{
			case 'k':
			case 'K':
				units = kilobytes;
				sizeArg[strlen(sizeArg) - 1] = '\0';
				break;
			case 'm':
			case 'M':
				units = megabytes;
				sizeArg[strlen(sizeArg) - 1] = '\0';
				break;
			case 'g':
			case 'G':
				units = gigabytes;
				sizeArg[strlen(sizeArg) - 1] = '\0';
				break;
		}

		// Get the size itself
		size = atoi(sizeArg);

		// Enlarge?
		switch (units)
		{
			case kilobytes:
				size *= 1024;
				break;
			case megabytes:
				size *= (1024 * 1024);
				break;
			case gigabytes:
				size *= (1024 * 1024 * 1024);
				break;
			default:
				break;
		}

		status = diskRamDiskCreate(size, name);
		if (status < 0)
		{
			fprintf(stderr, "%s", _("Error creating RAM disk\n"));
			return (status);
		}
		else
			printf(_("Created RAM disk %s size %u\n"), name, size);
	}

	else if (destroy)
	{
		strncpy(name, argv[argc - 1], DISK_MAX_NAMELENGTH);
		status = diskRamDiskDestroy(name);
		if (status < 0)
		{
			fprintf(stderr, _("Error destroying RAM disk %s\n"), name);
			return (status);
		}
		else
			printf(_("Destroyed RAM disk %s\n"), name);
	}

	// Return
	return (status = 0);
}

