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
//  vshPasswordPrompt.c
//

// This contains some useful functions written for the shell

#include <sys/vsh.h>
#include <sys/api.h>
#include <stdio.h>


_X_ void vshPasswordPrompt(const char *prompt, char *buffer)
{
	// Desc: Produces a text-mode prompt for the user to enter a password.  The prompt message is the first parameter, and a buffer to contain the result is the second parameter.

	int count = 0;
	int okay = 0;

	// Turn keyboard echo off
	textInputSetEcho(0);

	while (!okay)
	{
		printf("%s", prompt);

		// This loop grabs characters
		for (count = 0; count < 17; count ++)
		{
			buffer[count] = getchar();

			if (buffer[count] == (unsigned char) 10)
			{
				buffer[count] = '\0';
				printf("\n");
				okay = 1;
				break;
			}

			else if (count >= 16)
			{
				printf("\nThat password is too long.\n");
				buffer[0] = '\0';
				break;
			}

			else if (buffer[count] == (unsigned char) 8)
			{
				buffer[count] = '\0';
				if (count > 0)
				{
					textBackSpace();
					count -= 2;
				}
				else
					count -= 1;
				continue;
			}

			else
				printf("*");
		}
	}

	// Echo back on
	textInputSetEcho(1);

	buffer[16] = '\0';
	return;
}

