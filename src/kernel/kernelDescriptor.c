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
//  kernelDescriptor.c
//

// This file contains the C functions belonging to the kernel's descriptor
// manager.

#include "kernelDescriptor.h"
#include "kernelApi.h"
#include "kernelError.h"
#include "kernelParameters.h"
#include <string.h>
#include <sys/processor.h>

// Space for the global descriptor table
static volatile kernelDescriptor globalDescriptorTable[GDT_SIZE];
static volatile kernelDescriptor interruptDescriptorTable[IDT_SIZE];

// List of free entries in the global descriptor table
static volatile kernelSelector freeDescriptors[GDT_SIZE];
static volatile int numFreeDescriptors = 0;

// A flag to make sure we have been initialized properly
static int initialized = 0;


/////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////
//
//  Below here, the functions are exported for external use
//
/////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////

int kernelDescriptorInitialize(void)
{
	// This function should be called by the kernel's initialization function,
	// before multitasking is enabled.  This will set up the initial Global
	// Descriptor Table (GDT) and Interrupt Descriptor Table (IDT)

	int status = 0;
	int count;

	// We need to reinitialize and take control of the Global Descriptor
	// Table, and will serve as the interface to that.  We'll prepare space
	// for it, and add all of the new, free descriptors to the free
	// descriptors list

	// Initialize the Global Descriptor Table
	memset((kernelDescriptor *) globalDescriptorTable, 0,
		(sizeof(kernelDescriptor) * GDT_SIZE));

	// Initialize the Interrupt Descriptor Table
	memset((kernelDescriptor *) interruptDescriptorTable, 0,
		(sizeof(kernelDescriptor) * IDT_SIZE));

	// That's all we need to do for the IDT, however we need to do more
	// work for the GDT.  The IDT is mostly just filled once at startup,
	// but the GDT is much more dynamic in our implementation.

	// Make note of the fact that we are initialized BEFORE we try to call
	// the SetXXXEntry functions
	initialized = 1;

	// Now, we have to install some basic descriptors in the GDT.  The
	// positions of these are fixed, so we don't use the request function
	// to get them; rather we install them manually

	// Make the global, privileged code segment descriptor
	status = kernelDescriptorSet(
		PRIV_CODE,				// Privileged code selector number
		0,						// Starts at zero
		0x000FFFFF,				// Maximum size
		1,						// Present in memory
		PRIVILEGE_SUPERVISOR,	// Supervisor privilege
		1,						// Code segments are not system segs
		0xA,					// Code, non-conforming, readable
		1,						// LARGE size granularity
		1);						// 32-bit code segment

	if (status < 0)
		// Something went wrong
		return (status);

	// Make the global, privileged data segment descriptor
	status = kernelDescriptorSet(
		PRIV_DATA,				// Privileged data selector number
		0,						// Starts at zero
		0x000FFFFF,				// Maximum size
		1,						// Present in memory
		PRIVILEGE_SUPERVISOR,	// Supervisor privilege
		1,						// Data segments are not system segs
		0x2,					// Data, expand-up, writable
		1,						// LARGE size granularity
		1);						// 32-bit data segment

	if (status < 0)
		// Something went wrong
		return (status);

	// Make the global, privileged stack segment descriptor
	status = kernelDescriptorSet(
		PRIV_STACK,				// Privileged stack selector number
		0,						// Starts at zero
		0x000FFFFF,				// Maximum size
		1,						// Present in memory
		PRIVILEGE_SUPERVISOR,	// Supervisor privilege
		1,						// Stack segments are not system segs
		0x2,					// Stack, expand-up, writable
		1,						// LARGE size granularity
		1);						// 32-bit stack segment

	if (status < 0)
		// Something went wrong
		return (status);

	// Make the global, user code segment descriptor
	status = kernelDescriptorSet(
		USER_CODE,				// User code selector number
		0,						// Starts at zero
		0x000FFFFF,				// Maximum size
		1,						// Present in memory
		PRIVILEGE_USER,			// User privilege
		1,						// Code segments are not system segs
		0xA,					// Code, non-conforming, readable
		1,						// LARGE size granularity
		1);						// 32-bit code segment

	if (status < 0)
		// Something went wrong
		return (status);

	// Make the global, user data segment descriptor
	status = kernelDescriptorSet(
		USER_DATA,				// Privileged data selector number
		0,						// Starts at zero
		0x000FFFFF,				// Maximum size
		1,						// Present in memory
		PRIVILEGE_USER,			// User privilege
		1,						// Data segments are not system segs
		0x2,					// Data, expand-up, writable
		1,						// LARGE size granularity
		1);						// 32-bit data segment

	if (status < 0)
		// Something went wrong
		return (status);

	// Make the global, user stack segment descriptor
	status = kernelDescriptorSet(
		USER_STACK,				// Privileged stack selector number
		0,						// Starts at zero
		0x000FFFFF,				// Maximum size
		1,						// Present in memory
		PRIVILEGE_USER,			// User privilege
		1,						// Stack segments are not system segs
		0x2,					// Stack, expand-up, writable
		1,						// LARGE size granularity
		1);						// 32-bit stack segment

	if (status < 0)
		// Something went wrong
		return (status);

	// Make the kernel API callgate descriptor
	status = kernelDescriptorSetUnformatted(
		KERNEL_CALLGATE,							// Kernel callgate selector
		((unsigned) kernelApi & 0x000000FF),		// Address 1
		(((unsigned) kernelApi & 0x0000FF00) >> 8),	// Address 2
		(PRIV_CODE & 0x000000FF),					// Code selector 1
		((PRIV_CODE & 0x0000FF00) >> 8),			// Code selector 2
		0x00,										// Copy 0 dwords to API stack
		0xEC,										// Present, priv 3, 32-bit
		(((unsigned) kernelApi & 0x00FF0000) >> 16), // Address 3
		(((unsigned) kernelApi & 0xFF000000) >> 24));// Address 4

	if (status < 0)
		// Something went wrong
		return (status);

	// Initialize the list of "free" descriptors
	numFreeDescriptors = (GDT_SIZE - RES_GLOBAL_DESCRIPTORS);

	for (count = 0; count < numFreeDescriptors; count ++)
		freeDescriptors[count] = ((count + RES_GLOBAL_DESCRIPTORS) * 8);

	// Now we can install our new GDT
	processorSetGDT((void *) globalDescriptorTable, (GDT_SIZE * 8));

	// And our new IDT
	processorSetIDT((void *) interruptDescriptorTable, (IDT_SIZE * 8));

	return (status = 0);
}


int kernelDescriptorRequest(volatile kernelSelector *descNumPointer)
{
	// This function is used to allocate a free descriptor from the
	// global descriptor table.

	int status = 0;
	kernelSelector newDescriptorNumber = 0;

	// Don't do anything unless we have been initialized first
	if (!initialized)
		// We have not been initialized
		return (status = ERR_NOTINITIALIZED);

	// Make sure there's at least one free descriptor
	if (numFreeDescriptors < 1)
		// Damn.  Out of descriptors
		return (status = ERR_NOFREE);

	// Make sure the pointer->pointer parameter we were passed isn't NULL
	if (!descNumPointer)
		// Oops.
		return (status = ERR_NULLPARAMETER);

	// Get the first free descriptor from the list
	newDescriptorNumber = freeDescriptors[0];

	// (Make sure it isn't NULL)
	if (!newDescriptorNumber)
		// Crap.  We're corrupted or something
		return (status = ERR_BADDATA);

	// Reduce the count of free descriptors
	numFreeDescriptors -= 1;

	// If there are any remaining descriptors in the free descriptor list,
	// we should shift the last descriptor into the first spot that we
	// just vacated
	if (numFreeDescriptors > 0)
		// numFreeDescriptors now points to the last free descriptor in the
		// list.  We need to move it to the front spot.
		freeDescriptors[0] = freeDescriptors[numFreeDescriptors];

	// Success.  Set the pointer for the calling function
	*descNumPointer = newDescriptorNumber;

	return (status = 0);
}


int kernelDescriptorRelease(kernelSelector selector)
{
	// This function is used to release a used descriptor back to
	// the global descriptor table.

	int status = 0;

	// Don't do anything unless we have been initialized first
	if (!initialized)
		// We have not been initialized
		return (status = ERR_NOTINITIALIZED);

	// Add the freed descriptor to the free descriptor list.  Mask out the
	// least-significant 3 bits (table index and permission bits)
	freeDescriptors[numFreeDescriptors++] = (selector & 0x0000FFF8);

	// Return success
	return (status = 0);
}


int kernelDescriptorSetUnformatted(volatile kernelSelector selector,
	unsigned char segSizeByte1, unsigned char segSizeByte2,
	unsigned char baseAddress1, unsigned char baseAddress2,
	unsigned char baseAddress3, unsigned char attributes1,
	unsigned char attributes2,  unsigned char baseAddress4)
{
	// This function is mostly internal, and is used to change a descriptor
	// in the global descriptor table.  This function is a simplified version
	// of the other one -- it does not do any convenient conversions of things
	// for the calling function, and it does not check the legality of any
	// of the contents -- it simply assigns them to the corresponding bytes
	// of the desctriptor.  A function such as this is necessary for doing
	// things like installing call gates, which do not closely resemble
	// memory segment descriptors and the like.  Returns 0 on success,
	// negative otherwise

	int status = 0;
	int entryNumber = 0;

	// Don't do anything unless we have been initialized first
	if (!initialized)
		// We have not been initialized
		return (status = ERR_NOTINITIALIZED);

	// Convert the requested descriptor into its entry number
	entryNumber = (selector >> 3);

	// Initialize the descriptor we're changing
	memset((kernelDescriptor *) &globalDescriptorTable[entryNumber], 0,
		sizeof(kernelDescriptor));

	// Now, simply copy the data we were passed into the requested entry
	globalDescriptorTable[entryNumber].segSizeByte1 = segSizeByte1;
	globalDescriptorTable[entryNumber].segSizeByte2 = segSizeByte2;
	globalDescriptorTable[entryNumber].baseAddress1 = baseAddress1;
	globalDescriptorTable[entryNumber].baseAddress2 = baseAddress2;
	globalDescriptorTable[entryNumber].baseAddress3 = baseAddress3;
	globalDescriptorTable[entryNumber].attributes1 = attributes1;
	globalDescriptorTable[entryNumber].attributes2 = attributes2;
	globalDescriptorTable[entryNumber].baseAddress4 = baseAddress4;

	// There.  We made a descriptor.  Return success.
	return (status = 0);
}


int kernelDescriptorSet(volatile kernelSelector selector, volatile void *base,
	unsigned size, int present, int privilegeLevel, int system, int type,
	int granularity, int bitSize)
{
	// This function is mostly internal, and is used to change a descriptor
	// in a descriptor table.  All of the relevant information should be
	// passed in as parameters, and they will then be added to the table

	int status = 0;
	int entryNumber = 0;

	// Don't do anything unless we have been initialized first
	if (!initialized)
		// We have not been initialized
		return (status = ERR_NOTINITIALIZED);

	// Convert the requested descriptor into its entry number
	entryNumber = (selector >> 3);

	// Base can be any value.  Size must be small enough to fit into 20 bits.
	if (size > 0x000FFFFF)
	{
		kernelError(kernel_error, "Invalid segment size");
		return (status = ERR_INVALID);
	}

	// The present bit should be either 0 or 1
	if ((present < 0) || (present > 1))
	{
		kernelError(kernel_error, "Invalid 'segment present' value");
		return (status = ERR_INVALID);
	}

	// The descriptor privilege level should be 0-3
	if ((privilegeLevel < 0) || (privilegeLevel > 3))
	{
		kernelError(kernel_error, "Invalid segment privilege");
		return (status = ERR_INVALID);
	}

	// The system bit should be either 0 or 1
	if ((system < 0) || (system > 1))
	{
		kernelError(kernel_error, "Invalid 'system' segment value");
		return (status = ERR_INVALID);
	}

	// The type nybble should be 0-15
	if ((type < 0) || (type > 15))
	{
		kernelError(kernel_error, "Invalid selector type");
		return (status = ERR_INVALID);
	}

	// The granularity bit should be either 0 or 1
	if ((granularity < 0) || (granularity > 1))
	{
		kernelError(kernel_error, "Invalid size granularity");
		return (status = ERR_INVALID);
	}

	// The bitsize bit should be either 0 or 1
	if ((bitSize < 0) || (bitSize > 1))
	{
		kernelError(kernel_error, "Invalid bitsize value");
		return (status = ERR_INVALID);
	}

	// Initialize the descriptor we're changing
	memset((kernelDescriptor *) &globalDescriptorTable[entryNumber], 0,
		sizeof(kernelDescriptor));

	// Now we can begin constructing the descriptor.  Start working through
	// each of the descriptor's bits.

	// The two least-significant segment size bytes
	globalDescriptorTable[entryNumber].segSizeByte1 = (unsigned char)
		(size & 0x000000FF);
	globalDescriptorTable[entryNumber].segSizeByte2 = (unsigned char)
		((size & 0x0000FF00) >> 8);

	// The three least significant base address bytes
	globalDescriptorTable[entryNumber].baseAddress1 = (unsigned char)
		((unsigned) base & 0x000000FF);
	globalDescriptorTable[entryNumber].baseAddress2 = (unsigned char)
		(((unsigned) base & 0x0000FF00) >> 8);
	globalDescriptorTable[entryNumber].baseAddress3 = (unsigned char)
		(((unsigned) base & 0x00FF0000) >> 16);

	// The two attribute bytes
	globalDescriptorTable[entryNumber].attributes1 = (unsigned char)
		((present << 7) | (privilegeLevel << 5) | (system << 4) | type);

	globalDescriptorTable[entryNumber].attributes2 = (unsigned char)
		((granularity << 7) | (bitSize << 6) | ((size & 0x000F0000) >> 16));

	// The most-significant base address byte
	globalDescriptorTable[entryNumber].baseAddress4 = (unsigned char)
		(((unsigned) base & 0xFF000000) >> 24);

	// There.  We made a descriptor.  Return success.
	return (status = 0);
}


int kernelDescriptorGet(kernelSelector selector, kernelDescriptor *descriptor)
{
	// This function will return the contents of the requested GDT descriptor
	// to the calling function.  Returns 0 on success, negative otherwise

	int status = 0;
	int entryNumber = 0;

	// Don't do anything unless we have been initialized first
	if (!initialized)
		// We have not been initialized
		return (status = ERR_NOTINITIALIZED);

	// Convert the requested descriptor into its entry number
	entryNumber = (selector >> 3);

	// Make sure the kernelDescriptor pointer we're being passed is not NULL
	if (!descriptor)
		return (status = ERR_NULLPARAMETER);

	// OK, let's get the values from the requested table entry and fill out
	// the structure as requested
	descriptor->segSizeByte1 = globalDescriptorTable[entryNumber].segSizeByte1;
	descriptor->segSizeByte2 = globalDescriptorTable[entryNumber].segSizeByte2;
	descriptor->baseAddress1 = globalDescriptorTable[entryNumber].baseAddress1;
	descriptor->baseAddress2 = globalDescriptorTable[entryNumber].baseAddress2;
	descriptor->baseAddress3 = globalDescriptorTable[entryNumber].baseAddress3;
	descriptor->attributes1 = globalDescriptorTable[entryNumber].attributes1;
	descriptor->attributes2 = globalDescriptorTable[entryNumber].attributes2;
	descriptor->baseAddress4 = globalDescriptorTable[entryNumber].baseAddress4;

	// Return success
	return (status = 0);
}


int kernelDescriptorSetIDTInterruptGate(int number, void *address)
{
	// This function can be used to install an interrupt gate descriptor
	// in the interrupt descriptor table.  Takes the number of the interrupt
	// as the first parameter, and the address of the Interrupt Service
	// Routine (ISR) as the second.  Returns 0 on success, negative otherwise

	int status = 0;

	// Don't do anything unless we have been initialized first
	if (!initialized)
		// We have not been initialized
		return (status = ERR_NOTINITIALIZED);

	// Make sure that the requested interrupt number doesn't exceed the
	// max size of the table (it's allowed to be zero here, however)
	if (number >= IDT_SIZE)
	{
		kernelError(kernel_error, "Invalid table entry number");
		return (status = ERR_NOSUCHENTRY);
	}

	// Make sure the ISR address isn't NULL
	if (!address)
	{
		kernelError(kernel_error, "NULL parameter");
		return (status = ERR_BADADDRESS);
	}

	// Initialize the descriptor we're changing
	memset((kernelDescriptor *) &interruptDescriptorTable[number], 0,
		sizeof(kernelDescriptor));

	// Now we can begin constructing the descriptor.  Start working through
	// each of the descriptor's bytes.
	interruptDescriptorTable[number].segSizeByte1 =
		(unsigned char)((int)address & 0x000000FF);
	interruptDescriptorTable[number].segSizeByte2 =
		(unsigned char)(((int)address & 0x0000FF00) >> 8);
	interruptDescriptorTable[number].baseAddress1 =
		(unsigned char)(PRIV_CODE & 0x000000FF);
	interruptDescriptorTable[number].baseAddress2 =
		(unsigned char)((PRIV_CODE & 0x0000FF00) >> 8);
	interruptDescriptorTable[number].baseAddress3 = (unsigned char) 0;
	interruptDescriptorTable[number].attributes1 = (unsigned char) 0x8E;
	interruptDescriptorTable[number].attributes2 =
		(unsigned char)(((int)address & 0x00FF0000) >> 16);
	interruptDescriptorTable[number].baseAddress4 =
		(unsigned char)(((int)address & 0xFF000000) >> 24);

	// There.  We made an interrupt gate descriptor.
	return (status = 0);
}


int kernelDescriptorSetIDTTaskGate(int number, kernelSelector selector)
{
	// This function can be used to install an task gate descriptor
	// in the interrupt descriptor table.  Takes the number of the interrupt
	// as the first parameter, and the address of the Interrupt Service
	// Routine (ISR) as the second.  Returns 0 on success, negative otherwise

	int status = 0;

	// Don't do anything unless we have been initialized first
	if (!initialized)
		// We have not been initialized
		return (status = ERR_NOTINITIALIZED);

	// Make sure that the requested interrupt number doesn't exceed the
	// max size of the table (it's allowed to be zero here, however)
	if (number >= IDT_SIZE)
	{
		kernelError(kernel_error, "Invalid table entry number");
		return (status = ERR_NOSUCHENTRY);
	}

	// Initialize the descriptor we're changing
	memset((kernelDescriptor *) &interruptDescriptorTable[number], 0,
		sizeof(kernelDescriptor));

	// Now we can begin constructing the descriptor.  Start working through
	// each of the descriptor's bytes.
	interruptDescriptorTable[number].segSizeByte1 = (unsigned char) 0;
	interruptDescriptorTable[number].segSizeByte2 = (unsigned char) 0;
	interruptDescriptorTable[number].baseAddress1 =
		(unsigned char)(selector & 0x000000FF);
	interruptDescriptorTable[number].baseAddress2 =
		(unsigned char)((selector & 0x0000FF00) >> 8);
	interruptDescriptorTable[number].baseAddress3 = (unsigned char) 0;
	interruptDescriptorTable[number].attributes1 = (unsigned char) 0x85;
	interruptDescriptorTable[number].attributes2 = (unsigned char) 0;
	interruptDescriptorTable[number].baseAddress4 = (unsigned char) 0;

	// There.  We made a task gate descriptor.
	return (status = 0);
}

