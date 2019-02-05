//
//  Visopsys
//  Copyright (C) 1998-2006 J. Andrew McLaughlin
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
//  kernelDisk.c
//
	
// This file functions for disk access, and routines for managing the array
// of disks in the kernel's data structure for such things.  

#include "kernelDisk.h"
#include "kernelMain.h"
#include "kernelParameters.h"
#include "kernelFilesystem.h"
#include "kernelMalloc.h"
#include "kernelMemory.h"
#include "kernelMultitasker.h"
#include "kernelLock.h"
#include "kernelMisc.h"
#include "kernelSysTimer.h"
#include "kernelLog.h"
#include "kernelError.h"
#include <stdio.h>
#include <string.h>

static void diskd(void) __attribute__((noreturn));

// All the disks
static kernelPhysicalDisk *physicalDisks[DISK_MAXDEVICES];
static volatile int physicalDiskCounter = 0;
static kernelDisk *logicalDisks[DISK_MAXDEVICES];
static volatile int logicalDiskCounter = 0;

// The name of the disk we booted from
static char bootDisk[DISK_MAX_NAMELENGTH];

// Modes for the readWriteSectors routine
#define IOMODE_READ     0x01
#define IOMODE_WRITE    0x02
#define IOMODE_NOCACHE  0x04

// For the disk daemon
static int diskdPID = 0;

// This is a table for keeping known partition type codes and descriptions
static partitionType partitionTypes[] = {
  { 0x01, "FAT12"},
  { 0x02, "XENIX root"},
  { 0x03, "XENIX /usr"},
  { 0x04, "FAT16 (small)"},
  { 0x05, "Extended"},
  { 0x06, "FAT16"},
  { 0x07, "NTFS or HPFS"},
  { 0x08, "OS/2 or AIX boot"},
  { 0x09, "AIX data"},
  { 0x0A, "OS/2 Boot Manager"},
  { 0x0B, "FAT32"},
  { 0x0C, "FAT32 (LBA)"},
  { 0x0E, "FAT16 (LBA)"},
  { 0x0F, "Extended (LBA)"},
  { 0x11, "Hidden FAT12"},
  { 0x12, "FAT diagnostic"},
  { 0x14, "Hidden FAT16 (small)"},
  { 0x16, "Hidden FAT16"},
  { 0x17, "Hidden HPFS or NTFS"},
  { 0x1B, "Hidden FAT32"},
  { 0x1C, "Hidden FAT32 (LBA)"},
  { 0x1E, "Hidden FAT16 (LBA)"},
  { 0x35, "JFS" },
  { 0x39, "Plan 9" },
  { 0x3C, "PartitionMagic" },
  { 0x3D, "Hidden Netware" },
  { 0x4D, "QNX4.x" },
  { 0x4D, "QNX4.x 2nd" },
  { 0x4D, "QNX4.x 3rd" },
  { 0x52, "CP/M" },
  { 0x63, "GNU HURD"},
  { 0x64, "Netware 2"},
  { 0x65, "Netware 3/4"},
  { 0x80, "Minix"},
  { 0x81, "Linux or Minix"},
  { 0x82, "Linux swap or Solaris"},
  { 0x83, "Linux"},
  { 0x84, "Hibernation"},
  { 0x85, "Linux extended"},
  { 0x86, "HPFS or NTFS mirrored"},
  { 0x87, "HPFS or NTFS mirrored"},
  { 0x8E, "Linux LVM"},
  { 0x93, "Hidden Linux"},
  { 0x9F, "BSD/OS"},
  { 0xA0, "Hibernation"},
  { 0xA1, "Hibernation"},
  { 0xA5, "BSD, NetBSD, FreeBSD"},
  { 0xA6, "OpenBSD"},
  { 0xA7, "NeXTSTEP"},
  { 0xA8, "Darwin UFS"},
  { 0xA9, "NetBSD"},
  { 0xAB, "OS-X boot"},
  { 0xB7, "BSDI"},
  { 0xB8, "BSDI swap"},
  { 0xBE, "Solaris boot"},
  { 0xC1, "DR-DOS FAT12"},
  { 0xC4, "DR-DOS FAT16 (small)"},
  { 0xC5, "DR-DOS Extended"},
  { 0xC6, "DR-DOS FAT16"},
  { 0xC7, "HPFS mirrored"},
  { 0xCB, "DR-DOS FAT32"},
  { 0xCC, "DR-DOS FAT32 (LBA)"},
  { 0xCE, "DR-DOS FAT16 (LBA)"},
  { 0xEB, "BeOS BFS"},
  { 0xEE, "EFI GPT protective"},
  { 0xF2, "DOS 3.3+ second"},
  { 0xFA, "Bochs"},
  { 0xFB, "VmWare"},
  { 0xFC, "VmWare swap"},
  { 0xFD, "Linux RAID"},
  { 0xFE, "NT hidden or Veritas VM"},
  { 0xFF, "Veritas VM"},
  { 0, "" }
};

static int initialized = 0;

// Circular dependency here
static int readWriteSectors(kernelPhysicalDisk *, unsigned, unsigned, void *,
			    int);


#if (DISK_CACHE)
static int getDiskCache(kernelPhysicalDisk *physicalDisk)
{
  // This routine is called when a physical disk structure is first used
  // by the read/write function.  It initializes the cache memory and
  // control structures..

  int status = 0;
  unsigned count;

  if (!physicalDisk->cache.initialized)
    {
      // Get some memory for our array of disk cache sector metadata
      physicalDisk->cache.numSectors =
	(DISK_MAX_CACHE / physicalDisk->sectorSize);
  
      physicalDisk->cache.sectors =
	kernelMalloc(physicalDisk->cache.numSectors *
		     sizeof(kernelDiskCacheSector *));
      physicalDisk->cache.sectorMemory =
	kernelMalloc(physicalDisk->cache.numSectors *
		     sizeof(kernelDiskCacheSector));
      physicalDisk->cache.dataMemory = kernelMalloc(DISK_MAX_CACHE);
  
      if ((physicalDisk->cache.sectors == NULL) ||
	  (physicalDisk->cache.sectorMemory == NULL) ||
	  (physicalDisk->cache.dataMemory == NULL))
	{
	  kernelError(kernel_error, "Unable to get disk cache memory");
	  return (status = ERR_MEMORY);
	}

      // Initialize the cache structures
      for (count = 0; count < physicalDisk->cache.numSectors; count ++)
	{
	  // The pointers to the sector structures
	  physicalDisk->cache.sectors[count] =
	    &(physicalDisk->cache.sectorMemory[count]);

	  physicalDisk->cache.sectors[count]->number = -1;
	  physicalDisk->cache.sectors[count]->dirty = 0;
	  
	  // The data memory pointers in the sector structures
	  physicalDisk->cache.sectors[count]->data =
	    (physicalDisk->cache.dataMemory +
	     (count * physicalDisk->sectorSize));
	}

      physicalDisk->cache.initialized = 1;
    }

  // Return success
  return (status = 0);
}


static inline int findCachedSector(kernelPhysicalDisk *physicalDisk,
				   unsigned sectorNum)
{
  // Just loops through the cache and returns the index of a cached
  // sector, if found
  
  int status = 0;
  unsigned count;

  status = kernelLockGet(&(physicalDisk->cache.cacheLock));
  if (status < 0)
    return (status);

  for (count = 0; (count < physicalDisk->cache.usedSectors); count ++)
    if (physicalDisk->cache.sectors[count]->number == sectorNum)
      {
	kernelLockRelease(&(physicalDisk->cache.cacheLock));
	return (count);
      }
  kernelLockRelease(&(physicalDisk->cache.cacheLock));
  return (status = ERR_NOSUCHENTRY);
}


static unsigned countUncachedSectors(kernelPhysicalDisk *physicalDisk,
				     unsigned startSector,
				     unsigned sectorCount)
{
  // This function returns the number of consecutive uncached clusters
  // starting in the range supplied.  For example, if none of the sectors
  // are cached, the return value will be sectorCount.  Conversely, if
  // the first sector is cached, it will return 0.
  
  int status = 0;
  unsigned idx;

  status = kernelLockGet(&(physicalDisk->cache.cacheLock));
  if (status < 0)
    return (status);

  // Loop through the cache until we find a sector number that is >=
  // startSector
  for (idx = 0; idx < physicalDisk->cache.usedSectors; idx++)
    if (physicalDisk->cache.sectors[idx]->number >= startSector)
      {
	kernelLockRelease(&(physicalDisk->cache.cacheLock));

	// The sector number of this sector determines the value we return.
	if ((physicalDisk->cache.sectors[idx]->number - startSector) >
	    sectorCount)
	  return (sectorCount);
	else
	  return (physicalDisk->cache.sectors[idx]->number - startSector);
      }

  // There were no sectors with a >= number.  Return sectorCount.
  kernelLockRelease(&(physicalDisk->cache.cacheLock));
  return (sectorCount);
}


static int writeConsecutiveDirty(kernelPhysicalDisk *physicalDisk,
				 unsigned start)
{
  // Starting at 'start', write any consecutive dirty cache sectors and
  // return the number written.  NB: A lock on the cache must already be
  // held

  int status = 0;
  unsigned consecutive = 0;
  void *data = NULL;
  unsigned count;

  // Get a count of the consecutive dirty sectors
  for (count = start; count < physicalDisk->cache.usedSectors; count ++)
    {
      if (!(physicalDisk->cache.sectors[count]->dirty))
	break;

      consecutive += 1;
      
      if ((count == (physicalDisk->cache.usedSectors - 1)) ||
	  (physicalDisk->cache.sectors[count + 1]->number !=
	   (physicalDisk->cache.sectors[count]->number + 1)))
	break;
    }

  if (consecutive)
    {
      // Get a buffer to hold all the data
      data = kernelMalloc(consecutive * physicalDisk->sectorSize);
      if (data == NULL)
	return (status = ERR_MEMORY);

      // Copy the sectors' data into our buffer
      for (count = 0; count < consecutive; count ++)
	kernelMemCopy(physicalDisk->cache.sectors[start + count]->data,
		      (data + (count * physicalDisk->sectorSize)),
		      physicalDisk->sectorSize);

      // Write the data
      status = readWriteSectors(physicalDisk, physicalDisk->cache
				.sectors[start]->number, consecutive, data,
				(IOMODE_WRITE | IOMODE_NOCACHE));
      // Free the memory
      kernelFree(data);

      if (status < 0)
	return (status);

      // Mark the sectors as clean
      for (count = start; count < (start + consecutive); count ++)
	physicalDisk->cache.sectors[count]->dirty = 0;
    }

  return (consecutive);
}


static int cacheSync(kernelPhysicalDisk *physicalDisk)
{
  // Write all dirty cached sectors to the disk

  int status = 0;
  int errors = 0;
  unsigned count;

  if (!(physicalDisk->cache.dirty) || physicalDisk->readOnly)
    return (0);

  status = kernelLockGet(&(physicalDisk->cache.cacheLock));
  if (status < 0)
    return (status);

  for (count = 0; count < physicalDisk->cache.usedSectors; count ++)
    // If the disk sector is dirty, write it and any consecutive ones
    // after it that are dirty
    if (physicalDisk->cache.sectors[count]->dirty)
      {
	status = writeConsecutiveDirty(physicalDisk, count);
	if (status < 0)
	  errors = status;
	else
	  count += (status - 1);
      }

  // Reset the dirty flag for the whole cache
  if (!errors)
    physicalDisk->cache.dirty = 0;

  kernelLockRelease(&(physicalDisk->cache.cacheLock));
  return (errors);
}


static int cacheInvalidate(kernelPhysicalDisk *physicalDisk)
{
  // Evacuate the disk cache

  int status = 0;
  unsigned count;

  status = kernelLockGet(&(physicalDisk->cache.cacheLock));
  if (status < 0)
    return (status);

  for (count = 0; count < physicalDisk->cache.usedSectors; count ++)
    {
      physicalDisk->cache.sectors[count]->number = -1;
      physicalDisk->cache.sectors[count]->dirty = 0;
    }

  physicalDisk->cache.usedSectors = 0;
  physicalDisk->cache.dirty = 0;

  kernelLockRelease(&(physicalDisk->cache.cacheLock));
  return (status);
}


static int uncacheSectors(kernelPhysicalDisk *physicalDisk,
			  unsigned sectorCount)
{
  // Removes the least recently used sectors from the cache
  
  int status = 0;
  int errors = 0;
  kernelDiskCacheSector *tmpSector = NULL;
  unsigned count1, count2;

  status = kernelLockGet(&(physicalDisk->cache.cacheLock));
  if (status < 0)
    return (status);

  // If we're supposed to uncache everything, that's easy
  if (sectorCount == physicalDisk->cache.usedSectors)
    {
      kernelLockRelease(&(physicalDisk->cache.cacheLock));
      status = cacheSync(physicalDisk);
      for (count1 = 0; count1 < physicalDisk->cache.usedSectors; count1 ++)
	{
	  physicalDisk->cache.sectors[count1]->number = -1;
	  physicalDisk->cache.sectors[count1]->dirty = 0;
	}
      if (status == 0)
	physicalDisk->cache.usedSectors = 0;
      return (status);
    }

  // Bubble-sort it by age, most-recently-used first
  for (count1 = 0; count1 < physicalDisk->cache.usedSectors; count1 ++)
    for (count2 = 0; count2 < (physicalDisk->cache.usedSectors - 1); count2 ++)
      if (physicalDisk->cache.sectors[count2]->lastAccess <
	  physicalDisk->cache.sectors[count2 + 1]->lastAccess)
	{
	  tmpSector = physicalDisk->cache.sectors[count2 + 1];
	  physicalDisk->cache.sectors[count2 + 1] =
	    physicalDisk->cache.sectors[count2];
	  physicalDisk->cache.sectors[count2] = tmpSector;
	}

  // Now our list has the youngest sectors at the front.

  // Write any dirty sectors that we are discarding from the end
  for (count1 = (physicalDisk->cache.usedSectors - sectorCount);
       count1 < physicalDisk->cache.usedSectors; count1 ++)
    if (physicalDisk->cache.sectors[count1]->dirty)
      {
	status = writeConsecutiveDirty(physicalDisk, count1);
	if (status < 0)
	  errors = status;
	else
	  count1 += (status - 1);
      }

  for (count1 = (physicalDisk->cache.usedSectors - sectorCount);
       count1 < physicalDisk->cache.usedSectors; count1 ++)
    {
      physicalDisk->cache.sectors[count1]->number = -1;
      physicalDisk->cache.sectors[count1]->dirty = 0;
    }
 
  if (!errors)
    physicalDisk->cache.usedSectors -= sectorCount;

  // Bubble-sort the remaining ones again by sector number
  for (count1 = 0; count1 < physicalDisk->cache.usedSectors; count1 ++)
    for (count2 = 0; count2 < (physicalDisk->cache.usedSectors - 1); count2 ++)
      if (physicalDisk->cache.sectors[count2]->number >
	  physicalDisk->cache.sectors[count2 + 1]->number)
	{
	  tmpSector = physicalDisk->cache.sectors[count2 + 1];
	  physicalDisk->cache.sectors[count2 + 1] =
	    physicalDisk->cache.sectors[count2];
	  physicalDisk->cache.sectors[count2] = tmpSector;
	}

  kernelLockRelease(&(physicalDisk->cache.cacheLock));
  return (status = errors);
}


static int addCacheSectors(kernelPhysicalDisk *physicalDisk,
			   unsigned startSector, unsigned sectorCount,
			   void *data, int dirty)
{
  // This routine will add disk sectors to the cache.

  int status = 0;
  kernelDiskCacheSector *cacheSector = NULL;
  unsigned idx, count;

  // Only cache what will fit
  if (sectorCount > physicalDisk->cache.numSectors)
    sectorCount = physicalDisk->cache.numSectors;

  // Make sure the cache isn't full
  if ((physicalDisk->cache.usedSectors + sectorCount) >
      physicalDisk->cache.numSectors)
    {
      // Uncache some sectors
      status =
	uncacheSectors(physicalDisk,
		       ((physicalDisk->cache.usedSectors + sectorCount) -
			physicalDisk->cache.numSectors)); 
      if (status < 0)
	return (status);
    }

  // Make sure none of these sectors are already cached.  We could do some
  // clever things to take care of such a case, but no, we want to keep
  // it simple here.  It is the caller's responsibility to ensure that we
  // are not 're-caching' things.
  if (countUncachedSectors(physicalDisk, startSector, sectorCount) !=
      sectorCount)
    {
      kernelError(kernel_error, "Attempt to cache a range of disk sectors "
		  "(%u-%u) that are already (partially) cached", startSector,
		  (startSector + (sectorCount - 1)));
      return (status = ERR_ALREADY);
    }

  status = kernelLockGet(&(physicalDisk->cache.cacheLock));
  if (status < 0)
    return (status);

  // Find the spot in the cache where these should go.  That will be in the
  // spot where the next sector's number is > startSector
  if ((physicalDisk->cache.usedSectors == 0) ||
      (physicalDisk->cache.sectors[physicalDisk->cache.usedSectors - 1]
       ->number < startSector))
    // Put these new ones at the end
    idx = physicalDisk->cache.usedSectors;

  else
    {
      for (idx = 0; idx < physicalDisk->cache.usedSectors; idx++)
	if (physicalDisk->cache.sectors[idx]->number >= startSector)
	  {
	    // We will have to shift all sectors starting from here to make
	    // room for our new ones.

	    count = (physicalDisk->cache.usedSectors - (idx + 1));

	    while(1)
	      {
		cacheSector =
		  physicalDisk->cache.sectors[idx + sectorCount + count];
		physicalDisk->cache.sectors[idx + sectorCount + count] =
		  physicalDisk->cache.sectors[idx + count];
		physicalDisk->cache.sectors[idx + count] = cacheSector;
		if (count == 0)
		  break;
		count -= 1;
	      }
	    break;
	  }
    }

  physicalDisk->cache.usedSectors += sectorCount;

  // Now copy our new sectors into the cache
  
  for (count = 0; count < sectorCount; count ++)
    {
      cacheSector = physicalDisk->cache.sectors[idx + count];

      // Set the number
      cacheSector->number = (startSector + count);

      // Copy the data
      kernelMemCopy((data + (count * physicalDisk->sectorSize)),
		    cacheSector->data, physicalDisk->sectorSize);

      // Clean or dirty?
      cacheSector->dirty = dirty;

      // Set the last access time
      cacheSector->lastAccess = kernelSysTimerRead();
    }

  if (dirty)
    physicalDisk->cache.dirty = 1;

  kernelLockRelease(&(physicalDisk->cache.cacheLock));
  return (status = sectorCount);
}


static int getCachedSectors(kernelPhysicalDisk *physicalDisk,
			    unsigned sectorNum, int sectorCount, void *data)
{
  // This function is used to retrieve one or more (consecutive) sectors from
  // the cache.  If sectors are cached, this routine copies the data into the
  // pointer supplied and returns the number it copied.

  int status = 0;
  unsigned idx;
  int copied = 0;
  kernelDiskCacheSector *cacheSector = NULL;

  status = findCachedSector(physicalDisk, sectorNum);
  if (status < 0)
    return (copied = 0);
  idx = status;

  status = kernelLockGet(&(physicalDisk->cache.cacheLock));
  if (status < 0)
    return (status);

  // We've found the starting sector.  Start copying data
  for ( ; (idx < physicalDisk->cache.usedSectors) &&
	  (copied < sectorCount) ; idx ++)
    {
      cacheSector = physicalDisk->cache.sectors[idx];
      
      if (cacheSector->number != sectorNum)
	break;
      
      // This sector is cached.  Copy the data.
      kernelMemCopy(cacheSector->data, data, physicalDisk->sectorSize);
      
      copied++;
      sectorNum++;
      data += physicalDisk->sectorSize;
	      
      // Set the last access time
      cacheSector->lastAccess = kernelSysTimerRead();
    }

  kernelLockRelease(&(physicalDisk->cache.cacheLock));
  return (copied);
}


static int writeCachedSectors(kernelPhysicalDisk *physicalDisk,
			      unsigned sectorNum, int sectorCount, void *data)
{
  // This function is used to change one or more (consecutive) sectors stored
  // in the cache.  If sectors are cached, this routine copies the data from
  // the pointer supplied and returns the number it copied.

  int status = 0;
  unsigned idx;
  int copied = 0;
  kernelDiskCacheSector *cacheSector = NULL;

  status = findCachedSector(physicalDisk, sectorNum);
  if (status < 0)
    return (copied = 0);
  idx = status;

  status = kernelLockGet(&(physicalDisk->cache.cacheLock));
  if (status < 0)
    return (status);

  // We've found the starting sector.  Start copying data
  for ( ; (idx < physicalDisk->cache.usedSectors) &&
	  (copied < sectorCount) ; idx ++)
    {
      cacheSector = physicalDisk->cache.sectors[idx];
      
      if (cacheSector->number != sectorNum)
	break;
      
      // This sector is cached.  Copy the data if it's different
      if (kernelMemCmp(data, cacheSector->data, physicalDisk->sectorSize))
	{
	  kernelMemCopy(data, cacheSector->data, physicalDisk->sectorSize);
	  
	  // The sector and cache are now dirty
	  cacheSector->dirty = 1;
	  physicalDisk->cache.dirty = 1;
	}

      copied++;
      sectorNum++;
      data += physicalDisk->sectorSize;

      // Set the last access time
      cacheSector->lastAccess = kernelSysTimerRead();
    }

  kernelLockRelease(&(physicalDisk->cache.cacheLock));
  return (copied);
}
#endif // DISK_CACHE


static int motorOff(kernelPhysicalDisk *physicalDisk)
{
  // Calls the target disk driver's 'motor off' routine.

  int status = 0;

  // Reset the 'idle since' value.
  physicalDisk->idleSince = kernelSysTimerRead();
  
  // If it's a fixed disk, we don't turn the motor off, for now
  if (physicalDisk->flags & DISKFLAG_FIXED)
    return (status = 0);

  // Make sure the motor isn't already off
  if (!(physicalDisk->motorState))
    return (status = 0);

  // Now make sure the device driver motor off routine has been installed
  if (((kernelDiskOps *) physicalDisk->driver->ops)
      ->driverSetMotorState == NULL)
    // Don't make this an error.  It's just not available in some drivers.
    return (status = 0);

  // Lock the disk
  status = kernelLockGet(&(physicalDisk->diskLock));
  if (status < 0)
    return (status = ERR_NOLOCK);

  // Ok, now turn the motor off
  status = ((kernelDiskOps *) physicalDisk->driver->ops)
    ->driverSetMotorState(physicalDisk->deviceNumber, 0);
  if (status < 0)
    return (status);
  else
    // Make note of the fact that the motor is off
    physicalDisk->motorState = 0;

  // Reset the 'idle since' value
  physicalDisk->idleSince = kernelSysTimerRead();
  
  // Unlock the disk
  kernelLockRelease(&(physicalDisk->diskLock));

  return (status);
}


static void diskd(void)
{
  // This function will be a thread spawned at inititialization time
  // to do any required ongoing operations on disks, such as shutting off
  // floppy and cdrom motors
  
  kernelPhysicalDisk *physicalDisk = NULL;
  unsigned currentTime;
  int count;

  // Don't try to do anything until we have registered disks
  while (!initialized || (physicalDiskCounter <= 0))
    kernelMultitaskerWait(60);

  while(1)
    {
      // Loop for each physical disk
      for (count = 0; count < physicalDiskCounter; count ++)
	{
	  physicalDisk = physicalDisks[count];

	  currentTime = kernelSysTimerRead();

	  // If the disk is a floppy and has been idle for >= 2 seconds,
	  // turn off the motor.
	  if ((physicalDisk->flags & DISKFLAG_FLOPPY) &&
	      (currentTime > (physicalDisk->idleSince + 40)))
	    motorOff(physicalDisk);
	}

      // Yield the rest of the timeslice and wait for 1 second
      kernelMultitaskerWait(20);
    }
}


static int spawnDiskd(void)
{
  // Launches the disk daemon

  diskdPID = kernelMultitaskerSpawnKernelThread(diskd, "disk thread", 0, NULL);
  if (diskdPID < 0)
    return (diskdPID);

  // Re-nice the disk daemon
  kernelMultitaskerSetProcessPriority(diskdPID, (PRIORITY_LEVELS - 2));
 
  // Success
  return (diskdPID);
}


static int readWriteSectors(kernelPhysicalDisk *physicalDisk,
			    unsigned logicalSector, unsigned numSectors,
			    void *dataPointer, int mode)
{
  // This is the combined "read sectors" and "write sectors" routine 
  // which invokes the driver routines designed for those functions.  
  // If an error is encountered, the function returns negative.  
  // Otherwise, it returns the number of sectors it actually read or
  // wrote.  This should not be exported, and should not be called by 
  // users.  Users should call the routines kernelDiskReadSectors
  // and kernelDiskWriteSectors which in turn call this routine.

  int status = 0;
  unsigned doSectors = 0;
  unsigned extraSectors = 0;
  processState tmpState;
  // next added by Davide Airaghi
  int skip_cache;
  
  // Make sure the appropriate device driver routine has been installed
  if (((mode & IOMODE_READ) &&
       (((kernelDiskOps *) physicalDisk->driver->ops)
	->driverReadSectors == NULL)) ||
      ((mode & IOMODE_WRITE) &&
       (((kernelDiskOps *) physicalDisk->driver->ops)
	->driverWriteSectors == NULL)))
    {
      kernelError(kernel_error, "Disk cannot %s",
		  ((mode & IOMODE_READ)? "read" : "write"));
      return (status = ERR_NOSUCHFUNCTION);
    }

  // Don't try to write a read-only disk
  if ((mode & IOMODE_WRITE) && physicalDisk->readOnly)
    return (status = ERR_NOWRITE);

  // next one added by Davide Airaghi
  skip_cache = physicalDisk->skip_cache;

#if (DISK_CACHE)
  // Check disk cache initialization.
  
  // skip_cache added by Davide Airaghi
  if (!skip_cache) {
  
  if (!physicalDisk->cache.initialized)
    {
      // Get a cache for the disk
      status = getDiskCache(physicalDisk);
      if (status < 0)
	{
	  kernelError(kernel_error, "Unable to initialize disk cache");
	  return (status);
	}
    } 
  
  // next one added by Davide Airaghi    
  }    
#endif // DISK_CACHE

  // Make sure the disk daemon is running
  if (kernelMultitaskerGetProcessState(diskdPID, &tmpState) < 0)
    // Re-spawn the disk daemon
    spawnDiskd();

  // Now we start the actual read/write operation

  // This loop deals with contiguous blocks of sectors, either cached
  // or to/from the disk.

  while (numSectors > 0)
    {
      doSectors = numSectors;
      extraSectors = 0;

#if (DISK_CACHE)

      void *savePointer = NULL;
      unsigned cached = 0;

    // next one added by Davide Airaghi
    if (!skip_cache) {

      if (!(mode & IOMODE_NOCACHE))
	{
	  if (mode & IOMODE_READ)
	    // If the data is cached, get it from the cache instead
	    cached = getCachedSectors(physicalDisk, logicalSector,
				      numSectors, dataPointer);
	  else
	    // If the data is cached, write it to the cache instead
	    cached = writeCachedSectors(physicalDisk, logicalSector,
					numSectors, dataPointer);
	  if (cached)
	    {
	      // Some number of sectors was cached.
	      logicalSector += cached;
	      numSectors -= cached;
	  
	      // Increment the place in the buffer we're using
	      dataPointer += (physicalDisk->sectorSize * cached);
	    }

	  // Anything left to do?
	  if (numSectors == 0)
	    continue;

	  // Only attempt to do as many sectors as are not cached.
	  doSectors = countUncachedSectors(physicalDisk, logicalSector,
					   numSectors);

	  // Could we read some extra to possibly speed up future operations?
	  if ((mode & IOMODE_READ) && (doSectors == numSectors) && 
	      (doSectors < DISK_READAHEAD_SECTORS))
	    {
	      // We read extraSectors sectors extra.
	      unsigned tmp = countUncachedSectors(physicalDisk, logicalSector,
						  DISK_READAHEAD_SECTORS);

	      if ((logicalSector + tmp - 1) < physicalDisk->numSectors)
		{
		  extraSectors = (tmp - doSectors);

		  if (extraSectors)
		    {
		      doSectors += extraSectors;
		      savePointer = dataPointer;
		      dataPointer =
			kernelMalloc(doSectors * physicalDisk->sectorSize);
		      if (dataPointer == NULL)
			{
			  // Oops.  Just put everything back.
			  doSectors -= extraSectors;
			  dataPointer = savePointer;
			  extraSectors = 0;
			}
		    }
		}
	    }

	  else if (mode & IOMODE_WRITE)
	    {
	      // Add the remaining sectors to the cache
	      status = addCacheSectors(physicalDisk, logicalSector, doSectors,
				       dataPointer, 1 /* dirty */);
	      if (status > 0)
		{
		  logicalSector += status;
		  numSectors -= status;
		  dataPointer += (physicalDisk->sectorSize * status);
		  continue;
		}

	      // Eek.  No caching.  Better fall through and write the data.
	    }
	}
	
    // next one added by Davide Airaghi
    }	
	
#endif // DISK_CACHE

      // Call the read or write routine
      if (mode & IOMODE_READ)
	status = ((kernelDiskOps *) physicalDisk->driver->ops)
	  ->driverReadSectors(physicalDisk->deviceNumber, logicalSector,
			      doSectors, dataPointer);
      else
	status = ((kernelDiskOps *) physicalDisk->driver->ops)
	  ->driverWriteSectors(physicalDisk->deviceNumber, logicalSector,
			       doSectors, dataPointer);
      if (status < 0)
	{
	  // If it is a write-protect error, mark the disk as read only
	  if ((mode & IOMODE_WRITE) && (status == ERR_NOWRITE))
	    {
	      kernelError(kernel_error, "Read-only disk.");
	      physicalDisk->readOnly = 1;
	    }
	  
	  return (status);
	}

#if (DISK_CACHE)

    // next one added by Davide Airaghi
    if (!skip_cache) {

      if ((!(mode & IOMODE_NOCACHE)) && (mode & IOMODE_READ))
	{
	  // If it's a read operation, cache the sectors we read
	  addCacheSectors(physicalDisk, logicalSector,
			  doSectors, dataPointer, 0 /* not dirty */);

	  if (extraSectors)
	    {
	      doSectors -= extraSectors;
	      // Copy the requested sectors into the user's buffer
	      kernelMemCopy(dataPointer, savePointer,
			    (doSectors * physicalDisk->sectorSize));
	      kernelFree(dataPointer);
	      dataPointer = savePointer;
	    }
	}

    // next one added by Davide Airaghi	
    }	
	
#endif // DISK_CACHE

      // Update the current logical sector, the remaining number to read,
      // and the buffer pointer
      logicalSector += doSectors;
      numSectors -= doSectors;
      dataPointer += (doSectors * physicalDisk->sectorSize);
      
    } // per-operation loop
  
  // Finished.  Return success
  return (status = 0);
}


static kernelPhysicalDisk *getPhysicalByName(const char *name)
{
  // This routine takes the name of a physical disk and finds it in the
  // array, returning a pointer to the disk.  If the disk doesn't exist,
  // the function returns NULL

  kernelPhysicalDisk *physicalDisk = NULL;
  int count;

  if (!initialized)
    return (physicalDisk = NULL);

  // Check params
  if (name == NULL)
    {
      kernelError(kernel_error, "Disk name is NULL");
      return (physicalDisk = NULL);
    }

  for (count = 0; count < physicalDiskCounter; count ++)
    if (!strcmp(name, (char *) physicalDisks[count]->name))
      {
	physicalDisk = physicalDisks[count];
	break;
      }

  return (physicalDisk);
}


static int diskFromPhysical(kernelPhysicalDisk *physicalDisk, disk *userDisk)
{
  // Takes our physical disk kernel structure and turns it into a user space
  // 'disk' object

  int status = 0;

  if (!initialized)
    return (status = ERR_NOTINITIALIZED);

  // Check params
  if ((physicalDisk == NULL) || (userDisk == NULL))
    return (status = ERR_NULLPARAMETER);

  kernelMemClear(userDisk, sizeof(disk));
  strncpy(userDisk->name, (char *) physicalDisk->name, DISK_MAX_NAMELENGTH);
  userDisk->deviceNumber = physicalDisk->deviceNumber;
  userDisk->flags = physicalDisk->flags;
  userDisk->readOnly = physicalDisk->readOnly;
  userDisk->heads = physicalDisk->heads;
  userDisk->cylinders = physicalDisk->cylinders;
  userDisk->sectorsPerCylinder = physicalDisk->sectorsPerCylinder;
  userDisk->startSector = 0;
  userDisk->numSectors = physicalDisk->numSectors;
  userDisk->sectorSize = physicalDisk->sectorSize;

  return (status = 0);
}


static int unmountAll(void)
{
  // This routine will unmount all mounted filesystems from the disks,
  // including the root filesystem.

  int status = 0;
  kernelDisk *theDisk = NULL;
  int errors = 0;
  int count;

  // We will loop through all of the mounted disks, unmounting each of them
  // (except the root disk) until only root remains.  Finally, we unmount the
  // root also.

  for (count = 0; count < logicalDiskCounter; count ++)
    {
      theDisk = logicalDisks[count];

      if (!theDisk->filesystem.mounted)
	continue;

      if (!strcmp((char *) theDisk->filesystem.mountPoint, "/"))
	continue;

      // Unmount this filesystem
      status =
	kernelFilesystemUnmount((char *) theDisk->filesystem.mountPoint);
      if (status < 0)
	{
	  // Don't quit, just make an error message
	  kernelError(kernel_warn, "Unable to unmount filesystem %s from "
		      "disk %s", theDisk->filesystem.mountPoint,
		      theDisk->name);
	  errors++;
	  continue;
	}
    }

  // Now unmount the root filesystem
  status = kernelFilesystemUnmount("/");
  if (status < 0)
    // Don't quit, just make an error message
    errors++;

  // If there were any errors, we should return an error code of some kind
  if (errors)
    return (status = ERR_INVALID);
  else
    // Return success
    return (status = 0);
}


/////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////
//
// Below here, the functions are exported for external use
//
/////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////


int kernelDiskRegisterDevice(kernelDevice *dev)
{
  // This routine will receive a new device structure, add the
  // kernelPhysicalDisk to our array, and register all of its logical disks
  // for use by the system.

  int status = 0;
  kernelPhysicalDisk *physicalDisk = NULL;
  int count;

  // Check params
  if (dev == NULL)
    {
      kernelError(kernel_error, "Disk device structure is NULL");
      return (status = ERR_NULLPARAMETER);
    }

  physicalDisk = dev->data;

  if ((physicalDisk == NULL) || (physicalDisk->driver == NULL))
    {
      kernelError(kernel_error, "Physical disk structure or driver is NULL");
      return (status = ERR_NULLPARAMETER);
    }

  // Make sure the arrays of disk structures aren't full
  if ((physicalDiskCounter >= DISK_MAXDEVICES) ||
      (logicalDiskCounter >= DISK_MAXDEVICES))
    {
      kernelError(kernel_error, "Max disk structures already registered");
      return (status = ERR_NOFREE);
    }

  // Disk cache initialization is deferred until cache use is attempted.
  // Otherwise we waste memory allocating caches for disks that might
  // never be used.
  
  // Add the physical disk to our list
  physicalDisks[physicalDiskCounter++] = physicalDisk;

  // Loop through the physical device's logical disks
  for (count = 0; count < physicalDisk->numLogical; count ++)
    // Put the device at the end of the list and increment the counter
    logicalDisks[logicalDiskCounter++] = &physicalDisk->logical[count];

  // If it's a floppy, make sure the motor is off
  if (physicalDisk->flags & DISKFLAG_FLOPPY)
    motorOff(physicalDisk);

  // Reset the 'idle since' and 'last sync' values
  physicalDisk->idleSince = kernelSysTimerRead();
  
  // Success
  return (status = 0);
}


int kernelDiskRemoveDevice(kernelDevice *dev)
{
  // This routine will receive a new device structure, remove the
  // kernelPhysicalDisk from our array, and remove all of its logical disks

  int status = 0;
  kernelPhysicalDisk *physicalDisk = NULL;
  kernelDisk *newLogicalDisks[DISK_MAXDEVICES];
  int newLogicalDiskCounter = 0;
  int position = -1;
  int count;

  // Check params
  if (dev == NULL)
    {
      kernelError(kernel_error, "Disk device structure is NULL");
      return (status = ERR_NULLPARAMETER);
    }

  physicalDisk = dev->data;

  if ((physicalDisk == NULL) || (physicalDisk->driver == NULL))
    {
      kernelError(kernel_error, "Physical disk structure or driver is NULL");
      return (status = ERR_NULLPARAMETER);
    }

  // Add all the logical disks that don't belong to this physical disk
  for (count = 0; count < logicalDiskCounter; count ++)
    if (logicalDisks[count]->physical != physicalDisk)
      newLogicalDisks[newLogicalDiskCounter++] = logicalDisks[count];

  // Now copy our new array of logical disks
  for (logicalDiskCounter = 0; logicalDiskCounter < newLogicalDiskCounter;
       logicalDiskCounter ++)
    logicalDisks[logicalDiskCounter] = newLogicalDisks[logicalDiskCounter];

  // Remove this physical disk from our array.  Find its position
  for (count = 0; count < physicalDiskCounter; count ++)
    {
      if (physicalDisks[count] == physicalDisk)
	{
	  position = count;
	  break;
	}
    }

  if (position >= 0)
    {
      if ((physicalDiskCounter > 1) && (position < (physicalDiskCounter - 1)))
	{
	  for (count = position; count < (physicalDiskCounter - 1); count ++)
	    physicalDisks[count] = physicalDisks[count + 1];
	}

      physicalDiskCounter -= 1;
    }

  return (status = 0);
}


int kernelDiskInitialize(void)
{
  // This is the "initialize" routine which invokes  the driver routine 
  // designed for that function.  Normally it returns zero, unless there
  // is an error.  If there's an error it returns negative.
  
  int status = 0;
  kernelPhysicalDisk *physicalDisk = NULL;
  kernelDisk *logicalDisk = NULL;
  int count1, count2;

  // Check whether any disks have been registered.  If not, that's 
  // an indication that the hardware enumeration has not been done
  // properly.  We'll issue an error in this case
  if (physicalDiskCounter <= 0)
    {
      kernelError(kernel_error, "No disks have been registered");
      return (status = ERR_NOTINITIALIZED);
    }

  // Spawn the disk daemon
  status = spawnDiskd();
  if (status < 0)
    kernelError(kernel_warn, "Unable to start disk thread");

  // We're initialized
  initialized = 1;

  // Read the partition tables
  status = kernelDiskReadPartitionsAll();
  if (status < 0)
    kernelError(kernel_error, "Unable to read disk partitions");

  // Copy the name of the physical boot disk
  strcpy(bootDisk, kernelOsLoaderInfo->bootDisk);

  // If we booted from a hard disk, we need to find out which partition
  // (logical disk) it was.
  if (!strncmp(bootDisk, "hd", 2))
    {
      // Loop through the physical disks and find the one with this name
      for (count1 = 0; count1 < physicalDiskCounter; count1 ++)
	{
	  physicalDisk = physicalDisks[count1];
	  if (!strcmp((char *) physicalDisk->name, bootDisk))
	    {
	      // This is the physical disk we booted from.  Find the
	      // partition
	      for (count2 = 0; count2 < physicalDisk->numLogical; count2 ++)
		{
		  logicalDisk = &(physicalDisk->logical[count2]);
		  // If the boot sector we booted from is in this partition,
		  // save its name as our boot disk.
		  if (logicalDisk->startSector ==
		      kernelOsLoaderInfo->bootSector)
		    {
		      strcpy(bootDisk, (char *) logicalDisk->name);
		      break;
		    }
		}
	      break;
	    }
	}
    }

  return (status = 0);
}


int kernelDiskSyncDisk(const char *diskName)
{
  // Syncronize the named disk

  int status = 0;

  if (!initialized)
    return (status = ERR_NOTINITIALIZED);

  // Check params
  if (diskName == NULL)
    return (status = ERR_NULLPARAMETER);

#if (DISK_CACHE)
  kernelDisk *theDisk = NULL;
  kernelPhysicalDisk *physicalDisk = NULL;

  theDisk = kernelDiskGetByName(diskName);
  if (theDisk == NULL)
    {
      // No such disk.
      kernelError(kernel_error, "No such disk \"%s\"", diskName);
      return (status = ERR_NOSUCHENTRY);
    }

  physicalDisk = theDisk->physical;

  // next one added by Davide Airaghi
  if (!physicalDisk->skip_cache) {

  // Lock the physical disk
  status = kernelLockGet(&(physicalDisk->diskLock));
  if (status < 0)
    {
      kernelError(kernel_error, "Unable to lock disk \"%s\" for sync",
		  physicalDisk->name);
      return (status);
    }

  status = cacheSync(physicalDisk);
  
  kernelLockRelease(&(physicalDisk->diskLock));  
  
  if (status < 0)
    kernelError(kernel_warn, "Error synchronizing the disk \"%s\"",
		physicalDisk->name);

  // next one added by Davide Airaghi
  }
  
#endif // DISK_CACHE

  return (status);
}


int kernelDiskInvalidateCache(const char *diskName)
{
  // Invalidate the cache of the named disk

  int status = 0;

  if (!initialized)
    return (status = ERR_NOTINITIALIZED);

  // Check params
  if (diskName == NULL)
    return (status = ERR_NULLPARAMETER);

#if (DISK_CACHE)
  kernelPhysicalDisk *physicalDisk = NULL;

  physicalDisk = getPhysicalByName(diskName);
  if (physicalDisk == NULL)
    {
      kernelError(kernel_error, "No such disk \"%s\"", diskName);
      return (status = ERR_NOSUCHENTRY);
    }


  // next one added by Davide Airaghi
  if (!physicalDisk->skip_cache) {

  // Lock the physical disk
  status = kernelLockGet(&(physicalDisk->diskLock));
  if (status < 0)
    {
      kernelError(kernel_error, "Unable to lock disk \"%s\" for cache "
		  "invalidation", physicalDisk->name);
      return (status);
    }

  if (physicalDisk->cache.dirty)
    kernelError(kernel_warn, "Invalidating dirty disk cache!");

  status = cacheInvalidate(physicalDisk);
  
  kernelLockRelease(&(physicalDisk->diskLock));  
  
  if (status < 0)
    kernelError(kernel_warn, "Error invalidating disk \"%s\" cache",
		physicalDisk->name);

  // next one added by Davide Airaghi
  }
  
#endif // DISK_CACHE

  return (status);
}


int kernelDiskShutdown(void)
{
  // Shut down.

  int status = 0;
  kernelPhysicalDisk *physicalDisk = NULL;
  int count;
  
  if (!initialized)
    return (status = ERR_NOTINITIALIZED);

  // Unmount all the disks
  unmountAll();

  // Synchronize all the disks
  status = kernelDiskSync();

  for (count = 0; count < physicalDiskCounter; count ++)
    {
      physicalDisk = physicalDisks[count];

      if ((physicalDisk->flags & DISKFLAG_REMOVABLE) &&
	  physicalDisk->motorState)
	motorOff(physicalDisk);
    }

  return (status);
}


int kernelDiskFromLogical(kernelDisk *logical, disk *userDisk)
{
  // Takes our logical disk kernel structure and turns it into a user space
  // 'disk' object

  int status = 0;

  if (!initialized)
    return (status = ERR_NOTINITIALIZED);

  // Check params
  if ((logical == NULL) || (userDisk == NULL))
    return (status = ERR_NULLPARAMETER);

  kernelMemClear(userDisk, sizeof(disk));

  // Get the physical disk info
  status = diskFromPhysical(logical->physical, userDisk);
  if (status < 0)
    return (status);

  // Add/override some things specific to logical disks
  strncpy(userDisk->name, (char *) logical->name, DISK_MAX_NAMELENGTH);
  userDisk->flags = ((logical->physical->flags & ~DISKFLAG_LOGICALPHYSICAL) |
		     DISKFLAG_LOGICAL);
  if (logical->primary)
    userDisk->flags |= DISKFLAG_PRIMARY;
  kernelMemCopy((void *) &(logical->partType), &(userDisk->partType),
		sizeof(partitionType));
  strncpy(userDisk->fsType, (char *) logical->fsType, FSTYPE_MAX_NAMELENGTH);
  userDisk->opFlags = logical->opFlags;
  userDisk->startSector = logical->startSector;
  userDisk->numSectors = logical->numSectors;

  // Filesystem-related
  userDisk->blockSize = logical->filesystem.blockSize;
  userDisk->minSectors = logical->filesystem.minSectors;
  userDisk->maxSectors = logical->filesystem.maxSectors;
  userDisk->mounted = logical->filesystem.mounted;
  if (userDisk->mounted)
    {
      userDisk->freeBytes =
	kernelFilesystemGetFree((char *) logical->filesystem.mountPoint);
      strncpy(userDisk->mountPoint, (char *) logical->filesystem.mountPoint,
	      MAX_PATH_LENGTH);
    }
  userDisk->readOnly = logical->filesystem.readOnly;

  return (status = 0);
}


kernelDisk *kernelDiskGetByName(const char *name)
{
  // This routine takes the name of a logical disk and finds it in the
  // array, returning a pointer to the disk.  If the disk doesn't exist,
  // the function returns NULL

  kernelDisk *theDisk = NULL;
  int count;

  if (!initialized)
    return (theDisk = NULL);

  // Check params
  if (name == NULL)
    {
      kernelError(kernel_error, "Disk name is NULL");
      return (theDisk = NULL);
    }

  for (count = 0; count < logicalDiskCounter; count ++)
    if (!strcmp(name, (char *) logicalDisks[count]->name))
      {
	theDisk = logicalDisks[count];
	break;
      }

  return (theDisk);
}


kernelDisk *kernelDiskGetByPath(const char *path)
{
  // This routine takes the name of a mount point and attempts to find a
  // disk that is mounted there.  If no such disk exists, the function
  // returns NULL

  kernelDisk *theDisk = NULL;
  int count;

  if (!initialized)
    return (theDisk = NULL);

  // Check params
  if (path == NULL)
    {
      kernelError(kernel_error, "Disk path is NULL");
      return (theDisk = NULL);
    }
  
  for (count = 0; count < logicalDiskCounter; count ++)
    {
      if (!logicalDisks[count]->filesystem.mounted)
	continue;
      
      if (!strcmp(path, (char *) logicalDisks[count]->filesystem.mountPoint))
	{
	  theDisk = logicalDisks[count];
	  break;
	}
    }

  return (theDisk);
}


/////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////
//
//  Below here, the functions are exported outside the kernel to user
//  space.
//
/////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////


int kernelDiskReadPartitions(const char *diskName)
{
  // Read the partition tables for all the registered physical disks, and
  // (re)build the list of logical disks.  This will be done initially at
  // startup time, but can be re-called during operation if the partitions
  // have been changed.

  int status = 0;
  kernelPhysicalDisk *physicalDisk = NULL;
  kernelDisk *newLogicalDisks[DISK_MAXDEVICES];
  int newLogicalDiskCounter = 0;
  int mounted = 0;
  kernelDisk *logicalDisk = NULL;
  unsigned char sectBuf[512];
  int partition = 0;
  unsigned char *partitionRecord = NULL;
  unsigned char *extendedRecord = NULL;
  unsigned extendedStartSector = 0;
  unsigned startSector = 0;
  unsigned char partTypeCode = 0;
  partitionType partType;
  int count;

  if (!initialized)
    return (status = ERR_NOTINITIALIZED);

  // Check params
  if (diskName == NULL)
    return (status = ERR_NULLPARAMETER);

  // Find the disk structure.
  physicalDisk = getPhysicalByName(diskName);
  if (physicalDisk == NULL)
    // No such disk.
    return (status = ERR_NOSUCHENTRY);

  // Add all the logical disks that don't belong to this physical disk
  for (count = 0; count < logicalDiskCounter; count ++)
    if (logicalDisks[count]->physical != physicalDisk)
      newLogicalDisks[newLogicalDiskCounter++] = logicalDisks[count];

  // Assume UNKNOWN (code 0) partition type for now.
  partType.code = 0;
  strcpy((char *) partType.description, physicalDisk->description);

  // If this is a hard disk, get the logical disks from reading the partitions.
  if (physicalDisk->flags & DISKFLAG_HARDDISK)
    {
      // It's a hard disk.  We need to read the partition table

      // Make sure it has no mounted partitions.
      mounted = 0;
      for (count = 0; count < physicalDisk->numLogical; count ++)
	if (physicalDisk->logical[count].filesystem.mounted)
	  {
	    kernelError(kernel_warn, "Logical disk %s is mounted.  Will "
			"not rescan %s until reboot.",
			physicalDisk->logical[count].name,
			physicalDisk->name);
	    mounted = 1;
	    break;
	  }

      if (mounted)
	{
	  // It has mounted partitions.  Add the existing logical disks to
	  // our array and continue to the next physical disk.
	  for (count = 0; count < physicalDisk->numLogical; count ++)
	    newLogicalDisks[newLogicalDiskCounter++] =
	      &(physicalDisk->logical[count]);
	  return (status = 1);
	}

      // Initialize the sector buffer
      kernelMemClear(sectBuf, 512);

      startSector = 0;
      extendedStartSector = 0;
      
      // Clear the logical disks
      physicalDisk->numLogical = 0;
      kernelMemClear(&(physicalDisk->logical),
		     (sizeof(kernelDisk) * DISK_MAX_PARTITIONS));

      // Read the first sector of the disk
      status =
	kernelDiskReadSectors((char *) physicalDisk->name, 0, 1, sectBuf);
      if (status < 0)
	return (status);

      while (physicalDisk->numLogical < DISK_MAX_PARTITIONS)
	{
	  extendedRecord = NULL;

	  // Is this a valid MBR?
	  if ((sectBuf[511] == (unsigned char) 0xAA) ||
	      (sectBuf[510] == (unsigned char) 0x55))
	    {
	      // Set this pointer to the first partition record in the
	      // master boot record
	      partitionRecord = (sectBuf + 0x01BE);

	      // Loop through the partition records, looking for non-zero
	      // entries
	      for (partition = 0; partition < 4; partition ++)
		{
		  logicalDisk =
		    &(physicalDisk->logical[physicalDisk->numLogical]);

		  partTypeCode = partitionRecord[4];
		  if (partTypeCode == 0)
		    {
		      // The "rules" say we must be finished with this
		      // physical device.  But that is not the way things
		      // often happen in real life -- empty records often
		      // come before valid ones.
		      partitionRecord += 16;
		      continue;
		    }

		  if (PARTITION_TYPEID_IS_EXTD(partTypeCode))
		    {
		      extendedRecord = partitionRecord;
		      partitionRecord += 16;
		      continue;
		    }

		  kernelDiskGetPartType(partTypeCode, &partType);
	  
		  // We will add a logical disk corresponding to the
		  // partition we've discovered
		  sprintf((char *) logicalDisk->name, "%s%c",
			  physicalDisk->name,
			  ('a' + physicalDisk->numLogical));
		  kernelMemCopy(&partType, (void *) &(logicalDisk->partType),
				sizeof(partitionType));
		  strncpy((char *) logicalDisk->fsType, "unknown",
			  FSTYPE_MAX_NAMELENGTH);
		  logicalDisk->physical = physicalDisk;
		  logicalDisk->startSector =
		    (startSector + *((unsigned *)(partitionRecord + 0x08)));
		  logicalDisk->numSectors =
		    *((unsigned *)(partitionRecord + 0x0C));
		  if (!extendedStartSector)
		    logicalDisk->primary = 1;
		  
		  newLogicalDisks[newLogicalDiskCounter++] = logicalDisk;
		      
		  physicalDisk->numLogical++;

		  // Move to the next partition record
		  partitionRecord += 16;
		}
	    }

	  if (!extendedRecord)
	    break;

	  // Make sure the extended entry doesn't loop back on itself.
	  // It can happen.
	  if (extendedStartSector &&
	      ((*((unsigned *)(extendedRecord + 0x08)) +
		extendedStartSector) == startSector))
	    {
	      kernelError(kernel_error, "Extended partition links to itself");
	      break;
	    }

	  // We have an extended partition chain.  We need to go through
	  // that as well.
	  startSector = *((unsigned *)(extendedRecord + 0x08));

	  if (!extendedStartSector)
	    extendedStartSector = startSector;
	  else
	    startSector += extendedStartSector;	

	  if (kernelDiskReadSectors((char *) physicalDisk->name, startSector,
				    1, sectBuf) < 0)
	    break;
	}
    }
  else
    {
      // If this is a not a hard disk, make the logical disk be the same
      // as the physical disk
      physicalDisk->numLogical = 1;
      logicalDisk = &(physicalDisk->logical[0]);
      // Logical disk name same as device name
      strcpy((char *) logicalDisk->name, (char *) physicalDisk->name);
      kernelMemCopy(&partType, (void *) &(logicalDisk->partType),
		    sizeof(partitionType));
      if (logicalDisk->fsType[0] == '\0')
	strncpy((char *) logicalDisk->fsType, "unknown",
		FSTYPE_MAX_NAMELENGTH);
      logicalDisk->physical = physicalDisk;
      logicalDisk->startSector = 0;
      logicalDisk->numSectors = physicalDisk->numSectors;

      newLogicalDisks[newLogicalDiskCounter++] = logicalDisk;
    }

  // Now copy our new array of logical disks
  for (logicalDiskCounter = 0; logicalDiskCounter < newLogicalDiskCounter;
       logicalDiskCounter ++)
    logicalDisks[logicalDiskCounter] = newLogicalDisks[logicalDiskCounter];

  // See if we can determine the filesystem types
  for (count = 0; count < logicalDiskCounter; count ++)
    {
      logicalDisk = logicalDisks[count];
      
      if (logicalDisk->physical == physicalDisk)
	{
	  if (physicalDisk->motorState)
	    kernelFilesystemScan(logicalDisk);

	  kernelLog("Disk %s (hard disk %s, %s): %s",
		    logicalDisk->name, physicalDisk->name,
		    (logicalDisk->primary? "primary" : "logical"),
		    logicalDisk->fsType);
	}
    }

  return (status = 0);
}


int kernelDiskReadPartitionsAll(void)
{
  // Read the partition tables for all the registered physical disks, and
  // (re)build the list of logical disks.  This will be done initially at
  // startup time, but can be re-called during operation if the partitions
  // have been changed.

  int status = 0;
  int mounts = 0;
  int errors = 0;
  int count;

  if (!initialized)
    return (errors = ERR_NOTINITIALIZED);

  // Loop through all of the registered physical disks
  for (count = 0; count < physicalDiskCounter; count ++)
    {
      status = kernelDiskReadPartitions((char *) physicalDisks[count]->name);
      if (status < 0)
	errors = status;
      else
	mounts += status;
    }

  if (errors)
    return (status = errors);
  else
    return (status = mounts);
}


int kernelDiskSync(void)
{
  // Force a synchronization of all disks
  
  int errors = 0;

  if (!initialized)
    return (errors = ERR_NOTINITIALIZED);

#if (DISK_CACHE)
  int status = 0;
  kernelPhysicalDisk *physicalDisk = NULL;
  int count;

  for (count = 0; count < physicalDiskCounter; count ++)
    {
      physicalDisk = physicalDisks[count];

      // next one added by Davide Airaghi
      if (physicalDisk->skip_cache)
        continue;

      // Lock the physical disk
      status = kernelLockGet(&(physicalDisk->diskLock));
      if (status < 0)
	{
	  kernelError(kernel_error, "Unable to lock disk \"%s\" for sync",
		      physicalDisk->name);
	  errors = status;
	  continue;
	}

      status = cacheSync(physicalDisk);
      if (status < 0)
	{
	  kernelError(kernel_warn, "Error synchronizing the disk \"%s\"",
		      physicalDisk->name);
	  errors = status;
	}

      kernelLockRelease(&(physicalDisk->diskLock));
    }

#endif // DISK_CACHE

  return (errors);
}


int kernelDiskGetBoot(char *boot)
{
  // Returns the disk name of the boot device

  int status = 0;

  if (!initialized)
    return (status = ERR_NOTINITIALIZED);

  // Check params
  if (boot == NULL)
    return (status = ERR_NULLPARAMETER);
  
  strncpy(boot, bootDisk, DISK_MAX_NAMELENGTH);
  return (status = 0);
}


int kernelDiskGetCount(void)
{
  // Returns the number of registered logical disk structures.  Useful for
  // iterating through calls to kernelGetDiskByName or kernelDiskGetInfo

  if (!initialized)
    return (ERR_NOTINITIALIZED);

  return (logicalDiskCounter);
}


int kernelDiskGetPhysicalCount(void)
{
  // Returns the number of registered physical disk structures.  Useful for
  // iterating through calls to kernelGetDiskByName or kernelDiskGetInfo

  if (!initialized)
    return (ERR_NOTINITIALIZED);

  return (physicalDiskCounter);
}


int kernelDiskGet(const char *diskName, disk *userDisk)
{
  // Given a disk name, return the corresponding user space disk structure

  int status = 0;
  kernelPhysicalDisk *physicalDisk = NULL;
  kernelDisk *logicalDisk = NULL;

  if (!initialized)
    return (status = ERR_NOTINITIALIZED);

  // Check params
  if ((diskName == NULL) || (userDisk == NULL))
    return (status = ERR_NULLPARAMETER);

  // Find the disk structure.

  // Try for a logical disk first.
  if ((logicalDisk = kernelDiskGetByName(diskName)))
    return(kernelDiskFromLogical(logicalDisk, userDisk));

  // Try physical instead
  else if ((physicalDisk = getPhysicalByName(diskName)))
    return(diskFromPhysical(physicalDisk, userDisk));

  else
    // No such disk.
    return (status = ERR_NOSUCHENTRY);
}


int kernelDiskGetAll(disk *userDiskArray, unsigned buffSize)
{
  // Return user space disk structures for each logical disk, up to
  // buffSize bytes

  int status = 0;
  unsigned doDisks = logicalDiskCounter;
  unsigned count;

  if (!initialized)
    return (status = ERR_NOTINITIALIZED);

  // Check params
  if (userDiskArray == NULL)
    return (status = ERR_NULLPARAMETER);

  if ((buffSize / sizeof(disk)) < doDisks)
    doDisks = (buffSize / sizeof(disk));

  // Loop through the disks, filling the array supplied
  for (count = 0; count < doDisks; count ++)
    kernelDiskFromLogical(logicalDisks[count], &userDiskArray[count]);

  return (status = 0);
}


int kernelDiskGetAllPhysical(disk *userDiskArray, unsigned buffSize)
{
  // Return user space disk structures for each physical disk, up to
  // buffSize bytes

  int status = 0;
  unsigned doDisks = physicalDiskCounter;
  unsigned count;

  if (!initialized)
    return (status = ERR_NOTINITIALIZED);

  // Check params
  if (userDiskArray == NULL)
    return (status = ERR_NULLPARAMETER);
 
  if ((buffSize / sizeof(disk)) < doDisks)
    doDisks = (buffSize / sizeof(disk));
 
  // Loop through the physical disks, filling the array supplied
  for (count = 0; count < doDisks; count ++)
    diskFromPhysical(physicalDisks[count], &userDiskArray[count]);

   return (status = 0);
}


int kernelDiskGetFilesystemType(const char *diskName, char *buffer,
				unsigned buffSize)
{
  // This function takes the supplied disk name and attempts to explicitly
  // detect the filesystem type.  Particularly useful for things like removable
  // media where the correct info may not be automatically provided in the
  // disk structure.

  int status = 0;
  kernelDisk *logicalDisk = NULL;

  if (!initialized)
    return (status = ERR_NOTINITIALIZED);

  // Check params
  if (diskName == NULL)
    return (status = ERR_NULLPARAMETER);

  // There must exist a logical disk with this name.
  logicalDisk = kernelDiskGetByName(diskName);
  if (logicalDisk == NULL)
    {
      // No such disk.
      kernelError(kernel_error, "No such disk \"%s\"", diskName);
      return (status = ERR_NOSUCHENTRY);
    }

  // See if we can determine the filesystem type
  status = kernelFilesystemScan(logicalDisk);
  if (status < 0)
    return (status);

  strncpy(buffer, (char *) logicalDisk->fsType, buffSize);
  return (status = 0);
}


int kernelDiskGetPartType(int code, partitionType *partType)
{
  // This function takes the supplied code and returns a corresponding
  // partition type structure in the memory provided.

  int status = 0;
  int count;

  // We don't check for initialization; the table is static.

  if (partType == NULL)
    return (status = ERR_NULLPARAMETER);

  for (count = 0; (partitionTypes[count].code != 0); count ++)
    if (partitionTypes[count].code == code)
      {
	kernelMemCopy(&(partitionTypes[count]), partType,
		      sizeof(partitionType));
	break;
      }

  return (status = 0);
}


partitionType *kernelDiskGetPartTypes(void)
{
  // Allocate and return a copy of our table of known partition types
  // We don't check for initialization; the table is static.

  partitionType *types =
    kernelMemoryGet(sizeof(partitionTypes), "partition types");
  if (types == NULL)
    return (types);

  kernelMemCopy(partitionTypes, types, sizeof(partitionTypes));
  return (types);
}


int kernelDiskSetLockState(const char *diskName, int state)
{
  // This routine is the user-accessible interface for locking or unlocking
  // a removable disk device.

  int status = 0;
  kernelDisk *logicalDisk = NULL;
  kernelPhysicalDisk *physicalDisk = NULL;

  if (!initialized)
    return (status = ERR_NOTINITIALIZED);

  // Check params
  if (diskName == NULL)
    return (status = ERR_NULLPARAMETER);

  // Get the disk structure
  physicalDisk = getPhysicalByName(diskName);
  if (physicalDisk == NULL)
    {
      // Try logical
      if ((logicalDisk = kernelDiskGetByName(diskName)))
	physicalDisk = logicalDisk->physical;
      else
	return (status = ERR_NOSUCHENTRY);
    }

  // Reset the 'idle since' value
  physicalDisk->idleSince = kernelSysTimerRead();
  
  // Make sure the operation is supported
  if (((kernelDiskOps *) physicalDisk->driver->ops)
      ->driverSetLockState == NULL)
    {
      kernelError(kernel_error, "Driver routine is NULL");
      return (status = ERR_NOSUCHFUNCTION);
    }
  
  // Lock the disk
  status = kernelLockGet(&(physicalDisk->diskLock));
  if (status < 0)
    return (status = ERR_NOLOCK);

  // Call the door lock operation
  status = ((kernelDiskOps *) physicalDisk->driver->ops)
    ->driverSetLockState(physicalDisk->deviceNumber, state);

  // Reset the 'idle since' value
  physicalDisk->idleSince = kernelSysTimerRead();

  // Unlock the disk
  kernelLockRelease(&(physicalDisk->diskLock));

  return (status);
}


int kernelDiskSetDoorState(const char *diskName, int state)
{
  // This routine is the user-accessible interface for opening or closing
  // a removable disk device.

  int status = 0;
  kernelDisk *logicalDisk = NULL;
  kernelPhysicalDisk *physicalDisk = NULL;

  if (!initialized)
    return (status = ERR_NOTINITIALIZED);

  // Check params
  if (diskName == NULL)
    return (status = ERR_NULLPARAMETER);

  // Get the disk structure
  physicalDisk = getPhysicalByName(diskName);
  if (physicalDisk == NULL)
    {
      // Try logical
      if ((logicalDisk = kernelDiskGetByName(diskName)))
	physicalDisk = logicalDisk->physical;
      else
	return (status = ERR_NOSUCHENTRY);
    }

  // Make sure it's a removable disk
  if (physicalDisk->flags & DISKFLAG_FIXED)
    {
      kernelError(kernel_error, "Cannot open/close a non-removable disk");
      return (status = ERR_INVALID);
    }

  // Reset the 'idle since' value
  physicalDisk->idleSince = kernelSysTimerRead();
  
  // Make sure the operation is supported
  if (((kernelDiskOps *) physicalDisk->driver->ops)
      ->driverSetDoorState == NULL)
    {
      kernelError(kernel_error, "Driver routine is NULL");
      return (status = ERR_NOSUCHFUNCTION);
    }
  
  // Lock the disk
  status = kernelLockGet(&(physicalDisk->diskLock));
  if (status < 0)
    return (status = ERR_NOLOCK);

#if (DISK_CACHE)
  // Make sure the cache is invalidated
  cacheInvalidate(physicalDisk);
#endif

  // Call the door control operation
  status = ((kernelDiskOps *) physicalDisk->driver->ops)
    ->driverSetDoorState(physicalDisk->deviceNumber, state);

  // Reset the 'idle since' value
  physicalDisk->idleSince = kernelSysTimerRead();

  // Unlock the disk
  kernelLockRelease(&(physicalDisk->diskLock));

  return (status);
}


int kernelDiskGetMediaState(const char *diskName)
{
  // This routine returns 1 if the requested disk has media present,
  // 0 otherwise

  int status = 0;
  kernelDisk *logicalDisk = NULL;
  kernelPhysicalDisk *physicalDisk = NULL;
  void *buffer = NULL;

  if (!initialized)
    return (status = 0);

  // Check params
  if (diskName == NULL)
    return (status = 0);

  // Get the disk structure
  physicalDisk = getPhysicalByName(diskName);
  if (physicalDisk == NULL)
    {
      // Try logical
      if ((logicalDisk = kernelDiskGetByName(diskName)))
	physicalDisk = logicalDisk->physical;
      else
	return (status = 0);
    }

  // Make sure it's a removable disk
  if (!(physicalDisk->flags & DISKFLAG_REMOVABLE))
    return (status = 1);

  // Reset the 'idle since' value
  physicalDisk->idleSince = kernelSysTimerRead();

  buffer = kernelMalloc(physicalDisk->sectorSize);
  if (buffer == NULL)
    return (status = 0);

  // Try to read one sector
  status = readWriteSectors(physicalDisk, 0, 1, buffer,
			    (IOMODE_READ | IOMODE_NOCACHE));

  kernelFree(buffer);

  if (status < 0)
    return (0);
  else
    return (1);  
}


int kernelDiskReadSectors(const char *diskName, unsigned logicalSector,
			  unsigned numSectors, void *dataPointer)
{
  // This routine is the user-accessible interface to reading data using
  // the various disk routines in this file.  Basically, it is a gatekeeper
  // that helps ensure correct use of the "read-write" method.  

  int status = 0;
  kernelPhysicalDisk *physicalDisk = NULL;
  kernelDisk *theDisk = NULL;

  if (!initialized)
    return (status = ERR_NOTINITIALIZED);

  // Check params
  if ((diskName == NULL) || (dataPointer == NULL))
    return (status = ERR_NULLPARAMETER);

  // Get the disk structure.  Try a physical disk first.
  physicalDisk = getPhysicalByName(diskName);
  if (physicalDisk == NULL)
    {
      // Try logical
      theDisk = kernelDiskGetByName(diskName);
      if (theDisk == NULL)
	// No such disk.
	return (status = ERR_NOSUCHENTRY);

      // Start at the beginning of the logical volume.
      logicalSector += theDisk->startSector;
      
      // Make sure the logical sector number does not exceed the number
      // of logical sectors on this volume
      if ((logicalSector >= (theDisk->startSector + theDisk->numSectors)) ||
	  ((logicalSector + numSectors) >
	   (theDisk->startSector + theDisk->numSectors)))
	{
	  // Make a kernelError.
	  kernelError(kernel_error, "Sector range %u-%u exceeds volume "
		      "boundary of %u", logicalSector,
		      (logicalSector + numSectors - 1),
		      (theDisk->startSector + theDisk->numSectors));
	  return (status = ERR_BOUNDS);
	}

      physicalDisk = theDisk->physical;
    }

  // Reset the 'idle since' value
  physicalDisk->idleSince = kernelSysTimerRead();
  
  // Lock the disk
  status = kernelLockGet(&(physicalDisk->diskLock));
  if (status < 0)
    return (status = ERR_NOLOCK);

  // Call the read-write routine for a read operation
  status = readWriteSectors(physicalDisk, logicalSector, numSectors,
			    dataPointer, IOMODE_READ);

  // Reset the 'idle since' value
  physicalDisk->idleSince = kernelSysTimerRead();
  
  // Unlock the disk
  kernelLockRelease(&(physicalDisk->diskLock));
  
  return (status);
}


int kernelDiskWriteSectors(const char *diskName, unsigned logicalSector, 
			   unsigned numSectors, const void *dataPointer)
{
  // This routine is the user-accessible interface to writing data using
  // the various disk routines in this file.  Basically, it is a gatekeeper
  // that helps ensure correct use of the "read-write" method.  
  
  int status = 0;
  kernelPhysicalDisk *physicalDisk = NULL;
  kernelDisk *theDisk = NULL;

  if (!initialized)
    return (status = ERR_NOTINITIALIZED);

  // Check params
  if ((diskName == NULL) || (dataPointer == NULL))
    return (status = ERR_NULLPARAMETER);

  // Get the disk structure.  Try a physical disk first.
  physicalDisk = getPhysicalByName(diskName);
  if (physicalDisk == NULL)
    {
      // Try logical
      theDisk = kernelDiskGetByName(diskName);
      if (theDisk == NULL)
	// No such disk.
	return (status = ERR_NOSUCHENTRY);

      // Start at the beginning of the logical volume.
      logicalSector += theDisk->startSector;
      
      // Make sure the logical sector number does not exceed the number
      // of logical sectors on this volume
      if ((logicalSector >= (theDisk->startSector + theDisk->numSectors)) ||
	  ((logicalSector + numSectors) >
	   (theDisk->startSector + theDisk->numSectors)))
	{
	  // Make a kernelError.
	  kernelError(kernel_error, "Exceeding volume boundary");
	  return (status = ERR_BOUNDS);
	}
      
      physicalDisk = theDisk->physical;
    }

  // Reset the 'idle since' value
  physicalDisk->idleSince = kernelSysTimerRead();
  
  // Lock the disk
  status = kernelLockGet(&(physicalDisk->diskLock));
  if (status < 0)
    return (status = ERR_NOLOCK);

  // Call the read-write routine for a write operation
  status = readWriteSectors(physicalDisk, logicalSector, numSectors,
			    (void *) dataPointer, IOMODE_WRITE);

  // Reset the 'idle since' value
  physicalDisk->idleSince = kernelSysTimerRead();
  
  // Unlock the disk
  kernelLockRelease(&(physicalDisk->diskLock));

  return (status);
}


