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
//  vshMoveFile.c
//

// This contains some useful functions written for the shell

#include <errno.h>
#include <sys/vsh.h>
#include <sys/api.h>


_X_ int vshMoveFile(const char *srcFile, const char *destFile)
{
	// Desc: Move (rename) the file specified by the name 'srcFile' to the destination 'destFile'.  Both filenames must be absolute pathnames -- beginning with '/' -- and must be within the same filesystem.

	int status = 0;

	// Make sure filename arguments aren't NULL
	if (!srcFile || !destFile)
		return (errno = ERR_NULLPARAMETER);

	// Attempt to rename the file
	status = fileMove(srcFile, destFile);
	if (status < 0)
		errno = status;

	return (status);
}

