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
//  getprotobyname.c
//

// This is the standard "getprotobyname" function, as found in standard C
// libraries

#include <netdb.h>
#include <string.h>
#include <sys/network.h>

#define ALIASES(arg, args...) (char *[]) { arg, ##args }

// Hint: see /etc/protocols in Linux
struct protoent _protocolEntries[] = {
	// name, aliases, protocol
	{ "icmp", ALIASES("ICMP", NULL), NETWORK_TRANSPROTOCOL_ICMP },
	{ "tcp", ALIASES("TCP", NULL), NETWORK_TRANSPROTOCOL_TCP },
	{ "udp", ALIASES("UDP", NULL), NETWORK_TRANSPROTOCOL_UDP },
	{ NULL, NULL, 0 }
};


struct protoent *getprotobyname(const char *name)
{
	// Returns a protoent structure for the entry that matches the protocol
	// name.

	struct protoent *entries = _protocolEntries;
	int count1, count2;

	// Check params
	if (!name)
		return (NULL);

	for (count1 = 0; entries[count1].p_name; count1 ++)
	{
		if (!strcmp(name, entries[count1].p_name))
			return (&entries[count1]);

		if (entries[count1].p_aliases)
		{
			for (count2 = 0; entries[count1].p_aliases[count2]; count2 ++)
			{
				if (!strcmp(name, entries[count1].p_aliases[count2]))
					return (&entries[count1]);
			}
		}
	}

	// Not found
	return (NULL);
}

