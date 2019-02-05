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
//  assert.h
//

// This is the Visopsys version of the standard header file assert.h

#if !defined(_ASSERT_H)

#include <stdio.h>
#include <stdlib.h>

#if !defined(NDEBUG)
	#define assert(expression) { \                                                                  \
		if (!(expression)) { \                                                              \
			fprintf(stderr, "ASSERT failed: %s line %d: %s\n", __FILE__, \
				__LINE__, (expression)); \
			abort(); } }
#else
	#define assert(expression) ((void) 0)
#endif

#define _ASSERT_H
#endif

