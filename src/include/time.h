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
//  time.h
//

// This is the Visopsys version of the standard header file time.h

#if !defined(_TIME_H)

#define US_PER_MS		1000
#define MS_PER_SEC		1000
#define SECS_PER_MIN	60
#define MINS_PER_HR		60
#define HRS_PER_DAY		24
#define DAYS_PER_YEAR	365

#define US_PER_SEC		(US_PER_MS * MS_PER_SEC)
#define US_PER_MIN		(US_PER_SEC * SECS_PER_MIN)
#define US_PER_HOUR		(US_PER_MIN * MINS_PER_HR)

#define MS_PER_MIN		(MS_PER_SEC * SECS_PER_MIN)
#define MS_PER_HR		(MS_PER_MIN * MINS_PER_HR)
#define MS_PER_DAY		(MS_PER_HOUR * HRS_PER_DAY)

#define SECS_PER_HR		(SECS_PER_MIN * MINS_PER_HR)
#define SECS_PER_DAY	(SECS_PER_HR * HRS_PER_DAY)
#define SECS_PER_YR		(SECS_PER_DAY * DAYS_PER_YEAR)

#ifndef NULL
	#define NULL		0
#endif

struct tm {
	int tm_sec;		// seconds (0-59)
	int tm_min;		// minutes (0-59)
	int tm_hour;	// hours (0-23)
	int tm_mday;	// day of the month (1-31)
	int tm_mon;		// month (0-11)
	int tm_year;	// year (since 1900)
	int tm_wday;	// day of the week (0-6, 0=Sunday)
	int tm_yday;	// day in the year (0-364)
	int tm_isdst;	// daylight saving time (0-1)
};

typedef unsigned clock_t;
typedef unsigned time_t;

char *asctime(const struct tm *);
clock_t clock(void);
char *ctime(const time_t);
double difftime(time_t, time_t);
struct tm *gmtime(time_t);
time_t mktime(struct tm *);
time_t time(time_t *t);

#define _TIME_H
#endif

