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
//  tmpfile.c
//

// This is the standard "tmpfile" function, as found in standard C libraries

#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <sys/api.h>


FILE *tmpfile(void)
{
	// Opens a unique temporary file in read/write mode, and returns the
	// stream pointer.  The file is automatically deleted on closure.

	int status = 0;
	fileStream *theStream = NULL;

	if (visopsys_in_kernel)
	{
		errno = ERR_BUG;
		return (theStream = NULL);
	}

	// Get memory for the file stream
	theStream = malloc(sizeof(fileStream));
	if (!theStream)
	{
		errno = ERR_MEMORY;
		return (theStream);
	}

	memset(theStream, 0, sizeof(fileStream));

	status = fileStreamGetTemp(theStream);
	if (status < 0)
	{
		errno = status;
		free(theStream);
		return (theStream = NULL);
	}

	// Note that we want the kernel to delete it when it's closed
	theStream->f.openMode |= OPENMODE_DELONCLOSE;

	return ((FILE *) theStream);
}

