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
//  gets.c
//

// This is the standard "gets" function, as found in standard C libraries

#include <stdio.h>
#include <errno.h>
#include <sys/api.h>


char *gets(char *s)
{
	// gets() reads a line from stdin into the buffer pointed to by s until
	// either a terminating newline or EOF, which it replaces with '\0'.
	// No check for buffer overrun is performed.

	// The algorithm is to run in a loop until a newline or EOF is encountered.
	// Since we are reading from the text input stream, we need to continually
	// check whether there is any input before we call to get the character.
	// If there is no input, yield the current time slice back to the
	// scheduler.

	int status = 0;
	int read = 0;
	char c = '\0';

	if (visopsys_in_kernel)
	{
		errno = ERR_BUG;
		return (NULL);
	}

	while (1)
	{
		// Is there anything in the input stream?
		if (!textInputCount())
		{
			// Nothing to process.  Yield.
			multitaskerYield();
			continue;
		}

		// Always terminate with NULL
		s[read] = NULL;

		// Get a character from the text input stream
		status = textInputGetc(&c);
		if (status < 0)
		{
			errno = status;
			return (NULL);
		}

		// We have a character.

		// Is it an EOF or newline?  That would mean we're finished
		if ((c == EOF) || (c == '\n'))
		{
			textNewline();

			if (!read)
				return (NULL);
			else
				return (s);
		}

		// It's some other character.  Copy it into the target string.
		else
		{
			s[read++] = c;
			textPutc(c);
			continue;
		}
	}
}

