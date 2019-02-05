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
//  mkdir.c
//

// This is the standard "mkdir" function, as found in standard C libraries

#include <sys/stat.h>
#include <errno.h>
#include <sys/api.h>


int mkdir(const char *dirName, mode_t mode __attribute__((unused)))
{
	// Creates a directory.

	int status = 0;

	if (visopsys_in_kernel)
	{
		errno = ERR_BUG;
		return (status = -1);
	}

	// Check params
	if (!dirName)
	{
		errno = ERR_NULLPARAMETER;
		return (status = -1);
	}

	status = fileMakeDir(dirName);
	if (status < 0)
		errno = status;

	return (status);
}

