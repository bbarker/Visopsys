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
//  kernelMemory.h
//

#if !defined(_KERNELMEMORY_H)

#include <sys/memory.h>

// Maximum number of raw memory allocations
#define MAXMEMORYBLOCKS			2048

// Descriptions for standard reserved memory areas
#define MEMORYDESC_IVT_BDA		"real mode ivt and bda"
#define MEMORYDESC_HOLE_EBDA	"memory hole and ebda"
#define MEMORYDESC_VIDEO_ROM	"video memory and rom"
#define MEMORYDESC_KERNEL		"kernel memory"
#define MEMORYDESC_PAGING		"kernel paging data"
#define MEMORYDESC_USEDBLOCKS	"used memory block list"
#define MEMORYDESC_FREEBITMAP	"free memory bitmap"

typedef struct {
	unsigned size;
	unsigned physical;
	void *virtual;

} kernelIoMemory;

// Functions from kernelMemory.c
int kernelMemoryInitialize(unsigned);
unsigned kernelMemoryGetPhysical(unsigned, unsigned, int, const char *);
int kernelMemoryReleasePhysical(unsigned);
void *kernelMemoryGetSystem(unsigned, const char *);
int kernelMemoryReleaseSystem(void *);
int kernelMemoryGetIo(unsigned, unsigned, int, const char *,
	kernelIoMemory *);
int kernelMemoryReleaseIo(kernelIoMemory *);
int kernelMemoryChangeOwner(int, int, int, void *, void **);
int kernelMemoryShare(int, int, void *, void **);

// Functions exported to userspace
void *kernelMemoryGet(unsigned, const char *);
int kernelMemoryRelease(void *);
int kernelMemoryReleaseAllByProcId(int);
int kernelMemoryGetStats(memoryStats *, int);
int kernelMemoryGetBlocks(memoryBlock *, unsigned, int);

#define _KERNELMEMORY_H
#endif

