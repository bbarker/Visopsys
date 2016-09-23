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
//  kernelMemory.h
//

#if !defined(_KERNELMEMORY_H)

#include <sys/memory.h>

// Definitions
#define MAXMEMORYBLOCKS  2048

typedef struct {
	unsigned size;
	unsigned physical;
	void *virtual;

} kernelIoMemory;

// Functions from kernelMemory.c
int kernelMemoryInitialize(unsigned);
unsigned kernelMemoryGetPhysical(unsigned, unsigned, const char *);
int kernelMemoryReleasePhysical(unsigned);
void *kernelMemoryGetSystem(unsigned, const char *);
int kernelMemoryReleaseSystem(void *);
int kernelMemoryGetIo(unsigned, unsigned, kernelIoMemory *);
int kernelMemoryReleaseIo(kernelIoMemory *);
int kernelMemoryChangeOwner(int, int, int, void *, void **);
int kernelMemoryShare(int, int, void *, void **);

// Functions exported to userspace
void *kernelMemoryGet(unsigned, const char *);
int kernelMemoryRelease(void *);
int kernelMemoryReleaseAllByProcId(int);
int kernelMemoryGetStats(memoryStats *, int);
int kernelMemoryGetBlocks(memoryBlock *, unsigned, int);
int kernelMemoryBlockInfo(void *, memoryBlock *);

#define _KERNELMEMORY_H
#endif

