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
//  mktime.c
//

// This is the standard "mktime" function, as found in standard C libraries

#include <time.h>
#include <errno.h>


time_t mktime(struct tm *timeStruct)
{
	// The mktime() function converts the tm structure into UNIX time (seconds
	// since 00:00:00 UTC, January 1, 1970.  On error, ((time_t) -1) is
	// returned, and errno is set appropriately.

	int year = 0;
	time_t timeSimple = 0;
	int count;

	static const int monthDays[12] = {
		31 /* Jan */, 28 /* Feb */, 31 /* Mar */, 30 /* Apr */, 31 /* May */,
		30 /* Jun */, 31 /* Jul */, 31 /* Aug */, 30 /* Sep */, 31 /* Oct */,
		30 /* Nov */, 31 /* Dec */
	};

	// Check params
	if (!timeStruct)
	{
		errno = ERR_NULLPARAMETER;
		return (timeSimple = -1);
	}

	// Check the year
	year = (1900 + timeStruct->tm_year);
	if (year < 1970)
	{
		// Eek, these results would be wrong
		errno = ERR_INVALID;
		return (timeSimple = -1);
	}

	// Turn the time structure into the number of seconds since 0:00:00
	// 01/01/1970.

	// Calculate seconds for all complete years
	timeSimple = ((year - 1970) * SECPERYR);

	// Add 1 day's worth of seconds for every complete leap year.  There
	// is a leap year in every year divisible by 4 except for years which
	// are both divisible by 100 not by 400.  Got it?
	for (count = year; count >= 1972; count--)
	{
		if (!(count % 4) && ((count % 100) || !(count % 400)))
			timeSimple += SECPERDAY;
	}

	// Add seconds for all complete months in the year
	for (count = (timeStruct->tm_mon - 1); count >= 0; count --)
		timeSimple += (monthDays[count] * SECPERDAY);

	// Add seconds for all complete days in the month
	timeSimple += ((timeStruct->tm_mday - 1) * SECPERDAY);

	// Add one day's worth of seconds if THIS is a leap year, and if the
	// current month and day are greater than Feb 28
	if (!(year % 4) && ((year % 100) || !(year % 400)))
	{
		if ((timeStruct->tm_mon > 1) ||
			((timeStruct->tm_mon == 1) &&
			(timeStruct->tm_mday > 28)))
		{
			timeSimple += SECPERDAY;
		}
	}

	// Add seconds for all complete hours in the day
	timeSimple += (timeStruct->tm_hour * SECPERHR);

	// Add seconds for all complete minutes in the hour
	timeSimple += (timeStruct->tm_min * SECPERMIN);

	// Add the seconds
	timeSimple += timeStruct->tm_sec;

	// Done.
	return (timeSimple);
}

