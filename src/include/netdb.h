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
//  netdb.h
//

// This is the Visopsys version of the standard C library header file
// <netdb.h>

#if !defined(_NETDB_H)

// Contains the socklen_t and sockaddr definitions
#include <sys/socket.h>

struct addrinfo {
	int ai_flags;				// additional options
	int ai_family;				// address family (e.g. AF_INET, AF_UNSPEC)
	int ai_socktype;			// socket type (e.g. SOCK_STREAM, SOCK_DGRAM)
	int ai_protocol;			// protocol (0 for any)
	socklen_t ai_addrlen;		// length of ai_addr
	struct sockaddr *ai_addr;	// address
	char *ai_canonname;			// full canonical hostname
	struct addrinfo *ai_next;	// next in the linked list
};

struct protoent {
	char *p_name;				// official protocol name
	char **p_aliases;			// alias list
	int p_proto;				// protocol number
};

struct servent {
	char *s_name;				// official service name
	char **s_aliases;			// alias list
	int s_port;					// port number
	char *s_proto;				// protocol to use
};

struct protoent *getprotobyname(const char *);
struct protoent *getprotobynumber(int);
struct servent *getservbyname(const char *, const char *);
struct servent *getservbyport(int, const char *);

#define _NETDB_H
#endif

