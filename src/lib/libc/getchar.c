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
//  getchar.c
//

// This is the standard "getchar" function, as found in standard C libraries

#include <stdio.h>
#include <errno.h>
#include <sys/api.h>


int getchar(void)
{
	// getchar() is equivalent to getc(stdin).

	int status = 0;
	char c = '\0';

	if (visopsys_in_kernel)
	{
		errno = ERR_BUG;
		return (EOF);
	}

	// Get a character from the text input stream
	status = textInputGetc(&c);
	if (status < 0)
	{
		errno = status;
		return (EOF);
	}

	return ((int) c);
}

