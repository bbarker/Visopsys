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
//  kernelMultitasker.c
//

// This file contains the C functions belonging to the kernel's 
// multitasker

#include "kernelMultitasker.h"
#include "kernelParameters.h"
#include "kernelMemoryManager.h"
#include "kernelMalloc.h"
#include "kernelFile.h"
#include "kernelMain.h"
#include "kernelPageManager.h"
#include "kernelProcessorX86.h"
#include "kernelPic.h"
#include "kernelSysTimer.h"
#include "kernelEnvironment.h"
#include "kernelShutdown.h"
#include "kernelMiscFunctions.h"
#include "kernelInterrupt.h"
#include "kernelLog.h"
#include "kernelError.h"
#include <stdio.h>
#include <string.h>
#include <signal.h>


// Global multitasker stuff
static int multitaskingEnabled = 0;
static volatile int processIdCounter = KERNELPROCID;
static kernelProcess *kernelProc = NULL;
static kernelProcess *idleProc = NULL;
static kernelProcess *exceptionProc = NULL;
static kernelProcess *deadProcess;
static volatile int schedulerSwitchedByCall = 0;

// We allow the pointer to the current process to be exported, so that
// when a process uses system calls, there is an easy way for the
// process to get information about itself.
kernelProcess *kernelCurrentProcess = NULL;

// Process queue for CPU execution
static kernelProcess* processQueue[MAX_PROCESSES];
static volatile int numQueued = 0;

// Things specific to the scheduler.  The scheduler process is just a
// convenient place to keep things, we don't use all of it and it doesn't
// go in the queue
static kernelProcess *schedulerProc = NULL;
static volatile int schedulerStop = 0;
static void (*oldSysTimerHandler)(void) = NULL;
static volatile unsigned schedulerTimeslices = 0;
static volatile unsigned schedulerTime = 0;

#define PROC_KILLABLE(proc) ((proc != kernelProc) &&        \
                             (proc != exceptionProc) &&     \
                             (proc != idleProc) &&          \
                             (proc != kernelCurrentProcess))
  

static kernelProcess *getProcessById(int processId)
{
  // This routine is used to find a process' pointer based on the process
  // Id.  Nothing fancy -- it just searches through the list.  Maybe later
  // it can be some kind of fancy sorting/searching procedure.  Returns NULL
  // if the process doesn't exist
  
  kernelProcess *theProcess = NULL;
  int count;

  for (count = 0; count < numQueued; count ++)
    if (processQueue[count]->processId == processId)
      {
        theProcess = processQueue[count];
        break;
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
    if (!strcmp((char *) processQueue[count]->processName, name))
      {
        theProcess = processQueue[count];
        break;
      }

  // If we didn't find it, this will still be NULL
  return (theProcess);
}


static inline int requestProcess(kernelProcess **processPointer)
{
  // This routine is used to allocate new process control memory.  It
  // should be passed a reference to a pointer that will point to
  // the new process, if allocated successfully.

  int status = 0;
  kernelProcess *newProcess = NULL;

  // Make sure the pointer->pointer parameter we were passed isn't NULL
  if (processPointer == NULL)
    // Oops.
    return (status = ERR_NULLPARAMETER);
  
  newProcess = kernelMalloc(sizeof(kernelProcess));
  if (newProcess == NULL)
    return (status = ERR_MEMORY);

  // Success.  Set the pointer for the calling process
  *processPointer = newProcess;
  return (status = 0);
}


static inline int releaseProcess(kernelProcess *killProcess)
{
  // This routine is used to free process control memory.  It
  // should be passed the process Id of the process to kill.  It
  // returns 0 on success, negative otherwise

  int status = 0;
  status = kernelFree((void *) killProcess);
  return (status);
}


static int addProcessToQueue(kernelProcess *targetProcess)
{
  // This routine will add a process to the task queue.  It returns zero 
  // on success, negative otherwise

  int status = 0;
  int count;
  
  if (targetProcess == NULL)
    // The process does not exist, or is not accessible
    return (status = ERR_NOSUCHPROCESS);

  // Make sure the priority is a legal value
  if ((targetProcess->priority < 0) || 
      (targetProcess->priority > (PRIORITY_LEVELS - 1)))
    // The process' priority is an illegal value
    return (status = ERR_INVALID);

  // Search the process queue to make sure it isn't already present
  for (count = 0; count < numQueued; count ++)
    if (processQueue[count] == targetProcess)
      // Oops, it's already there
      return (status = ERR_ALREADY);
  
  // OK, now we can add the process to the queue
  processQueue[numQueued++] = targetProcess;

  // Done
  return (status = 0);
}


static int removeProcessFromQueue(kernelProcess *targetProcess)
{
  // This routine will remove a process from the task queue.  It returns zero 
  // on success, negative otherwise

  int status = 0;
  int processPosition = 0;
  int count;
  
  if (targetProcess == NULL)
    // The process does not exist, or is not accessible
    return (status = ERR_NOSUCHPROCESS);

  // Search the queue for the matching process
  for (count = 0; count < numQueued; count ++)
    if (processQueue[count] == targetProcess)
      {
	processPosition = count;
	break;
      }

  // Make sure we found the process
  if (processQueue[processPosition] != targetProcess)
    // The process is not in the task queue
    return (status = ERR_NOSUCHPROCESS);

  // Subtract one from the number of queued processes
  numQueued -= 1;

  // OK, now we can remove the process from the queue.  If there are one
  // or more remaining processes in this queue, we will shorten the queue
  // by moving the LAST process into the spot we're vacating
  if ((numQueued > 0) && (processPosition != numQueued))
    processQueue[processPosition] = processQueue[numQueued];

  // Done
  return (status = 0);
}


static int createTaskStateSegment(kernelProcess *theProcess,
				  void *processPageDir)
{
  // This function will create a TSS (Task State Segment) for a new
  // process based on the attributes of the process.  This function relies
  // on the privilege, userStackSize, and superStackSize attributes having 
  // been previously set.  Returns 0 on success, negative on error.

  int status = 0;

  // Get a free descriptor for the process' TSS
  status = kernelDescriptorRequest(&(theProcess->tssSelector));
  if ((status < 0) || (theProcess->tssSelector == 0))
    // Crap.  An error getting a free descriptor.
    return (status);
  
  // Fill in the process' Task State Segment descriptor
  status = kernelDescriptorSet(
     theProcess->tssSelector, // TSS selector number
     &(theProcess->taskStateSegment), // Starts at...
     sizeof(kernelTSS),      // Limit of a TSS segment
     1,                      // Present in memory
     PRIVILEGE_SUPERVISOR,   // TSSs are supervisor privilege level
     0,                      // TSSs are system segs
     0xB,                    // TSS, 32-bit, busy
     0,                      // 0 for SMALL size granularity
     0);                     // Must be 0 in TSS
  if (status < 0)
    {
      // Crap.  An error getting a free descriptor.
      kernelDescriptorRelease(theProcess->tssSelector);
      return (status);
    }

  // Now, fill in the TSS (Task State Segment) for the new process.  Parts
  // of this will be different depending on whether this is a user
  // or supervisor mode process

  kernelMemClear((void *) &(theProcess->taskStateSegment), sizeof(kernelTSS));

  if (theProcess->privilege == PRIVILEGE_SUPERVISOR)
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
    }

  theProcess->taskStateSegment.ES = theProcess->taskStateSegment.DS;
  theProcess->taskStateSegment.FS = theProcess->taskStateSegment.DS;
  theProcess->taskStateSegment.GS = theProcess->taskStateSegment.DS;

  theProcess->taskStateSegment.ESP =
    ((unsigned) theProcess->userStack +
     (theProcess->userStackSize - sizeof(int)));

  if (theProcess->privilege == PRIVILEGE_USER)
    {
      theProcess->taskStateSegment.SS0 = PRIV_STACK;
      theProcess->taskStateSegment.ESP0 = ((unsigned) theProcess->superStack + 
					   (theProcess->superStackSize -
					    sizeof(int)));
    }

  theProcess->taskStateSegment.EFLAGS = 0x00000202; // Interrupts enabled
  theProcess->taskStateSegment.CR3 = ((unsigned) processPageDir);

  // All remaining values will be NULL from initialization.  Note that this 
  // includes the EIP.

  // Return success
  return (status = 0);
}


static int createNewProcess(const char *name, int priority, int privilege,
			    processImage *execImage, int newPageDir)
{
  // This function is used to create a new process in the process
  // queue.  It makes a "defaults" kind of process -- it sets up all of
  // the process' attributes with default values.  If the calling process
  // wants something different, it should reset those attributes afterward.
  // If successful, it returns the processId of the new process.  Otherwise, 
  // it returns negative.

  int status = 0;
  kernelProcess *newProcess = NULL;
  void *stackMemoryAddr = NULL;
  void *processPageDir = NULL;
  void *physicalCodeData = NULL;
  int *args = NULL;
  char **argv = NULL;
  int argSpaceSize = 0;
  char *argSpace = NULL;
  char *newArgAddress = NULL;
  int length = 0;
  int count;

  // Don't bother checking the parameters, as the external functions 
  // should have done this already.

  // We need to see if we can get some fresh process control memory
  status = requestProcess(&newProcess);
  if (status < 0)
    return (status);

  if (newProcess == NULL)
    return (status = ERR_NOFREE);

  // Ok, we got a new, fresh process.  We need to start filling in some
  // of the process' data (after initializing it, of course)
  kernelMemClear((void *) newProcess, sizeof(kernelProcess));

  // Fill in the process' Id number
  newProcess->processId = processIdCounter++;

  // By default, the type is process
  newProcess->type = proc_normal;

  // Now, if the process Id is KERNELPROCID, then we are creating the
  // kernel process, and it will be its own parent.  Otherwise, get the
  // current process and make IT be the parent of this new process
  if (newProcess->processId == KERNELPROCID)
    {
      newProcess->parentProcessId = newProcess->processId;
      newProcess->userId = 1;   // root
      // Give it "/" as current working directory
      strncpy((char *) newProcess->currentDirectory, "/", 2);
    }
  else
    {
      // Make sure the current process isn't NULL
      if (kernelCurrentProcess == NULL)
	{
	  releaseProcess(newProcess);
	  return (status = ERR_NOSUCHPROCESS);
	}

      // Fill in the process' parent Id number
      newProcess->parentProcessId = kernelCurrentProcess->processId;
      // Fill in the process' user Id number
      newProcess->userId = kernelCurrentProcess->userId;
      // Fill in the current working directory
      strncpy((char *) newProcess->currentDirectory, 
	      (char *) kernelCurrentProcess->currentDirectory,
	      MAX_PATH_LENGTH);
      newProcess->currentDirectory[MAX_PATH_LENGTH - 1] = '\0';
    }

  // Fill in the process name
  strncpy((char *) newProcess->processName, name, MAX_PROCNAME_LENGTH);
  newProcess->processName[MAX_PROCNAME_LENGTH - 1] = '\0';

  // Fill in the process' priority level
  newProcess->priority = priority;

  // Fill in the process' privilege level
  newProcess->privilege = privilege;

  // The amount of time since started (now)
  newProcess->startTime = kernelSysTimerRead();

  // The thread's initial state will be "stopped"
  newProcess->state = proc_stopped;

  // Do we need to create a new page directory and a set of page tables for 
  // this process?
  if (newPageDir)
    {
      // We need to make a new page directory, etc.
      processPageDir = kernelPageNewDirectory(newProcess->processId,
					      newProcess->privilege);
      if (processPageDir == NULL)
	{
	  // Not able to setup a page directory
	  releaseProcess(newProcess);
	  return (status = ERR_NOVIRTUAL);
	}

      // Get the physical address of the code/data
      physicalCodeData =
	kernelPageGetPhysical(newProcess->parentProcessId, execImage->code);

      // Make the process own its code/data memory.  Don't remap it yet
      // because we want to map it at the requested virtual address.
      status = kernelMemoryChangeOwner(newProcess->parentProcessId,
				newProcess->processId, 0, execImage->code,
				NULL);
      if (status < 0)
	{
	  // Couldn't make the process own its memory
	  releaseProcess(newProcess);
	  return (status);
	}

      // Remap the code/data to the requested virtual address.
      status = kernelPageMap(newProcess->processId, physicalCodeData,
			     execImage->virtualAddress, execImage->imageSize);
      if (status < 0)
	{
	  // Couldn't map the process memory
	  releaseProcess(newProcess);
	  return (status);
	}

      // Code should be read-only
      status =
	kernelPageSetAttrs(newProcess->processId, 0, PAGEFLAG_WRITABLE,
			   execImage->virtualAddress, execImage->codeSize);
      if (status < 0)
	{
	  releaseProcess(newProcess);
	  return (status);
	}
    }
  else
    {
      // This process will share a page directory with its parent
      processPageDir = kernelPageShareDirectory(newProcess->parentProcessId, 
						newProcess->processId);
      if (processPageDir == NULL)
	{
	  // Not able to setup a page directory
	  releaseProcess(newProcess);
	  return (status = ERR_NOVIRTUAL);
	}
    }

  // Give the process a stack
  stackMemoryAddr =
    kernelMemoryGet((DEFAULT_STACK_SIZE + DEFAULT_SUPER_STACK_SIZE),
		    "process stack");
  if (stackMemoryAddr == NULL)
    {
      // We couldn't make a stack for the new process.  Maybe the system
      // doesn't have anough available memory?
      releaseProcess(newProcess);
      return (status = ERR_MEMORY);
    }

  if (newProcess->privilege == PRIVILEGE_USER)
    {
      newProcess->userStackSize = DEFAULT_STACK_SIZE;
      newProcess->superStackSize = DEFAULT_SUPER_STACK_SIZE;
    }
  else
    newProcess->userStackSize =
      (DEFAULT_STACK_SIZE + DEFAULT_SUPER_STACK_SIZE);

  // Copy 'argc' and 'argv' arguments to the new process' stack while we
  // still own the stack memory.

  // Set pointers to the appropriate stack locations for the arguments
  args = (stackMemoryAddr + newProcess->userStackSize - (2 * sizeof(int)));

  // Calculate the amount of memory we need to allocate for argument data.
  // Leave space for pointers to the strings, since the (int argc,
  // char *argv[]) scheme means just 2 values on the stack: an integer
  // an a pointer to an array of char* pointers...
  argSpaceSize = ((execImage->argc + 1) * sizeof(char *));
  for (count = 0; count < execImage->argc; count ++)
    argSpaceSize += (strlen(execImage->argv[count]) + 1);

  // Get memory for the argument data
  argSpace = kernelMemoryGet(argSpaceSize, "process arguments");
  if (argSpace == NULL)
    {
      kernelMemoryRelease(stackMemoryAddr);
      releaseProcess(newProcess);
      return (status = ERR_MEMORY);
    }
      
  // Change ownership to the new process, and share it back with this process.
  if (kernelMemoryChangeOwner(newProcess->parentProcessId,
			      newProcess->processId, 1, argSpace,
			      (void **) &newArgAddress) < 0)
    {
      kernelMemoryRelease(stackMemoryAddr);
      kernelMemoryRelease(argSpace);
      releaseProcess(newProcess);
      return (status = ERR_MEMORY);
    }

  if (kernelMemoryShare(newProcess->processId, newProcess->parentProcessId,
			newArgAddress, (void **) &argSpace) < 0)
    {
      kernelMemoryRelease(stackMemoryAddr);
      releaseProcess(newProcess);
      return (status = ERR_MEMORY);
    }

  args[0] = execImage->argc;
  args[1] = (int) newArgAddress;
	
  argv = (char **) argSpace;
  argSpace += ((execImage->argc + 1) * sizeof(char *));
  newArgAddress += ((execImage->argc + 1) * sizeof(char *));

  // Copy the args into argv
  for (count = 0; count < execImage->argc; count ++)
    {
      strcpy((argSpace + length), execImage->argv[count]);
      argv[count] = (newArgAddress + length);
      length += (strlen(execImage->argv[count]) + 1);
    }

  // argv[argc] is supposed to be a NULL pointer, according to
  // some standard or other
  argv[args[0]] = NULL;

  // Unmap the argument space from this process
  kernelPageUnmap(newProcess->parentProcessId, argSpace, argSpaceSize);

  // Make the process own its stack memory
  status = kernelMemoryChangeOwner(newProcess->parentProcessId, 
				   newProcess->processId, 1, // remap
				   stackMemoryAddr, 
				   (void **) &(newProcess->userStack));
  if (status < 0)
    {
      // Couldn't make the process own its memory
      kernelMemoryRelease(stackMemoryAddr);
      releaseProcess(newProcess);
      return (status);
    }

  // Get the new virtual address of supervisor stack
  if (newProcess->privilege == PRIVILEGE_USER)
    newProcess->superStack = (newProcess->userStack + DEFAULT_STACK_SIZE);

  // Create the TSS (Task State Segment) for this process.
  status = createTaskStateSegment(newProcess, processPageDir);
  if (status < 0)
    {
      // Not able to create the TSS
      releaseProcess(newProcess);
      return (status);
    }

  // Adjust the stack pointer to account for the arguments that we copied to
  // the process' stack
  newProcess->taskStateSegment.ESP -= sizeof(int);

  // Set the EIP to the entry point
  newProcess->taskStateSegment.EIP = (unsigned) execImage->entryPoint;

  // added by Davide Airaghi for FPU-state handling                                                                                             
  FPU_STATUS_ZERO(newProcess->fpu,count)    

  // Finally, add the process to the process queue
  status = addProcessToQueue(newProcess);
  if (status < 0)
    {
      // Not able to queue the process.
      releaseProcess(newProcess);
      return (status);
    }

  // Return the processId on success.
  return (status = newProcess->processId);
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

  // Remove the process from the multitasker's process queue.
  status = removeProcessFromQueue(killProcess);
  if (status < 0)
    {
      // Not able to remove the process
      kernelError(kernel_error, "Can't dequeue process");
      return (status);
    }

  // We need to deallocate the TSS descriptor allocated to the process, if
  // it has one
  if (killProcess->tssSelector)
    {
      status = kernelDescriptorRelease(killProcess->tssSelector);
      // If this was unsuccessful, we don't want to continue and "lose" 
      // the descriptor
      if (status < 0)
	{
	  kernelError(kernel_error, "Can't release TSS");
	  return (status);
	}
    }

  // If the process has a signal stream, destroy it
  if (killProcess->signalStream.buffer)
    kernelStreamDestroy((stream *) &(killProcess->signalStream));

  // Deallocate all memory owned by this process
  status = kernelMemoryReleaseAllByProcId(killProcess->processId);
  // Likewise, if this deallocation was unsuccessful, we don't want to
  // deallocate the process structure.  If we did, the memory would become
  // "lost".
  if (status < 0)
    {
      kernelError(kernel_error, "Can't release process memory");
      return (status);
    }

  // Delete the page table we created for this process
  status = kernelPageDeleteDirectory(killProcess->processId);
  // If this deletion was unsuccessful, we don't want to deallocate the 
  // process structure.  If we did, the page directory would become "lost".
  if (status < 0)
    {
      kernelError(kernel_error, "Can't release page directory");
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


static int exceptionThreadInitialize(void)
{
  // This function will initialize the kernel's exception handler thread.  
  // It should be called after multitasking has been initialized.  

  int status = 0;
  int procId = 0;
  int count;

  // One of the first things the kernel does at startup time is to install 
  // a simple set of interrupt handlers, including ones for handling 
  // processor exceptions.  We want to replace those with a set of task
  // gates, so that a context switch will occur -- giving control to the
  // exception handler thread.

  // OK, we will now create the kernel's exception handler thread.

  procId = kernelMultitaskerSpawn(&kernelExceptionHandler, "exception thread",
				  0, NULL);
  if (procId < 0)
    {
      kernelError(kernel_error, "Unable to create the kernel's exception "
		  "thread");
      return (procId);
    }

  exceptionProc = getProcessById(procId);
  if (exceptionProc == NULL)
    {
      kernelError(kernel_error, "Unable to create the kernel's exception "
		  "thread");
      return (procId);
    }

  // Set the process state to sleep
  exceptionProc->state = proc_sleeping;

  status = kernelDescriptorSet(
	       exceptionProc->tssSelector, // TSS selector
	       &(exceptionProc->taskStateSegment), // Starts at...
	       sizeof(kernelTSS),      // Maximum size of a TSS selector
	       1,                      // Present in memory
	       PRIVILEGE_SUPERVISOR,   // Highest privilege level
	       0,                      // TSS's are system segs
	       0x9,                    // TSS, 32-bit, non-busy
	       0,                      // 0 for SMALL size granularity
	       0);                     // Must be 0 in TSS
  if (status < 0)
    // Something went wrong
    return (status);

  // Interrupts should always be disabled for this task 
  exceptionProc->taskStateSegment.EFLAGS = 0x00000002;

  // Set up interrupt task gates to send all the exceptions to this new
  // thread
  for (count = 0; count < 19; count ++)
    {
      status =
	kernelDescriptorSetIDTTaskGate(count, exceptionProc->tssSelector);
      if (status < 0)
	{
	  kernelError(kernel_error, "Unable to set interrupt task gate for "
		      "exception %d", count);
	  return (status);
	}
    }

  return (status = 0);
}


static void idleThread(void)
{
  // This is the idle task.  It runs in this loop whenever no other 
  // processes need the CPU.  However, the only thing it does inside
  // that loop -- as you can probably see -- is run in a loop.  This should
  // be run at the absolute lowest possible priority so that it will not 
  // be run unless there is absolutely nothing else in the other queues 
  // that is ready.
  while(1);
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
  if (idleProc == NULL)
    return (status = ERR_NOSUCHPROCESS);

  // Set it to the lowest priority
  status =
    kernelMultitaskerSetProcessPriority(idleProcId, (PRIORITY_LEVELS - 1));
  if (status < 0)
    // There's no reason we should have to fail here, but make a warning
    kernelError(kernel_warn, "The multitasker was unable to lower the "
		"priority of the idle thread");

  // Return success
  return (status = 0);
}


static int schedulerShutdown(void)
{
  // This function will perform all of the necessary shutdown to stop
  // the sheduler and return control to the kernel's main task.
  // This will probably only be useful at system shutdown time.

  // NOTE that this function should NEVER be called directly.  If this
  // advice is ignored, you have no assurance that things will occur
  // the way you might expect them to.  To shut down the scheduler,
  // set the variable schedulerStop to a nonzero value.  The scheduler
  // will then invoke this function when it's ready.

  int status = 0;

  // Restore the normal operation of the system timer 0, which is
  // mode 3, initial count of 0
  status = kernelSysTimerSetupTimer(0, 3, 0);
  if (status < 0)
    kernelError(kernel_warn, "Could not restore system timer");

  // Remove the task gate that we were using to capture the timer
  // interrupt.  Replace it with the old default timer interrupt handler
  kernelInterruptHook(INTERRUPT_NUM_SYSTIMER, oldSysTimerHandler);

  // Give exclusive control to the current task
  schedulerProc->taskStateSegment.oldTSS = kernelCurrentProcess->tssSelector;
  // Do an interrupt return.
  kernelProcessorIntReturn();

  // We should never get here
  return (status = 0);
}


static int markTaskBusy(int tssSelector, int busy)
{
  // This function gets the requested TSS selector from the GDT and
  // marks it as busy/not busy.  Returns negative on error.
  
  int status = 0;
  kernelDescriptor descriptor;
  
  // Initialize our empty descriptor
  kernelMemClear(&descriptor, sizeof(kernelDescriptor));

  // Fill out our descriptor with data from the "official" one that 
  // corresponds to the selector we were given
  status = kernelDescriptorGet(tssSelector, &descriptor);
  if (status < 0)
    return (status);

  // Ok, now we can change the selector in the table
  if (busy)
    descriptor.attributes1 |= 0x00000002;
  else
    descriptor.attributes1 &= ~0x00000002;
    
  // Re-set the descriptor in the GDT
  status =  kernelDescriptorSetUnformatted(tssSelector, 
   descriptor.segSizeByte1, descriptor.segSizeByte2, descriptor.baseAddress1,
   descriptor.baseAddress2, descriptor.baseAddress3, descriptor.attributes1, 
   descriptor.attributes2, descriptor.baseAddress4);
  if (status < 0)
    return (status);

  // Return success
  return (status = 0);
}


static int scheduler(void)
{
  // Well, here it is:  the kernel multitasker's scheduler.  This little
  // program will run continually in a loop, handing out time slices to
  // all processes, including the kernel itself.

  // By the time this scheduler is invoked, the kernel should already have
  // created itself a process in the task queue.  Thus, the scheduler can 
  // begin by simply handing all time slices to the kernel.

  // Additional processes will be created with calls to the kernel, which
  // will create them and place them in the queue.  Thus, when the 
  // scheduler regains control after a time slice has expired, the 
  // queue of processes that it examines will have the new process added.

  int status = 0;
  kernelProcess *miscProcess = NULL;
  volatile unsigned theTime = 0;
  volatile int timeUsed = 0;
  volatile int timerTicks = 0;
  int count;

  // This is info about the processes we run
  kernelProcess *nextProcess = NULL;
  kernelProcess *previousProcess = NULL;
  unsigned processWeight = 0;
  unsigned topProcessWeight = 0;

  // Here is where we make decisions about which tasks to schedule,
  // and when.  Below is a brief description of the scheduling
  // algorithm.
  
  // There will be two "special" queues in the multitasker.  The
  // first (highest-priority) queue will be the "real time" queue.
  // When there are any processes running and ready at this priority 
  // level, they will be serviced to the exclusion of all processes from
  // other queues.  Not even the kernel process will reside in this
  // queue.  
  
  // The last (lowest-priority) queue will be the "background" 
  // queue.  Processes in this queue will only receive processor time
  // when there are no ready processes in any other queue.  Unlike
  // all of the "middle" queues, it will be possible for processes
  // in this background queue to starve.
  
  // The number of priority queues will be somewhat flexible based on
  // a configuration macro in the multitasker's header file.  However, 
  // The special queues mentioned above will exhibit the same behavior 
  // regardless of the number of "normal" queues in the system.
  
  // Amongst all of the processes in the other queues, there will be
  // a more even-handed approach to scheduling.  We will attempt
  // a fair algorithm with a weighting scheme.  Among the weighting
  // variables will be the following: priority, waiting time, and
  // "shortness".  Shortness will probably come later 
  // (shortest-job-first), so for now we will concentrate on 
  // priority and shortness.  The formula will look like this:
  //
  // weight = ((NUM_QUEUES - taskPriority) * PRIORITY_RATIO) + waitTime
  //
  // This means that the inverse of the process priority will be
  // multiplied by the "priority ratio", and to that will be added the
  // current waiting time.  For example, if we have 4 priority levels, 
  // the priority ratio is 3, and we have two tasks as follows:
  //
  // Task 1: priority=0, waiting time=7
  // Task 2: priority=2, waiting time=12
  // 
  // then
  //
  // task1Weight = ((4 - 0) * 3) + 7  = 19  <- winner
  // task2Weight = ((4 - 2) * 3) + 12 = 18
  // 
  // Thus, even though task 2 has been waiting considerably longer, 
  // task 1's higher priority wins.  However in a slightly different 
  // scenario -- using the same constants -- if we had:
  //
  // Task 1: priority=0, waiting time=7
  // Task 2: priority=2, waiting time=14
  // 
  // then
  //
  // task1Weight = ((4 - 0) * 3) + 7  = 19
  // task2Weight = ((4 - 2) * 3) + 14 = 20  <- winner
  // 
  // In this case, task 2 gets to run since it has been waiting 
  // long enough to overcome task 1's higher priority.  This possibility
  // helps to ensure that no processes will starve.  The priority 
  // ratio determines the weighting of priority vs. waiting time.  
  // A priority ratio of zero would give higher-priority processes 
  // no advantage over lower-priority, and waiting time would
  // determine execution order.
  // 
  // A tie beteen the highest-weighted tasks is broken based on 
  // queue order.  The queue is neither FIFO nor LIFO, but closer to
  // LIFO.

  // Here is the scheduler's big loop

  while (schedulerStop == 0)
    {
      // Make sure.  No interrupts allowed inside this task.
      kernelProcessorDisableInts();

      // Calculate how many timer ticks were used in the previous time slice.
      // This will be different depending on whether the previous timeslice
      // actually expired, or whether we were called for some other reason
      // (for example a yield()).  The timer wraps around if the timeslice
      // expired, so we can't use that value -- we use the length of an entire
      // timeslice instead.

      if (!schedulerSwitchedByCall)
	timeUsed = TIME_SLICE_LENGTH;
      else
	timeUsed = (TIME_SLICE_LENGTH - kernelSysTimerReadValue(0));

      // We count the timer ticks that were used
      timerTicks += timeUsed;

      // Have we had the equivalent of a full timer revolution?  If so, we 
      // need to call the standard timer interrupt handler
      if (timerTicks >= 65535)
	{
	  // Reset to zero
	  timerTicks = 0;
	  
	  // Artifically register a system timer tick.
	  kernelSysTimerTick();
	}

      // The scheduler is the current process.
      kernelCurrentProcess = schedulerProc;      

      // Remember the previous process we ran
      previousProcess = nextProcess;

      if (previousProcess)
	{
	  if (previousProcess->state == proc_running)
	    // Change the state of the previous process to ready, since it
	    // was interrupted while still on the CPU.
	    previousProcess->state = proc_ready;

	    // added by Davide Airaghi for FPU-state handling
	    FPU_STATUS_SAVE(previousProcess->fpu)

	  // Add the last timeslice to the process' CPU time
	  previousProcess->cpuTime += timeUsed;
	}

      // Increment the counts of scheduler time slices and scheduler time
      // time
      schedulerTimeslices += 1;
      schedulerTime += timeUsed;

      // Get the system timer time
      theTime = kernelSysTimerRead();

      // Reset the selected process to NULL so we can evaluate
      // potential candidates
      nextProcess = NULL;
      topProcessWeight = 0;

      // We have to loop through the process queue, and determine which
      // process to run.

      for (count = 0; count < numQueued; count ++)
	{
	  // Every CPU_PERCENT_TIMESLICES timeslices we will update the %CPU 
	  // value for each process currently in the queue
	  if ((schedulerTimeslices % CPU_PERCENT_TIMESLICES) == 0)
	    {
	      // Calculate the CPU percentage
	      if (schedulerTime == 0)
		processQueue[count]->cpuPercent = 0;
	      else
		processQueue[count]->cpuPercent = 
		  ((processQueue[count]->cpuTime * 100) / schedulerTime);

	      // Reset the process' cpuTime counter
	      miscProcess->cpuTime = 0;
	    }

	  // Get a pointer to the process' main process
	  miscProcess = processQueue[count];

	  // This will change the state of a waiting process to 
	  // "ready" if the specified "waiting reason" has come to pass
	  if (miscProcess->state == proc_waiting)
	    {
	      // If the process is waiting for a specified time.  Has the
	      // requested time come?
	      if ((miscProcess->waitUntil != 0) &&
		  (miscProcess->waitUntil < theTime))
		// The process is ready to run
		miscProcess->state = proc_ready;

	      else
		// The process must continue waiting
		continue;
	    }

	  // This will dismantle any process that has identified itself
	  // as finished
	  else if (miscProcess->state == proc_finished)
	    {
	      kernelMultitaskerKillProcess(miscProcess->processId, 0);

	      // This removed it from the queue and placed another process
	      // in its place.  Decrement the current loop counter
	      count--;

	      continue;
	    }

	  else if (miscProcess->state != proc_ready)
	    // Otherwise, this process should not be considered for
	    // execution in this time slice (might be stopped, sleeping,
	    // or zombie)
	    continue;

	  // If the process is of the highest (real-time) priority, it
	  // should get an infinite weight
	  if (miscProcess->priority == 0)
	    processWeight = 0xFFFFFFFF;

	  // Else if the process is of the lowest priority, it should
	  // get a weight of zero
	  else if (miscProcess->priority == (PRIORITY_LEVELS - 1))
	    processWeight = 0;

	  // If this process has yielded this timeslice already, we
	  // should give it a low weight this time so that high-priority
	  // processes don't gobble time unnecessarily
	  else if (schedulerSwitchedByCall &&
		   (miscProcess->yieldSlice == theTime))
	    processWeight = 0;

	  // Otherwise, calculate the weight of this task, using the
	  // algorithm described above
	  else
	    processWeight = (((PRIORITY_LEVELS - miscProcess->priority) *
			     PRIORITY_RATIO) + miscProcess->waitTime);

	  if (processWeight < topProcessWeight)
	    {
	      // Increase the waiting time of this process, since it's not
	      // the one we're selecting
	      miscProcess->waitTime += 1;
	      continue;
	    }
	  else
	    {
	      if (nextProcess)
		{
		  // If the process' weight is tied with that of the
		  // previously winning process, it will NOT win if the
		  // other process has been waiting as long or longer
		  if ((processWeight == topProcessWeight) &&
		      (nextProcess->waitTime >= miscProcess->waitTime))
		    {
		      miscProcess->waitTime += 1;
		      continue;
		    }

		  else
		    {
		      // We have the currently winning process here.
		      // Remember it in case we don't find a better one,
		      // and increase the waiting time of the process this
		      // one is replacing
		      nextProcess->waitTime += 1;
		    }
		}
	      
	      topProcessWeight = processWeight;
	      nextProcess = miscProcess;
	    }
	}

      if ((schedulerTimeslices % CPU_PERCENT_TIMESLICES) == 0)
	// Reset the schedulerTime counter
	schedulerTime = 0;

      // We should now have selected a process to run.  If not, we should
      // re-start the loop.  This should only be likely to happen if some
      // goombah kills the idle task.  Starting the loop again like this
      // might simply result in a hung system -- but maybe that's OK because
      // there's simply nothing to do.  However, if there is some process
      // that is in a 'waiting' state, then there will be stuff to do when
      // it wakes up.
      if (nextProcess == NULL)
	{
	  // Resume the loop
	  schedulerSwitchedByCall = 1;
	  continue;
	}

      // Update some info about the next process
      nextProcess->waitTime = 0;
      nextProcess->state = proc_running;
      
      // Export (to the rest of the multitasker) the pointer to the
      // currently selected process.
      kernelCurrentProcess = nextProcess;

      // Make sure the exception handler process is ready to go
      if (exceptionProc)
	{
	  status = kernelDescriptorSet(
		   exceptionProc->tssSelector, // TSS selector
		   &(exceptionProc->taskStateSegment), // Starts at...
		   sizeof(kernelTSS),      // Maximum size of a TSS selector
		   1,                      // Present in memory
		   0,                      // Highest privilege level
		   0,                      // TSS's are system segs
		   0x9,                    // TSS, 32-bit, non-busy
		   0,                      // 0 for SMALL size granularity
		   0);                     // Must be 0 in TSS
	  exceptionProc->taskStateSegment.EIP = (unsigned)
	    &kernelExceptionHandler;
	}

      // Set the system timer 0 to interrupt this task after a known
      // period of time (mode 0)
      status = kernelSysTimerSetupTimer(0, 0, TIME_SLICE_LENGTH);
      if (status < 0)
	{
	  kernelError(kernel_warn, "The scheduler was unable to control "
		      "the system timer");
	  // Shut down the scheduler.
	  schedulerShutdown();
	  // Just in case
	  kernelProcessorStop();
	}

      // In the final part, we do the actual context switch.

      // added by Davide Airaghi for FPU-state handling
      FPU_STATUS_RESTORE(nextProcess->fpu)

      // Move the selected task's selector into the link field
      schedulerProc->taskStateSegment.oldTSS = nextProcess->tssSelector;

      markTaskBusy(nextProcess->tssSelector, 1);
      nextProcess->taskStateSegment.EFLAGS &= ~0x4000;

      // int flags = 0, taskreg = 0;
      // kernelProcessorGetFlags(flags);
      // kernelProcessorGetTaskReg(taskreg);
      // kernelTextPrintLine("Flags: 0x%x Task reg: 0x%x\n"
      // 			  "Flags: 0x%x EIP: 0x%x Task "
      // 			  "reg: 0x%x", flags, taskreg,
      // 			  nextProcess->taskStateSegment.EFLAGS,
      // 			  nextProcess->taskStateSegment.EIP,
      // 			  //nextProcess->taskStateSegment.ESP,
      // 			  nextProcess->tssSelector);

      // Acknowledge the timer interrupt if one occurred
      if (!schedulerSwitchedByCall)
	kernelPicEndOfInterrupt(INTERRUPT_NUM_SYSTIMER);

      // Reset the "switched by call" flag
      schedulerSwitchedByCall = 0;

      // Return to the task.  Do an interrupt return.
      kernelProcessorIntReturn();

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

  // The scheduler needs to make a task (but not a fully-fledged
  // process) for itself.  All other task switches will follow
  // as nested tasks from the scheduler.
  
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
			    kernelProc->privilege, &schedImage, 0);
  if (status < 0)
    return (status);

  schedulerProc = getProcessById(status);
  removeProcessFromQueue(schedulerProc);

  // Set the instruction pointer to the scheduler task
  schedulerProc->taskStateSegment.EIP = (unsigned) scheduler;

  // Interrupts should always be disabled for this task, and we manually set
  // the NT (nested task) flag as well, since Virtual PC doesn't do it when
  // we switch the first time.
  schedulerProc->taskStateSegment.EFLAGS = 0x00004002;

  // Get a page directory
  schedulerProc->taskStateSegment.CR3 =
    (unsigned) kernelPageGetDirectory(KERNELPROCID);

  // Not busy
  markTaskBusy(schedulerProc->tssSelector, 0);

  // The scheduler task should now be set up to run.  We should set 
  // up the kernel task to resume operation
  
  // Before we load the kernel's selector into the task reg, mark it as
  // not busy, since one cannot load the task register, with a busy TSS
  // selector...
  markTaskBusy(kernelProc->tssSelector, 0);

  // Make the kernel's Task State Segment be the current one.  In
  // reality, it IS still the currently running code
  kernelProcessorLoadTaskReg(kernelProc->tssSelector);

  // Reset the schedulerTime and schedulerTimeslices
  schedulerTime = 0;
  schedulerTimeslices = 0;
  
  // Make sure the scheduler is set to "run"
  schedulerStop = 0;

  // Set the "switched by call" flag
  schedulerSwitchedByCall = 0;

  // Make note that the multitasker has been enabled.  We do it a little
  // early so we can finish some of our tasks of creating threads without
  // complaints
  multitaskingEnabled = 1;

  // Yield control to the scheduler
  kernelMultitaskerYield();

  // Disable interrupts, so we can insure that we don't immediately get
  // a timer interrupt.
  kernelProcessorSuspendInts(interrupts);

  // Hook the system timer interrupt.
  oldSysTimerHandler = kernelInterruptGetHandler(INTERRUPT_NUM_SYSTIMER);
  if (oldSysTimerHandler == NULL)
    return (status = ERR_NOTINITIALIZED);

  // Install a task gate for the interrupt, which will
  // be the scheduler's timer interrupt.  After this point, our
  // new scheduler task will run with every clock tick
  status = kernelDescriptorSetIDTTaskGate((0x20 + INTERRUPT_NUM_SYSTIMER), 
	 				  schedulerProc->tssSelector);
  if (status < 0)
    {
      kernelProcessorRestoreInts(interrupts);
      return (status);
    }

  // Reenable interrupts after we get control back from the scheduler
  kernelProcessorRestoreInts(interrupts);

  // Return success
  return (status = 0);
}


static int createKernelProcess(void)
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

  // The kernel process is its own parent, of course, and it is owned 
  // by "admin".  We create no page table, and there are no arguments.
  kernelProcId = 
    createNewProcess("kernel process", 1, PRIVILEGE_SUPERVISOR, &kernImage, 0);

  if (kernelProcId < 0)
    // Damn.  Not able to create the kernel process
    return (kernelProcId);

  // Get the pointer to the kernel's process
  kernelProc = getProcessById(kernelProcId);

  // Make sure it's not NULL
  if (kernelProc == NULL)
    // Can't access the kernel process
    return (status = ERR_NOSUCHPROCESS);

  // Interrupts are initially disabled for the kernel
  kernelProc->taskStateSegment.EFLAGS = 0x00000002;

  // Set the current process to initially be the kernel process
  kernelCurrentProcess = kernelProc;

  // Deallocate the stack that was allocated, since the kernel already
  // has one.
  kernelMemoryRelease(kernelProc->userStack);

  // Create the kernel process' environment
  status = kernelEnvironmentCreate(KERNELPROCID, (variableList *)
				   &(kernelProc->environment), NULL);
  if (status < 0)
    // Couldn't create an environment structure for this process
    return (status);

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
  // This will walk up a chain of dependent child threads, incrementing
  // the descendent count of each parent
  
  kernelProcess *parentProcess = NULL;

  if (theProcess->processId == KERNELPROCID)
    // The kernel is its own parent
    return;

  parentProcess = getProcessById(theProcess->parentProcessId);
  if (parentProcess == NULL)
    // No worries.  Probably not a problem
    return;

  parentProcess->descendentThreads++;

  // Do a recursion to walk up the chain
  incrementDescendents(parentProcess);

  // Done
  return;
}


static void decrementDescendents(kernelProcess *theProcess)
{
  // This will walk up a chain of dependent child threads, decrementing
  // the descendent count of each parent
  
  kernelProcess *parentProcess = NULL;

  if (theProcess->processId == KERNELPROCID)
    // The kernel is its own parent
    return;

  parentProcess = getProcessById(theProcess->parentProcessId);
  if (parentProcess == NULL)
    // No worries.  Probably not a problem
    return;

  parentProcess->descendentThreads--;

  // Do a recursion to walk up the chain
  decrementDescendents(parentProcess);

  // Done
  return;
}


static void kernelProcess2Process(kernelProcess *kernProcess,
				  process *userProcess)
{
  // Given a kernel-space process structure, create the corresponding
  // user-space version.
  
  strncpy(userProcess->processName, (char *) kernProcess->processName,
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


/////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////
//
//  Below here, the functions are exported for external use
//
/////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////


int kernelMultitaskerInitialize(void)
{
  // This function intializes the kernel's multitasker.

  int status = 0;
  int count;
  
  // Make sure multitasking is NOT enabled already
  if (multitaskingEnabled)
    return (status = ERR_ALREADY);

  // Now we must initialize the process queue
  for (count = 0; count < MAX_PROCESSES; count ++)
    processQueue[count] = NULL;
  numQueued = 0;
 
  // We need to create the kernel's own process.  
  status = createKernelProcess();
  // Make sure it was successful
  if (status < 0)
    return (status);

  // Now start the scheduler
  status = schedulerInitialize();
  if (status < 0)
    // The scheduler couldn't start
    return (status);

  // Create an "idle" thread to consume all unused cycles
  status = spawnIdleThread();
  // Make sure it was successful
  if (status < 0)
    return (status);

  // Start the exception handler thread.
  status = exceptionThreadInitialize();
  // Make sure it was successful
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
    // We can't yield if we're not multitasking yet
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
  kernelMemoryReleaseSystem(schedulerProc->userStack);

  // Print a message
  kernelLog("Multitasking stopped");
  
  return (status = 0);
}


void kernelExceptionHandler(void)
{
  // This code sleeps until woken up by an exception.  Before multitasking
  // starts it is referenced by an interrupt gate.  Afterward, it has its
  // TSS referenced by a task gate descriptor in the IDT.

  char tmpMsg[256];
  void *stackMemory = NULL;
  int count;
  extern kernelSymbol *kernelSymbols;
  extern int kernelNumberSymbols;

  while(1)
    {
      // We got an exception.

      deadProcess = kernelCurrentProcess;
      deadProcess->state = proc_stopped;

      kernelCurrentProcess = exceptionProc;

      // Don't get into a loop.
      if (exceptionProc->state != proc_sleeping)
	kernelPanic("Double-fault while processing exception");

      exceptionProc->state = proc_running;

      // If the fault occurred while we were processing an interrupt,
      // we should tell the PIC that the interrupt service routine is
      // finished.  It's not really fair to kill a process because an
      // interrupt handler is screwy, but that's what we have to do for
      // the time being.
      if (kernelProcessingInterrupt)
	kernelPicEndOfInterrupt(0xFF);

      if (!multitaskingEnabled || (deadProcess == kernelProc))
	strcpy(tmpMsg, "The kernel has experienced a fatal exception");
      // Get the dead process
      else if (deadProcess == NULL)
	// We have to make an error here.  We can't return to the program
	// that caused the exception, and we can't tell the multitasker
	// to kill it.  We'd better make a kernel panic.
	kernelPanic("Exception handler unable to determine current process");
      else
	sprintf(tmpMsg, "Process \"%s\" caused a fatal exception",
		deadProcess->processName);

      if (multitaskingEnabled)
	{
	  if (deadProcess->taskStateSegment.EIP >= KERNEL_VIRTUAL_ADDRESS)
	    {
	      char *symbolName = NULL;

	      if (kernelSymbols)
		{
		  // Find roughly the kernel function where the exception
		  // happened
		  for (count = 0; count < kernelNumberSymbols; count ++)
		    {
		      if ((deadProcess->taskStateSegment.EIP >=
			   kernelSymbols[count].address) &&
			  (deadProcess->taskStateSegment.EIP <
			   kernelSymbols[count + 1].address))
			{
			  symbolName = kernelSymbols[count].symbol;
			  break;
			}
		    }
		}

	      if (symbolName)
		sprintf(tmpMsg, "%s in function %s", tmpMsg, symbolName);
	      else
		sprintf(tmpMsg, "%s at kernel address %08x", tmpMsg,
			deadProcess->taskStateSegment.EIP);
	    }
	  else
	    sprintf(tmpMsg, "%s at application address %08x", tmpMsg,
		    deadProcess->taskStateSegment.EIP);
	}

      if (kernelProcessingInterrupt)
	sprintf(tmpMsg, "%s while processing interrupt %d", tmpMsg,
		kernelPicGetActive());

      if (!multitaskingEnabled || (deadProcess == kernelProc))
	// If it's the kernel, we're finished
	kernelPanic(tmpMsg);
      else
	{
	  kernelError(kernel_error, tmpMsg);
	  if (kernelGraphicsAreEnabled())
	    kernelErrorDialog("Application Exception", tmpMsg);
	}

      // If the process was in kernel code, and we are not processing an
      // interrupt, take ownership of the process' stack memory and do a
      // stack trace
      if (!kernelProcessingInterrupt &&
	  (deadProcess->taskStateSegment.EIP >= KERNEL_VIRTUAL_ADDRESS))
	{
	  kernelMemoryChangeOwner(deadProcess->processId,
				  exceptionProc->processId, 1,
				  deadProcess->userStack, &stackMemory);

	  // If possible, we will do a stack memory dump to disk.  Don't try
	  // this if we were servicing an interrupt when we faulted
	  if (stackMemory)
	    {
	      kernelStackTrace((stackMemory +
				((void *) deadProcess->taskStateSegment.ESP -
				 deadProcess->userStack)),
			       (stackMemory + deadProcess->userStackSize -
				sizeof(void *)));

	      // Release the stack memory
	      kernelMemoryRelease(stackMemory);
	    }
	}

      // The scheduler may now dismantle the process
      deadProcess->state = proc_finished;

      kernelProcessingInterrupt = 0;

      // Make sure that when we return, we return to the scheduler
      exceptionProc->taskStateSegment.oldTSS = schedulerProc->tssSelector;

      // Mark the process as finished and yield the timeslice back to the
      // scheduler.  The scheduler will take care of dismantling the process
      exceptionProc->state = proc_sleeping;
      kernelMultitaskerYield();
    }
}


void kernelMultitaskerDumpProcessList(void)
{
  // This routine is used to dump an internal listing of the current
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
	  
	  sprintf(buffer, "\"%s\"  PID=%d UID=%d priority=%d "
		  "priv=%d parent=%d\n        %d%% CPU State=",
		  (char *) tmpProcess->processName,
		  tmpProcess->processId, tmpProcess->userId,
		  tmpProcess->priority, tmpProcess->privilege,
		  tmpProcess->parentProcessId, tmpProcess->cpuPercent);
	  // Get the state
	  switch(tmpProcess->state)
	    {
	    case proc_running:
	      strcat(buffer, "running");
	      break;
	    case proc_ready:
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
    // This doesn't seem at all likely.
    kernelTextStreamPrintLine(currentOutput, "No processes remaining");

  kernelTextStreamNewline(currentOutput);
  return;
}


int kernelMultitaskerCreateProcess(const char *name, int privilege,
				   processImage *execImage)
{
  // This function is called to set up an (initially) single-threaded
  // process in the multitasker.  This is the routine used by external
  // sources -- the loader for example -- to define new processes.  This 
  // new process thread we're creating will have its state set to "stopped" 
  // after this call.  The caller should use the 
  // kernelMultitaskerChangeThreadState routine to start the new thread.  
  // This function returns the processId of the new process on success, 
  // negative otherwise.

  int status = 0;
  int processId = 0;
  kernelProcess *newProcess = NULL;

  // Make sure multitasking has been enabled
  if (!multitaskingEnabled)
    return (status = ERR_NOTINITIALIZED);

  // Make sure the parameters are valid
  if ((name == NULL) || (execImage == NULL))
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
			       1); // create page directory

  // Get the pointer to the new process from its process Id
  newProcess = getProcessById(processId);
  if (newProcess == NULL)
    // We couldn't get access to the new process
    return (status = ERR_NOCREATE);

  // Create the process' environment
  status =
    kernelEnvironmentCreate(newProcess->processId, (variableList *)
			    &(newProcess->environment), (variableList *)
			    &(kernelCurrentProcess->environment));
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
  // This function is used to spawn a new thread from the current
  // process.  The function needs to be told the starting address of
  // the code to execute, and an optional argument to pass to the 
  // spawned function.  It returns the new process Id on success,
  // negative otherwise.

  int status = 0;
  int processId = 0;
  kernelProcess *newProcess = NULL;
  processImage execImage;
  int count;

  // Make sure multitasking has been enabled
  if (!multitaskingEnabled)
    return (status = ERR_NOTINITIALIZED);

  // The start address CAN be NULL, if it is the zero offset in a
  // process' private address space.

  // Make sure the pointer to the name is not NULL
  if (name == NULL)
    // We cannot continue here
    return (status = ERR_NULLPARAMETER);

  // If the number of arguments is not zero, make sure the arguments
  // pointer is not NULL
  if (argc && !argv)
    return (status = ERR_NULLPARAMETER);

  // Make sure the current process isn't NULL
  if (kernelCurrentProcess == NULL)
    return (status = ERR_NOSUCHPROCESS);

  kernelMemClear(&execImage, sizeof(processImage));
  execImage.virtualAddress = startAddress;
  execImage.entryPoint = startAddress;
  execImage.code = NULL;
  execImage.codeSize = 0;
  execImage.data = NULL;
  execImage.dataSize = 0;
  execImage.imageSize = 0;

  // Set up arguments
  execImage.argc = (argc + 1);
  execImage.argv[0] = (char *) name;
  for (count = 0; count < argc; count ++)
    execImage.argv[count + 1] = argv[count];

  // OK, now we should create the new process
  processId = createNewProcess(name, kernelCurrentProcess->priority,
			       kernelCurrentProcess->privilege,
			       &execImage, 0);
  if (processId < 0)
    return (status = processId);
  
  // Get the pointer to the new process from its process Id
  newProcess = getProcessById(processId);

  // Make sure it's valid
  if (newProcess == NULL)
    // We couldn't get access to the new process
    return (status = ERR_NOCREATE);

  // Change the type to thread
  newProcess->type = proc_thread;
  
  // Increment the descendent counts 
  incrementDescendents(newProcess);

  // Since we assume that the thread is invoked as a function call, 
  // subtract 4 additional bytes from the stack pointer to account for
  // the space where the return address would normally go.
  newProcess->taskStateSegment.ESP -= 4;

  // Copy the environment
  newProcess->environment = kernelCurrentProcess->environment;

  // The new process should share (but not own) the same text streams as the
  // parent
  newProcess->textInputStream = kernelCurrentProcess->textInputStream;
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
  kernelProcessorSuspendInts(interrupts);

  // Change the current process to the kernel process
  kernelCurrentProcess = kernelProc;

  // Spawn

  status = kernelMultitaskerSpawn(startAddress, name, argc, argv);

  // Reset the current process
  kernelCurrentProcess = myProcess;

  // Reenable interrupts
  kernelProcessorRestoreInts(interrupts);

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
  if (userProcess == NULL)
    return (status = ERR_NULLPARAMETER);

  // Try to match the requested process Id number with a real
  // live process structure
  kernProcess = getProcessById(processId);
  if (kernProcess == NULL)
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
  if ((processName == NULL) || (userProcess == NULL))
    return (status = ERR_NULLPARAMETER);

  // Try to match the requested process Id number with a real
  // live process structure
  kernProcess = getProcessByName(processName);
  if (kernProcess == NULL)
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
  if (buffer == NULL)
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
  // This is a very simple routine that can be called by external 
  // programs to get the PID of the current running process.  Of course,
  // internal functions can perform this action very easily themselves.

  int status = 0;

  // Make sure multitasking has been enabled
  if (!multitaskingEnabled)
    // If we're not multitasking,return the kernel's process Id
    return (status = KERNELPROCID);
  
  // Double-check the current process to make sure it's not NULL
  if (kernelCurrentProcess == NULL)
    return (status = ERR_NOSUCHPROCESS);

  // OK, we can return process Id of the currently running process
  return (status = kernelCurrentProcess->processId);
}


int kernelMultitaskerGetProcessState(int processId, processState *state)
{
  // This is a very simple routine that can be called by external 
  // programs to request the state of a "running" process.  Of course,
  // internal functions can perform this action very easily themselves.

  int status = 0;
  kernelProcess *theProcess;

  // Make sure multitasking has been enabled
  if (!multitaskingEnabled)
    return (status = ERR_NOTINITIALIZED);
  
  // We need to find the process structure based on the process Id
  theProcess = getProcessById(processId);

  if (theProcess == NULL)
    // The process does not exist
    return (status = ERR_NOSUCHPROCESS);

  // Make sure the kernelProcessState pointer we were given is legal
  if (state == NULL)
    // Oopsie
    return (status = ERR_NULLPARAMETER);

  // Set the state value of the process
  *state = theProcess->state;

  return (status = 0);
}


int kernelMultitaskerSetProcessState(int processId, processState newState)
{
  // This is a very simple routine that can be called by external 
  // programs to change the state of a "running" process.  Of course,
  // internal functions can perform this action very easily themselves.

  int status = 0;
  kernelProcess *changeProcess;

  // Make sure multitasking has been enabled
  if (!multitaskingEnabled)
    return (status = ERR_NOTINITIALIZED);
  
  // We need to find the process structure based on the process Id
  changeProcess = getProcessById(processId);

  if (changeProcess == NULL)
    // The process does not exist
    return (status = ERR_NOSUCHPROCESS);

  // Permission check.  A privileged process can change the state of any 
  // other process, but a non-privileged process can only change the state 
  // of processes owned by the same user
  if (kernelCurrentProcess->privilege != PRIVILEGE_SUPERVISOR)
    if (kernelCurrentProcess->userId != changeProcess->userId)
      return (status = ERR_PERMISSION);

  // Make sure the new state is a legal one
  if ((newState != proc_running) && (newState != proc_ready) && 
      (newState != proc_waiting) && (newState != proc_sleeping) &&
      (newState != proc_stopped) && (newState != proc_finished) &&
      (newState != proc_zombie))
    // Not a legal state value
    return (status = ERR_INVALID);

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
    return (0);

  // Try to match the requested process Id number with a real
  // live process structure
  targetProcess = getProcessById(processId);

  if (targetProcess && (targetProcess->state != proc_finished) &&
      (targetProcess->state != proc_zombie))
    return (1);
  else
    return (0);
}


int kernelMultitaskerGetProcessPriority(int processId)
{
  // This is a very simple routine that can be called by external 
  // programs to get the priority of a process.  Of course, internal 
  // functions can perform this action very easily themselves.

  int status = 0;
  kernelProcess *getProcess;

  // Make sure multitasking has been enabled
  if (!multitaskingEnabled)
    return (status = ERR_NOTINITIALIZED);
  
  // We need to find the process structure based on the process Id
  getProcess = getProcessById(processId);

  if (getProcess == NULL)
    // The process does not exist
    return (status = ERR_NOSUCHPROCESS);

  // No permission check necessary here

  // Return the privilege value of the process
  return (getProcess->priority);
}


int kernelMultitaskerSetProcessPriority(int processId, int newPriority)
{
  // This is a very simple routine that can be called by external 
  // programs to change the priority of a process.  Of course, internal
  // functions can perform this action very easily themselves.

  int status = 0;
  kernelProcess *changeProcess;

  // Make sure multitasking has been enabled
  if (!multitaskingEnabled)
    return (status = ERR_NOTINITIALIZED);
  
  // We need to find the process structure based on the process Id
  changeProcess = getProcessById(processId);

  if (changeProcess == NULL)
    // The process does not exist
    return (status = ERR_NOSUCHPROCESS);

  // Permission check.  A privileged process can set the priority of any 
  // other process, but a non-privileged process can only change the 
  // priority of processes owned by the same user.  Additionally, a 
  // non-privileged process can only set the new priority to a value equal 
  // to or lower than its own priority.
  if (kernelCurrentProcess->privilege != PRIVILEGE_SUPERVISOR)
    if ((kernelCurrentProcess->userId != changeProcess->userId) ||
	(newPriority < kernelCurrentProcess->priority))
      return (status = ERR_PERMISSION);

  // Make sure the new priority is a legal one
  if ((newPriority < 0) || (newPriority >= (PRIORITY_LEVELS)))
    // Not a legal priority value
    return (status = ERR_INVALID);

  // Set the priority value of the process
  changeProcess->priority = newPriority;

  return (status);
}


int kernelMultitaskerGetProcessPrivilege(int processId)
{
  // This is a very simple routine that can be called by external 
  // programs to request the privilege of a "running" process.  Of course,
  // internal functions can perform this action very easily themselves.

  int status = 0;
  kernelProcess *theProcess;

  // Make sure multitasking has been enabled
  if (!multitaskingEnabled)
    return (status = ERR_NOTINITIALIZED);
  
  // We need to find the process structure based on the process Id
  theProcess = getProcessById(processId);

  if (theProcess == NULL)
    // The process does not exist
    return (status = ERR_NOSUCHPROCESS);

  // Return the nominal privilege value of the process
  return (theProcess->privilege);
}


int kernelMultitaskerGetCurrentDirectory(char *buffer, int buffSize)
{
  // This function will fill the supplied buffer with the name of the
  // current working directory for the current process.  Returns 0 on 
  // success, negative otherwise.

  int status = 0;
  int lengthToCopy = 0;

  // Make sure multitasking has been enabled
  if (!multitaskingEnabled)
    return (status = ERR_NOTINITIALIZED);
  
  // Make sure the buffer we've been passed is not NULL
  if (buffer == NULL)
    return (status = ERR_NULLPARAMETER);

  // Now, the number of characters we will copy is the lesser of 
  // buffSize or MAX_PATH_LENGTH
  if (buffSize < MAX_PATH_LENGTH)
    lengthToCopy = buffSize;
  else
    lengthToCopy = MAX_PATH_LENGTH;

  // Otay, copy the name of the current directory into the caller's buffer
  strncpy(buffer, (char *) kernelCurrentProcess->currentDirectory,
	  lengthToCopy);
  buffer[lengthToCopy - 1] = '\0';

  // Return success
  return (status = 0);
}


int kernelMultitaskerSetCurrentDirectory(const char *newDirectoryName)
{
  // This function will change the current directory of the current
  // process.  Returns 0 on success, negative otherwise.

  int status = 0;
  file newDirectory;

  // Make sure multitasking has been enabled
  if (!multitaskingEnabled)
    return (status = ERR_NOTINITIALIZED);
  
  // Make sure the buffer we've been passed is not NULL
  if (newDirectoryName == NULL)
    return (status = ERR_NULLPARAMETER);

  // Initialize our file
  kernelMemClear((void *) &newDirectory, sizeof(file));

  // Call the appropriate filesystem function to find this supposed new 
  // directory.
  kernelFileFind(newDirectoryName, &newDirectory);

  // Make sure the target file is actually a directory
  if (newDirectory.type != dirT)
    return (status = ERR_NOSUCHDIR);

  // Okay, copy the name of the current directory into the process
  strncpy((char *) kernelCurrentProcess->currentDirectory, newDirectoryName,
	  MAX_PATH_LENGTH);
  kernelCurrentProcess->currentDirectory[MAX_PATH_LENGTH - 1] = '\0';

  // Return success
  return (status = 0);
}


kernelTextInputStream *kernelMultitaskerGetTextInput(void)
{
  // This function will return the text input stream that is attached
  // to the current process

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
  if (theProcess == NULL)
    return (status = ERR_NOSUCHPROCESS);

  theProcess->textInputStream = theStream;

  if (theStream && (theProcess->type == proc_normal))
    theStream->ownerPid = theProcess->processId;

  // Do any child threads recursively as well.
  if (theProcess->descendentThreads)
    for (count = 0; count < numQueued; count ++)
      if ((processQueue[count]->parentProcessId == processId) &&
	  (processQueue[count]->type == proc_thread))
	{
	  status =
	    kernelMultitaskerSetTextInput(processQueue[count]->processId,
					  theStream);
	  if (status < 0)
	    return (status);
	}

  return (status = 0);
}


kernelTextOutputStream *kernelMultitaskerGetTextOutput(void)
{
  // This function will return the text output stream that is attached
  // to the current process

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
  if (theProcess == NULL)
    return (status = ERR_NOSUCHPROCESS);

  theProcess->textOutputStream = theStream;

  // Do any child threads recursively as well.
  if (theProcess->descendentThreads)
    for (count = 0; count < numQueued; count ++)
      if ((processQueue[count]->parentProcessId == processId) &&
	  (processQueue[count]->type == proc_thread))
	{
	  status =
	    kernelMultitaskerSetTextOutput(processQueue[count]->processId,
					   theStream);
	  if (status < 0)
	    return (status);
	}

  return (status = 0);
}


int kernelMultitaskerDuplicateIO(int firstPid, int secondPid, int clear)
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

  if ((firstProcess == NULL) || (secondProcess == NULL))
    return (status = ERR_NOSUCHPROCESS);

  input = firstProcess->textInputStream;
  output = firstProcess->textOutputStream;

  if (input)
    {
      secondProcess->textInputStream = input;
      input->ownerPid = secondPid;
      
      if (clear)
	kernelTextInputStreamRemoveAll(input);
    }

  if (output)
    secondProcess->textOutputStream = output;

  return (status = 0);
}


int kernelMultitaskerGetProcessorTime(clock_t *clk)
{
  // Returns processor time used by a process since its start.  This
  // value is the number of timer ticks from the system timer.
  
  int status = 0;

  // Make sure multitasking has been enabled
  if (!multitaskingEnabled)
    // We can't yield if we're not multitasking yet
    return (status = ERR_NOTINITIALIZED);

  if (clk == NULL)
    return (status = ERR_NULLPARAMETER);

  // Return the processor time of the current process
  *clk = kernelCurrentProcess->cpuTime;

  return (status = 0);
}


void kernelMultitaskerYield(void)
{
  // This routine will yield control from the current running thread
  // back to the scheduler.

  // Make sure multitasking has been enabled
  if (!multitaskingEnabled)
    // We can't yield if we're not multitasking yet
    return;

  // Don't do this inside an interrupt
  if (kernelProcessingInterrupt)
    {
      kernelError(kernel_warn, "Cannot yield() inside interrupt handler");
      return;
    }

  // We accomplish a yield by doing a far call to the scheduler's task.
  // The scheduler sees this almost as if the current timeslice had expired.
  kernelCurrentProcess->yieldSlice = kernelSysTimerRead();
  schedulerSwitchedByCall = 1;
  kernelProcessorFarCall(schedulerProc->tssSelector);

  return;
}


void kernelMultitaskerWait(unsigned timerTicks)
{
  // This function will put a process into the waiting state for *at least*
  // the specified number of timer ticks, and yield control back to the
  // scheduler

  // Make sure multitasking has been enabled
  if (!multitaskingEnabled)
    // We can't wait if we're not multitasking yet
    return;
  
  // Don't do this inside an interrupt
  if (kernelProcessingInterrupt)
    kernelPanic("Cannot wait() inside interrupt handler");

  // Make sure the current process isn't NULL
  if (kernelCurrentProcess == NULL)
    // Can't return an error code, but we can't perform the specified
    // action either
    return;

  // Set the current process to "waiting"
  kernelCurrentProcess->state = proc_waiting;

  // Set the wait until time
  kernelCurrentProcess->waitUntil = (kernelSysTimerRead() + timerTicks);
  kernelCurrentProcess->waitForProcess = 0;

  // We accomplish a yield by doing a far call to the scheduler's task.
  // The scheduler sees this almost as if the current timeslice had expired.
  kernelCurrentProcess->yieldSlice = kernelSysTimerRead();
  schedulerSwitchedByCall = 1;
  kernelProcessorFarCall(schedulerProc->tssSelector);

  return;
}


int kernelMultitaskerBlock(int processId)
{
  // This function will put a process into the waiting state
  // until the requested blocking process has completed

  int status = 0;
  kernelProcess *blockProcess = NULL;

  // Make sure multitasking has been enabled
  if (!multitaskingEnabled)
    // We can't yield if we're not multitasking yet
    return (status = ERR_NOTINITIALIZED);

  // Don't do this inside an interrupt
  if (kernelProcessingInterrupt)
    kernelPanic("Cannot block() inside interrupt handler");

  // Get the process that we're supposed to block on
  blockProcess = getProcessById(processId);
  if (blockProcess == NULL)
    {
      // The process does not exist.
      kernelError(kernel_error, "The process on which to block does not "
		  "exist");
      return (status = ERR_NOSUCHPROCESS);
    }

  // Make sure the current process isn't NULL
  if (kernelCurrentProcess == NULL)
    {
      kernelError(kernel_error, "Can't determine the current process");
      return (status = ERR_BUG);
    }

  // Take the text streams that belong to the current process and
  // give them to the target process
  kernelMultitaskerDuplicateIO(kernelCurrentProcess->processId,
			       processId, 0); // don't clear

  // Set the wait for process values
  kernelCurrentProcess->waitForProcess = processId;
  kernelCurrentProcess->waitUntil = NULL;

  // Set the current process to "waiting"
  kernelCurrentProcess->state = proc_waiting;

  // We accomplish a yield by doing a far call to the scheduler's task.
  // The scheduler sees this almost as if the current timeslice had expired.
  kernelCurrentProcess->yieldSlice = kernelSysTimerRead();
  schedulerSwitchedByCall = 1;
  kernelProcessorFarCall(schedulerProc->tssSelector);

  // Get the exit code from the process
  return (kernelCurrentProcess->blockingExitCode);
}


int kernelMultitaskerDetach(void)
{
  int status = 0;
  kernelProcess *parentProcess = NULL;

  // This will allow a program or daemon to detach from its parent process
  // if the parent process is blocking.
  // Make sure multitasking has been enabled
  if (!multitaskingEnabled)
    // We can't yield if we're not multitasking yet
    return (status = ERR_NOTINITIALIZED);

  // Make sure the current process isn't NULL
  if (kernelCurrentProcess == NULL)
    {
      kernelError(kernel_error, "Can't determine the current process");
      return (status = ERR_BUG);
    }

  // Set the input/output streams to the console
  kernelMultitaskerDuplicateIO(KERNELPROCID, kernelCurrentProcess->processId,
			       0); // don't clear  

  // Get the process that's blocking on this one, if any
  parentProcess = getProcessById(kernelCurrentProcess->parentProcessId);
  if (parentProcess &&
      (parentProcess->waitForProcess == kernelCurrentProcess->processId))
    {
      // Clear the return code of the parent process
      parentProcess->blockingExitCode = 0;
      
      // Clear the parent's wait for process value
      parentProcess->waitForProcess = 0;

      // Make it runnable
      parentProcess->state = proc_ready;
    }

  // We accomplish a yield by doing a far call to the scheduler's task.
  // Get the exit code from the process
  return (status = 0);
}


int kernelMultitaskerKillProcess(int processId, int force)
{
  // This function should be used to properly kill a process.  This
  // will deallocate all of the internal resources used by the 
  // multitasker in maintaining the process and all of its processes

  // This function should be used to properly kill a process.  This
  // will deallocate all of the internal resources used by the 
  // multitasker in maintaining the process and all of its children.
  // This function will commonly employ a recursive tactic for
  // killing processes with spawned children.  Returns 0 on success,
  // negative on error.

  int status = 0;
  kernelProcess *killProcess = NULL;
  int count;

  // Make sure multitasking has been enabled
  if (!multitaskingEnabled)
    return (status = ERR_NOTINITIALIZED);

  // OK, we need to find the process structure based on the Id we were passed
  killProcess = getProcessById(processId);
  if (killProcess == NULL)
    // There's no such process
    return (status = ERR_NOSUCHPROCESS);

  // Processes are not allowed to actually kill themselves.  They must use
  // the terminate function to do it normally.
  if (killProcess == kernelCurrentProcess)
    kernelMultitaskerTerminate(0);

  // Permission check.  A privileged process can kill any other process,
  // but a non-privileged process can only kill processes owned by the
  // same user
  if (kernelCurrentProcess->privilege != PRIVILEGE_SUPERVISOR)
    if (kernelCurrentProcess->userId != killProcess->userId)
      return (status = ERR_PERMISSION);

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
  // Instead we will mark it as 'finished' and let the kernel clean us
  // all up later
  if ((kernelCurrentProcess->type == proc_thread) &&
      (processId == kernelCurrentProcess->parentProcessId))
    {
      killProcess->state = proc_finished;
      while (1)
	kernelMultitaskerYield();
    }

  // The request is legitimate.

  // Mark the process as stopped in the process queue, so that the
  // scheduler will not inadvertently select it to run while we're
  // destroying it.
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
	  if (killProcess->waitForProcess != 0)
	    processQueue[count]->waitForProcess = killProcess->waitForProcess;
	  else
	    {
	      processQueue[count]->blockingExitCode = ERR_KILLED;
	      processQueue[count]->waitForProcess = 0;
	      processQueue[count]->state = proc_ready;
	    }
	  continue;
	}

      // If this process is a child thread of the process we're killing,
      // or if the process we're killing was blocking on this process,
      // kill it first.
      if ((processQueue[count]->state != proc_finished) &&
	  (processQueue[count]->parentProcessId == killProcess->processId) &&
	  ((processQueue[count]->type == proc_thread) ||
	   (killProcess->waitForProcess == processQueue[count]->processId)))

	{
	  status = kernelMultitaskerKillProcess(processQueue[count]->processId,
						force);
	  if (status < 0)
	    kernelError(kernel_warn, "Unable to kill child process \"%s\" of "
			"parent process \"%s\"",
			processQueue[count]->processName,
			killProcess->processName);
	  // Restart the loop
	  count = 0;
	  continue;
	}
    }

  // Now we look after killing the process with the Id we were passed

  // If this process is a thread, decrement the count of descendent
  // threads of its parent
  if (killProcess->type == proc_thread)
    decrementDescendents(killProcess);

  // Dismantle the process
  status = deleteProcess(killProcess);
  if (status < 0)
    {
      // Eek, there was a problem deallocating something, we guess.
      // Simply mark the process as a zombie so that it won't be run
      // any more, but it's resources won't be 'lost'
      kernelError(kernel_error, "Couldn't delete process %d: \"%s\"",
		  killProcess->processId, killProcess->processName);
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
  // Normally, this will be done during multitasker shutdown.  Returns
  // 0 on success, negative otherwise.

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
    if (PROC_KILLABLE(processQueue[count]))
      processQueue[count]->state = proc_stopped;

  for (count = 0; count < numQueued; )
    {
      if (!PROC_KILLABLE(processQueue[count]))
	{
	  count++;
	  continue;
	}

      killProcess = processQueue[count];

      // Attempt to kill it.
      status =
	kernelMultitaskerKillProcess(killProcess->processId, 0); // no force
      if (status < 0)
	{
	  // Try it with a force
	  status =
	    kernelMultitaskerKillProcess(killProcess->processId, 1); // force
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
  if (kernelProcessingInterrupt)
    kernelPanic("Cannot terminate() inside interrupt handler");

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
      // If we still have threads out there, we don't dismantle until they are
      // finished
      if (!kernelCurrentProcess->descendentThreads)
	// Terminate
	kernelCurrentProcess->state = proc_finished;

      kernelMultitaskerYield();
    }

  return (status);
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
  if (signalProcess == NULL)
    {
      // There's no such process
      kernelError(kernel_error, "No process %d to signal", processId);
      return (status = ERR_NOSUCHPROCESS);
    }

  // If there is not yet a signal stream allocated for this process, do it now.
  if (!(signalProcess->signalStream.buffer))
    {
      status = kernelStreamNew((stream *) &(signalProcess->signalStream),
			       16, 1);
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
  if (signalProcess == NULL)
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
  status = signalProcess->signalStream
    .append((stream *) &(signalProcess->signalStream), sig);

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
  if (signalProcess == NULL)
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

  if (!(signalProcess->signalStream.count))
    return (sig = 0);

  status = signalProcess->signalStream
    .pop((stream *) &(signalProcess->signalStream), &sig);
  
  if (status < 0)
    return (status);
  else
    return (sig);
}
