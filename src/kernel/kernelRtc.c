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
//  kernelRtc.c
//

#include "kernelRtc.h"
#include "kernelCpu.h"
#include "kernelError.h"

static kernelDevice *systemRtc = NULL;
static kernelRtcOps *ops = NULL;
static int startSeconds, startMinutes, startHours, startDayOfMonth,
	startMonth, startYear; // Set when the timer code is initialized


/////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////
//
//  Below here, the functions are exported for external use
//
/////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////

int kernelRtcInitialize(kernelDevice *dev)
{
	// This function initializes the RTC.

	int status = 0;

	// Check params
	if (!dev)
	{
		kernelError(kernel_error, "NULL parameter");
		return (status = ERR_NOTINITIALIZED);
	}

	systemRtc = dev;

	if (!systemRtc->driver || !systemRtc->driver->ops)
	{
		kernelError(kernel_error, "The RTC driver or ops are NULL");
		return (status = ERR_NULLPARAMETER);
	}

	ops = systemRtc->driver->ops;

	// Now, register the starting time that the kernel was booted.
	startSeconds = kernelRtcReadSeconds();
	startMinutes = kernelRtcReadMinutes();
	startHours = kernelRtcReadHours();
	startDayOfMonth = kernelRtcReadDayOfMonth();
	startMonth = kernelRtcReadMonth();
	startYear = kernelRtcReadYear();

	// Measure CPU timestamp frequency
	kernelCpuTimestampFreq();

	// Return success
	return (status = 0);
}


int kernelRtcReadSeconds(void)
{
	// This is a generic routine for invoking the corresponding routine
	// in a Real-Time Clock driver.  It takes no arguments and returns the
	// result from the device driver call

	int status = 0;

	if (!systemRtc)
		return (status = ERR_NOTINITIALIZED);

	// Make sure the device driver 'read seconds' function has been installed
	if (!ops->driverReadSeconds)
	{
		kernelError(kernel_error, "The device driver routine is NULL");
		return (status = ERR_NOSUCHFUNCTION);
	}

	// Call the driver function
	status = ops->driverReadSeconds();
	return (status);
}


int kernelRtcReadMinutes(void)
{
	// This is a generic routine for invoking the corresponding routine
	// in a Real-Time Clock driver.  It takes no arguments and returns the
	// result from the device driver call

	int status = 0;

	if (!systemRtc)
		return (status = ERR_NOTINITIALIZED);

	// Make sure the device driver 'read minutes' function has been installed
	if (!ops->driverReadMinutes)
	{
		kernelError(kernel_error, "The device driver routine is NULL");
		return (status = ERR_NOSUCHFUNCTION);
	}

	// Call the driver function
	status = ops->driverReadMinutes();
	return (status);
}


int kernelRtcReadHours(void)
{
	// This is a generic routine for invoking the corresponding routine
	// in a Real-Time Clock driver.  It takes no arguments and returns the
	// result from the device driver call

	int status = 0;

	if (!systemRtc)
		return (status = ERR_NOTINITIALIZED);

	// Make sure the device driver 'read hours' function has been installed
	if (!ops->driverReadHours)
	{
		kernelError(kernel_error, "The device driver routine is NULL");
		return (status = ERR_NOSUCHFUNCTION);
	}

	// Call the driver function
	status = ops->driverReadHours();
	return (status);
}


int kernelRtcReadDayOfMonth(void)
{
	// This is a generic routine for invoking the corresponding routine
	// in a Real-Time Clock driver.  It takes no arguments and returns the
	// result from the device driver call

	int status = 0;

	if (!systemRtc)
		return (status = ERR_NOTINITIALIZED);

	// Make sure the device driver 'read day-of-month' function has been
	// installed
	if (!ops->driverReadDayOfMonth)
	{
		kernelError(kernel_error, "The device driver routine is NULL");
		return (status = ERR_NOSUCHFUNCTION);
	}

	// Call the driver function
	status = ops->driverReadDayOfMonth();
	return (status);
}


int kernelRtcReadMonth(void)
{
	// This is a generic routine for invoking the corresponding routine
	// in a Real-Time Clock driver.  It takes no arguments and returns the
	// result from the device driver call

	int status = 0;

	if (!systemRtc)
		return (status = ERR_NOTINITIALIZED);

	// Make sure the device driver 'read month' function has been installed
	if (!ops->driverReadMonth)
	{
		kernelError(kernel_error, "The device driver routine is NULL");
		return (status = ERR_NOSUCHFUNCTION);
	}

	// Call the driver function
	status = ops->driverReadMonth();
	return (status);
}


int kernelRtcReadYear(void)
{
	// This is a generic routine for invoking the corresponding routine
	// in a Real-Time Clock driver.  It takes no arguments and returns the
	// result from the device driver call

	int status = 0;

	if (!systemRtc)
		return (status = ERR_NOTINITIALIZED);

	// Make sure the device driver 'read year' function has been installed
	if (!ops->driverReadYear)
	{
		kernelError(kernel_error, "The device driver routine is NULL");
		return (status = ERR_NOSUCHFUNCTION);
	}

	// Call the driver function
	status = ops->driverReadYear();

	// Y2K COMPLIANCE SECTION :-)

	// Here is where we put in the MOTHER Y2K BUG.  OK, just
	// kidding.  Here is where we avoid the problem, right at the
	// kernel level.  We have to determine what size of value we are
	// getting from the driver.  If we are using the default driver,
	// it returns the same two-digit number that the hardware returns.
	// In this case (or any other where we get a value < 100) we add the
	// century to the value.  We make an inference based on the following:
	// If the year is less than 80, i.e. 1980, we assume we are in
	// the 21st century and add 2000.  Otherwise, we add 1900.

	if (status < 100)
	{
		if (status < 80)
			return (status + 2000);

		else
			return (status + 1900);
	}

	// Must be using a different driver that returns a 4-digit year.
	// Good, I suppose.  Why would someone rewrite this driver?  Dunno.
	// Power to the people, anyway.
	else if (status >= 1980)
		return (status);

	// We have some other gibbled value.  Return an error code.
	else
		return (status = ERR_BADDATA);
}


unsigned kernelRtcUptimeSeconds(void)
{
	// This returns the number of seconds since the RTC driver was
	// initialized.

	unsigned upSeconds = 0;

	if (!systemRtc)
		return (upSeconds = 0);

	upSeconds += (kernelRtcReadSeconds() - startSeconds);
	upSeconds += ((kernelRtcReadMinutes() - startMinutes) * 60);
	upSeconds += ((kernelRtcReadHours() - startHours) * 60 * 60);
	upSeconds += ((kernelRtcReadDayOfMonth() - startDayOfMonth) * 24 * 60 * 60);
	upSeconds += ((kernelRtcReadMonth() - startMonth) * 31 * 24 * 60 * 60);
	upSeconds += ((kernelRtcReadYear() - startYear) * 12 * 31 * 24 * 60 * 60);

	return (upSeconds);
}


unsigned kernelRtcPackedDate(void)
{
	// This function takes a pointer to an unsigned and places the
	// current date in the variable, in a packed format.  It returns 0 on
	// success, negative on error

	// The format for dates is as follows:
	// [year (n bits)] [month (4 bits)] [day (5 bits)]

	unsigned temp = 0;
	unsigned returnedDate = 0;

	if (!systemRtc)
		return (returnedDate = 0);

	// The RTC function for reading the day of the month will return a value
	// between 1 and 31 inclusive.
	temp = kernelRtcReadDayOfMonth();
	// Day is in the least-significant 5 bits.
	temp = (temp & 0x0000001F);
	returnedDate = temp;

	// The RTC function for reading the month will return a value
	// between 1 and 12 inclusive.
	temp = kernelRtcReadMonth();
	// Month is 4 bits in places 5-8
	temp = (temp << 5);
	temp = (temp & 0x000001E0);
	returnedDate |= temp;

	// The year
	temp = kernelRtcReadYear();
	// Year is n bits in places 9->
	temp = (temp << 9);
	temp = (temp & 0xFFFFFE00);
	returnedDate |= temp;

	return (returnedDate);
}


unsigned kernelRtcPackedTime(void)
{
	// This function takes a pointer to an unsigned and places the
	// current time in the variable, in a packed format.  It returns 0 on
	// success, negative on error
	// The format for times is as follows:
	// [hours (5 bits)] [minutes (6 bits)] [seconds (6 bits)]

	unsigned temp = 0;
	unsigned returnedTime = 0;

	if (!systemRtc)
		return (returnedTime = 0);

	// The RTC function for reading seconds will pass us a value between
	// 0 and 59 inclusive.
	temp = kernelRtcReadSeconds();
	// Seconds are in the least-significant 6 bits.
	temp = (temp & 0x0000003F);
	returnedTime = temp;

	// The RTC function for reading minutes will pass us a value between
	// 0 and 59 inclusive.
	temp = kernelRtcReadMinutes();
	// Minutes are six bits in places 6-11
	temp = (temp << 6);
	temp = (temp & 0x00000FC0);
	returnedTime |= temp;

	// The RTC function for reading hours will pass us a value between
	// 0 and 23 inclusive.
	temp = kernelRtcReadHours();
	// Hours are five bits in places 12-16
	temp = (temp << 12);
	temp = (temp & 0x0003F000);
	returnedTime |= temp;

	return (returnedTime);
}


int kernelRtcDayOfWeek(unsigned day, unsigned month, unsigned year)
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


int kernelRtcDateTime(struct tm *timeStruct)
{
	// This function will fill out a 'tm' structure according to the current
	// date and time.  This function is just a convenience, as all of the
	// functionality here could be reproduced with other calls

	int status = 0;

	if (!systemRtc)
		return (status = ERR_NOTINITIALIZED);

	// Make sure our time struct isn't NULL
	if (!timeStruct)
		return (status = ERR_NULLPARAMETER);

	timeStruct->tm_sec = kernelRtcReadSeconds();
	timeStruct->tm_min = kernelRtcReadMinutes();
	timeStruct->tm_hour = kernelRtcReadHours();
	timeStruct->tm_mday = (kernelRtcReadDayOfMonth() - 1);
	timeStruct->tm_mon = (kernelRtcReadMonth() - 1);
	timeStruct->tm_year = (kernelRtcReadYear() - 1900);
	timeStruct->tm_wday = ((kernelRtcDayOfWeek((timeStruct->tm_mday + 1),
		(timeStruct->tm_mon + 1), (timeStruct->tm_year + 1900)) + 1) % 7);
	timeStruct->tm_yday = 0;  // unimplemented
	timeStruct->tm_isdst = 0; // We don't know anything about DST yet

	// Return success
	return (status = 0);
}


int kernelRtcDateTime2Tm(unsigned rtcPackedDate, unsigned rtcPackedTime,
	struct tm *timeStruct)
{
	// This function will fill out a 'struct tm' structure using our arbitrary
	// RTC 'packed date' and 'packed time' formats

	int status = 0;

	// Make sure our time struct isn't NULL
	if (!timeStruct)
		return (status = ERR_NULLPARAMETER);

	timeStruct->tm_sec = (rtcPackedTime & 0x0000003F);
	timeStruct->tm_min = ((rtcPackedTime & 0x00000FC0) >> 6);
	timeStruct->tm_hour = ((rtcPackedTime & 0x0003F000) >> 12);
	timeStruct->tm_mday = ((rtcPackedDate & 0x0000001F) - 1);
	timeStruct->tm_mon = (((rtcPackedDate & 0x000001E0) >> 5) - 1);
	timeStruct->tm_year = (((rtcPackedDate & 0xFFFFFE00) >> 9) - 1900);
	timeStruct->tm_wday = ((kernelRtcDayOfWeek((timeStruct->tm_mday + 1),
		(timeStruct->tm_mon + 1), (timeStruct->tm_year + 1900)) + 1) % 7);
	timeStruct->tm_yday = 0;  // unimplemented
	timeStruct->tm_isdst = 0; // We don't know anything about DST yet

	// Return success
	return (status = 0);
}

