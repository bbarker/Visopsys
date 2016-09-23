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
//  toupper.c
//

// This is the standard "toupper" function, as found in standard C libraries

#include <ctype.h>


int toupper(int c)
{
	// If the argument of toupper() represents an lower-case letter, and there
	// exists a corresponding upper-case letter, the result is the
	// corresponding upper-case letter.  All other arguments are returned
	// unchanged.

	// We use the ISO-8859-15 character set for this conversion.
	if (islower(c))
	{
		if (c == 168)
			return (c = 166);
		else if (c == 184)
			return (c = 180);
		else if (c == 189)
			return (c = 188);
		else if (c == 255)
			return (c = 190);
		else
			return (c - 32);
	}
	else
		return (c);
}

