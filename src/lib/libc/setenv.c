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
//  setenv.c
//

// This is the standard "setenv" function, as found in standard C libraries

#include <errno.h>
#include <stdlib.h>
#include <sys/api.h>


int setenv(const char *variable, const char *value, int overWrite)
{
	// Set the value of an environment variable, if it is not already set, or
	// if overWrite is non-zero.

	int status = 0;
	char *tmpValue = NULL;

	if (visopsys_in_kernel)
	{
		errno = ERR_BUG;
		return (-1);
	}

	if (!variable || !value)
	{
		errno = ERR_NULLPARAMETER;
		return (-1);
	}

	tmpValue = malloc(MAXSTRINGLENGTH);
	if (!tmpValue)
	{
		errno = ERR_MEMORY;
		return (-1);
	}

	status = environmentGet(variable, tmpValue, MAXSTRINGLENGTH);

	free(tmpValue);

	if ((status < 0) || overWrite)
	{
		status = environmentSet(variable, value);
		if (status >= 0)
		{
			return (0);
		}
		else
		{
			errno = status;
			return (-1);
		}
	}

	return (0);
}

