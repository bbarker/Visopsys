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
//  fgetpos.c
//

// This is the standard "fgetpos" function, as found in standard C libraries

#include <stdio.h>
#include <errno.h>
#include <sys/api.h>


int fgetpos(FILE *theStream, fpos_t *pos)
{
	// The fgetpos() function stores the current value of the file position
	// indicator for the stream pointed to by stream in the object pointed
	// to by pos.  The value stored contains unspecified information usable
	// by fsetpos() for repositioning the stream to its position at the time
	// of the call to fgetpos().  Upon successful completion, fgetpos()
	// returns 0.  Otherwise, it returns a non-zero value and sets errno to
	// indicate the error.

	// This call is not applicable for stdin, stdout, and stderr
	if ((theStream == stdin) || (theStream == stdout) || (theStream == stderr))
		return (errno = ERR_NOTAFILE);

	*pos = theStream->offset;
	return (0);
}

