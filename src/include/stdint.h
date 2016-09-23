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
//  stdint.h
//

// This is the Visopsys version of the header file stdint.h

#if !defined(_STDINT_H)

typedef unsigned char		uint8_t;
typedef unsigned short		uint16_t;
typedef unsigned			uint32_t;
typedef unsigned long long	uint64_t;
typedef char				int8_t;
typedef short				int16_t;
typedef int					int32_t;
typedef long long			int64_t;

#define _STDINT_H
#endif

