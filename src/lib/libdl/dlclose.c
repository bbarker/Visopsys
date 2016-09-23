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
//  dlclose.c
//

// This is the "dlclose" dynamic linking loader function as found in standard
// C libraries

#include <dlfcn.h>
#include <errno.h>
#include <string.h>

extern void **dlopenHandles;
extern int dlopenNumHandles;
extern char *dlerrorMessage;


int dlclose(void *handle)
{
	// Excerpted from the GNU man page:
	//
	// The function dlclose() decrements the reference count on the dynamic
	// library handle handle.  If the reference count drops to zero and no
	// other loaded libraries use symbols in it, then the dynamic library is
	// unloaded.
	//
	// ...but it's not unloaded.  The kernel doesn't do that at present.  And
	// there's no reference counting either.  We put this here merely for
	// compatibility at the moment.

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
		return (errno);
	}
	else
	{
		return (0);
	}
}

