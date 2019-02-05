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
//  rmdir.c
//

// This is the standard "rmdir" function, as found in standard C libraries

#include <unistd.h>
#include <errno.h>
#include <sys/api.h>


int rmdir(const char *pathname)
{
	// The rmdir() function causes the directory to be removed from the
	// filesystem.  The directory must be empty.  On success, zero is
	// returned.  On error, -1 is returned, and errno is set appropriately.

	int status = 0;
	file f;

	if (visopsys_in_kernel)
	{
		errno = ERR_BUG;
		return (-1);
	}

	// Figure out whether the directory exists
	status = fileFind(pathname, &f);
	if (status < 0)
	{
		errno = status;
		return (-1);
	}

	// Now we should have some info about the file.  Is it a file or a
	// directory?

	switch (f.type)
	{
		case dirT:
			status = fileRemoveDir(pathname);
			break;

		default:
			status = ERR_INVALID;
			break;
	}

	if (status < 0)
	{
		errno = status;
		return (-1);
	}

	return (0);
}

