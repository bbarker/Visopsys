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
//  _str2num.c
//

// This is a generic function to interpret a string as a number and return
// the value.  Follows the conventions of the strtol family of C library
// functions.

#include <ctype.h>
#include <errno.h>
#include <string.h>
#include <sys/cdefs.h>


unsigned long long _str2num(const char *string, unsigned base, int sign,
	int *consumed)
{
	unsigned long long result = 0;
	int length = 0;
	int negative = 0;
	int count = 0;

	if (!string)
	{
		errno = ERR_NULLPARAMETER;
		return (0);
	}

	// Get the length of the string
	length = strlen(string);

	// Skip whitespace
	while ((count < length) && isspace(string[count]))
		count += 1;

	if (count >= length)
	{
		errno = ERR_INVALID;
		goto out;
	}

	// Note sign, if applicable
	if ((string[count] == '+') || (string[count] == '-'))
	{
		if (sign && (string[count] == '-'))
			negative = 1;
		count += 1;
	}

	if (count >= length)
	{
		errno = ERR_INVALID;
		goto out;
	}

	// Handle '0x' for base 0 or base 16
	if ((count < (length - 1)) && (!base || (base == 16)))
	{
		if ((string[count] == '0') && (string[count + 1] == 'x'))
		{
			base = 16;
			count += 2;
		}
	}

	if (count >= length)
	{
		errno = ERR_INVALID;
		goto out;
	}

	// Handle additional base 0 situations
	if (!base)
	{
		if (string[count] == '0')
		{
			base = 8;
			count += 1;
		}
		else
		{
			base = 10;
		}
	}

	if (count >= length)
	{
		errno = ERR_INVALID;
		goto out;
	}

	// Do a loop to iteratively add to the value of 'result'.
	while (count < length)
	{
		switch (base)
		{
			case 2:
			case 3:
			case 4:
			case 5:
			case 6:
			case 7:
			case 8:
			case 9:
			case 10:
				if (!isdigit(string[count]) ||
					((string[count] - '0') >= (int) base))
				{
					errno = ERR_INVALID;
					goto out;
				}
				result *= base;
				result += (string[count] - '0');
				break;

			case 16:
				if (!isxdigit(string[count]))
				{
					errno = ERR_INVALID;
					goto out;
				}
				result *= base;
				if ((string[count] >= '0') && (string[count] <= '9'))
					result += (string[count] - '0');
				else if ((string[count] >= 'a') && (string[count] <= 'f'))
					result += ((string[count] - 'a') + 10);
				else
					result += ((string[count] - 'A') + 10);
				break;

			default:
				errno = ERR_NOTIMPLEMENTED;
				goto out;
		}

		count += 1;
	}

out:
	if (negative)
		result = ((long long) result * -1);

	if (consumed)
		*consumed = count;

	return (result);
}

