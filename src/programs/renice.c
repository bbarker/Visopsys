//
//  Visopsys
//  Copyright (C) 1998-2018 J. Andrew McLaughlin
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
//  renice.c
//

// This is the UNIX-style command for changing the priority levels
// of running processes

/* This is the text that appears when a user requests help about this program
<help>

 -- renice --

Change the priority level(s) of one or more processes.

Usage:
  renice <priority> <process1> [process2] [...]

The 'renice' command can be used to change the priority level(s) of one or
more processes.  The first parameter is the new priority level for the
process(es).  Priority level can be from 0-7, with 0 being highest (real time)
priority and 7 being lowest.  The second parameter (and, optionally, any
number of additional parameters) is the process ID of the process to change.
To see a list of running processes, use the 'ps' command.

</help>
*/

#include <errno.h>
#include <libintl.h>
#include <locale.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/api.h>
#include <sys/env.h>

#define _(string) gettext(string)


static void usage(char *name)
{
	printf("%s", _("usage:\n"));
	printf(_("%s <priority> <process1> [process2] [...]\n"), name);
	return;
}


int main(int argc, char *argv[])
{
	// This command will prompt the multitasker to set the priority of the
	// process with the supplied process id

	int status = 0;
	int processId = 0;
	int newPriority = 0;
	int count;

	setlocale(LC_ALL, getenv(ENV_LANG));
	textdomain("renice");

	if (argc < 3)
	{
		usage(argv[0]);
		return (status = ERR_ARGUMENTCOUNT);
	}

	// What is the requested priority
	newPriority = atoi(argv[1]);

	// OK?
	if (errno)
	{
		perror(argv[0]);
		usage(argv[0]);
		return (status = errno);
	}

	// Loop through all of our process ID arguments
	for (count = 2; count < argc; count ++)
	{
		processId = atoi(argv[count]);

		// OK?
		if (errno)
		{
			perror(argv[0]);
			usage(argv[0]);
			return (status = errno);
		}

		// Set the process
		status = multitaskerSetProcessPriority(processId, newPriority);
		if (status < 0)
		{
			errno = status;
			perror(argv[0]);
		}
		else
			printf(_("%d changed\n"), processId);
	}

	// Return success
	return (status = 0);
}

