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
//  inet_pton.c
//

// This is the standard "inet_pton" function, as found in standard C libraries

#include <arpa/inet.h>
#include <ctype.h>
#include <errno.h>
#include <string.h>
#include <sys/socket.h>


static int ipv4(const char *src, int srcLen, void *dest)
{
	// IPV4: ddd.ddd.ddd.ddd

	struct in_addr *addr = (struct in_addr *) dest;
	int segment = 0;
	int value = 0;
	int count;

	addr->s_addr = 0;

	for (count = 0; count < srcLen; count ++)
	{
		if (src[count] == '.')
		{
			addr->s_addr |= (value << (segment << 3));
			value = 0;
			segment += 1;

			if (segment > 3)
				// Invalid
				return (0);
		}
		else
		{
			if (!isdigit(src[count]))
				// Invalid
				return (0);

			value *= 10;
			value += (src[count] - 48);

			if (value > 255)
				// Invalid
				return (0);
		}
	}

	if (!segment)
		// Invalid
		return (0);

	// Last segment
	addr->s_addr |= (value << (segment << 3));

	// Success
	return (1);
}


static int ipv6(const char *src, int srcLen, void *dest)
{
	// IPV6: xxxx:xxxx:xxxx:xxxx:xxxx:xxxx:xxxx:xxxx
	//       xxxx:xxxx:xxxx:xxxx:xxxx:xxxx:ddd.ddd.ddd.ddd
	//       xxxx::xxxx
	//       xxxx::
	//       ::x
	//       ::

	struct in6_addr *addr = (struct in6_addr *) dest;
	int segment = 0;
	int value = 0;
	int elide = 0;
	int count;

	memset(addr, 0, sizeof(struct in6_addr));

	for (count = 0; count < srcLen; count ++)
	{
		if (src[count] == ':')
		{
			// See whether a sequence of zeros is elided with '::'
			if (count && (src[count - 1] == ':'))
			{
				if (elide > 0)
					// More than one of these is not allowed.  Invalid.
					return (0);

				// Will/must always be >= 1
				elide = segment;
			}
			else
			{
				*((unsigned short *) &addr->s6_addr[segment << 1]) =
					htons(value);
				value = 0;
				segment += 1;

				if (segment > 7)
					// Invalid
					return (0);
			}
		}
		else
		{
			if (!isxdigit(src[count]))
				// Invalid
				return (0);

			value <<= 4;

			if (src[count] >= 'a')
				value |= (10 + (src[count] - 'a'));
			else if (src[count] >= 'A')
				value |= (10 + (src[count] - 'A'));
			else
				value |= (src[count] - '0');

			if (value > 0xFFFF)
				// Invalid
				return (0);
		}
	}

	if (!segment)
		// Invalid
		return (0);

	// Last segment
	*((unsigned short *) &addr->s6_addr[segment << 1]) = htons(value);
	segment += 1;

	// At this point, elide > 0 (if set), segment >= 2, and segment > elide.

	// If zeros were elided, we need to move any subsequent data.
	if ((elide > 0) /* && (segment > elide) */ && (segment < 8))
	{
		// (segment - elide) : quantity to move
		// (8 - segment)     : how far to move
		memmove((addr->s6_addr + ((elide + (8 - segment)) << 1)),
			(addr->s6_addr + (elide << 1)), ((segment - elide) << 1));
		memset((addr->s6_addr + (elide << 1)), 0, ((8 - segment) << 1));
	}

	// Success
	return (1);
}


/////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////
//
//  Below here, the functions are exported for external use
//
/////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////

int inet_pton(int family, const char *src, void *dest)
{
	// Converts a network address, expressed as a string, into an in_addr
	// family structure

	int status = 0;
	int srcLen = 0;

	// Check params
	if (!src || !dest)
	{
		errno = ERR_NULLPARAMETER;
		return (status = 0);
	}

	srcLen = strlen(src);
	if (!srcLen)
	{
		errno = ERR_NODATA;
		return (status = 0);
	}

	switch (family)
	{
		case AF_INET:
		{
			status = ipv4(src, srcLen, dest);
			if (!status)
				errno = ERR_BADDATA;
			break;
		}

		case AF_INET6:
		{
			status = ipv6(src, srcLen, dest);
			if (!status)
				errno = ERR_BADDATA;
			break;
		}

		default:
		{
			// Not (yet?) supported
			errno = ERR_NOTIMPLEMENTED;
			status = -1;
			break;
		}
	}

	return (status);
}

