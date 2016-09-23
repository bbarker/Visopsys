//
//  Visopsys
//  Copyright (C) 1998-2016 J. Andrew McLaughlin
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

#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <time.h>
#include <sys/api.h>


int stat(const char *fileName, struct stat *buf)
{
	// Returns information about the specified file

	int status = 0;
	file theFile;
	disk theDisk;

	if (visopsys_in_kernel)
		return (errno = ERR_BUG);

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

	buf->st_dev = theDisk.deviceNumber;
	buf->st_ino = 1;      // bogus

	// Set the file type, if known
	buf->st_mode = 0;
	if (theFile.type == fileT)
		buf->st_mode |= S_IFREG;
	if (theFile.type == dirT)
		buf->st_mode |= S_IFDIR;
	if (theFile.type == linkT)
		buf->st_mode |= S_IFLNK;

	buf->st_nlink = 1;    // bogus
	buf->st_uid = 1;      // bogus
	buf->st_gid = 1;      // bogus
	buf->st_rdev = 0;     // bogus
	buf->st_size = theFile.size;
	buf->st_blksize = theFile.blockSize;
	buf->st_blocks = theFile.blocks;
	buf->st_atime = mktime(&theFile.accessed);
	buf->st_mtime = mktime(&theFile.modified);
	buf->st_ctime = mktime(&theFile.created);

	return (0);
}

