//
//  Visopsys
//  Copyright (C) 1998-2005 J. Andrew McLaughlin
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
//  59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
//
//  kernelPage.h
//

#if !defined(_KERNELPAGE_H)

#include "kernelLock.h"

// Definitions

// x86 constants
#define PAGE_TABLES_PER_DIR   1024
#define PAGE_PAGES_PER_TABLE  1024

// Page entry bitfield values for x86, but we'll make them global.
#define PAGEFLAG_PRESENT      0x0001
#define PAGEFLAG_WRITABLE     0x0002
#define PAGEFLAG_USER         0x0004
#define PAGEFLAG_WRITETHROUGH 0x0008
#define PAGEFLAG_CACHEDISABLE 0x0010
#define PAGEFLAG_GLOBAL       0x0100

// Page mapping schemes
#define PAGE_MAP_ANY          0x01
#define PAGE_MAP_EXACT        0x02

//#define PAGE_DEBUG 1

// Data structures

typedef volatile struct {
  unsigned table[PAGE_TABLES_PER_DIR];

} kernelPageDirPhysicalMem;

typedef kernelPageDirPhysicalMem kernelPageDirVirtualMem;

typedef volatile struct {
  unsigned page[PAGE_PAGES_PER_TABLE];

} kernelPageTablePhysicalMem;

typedef kernelPageTablePhysicalMem kernelPageTableVirtualMem;

typedef volatile struct {
  int processId;
  int numberShares;
  int parent;
  int privilege;
  kernelPageDirPhysicalMem *physical;
  kernelPageDirVirtualMem *virtual;
  lock dirLock;
  // next by Davide Airaghi
  int kernel;
  
} kernelPageDirectory;

typedef volatile struct {
  kernelPageDirectory *directory;
  int tableNumber;
  int freePages;
  kernelPageTablePhysicalMem *physical;
  kernelPageTableVirtualMem *virtual;

} kernelPageTable;

// Little macros for rounding up or down to the nearest page size
#define kernelPageRoundUp(size)                                       \
  (((((unsigned) size) / MEMORY_PAGE_SIZE) +                          \
   ((((unsigned) size) % MEMORY_PAGE_SIZE) != 0)) * MEMORY_PAGE_SIZE)
#define kernelPageRoundDown(size)                                     \
  ((((unsigned) size) / MEMORY_PAGE_SIZE) * MEMORY_PAGE_SIZE)

// Functions exported by kernelPage.c
int kernelPageInitialize(unsigned);
void *kernelPageGetDirectory(int);
void *kernelPageNewDirectory(int, int);
void *kernelPageShareDirectory(int, int);
int kernelPageDeleteDirectory(int);
int kernelPageMap(int, void *, void *, unsigned);
int kernelPageMapToFree(int, void *, void **, unsigned);
int kernelPageUnmap(int, void *, unsigned);
void *kernelPageGetPhysical(int, void *);
void *kernelPageFindFree(int, unsigned);
int kernelPageSetAttrs(int, int, unsigned char, void *, unsigned);

#ifdef PAGE_DEBUG
void kernelPageTableDebug(int);
#endif

#define _KERNELPAGE_H
#endif
