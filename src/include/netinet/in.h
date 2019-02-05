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
//  in.h
//

// This file is the Visopsys implementation of the standard C library header
// file <netinet/in.h>.

#if !defined(_IN_H)

#define INET_ADDRSTRLEN			16
#define INET6_ADDRSTRLEN		46

#define INADDR_ANY				0x00000000
#define IN6ADDR_ANY_INIT	\
	((unsigned char[]) { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 })
#define INADDR_LOOPBACK			0x7F000001
#define IN6ADDR_LOOPBACK_INIT	\
	((unsigned char[]) { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1 })

struct in_addr {
    unsigned s_addr; // 32-bit int
};

struct in6_addr {
	unsigned char s6_addr[16]; // 16 bytes
};

#define _IN_H
#endif

