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
//  rewinddir.c
//

// This is the standard "rewinddir" function, as found in standard C libraries

#include <dirent.h>
#include <errno.h>
#include <stdlib.h>
#include <sys/api.h>


void rewinddir(DIR *dir)
{
	// This function resets a 'directory stream' back to the first entry.  In
	// Visopsys, this is an iterator.

	if (visopsys_in_kernel)
	{
		errno = ERR_BUG;
		return;
	}

	// Check params
	if (!dir)
	{
		errno = ERR_NULLPARAMETER;
		return;
	}

	// Get the first file, if applicable
	if (fileFirst(dir->name, &dir->f) < 0)
		memset(&dir->f, 0, sizeof(file));
}

