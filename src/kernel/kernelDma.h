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
//  kernelDma.h
//

// These are the generic functions for DMA access.  These are above the
// level of the actual DMA driver.

#if !defined(_KERNELDMA_H)

#include "kernelDevice.h"

// Definitions.

// These are 8-bit bitwise numbers sent to the controller's
// mode registers.
#define DMA_READMODE	0x08
#define DMA_WRITEMODE	0x04

// A structure used to register a DMA driver.

typedef struct {
	int (*driverOpenChannel)(int, void *, int, int);
	int (*driverCloseChannel)(int);

} kernelDmaOps;

// Functions exported from kernelDma.c
int kernelDmaInitialize(kernelDevice *);
int kernelDmaOpenChannel(int, void *, int, int);
int kernelDmaCloseChannel(int);

#define _KERNELDMA_H
#endif

