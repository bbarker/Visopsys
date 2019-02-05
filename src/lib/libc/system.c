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
//  system.c
//

// This is the standard "system" function, as found in standard C libraries.
// Unlike UNIX it does not execute a shell program to run the command, but
// rather passes the command and arguments straight to the kernel's loader.

#include <stdlib.h>
#include <sys/api.h>
#include <errno.h>


int system(const char *command)
{
	int status = 0;
	int privilege = 0;

	// Check params
	if (!command)
		return (status = ERR_NULLPARAMETER);

	if (visopsys_in_kernel)
		return (errno = ERR_BUG);

	// What is my privilege level?
	privilege = multitaskerGetProcessPrivilege(multitaskerGetCurrentProcessId());
	if (privilege < 0)
		return (status = privilege);

	// Try to execute the command
	status = loaderLoadAndExec(command, privilege, 1 /* block */);

	return (status);
}

