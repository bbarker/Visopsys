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
//  fgets.c
//

// This is the standard "fgets" function, as found in standard C libraries

#include <stdio.h>
#include <stdlib.h>
#include <readline.h>
#include <errno.h>
#include <sys/api.h>


char *fgets(char *string, int size, FILE *theStream)
{
	// fgets() reads a line from the file stream into the buffer pointed to by
	// string until either a terminating newline or EOF, which it replaces with
	// '\0'.  No check for buffer overrun is performed.

	int status = 0;
	char *tmpString = NULL;

	if (visopsys_in_kernel)
	{
		errno = ERR_BUG;
		return (string = NULL);
	}

	if (theStream == stdin)
	{
		tmpString = readline(NULL);
		if (!tmpString)
		{
			errno = ERR_IO;
			return (string = NULL);
		}

		strncpy(string, tmpString, size);
		free(tmpString);
	}
	else
	{
		status = fileStreamReadLine(theStream, (size - 1), string);
		if (status <= 0)
		{
			errno = status;
			return (string = NULL);
		}
	}

	string[size - 1] = '\0';

	return (string);
}

