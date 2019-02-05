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
//  kernelDescriptor.h
//

// This is the header file to go with the kernel's descriptor manager

#if !defined(_KERNELDESCRIPTOR_H)

// Definitions
#define PRIV_CODE				0x00000008
#define PRIV_DATA				0x00000010
#define PRIV_STACK				0x00000018
#define USER_CODE				0x00000023
#define USER_DATA				0x0000002B
#define USER_STACK				0x00000033
#define KERNEL_CALLGATE			0x0000003B

#define RES_GLOBAL_DESCRIPTORS	8	// (0 is unusable)
#define GDT_SIZE				1024
#define IDT_SIZE				256

// This is just so we know when we're dealing with a descriptor rather
// than a regular integer
typedef int kernelSelector;

// This structure describes the fields of a descriptor in the x86
// architecture
typedef struct {
	unsigned char segSizeByte1;
	unsigned char segSizeByte2;
	unsigned char baseAddress1;
	unsigned char baseAddress2;
	unsigned char baseAddress3;
	unsigned char attributes1;
	unsigned char attributes2;
	unsigned char baseAddress4;

} __attribute__((packed)) kernelDescriptor;

// Functions exported by kernelDescriptor.c
int kernelDescriptorInitialize(void);
int kernelDescriptorRequest(volatile kernelSelector *);
int kernelDescriptorRelease(kernelSelector descriptorNumber);
int kernelDescriptorSetUnformatted(volatile kernelSelector, unsigned char,
	unsigned char, unsigned char, unsigned char, unsigned char,
	unsigned char, unsigned char, unsigned char);
int kernelDescriptorSet(volatile kernelSelector, volatile void *,
	unsigned, int, int, int, int, int, int);
int kernelDescriptorGet(volatile kernelSelector, kernelDescriptor *);
int kernelDescriptorSetIDTInterruptGate(int, void *);
int kernelDescriptorSetIDTTaskGate(int, kernelSelector);

#define _KERNELDESCRIPTOR_H
#endif

