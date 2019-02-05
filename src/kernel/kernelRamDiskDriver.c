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
//  kernelRamDiskDriver.c
//

// Driver for RAM disks.
// - Originally contributed by Davide Airaghi
// - Modified by Andy McLaughlin.

#include "kernelRamDiskDriver.h"
#include "kernelDisk.h"
#include "kernelError.h"
#include "kernelFilesystem.h"
#include "kernelLog.h"
#include "kernelMalloc.h"
#include "kernelMemory.h"
#include <stdio.h>
#include <string.h>

static kernelDriver *ramDiskDriver = NULL;
static kernelPhysicalDisk *disks[RAMDISK_MAX_DISKS];
static int numDisks = 0;


static int getNewDiskNumber(void)
{
	// Return an unused disk number

	int diskNumber = 0;
	int count;

	for (count = 0; count < numDisks ; count ++)
	{
		if (disks[count]->deviceNumber == diskNumber)
		{
			diskNumber += 1;
			count = -1;
			continue;
		}
	}

	return (diskNumber);
}


static kernelPhysicalDisk *findDiskByNumber(int diskNum)
{
	int count = 0;

	for (count = 0; count < numDisks; count ++)
	{
		if (disks[count]->deviceNumber == diskNum)
			return (disks[count]);
	}

	// Not found
	return (NULL);
}


static kernelPhysicalDisk *findDiskByName(const char *name)
{
	int count = 0;

	for (count = 0; count < numDisks; count ++)
	{
		if (!strncmp((char *) disks[count]->name, name, DISK_MAX_NAMELENGTH))
			return (disks[count]);
	}

	// Not found
	return (NULL);
}


static int readWriteSectors(int diskNum, uquad_t logicalSector,
	uquad_t numSectors, void *buffer, int read)
{
	// This function reads or writes sectors to/from the drive.  Returns 0 on
	// success, negative otherwise.

	int status = 0;
	kernelPhysicalDisk *physical = NULL;
	kernelRamDisk *ramDisk = NULL;
	unsigned start = 0;
	unsigned length = 0;

	physical = findDiskByNumber(diskNum);
	if (!physical)
	{
		kernelError(kernel_error, "No such RAM disk %d", diskNum);
		return (status = ERR_NOSUCHENTRY);
	}

	ramDisk = disks[diskNum]->driverData;
	if (!ramDisk)
	{
		kernelError(kernel_error, "RAM disk %s has no private data",
			physical->name);
		return (status = ERR_NODATA);
	}

	if ((logicalSector + numSectors) > physical->numSectors)
	{
		kernelError(kernel_error, "I/O attempt is outside the bounds of the "
			"disk");
		return (status = ERR_BOUNDS);
	}

	start = (logicalSector * RAMDISK_SECTOR_SIZE);
	length = (numSectors * RAMDISK_SECTOR_SIZE);

	// Wait for a lock
	status = kernelLockGet(&physical->lock);
	if (status < 0)
		return (status);

	// read/write

	if (read)
		memcpy(buffer, (ramDisk->data + start), length);
	else
		memcpy((ramDisk->data + start), buffer, length);

	// We are finished.  The data should be transferred.

	// Unlock
	kernelLockRelease(&physical->lock);

	return (status = 0);
}


static int driverReadSectors(int diskNum, uquad_t logicalSector,
	uquad_t numSectors, void *buffer)
{
	// This function is a wrapper for the readWriteSectors function.
	return (readWriteSectors(diskNum, logicalSector, numSectors, buffer,
		1));  // Read operation
}


static int driverWriteSectors(int diskNum, uquad_t logicalSector,
	uquad_t numSectors, const void *buffer)
{
	// This function is a wrapper for the readWriteSectors function.
	return (readWriteSectors(diskNum, logicalSector, numSectors,
		(void *) buffer, 0));  // Write operation
}


static int driverDetect(void *parent __attribute__((unused)),
	kernelDriver *driver)
{
	// Normally this function is used to detect and initialize devices, as
	// well as registering each one with any higher-level interfaces.  Since
	// RAM disks are not detected this way (rather, created by
	// kernelRamDiskCreate()) we'll save the parameters we were passed for
	// when we create disks later.

	ramDiskDriver = driver;
	return (0);
}


static kernelDiskOps ramDiskOps = {
	NULL,	// driverSetMotorState
	NULL,	// driverSetLockState
	NULL,	// driverSetDoorState
	NULL,	// driverMediaPresent
	NULL,	// driverMediaChanged
	driverReadSectors,
	driverWriteSectors,
	NULL	// driverFlush
};


/////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////
//
// Below here, the functions are exported for external use
//
/////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////

void kernelRamDiskDriverRegister(kernelDriver *driver)
{
	// Device driver registration.

	driver->driverDetect = driverDetect;
	driver->ops = &ramDiskOps;

	return;
}


int kernelDiskRamDiskCreate(unsigned size, char *name)
{
	// Given a size in bytes, and a pointer to a name buffer, create a RAM
	// disk and place the name of the new disk in the buffer.

	int status = 0;
	kernelPhysicalDisk *physical = NULL;
	kernelRamDisk *ramDisk = NULL;
	int diskNum = 0;

	// Check params.  It's okay for 'name' to be NULL.
	if (!size)
	{
		kernelError(kernel_error, "Disk size is NULL");
		return (status = ERR_NULLPARAMETER);
	}

	// Round the size value up to a multiple of RAMDISK_SECTOR_SIZE
	if (size % RAMDISK_SECTOR_SIZE)
		size += (RAMDISK_SECTOR_SIZE - (size % RAMDISK_SECTOR_SIZE));

	// Get memory for the physical disk and our private data
	physical = kernelMalloc(sizeof(kernelPhysicalDisk));
	ramDisk = kernelMalloc(sizeof(kernelRamDisk));
	if (!physical || !ramDisk)
		return (status = ERR_MEMORY);

	// Get a new disk number
	diskNum = getNewDiskNumber();

	sprintf((char *) physical->name, "ram%d", diskNum);
	physical->deviceNumber = diskNum;
	physical->description = "RAM disk";
	physical->type = (DISKTYPE_PHYSICAL | DISKTYPE_FIXED | DISKTYPE_RAMDISK);
	physical->flags = DISKFLAG_NOCACHE;

	physical->heads = 1;
	physical->cylinders = 1;
	physical->sectorsPerCylinder = (size / RAMDISK_SECTOR_SIZE);
	physical->numSectors = physical->sectorsPerCylinder;
	physical->sectorSize = RAMDISK_SECTOR_SIZE;

	physical->driverData = ramDisk;
	physical->driver = ramDiskDriver;

	// Get memory for the data
	ramDisk->data = kernelMemoryGetSystem(size, "ramdisk data");
	if (!ramDisk->data)
	{
		status = ERR_MEMORY;
		goto err_out;
	}

	disks[numDisks++] = physical;

	// Set up the kernel device
	ramDisk->dev.device.class = kernelDeviceGetClass(DEVICECLASS_DISK);
	ramDisk->dev.device.subClass =
		kernelDeviceGetClass(DEVICESUBCLASS_DISK_RAMDISK);
	ramDisk->dev.driver = ramDiskDriver;
	ramDisk->dev.data = (void *) physical;

	// Register the disk
	status = kernelDiskRegisterDevice(&ramDisk->dev);
	if (status < 0)
		goto err_out;

	kernelDiskReadPartitions((char *) physical->name);

	// Success
	if (name)
		strncpy(name, (char *) physical->name, DISK_MAX_NAMELENGTH);
	kernelLog("RAM disk %s created size %u", physical->name, size);
	return (status = 0);

err_out:
	if (ramDisk->data)
		kernelMemoryRelease(ramDisk->data);
	if (ramDisk)
		kernelFree(ramDisk);
	if (physical)
		kernelFree((void *) physical);
	return (status);
}


int kernelDiskRamDiskDestroy(const char *name)
{
	// Given the name of an existing RAM disk, destroy and deallocate it.

	int status = 0;
	kernelPhysicalDisk *physical = NULL;
	kernelRamDisk *ramDisk = NULL;
	int count;

	// Check params
	if (!name)
	{
		kernelError(kernel_error, "NULL parameter");
		return (status = ERR_NULLPARAMETER);
	}

	// Try to find the disk
	physical = findDiskByName(name);
	if (!physical)
	{
		kernelError(kernel_error, "No such RAM disk %s", name);
		return (status = ERR_NOSUCHENTRY);
	}

	ramDisk = physical->driverData;

	// If there are filesystems mounted on this disk, try to unmount them
	for (count = 0; count < physical->numLogical; count ++)
	{
		if (physical->logical[count].filesystem.mounted)
			kernelFilesystemUnmount((char *) physical->logical[count]
				.filesystem.mountPoint);
	}

	// Remove it from our list.
	if (numDisks > 1)
		for (count = 0; count < numDisks; count ++)
			if ((disks[count] == physical) && (count < (numDisks - 1)))
				disks[count] = disks[numDisks - 1];

	numDisks -= 1;

	// Wait for a lock on the disk
	status = kernelLockGet(&physical->lock);
	if (status < 0)
		return (status);

	// Remove it from the system's disks
	kernelDiskRemoveDevice(&ramDisk->dev);

	kernelLockRelease(&physical->lock);

	kernelLog("RAM disk %s destroyed", physical->name);

	// Free the data, driver data, and physical disk.
	if (ramDisk->data)
		kernelMemoryRelease(ramDisk->data);
	if (ramDisk)
		kernelFree(ramDisk);
	if (physical)
		kernelFree((void *) physical);

	return (status = 0);
}

