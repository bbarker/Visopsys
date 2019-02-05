//
//  Visopsys
//  Copyright (C) 1998-2018 J. Andrew McLaughlin
//
//  This library is free software; you can redistribute it and/or modify it
//  under the terms of the GNU Lesser General Public License as published by
//  the Free Software Foundation; either version 2.1 of the License, or (at
//  your option) any later version.
//
//  This library is distributed in the hope that it will be useful, but
//  WITHOUT ANY WARRANTY; without even the implied warranty of
//  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU Lesser
//  General Public License for more details.
//
//  You should have received a copy of the GNU Lesser General Public License
//  along with this library; if not, write to the Free Software Foundation,
//  Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
//
//  stat.c
//

// This is the standard "stat" function, as found in standard C libraries

#include <sys/stat.h>
#include <errno.h>
#include <string.h>
#include <time.h>
#include <sys/api.h>


int stat(const char *fileName, struct stat *st)
{
	// Returns information about the specified file

	int status = 0;
	file theFile;
	disk theDisk;

	if (visopsys_in_kernel)
		return (errno = ERR_BUG);

	// Check params
	if (!fileName || !st)
	{
		errno = ERR_NULLPARAMETER;
		return (-1);
	}

	// Try to find the file
	memset(&theFile, 0, sizeof(file));
	status = fileFind(fileName, &theFile);
	if (status < 0)
	{
		errno = status;
		return (-1);
	}

	// Get the disk
	memset(&theDisk, 0, sizeof(disk));
	status = fileGetDisk(fileName, &theDisk);
	if (status < 0)
	{
		errno = status;
		return (-1);
	}

	st->st_dev = theDisk.deviceNumber;
	st->st_ino = 1;      // bogus

	// Set the file type, if known
	st->st_mode = 0;
	if (theFile.type == fileT)
		st->st_mode |= S_IFREG;
	if (theFile.type == dirT)
		st->st_mode |= S_IFDIR;
	if (theFile.type == linkT)
		st->st_mode |= S_IFLNK;

	st->st_nlink = 1;    // bogus
	st->st_uid = 1;      // bogus
	st->st_gid = 1;      // bogus
	st->st_rdev = 0;     // bogus
	st->st_size = theFile.size;
	st->st_blksize = theFile.blockSize;
	st->st_blocks = theFile.blocks;
	st->st_atime = mktime(&theFile.accessed);
	st->st_mtime = mktime(&theFile.modified);
	st->st_ctime = mktime(&theFile.created);

	return (0);
}

