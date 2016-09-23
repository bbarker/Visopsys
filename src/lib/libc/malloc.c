//
//  Visopsys
//  Copyright (C) 1998-2016 J. Andrew McLaughlin
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
//  malloc.c
//

// These routines comprise Visopsys heap memory management system.  It relies
// upon the kernelMemory code, and does similar things, but instead of whole
// memory pages, it allocates arbitrary-sized chunks.

#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <sys/memory.h>
#include <sys/api.h>

static mallocBlock *usedBlockList = NULL;
static mallocBlock *freeBlockList = NULL;
static mallocBlock *vacantBlockList = NULL;
static volatile unsigned totalBlocks = 0;
static volatile unsigned vacantBlocks = 0;
static volatile unsigned totalMemory = 0;
static volatile unsigned usedMemory = 0;
static lock blocksLock;

unsigned mallocHeapMultiple = USER_MEMORY_HEAP_MULTIPLE;
mallocKernelOps mallocKernOps;

#define USEDLIST_REF (&usedBlockList)
#define FREELIST_REF (&freeBlockList)
#define blockEnd(block) (block->start + (block->size - 1))

// Malloc debugging messages are off by default even in a debug build.
#undef DEBUG

#if defined(DEBUG)
#define debug(message, arg...) do { \
	if (visopsys_in_kernel) { \
		if (mallocKernOps.debug) \
			mallocKernOps.debug(__FILE__, __FUNCTION__, __LINE__, \
				debug_memory, message, ##arg); \
	} else { \
		printf("DEBUG: %s:%s(%d): ", __FILE__, __FUNCTION__, __LINE__); \
		printf(message, ##arg); \
		printf("\n"); \
	} } while (0)
#else
	#define debug(message, arg...) do { } while (0)
#endif // defined(DEBUG)

#define error(message, arg...) do { \
	if (visopsys_in_kernel) { \
		mallocKernOps.error(__FILE__, __FUNCTION__, __LINE__, kernel_error, \
			message, ##arg); \
	} else { \
		printf("Error: %s:%s(%d): ", __FILE__, __FUNCTION__, __LINE__); \
		printf(message, ##arg); \
		printf("\n"); \
	} } while (0)


static inline int procid(void)
{
	if (visopsys_in_kernel)
		return (mallocKernOps.multitaskerGetCurrentProcessId());
	else
		return (multitaskerGetCurrentProcessId());
}


static inline void *memory_get(unsigned size, const char *desc)
{
	debug("Request memory block of size %u", size);
	if (visopsys_in_kernel)
		return (mallocKernOps.memoryGet(size, desc));
	else
		return (memoryGet(size, desc));
}


static inline int memory_release(void *start)
{
	debug("Release memory block at %p", start);
	if (visopsys_in_kernel)
		return (mallocKernOps.memoryRelease(start));
	else
		return (memoryRelease(start));
}


static inline int lock_get(lock *lk)
{
	if (visopsys_in_kernel)
		return (mallocKernOps.lockGet(lk));
	else
		return (lockGet(lk));
}


static inline void lock_release(lock *lk)
{
	if (visopsys_in_kernel)
		mallocKernOps.lockRelease(lk);
	else
		lockRelease(lk);
}


static inline void insertBlock(mallocBlock **list, mallocBlock *insBlock,
	mallocBlock *nextBlock)
{
	// Stick the first block in front of the second block

	debug("Insert block %08x->%08x (%u) before %08x->%08x (%u)",
		insBlock->start, blockEnd(insBlock), insBlock->size, nextBlock->start,
		blockEnd(nextBlock), nextBlock->size);

	insBlock->prev = nextBlock->prev;
	insBlock->next = nextBlock;

	if (nextBlock->prev)
		nextBlock->prev->next = insBlock;

	nextBlock->prev = insBlock;

	if (nextBlock == *list)
		*list = insBlock;
}


static inline void appendBlock(mallocBlock *appBlock, mallocBlock *prevBlock)
{
	// Stick the first block behind the second block

	debug("Append block %08x->%08x (%u) after %08x->%08x (%u)",
		appBlock->start, blockEnd(appBlock), appBlock->size,
		prevBlock->start, blockEnd(prevBlock), prevBlock->size);

	appBlock->prev = prevBlock;
	appBlock->next = prevBlock->next;

	if (prevBlock->next)
		prevBlock->next->prev = appBlock;

	prevBlock->next = appBlock;
}


static int sortInsertBlock(mallocBlock **list, mallocBlock *block)
{
	// Find the correct (sorted) place for in the block list for this block.

	int status = 0;
	mallocBlock *nextBlock = NULL;

	debug("Sort insert block %08x->%08x (%u) in %s list", block->start,
		blockEnd(block), block->size,
		((list == USEDLIST_REF)? "used" : "free"));

	if (*list)
	{
		nextBlock = *list;

		while (nextBlock)
		{
			if (nextBlock->start > block->start)
			{
				insertBlock(list, block, nextBlock);
				break;
			}

			if (!nextBlock->next)
			{
				appendBlock(block, nextBlock);
				break;
			}

			nextBlock = nextBlock->next;
		}
	}
	else
	{
		block->prev = NULL;
		block->next = NULL;
		*list = block;
	}

	return (status = 0);
}


static int allocVacantBlocks(void)
{
	// This gets 1 memory page of memory for a new batch of vacant blocks.

	int status = 0;
	int numBlocks = 0;
	int count;

	debug("Allocating new vacant blocks");

	if (visopsys_in_kernel)
		vacantBlockList = memory_get(MEMORY_BLOCK_SIZE, "kernel heap metadata");
	else
		vacantBlockList = memory_get(MEMORY_BLOCK_SIZE, "user heap metadata");

	if (!vacantBlockList)
	{
		error("Unable to allocate heap management memory");
		return (status = ERR_MEMORY);
	}

	// How many blocks is that?
	numBlocks = (MEMORY_BLOCK_SIZE / sizeof(mallocBlock));

	// Initialize the pointers in our list of blocks
	for (count = 0; count < numBlocks; count ++)
	{
		if (count > 0)
			vacantBlockList[count].prev = &vacantBlockList[count - 1];
		if (count < (numBlocks - 1))
			vacantBlockList[count].next = &vacantBlockList[count + 1];
	}

	totalBlocks += numBlocks;
	vacantBlocks += numBlocks;

	return (status = 0);
}


static mallocBlock *getBlock(void)
{
	// Get a block from the vacant block list.

	mallocBlock *block = NULL;

	// Do we have any more unused blocks?
	if (!vacantBlockList)
	{
		if (allocVacantBlocks() < 0)
			return (block = NULL);
	}

	block = vacantBlockList;
	vacantBlockList = block->next;

	// Clear it
	memset(block, 0, sizeof(mallocBlock));

	vacantBlocks -= 1;

	return (block);
}


static void removeBlock(mallocBlock **list, mallocBlock *block)
{
	// Remove a block from a list

	debug("Remove block %08x->%08x (%u) from %s list", block->start,
		blockEnd(block), block->size,
		((list == USEDLIST_REF)? "used" : "free"));

	if (block->prev)
		block->prev->next = block->next;
	if (block->next)
		block->next->prev = block->prev;

	if (block == *list)
		*list = block->next;

	block->prev = NULL;
	block->next = NULL;

	return;
}


static void putBlock(mallocBlock **list, mallocBlock *block)
{
	// This function gets called when a block is no longer needed.  We
	// zero out its fields and move it to the end of the used blocks.

	// Remove the block from the list.
	removeBlock(list, block);

	// Clear it
	memset(block, 0, sizeof(mallocBlock));

	// Put it at the head of the unused block list
	block->next = vacantBlockList;
	vacantBlockList = block;

	vacantBlocks += 1;

	return;
}


static int createBlock(mallocBlock **list, unsigned start, unsigned size,
	unsigned heapAlloc, unsigned heapAllocSize)
{
	// Creates a used or free block in the supplied block list.

	int status = 0;
	mallocBlock *block = NULL;

	debug("Create block %08x-%08x (%u) in %s list", start,
		(start + (size - 1)), size, ((list == USEDLIST_REF)? "used" : "free"));

	block = getBlock();
	if (!block)
		return (status = ERR_NOFREE);

	block->start = start;
	block->size = size;
	block->heapAlloc = heapAlloc;
	block->heapAllocSize = heapAllocSize;

	return (status = sortInsertBlock(list, block));
}


static int growHeap(unsigned minSize)
{
	// This grows the pool of heap memory by at least minSize bytes.

	void *newHeap = NULL;

	// Don't allocate less than the default heap multiple
	if (minSize < mallocHeapMultiple)
		minSize = mallocHeapMultiple;

	// Allocation should be a multiple of MEMORY_BLOCK_SIZE
	if (minSize % MEMORY_BLOCK_SIZE)
		minSize += (MEMORY_BLOCK_SIZE - (minSize % MEMORY_BLOCK_SIZE));

	debug("Grow heap by at least %u", minSize);
	if (minSize > mallocHeapMultiple)
		debug("Size is greater than %u", mallocHeapMultiple);

	// Get the heap memory
	if (visopsys_in_kernel)
		newHeap = memory_get(minSize, "kernel heap");
	else
		newHeap = memory_get(minSize, "user heap");

	if (!newHeap)
	{
		error("Unable to allocate heap memory");
		return (ERR_MEMORY);
	}

	totalMemory += minSize;

	// Add it as a single free block
	return (createBlock(FREELIST_REF, (unsigned) newHeap, minSize,
		(unsigned) newHeap, minSize));
}


static mallocBlock *findFree(unsigned size)
{
	// This is a best-fit algorithm to search the free block list for the free
	// block that's closest to the size requested.  We do it this way in order
	// to (hopefully) reduce memory fragmentation - i.e. reduce heap memory
	// usage - at the possible expense of slightly longer searches.

	mallocBlock *block = freeBlockList;
	mallocBlock *closestBlock = NULL;

	debug("Search for free block of at least %u", size);

	while (block)
	{
		// If the block is exactly the right size, return it
		if (block->size == size)
		{
			debug("Found free block of size %u", block->size);
			return (block);
		}

		// If the block is closer in size than any others, remember it
		if (block->size > size)
		{
			if (!closestBlock || (block->size < closestBlock->size))
				closestBlock = block;
		}

		block = block->next;
	}

	if (closestBlock)
	{
		debug("Found free block of size %u", closestBlock->size);
		return (closestBlock);
	}
	else
	{
		debug("No block found");
		return (block = NULL);
	}
}


static void *allocateBlock(unsigned size, const char *function)
{
	// Find a block of unused memory, and return the start pointer.

	int status = 0;
	mallocBlock *block = NULL;
	unsigned remainder = 0;

	// Make sure we do allocations on nice boundaries
	if (size % sizeof(int))
	{
		debug("Increase allocation size from %u to %u", size,
			(size + (sizeof(int) - (size % sizeof(int)))));
		size += (sizeof(int) - (size % sizeof(int)));
	}

	// Make sure there's enough heap memory.  This will get called the first
	// time we're invoked, as totalMemory will be zero.
	if ((size > (totalMemory - usedMemory)) || !(block = findFree(size)))
	{
		status = growHeap(size);
		if (status < 0)
		{
			errno = status;
			return (NULL);
		}

		block = findFree(size);
		if (!block)
		{
			// Something really wrong.
			error("Unable to allocate block of size %u (%s)", size, function);
			return (NULL);
		}
	}

	// Remove it from the free list
	removeBlock(FREELIST_REF, block);

	block->function = function;
	block->process = procid();

	// Add it to the used block list
	sortInsertBlock(USEDLIST_REF, block);
	if (status < 0)
		return (NULL);

	// If part of this block will be unused, we will need to create a free
	// block for the remainder
	if (block->size > size)
	{
		remainder = (block->size - size);
		block->size = size;

		debug("Split block of size %u from remainder of size %u", size,
		remainder);

		if (createBlock(FREELIST_REF, (block->start + size), remainder,
			block->heapAlloc, block->heapAllocSize) < 0)
		return (NULL);
	}

	usedMemory += size;

	return ((void *) block->start);
}


static void mergeFree(mallocBlock *block)
{
	// Merge this free block with the previous and/or next blocks if they
	// are also free.

	// Check whether we're contiguous with the previous block
	if (block->prev && (block->prev->heapAlloc == block->heapAlloc) &&
		(blockEnd(block->prev) == (block->start - 1)))
	{
		block->start = block->prev->start;
		block->size += block->prev->size;
		putBlock(FREELIST_REF, block->prev);
	}

	// Check whether we're contiguous with the next block
	if (block->next && (block->next->heapAlloc == block->heapAlloc) &&
		(blockEnd(block) == (block->next->start - 1)))
	{
		block->size += block->next->size;
		putBlock(FREELIST_REF, block->next);
	}
}


static void cleanupHeap(mallocBlock *block)
{
	// If the supplied free block comprises an entire heap allocation,
	// return that heap memory and get rid of the block.

	if (block->size != block->heapAllocSize)
		return;

	// Looks like we can return this memory.
	debug("Release heap memory allocation %08x->%08x (%u)", block->start,
		blockEnd(block), block->size);

	memory_release((void *) block->start);
	totalMemory -= block->size;
	putBlock(FREELIST_REF, block);
}


static int deallocateBlock(void *start, const char *function)
{
	// Find an allocated (used) block and deallocate it.

	int status = 0;
	mallocBlock *block = usedBlockList;

	while (block)
	{
		if (block->start == (unsigned) start)
		{
			// Remove it from the used list
			removeBlock(USEDLIST_REF, block);

			// Clear out the memory
			memset(start, 0, block->size);

			block->process = 0;
			block->function = NULL;

			// Add it to the free block list
			sortInsertBlock(FREELIST_REF, block);
			if (status < 0)
				return (status);

			usedMemory -= block->size;

			// Merge free blocks on either side of this one
			mergeFree(block);

			// Can the heap be deallocated?
			cleanupHeap(block);

			// Don't try to use 'block' after this point, it might have
			// disappeared.
			block = NULL;

			return (status = 0);
		}

		block = block->next;
	}

	error("No such memory block %08x to deallocate (%s)", (unsigned) start,
		function);
	return (status = ERR_NOSUCHENTRY);
}


static inline void mallocBlock2MemoryBlock(mallocBlock *maBlock,
	memoryBlock *meBlock)
{
	meBlock->processId = maBlock->process;
	strncpy(meBlock->description, maBlock->function, MEMORY_MAX_DESC_LENGTH);
	meBlock->description[MEMORY_MAX_DESC_LENGTH - 1] = '\0';
	meBlock->startLocation = maBlock->start;
	meBlock->endLocation = blockEnd(maBlock);
}


#if defined(DEBUG)
static int checkPointer(mallocBlock *block)
{
	int status = 0;

	if (visopsys_in_kernel)
	{
		if ((unsigned) block < 0xC0000000)
		{
			error("Kernel block %p is not in kernel memory space", block);
			return (status = ERR_BADADDRESS);
		}
	}
	else
	{
		if ((unsigned) block > 0xC0000000)
		{
			error("User block %p is in kernel memory space", block);
			return (status = ERR_BADADDRESS);
		}
	}

	return (status = 0);
}


static int checkBlocks(void)
{
	int status = 0;
	mallocBlock **lists[2] = {
		FREELIST_REF, USEDLIST_REF,
	};
	const char *listName = NULL;
	mallocBlock *block = NULL;
	mallocBlock *prev = NULL;
	mallocBlock *next = NULL;
	int count;

	for (count = 0; count < 2; count ++)
	{
		if (lists[count] == FREELIST_REF)
			listName = "free";
		else if (lists[count] == USEDLIST_REF)
			listName = "used";
		else
			listName = "unknown";

		block = *lists[count];

		while (block)
		{
			status = checkPointer(block);
			if (status < 0)
				return (status);

			if (block->prev)
			{
				prev = block->prev;

				status = checkPointer(prev);
				if (status < 0)
					return (status);

				if (prev->next != block)
				{
					error("Previous block %08x->%08x (%u) does not point to "
						"current block %08x->%08x (%u) in %s list",
						prev->start, blockEnd(prev), prev->size, block->start,
						blockEnd(block), block->size, listName);
					return (status = ERR_BADDATA);
				}

				if (prev->start >= block->start)
				{
					error("Previous block %08x->%08x (%u) does not start "
						"before current block %08x->%08x (%u) in %s list",
						prev->start, blockEnd(prev), prev->size, block->start,
						blockEnd(block), block->size, listName);
					return (status = ERR_BADDATA);
				}

				if (blockEnd(prev) >= block->start)
				{
					error("Previous block %08x->%08x (%u) end overlaps "
						"current block %08x->%08x (%u) in %s list",
						prev->start, blockEnd(prev), prev->size, block->start,
						blockEnd(block), block->size, listName);
					return (status = ERR_BADDATA);
				}
			}

			if (block->next)
			{
				next = block->next;

				status = checkPointer(next);
				if (status < 0)
					return (status);

				if (next->prev != block)
				{
					error("Next block %08x->%08x (%u) does not point to "
						"current block %08x->%08x (%u) in %s list",
						next->start, blockEnd(next), next->size, block->start,
						blockEnd(block), block->size, listName);
					return (status = ERR_BADDATA);
				}

				if (block->start >= next->start)
				{
					error("Next block %08x->%08x (%u) does not start after "
						"current block %08x->%08x (%u) in %s list",
						next->start, blockEnd(next), next->size, block->start,
						blockEnd(block), block->size, listName);
					return (status = ERR_BADDATA);
				}

				if (blockEnd(block) >= next->start)
				{
					error("Current block %08x->%08x (%u) end overlaps next "
						"block %08x->%08x (%u) in %s list", block->start,
						blockEnd(block), block->size, next->start,
						blockEnd(next), next->size, listName);
					return (status = ERR_BADDATA);
				}
			}

			block = block->next;
		}
	}

	return (status = 0);
}
#endif // defined(DEBUG)


/////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////
//
//  Below here, the functions are exported for external use
//
/////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////


void *_doMalloc(unsigned size, const char *function)
{
	// These are the guts of malloc() and kernelMalloc()

	int status = 0;
	void *address = NULL;

	debug("%s alloc %u", function, size);

	// If the requested block size is zero, forget it.  We can probably
	// assume something has gone wrong in the calling program
	if (!size)
	{
		error("Can't allocate zero bytes (%s)", function);
		errno = ERR_INVALID;
		return (address = NULL);
	}

	status = lock_get(&blocksLock);
	if (status < 0)
	{
		error("Can't get memory lock");
		errno = status;
		return (address = NULL);
	}

	// Find a free block big enough
	address = allocateBlock(size, function);

	#if defined(DEBUG)
	if (checkBlocks())
		while (1);
	#endif

	lock_release(&blocksLock);

	return (address);
}


void *_malloc(size_t size, const char *function)
{
	// User space wrapper for _doMalloc() so we can ensure kernel-space calls
	// use kernelMalloc()

	if (visopsys_in_kernel)
	{
		error("Cannot call malloc() directly from kernel space (%s)", function);
		return (NULL);
	}
	else
		return (_doMalloc(size, function));
}


void _doFree(void *start, const char *function)
{
	// These are the guts of free() and kernelFree()

	int status = 0;

	if (!start)
	{
		error("Can't free NULL pointer (%s)", function);
		errno = ERR_INVALID;
		return;
	}

	// Make sure we've been initialized
	if (!usedBlockList)
	{
		error("No memory allocated (%s)", function);
		errno = ERR_NOTINITIALIZED;
		return;
	}

	status = lock_get(&blocksLock);
	if (status < 0)
	{
		error("Can't get memory lock");
		errno = status;
		return;
	}

	status = deallocateBlock(start, function);

	#if defined(DEBUG)
	if (checkBlocks())
		while (1);
	#endif

	lock_release(&blocksLock);

	if (status < 0)
		errno = status;

	return;
}


void _free(void *start, const char *function)
{
	// User space wrapper for _doFree() so we can ensure kernel-space calls
	// use kernelFree()

	if (visopsys_in_kernel)
	{
		error("Cannot call free() directly from kernel space (%s)", function);
		return;
	}
	else
		return (_doFree(start, function));
}


int _mallocBlockInfo(void *start, memoryBlock *meBlock)
{
	// Try to find the block that starts at the supplied address and fill out
	// the structure with information about it.

	int status = 0;
	mallocBlock *maBlock = usedBlockList;

	// Check params
	if (!start || !meBlock)
		return (status = ERR_NULLPARAMETER);

	status = lock_get(&blocksLock);
	if (status < 0)
	{
		error("Can't get memory lock");
		return (errno = status);
	}

	// Loop through the used block list
	while (maBlock)
	{
		if (maBlock->start == (unsigned) start)
		{
			mallocBlock2MemoryBlock(maBlock, meBlock);
			lock_release(&blocksLock);
			return (status = 0);
		}

		maBlock = maBlock->next;
	}

	lock_release(&blocksLock);

	// Fell through -- no such block
	return (status = ERR_NOSUCHENTRY);
}


int _mallocGetStats(memoryStats *stats)
{
	// Return malloc memory usage statistics

	int status = 0;
	mallocBlock *block = usedBlockList;

	// Check params
	if (!stats)
	{
		error("Stats structure pointer is NULL");
		return (status = ERR_NULLPARAMETER);
	}

	status = lock_get(&blocksLock);
	if (status < 0)
	{
		error("Can't get memory lock");
		return (errno = status);
	}

	stats->totalBlocks = totalBlocks;
	stats->usedBlocks = 0;
	while (block)
	{
		stats->usedBlocks += 1;
		block = block->next;
	}
	stats->totalMemory = totalMemory;
	stats->usedMemory = usedMemory;

	lock_release(&blocksLock);

	return (status = 0);
}


int _mallocGetBlocks(memoryBlock *blocksArray, int doBlocks)
{
	// Fill a memoryBlock array with 'doBlocks' used malloc blocks information

	int status = 0;
	mallocBlock *block = usedBlockList;
	int count;

	// Check params
	if (!blocksArray)
	{
		error("Blocks array pointer is NULL");
		return (status = ERR_NULLPARAMETER);
	}

	status = lock_get(&blocksLock);
	if (status < 0)
	{
		error("Can't get memory lock");
		return (errno = status);
	}

	// Loop through the used block list
	for (count = 0; (block && (count < doBlocks)); count ++)
	{
		mallocBlock2MemoryBlock(block, &blocksArray[count]);
		block = block->next;
	}

	lock_release(&blocksLock);

	return (status = 0);
}


int _mallocCheck(void)
{
	int status = 0;

	status = lock_get(&blocksLock);
	if (status < 0)
	{
		error("Can't get memory lock");
		return (errno = status);
	}

	#if defined(DEBUG)
	status = checkBlocks();
	#endif

	lock_release(&blocksLock);

	if (status < 0)
		errno = status;

	return (status);
}

