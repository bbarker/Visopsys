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
//  _ldigits.c
//

// This function takes a long numeric value and a base, and returns the number
// of digits required to represent it, including a sign character if
// applicable.

#include <errno.h>
#include <sys/cdefs.h>


int _ldigits(unsigned long long num, int base, int sign)
{
	int digits = 1;

	if (base < 2)
	{
		errno = ERR_INVALID;
		return (digits = -1);
	}

	if (sign && ((long long) num < 0))
	{
		num = ((long long) num * -1);
		digits += 1;
	}

	while (num >= (unsigned long long) base)
	{
		digits += 1;
		num /= (unsigned long long) base;
	}

	return (digits);
}

