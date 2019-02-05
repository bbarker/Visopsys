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
//  read.c
//

// This is the standard "read" function, as found in standard C libraries

#include <unistd.h>
#include <errno.h>
#include <sys/api.h>
#include <sys/cdefs.h>


ssize_t read(int fd, void *buf, size_t count)
{
	// Read count bytes from the stream

	int status = 0;
	fileDescType type = filedesc_unknown;
	void *data = NULL;

	if (visopsys_in_kernel)
	{
		errno = ERR_BUG;
		return (status = -1);
	}

	// Look up the file descriptor
	status = _fdget(fd, &type, &data);
	if (status < 0)
	{
		errno = status;
		return (status = -1);
	}

	switch (type)
	{
		case filedesc_textstream:
			status = textInputStreamReadN(multitaskerGetTextInput(), count,
				buf);
			break;

		case filedesc_filestream:
			status = fileStreamRead((fileStream *) data, count, buf);
			break;

		default:
			status = ERR_NOTIMPLEMENTED;
			break;
	}

	if (status < 0)
	{
		errno = status;
		return (status = -1);
	}

	return (count);
}

