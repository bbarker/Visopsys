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
//  remove.c
//

// This is the standard "remove" function, as found in standard C libraries

#include <stdio.h>
#include <errno.h>
#include <sys/api.h>


int remove(const char *pathname)
{
	// The remove() function causes the file or empty directory whose name
	// is the string pointed to by pathname to be removed from the filesystem.
	// It calls delete() for files, and removeDir() for directories.  On
	// success, zero is returned.  On error, -1 is returned, and errno is
	// set appropriately.

	int status = 0;
	file f;

	if (visopsys_in_kernel)
		return (errno = ERR_BUG);

	// Figure out whether the file exists
	status = fileFind(pathname, &f);
	if (status < 0)
	{
		errno = status;
		return (-1);
	}

	// Now we should have some info about the file.  Is it a file or a
	// directory?
	if (f.type == fileT)
		// This is a regular file.
		status = fileDelete(pathname);
	else if (f.type == dirT)
		// This is a directory
		status = fileRemoveDir(pathname);
	else
		// Eek.  What kind of file is this?
		status = ERR_INVALID;

	if (status < 0)
	{
		errno = status;
		return (-1);
	}

	return (0);
}

