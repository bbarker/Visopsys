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
//  vshMakeDirRecursive.c
//

// This contains some useful functions written for the shell

#include <errno.h>
#include <libgen.h>
#include <stdlib.h>
#include <sys/api.h>
#include <sys/vsh.h>


_X_ int vshMakeDirRecursive(char *path)
{
	// Desc: Attempts to create a directory 'path' and any nonexistent parent directories that precede it in the path.

	int status = 0;
	char *parent = NULL;

	if (fileFind(path, NULL) >= 0)
		return (status = 0);

	parent = dirname(path);
	if (!parent)
		return (status = ERR_NOSUCHENTRY);

	status = vshMakeDirRecursive(parent);

	free(parent);

	if (status < 0)
		return (status);

	return (status = fileMakeDir(path));
}

