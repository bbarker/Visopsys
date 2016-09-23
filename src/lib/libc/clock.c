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
//  clock.c
//

// This is the standard "clock" function, as found in standard C libraries

#include <time.h>
#include <errno.h>
#include <sys/api.h>


clock_t clock(void)
{
	// The clock() function returns an approximation of processor time used
	// by the program.  The value returned is the CPU time used so far as a
	// clock_t; to get the number of seconds used, divide by CLOCKS_PER_SEC.
	// POSIX requires that CLOCKS_PER_SEC equals 1000000 independent of the
	// actual resolution.  The C standard allows for arbitrary values at the
	// start of the program; take the difference between the value returned
	// from a call to clock() at the start of the program and the end to get
	// maximum portability.

	int status = 0;
	clock_t clk = 0;

	if (visopsys_in_kernel)
	{
		errno = ERR_BUG;
		return (clk = 0);
	}

	// Call the api function to get the current CPU time
	status = multitaskerGetProcessorTime(&clk);
	if (status < 0)
		errno = status;

	return (clk);
}

