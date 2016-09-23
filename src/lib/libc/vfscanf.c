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
//  vfscanf.c
//

// This is the standard "vfscanf" function, as found in standard C libraries

#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <sys/api.h>
#include <sys/cdefs.h>


int vfscanf(FILE *theStream, const char *format, va_list list)
{
	int status = 0;
	int matchItems = 0;
	char input[MAXSTRINGLENGTH];

	if (visopsys_in_kernel)
		return (errno = ERR_BUG);

	if ((theStream == stdout) || (theStream == stderr))
	{
		status = vscanf(format, list);
		return (status);
	}

	// Read a line of input
	status = fileStreamReadLine(theStream, MAXSTRINGLENGTH, input);
	if (status <= 0)
	{
		// We matched zero items
		errno = status;
		return (matchItems = 0);
	}

	// Now assign the input values based on the input data and the format
	// string
	matchItems = _fmtinpt(input, format, list);

	return (matchItems);
}

