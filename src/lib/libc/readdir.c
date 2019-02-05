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
//  readdir.c
//

// This is the standard "readdir" function, as found in standard C libraries

#include <dirent.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/api.h>


struct dirent *readdir(DIR *dir)
{
	// This function reads one entry from a 'directory stream'.  In Visopsys,
	// this is an iterator.

	int status = 0;
	struct dirent *entry = NULL;

	if (visopsys_in_kernel)
	{
		errno = ERR_BUG;
		return (entry = NULL);
	}

	// Check params
	if (!dir)
	{
		errno = ERR_NULLPARAMETER;
		return (entry = NULL);
	}

	// Any more entries?  The first one is pre-loaded by opendir(), and the
	// next one is post-loaded by this function.
	if (!dir->f.name[0])
		return (entry = NULL);

	// Get memory for the entry
	if (!dir->entry)
	{
		dir->entry = calloc(1, sizeof(struct dirent));
		if (!dir->entry)
		{
			errno = ERR_MEMORY;
			return (entry = NULL);
		}
	}

	// Construct the entry
	entry = (struct dirent *) dir->entry;
	entry->d_ino = 1;	// bogus
	entry->d_type = dir->f.type;
	strncpy(entry->d_name, dir->f.name, MAX_NAME_LENGTH);
	entry->d_name[MAX_NAME_LENGTH - 1] = '\0';

	// Get the next file, if applicable
	status = fileNext(dir->name, &dir->f);
	if (status < 0)
		memset(&dir->f, 0, sizeof(file));

	return (entry);
}

