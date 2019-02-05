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
//  isupper.c
//

// This is the standard "isupper" function, as found in standard C libraries

#include <ctype.h>


int isupper(int c)
{
	// Checks for an uppercase letter.

	// We use the ISO-8859-15 character set for this determination.
	return (((c >= 'A') && (c <= 'Z')) ||
		(c == 166) || (c == 180) || (c == 188) || (c == 190) || (c == 223) ||
		((c >= 192) && (c <= 214)) ||
		((c >= 216) && (c <= 223)));
}

