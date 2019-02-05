//
//  Visopsys
//  Copyright (C) 1998-2018 J. Andrew McLaughlin
//
//  This program is free software; you can redistribute it and/or modify it
//  under the terms of the GNU General Public License as published by the Free
//  Software Foundation; either version 2 of the License, or (at your option)
//  any later version.
//
//  This program is distributed in the hope that it will be useful, but
//  WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
//  or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License
//  for more details.
//
//  You should have received a copy of the GNU General Public License along
//  with this program; if not, write to the Free Software Foundation, Inc.,
//  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
//
//  kernelRandom.c
//

// These functions are used for pseudorandom number generation.

#include "kernelRandom.h"
#include "kernelError.h"
#include "kernelLog.h"
#include "kernelRtc.h"
#include "kernelSysTimer.h"
#include <sys/types.h>

#define MULTIPLIER		0x5DEECE66DLL
#define ADDEND			0xBLL
#define MASK			((1LL << 48) - 1)

static volatile quad_t kernelRandomSeed = 0;


//
// The following 2 functions - setSeed() and random() - and the constant
// MULTIPLIER, ADDEND, and MASK values, are inspired by Nick Galbreath's
// <nickg@modp.com> re-implementation of the PRNG from Sun's
// java.util.Random at:
// http://javarng.googlecode.com/svn/trunk/com/modp/random/LinearSunJDK.java
//


static inline void setSeed(quad_t seed)
{
	kernelRandomSeed = ((seed ^ MULTIPLIER) & MASK);
}


static inline quad_t random(int numBits)
{
	kernelRandomSeed = ((kernelRandomSeed * MULTIPLIER + ADDEND) & MASK);
	return (kernelRandomSeed >> (48 - numBits));
}


/////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////
//
//  Below here, the functions are exported for external use
//
/////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////


int kernelRandomInitialize(void)
{
	// Initialize kernel pseudorandom number generation

	unsigned seed = 0;

	// Initialize the seed with the current system timer value and some things
	// from the real-time clock

	while (!seed)
	{
		seed = (kernelSysTimerRead() | ((kernelRtcReadSeconds() << 24) |
			(kernelRtcReadMinutes() << 16) | (kernelRtcReadHours() << 8)));
	}

	setSeed(seed);

	kernelLog("The kernel's random seed is: %lld", kernelRandomSeed);

	return (0);
}


unsigned kernelRandomUnformatted(void)
{
	// This is for getting an unformatted random number.
	return ((unsigned) random(8 * sizeof(unsigned)));
}


unsigned kernelRandomFormatted(unsigned start, unsigned end)
{
	// This function will return a random number between "start" and "end"

	unsigned result = 0;
	unsigned range = 0;

	if (end == start)
		// Ok, whatever
		return (start);

	if (end < start)
	{
		kernelError(kernel_error, "end (%u) < start (%u)", end, start);
		return (start);
	}

	result = kernelRandomUnformatted();

	range = ((end - start) + 1);
	if (!range)
		range = ~0UL;

	return ((result % range) + start);
}


unsigned kernelRandomSeededUnformatted(unsigned seed)
{
	// This is for getting an unformatted random number using the user's
	// seed value.

	setSeed(seed);
	return (kernelRandomUnformatted());
}


unsigned kernelRandomSeededFormatted(unsigned seed, unsigned start,
	unsigned end)
{
	// This function will return a random number between "start" and "end"
	// using the user's seed value.

	setSeed(seed);
	return (kernelRandomFormatted(start, end));
}


void kernelRandomBytes(unsigned char *buffer, unsigned size)
{
	// Fill the supplied buffer with random data.  Saves applications from
	// calling into the kernel millions of times.

	unsigned count;

	for (count = (size / 4); count > 0; count --)
	{
		*((unsigned *) buffer) = kernelRandomUnformatted();
		buffer += sizeof(unsigned);
	}
	for (count = (size % 4); count > 0; count --)
	{
		*buffer = (kernelRandomUnformatted() & 0xFF);
		buffer += sizeof(unsigned char);
	}
}

