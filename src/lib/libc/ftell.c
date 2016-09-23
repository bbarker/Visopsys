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
//  ftell.c
//

// This is the standard "ftell" function, as found in standard C libraries

#include <stdio.h>
#include <errno.h>
#include <sys/api.h>


long ftell(FILE *theStream)
{
	// The ftell() function obtains the current value of the file-position
	// indicator for the stream pointed to by stream.  Upon successful
	// completion, the ftell() function returns the current value of the
	// file-position indicator for the stream measured in bytes from the
	// beginning of the file.  Otherwise, they return -1 and sets errno to
	// indicate the error.

	// This call is not applicable for stdin, stdout, and stderr
	if ((theStream == stdin) || (theStream == stdout) || (theStream == stderr))
		return (errno = ERR_NOTAFILE);

	return (theStream->offset);
}

