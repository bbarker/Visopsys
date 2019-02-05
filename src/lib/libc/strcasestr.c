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
//  strcasestr.c
//

// This is the standard "strcasestr" function, as found in standard C libraries

#include <string.h>


char *strcasestr(const char *s1, const char *s2)
{
	// The strcasestr() function finds the first occurrence of the substring s2
	// in the string s1, ignoring case.  The terminating `\0' characters are not
	// compared.  The strcasestr() function returns a pointer to the beginning of
	// the substring, or NULL if the substring is not found.

	int count = 0;
	char *ptr = NULL;
	int s1_length = strlen(s1);
	int s2_length = strlen(s2);

	ptr = (char *) s1;

	for (count = 0; count < s1_length; count ++)
	{
		if (!strncasecmp(ptr, s2, s2_length))
			return (ptr);
		else
			ptr++;
	}

	// Not found
	return (ptr = NULL);
}

