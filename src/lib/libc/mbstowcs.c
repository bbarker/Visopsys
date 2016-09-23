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
//  mbstowcs.c
//

// This is the standard "mbstowcs" function, as found in standard C libraries

#include <stdlib.h>


size_t mbstowcs(wchar_t *dest, const char *src, size_t n)
{
	// Here's how the Linux man page describes this function (oh boy):
	//
	// If dest is not a NULL pointer, the mbstowcs function converts the
	// multibyte string src to a wide-character string starting at dest.  At
	// most n wide characters are written to dest.  The conversion starts in
	// the initial state.  The conversion can stop for three reasons:
	//
	// 1. An invalid multibyte sequence has been encountered.  In this case
	// (size_t)(-1) is returned.
	//
	// 2. n non-L'\0' wide characters have been stored at dest.  In this case
	// the number of wide characters written to dest is returned, but the
	// shift state at this point is lost.
	//
	// 3.  The  multibyte  string has been completely converted, including the
	// terminating '\0'.  In this case the number of wide characters written to
	// dest, excluding the terminating L'\0' character, is returned.
	//
	// The programmer must ensure that there is room for at least n wide
	// characters at dest.
	//
	// If dest is NULL, n is ignored, and the conversion  proceeds as above,
	// except that the converted wide characters are not written out to memory,
	// and that no length limit exists.
	//
	// In order to avoid the case 2 above, the programmer should make sure n
	// is greater or equal to mbstowcs(NULL,src,0)+1.
	//
	// We're going to attempt to support UTF-8 as our multibyte standard.

	int numBytes = 0;
	int maxBytes = (n * MB_CUR_MAX);
	size_t count;

	for (count = 0; count < n; count ++)
	{
		numBytes = mbtowc(dest, src, maxBytes);

		if (numBytes < 0)
			return (-1);

		if (*dest == (wchar_t) NULL)
			return (count);

		dest += 1;
		src += numBytes;
		maxBytes -= numBytes;
	}

	return (n);
}

