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
//  rand.c
//

// This is the standard "rand" function, as found in standard C libraries

#include <stdlib.h>
#include <errno.h>
#include <sys/api.h>

// The application's random seed for rand() and srand()
unsigned __random_seed = 1;


int rand(void)
{
	// The rand() function returns a pseudo-random integer between 0 and
	// RAND_MAX (defined in <stdlib.h>).  The man pages in linux and solaris
	// say that it uses 'a multiplicative congruential random-number generator
	// with period 2^32'.  Right, ok, well, we'll use the kernel's one instead.

	if (visopsys_in_kernel)
		return (errno = ERR_BUG);

	// __random_seed is initialized with a value of 1.  If the user wants to
	// initialize it, he/she should call srand() first
	__random_seed = randomSeededFormatted(__random_seed, 0, RAND_MAX);
	return (__random_seed);
}

