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
//  sync.c
//

// This is the UNIX-style command for synchronizing changes to the disk.

/* This is the text that appears when a user requests help about this program
<help>

 -- sync --

Synchronize (commit) data to all disks.

Usage:
  sync

This command will cause all pending disk data in memory to be written out
to the disk.

Disk I/O in Visopsys is usually ansynchronous -- i.e. changes are first
made in memory, and then committed to disk at an opportune time.  This
command causes all such data to be committed at once.

</help>
*/

#include <stdio.h>
#include <errno.h>
#include <sys/api.h>


int main(int argc, char *argv[])
{
	// Attempts to synchronize all disks

	int status = 0;

	// This will sync all disks
	status = diskSyncAll();
	if (status < 0)
	{
		errno = status;
		if (argc)
			perror(argv[0]);
	}

	return (status);
}

