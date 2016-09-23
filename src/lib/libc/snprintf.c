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
//  snprintf.c
//

// This is the standard "snprintf" function, as found in standard C libraries

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <sys/cdefs.h>


int snprintf(char *output, size_t size, const char *format, ...)
{
	// This function will construct a single string out of the format
	// string and arguments that are passed, up to 'size' bytes.  Returns the
	// number of characters copied to the output string.

	va_list list;
	int len = 0;

	memset(output, 0, size);

	// Initialize the argument list
	va_start(list, format);

	// Fill out the output line based on
	len = _xpndfmt(output, size, format, list);

	va_end(list);

	// Return the number of characters we wrote to the string
	return (len);
}

