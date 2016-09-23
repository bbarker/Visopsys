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
//  calloc.c
//

// This is the standard "calloc" function, as found in standard C libraries

#include <stdlib.h>
#include <string.h>


void *_calloc(size_t items, size_t itemSize, const char *function)
{
	// This is what the linux man page says about this function:
	// calloc() allocates memory for an array of  nmemb  elements
	// of  size bytes each and returns a pointer to the allocated
	// memory.  The memory is set to zero.

	size_t totalSize = 0;
	void *memoryPointer = NULL;

	// Total size is (items * itemSize)
	totalSize = (items * itemSize);

	memoryPointer = _malloc(totalSize, function);

	if (memoryPointer)
		// Clear the memory
		memset(memoryPointer, 0, totalSize);

	// Return this value, whether or not we were successful
	return (memoryPointer);
}

