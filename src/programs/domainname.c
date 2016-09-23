//
//  Visopsys
//  Copyright (C) 1998-2016 J. Andrew McLaughlin
//
//  This program is free software; you can redistribute it and/or modify it
//  under the terms of the GNU General Public License as published by the Free
//  Software Foundation; either version 2 of the License, or (at your option)
//  any later version.
//
//  This program is distributed in the hope that it will be useful, but
//  WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
//  or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License
//  for more details.
//
//  You should have received a copy of the GNU General Public License along
//  with this program; if not, write to the Free Software Foundation, Inc.,
//  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
//
//  domainname.c
//

// This is the UNIX-style command for getting and setting the network
// domain name

/* This is the text that appears when a user requests help about this program
<help>

 -- domainname --

Prints or sets the system's network domain name.

Usage:
  domainname [name]

If a name parameter is supplied, the domain name will be set to that name.
Otherwise, the program prints out the current domain name.  If networking
has not been enabled, this command has no effect.

</help>
*/

#include <stdio.h>
#include <sys/api.h>
#include <sys/network.h>


int main(int argc, char *argv[])
{
	char buffer[NETWORK_MAX_DOMAINNAMELENGTH];

	int status = 0;

	if (argc > 1)
	{
		// Set the domain name

		strncpy(buffer, argv[argc - 1], NETWORK_MAX_DOMAINNAMELENGTH);

		status = networkSetDomainName(buffer, NETWORK_MAX_DOMAINNAMELENGTH);
		if (status < 0)
			return (status);
	}
	else
	{
		// Just print the current domain name

		status = networkGetDomainName(buffer, NETWORK_MAX_DOMAINNAMELENGTH);
		if (status < 0)
			return (status);

		printf("%s\n", buffer);
	}

	return (status = 0);
}

