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
//  poll.h
//

// This is the Visopsys version of the standard C library header file <poll.h>

#if !defined(_POLL_H)

// Bitfields for the events and revents fields of struct pollfd
#define POLLIN		0x0001	// data to read
#define POLLPRI		0x0002	// urgent data to read
#define POLLOUT		0x0004	// writing now possible
#define POLLRDHUP	0x0008	// socket peer closed connection
#define POLLERR		0x0010	// error condition (only returned in revents)
#define POLLHUP		0x0020	// hang up (only returned in revents)
#define POLLNVAL	0x0040	// invalid request (only returned in revents)

typedef int nfds_t;

struct pollfd {
	int fd;			// file descriptor
	short events;	// requested events
	short revents;	// returned events
};

int poll(struct pollfd *, nfds_t, int);

#define _POLL_H
#endif

