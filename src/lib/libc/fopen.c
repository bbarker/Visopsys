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
//  fopen.c
//

// This is the standard "fopen" function, as found in standard C libraries

#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/api.h>


FILE *fopen(const char *fileName, const char *mode)
{
	// Excerpted from the GNU man page:
	//
	// The fopen() function opens the file whose name is the string pointed to
	// by fileName and associates a stream with it.
	//
	// The argument mode points to a string beginning with one of the
	// following sequences (Additional characters may follow these sequences.):
	//
	// r      Open text file for reading.  The stream is positioned at the
	//        beginning of the file.
	//
	// r+     Open for reading and writing.  The stream is positioned at the
	//        beginning of the file.
	//
	// w      Truncate file to zero length or create text file for writing.
	//        The stream is positioned at the beginning of the file.
	//
	// w+     Open for reading and writing.  The file is created if it does
	//        not exist, otherwise it is truncated.  The stream is positioned
	//        at the beginning of the file.
	//
	// a      Open for appending (writing at end of file).  The file is created
	//        if it does not exist.  The stream is positioned at the end
	//        of the file.
	//
	// a+     Open for reading and appending (writing at end of file).  The
	//        file is created if it does not exist.  The initial file position
	//        for reading is at the beginning of the file, but output is
	//        always appended to the end of the file.
	//
	// N.B.:  The a+ argument will always be broken in Visopsys.  Reading from
	//        the start of the file and writing to the end of the file are silly
	//        IMO and have no equivalent in Visopsys.

	int status = 0;
	fileStream *theStream = NULL;
	int flags = 0;
	int append = 0;

	if (visopsys_in_kernel)
	{
		errno = ERR_BUG;
		return (theStream = NULL);
	}

	// Check params
	if (!fileName || !mode)
	{
		errno = ERR_NULLPARAMETER;
		return (theStream = NULL);
	}

	// We have to convert the text string mode arguments to our flags

	if (strstr(mode, "r+"))
		flags |= OPENMODE_READWRITE;
	else if (strchr(mode, 'r'))
		flags |= OPENMODE_READ;

	if (strstr(mode, "w+"))
		flags |= (OPENMODE_READWRITE | OPENMODE_CREATE | OPENMODE_TRUNCATE);
	else if (strchr(mode, 'w'))
		flags |= (OPENMODE_WRITE | OPENMODE_CREATE | OPENMODE_TRUNCATE);

	if (strstr(mode, "a+"))
	{
		flags |= (OPENMODE_READWRITE | OPENMODE_CREATE);
		append = 1;
	}
	else if (strchr(mode, 'a'))
	{
		flags |= (OPENMODE_WRITE | OPENMODE_CREATE);
		append = 1;
	}

	// Get memory for the file stream
	theStream = malloc(sizeof(fileStream));
	if (!theStream)
	{
		errno = ERR_MEMORY;
		return (theStream);
	}

	memset(theStream, 0, sizeof(fileStream));
	status = fileStreamOpen(fileName, flags, theStream);
	if (status < 0)
	{
		errno = status;
		free(theStream);
		return (theStream = NULL);
	}

	// If we're only writing, and not appending, seek to the beginning of the
	// file, since the fileStreamOpen() call is automatically in 'append' mode
	// when the mode is write-only.
	if (OPENMODE_ISWRITEONLY(flags) && !append)
	{
		status = fileStreamSeek(theStream, 0);
		if (status < 0)
		{
			errno = status;
			fileStreamClose(theStream);
			free(theStream);
			return (theStream = NULL);
		}
	}

	return ((FILE *) theStream);
}

