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
//  fdopen.c
//

// This is the standard "fdopen" function, as found in standard C libraries

#include <stdio.h>
#include <errno.h>
#include <sys/api.h>
#include <sys/cdefs.h>


FILE *fdopen(int fd, const char *mode __attribute__((unused)))
{
	// The fdopen() function receives a file descriptor to an opened file,
	// and returns the stream associated with it.  In this implementation, the
	// mode is ignored, and the fileStream code will handle any errors from
	// inappropriate operations.

	int status = 0;
	fileStream *theStream = NULL;
	fileDescType type = filedesc_unknown;
	void *data = NULL;

	if (visopsys_in_kernel)
	{
		errno = ERR_BUG;
		return (theStream = NULL);
	}

	// Check params
	if ((fd < 0) || !mode)
	{
		errno = ERR_NULLPARAMETER;
		return (theStream = NULL);
	}

	// Look up the file descriptor
	status = _fdget(fd, &type, &data);
	if (status < 0)
	{
		errno = status;
		return (theStream = NULL);
	}

	switch (type)
	{
		case filedesc_filestream:
			theStream = (fileStream *) data;
			break;

		default:
			errno = ERR_NOTIMPLEMENTED;
			break;
	}

	return ((FILE *) theStream);
}

