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
//  _dbl2str.c
//

// This is a generic function to turn a double into a string.

#include <sys/cdefs.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>


void _dbl2str(double num, char *string, int roundPlaces)
{
	int charCount = 0;
	unsigned long long *u = NULL;
	int sign = 0;
	long long exponent = 0;
	unsigned long long intPart = 0;
	unsigned long long fractPart = 0;
	unsigned long long place = 0;
	unsigned long long outputFraction = 0;
	unsigned long long rem = 0;
	unsigned long long count;

	if (!string)
	{
		errno = ERR_NULLPARAMETER;
		return;
	}

	string[0] = '\0';

	u = (unsigned long long *) &num;
	sign = (int)(*u >> 63);
	exponent = (((*u & (0x7FFULL << 52)) >> 52) - 1023);
	intPart = 1;
	fractPart = ((*u & 0x000FFFFFFFFFFFFFULL) << 12);

	// Output the sign, if any
	if (sign)
		string[charCount++] = '-';

	// Special case exponents
	if (exponent == 0x7FF)
	{
		strcat((string + charCount), "Infinity");
		return;
	}

	while (exponent)
	{
		if (exponent > 0)
		{
			intPart <<= 1;
			if (fractPart & (0x1ULL << 63))
				intPart |= 1;
			fractPart <<= 1;
			exponent -= 1;
		}
		else
		{
			fractPart >>= 1;
			if (intPart & 0x1ULL)
				fractPart |= (0x1ULL << 63);
			intPart >>= 1;
			exponent += 1;
		}
	}

	// Output the whole number part
	_lnum2str(intPart, (string + charCount), 10, 0);
	charCount = strlen(string);

	string[charCount++] = '.';

	// Calculate the fraction part
	place = 10000000000000000000ULL;
	for (count = 2; fractPart; count *= 2)
	{
		if (!count)
			break;

		if (fractPart & (0x1ULL << 63))
			outputFraction += (place / count);

		fractPart <<= 1;
	}

	// Output the fraction part
	place = 1000000000000000000ULL;
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

