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
//  strtok.c
//

// This is the standard "strtok" function, as found in standard C libraries

#include <string.h>

static char *saveptr = NULL;


char *strtok(char *string, const char *delim)
{
	// The strtok() function parses a string into a sequence of tokens.  The
	// string to be parsed is passed to the first call of the function along
	// with a second string containing delimiter characters.

	char *token = NULL;

	// Check params
	if (!string)
	{
		if (!saveptr)
			return (saveptr = NULL);
	}
	else
	{
		// This is the first call with this string
		saveptr = string;
	}

	if (!delim)
		// We need delimiters
		return (saveptr = NULL);

	// Skip any leading delimiter characters.
	while (saveptr[0] && strchr(delim, saveptr[0]))
		saveptr += 1;

	if (!saveptr[0])
		// Nothing left
		return (saveptr = NULL);

	// Remember the start of the token.  This will be our return value.
	token = saveptr;

	// Move our save pointer along to the next delimiter or NULL
	while (saveptr[0] && !strchr(delim, saveptr[0]))
		saveptr += 1;

	if (saveptr[0])
	{
		// Insert a NULL at the delimiter
		saveptr[0] = '\0';

		// Move to the next char.  We don't care what it is (NULL, delimiter,
		// etc) because the next call will deal with that.
		saveptr += 1;
	}

	return (token);
}

