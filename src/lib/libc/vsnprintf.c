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
//  vsnprintf.c
//

// This is the standard "vsnprintf" function, as found in standard C libraries

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/cdefs.h>


int vsnprintf(char *output, size_t size, const char *format, va_list list)
{
	// This function will construct a single string out of the format
	// string and arguments that are passed, up to 'size' bytes.  Returns the
	// number of characters copied to the output string.

	int len = 0;

	size = min(size, MAXSTRINGLENGTH);

	memset(output, 0, size);

	// Fill out the output line based on
	len = _xpndfmt(output, size, format, list);

	// Return the number of characters we wrote to the string
	return (len);
}

