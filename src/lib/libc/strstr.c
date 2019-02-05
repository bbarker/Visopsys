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
//  strstr.c
//

// This is the standard "strstr" function, as found in standard C libraries

#include <string.h>


char *strstr(const char *s1, const char *s2)
{
	// The strstr() function finds the first occurrence of the substring s2
	// in the string s1.  The terminating `\0' characters are not compared.
	// The strstr() function returns a pointer to the beginning of the
	// substring, or NULL if the substring is not found.

	int count = 0;
	int s2Len = strlen(s2);

	for (count = 0; s1[0]; count ++)
	{
		if (!strncmp(s1, s2, s2Len))
			return ((char *) s1);
		else
			s1 ++;
	}

	// Not found
	return (NULL);
}

