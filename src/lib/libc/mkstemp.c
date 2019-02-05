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
//  mkstemp.c
//

// This is the standard "mkstemp" function, as found in standard C libraries

#include <stdlib.h>
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <sys/api.h>


int mkstemp(char *template)
{
	// Opens a unique temporary file (named according to 'template') in read/
	// write mode, and returns the file descriptor.  The last 6 characters of
	// 'template' must be XXXXXX, and the string must be writable.

	int fd = 0;
	size_t len = 0;
	char *string = NULL;
	file tmp;
	int count;

	if (visopsys_in_kernel)
	{
		errno = ERR_BUG;
		return (fd = -1);
	}

	// Check params
	if (!template)
	{
		errno = ERR_NULLPARAMETER;
		return (fd = -1);
	}

	// Check more

	len = strlen(template);

	if (len < 6)
	{
		errno = ERR_RANGE;
		return (fd = -1);
	}

	// Get a pointer to the last 6 XXXXXX characters
	string = (template + (len - 6));

	if (strcmp(string, "XXXXXX"))
	{
		errno = ERR_BADDATA;
		return (fd = -1);
	}

	while (1)
	{
		// Create a random string of letters
		for (count = 0; count < 6; count ++)
			string[count] = ('a' + randomFormatted(0, 25));

		// Make sure it doesn't exist
		if (fileFind(template, &tmp) < 0)
		{
			// Create the file in read/write mode
			fd = open(template, (O_RDWR | O_CREAT));
			break;
		}
	}

	return (fd);
}

