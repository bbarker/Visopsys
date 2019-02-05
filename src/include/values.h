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
//  values.h
//

// This is the Visopsys version of the standard header file values.h

#if !defined(_VALUES_H)

#include <limits.h>
#include <float.h>

#define CHARBITS        (sizeof(char) * CHAR_BIT)
#define SHORTBITS       (sizeof(short int) * CHAR_BIT)
#define INTBITS	        (sizeof(int) * CHAR_BIT)
#define LONGBITS        (sizeof(long int) * CHAR_BIT)
#define PTRBITS	        (sizeof(char *) * CHAR_BIT)
#define DOUBLEBITS      (sizeof(double) * CHAR_BIT)
#define FLOATBITS       (sizeof(float) * CHAR_BIT)

#define MINSHORT        SHRT_MIN
#define	MININT          INT_MIN
#define	MINLONG         LONG_MIN

#define	MAXSHORT        SHRT_MAX
#define	MAXINT          INT_MAX
#define	MAXLONG         LONG_MAX

#define HIBITS          MINSHORT
#define HIBITL          MINLONG

#define	MAXDOUBLE       DBL_MAX
#define	MAXFLOAT        FLT_MAX
#define	MINDOUBLE       DBL_MIN
#define	MINFLOAT        FLT_MIN
#define	DMINEXP         DBL_MIN_EXP
#define	FMINEXP         FLT_MIN_EXP
#define	DMAXEXP         DBL_MAX_EXP
#define	FMAXEXP         FLT_MAX_EXP
#define BITSPERBYTE     CHAR_BIT

#define	_VALUES_H
#endif

