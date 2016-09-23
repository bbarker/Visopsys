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
//  strcat.c
//

// This is the standard "strcat" function, as found in standard C libraries

#include <string.h>
#include <errno.h>


char *strcat(char *destString, const char *sourceString)
{
	int count1, count2;

	// Find the end of the first String
	for (count1 = 0; count1 < MAXSTRINGLENGTH; )
	{
		if (destString[count1] == (char) NULL) break;
			else count1++;
	}

	// If this is true, then we probably have an unterminated string
	// constant.  Checking for a string that exceeds MAXSTRINGLENGTH will
	// help to prevent the routine from running off too far into memory.
	if (count1 >= MAXSTRINGLENGTH)
	{
		errno = ERR_BOUNDS;
		return (destString = NULL);
	}

	// Now copy the source string into the dest until the source is a
	// NULL character.
	for (count2 = 0; count2 < MAXSTRINGLENGTH; )
	{
		destString[count1] = sourceString[count2];

		if (sourceString[count2] == (char) NULL)
			break;

		else
			count1++; count2++;
	}

	return (destString);
}

