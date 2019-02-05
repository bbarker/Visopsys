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
//  readline.c
//

// This is a simplified version of the readline function from GNU.  The
// description from the man page is exceprted as follows:
//
// readline will read a line from the terminal and return it,
// using prompt as a prompt.  If prompt is null, no prompt is
// issued.  The line returned is allocated with malloc(3), so
// the  caller must free it when finished.  The line returned
// has the final newline removed, so only  the  text  of  the
// line remains.
//
// readline returns the text of the line read.  A blank  line
// returns  the  empty  string.   If EOF is encountered while
// reading a line, and the line is empty, NULL  is  returned.
// If  an EOF is read with a non-empty line, it is treated as
// a newline.

#include <readline.h>
#include <stdlib.h>
#include <errno.h>
#include <sys/api.h>


char *readline(const char *prompt)
{
	char *returnString = NULL;
	int inputCount = 0;
	char oneChar;

	if (visopsys_in_kernel)
	{
		errno = ERR_BUG;
		return (returnString = NULL);
	}

	// Allocate an array to hold the data
	returnString = malloc(MAXSTRINGLENGTH);
	if (!returnString)
		return (returnString);

	// Output the prompt, if there is any
	if (prompt)
		textPrint(prompt);

	while (1)
	{
		// Get one char
		while (!textInputCount())
			multitaskerYield();
		textInputGetc(&oneChar);

		// Is it a newline?  If so, quit
		if (oneChar == '\n')
			break;

		// Put it into the array
		returnString[inputCount++] = oneChar;
	}

	// Put a NULL at the end
	returnString[inputCount] = '\0';

	return (returnString);
}

