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
//  vprintf.c
//

// This is the standard "vprintf" function, as found in standard C libraries

#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <sys/api.h>
#include <sys/cdefs.h>


int vprintf(const char *format, va_list list)
{
	int len = 0;
	char output[MAXSTRINGLENGTH];
	textAttrs attrs;

	if (visopsys_in_kernel)
		return (errno = ERR_BUG);

	// Fill out the output line based on
	len = _xpndfmt(output, MAXSTRINGLENGTH, format, list);

	if (len > 0)
	{
		memset(&attrs, 0, sizeof(textAttrs));
		attrs.flags |= TEXT_ATTRS_NOFORMAT;
		textPrintAttrs(&attrs, output);
	}

	return (len);
}

