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
//  strcasecmp.c
//

// This is the standard "strcasecmp" function, as found in standard C
// libraries

#include <string.h>
#include <errno.h>


int strcasecmp(const char *s1, const char *s2)
{
	// The strcasecmp() function compares the two strings s1 and s2,
	// ignoring the case of the characters.  It returns an integer less than,
	// equal to, or greater than zero if s1 is found, respectively, to be less
	// than, to match, or be greater than s2.

	int count = 0;

	for (count = 0; count < MAXSTRINGLENGTH; count ++)
	{
		if ((s1[count] == '\0') && (s2[count] == '\0'))
			// The strings match
			return (0);

		// Is the ascii code a lowercase alphabet character?
		else if ((s1[count] >= (char) 97) &&
			(s1[count] <= (char) 122) &&
			(s2[count] == (s1[count] - (char) 32)))
		{
			// We call it a match
			continue;
		}

		// Is the ascii code an uppercase alphabet character?
		else if ((s1[count] >= (char) 65) &&
			(s1[count] <= (char) 90) &&
			(s2[count] == (s1[count] + (char) 32)))
		{
			// We call it a match
			continue;
		}

		else if (s1[count] != s2[count])
		{
			// The strings stop matching here.

			// Is one of the characters a NULL character?  If so, that string
			// is 'less than' the other
			if (s1[count] == '\0')
				return (-1);
			else if (s2[count] == '\0')
				return (1);

			// Otherwise, is the s1 character in question 'less than' or
			// 'greater than' the s2 character?  This will return a positive
			// number if the s1 character is greater than s2, else a negative
			// number.
			return (s1[count] - s2[count]);
		}
	}

	// EEK, we have an overflow, but the strings match up to this point.
	// I'm not sure what the 'correct' thing to do is, but return 'equal'
	// anyway, whilst setting errno
	errno = ERR_BOUNDS;
	return (0);
}

