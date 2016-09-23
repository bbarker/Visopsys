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
//  kernelPage.c
//

// This file contains the C functions belonging to the kernel's 
// paging manager.  It keeps lists of page directories and page tables,
// and performs all the work of mapping and unmapping pages in the tables.

#include "kernelPage.h"
#include "kernelParameters.h"
#include "kernelMemory.h"
#include "kernelMultitasker.h"
#include "kernelProcessorX86.h"
#include "kernelLog.h"
#include "kernelMisc.h"
#include "kernelError.h"
#include <string.h>

// The kernel's page directory and first page table
static kernelPageDirectory *kernelPageDir = NULL;

// A list of all the page directories and page tables we've created, so we 
// can keep track of all the physical vs. virtual addresses of these.
static kernelPageDirectory pageDirMemory[MAX_PROCESSES];
static kernelPageDirectory *pageDirList[MAX_PROCESSES];
static volatile int numberPageDirectories = 0;
static kernelPageTable pageTableMemory[MAX_PROCESSES];
static kernelPageTable *pageTableList[MAX_PROCESSES];
static volatile int numberPageTables = 0;

// The physical memory location where we'll store the kernel's paging data.
static unsigned kernelPagingData = 0;

static volatile int initialized = 0;

// Macros used internally
#define getTableNumber(address) ((((unsigned) address) >> 22) & 0x000003FF)
#define getPageNumber(address) ((((unsigned) address) >> 12) & 0x000003FF)


static kernelPageTable *findPageTable(kernelPageDirectory *directory,
				      int tableNumber)
{
  kernelPageTable *table = NULL;
  int count;

  for (count = 0; count < numberPageTables; count ++)
    {
      if (pageTableList[count]->directory == directory)
	if (pageTableList[count]->tableNumber == tableNumber)
	  {
	    table = pageTableList[count];
	    break;
	  }
    }

  return (table);
}


static unsigned countFreePages(kernelPageDirectory *directory)
{
  // Returns the number of unallocated pages in all the page tables
  // of the supplied page directory

  kernelPageTable *table;
  int freePages = 0;
  int tableNumber = 0;
  int maxTableNumber = 0;

  if (directory == kernelPageDir)
    {
      tableNumber = getTableNumber(KERNEL_VIRTUAL_ADDRESS);
      maxTableNumber = PAGE_TABLES_PER_DIR;
    }
  else
    {
      tableNumber = 0;
      maxTableNumber = (getTableNumber(KERNEL_VIRTUAL_ADDRESS) - 1);
    }

  // Loop through all of the page tables
  for ( ; tableNumber < maxTableNumber; tableNumber ++)
    {
      table = findPageTable(directory, tableNumber);

      if (table)
	freePages += table->freePages;
    }

  return (freePages);
}


static int findFreeTableNumber(kernelPageDirectory *directory)
{
  // This returns the first unused page table number.

  int tableNumber = 0;
  int maxTableNumber = 0;
  
  if (directory == kernelPageDir)
    {
      tableNumber = getTableNumber(KERNEL_VIRTUAL_ADDRESS);
      maxTableNumber = PAGE_TABLES_PER_DIR;
    }
  else
    {
      tableNumber = 0;
      maxTableNumber = (getTableNumber(KERNEL_VIRTUAL_ADDRESS) - 1);
    }

  for ( ; tableNumber < maxTableNumber; tableNumber ++)
    if (findPageTable(directory, tableNumber) == NULL)
      return (tableNumber);

  return (tableNumber = -1);
}


static int findFreePages(kernelPageDirectory *directory, int pages,
			 void **virtualAddress)
{
  // This function will find a range of unused pages in the supplied
  // page directory that is as large as the number of pages requested.
  // Sets a pointer representing the virtual address of the free pages
  // on success, and returns 0.  On failure it returns negative.

  int status = 0;
  void *startAddress = NULL;
  int numberFree = 0;
  kernelPageTable *table = NULL;
  int tableNumber = 0;
  int maxTableNumber = 0;
  int pageNumber = 0;

  if (directory == kernelPageDir)
    {
      tableNumber = getTableNumber(KERNEL_VIRTUAL_ADDRESS);
      maxTableNumber = PAGE_TABLES_PER_DIR;
    }
  else
    {
      tableNumber = 0;
      maxTableNumber = (getTableNumber(KERNEL_VIRTUAL_ADDRESS) - 1);
    }

  // Loop through the supplied page directory.
  for ( ; tableNumber < maxTableNumber; tableNumber++)
    {
      // Get a pointer to this page table.
      table = findPageTable(directory, tableNumber);
      if (table == NULL)
	{
	  numberFree = 0;
	  startAddress = NULL;
	  continue;
	}

      // Loop through the pages in this page table.  If we find a free
      // page and numberFree is zero, set freeSpace to the corresponding
      // virtual address.  If we find a used page, we reset both numberFree
      // and freeStart to NULL.  If the table number is zero, skip the
      // first page
      for (pageNumber = (tableNumber == 0) ;
	   pageNumber < PAGE_PAGES_PER_TABLE; pageNumber++)
	{
	  if (table->virtual->page[pageNumber] == NULL)
	    {
	      if (numberFree == 0)
		startAddress =
		  (void *) ((tableNumber << 22) | (pageNumber << 12));
	      
	      numberFree++;

	      if (numberFree >= pages)
		{
		  *virtualAddress = startAddress;
		  return (status = 0);
		}
	    }
	  else
	    {
	      numberFree = 0;
	      startAddress = NULL;
	    }
	}
      // If we fall through here, we're moving on to the next page table. 
    }

  // If we fall through to here, we did not find enough free memory
  return (status = ERR_NOFREE);
}


static kernelPageTable *createPageTable(kernelPageDirectory *directory,
					int number, int kernel) // last param by Davide Airaghi
{
  // This function creates an empty page table and maps it into the
  // supplied page directory.

  int status = 0;
  kernelPageTablePhysicalMem *physicalAddr = NULL;
  kernelPageTableVirtualMem *virtualAddr = NULL;
  int kernelTableNumber = 0;
  int kernelPageNumber = 0;
  kernelPageTable *kernelTable = NULL;
  kernelPageTable *newTable = NULL;
  int count;

  // Allocate some physical memory for the page table
  physicalAddr = kernelMemoryGetPhysical(sizeof(kernelPageTablePhysicalMem),
					 MEMORY_PAGE_SIZE, "page table");
  if (physicalAddr == NULL)
    return (newTable = NULL);

  // Map it into the kernel's virtual address space.  We can't use the
  // map function because it is the one that calls this function (we
  // don't want to get into a loop) when page table space is low.

  // If the directory is not the kernel directory, we have to be careful
  // to make sure that there's always one more free page available
  // in the kernel's directory for its own next page table
  if ((directory != kernelPageDir) && (countFreePages(kernelPageDir) < 2))
    {
      // Recurse
      if (createPageTable(kernelPageDir,
			  findFreeTableNumber(kernelPageDir),kernel) == NULL) // modified by Davide Airaghi
	// This is probably trouble for the kernel.  We certainly don't care
	// about this user process
	return (newTable = NULL);
    }

  // Try to find 1 free page in kernel space for the table to occupy
  status = findFreePages(kernelPageDir, 1, (void **) &virtualAddr);
  if (status < 0)
    // Didn't find one
    return (newTable = NULL);

  // Get the kernel's kernelPageTable into which this new one will be mapped.
  kernelTableNumber = getTableNumber(virtualAddr);
  kernelPageNumber = getPageNumber(virtualAddr);
  kernelTable = findPageTable(kernelPageDir, kernelTableNumber);
  if (kernelTable == NULL)
    return (newTable = NULL);

  // Put the real address into the page table entry.  Set the global bit, the
  // writable bit, and the page present bit.
  kernelTable->virtual->page[kernelPageNumber] = (unsigned) physicalAddr;
  kernelTable->virtual->page[kernelPageNumber] |=
    (PAGEFLAG_GLOBAL | PAGEFLAG_WRITABLE | PAGEFLAG_PRESENT);
  kernelTable->freePages--;

  // Clear this memory block, since kernelMemoryGetPhysical can't do it for us
  kernelMemClear((void *) virtualAddr, sizeof(kernelPageTableVirtualMem));

  // Put our new table in the next available kernelPageTable slot of the
  // page table list, and increase the count of kernelPageTables
  newTable = pageTableList[numberPageTables++];
  kernelMemClear((void *) newTable, sizeof(kernelPageTable));

  // Fill in this page table
  newTable->directory = directory;
  newTable->tableNumber = number;
  newTable->freePages = PAGE_PAGES_PER_TABLE;
  newTable->physical = physicalAddr;
  newTable->virtual = virtualAddr;

  // Now we actually go into the page directory memory and add the
  // real page table to the requested slot number.  Always enable 
  // read/write and page-present
  directory->virtual->table[number] = (unsigned) newTable->physical;
  directory->virtual->table[number] |= (PAGEFLAG_WRITABLE | PAGEFLAG_PRESENT);

  // Set the 'user' bit, if this page table is not privileged
  if (directory->privilege != PRIVILEGE_SUPERVISOR || (kernel==0)) // last condition by Davide Airaghi
    directory->virtual->table[number] |= PAGEFLAG_USER;

  // A couple of extra things we do if this new page table belongs to the
  // kernel or one of its threads
  if (directory == kernelPageDir)
    {
      // Set the 'global' bit, so that if this is a Pentium Pro or better
      // processor, the page table won't be invalidated during a context
      // switch
      directory->virtual->table[number] |= PAGEFLAG_GLOBAL;

      // It needs to be 'shared' with all of the other real page directories.
      for (count = 0; count < numberPageDirectories; count ++)
	if (!(pageDirList[count]->parent))
	  pageDirList[count]->virtual->table[number] =
	    kernelPageDir->virtual->table[number];
    }

  // Return the table
  return (newTable);
}


static int deletePageTable(kernelPageDirectory *directory,
			   kernelPageTable *table)
{
  // This function is for the maintenance of our dynamic list of page table
  // pointers.  It will remove the supplied page table from the list and
  // deallocate the memory that was reserved for it.  Returns 0 on success,
  // negative otherwise.

  int status = 0;
  int kernelTableNumber = 0;
  int kernelPageNumber = 0;
  kernelPageTable *kernelTable = NULL;
  int listPosition = 0;
  int count;

  // First remove the table from the directory
  directory->virtual->table[table->tableNumber] = NULL;

  // If this page table belonged to the kernel or one of its threads, it
  // needs to be 'unshared' from all of the other real page directories.
  if (directory == kernelPageDir)
    for (count = 0; count < numberPageDirectories; count ++)
      if (!(pageDirList[count]->parent))
	pageDirList[count]->virtual->table[table->tableNumber] = NULL;

  // Unmap it from the kernel's virtual address space.  We can't use the
  // unmap function because it is the one that calls this function (we
  // don't want to get into a loop) when when a page table is empty

  // Get the kernel's kernelPageTable from which this new one will be
  // unmapped.
  kernelTableNumber = getTableNumber(table->virtual);
  kernelPageNumber = getPageNumber(table->virtual);
  kernelTable = findPageTable(kernelPageDir, kernelTableNumber);

  if (kernelTable == NULL)
    return (status = ERR_NOSUCHENTRY);

  // Erase the entry for the page of kernel memory that this table used
  kernelTable->virtual->page[kernelPageNumber] = NULL;
  kernelTable->freePages++;

  // Clear the TLB entry for the table's virtual memory
  kernelProcessorClearAddressCache(table->virtual);
  
  // Release the physical memory used by the table
  status = kernelMemoryReleasePhysical((void *) table->physical);
  if (status < 0)
    return (status = ERR_NOSUCHENTRY);
  
  // Now move the table to the unused list.  This list is the same as
  // several other lists in the kernel.  We remove this pointer from the
  // list by swapping its pointer in the list with that of the last item
  // in the list and decrementing the count (UNLESS: this is the last one,
  // or the only one).

  // Ok, now we need to find this page table in the list.
  for (listPosition = 0; listPosition < numberPageTables; )
    if (pageTableList[listPosition] == table)
      break;
    else
      listPosition++;

  if ((listPosition == numberPageTables) ||
      (pageTableList[listPosition] != table))
    return (status = ERR_NOSUCHENTRY);

  // Decrease the count of page tables BEFORE the following operation
  numberPageTables--;

  if ((numberPageTables > 0) && (listPosition < numberPageTables))
    {
      // Swap this item with the last item
      pageTableList[listPosition] = pageTableList[numberPageTables];
      pageTableList[numberPageTables] = table;
    }

  // Return success
  return (status = 0);
}


static int findPageTableEntry(kernelPageDirectory *directory,
			      void *virtualAddress, void **entry)
{
  // Given a page directory and a virtual address, this function will find
  // the appropriate page table entry and return it.  

  int status = 0;
  int tableNumber = 0;
  kernelPageTable *table = NULL;
  int pageNumber = 0;

  // virtualAddress is allowed to be NULL.

  if ((unsigned) virtualAddress % MEMORY_PAGE_SIZE)
    return (status = ERR_ALIGN);

  // Figure out which page table corresponds to this virtual address, and
  // get the page table
  tableNumber = getTableNumber(virtualAddress);
  pageNumber = getPageNumber(virtualAddress);
  table = findPageTable(directory, tableNumber);
  if (table == NULL)
    // We're hosed.  This table should already exist.
    return (status = ERR_NODATA);

  // Grab the value from the page table
  *entry = (void *) (table->virtual->page[pageNumber] & 0xFFFFF000);
  return (status = 0);
}


static inline int getNumPages(unsigned size)
{
  // Turn a size into a number of pages
  return ((size / MEMORY_PAGE_SIZE) + ((size % MEMORY_PAGE_SIZE) != 0));
}


static int areFreePagesAt(kernelPageDirectory *directory, int pages,
			  void *virtualAddress,int kernel) // lst param by Davide Airaghi
{
  // This function will return 1 if the requested number of pages are unused
  // at the requested address in the supplied page directory.

  int numberFree = 0;
  kernelPageTable *table = NULL;
  int tableNumber = 0;
  int maxTableNumber = 0;
  int pageNumber = 0;

  if (directory == kernelPageDir)
    maxTableNumber = PAGE_TABLES_PER_DIR;
  else
    maxTableNumber = (getTableNumber(KERNEL_VIRTUAL_ADDRESS) - 1);

  tableNumber = getTableNumber(virtualAddress);
  pageNumber = getPageNumber(virtualAddress);

  // Loop through the supplied page directory.
  for ( ; tableNumber < maxTableNumber; tableNumber++)
    {
      // Get a pointer to this page table.
      table = findPageTable(directory, tableNumber);
      if (table == NULL)
	{
	  // Create the page table
	  table = createPageTable(directory, tableNumber,kernel);
	  if (table == NULL)
	    return (0);
	}
      
      // Loop through the pages in this page table.  If we find a used
      // page before 'numberFree' equals 'pages', return 0
      for ( ; pageNumber < PAGE_PAGES_PER_TABLE; pageNumber++)
	{
	  if (table->virtual->page[pageNumber] != NULL)
	    return (0);

	  numberFree++;
	  if (numberFree >= pages)
	    return (1);
	}

      // If we fall through, we're moving on to the next page table.
      pageNumber = 0;
    }

  // If we fall through, we're out of range
  return (0);
}


static int map(kernelPageDirectory *directory, void *physicalAddress, 
	       void **virtualAddress, unsigned size, int flags, int kernel) // last by Davide Airaghi
{
  // This function is used by the rest of the kernel to map physical memory
  // pages in the address space of a process.  This will map the physical
  // memory to the first range of the process' unused pages that is large
  // enough to handle the request.  By default, it will make all pages that
  // it maps writable.

  int status = 0;
  kernelPageTable *pageTable = NULL;
  void *currentVirtualAddress = NULL;
  void *currentPhysicalAddress = NULL;
  int tableNumber = 0;
  unsigned pageNumber = 0;
  unsigned numPages = 0;
  
  // Make sure that our arguments are reasonable.  The wrapper functions
  // that are used to call us from external locations do not check them.

  if (size == 0)
    return (status = ERR_INVALID);

  // Make sure the pointer to virtualAddress is not NULL
  if (virtualAddress == NULL)
    return (status = ERR_NULLPARAMETER);

  if ((unsigned) physicalAddress % MEMORY_PAGE_SIZE)
    return (status = ERR_ALIGN);

  // Determine how many pages we need to map
  numPages = getNumPages(size);

  if (flags == PAGE_MAP_ANY)
    {
      // Are there enough free pages in this page directory (plus 1 for
      // the next page table)?  If not, add more page tables until we have
      // enough.
      while (((numPages + 1) >= countFreePages(directory)) ||
	     (findFreePages(directory, numPages, virtualAddress) < 0))
	{
	  if (createPageTable(directory,
			      findFreeTableNumber(directory),kernel) == NULL) //modified by Davide Airaghi
	    return (status = ERR_NOFREE);
	}
    }
  else if (flags == PAGE_MAP_EXACT)
    {
      if ((unsigned) *virtualAddress % MEMORY_PAGE_SIZE)
	return (status = ERR_ALIGN);

      if (!areFreePagesAt(directory, numPages, *virtualAddress,kernel)) // last by Davide Airaghi
	return (status = ERR_NOFREE);

      // Make sure there's enough for the next page table
      if ((numPages + 1) >= countFreePages(directory))
	{
	  if (createPageTable(directory,
			      findFreeTableNumber(directory),kernel) == NULL) // modified by Davide Airaghi
	    return (status = ERR_NOFREE);

	  if (!areFreePagesAt(directory, numPages, *virtualAddress,kernel)) // modified by Davide Airaghi
	    return (status = ERR_NOFREE);
	}
    }
  else
    return (status = ERR_INVALID);
    
  currentPhysicalAddress = physicalAddress;
  currentVirtualAddress = *virtualAddress;

  // Change the entries in the page table
  while (numPages > 0)
    {
      pageNumber = getPageNumber(currentVirtualAddress);

      if ((pageTable == NULL) || (pageNumber == 0))
	{
	  // Get the address of the page table.  Figure out the page table
	  // number based on the virtual address we're currently working with,
	  // and get the page table.
	  tableNumber = getTableNumber(currentVirtualAddress);

	  pageTable = findPageTable(directory, tableNumber);
	  if (pageTable == NULL)
	    // We're hosed.  This table should already exist.
	    return (status = ERR_NOSUCHENTRY);
	}

      // Put the real address into the page table entry.  Set the
      // writable bit and the page present bit.
      pageTable->virtual->page[pageNumber] =
	(unsigned) currentPhysicalAddress;
      pageTable->virtual->page[pageNumber] |=
	(PAGEFLAG_WRITABLE | PAGEFLAG_PRESENT);

      if (directory == kernelPageDir)
	// Set the 'global' bit, so that if this is a Pentium Pro or better
	// processor, the page table won't be invalidated during a context
	// switch
	pageTable->virtual->page[pageNumber] |= PAGEFLAG_GLOBAL;

      // Set the 'user' bit, if this page is not privileged
      if (directory->privilege != PRIVILEGE_SUPERVISOR || (kernel == 0)) // last cond by DAvide Airaghi
	pageTable->virtual->page[pageNumber] |= PAGEFLAG_USER;

      // Decrease the count of free pages
      pageTable->freePages--;

      // Increment the working memory addresses
      currentVirtualAddress += MEMORY_PAGE_SIZE;
      currentPhysicalAddress += MEMORY_PAGE_SIZE;

      // Decrement the number of pages left to map
      numPages--;

      // Loop again
    }

  // Return success
  return (status = 0);
}


static int unmap(kernelPageDirectory *directory, void *virtualAddress,
		 unsigned size)
{
  // This function is used by the rest of the kernel to unmap virtual memory
  // pages from the address space of a process.  

  int status = 0;
  kernelPageTable *pageTable = NULL;
  unsigned tableNumber = 0;
  unsigned pageNumber = 0;
  unsigned numPages = 0;

  // Make sure that our arguments are reasonable.  The wrapper functions
  // that are used to call us from external locations do not check them.

  if (size == 0)
    return (status = ERR_INVALID);

  if (((unsigned) virtualAddress % MEMORY_PAGE_SIZE) != 0)
    return (status = ERR_ALIGN);

  // Determine how many pages we need to unmap
  numPages = getNumPages(size);

  // Change the entries in the page table
  while (numPages > 0)
    {
      pageNumber = getPageNumber(virtualAddress);

      if ((pageTable == NULL) || (pageNumber == 0))
	{
	  // Get the address of the page table.  Figure out the page table
	  // number based on the virtual address we're currently working with,
	  // and get the page table.
	  tableNumber = getTableNumber(virtualAddress);

	  pageTable = findPageTable(directory, tableNumber);
	  if (pageTable == NULL)
	    // We're hosed.  This table should already exist.
	    return (status = ERR_NOSUCHENTRY);
	}

      // Clear out the physical address from the page table entry
      pageTable->virtual->page[pageNumber] = NULL;

      // Clear the TLB entry for this page
      kernelProcessorClearAddressCache(virtualAddress);

      // Increase the count of free pages
      pageTable->freePages++;

      // Is the table now unused?
      if (pageTable->freePages == PAGE_PAGES_PER_TABLE)
	// Try to deallocate it
	deletePageTable(directory, pageTable);

      // Increment the working memory addresses
      virtualAddress += MEMORY_PAGE_SIZE;

      // Decrement the number of pages left to map
      numPages--;

      // Loop again
    }

  // Return success
  return (status = 0);
}


static kernelPageDirectory *createPageDirectory(int processId, int privilege, int kernel) // last by Davide Airaghi
{
  // This function creates an empty page directory by allocating physical
  // memory for it, and on success, returns a pointer to a 
  // kernelPageDirectory holding information about the directory.  Returns
  // NULL on error.

  int status = 0;
  kernelPageDirectory *directory = NULL;
  kernelPageDirPhysicalMem *physicalAddr = NULL;
  kernelPageDirVirtualMem *virtualAddr = NULL;

  // Get some physical memory for the page directory
  physicalAddr = (kernelPageDirPhysicalMem *) 
    kernelMemoryGetPhysical(sizeof(kernelPageDirPhysicalMem), MEMORY_PAGE_SIZE,
			    "page directory");
  if (physicalAddr == NULL)
    return (directory = NULL);

  // Map it into the kernel's virtual address space.
  status = map(kernelPageDir, (void *) physicalAddr, (void **) &virtualAddr,
	       sizeof(kernelPageDirPhysicalMem), PAGE_MAP_ANY, kernel); // last by Davide Airaghi
  if (status < 0)
    return (directory = NULL);

  // Clear this memory block, since kernelMemoryGetPhysical can't do it for us
  kernelMemClear((void *) virtualAddr, sizeof(kernelPageDirPhysicalMem));

  // Put it in the next available kernelPageDirectory slot, and increase
  // the count of kernelPageDirectories
  directory = pageDirList[numberPageDirectories++];
  kernelMemClear((void *) directory, sizeof(kernelPageDirectory));

  // Fill in this page directory
  directory->processId = processId;
  directory->numberShares = 0;
  directory->parent = 0;
  // next by Davide Airaghi
  directory->kernel = kernel;
  directory->privilege = privilege;
  directory->physical = physicalAddr;
  directory->virtual = virtualAddr;

  // Return the directory
  return (directory);
}


static kernelPageDirectory *findPageDirectory(int processId)
{
  // This function just finds the page directory structure that belongs
  // to the requested process.  Returns NULL on failure.

  kernelPageDirectory *dir = NULL;
  int count;

  if (processId == KERNELPROCID)
    return (dir = kernelPageDir);

  for (count = 0; count < numberPageDirectories; count ++)
    if (pageDirList[count]->processId == processId)
      {
	// If this page directory is 'shared' from another page directory,
	// then we need to recurse to find the parent (since this one will,
	// essentially, be 'empty')
	
	if (pageDirList[count]->parent)
	  dir = findPageDirectory(pageDirList[count]->parent);
	else
	  dir = pageDirList[count];

	break;
      }

  return (dir);
}


static int deletePageDirectory(kernelPageDirectory *directory)
{
  // This function is for the maintenance of our dynamic list of
  // page directory pointers.  It will remove the supplied page
  // directory from the list and deallocate the memory that was
  // reserved for the directory.  Returns 0 on success, negative
  // otherwise.

  int status = 0;
  kernelPageDirectory *parent = NULL;
  int listPosition = 0;

  // Make sure this page directory isn't currently shared.  If it is, 
  // we can't delete it.
  if (directory->numberShares)
    return (status = ERR_BUSY);

  // Figure out whether this page directory is 'sharing' from another
  // page directory.  If so, we don't need to do much other than remove
  // it from the list, and decrement the refcount in the parent
  if (directory->parent)
    {
      // Find the parent page directory
      parent = findPageDirectory(directory->parent);
      if (parent == NULL)
	return (status = ERR_NOSUCHENTRY);

      parent->numberShares--;
    }
  else
    {
      // It's a 'real' page table.  Deallocate the dynamic memory that
      // this directory is occupying
      kernelMemoryReleasePhysical((void *) directory->physical);

      // Unmap the directory from kernel memory
      status = unmap(kernelPageDir, (void *) directory->virtual,
		     sizeof(kernelPageDirVirtualMem));
      if (status < 0)
	return (status);
    }

  // Now we need to remove it from the list.  First find its position
  // in the list.
  for (listPosition = 0; listPosition < numberPageDirectories; )
    if (pageDirList[listPosition] == directory)
      break;
    else
      listPosition++;

  if ((listPosition == numberPageDirectories) || 
      (pageDirList[listPosition] != directory))
    return (status = ERR_NOSUCHENTRY);

  // This list is the same as several other lists in the kernel.  We remove
  // this pointer from the list by swapping its pointer in the list with
  // that of the last item in the list and decrementing the count
  // (UNLESS: this is the last one, or the only one).

  // Decrement the count of page directories BEFORE the following operation
  numberPageDirectories--;

  if ((numberPageDirectories > 0) && (listPosition < numberPageDirectories))
    {
      // Swap this item with the last item
      pageDirList[listPosition] = pageDirList[numberPageDirectories];
      pageDirList[numberPageDirectories] = directory;
    }

  // Return success
  return (status = 0);
}


static int firstPageDirectory(void)
{
  // This will create the first page directory, specifically for the
  // kernel.  This presents some special problems, since we don't want
  // to map it into the current, temporary page directory set up by
  // the loader.  We have to do this one manually.  Returns a pointer
  // to the directory on success, NULL on failure.

  int status = 0;

  // Make it occupy the first spot.
  kernelPageDir = pageDirList[numberPageDirectories++];

  // Get some physical memory for the page directory.  The physical
  // address we use for this is static, and is defined in kernelParameters.h 

  if ((kernelPagingData % MEMORY_PAGE_SIZE) != 0)
    return (status = ERR_ALIGN);

  kernelPageDir->physical = (kernelPageDirPhysicalMem *) kernelPagingData;

  // Make the virtual address be physical for now
  kernelPageDir->virtual = kernelPageDir->physical;

  // Clear the physical memory
  kernelMemClear((void *) kernelPageDir->physical,
		 sizeof(kernelPageDirPhysicalMem));

  kernelPageDir->processId = KERNELPROCID;
  kernelPageDir->numberShares = 0;
  kernelPageDir->parent = 0;
  kernelPageDir->privilege = PRIVILEGE_SUPERVISOR;

  return (status = 0);
}


static int firstPageTable(void)
{
  // This will create the first page table, specifically for the
  // kernel.  Just as like the first page directory, we don't want to map
  // it into the current, temporary page directory set up by the loader.
  // We have to do this one manually.  Returns a pointer to the first table
  // on success, NULL on failure.

  int status = 0;
  int tableNumber = 0;
  kernelPageTable *table = NULL;

  // Assign the page table to a slot
  table = pageTableList[numberPageTables++];

  // Get some physical memory for the page table.  The base physical
  // address we use for this is static, and is defined in 
  // kernelParameters.h 

  if ((kernelPagingData % MEMORY_PAGE_SIZE) != 0)
    return (status = ERR_ALIGN);

  tableNumber = getTableNumber(KERNEL_VIRTUAL_ADDRESS);

  table->directory = kernelPageDir;
  table->tableNumber = tableNumber;
  table->freePages = PAGE_PAGES_PER_TABLE;

  table->physical = (kernelPageTablePhysicalMem *) 
    (kernelPagingData + sizeof(kernelPageDirPhysicalMem));

  // Make the virtual address be physical, for now
  table->virtual = table->physical;

  // Clear the physical memory
  kernelMemClear((void *) table->physical,
		 sizeof(kernelPageTablePhysicalMem));

  // Now we actually go into the page directory memory and add the
  // real page table to the requested slot number.

  // Write the page table entry into the kernel's page directory.
  // Enable read/write and page-present
  kernelPageDir->physical->table[tableNumber] = (unsigned) table->physical;
  kernelPageDir->physical->table[tableNumber] |=
    (PAGEFLAG_WRITABLE | PAGEFLAG_PRESENT);

  return (status = 0);
}


static int kernelPaging(unsigned kernelMemory)
{
  // This function will reinitialize the paging environment at kernel
  // startup.  This needs to be handled differently than when regular
  // processes are created.
  
  int status = 0;
  kernelPageDirPhysicalMem *oldPageDirectory = NULL;
  kernelPageTablePhysicalMem *oldPageTable = NULL;
  kernelPageTable *newPageTable = NULL;
  int tableNumber = 0;
  int pageNumber = 0;
  void *kernelAddress;

  // Interrupts should currently be disabled at this point.
  kernelProcessorSuspendInts(status);

  // The kernel is currently located at kernelVirtualAddress (virtually).
  // We need to locate the current, temporary page directory, then the page
  // table that corresponds to kernelVirtualAddress.  From there, we need
  // to copy the contents of the page table(s) until all of the kernel's
  // current memory set has been remapped.

  // Get the address of the old page directory.  In this special instance,
  // the physical address we get back from this call can be used like 
  // a virtual address, since the lower part of memory should presently
  // be identity-mapped.
  kernelProcessorGetCR3(oldPageDirectory);
  oldPageDirectory = (kernelPageDirPhysicalMem *) 
    ((unsigned) oldPageDirectory & 0xFFFFF800);

  // The index of the page table can be determined from the virtual
  // address of the kernel.  This will be the same value we use in our
  // new page directory.
  tableNumber = getTableNumber(KERNEL_VIRTUAL_ADDRESS);

  // Get the old page table address.  The number of the old page table
  // can be used as an index into the old page directory.  We mask out
  // the lower 12 bits of the value we find at that index, and voila, we
  // have a pointer to the old page table.  Again, we could normally
  // not use this for much since it's a physical address, but again,
  // this time it's also a virtual address.
  oldPageTable = (kernelPageTablePhysicalMem *) 
    (oldPageDirectory->table[tableNumber] & 0xFFFFF000);

  if (oldPageTable == NULL)
    return (status = ERR_NOTINITIALIZED);

  // Create a new page directory for the kernel.  We have to do this
  // differently, as opposed to calling createPageDirectory(), since this
  // should not be "mapped" into the current, temporary page directory.
  status = firstPageDirectory();
  if (status < 0)
    return (status = ERR_NOTINITIALIZED);

  // Create a new, initial page table for the kernel.  Again, we have to
  // do this manually, as opposed to calling createPageTable(), since
  // this should also not be mapped into the current, temporary page
  // directory.
  status = firstPageTable();
  if (status < 0)
    return (status = ERR_NOTINITIALIZED);

  // The index of the first page in the page table can also be determined
  // from the virtual address of the kernel.  This will be the same value
  // we use to start our new page tables.
  pageNumber = getPageNumber(KERNEL_VIRTUAL_ADDRESS);

  // Copy the RELEVANT contents of the old page table into the new
  // page table.  This suggests that some of the data in the old page
  // table might be irrelevant.  That would be correct.  You see, the 
  // loader might (presently DOES) map pages gratuitously, irrespective
  // of how many pages the kernel actually uses.  We will only copy the
  // pages that the kernel uses, based on the kernelSize.  The following
  // code needs to assume that the kernel does not cross a 4-Mb boundary.
  
  newPageTable = findPageTable(kernelPageDir, tableNumber);
  if (newPageTable == NULL)
    return (status = ERR_NOTINITIALIZED);

  // Map the kernel memory into the existing page directory and page
  // table
  kernelAddress = (void *) KERNEL_VIRTUAL_ADDRESS;

  // Map the kernel itself at the prescribed virtual address
  status = map(kernelPageDir, (void *) KERNEL_LOAD_ADDRESS, &kernelAddress,
	       kernelMemory, PAGE_MAP_EXACT, 1); // last by Davide Airaghi
  if (status < 0)
    return (status);

  status = map(kernelPageDir, (void *) kernelPageDir->physical,
	       (void **) &(kernelPageDir->virtual),
	       sizeof(kernelPageDirPhysicalMem), PAGE_MAP_ANY, 1); // last by Davide Airaghi
  if (status < 0)
    return (status);

  status = map(kernelPageDir, (void *) newPageTable->physical,
	       (void **) &(newPageTable->virtual),
	       sizeof(kernelPageTablePhysicalMem), PAGE_MAP_ANY, 1 ); // last by Davide Airaghi
  if (status < 0)
    return (status);

  // Now we should be able to switch the processor to our new page 
  // directory and table(s).

  kernelProcessorSetCR3((unsigned) kernelPageDir->physical);

  // Return success
  return (status = 0);
}


static void shareKernelPages(kernelPageDirectory *directory)
{
  // This routine will put pointers to the kernel's page tables into the
  // supplied page directory.  This effectively puts the kernel "into
  // the virtual address space" of the process that owns the directory.

  int kernelStartingTable = 0;
  int count;

  // Determine the starting page table of the kernel's address space
  kernelStartingTable = getTableNumber(KERNEL_VIRTUAL_ADDRESS);

  // We will do a loop, copying the table entries from the kernel's
  // page directory to the target page directory.
  for (count = kernelStartingTable; count < PAGE_TABLES_PER_DIR; count ++)
    directory->virtual->table[count] = kernelPageDir->virtual->table[count];

  // Return
  return;
}


static int setPageAttrs(kernelPageDirectory *directory, int set,
			unsigned char flags, void *virtualAddress, int pages)
{
  // This allows the setting/clearing of page attributes

  int status = 0;
  kernelPageTable *pageTable = NULL;
  int pageNumber = 0;

  while (pages > 0)
    {
      pageTable = findPageTable(directory, getTableNumber(virtualAddress));
      if (pageTable == NULL)
	return (status = ERR_NOSUCHENTRY);

      pageNumber = getPageNumber(virtualAddress);

      for ( ; (pages > 0) && (pageNumber < PAGE_PAGES_PER_TABLE);
	    pageNumber ++)
	{
	  if (!pageTable->virtual->page[pageNumber])
	    {
	      kernelError(kernel_error, "Virtual address %08x is not mapped",
			  virtualAddress);
	      return (status = ERR_NODATA);
	    }

	  if (set)
	    pageTable->virtual->page[pageNumber] |= (flags & 0x0FFF);
	  else
	    pageTable->virtual->page[pageNumber] &= ~(flags & 0x0FFF);

	  virtualAddress += MEMORY_PAGE_SIZE;
	  pages -= 1;
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


int kernelPageInitialize(unsigned kernelMemory)
{
  // This function will initialize the page manager and call the kernelPaging
  // routine to create a set of new page tables for the kernel environment.
  // (This is based on the assumptions that paging has been enabled prior
  // to the kernel starting, and that there must be an existing set of
  // basic page tables created by the loader).  Returns 0 on success, 
  // negative on error.

  int status = 0;
  int count;

  // Clear out the memory we'll use to keep track of all the page 
  // directories and page tables, and set both counters to zero.
  kernelMemClear((void *) pageDirMemory, 
			(sizeof(kernelPageDirectory) * MAX_PROCESSES));
  kernelMemClear((void *) pageTableMemory, 
			(sizeof(kernelPageTable) * MAX_PROCESSES));

  // Loop through both of the dynamic lists that we'll use to keep 
  // pointers to the memory space we just reserved
  for (count = 0; count < MAX_PROCESSES; count ++)
    {
      pageDirList[count] = &pageDirMemory[count];
      pageTableList[count] = &pageTableMemory[count];
    }

  numberPageDirectories = 0;
  numberPageTables = 0;

  // Calculate the physical memory location where we'll store the kernel's
  // paging data.
  kernelPagingData = (KERNEL_LOAD_ADDRESS + kernelMemory);

  // Initialize the kernel's paging environment, which is done differently
  // than for a normal process.
  status = kernelPaging(kernelMemory);
  if (status < 0)
    return (status);

  // Make note that we're initialized
  initialized = 1;

  // Return success
  return (status = 0);
}


void *kernelPageGetDirectory(int processId)
{
  // This is an accessor function, which just returns the physical address of
  // the requested page directory (suitable for putting in the CR3 register).
  // Returns NULL on failure

  int status = 0;
  kernelPageDirectory *directory = NULL;
  void *physicalAddress = NULL;

  // Have we been initialized?
  if (!initialized)
    return (NULL);

  // Find the appropriate page directory
  directory = findPageDirectory(processId);
  if (directory == NULL)
    return (NULL);

  status = kernelLockGet(&(directory->dirLock));
  if (status < 0)
    return (physicalAddress = NULL);
  
  physicalAddress = (void *) directory->physical;

  kernelLockRelease(&(directory->dirLock));
  return (physicalAddress);
}


void *kernelPageNewDirectory(int processId, int privilege)
{
  // This function will create a new page directory and one page table for
  // a new process.

  int status = 0;
  kernelPageDirectory *directory = NULL;
  kernelPageTable *table = NULL;
  void *physicalAddress = NULL;
  // next by Davide Airaghi
  int kernel = 0;
  
  // Have we been initialized?
  if (!initialized)
    return (physicalAddress = NULL);

  // Make sure privilege is legal
  if ((privilege != PRIVILEGE_USER) && (privilege != PRIVILEGE_SUPERVISOR)) 
    return (physicalAddress = NULL);
  
  // by Davide Airaghi
  if (privilege != PRIVILEGE_SUPERVISOR && kernel)
    return (physicalAddress = NULL); // normal user can't be at ring0!
    
  // next 5 lines by Davide Airaghi
  kernel = kernelMultitaskerIsLowLevelProcess(processId);
  if (kernel < 0)
    kernel = 0;
  else
    kernel = 1;
    
  // Create a page directory for the process
  directory = createPageDirectory(processId, privilege, kernel); // last by Davide Airaghi
  if (directory == NULL)
    return (physicalAddress = NULL);

  status = kernelLockGet(&(directory->dirLock));
  if (status < 0)
    return (physicalAddress = NULL);
  
  // Create an initial page table in the page directory, in the first spot
  table = createPageTable(directory, 0, kernel ); // slot 0, last by Davide Airaghi
  if (table == NULL)
    {
      // Deallocate the page directory we created.  Don't unlock it since
      // it's going away
      deletePageDirectory(directory);
      return (physicalAddress = NULL);
    }
  
  // Finally, we need to map the kernel's address space into that of this
  // new process.  The process will not receive copies of the kernel's
  // page tables.  It will only get mappings in its page directory.
  shareKernelPages(directory);

  physicalAddress = (void *) directory->physical;

  kernelLockRelease(&(directory->dirLock));

  // Return the physical address of the new page directory.
  return (physicalAddress);
}


void *kernelPageShareDirectory(int parentId, int childId)
{
  // This function will allow a new process thread to share the page
  // directory of its parent.

  int status = 0;
  kernelPageDirectory *parentDirectory = NULL;
  kernelPageDirectory *childDirectory = NULL;
  void *physicalAddress = NULL;

  // Have we been initialized?
  if (!initialized)
    return (physicalAddress = NULL);

  // Find the page directory belonging to the parent process
  parentDirectory = findPageDirectory(parentId);
  if (parentDirectory == NULL)
    return (physicalAddress = NULL);

  status = kernelLockGet(&(parentDirectory->dirLock));
  if (status < 0)
    return (physicalAddress = NULL);
  
  // It could happen that the parentId and childId are the same.  (really?)
  if (parentId != childId)
    {
      // Ok.  Make room for a new page directory structure for the child.
      // We do this manually (i.e. without calling createPageDirectory())
      // because this shared page directory needs no real memory allocated
      // to it.
      childDirectory = pageDirList[numberPageDirectories++];

      // Clear the child directory.
      kernelMemClear((void *) childDirectory, sizeof(kernelPageDirectory));

      // Set the process Id of the child's directory
      childDirectory->processId = childId;

      // Note that the parent directory is referenced and child directory
      // is shared.
      parentDirectory->numberShares++;
      childDirectory->parent = parentDirectory->processId;
    }

  physicalAddress = (void *) parentDirectory->physical;

  kernelLockRelease(&(parentDirectory->dirLock));

  // Return the physical address of the shared page directory.
  return (physicalAddress);
}


int kernelPageDeleteDirectory(int processId)
{
  // This will delete a page directory and all of its assoctiated (unshared)
  // page tables.

  int status = 0;
  kernelPageDirectory *directory = NULL;
  kernelPageTable *table = NULL;
  int count;
  
  // Have we been initialized?
  if (!initialized)
    return (status = ERR_NOTINITIALIZED);

  // Find the page directory belonging to the process.  We can't use the
  // findPageDirectory() function to find it since THAT will always
  // return the PARENT page directory if a page directory is shared.  We
  // don't want that to happen, so we'll search through the list manually.
  for (count = 0; count < numberPageDirectories; count ++)
    if (pageDirList[count]->processId == processId)
      {
	directory = pageDirList[count];
	break;
      }

  if (directory == NULL)
    return (status = ERR_NOSUCHENTRY);

  status = kernelLockGet(&(directory->dirLock));
  if (status < 0)
    return (status = ERR_NOLOCK);
  
  // Ok, found it.  We need to walk through all of its page tables,
  // deallocating them as we go.
  for (count = 0; count < PAGE_PAGES_PER_TABLE; count ++)
    {
      table = findPageTable(directory, count);

      if (table)
	{
	  status = deletePageTable(directory, table);
	  if (status < 0)
	    {
	      kernelLockRelease(&(directory->dirLock));
	      return (status);
	    }
	}
    }

  // Delete the directory.
  status = deletePageDirectory(directory);
  
  // Return success
  return (status = 0);
}


int kernelPageMap(int processId, void *physicalAddress, void *virtualAddress,
		  unsigned size)
{
  // This is a publicly accessible wrapper function for the map() function.
  // It maps physical pages into an address space, at the specified virtual
  // address.  Parameter checking is done inside the map() function, not here.

  int status = 0;
  // next by Davide Airaghi
  int kernel = 0;
  
  kernelPageDirectory *directory = NULL;

  // Have we been initialized?
  if (!initialized)
    return (status = ERR_NOTINITIALIZED);

  // Find the appropriate page directory
  directory = findPageDirectory(processId);
  if (directory == NULL)
    return (status = ERR_NOSUCHENTRY);

  // next 5 lines by Davide Airaghi
  kernel = kernelMultitaskerIsLowLevelProcess(processId);
  if (kernel < 0)
    kernel = 0;
  else
    kernel = 1;
    
  status = kernelLockGet(&(directory->dirLock));
  if (status < 0)
    return (status = ERR_NOLOCK);
  
  status =
    map(directory, physicalAddress, &virtualAddress, size, PAGE_MAP_EXACT,kernel);

  kernelLockRelease(&(directory->dirLock));
  return (status);
}


int kernelPageMapToFree(int processId, void *physicalAddress, 
			void **virtualAddress, unsigned size)
{
  // This is a publicly accessible wrapper function for the map() function.
  // It maps physical pages into an address space, at the first available
  // virtual address.  Parameter checking is done inside the map() function,
  // not here.

  int status = 0;
  kernelPageDirectory *directory = NULL;
  int kernel = 0;
  
  // Have we been initialized?
  if (!initialized)
    return (status = ERR_NOTINITIALIZED);

  // Find the appropriate page directory
  directory = findPageDirectory(processId);
  if (directory == NULL)
    return (status = ERR_NOSUCHENTRY);

  // next 5 lines by Davide Airaghi
  kernel = kernelMultitaskerIsLowLevelProcess(processId);
  if (kernel < 0)
    kernel = 0;
  else
    kernel = 1;
    
  status = kernelLockGet(&(directory->dirLock));
  if (status < 0)
    return (status = ERR_NOLOCK);
  
  status = map(directory, physicalAddress, virtualAddress, size, PAGE_MAP_ANY,kernel); // last by Davide Airaghi

  kernelLockRelease(&(directory->dirLock));
  return (status);
}


int kernelPageUnmap(int processId, void *virtualAddress, unsigned size)
{
  // This is a publicly accessible wrapper function for the map() function.
  // This one is used to remove mapped pages from an address space.  Parameter
  // checking is done inside the map() function, not here.

  int status = 0;
  kernelPageDirectory *directory = NULL;

  // Have we been initialized?
  if (!initialized)
    return (status = ERR_NOTINITIALIZED);

  // Find the appropriate page directory
  directory = findPageDirectory(processId);
  if (directory == NULL)
    return (status = ERR_NOSUCHENTRY);

  status = kernelLockGet(&(directory->dirLock));
  if (status < 0)
    return (status = ERR_NOLOCK);
  
  status = unmap(directory, virtualAddress, size);

  kernelLockRelease(&(directory->dirLock));
  return (status);
}


void *kernelPageGetPhysical(int processId, void *virtualAddress)
{
  // Let's get physical.  I wanna get physical.  Let me hear your body talk.
  // Return the physical address mapped to this virtual address.  The
  // virtualAddress parameter is allowed to be NULL.

  int status = 0;
  kernelPageDirectory *directory = NULL;
  void *address = NULL;

  // Have we been initialized?
  if (!initialized)
    return (address = NULL);

  // Find the appropriate page directory
  directory = findPageDirectory(processId);
  if (directory == NULL)
    return (address = NULL);

  status = kernelLockGet(&(directory->dirLock));
  if (status < 0)
    return (address = NULL);
  
  status =
    findPageTableEntry(directory, (void *) kernelPageRoundDown(virtualAddress),
		       &address);

  kernelLockRelease(&(directory->dirLock));

  if (status < 0)
    return (address = NULL);
  else
    return (address + ((unsigned) virtualAddress % MEMORY_PAGE_SIZE));
}


void *kernelPageFindFree(int processId, unsigned size)
{
  // Simply locate the virtual address of a range of free pages of the
  // requested size.

  int status = 0;
  kernelPageDirectory *directory = NULL;
  int pages = 0;
  void *address = NULL;

  // Have we been initialized?
  if (!initialized)
    return (address = NULL);
  
  // Find the appropriate page directory
  directory = findPageDirectory(processId);
  if (directory == NULL)
    return (address = NULL);

  // Calculate the desired number of pages
  pages = ((size / MEMORY_PAGE_SIZE) + ((size % MEMORY_PAGE_SIZE) != 0));

  status = kernelLockGet(&(directory->dirLock));
  if (status < 0)
    return (address = NULL);

  status = findFreePages(directory, pages, &address);

  kernelLockRelease(&(directory->dirLock));

  if (status < 0)
    return (address = NULL);
  else
    return (address);
}


int kernelPageSetAttrs(int processId, int set, unsigned char flags,
		       void *virtualAddress, unsigned size)
{
  // This is a wrapper for setPageAttrs() which allows the setting/clearing
  // of page attributes

  int status = 0;
  kernelPageDirectory *directory = NULL;
  int pages = 0;

  // Have we been initialized?
  if (!initialized)
    return (status = ERR_NOTINITIALIZED);
  
  // Find the appropriate page directory
  directory = findPageDirectory(processId);
  if (directory == NULL)
    return (status = ERR_NOSUCHENTRY);

  // Calculate the number of pages
  pages = ((size / MEMORY_PAGE_SIZE) + ((size % MEMORY_PAGE_SIZE) != 0));

  status = kernelLockGet(&(directory->dirLock));
  if (status < 0)
    return (status);

  status = setPageAttrs(directory, set, flags, virtualAddress, pages);

  kernelLockRelease(&(directory->dirLock));
  return (status);
}


#ifdef PAGE_DEBUG
void kernelPageTableDebug(int processId)
{
  kernelPageDirectory *directory = NULL;
  int numPages = 0;
  int numTables = 0;
  kernelPageTable *pageTable = NULL;
  void *tableAddress = NULL;
  void *pageAddress = NULL;
  void *pagePhysical = NULL;
  void *rangeStart = (void *) -1;
  void *rangePhysicalStart = 0;
  unsigned rangeSize = 0;
  memoryStats stats;
  memoryBlock *blocksArray = NULL;
  int count, tableCount, pageCount;

  // Have we been initialized?
  if (!initialized)
    return;
  
  // Find the appropriate page directory
  directory = findPageDirectory(processId);
  if (directory == NULL)
    {
      kernelError(kernel_error, "Page directory %d not found", processId);
      return;
    }
  
  numPages = (KERNEL_VIRTUAL_ADDRESS / MEMORY_PAGE_SIZE);
  numTables = (numPages / PAGE_TABLES_PER_DIR);

  kernelTextPrintLine("Directory %d:\nvirtStart->virtEnd = physStart->physEnd "
		      "(size)\n----------------", processId);
  rangeStart = (void *) -1;

  for (tableCount = 0; tableCount < numTables; tableCount ++)
    {
      pageTable = findPageTable(directory, tableCount);
      if (pageTable)
	{
	  tableAddress = (void *) 
	    (tableCount * PAGE_PAGES_PER_TABLE * MEMORY_PAGE_SIZE);

	  for (pageCount = 0; pageCount < PAGE_PAGES_PER_TABLE; pageCount ++)
	    {
	      pageAddress = (tableAddress + (pageCount * MEMORY_PAGE_SIZE));

	      if (pageTable->virtual->page[pageCount])
		{
		  pagePhysical = (void *)
		    (pageTable->virtual->page[pageCount] & 0xFFFFF000);

		  if (rangeStart == (void *) -1)
		    {
		      rangeStart = pageAddress;
		      rangePhysicalStart = pagePhysical;
		    }
		  else if (pageCount &&
			   ((void *)(pageTable->virtual->page[pageCount - 1] &
			     0xFFFFF000) != (pagePhysical - MEMORY_PAGE_SIZE)))
		    {
		      rangeSize = (pageAddress - rangeStart);
		      kernelTextPrintLine("%08x->%08x = %08x->%08x (%08x)",
					  rangeStart, (pageAddress - 1),
					  rangePhysicalStart,
					  (rangePhysicalStart + rangeSize - 1),
					  rangeSize);
		      rangeStart = pageAddress;
		      rangePhysicalStart = pagePhysical;
		    }
		}

	      else if (rangeStart != (void *) -1)
		{
		  rangeSize = (pageAddress - rangeStart);
		  kernelTextPrintLine("%08x->%08x = %08x->%08x (%08x)",
				      rangeStart, (pageAddress - 1),
				      rangePhysicalStart,
				      (rangePhysicalStart + rangeSize - 1),
				      rangeSize);
		  rangeStart = (void *) -1;
		}
	    }
	}
    }

  kernelTextPrintLine("----------------\nPhysical blocks:");

  kernelMemoryGetStats(&stats, 0);
  blocksArray = kernelMemoryGet((stats.usedBlocks * sizeof(memoryBlock)),
				"memory block list");
  kernelMemoryGetBlocks(blocksArray,
			(stats.usedBlocks * sizeof(memoryBlock)), 0);
  for (count = 0; count < (int) stats.usedBlocks; count ++)
    if (blocksArray[count].processId == processId)
      kernelTextPrintLine("proc=%d %08x->%08x (size %08x) %s",
			  blocksArray[count].processId,
			  blocksArray[count].startLocation,
			  blocksArray[count].endLocation,
			  (blocksArray[count].endLocation - 
			   blocksArray[count].startLocation + 1),
			  blocksArray[count].description);
  kernelMemoryRelease(blocksArray);
  kernelTextPrintLine("---------------- ...done");

  return;
}
#endif // PAGE_DEBUG
