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
//  rename.c
//

// This is the standard "rename" function, as found in standard C libraries

#include <stdio.h>
#include <errno.h>
#include <sys/api.h>


int rename(const char *old, const char *new)
{
	// The rename() function changes the name of a file.  The old argument
	// points to the pathname of the file to be renamed.  The new argument
	// points to the new pathname of the file.  Upon successful completion,
	// 0 is returned. Otherwise, -1 is returned and errno is set to indicate
	// an error.

	if (visopsys_in_kernel)
		return (errno = ERR_BUG);

	// Let the kernel do all the work, baby.
	int status = fileMove(old, new);
	if (status < 0)
	{
		errno = status;
		return (-1);
	}

	return (0);
}

