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
//  kernelInterrupt.c
//

// Interrupt handling routines for basic exceptions and hardware interfaces.

#include "kernelInterrupt.h"
#include "kernelError.h"
#include "kernelMalloc.h"
#include "kernelMisc.h"
#include "kernelMultitasker.h"
#include "kernelPic.h"
#include <sys/processor.h>

// Hooked interrupt vectors
static void **vectorList = NULL;
static int numVectors = 0;
static volatile int processingInterrupt = 0;
static int initialized = 0;

#define EXHANDLERX(exceptionNum) {	\
	unsigned exAddress = 0;			\
	int exInterrupts = 0;			\
	processorExceptionEnter(exAddress, exInterrupts);	\
	kernelException(exceptionNum, exAddress);	\
	processorExceptionExit(exInterrupts);	\
}

static void exHandler0(void) EXHANDLERX(EXCEPTION_DIVBYZERO)
static void exHandler1(void) EXHANDLERX(EXCEPTION_DEBUG)
static void exHandler2(void) EXHANDLERX(EXCEPTION_NMI)
static void exHandler3(void) EXHANDLERX(EXCEPTION_BREAK)
static void exHandler4(void) EXHANDLERX(EXCEPTION_OVERFLOW)
static void exHandler5(void) EXHANDLERX(EXCEPTION_BOUNDS)
static void exHandler6(void) EXHANDLERX(EXCEPTION_OPCODE)
static void exHandler7(void) EXHANDLERX(EXCEPTION_DEVNOTAVAIL)
static void exHandler8(void) EXHANDLERX(EXCEPTION_DOUBLEFAULT)
static void exHandler9(void) EXHANDLERX(EXCEPTION_COPROCOVER)
static void exHandler10(void) EXHANDLERX(EXCEPTION_INVALIDTSS)
static void exHandler11(void) EXHANDLERX(EXCEPTION_SEGNOTPRES)
static void exHandler12(void) EXHANDLERX(EXCEPTION_STACK)
static void exHandler13(void) EXHANDLERX(EXCEPTION_GENPROTECT)
static void exHandler14(void) EXHANDLERX(EXCEPTION_PAGE)
static void exHandler15(void) EXHANDLERX(EXCEPTION_RESERVED)
static void exHandler16(void) EXHANDLERX(EXCEPTION_FLOAT)
static void exHandler17(void) EXHANDLERX(EXCEPTION_ALIGNCHECK)
static void exHandler18(void) EXHANDLERX(EXCEPTION_MACHCHECK)

static void intHandlerUnimp(void)
{
	// This is the "unimplemented interrupt" handler

	void *address = NULL;

	processorIsrEnter(address);
	processorIsrExit(address);
}


/////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////
//
// Below here, the functions are exported for external use
//
/////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////

int kernelInterruptInitialize(void)
{
	// This function is called once at startup time to install all
	// of the appropriate interrupt vectors into the Interrupt Descriptor
	// Table.  Returns 0 on success, negative otherwise.

	int status = 0;
	int count;

	// Set all the exception handlers
	kernelDescriptorSetIDTInterruptGate(EXCEPTION_DIVBYZERO, &exHandler0);
	kernelDescriptorSetIDTInterruptGate(EXCEPTION_DEBUG, &exHandler1);
	kernelDescriptorSetIDTInterruptGate(EXCEPTION_NMI, &exHandler2);
	kernelDescriptorSetIDTInterruptGate(EXCEPTION_BREAK, &exHandler3);
	kernelDescriptorSetIDTInterruptGate(EXCEPTION_OVERFLOW, &exHandler4);
	kernelDescriptorSetIDTInterruptGate(EXCEPTION_BOUNDS, &exHandler5);
	kernelDescriptorSetIDTInterruptGate(EXCEPTION_OPCODE, &exHandler6);
	kernelDescriptorSetIDTInterruptGate(EXCEPTION_DEVNOTAVAIL, &exHandler7);
	kernelDescriptorSetIDTInterruptGate(EXCEPTION_DOUBLEFAULT, &exHandler8);
	kernelDescriptorSetIDTInterruptGate(EXCEPTION_COPROCOVER, &exHandler9);
	kernelDescriptorSetIDTInterruptGate(EXCEPTION_INVALIDTSS, &exHandler10);
	kernelDescriptorSetIDTInterruptGate(EXCEPTION_SEGNOTPRES, &exHandler11);
	kernelDescriptorSetIDTInterruptGate(EXCEPTION_STACK, &exHandler12);
	kernelDescriptorSetIDTInterruptGate(EXCEPTION_GENPROTECT, &exHandler13);
	kernelDescriptorSetIDTInterruptGate(EXCEPTION_PAGE, &exHandler14);
	kernelDescriptorSetIDTInterruptGate(EXCEPTION_RESERVED, &exHandler15);
	kernelDescriptorSetIDTInterruptGate(EXCEPTION_FLOAT, &exHandler16);
	kernelDescriptorSetIDTInterruptGate(EXCEPTION_ALIGNCHECK, &exHandler17);
	kernelDescriptorSetIDTInterruptGate(EXCEPTION_MACHCHECK, &exHandler18);

	// Initialize the rest of the table with the vector for the standard
	// "unimplemented" interrupt vector
	for (count = 19; count < IDT_SIZE; count ++)
		kernelDescriptorSetIDTInterruptGate(count, intHandlerUnimp);

	// Note that we've been called
	initialized = 1;

	// Return success
	return (status = 0);
}


void *kernelInterruptGetHandler(int intNumber)
{
	// Returns the address of the handler for the requested interrupt.

	if (!initialized)
		return (NULL);

	if ((intNumber < 0) || (intNumber >= numVectors))
		return (NULL);

	return (vectorList[intNumber]);
}


int kernelInterruptHook(int intNumber, void *handlerAddress,
	kernelSelector handlerTask)
{
	// This allows the requested interrupt number to be hooked by a new
	// handler.  At the moment it doesn't chain them, so anyone who calls
	// this needs to fully implement the handler, or else chain them manually
	// using the 'get handler' function, above.  If you don't know what this
	// means, please stay away from hooking interrupts!  ;)

	int status = 0;
	int vector = 0;

	if (!initialized)
		return (status = ERR_NOTINITIALIZED);

	// Check params
	if (!((handlerAddress && !handlerTask) ||
		(!handlerAddress && handlerTask)))
	{
		kernelError(kernel_error, "Exactly one of handlerAddress or "
			"handlerTask must be set");
		return (status = ERR_INVALID);
	}

	vector = kernelPicGetVector(intNumber);
	if (vector < 0)
		return (status = vector);

	if (intNumber >= numVectors)
	{
		numVectors = (intNumber + 1);

		vectorList = kernelRealloc(vectorList, (numVectors * sizeof(void *)));
		if (!vectorList)
			return (status = ERR_MEMORY);
	}

	if (handlerAddress)
	{
		vectorList[intNumber] = handlerAddress;
		status = kernelDescriptorSetIDTInterruptGate(vector, handlerAddress);
	}
	else
	{
		vectorList[intNumber] = (void *) handlerTask;
		status = kernelDescriptorSetIDTTaskGate(vector, handlerTask);
	}

	return (status);
}


int kernelProcessingInterrupt(void)
{
	return (processingInterrupt & 1);
}


int kernelInterruptGetCurrent(void)
{
	return (processingInterrupt >> 16);
}


void kernelInterruptSetCurrent(int intNumber)
{
	if ((intNumber < 0) || (intNumber >= numVectors))
		kernelError(kernel_error, "Interrupt number %d is out of range",
			intNumber);
	else
		processingInterrupt = ((intNumber << 16) | 1);
}


void kernelInterruptClearCurrent(void)
{
	processingInterrupt = 0;
}

