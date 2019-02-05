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
//  tolower.c
//

// This is the standard "tolower" function, as found in standard C libraries

#include <ctype.h>


int tolower(int c)
{
	// If the argument of tolower() represents an upper-case letter, and there
	// exists a corresponding lower-case letter, the result is the
	// corresponding lower-case letter.  All other arguments are returned
	// unchanged.

	// We use the ISO-8859-15 character set for this conversion.
	if (isupper(c))
	{
		if (c == 166)
			return (c = 168);
		else if (c == 180)
			return (c = 184);
		else if (c == 188)
			return (c = 189);
		else if (c == 190)
			return (c = 255);
		else if (c == 223)
			return (c = 223);
		else
			return (c + 32);
	}
	else
		return (c);
}

