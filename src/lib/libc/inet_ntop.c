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
//  inet_ntop.c
//

// This is the standard "inet_ntop" function, as found in standard C libraries

#include <arpa/inet.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <netinet/in.h>
#include <sys/socket.h>


const char *inet_ntop(int family, const void *src, char *dest, socklen_t size)
{
	// Converts a network address, expressed as an in_addr family structure,
	// into a string

	const char *output = NULL;
	struct in_addr *addr = (struct in_addr *) src;
	struct in6_addr *addr6 = (struct in6_addr *) src;
	unsigned short value = 0;
	int count;

	// Check params
	if (!src || !dest)
	{
		errno = ERR_NULLPARAMETER;
		return (output = NULL);
	}

	switch (family)
	{
		case AF_INET:
		{
			// IPV4: ddd.ddd.ddd.ddd

			if (size < INET_ADDRSTRLEN)
			{
				errno = ERR_BOUNDS;
				output = NULL;
				break;
			}

			snprintf(dest, size, "%d.%d.%d.%d", (addr->s_addr & 0xFF),
				((addr->s_addr >> 8) & 0xFF), ((addr->s_addr >> 16) & 0xFF),
				((addr->s_addr >> 24) & 0xFF));

			output = dest;
			break;
		}

		case AF_INET6:
		{
			// IPV6: xxxx:xxxx:xxxx:xxxx:xxxx:xxxx:xxxx:xxxx
			// TODO: elision

			if (size < INET6_ADDRSTRLEN)
			{
				errno = ERR_BOUNDS;
				output = NULL;
				break;
			}

			for (count = 0; count < 8; count ++)
			{
				value = *((unsigned short *) &addr6->s6_addr[count << 1]);
				value = ntohs(value);

				snprintf((dest + strlen(dest)), (size - strlen(dest)), "%s%x",
					(count? ":" : ""), value);
			}

			output = dest;
			break;
		}

		default:
		{
			// Not (yet?) supported
			errno = ERR_NOTIMPLEMENTED;
			output = NULL;
			break;
		}
	}

	return (output);
}

