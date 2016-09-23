//
//  Visopsys
//  Copyright (C) 1998-2016 J. Andrew McLaughlin
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
//  kernelMalloc.h
//

#if !defined(_KERNELMALLOC_H)

#include <sys/memory.h>

// Functions from kernelMalloc.c
void *_kernelMalloc(unsigned, const char *);
#define kernelMalloc(size) _kernelMalloc(size, __FUNCTION__)
int _kernelFree(void *, const char *);
#define kernelFree(ptr) _kernelFree(ptr, __FUNCTION__)
void *_kernelRealloc(void *, unsigned, const char *);
#define kernelRealloc(ptr, size) _kernelRealloc(ptr, size, __FUNCTION__)
int kernelMallocGetStats(memoryStats *);
int kernelMallocGetBlocks(memoryBlock *, int);
void _kernelMallocCheck(const char *, int);
#define kernelMallocCheck() _kernelMallocCheck(__FILE__, __LINE__)

#define _KERNELMALLOC_H
#endif

