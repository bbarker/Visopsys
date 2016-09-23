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
//  truncate.c
//

// This is the standard "truncate" function, as found in standard C libraries

#include <unistd.h>
#include <errno.h>
#include <fcntl.h>

#include <stdio.h>
#include <sys/file.h>


int truncate(const char *path, off_t length)
{
	// This is a convenience wrapper for the ftruncate() function.  The truncate
	// function causes the size of the named file to be set to the requested
	// length.  If the file was previously larger than this size, the extra
	// data is lost.  If the file was previously smaller, the file is expanded.

	int status = 0;
	int fd = 0;

	// Check params
	if (!path)
	{
		errno = ERR_NULLPARAMETER;
		return (-1);
	}

	// Open the file for writing
	fd = open(path, O_RDWR);//O_WRONLY);
	if (fd < 0)
		return (-1);

	fileStream *newStream = (fileStream *) fd;
	printf("%s open mode %x\n", newStream->f.name, newStream->f.openMode);

	// Hand it over to our ftruncate function
	status = ftruncate(fd, length);
	if (status < 0)
		return (-1);

	// Close the file
	close(fd);

	// Return success
	return (0);
}

