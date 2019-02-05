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
//  ftruncate.c
//

// This is the standard "ftruncate" function, as found in standard C libraries

#include <unistd.h>
#include <errno.h>
#include <sys/api.h>
#include <sys/cdefs.h>


int ftruncate(int fd, off_t length)
{
	// The ftruncate function causes the file referenced by the supplied file
	// descriptor to be set to the requested length.  The file must be open
	// for writing.  If the file was previously larger than this size, the
	// extra data is lost.  If the file was previously smaller, the file is
	// expanded.

	int status = 0;
	fileDescType type = filedesc_unknown;
	fileStream *theStream = NULL;

	if (visopsys_in_kernel)
	{
		errno = ERR_BUG;
		return (status = -1);
	}

	// Look up the file descriptor
	status = _fdget(fd, &type, (void **) &theStream);
	if (status < 0)
	{
		errno = status;
		return (status = -1);
	}

	// This call is only applicable for file types
	if (type != filedesc_filestream)
	{
		errno = ERR_NOTAFILE;
		return (status = -1);
	}

	// Let the kernel do the rest of the work
	status = fileSetSize(&theStream->f, length);
	if (status < 0)
	{
		errno = status;
		return (status = -1);
	}

	return (status = 0);
}

