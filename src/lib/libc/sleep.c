//
//  Visopsys
//  Copyright (C) 1998-2016 J. Andrew McLaughlin
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
//  sleep.c
//

// This is the standard "sleep" function, as found in standard C libraries

#include <errno.h>
#include <unistd.h>
#include <sys/api.h>


unsigned sleep(unsigned seconds)
{
	// Sleep for the specified number of seconds. Returns 0 if the requested
	// time has elapsed, or the number of seconds left to sleep.

	if (visopsys_in_kernel)
	{
		errno = ERR_BUG;
		return (seconds);
	}

	for ( ; seconds > 0; seconds --)
		multitaskerWait(1000);

	return (seconds);
}

