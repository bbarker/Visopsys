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
//  open.c
//

// This is the standard "open" function, as found in standard C libraries

#include <fcntl.h>
#include <errno.h>
#include <stdlib.h>
#include <sys/api.h>
#include <sys/cdefs.h>


int open(const char *fileName, int flags)
{
	// Excerpted from the GNU man page:
	//
	// The open() system call is used to convert a pathname into a file
	// descriptor (a small, non-negative integer for use in subsequent I/O as
	// with read, write, etc.).  The file offset is set to the beginning of
	// the file.
	//
	// The parameter flags is one of O_RDONLY, O_WRONLY or O_RDWR which
	// request opening the file read-only, write-only or read/write, respec-
	// tively, bitwise-or'ed with zero or more of the following:
	//
	// O_CREAT
	//   If the file does not exist it will be created.
	// O_TRUNC
	//   If the file already exists and is a regular file and the open
	//   mode allows writing (i.e., is O_RDWR or O_WRONLY) it will be
	//   truncated to length 0.
	// O_EXCL When used with O_CREAT, if the file already exists it is an
	//   error and the open will fail.
	// O_APPEND
	//   The file is opened in append mode.  Before each write, the file
	//   pointer is positioned at the end of the file, as if with lseek.
	// O_DIRECTORY
	//   If pathname is not a directory, cause the open to fail.

	int status = 0;
	int newFlags = 0;
	fileStream *theStream = NULL;
	int fd = 0;

	// We have to adapt the UNIX/POSIX flags to our flags

	if (visopsys_in_kernel)
	{
		errno = ERR_BUG;
		return (status = -1);
	}

	// First the 'exclusive' ones
	if (flags & O_RDONLY)
		newFlags |= OPENMODE_READ;
	else if (flags & O_RDWR)
		newFlags |= OPENMODE_READWRITE;
	else if (flags & O_WRONLY)
		newFlags |= OPENMODE_WRITE;

	// The rest
	if (flags & O_CREAT)
		newFlags |= OPENMODE_CREATE;
	if (flags & O_TRUNC)
		newFlags |= OPENMODE_TRUNCATE;
	if ((newFlags & OPENMODE_CREATE) && (flags & O_EXCL))
		newFlags &= ~OPENMODE_CREATE;
	if ((newFlags & OPENMODE_TRUNCATE) && (flags & O_APPEND))
		newFlags &= ~OPENMODE_TRUNCATE;

	// Get memory for the file stream
	theStream = calloc(1, sizeof(fileStream));
	if (!theStream)
	{
		errno = ERR_MEMORY;
		return (status = -1);
	}

	status = fileStreamOpen(fileName, newFlags, theStream);
	if (status < 0)
	{
		free(theStream);
		errno = status;
		return (status = -1);
	}

	if ((flags & O_DIRECTORY) && (theStream->f.type != dirT))
	{
		// Supposed to fail if not a directory
		fileStreamClose(theStream);
		free(theStream);
		errno = ERR_NOTADIR;
		return (status = -1);
	}

	// If we're not appending, seek to the beginning of the file, since the
	// fileStreamOpen() call is automatically in 'append' mode
	if (!(flags & O_APPEND))
	{
		status = fileStreamSeek(theStream, 0);
		if (status < 0)
		{
			fileStreamClose(theStream);
			free(theStream);
			errno = status;
			return (status = -1);
		}
	}

	// Get a POSIX-style file descriptor for it
	fd = _fdalloc(filedesc_filestream, theStream, 1 /* free data on close */);
	if (fd < 0)
	{
		fileStreamClose(theStream);
		free(theStream);
		errno = fd;
		return (status = -1);
	}

	return (fd);
}

