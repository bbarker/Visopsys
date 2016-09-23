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
//  cosf.c
//

// This is the standard "cosf" function, as found in standard C libraries

#include <math.h>


float cosf(float radians)
{
	// Returns the cosine of x (x given in radians).  Adapted from an algorithm
	// found at http://www.dontletgo.com/planets/math.html

	float result = 0;
	float sign = 0;
	float x2n = 0;
	float factorial = 0;
	float n, count;

	while (radians > (M_PI * 2))
		radians -= (M_PI * 2);
	while (radians < -(M_PI * 2))
		radians += (M_PI * 2);

	for (n = 0; n < 10; n += 1)
	{
		sign = 1.0;
		for (count = 0; count < n; count += 1)
			sign *= -1;

		x2n = 1.0;
		for (count = 0; count < (2 * n); count += 1)
			x2n *= radians;

		factorial = 1.0;
		for (count = (2 * n); count > 0; count -= 1)
			factorial *= count;

		if (factorial)
			result += (sign * (x2n / factorial));
	}

	return (result);
}

