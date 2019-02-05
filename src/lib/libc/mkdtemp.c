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
//  mkdtemp.c
//

// This is the standard "mkdtemp" function, as found in standard C libraries

#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <sys/api.h>
#include <sys/stat.h>


char *mkdtemp(char *template)
{
	// Creates a unique temporary directory (named according to 'template'),
	// and returns a pointer to the modified directory name string on success,
	// or else NULL.  The last 6 characters of 'template' must be XXXXXX, and
	// the string must be writable.

	char *dirName = NULL;
	size_t len = 0;
	char *string = NULL;
	file tmp;
	int count;

	if (visopsys_in_kernel)
	{
		errno = ERR_BUG;
		return (dirName = NULL);
	}

	// Check params
	if (!template)
	{
		errno = ERR_NULLPARAMETER;
		return (dirName = NULL);
	}

	// Check more

	len = strlen(template);

	if (len < 6)
	{
		errno = ERR_RANGE;
		return (dirName = NULL);
	}

	// Get a pointer to the last 6 XXXXXX characters
	string = (template + (len - 6));

	if (strcmp(string, "XXXXXX"))
	{
		errno = ERR_BADDATA;
		return (dirName = NULL);
	}

	while (1)
	{
		// Create a random string of letters
		for (count = 0; count < 6; count ++)
			string[count] = ('a' + randomFormatted(0, 25));

		dirName = template;

		// Make sure it doesn't exist
		if (fileFind(template, &tmp) < 0)
		{
			// Create the directory
			if (mkdir(template, 0 /* default mode */) < 0)
				dirName = NULL;
			break;
		}
	}

	return (dirName);
}

