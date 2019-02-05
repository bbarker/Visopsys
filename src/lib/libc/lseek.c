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
//  lseek.c
//

// This is a wrapper for the "fseek" function, as found in standard C
// libraries, but converts a POSIX-style file descriptor into a FILE *.

#include <unistd.h>
#include <errno.h>
#include <stdio.h>
#include <sys/api.h>
#include <sys/cdefs.h>


off_t lseek(int fd, off_t offset, int whence)
{
	int status = 0;
	void *data = NULL;

	if (visopsys_in_kernel)
	{
		errno = ERR_BUG;
		return (status = -1);
	}

	// Look up the file descriptor
	status = _fdget(fd, NULL /* type */, &data);
	if (status < 0)
	{
		errno = status;
		return (status = -1);
	}

	return (status = fseek((FILE *) data, offset, whence));
}

