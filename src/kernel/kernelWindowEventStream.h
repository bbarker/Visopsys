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
//  kernelWindowEventStream.h
//

// This file describes the kernel's facilities for reading and writing
// window events using a 'streams' abstraction.

#if !defined(_KERNELWINDOWEVENTSTREAM_H)

#include "kernelWindow.h"

#define WINDOW_EVENT_DWORDS (sizeof(windowEvent) / sizeof(unsigned))

// Functions exported by kernelWindowEventStream.c
int kernelWindowEventStreamNew(windowEventStream *);
int kernelWindowEventStreamPeek(windowEventStream *);
int kernelWindowEventStreamRead(windowEventStream *, windowEvent *);
int kernelWindowEventStreamWrite(windowEventStream *, windowEvent *);

#define _KERNELWINDOWEVENTSTREAM_H
#endif

