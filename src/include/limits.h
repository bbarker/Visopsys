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
//  limits.h
//

// This is the Visopsys version of the standard header file limits.h

#if !defined(_LIMITS_H)

#define CHAR_BIT     8                      // Bits in a char

#define SCHAR_MAX    127                    // Max value of a signed char
#define SCHAR_MIN    -128                   // Min value of a signed char

// Assumes that a char type is signed
#define CHAR_MAX     SCHAR_MAX              // Max value of a char
#define CHAR_MIN     SCHAR_MIN              // Min value of a char
#define UCHAR_MAX    255                    // Max value of an unsigned char

// Assumes that a short int type is 16 bits
#define SHRT_MAX     32767                  // Max value of a short int
#define SHRT_MIN     -32768                 // Min value of a short int
#define USHRT_MAX    65535                  // Max value of an unsigned short

// Assumes that an int type is 32 bits
#define INT_MAX      2147483647             // Max value of an int
#define INT_MIN      (-INT_MAX - 1)         // Min value of an int
#define UINT_MAX     4294967295U            // Max value of an unsigned int

// Assumes that a long int type is 32 bits
#define LONG_MAX     2147483647L            // Max value of a long int
#define LONG_MIN     (-LONG_MAX - 1L)       // Min value of a long int
#define ULONG_MAX    4294967295UL           // Max value of an unsigned long

// We don't support multibyte characters right now
#define MB_LEN_MAX   4

#define _LIMITS_H
#endif

