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
//  fls.c
//

// This is an "fls" function, not normally found in C libraries, which is
// like ffs() but returns the last bit set.

#include <string.h>
#include <values.h>


int fls(int i)
{
	// Returns the most significant bit set in the word.

	int count;

	if (!i)
		return (0);

	for (count = INTBITS; !(i & 0x80000000) && (count > 0); count --)
		i <<= 1;

	return (count);
}

