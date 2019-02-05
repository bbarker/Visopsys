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
//  mbtowc.c
//

// This is the standard "mbtowc" function, as found in standard C libraries

#include <stdlib.h>


int mbtowc(wchar_t *wc, const char *bytes, size_t n)
{
	// Here's how the Linux man page describes this function:
	//
	// The main case for this function is when bytes is not NULL and pwc is not
	// NULL.  In this case, the mbtowc function inspects at most n bytes of the
	// multibyte string starting at bytes, extracts the next complete multibyte
	// character, converts it to a wide character and stores it at *pwc.  It
	// updates an internal shift state only known to the mbtowc function. If
	// bytes does not point to a '\0' byte, it returns the number of bytes
	// that were consumed from bytes, otherwise it returns 0.
	//
	// If the n bytes starting at bytes do not contain a complete multibyte
	// character, or if they contain an invalid multibyte sequence, mbtowc
	// returns -1.  This can happen even if n >= MB_CUR_MAX, if the multibyte
	// string contains redundant shift sequences.
	//
	// A different case is when bytes is not NULL but pwc is NULL.  In this
	// case the mbtowc function behaves as above, excepts that it does not store
	// the converted wide character in memory.
	//
	// A third case is when bytes is NULL.  In this case, pwc and n are ignored.
	// The mbtowc function resets the shift state, only known to this func’¡¾
	// tion, to the initial state, and returns non-zero if the encoding has
	// non-trivial shift state, or zero if the encoding is stateless.
	//
	// We're going to attempt to support UTF-8 as our multibyte standard.

	int numBytes = 0;

	if (!bytes)
		// Stateless.
		return (0);

	if (n < 1)
		return (-1);

	if ((unsigned char) bytes[0] <= 0x7F)
	{
		numBytes = 1;
	}
	else if ((bytes[0] & 0xE0) == 0xC0)
	{
		numBytes = 2;
		if (n < 2)
			return (-1);
	}
	else if ((bytes[0] & 0xF0) == 0xE0)
	{
		numBytes = 3;
		if (n < 3)
			return (-1);
	}
	else if ((bytes[0] & 0xF8) == 0xF0)
	{
		numBytes = 4;
		if (n < 4)
			return (-1);
	}
	else
	{
		return (-1);
	}

	if (wc)
	{
		if (numBytes == 1)
			*wc = (wchar_t)(bytes[0] & 0x7F);
		else if (numBytes == 2)
			*wc = ((((wchar_t)(bytes[0] & 0x1F)) >> 2) |
				((wchar_t)(bytes[1] & 0x3F)));
		else if (numBytes == 3)
			*wc = ((((wchar_t)(bytes[0] & 0x0F)) >> 4) |
				(((wchar_t)(bytes[1] & 0x3F)) >> 2) |
				((wchar_t)(bytes[2] & 0x3F)));
		else if (numBytes == 4)
			*wc = ((((wchar_t)(bytes[0] & 0x07)) >> 6) |
				(((wchar_t)(bytes[1] & 0x3F)) >> 4) |
				(((wchar_t)(bytes[2] & 0x3F)) >> 2) |
				((wchar_t)(bytes[3] & 0x3F)));
	}

	return (numBytes);
}

