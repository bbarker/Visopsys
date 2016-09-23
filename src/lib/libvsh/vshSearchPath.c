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
//  vshSearchPath.c
//

// This contains some useful functions written for the shell

#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <sys/vsh.h>
#include <sys/api.h>
#include <sys/env.h>


_X_ int vshSearchPath(const char *orig, char *new)
{
	// Desc: Search the current path (defined by the PATH environment variable) for the first occurrence of the filename specified in 'orig', and place the complete, absolete pathname result in 'new'.  If a match is found, the function returns zero.  Otherwise, it returns a negative error code.  'new' must be large enough to hold the complete absolute filename of any match found.

	// Use shared code
	#include "../shared/srchpath.c"
}

