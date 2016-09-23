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
//  dirname.c
//

// This is the standard "dirname" function, as found in standard C libraries

#include <libgen.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>


char *dirname(char *path)
{
	// The GNU manual page says the following:
	//
	//   The functions dirname() and basename() break a null-terminated pathname
	//   string into directory and filename components.  In the usual case,
	//   dirname() returns the string up to, but not including, the final '/',
	//   and basename() returns the component following the final '/'.  Trailing
	//   '/' characters are not counted as part of the pathname.
	//
	//   If path does not contain a slash, dirname() returns the string "."
	//   while basename() returns a copy of path.  If path is the string  "/",
	//   then both dirname() and basename() return the string "/".  If path is a
	//   NULL pointer or points to an empty string, then both dirname() and
	//   basename() return the string ".".
	//
	// The GNU manual page also says that the POSIX versions might modify their
	// arguments or use statically allocated memory.  Our version will never
	// do either of these things; it will return a dynamically allocated string
	// which the caller is responsible for freeing.  Also, unlike GNU basename,
	// we will not return the empty string when path has a trailing '/'.

	char *newPath = NULL;
	char *lastSlash = NULL;
	int count;

	// Get the memory to return.  Always a maxed-out pathname.
	newPath = malloc(MAX_PATH_LENGTH);
	if (!newPath)
	{
		// Nothing much we can do here.
		errno = ERR_MEMORY;
		return (newPath);
	}

	// Look for NULL, empty string, or no '/'
	if (!path || !path[0] || !strrchr(path, '/'))
	{
		newPath[0] = '.';
		newPath[1] = '\0';
		return (newPath);
	}

	strncpy(newPath, path, MAX_PATH_LENGTH);

	// Remove any trailing separators, not including the first character
	for (count = (strlen(newPath) - 1);
		((count > 0) && (newPath[count] == '/')); count --)
	{
		newPath[count] = '\0';
	}

	// Look for the last instance of '/'
	lastSlash = strrchr(newPath, '/');

	if (!lastSlash)
	{
		// The only '/' was a trailing one, which we removed
		newPath[0] = '.';
		newPath[1] = '\0';
		return (newPath);
	}

	// Terminate it there.
	if (lastSlash != newPath)
		lastSlash[0] = '\0';
	else
		lastSlash[1] = '\0';

	return (newPath);
}

