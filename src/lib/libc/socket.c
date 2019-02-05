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
//  socket.c
//

// This is the standard "socket" function, as found in standard C libraries

#include <errno.h>
#include <stdlib.h>
#include <sys/api.h>
#include <sys/cdefs.h>
#include <sys/socket.h>


int socket(int domain, int type, int protocol)
{
	// Return a file descriptor for the userspace representation of a UNIX/
	// POSIX-style socket.

	int status = 0;
	networkFilter *filter = NULL;
	int fd = 0;

	if (visopsys_in_kernel)
	{
		errno = ERR_BUG;
		return (status = -1);
	}

	// Initially, the data for the socket will be a Visopsys networkFilter,
	// which specifies the type of the connection.
	filter = calloc(1, sizeof(networkFilter));
	if (!filter)
	{
		errno = ERR_MEMORY;
		return (status = -1);
	}

	switch (domain)
	{
		case AF_INET:
			filter->flags |= NETWORK_FILTERFLAG_NETPROTOCOL;
			filter->netProtocol = NETWORK_NETPROTOCOL_IP4;
			break;

		default:
			free(filter);
			errno = ERR_NOTIMPLEMENTED;
			return (status = -1);
	}

	switch (type)
	{
		case SOCK_STREAM:
			filter->flags |= NETWORK_FILTERFLAG_TRANSPROTOCOL;
			filter->transProtocol = NETWORK_TRANSPROTOCOL_TCP;
			break;

		case SOCK_DGRAM:
			filter->flags |= NETWORK_FILTERFLAG_TRANSPROTOCOL;
			filter->transProtocol = NETWORK_TRANSPROTOCOL_UDP;
			break;
	}

	if (protocol)
	{
		filter->flags |= NETWORK_FILTERFLAG_TRANSPROTOCOL;
		filter->transProtocol = protocol;
	}

	// Get a POSIX-style file descriptor for it
	fd = _fdalloc(filedesc_socket, filter, 1 /* free data on close */);
	if (fd < 0)
	{
		free(filter);
		errno = fd;
		return (status = -1);
	}

	return (fd);
}

