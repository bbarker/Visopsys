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
//  stream.h
//

// This file contains definitions and structures for using and manipulating
// streams in Visopsys.

#if !defined(_STREAM_H)

#include <sys/lock.h>

// An enum for describing the size of each item in the stream
typedef enum {
	itemsize_byte, itemsize_dword

} streamItemSize;

// This data structure is the generic stream
typedef volatile struct _stream {
	unsigned char *buffer;
	unsigned buffSize;
	unsigned size;
	unsigned first;
	unsigned last;
	unsigned count;
	lock lock;

	// Stream functions.  These are not for calling from user space.
	int (*clear)(volatile struct _stream *);
	int (*intercept)(volatile struct _stream *, ...);
	int (*append)(volatile struct _stream *, ...);
	int (*appendN)(volatile struct _stream *, unsigned, ...);
	int (*push)(volatile struct _stream *, ...);
	int (*pushN)(volatile struct _stream *, unsigned, ...);
	int (*pop)(volatile struct _stream *, ...);
	int (*popN)(volatile struct _stream *, unsigned, ...);

} stream;

// Some specialized kinds of streams

typedef stream networkStream;

#define _STREAM_H
#endif

