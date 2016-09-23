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
//  _flt2str.c
//

// This is a generic function to turn a float into a string.

#include <sys/cdefs.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>


void _flt2str(float num, char *string, int roundPlaces)
{
	int charCount = 0;
	unsigned *u = NULL;
	int sign = 0;
	int exponent = 0;
	unsigned intPart = 0;
	unsigned fractPart = 0;
	unsigned place = 0;
	unsigned outputFraction = 0;
	unsigned rem = 0;
	unsigned count;

	if (!string)
	{
		errno = ERR_NULLPARAMETER;
		return;
	}

	string[0] = '\0';

	u = (unsigned *) &num;
	sign = (*u >> 31);
	exponent = (((*u & 0x7F800000) >> 23) - 127);
	intPart = 1;
	fractPart = ((*u & 0x007FFFFF) << 9);

	// Output the sign, if any
	if (sign)
		string[charCount++] = '-';

	// Special case exponents
	if (exponent == 0xFF)
	{
		strcat((string + charCount), "Infinity");
		return;
	}

	while (exponent)
	{
		if (exponent > 0)
		{
			intPart <<= 1;
			if (fractPart & (0x1 << 31))
				intPart |= 1;
			fractPart <<= 1;
			exponent -= 1;
		}
		else
		{
			fractPart >>= 1;
			if (intPart & 0x01)
				fractPart |= (0x1 << 31);
			intPart >>= 1;
			exponent += 1;
		}
	}

	// Output the whole number part
	_num2str(intPart, (string + charCount), 10, 0);
	charCount = strlen(string);

	string[charCount++] = '.';

	// Calculate the fraction part
	place = 1000000000;
	for (count = 2; fractPart; count *= 2)
	{
		if (!count)
			break;

		if (fractPart & (0x1 << 31))
			outputFraction += (place / count);

		fractPart <<= 1;
	}

	// Output the fraction part
	place = 100000000;
	while (place)
	{
		rem = (outputFraction % place);
		outputFraction = (outputFraction / place);

		if (roundPlaces)
		{
			string[charCount++] = ('0' + outputFraction);
			roundPlaces -= 1;
		}
		else
		{
			if ((string[charCount - 1] < '9') && (outputFraction > 4))
				string[charCount - 1] += 1;
			break;
		}

		outputFraction = rem;
		place /= 10;
	}

	string[charCount] = '\0';
	return;
}

