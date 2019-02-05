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
//  fread.c
//

// This is the standard "fread" function, as found in standard C libraries

#include <stdio.h>
#include <errno.h>
#include <sys/api.h>


size_t fread(void *buf, size_t size, size_t number, FILE *theStream)
{
	// Read 'size' bytes from the stream 'number' times

	int status = 0;
	size_t bytes = (size * number);

	if (visopsys_in_kernel)
	{
		errno = ERR_BUG;
		return (bytes = 0);
	}

	if (!size)
		return (bytes = 0);

	if (theStream == stdin)
		status = textInputStreamReadN(multitaskerGetTextInput(), bytes, buf);
	else
		status = fileStreamRead(theStream, bytes, buf);

	if (status < 0)
	{
		errno = status;
		return 0;
	}

	bytes = (status / size);

	return (bytes);
}

