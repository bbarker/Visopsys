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
//  strncasecmp.c
//

// This is the standard "strncasecmp" function, as found in standard C
// libraries

#include <string.h>


int strncasecmp(const char *s1, const char *s2, size_t length)
{
	int result = 0;

	// Go through the strings, counting as we go.  If we get to the end, or
	// "length" and everything matches, we return 0.  Otherwise, if the strings
	// match partially, we return the count at which they diverge.  If they
	// don't match at all, we return -1

	for (result = 0; ((result < MAXSTRINGLENGTH) &&
		((unsigned) result < length)); result ++)
	{
		if ((s1[result] == '\0') && (s2[result] == '\0'))
			return (result = 0);

		// Is the ascii code a lowercase alphabet character?
		else if ((s1[result] >= (char) 97) &&
			(s1[result] <= (char) 122) &&
			(s2[result] == (s1[result] - (char) 32)))
		{
			// We call it a match
			continue;
		}

		// Is the ascii code an uppercase alphabet character?
		else if ((s1[result] >= (char) 65) &&
			(s1[result] <= (char) 90) &&
			(s2[result] == (s1[result] + (char) 32)))
		{
			// We call it a match
			continue;
		}
		else if (s1[result] != s2[result])
		{
			if (!result)
				return (result = -1);

			else
				return (result);
		}
	}

	// If we fall through to here, we matched as many as we could
	return (result = 0);
}

