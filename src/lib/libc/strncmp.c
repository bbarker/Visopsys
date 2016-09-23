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
//  strncmp.c
//

// This is the standard "strncmp" function, as found in standard C libraries

#include <string.h>


int strncmp(const char *s1, const char *s2, size_t length)
{
	// The strncmp() function compares the first (at most) n characters of s1
	// and s2.  It returns an integer less than, equal to, or greater than zero
	// if s1 is found, respectively, to be less than, to match, or be greater
	// than s2.

	size_t count = 0;

	// The spec doesn't really make it clear what to do with NULL parameters here
	if (!s1 || !s2)
	{
		if (!s1 && s2)
			return -1;
		if (s1 && !s2)
			return (1);
		else
			// Both NULL.  Fine.
			return (0);
	}

	for (count = 0; ((count < MAXSTRINGLENGTH) && (count < length)); count ++)
	{
		if ((s1[count] == '\0') && (s2[count] == '\0'))
			// We're stopping early and the strings are identical
			return (0);

		else if (s1[count] != s2[count])
		{
			// The strings stop matching here.  Is the s1 character in question
			// 'less than' or 'greater than' the s2 character?
			return ((s1[count] > s2[count])? 1 : -1);
		}
	}

	// If we fall through to here, we matched as many as we could
	return (0);
}

