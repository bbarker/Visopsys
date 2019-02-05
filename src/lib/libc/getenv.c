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
//  getenv.c
//

// This is the standard "getenv" function, as found in standard C libraries

#include <errno.h>
#include <stdlib.h>
#include <sys/api.h>

static char *value = NULL;


char *getenv(const char *variable)
{
	// Get a pointer to the value of an environment variable, or NULL if it
	// isn't set.

	int status = 0;

	if (visopsys_in_kernel)
	{
		errno = ERR_BUG;
		return (NULL);
	}

	if (!variable)
	{
		errno = ERR_NULLPARAMETER;
		return (NULL);
	}

	if (!value)
		value = malloc(MAXSTRINGLENGTH);

	if (value)
	{
		status = environmentGet(variable, value, MAXSTRINGLENGTH);
		if (status < 0)
		{
			errno = status;
			return (NULL);
		}
	}

	return (value);
}

