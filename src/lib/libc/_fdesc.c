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
//  _fdesc.c
//

// These internal functions allocate, find, and free per-process UNIX/POSIX-
// style integer file descriptor numbers for the standard C libraries

#include <stdio.h>
#include <stdlib.h>
#include <sys/api.h>
#include <sys/cdefs.h>
#include <sys/errors.h>

#define FDS_PER_ALLOC	16

typedef struct {
	fileDescType type;
	void *data;
	int free;

} fileDesc;

static fileDesc *fds = NULL;
static int numFds = 0;


static int initialize(void)
{
	fds = calloc(FDS_PER_ALLOC, sizeof(fileDesc));
	if (!fds)
		return (ERR_MEMORY);

	numFds = FDS_PER_ALLOC;

	// Set up the default ones

	fds[STDIN_FILENO].type = filedesc_textstream;
	fds[STDIN_FILENO].data = stdin;
	numFds += 1;

	fds[STDOUT_FILENO].type = filedesc_textstream;
	fds[STDOUT_FILENO].data = stdout;
	numFds += 1;

	fds[STDERR_FILENO].type = filedesc_textstream;
	fds[STDERR_FILENO].data = stderr;
	numFds += 1;

	return (0);
}


static int expand(void)
{
	fileDesc *newFds = NULL;

	newFds = calloc((numFds + FDS_PER_ALLOC), sizeof(fileDesc));
	if (!newFds)
		return (ERR_MEMORY);

	memcpy(newFds, fds, (numFds * sizeof(fileDesc)));

	fds = newFds;
	numFds += FDS_PER_ALLOC;

	return (0);
}


/////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////
//
//  Below here, the functions are exported for external use
//
/////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////

int _fdalloc(fileDescType type, void *data, int free)
{
	// Allocate the first available entry in our descriptor table

	int status = 0;
	int count;

	if (visopsys_in_kernel)
		return (status = ERR_BUG);

	// First call?
	if (!fds)
	{
		status = initialize();
		if (status < 0)
			return (status);
	}

	// Search for the first unused slot, expanding the memory space if
	// necessary.
	for (count = 0; count < numFds; count ++)
	{
		if (!fds[count].type && !fds[count].data)
		{
			fds[count].type = type;
			fds[count].data = data;
			fds[count].free = free;
			return (status = count);

			if (count >= (numFds - 1))
			{
				// Need to allocate more
				status = expand();
				if (status < 0)
					return (status);
			}
		}
	}

	return (status = ERR_NOFREE);
}


int _fdget(int fd, fileDescType *type, void **data)
{
	// Return info from an entry in our descriptor table

	if (visopsys_in_kernel)
		return (ERR_BUG);

	// Within bounds?
	if (!fds || (fd < 0) || (fd >= numFds))
		return (ERR_BOUNDS);

	// Allocated?
	if (!fds[fd].type && !fds[fd].data)
		return (ERR_NOSUCHENTRY);

	if (type)
		*type = fds[fd].type;
	if (data)
		*data = fds[fd].data;

	return (0);
}


int _fdset_type(int fd, fileDescType type)
{
	// Set an entry in our descriptor table

	if (visopsys_in_kernel)
		return (ERR_BUG);

	// Within bounds?
	if (!fds || (fd < 0) || (fd >= numFds))
		return (ERR_BOUNDS);

	fds[fd].type = type;

	return (0);
}


int _fdset_data(int fd, void *data, int free)
{
	// Set an entry in our descriptor table

	if (visopsys_in_kernel)
		return (ERR_BUG);

	// Within bounds?
	if (!fds || (fd < 0) || (fd >= numFds))
		return (ERR_BOUNDS);

	fds[fd].data = data;
	fds[fd].free = free;

	return (0);
}


void _fdfree(int fd)
{
	// Free (clear) an entry in our descriptor table

	if (visopsys_in_kernel)
		return;

	// Within bounds?
	if (!fds || (fd < 0) || (fd >= numFds))
		return;

	if (fds[fd].data && fds[fd].free)
		free(fds[fd].data);

	// Clear it
	memset(&fds[fd], 0, sizeof(fileDesc));
}

