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
//  wctomb.c
//

// This is the standard "wctomb" function, as found in standard C libraries

#include <stdlib.h>


int wctomb(char *string, wchar_t wc)
{
	// Here's how the Linux man page describes this function:
	//
	// If s is not NULL, the wctomb function converts the wide character wc to
	// its multibyte representation and stores it at the beginning of the
	// character array pointed to by string.  It updates the shift state, which
	// is stored in a static anonymous variable only known to the wctomb func-
	// tion, and returns the length of said multibyte representation, i.e. the
	// number of bytes written at string. The programmer must ensure that there
	// is room for at least MB_CUR_MAX bytes at string.  If string is NULL,
	// the wctomb function resets the shift state, only known to this function,
	// to the initial state, and returns non-zero if the encoding has non-
	// trivial shift state, or zero if the encoding is stateless.
	//
	// We're going to attempt to support UTF-8 as our multibyte standard.

	int numBytes = 1;

	if (!string)
		// Stateless.
		return (0);

	if (wc > 0x0010FFFF)
		// Too large
		return (-1);

	if (wc > 0x7F)
		numBytes += 1;
	if (wc > 0x7FF)
		numBytes += 1;
	if (wc > 0xFFFF)
		numBytes += 1;

	if (numBytes == 1)
	{
		string[0] = (wc & 0x7F);
	}
	else if (numBytes == 2)
	{
		string[0] = (0xC0 | ((wc & 0x07D0) >> 6));
		string[1] = (0x80 | (wc & 0x003F));
	}
	else if (numBytes == 3)
	{
		string[0] = (0xE0 | ((wc & 0xF000) >> 12));
		string[1] = (0x80 | ((wc & 0x0FD0) >> 6));
		string[2] = (0x80 | (wc & 0x003F));
	}
	else if (numBytes == 4)
	{
		string[0] = (0xF0 | ((wc & 0x001D0000) >> 18));
		string[1] = (0x80 | ((wc & 0x0003F000) >> 12));
		string[2] = (0x80 | ((wc & 0x00000FD0) >> 6));
		string[3] = (0x80 | (wc & 0x0000003F));
	}

	return (numBytes);
}

