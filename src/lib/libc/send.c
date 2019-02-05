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
//  send.c
//

// This is the standard "send" function, as found in standard C libraries

#include <sys/socket.h>
#include <errno.h>
#include <sys/api.h>
#include <sys/cdefs.h>


ssize_t send(int fd, const void *buf, size_t count,
	int flags __attribute__((unused)))
{
	// Write count bytes to the connection

	int status = 0;
	fileDescType type = filedesc_unknown;
	objectKey connection = NULL;

	if (visopsys_in_kernel)
	{
		errno = ERR_BUG;
		return (status = -1);
	}

	// Look up the file descriptor
	status = _fdget(fd, &type, (void **) &connection);
	if (status < 0)
	{
		errno = status;
		return (status = -1);
	}

	switch (type)
	{
		case filedesc_socket:
			status = networkWrite(connection, (unsigned char *) buf, count);
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

	return (status = count);
}

