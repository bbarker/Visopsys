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
//  dlerror.c
//

// This is the "dlerror" dynamic linking loader function as found in standard
// C libraries

#include <dlfcn.h>
#include <string.h>

char *dlerrorMessage = NULL;


char *dlerror(void)
{
	// Excerpted from the GNU man page:
	//
	// The function dlerror() returns a human readable string describing the
	// most recent error that occurred from dlopen(), dlsym() or dlclose()
	// since the last call to dlerror().  It returns NULL if no errors have
	// occurred since initialization or since it was last called.

	char *message = NULL;

	if (!dlerrorMessage)
		return (NULL);

	message = dlerrorMessage;
	dlerrorMessage = NULL;
	return (message);
}

