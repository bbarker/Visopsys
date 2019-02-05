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

 -- mount --

Mount a filesystem.

Usage:
  mount <disk> [mount_point]

This command will mount (make usable) the filesystem on the specified logical
disk.  Available logical disks can be listed using the 'disks' command.
The second parameter is a location (the mount point) where the contents of
the filesystem should be mounted.  The mount point parameter is optional if
it is listed in the mount configuration file (usually located at
/system/config/mount.conf).

Example:
  mount cd0 /cdrom

This will mount the first CD-ROM device, and make its contents accessible
in the /cdrom subdirectory.

Note that the mount point parameter should specify a name that does *not*
exist.  This is the opposite of the UNIX mount command behaviour.  The
example above will fail if there is already a file or directory called /cdrom.

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

#define MAX_VARIABLE_LEN	128


static void usage(char *name)
{
	fprintf(stderr, "%s", _("usage:\n"));
	fprintf(stderr, _("%s <disk> [mount point]\n"), name);
	return;
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


int main(int argc, char *argv[])
{
	// Attempts to mount the named filesystem to the named mount point

	int status = 0;
	char *diskName = NULL;
	char *mountPoint = NULL;
	disk *theDisk = NULL;

	setlocale(LC_ALL, getenv(ENV_LANG));
	textdomain("mount");

	if (argc < 2)
	{
		usage(argv[0]);
		status = ERR_ARGUMENTCOUNT;
		goto out;
	}

	diskName = argv[1];

	mountPoint = calloc(MAX_PATH_LENGTH, 1);
	if (!mountPoint)
	{
		status = ERR_MEMORY;
		goto out;
	}

	if (argc < 3)
	{
		status = getMountPoint(diskName, mountPoint);
		if (status < 0)
		{
			fprintf(stderr, _("No mount point specified for %s in %s\n"),
				diskName, DISK_MOUNT_CONFIG);
			usage(argv[0]);
			status = ERR_ARGUMENTCOUNT;
			goto out;
		}
	}
	else
	{
		strncpy(mountPoint, argv[argc - 1], MAX_PATH_LENGTH);
	}

	theDisk = calloc(1, sizeof(disk));
	if (!theDisk)
	{
		perror(argv[0]);
		status = ERR_MEMORY;
		goto out;
	}

	// Get the disk and try to check whether there's media
	status = diskGet(diskName, theDisk);
	if (status >= 0)
	{
		// If it's removable, see if there is any media present
		if ((theDisk->type & DISKTYPE_REMOVABLE) &&
			!diskMediaPresent(theDisk->name))
		{
			fprintf(stderr, _("No media in disk %s\n"), theDisk->name);
			status = ERR_INVALID;
			goto out;
		}
	}

	// Mount
	status = filesystemMount(diskName, mountPoint);
	if (status < 0)
	{
		perror(argv[0]);
		goto out;
	}

	// Should we save the mount point in the mount.conf file?
	if ((argc >= 3) && (getMountPoint(diskName, mountPoint) < 0))
		setMountPoint(diskName, mountPoint);

	// Finished
	status = 0;

out:
	if (mountPoint)
		free(mountPoint);
	if (theDisk)
		free(theDisk);

	return (status);
}

