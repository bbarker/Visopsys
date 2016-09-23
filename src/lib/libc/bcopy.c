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
//  bcopy.c
//

// This is the standard "bcopy" function, as found in standard C libraries

#include <string.h>


void bcopy(const void *src, void *dest, size_t len)
{
	// The bcopy() function copies the first n bytes of the source string src
	// to the destination string dest.  If len is zero, no bytes are copied.

	size_t count = 0;

	for (count = 0; count < len; count ++)
		((char *) dest)[count] = ((char *) src)[count];

	return;
}

