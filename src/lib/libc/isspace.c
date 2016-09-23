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
//  isspace.c
//

// This is the standard "isspace" function, as found in standard C libraries

#include <ctype.h>


int isspace(int c)
{
	// Checks for white-space characters.

	// In the "C" and "POSIX" locales, these are: space, form-feed ('\f'),
	// newline ('\n'), carriage return ('\r'), horizontal tab ('\t'),
	// and vertical tab ('\v').
	return ((c == ' ') || (c == '\f') || (c == '\n') || (c == '\r') ||
		(c == '\t') || (c == '\v') || (c == 160));
}

