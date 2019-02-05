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
//  realpath.c
//

// This is the standard "realpath" function, as found in standard C libraries

#include <errno.h>
#include <stdlib.h>
#include <sys/api.h>


char *realpath(const char *path, char *fullPath)
{
	// Takes a pathname, possibly relative, possibly containing symbolic links,
	// '.' and '..' elements, and extra separators, and returns a proper
	// canonical pathname.

	int status = 0;

	if (visopsys_in_kernel)
	{
		errno = ERR_BUG;
		return (fullPath = NULL);
	}

	status = fileFixupPath(path, fullPath);
	if (status < 0)
	{
		errno = status;
		return (fullPath = NULL);
	}

	return (fullPath);
}

