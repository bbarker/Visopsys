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
//  inet.h
//

// This is the Visopsys version of the standard header file arpa/inet.h

#if !defined(_INET_H)

// Contains the processorSwap* definitions
#include <sys/processor.h>

// Contains the socklen_t definition
#include <sys/socket.h>

#ifdef PROCESSOR_LITTLE_ENDIAN
	#define htonl(hostlong)		processorSwap32(hostlong)
	#define htons(hostshort)	processorSwap16(hostshort)
	#define ntohl(netlong)		processorSwap32(netlong)
	#define ntohs(netshort)		processorSwap16(netshort)
#else
	#define htonl(hostlong)		(hostlong)
	#define htons(hostshort)	(hostshort)
	#define ntohl(netlong)		(netlong)
	#define ntohs(netshort)		(netshort)
#endif

int inet_pton(int af, const char *, void *);
const char *inet_ntop(int, const void *, char *, socklen_t);

#define _INET_H
#endif

