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
//  asctime.c
//

// This is the standard "asctime" function, as found in standard C libraries

#include <time.h>
#include <stdio.h>
#include <errno.h>


char *asctime(const struct tm *timePtr)
{
	// From the linux man page about this function:
	// The asctime() function converts the broken-down time value
	// timeptr into a string with the  same  format  as  ctime().
	// The  return  value points to a statically allocated string
	// which might be overwritten by subsequent calls to  any  of
	// the date and time functions.

	// ctime() time format:
	// "Wed Jun 30 21:49:08 1993"

	static const char *weekDay[] = { "Sun", "Mon", "Tue", "Wed", "Thu", "Fri",
		"Sat" };
	static const char *month[] = { "Jan", "Feb", "Mar", "Apr", "May", "Jun",
		"Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };
	static char timeString[26];

	// Make sure timePtr is not NULL
	if (!timePtr)
	{
		errno = ERR_NULLPARAMETER;
		return (NULL);
	}

	// Create the string
	sprintf(timeString, "%s %s %d %02d:%02d:%02d %d",
		weekDay[timePtr->tm_wday], month[timePtr->tm_mon],
		(timePtr->tm_mday + 1), timePtr->tm_hour, timePtr->tm_min,
		timePtr->tm_sec, (1900 + timePtr->tm_year));

	// Ok, return a pointer to timeString
	return (timeString);
}

