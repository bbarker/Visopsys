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
//  strtoull.c
//

// This is the standard "strtoull" function, as found in standard C libraries

#include <stdio.h>
#include <errno.h>
#include <sys/cdefs.h>


unsigned long long int strtoull(const char *string, char **endString, int base)
{
	unsigned long long int ret = 0;
	int consumed = 0;

	// Check params.  endString can be NULL.
	if (!string)
	{
		errno = ERR_NULLPARAMETER;
		return (0);
	}

	ret = (unsigned long long int) _str2num(string, base, 0 /* unsigned */,
		&consumed);

	if (endString)
		*endString = ((char *) string + consumed);

	return (ret);
}

