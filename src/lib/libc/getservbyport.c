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
//  getservbyport.c
//

// This is the standard "getservbyport" function, as found in standard C
// libraries

#include <netdb.h>
#include <string.h>

// Defined in getservbyname.c
extern struct servent _serviceEntries[];


struct servent *getservbyport(int port, const char *proto)
{
	// Returns a servent structure for the entry that matches the port, and if
	// given, the protocol name.

	struct servent *entries = _serviceEntries;
	int count;

	// 'proto' may be NULL.

	for (count = 0; entries[count].s_name; count ++)
	{
		if (entries[count].s_port == port)
		{
			if (!proto || !strcmp(proto, entries[count].s_proto))
				return (&entries[count]);
		}
	}

	// Not found
	return (NULL);
}

