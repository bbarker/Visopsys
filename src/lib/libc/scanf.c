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
//  scanf.c
//

// This is the standard "scanf" function, as found in standard C libraries

#include <errno.h>
#include <readline.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/api.h>
#include <sys/cdefs.h>


int scanf(const char *format, ...)
{
	va_list list;
	int matchItems = 0;
	char *input = NULL;

	if (visopsys_in_kernel)
		return (errno = ERR_BUG);

	// Read a line of input
	input = readline(NULL);
	if (!input)
		// We matched zero items
		return (matchItems = 0);

	// Initialize the argument list
	va_start(list, format);

	// Now assign the input values based on the input data and the format
	// string
	matchItems = _fmtinpt(input, format, list);

	va_end(list);

	// This gets malloc'd by readline, but we're finished with it.
	free(input);

	return (matchItems);
}

