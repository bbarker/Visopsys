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
//  kernelInterrupt.h
//

// This is the header file to go with the kernel's interrupt handlers

#if !defined(_KERNELINTERRUPT_H)

#include "kernelDescriptor.h"

#define INTERRUPT_VECTORSTART			0x20

// ISA/PIC interrupt numbers
#define INTERRUPT_NUM_SYSTIMER			0
#define INTERRUPT_NUM_KEYBOARD			1
#define INTERRUPT_NUM_SLAVEPIC			2
#define INTERRUPT_NUM_COM2				3
#define INTERRUPT_NUM_COM1				4
#define INTERRUPT_NUM_SOUNDCARD			5
#define INTERRUPT_NUM_FLOPPY			6
#define INTERRUPT_NUM_LPT				7
#define INTERRUPT_NUM_RTC				8
#define INTERRUPT_NUM_VGA				9
#define INTERRUPT_NUM_AVAILABLE1		10
#define INTERRUPT_NUM_AVAILABLE2		11
#define INTERRUPT_NUM_MOUSE				12
#define INTERRUPT_NUM_COPROCERR			13
#define INTERRUPT_NUM_PRIMARYIDE		14
#define INTERRUPT_NUM_SECONDARYIDE		15

int kernelInterruptInitialize(void);
void *kernelInterruptGetHandler(int);
int kernelInterruptHook(int, void *, kernelSelector);
int kernelProcessingInterrupt(void);
int kernelInterruptGetCurrent(void);
void kernelInterruptSetCurrent(int);
void kernelInterruptClearCurrent(void);

#define _KERNELINTERRUPT_H
#endif

