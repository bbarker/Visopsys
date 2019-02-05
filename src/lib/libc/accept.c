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
//  accept.c
//

// This is the standard "accept" function, as found in standard C libraries

#include <errno.h>
#include <sys/api.h>
#include <sys/cdefs.h>
#include <sys/socket.h>


int accept(int fd, const struct sockaddr *addr, socklen_t *addrLen)
{
	// Wait for an inbound network connection using a file descriptor
	// previously instantiated with a call to socket()

	// NOTE: This is a dummy function for now, as the kernel's network stack
	// doesn't yet implement incoming connections.

	int status = 0;
	fileDescType type = filedesc_unknown;
	objectKey connection = NULL;

	if (visopsys_in_kernel)
	{
		errno = ERR_BUG;
		return (status = -1);
	}

	if (!addr || !addrLen)
	{
		errno = ERR_NULLPARAMETER;
		return (status = -1);
	}

	// Look up the file descriptor
	status = _fdget(fd, &type, (void **) &connection);
	if (status < 0)
	{
		errno = status;
		return (status = -1);
	}

	// Only supported for socket file descriptors
	if (type != filedesc_socket)
	{
		errno = ERR_INVALID;
		return (status = -1);
	}

	while (1)
		multitaskerYield();

	return (status = 0);
}

