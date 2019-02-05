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
//  dlsym.c
//

// This is the "dlsym" dynamic linking loader function as found in standard
// C libraries

#include <dlfcn.h>
#include <errno.h>
#include <sys/api.h>

extern void **dlopenHandles;
extern int dlopenNumHandles;
extern char *dlerrorMessage;


void *dlsym(void *handle, const char *symbolName)
{
	// Excerpted from the GNU man page:
	//
	// The function dlsym() takes a "handle" of a dynamic library returned by
	// dlopen() and the null-terminated symbol name, returning the address
	// where that symbol is loaded into memory.  If the symbol is not found,
	// in the specified library or any of the libraries that were automati-
	// cally loaded by dlopen() when that library was loaded, dlsym() returns
	// NULL.  (The search performed by dlsym() is breadth first through the
	// dependency tree of these libraries.)  Since the value of the symbol
	// could actually be NULL (so that a NULL return from dlsym() need not
	// indicate an error), the correct way to test for an error is to call
	// dlerror() to clear any old error conditions, then call dlsym(), and
	// then call dlerror() again, saving its return value into a variable, and
	// check whether this saved value is not NULL.

	void *symbolAddress = NULL;
	int foundHandle = 0;
	int count;

	// Make sure the handle is in our list of handles, though we don't do
	// anything else with it.
	for (count = 0; count < dlopenNumHandles; count ++)
	{
		if (dlopenHandles[count] == handle)
		{
			foundHandle = 1;
			break;
		}
	}

	if (!foundHandle)
	{
		errno = ERR_NOSUCHENTRY;
		dlerrorMessage = strerror(errno);
		return (symbolAddress = NULL);
	}

	symbolAddress = loaderGetSymbol(symbolName);

	return (symbolAddress);
}

