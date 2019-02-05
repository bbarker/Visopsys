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
//  time.c
//

// This is the standard "time" function, as found in standard C libraries

#include <time.h>
#include <errno.h>
#include <sys/api.h>


time_t time(time_t *t)
{
	// The time() function returns the value of time in seconds since 00:00:00
	// UTC, January 1, 1970.  If t is non-NULL, the return value is also
	// stored in the location to which t points.  On error, ((time_t) -1)
	// is returned, and errno is set appropriately.

	int status = 0;
	time_t timeSimple = 0;
	struct tm timeStruct;

	if (visopsys_in_kernel)
		return (errno = ERR_BUG);

	// Get the date and time according to the kernel
	status = rtcDateTime(&timeStruct);
	if (status < 0)
	{
		errno = status;
		return (timeSimple = -1);
	}

	timeSimple = mktime(&timeStruct);

	// Done.
	if (t)
		*t = timeSimple;

	return (timeSimple);
}

