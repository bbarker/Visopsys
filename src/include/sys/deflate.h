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
//  deflate.h
//

// This file contains definitions and structures used by the deflate algorithm.

#if !defined(_DEFLATE_H)

#define DEFLATE_MAX_INBUFFERSIZE	32768
#define DEFLATE_MAX_OUTBUFFERSIZE	65536
#define DEFLATE_MAX_DISTANCE		32768
#define DEFLATE_CODE_EOB			256
#define DEFLATE_LITERAL_CODES		(DEFLATE_CODE_EOB + 1)
#define DEFLATE_LENGTH_CODES		31
#define DEFLATE_LITLEN_CODES		\
	(DEFLATE_LITERAL_CODES + DEFLATE_LENGTH_CODES)
#define DEFLATE_DIST_CODES			32
#define DEFLATE_CODELEN_CODES		19

// Block format flag fields
#define DEFLATE_BTYPE_NONE			0x00
#define DEFLATE_BTYPE_FIXED			0x01
#define DEFLATE_BTYPE_DYN			0x02

#define _DEFLATE_H
#endif

