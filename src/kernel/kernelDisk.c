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
//  kernelDisk.c
//

// These are the generic functions for disk access.  These are below the level
// of the filesystem, and will generally be called by the filesystem drivers.

#include "kernelDisk.h"
#include "kernelCpu.h"
#include "kernelDebug.h"
#include "kernelError.h"
#include "kernelFilesystem.h"
#include "kernelLock.h"
#include "kernelLog.h"
#include "kernelMain.h"
#include "kernelMalloc.h"
#include "kernelMemory.h"
#include "kernelMisc.h"
#include "kernelMultitasker.h"
#include "kernelParameters.h"
#include "kernelRandom.h"
#include "kernelSysTimer.h"
#include "kernelVariableList.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/gpt.h>
#include <sys/iso.h>
#include <sys/msdos.h>

// All the disks
static kernelPhysicalDisk *physicalDisks[DISK_MAXDEVICES];
static volatile int physicalDiskCounter = 0;
static kernelDisk *logicalDisks[DISK_MAXDEVICES];
static volatile int logicalDiskCounter = 0;

// The name of the disk we booted from
static char bootDisk[DISK_MAX_NAMELENGTH];

// Modes for the readWriteSectors function
#define IOMODE_READ		0x01
#define IOMODE_WRITE	0x02
#define IOMODE_NOCACHE	0x04

// For the disk thread
static int threadPid = 0;

// This is a table for keeping known MS-DOS partition type codes and
// descriptions
static msdosPartType msdosPartTypes[] = {
	{ MSDOSTAG_FAT12, "FAT12"},
	{ 0x02, "XENIX root"},
	{ 0x03, "XENIX /usr"},
	{ MSDOSTAG_FAT16_SM, "FAT16 (small)"},
	{ MSDOSTAG_EXTD, "Extended"},
	{ MSDOSTAG_FAT16, "FAT16"},
	{ MSDOSTAG_HPFS_NTFS, "NTFS or HPFS"},
	{ 0x08, "OS/2 or AIX boot"},
	{ 0x09, "AIX data"},
	{ 0x0A, "OS/2 Boot Manager"},
	{ MSDOSTAG_FAT32, "FAT32"},
	{ MSDOSTAG_FAT32_LBA, "FAT32 (LBA)"},
	{ MSDOSTAG_FAT16_LBA, "FAT16 (LBA)"},
	{ MSDOSTAG_EXTD_LBA, "Extended (LBA)"},
	{ MSDOSTAG_HIDDEN_FAT12, "Hidden FAT12"},
	{ 0x12, "FAT diagnostic"},
	{ MSDOSTAG_HIDDEN_FAT16_SM, "Hidden FAT16 (small)"},
	{ MSDOSTAG_HIDDEN_FAT16, "Hidden FAT16"},
	{ MSDOSTAG_HIDDEN_HPFS_NTFS, "Hidden HPFS or NTFS"},
	{ MSDOSTAG_HIDDEN_FAT32, "Hidden FAT32"},
	{ MSDOSTAG_HIDDEN_FAT32_LBA, "Hidden FAT32 (LBA)"},
	{ MSDOSTAG_HIDDEN_FAT16_LBA, "Hidden FAT16 (LBA)"},
	{ 0x35, "JFS" },
	{ 0x39, "Plan 9" },
	{ 0x3C, "PartitionMagic" },
	{ 0x3D, "Hidden Netware" },
	{ 0x41, "PowerPC PReP" },
	{ 0x42, "Win2K dynamic extended" },
	{ 0x44, "GoBack" },
	{ 0x4D, "QNX4.x" },
	{ 0x4D, "QNX4.x 2nd" },
	{ 0x4D, "QNX4.x 3rd" },
	{ 0x50, "Ontrack R/O" },
	{ 0x51, "Ontrack R/W or Novell" },
	{ 0x52, "CP/M" },
	{ 0x63, "GNU HURD or UNIX SysV"},
	{ 0x64, "Netware 2"},
	{ 0x65, "Netware 3/4"},
	{ 0x66, "Netware SMS"},
	{ 0x67, "Novell"},
	{ 0x68, "Novell"},
	{ 0x69, "Netware 5+"},
	{ 0x7E, "Veritas VxVM public"},
	{ 0x7F, "Veritas VxVM private"},
	{ 0x80, "Minix"},
	{ 0x81, "Linux or Minix"},
	{ 0x82, "Linux swap or Solaris"},
	{ MSDOSTAG_LINUX, "Linux"},
	{ 0x84, "Hibernation"},
	{ MSDOSTAG_EXTD_LINUX, "Linux extended"},
	{ 0x86, "HPFS or NTFS mirrored"},
	{ 0x87, "HPFS or NTFS mirrored"},
	{ 0x8E, "Linux LVM"},
	{ MSDOSTAG_HIDDEN_LINUX, "Hidden Linux"},
	{ 0x9F, "BSD/OS"},
	{ 0xA0, "Laptop hibernation"},
	{ 0xA1, "Laptop hibernation"},
	{ 0xA5, "BSD, NetBSD, FreeBSD"},
	{ 0xA6, "OpenBSD"},
	{ 0xA7, "NeXTSTEP"},
	{ 0xA8, "OS-X UFS"},
	{ 0xA9, "NetBSD"},
	{ 0xAB, "OS-X boot"},
	{ 0xAF, "OS-X HFS"},
	{ 0xB6, "NT corrupt mirror"},
	{ 0xB7, "BSDI"},
	{ 0xB8, "BSDI swap"},
	{ 0xBE, "Solaris 8 boot"},
	{ 0xBF, "Solaris x86"},
	{ 0xC0, "NTFT"},
	{ 0xC1, "DR-DOS FAT12"},
	{ 0xC2, "Hidden Linux"},
	{ 0xC3, "Hidden Linux swap"},
	{ 0xC4, "DR-DOS FAT16 (small)"},
	{ 0xC5, "DR-DOS Extended"},
	{ 0xC6, "DR-DOS FAT16"},
	{ 0xC7, "HPFS mirrored"},
	{ 0xCB, "DR-DOS FAT32"},
	{ 0xCC, "DR-DOS FAT32 (LBA)"},
	{ 0xCE, "DR-DOS FAT16 (LBA)"},
	{ 0xD0, "MDOS"},
	{ 0xD1, "MDOS FAT12"},
	{ 0xD4, "MDOS FAT16 (small)"},
	{ 0xD5, "MDOS Extended"},
	{ 0xD6, "MDOS FAT16"},
	{ 0xD8, "CP/M-86"},
	{ 0xEB, "BeOS BFS"},
	{ MSDOSTAG_EFI_GPT_PROT, "EFI GPT protective"},
	{ 0xEF, "EFI filesystem"},
	{ 0xF0, "Linux/PA-RISC boot"},
	{ 0xF2, "DOS 3.3+ second"},
	{ 0xFA, "Bochs"},
	{ 0xFB, "VmWare"},
	{ 0xFC, "VmWare swap"},
	{ 0xFD, "Linux RAID"},
	{ 0xFE, "NT hidden"},
	{ 0, "" }
};

// This is a table for keeping known GPT partition type GUIDs and descriptions
static gptPartType gptPartTypes[] = {
	{ GUID_MBRPART,		GUID_MBRPART_DESC },
	{ GUID_EFISYS,		GUID_EFISYS_DESC },
	{ GUID_BIOSBOOT,	GUID_BIOSBOOT_DESC },
	{ GUID_MSRES,		GUID_MSRES_DESC},
	{ GUID_WINDATA,		GUID_WINDATA_DESC },
	{ GUID_WINLDMMETA,	GUID_WINLDMMETA_DESC },
	{ GUID_WINLDMDATA,	GUID_WINLDMDATA_DESC },
	{ GUID_WINRECOVER,	GUID_WINRECOVER_DESC },
	{ GUID_IMBGPFS,		GUID_IMBGPFS_DESC },
	{ GUID_HPUXDATA,	GUID_HPUXDATA_DESC },
	{ GUID_HPUXSERV,	GUID_HPUXSERV_DESC },
	{ GUID_LINUXDATA,	GUID_LINUXDATA_DESC },
	{ GUID_LINUXRAID,	GUID_LINUXRAID_DESC },
	{ GUID_LINUXSWAP,	GUID_LINUXSWAP_DESC },
	{ GUID_LINUXLVM,	GUID_LINUXLVM_DESC },
	{ GUID_LINUXRES,	GUID_LINUXRES_DESC },
	{ GUID_FREEBSDBOOT,	GUID_FREEBSDBOOT_DESC },
	{ GUID_FREEBSDDATA,	GUID_FREEBSDDATA_DESC },
	{ GUID_FREEBSDSWAP,	GUID_FREEBSDSWAP_DESC },
	{ GUID_FREEBSDUFS,	GUID_FREEBSDUFS_DESC },
	{ GUID_FREEBSDVIN,	GUID_FREEBSDVIN_DESC },
	{ GUID_FREEBSDZFS,	GUID_FREEBSDZFS_DESC },
	{ GUID_MACOSXHFS,	GUID_MACOSXHFS_DESC },
	{ GUID_APPLEUFS,	GUID_APPLEUFS_DESC },
	{ GUID_APPLERAID,	GUID_APPLERAID_DESC },
	{ GUID_APPLERDOFFL,	GUID_APPLERDOFFL_DESC },
	{ GUID_APPLEBOOT,	GUID_APPLEBOOT_DESC },
	{ GUID_APPLELABEL,	GUID_APPLELABEL_DESC },
	{ GUID_APPLETVRECV,	GUID_APPLETVRECV_DESC },
	{ GUID_APPLECOREST,	GUID_APPLECOREST_DESC },
	{ GUID_SOLBOOT,		GUID_SOLBOOT_DESC },
	{ GUID_SOLROOT,		GUID_SOLROOT_DESC },
	{ GUID_SOLSWAP,		GUID_SOLSWAP_DESC },
	{ GUID_SOLBACKUP,	GUID_SOLBACKUP_DESC },
	{ GUID_SOLUSR,		GUID_SOLUSR_DESC },
	{ GUID_SOLVAR,		GUID_SOLVAR_DESC },
	{ GUID_SOLHOME,		GUID_SOLHOME_DESC },
	{ GUID_SOLALTSECT,	GUID_SOLALTSECT_DESC },
	{ GUID_SOLRES1,		GUID_SOLRES1_DESC },
	{ GUID_SOLRES2,		GUID_SOLRES2_DESC },
	{ GUID_SOLRES3,		GUID_SOLRES3_DESC },
	{ GUID_SOLRES4,		GUID_SOLRES4_DESC },
	{ GUID_SOLRES5,		GUID_SOLRES5_DESC },
	{ GUID_NETBSDSWAP,	GUID_NETBSDSWAP_DESC },
	{ GUID_NETBSDFFS,	GUID_NETBSDFFS_DESC },
	{ GUID_NETBSDLFS,	GUID_NETBSDLFS_DESC },
	{ GUID_NETBSDRAID,	GUID_NETBSDRAID_DESC },
	{ GUID_NETBSDCONCT,	GUID_NETBSDCONCT_DESC },
	{ GUID_NETBSDENCR,	GUID_NETBSDENCR_DESC },
	{ GUID_CHROMEKERN,	GUID_CHROMEKERN_DESC },
	{ GUID_CHROMEROOT,	GUID_CHROMEROOT_DESC },
	{ GUID_CHROMEFUT,	GUID_CHROMEFUT_DESC },
	{ GUID_UNUSED,		GUID_UNUSED_DESC }
};

static int initialized = 0;


#if defined(DEBUG)
static void debugLockCheck(kernelPhysicalDisk *physicalDisk,
	const char *function)
{
	if (physicalDisk->lock.processId != kernelMultitaskerGetCurrentProcessId())
	{
		kernelError(kernel_error, "%s is not locked by process %d in function "
			"%s", physicalDisk->name, kernelMultitaskerGetCurrentProcessId(),
			function);
		while (1);
	}
}
#else
	#define debugLockCheck(physicalDisk, function) do { } while (0)
#endif // DEBUG


static int motorOff(kernelPhysicalDisk *physicalDisk)
{
	// Calls the target disk driver's 'motor off' function.

	int status = 0;
	kernelDiskOps *ops = (kernelDiskOps *) physicalDisk->driver->ops;

	debugLockCheck(physicalDisk, __FUNCTION__);

	// If it's a fixed disk, we don't turn the motor off, for now
	if (physicalDisk->type & DISKTYPE_FIXED)
		return (status = 0);

	// Make sure the motor isn't already off
	if (!(physicalDisk->flags & DISKFLAG_MOTORON))
		return (status = 0);

	// Make sure the device driver function is available.
	if (!ops->driverSetMotorState)
		// Don't make this an error.  It's just not available in some drivers.
		return (status = 0);

	// Ok, now turn the motor off
	status = ops->driverSetMotorState(physicalDisk->deviceNumber, 0);
	if (status < 0)
		return (status);

	// Make note of the fact that the motor is off
	physicalDisk->flags &= ~DISKFLAG_MOTORON;

	return (status);
}


__attribute__((noreturn))
static void diskThread(void)
{
	// This thread will be spawned at inititialization time to do any required
	// ongoing operations on disks, such as shutting off floppy and CD/DVD
	// motors

	kernelPhysicalDisk *physicalDisk = NULL;
	int count;

	// Don't try to do anything until we have registered disks
	while (!initialized || (physicalDiskCounter <= 0))
		kernelMultitaskerWait(3 * MS_PER_SEC);

	while (1)
	{
		// Loop for each physical disk
		for (count = 0; count < physicalDiskCounter; count ++)
		{
			physicalDisk = physicalDisks[count];

			// If the disk is a floppy and has been idle for >= 2 seconds,
			// turn off the motor.
			if ((physicalDisk->type & DISKTYPE_FLOPPY) &&
				(kernelSysTimerRead() > (physicalDisk->lastAccess + 40)))
			{
				// Lock the disk
				if (kernelLockGet(&physicalDisk->lock) < 0)
					continue;

				motorOff(physicalDisk);

				// Unlock the disk
				kernelLockRelease(&physicalDisk->lock);
			}
		}

		// Yield the rest of the timeslice and wait for 1 second
		kernelMultitaskerWait(MS_PER_SEC);
	}
}


static int spawnDiskThread(void)
{
	// Launches the disk thread

	threadPid = kernelMultitaskerSpawnKernelThread(diskThread, "disk thread",
		0, NULL);
	if (threadPid < 0)
		return (threadPid);

	// Re-nice the disk thread
	kernelMultitaskerSetProcessPriority(threadPid, (PRIORITY_LEVELS - 2));

	// Success
	return (threadPid);
}


static int realReadWrite(kernelPhysicalDisk *physicalDisk, uquad_t startSector,
	uquad_t numSectors, void *data, unsigned mode)
{
	// This function does all real, physical disk reads or writes.

	int status = 0;
	kernelDiskOps *ops = (kernelDiskOps *) physicalDisk->driver->ops;
	processState tmpState;

	debugLockCheck(physicalDisk, __FUNCTION__);

	// Update the 'last access' value
	physicalDisk->lastAccess = kernelSysTimerRead();

	// Make sure the disk thread is running
	if (kernelMultitaskerGetProcessState(threadPid, &tmpState) < 0)
		// Re-spawn the disk thread
		spawnDiskThread();

	// Make sure the device driver function is available.
	if (((mode & IOMODE_READ) && !ops->driverReadSectors) ||
		((mode & IOMODE_WRITE) && !ops->driverWriteSectors))
	{
		kernelError(kernel_error, "Disk %s cannot %s", physicalDisk->name,
			((mode & IOMODE_READ)? "read" : "write"));
		return (status = ERR_NOSUCHFUNCTION);
	}

	// Do the actual read/write operation

	kernelDebug(debug_io, "Disk %s %s %llu sectors at %llu",
		physicalDisk->name, ((mode & IOMODE_READ)? "read" : "write"),
		numSectors, startSector);

	if (mode & IOMODE_READ)
		status = ops->driverReadSectors(physicalDisk->deviceNumber,
			startSector, numSectors, data);
	else
		status = ops->driverWriteSectors(physicalDisk->deviceNumber,
			startSector, numSectors, data);

	kernelDebug(debug_io, "Disk %s done %sing %llu sectors at %llu",
		physicalDisk->name, ((mode & IOMODE_READ)? "read" : "writ"),
		numSectors, startSector);

	// Update the 'last access' value again
	physicalDisk->lastAccess = kernelSysTimerRead();

	if (status < 0)
	{
		// If it is a write-protect error, mark the disk as read only
		if ((mode & IOMODE_WRITE) && (status == ERR_NOWRITE))
		{
			kernelError(kernel_error, "Disk %s is write-protected",
				physicalDisk->name);
			physicalDisk->flags |= DISKFLAG_READONLY;
		}
		else
		{
			kernelError(kernel_error, "Error %d %sing %llu sectors at %llu, "
				"disk %s", status, ((mode & IOMODE_READ)? "read" : "writ"),
				numSectors, startSector, physicalDisk->name);
		}
	}

	return (status);
}


#if (DISK_CACHE)

#define bufferEnd(buffer) (buffer->startSector + buffer->numSectors - 1)
#define bufferBytes(physicalDisk, buffer) \
	(buffer->numSectors * physicalDisk->sectorSize)

static inline void cacheMarkDirty(kernelPhysicalDisk *physicalDisk,
	kernelDiskCacheBuffer *buffer)
{
	if (!buffer->dirty)
	{
		buffer->dirty = 1;
		physicalDisk->cache.dirty += 1;
	}
}


static inline void cacheMarkClean(kernelPhysicalDisk *physicalDisk,
	kernelDiskCacheBuffer *buffer)
{
	if (buffer->dirty)
	{
		buffer->dirty = 0;
		physicalDisk->cache.dirty -= 1;
	}
}


static int cacheSync(kernelPhysicalDisk *physicalDisk)
{
	// Write all dirty cached buffers to the disk

	int status = 0;
	kernelDiskCacheBuffer *buffer = physicalDisk->cache.buffer;
	int errors = 0;

	debugLockCheck(physicalDisk, __FUNCTION__);

	if (!physicalDisk->cache.dirty || (physicalDisk->flags & DISKFLAG_READONLY))
		return (status = 0);

	while (buffer)
	{
		if (buffer->dirty)
		{
			status = realReadWrite(physicalDisk, buffer->startSector,
				buffer->numSectors, buffer->data, IOMODE_WRITE);
			if (status < 0)
				errors = status;
			else
				cacheMarkClean(physicalDisk, buffer);
		}

		buffer = buffer->next;
	}

	return (status = errors);
}


static kernelDiskCacheBuffer *cacheGetBuffer(kernelPhysicalDisk *physicalDisk,
	uquad_t startSector, uquad_t numSectors)
{
	// Get a new cache buffer for the specified number of sectors.

	kernelDiskCacheBuffer *buffer = NULL;

	debugLockCheck(physicalDisk, __FUNCTION__);

	// Get memory for the structure
	buffer = kernelMalloc(sizeof(kernelDiskCacheBuffer));
	if (!buffer)
		return (buffer);

	buffer->startSector = startSector;
	buffer->numSectors = numSectors;

	// Get memory for the data
	buffer->data = kernelMalloc(numSectors * physicalDisk->sectorSize);
	if (!buffer->data)
	{
		kernelFree((void *) buffer);
		return (buffer = NULL);
	}

	return (buffer);
}


static inline void cachePutBuffer(kernelDiskCacheBuffer *buffer)
{
	// Deallocate a cache buffer.

	if (buffer->data)
		kernelFree(buffer->data);

	kernelFree((void *) buffer);

	return;
}


static int cacheInvalidate(kernelPhysicalDisk *physicalDisk)
{
	// Invalidate the disk cache, syncing dirty sectors first.

	int status = 0;
	kernelDiskCacheBuffer *buffer = physicalDisk->cache.buffer;
	kernelDiskCacheBuffer *next = NULL;

	debugLockCheck(physicalDisk, __FUNCTION__);

	// Try to sync dirty sectors first.
	cacheSync(physicalDisk);

	if (physicalDisk->cache.dirty)
		kernelError(kernel_warn, "Invalidating dirty disk cache!");

	while (buffer)
	{
		next = buffer->next;
		cachePutBuffer(buffer);
		buffer = next;
	}

	physicalDisk->cache.buffer = NULL;
	physicalDisk->cache.size = 0;
	physicalDisk->cache.dirty = 0;

	return (status);
}


static kernelDiskCacheBuffer *cacheFind(kernelPhysicalDisk *physicalDisk,
	uquad_t startSector, uquad_t numSectors)
{
	// Finds the first buffer that intersects the supplied range of sectors.
	// If not found, return NULL.

	uquad_t endSector = (startSector + numSectors - 1);
	kernelDiskCacheBuffer *buffer = physicalDisk->cache.buffer;

	debugLockCheck(physicalDisk, __FUNCTION__);

	while (buffer)
	{
		// Start sector inside buffer?
		if ((startSector >= buffer->startSector) &&
			(startSector <= bufferEnd(buffer)))
		{
			return (buffer);
		}

		// End sector inside buffer?
		if ((endSector >= buffer->startSector) &&
			(endSector <= bufferEnd(buffer)))
		{
			return (buffer);
		}

		// Range overlaps buffer?
		if ((startSector < buffer->startSector) &&
			(endSector > bufferEnd(buffer)))
		{
			return (buffer);
		}

		buffer = buffer->next;
	}

	// Not found
	return (buffer = NULL);
}


static unsigned cacheQueryRange(kernelPhysicalDisk *physicalDisk,
	uquad_t startSector, uquad_t numSectors, uquad_t *firstCached)
{
	// Search the cache for a range of sectors.  If any of the range is cached,
	// return the *first* portion that is cached.

	kernelDiskCacheBuffer *buffer = NULL;
	uquad_t numCached = 0;

	debugLockCheck(physicalDisk, __FUNCTION__);

	buffer = cacheFind(physicalDisk, startSector, numSectors);
	if (buffer)
	{
		*firstCached = max(startSector, buffer->startSector);
		numCached = min((numSectors - (*firstCached - startSector)),
			(buffer->numSectors - (*firstCached - buffer->startSector)));
		kernelDebug(debug_io, "Disk %s found %llu->%llu in %llu->%llu, "
			"first=%llu num=%llu", physicalDisk->name, startSector,
			(startSector + numSectors - 1), buffer->startSector,
			bufferEnd(buffer), *firstCached, numCached);
	}
	else
	{
		kernelDebug(debug_io, "Disk %s %llu->%llu not found",
			physicalDisk->name, startSector, (startSector + numSectors - 1));
	}

	return (numCached);
}


#if defined(DEBUG)
static void cachePrint(kernelPhysicalDisk *physicalDisk)
{
	kernelDiskCacheBuffer *buffer = physicalDisk->cache.buffer;

	while (buffer)
	{
		kernelTextPrintLine("%s cache: %llu->%llu (%llu sectors) %s",
			physicalDisk->name, buffer->startSector, bufferEnd(buffer),
			buffer->numSectors, (buffer->dirty? "(dirty)" : ""));
		buffer = buffer->next;
	}
}


static void cacheCheck(kernelPhysicalDisk *physicalDisk)
{
	kernelDiskCacheBuffer *buffer = physicalDisk->cache.buffer;
	uquad_t cacheSize = 0;
	uquad_t numDirty = 0;

	while (buffer)
	{
		if (buffer->next)
		{
			if (buffer->startSector >= buffer->next->startSector)
			{
				kernelError(kernel_warn, "%s startSector (%llu) >= "
					"next->startSector (%llu)", physicalDisk->name,
					buffer->startSector, buffer->next->startSector);
				cachePrint(physicalDisk); while (1);
			}

			if (bufferEnd(buffer) >= buffer->next->startSector)
			{
				kernelError(kernel_warn, "%s (startSector(%llu) + "
					"numSectors(%llu) = %llu) > next->startSector(%llu)",
					physicalDisk->name, buffer->startSector, buffer->numSectors,
					(buffer->startSector + buffer->numSectors),
					buffer->next->startSector);
				cachePrint(physicalDisk); while (1);
			}

			if ((bufferEnd(buffer) == (buffer->next->startSector - 1)) &&
				(buffer->dirty == buffer->next->dirty))
			{
				kernelError(kernel_warn, "%s buffer %llu->%llu should be "
					"joined with %llu->%llu (%s)", physicalDisk->name,
					buffer->startSector, bufferEnd(buffer),
					buffer->next->startSector, bufferEnd(buffer->next),
					(buffer->dirty? "dirty" : "clean"));
				cachePrint(physicalDisk); while (1);
			}

			if (buffer->next->prev != buffer)
			{
				kernelError(kernel_warn, "%s buffer->next->prev != buffer",
					physicalDisk->name);
				cachePrint(physicalDisk); while (1);
			}
		}

		if (buffer->prev)
		{
			if (buffer->prev->next != buffer)
			{
				kernelError(kernel_warn, "%s buffer->prev->next != buffer",
					physicalDisk->name);
				cachePrint(physicalDisk); while (1);
			}
		}

		cacheSize += bufferBytes(physicalDisk, buffer);
		if (buffer->dirty)
			numDirty += 1;

		buffer = buffer->next;
	}

	if (cacheSize != physicalDisk->cache.size)
	{
		kernelError(kernel_warn, "%s cacheSize(%llu) != "
			"physicalDisk->cache.size(%llu)", physicalDisk->name, cacheSize,
			physicalDisk->cache.size);
		cachePrint(physicalDisk); while (1);
	}

	if (numDirty != physicalDisk->cache.dirty)
	{
		kernelError(kernel_warn, "%s numDirty(%llu) != "
			"physicalDisk->cache.dirty(%llu)", physicalDisk->name, numDirty,
			physicalDisk->cache.dirty);
		cachePrint(physicalDisk); while (1);
	}
}
#else
	#define cacheCheck(physicalDisk) do { } while (0)
#endif // DEBUG


static void cacheRemove(kernelPhysicalDisk *physicalDisk,
	kernelDiskCacheBuffer *buffer)
{
	debugLockCheck(physicalDisk, __FUNCTION__);

	if (buffer == physicalDisk->cache.buffer)
		physicalDisk->cache.buffer = buffer->next;

	if (buffer->prev)
		buffer->prev->next = buffer->next;
	if (buffer->next)
		buffer->next->prev = buffer->prev;

	physicalDisk->cache.size -= bufferBytes(physicalDisk, buffer);
	cachePutBuffer(buffer);
}


static void cachePrune(kernelPhysicalDisk *physicalDisk)
{
	// If the cache has grown larger than the pre-ordained DISK_CACHE_MAX
	// value, uncache some data.  Uncache the least-recently-used buffers
	// until we're under the limit.

	kernelDiskCacheBuffer *currBuffer = NULL;
	unsigned oldestTime = 0;
	kernelDiskCacheBuffer *oldestBuffer = NULL;

	debugLockCheck(physicalDisk, __FUNCTION__);

	while (physicalDisk->cache.size > DISK_MAX_CACHE)
	{
		currBuffer = physicalDisk->cache.buffer;

		// Don't bother uncaching the only buffer
		if (!currBuffer->next)
			break;

		oldestTime = ~0UL;
		oldestBuffer = NULL;

		while (currBuffer)
		{
			if (currBuffer->lastAccess < oldestTime)
			{
				oldestTime = currBuffer->lastAccess;
				oldestBuffer = currBuffer;
			}

			currBuffer = currBuffer->next;
		}

		if (!oldestBuffer)
		{
			kernelDebug(debug_io, "Disk %s, no oldest buffer!",
				physicalDisk->name);
			break;
		}

		kernelDebug(debug_io, "Disk %s uncache buffer %llu->%llu, mem=%p, "
			"dirty=%d", physicalDisk->name, oldestBuffer->startSector,
			bufferEnd(oldestBuffer), oldestBuffer->data, oldestBuffer->dirty);

		if (oldestBuffer->dirty)
		{
			if (realReadWrite(physicalDisk, oldestBuffer->startSector,
				oldestBuffer->numSectors, oldestBuffer->data, IOMODE_WRITE) < 0)
			{
				kernelDebug(debug_io, "Disk %s error writing dirty buffer",
					physicalDisk->name);
				return;
			}

			cacheMarkClean(physicalDisk, oldestBuffer);
		}

		cacheRemove(physicalDisk, oldestBuffer);
	}

	return;
}


static kernelDiskCacheBuffer *cacheAdd(kernelPhysicalDisk *physicalDisk,
	uquad_t startSector, uquad_t numSectors, void *data)
{
	// Add the supplied range of sectors to the cache.

	kernelDiskCacheBuffer *prevBuffer = NULL;
	kernelDiskCacheBuffer *nextBuffer = NULL;
	kernelDiskCacheBuffer *newBuffer = NULL;

	//kernelDebug(debug_io, "Disk %s adding %llu->%llu", physicalDisk->name,
	//	startSector, (startSector + numSectors - 1));

	debugLockCheck(physicalDisk, __FUNCTION__);

	// Find out where in the order the new buffer would go.
	nextBuffer = physicalDisk->cache.buffer;
	while (nextBuffer)
	{
		if (startSector > nextBuffer->startSector)
		{
			prevBuffer = nextBuffer;
			nextBuffer = nextBuffer->next;
		}
		else
			break;
	}

	// Get a new cache buffer.
	newBuffer = cacheGetBuffer(physicalDisk, startSector, numSectors);
	if (!newBuffer)
	{
		kernelError(kernel_error, "Couldn't get a new buffer for %s's disk "
			"cache", physicalDisk->name);
		return (newBuffer);
	}

	// Copy the data into the cache buffer.
	memcpy(newBuffer->data, data, bufferBytes(physicalDisk, newBuffer));

	newBuffer->prev = prevBuffer;
	newBuffer->next = nextBuffer;

	if (newBuffer->prev)
		newBuffer->prev->next = newBuffer;
	else
		// This will be the first cache buffer in the cache.
		physicalDisk->cache.buffer = newBuffer;

	if (newBuffer->next)
		newBuffer->next->prev = newBuffer;

	physicalDisk->cache.size += bufferBytes(physicalDisk, newBuffer);

	return (newBuffer);
}


static void cacheMerge(kernelPhysicalDisk *physicalDisk)
{
	// Check whether we should merge cache entries.  We do this if they are
	// a) adjacent; and b) their clean/dirty state matches.

	kernelDiskCacheBuffer *currBuffer = NULL;
	kernelDiskCacheBuffer *nextBuffer = NULL;
	void *newData = NULL;

	debugLockCheck(physicalDisk, __FUNCTION__);

	currBuffer = physicalDisk->cache.buffer;

	while (currBuffer)
	{
		nextBuffer = currBuffer->next;

		if (nextBuffer)
		{
			if ((bufferEnd(currBuffer) == (nextBuffer->startSector - 1)) &&
				(currBuffer->dirty == nextBuffer->dirty))
			{
				// Merge the 2 entries by expanding the memory of the first
				// entry, copying both entries' data into it, and removing the
				// second entry.

				kernelDebug(debug_io, "Disk %s merge %llu->%llu and %llu->%llu",
					physicalDisk->name, currBuffer->startSector,
					bufferEnd(currBuffer), nextBuffer->startSector,
					bufferEnd(nextBuffer));

				// Get a new cache buffer
				newData = kernelMalloc(bufferBytes(physicalDisk, currBuffer) +
					bufferBytes(physicalDisk, nextBuffer));
				if (!newData)
				{
					kernelError(kernel_error, "Couldn't get a new buffer for "
						"%s's disk cache", physicalDisk->name);
					return;
				}

				// Copy the data from each existing entry
				memcpy(newData, currBuffer->data,
					bufferBytes(physicalDisk, currBuffer));
				memcpy((newData + bufferBytes(physicalDisk, currBuffer)),
					nextBuffer->data, bufferBytes(physicalDisk, nextBuffer));

				// Replace the buffer pointer
				kernelFree(currBuffer->data);
				currBuffer->data = newData;

				// Update the first entry's size
				currBuffer->numSectors += nextBuffer->numSectors;

				// Briefly grow the cache size; removing the 'next' entry will
				// shrink it back again
				physicalDisk->cache.size +=
					bufferBytes(physicalDisk, nextBuffer);

				// Remove the second entry
				cacheRemove(physicalDisk, nextBuffer);

				if (currBuffer->dirty)
					physicalDisk->cache.dirty -= 1;

				// Process this one again, since we might want to merge it
				// with the next one as well.
				continue;
			}
		}

		// Move to the next one
		currBuffer = nextBuffer;
	}
}


static int cacheRead(kernelPhysicalDisk *physicalDisk, uquad_t startSector,
	uquad_t numSectors, void *data)
{
	// For ranges of sectors that are in the cache, copy them into the target
	// data buffer.  For ranges that are not in the cache, read the sectors
	// from disk and put a copy in a new cache buffer.

	int status = 0;
	uquad_t numCached = 0;
	uquad_t firstCached = 0;
	uquad_t notCached = 0;
	int added = 0;
	kernelDiskCacheBuffer *buffer = NULL;

	debugLockCheck(physicalDisk, __FUNCTION__);

	while (numSectors)
	{
		numCached = cacheQueryRange(physicalDisk, startSector, numSectors,
			&firstCached);

		if (numCached)
		{
			// At least some of the data is cached.  Any uncached portion that
			// comes before the cached portion needs to be read from disk and
			// added to the cache.

			notCached = (firstCached - startSector);

			// Read the uncached portion from disk.
			if (notCached)
			{
				status = realReadWrite(physicalDisk, startSector, notCached,
					data, IOMODE_READ);
				if (status < 0)
					return (status);

				// Add the data to the cache.
				buffer = cacheAdd(physicalDisk, startSector, notCached, data);
				if (buffer)
				{
					buffer->lastAccess = kernelSysTimerRead();
					added = 1;
				}

				startSector += notCached;
				numSectors -= notCached;
				data += (notCached * physicalDisk->sectorSize);
			}

			// Get the cached portion
			buffer = cacheFind(physicalDisk, startSector, numCached);
			if (buffer)
			{
				memcpy(data, (buffer->data +
					((startSector - buffer->startSector) *
						physicalDisk->sectorSize)),
					(numCached * physicalDisk->sectorSize));
				buffer->lastAccess = kernelSysTimerRead();
			}

			startSector += numCached;
			numSectors -= numCached;
			data += (numCached * physicalDisk->sectorSize);
		}
		else
		{
			// Nothing is cached.  Read everything from disk.
			status = realReadWrite(physicalDisk, startSector, numSectors, data,
				IOMODE_READ);
			if (status < 0)
				return (status);

			// Add the data to the cache.
			buffer = cacheAdd(physicalDisk, startSector, numSectors, data);
			if (buffer)
			{
				buffer->lastAccess = kernelSysTimerRead();
				added = 1;
			}

			break;
		}
	}

	if (added)
	{
		// Since we added something to the cache above, check whether we should
		// prune it.
		if (physicalDisk->cache.size > DISK_MAX_CACHE)
			cachePrune(physicalDisk);
	}

	// Check whether we should merge any entries
	cacheMerge(physicalDisk);

	cacheCheck(physicalDisk);

	return (status = 0);
}


static kernelDiskCacheBuffer *cacheSplit(kernelPhysicalDisk *physicalDisk,
	uquad_t startSector, uquad_t numSectors, void *data,
	kernelDiskCacheBuffer *buffer)
{
	// Given a range of sectors, split them from the supplied buffer, resulting
	// in a previous buffer (if applicable), a next buffer(if applicable),
	// and the new split-off buffer which we return.

	uquad_t prevSectors = 0;
	uquad_t nextSectors = 0;
	kernelDiskCacheBuffer *prevBuffer = NULL;
	kernelDiskCacheBuffer *newBuffer = NULL;
	kernelDiskCacheBuffer *nextBuffer = NULL;

	prevSectors = (startSector - buffer->startSector);
	nextSectors = ((buffer->startSector + buffer->numSectors) -
		(startSector + numSectors));

	if (!prevSectors && !nextSectors)
	{
		kernelError(kernel_error, "Cannot split %llu sectors from a %llu-sector"
			"buffer", numSectors, buffer->numSectors);
		return (newBuffer = NULL);
	}

	if (prevSectors)
		prevBuffer = cacheGetBuffer(physicalDisk, buffer->startSector,
			prevSectors);

	newBuffer = cacheGetBuffer(physicalDisk, startSector, numSectors);

	if (nextSectors)
		nextBuffer = cacheGetBuffer(physicalDisk, (startSector + numSectors),
			nextSectors);

	if ((prevSectors && !prevBuffer) || !newBuffer ||
		(nextSectors && !nextBuffer))
	{
		kernelError(kernel_error, "Couldn't get a new buffer for %s's disk "
			"cache", physicalDisk->name);
		return (newBuffer = NULL);
	}

	// Copy data
	if (prevBuffer)
	{
		memcpy(prevBuffer->data, buffer->data,
			(prevSectors * physicalDisk->sectorSize));
		if (buffer->dirty)
			cacheMarkDirty(physicalDisk, prevBuffer);
		prevBuffer->lastAccess = buffer->lastAccess;

		prevBuffer->prev = buffer->prev;
		prevBuffer->next = newBuffer;

		if (prevBuffer->prev)
			prevBuffer->prev->next = prevBuffer;
		else
			physicalDisk->cache.buffer = prevBuffer;

		if (prevBuffer->next)
			prevBuffer->next->prev = prevBuffer;
	}
	else
	{
		newBuffer->prev = buffer->prev;

		if (newBuffer->prev)
			newBuffer->prev->next = newBuffer;
		else
			physicalDisk->cache.buffer = newBuffer;
	}

	memcpy(newBuffer->data, data,
		(numSectors * physicalDisk->sectorSize));
	if (buffer->dirty)
		cacheMarkDirty(physicalDisk, newBuffer);
	newBuffer->lastAccess = buffer->lastAccess;

	if (nextBuffer)
	{
		memcpy(nextBuffer->data, (buffer->data + (prevSectors *
			physicalDisk->sectorSize) + (numSectors *
				physicalDisk->sectorSize)),
			(nextSectors * physicalDisk->sectorSize));
		if (buffer->dirty)
			cacheMarkDirty(physicalDisk, nextBuffer);
		nextBuffer->lastAccess = buffer->lastAccess;

		nextBuffer->prev = newBuffer;
		nextBuffer->next = buffer->next;

		if (nextBuffer->prev)
			nextBuffer->prev->next = nextBuffer;
		if (nextBuffer->next)
			nextBuffer->next->prev = nextBuffer;
	}
	else
	{
		newBuffer->next = buffer->next;

		if (newBuffer->next)
			newBuffer->next->prev = newBuffer;
	}

	if (buffer->dirty)
		cacheMarkClean(physicalDisk, buffer);

	cachePutBuffer(buffer);

	return (newBuffer);
}


static int cacheWrite(kernelPhysicalDisk *physicalDisk, uquad_t startSector,
	uquad_t numSectors, void *data)
{
	// For ranges of sectors that are in the cache, overwrite the cache buffer
	// with the new data.  For ranges that are not in the cache, allocate a
	// new cache buffer for the new data.

	int status = 0;
	uquad_t numCached = 0;
	uquad_t firstCached = 0;
	uquad_t notCached = 0;
	int added = 0;
	kernelDiskCacheBuffer *buffer = NULL;

	debugLockCheck(physicalDisk, __FUNCTION__);

	while (numSectors)
	{
		numCached = cacheQueryRange(physicalDisk, startSector, numSectors,
			&firstCached);

		if (numCached)
		{
			// At least some of the data is cached.  For any uncached portion
			// that comes before the cached portion, allocate a new cache
			// buffer.

			notCached = (firstCached - startSector);

			if (notCached)
			{
				// Add the data to the cache, and mark it dirty.
				buffer = cacheAdd(physicalDisk, startSector, notCached, data);
				if (buffer)
				{
					cacheMarkDirty(physicalDisk, buffer);
					buffer->lastAccess = kernelSysTimerRead();
					added = 1;
				}

				startSector += notCached;
				numSectors -= notCached;
				data += (notCached * physicalDisk->sectorSize);
			}

			buffer = cacheFind(physicalDisk, startSector, numCached);

			// If the buffer is clean, and we're not dirtying the whole thing,
			// split off the bit we're making dirty.
			if (!buffer->dirty && (numCached != buffer->numSectors))
			{
				buffer = cacheSplit(physicalDisk, startSector, numCached, data,
					buffer);
			}
			else
			{
				// Overwrite the cached portion.
				memcpy((buffer->data + ((startSector - buffer->startSector) *
					physicalDisk->sectorSize)), data,
					(numCached * physicalDisk->sectorSize));
			}

			if (buffer)
			{
				cacheMarkDirty(physicalDisk, buffer);
				buffer->lastAccess = kernelSysTimerRead();
			}

			startSector += numCached;
			numSectors -= numCached;
			data += (numCached * physicalDisk->sectorSize);
		}
		else
		{
			// Nothing is cached.  Add it all to the cache, and mark it dirty.
			buffer = cacheAdd(physicalDisk, startSector, numSectors, data);
			if (buffer)
			{
				cacheMarkDirty(physicalDisk, buffer);
				buffer->lastAccess = kernelSysTimerRead();
				added = 1;
			}
			break;
		}
	}

	if (added)
	{
		// Since we added something to the cache above, check whether we should
		// prune it.
		if (physicalDisk->cache.size > DISK_MAX_CACHE)
			cachePrune(physicalDisk);
	}

	// Check whether we should merge any entries
	cacheMerge(physicalDisk);

	cacheCheck(physicalDisk);

	return (status = 0);
}
#endif // DISK_CACHE


static int readWrite(kernelPhysicalDisk *physicalDisk, uquad_t startSector,
	uquad_t numSectors, void *data, int mode)
{
	// This is the combined "read sectors" and "write sectors" function.  Uses
	// the cache where available/permitted.

	int status = 0;
	uquad_t startTime = kernelCpuGetMs();

	debugLockCheck(physicalDisk, __FUNCTION__);

	// Don't try to write a read-only disk
	if ((mode & IOMODE_WRITE) && (physicalDisk->flags & DISKFLAG_READONLY))
	{
		kernelError(kernel_error, "Disk %s is read-only", physicalDisk->name);
		return (status = ERR_NOWRITE);
	}

	#if (DISK_CACHE)
	if (!(physicalDisk->flags & DISKFLAG_NOCACHE) && !(mode & IOMODE_NOCACHE))
	{
		if (mode & IOMODE_READ)
			status = cacheRead(physicalDisk, startSector, numSectors, data);
		else
			status = cacheWrite(physicalDisk, startSector, numSectors, data);
	}
	else
	#endif // DISK_CACHE
	{
		status = realReadWrite(physicalDisk, startSector, numSectors, data,
			mode);
	}

	// Throughput stats collection
	if (mode & IOMODE_READ)
	{
		physicalDisk->stats.readTimeMs += (unsigned)(kernelCpuGetMs() -
			startTime);
		physicalDisk->stats.readKbytes += ((numSectors *
			physicalDisk->sectorSize) / 1024);
	}
	else
	{
		physicalDisk->stats.writeTimeMs += (unsigned)(kernelCpuGetMs() -
			startTime);
		physicalDisk->stats.writeKbytes += ((numSectors *
			physicalDisk->sectorSize) / 1024);
	}

	return (status);
}


static kernelPhysicalDisk *getPhysicalByName(const char *name)
{
	// This function takes the name of a physical disk and finds it in the
	// array, returning a pointer to the disk.  If the disk doesn't exist,
	// the function returns NULL

	kernelPhysicalDisk *physicalDisk = NULL;
	int count;

	for (count = 0; count < physicalDiskCounter; count ++)
	{
		if (!strcmp(name, (char *) physicalDisks[count]->name))
		{
			physicalDisk = physicalDisks[count];
			break;
		}
	}

	return (physicalDisk);
}


static int diskFromPhysical(kernelPhysicalDisk *physicalDisk, disk *userDisk)
{
	// Takes our physical disk kernel structure and turns it into a user space
	// 'disk' object

	int status = 0;

	// Check params
	if (!physicalDisk || !userDisk)
		return (status = ERR_NULLPARAMETER);

	memset(userDisk, 0, sizeof(disk));
	strncpy(userDisk->name, (char *) physicalDisk->name, DISK_MAX_NAMELENGTH);
	userDisk->deviceNumber = physicalDisk->deviceNumber;
	userDisk->type = physicalDisk->type;
	strncpy(userDisk->model, (char *) physicalDisk->model, DISK_MAX_MODELLENGTH);
	userDisk->model[DISK_MAX_MODELLENGTH - 1] = '\0';
	userDisk->flags = physicalDisk->flags;
	userDisk->heads = physicalDisk->heads;
	userDisk->cylinders = physicalDisk->cylinders;
	userDisk->sectorsPerCylinder = physicalDisk->sectorsPerCylinder;
	userDisk->startSector = 0;
	userDisk->numSectors = physicalDisk->numSectors;
	userDisk->sectorSize = physicalDisk->sectorSize;

	return (status = 0);
}


static inline int checkDosSignature(unsigned char *sectorData)
{
	// Returns 1 if the buffer contains an MSDOS signature.

	if ((sectorData[510] != (unsigned char) 0x55) ||
		(sectorData[511] != (unsigned char) 0xAA))
	{
		// No signature.  Return 0.
		return (0);
	}
	else
		// We'll say this has an MSDOS signature.
		return (1);
}


static int isDosDisk(kernelPhysicalDisk *physicalDisk)
{
	// Return 1 if the physical disk appears to have an MS-DOS label on it.

	int status = 0;
	unsigned char *sectorData = NULL;

	sectorData = kernelMalloc(physicalDisk->sectorSize);
	if (!sectorData)
		return (status = ERR_MEMORY);

	// Read the first sector of the device
	status = kernelDiskReadSectors((char *) physicalDisk->name, 0, 1,
		sectorData);
	if (status < 0)
	{
		kernelFree(sectorData);
		return (status);
	}

	// Is this a valid partition table?  Make sure the signature is at the end.
	status = checkDosSignature(sectorData);

	kernelFree(sectorData);

	if (status == 1)
	{
		// Call this an MSDOS label.
		kernelDebug(debug_io, "Disk %s MSDOS partition table found",
			physicalDisk->name);
		return (status);
	}
	else
		// Not an MSDOS label
		return (status = 0);
}


static inline int checkGptSignature(unsigned char *sectorData)
{
	// Returns 1 if the buffer contains a GPT signature.

	if (memcmp(sectorData, "EFI PART", 8))
		// No signature.  Return 0.
		return (0);
	else
		// We'll say this has a GPT signature.
		return (1);
}


static int isGptDisk(kernelPhysicalDisk *physicalDisk)
{
	// Return 1 if the physical disk appears to have a GPT label on it.

	int status = 0;
	unsigned char *sectorData = NULL;
	msdosTable *table = NULL;
	int foundMsdosProtective = 0;
	int count;

	// A GPT disk must have a "guard" MS-DOS table, so a call to the MS-DOS
	// detect() function must succeed first.
	kernelDebug(debug_io, "Disk %s GPT check for MSDOS guard table",
		physicalDisk->name);
	if (isDosDisk(physicalDisk) != 1)
	{
		// Not a GPT label
		kernelDebug(debug_io, "Disk %s GPT MSDOS guard table not found",
			physicalDisk->name);
		return (status = 0);
	}

	sectorData = kernelMalloc(physicalDisk->sectorSize);
	if (!sectorData)
		return (status = ERR_MEMORY);

	kernelDebug(debug_io, "Disk %s GPT check for MSDOS protective partition",
		physicalDisk->name);

	// Read the MS-DOS table
	status = kernelDiskReadSectors((char *) physicalDisk->name, 0, 1,
		sectorData);
	if (status < 0)
	{
		kernelFree(sectorData);
		return (status);
	}

	// Make sure it has the GPT protective partition
	table = (msdosTable *)(sectorData + MSDOS_TABLE_OFFSET);
	for (count = 0; count < MSDOS_TABLE_ENTRIES; count ++)
	{
		if (table->entries[count].tag == MSDOSTAG_EFI_GPT_PROT)
		{
			foundMsdosProtective = 1;
			break;
		}
	}

	if (!foundMsdosProtective)
	{
		// Say it's not a valid GPT label
		kernelDebug(debug_io, "Disk %s GPT MSDOS protective partition not "
			"found", physicalDisk->name);
		kernelFree(sectorData);
		return (status = 0);
	}

	// Read the GPT header.  The guard MS-DOS table in the first sector.  Read
	// the second sector.
	status = kernelDiskReadSectors((char *) physicalDisk->name, 1, 1,
		sectorData);
	if (status < 0)
	{
		kernelError(kernel_error, "Can't read GPT header");
		kernelFree(sectorData);
		return (status);
	}

	// Check for the GPT signature
	status = checkGptSignature(sectorData);

	kernelFree(sectorData);

	if (status == 1)
	{
		// Call this a GPT label.
		kernelDebug(debug_io, "Disk %s GPT partition table found",
			physicalDisk->name);
		return (status);
	}
	else
	{
		// Not a GPT label
		kernelDebug(debug_io, "Disk %s GPT partition table not found",
			physicalDisk->name);
		return (status = 0);
	}
}


static unsigned gptHeaderChecksum(unsigned char *sectorData)
{
	// Given a GPT header, compute the checksum

	unsigned *headerBytesField = ((unsigned *)(sectorData + 12));
	unsigned *checksumField = ((unsigned *)(sectorData + 16));
	unsigned oldChecksum = 0;
	unsigned checksum = 0;

	// Zero the checksum field
	oldChecksum = *checksumField;
	*checksumField = 0;

	// Get the checksum
	checksum = kernelCrc32(sectorData, *headerBytesField, NULL);

	*checksumField = oldChecksum;

	return (checksum);
}


static int readGptPartitions(kernelPhysicalDisk *physicalDisk,
	kernelDisk *newLogicalDisks[], int *newLogicalDiskCounter)
{
	int status = 0;
	unsigned char *sectorData = NULL;
	unsigned checksum = 0;
	uquad_t entriesLogical = 0;
	unsigned numEntries = 0;
	unsigned entrySize = 0;
	unsigned entryBytes = 0;
	unsigned entrySectors = 0;
	unsigned char *entry = NULL;
	kernelDisk *logicalDisk = NULL;
	guid *typeGuid = NULL;
	gptPartType gptType;
	unsigned count;

	sectorData = kernelMalloc(physicalDisk->sectorSize);
	if (!sectorData)
		return (status = ERR_MEMORY);

	// Read the header.  The guard MS-DOS table in the first sector.  Read
	// the second sector.
	status = kernelDiskReadSectors((char *) physicalDisk->name, 1, 1,
		sectorData);
	if (status < 0)
	{
		kernelFree(sectorData);
		return (status);
	}

	checksum = *((unsigned *)(sectorData + 16));
	entriesLogical = *((uquad_t *)(sectorData + 72));
	numEntries = *((unsigned *)(sectorData + 80));
	entrySize = *((unsigned *)(sectorData + 84));

	kernelDebug(debug_io, "Disk %s has %u GPT entries of size %u",
		physicalDisk->name, numEntries, entrySize);

	// Check the checksum
	if (checksum != gptHeaderChecksum(sectorData))
	{
		kernelError(kernel_error, "GPT header bad checksum");
		kernelFree(sectorData);
		return (status = ERR_BADDATA);
	}

	// Calculate the number of sectors we need to read
	entryBytes = (numEntries * entrySize);
	entrySectors = ((entryBytes / physicalDisk->sectorSize) +
		((entryBytes % physicalDisk->sectorSize)? 1 : 0));

	// Reallocate the buffer for reading the entries
	kernelFree(sectorData);
	sectorData = kernelMalloc(entrySectors * physicalDisk->sectorSize);
	if (!sectorData)
		return (status = ERR_MEMORY);

	// Read the first sector of the entries.
	kernelDebug(debug_io, "Disk %s read %u sectors of GPT entries at %llu",
		physicalDisk->name, entrySectors, entriesLogical);
	status = kernelDiskReadSectors((char *) physicalDisk->name,
		(unsigned) entriesLogical, entrySectors, sectorData);
	if (status < 0)
	{
		kernelFree(sectorData);
		return (status);
	}

	for (count = 0; ((count < numEntries) &&
		(physicalDisk->numLogical < DISK_MAX_PARTITIONS)); count ++)
	{
		kernelDebug(debug_io, "Disk %s read GPT entry %d", physicalDisk->name,
			count);

		entry = (sectorData + (count * entrySize));
		logicalDisk = &physicalDisk->logical[physicalDisk->numLogical];

		typeGuid = (guid *) entry;

		if (!memcmp(typeGuid, &GUID_UNUSED, sizeof(guid)))
		{
			// Empty
			kernelDebug(debug_io, "Disk %s GPT entry %d is empty",
				physicalDisk->name, count);
			continue;
		}

		// We will add a logical disk corresponding to the partition we've
		// discovered
		sprintf((char *) logicalDisk->name, "%s%c", physicalDisk->name,
			('a' + physicalDisk->numLogical));

		// Assume UNKNOWN partition type for now.
		strcpy((char *) logicalDisk->partType, physicalDisk->description);

		// Now try to figure out the real one.
		if (kernelDiskGetGptPartType((guid *) entry, &gptType) >= 0)
			strncpy((char *) logicalDisk->partType, gptType.description,
				FSTYPE_MAX_NAMELENGTH);

		strncpy((char *) logicalDisk->fsType, "unknown", FSTYPE_MAX_NAMELENGTH);
		logicalDisk->physical = physicalDisk;
		logicalDisk->startSector = (unsigned) *((uquad_t *)(entry + 32));
		logicalDisk->numSectors = ((unsigned) *((uquad_t *)(entry + 40)) -
			logicalDisk->startSector + 1);

		// GPT partitions are always 'primary'
		logicalDisk->primary = 1;

		kernelDebug(debug_io, "Disk %s GPT entry %d startSector=%llu "
			"numSectors=%llu", physicalDisk->name, count,
			logicalDisk->startSector, logicalDisk->numSectors);

		newLogicalDisks[*newLogicalDiskCounter] = logicalDisk;
		*newLogicalDiskCounter += 1;
		physicalDisk->numLogical += 1;
	}

	kernelFree(sectorData);
	return (status = 0);
}


static int readDosPartitions(kernelPhysicalDisk *physicalDisk,
	kernelDisk *newLogicalDisks[], int *newLogicalDiskCounter)
{
	// Given a disk with an MS-DOS label, read the partitions and construct
	// the logical disks.

	int status = 0;
	unsigned char *sectorData = NULL;
	unsigned char *partitionRecord = NULL;
	unsigned char *extendedRecord = NULL;
	int partition = 0;
	kernelDisk *logicalDisk = NULL;
	unsigned char msdosTag = 0;
	msdosPartType msdosType;
	uquad_t startSector = 0;
	uquad_t extendedStartSector = 0;

	sectorData = kernelMalloc(physicalDisk->sectorSize);
	if (!sectorData)
		return (status = ERR_MEMORY);

	// Read the first sector of the disk
	status = kernelDiskReadSectors((char *) physicalDisk->name, 0, 1,
		sectorData);
	if (status < 0)
	{
		kernelFree(sectorData);
		return (status);
	}

	while (physicalDisk->numLogical < DISK_MAX_PARTITIONS)
	{
		extendedRecord = NULL;

		// Set this pointer to the first partition record in the
		// master boot record
		partitionRecord = (sectorData + 0x01BE);

		// Loop through the partition records, looking for non-zero
		// entries
		for (partition = 0; partition < 4; partition ++)
		{
			logicalDisk = &physicalDisk->logical[physicalDisk->numLogical];

			msdosTag = partitionRecord[4];
			if (!msdosTag)
			{
				// The "rules" say we must be finished with this
				// physical device.  But that is not the way things
				// often happen in real life -- empty records often
				// come before valid ones.
				partitionRecord += 16;
				continue;
			}

			if (MSDOSTAG_IS_EXTD(msdosTag))
			{
				extendedRecord = partitionRecord;
				partitionRecord += 16;
				continue;
			}

			// Assume UNKNOWN (code 0) partition type for now.
			msdosType.tag = 0;
			strcpy((char *) msdosType.description, physicalDisk->description);

			// Now try to figure out the real one.
			kernelDiskGetMsdosPartType(msdosTag, &msdosType);

			// We will add a logical disk corresponding to the
			// partition we've discovered
			sprintf((char *) logicalDisk->name, "%s%c", physicalDisk->name,
				('a' + physicalDisk->numLogical));
			strncpy((char *) logicalDisk->partType,
				msdosType.description, FSTYPE_MAX_NAMELENGTH);
			strncpy((char *) logicalDisk->fsType, "unknown",
				FSTYPE_MAX_NAMELENGTH);
			logicalDisk->physical = physicalDisk;
			logicalDisk->startSector =
				(startSector + *((unsigned *)(partitionRecord + 0x08)));
			logicalDisk->numSectors =
				*((unsigned *)(partitionRecord + 0x0C));
			if (!extendedStartSector)
				logicalDisk->primary = 1;

			newLogicalDisks[*newLogicalDiskCounter] = logicalDisk;
			*newLogicalDiskCounter += 1;
			physicalDisk->numLogical += 1;

			// If the partition's ending geometry values (heads and sectors)
			// are larger from what we've already recorded for the physical
			// disk, change the values in the physical disk to match the
			// partitions.
			if ((partitionRecord[5] >= physicalDisk->heads) ||
				((partitionRecord[6] & 0x3F) >
					physicalDisk->sectorsPerCylinder))
			{
				physicalDisk->heads = (partitionRecord[5] + 1);
				physicalDisk->sectorsPerCylinder = (partitionRecord[6] & 0x3F);
				physicalDisk->cylinders = (physicalDisk->numSectors /
					(physicalDisk->heads * physicalDisk->sectorsPerCylinder));
			}

			// Move to the next partition record
			partitionRecord += 16;
		}

		if (!extendedRecord)
			break;

		// Make sure the extended entry doesn't loop back on itself.
		// It can happen.
		if (extendedStartSector && ((*((unsigned *)(extendedRecord + 0x08)) +
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

		if (kernelDiskReadSectors((char *) physicalDisk->name, startSector,	1,
			sectorData) < 0)
		{
			break;
		}
	}

	kernelFree(sectorData);
	return (status = 0);
}


static int unmountAll(void)
{
	// This function will unmount all mounted filesystems from the disks,
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
		status = kernelFilesystemUnmount((char *)
			theDisk->filesystem.mountPoint);
		if (status < 0)
		{
			// Don't quit, just make an error message
			kernelError(kernel_warn, "Unable to unmount filesystem %s from "
				"disk %s", theDisk->filesystem.mountPoint, theDisk->name);
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


static int getUnusedDiskNumber(unsigned type)
{
	int diskNum = 0;
	const char *prefix = NULL;
	char name[8];
	int count;

	if (type & DISKTYPE_FLOPPY)
		prefix = DISK_NAME_PREFIX_FLOPPY;
	else if (type & DISKTYPE_CDROM)
		prefix = DISK_NAME_PREFIX_CDROM;
	else if (type & DISKTYPE_SCSIDISK)
		prefix = DISK_NAME_PREFIX_SCSIDISK;
	else if (type & DISKTYPE_HARDDISK)
		prefix = DISK_NAME_PREFIX_HARDDISK;
	else
	{
		kernelError(kernel_error, "Disk type %x is unknown", type);
		return (diskNum = ERR_NOTIMPLEMENTED);
	}

	for (count = 0; count < DISK_MAXDEVICES; count ++)
	{
		snprintf(name, 8, "%s%d", prefix, count);
		if (!getPhysicalByName(name))
			return (diskNum = count);
	}

	// Looks like we've reached the maximum number
	kernelError(kernel_error, "No free disk number of type %s", prefix);
	return (diskNum = ERR_NOFREE);
}


static int identifyBootCd(void)
{
	// If we believe we are booting from a CD-ROM in floppy emulation mode,
	// we should not attempt to identify it using the same method as other
	// types of disks, as we will have booted from a floppy disk image contained
	// within the disk.

	int status = 0;
	kernelPhysicalDisk *physicalDisk = NULL;
	unsigned char *buffer = NULL;
	isoBootCatInitEntry *bootCatEntry = NULL;
	unsigned imageSector = 0;
	int count;

	kernelDebug(debug_io, "Disk searching for CD-ROM boot image with "
		"signature 0x%08x", kernelOsLoaderInfo->bootSectorSig);

	bootDisk[0] = '\0';
	for (count = 0; count < physicalDiskCounter; count ++)
	{
		physicalDisk = physicalDisks[count];

		if (!(physicalDisk->type & DISKTYPE_CDROM))
			continue;

		// This is a CD-ROM.

		buffer = kernelMalloc(physicalDisk->sectorSize);
		if (!buffer)
			return (status = ERR_MEMORY);

		// Lock the disk
		status = kernelLockGet(&physicalDisk->lock);
		if (status < 0)
		{
			kernelFree(buffer);
			continue;
		}

		// Read the boot record descriptor
		status = readWrite(physicalDisk, ISO_BOOTRECORD_SECTOR, 1, buffer,
			IOMODE_READ);
		if (status < 0)
		{
			kernelLockRelease(&physicalDisk->lock);
			kernelFree(buffer);
			continue;
		}

		// Read the first sector of the boot catalog
		status = readWrite(physicalDisk,
			((isoBootRecordDescriptor *) buffer)->bootCatSector, 1, buffer,
				IOMODE_READ);
		if (status < 0)
		{
			kernelLockRelease(&physicalDisk->lock);
			kernelFree(buffer);
			continue;
		}

		bootCatEntry = (isoBootCatInitEntry *) buffer;
		if (bootCatEntry[1].bootIndicator != 0x88)
		{
			kernelDebug(debug_io, "Disk %s is not bootable",
				physicalDisk->name);
			kernelLockRelease(&physicalDisk->lock);
			kernelFree(buffer);
			continue;
		}

		imageSector = bootCatEntry[1].loadRba;
		kernelDebug(debug_io, "Disk %s image at sector %u", physicalDisk->name,
			imageSector);

		// Read the first sector of the boot image
		status = readWrite(physicalDisk, imageSector, 1, buffer, IOMODE_READ);

		// Unlock the disk
		kernelLockRelease(&physicalDisk->lock);

		if (status < 0)
		{
			kernelFree(buffer);
			continue;
		}

		// Make sure that this is a boot sector
		if (*((unsigned short *)(buffer + 510)) != MSDOS_BOOT_SIGNATURE)
		{
			kernelDebugError("%s first sector of boot image is not valid",
				physicalDisk->name);
			kernelFree(buffer);
			continue;
		}

		// Does the boot sector signature match?
		if (*((unsigned *)(buffer + 498)) == kernelOsLoaderInfo->bootSectorSig)
		{
			// We found the logical disk we booted from
			kernelDebug(debug_io, "Disk %s boot sector signature matches",
				physicalDisk->name);
			strcpy(bootDisk, (char *) physicalDisk->name);
			kernelFree(buffer);
			break;
		}
		else
		{
			kernelDebug(debug_io, "Disk %s boot sector signature (0x%08x) "
				"doesn't match", physicalDisk->name,
				*((unsigned *)(buffer + 498)));
			kernelFree(buffer);
		}
	}

	if (!bootDisk[0])
	{
		kernelError(kernel_error, "The boot CD could not be identified");
		return (status = ERR_NOSUCHDRIVER);
	}

	return (status = 0);
}


static int identifyBootDisk(void)
{
	// Try to locate the logical disk we booted from, by examining the boot
	// sector signatures and comparing them with the one we were passed.

	int status = 0;
	kernelDisk *logicalDisk = NULL;
	kernelPhysicalDisk *physicalDisk = NULL;
	unsigned char *buffer = NULL;
	int count;

	kernelDebug(debug_io, "Disk searching for boot sector with signature "
		"0x%08x", kernelOsLoaderInfo->bootSectorSig);

	bootDisk[0] = '\0';
	for (count = 0; count < logicalDiskCounter; count ++)
	{
		logicalDisk = logicalDisks[count];

		kernelDebug(debug_io, "Disk trying %s", logicalDisk->name);

		physicalDisk = logicalDisk->physical;

		// Read the boot sector.

		buffer = kernelMalloc(physicalDisk->sectorSize);
		if (!buffer)
			return (status = ERR_MEMORY);

		// Lock the disk
		status = kernelLockGet(&physicalDisk->lock);
		if (status < 0)
		{
			kernelFree(buffer);
			continue;
		}

		// Read the logical start sector
		status = readWrite(physicalDisk, logicalDisk->startSector, 1, buffer,
			IOMODE_READ);

		// Unlock the disk
		kernelLockRelease(&physicalDisk->lock);

		if (status < 0)
		{
			kernelFree(buffer);
			continue;
		}

		// Does the boot sector signature match?
		if (*((unsigned *)(buffer + 498)) == kernelOsLoaderInfo->bootSectorSig)
		{
			// We found the logical disk we booted from
			kernelDebug(debug_io, "Disk %s boot sector signature matches",
				logicalDisk->name);
			strcpy(bootDisk, (char *) logicalDisk->name);
			kernelFree(buffer);
			break;
		}
		else
		{
			kernelDebug(debug_io, "Disk %s boot sector signature (0x%08x) "
				"doesn't match", logicalDisk->name,
				*((unsigned *)(buffer + 498)));
				kernelFree(buffer);
		}
	}

	if (!bootDisk[0])
	{
		kernelError(kernel_error, "The boot device could not be identified");
		return (status = ERR_NOSUCHDRIVER);
	}

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
	// This function will receive a new device structure, add the
	// kernelPhysicalDisk to our array, and register all of its logical disks
	// for use by the system.

	int status = 0;
	kernelPhysicalDisk *physicalDisk = NULL;
	int count;

	// Check params
	if (!dev)
	{
		kernelError(kernel_error, "NULL parameter");
		return (status = ERR_NULLPARAMETER);
	}

	physicalDisk = dev->data;

	if (!physicalDisk || !physicalDisk->driver)
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

	// Compute the name for the disk, depending on what type of device it is
	status = getUnusedDiskNumber(physicalDisk->type);
	if (status < 0)
		return (status);

	if (physicalDisk->type & DISKTYPE_FLOPPY)
	{
		sprintf((char *) physicalDisk->name, "%s%d", DISK_NAME_PREFIX_FLOPPY,
			status);
	}
	else if (physicalDisk->type & DISKTYPE_CDROM)
	{
		sprintf((char *) physicalDisk->name, "%s%d", DISK_NAME_PREFIX_CDROM,
			status);
	}
	else if (physicalDisk->type & DISKTYPE_SCSIDISK)
	{
		sprintf((char *) physicalDisk->name, "%s%d", DISK_NAME_PREFIX_SCSIDISK,
			status);
	}
	else if (physicalDisk->type & DISKTYPE_HARDDISK)
	{
		sprintf((char *) physicalDisk->name, "%s%d", DISK_NAME_PREFIX_HARDDISK,
			status);
	}

	// Disk cache initialization is deferred until cache use is attempted.
	// Otherwise we waste memory allocating caches for disks that might
	// never be used.

	// Lock the disk
	status = kernelLockGet(&physicalDisk->lock);
	if (status < 0)
		return (status = ERR_NOLOCK);

	// Add the physical disk to our list
	physicalDisks[physicalDiskCounter++] = physicalDisk;

	// Loop through the physical device's logical disks
	for (count = 0; count < physicalDisk->numLogical; count ++)
		// Put the device at the end of the list and increment the counter
		logicalDisks[logicalDiskCounter++] = &physicalDisk->logical[count];

	// If it's a floppy, make sure the motor is off
	if (physicalDisk->type & DISKTYPE_FLOPPY)
		motorOff(physicalDisk);

	// Reset the 'last access' and 'last sync' values
	physicalDisk->lastAccess = kernelSysTimerRead();

	// Unlock the disk
	kernelLockRelease(&physicalDisk->lock);

	// Success
	return (status = 0);
}


int kernelDiskRemoveDevice(kernelDevice *dev)
{
	// Removes all the logical disks associated with a physical disk, and
	// then removes the physical disk.

	int status = 0;
	kernelPhysicalDisk *physicalDisk = NULL;
	kernelDisk *newLogicalDisks[DISK_MAXDEVICES];
	int newLogicalDiskCounter = 0;
	int position = -1;
	int count;

	// Check params
	if (!dev)
	{
		kernelError(kernel_error, "NULL parameter");
		return (status = ERR_NULLPARAMETER);
	}

	physicalDisk = dev->data;

	if (!physicalDisk || !physicalDisk->driver)
	{
		kernelError(kernel_error, "Physical disk structure or driver is NULL");
		return (status = ERR_NULLPARAMETER);
	}

	kernelDebug(debug_io, "Disk %s remove device",  physicalDisk->name);

	// Add all the logical disks that don't belong to this physical disk
	for (count = 0; count < logicalDiskCounter; count ++)
		if (logicalDisks[count]->physical != physicalDisk)
			newLogicalDisks[newLogicalDiskCounter++] = logicalDisks[count];

	// Now copy our new array of logical disks
	for (logicalDiskCounter = 0; logicalDiskCounter < newLogicalDiskCounter;
		logicalDiskCounter ++)
	{
		logicalDisks[logicalDiskCounter] = newLogicalDisks[logicalDiskCounter];
	}

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

	kernelDebug(debug_io, "Disk %s removed",  physicalDisk->name);

	return (status = 0);
}


int kernelDiskInitialize(void)
{
	// This is the "initialize" function which scans all of the disks that
	// have been previously detected/added by drivers.  It starts the disk
	// thread and attempts to identify the boot disk.

	int status = 0;

	// Check whether any disks have been registered.  If not, that's
	// an indication that the hardware enumeration has not been done
	// properly.  We'll issue an error in this case
	if (physicalDiskCounter <= 0)
	{
		kernelError(kernel_error, "No disks have been registered");
		return (status = ERR_NOTINITIALIZED);
	}

	// Spawn the disk thread
	status = spawnDiskThread();
	if (status < 0)
		kernelError(kernel_warn, "Unable to start disk thread");

	// We're initialized
	initialized = 1;

	// Read the partition tables
	status = kernelDiskReadPartitionsAll();
	if (status < 0)
		kernelError(kernel_error, "Unable to read disk partitions");

	// Identify the name of the boot disk.
	if (kernelOsLoaderInfo->bootCd)
		status = identifyBootCd();
	else
		status = identifyBootDisk();

	if (status < 0)
		return (status);

	return (status = 0);
}


void kernelDiskAutoMount(kernelDisk *theDisk)
{
	// Given a disk, see if it is listed in the mount.conf file, whether it
	// is supposed to be automounted, and if so, mount it.

	int status = 0;
	variableList mountConfig;
	char variable[128];
	const char *value = NULL;
	char mountPoint[MAX_PATH_LENGTH];

	// Already mounted?
	if (theDisk->filesystem.mounted)
		return;

	// Try reading the mount configuration file
	status = kernelConfigRead(DISK_MOUNT_CONFIG, &mountConfig);
	if (status < 0)
		return;

	// See if we're supposed to automount it.
	snprintf(variable, 128, "%s.automount", theDisk->name);
	value = kernelVariableListGet(&mountConfig, variable);
	if (!value)
		goto out;

	if (strcasecmp(value, "yes"))
		goto out;

	// Does the disk have removable media?
	if ((theDisk->physical->type & DISKTYPE_REMOVABLE) &&
		// See if there's any media there
		!kernelDiskMediaPresent((const char *) theDisk->name))
	{
		kernelError(kernel_error, "Can't automount %s on disk %s - no media",
			mountPoint, theDisk->name);
		goto out;
	}

	// See if a mount point is specified
	snprintf(variable, 128, "%s.mountpoint", theDisk->name);
	value = kernelVariableListGet(&mountConfig, variable);
	if (value)
	{
		status = kernelFileFixupPath(value, mountPoint);
		if (status < 0)
			goto out;
	}
	else
	{
		// Try a default
		sprintf(mountPoint, "/%s", theDisk->name);
	}

	kernelFilesystemMount((const char *) theDisk->name, mountPoint);

 out:
	kernelVariableListDestroy(&mountConfig);
	return;
}


void kernelDiskAutoMountAll(void)
{
	int count;

	// Loop through the logical disks and see whether they should be
	// automounted.
	for (count = 0; count < logicalDiskCounter; count ++)
		kernelDiskAutoMount(logicalDisks[count]);

	return;
}


int kernelDiskInvalidateCache(const char *diskName)
{
	// Invalidate the cache of the named disk

	int status = 0;

	if (!initialized)
		return (status = ERR_NOTINITIALIZED);

	// Check params
	if (!diskName)
		return (status = ERR_NULLPARAMETER);

	kernelDebug(debug_io, "Disk %s invalidate cache",  diskName);

	#if (DISK_CACHE)
	kernelPhysicalDisk *physicalDisk = NULL;

	physicalDisk = getPhysicalByName(diskName);
	if (!physicalDisk)
	{
		kernelError(kernel_error, "No such disk \"%s\"", diskName);
		return (status = ERR_NOSUCHENTRY);
	}

	// Lock the physical disk
	status = kernelLockGet(&physicalDisk->lock);
	if (status < 0)
	{
		kernelError(kernel_error, "Unable to lock disk \"%s\" for cache "
			"invalidation", physicalDisk->name);
		return (status);
	}

	status = cacheInvalidate(physicalDisk);

	kernelLockRelease(&physicalDisk->lock);

	if (status < 0)
		kernelError(kernel_warn, "Error invalidating disk \"%s\" cache",
			physicalDisk->name);
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
	status = kernelDiskSyncAll();

	for (count = 0; count < physicalDiskCounter; count ++)
	{
		physicalDisk = physicalDisks[count];

		// Lock the disk
		status = kernelLockGet(&physicalDisk->lock);
		if (status < 0)
			return (status = ERR_NOLOCK);

		if ((physicalDisk->type & DISKTYPE_REMOVABLE) &&
			(physicalDisk->flags & DISKFLAG_MOTORON))
		{
			motorOff(physicalDisk);
		}

		// Unlock the disk
		kernelLockRelease(&physicalDisk->lock);
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
	if (!logical || !userDisk)
		return (status = ERR_NULLPARAMETER);

	memset(userDisk, 0, sizeof(disk));

	// Get the physical disk info
	status = diskFromPhysical(logical->physical, userDisk);
	if (status < 0)
		return (status);

	// Add/override some things specific to logical disks
	strncpy(userDisk->name, (char *) logical->name, DISK_MAX_NAMELENGTH);
	userDisk->type = ((logical->physical->type & ~DISKTYPE_LOGICALPHYSICAL) |
		DISKTYPE_LOGICAL);
	if (logical->primary)
		userDisk->type |= DISKTYPE_PRIMARY;
	userDisk->flags = logical->physical->flags;
	strncpy(userDisk->partType, (char *) logical->partType,
		FSTYPE_MAX_NAMELENGTH);
	strncpy(userDisk->fsType, (char *) logical->fsType, FSTYPE_MAX_NAMELENGTH);
	userDisk->opFlags = logical->opFlags;
	userDisk->startSector = logical->startSector;
	userDisk->numSectors = logical->numSectors;

	// Filesystem-related
	strncpy(userDisk->label, (char *) logical->filesystem.label,
		MAX_NAME_LENGTH);
	userDisk->blockSize = logical->filesystem.blockSize;
	userDisk->minSectors = logical->filesystem.minSectors;
	userDisk->maxSectors = logical->filesystem.maxSectors;

	userDisk->mounted = logical->filesystem.mounted;
	if (userDisk->mounted)
	{
		userDisk->freeBytes = kernelFilesystemGetFreeBytes((char *)
			logical->filesystem.mountPoint);
		strncpy(userDisk->mountPoint, (char *) logical->filesystem.mountPoint,
			MAX_PATH_LENGTH);
	}

	userDisk->readOnly = logical->filesystem.readOnly;

	return (status = 0);
}


kernelDisk *kernelDiskGetByName(const char *name)
{
	// This function takes the name of a logical disk and finds it in the
	// array, returning a pointer to the disk.  If the disk doesn't exist,
	// the function returns NULL

	kernelDisk *theDisk = NULL;
	int count;

	if (!initialized)
		return (theDisk = NULL);

	// Check params
	if (!name)
	{
		kernelError(kernel_error, "NULL parameter");
		return (theDisk = NULL);
	}

	for (count = 0; count < logicalDiskCounter; count ++)
	{
		if (!strcmp(name, (char *) logicalDisks[count]->name))
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
	// Read the partition table for the requested physical disk, and
	// (re)build the list of logical disks.  This will be done initially at
	// startup time, but can be re-called during operation if the partitions
	// have been changed.

	int status = 0;
	kernelPhysicalDisk *physicalDisk = NULL;
	kernelDisk *newLogicalDisks[DISK_MAXDEVICES];
	int newLogicalDiskCounter = 0;
	int mounted = 0;
	kernelDisk *logicalDisk = NULL;
	msdosPartType msdosType;
	int count;

	if (!initialized)
		return (status = ERR_NOTINITIALIZED);

	// Check params
	if (!diskName)
		return (status = ERR_NULLPARAMETER);

	kernelDebug(debug_io, "Disk read partitions on disk %s",  diskName);

	// Find the disk structure.
	physicalDisk = getPhysicalByName(diskName);
	if (!physicalDisk)
	{
		// No such disk.
		kernelError(kernel_error, "No such disk \"%s\"", diskName);
		return (status = ERR_NOSUCHENTRY);
	}

	// Add all the logical disks that don't belong to this physical disk
	for (count = 0; count < logicalDiskCounter; count ++)
	{
		if (logicalDisks[count]->physical != physicalDisk)
			newLogicalDisks[newLogicalDiskCounter++] = logicalDisks[count];
	}

	// Assume UNKNOWN (code 0) partition type for now.
	msdosType.tag = 0;
	strcpy((char *) msdosType.description, physicalDisk->description);

	// If this is a hard disk, get the logical disks from reading the
	// partitions.
	if (physicalDisk->type & DISKTYPE_HARDDISK)
	{
		// It's a hard disk.  We need to read the partition table

		// Make sure it has no mounted partitions.
		mounted = 0;
		for (count = 0; count < physicalDisk->numLogical; count ++)
		{
			if (physicalDisk->logical[count].filesystem.mounted)
			{
				kernelError(kernel_warn, "Logical disk %s is mounted.  Will "
					"not rescan %s until reboot.",
					physicalDisk->logical[count].name, physicalDisk->name);
				mounted = 1;
				break;
			}
		}

		if (mounted)
		{
			// It has mounted partitions.  Add the existing logical disks to
			// our array and continue to the next physical disk.
			for (count = 0; count < physicalDisk->numLogical; count ++)
			{
				newLogicalDisks[newLogicalDiskCounter++] =
					&physicalDisk->logical[count];
			}

			return (status = 1);
		}

		// Clear the logical disks
		physicalDisk->numLogical = 0;
		memset((void *) &physicalDisk->logical, 0,
			 (sizeof(kernelDisk) * DISK_MAX_PARTITIONS));

		// Check to see if it's a GPT disk first, since a GPT disk is also
		// technically an MS-DOS disk.
		if (isGptDisk(physicalDisk) == 1)
		{
			status = readGptPartitions(physicalDisk, newLogicalDisks,
				&newLogicalDiskCounter);
		}

		// Now check whether it's an MS-DOS disk.
		else if (isDosDisk(physicalDisk) == 1)
		{
			status = readDosPartitions(physicalDisk, newLogicalDisks,
				&newLogicalDiskCounter);
		}

		else
		{
			kernelDebug(debug_io, "Disk %s unknown disk label",
				physicalDisk->name);
		}

		if (status < 0)
			return (status);
	}

	else
	{
		kernelDebug(debug_io, "Disk %s is not a partitioned disk", diskName);

		// If this is a not a hard disk with partitions, etc, make the logical
		// disk be the same as the physical disk
		physicalDisk->numLogical = 1;
		logicalDisk = &physicalDisk->logical[0];

		// Logical disk name same as physical
		strcpy((char *) logicalDisk->name, (char *) physicalDisk->name);
		strncpy((char *) logicalDisk->partType, msdosType.description,
			FSTYPE_MAX_NAMELENGTH);

		if (logicalDisk->fsType[0] == '\0')
		{
			strncpy((char *) logicalDisk->fsType, "unknown",
				FSTYPE_MAX_NAMELENGTH);
		}

		logicalDisk->physical = physicalDisk;
		logicalDisk->startSector = 0;
		logicalDisk->numSectors = physicalDisk->numSectors;
		logicalDisk->primary = 1;

		newLogicalDisks[newLogicalDiskCounter++] = logicalDisk;
	}

	// Now copy our new array of logical disks
	for (logicalDiskCounter = 0; logicalDiskCounter < newLogicalDiskCounter;
		logicalDiskCounter ++)
	{
		logicalDisks[logicalDiskCounter] = newLogicalDisks[logicalDiskCounter];
	}

	// See if we can determine the filesystem types
	for (count = 0; count < logicalDiskCounter; count ++)
	{
		logicalDisk = logicalDisks[count];

		if (logicalDisk->physical == physicalDisk)
		{
			if (physicalDisk->flags & DISKFLAG_MOTORON)
				kernelFilesystemScan((char *) logicalDisk->name);

			kernelLog("Disk %s (%sdisk %s, %s): %s", logicalDisk->name,
				((physicalDisk->type & DISKTYPE_HARDDISK)? "hard " : ""),
				physicalDisk->name,
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


int kernelDiskSync(const char *diskName)
{
	// Synchronize the named physical disk.

	int status = 0;
	kernelPhysicalDisk *physicalDisk = NULL;
	kernelDisk *logicalDisk = NULL;
	int errors = 0;
	kernelDiskOps *ops = NULL;

	if (!initialized)
		return (errors = ERR_NOTINITIALIZED);

	// Check params
	if (!diskName)
		return (status = ERR_NULLPARAMETER);

	// Get the disk structure
	physicalDisk = getPhysicalByName(diskName);
	if (!physicalDisk)
	{
		// Try logical
		if ((logicalDisk = kernelDiskGetByName(diskName)))
			physicalDisk = logicalDisk->physical;
		else
			return (status = ERR_NOSUCHENTRY);
	}

	// Lock the physical disk
	status = kernelLockGet(&physicalDisk->lock);
	if (status < 0)
	{
		kernelError(kernel_error, "Unable to lock disk \"%s\" for sync",
			physicalDisk->name);
		return (status);
	}

	// If disk caching is enabled, write out dirty sectors
	#if (DISK_CACHE)
	status = cacheSync(physicalDisk);
	if (status < 0)
	{
		kernelError(kernel_warn, "Error synchronizing disk \"%s\" cache",
			physicalDisk->name);
		errors = status;
	}
	#endif // DISK_CACHE

	ops = (kernelDiskOps *) physicalDisk->driver->ops;

	// If the disk driver has a flush function, call it now
	if (ops->driverFlush)
	{
		status = ops->driverFlush(physicalDisk->deviceNumber);
		if (status < 0)
		{
			kernelError(kernel_warn, "Error flushing disk \"%s\"",
				physicalDisk->name);
			errors = status;
		}
	}

	kernelLockRelease(&physicalDisk->lock);

	return (status = errors);
}


int kernelDiskSyncAll(void)
{
	// Syncronize all the registered physical disks.

	int status = 0;
	int errors = 0;
	int count;

	if (!initialized)
		return (errors = ERR_NOTINITIALIZED);

	// Loop through all of the registered physical disks
	for (count = 0; count < physicalDiskCounter; count ++)
	{
		status = kernelDiskSync((char *) physicalDisks[count]->name);
		if (status < 0)
			errors = status;
	}

	return (status = errors);
}


int kernelDiskGetBoot(char *boot)
{
	// Returns the disk name of the boot device

	int status = 0;

	if (!initialized)
		return (status = ERR_NOTINITIALIZED);

	// Check params
	if (!boot)
		return (status = ERR_NULLPARAMETER);

	strncpy(boot, bootDisk, DISK_MAX_NAMELENGTH);
	return (status = 0);
}


int kernelDiskGetCount(void)
{
	// Returns the number of registered logical disk structures.

	if (!initialized)
		return (ERR_NOTINITIALIZED);

	return (logicalDiskCounter);
}


int kernelDiskGetPhysicalCount(void)
{
	// Returns the number of registered physical disk structures.

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
	if (!diskName || !userDisk)
		return (status = ERR_NULLPARAMETER);

	// Find the disk structure.

	// Try for a logical disk first.
	if ((logicalDisk = kernelDiskGetByName(diskName)))
		return (kernelDiskFromLogical(logicalDisk, userDisk));

	// Try physical instead
	else if ((physicalDisk = getPhysicalByName(diskName)))
		return (diskFromPhysical(physicalDisk, userDisk));

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
	if (!userDiskArray)
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
	if (!userDiskArray)
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
	if (!diskName || !buffer)
		return (status = ERR_NULLPARAMETER);

	// There must exist a logical disk with this name.
	logicalDisk = kernelDiskGetByName(diskName);
	if (!logicalDisk)
	{
		// No such disk.
		kernelError(kernel_error, "No such disk \"%s\"", diskName);
		return (status = ERR_NOSUCHENTRY);
	}

	// See if we can determine the filesystem type
	status = kernelFilesystemScan((char *) logicalDisk->name);
	if (status < 0)
		return (status);

	strncpy(buffer, (char *) logicalDisk->fsType, buffSize);
	return (status = 0);
}


int kernelDiskGetMsdosPartType(int tag, msdosPartType *type)
{
	// This function takes the supplied code and returns a corresponding
	// MS-DOS partition type structure in the memory provided.

	int status = 0;
	int count;

	// We don't check for initialization; the table is static.

	if (!type)
		return (status = ERR_NULLPARAMETER);

	for (count = 0; msdosPartTypes[count].tag; count ++)
	{
		if (msdosPartTypes[count].tag == tag)
		{
			memcpy(type, &msdosPartTypes[count], sizeof(msdosPartType));
			return (status = 0);
		}
	}

	// Not found
	return (status = ERR_NOSUCHENTRY);
}


msdosPartType *kernelDiskGetMsdosPartTypes(void)
{
	// Allocate and return a copy of our table of known MS-DOS partition types
	// We don't check for initialization; the table is static.

	msdosPartType *types =
		kernelMemoryGet(sizeof(msdosPartTypes), "partition types");
	if (!types)
		return (types);

	memcpy(types, msdosPartTypes, sizeof(msdosPartTypes));
	return (types);
}


int kernelDiskGetGptPartType(guid *g, gptPartType *type)
{
	// This function takes the supplied GUID and returns a corresponding
	// GPT partition type structure in the memory provided.

	int status = 0;
	int count;

	// We don't check for initialization; the table is static.

	if (!type)
		return (status = ERR_NULLPARAMETER);

	for (count = 0; memcmp(&gptPartTypes[count].typeGuid, &GUID_UNUSED,
		sizeof(guid)); count ++)
	{
		if (!memcmp(&gptPartTypes[count].typeGuid, g, sizeof(guid)))
		{
			memcpy(type, &gptPartTypes[count], sizeof(gptPartType));
			return (status = 0);
		}
	}

	// Not found
	return (status = ERR_NOSUCHENTRY);
}


gptPartType *kernelDiskGetGptPartTypes(void)
{
	// Allocate and return a copy of our table of known GPT partition types
	// We don't check for initialization; the table is static.

	gptPartType *types =
		kernelMemoryGet(sizeof(gptPartTypes), "partition types");
	if (!types)
		return (types);

	memcpy(types, gptPartTypes, sizeof(gptPartTypes));
	return (types);
}


int kernelDiskSetFlags(const char *diskName, unsigned flags, int set)
{
	// This function is the user-accessible interface for setting or clearing
	// (user-settable) disk flags.

	int status = 0;
	kernelDisk *logicalDisk = NULL;
	kernelPhysicalDisk *physicalDisk = NULL;

	if (!initialized)
		return (status = ERR_NOTINITIALIZED);

	// Check params
	if (!diskName)
		return (status = ERR_NULLPARAMETER);

	// Only allow the user-settable flags
	flags &= DISKFLAG_USERSETTABLE;

	// Get the disk structure
	physicalDisk = getPhysicalByName(diskName);
	if (!physicalDisk)
	{
		// Try logical
		if ((logicalDisk = kernelDiskGetByName(diskName)))
			physicalDisk = logicalDisk->physical;
		else
			return (status = ERR_NOSUCHENTRY);
	}

	// Lock the disk
	status = kernelLockGet(&physicalDisk->lock);
	if (status < 0)
		goto out;

	#if (DISK_CACHE)
	if ((set && (flags & DISKFLAG_READONLY)) || (flags & DISKFLAG_NOCACHE))
	{
		status = cacheSync(physicalDisk);
		if (status < 0)
			goto out;
	}
	if (flags & DISKFLAG_NOCACHE)
	{
		status = cacheInvalidate(physicalDisk);
		if (status < 0)
			goto out;
	}
	#endif

	if (set)
		physicalDisk->flags |= flags;
	else
		physicalDisk->flags &= ~flags;

	status = 0;

out:
	// Unlock the disk
	kernelLockRelease(&physicalDisk->lock);

	return (status);
}


int kernelDiskSetLockState(const char *diskName, int state)
{
	// This function is the user-accessible interface for locking or unlocking
	// a removable disk device.

	int status = 0;
	kernelDisk *logicalDisk = NULL;
	kernelPhysicalDisk *physicalDisk = NULL;
	kernelDiskOps *ops = NULL;

	if (!initialized)
		return (status = ERR_NOTINITIALIZED);

	// Check params
	if (!diskName)
		return (status = ERR_NULLPARAMETER);

	// Get the disk structure
	physicalDisk = getPhysicalByName(diskName);
	if (!physicalDisk)
	{
		// Try logical
		if ((logicalDisk = kernelDiskGetByName(diskName)))
			physicalDisk = logicalDisk->physical;
		else
			return (status = ERR_NOSUCHENTRY);
	}

	ops = (kernelDiskOps *) physicalDisk->driver->ops;

	// Make sure the operation is supported
	if (!ops->driverSetLockState)
	{
		kernelError(kernel_error, "Driver function is NULL");
		return (status = ERR_NOSUCHFUNCTION);
	}

	// Lock the disk
	status = kernelLockGet(&physicalDisk->lock);
	if (status < 0)
		return (status = ERR_NOLOCK);

	// Call the door lock operation
	status = ops->driverSetLockState(physicalDisk->deviceNumber, state);

	// Unlock the disk
	kernelLockRelease(&physicalDisk->lock);

	return (status);
}


int kernelDiskSetDoorState(const char *diskName, int state)
{
	// This function is the user-accessible interface for opening or closing
	// a removable disk device.

	int status = 0;
	kernelDisk *logicalDisk = NULL;
	kernelPhysicalDisk *physicalDisk = NULL;
	kernelDiskOps *ops = NULL;

	if (!initialized)
		return (status = ERR_NOTINITIALIZED);

	// Check params
	if (!diskName)
		return (status = ERR_NULLPARAMETER);

	// Get the disk structure
	physicalDisk = getPhysicalByName(diskName);
	if (!physicalDisk)
	{
		// Try logical
		if ((logicalDisk = kernelDiskGetByName(diskName)))
			physicalDisk = logicalDisk->physical;
		else
			return (status = ERR_NOSUCHENTRY);
	}

	// Make sure it's a removable disk
	if (physicalDisk->type & DISKTYPE_FIXED)
	{
		kernelError(kernel_error, "Cannot open/close a non-removable disk");
		return (status = ERR_INVALID);
	}

	ops = (kernelDiskOps *) physicalDisk->driver->ops;

	// Make sure the operation is supported
	if (!ops->driverSetDoorState)
	{
		kernelError(kernel_error, "Driver function is NULL");
		return (status = ERR_NOSUCHFUNCTION);
	}

	// Lock the disk
	status = kernelLockGet(&physicalDisk->lock);
	if (status < 0)
		return (status = ERR_NOLOCK);

	#if (DISK_CACHE)
	// Make sure the cache is invalidated
	cacheInvalidate(physicalDisk);
	#endif

	// Call the door control operation
	status = ops->driverSetDoorState(physicalDisk->deviceNumber, state);

	// Unlock the disk
	kernelLockRelease(&physicalDisk->lock);

	return (status);
}


int kernelDiskMediaPresent(const char *diskName)
{
	// This function returns 1 if the requested disk has media present,
	// 0 otherwise

	int present = 0;
	kernelDisk *logicalDisk = NULL;
	kernelPhysicalDisk *physicalDisk = NULL;
	kernelDiskOps *ops = NULL;
	void *buffer = NULL;

	if (!initialized)
		return (present = 0);

	// Check params
	if (!diskName)
		return (present = 0);

	// Get the disk structure
	physicalDisk = getPhysicalByName(diskName);
	if (!physicalDisk)
	{
		// Try logical
		if ((logicalDisk = kernelDiskGetByName(diskName)))
			physicalDisk = logicalDisk->physical;
		else
			return (present = 0);
	}

	// If it's not removable, we say media is present
	if (!(physicalDisk->type & DISKTYPE_REMOVABLE))
		return (present = 1);

	ops = (kernelDiskOps *) physicalDisk->driver->ops;

	// Lock the disk
	if (kernelLockGet(&physicalDisk->lock) < 0)
		return (present = 0);

	// Does the driver implement the 'media present' function?
	if (ops->driverMediaPresent)
	{
		// Call the driver to tell us whether media is present
		if (ops->driverMediaPresent(physicalDisk->deviceNumber) >= 1)
			present = 1;
	}
	else
	{
		// Try to read one sector
		buffer = kernelMalloc(physicalDisk->sectorSize);
		if (buffer)
		{
			if (readWrite(physicalDisk, 0, 1, buffer,
				(IOMODE_READ | IOMODE_NOCACHE)) >= 0)
			{
				present = 1;
			}

			kernelFree(buffer);
		}
	}

	// Unlock the disk
	kernelLockRelease(&physicalDisk->lock);

	return (present);
}


int kernelDiskMediaChanged(const char *diskName)
{
	// Returns 1 if the device a) is a removable type; and b) supports the
	// driverMediaChanged() function; and c) has had the disk changed.

	int changed = 0;
	kernelPhysicalDisk *physicalDisk = NULL;
	kernelDisk *logicalDisk = NULL;
	kernelDiskOps *ops = NULL;

	if (!initialized)
		return (changed = 0);

	// Check params
	if (!diskName)
		return (changed = 0);

	// Get the disk structure
	physicalDisk = getPhysicalByName(diskName);
	if (!physicalDisk)
	{
		// Try logical
		if ((logicalDisk = kernelDiskGetByName(diskName)))
			physicalDisk = logicalDisk->physical;
		else
			return (changed = 0);
	}

	// Make sure it's a removable disk
	if (!(physicalDisk->type & DISKTYPE_REMOVABLE))
		return (changed = 0);

	ops = (kernelDiskOps *) physicalDisk->driver->ops;

	// Make sure the the 'disk changed' function is implemented
	if (!ops->driverMediaChanged)
		return (changed = 0);

	// Lock the disk
	if (kernelLockGet(&physicalDisk->lock) < 0)
		return (changed = 0);

	changed = ops->driverMediaChanged(physicalDisk->deviceNumber);

	if (changed)
	{
		#if (DISK_CACHE)
		// Make sure the cache is invalidated
		cacheInvalidate(physicalDisk);
		#endif
	}

	// Unlock the disk
	kernelLockRelease(&physicalDisk->lock);

	return (changed);
}


int kernelDiskReadSectors(const char *diskName, uquad_t logicalSector,
	uquad_t numSectors, void *dataPointer)
{
	// This function is the user-accessible interface to reading data using
	// the various disk functions in this file.  Basically, it is a gatekeeper
	// that helps ensure correct use of the "read-write" method.

	int status = 0;
	kernelPhysicalDisk *physicalDisk = NULL;
	kernelDisk *theDisk = NULL;

	if (!initialized)
		return (status = ERR_NOTINITIALIZED);

	// Check params
	if (!diskName || !dataPointer)
		return (status = ERR_NULLPARAMETER);

	// Get the disk structure.  Try a physical disk first.
	physicalDisk = getPhysicalByName(diskName);
	if (!physicalDisk)
	{
		// Try logical
		theDisk = kernelDiskGetByName(diskName);
		if (!theDisk)
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
			kernelError(kernel_error, "Sector range %llu-%llu exceeds volume "
				"boundary of %llu", logicalSector,
				(logicalSector + numSectors - 1),
				(theDisk->startSector + theDisk->numSectors));
			return (status = ERR_BOUNDS);
		}

		physicalDisk = theDisk->physical;

		if (!physicalDisk)
		{
			kernelError(kernel_error, "Logical disk's physical disk is NULL");
			return (status = ERR_NOSUCHENTRY);
		}
	}

	// Lock the disk
	status = kernelLockGet(&physicalDisk->lock);
	if (status < 0)
		return (status = ERR_NOLOCK);

	// Call the read-write function for a read operation
	status = readWrite(physicalDisk, logicalSector, numSectors, dataPointer,
		IOMODE_READ);

	// Unlock the disk
	kernelLockRelease(&physicalDisk->lock);

	return (status);
}


int kernelDiskWriteSectors(const char *diskName, uquad_t logicalSector,
	uquad_t numSectors, const void *data)
{
	// This function is the user-accessible interface to writing data using
	// the various disk functions in this file.  Basically, it is a gatekeeper
	// that helps ensure correct use of the "read-write" method.

	int status = 0;
	kernelPhysicalDisk *physicalDisk = NULL;
	kernelDisk *theDisk = NULL;

	if (!initialized)
		return (status = ERR_NOTINITIALIZED);

	// Check params
	if (!diskName || !data)
		return (status = ERR_NULLPARAMETER);

	// Get the disk structure.  Try a physical disk first.
	physicalDisk = getPhysicalByName(diskName);
	if (!physicalDisk)
	{
		// Try logical
		theDisk = kernelDiskGetByName(diskName);
		if (!theDisk)
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

		if (!physicalDisk)
		{
			kernelError(kernel_error, "Logical disk's physical disk is NULL");
			return (status = ERR_NOSUCHENTRY);
		}
	}

	// Lock the disk
	status = kernelLockGet(&physicalDisk->lock);
	if (status < 0)
		return (status = ERR_NOLOCK);

	// Call the read-write function for a write operation
	status = readWrite(physicalDisk, logicalSector, numSectors, (void *) data,
		IOMODE_WRITE);

	// Unlock the disk
	kernelLockRelease(&physicalDisk->lock);

	return (status);
}


int kernelDiskEraseSectors(const char *diskName, uquad_t logicalSector,
	uquad_t numSectors, int passes)
{
	// This function synchronously and securely erases disk sectors.  It writes
	// (passes - 1) successive passes of random data followed by a final pass
	// of NULLs.

	int status = 0;
	kernelPhysicalDisk *physicalDisk = NULL;
	kernelDisk *theDisk = NULL;
	unsigned bufferSize = 0;
	unsigned char *buffer = NULL;
	int count1;
	unsigned count2;

	if (!initialized)
		return (status = ERR_NOTINITIALIZED);

	// Check params
	if (!diskName)
		return (status = ERR_NULLPARAMETER);

	// Get the disk structure.  Try a physical disk first.
	physicalDisk = getPhysicalByName(diskName);
	if (!physicalDisk)
	{
		// Try logical
		theDisk = kernelDiskGetByName(diskName);
		if (!theDisk)
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

	// Get a buffer for the data
	bufferSize = (numSectors * physicalDisk->sectorSize);
	buffer = kernelMalloc(bufferSize);
	if (!buffer)
		return (status = ERR_MEMORY);

	// Lock the disk
	status = kernelLockGet(&physicalDisk->lock);
	if (status < 0)
		return (status = ERR_NOLOCK);

	for (count1 = 0; count1 < passes; count1 ++)
	{
		if (count1 < (passes - 1))
		{
			// Fill the buffer with semi-random data
			for (count2 = 0; count2 < physicalDisk->sectorSize; count2 ++)
				buffer[count2] = kernelRandomFormatted(0, 255);

			for (count2 = 1; count2 < numSectors; count2 ++)
				memcpy((buffer + (count2 * physicalDisk->sectorSize)), buffer,
					physicalDisk->sectorSize);
		}
		else
		{
			// Clear the buffer with NULLs
			memset(buffer, 0, bufferSize);
		}

		// Call the read-write function for a write operation
		status = readWrite(physicalDisk, logicalSector, numSectors, buffer,
			IOMODE_WRITE);
		if (status < 0)
			break;

		#if (DISK_CACHE)
		// Flush the data
		status = cacheSync(physicalDisk);
		if (status < 0)
			break;
		#endif // DISK_CACHE
	}

	kernelFree(buffer);

	// Unlock the disk
	kernelLockRelease(&physicalDisk->lock);

	return (status);
}


int kernelDiskGetStats(const char *diskName, diskStats *stats)
{
	// Return performance stats about the supplied disk name (if non-NULL,
	// otherwise about all the disks combined).

	int status = 0;
	kernelPhysicalDisk *physicalDisk = NULL;
	kernelDisk *logicalDisk = NULL;
	int count;

	if (!initialized)
		return (status = ERR_NOTINITIALIZED);

	// Check params.  It's okay for diskName to be NULL.
	if (!stats)
		return (status = ERR_NULLPARAMETER);

	memset(stats, 0, sizeof(diskStats));

	if (diskName)
	{
		// Get the disk structure
		physicalDisk = getPhysicalByName(diskName);
		if (!physicalDisk)
		{
			// Try logical
			if ((logicalDisk = kernelDiskGetByName(diskName)))
				physicalDisk = logicalDisk->physical;
			else
				return (status = ERR_NOSUCHENTRY);
		}

		memcpy(stats, (void *) &physicalDisk->stats, sizeof(diskStats));
	}
	else
	{
		for (count = 0; count < physicalDiskCounter; count ++)
		{
			physicalDisk = physicalDisks[count];
			stats->readTimeMs += physicalDisk->stats.readTimeMs;
			stats->readKbytes += physicalDisk->stats.readKbytes;
			stats->writeTimeMs += physicalDisk->stats.writeTimeMs;
			stats->writeKbytes += physicalDisk->stats.writeKbytes;
		}
	}

	return (status = 0);
}

