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
//  kernelMalloc.c
//
	
// These routines comprise Visopsys' internal, kernel-only memory management
// system.  It relies upon the kernelMemory code, and does similar things,
// but instead of whole memory pages, it allocates arbitrary-sized chunks.

#include "kernelMalloc.h"
#include "kernelMemory.h"
#include "kernelParameters.h"
#include "kernelMultitasker.h"
#include "kernelMisc.h"
#include "kernelText.h"
#include "kernelError.h"
#include "kernelLock.h"
#include <stdio.h>
#include <string.h>

static kernelMallocBlock *blockList = NULL;
static kernelMallocBlock *firstUnusedBlock = NULL;
static volatile unsigned totalBlocks = 0;
static volatile unsigned usedBlocks = 0;
static volatile unsigned totalMemory = 0;
static volatile unsigned usedMemory = 0;

static const char *FUNCTION;
static lock locksLock;

#define blockSize(block) \
  (((unsigned) block->end - (unsigned) block->start) + 1)




static inline void insertBlock(kernelMallocBlock *firstBlock,
			       kernelMallocBlock *secondBlock)
{
  // Stick the first block in front of the second block
  firstBlock->previous = secondBlock->previous;
  firstBlock->next = (void *) secondBlock;
  if (secondBlock->previous)
    ((kernelMallocBlock *) secondBlock->previous)->next = (void *) firstBlock;
  secondBlock->previous = (void *) firstBlock;
}


static int sortInsertBlock(kernelMallocBlock *block)
{
  // Find the correct (sorted) place for it
  int status = 0;
  kernelMallocBlock *nextBlock = blockList;

  if (blockList == firstUnusedBlock)
    {
      insertBlock(block, firstUnusedBlock);
      blockList = block;
      return (status = 0);
    }
  
  while (1)
    {
      // This should never happen
      if (nextBlock == NULL)
	{
	  kernelError(kernel_error, "Unable to insert memory block %s %u->%u "
		      "(%u)", FUNCTION, block->start, block->end,
		      blockSize(block));
	  return (status = ERR_BADDATA);
	}

      if ((nextBlock->start > block->start) || (nextBlock == firstUnusedBlock))
	{
	  insertBlock(block, nextBlock);
	  if (nextBlock == blockList)
	    blockList = block;
	  return (status = 0);
	}
      
      nextBlock = (kernelMallocBlock *) nextBlock->next;
    }
}


static int growList(void)
{
  // This grows the block list by 1 memory page.  It should only be called
  // when the list is empty/full.

  int status = 0;
  kernelMallocBlock *newBlocks = NULL;
  int numBlocks = 0;
  int count;

  newBlocks = kernelMemoryGetSystem(MEMBLOCKSIZE, "kernel memory data");
  if (newBlocks == NULL)
    {
      kernelError(kernel_error, "Unable to allocate kernel memory");
      return (status = ERR_MEMORY);
    }

  // How many blocks is that?
  numBlocks = (MEMBLOCKSIZE / sizeof(kernelMallocBlock));

  // Initialize the pointers in our list of blocks
  for (count = 0; count < numBlocks; count ++)
    {
      if (count > 0)
	newBlocks[count].previous = (void *) &(newBlocks[count - 1]);
      if (count < (numBlocks - 1))
	newBlocks[count].next = (void *) &(newBlocks[count + 1]);
    }

  if (blockList == NULL)
    {
      blockList = newBlocks;
      firstUnusedBlock = newBlocks;
    }
  else
    {
      // Add our new stuff to the end of the existing list
      firstUnusedBlock->next = (void *) newBlocks;
      newBlocks[0].previous = (void *) firstUnusedBlock;
    }

  totalBlocks += numBlocks;

  return (status = 0);
}


static kernelMallocBlock *getBlock(void)
{
  kernelMallocBlock *block = NULL;
  kernelMallocBlock *previousBlock = NULL;
  kernelMallocBlock *nextBlock = NULL;

  // Do we have more than one free block?
  if ((firstUnusedBlock == NULL) || (firstUnusedBlock->next == NULL))
    {
      if (growList() < 0)
	return (block = NULL);
    }

  block = firstUnusedBlock;
  previousBlock = (kernelMallocBlock *) block->previous;
  nextBlock = (kernelMallocBlock *) block->next;

  // Remove it from its place in the list, linking its previous and next
  // blocks together.
  if (previousBlock)
    previousBlock->next = (void *) nextBlock;
  if (nextBlock)
    nextBlock->previous = (void *) previousBlock;

  firstUnusedBlock = nextBlock;
  if (block == blockList)
    blockList = nextBlock;

  // Clear it
  kernelMemClear((void *) block, sizeof(kernelMallocBlock));

  usedBlocks++;

  return (block);
}


static void releaseBlock(kernelMallocBlock *block)
{
  // This function gets called when a block is no longer needed.  We
  // zero out its fields and move it to the end of the used blocks.

  kernelMallocBlock *previousBlock = (kernelMallocBlock *) block->previous;
  kernelMallocBlock *nextBlock = (kernelMallocBlock *) block->next;

  // Temporarily remove it from the list, linking its previous and next
  // blocks together.
  if (previousBlock)
    previousBlock->next = (void *) nextBlock;
  if (nextBlock)
    {
      nextBlock->previous = (void *) previousBlock;

      if (block == blockList)
	blockList = nextBlock;
    }

  // Clear it
  kernelMemClear((void *) block, sizeof(kernelMallocBlock));

  // Stick it in front of the first unused block
  insertBlock(block, firstUnusedBlock);
  if (firstUnusedBlock == blockList)
    blockList = block;

  firstUnusedBlock = block;

  usedBlocks--;

  return;
}


static void mergeFree(kernelMallocBlock *block)
{
  // Merge any free blocks on either side of this one with this one

  kernelMallocBlock *previous = (kernelMallocBlock *) block->previous;
  kernelMallocBlock *next = (kernelMallocBlock *) block->next;

  if (previous)
    if ((previous->used == 0) && (previous->end == (block->start - 1)))
      {
	block->start = previous->start;
	releaseBlock(previous);
      }
  
  if (next)
    if ((next->used == 0) && (next->start == (block->end + 1)))
      {
	block->end = next->end;
	releaseBlock(next);
      }

  return;
}


static int addBlock(int used, void *start, void *end)
{
  // This puts the supplied data into our block list

  int status = 0;
  kernelMallocBlock *block = NULL;

  block = getBlock();
  if (block == NULL)
    return (status = ERR_NOFREE);

  block->used = used;
  block->start = start;
  block->end = end;

  status = sortInsertBlock(block);
  if (status < 0)
    return (status);

  if (used == 0)
    // If it's free, make sure it's merged with any other adjacent free
    // blocks on either side
    mergeFree(block);

  return (status = 0);
}


static int growHeap(unsigned minSize)
{
  // This grows the pool of heap memory by MEMORY_HEAP_MULTIPLE bytes.

  void *newHeap = NULL;

  if (minSize < MEMORY_HEAP_MULTIPLE)
    minSize = MEMORY_HEAP_MULTIPLE;

  // Get the heap memory
  newHeap = kernelMemoryGetSystem(minSize, "kernel memory");
  if (newHeap == NULL)
    {
      kernelError(kernel_error, "Unable to allocate kernel memory");
      return (ERR_MEMORY);
    }

  totalMemory += minSize;

  // Add it as a single free block
  return (addBlock(0 /* Free */, newHeap,
		   ((void *)((((unsigned) newHeap) + minSize) - 1)))); 
}


static kernelMallocBlock *findFree(unsigned size)
{
  kernelMallocBlock *block = blockList;
  // next 3 added by Davide Airaghi
  kernelMallocBlock *best_block = NULL;
  unsigned int delta = 0xffffffff;
  unsigned int block_size = 0;
  
  while (1)
    {
      if (block == NULL)
        break; // return (block);

      // store blocksize, next we'll use it not only once
      block_size = blockSize(block);

      // find the smallest free block, by Davide Airaghi
      // this method increases time required to get a free
      // block but could reduce memory fragmentation
      if ((block->used == 0) && (block_size >= size)) {
        if ((block_size - size) < delta) {
	    best_block = block;
	    delta = block_size - size;
	}
	// return (block);
      }
       
      block = (kernelMallocBlock *) block->next;
      
      if (block == firstUnusedBlock)
	break; // return (block = NULL);
    }
    
    return best_block;
}


static void *allocateBlock(unsigned size)
{
  // Find a block of unused memory, and return the start pointer.

  kernelMallocBlock *block = NULL;

  block = findFree(size);

  if (block == NULL)
    {
      // There is no block big enough to accommodate this.
      if (growHeap(size) < 0)
	return (NULL);

      block = findFree(size);
      if (block == NULL)
	{
	  // Something really wrong.
	  kernelError(kernel_error, "Unable to allocate block of size %u",
		      size);
	  return (NULL);
	}
    }

  block->used = 1;
  block->function = FUNCTION;
  block->process = kernelMultitaskerGetCurrentProcessId();

  // If part of this block will be unused, we will need to create a free
  // block for the remainder
  if (blockSize(block) > size)
    {
      if (addBlock(0 /* unused */, (block->start + size), block->end) < 0)
	return (NULL);
      block->end = ((block->start + size) - 1);
    }

  usedMemory += size;

  return (block->start);
}


static int deallocateBlock(void *start)
{
  // Find an allocated (used) block and deallocate it.

  int status = 0;
  kernelMallocBlock *block = blockList;

  while (1)
    {
      if (block == NULL)
	{
	  kernelError(kernel_error, "Block is NULL");
	  return (status = ERR_NODATA);
	}

      if (block->start == start)
	{
	  if (block->used == 0)
	    {
	      kernelError(kernel_error, "Block at %u is not allocated",
			  (unsigned) start);
	      return (status = ERR_ALREADY);
	    }
	  
	  // Clear out the memory
	  kernelMemClear(block->start, blockSize(block));

	  block->function = NULL;
	  block->process = 0;
	  block->used = 0;

	  usedMemory -= blockSize(block);

	  // Merge free blocks on either side of this one
	  mergeFree(block);
  
	  return (status = 0);
	}

      block = (kernelMallocBlock *) block->next;

      if (block == firstUnusedBlock)
	{
	  kernelError(kernel_error, "No such memory block %u to deallocate",
		      (unsigned) start);
	  return (status = ERR_NOSUCHENTRY);
	}
    }
}


/////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////
//
//  Below here, the functions are exported for external use
//
/////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////


void *_kernelMalloc(const char *function, unsigned size)
{
  // Just like a malloc(), for kernel memory, but the data is cleared like
  // calloc.

  int status = 0;
  void *address = NULL;

  status = kernelLockGet(&locksLock);
  if (status < 0)
    return (address = NULL);

  FUNCTION = function;

  // Make sure we do allocations on nice boundaries
  if (size % sizeof(int))
    size += (sizeof(int) - (size % sizeof(int)));

  // Make sure there's enough heap memory.  This will get called the first
  // time we're invoked, as totalMemory will be zero.
  while (size > (totalMemory - usedMemory))
    {
      status = growHeap(size);
      if (status < 0)
	return (address = NULL);
    }

  // Find a free block big enough
  address = allocateBlock(size);

  kernelLockRelease(&locksLock);

  return (address);
}


int _kernelFree(const char *function, void *start)
{
  // Just like free(), for kernel memory

  int status = 0;

  status = kernelLockGet(&locksLock);
  if (status < 0)
    return (status);

  FUNCTION = function;

  // Make sure we've been initialized
  if (!usedBlocks)
    return (status = ERR_NOSUCHENTRY);

  // The start address must be in kernel address space
  if (start < (void *) KERNEL_VIRTUAL_ADDRESS)
    {
      kernelError(kernel_error, "The kernel memory block to release is not "
		  "in the kernel's address space");
      return (status = ERR_INVALID);
    }

  status = deallocateBlock(start);

  kernelLockRelease(&locksLock);

  return (status);
}


int kernelMallocGetStats(memoryStats *stats)
{
  // Return kernelMalloc memory usage statistics
  
  int status = 0;

  // Check params
  if (stats == NULL)
    {
      kernelError(kernel_error, "Stats structure pointer is NULL");
      return (status = ERR_NULLPARAMETER);
    }

  stats->totalBlocks = totalBlocks;
  stats->usedBlocks = usedBlocks;
  stats->totalMemory = totalMemory;
  stats->usedMemory = usedMemory;
  return (status = 0);
}


int kernelMallocGetBlocks(memoryBlock *blocksArray, int doBlocks)
{
  // Fill a memoryBlock array with 'doBlocks' used kernelMalloc blocks
  // information
  
  int status = 0;
  kernelMallocBlock *block = NULL;
  int count;

  // Check params
  if (blocksArray == NULL)
    {
      kernelError(kernel_error, "Blocks array pointer is NULL");
      return (status = ERR_NULLPARAMETER);
    }

  // Loop through the block list
  for (count = 0, block = blockList;
       (block && (block != firstUnusedBlock) && (count < doBlocks));
       count ++)
    {
      blocksArray[count].processId = block->process;
      strncpy(blocksArray[count].description,
	      (block->used? block->function : "--free--"),
	      MEMORY_MAX_DESC_LENGTH);
      blocksArray[count].description[MEMORY_MAX_DESC_LENGTH - 1] = '\0';
      blocksArray[count].startLocation = (unsigned) block->start;
      blocksArray[count].endLocation = (unsigned) block->end;

      block = (kernelMallocBlock *) block->next;
    }
  
  return (status = 0);
}




