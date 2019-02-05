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
//  vshMakeAbsolutePath.c
//

// This contains some useful functions written for the shell

#include <string.h>
#include <errno.h>
#include <sys/vsh.h>
#include <sys/api.h>


_X_ void vshMakeAbsolutePath(const char *orig, char *new)
{
	// Desc: Turns a filename, specified by 'orig', into an absolute pathname 'new'.  This basically just amounts to prepending the name of the current directory (plus a '/') to the supplied name.  'new' must be a buffer large enough to hold the entire filename.

	int status = 0;

	// Check params
	if (!orig || !new)
	{
		errno = ERR_NULLPARAMETER;
		return;
	}

	if ((orig[0] != '/') && (orig[0] != '\\'))
	{
		// Get the current directory
		status = multitaskerGetCurrentDirectory(new, MAX_PATH_LENGTH);
		if (status < 0)
		{
			errno = status;
			return;
		}

		if ((new[strlen(new) - 1] != '/') && (new[strlen(new) - 1] != '\\'))
			strcat(new, "/");

		strcat(new, orig);
	}
	else
	{
		strcpy(new, orig);
	}

	return;
}

