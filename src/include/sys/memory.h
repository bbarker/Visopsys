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
//  memory.h
//

// This file contains definitions and structures for using and manipulating
// memory in Visopsys.

#if !defined(_MEMORY_H)

#include <sys/debug.h>
#include <sys/errors.h>
#include <sys/lock.h>

// Definitions
#define MEMORY_PAGE_SIZE				4096
#define MEMORY_BLOCK_SIZE				MEMORY_PAGE_SIZE
#define MEMORY_MAX_DESC_LENGTH			32

#define USER_MEMORY_HEAP_MULTIPLE		(64 * 1024)    // 64 Kb
#define KERNEL_MEMORY_HEAP_MULTIPLE		(1024 * 1024)  // 1 meg

typedef struct _mallocBlock {
	int process;
	unsigned start;
	unsigned size;
	unsigned heapAlloc;
	unsigned heapAllocSize;
	struct _mallocBlock *prev;
	struct _mallocBlock *next;
	const char *function;

} mallocBlock;

// Struct that describes one memory block
typedef struct {
	int processId;
	char description[MEMORY_MAX_DESC_LENGTH];
	unsigned startLocation;
	unsigned endLocation;

} memoryBlock;

// Struct that describes overall memory statistics
typedef struct {
	unsigned totalBlocks;
	unsigned usedBlocks;
	unsigned totalMemory;
	unsigned usedMemory;

} memoryStats;

typedef struct {
	int (*multitaskerGetCurrentProcessId)(void);
	void *(*memoryGet)(unsigned, const char *);
	int (*memoryRelease)(void *);
	int (*lockGet)(lock *);
	int (*lockRelease)(lock *);
	void (*debug)(const char *, const char *, int, debug_category,
		const char *, ...) __attribute__((format(printf, 5, 6)));
	void (*error)(const char *, const char *, int, kernelErrorKind,
		const char *, ...) __attribute__((format(printf, 5, 6)));

} mallocKernelOps;

// For using malloc() in kernel space
extern unsigned mallocHeapMultiple;
extern mallocKernelOps mallocKernOps;

// Extras for malloc debugging
void *_doMalloc(unsigned, const char *);
void _doFree(void *, const char *);
int _mallocBlockInfo(void *, memoryBlock *);
int _mallocGetStats(memoryStats *);
int _mallocGetBlocks(memoryBlock *, int);
int _mallocCheck(void);

#define _MEMORY_H
#endif

