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
//  kernelFilesystemLinuxSwap.c
//

// This file contains the functions designed to interpret the Linux swap
// filesystem

#include "kernelFilesystem.h"
#include "kernelDriver.h"
#include "kernelError.h"
#include "kernelMalloc.h"
#include <string.h>
#include <sys/linuxswap.h>

static int initialized = 0;


static int readSwapHeader(const kernelDisk *theDisk, linuxSwapHeader *header)
{
	// This simple function will read the swap header into the supplied
	// structure.  Returns 0 on success negative on error.

	unsigned sectors = 0;
	kernelPhysicalDisk *physicalDisk = NULL;

	// Initialize the buffer we were given
	memset(header, 0, sizeof(linuxSwapHeader));

	physicalDisk = theDisk->physical;

	// The sector size must be non-zero
	if (!physicalDisk->sectorSize)
	{
		kernelError(kernel_error, "Disk sector size is zero");
		return (ERR_INVALID);
	}

	sectors = ((MEMORY_PAGE_SIZE / physicalDisk->sectorSize) +
		((MEMORY_PAGE_SIZE % physicalDisk->sectorSize)? 1 : 0));

	// Read the swap header
	return (kernelDiskReadSectors((char *) theDisk->name, 0, sectors, header));
}


static int writeSwapHeader(const kernelDisk *theDisk, linuxSwapHeader *header)
{
	// This simple function will write the swap header from the supplied
	// structure.  Returns 0 on success negative on error.

	unsigned sectors = 0;
	kernelPhysicalDisk *physicalDisk = NULL;

	physicalDisk = theDisk->physical;

	// The sector size must be non-zero
	if (!physicalDisk->sectorSize)
	{
		kernelError(kernel_error, "Disk sector size is zero");
		return (ERR_INVALID);
	}

	sectors = ((MEMORY_PAGE_SIZE / physicalDisk->sectorSize) +
		((MEMORY_PAGE_SIZE % physicalDisk->sectorSize)? 1 : 0));

	// Write the swap header
	return (kernelDiskWriteSectors((char *) theDisk->name, 0, sectors, header));
}


static int formatSectors(kernelDisk *theDisk, unsigned sectors, progress *prog)
{
	// This function does a basic format of a linux swap filesystem.

	int status = 0;
	kernelPhysicalDisk *physicalDisk = NULL;
	unsigned numPages = 0;
	linuxSwapHeader *header = NULL;

	physicalDisk = theDisk->physical;

	// Only format disk with 512-byte sectors
	if (physicalDisk->sectorSize != 512)
	{
		kernelError(kernel_error, "Cannot format a disk with sector size of "
			"%u (512 only)", physicalDisk->sectorSize);
		return (status = ERR_INVALID);
	}

	if (prog && (kernelLockGet(&prog->progLock) >= 0))
	{
		strcpy((char *) prog->statusMessage, "Formatting");
		kernelLockRelease(&prog->progLock);
	}

	// Get memory for the signature page
	if (sizeof(linuxSwapHeader) != MEMORY_PAGE_SIZE)
		kernelError(kernel_error, "linuxSwapHeader size != MEMORY_PAGE_SIZE");

	numPages = ((sectors / (MEMORY_PAGE_SIZE / physicalDisk->sectorSize)) - 1);

	if ((numPages < 10) || (numPages > LINUXSWAP_MAXPAGES))
	{
		kernelError(kernel_error, "Illegal number of pages (%u) must be "
			"10-%lu", numPages, LINUXSWAP_MAXPAGES);
		return (status = ERR_BOUNDS);
	}

	header = kernelMalloc(sizeof(linuxSwapHeader));
	if (!header)
		return (status = ERR_MEMORY);

	// Fill out the header
	strncpy(header->magic.magic, LINUXSWAP_MAGIC2, 10);
	header->info.version = 1;
	header->info.lastPage = (numPages - 1);

	status = kernelDiskWriteSectors((char *) theDisk->name, 0,
		(sizeof(linuxSwapHeader) / physicalDisk->sectorSize), header);
	kernelFree(header);

	if (status < 0)
		return (status);

	strcpy((char *) theDisk->fsType, FSNAME_LINUXSWAP);

	if (prog && (kernelLockGet(&prog->progLock) >= 0))
	{
		strcpy((char *) prog->statusMessage, "Syncing disk");
		kernelLockRelease(&prog->progLock);
	}

	return (status = 0);
}


/////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////
//
//  Standard filesystem driver functions
//
/////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////

static int detect(kernelDisk *theDisk)
{
	// This function is used to determine whether the data on a disk
	// structure is using a Linux swap filesystem.  Returns 1 for true, 0 for
	// false, and negative if it encounters an error

	int status = 0;
	unsigned char sectBuff[MEMORY_PAGE_SIZE];
	linuxSwapHeader *header = (linuxSwapHeader *) sectBuff;

	if (!initialized)
		return (status = ERR_NOTINITIALIZED);

	// Check params
	if (!theDisk)
	{
		kernelError(kernel_error, "NULL parameter");
		return (status = ERR_NULLPARAMETER);
	}

	// Read the swap header
	status = readSwapHeader(theDisk, header);
	if (status < 0)
		// Not linux-swap
		return (status = 0);

	// Check for the signature
	if (!strncmp(header->magic.magic, LINUXSWAP_MAGIC1, 10) ||
		!strncmp(header->magic.magic, LINUXSWAP_MAGIC2, 10))
	{
		// Linux-swap
		strcpy((char *) theDisk->fsType, FSNAME_LINUXSWAP);
		strncpy((char *) theDisk->filesystem.label, header->info.volumeLabel,
			16);
		return (status = 1);
	}
	else
	{
		// Not linux-swap
		return (status = 0);
	}
}


static int format(kernelDisk *theDisk, const char *type,
	const char *label __attribute__((unused)),
	int longFormat __attribute__((unused)), progress *prog)
{
	// This function does a basic format of a linux swap filesystem.

	int status = 0;

	if (!initialized)
		return (status = ERR_NOTINITIALIZED);

	// Check params.  It's okay for all other params to be NULL.
	if (!theDisk || !type)
	{
		kernelError(kernel_error, "NULL parameter");
		return (status = ERR_NULLPARAMETER);
	}

	status = formatSectors(theDisk, theDisk->numSectors, prog);

	if (prog && (kernelLockGet(&prog->progLock) >= 0))
	{
		prog->complete = 1;
		kernelLockRelease(&prog->progLock);
	}

	return (status);
}


static int clobber(kernelDisk *theDisk)
{
	// This function destroys anything that might cause this disk to be
	// detected as having a linux swap filesystem.

	int status = 0;
	unsigned char sectBuff[MEMORY_PAGE_SIZE];
	linuxSwapHeader *header = (linuxSwapHeader *) sectBuff;

	if (!initialized)
		return (status = ERR_NOTINITIALIZED);

	// Check params
	if (!theDisk)
	{
		kernelError(kernel_error, "NULL parameter");
		return (status = ERR_NULLPARAMETER);
	}

	status = readSwapHeader(theDisk, header);
	if (status < 0)
		return (status);

	memset(header->magic.magic, 0, 10);

	return (status = writeSwapHeader(theDisk, header));
}


static int resizeConstraints(kernelDisk *theDisk, uquad_t *minSectors,
	uquad_t *maxSectors, progress *prog)
{
	// Return the minimum and maximum resize values

	int status = 0;
	kernelPhysicalDisk *physicalDisk = NULL;

	physicalDisk = theDisk->physical;

	*minSectors = 10;
	*maxSectors = ((MEMORY_PAGE_SIZE / physicalDisk->sectorSize) *
		LINUXSWAP_MAXPAGES);

	if (prog && (kernelLockGet(&prog->progLock) >= 0))
	{
		prog->complete = 1;
		kernelLockRelease(&prog->progLock);
	}

	return (status = 0);
}


static int resize(kernelDisk *theDisk, uquad_t sectors, progress *prog)
{
	// This is a dummy resize function, since all we do is format the disk
	// to the requested size

	int status = 0;

	// Check params
	if (!theDisk)
	{
		kernelError(kernel_error, "NULL parameter");
		return (status = ERR_NULLPARAMETER);
	}

	if (sectors > theDisk->numSectors)
	{
		kernelError(kernel_error, "Resize value (%llu) exceeds disk size "
			"(%llu)", sectors, theDisk->numSectors);
		return (status = ERR_RANGE);
	}

	return (status = formatSectors(theDisk, sectors, prog));
}


static int readDir(kernelFileEntry *directory)
{
	// This is a dummy readDir function.  See comment for mount() above.

	int status = 0;

	if (!initialized)
		return (status = ERR_NOTINITIALIZED);

	// Check params
	if (!directory)
	{
		kernelError(kernel_error, "NULL parameter");
		return (status = ERR_NULLPARAMETER);
	}

	// Make sure it's really a directory, and not a regular file
	if (directory->type != dirT)
	{
		kernelError(kernel_error, "Entry to scan is not a directory");
		return (status = ERR_NOTADIR);
	}

	// Manufacture some "." and ".." entries
	status = kernelFileMakeDotDirs(directory->parentDirectory, directory);
	if (status < 0)
	{
		kernelError(kernel_warn, "Unable to create '.' and '..' directory "
			"entries");
		return (status);
	}

	return (status = 0);
}


static int mount(kernelDisk *theDisk)
{
	// For the moment, this is a dummy mount function.  Basically it allows
	// a 'mount' operation to succeed without actually doing anything --
	// because for the moment we don't implement swapping, and of course there
	// are no files in a linux swap partition.  In other words, a placeholder.

	int status = 0;
	kernelPhysicalDisk *physicalDisk = NULL;
	linuxSwapHeader *header = NULL;

	if (!initialized)
		return (status = ERR_NOTINITIALIZED);

	// Check params
	if (!theDisk)
	{
		kernelError(kernel_error, "NULL parameter");
		return (status = ERR_NULLPARAMETER);
	}

	// The filesystem data cannot exist
	theDisk->filesystem.filesystemData = NULL;

	physicalDisk = theDisk->physical;

	// Attach our new FS data
	header = kernelMalloc(sizeof(linuxSwapHeader));
	if (!header)
		return (status = ERR_MEMORY);

	status = readSwapHeader(theDisk, header);
	if (status < 0)
		return (status);

	status = readDir(theDisk->filesystem.filesystemRoot);
	if (status < 0)
		return (status);

	theDisk->filesystem.filesystemData = header;

	// Get the label
	strncpy((char *) theDisk->filesystem.label, header->info.volumeLabel, 16);

	// Specify the filesystem block size
	theDisk->filesystem.blockSize = physicalDisk->sectorSize;

	resizeConstraints(theDisk, (uquad_t *) &theDisk->filesystem.minSectors,
		(uquad_t *) &theDisk->filesystem.maxSectors, NULL);

	// Read-only
	theDisk->filesystem.readOnly = 1;

	// Set the proper filesystem type name on the disk structure
	strcpy((char *) theDisk->fsType, FSNAME_LINUXSWAP);

	return (status = 0);
}


static int unmount(kernelDisk *theDisk)
{
	// This is a dummy unmount function.  See comment for mount() above.

	int status = 0;

	if (!initialized)
		return (status = ERR_NOTINITIALIZED);

	// Check params
	if (!theDisk)
	{
		kernelError(kernel_error, "NULL parameter");
		return (status = ERR_NULLPARAMETER);
	}

	// Deallocate memory
	if (theDisk->filesystem.filesystemData)
		kernelFree(theDisk->filesystem.filesystemData);
	theDisk->filesystem.filesystemData = NULL;

	return (status = 0);
}


static kernelFilesystemDriver defaultLinuxSwapDriver = {
	FSNAME_LINUXSWAP, // Driver name
	detect,
	format,
	clobber,
	NULL,	// driverCheck
	NULL,	// driverDefragment
	NULL,	// driverStat
	NULL,	// getFreeBytes
	resizeConstraints,
	resize,
	mount,
	unmount,
	NULL,	// driverNewEntry
	NULL,	// driverInactiveEntry
	NULL,	// driverResolveLink
	NULL,	// driverReadFile
	NULL,	// driverWriteFile
	NULL,	// driverCreateFile
	NULL,	// driverDeleteFile
	NULL,	// driverFileMoved
	readDir,
	NULL,	// driverWriteDir
	NULL,	// driverMakeDir
	NULL,	// driverRemoveDir
	NULL,	// driverTimestamp
	NULL	// driverSetBlocks
};


/////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////
//
//  Below here, the functions are exported for external use
//
/////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////

int kernelFilesystemLinuxSwapInitialize(void)
{
	// Initialize the driver

	int status = 0;

	// Register our driver
	status = kernelSoftwareDriverRegister(linuxSwapDriver,
		&defaultLinuxSwapDriver);

	initialized = 1;

	return (status);
}

