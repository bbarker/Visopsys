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
//  memset.c
//

// This is the standard "memset" function, as found in standard C libraries

// The description from the GNU man page reads as follows:
// The  memset() function fills the first n bytes of the memory area pointed
// to by s with the constant byte c. Returns a pointer to the memory area s.

#include <string.h>
#include <errno.h>
#include <sys/processor.h>


void *memset(void *dest, int value, size_t bytes)
{
	unsigned dwords = (bytes >> 2);
	unsigned tmpDword = 0;

	// Check params
	if (!dest)
	{
		errno = ERR_NULLPARAMETER;
		return (NULL);
	}

	if (bytes)
	{
		if (!dwords || ((unsigned) dest % 4) || (bytes % 4))
		{
			processorWriteBytes(value, dest, bytes);
		}
		else
		{
			value &= 0xFF;
			tmpDword = ((value << 24) | (value << 16) |	(value << 8) | value);
			processorWriteDwords(tmpDword, dest, dwords);
		}
	}

	// Return success
	return (dest);
}

