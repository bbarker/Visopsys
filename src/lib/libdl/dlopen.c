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
//  dlopen.c
//

// This is the "dlopen" dynamic linking loader function as found in standard
// C libraries

#include <dlfcn.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/api.h>

void **dlopenHandles = NULL;
int dlopenNumHandles = 0;
extern char *dlerrorMessage;
static int dlopenMaxHandles = 0;


void *dlopen(const char *fileName, int flags __attribute__((unused)))
{
	// Excerpted from the GNU man page:
	//
	// The function dlopen() loads the dynamic library file named by the null-
	// terminated string 'filename' and  returns an opaque "handle" for the
	// dynamic library.  If filename is NULL, then the returned handle is for
	// the main program.  If filename contains a slash ("/"), then it is
	// interpreted as a (relative  or  absolute) pathname.
	//
	// If there's no slash, the /system/libraries/ directory will be searched.

	void *handle = NULL;

	// Make sure we've got room in our array to store the handle we get.
	if (!dlopenHandles || (dlopenNumHandles >= dlopenMaxHandles))
	{
		dlopenMaxHandles += 16;

		if (!dlopenHandles)
			dlopenHandles = malloc(dlopenMaxHandles * sizeof(void *));
		else
			dlopenHandles =
				realloc(dlopenHandles, (dlopenMaxHandles * sizeof(void *)));

		if (!dlopenHandles)
		{
			errno = ERR_MEMORY;
			dlerrorMessage = strerror(errno);
			return (NULL);
		}
	}

	handle = loaderLinkLibrary(fileName);
	if (!handle)
	{
		errno = ERR_NOSUCHENTRY;
		dlerrorMessage = strerror(errno);
	}
	else
	{
		dlopenHandles[dlopenNumHandles++] = handle;
	}

	return (handle);
}

