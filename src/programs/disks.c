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
//  disks.c
//

// This command just lists all the disks registered in the system

/* This is the text that appears when a user requests help about this program
<help>

 -- disks --

Print all of the logical disks attached to the system.

Usage:
  disks

This command will print all of the disks by name, along with any device,
filesystem, or logical partition information that is appropriate.

Disk names start with certain combinations of letters which tend to indicate
the type of disk.  Examples

cd0  - First CD-ROM disk
fd1  - Second floppy disk
hd0b - Second logical partition on the first hard disk.

Note in the third example above, the physical device is the first hard disk,
hd0.  Logical partitions are specified with letters, in partition table order
(a = first partition, b = second partition, etc.).

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

#define _(string) gettext(string)


int main(int argc __attribute__((unused)), char *argv[])
{
	int status = 0;
	int availableDisks = 0;
	disk *diskInfo = NULL;
	int count;

	setlocale(LC_ALL, getenv(ENV_LANG));
	textdomain("disks");

	// Call the kernel to give us the number of available disks
	availableDisks = diskGetCount();

	diskInfo = malloc(DISK_MAXDEVICES * sizeof(disk));
	if (!diskInfo)
	{
		perror(argv[0]);
		return (status = ERR_MEMORY);
	}

	status = diskGetAll(diskInfo, (DISK_MAXDEVICES * sizeof(disk)));
	if (status < 0)
	{
		// Eek.  Problem getting disk info
		errno = status;
		perror(argv[0]);
		free(diskInfo);
		return (status);
	}

	printf("%s", _("\nDisk name"));
	textSetColumn(11);
	printf("%s", _("Partition"));
	textSetColumn(37);
	printf("%s", _("Filesystem"));
	textSetColumn(49);
	printf("%s", _("Mount\n"));

	for (count = 0; count < availableDisks; count ++)
	{
		// Print disk info
		printf("%s", diskInfo[count].name);

		textSetColumn(11);
		printf("%s", diskInfo[count].partType);

		if (strcmp(diskInfo[count].fsType, "unknown"))
		{
			textSetColumn(37);
			printf("%s", diskInfo[count].fsType);
		}

		if (diskInfo[count].mounted)
		{
			textSetColumn(49);
			printf("%s", diskInfo[count].mountPoint);
		}

		printf("\n");
	}

	errno = 0;
	free(diskInfo);
	return (status = errno);
}

