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
//  gmtime.c
//

// This is the standard "gmtime" function, as found in standard C libraries

#include <time.h>
#include <string.h>


static int dayOfWeek(unsigned day, unsigned month, unsigned year)
{
	// This function, given a date value, returns the day of the week as 0-6,
	// with 0 being Monday

	if (month < 3)
	{
		month += 12;
		year -= 1;
	}

	return (((((13 * month) + 3) / 5) + day + year + (year / 4) -
		(year / 100) + (year / 400)) % 7);
}


struct tm *gmtime(time_t timeSimple)
{
	// The gmtime() function converts the calendar time timeSimple to broken-
	// down time representation

	int year = 0;
	int count;

	static struct tm timeStruct;
	static int monthDays[12] = {
		31 /* Jan */, 00 /* Feb */, 31 /* Mar */, 30 /* Apr */, 31 /* May */,
		30 /* Jun */, 31 /* Jul */, 31 /* Aug */, 30 /* Sep */, 31 /* Oct */,
		30 /* Nov */, 31 /* Dec */
	};

	memset(&timeStruct, 0, sizeof(struct tm));

	// Calculate seconds
	timeStruct.tm_sec = (timeSimple % SECS_PER_MIN);
	timeSimple -= timeStruct.tm_sec;

	// Complete minutes
	timeStruct.tm_min = ((timeSimple % SECS_PER_HR) / SECS_PER_MIN);
	timeSimple -= (timeStruct.tm_min * SECS_PER_MIN);

	// Complete hours
	timeStruct.tm_hour = ((timeSimple % SECS_PER_DAY) / SECS_PER_HR);
	timeSimple -= (timeStruct.tm_hour * SECS_PER_HR);

	// Calculate complete years
	year = 1970;
	while (timeSimple >= SECS_PER_YR)
	{
		// There is a leap year in every year divisible by 4 except for years
		// which are both divisible by 100 not by 400.  Got it?
		if (!(year % 4) && ((year % 100) || !(year % 400)))
		{
			if (timeSimple >= (SECS_PER_YR + SECS_PER_DAY))
				timeSimple -= (SECS_PER_YR + SECS_PER_DAY);
			else
				break;
		}
		else
		{
			timeSimple -= SECS_PER_YR;
		}

		year += 1;
	}

	timeStruct.tm_year = (year - 1900);

	// Calculate day of the year
	timeStruct.tm_yday = (timeSimple / SECS_PER_DAY);

	if (!(year % 4) && ((year % 100) || !(year % 400)))
		monthDays[1] = 29;
	else
		monthDays[1] = 28;

	// Calculate complete months
	for (count = 0; count < 12; count ++)
	{
		if (timeSimple < (unsigned)(monthDays[count] * SECS_PER_DAY))
			break;

		timeSimple -= (monthDays[count] * SECS_PER_DAY);
		timeStruct.tm_mon += 1;
	}

	// Calculate the day
	timeStruct.tm_mday = ((timeSimple / SECS_PER_DAY) + 1);

	// Get the day of the week
	timeStruct.tm_wday = ((dayOfWeek((timeStruct.tm_mday + 1),
		(timeStruct.tm_mon + 1), year) + 1) % 7);

	// Done.
	return (&timeStruct);
}

