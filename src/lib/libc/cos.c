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
//  cos.c
//

// This is the standard "cos" function, as found in standard C libraries

#include <math.h>


double cos(double radians)
{
	// Returns the cosine of x (x given in radians).  Adapted from an algorithm
	// found at http://www.dontletgo.com/planets/math.html

	double result = 0;
	double sign = 0;
	double x2n = 0;
	double factorial = 0;
	double n, count;

	while (radians > (M_PI * 2))
		radians -= (M_PI * 2);
	while (radians < -(M_PI * 2))
		radians += (M_PI * 2);

	for (n = 0; n < 15; n += 1)
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

