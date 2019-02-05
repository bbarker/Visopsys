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
//  rewind.c
//

// This is the standard "rewind" function, as found in standard C libraries

#include <stdio.h>
#include <errno.h>
#include <sys/api.h>


void rewind(FILE *theStream)
{
	// The rewind function sets the file position indicator for the stream
	// pointed to by stream to the beginning of the file.  It is equivalent
	// to:
	//      (void)fseek(stream, 0L, SEEK_SET)
	// except that the error indicator for the stream is also cleared.  The
	// rewind function returns no value.

	if (visopsys_in_kernel)
	{
		errno = ERR_BUG;
		return;
	}

	// Let the kernel do all the work, baby.
	int status = fileStreamSeek(theStream, 0);
	if (status < 0)
		errno = status;

	return;
}

