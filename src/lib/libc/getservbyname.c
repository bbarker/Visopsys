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
//  getservbyname.c
//

// This is the standard "getservbyname" function, as found in standard C
// libraries

#include <netdb.h>
#include <string.h>
#include <sys/network.h>

#define ALIASES(arg, args...) (char *[]) { arg, ##args }

// Hint: see /etc/services in Linux
struct servent _serviceEntries[] = {
	// name, aliases, port, protocol
	{ NETWORK_PORTNAME_FTPDATA, NULL, NETWORK_PORT_FTPDATA, "tcp" },
	{ NETWORK_PORTNAME_FTP, NULL, NETWORK_PORT_FTP, "tcp" },

	{ NETWORK_PORTNAME_SSH, NULL, NETWORK_PORT_SSH, "tcp" },
	{ NETWORK_PORTNAME_SSH, NULL, NETWORK_PORT_SSH, "udp" },

	{ NETWORK_PORTNAME_TELNET, NULL, NETWORK_PORT_TELNET, "tcp" },
	{ NETWORK_PORTNAME_TELNET, NULL, NETWORK_PORT_TELNET, "udp" },

	{ NETWORK_PORTNAME_SMTP, ALIASES("mail", NULL),
		NETWORK_PORT_SMTP, "tcp" },

	{ NETWORK_PORTNAME_DNS, NULL, NETWORK_PORT_DNS, "tcp" },
	{ NETWORK_PORTNAME_DNS, NULL, NETWORK_PORT_DNS, "udp" },

	{ NETWORK_PORTNAME_BOOTPSERVER, NULL, NETWORK_PORT_BOOTPSERVER, "tcp" },
	{ NETWORK_PORTNAME_BOOTPSERVER, NULL, NETWORK_PORT_BOOTPSERVER, "udp" },

	{ NETWORK_PORTNAME_BOOTPCLIENT, ALIASES("dhcpc", NULL),
		NETWORK_PORT_BOOTPCLIENT, "tcp" },
	{ NETWORK_PORTNAME_BOOTPCLIENT, ALIASES("dhcpc", NULL),
		NETWORK_PORT_BOOTPCLIENT, "udp" },

	{ NETWORK_PORTNAME_HTTP, ALIASES("www", NULL), NETWORK_PORT_HTTP, "tcp" },
	{ NETWORK_PORTNAME_HTTP, ALIASES("www", NULL), NETWORK_PORT_HTTP, "udp" },

	{ NETWORK_PORTNAME_POP3, ALIASES("pop-3", NULL),
		NETWORK_PORT_POP3, "tcp" },
	{ NETWORK_PORTNAME_POP3, ALIASES("pop-3", NULL),
		NETWORK_PORT_POP3, "udp" },

	{ NETWORK_PORTNAME_NTP, NULL, NETWORK_PORT_NTP, "tcp" },
	{ NETWORK_PORTNAME_NTP, NULL, NETWORK_PORT_NTP, "udp" },

	{ NETWORK_PORTNAME_IMAP3, NULL, NETWORK_PORT_IMAP3, "tcp" },
	{ NETWORK_PORTNAME_IMAP3, NULL, NETWORK_PORT_IMAP3, "udp" },

	{ NETWORK_PORTNAME_LDAP, NULL, NETWORK_PORT_LDAP, "tcp" },
	{ NETWORK_PORTNAME_LDAP, NULL, NETWORK_PORT_LDAP, "udp" },

	{ NETWORK_PORTNAME_HTTPS, NULL, NETWORK_PORT_HTTPS, "tcp" },
	{ NETWORK_PORTNAME_HTTPS, NULL, NETWORK_PORT_HTTPS, "udp" },

	{ NETWORK_PORTNAME_FTPSDATA, NULL, NETWORK_PORT_FTPSDATA, "tcp" },
	{ NETWORK_PORTNAME_FTPS, NULL, NETWORK_PORT_FTPS, "tcp" },

	{ NETWORK_PORTNAME_TELNETS, NULL, NETWORK_PORT_TELNETS, "tcp" },
	{ NETWORK_PORTNAME_TELNETS, NULL, NETWORK_PORT_TELNETS, "udp" },

	{ NETWORK_PORTNAME_IMAPS, NULL, NETWORK_PORT_IMAPS, "tcp" },
	{ NETWORK_PORTNAME_IMAPS, NULL, NETWORK_PORT_IMAPS, "udp" },

	{ NETWORK_PORTNAME_POP3S, NULL, NETWORK_PORT_POP3S, "tcp" },
	{ NETWORK_PORTNAME_POP3S, NULL, NETWORK_PORT_POP3S, "udp" },

	{ NULL, NULL, 0, NULL }
};


struct servent *getservbyname(const char *name, const char *proto)
{
	// Returns a servent structure for the entry that matches the service
	// name, and if given, the protocol name.

	struct servent *entries = _serviceEntries;
	int count1, count2;

	// Check params.  'proto' may be NULL.
	if (!name)
		return (NULL);

	for (count1 = 0; entries[count1].s_name; count1 ++)
	{
		if (!strcmp(name, entries[count1].s_name))
		{
			if (!proto || !strcmp(proto, entries[count1].s_proto))
				return (&entries[count1]);
		}

		if (entries[count1].s_aliases)
		{
			for (count2 = 0; entries[count1].s_aliases[count2]; count2 ++)
			{
				if (!strcmp(name, entries[count1].s_aliases[count2]))
				{
					if (!proto || !strcmp(proto, entries[count1].s_proto))
						return (&entries[count1]);
				}
			}
		}
	}

	// Not found
	return (NULL);
}

