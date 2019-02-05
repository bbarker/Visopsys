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
//  strncpy.c
//

// This is the standard "strncpy" function, as found in standard C libraries

// Here is the description from the GNU man pages:
// The strncpy() function is similar [to strcpy], except that not more
// than n bytes of src are copied. Thus, if there is no null byte among the
// first n bytes of src, the result wil not be null-terminated.

#include <string.h>
#include <errno.h>


char *strncpy(char *destString, const char *sourceString, size_t maxLength)
{
	unsigned count;

	// Make sure neither of the pointers are NULL
	if ((destString == (char *) NULL) || (sourceString == (char *) NULL))
	{
		errno = ERR_NULLPARAMETER;
		return (destString = NULL);
	}

	if (maxLength > MAXSTRINGLENGTH)
		maxLength = MAXSTRINGLENGTH;

	for (count = 0; count < maxLength; count ++)
	{
		destString[count] = sourceString[count];

		if (sourceString[count] == '\0')
			break;
	}

	// If this is true, then we probably have an unterminated string
	// constant.  Checking for a string that exceeds MAXSTRINGLENGTH will
	// help to prevent the function from running off too far into memory.
	if (count >= MAXSTRINGLENGTH)
	{
		errno = ERR_BOUNDS;
		return (destString = NULL);
	}

	// Return success
	return (destString);
}

