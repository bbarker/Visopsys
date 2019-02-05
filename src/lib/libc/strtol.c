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
//  strtol.c
//

// This is the standard "strtol" function, as found in standard C libraries

#include <stdio.h>
#include <errno.h>
#include <sys/cdefs.h>


long int strtol(const char *string, char **endString, int base)
{
	long int ret = 0;
	int consumed = 0;

	// Check params.  endString can be NULL.
	if (!string)
	{
		errno = ERR_NULLPARAMETER;
		return (0);
	}

	ret = (long int) _str2num(string, base, 1 /* signed */, &consumed);

	if (endString)
		*endString = ((char *) string + consumed);

	return (ret);
}

