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
//  fsetpos.c
//

// This is the standard "fsetpos" function, as found in standard C libraries

#include <stdio.h>
#include <errno.h>
#include <sys/api.h>


int fsetpos(FILE *theStream, fpos_t *pos)
{
	// The fsetpos() function sets the file position indicator for the
	// stream pointed to by stream according to the value of the object
	// pointed to by pos, which must be a value obtained from an earlier
	// call to fgetpos() on the same stream.  Upon successful completion,
	// fsetpos returns 0.  Otherwise, -1 is returned and the global variable
	// errno is set to indicate the error.

	// This call is not applicable for stdin, stdout, and stderr
	if ((theStream == stdin) || (theStream == stdout) || (theStream == stderr))
		return (errno = ERR_NOTAFILE);

	if (visopsys_in_kernel)
		return (errno = ERR_BUG);

	// Let the kernel do the work, baby.

	int status = fileStreamSeek(theStream, *pos);
	if (status < 0)
	{
		errno = status;
		return (-1);
	}

	return (0);
}

