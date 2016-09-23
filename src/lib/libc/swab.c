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
//  swab.c
//

// This is the standard "swab" function, as found in standard C libraries

#include <unistd.h>


void swab(const void *from, void *to, ssize_t num)
{
	// From the GNU man page:
	// The swab() function copies n bytes from the array pointed to by from to
	// the array pointed to by to, exchanging adjacent even and odd bytes.
	// This function is used to exchange data between machines that have
	// different low/high byte ordering.
	//
	// This function does nothing when n is negative.  When n is positive and
	// odd, it handles n-1 bytes as above, and does something unspecified with
	// the last byte.  (In other words, n should be even.)

	int count;

	if (num < 0)
		return;

	// Make the number even
	num &= ~1;

	if (to != from)
		// Copy data
		for (count = 0; count < num; count ++)
			((char *) to)[count] = ((const char *) from)[count];

	// Swappem.
	for (count = 0; count < (num - 1); count ++)
	{
		unsigned char tmp = ((char *) to)[count];
		((char *) to)[count] = ((char *) to)[count + 1];
		((char *) to)[count + 1] = tmp;
	}

	return;
}

