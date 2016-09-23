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
//  strspn.c
//

// This is the standard "strspn" function, as found in standard C libraries

#include <string.h>


size_t strspn(const char *s1, const char *s2)
{
	// The strspn() function calculates the length of the initial segment of
	// s1 which consists entirely of characters in s2.

	int match = 0;
	int s1_length = strlen(s1);
	int s2_length = strlen(s2);
	int count;

	for (count = 0; ((count < s1_length) && (count < s2_length)); count ++)
	{
		if (s1[count] != s2[count])
			break;

		match++;
	}

	return (match);
}

