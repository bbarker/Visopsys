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
//  pow.c
//

// This is the standard "pow" function, as found in standard C libraries

#include <math.h>
#include <errno.h>


double pow(double x, double y)
{
	// The pow() function returns the value of x raised to the power of y.

	int count;

	if ((x < 0) && (floor(y) != y))
	{
		// The argument x is negative and y is not an integral value.  This
		// would result in a complex number.
		errno = ERR_DOMAIN;
		return (0);
	}

	if (!y)
	{
		x = 1;
	}
	else
	{
		for (count = 1; count < y; count ++)
		x *= x;
	}

	return (x);
}

