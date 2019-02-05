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
//  opendir.c
//

// This is the standard "opendir" function, as found in standard C libraries

#include <dirent.h>
#include <errno.h>
#include <stdlib.h>
#include <sys/api.h>


DIR *opendir(const char *dirName)
{
	// This function opens a 'directory stream'.  In Visopsys, this is an
	// iterator.

	int status = 0;
	DIR *dir = NULL;

	if (visopsys_in_kernel)
	{
		status = ERR_BUG;
		goto out;
	}

	// Check params
	if (!dirName)
	{
		status = ERR_NULLPARAMETER;
		goto out;
	}

	// Get memory for the directory stream
	dir = calloc(1, sizeof(DIR));
	if (!dir)
	{
		status = ERR_MEMORY;
		goto out;
	}

	// Call the "find file" function to see whether the directory exists
	status = fileFind(dirName, &dir->f);
	if (status < 0)
		goto out;

	dir->name = strdup(dirName);
	if (!dir->name)
	{
		status = ERR_MEMORY;
		goto out;
	}

	// Get the first file, if applicable
	status = fileFirst(dirName, &dir->f);
	if (status < 0)
		memset(&dir->f, 0, sizeof(file));

	status = 0;

out:
	if (status < 0)
	{
		if (dir)
		{
			closedir(dir);
			dir = NULL;
		}

		errno = status;
	}

	return (dir);
}

