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
//  listen.c
//

// This is the standard "listen" function, as found in standard C libraries

#include <errno.h>
#include <stdlib.h>
#include <sys/api.h>
#include <sys/cdefs.h>
#include <sys/network.h>
#include <sys/socket.h>


int listen(int fd, int backlog __attribute__((unused)))
{
	// Initiate a listening network connection using a file descriptor
	// previously instantiated with a call to socket()

	int status = 0;
	fileDescType type = filedesc_unknown;
	networkFilter *filter = NULL;
	networkAddress address;
	objectKey connection = NULL;

	if (visopsys_in_kernel)
	{
		errno = ERR_BUG;
		return (status = -1);
	}

	// Look up the file descriptor
	status = _fdget(fd, &type, (void **) &filter);
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

	// Try to open the connection in the kernel
	connection = networkOpen(NETWORK_MODE_LISTEN, &address, filter);

	// Finished with the filter
	free(filter);

	// Set the connection as the file descriptor data.  May be NULL in case of
	// failure above, but the file descriptor still exists in any case.
	status = _fdset_data(fd, (void *) connection,
		0 /* don't free data on close */);
	if (status < 0)
	{
		errno = status;
		return (status = -1);
	}

	if (!connection)
	{
		errno = ERR_NOCONNECTION;
		return (status = -1);
	}

	return (status = 0);
}

