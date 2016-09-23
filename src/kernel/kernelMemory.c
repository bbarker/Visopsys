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
//  kernelMemory.c
//

// These functions comprise Visopsys' memory management subsystem.  This
// memory manager is implemented using a "first-fit" strategy because
// it's a speedy algorithm, and because supposedly "best-fit" and
// "worst-fit" don't really provide a significant memory utilization
// advantage but do imply significant overhead.

#include "kernelMemory.h"
#include "kernelError.h"
#include "kernelInterrupt.h"
#include "kernelLock.h"
#include "kernelMain.h"
#include "kernelMalloc.h"
#include "kernelMultitasker.h"
#include "kernelPage.h"
#include "kernelParameters.h"
#include <stdio.h>
#include <string.h>

static volatile int initialized = 0;
static lock memoryLock;

static volatile unsigned totalMemory = 0;
static memoryBlock usedBlockMemory[MAXMEMORYBLOCKS];
static memoryBlock * volatile usedBlockList[MAXMEMORYBLOCKS];
static volatile int usedBlocks = 0;
static unsigned char * volatile freeBlockBitmap = NULL;
static volatile int totalBlocks = 0;
static volatile unsigned totalFree = 0;
static volatile unsigned totalUsed = 0;


// This structure can be used to "reserve" memory blocks so that they
// will be marked as "used" by the memory manager and then left alone.
// It should be terminated with a NULL entry.  The addresses used here
// are defined in kernelParameters.h
static memoryBlock reservedBlocks[] =
{
	// { processId, description, startlocation, endlocation }

	{ KERNELPROCID, "real mode ivt and bda", 0, (MEMORY_BLOCK_SIZE - 1) },

	{ KERNELPROCID, "memory hole and ebda", 0x00080000, 0x0009FFFF },

	{ KERNELPROCID, "video memory and rom", VIDEO_MEMORY, 0x000FFFFF },

	// Ending value is set during initialization, since it is variable
	{ KERNELPROCID, "kernel memory", KERNEL_LOAD_ADDRESS, 0 },

	// Starting and ending values are set during initialization, since they
	// are variable
	{ KERNELPROCID, "kernel paging data", 0, 0 },

	// The bitmap for free memory.  This one is also completely variable and
	// dependent upon the previous two
	{ KERNELPROCID, "free memory bitmap", 0, 0 },

	{ 0, "", 0, 0 }
};


static int allocateBlock(int processId, unsigned start, unsigned end,
	const char *description)
{
	// This function will allocate a block in the used block list, mark the
	// corresponding blocks as allocated in the free-block bitmap, and adjust
	// the totalUsed and totalFree values accordingly.

	int status = 0;
	unsigned lastBit = 0;
	unsigned count;

	// The description pointer is allowed to be NULL

	if ((start % MEMORY_BLOCK_SIZE) ||
		(((end - start) + 1) % MEMORY_BLOCK_SIZE))
	{
		kernelError(kernel_error, "Memory block start or size is not "
			"block-aligned");
		return (status = ERR_INVALID);
	}

	if ((start >= totalMemory) || (end >= totalMemory))
		return (status = ERR_INVALID);

	// Clear the memory occupied by the first unused memory block structure
	memset((void *) usedBlockList[usedBlocks], 0, sizeof(memoryBlock));

	// Assign the appropriate values to the block structure
	usedBlockList[usedBlocks]->processId = processId;
	usedBlockList[usedBlocks]->startLocation = start;
	usedBlockList[usedBlocks]->endLocation = end;

	if (description)
	{
		strncpy((char *) usedBlockList[usedBlocks]->description,
			description, MEMORY_MAX_DESC_LENGTH);
		usedBlockList[usedBlocks]
			->description[MEMORY_MAX_DESC_LENGTH - 1] = '\0';
	}
	else
		usedBlockList[usedBlocks]->description[0] = '\0';

	// Increment the count of used memory blocks.
	usedBlocks += 1;

	// Take the whole range of memory covered by this new block, and mark
	// each of its physical memory blocks as "used" in the free-block bitmap,
	// adjusting the totalUsed and totalFree values as we go.
	lastBit = ((end + (MEMORY_BLOCK_SIZE - 1)) / MEMORY_BLOCK_SIZE);
	for (count = (start / MEMORY_BLOCK_SIZE); count <= lastBit; count ++)
	{
		if (!(freeBlockBitmap[count / 8] & (0x80 >> (count % 8))))
		{
			freeBlockBitmap[count / 8] |= (0x80 >> (count % 8));
			totalUsed += MEMORY_BLOCK_SIZE;
			totalFree -= MEMORY_BLOCK_SIZE;
		}
	}

	// Return success
	return (status = 0);
}


static int requestBlock(int processId, unsigned size, unsigned alignment,
	const char *description, unsigned *memory)
{
	// This function takes a size and some other parameters, and allocates
	// a memory block.  The alignment parameter allows the caller to request
	// the physical alignment of the block (but only on a MEMORY_BLOCK_SIZE
	// boundary, otherwise an error will result).  If no particular alignment
	// is needed, specifying this parameter as 0 will effectively nullify
	// this.

	int status = 0;
	unsigned blockPointer = NULL;
	unsigned consecutiveBlocks = 0;
	int foundBlock = 0;
	int count;

	// If the requested block size is zero, forget it.  We can probably
	// assume something has gone wrong in the calling program
	if (!size)
	{
		kernelError(kernel_error, "Can't allocate 0 bytes");
		return (status = ERR_INVALID);
	}

	// Make sure that we have room for a new block.  If we don't, return
	// NULL.
	if (usedBlocks >= MAXMEMORYBLOCKS)
	{
		// Not enough memory blocks left over
		kernelError(kernel_error, "The number of memory blocks has been "
			"exhausted");
		return (status = ERR_MEMORY);
	}

	// Make sure the requested alignment is a multiple of MEMORY_BLOCK_SIZE.
	// Obviously, if MEMORY_BLOCK_SIZE is the size of each block, we can't
	// really start allocating memory which uses only bits and pieces
	// of blocks
	if (alignment && (alignment % MEMORY_BLOCK_SIZE))
	{
		kernelError(kernel_error, "Physical memory can only be aligned on "
			"%u-byte boundary (not %u)", MEMORY_BLOCK_SIZE, alignment);
		return (status = ERR_ALIGN);
	}

	// Make the requested size be a multiple of MEMORY_BLOCK_SIZE.  Our memory
	// blocks are all going to be multiples of this size
	if (size % MEMORY_BLOCK_SIZE)
		size = (((size / MEMORY_BLOCK_SIZE) + 1) * MEMORY_BLOCK_SIZE);

	// Now, make sure that there's enough total free memory to satisfy
	// this request (whether or not there is a large enough contiguous
	// block is another matter.)
	if (size > totalFree)
	{
		// There is not enough free memory
		kernelError(kernel_error, "The computer is out of physical memory");
		return (status = ERR_MEMORY);
	}

	// Adjust "alignment" so that it is expressed in blocks
	alignment /= MEMORY_BLOCK_SIZE;

	// Skip through the free-block bitmap and find the first block large
	// enough to fit the requested size, plus the alignment value if
	// applicable.
	for (count = 0; count < totalBlocks; count ++)
	{
		// Is the current block used or free?
		if (freeBlockBitmap[count / 8] & (0x80 >> (count % 8)))
		{
			// This block is allocated.  We're not there yet.
			consecutiveBlocks = 0;

			// If alignment is desired, we need to advance "count" to the
			// next multiple of the alignment size
			if (alignment)
				// Subract one from the expected number because count will get
				// incremented by the loop
				count += ((alignment - (count % alignment)) - 1);

			continue;
		}

		// This block is free.
		consecutiveBlocks += 1;

		// Do we have enough yet?
		if ((consecutiveBlocks * MEMORY_BLOCK_SIZE) >= size)
		{
			blockPointer = ((count - (consecutiveBlocks - 1)) *
				MEMORY_BLOCK_SIZE);
			foundBlock = 1;
			break;
		}
	}

	// Did we find an appropriate block?
	if (!foundBlock)
		return (status = ERR_MEMORY);

	// It looks like we will be able to satisfy this request.  We found a block
	// in the loop above.  We now have to allocate the new "used" block.

	// blockPointer should point to the start of the memory area.
	status = allocateBlock(processId, blockPointer, (blockPointer + size - 1),
		description);

	// Make sure we were successful with the allocation, above
	if (status < 0)
		return (status);

	// Assign the start location of the new memory
	*memory = blockPointer;

	// Success
	return (status = 0);
}



static int findBlock(unsigned memory)
{
	// Search the used block list for one with the supplied physical starting
	// address.  If found, return the index of the block (else negative error
	// code)

	int index = 0;

	// Now we can go through the "used" block list watching for a start
	// value that matches this pointer
	for (index = 0; ((index < usedBlocks) && (index < MAXMEMORYBLOCKS));
		index ++)
	{
		if (usedBlockList[index]->startLocation == memory)
			// This is the one
			return (index);
	}

	// Not found
	return (ERR_NOSUCHENTRY);
}


static int releaseBlock(int index)
{
	// This function will remove a block from the used block list, mark the
	// corresponding blocks as free in the free-block bitmap, and adjust
	// the totalUsed and totalFree values accordingly.  Returns 0 on success,
	// negative otherwise.

	int status = 0;
	memoryBlock *unused;
	unsigned temp = 0;
	unsigned lastBit = 0;

	// Make sure the block location is reasonable
	if (index > (usedBlocks - 1))
		return (status = ERR_NOSUCHENTRY);

	// Mark all of the applicable blocks in the free block bitmap as unused
	lastBit = (usedBlockList[index]->endLocation / MEMORY_BLOCK_SIZE);
	for (temp = (usedBlockList[index]->startLocation /
		MEMORY_BLOCK_SIZE); temp <= lastBit; temp ++)
	{
		freeBlockBitmap[temp / 8] &= (0xFF7F >> (temp % 8));
	}

	// Adjust the total used and free memory quantities
	temp = ((usedBlockList[index]->endLocation -
		usedBlockList[index]->startLocation) + 1);
	totalUsed -= temp;
	totalFree += temp;

	// Remove this element from the "used" part of the list.  What we have to
	// do is this: Since this is an unordered list, the way we accomplish this
	// is by copying the last entry into the place of the current entry and
	// reducing the total block count, unless the entry we're removing happens
	// to be the only item, or the last item in the list.  In those cases we
	// simply reduce the total number

	if ((usedBlocks > 1) && (index < (usedBlocks - 1)))
	{
		unused = usedBlockList[index];
		usedBlockList[index] = usedBlockList[usedBlocks - 1];
		usedBlockList[usedBlocks - 1] = unused;
	}

	// Now reduce the total count.
	usedBlocks -= 1;

	// Return success
	return (status = 0);
}


/////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////
//
//  Below here, the functions are exported for external use
//
/////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////

int kernelMemoryInitialize(unsigned kernelMemory)
{
	// This function will initialize all of the machine's memory between the
	// starting point and the ending point.  It will call a function to test
	// all memory using a test pattern defined in the header file, then it
	// will "zero" all the memory.  Returns 0 on success, negative otherwise.

	int status = 0;
	unsigned bitmapPhysical = NULL;
	unsigned bitmapSize = 0;
	const char *desc = NULL;
	unsigned start = 0, end = 0;
	int count;

	// Make sure that this initialization function only gets called once
	if (initialized)
		return (status = ERR_ALREADY);

	// Clear the static memory manager lock
	memset((void *) &memoryLock, 0, sizeof(lock));

	// Calculate the amount of total memory we're managing.  First, count
	// 1024 kilobytes for standard and high memory (first "megabyte")
	totalMemory = (1024 * 1024);

	// Add all the extended memory
	totalMemory += (kernelOsLoaderInfo->extendedMemory * 1024);

	// Make sure that totalMemory is a multiple of MEMORY_BLOCK_SIZE.
	if (totalMemory % MEMORY_BLOCK_SIZE)
		totalMemory = ((totalMemory / MEMORY_BLOCK_SIZE) * MEMORY_BLOCK_SIZE);

	totalUsed = 0;
	totalFree = totalMemory;

	// Initialize the used memory block list.  We don't need to initialize all
	// of the memory we use (we will be careful to initialize blocks when we
	// allocate them), but we do need to fill up the list of pointers to those
	// memory structures
	for (count = 0; count < MAXMEMORYBLOCKS; count ++)
		usedBlockList[count] = &usedBlockMemory[count];

	totalBlocks = (totalMemory / MEMORY_BLOCK_SIZE);
	usedBlocks = 0;

	// We need to define memory for the free-block bitmap.  However,
	// we will have to do it manually since, without the bitmap, we can't
	// do a "normal" block allocation.
	// This is a physical address.
	bitmapPhysical =
		(KERNEL_LOAD_ADDRESS + kernelMemory + KERNEL_PAGING_DATA_SIZE);

	// Calculate the size of the free-block bitmap, based on the total
	// number of memory blocks we'll be managing
	bitmapSize = ((totalBlocks + 7) / 8);

	// Make sure the bitmap is allocated to block boundaries
	bitmapSize += (MEMORY_BLOCK_SIZE - (bitmapSize % MEMORY_BLOCK_SIZE));

	// If we want to actually USE the memory we just allocated, we will
	// have to map it into the kernel's address space
	status = kernelPageMapToFree(KERNELPROCID, bitmapPhysical,
		(void **) &freeBlockBitmap, bitmapSize);
	if (status < 0)
		return (status);

	// Clear the memory we use for the bitmap
	memset((void *) freeBlockBitmap, 0, bitmapSize);

	// The list of reserved memory blocks needs to be completed here, before
	// we attempt to use it to calculate the location of the free-block
	// bitmap.
	for (count = 0; reservedBlocks[count].processId; count ++)
	{
		// Set the end value for the "kernel memory" reserved block
		if (!strcmp((char *) reservedBlocks[count].description,
			"kernel memory"))
		{
			reservedBlocks[count].endLocation =
				(KERNEL_LOAD_ADDRESS + kernelMemory - 1);
		}

		// Set the start and end values for the "kernel paging data"
		// reserved block
		if (!strcmp((char *) reservedBlocks[count].description,
			"kernel paging data"))
		{
			reservedBlocks[count].startLocation =
				(KERNEL_LOAD_ADDRESS + kernelMemory);
			reservedBlocks[count].endLocation =
				(reservedBlocks[count].startLocation +
					KERNEL_PAGING_DATA_SIZE - 1);
		}

		// Set the start and end values for the "free memory bitmap"
		// reserved block
		if (!strcmp((char *) reservedBlocks[count].description,
			"free memory bitmap"))
		{
			reservedBlocks[count].startLocation = (unsigned) bitmapPhysical;
			reservedBlocks[count].endLocation =
				(reservedBlocks[count].startLocation + bitmapSize - 1);
		}
	}

	// Allocate blocks for all our static reserved memory ranges, including the
	// free block bitmap (which is cool, because this allocation will use the
	// bitmap itself (which is 'unofficially' allocated).  Woo, paradox...
	for (count = 0; reservedBlocks[count].processId; count ++)
	{
		// No point in checking status from this call, as we don't want to fail
		// kernel initialization because of this, and logging and error output
		// aren't initialized at this stage.
		allocateBlock(reservedBlocks[count].processId,
			reservedBlocks[count].startLocation,
			reservedBlocks[count].endLocation,
			(char *) reservedBlocks[count].description);
	}

	// Now do the same for all the BIOS's non-available memory blocks.  It's OK
	// if these overlap with - or are already covered by - our static ones.
	for (count = 0; (kernelOsLoaderInfo->memoryMap[count].type &&
		(count < (int)(sizeof(kernelOsLoaderInfo->memoryMap) /
			sizeof(memoryInfoBlock)))); count ++)
	{
		if (kernelOsLoaderInfo->memoryMap[count].type == available)
			continue;

		switch(kernelOsLoaderInfo->memoryMap[count].type)
		{
			case reserved:
				desc = "bios reserved";
				break;
			case acpi_reclaim:
				desc = "acpi reclaim";
				break;
			case acpi_nvs:
				desc = "acpi nvs";
				break;
			case bad:
				desc = "bios bad";
				break;
			default:
				desc = "bios unknown";
				break;
		}

		// Make sure start locations are rounded down to block boundaries, and
		// sizes are rounded up.
		start = kernelOsLoaderInfo->memoryMap[count].start;
		end = (kernelOsLoaderInfo->memoryMap[count].start +
			(kernelOsLoaderInfo->memoryMap[count].size - 1));
		allocateBlock(KERNELPROCID, (start - (start % MEMORY_BLOCK_SIZE)),
			(end + ((MEMORY_BLOCK_SIZE -
				(end % MEMORY_BLOCK_SIZE)) - 1)), desc);
	}

	// Make note of the fact that we've now been initialized
	initialized = 1;

	// Return success
	return (status = 0);
}


unsigned kernelMemoryGetPhysical(unsigned size, unsigned alignment,
	const char *description)
{
	// This function is a wrapper around the requestBlock function.
	// It returns the same physical memory address returned by requestBlock.

	int status = 0;
	unsigned physical = NULL;

	// Make sure the memory manager has been initialized
	if (!initialized)
		return (physical = NULL);

	if (kernelProcessingInterrupt())
		return (physical = NULL);

	// Obtain a lock on the memory data
	status = kernelLockGet(&memoryLock);
	if (status < 0)
		return (physical = NULL);

	// Call requestBlock to find a free memory region.
	status = requestBlock(KERNELPROCID, size, alignment, description,
		&physical);

	// Release the lock on the memory data
	kernelLockRelease(&memoryLock);

	if (status < 0)
		return (physical = NULL);

	// Don't clear this memory, since it has not been mapped into the
	// virtual address space.  The caller must clear it, if desired.
	return (physical);
}


int kernelMemoryReleasePhysical(unsigned physical)
{
	// This function will find the block of the block that begins with
	// the the supplied physical address, and releases it.

	int status = 0;
	int index = 0;

	// Make sure the memory manager has been initialized
	if (!initialized)
		return (status = ERR_NOTINITIALIZED);

	// Check params
	if (!physical)
		return (status = ERR_NOSUCHENTRY);

	if (kernelProcessingInterrupt())
		return (status = ERR_INVALID);

	// Obtain a lock on the memory data
	status = kernelLockGet(&memoryLock);
	if (status < 0)
		return (status);

	// Try to find the block
	index = findBlock(physical);

	if (index >= 0)
		// Call the releaseBlock function with this block's index.
		status = releaseBlock(index);

	// Release the lock on the memory data
	kernelLockRelease(&memoryLock);

	if (index < 0)
		return (status = index);
	else
		return (status);
}


void *kernelMemoryGetSystem(unsigned size, const char *description)
{
	// This function will call kernelMemoryGetPhysical to allocate some
	// physical memory, and map it to virtual memory pages in the address
	// space of the kernel.

	int status = 0;
	unsigned physical = NULL;
	void *virtual = NULL;

	// This function will check initialization, etc
	physical = kernelMemoryGetPhysical(size, 0 /* alignment */, description);
	if (!physical)
		return (virtual = NULL);

	// Now we will ask the page manager to map this physical memory to
	// virtual memory pages in the address space of the kernel.
	status = kernelPageMapToFree(KERNELPROCID, physical, &virtual, size);
	if (status < 0)
	{
		// Attempt to deallocate the physical memory block.
		kernelError(kernel_error, "Unable to map system memory block");
		kernelMemoryReleasePhysical(physical);
		return (virtual = NULL);
	}

	// Clear the memory area we allocated
	memset(virtual, 0, size);

	return (virtual);
}


int kernelMemoryReleaseSystem(void *virtual)
{
	// This function will determine the blockId of the block that contains
	// the memory location pointed to by the parameter, unmap it from the
	// kernel's page table, and deallocate it.

	int status = 0;
	unsigned physical = NULL;
	int index = 0;

	// Make sure the memory manager has been initialized
	if (!initialized)
		return (status = ERR_NOTINITIALIZED);

	// Check params
	if (!virtual)
		return (status = ERR_NULLPARAMETER);

	if (kernelProcessingInterrupt())
		return (status = ERR_INVALID);

	// Get the memory's physical address.  We could simply unmap it here,
	// except that we don't yet know the size of the block.
	physical = kernelPageGetPhysical(KERNELPROCID, virtual);
	if (!physical)
	{
		kernelError(kernel_error, "The memory pointer is not mapped");
		return (status = ERR_NOSUCHENTRY);
	}

	// Obtain a lock on the memory data
	status = kernelLockGet(&memoryLock);
	if (status < 0)
		return (status);

	// Try to find the block
	index = findBlock(physical);

	if (index >= 0)
	{
		// Now that we know the index of the memory block, we can get the
		// size, and unmap it from the virtual address space.
		status = kernelPageUnmap(KERNELPROCID, virtual,
			(usedBlockList[index]->endLocation -
				usedBlockList[index]->startLocation + 1));
		if (status < 0)
			kernelError(kernel_error, "Unable to unmap memory from the "
				"irtual address space");

		// Call the releaseBlock function with this block's index.
		status = releaseBlock(index);
	}
	else
		status = index;

	// Release the lock on the memory data
	kernelLockRelease(&memoryLock);

	return (status);
}


int kernelMemoryGetIo(unsigned size, unsigned alignment, kernelIoMemory *ioMem)
{
	// Use this to allocate kernel-owned I/O memory when we need to
	//	a) possibly align it;
	//	b) know both the physical and virtual addresses; and
	//	c) make it non-cacheable

	int status = 0;

	// Make sure the memory manager has been initialized
	if (!initialized)
		return (status = ERR_NOTINITIALIZED);

	// Check params
	if (!size || !ioMem)
		return (status = ERR_NULLPARAMETER);

	ioMem->size = size;

	// Can only align on page boundaries, so round up if necessary
	if (alignment && (alignment < MEMORY_BLOCK_SIZE))
		alignment = MEMORY_BLOCK_SIZE;

	// Request memory for an aligned array of TRBs
	ioMem->physical = kernelMemoryGetPhysical(ioMem->size, alignment,
		"i/o memory");
	if (!ioMem->physical)
	{
		status = ERR_MEMORY;
		goto err_out;
	}

	// Map the physical memory into virtual memory
	status = kernelPageMapToFree(KERNELPROCID, ioMem->physical,
		&ioMem->virtual, ioMem->size);
	if (status < 0)
		goto err_out;

	// Make it non-cacheable
	status = kernelPageSetAttrs(KERNELPROCID, 1 /* set */,
		PAGEFLAG_CACHEDISABLE, ioMem->virtual, ioMem->size);
	if (status < 0)
		goto err_out;

	// Clear it out, as the kernelMemoryGetPhysical function can't do that
	// for us
	memset(ioMem->virtual, 0, ioMem->size);

	return (status = 0);

err_out:
	kernelMemoryReleaseIo(ioMem);

	return (status);
}


int kernelMemoryReleaseIo(kernelIoMemory *ioMem)
{
	// Attempt to unmap and free any memory allocated using the
	// kernelMemoryGetIo() function, above

	int status = 0;

	// Make sure the memory manager has been initialized
	if (!initialized)
		return (status = ERR_NOTINITIALIZED);

	// Check params
	if (!ioMem)
		return (status = ERR_NULLPARAMETER);

	if (ioMem->virtual)
	{
		status = kernelPageUnmap(KERNELPROCID, ioMem->virtual, ioMem->size);
		if (status < 0)
			return (status);
	}

	if (ioMem->physical)
	{
		status = kernelMemoryReleasePhysical(ioMem->physical);
		if (status < 0)
			return (status);
	}

	return (status = 0);
}


void *kernelMemoryGet(unsigned size, const char *description)
{
	// This function will allocate some physical memory, and map it to
	// virtual memory pages in the address space of the current process.

	int status = 0;
	int processId = 0;
	unsigned physical = NULL;
	void *virtual = NULL;

	// Make sure the memory manager has been initialized
	if (!initialized)
		return (virtual = NULL);

	if (kernelProcessingInterrupt())
		return (virtual = NULL);

	// Get the current process Id
	processId = kernelMultitaskerGetCurrentProcessId();
	if (processId < 0)
	{
		kernelError(kernel_error, "Unable to determine the current process");
		return (virtual = NULL);
	}

	// Obtain a lock on the memory data
	status = kernelLockGet(&memoryLock);
	if (status < 0)
		return (virtual = NULL);

	// Call requestBlock to find a free memory region.
	status = requestBlock(processId, size, 0 /* alignment */, description,
		&physical);

	// Release the lock on the memory data
	kernelLockRelease(&memoryLock);

	if (status < 0)
		return (virtual = NULL);

	// Now we will ask the page manager to map this physical memory to
	// virtual memory pages in the address space of the current process.
	status = kernelPageMapToFree(processId, physical, &virtual, size);
	if (status < 0)
	{
		// Attempt to deallocate the physical memory block.
		kernelMemoryReleasePhysical(physical);
		return (virtual = NULL);
	}

	// Clear the memory area we allocated
	memset(virtual, 0, size);

	return (virtual);
}


int kernelMemoryRelease(void *virtual)
{
	// This function will determine the blockId of the block that contains
	// the memory location pointed to by the parameter, unmap it from the
	// relevant page table, and deallocate it.

	int status = 0;
	int pid = 0;
	unsigned physical = NULL;
	int index = 0;

	// Make sure the memory manager has been initialized
	if (!initialized)
		return (status = ERR_NOTINITIALIZED);

	// Check params
	if (!virtual)
		return (status = ERR_NULLPARAMETER);

	if (kernelProcessingInterrupt())
		return (status = ERR_INVALID);

	pid = kernelMultitaskerGetCurrentProcessId();

	// Permission check: This function can only be used to release system
	// blocks by privileged processes because it is accessible to user functions
	if ((virtual >= (void *) KERNEL_VIRTUAL_ADDRESS) &&
		(pid != KERNELPROCID) &&
		(kernelMultitaskerGetProcessPrivilege(pid) != PRIVILEGE_SUPERVISOR))
	{
		kernelError(kernel_error, "Cannot release system memory block from "
			"unprivileged user process %d", pid);
		return (status = ERR_PERMISSION);
	}

	if (virtual >= (void *) KERNEL_VIRTUAL_ADDRESS)
		pid = KERNELPROCID;

	// Get the memory's physical address.  We could simply unmap it here,
	// except that we don't yet know the size of the block.
	physical = kernelPageGetPhysical(pid, virtual);
	if (!physical)
	{
		kernelError(kernel_error, "The memory pointer is not mapped");
		return (status = ERR_NOSUCHENTRY);
	}

	// Obtain a lock on the memory data
	status = kernelLockGet(&memoryLock);
	if (status < 0)
		return (status);

	// Try to find the block
	index = findBlock(physical);

	if (index >= 0)
	{
		// Now that we know the index of the memory block, we can get the size,
		// and unmap it from the virtual address space.
		status = kernelPageUnmap(pid, virtual,
			(usedBlockList[index]->endLocation -
				usedBlockList[index]->startLocation + 1));
		if (status < 0)
			kernelError(kernel_error, "Unable to unmap memory from the "
				"virtual address space");

		// Call the releaseBlock function with this block's index.
		status = releaseBlock(index);
	}
	else
		status = index;

	// Release the lock on the memory data
	kernelLockRelease(&memoryLock);

	return (status);
}


int kernelMemoryReleaseAllByProcId(int processId)
{
	// This function will find all memory blocks owned by a particular
	// process and call releaseBlock to remove each one.  It returns 0
	// on success, negative otherwise.

	int status = 0;
	int count;

	// Make sure the memory manager has been initialized
	if (!initialized)
		return (status = ERR_NOTINITIALIZED);

	if (kernelProcessingInterrupt())
		return (status = ERR_INVALID);

	// Obtain a lock on the memory data
	status = kernelLockGet(&memoryLock);
	if (status < 0)
		return (status);

	for (count = 0; ((count < usedBlocks) && (count < MAXMEMORYBLOCKS));
		count ++)
	{
		if (usedBlockList[count]->processId == processId)
		{
			// This is one.
			status = releaseBlock(count);
			if (status < 0)
			{
				kernelLockRelease(&memoryLock);
				return (status);
			}

			// Reduce "count" by one, since the block it points at now
			// will be one forward of where we want to be
			count -= 1;
		}
	}

	// Release the lock on the memory data
	kernelLockRelease(&memoryLock);

	// Return success
	return (status = 0);
}


int kernelMemoryChangeOwner(int oldPid, int newPid, int remap,
	void *oldVirtual, void **newVirtual)
{
	// This function can be used by a privileged process to change the
	// process owner of a block of allocated memory.

	int status = 0;
	unsigned physical = NULL;
	int index = 0;
	unsigned blockSize = 0;

	// Make sure the memory manager has been initialized
	if (!initialized)
		return (status = ERR_NOTINITIALIZED);

	// Make sure newVirtual isn't NULL if we're remapping
	if (remap && !newVirtual)
	{
		kernelError(kernel_error, "Pointer for new virtual address is NULL");
		return (status = ERR_NULLPARAMETER);
	}

	// Do we really need to change anything?
	if (oldPid == newPid)
	{
		// Nope.  The processes are the same.
		if (remap)
			*newVirtual = oldVirtual;
		return (status = 0);
	}

	// Turn the virtual address into a physical one
	physical = kernelPageGetPhysical(oldPid, oldVirtual);
	if (!physical)
	{
		kernelError(kernel_error, "The memory pointer is NULL");
		return (status = ERR_NOSUCHENTRY);
	}

	// Obtain a lock on the memory data
	status = kernelLockGet(&memoryLock);
	if (status < 0)
		return (status);

	// Try to find the block
	index = findBlock(physical);

	// Release the lock on the memory data
	kernelLockRelease(&memoryLock);

	if (index < 0)
		return (status = index);

	if (usedBlockList[index]->processId != oldPid)
	{
		kernelError(kernel_error, "Attempt to change memory ownership from "
			"incorrect owner (%d should be %d)", oldPid,
			usedBlockList[index]->processId);
		return (status = ERR_PERMISSION);
	}

	// Change the pid number on this block
	usedBlockList[index]->processId = newPid;

	if (remap)
	{
		if (kernelMultitaskerGetPageDir(newPid) !=
			kernelMultitaskerGetPageDir(oldPid))
		{
			blockSize = ((usedBlockList[index]->endLocation -
				usedBlockList[index]->startLocation) + 1);

			// Map the memory into the new owner's address space
			status = kernelPageMapToFree(newPid, physical, newVirtual,
				blockSize);
			if (status < 0)
				return (status);

			// Unmap the memory from the old owner's address space
			status = kernelPageUnmap(oldPid, oldVirtual, blockSize);
			if (status < 0)
				return (status);
		}
		else
			*newVirtual = oldVirtual;
	}

	// Return success
	return (status = 0);
}


int kernelMemoryShare(int sharerPid, int shareePid, void *oldVirtual,
	void **newVirtual)
{
	// This function can be used by a privileged process to share a piece
	// of memory owned by one process with another process.  This will not
	// adjust the privilege levels of memory that is shared, so a privileged
	// process cannot share memory with an unprivileged process (an access by
	// the second process will cause a page fault).

	int status = 0;
	unsigned physical = NULL;
	int index = 0;
	unsigned blockSize = 0;

	// Make sure the memory manager has been initialized
	if (!initialized)
		return (status = ERR_NOTINITIALIZED);

	// We do a privilege check here, to make sure that this operation
	// is allowed

	// Do we really need to change anything?
	if ((sharerPid == shareePid) ||
		(kernelMultitaskerGetPageDir(sharerPid) ==
			kernelMultitaskerGetPageDir(shareePid)))
	{
		// Nothing to do.  The processes are either the same or else use the
		// same page directory.
		*newVirtual = oldVirtual;
		return (status = 0);
	}

	// Turn the virtual address into a physical one
	physical = kernelPageGetPhysical(sharerPid, oldVirtual);
	if (!physical)
	{
		kernelError(kernel_error, "The memory pointer is NULL");
		return (status = ERR_NOSUCHENTRY);
	}

	// Obtain a lock on the memory data
	status = kernelLockGet(&memoryLock);
	if (status < 0)
		return (status);

	// Try to find the block
	index = findBlock(physical);

	// Release the lock on the memory data
	kernelLockRelease(&memoryLock);

	if (index < 0)
		return (status = index);

	if (usedBlockList[index]->processId != sharerPid)
	{
		kernelError(kernel_error, "Attempt to share memory from incorrect "
			"owner (%d should be %d)", sharerPid,
			usedBlockList[index]->processId);
		return (status = ERR_PERMISSION);
	}

	blockSize = ((usedBlockList[index]->endLocation -
		usedBlockList[index]->startLocation) + 1);

	// Map the memory into the sharee's address space
	status = kernelPageMapToFree(shareePid, physical, newVirtual, blockSize);

	// The sharer still owns the memory, so don't change the block's PID.
	return (status);
}


int kernelMemoryGetStats(memoryStats *stats, int kernel)
{
	// Return overall memory usage statistics

	int status = 0;

	// Make sure the memory manager has been initialized
	if (!initialized)
		return (status = ERR_NOTINITIALIZED);

	if (kernel)
		// Do kernelMalloc stats instead
		return (kernelMallocGetStats(stats));

	// Check params
	if (!stats)
	{
		kernelError(kernel_error, "NULL parameter");
		return (status = ERR_NULLPARAMETER);
	}

	stats->totalBlocks = totalBlocks;
	stats->usedBlocks = usedBlocks;
	stats->totalMemory = totalMemory;
	stats->usedMemory = totalUsed;

	return (status = 0);
}


int kernelMemoryGetBlocks(memoryBlock *blocksArray, unsigned buffSize,
	int kernel)
{
	// Fill a memoryBlock array with used blocks information, up to buffSize
	// bytes.

	int status = 0;
	int doBlocks = 0;
	memoryBlock *temp = NULL;
	int count1, count2;

	// Make sure the memory manager has been initialized
	if (!initialized)
		return (status = ERR_NOTINITIALIZED);

	if (!buffSize)
		// Alrighty then
		return (status = 0);

	doBlocks = (buffSize / sizeof(memoryBlock));

	if (kernel)
		// Do kernelMalloc blocks instead
		return (kernelMallocGetBlocks(blocksArray, doBlocks));

	// Check params
	if (!blocksArray)
	{
		kernelError(kernel_error, "NULL parameter");
		return (status = ERR_NULLPARAMETER);
	}

	// Obtain a lock on the memory data
	status = kernelLockGet(&memoryLock);
	if (status < 0)
		return (status);

	// Before we return the list of used memory blocks, we should sort it
	// so that it's a little easier to see the distribution of memory.
	// This will not help the memory manager to function more efficiently
	// or anything, it's purely cosmetic.  Just a bubble sort.
	for (count1 = 0; count1 < usedBlocks; count1 ++)
	{
		for (count2 = 0;  count2 < (usedBlocks - 1); count2 ++)
		{
			if (usedBlockList[count2]->startLocation >
				usedBlockList[count2 + 1]->startLocation)
			{
				temp = usedBlockList[count2 + 1];
				usedBlockList[count2 + 1] = usedBlockList[count2];
				usedBlockList[count2] = temp;
			}
		}
	}

	// Release the lock on the memory data
	kernelLockRelease(&memoryLock);

	// Here's the loop through the used list
	for (count1 = 0; count1 < doBlocks; count1 ++)
	{
		blocksArray[count1].processId = usedBlockList[count1]->processId;
		strncpy(blocksArray[count1].description,
			usedBlockList[count1]->description, MEMORY_MAX_DESC_LENGTH);
		blocksArray[count1].startLocation = usedBlockList[count1]->startLocation;
		blocksArray[count1].endLocation = usedBlockList[count1]->endLocation;
	}

	return (status = 0);
}


int kernelMemoryBlockInfo(void *virtual, memoryBlock *block)
{
	// Given a virtual address, fill in the user-space memoryBlock structure
	// with information about that block.

	int status = 0;
	int currentPid = 0;
	unsigned physical = NULL;
	int index = 0;

	// Make sure the memory manager has been initialized
	if (!initialized)
		return (status = ERR_NOTINITIALIZED);

	// Check params.  It's OK for virtualAddress to be NULL.
	if (!block)
		return (status = ERR_NULLPARAMETER);

	currentPid = kernelMultitaskerGetCurrentProcessId();

	// Turn the virtual address into a physical one
	physical = kernelPageGetPhysical(currentPid, virtual);
	if (!physical)
	{
		kernelError(kernel_error, "The memory pointer is not mapped");
		return (status = ERR_NOSUCHENTRY);
	}

	// Obtain a lock on the memory data
	status = kernelLockGet(&memoryLock);
	if (status < 0)
		return (status);

	// Try to find the block
	index = findBlock(physical);

	// Release the lock on the memory data
	kernelLockRelease(&memoryLock);

	if (index < 0)
		return (status = ERR_NOSUCHENTRY);

	memcpy(block, usedBlockList[index], sizeof(memoryBlock));
	block->endLocation = (unsigned)(virtual + (block->endLocation -
		block->startLocation));
	block->startLocation = (unsigned) virtual;

	return (status = 0);
}

