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
//  memmove.c
//

// This is the standard "memmove" function, as found in standard C libraries

#include <errno.h>
#include <string.h>
#include <sys/processor.h>


void *memmove(void *dest, const void *src, size_t bytes)
{
	// The memmove() function copies n bytes from memory area src to memory
	// area dest.  The memory areas may overlap.

	unsigned dwords = (bytes >> 2);

	if (!dest || !src)
	{
		errno = ERR_NULLPARAMETER;
		return (dest);
	}

	if (bytes)
	{
		// In case the memory areas overlap, we will copy the data differently
		// depending on the position of the src and dest pointers
		if (dest < src)
		{
			if (!dwords || ((src - dest) < 4) || ((unsigned) src % 4) ||
				((unsigned) dest % 4) || (bytes % 4))
			{
				processorCopyBytes(src, dest, bytes);
			}
			else
			{
				processorCopyDwords(src, dest, dwords);
			}
		}

		else if (dest > src)
		{
			if (!dwords || ((dest - src) < 4) || ((unsigned) src % 4) ||
				((unsigned) dest % 4) || (bytes % 4))
			{
				processorCopyBytesBackwards((src + (bytes - 1)),
					(dest + (bytes - 1)), bytes);
			}
			else
			{
				processorCopyDwordsBackwards((src + (bytes - 4)),
					(dest + (bytes - 4)), dwords);
			}
		}
	}

	return (dest);
}

