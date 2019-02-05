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
//  isxdigit.c
//

// This is the standard "isxdigit" function, as found in standard C libraries

#include <ctype.h>


int isxdigit(int c)
{
	// Checks for hexadecimal digits, i.e. one of 0 1 2 3 4 5 6 7 8 9
	// a b c d e f A B C D E F.

	return (((c >= '0') && (c <= '9')) || ((c >= 'a') && (c <= 'f')) ||
		((c >= 'A') && (c <= 'F')));
}

