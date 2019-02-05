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
//  soket.h
//

// This file is the Visopsys implementation of the standard C library header
// file <sys/socket.h>.

#if !defined(_SOCKET_H)

// Contains the size_t and ssize_t definitions
#include <stddef.h>

// Contains the in_addr definition
#include <netinet/in.h>

// Supported socket protocol families
#define PF_UNSPEC	0	// unspecified
#define PF_INET		2	// IPv4 protocol family
#define PF_INET6	10	// IPv6 protocol family

// Supported socket address families
#define AF_UNSPEC	PF_UNSPEC
#define AF_INET		PF_INET
#define AF_INET6	PF_INET6

// Flags for recv() and send().  Mostly not implemented.
#define MSG_OOB         	0x00000001	// process out-of-band data
#define MSG_PEEK        	0x00000002	// peek at incoming messages
#define MSG_DONTROUTE   	0x00000004	// don't use a gateway (local only)
#define MSG_TRUNC      		0x00000008	// report truncated data
#define MSG_DONTWAIT    	0x00000040	// nonblocking I/O
#define MSG_EOR         	0x00000080	// end of record
#define MSG_WAITALL     	0x00000100	// wait for a full request
#define MSG_CONFIRM     	0x00000800	// confirm path validity
#define MSG_ERRQUEUE    	0x00002000	// receive queued from error queue
#define MSG_NOSIGNAL    	0x00004000	// do not generate signal
#define MSG_MORE        	0x00008000	// sender has more data to send
#define MSG_CMSG_CLOEXEC	0x40000000	// close file descriptor on exec

// Shutdown modes for shutdown()
#define SHUT_RD				0
#define SHUT_WR				1
#define SHUT_RDWR			2

typedef unsigned socklen_t;

// Types of sockets
typedef enum {
  SOCK_STREAM, SOCK_DGRAM, SOCK_RAW

} socket_type;

struct sockaddr {
    unsigned short sa_family;	// address family
    unsigned char sa_data[14];	// 14 bytes of protocol address
};

struct sockaddr_in {
    unsigned short sin_family;	// address family
    unsigned short sin_port;	// port number
    struct in_addr sin_addr;	// internet address
    unsigned char sin_zero[8];	// same size as struct sockaddr
};

struct sockaddr_in6 {
    unsigned short sin6_family;	// address family
    unsigned short sin6_port;	// port number
    unsigned sin6_flowinfo;		// flow information
    struct in6_addr sin6_addr;	// internet address
    unsigned sin6_scope_id;		// scope ID
};

union _ss_union {
	struct sockaddr_in in;
	struct sockaddr_in6 in6;
};

#define _SS_PADSIZE (sizeof(union _ss_union) - sizeof(unsigned short))

struct sockaddr_storage {
	unsigned short ss_family;
	unsigned char __ss_pad[_SS_PADSIZE];
};

int accept(int, const struct sockaddr *, socklen_t *);
int bind(int, const struct sockaddr *, socklen_t);
int connect(int, const struct sockaddr *, socklen_t);
int listen(int, int);
ssize_t recv(int, void *, size_t, int);
ssize_t send(int, const void *, size_t, int);
int shutdown(int, int);
int socket(int, int, int);

#define _SOCKET_H
#endif

