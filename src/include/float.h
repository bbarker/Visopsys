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
//  float.h
//

// This is the Visopsys version of the standard header file float.h

#if !defined(_FLOAT_H)

// These values are intelligent guesses based on reconciling the float.h
// files from linux and solaris on i386 machines, and based on the
// 'Standard C' specification Copyright (c) 1989-1996 by P.J. Plauger
// and Jim Brodie.

#define DBL_DIG         15               // <integer rvalue >= 10>
#define DBL_EPSILON     2.2204460492503131E-16
                                         // <double rvalue <= 10^(-9)>
#define DBL_MANT_DIG    53               // <integer rvalue>
#define DBL_MAX         1.7976931348623157E+308
                                         // <double rvalue >= 10^37>
#define DBL_MAX_10_EXP  308              // <integer rvalue >= 37>
#define DBL_MAX_EXP     1024             // <integer rvalue>
#define DBL_MIN         2.2250738585072014E-308
                                         // <double rvalue <= 10^(-37)>
#define DBL_MIN_10_EXP  -307             // <integer rvalue <= -37>
#define DBL_MIN_EXP     -1021            // <integer rvalue>

#define FLT_DIG         6                // <integer rvalue >= 10>
#define FLT_EPSILON     1.19209290e-07F  // <double rvalue <= 10^(-9)>
#define FLT_MANT_DIG    24               // <integer rvalue>
#define FLT_MAX         3.402823466E+38F // <float rvalue >= 10^37>
#define FLT_MAX_10_EXP  38               // <integer rvalue >= 37>
#define FLT_MAX_EXP     128              // <integer rvalue>
#define FLT_MIN         1.175494351E-38F // <float rvalue <= 10^(-37)>
#define FLT_MIN_10_EXP  -37              // <integer rvalue <= -37>
#define FLT_MIN_EXP     -125             // <integer rvalue>
#define FLT_RADIX       2                // <#if expression >= 2>
#define FLT_ROUNDS      1                // <integer rvalue>

#define LDBL_DIG        18               // <integer rvalue >= 10>
#define LDBL_EPSILON    1.0842021724855044340075E-19L
                                         // <long double rvalue <= 10^(-9)>
#define LDBL_MANT_DIG   64               // <integer rvalue>
#define LDBL_MAX        1.1897314953572317650213E+4932L
                                         // <long double rvalue >= 10^37>
#define LDBL_MAX_10_EXP 4932             // <integer rvalue >= 37>
#define LDBL_MAX_EXP    16384            // <integer rvalue>
#define LDBL_MIN        3.362103143112093506262677817321752603E-4932L
                                         // <long double rvalue <= 10^(-37)>
#define LDBL_MIN_10_EXP 4931             // <integer rvalue <= -37>
#define LDBL_MIN_EXP    -16381           // <integer rvalue>

#define _FLOAT_H
#endif

