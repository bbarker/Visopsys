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
//  _lnum2str.c
//

// This is a generic function to turns a long number into a string.

#include <errno.h>
#include <sys/cdefs.h>


void _lnum2str(unsigned long long num, char *string, int base, int sign)
{
	int digits = _ldigits(num, base, sign);
	int charCount = 0;
	unsigned long long place = 1;
	unsigned long long rem = 0;
	int count;

	if (!string)
	{
		errno = ERR_NULLPARAMETER;
		return;
	}

	// Negative?
	if (sign && ((long long) num < 0))
	{
		string[charCount++] = '-';
		num = ((long long) num * -1);
		digits -= 1;
	}

	for (count = 0; count < (digits - 1); count ++)
		place *= (unsigned long long) base;

	while (place)
	{
		rem = (num % place);
		num = (num / place);

		if (num < 10)
			string[charCount++] = ('0' + num);
		else
			string[charCount++] = ('a' + (num - 10));
		num = rem;
		place /= (unsigned long long) base;
	}

	string[charCount] = '\0';

	// Done
	return;
}

