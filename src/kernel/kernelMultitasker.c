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
//  kernelMultitasker.c
//

// This file contains the C functions belonging to the kernel's
// multitasker

#include "kernelMultitasker.h"
#include "kernelCpu.h"
#include "kernelDebug.h"
#include "kernelEnvironment.h"
#include "kernelError.h"
#include "kernelFile.h"
#include "kernelInterrupt.h"
#include "kernelLog.h"
#include "kernelMain.h"
#include "kernelMalloc.h"
#include "kernelMemory.h"
#include "kernelMisc.h"
#include "kernelNetwork.h"
#include "kernelPage.h"
#include "kernelParameters.h"
#include "kernelPic.h"
#include "kernelShutdown.h"
#include "kernelSysTimer.h"
#include "kernelVariableList.h"
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/processor.h>

#define PROC_KILLABLE(proc) ((proc != kernelProc) && \
	(proc != exceptionProc) && \
	(proc != idleProc) && \
	(proc != kernelCurrentProcess))

#define SET_PORT_BIT(bitmap, port) \
	do { bitmap[port / 8] |=  (1 << (port % 8)); } while (0)
#define UNSET_PORT_BIT(bitmap, port) \
	do { bitmap[port / 8] &= ~(1 << (port % 8)); } while (0)
#define GET_PORT_BIT(bitmap, port) ((bitmap[port / 8] >> (port % 8)) & 0x01)

// Global multitasker stuff
static int multitaskingEnabled = 0;
static volatile int processIdCounter = KERNELPROCID;
static kernelProcess *kernelProc = NULL;
static kernelProcess *idleProc = NULL;
static kernelProcess *exceptionProc = NULL;
static volatile int processingException = 0;
static volatile unsigned exceptionAddress = 0;
static volatile int schedulerSwitchedByCall = 0;
static kernelProcess *fpuProcess = NULL;

// We allow the pointer to the current process to be exported, so that when a
// process uses system calls, there is an easy way for the process to get
// information about itself.
kernelProcess *kernelCurrentProcess = NULL;

// Process queue for CPU execution
static kernelProcess *processQueue[MAX_PROCESSES];
static volatile int numQueued = 0;

// Things specific to the scheduler.  The scheduler process is just a
// convenient place to keep things, we don't use all of it and it doesn't go
// in the queue
static kernelProcess *schedulerProc = NULL;
static volatile int schedulerStop = 0;
static void (*oldSysTimerHandler)(void) = NULL;
static volatile unsigned schedulerTimeslices = 0;

// An array of exception types.  The selectors are initialized later.
static struct {
	int index;
	kernelSelector tssSelector;
	const char *a;
	const char *name;
	int (*handler)(void);

} exceptionVector[19] = {
	{ EXCEPTION_DIVBYZERO, 0, "a", "divide-by-zero", NULL },
	{ EXCEPTION_DEBUG, 0, "a", "debug", NULL },
	{ EXCEPTION_NMI, 0, "a", "non-maskable interrupt (NMI)", NULL },
	{ EXCEPTION_BREAK, 0, "a", "breakpoint", NULL },
	{ EXCEPTION_OVERFLOW, 0, "a", "overflow", NULL },
	{ EXCEPTION_BOUNDS, 0, "a", "out-of-bounds", NULL },
	{ EXCEPTION_OPCODE, 0, "an", "invalid opcode", NULL },
	{ EXCEPTION_DEVNOTAVAIL, 0, "a", "device not available", NULL },
	{ EXCEPTION_DOUBLEFAULT, 0, "a", "double-fault", NULL },
	{ EXCEPTION_COPROCOVER, 0, "a", "co-processor segment overrun", NULL },
	{ EXCEPTION_INVALIDTSS, 0, "an", "invalid TSS", NULL },
	{ EXCEPTION_SEGNOTPRES, 0, "a", "segment not present", NULL },
	{ EXCEPTION_STACK, 0, "a", "stack", NULL },
	{ EXCEPTION_GENPROTECT, 0, "a", "general protection", NULL },
	{ EXCEPTION_PAGE, 0, "a", "page fault", NULL },
	{ EXCEPTION_RESERVED, 0, "a", "\"reserved\"", NULL },
	{ EXCEPTION_FLOAT, 0, "a", "floating point", NULL },
	{ EXCEPTION_ALIGNCHECK, 0, "an", "alignment check", NULL },
	{ EXCEPTION_MACHCHECK, 0, "a", "machine check", NULL }
};


static void debugTSS(kernelProcess *proc, char *buffer, int len)
{
	if (!buffer)
		return;

	snprintf(buffer, len, "Multitasker debug TSS selector:\n");
	snprintf((buffer + strlen(buffer)), (len - strlen(buffer)),
		"  oldTSS=%08x", proc->taskStateSegment.oldTSS);

	snprintf((buffer + strlen(buffer)), (len - strlen(buffer)),
		"  ESP0=%08x SS0=%08x\n", proc->taskStateSegment.ESP0,
		proc->taskStateSegment.SS0);

	snprintf((buffer + strlen(buffer)), (len - strlen(buffer)),
		"  ESP1=%08x SS1=%08x\n", proc->taskStateSegment.ESP1,
		proc->taskStateSegment.SS1);

	snprintf((buffer + strlen(buffer)), (len - strlen(buffer)),
		"  ESP2=%08x SS2=%08x\n", proc->taskStateSegment.ESP2,
		proc->taskStateSegment.SS2);

	snprintf((buffer + strlen(buffer)), (len - strlen(buffer)),
		"  CR3=%08x EIP=%08x EFLAGS=%08x\n", proc->taskStateSegment.CR3,
		proc->taskStateSegment.EIP, proc->taskStateSegment.EFLAGS);

	// Skip general-purpose registers for now -- not terribly interesting

	snprintf((buffer + strlen(buffer)), (len - strlen(buffer)),
		"  ESP=%08x EBP=%08x ESI=%08x EDI=%08x\n",
		proc->taskStateSegment.ESP, proc->taskStateSegment.EBP,
		proc->taskStateSegment.ESI, proc->taskStateSegment.EDI);

	snprintf((buffer + strlen(buffer)), (len - strlen(buffer)),
		"  CS=%08x SS=%08x\n", proc->taskStateSegment.CS,
		proc->taskStateSegment.SS);

	snprintf((buffer + strlen(buffer)), (len - strlen(buffer)),
		"  ES=%08x DS=%08x FS=%08x GS=%08x\n", proc->taskStateSegment.ES,
		proc->taskStateSegment.DS, proc->taskStateSegment.FS,
		proc->taskStateSegment.GS);

	snprintf((buffer + strlen(buffer)), (len - strlen(buffer)),
		"  LDTSelector=%08x IOMapBase=%04x\n",
		proc->taskStateSegment.LDTSelector,
		proc->taskStateSegment.IOMapBase);
}


static kernelProcess *getProcessById(int processId)
{
	// This function is used to find a process' pointer based on the process
	// Id.  Nothing fancy -- it just searches through the list.  Maybe later
	// it can be some kind of fancy sorting/searching procedure.  Returns NULL
	// if the process doesn't exist

	kernelProcess *theProcess = NULL;
	int count;

	for (count = 0; count < numQueued; count ++)
	{
		if (processQueue[count]->processId == processId)
		{
			theProcess = processQueue[count];
			break;
		}
	}

	// If we didn't find it, this will still be NULL
	return (theProcess);
}


static kernelProcess *getProcessByName(const char *name)
{
	// As above, but searches by name

	kernelProcess *theProcess = NULL;
	int count;

	for (count = 0; count < numQueued; count ++)
	{
		if (!strcmp((char *) processQueue[count]->name, name))
		{
			theProcess = processQueue[count];
			break;
		}
	}

	// If we didn't find it, this will still be NULL
	return (theProcess);
}


static inline int requestProcess(kernelProcess **processPointer)
{
	// This function is used to allocate new process control memory.  It
	// should be passed a reference to a pointer that will point to the new
	// process, if allocated successfully.

	int status = 0;
	kernelProcess *newProcess = NULL;

	// Make sure the pointer->pointer parameter we were passed isn't NULL
	if (!processPointer)
		// Oops.
		return (status = ERR_NULLPARAMETER);

	newProcess = kernelMalloc(sizeof(kernelProcess));
	if (!newProcess)
		return (status = ERR_MEMORY);

	// Success.  Set the pointer for the calling process
	*processPointer = newProcess;
	return (status = 0);
}


static inline int releaseProcess(kernelProcess *killProcess)
{
	// This function is used to free process control memory.  It should be
	// passed the process Id of the process to kill.  It returns 0 on success,
	// negative otherwise

	int status = 0;

	status = kernelFree((void *) killProcess);

	return (status);
}


static int addProcessToQueue(kernelProcess *targetProcess)
{
	// This function will add a process to the task queue.  It returns zero on
	// success, negative otherwise

	int status = 0;
	int count;

	if (!targetProcess)
		// The process does not exist, or is not accessible
		return (status = ERR_NOSUCHPROCESS);

	// Make sure the priority is a legal value
	if ((targetProcess->priority < 0) ||
		(targetProcess->priority > (PRIORITY_LEVELS - 1)))
	{
		// The process' priority is an illegal value
		return (status = ERR_INVALID);
	}

	// Search the process queue to make sure it isn't already present
	for (count = 0; count < numQueued; count ++)
	{
		if (processQueue[count] == targetProcess)
			// Oops, it's already there
			return (status = ERR_ALREADY);
	}

	// OK, now we can add the process to the queue
	processQueue[numQueued++] = targetProcess;

	// Done
	return (status = 0);
}


static int removeProcessFromQueue(kernelProcess *targetProcess)
{
	// This function will remove a process from the task queue.  It returns
	// zero on success, negative otherwise

	int status = 0;
	int processPosition = 0;
	int count;

	if (!targetProcess)
		// The process does not exist, or is not accessible
		return (status = ERR_NOSUCHPROCESS);

	// Search the queue for the matching process
	for (count = 0; count < numQueued; count ++)
	{
		if (processQueue[count] == targetProcess)
		{
			processPosition = count;
			break;
		}
	}

	// Make sure we found the process
	if (processQueue[processPosition] != targetProcess)
		// The process is not in the task queue
		return (status = ERR_NOSUCHPROCESS);

	// Subtract one from the number of queued processes
	numQueued -= 1;

	// OK, now we can remove the process from the queue.  If there are one or
	// more remaining processes in this queue, we will shorten the queue by
	// moving the LAST process into the spot we're vacating
	if ((numQueued > 0) && (processPosition != numQueued))
		processQueue[processPosition] = processQueue[numQueued];

	// Done
	return (status = 0);
}


static int createTaskStateSegment(kernelProcess *theProcess)
{
	// This function will create a TSS (Task State Segment) for a new process
	// based on the attributes of the process.  This function relies on the
	// privilege, userStackSize, and superStackSize attributes having been
	// previously set.  Returns 0 on success, negative on error.

	int status = 0;

	// Get a free descriptor for the process' TSS
	status = kernelDescriptorRequest(&theProcess->tssSelector);
	if ((status < 0) || !theProcess->tssSelector)
		// Crap.  An error getting a free descriptor.
		return (status);

	// Fill in the process' Task State Segment descriptor
	status = kernelDescriptorSet(
		theProcess->tssSelector,	// TSS selector number
		&theProcess->taskStateSegment, // Starts at...
		sizeof(kernelTSS),			// Limit of a TSS segment
		1,							// Present in memory
		PRIVILEGE_SUPERVISOR,		// TSSs are supervisor privilege level
		0,							// TSSs are system segs
		0xB,						// TSS, 32-bit, busy
		0,							// 0 for SMALL size granularity
		0);							// Must be 0 in TSS
	if (status < 0)
	{
		// Crap.  An error getting a free descriptor.
		kernelDescriptorRelease(theProcess->tssSelector);
		return (status);
	}

	// Now, fill in the TSS (Task State Segment) for the new process.  Parts
	// of this will be different depending on whether this is a user or
	// supervisor mode process

	memset((void *) &theProcess->taskStateSegment, 0, sizeof(kernelTSS));

	// Set the IO bitmap's offset
	theProcess->taskStateSegment.IOMapBase = IOBITMAP_OFFSET;

	if (theProcess->processorPrivilege == PRIVILEGE_SUPERVISOR)
	{
		theProcess->taskStateSegment.CS = PRIV_CODE;
		theProcess->taskStateSegment.DS = PRIV_DATA;
		theProcess->taskStateSegment.SS = PRIV_STACK;
	}
	else
	{
		theProcess->taskStateSegment.CS = USER_CODE;
		theProcess->taskStateSegment.DS = USER_DATA;
		theProcess->taskStateSegment.SS = USER_STACK;

		// Turn off access to all I/O ports by default
		memset((void *) theProcess->taskStateSegment.IOMap, 0xFF,
			PORTS_BYTES);
	}

	// All other data segments same as DS
	theProcess->taskStateSegment.ES = theProcess->taskStateSegment.FS =
		theProcess->taskStateSegment.GS = theProcess->taskStateSegment.DS;

	theProcess->taskStateSegment.ESP = ((unsigned) theProcess->userStack +
		(theProcess->userStackSize - sizeof(void *)));

	if (theProcess->processorPrivilege != PRIVILEGE_SUPERVISOR)
	{
		theProcess->taskStateSegment.SS0 = PRIV_STACK;
		theProcess->taskStateSegment.ESP0 = ((unsigned)
			theProcess->superStack + (theProcess->superStackSize -
				sizeof(int)));
	}

	theProcess->taskStateSegment.EFLAGS = 0x00000202; // Interrupts enabled
	theProcess->taskStateSegment.CR3 = (unsigned)
		theProcess->pageDirectory->physical;

	// All remaining values will be NULL from initialization.  Note that this
	// includes the EIP.

	// Return success
	return (status = 0);
}


static int createNewProcess(const char *name, int priority, int privilege,
	processImage *execImage, int newPageDir)
{
	// This function is used to create a new process in the process queue.  It
	// makes a "defaults" kind of process -- it sets up all of the process'
	// attributes with default values.  If the calling process wants something
	// different, it should reset those attributes afterward.  If successful,
	// it returns the processId of the new process.  Otherwise, it returns
	// negative.

	int status = 0;
	kernelProcess *newProcess = NULL;
	void *stackMemoryAddr = NULL;
	unsigned physicalCodeData = 0;
	int argMemorySize = 0;
	char *argMemory = NULL;
	char *oldArgPtr = NULL;
	char *newArgPtr = NULL;
	int *stackArgs = NULL;
	char **argv = NULL;
	int length = 0;
	int count;

	// Don't bother checking the parameters, as the external functions should
	// have done this already.

	// We need to see if we can get some fresh process control memory
	status = requestProcess(&newProcess);
	if (status < 0)
		return (status);

	if (!newProcess)
	{
		kernelError(kernel_error, "New process structure is NULL");
		return (status = ERR_NOFREE);
	}

	// Ok, we got a new, fresh process.  We need to start filling in some of
	// the process' data (after initializing it, of course)
	memset((void *) newProcess, 0, sizeof(kernelProcess));

	// Fill in the process name
	strncpy((char *) newProcess->name, name, MAX_PROCNAME_LENGTH);
	newProcess->name[MAX_PROCNAME_LENGTH - 1] = '\0';

	// Copy the process image data
	memcpy((processImage *) &newProcess->execImage, &execImage,
		sizeof(processImage));

	// Fill in the process' Id number
	newProcess->processId = processIdCounter++;

	// By default, the type is process
	newProcess->type = proc_normal;

	// Now, if the process Id is KERNELPROCID, then we are creating the kernel
	// process, and it will be its own parent.  Otherwise, get the current
	// process and make IT be the parent of this new process
	if (newProcess->processId == KERNELPROCID)
	{
		newProcess->parentProcessId = newProcess->processId;
		newProcess->userId = 1;   // Admin
		// Give it "/" as current working directory
		strncpy((char *) newProcess->currentDirectory, "/", 2);
	}
	else
	{
		// Make sure the current process isn't NULL
		if (!kernelCurrentProcess)
		{
			kernelError(kernel_error, "No current process!");
			status = ERR_NOSUCHPROCESS;
			goto out;
		}

		// Fill in the process' parent Id number
		newProcess->parentProcessId = kernelCurrentProcess->processId;
		// Fill in the process' user Id number
		newProcess->userId = kernelCurrentProcess->userId;
		// Fill in the current working directory
		strncpy((char *) newProcess->currentDirectory,
			(char *) kernelCurrentProcess->currentDirectory, MAX_PATH_LENGTH);
		newProcess->currentDirectory[MAX_PATH_LENGTH - 1] = '\0';
	}

	// Fill in the process' priority level
	newProcess->priority = priority;

	// Fill in the process' privilege level
	newProcess->privilege = privilege;

	// Fill in the process' processor privilege level.  The kernel and its
	// threads get PRIVILEGE_SUPERVISOR, all others get PRIVILEGE_USER.
	if (execImage->virtualAddress >= (void *) KERNEL_VIRTUAL_ADDRESS)
		newProcess->processorPrivilege = PRIVILEGE_SUPERVISOR;
	else
		newProcess->processorPrivilege = PRIVILEGE_USER;

	// The thread's initial state will be "stopped"
	newProcess->state = proc_stopped;

	// Add the process to the process queue so we can continue whilst doing
	// things like changing memory ownerships
	status = addProcessToQueue(newProcess);
	if (status < 0)
		// Not able to queue the process.
		goto out;

	// Do we need to create a new page directory and a set of page tables for
	// this process?
	if (newPageDir)
	{
		if (!execImage->virtualAddress || !execImage->code ||
			!execImage->codeSize || !execImage->data || !execImage->dataSize ||
			!execImage->imageSize)
		{
			kernelError(kernel_error, "New process \"%s\" executable image is "
				"missing data", name);
			status = ERR_NODATA;
			goto out;
		}

		// We need to make a new page directory, etc.
		newProcess->pageDirectory =
			kernelPageNewDirectory(newProcess->processId);
		if (!newProcess->pageDirectory)
		{
			// Not able to setup a page directory
			status = ERR_NOVIRTUAL;
			goto out;
		}

		// Get the physical address of the code/data
		physicalCodeData = kernelPageGetPhysical(newProcess->parentProcessId,
			execImage->code);

		// Make the process own its code/data memory.  Don't remap it yet
		// because we want to map it at the requested virtual address.
		status = kernelMemoryChangeOwner(newProcess->parentProcessId,
			newProcess->processId, 0, execImage->code, NULL);
		if (status < 0)
			// Couldn't make the process own its memory
			goto out;

		// Remap the code/data to the requested virtual address.
		status = kernelPageMap(newProcess->processId, physicalCodeData,
			execImage->virtualAddress, execImage->imageSize);
		if (status < 0)
			// Couldn't map the process memory
			goto out;

		// Code should be read-only
		status = kernelPageSetAttrs(newProcess->processId, 0,
			PAGEFLAG_WRITABLE, execImage->virtualAddress, execImage->codeSize);
		if (status < 0)
			goto out;
	}
	else
	{
		// This process will share a page directory with its parent
		newProcess->pageDirectory = kernelPageShareDirectory(
			newProcess->parentProcessId, newProcess->processId);
		if (!newProcess->pageDirectory)
		{
			status = ERR_NOVIRTUAL;
			goto out;
		}
	}

	// Give the process a stack
	newProcess->userStackSize = DEFAULT_STACK_SIZE;
	if (newProcess->processorPrivilege != PRIVILEGE_SUPERVISOR)
		newProcess->superStackSize = DEFAULT_SUPER_STACK_SIZE;

	stackMemoryAddr = kernelMemoryGet((newProcess->userStackSize +
		newProcess->superStackSize), "process stack");
	if (!stackMemoryAddr)
	{
		// We couldn't make a stack for the new process.  Maybe the system
		// doesn't have anough available memory?
		status = ERR_MEMORY;
		goto out;
	}

	// Copy 'argc' and 'argv' arguments to the new process' stack while we
	// still own the stack memory.

	// Calculate the amount of memory we need to allocate for argument data.
	// Leave space for pointers to the strings, since the (int argc,
	// char *argv[]) scheme means just 2 values on the stack: an integer
	// an a pointer to an array of char* pointers...
	argMemorySize = ((execImage->argc + 1) * sizeof(char *));
	for (count = 0; count < execImage->argc; count ++)
	{
		if (execImage->argv[count])
			argMemorySize += (strlen(execImage->argv[count]) + 1);
	}

	// Get memory for the argument data
	argMemory = kernelMemoryGet(argMemorySize, "process arguments");
	if (!argMemory)
	{
		status = ERR_MEMORY;
		goto out;
	}

	// Change ownership to the new process.  If it has its own page directory,
	// renap, and share it back with this process.

	if (kernelMemoryChangeOwner(newProcess->parentProcessId,
		newProcess->processId, (newPageDir? 1 : 0 /* remap */), argMemory,
		(newPageDir? (void **) &newArgPtr : NULL /* newVirtual */)) < 0)
	{
		status = ERR_MEMORY;
		goto out;
	}

	if (newPageDir)
	{
		if (kernelMemoryShare(newProcess->processId,
			newProcess->parentProcessId, newArgPtr, (void **) &argMemory) < 0)
		{
			status = ERR_MEMORY;
			goto out;
		}
	}
	else
	{
		newArgPtr = argMemory;
	}

	oldArgPtr = argMemory;

	// Set pointers to the beginning stack location for the arguments
	stackArgs = (stackMemoryAddr + newProcess->userStackSize -
		(2 * sizeof(int)));
	stackArgs[0] = execImage->argc;
	stackArgs[1] = (int) newArgPtr;

	argv = (char **) oldArgPtr;
	oldArgPtr += ((execImage->argc + 1) * sizeof(char *));
	newArgPtr += ((execImage->argc + 1) * sizeof(char *));

	// Copy the args into argv
	for (count = 0; count < execImage->argc; count ++)
	{
		if (execImage->argv[count])
		{
			strcpy((oldArgPtr + length), execImage->argv[count]);
			argv[count] = (newArgPtr + length);
			length += (strlen(execImage->argv[count]) + 1);
		}
	}

	// argv[argc] is supposed to be a NULL pointer, according to some standard
	// or other
	argv[execImage->argc] = NULL;

	if (newPageDir)
	{
		// Unmap the argument space from this process
		kernelPageUnmap(newProcess->parentProcessId, argMemory, argMemorySize);
		argMemory = NULL;
	}

	// Make the process own its stack memory
	status = kernelMemoryChangeOwner(newProcess->parentProcessId,
		newProcess->processId, 1 /* remap */, stackMemoryAddr,
		(void **) &newProcess->userStack);
	if (status < 0)
		// Couldn't make the process own its stack memory
		goto out;

	stackMemoryAddr = NULL;

	// Make the topmost page of the user stack privileged, so that we have
	// have a 'guard page' that produces a page fault in case of (userspace)
	// stack overflow
	kernelPageSetAttrs(newProcess->processId, 0 /* clear */, PAGEFLAG_USER,
		newProcess->userStack, MEMORY_PAGE_SIZE);

	if (newProcess->processorPrivilege != PRIVILEGE_SUPERVISOR)
	{
		// Get the new virtual address of supervisor stack
		newProcess->superStack = (newProcess->userStack + DEFAULT_STACK_SIZE);

		// Make the entire supervisor stack privileged
		kernelPageSetAttrs(newProcess->processId, 0, PAGEFLAG_USER,
			newProcess->superStack, newProcess->superStackSize);
	}

	// Create the TSS (Task State Segment) for this process.
	status = createTaskStateSegment(newProcess);
	if (status < 0)
		// Not able to create the TSS
		goto out;

	// Adjust the stack pointer to account for the arguments that we copied to
	// the process' stack
	newProcess->taskStateSegment.ESP -= sizeof(int);

	// Set the EIP to the entry point
	newProcess->taskStateSegment.EIP = (unsigned) execImage->entryPoint;

	// Get memory for the user process environment structure
	newProcess->environment = kernelMalloc(sizeof(variableList));
	if (!newProcess->environment)
	{
		status = ERR_MEMORY;
		goto out;
	}

	// Return the processId on success.
	status = newProcess->processId;

out:
	if (status < 0)
	{
		if (stackMemoryAddr)
			kernelMemoryRelease(stackMemoryAddr);

		if (argMemory)
			kernelMemoryRelease(argMemory);

		removeProcessFromQueue(newProcess);
		releaseProcess(newProcess);
	}

	return (status);
}


static int deleteProcess(kernelProcess *killProcess)
{
	// Does all the work of actually destroyng a process when there's really
	// no more use for it.  This occurs after all descendent threads have
	// terminated, for example.

	int status = 0;

	// Processes cannot delete themselves
	if (killProcess == kernelCurrentProcess)
	{
		kernelError(kernel_error, "Process %d cannot delete itself",
			killProcess->processId);
		return (status = ERR_INVALID);
	}

	// We need to deallocate the TSS descriptor allocated to the process, if
	// it has one
	if (killProcess->tssSelector)
	{
		status = kernelDescriptorRelease(killProcess->tssSelector);
		if (status < 0)
		{
			// If this was unsuccessful, we don't want to continue and "lose"
			// the descriptor
			kernelError(kernel_error, "Can't release TSS");
			return (status);
		}
	}

	// If the process has a signal stream, destroy it
	if (killProcess->signalStream.buffer)
		kernelStreamDestroy(&killProcess->signalStream);

	// Deallocate all memory owned by this process
	status = kernelMemoryReleaseAllByProcId(killProcess->processId);
	if (status < 0)
	{
		// If this deallocation was unsuccessful, we don't want to deallocate
		// the process structure.  If we did, the memory would become "lost".
		kernelError(kernel_error, "Can't release process memory");
		return (status);
	}

	// Delete the page table we created for this process
	status = kernelPageDeleteDirectory(killProcess->processId);
	if (status < 0)
	{
		// If this deletion was unsuccessful, we don't want to deallocate the
		// process structure.  If we did, the page directory would become
		// "lost".
		kernelError(kernel_error, "Can't release page directory");
		return (status);
	}

	// If this is a normal process and it has an environment memory structure,
	// deallocate it (threads share environment with their parents).
	if ((killProcess->type == proc_normal) && killProcess->environment)
	{
		status = kernelFree(killProcess->environment);
		if (status < 0)
		{
			kernelError(kernel_error, "Can't release environment structure");
			return (status);
		}
	}

	// If this is a normal process and there's a symbol table for it,
	// deallocate the table (threads share tables with their parents).
	if ((killProcess->type == proc_normal) && killProcess->symbols)
	{
		status = kernelFree(killProcess->symbols);
		if (status < 0)
		{
			kernelError(kernel_error, "Can't release symbol table");
			return (status);
		}
	}

	// If this process was using the FPU, it's not any more.
	if (fpuProcess == killProcess)
		fpuProcess = NULL;

	// Remove the process from the multitasker's process queue.
	status = removeProcessFromQueue(killProcess);
	if (status < 0)
	{
		// Not able to remove the process
		kernelError(kernel_error, "Can't dequeue process");
		return (status);
	}

	// Finally, release the process structure
	status = releaseProcess(killProcess);
	if (status < 0)
	{
		kernelError(kernel_error, "Can't release process structure");
		return (status);
	}

	return (status = 0);
}


static void exceptionHandler(void)
{
	// This code is the general exception handler.  Before multitasking
	// starts, it will be called as a function in the exception context (the
	// context of the process that experienced the exception).  After
	// multitasking starts, it is a separate kernel thread that sleeps until
	// woken up.

	int status = 0;
	kernelProcess *targetProc = NULL;
	char *message = NULL;
	char *details = NULL;
	const char *symbolName = NULL;
	const char *kernOrApp = NULL;

	message = kernelMalloc(MAXSTRINGLENGTH);
	details = kernelMalloc(MAXSTRINGLENGTH);
	if (!message || !details)
	{
		status = ERR_MEMORY;
		goto out;
	}

	while (1)
	{
		// We got an exception.

		targetProc = kernelCurrentProcess;
		kernelCurrentProcess = exceptionProc;

		if (multitaskingEnabled)
		{
			if (!targetProc)
			{
				// We have to make an error here.  We can't return to the
				// program that caused the exception, and we can't tell the
				// multitasker to kill it.  We'd better make a kernel panic.
				kernelPanic("Exception handler unable to determine current "
					"process");
			}
			else
			{
				targetProc->state = proc_stopped;
			}
		}

		if (!multitaskingEnabled || (targetProc == kernelProc))
			sprintf(message, "The kernel has experienced");
		else
			sprintf(message, "Process \"%s\" caused", targetProc->name);

		sprintf((message + strlen(message)), " %s %s exception",
			exceptionVector[processingException].a,
			exceptionVector[processingException].name);

		if (exceptionAddress >= KERNEL_VIRTUAL_ADDRESS)
			kernOrApp = "kernel";
		else
			kernOrApp = "application";

		if (multitaskingEnabled)
		{
			// Find roughly the symbolic address where the exception happened
			symbolName = kernelLookupClosestSymbol(targetProc,
				(void *) exceptionAddress);
		}

		if (symbolName)
		{
			sprintf((message + strlen(message)), " in %s function %s (%08x)",
				kernOrApp, symbolName, exceptionAddress);
		}
		else
		{
			sprintf((message + strlen(message)), " at %s address %08x",
				kernOrApp, exceptionAddress);
		}

		if (kernelProcessingInterrupt())
		{
			sprintf((message + strlen(message)), " while processing interrupt "
				"%d", kernelInterruptGetCurrent());

			// If the fault occurred while we were processing an interrupt, we
			// should tell the PIC that the interrupt service function is
			// finished.  It's not really fair to kill a process because an
			// interrupt handler is screwy, but that's what we have to do for
			// the time being.
			kernelPicEndOfInterrupt(0xFF);
		}

		kernelError(kernel_error, "%s", message);

		if (multitaskingEnabled)
		{
			// Get process info
			debugTSS(targetProc, details, MAXSTRINGLENGTH);

			// Try a stack trace
			kernelStackTrace(targetProc, (details + strlen(details)),
				(MAXSTRINGLENGTH - strlen(details)));

			// Output to the console
			kernelTextPrintLine("%s", details);
		}

		if (!multitaskingEnabled || (targetProc == kernelProc))
			// If it's the kernel, we're finished
			kernelPanic("%s", message);

		// If we're in graphics mode, make an error dialog (but don't get into
		// an endless loop if the crashed process was an error dialog thread
		// itself).
		if (kernelGraphicsAreEnabled() && strcmp((char *) targetProc->name,
			ERRORDIALOG_THREADNAME))
		{
			kernelErrorDialog("Application Exception", message, details);
		}

		// The scheduler may now dismantle the process
		targetProc->state = proc_finished;

		kernelInterruptClearCurrent();
		processingException = 0;
		exceptionAddress = 0;

		// Yield the timeslice back to the scheduler.  The scheduler will take
		// care of dismantling the process
		kernelMultitaskerYield();
	}

out:
	if (message)
		kernelFree(message);
	if (details)
		kernelFree(details);

	kernelMultitaskerTerminate(status);
}


static int spawnExceptionThread(void)
{
	// This function will initialize the kernel's exception handler thread. It
	// should be called after multitasking has been initialized.

	int status = 0;
	int procId = 0;

	// Create the kernel's exception handler thread.
	procId = kernelMultitaskerSpawn(&exceptionHandler, "exception thread", 0,
		NULL);
	if (procId < 0)
		return (status = procId);

	exceptionProc = getProcessById(procId);
	if (!exceptionProc)
		return (status = ERR_NOCREATE);

	// Set the process state to sleep
	exceptionProc->state = proc_sleeping;

	status = kernelDescriptorSet(
		exceptionProc->tssSelector,	// TSS selector
		&exceptionProc->taskStateSegment, // Starts at...
		sizeof(kernelTSS),			// Maximum size of a TSS selector
		1,							// Present in memory
		PRIVILEGE_SUPERVISOR,		// Highest privilege level
		0,							// TSS's are system segs
		0x9,						// TSS, 32-bit, non-busy
		0,							// 0 for SMALL size granularity
		0);							// Must be 0 in TSS
	if (status < 0)
		// Something went wrong
		return (status);

	// Interrupts should always be disabled for this task
	exceptionProc->taskStateSegment.EFLAGS = 0x00000002;

	return (status = 0);
}


__attribute__((noreturn))
static void idleThread(void)
{
	// This is the idle task.  It runs in this loop whenever no other
	// processes need the CPU.  This should be run at the absolute lowest
	// possible priority so that it will not be run unless there is absolutely
	// nothing else in the other queues that is ready.

	int count;

	while (1)
	{
		// Idle the processor until something happens
		processorIdle();

		// Loop through the process list looking for any that have changed
		// state to "I/O ready".
		for (count = 0; count < numQueued; count ++)
		{
			if (processQueue[count]->state == proc_ioready)
			{
				kernelMultitaskerYield();
				break;
			}
		}
	}
}


static int spawnIdleThread(void)
{
	// This function will create the idle thread at initialization time.
	// Returns 0 on success, negative otherwise.

	int status = 0;
	int idleProcId = 0;

	// The idle thread needs to be a child of the kernel
	idleProcId = kernelMultitaskerSpawn(idleThread, "idle thread", 0, NULL);
	if (idleProcId < 0)
		return (idleProcId);

	idleProc = getProcessById(idleProcId);
	if (!idleProc)
		return (status = ERR_NOSUCHPROCESS);

	// Set it to the lowest priority
	status = kernelMultitaskerSetProcessPriority(idleProcId,
		(PRIORITY_LEVELS - 1));
	if (status < 0)
	{
		// There's no reason we should have to fail here, but make a warning
		kernelError(kernel_warn, "The multitasker was unable to lower the "
			"priority of the idle thread");
	}

	// Return success
	return (status = 0);
}


static int markTaskBusy(int tssSelector, int busy)
{
	// This function gets the requested TSS selector from the GDT and marks it
	// as busy/not busy.  Returns negative on error.

	int status = 0;
	kernelDescriptor descriptor;

	// Initialize our empty descriptor
	memset(&descriptor, 0, sizeof(kernelDescriptor));

	// Fill out our descriptor with data from the "official" one from the GDT
	// that corresponds to the selector we were given
	status = kernelDescriptorGet(tssSelector, &descriptor);
	if (status < 0)
		return (status);

	// Ok, now we can change the selector in the table
	if (busy)
		descriptor.attributes1 |= 0x2;
	else
		descriptor.attributes1 &= ~0x2;

	// Re-set the descriptor in the GDT
	status =  kernelDescriptorSetUnformatted(tssSelector,
		descriptor.segSizeByte1, descriptor.segSizeByte2,
		descriptor.baseAddress1, descriptor.baseAddress2,
		descriptor.baseAddress3, descriptor.attributes1,
		descriptor.attributes2, descriptor.baseAddress4);
	if (status < 0)
		return (status);

	// Return success
	return (status = 0);
}


static int schedulerShutdown(void)
{
	// This function will perform all of the necessary shutdown to stop the
	// sheduler and return control to the kernel's main task.  This will
	// probably only be useful at system shutdown time.

	// NOTE that this function should NEVER be called directly.  If this
	// advice is ignored, you have no assurance that things will occur the way
	// you might expect them to.  To shut down the scheduler, set the variable
	// schedulerStop to a nonzero value.  The scheduler will then invoke this
	// function when it's ready.

	int status = 0;

	// Restore the normal operation of the system timer 0, which is mode 3,
	// initial count of 0
	status = kernelSysTimerSetupTimer(0 /* timer */, 3 /* mode */,
		0 /* count 0x10000 */);
	if (status < 0)
		kernelError(kernel_warn, "Could not restore system timer");

	// Remove the task gate that we were using to capture the timer interrupt.
	// Replace it with the old default timer interrupt handler
	kernelInterruptHook(INTERRUPT_NUM_SYSTIMER, oldSysTimerHandler, NULL);

	// Give exclusive control to the current task
	markTaskBusy(kernelCurrentProcess->tssSelector, 0);
	processorFarJump(kernelCurrentProcess->tssSelector);

	// We should never get here
	return (status = 0);
}


static kernelProcess *chooseNextProcess(void)
{
	// Loops through the process queue, and determines which process to run
	// next

	unsigned long long theTime = 0;
	kernelProcess *miscProcess = NULL;
	kernelProcess *nextProcess = NULL;
	unsigned processWeight = 0;
	unsigned topProcessWeight = 0;
	int count;

	// Here is where we make decisions about which tasks to schedule, and when.
	// Below is a brief description of the scheduling algorithm.

	// Priority level 0 (highest-priority) processes will be "real time"
	// scheduled.  When there are any processes running and ready at this
	// priority level, they will be serviced to the exclusion of all processes
	// not at level 0.  Not even the kernel process has this level of
	// priority.

	// The last (lowest-priority) priority level will be "background"
	// scheduled.  Processes at this level will only receive processor time
	// when there are no ready processes at any other level.  Unlike processes
	// at any other level, it will be possible for background processes to
	// starve.

	// The number of priority levels is flexible based on the configuration
	// macro in the multitasker's header file.  However, the "special" levels
	// mentioned above will exhibit the same behavior regardless of the number
	// of "normal" priority levels in the system.

	// Amongst all of the processes at other priority levels, there will be a
	// more even-handed approach to scheduling.  We will attempt a fair
	// algorithm with a weighting scheme.  Among the weighting variables will
	// be the following: priority, waiting time, and "shortness".  Shortness
	// will probably come later (shortest-job-first), so for now we will
	// concentrate on priority and waiting time.  The formula will look like
	// this:
	//
	// weight = ((PRIORITY_LEVELS - task_priority) * PRIORITY_RATIO) +
	//		wait_time
	//
	// This means that the inverse of the process priority will be multiplied
	// by the "priority ratio", and to that will be added the current waiting
	// time.  For example, if we have 4 priority levels, the priority ratio is
	// 3, and we have two tasks as follows:
	//
	//	Task 1: priority=1, waiting time=4
	//	Task 2: priority=2, waiting time=6
	//
	// then
	//
	//	task1Weight = ((4 - 1) * 3) + 4 = 13  <- winner
	//	task2Weight = ((4 - 2) * 3) + 6 = 12
	//
	// Thus, even though task 2 has been waiting longer, task 1's higher
	// priority wins.  However in a slightly different scenario -- using the
	// same constants -- if we had:
	//
	//	Task 1: priority=1, waiting time=3
	//	Task 2: priority=2, waiting time=7
	//
	// then
	//
	//	task1Weight = ((4 - 1) * 3) + 3 = 12
	//	task2Weight = ((4 - 2) * 3) + 7 = 13  <- winner
	//
	// In this case, task 2 gets to run since it has been waiting long enough
	// to overcome task 1's higher priority.  This possibility helps to ensure
	// that no processes will starve.  The priority ratio determines the
	// weighting of priority vs. waiting time.  A priority ratio of zero would
	// give higher-priority processes no advantage over lower-priority, and
	// waiting time would determine execution order.
	//
	// A tie beteen the highest-weighted tasks is broken based on queue order.
	// The queue is neither FIFO nor LIFO, but closer to LIFO.

	// Get the CPU time
	theTime = kernelCpuGetMs();

	for (count = 0; count < numQueued; count ++)
	{
		// Get a pointer to the process' main process
		miscProcess = processQueue[count];

		if (miscProcess->state == proc_waiting)
		{
			// This will change the state of a waiting process to "ready"
			// if the specified "waiting reason" has come to pass

			// If the process is waiting for a specified time.  Has the
			// requested time come?
			if (miscProcess->waitUntil && (miscProcess->waitUntil < theTime))
			{
				// The process is ready to run
				miscProcess->state = proc_ready;
			}
			else
			{
				// The process must continue waiting
				continue;
			}
		}

		if (miscProcess->state == proc_finished)
		{
			// This will dismantle any process that has identified itself as
			// finished
			kernelMultitaskerKillProcess(miscProcess->processId, 0);

			// This removed it from the queue and placed another process in
			// its place.  Decrement the current loop counter
			count--;

			continue;
		}

		if ((miscProcess->state != proc_ready) &&
			(miscProcess->state != proc_ioready))
		{
			// This process is not ready (might be stopped, sleeping, or
			// zombie)
			continue;
		}

		// This process is ready to run.  Determine its weight.

		if (!miscProcess->priority)
		{
			// If the process is of the highest (real-time) priority, it
			// should get an infinite weight
			processWeight = 0xFFFFFFFF;
		}
		else if (miscProcess->priority == (PRIORITY_LEVELS - 1))
		{
			// Else if the process is of the lowest priority, it should get a
			// weight of zero
			processWeight = 0;
		}
		else if (miscProcess->state == proc_ioready)
		{
			// If this process was waiting for I/O which has now arrived, give
			// it a high (1) temporary priority level
			processWeight = (((PRIORITY_LEVELS - 1) * PRIORITY_RATIO) +
				miscProcess->waitTime);
		}
		else if (schedulerSwitchedByCall && (miscProcess->lastSlice ==
			schedulerTimeslices))
		{
			// If this process has yielded this timeslice already, we should
			// give it no weight this time so that a bunch of yielding
			// processes don't gobble up all the CPU time.
			processWeight = 0;
		}
		else
		{
			// Otherwise, calculate the weight of this task, using the
			// algorithm described above
			processWeight = (((PRIORITY_LEVELS - miscProcess->priority) *
				PRIORITY_RATIO) + miscProcess->waitTime);
		}

		// Did this process win?

		if (processWeight < topProcessWeight)
		{
			// No.  Increase the waiting time of this process, since it's not
			// the one we're selecting
			miscProcess->waitTime += 1;
		}
		else
		{
			if (nextProcess)
			{
				if ((processWeight == topProcessWeight) &&
					(nextProcess->waitTime >= miscProcess->waitTime))
				{
					// If the process' weight is tied with that of the
					// previously winning process, it will NOT win if the
					// other process has been waiting as long or longer
					miscProcess->waitTime += 1;
					continue;
				}
				else
				{
					// We have a new winning process.  Increase the waiting
					// time of the previous winner this one is replacing
					nextProcess->waitTime += 1;
				}
			}

			topProcessWeight = processWeight;
			nextProcess = miscProcess;
		}
	}

	return (nextProcess);
}


static int scheduler(void)
{
	// This is the kernel multitasker's scheduler thread.  This little program
	// will run continually in a loop, handing out time slices to all
	// processes, including the kernel itself.

	// By the time this scheduler is invoked, the kernel should already have
	// created itself a process in the task queue.  Thus, the scheduler can
	// begin by simply handing all time slices to the kernel.

	// Additional processes will be created with calls to the kernel, which
	// will create them and place them in the queue.  Thus, when the scheduler
	// regains control after a time slice has expired, the queue of processes
	// that it examines will have the new process added.

	int status = 0;
	unsigned timeUsed = 0;
	unsigned systemTime = 0;
	unsigned schedulerTime = 0;
	unsigned sliceCount = 0;
	unsigned oldSliceCount = 0;
	int count;

	// This is info about the processes we run
	kernelProcess *nextProcess = NULL;
	kernelProcess *previousProcess = NULL;

	// Here is the scheduler's big loop

	while (!schedulerStop)
	{
		// Make sure.  No interrupts allowed inside this task.
		processorDisableInts();

		// The scheduler is the current process.
		kernelCurrentProcess = schedulerProc;

		// Calculate how many timer ticks were used in the previous time slice.
		// This will be different depending on whether the previous timeslice
		// actually expired, or whether we were called for some other reason
		// (for example a yield()).

		if (!schedulerSwitchedByCall)
			timeUsed = TIME_SLICE_LENGTH;
		else
			timeUsed = (TIME_SLICE_LENGTH - kernelSysTimerReadValue(0));

		// Count the time used for legacy system timer purposes
		systemTime += timeUsed;

		// Have we had the equivalent of a full timer revolution?  If so, we
		// need to call the standard timer interrupt handler
		if (systemTime >= SYSTIMER_FULLCOUNT)
		{
			// Reset to zero
			systemTime = 0;

			// Artifically register a system timer tick.
			kernelSysTimerTick();
		}

		// Count the time used for the purpose of tracking CPU usage
		schedulerTime += timeUsed;
		sliceCount = (schedulerTime / TIME_SLICE_LENGTH);
		if (sliceCount > oldSliceCount)
		{
			// Increment the count of time slices.  This can just keep going
			// up until it wraps, which is no problem.
			schedulerTimeslices += 1;

			oldSliceCount = sliceCount;
		}

		// Remember the previous process we ran
		previousProcess = nextProcess;

		if (previousProcess)
		{
			if (previousProcess->state == proc_running)
			{
				// Change the state of the previous process to ready, since it
				// was interrupted while still on the CPU.
				previousProcess->state = proc_ready;
			}

			// Add the last timeslice to the process' CPU time
			previousProcess->cpuTime += timeUsed;

			// Record the current timeslice number, so we can remember when
			// this process was last active (see chooseNextProcess())
			previousProcess->lastSlice = schedulerTimeslices;
		}

		// Every CPU_PERCENT_TIMESLICES timeslices we will update the %CPU
		// value for each process currently in the queue
		if (sliceCount >= CPU_PERCENT_TIMESLICES)
		{
			for (count = 0; count < numQueued; count ++)
			{
				// Calculate the CPU percentage.
				if (!schedulerTime)
				{
					processQueue[count]->cpuPercent = 0;
				}
				else
				{
					processQueue[count]->cpuPercent =
						((processQueue[count]->cpuTime * 100) / schedulerTime);
				}

				// Reset the process' cpuTime counter
				processQueue[count]->cpuTime = 0;
			}

			// Reset the schedulerTime and slice counters
			schedulerTime = sliceCount = oldSliceCount = 0;
		}

		if (processingException)
		{
			// If we were processing an exception (either the exception process
			// or another exception handler), keep it active
			nextProcess = previousProcess;
			kernelDebugError("Scheduler interrupt while processing "
				"exception");
		}
		else
		{
			// Choose the next process to run
			nextProcess = chooseNextProcess();
		}

		// We should now have selected a process to run.  If not, we should
		// re-start the old one.  This should only be likely to happen if some
		// goombah kills the idle thread.
		if (!nextProcess)
			nextProcess = kernelCurrentProcess;

		// Update some info about the next process
		nextProcess->waitTime = 0;
		nextProcess->state = proc_running;

		// Export (to the rest of the multitasker) the pointer to the
		// currently selected process.
		kernelCurrentProcess = nextProcess;

		if (!schedulerSwitchedByCall)
			// Acknowledge the timer interrupt if one occurred
			kernelPicEndOfInterrupt(INTERRUPT_NUM_SYSTIMER);
		else
			// Reset the "switched by call" flag
			schedulerSwitchedByCall = 0;

		// Set up a new time slice - PIT single countdown.
		while (kernelSysTimerSetupTimer(0 /* timer */, 0 /* mode */,
			TIME_SLICE_LENGTH) < 0)
		{
			kernelError(kernel_warn, "The scheduler was unable to control "
				"the system timer");
		}

		// In the final part, we do the actual context switch.

		// Mark the exception handler and scheduler tasks as not busy so they
		// can be jumped back to.
		if (exceptionProc)
			markTaskBusy(exceptionProc->tssSelector, 0);
		markTaskBusy(schedulerProc->tssSelector, 0);

		// Mark the next task as not busy and jump to it.
		markTaskBusy(nextProcess->tssSelector, 0);
		processorFarJump(nextProcess->tssSelector);

		// Continue to loop
	}

	// If we get here, then the scheduler is supposed to shut down
	schedulerShutdown();

	// We should never get here
	return (status = 0);
}


static int schedulerInitialize(void)
{
	// This function will do all of the necessary initialization for the
	// scheduler.  Returns 0 on success, negative otherwise

	// The scheduler needs to make a task (but not a fully-fledged process)
	// for itself.

	int status = 0;
	int interrupts = 0;
	processImage schedImage = {
		scheduler, scheduler,
		NULL, 0xFFFFFFFF,
		NULL, 0xFFFFFFFF,
		0xFFFFFFFF,
		"", 0, { NULL }
	};

	status = createNewProcess("scheduler process", kernelProc->priority,
		kernelProc->privilege, &schedImage, 0 /* no page directory */);
	if (status < 0)
		return (status);

	schedulerProc = getProcessById(status);

	// The scheduler process doesn't sit in the normal process queue
	removeProcessFromQueue(schedulerProc);

	// Interrupts should always be disabled for this task
	schedulerProc->taskStateSegment.EFLAGS = 0x00000002;

	// Not busy
	markTaskBusy(schedulerProc->tssSelector, 0);

	kernelDebug(debug_multitasker, "Multitasker initialize scheduler");

	// Disable interrupts, so we can insure that we don't immediately get a
	// timer interrupt.
	processorSuspendInts(interrupts);

	// Hook the system timer interrupt.
	kernelDebug(debug_multitasker, "Multitasker hook system timer interrupt");

	oldSysTimerHandler = kernelInterruptGetHandler(INTERRUPT_NUM_SYSTIMER);
	if (!oldSysTimerHandler)
	{
		processorRestoreInts(interrupts);
		return (status = ERR_NOTINITIALIZED);
	}

	// Install a task gate for the interrupt, which will be the scheduler's
	// timer interrupt.  After this point, our new scheduler task will run
	// with every clock tick
	status = kernelInterruptHook(INTERRUPT_NUM_SYSTIMER, NULL,
		schedulerProc->tssSelector);
	if (status < 0)
	{
		processorRestoreInts(interrupts);
		return (status);
	}

	// The scheduler task should now be set up to run.  We should set up the
	// kernel task to resume operation

	// Before we load the kernel's selector into the task reg, mark it as not
	// busy, since one cannot load the task register with a busy TSS selector
	markTaskBusy(kernelProc->tssSelector, 0);

	// Make the kernel's Task State Segment be the current one.  In reality,
	// it IS still the currently running code
	kernelDebug(debug_multitasker, "Multitasker load task reg");
	processorLoadTaskReg(kernelProc->tssSelector);

	// Make note that the multitasker has been enabled.
	multitaskingEnabled = 1;

	// Set up the initial timer countdown
	kernelSysTimerSetupTimer(0 /* timer */, 0 /* mode */, TIME_SLICE_LENGTH);

	processorRestoreInts(interrupts);

	// Yield control to the scheduler
	kernelMultitaskerYield();

	return (status = 0);
}


static int createKernelProcess(void *kernelStack, unsigned kernelStackSize)
{
	// This function will create the kernel process at initialization time.
	// Returns 0 on success, negative otherwise.

	int status = 0;
	int kernelProcId = 0;
	processImage kernImage = {
		(void *) KERNEL_VIRTUAL_ADDRESS,
		kernelMain,
		NULL, 0xFFFFFFFF,
		NULL, 0xFFFFFFFF,
		0xFFFFFFFF,
		"", 0, { NULL }
	};

	// The kernel process is its own parent, of course, and it is owned by
	// "admin".  We create no page table, and there are no arguments.
	kernelProcId = createNewProcess("kernel process", 1, PRIVILEGE_SUPERVISOR,
		&kernImage, 0 /* no page directory */);
	if (kernelProcId < 0)
		// Damn.  Not able to create the kernel process
		return (kernelProcId);

	// Get the pointer to the kernel's process
	kernelProc = getProcessById(kernelProcId);

	// Make sure it's not NULL
	if (!kernelProc)
		// Can't access the kernel process
		return (status = ERR_NOSUCHPROCESS);

	// Interrupts are initially disabled for the kernel
	kernelProc->taskStateSegment.EFLAGS = 0x00000002;

	// Set the current process to initially be the kernel process
	kernelCurrentProcess = kernelProc;

	// Deallocate the stack that was allocated, since the kernel already has
	// one set up by the OS loader.
	kernelMemoryRelease(kernelProc->userStack);
	kernelProc->userStack = kernelStack;
	kernelProc->userStackSize = kernelStackSize;

	// Make the kernel's text streams be the console streams
	kernelProc->textInputStream = kernelTextGetConsoleInput();
	kernelProc->textInputStream->ownerPid = KERNELPROCID;
	kernelProc->textOutputStream = kernelTextGetConsoleOutput();

	// Make the kernel process runnable
	kernelProc->state = proc_ready;

	// Return success
	return (status = 0);
}


static void incrementDescendents(kernelProcess *theProcess)
{
	// This will walk up a chain of dependent child threads, incrementing the
	// descendent count of each parent

	kernelProcess *parentProcess = NULL;

	if (theProcess->processId == KERNELPROCID)
		// The kernel is its own parent
		return;

	parentProcess = getProcessById(theProcess->parentProcessId);
	if (!parentProcess)
		// No worries.  Probably not a problem
		return;

	parentProcess->descendentThreads++;

	if (parentProcess->type == proc_thread)
		// Do a recursion to walk up the chain
		incrementDescendents(parentProcess);
}


static void decrementDescendents(kernelProcess *theProcess)
{
	// This will walk up a chain of dependent child threads, decrementing the
	// descendent count of each parent

	kernelProcess *parentProcess = NULL;

	if (theProcess->processId == KERNELPROCID)
		// The kernel is its own parent
		return;

	parentProcess = getProcessById(theProcess->parentProcessId);
	if (!parentProcess)
		// No worries.  Probably not a problem
		return;

	parentProcess->descendentThreads--;

	if (parentProcess->type == proc_thread)
		// Do a recursion to walk up the chain
		decrementDescendents(parentProcess);
}


static void kernelProcess2Process(kernelProcess *kernProcess,
	process *userProcess)
{
	// Given a kernel-space process structure, create the corresponding
	// user-space version.

	strncpy(userProcess->name, (char *) kernProcess->name,
		MAX_PROCNAME_LENGTH);
	userProcess->userId = kernProcess->userId;
	userProcess->processId = kernProcess->processId;
	userProcess->type = kernProcess->type;
	userProcess->priority = kernProcess->priority;
	userProcess->privilege = kernProcess->privilege;
	userProcess->parentProcessId = kernProcess->parentProcessId;
	userProcess->descendentThreads = kernProcess->descendentThreads;
	userProcess->cpuPercent = kernProcess->cpuPercent;
	userProcess->state = kernProcess->state;
}


static int fpuExceptionHandler(void)
{
	// This function gets called when a EXCEPTION_DEVNOTAVAIL (7) exception
	// occurs.  It can happen under two circumstances:
	// CR0[EM] is set: No FPU is present.  We can implement emulation here
	//		later in this case, if we want.
	// CR0[TS] and CR0[MP] are set: A task switch has occurred since the last
	//		FP operation, and we need to restore the state.

	int status = 0;
	unsigned short fpuReg = 0;

	//kernelDebug(debug_multitasker, "Multitasker FPU exception start");

	processorClearTaskSwitched();

	if (fpuProcess && (fpuProcess == kernelCurrentProcess))
	{
		// This was the last process to use the FPU.  The state should be the
		// same as it was, so there's nothing to do.
		//kernelDebug(debug_multitasker, "Multitasker FPU exception end: "
		//	"nothing to do");
		return (status = 0);
	}

	processorGetFpuStatus(fpuReg);
	while (fpuReg & 0x8000)
	{
		kernelDebugError("FPU is busy");
		processorGetFpuStatus(fpuReg);
	}

	// Save the FPU state for the previous process
	if (fpuProcess)
	{
		// Save FPU state
		//kernelDebug(debug_multitasker, "Multitasker switch FPU ownership "
		//	"from %s to %s", fpuProcess->name,
		//	kernelCurrentProcess->name);
		//kernelDebug(debug_multitasker, "Multitasker save FPU state for %s",
		//	fpuProcess->name);
		processorFpuStateSave(fpuProcess->fpuState[0]);
		fpuProcess->fpuStateSaved = 1;
	}

	if (kernelCurrentProcess->fpuStateSaved)
	{
		// Restore the FPU state
		//kernelDebug(debug_multitasker, "Multitasker restore FPU state for "
		//	"%s", kernelCurrentProcess->name);
		processorFpuStateRestore(kernelCurrentProcess->fpuState[0]);
	}
	else
	{
		// No saved state for the FPU.  Initialize it.
		//kernelDebug(debug_multitasker, "Multitasker initialize FPU for %s",
		//	kernelCurrentProcess->name);
		processorFpuInit();
		processorGetFpuControl(fpuReg);
		// Mask FPU exceptions.
		fpuReg |= 0x3F;
		processorSetFpuControl(fpuReg);
	}

	kernelCurrentProcess->fpuStateSaved = 0;

	processorFpuClearEx();

	fpuProcess = kernelCurrentProcess;

	//kernelDebug(debug_multitasker, "Multitasker FPU exception end");
	return (status = 0);
}


static int propagateEnvironmentRecursive(kernelProcess *parentProcess,
	variableList *srcEnv, const char *variable)
{
	// Recursive propagation of the value of environment variables to child
	// processes.  If 'variable' is set, only the named variable will
	// propagate.  Otherwise, all parent variables will propagate.  Variables
	// in the childrens' environments that don't exist in the parent process
	// are unaffected.

	int status = 0;
	kernelProcess *childProcess = NULL;
	void *childEnvMemory = NULL;
	const char *currentVariable = NULL;
	const char *value = NULL;
	int count1, count2;

	for (count1 = 0; count1 < numQueued; count1 ++)
	{
		if ((processQueue[count1]->type != proc_thread) &&
			(processQueue[count1]->parentProcessId ==
				parentProcess->processId))
		{
			childProcess = processQueue[count1];

			kernelDebug(debug_multitasker, "Multitasker propagate "
				"environment from %s to %s", parentProcess->name,
				childProcess->name);

			// Set variables.
			for (count2 = 0; count2 < srcEnv->numVariables;	count2 ++)
			{
				currentVariable = kernelVariableListGetVariable(srcEnv,
					count2);

				if (!variable || !strcmp(variable, currentVariable))
				{
					// We have to do this for every iteration, since the
					// variable list code might have to expand the memory
					// allocation.

					// Remember the child process' environment memory
					childEnvMemory = childProcess->environment->memory;

					// Share the child process' environment memory with the
					// current process.
					status = kernelMemoryShare(childProcess->processId,
						kernelCurrentProcess->processId,
						childProcess->environment->memory,
						(void **) &childProcess->environment->memory);
					if (status < 0)
						// Couldn't share the memory.
						return (status);

					value = kernelVariableListGet(srcEnv, currentVariable);

					kernelVariableListSet(childProcess->environment,
						currentVariable, value);

					// Unmap the child process' environment memory from the
					// current process' address space
					kernelPageUnmap(kernelCurrentProcess->processId,
						childProcess->environment->memory,
						childProcess->environment->memorySize);

					// Restore the child's memory pointer
					childProcess->environment->memory = childEnvMemory;
				}
			}

			// Recurse to propagate to the child process' children
			status = propagateEnvironmentRecursive(childProcess, srcEnv,
				variable);
			if (status < 0)
				return (status);
		}
	}

	return (status = 0);
}


/////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////
//
//  Below here, the functions are exported for external use
//
/////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////

int kernelMultitaskerInitialize(void *kernelStack, unsigned kernelStackSize)
{
	// This function intializes the kernel's multitasker.

	int status = 0;
	unsigned cr0 = 0;

	// Make sure multitasking is NOT enabled already
	if (multitaskingEnabled)
		return (status = ERR_ALREADY);

	// Now we must initialize the process queue
	memset(processQueue, 0, (MAX_PROCESSES * sizeof(kernelProcess *)));
	numQueued = 0;

	// Initialize the CPU for floating point operation.  We set
	// CR0[EM]=0 (no emulation)
	// CR0[MP]=1 (math present)
	// CR0[NE]=1 (floating point errors cause exceptions)
	processorGetCR0(cr0);
	cr0 = ((cr0 & ~0x04U) | 0x22);
	processorSetCR0(cr0);

	// We need to create the kernel's own process.
	status = createKernelProcess(kernelStack, kernelStackSize);
	if (status < 0)
		return (status);

	// Now start the scheduler
	status = schedulerInitialize();
	if (status < 0)
		// The scheduler couldn't start
		return (status);

	// Create an "idle" thread to consume all unused cycles
	status = spawnIdleThread();
	if (status < 0)
		return (status);

	// Set up any specific exception handlers.
	exceptionVector[EXCEPTION_DEVNOTAVAIL].handler = fpuExceptionHandler;

	// Start the exception handler thread.
	status = spawnExceptionThread();
	if (status < 0)
		return (status);

	// Log a boot message
	kernelLog("Multitasking started");

	// Return success
	return (status = 0);
}


int kernelMultitaskerShutdown(int nice)
{
	// This function will shut down the multitasker and halt the scheduler,
	// returning exclusive control to the kernel process.  If the nice
	// argument is non-zero, this function will do a nice orderly shutdown,
	// killing all the running processes gracefully.  If it is zero, the
	// resources allocated to the processes will never be freed, and the
	// multitasker will just stop.  Returns 0 on success, negative otherwise.

	int status = 0;

	// Make sure multitasking has been enabled
	if (!multitaskingEnabled)
		// We can't shut down if we're not multitasking yet
		return (status = ERR_NOTINITIALIZED);

	// If we are doing a "nice" shutdown, we will kill all the running
	// processes (except the kernel and scheduler) gracefully.
	if (nice)
		kernelMultitaskerKillAll();

	// Set the schedulerStop flag to stop the scheduler
	schedulerStop = 1;

	// Yield control back to the scheduler, so that it can stop
	kernelMultitaskerYield();

	// Make note that the multitasker has been disabled
	multitaskingEnabled = 0;

	// Deallocate the stack used by the scheduler
	kernelMemoryRelease(schedulerProc->userStack);

	// Print a message
	kernelLog("Multitasking stopped");

	return (status = 0);
}


void kernelException(int num, unsigned address)
{
	// If we are already processing one, then it's a double-fault and we are
	// totally finished
	if (processingException)
	{
		kernelPanic("Double-fault (%s) while processing %s %s exception",
			exceptionVector[num].name, exceptionVector[processingException].a,
			exceptionVector[processingException].name);
	}

	processingException = num;
	exceptionAddress = address;

	// If there's a handler for this exception type, call it
	if (exceptionVector[processingException].handler &&
		(exceptionVector[processingException].handler() >= 0))
	{
		// The exception was handled.  Return to the caller.
		processingException = 0;
		exceptionAddress = 0;
		return;
	}

	// If multitasking is enabled, switch to the exception thread.  Otherwise
	// just call the exception handler as a function.
	if (multitaskingEnabled)
		processorFarJump(exceptionProc->tssSelector);
	else
		exceptionHandler();

	// If the exception is handled, then we return.
}


void kernelMultitaskerDumpProcessList(void)
{
	// This function is used to dump an internal listing of the current
	// process to the output.

	kernelTextOutputStream *currentOutput = NULL;
	kernelProcess *tmpProcess = NULL;
	char buffer[1024];
	int count;

	// Make sure multitasking has been enabled
	if (!multitaskingEnabled)
		return;

	// Get the current output stream
	currentOutput = kernelTextGetCurrentOutput();

	if (numQueued > 0)
	{
		kernelTextStreamPrintLine(currentOutput, "Process list:");

		for (count = 0; count < numQueued; count ++)
		{
			tmpProcess = processQueue[count];

			sprintf(buffer, "\"%s\"  PID=%d UID=%d priority=%d priv=%d "
				"parent=%d\n        %d%% CPU State=",
				(char *) tmpProcess->name, tmpProcess->processId,
				tmpProcess->userId, tmpProcess->priority,
				tmpProcess->privilege, tmpProcess->parentProcessId,
				tmpProcess->cpuPercent);

			// Get the state
			switch(tmpProcess->state)
			{
				case proc_running:
					strcat(buffer, "running");
					break;

				case proc_ready:
				case proc_ioready:
					strcat(buffer, "ready");
					break;

				case proc_waiting:
					strcat(buffer, "waiting");
					break;

				case proc_sleeping:
					strcat(buffer, "sleeping");
					break;

				case proc_stopped:
					strcat(buffer, "stopped");
					break;

				case proc_finished:
					strcat(buffer, "finished");
					break;

				case proc_zombie:
					strcat(buffer, "zombie");
					break;

				default:
					strcat(buffer, "unknown");
					break;
			}

			kernelTextStreamPrintLine(currentOutput, buffer);
		}
	}
	else
	{
		// This doesn't seem at all likely.
		kernelTextStreamPrintLine(currentOutput, "No processes remaining");
	}

	kernelTextStreamNewline(currentOutput);
}


int kernelMultitaskerCreateProcess(const char *name, int privilege,
	processImage *execImage)
{
	// This function is called to set up an (initially) single-threaded
	// process in the multitasker.  This is the function used by external
	// sources -- the loader for example -- to define new processes.  This
	// new process thread we're creating will have its state set to "stopped"
	// after this call.  The caller should use the
	// kernelMultitaskerSetProcessState() function to start the new process.
	// This function returns the processId of the new process on success,
	// negative otherwise.

	int status = 0;
	int processId = 0;
	kernelProcess *newProcess = NULL;

	// Make sure multitasking has been enabled
	if (!multitaskingEnabled)
		return (status = ERR_NOTINITIALIZED);

	// Make sure the parameters are valid
	if (!name || !execImage)
		return (status = ERR_NULLPARAMETER);

	// Make sure that an unprivileged process is not trying to create a
	// privileged one
	if ((kernelCurrentProcess->privilege == PRIVILEGE_USER) &&
		(privilege == PRIVILEGE_SUPERVISOR))
	{
		kernelError(kernel_error, "An unprivileged process cannot create a "
			"privileged process");
		return (status == ERR_PERMISSION);
	}

	// Create the new process
	processId = createNewProcess(name, PRIORITY_DEFAULT, privilege, execImage,
		1 /* create page directory */);

	// Get the pointer to the new process from its process Id
	newProcess = getProcessById(processId);
	if (!newProcess)
		// We couldn't get access to the new process
		return (status = ERR_NOCREATE);

	// Create the process' environment
	status = kernelEnvironmentCreate(newProcess->processId,
		newProcess->environment, kernelCurrentProcess->environment);
	if (status < 0)
		// Couldn't create an environment structure for this process
		return (status);

	// Don't assign input or output streams to this process.  There are
	// multiple possibilities here, and the caller will have to either block
	// (which takes care of such things) or sort it out for themselves.

	// Return whatever was returned by the previous call
	return (processId);
}


int kernelMultitaskerSpawn(void *startAddress, const char *name, int argc,
	void *argv[])
{
	// This function is used to spawn a new thread from the current process.
	// The function needs to be told the starting address of the code to
	// execute, and an optional argument to pass to the spawned function.  It
	// returns the new process Id on success, negative otherwise.

	int status = 0;
	int processId = 0;
	kernelProcess *newProcess = NULL;
	processImage execImage;
	int count;

	// Make sure multitasking has been enabled
	if (!multitaskingEnabled)
		return (status = ERR_NOTINITIALIZED);

	// Check params
	if (!startAddress || !name)
		return (status = ERR_NULLPARAMETER);

	// If the number of arguments is not zero, make sure the arguments
	// pointer is not NULL
	if (argc && !argv)
		return (status = ERR_NULLPARAMETER);

	// Make sure the current process isn't NULL
	if (!kernelCurrentProcess)
		return (status = ERR_NOSUCHPROCESS);

	memset(&execImage, 0, sizeof(processImage));
	execImage.virtualAddress = startAddress;
	execImage.entryPoint = startAddress;

	// Set up arguments
	execImage.argc = (argc + 1);
	execImage.argv[0] = (char *) name;
	for (count = 0; count < argc; count ++)
		execImage.argv[count + 1] = argv[count];

	// OK, now we should create the new process
	processId = createNewProcess(name, kernelCurrentProcess->priority,
		kernelCurrentProcess->privilege, &execImage,
		0 /* no page directory */);
	if (processId < 0)
		return (status = processId);

	// Get the pointer to the new process from its process Id
	newProcess = getProcessById(processId);

	// Make sure it's valid
	if (!newProcess)
		// We couldn't get access to the new process
		return (status = ERR_NOCREATE);

	// Change the type to thread
	newProcess->type = proc_thread;

	// Increment the descendent counts
	incrementDescendents(newProcess);

	// Since we assume that the thread is invoked as a function call, subtract
	// additional bytes from the stack pointer to account for the space where
	// the return address would normally go.
	newProcess->taskStateSegment.ESP -= sizeof(void *);

	// Share the environment of the parent
	if (newProcess->environment)
		kernelFree(newProcess->environment);
	newProcess->environment = kernelCurrentProcess->environment;

	// Share the symbols
	newProcess->symbols = kernelCurrentProcess->symbols;

	// The new process should share (but not own) the same text streams as the
	// parent

	newProcess->textInputStream = kernelCurrentProcess->textInputStream;

	if (newProcess->textInputStream)
	{
		memcpy((void *) &newProcess->oldInputAttrs,
			(void *) &newProcess->textInputStream->attrs,
			sizeof(kernelTextInputStreamAttrs));
	}

	newProcess->textOutputStream = kernelCurrentProcess->textOutputStream;

	// Make the new thread runnable
	newProcess->state = proc_ready;

	// Return the new process' Id.
	return (newProcess->processId);
}


int kernelMultitaskerSpawnKernelThread(void *startAddress, const char *name,
	int argc, void *argv[])
{
	// This function is a wrapper around the regular spawn() call, which
	// causes threads to be spawned as children of the kernel, instead of
	// children of the calling process.  This is important for threads that
	// are spawned from code which belongs to the kernel.

	int status = 0;
	int interrupts = 0;
	kernelProcess *myProcess;

	// Make sure multitasking has been enabled
	if (!multitaskingEnabled)
		return (status = ERR_NOTINITIALIZED);

	// What is the current process?
	myProcess = kernelCurrentProcess;

	// Disable interrupts while we're monkeying
	processorSuspendInts(interrupts);

	// Change the current process to the kernel process
	kernelCurrentProcess = kernelProc;

	// Spawn
	status = kernelMultitaskerSpawn(startAddress, name, argc, argv);

	// Reset the current process
	kernelCurrentProcess = myProcess;

	// Reenable interrupts
	processorRestoreInts(interrupts);

	// Done
	return (status);
}


int kernelMultitaskerGetProcess(int processId, process *userProcess)
{
	// Return the requested process.

	int status = 0;
	kernelProcess *kernProcess = NULL;

	// Make sure multitasking has been enabled
	if (!multitaskingEnabled)
		return (status = ERR_NOTINITIALIZED);

	// Check params
	if (!userProcess)
		return (status = ERR_NULLPARAMETER);

	// Try to match the requested process Id number with a real live process
	// structure
	kernProcess = getProcessById(processId);
	if (!kernProcess)
		// That means there's no such process
		return (status = ERR_NOSUCHENTRY);

	// Make it into a user space process
	kernelProcess2Process(kernProcess, userProcess);
	return (status = 0);
}


int kernelMultitaskerGetProcessByName(const char *processName,
	process *userProcess)
{
	// Return the requested process.

	int status = 0;
	kernelProcess *kernProcess = NULL;

	// Make sure multitasking has been enabled
	if (!multitaskingEnabled)
		return (status = ERR_NOTINITIALIZED);

	// Check params
	if (!processName || !userProcess)
		return (status = ERR_NULLPARAMETER);

	// Try to match the requested process Id number with a real live process
	// structure
	kernProcess = getProcessByName(processName);
	if (!kernProcess)
		// That means there's no such process
		return (status = ERR_NOSUCHENTRY);

	// Make it into a user space process
	kernelProcess2Process(kernProcess, userProcess);
	return (status = 0);
}


int kernelMultitaskerGetProcesses(void *buffer, unsigned buffSize)
{
	// Return user-space process structures into the supplied buffer

	int status = 0;
	kernelProcess *kernProcess = NULL;
	process *userProcess = NULL;

	// Make sure multitasking has been enabled
	if (!multitaskingEnabled)
		return (status = ERR_NOTINITIALIZED);

	// Check params
	if (!buffer)
		return (status = ERR_NULLPARAMETER);

	for (status = 0; status < numQueued; status ++)
	{
		kernProcess = processQueue[status];
		userProcess = (buffer + (status * sizeof(process)));
		if ((void *) userProcess >= (buffer + buffSize))
			break;

		kernelProcess2Process(kernProcess, userProcess);
	}

	return (status);
}


int kernelMultitaskerGetCurrentProcessId(void)
{
	// This is a very simple function that can be called by external programs
	// to get the PID of the current running process.  Of course, internal
	// functions can perform this action very easily themselves.

	int status = 0;

	// Make sure multitasking has been enabled
	if (!multitaskingEnabled)
		// If we're not multitasking,return the kernel's process Id
		return (status = KERNELPROCID);

	// Double-check the current process to make sure it's not NULL
	if (!kernelCurrentProcess)
		return (status = ERR_NOSUCHPROCESS);

	// OK, we can return process Id of the currently running process
	return (status = kernelCurrentProcess->processId);
}


int kernelMultitaskerGetProcessState(int processId, processState *state)
{
	// This is a very simple function that can be called by external programs
	// to request the state of a "running" process.  Of course, internal
	// functions can perform this action very easily themselves.

	int status = 0;
	kernelProcess *theProcess = NULL;

	// Check params
	if (!state)
		return (status = ERR_NULLPARAMETER);

	// Make sure multitasking has been enabled
	if (!multitaskingEnabled)
	{
		if (processId == KERNELPROCID)
		{
			*state = proc_running;
			return (status = 0);
		}
		else
		{
			return (status = ERR_NOTINITIALIZED);
		}
	}

	// We need to find the process structure based on the process Id
	theProcess = getProcessById(processId);

	if (!theProcess)
		// The process does not exist
		return (status = ERR_NOSUCHPROCESS);

	// Set the state value of the process
	*state = theProcess->state;

	return (status = 0);
}


int kernelMultitaskerSetProcessState(int processId, processState newState)
{
	// This is a very simple function that can be called by external programs
	// to change the state of a "running" process.  Of course, internal
	// functions can perform this action very easily themselves.

	int status = 0;
	kernelProcess *changeProcess = NULL;

	// Make sure multitasking has been enabled
	if (!multitaskingEnabled)
		return (status = ERR_NOTINITIALIZED);

	// We need to find the process structure based on the process Id
	changeProcess = getProcessById(processId);

	if (!changeProcess)
		// The process does not exist
		return (status = ERR_NOSUCHPROCESS);

	// Permission check.  A privileged process can change the state of any
	// other process, but a non-privileged process can only change the state
	// of processes owned by the same user
	if (kernelCurrentProcess->privilege != PRIVILEGE_SUPERVISOR)
	{
		if (kernelCurrentProcess->userId != changeProcess->userId)
			return (status = ERR_PERMISSION);
	}

	// Make sure the new state is a legal one
	switch (newState)
	{
		case proc_running:
		case proc_ready:
		case proc_ioready:
		case proc_waiting:
		case proc_sleeping:
		case proc_stopped:
		case proc_finished:
		case proc_zombie:
			// Ok
			break;

		default:
			// Not a legal state value
			return (status = ERR_INVALID);
	}

	// Set the state value of the process
	changeProcess->state = newState;

	return (status);
}


int kernelMultitaskerProcessIsAlive(int processId)
{
	// Returns 1 if a process exists and has not finished (or been terminated)

	kernelProcess *targetProcess = NULL;

	// Make sure multitasking has been enabled
	if (!multitaskingEnabled)
	{
		if (processId == KERNELPROCID)
			return (1);
		else
			return (0);
	}

	// Try to match the requested process Id number with a real live process
	// structure
	targetProcess = getProcessById(processId);

	if (targetProcess && (targetProcess->state != proc_finished) &&
		(targetProcess->state != proc_zombie))
	{
		return (1);
	}
	else
	{
		return (0);
	}
}


int kernelMultitaskerGetProcessPriority(int processId)
{
	// This is a very simple function that can be called by external programs
	// to get the priority of a process.  Of course, internal functions can
	// perform this action very easily themselves.

	int status = 0;
	kernelProcess *getProcess = NULL;

	// Make sure multitasking has been enabled
	if (!multitaskingEnabled)
	{
		if (processId == KERNELPROCID)
			return (0);
		else
			return (status = ERR_NOTINITIALIZED);
	}

	// We need to find the process structure based on the process Id
	getProcess = getProcessById(processId);

	if (!getProcess)
		// The process does not exist
		return (status = ERR_NOSUCHPROCESS);

	// No permission check necessary here

	// Return the privilege value of the process
	return (getProcess->priority);
}


int kernelMultitaskerSetProcessPriority(int processId, int newPriority)
{
	// This is a very simple function that can be called by external programs
	// to change the priority of a process.  Of course, internal functions can
	// perform this action very easily themselves.

	int status = 0;
	kernelProcess *changeProcess = NULL;

	// Make sure multitasking has been enabled
	if (!multitaskingEnabled)
		return (status = ERR_NOTINITIALIZED);

	// We need to find the process structure based on the process Id
	changeProcess = getProcessById(processId);

	if (!changeProcess)
		// The process does not exist
		return (status = ERR_NOSUCHPROCESS);

	// Permission check.  A privileged process can set the priority of any
	// other process, but a non-privileged process can only change the
	// priority of processes owned by the same user.  Additionally, a
	// non-privileged process can only set the new priority to a value equal
	// to or lower than its own priority.
	if (kernelCurrentProcess->privilege != PRIVILEGE_SUPERVISOR)
	{
		if ((kernelCurrentProcess->userId != changeProcess->userId) ||
			(newPriority < kernelCurrentProcess->priority))
		{
			return (status = ERR_PERMISSION);
		}
	}

	// Make sure the new priority is a legal one
	if ((newPriority < 0) || (newPriority >= (PRIORITY_LEVELS)))
		// Not a legal priority value
		return (status = ERR_INVALID);

	// Set the priority value of the process
	changeProcess->priority = newPriority;

	return (status = 0);
}


int kernelMultitaskerGetProcessPrivilege(int processId)
{
	// This is a very simple function that can be called by external programs
	// to request the privilege of a "running" process.  Of course, internal
	// functions can perform this action very easily themselves.

	int status = 0;
	kernelProcess *theProcess = NULL;

	// Make sure multitasking has been enabled
	if (!multitaskingEnabled)
	{
		if (processId == KERNELPROCID)
			return (PRIVILEGE_SUPERVISOR);
		else
			return (status = ERR_NOTINITIALIZED);
	}

	// We need to find the process structure based on the process Id
	theProcess = getProcessById(processId);

	if (!theProcess)
		// The process does not exist
		return (status = ERR_NOSUCHPROCESS);

	// Return the nominal privilege value of the process
	return (theProcess->privilege);
}


int kernelMultitaskerGetProcessParent(int processId)
{
	// This is a very simple function that can be called by external programs
	// to get the parent of a process.  Of course, internal functions can
	// perform this action very easily themselves.

	int status = 0;
	kernelProcess *getProcess = NULL;

	// Make sure multitasking has been enabled
	if (!multitaskingEnabled)
	{
		if (processId == KERNELPROCID)
			return (KERNELPROCID);
		else
			return (status = ERR_NOTINITIALIZED);
	}

	// We need to find the process structure based on the process Id
	getProcess = getProcessById(processId);

	if (!getProcess)
		// The process does not exist
		return (status = ERR_NOSUCHPROCESS);

	// No permission check necessary here

	// Return the parent value of the process
	return (getProcess->parentProcessId);
}


int kernelMultitaskerSetProcessParent(int processId, int parentProcessId)
{
	// This is a very simple function that can be called by external programs
	// to change the parent of a process.  Of course, internal functions can
	// perform this action very easily themselves.

	int status = 0;
	kernelProcess *changeProcess = NULL;

	// Make sure multitasking has been enabled
	if (!multitaskingEnabled)
		return (status = ERR_NOTINITIALIZED);

	// We need to find the process structure based on the process Id
	changeProcess = getProcessById(processId);

	if (!changeProcess)
		// The process does not exist
		return (status = ERR_NOSUCHPROCESS);

	// Permission check.  A privileged process can set the parent of any other
	// process, but a non-privileged process can only change the priority of
	// processes owned by the same user.
	if ((kernelCurrentProcess->privilege != PRIVILEGE_SUPERVISOR) &&
		(kernelCurrentProcess->userId != changeProcess->userId))
	{
		return (status = ERR_PERMISSION);
	}

	// Make sure the new parent exists
	if (!kernelMultitaskerProcessIsAlive(parentProcessId))
		// Not a legal parent
		return (status = ERR_NOSUCHPROCESS);

	// Set the parent value of the process
	changeProcess->parentProcessId = parentProcessId;

	return (status = 0);
}


int kernelMultitaskerGetCurrentDirectory(char *buffer, int buffSize)
{
	// This function will fill the supplied buffer with the name of the
	// current working directory for the current process.  Returns 0 on
	// success, negative otherwise.

	int status = 0;
	int lengthToCopy = 0;

	// Make sure the buffer we've been passed is not NULL
	if (!buffer)
		return (status = ERR_NULLPARAMETER);

	// Now, the number of characters we will copy is the lesser of
	// buffSize or MAX_PATH_LENGTH
	lengthToCopy = min(buffSize, MAX_PATH_LENGTH);

	// Otay, copy the name of the current directory into the caller's buffer
	if (!multitaskingEnabled)
	{
		strncpy(buffer, "/", lengthToCopy);
	}
	else
	{
		strncpy(buffer, (char *) kernelCurrentProcess->currentDirectory,
			lengthToCopy);
	}

	// Return success
	return (status = 0);
}


int kernelMultitaskerSetCurrentDirectory(const char *newDirName)
{
	// This function will change the current directory of the current process.
	// Returns 0 on success, negative otherwise.

	int status = 0;
	kernelFileEntry *newDir = NULL;

	// Make sure multitasking has been enabled
	if (!multitaskingEnabled)
		return (status = ERR_NOTINITIALIZED);

	// Check params
	if (!newDirName)
		return (status = ERR_NULLPARAMETER);

	// Call the appropriate filesystem function to find this supposed new
	// directory.
	newDir = kernelFileLookup(newDirName);
	if (!newDir)
		return (status = ERR_NOSUCHDIR);

	// Make sure the target is actually a directory
	if (newDir->type != dirT)
		return (status = ERR_NOTADIR);

	// Okay, copy the full name of the directory into the process
	kernelFileGetFullName(newDir, (char *)
		kernelCurrentProcess->currentDirectory, MAX_PATH_LENGTH);

	// Return success
	return (status = 0);
}


kernelTextInputStream *kernelMultitaskerGetTextInput(void)
{
	// This function will return the text input stream that is attached to the
	// current process

	// If multitasking hasn't yet been enabled, we can safely assume that
	// we're currently using the default console text input.
	if (!multitaskingEnabled)
		return (kernelTextGetCurrentInput());
	else
		// Ok, return the pointer
		return (kernelCurrentProcess->textInputStream);
}


int kernelMultitaskerSetTextInput(int processId,
	 kernelTextInputStream *theStream)
{
	// Change the input stream of the process

	int status = 0;
	kernelProcess *theProcess = NULL;
	int count;

	// Make sure multitasking has been enabled
	if (!multitaskingEnabled)
		return (status = ERR_NOTINITIALIZED);

	// theStream is allowed to be NULL.

	theProcess = getProcessById(processId);
	if (!theProcess)
		return (status = ERR_NOSUCHPROCESS);

	theProcess->textInputStream = theStream;

	if (theStream)
	{
		if (theProcess->type == proc_normal)
			theStream->ownerPid = theProcess->processId;

		// Remember the current input attributes
		memcpy((void *) &theProcess->oldInputAttrs, (void *) &theStream->attrs,
			sizeof(kernelTextInputStreamAttrs));
	}

	// Do any child threads recursively as well.
	if (theProcess->descendentThreads)
	{
		for (count = 0; count < numQueued; count ++)
		{
			if ((processQueue[count]->parentProcessId == processId) &&
				(processQueue[count]->type == proc_thread))
			{
				status = kernelMultitaskerSetTextInput(processQueue[count]
					->processId, theStream);
				if (status < 0)
					return (status);
			}
		}
	}

	return (status = 0);
}


kernelTextOutputStream *kernelMultitaskerGetTextOutput(void)
{
	// This function will return the text output stream that is attached to
	// the current process

	// If multitasking hasn't yet been enabled, we can safely assume that
	// we're currently using the default console text output.
	if (!multitaskingEnabled)
		return (kernelTextGetCurrentOutput());
	else
		// Ok, return the pointer
		return (kernelCurrentProcess->textOutputStream);
}


int kernelMultitaskerSetTextOutput(int processId,
	kernelTextOutputStream *theStream)
{
	// Change the output stream of the process

	int status = 0;
	kernelProcess *theProcess = NULL;
	int count;

	// Make sure multitasking has been enabled
	if (!multitaskingEnabled)
		return (status = ERR_NOTINITIALIZED);

	// theStream is allowed to be NULL.

	theProcess = getProcessById(processId);
	if (!theProcess)
		return (status = ERR_NOSUCHPROCESS);

	theProcess->textOutputStream = theStream;

	// Do any child threads recursively as well.
	if (theProcess->descendentThreads)
	{
		for (count = 0; count < numQueued; count ++)
		{
			if ((processQueue[count]->parentProcessId == processId) &&
				(processQueue[count]->type == proc_thread))
			{
				status = kernelMultitaskerSetTextOutput(processQueue[count]
					->processId, theStream);
				if (status < 0)
					return (status);
			}
		}
	}

	return (status = 0);
}


int kernelMultitaskerDuplicateIo(int firstPid, int secondPid, int clear)
{
	// Copy the input and output streams of the first process to the second
	// process.

	int status = 0;
	kernelProcess *firstProcess = NULL;
	kernelProcess *secondProcess = NULL;
	kernelTextInputStream *input = NULL;
	kernelTextOutputStream *output = NULL;

	// Make sure multitasking has been enabled
	if (!multitaskingEnabled)
		return (status = ERR_NOTINITIALIZED);

	firstProcess = getProcessById(firstPid);
	secondProcess = getProcessById(secondPid);

	if (!firstProcess || !secondProcess)
		return (status = ERR_NOSUCHPROCESS);

	input = firstProcess->textInputStream;
	output = firstProcess->textOutputStream;

	if (input)
	{
		secondProcess->textInputStream = input;
		input->ownerPid = secondPid;

		// Remember the current input attributes
		memcpy((void *) &secondProcess->oldInputAttrs, (void *) &input->attrs,
			sizeof(kernelTextInputStreamAttrs));

		if (clear)
			kernelTextInputStreamRemoveAll(input);
	}

	if (output)
		secondProcess->textOutputStream = output;

	return (status = 0);
}


int kernelMultitaskerGetProcessorTime(clock_t *clk)
{
	// Returns processor time used by a process since its start.  This value
	// is the number of timer ticks from the system timer.

	int status = 0;

	// Make sure multitasking has been enabled
	if (!multitaskingEnabled)
		return (status = ERR_NOTINITIALIZED);

	if (!clk)
		return (status = ERR_NULLPARAMETER);

	// Return the processor time of the current process
	*clk = kernelCurrentProcess->cpuTime;

	return (status = 0);
}


void kernelMultitaskerYield(void)
{
	// This function will yield control from the current running thread back
	// to the scheduler.

	// Make sure multitasking has been enabled
	if (!multitaskingEnabled)
		// We can't yield if we're not multitasking yet
		return;

	// Don't do this inside an interrupt
	if (kernelProcessingInterrupt())
		return;

	// We accomplish a yield by doing a far call to the scheduler's task.  The
	// scheduler sees this almost as if the current timeslice had expired.
	schedulerSwitchedByCall = 1;
	processorFarJump(schedulerProc->tssSelector);
}


void kernelMultitaskerWait(unsigned milliseconds)
{
	// This function will put a process into the waiting state for *at least*
	// the specified number of milliseconds, and yield control back to the
	// scheduler

	// Make sure multitasking has been enabled
	if (!multitaskingEnabled)
	{
		// We can't wait properly if we're not multitasking yet, but we can
		// try to spin
		kernelDebugError("Cannot wait() before multitasking is enabled.  "
			"Spinning.");
		kernelCpuSpinMs(milliseconds);
		return;
	}

	// Don't do this inside an interrupt
	if (kernelProcessingInterrupt())
	{
		kernelPanic("Cannot wait() inside an interrupt handler (%d)",
			kernelInterruptGetCurrent());
	}

	// Make sure the current process isn't NULL
	if (!kernelCurrentProcess)
	{
		// Can't return an error code, but we can't perform the specified
		// action either
		return;
	}

	// Set the wait until time
	kernelCurrentProcess->waitUntil = (kernelCpuGetMs() + milliseconds);
	kernelCurrentProcess->waitForProcess = 0;

	// Set the current process to "waiting"
	kernelCurrentProcess->state = proc_waiting;

	// And yield
	kernelMultitaskerYield();
}


int kernelMultitaskerBlock(int processId)
{
	// This function will put a process into the waiting state until the
	// requested blocking process has completed

	int status = 0;
	kernelProcess *blockProcess = NULL;

	// Make sure multitasking has been enabled
	if (!multitaskingEnabled)
		// We can't yield if we're not multitasking yet
		return (status = ERR_NOTINITIALIZED);

	// Don't do this inside an interrupt
	if (kernelProcessingInterrupt())
	{
		kernelPanic("Cannot block() inside an interrupt handler (%d)",
			kernelInterruptGetCurrent());
	}

	// Get the process that we're supposed to block on
	blockProcess = getProcessById(processId);
	if (!blockProcess)
	{
		// The process does not exist.
		kernelError(kernel_error, "The process on which to block does not "
			"exist");
		return (status = ERR_NOSUCHPROCESS);
	}

	// Make sure the current process isn't NULL
	if (!kernelCurrentProcess)
	{
		kernelError(kernel_error, "Can't determine the current process");
		return (status = ERR_BUG);
	}

	// Take the text streams that belong to the current process and
	// give them to the target process
	kernelMultitaskerDuplicateIo(kernelCurrentProcess->processId, processId,
		0 /* don't clear */);

	// Set the wait for process values
	kernelCurrentProcess->waitForProcess = processId;
	kernelCurrentProcess->waitUntil = 0;

	// Set the current process to "waiting"
	kernelCurrentProcess->state = proc_waiting;

	// And yield
	kernelMultitaskerYield();

	// Get the exit code from the process
	return (kernelCurrentProcess->blockingExitCode);
}


int kernelMultitaskerDetach(void)
{
	// This will allow a program or daemon to detach from its parent process
	// if the parent process is blocking.

	int status = 0;
	kernelProcess *parentProcess = NULL;

	// Make sure multitasking has been enabled
	if (!multitaskingEnabled)
		// We can't detach if we're not multitasking yet
		return (status = ERR_NOTINITIALIZED);

	// Make sure the current process isn't NULL
	if (!kernelCurrentProcess)
	{
		kernelError(kernel_error, "Can't determine the current process");
		return (status = ERR_BUG);
	}

	// Set the input/output streams to the console
	kernelMultitaskerDuplicateIo(KERNELPROCID, kernelCurrentProcess->processId,
		0 /* don't clear */);

	// Get the process that's blocking on this one, if any
	parentProcess = getProcessById(kernelCurrentProcess->parentProcessId);

	if (parentProcess && (parentProcess->waitForProcess ==
		kernelCurrentProcess->processId))
	{
		// Clear the return code of the parent process
		parentProcess->blockingExitCode = 0;

		// Clear the parent's wait for process value
		parentProcess->waitForProcess = 0;

		// Make it runnable
		parentProcess->state = proc_ready;
	}

	return (status = 0);
}


int kernelMultitaskerKillProcess(int processId, int force)
{
	// This function should be used to properly kill a process.  This will
	// deallocate all of the internal resources used by the multitasker in
	// maintaining the process and all of its children.  This function will
	// commonly employ a recursive tactic for killing processes with spawned
	// children.  Returns 0 on success, negative on error.

	int status = 0;
	kernelProcess *killProcess = NULL;
	int count;

	// Make sure multitasking has been enabled
	if (!multitaskingEnabled)
		return (status = ERR_NOTINITIALIZED);

	// Find the process structure based on the Id we were passed
	killProcess = getProcessById(processId);
	if (!killProcess)
		// There's no such process
		return (status = ERR_NOSUCHPROCESS);

	// Processes are not allowed to actually kill themselves.  They must use
	// the terminate function to do it normally.
	if (killProcess == kernelCurrentProcess)
		kernelMultitaskerTerminate(0);

	// Permission check.  A privileged process can kill any other process, but
	// a non-privileged process can only kill processes owned by the same user
	if (kernelCurrentProcess->privilege != PRIVILEGE_SUPERVISOR)
	{
		if (kernelCurrentProcess->userId != killProcess->userId)
			return (status = ERR_PERMISSION);
	}

	// You can't kill the kernel on purpose
	if (killProcess == kernelProc)
	{
		kernelError(kernel_error, "It's not possible to kill the kernel "
			"process");
		return (status = ERR_INVALID);
	}

	// You can't kill the exception handler thread on purpose
	if (killProcess == exceptionProc)
	{
		kernelError(kernel_error, "It's not possible to kill the exception "
			"thread");
		return (status = ERR_INVALID);
	}

	// If a thread is trying to kill its parent, we won't do that here.
	// Instead we will mark it as 'finished' and let the kernel clean us all
	// up later
	if ((kernelCurrentProcess->type == proc_thread) &&
		(processId == kernelCurrentProcess->parentProcessId))
	{
		killProcess->state = proc_finished;
		while (1)
			kernelMultitaskerYield();
	}

	// The request is legitimate.

	// Mark the process as stopped in the process queue, so that the scheduler
	// will not inadvertently select it to run while we're destroying it.
	killProcess->state = proc_stopped;

	// We must loop through the list of existing processes, looking for any
	// other processes whose states depend on this one (such as child threads
	// who don't have a page directory).  If we remove a process, we need to
	// call this function recursively to kill it (and any of its own dependant
	// children) and then reset the counter, stepping through the list again
	// (since the list will be different after the recursion)

	for (count = 0; count < numQueued; count ++)
	{
		// Is this process blocking on the process we're killing?
		if (processQueue[count]->waitForProcess == processId)
		{
			// This process is blocking on the process we're killing.  If the
			// process being killed was not, in turn, blocking on another
			// process, the blocked process will be made runnable.  Otherwise,
			// the blocked process will be forced to block on the same
			// process as the one being killed.
			if (killProcess->waitForProcess)
			{
				processQueue[count]->waitForProcess =
					killProcess->waitForProcess;
			}
			else
			{
				processQueue[count]->blockingExitCode = ERR_KILLED;
				processQueue[count]->waitForProcess = 0;
				processQueue[count]->state = proc_ready;
			}

			continue;
		}

		// If this process is a child thread of the process we're killing, or
		// if the process we're killing was blocking on this process, kill it
		// first.
		if ((processQueue[count]->state != proc_finished) &&
			(processQueue[count]->parentProcessId == killProcess->processId) &&
			((processQueue[count]->type == proc_thread) ||
				(killProcess->waitForProcess ==
					processQueue[count]->processId)))
		{
			status = kernelMultitaskerKillProcess(processQueue[count]
				->processId, force);
			if (status < 0)
			{
				kernelError(kernel_warn, "Unable to kill child process \"%s\" "
					"of parent process \"%s\"", processQueue[count]->name,
					killProcess->name);
			}

			// Restart the loop
			count = 0;
			continue;
		}
	}

	// Now we look after killing the process with the Id we were passed

	if (kernelNetworkEnabled())
	{
		// Try to close all network connections owned by this process
		status = kernelNetworkCloseAll(killProcess->processId);
		if (status < 0)
			kernelError(kernel_warn, "Can't release network connections");
	}

	// Restore previous attrubutes to the input stream, if applicable
	if (killProcess->textInputStream)
	{
		memcpy((void *) &killProcess->textInputStream->attrs,
			(void *) &killProcess->oldInputAttrs,
			sizeof(kernelTextInputStreamAttrs));
	}

	// If this process is a thread, decrement the count of descendent threads
	// of its parent
	if (killProcess->type == proc_thread)
		decrementDescendents(killProcess);

	// Dismantle the process
	status = deleteProcess(killProcess);
	if (status < 0)
	{
		// Eek, there was a problem deallocating something, we guess.  Simply
		// mark the process as a zombie so that it won't be run any more, but
		// its resources won't be 'lost'
		kernelError(kernel_error, "Couldn't delete process %d: \"%s\"",
			killProcess->processId, killProcess->name);
		killProcess->state = proc_zombie;
		return (status);
	}

	// If the target process is the idle process, spawn another one
	if (killProcess == idleProc)
		spawnIdleThread();

	// Done.  Return success
	return (status = 0);
}


int kernelMultitaskerKillByName(const char *name, int force)
{
	// Try to kill all processes whose names match the one supplied

	int status = 0;
	kernelProcess *killProcess = NULL;

	// Make sure multitasking has been enabled
	if (!multitaskingEnabled)
		return (status = ERR_NOTINITIALIZED);

	while ((killProcess = getProcessByName(name)))
		status = kernelMultitaskerKillProcess(killProcess->processId, force);

	return (status);
}


int kernelMultitaskerKillAll(void)
{
	// This function is used to shut down all processes currently running.
	// Normally, this will be done during multitasker shutdown.  Returns 0 on
	// success, negative otherwise.

	int status = 0;
	kernelProcess *killProcess = NULL;
	int count;

	// Make sure multitasking has been enabled
	if (!multitaskingEnabled)
		return (status = ERR_NOTINITIALIZED);

	// Kill all of the processes, except the kernel process and the current
	// process.  This won't kill the scheduler's task either.

	// Stop all processes, except the kernel process, and the current process.
	for (count = 0; count < numQueued; count ++)
	{
		if (PROC_KILLABLE(processQueue[count]))
			processQueue[count]->state = proc_stopped;
	}

	for (count = 0; count < numQueued; )
	{
		if (!PROC_KILLABLE(processQueue[count]))
		{
			count++;
			continue;
		}

		killProcess = processQueue[count];

		// Attempt to kill it.
		status = kernelMultitaskerKillProcess(killProcess->processId,
			0 /* no force */);
		if (status < 0)
		{
			// Try it with a force
			status = kernelMultitaskerKillProcess(killProcess->processId,
				1 /* force */);
			if (status < 0)
			{
				// Still errors?  The process will still be in the queue.
				// We need to skip past it.
				count++;
				continue;
			}
		}
	}

	// Return success
	return (status = 0);
}


int kernelMultitaskerTerminate(int retCode)
{
	// This function is designed to allow a process to terminate itself
	// normally, and return a result code (this is the normal way to exit()).
	// On error, the function returns negative.  On success, of course, it
	// doesn't return at all.

	int status = 0;
	kernelProcess *parent = NULL;

	// Make sure multitasking has been enabled
	if (!multitaskingEnabled)
		return (status = ERR_NOTINITIALIZED);

	// Don't do this inside an interrupt
	if (kernelProcessingInterrupt())
	{
		kernelPanic("Cannot terminate() inside an interrupt handler (%d)",
			kernelInterruptGetCurrent());
	}

	// Find the parent process before we terminate ourselves
	parent = getProcessById(kernelCurrentProcess->parentProcessId);

	if (parent)
	{
		// We found our parent process.  Is it blocking, waiting for us?
		if (parent->waitForProcess == kernelCurrentProcess->processId)
		{
			// It's waiting for us to finish.  Put our return code into
			// its blockingExitCode field
			parent->blockingExitCode = retCode;
			parent->waitForProcess = 0;
			parent->state = proc_ready;

			// Done.
		}
	}

	while (1)
	{
		// If we still have threads out there, we don't dismantle until they
		// are finished
		if (!kernelCurrentProcess->descendentThreads)
			// Terminate
			kernelCurrentProcess->state = proc_finished;

		kernelMultitaskerYield();
	}
}


int kernelMultitaskerSignalSet(int processId, int sig, int on)
{
	// Set signal handling enabled (on) or disabled for the specified signal

	int status = 0;
	kernelProcess *signalProcess = NULL;

	// Make sure multitasking has been enabled
	if (!multitaskingEnabled)
		return (status = ERR_NOTINITIALIZED);

	// Make sure the signal number fits in the signal mask
	if ((sig <= 0) || (sig >= SIGNALS_MAX))
	{
		kernelError(kernel_error, "Invalid signal code %d", sig);
		return (status = ERR_RANGE);
	}

	// Try to find the process
	signalProcess = getProcessById(processId);
	if (!signalProcess)
	{
		// There's no such process
		kernelError(kernel_error, "No process %d to signal", processId);
		return (status = ERR_NOSUCHPROCESS);
	}

	// If there is not yet a signal stream allocated for this process, do it
	// now.
	if (!(signalProcess->signalStream.buffer))
	{
		status = kernelStreamNew(&signalProcess->signalStream, 16,
			itemsize_dword);
		if (status < 0)
			return (status);
	}

	if (on)
		signalProcess->signalMask |= (1 << sig);
	else
		signalProcess->signalMask &= ~(1 << sig);

	return (status = 0);
}


int kernelMultitaskerSignal(int processId, int sig)
{
	int status = 0;
	kernelProcess *signalProcess = NULL;

	// Make sure multitasking has been enabled
	if (!multitaskingEnabled)
		return (status = ERR_NOTINITIALIZED);

	// Make sure the signal number fits in the signal mask
	if ((sig <= 0) || (sig >= SIGNALS_MAX))
	{
		kernelError(kernel_error, "Invalid signal code %d", sig);
		return (status = ERR_RANGE);
	}

	// Try to find the process
	signalProcess = getProcessById(processId);
	if (!signalProcess)
	{
		// There's no such process
		kernelError(kernel_error, "No process %d to signal", processId);
		return (status = ERR_NOSUCHPROCESS);
	}

	// See if the signal is handled, and make sure there's a signal stream
	if (!(signalProcess->signalMask & (1 << sig)) ||
		!(signalProcess->signalStream.buffer))
	{
		// Not handled.  Terminate the process.
		signalProcess->state = proc_finished;
		return (status = 0);
	}

	// Put the signal into the signal stream
	status = signalProcess->signalStream.append(&signalProcess->signalStream,
		sig);

	return (status);
}


int kernelMultitaskerSignalRead(int processId)
{
	int status = 0;
	kernelProcess *signalProcess = NULL;
	int sig;

	// Make sure multitasking has been enabled
	if (!multitaskingEnabled)
		return (status = ERR_NOTINITIALIZED);

	// Try to find the process
	signalProcess = getProcessById(processId);
	if (!signalProcess)
	{
		// There's no such process
		kernelError(kernel_error, "No process %d to signal", processId);
		return (status = ERR_NOSUCHPROCESS);
	}

	// Any signals handled?
	if (!(signalProcess->signalMask))
		return (status = 0);

	// Make sure there's a signal stream
	if (!(signalProcess->signalStream.buffer))
	{
		kernelError(kernel_error, "Process has no signal stream");
		return (status = ERR_NOTINITIALIZED);
	}

	if (!signalProcess->signalStream.count)
		return (sig = 0);

	status = signalProcess->signalStream.pop(&signalProcess->signalStream,
		&sig);

	if (status < 0)
		return (status);
	else
		return (sig);
}


int kernelMultitaskerGetIoPerm(int processId, int portNum)
{
	// Check if the given process can use I/O ports specified.  Returns 1 if
	// permission is allowed.

	int status = 0;
	kernelProcess *ioProcess = NULL;

	// Make sure multitasking has been enabled
	if (!multitaskingEnabled)
		return (status = ERR_NOTINITIALIZED);

	ioProcess = getProcessById(processId);
	if (!ioProcess)
	{
		// There's no such process
		kernelError(kernel_error, "No process %d to get I/O permissions",
			processId);
		return (status = ERR_NOSUCHPROCESS);
	}

	if (portNum >= IO_PORTS)
		return (status = ERR_BOUNDS);

	// If the bit is set, permission is not granted.
	if (GET_PORT_BIT(ioProcess->taskStateSegment.IOMap, portNum))
		return (status = 0);
	else
		return (status = 1);
}


int kernelMultitaskerSetIoPerm(int processId, int portNum, int yesNo)
{
	// Allow or deny I/O port permission to the given process.

	int status = 0;
	kernelProcess *ioProcess = NULL;

	// Make sure multitasking has been enabled
	if (!multitaskingEnabled)
		return (status = ERR_NOTINITIALIZED);

	ioProcess= getProcessById(processId);
	if (!ioProcess)
	{
		// There's no such process
		kernelError(kernel_error, "No process %d to set I/O permissions",
			processId);
		return (status = ERR_NOSUCHPROCESS);
	}

	if (portNum >= IO_PORTS)
		return (status = ERR_BOUNDS);

	if (yesNo)
		UNSET_PORT_BIT(ioProcess->taskStateSegment.IOMap, portNum);
	else
		SET_PORT_BIT(ioProcess->taskStateSegment.IOMap, portNum);

	return (status = 0);
}


kernelPageDirectory *kernelMultitaskerGetPageDir(int processId)
{
	kernelProcess *dirProcess = NULL;

	// Make sure multitasking has been enabled
	if (!multitaskingEnabled)
		return (NULL);

	dirProcess = getProcessById(processId);
	if (!dirProcess)
	{
		// There's no such process
		kernelError(kernel_error, "No process %d to get page directory",
			processId);
		return (NULL);
	}

	return (dirProcess->pageDirectory);
}


loaderSymbolTable *kernelMultitaskerGetSymbols(int processId)
{
	// Given a process ID, return the symbol table of the process.

	kernelProcess *symProcess = NULL;

	// Make sure multitasking has been enabled
	if (!multitaskingEnabled)
		return (NULL);

	symProcess = getProcessById(processId);
	if (!symProcess)
	{
		// There's no such process
		kernelError(kernel_error, "No process %d to get symbols", processId);
		return (NULL);
	}

	return (symProcess->symbols);
}


int kernelMultitaskerSetSymbols(int processId, loaderSymbolTable *symbols)
{
	// Given a process ID and a symbol table, attach the symbol table to the
	// process.

	int status = 0;
	kernelProcess *symProcess = NULL;

	// Make sure multitasking has been enabled
	if (!multitaskingEnabled)
		return (status = ERR_NOTINITIALIZED);

	symProcess = getProcessById(processId);
	if (!symProcess)
	{
		// There's no such process
		kernelError(kernel_error, "No process %d to set symbols", processId);
		return (status = ERR_NOSUCHPROCESS);
	}

	symProcess->symbols = symbols;
	return (status = 0);
}


int kernelMultitaskerStackTrace(int processId)
{
	// Locate the process by ID and do a stack trace of it.

	int status = 0;
	kernelProcess *traceProcess = NULL;
	char buffer[MAXSTRINGLENGTH];

	// Make sure multitasking has been enabled
	if (!multitaskingEnabled)
		return (status = ERR_NOTINITIALIZED);

	// Locate the process
	traceProcess = getProcessById(processId);
	if (!traceProcess)
	{
		// There's no such process
		kernelError(kernel_error, "No process %d to trace", processId);
		return (status = ERR_NOSUCHPROCESS);
	}

	// Do the stack trace
	status = kernelStackTrace(traceProcess, buffer, MAXSTRINGLENGTH);

	if (status >= 0)
		kernelTextPrint("%s", buffer);

	return (status);
}


int kernelMultitaskerPropagateEnvironment(const char *variable)
{
	// Allows the current process to set and overwrite its descendent
	// processes' environment variables.  If 'variable' is set, only the named
	// variable will propagate.  Otherwise, all variables will propagate.
	// Variables in the childrens' environments that don't exist in the parent
	// process are unaffected.

	int status = 0;

	kernelDebug(debug_multitasker, "Multitasker propagate environment");

	// Make sure multitasking has been enabled
	if (!multitaskingEnabled)
		return (status = ERR_NOTINITIALIZED);

	// Make sure the current process isn't NULL
	if (!kernelCurrentProcess)
	{
		kernelError(kernel_error, "Can't determine the current process");
		return (status = ERR_BUG);
	}

	status = propagateEnvironmentRecursive(kernelCurrentProcess,
		kernelCurrentProcess->environment, variable);

	return (status);
}

