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
//  kernelFilesystemIso.c
//

// This file contains the functions designed to interpret the ISO9660
// filesystem (commonly found on CD-ROM disks)

#include "kernelFilesystemIso.h"
#include "kernelDisk.h"
#include "kernelDriver.h"
#include "kernelError.h"
#include "kernelFilesystem.h"
#include "kernelFile.h"
#include "kernelMalloc.h"
#include "kernelSysTimer.h"
#include <stdlib.h>
#include <string.h>


static int readPrimaryVolDesc(isoInternalData *isoData)
{
	// This simple function will read the primary volume descriptor into the
	// supplied buffer.  Returns 0 on success, negative on error.

	int status = 0;
	kernelPhysicalDisk *physicalDisk = isoData->disk->physical;

	// Do a dummy read from the CD-ROM to ensure that the TOC has been properly
	// read, and therefore the information for the last session is available.
	status = kernelDiskReadSectors((char *) isoData->disk->name,
		ISO_PRIMARY_VOLDESC_SECTOR, 1,
		(isoPrimaryDescriptor *) &isoData->volDesc);

	if (status < 0)
		return (status);

	// The sector size must be non-zero
	if (!physicalDisk->sectorSize)
	{
		kernelError(kernel_error, "Disk sector size is zero");
		return (status = ERR_INVALID);
	}

	// Initialize the volume descriptor we were given
	memset((void *) &isoData->volDesc, 0, sizeof(isoPrimaryDescriptor));

	// Read the primary volume descriptor
	status = kernelDiskReadSectors((char *) isoData->disk->name,
		(physicalDisk->lastSession + ISO_PRIMARY_VOLDESC_SECTOR), 1,
		(isoPrimaryDescriptor *) &isoData->volDesc);
	if (status < 0)
	{
		kernelError(kernel_error, "Unable to read the ISO primary volume "
			"descriptor");
		return (status);
	}

	// Not sure why this is necessary, exactly, since it's supposed to be
	// a 4-byte value.  But it's not.
	isoData->volDesc.blockSize &= 0xFFFF;

	return (status = 0);
}


static void makeSystemTime(unsigned char *isoTime, unsigned *date,
	unsigned *theTime)
{
	// This function takes an ISO date/time value and returns the equivalent in
	// packed-BCD system format.

	// The year
	*date = ((isoTime[0] + 1900) << 9);
	// The month (1-12)
	*date |= ((isoTime[1] & 0x0F) << 5);
	// Day of the month (1-31)
	*date |= (isoTime[2] & 0x1F);
	// The hour
	*theTime = ((isoTime[3] & 0x3F) << 12);
	// The minute
	*theTime |= ((isoTime[4] & 0x3F) << 6);
	// The second
	*theTime |= (isoTime[5] & 0x3F);

	return;
}


static void readDirRecord(isoDirectoryRecord *record,
	kernelFileEntry *fileEntry, unsigned blockSize)
{
	// Reads a directory record from the on-disk structure in 'buffer' to
	// our structure

	isoFileData *fileData = (isoFileData *) fileEntry->driverData;
	int count;

	// Copy the static bits of the directory record
	memcpy((void *) &fileData->dirRec, record, record->recordLength);

	// Copy the name into the file entry
	memcpy((char *) fileEntry->name, (char *) fileData->dirRec.name,
		fileData->dirRec.nameLength);
	fileEntry->name[fileData->dirRec.nameLength] = '\0';

	// Find the semicolon (if any) at the end of the name
	for (count = (fileData->dirRec.nameLength - 1); count > 0; count --)
	{
		if (fileEntry->name[count] == ';')
		{
			fileEntry->name[count] = '\0';
			fileData->versionNumber =
				atoi((char *)(fileEntry->name + count + 1));
			break;
		}
	}

	// Get the type
	fileEntry->type = fileT;

	if (fileData->dirRec.flags & ISO_FLAGMASK_DIRECTORY)
		fileEntry->type = dirT;

	if (fileData->dirRec.flags & ISO_FLAGMASK_ASSOCIATED)
		fileEntry->type = linkT;

	// Get the date and time
	makeSystemTime((unsigned char *) fileData->dirRec.date,
		(unsigned *) &fileEntry->creationDate,
		(unsigned *) &fileEntry->creationTime);
	fileEntry->accessedTime = fileEntry->creationTime;
	fileEntry->accessedDate = fileEntry->creationDate;
	fileEntry->modifiedTime = fileEntry->creationTime;
	fileEntry->modifiedDate = fileEntry->creationDate;

	fileEntry->size = fileData->dirRec.size;
	fileEntry->blocks = (fileEntry->size / blockSize);
	if (fileEntry->size % blockSize)
		fileEntry->blocks += 1;
	fileEntry->lastAccess = kernelSysTimerRead();
}


static isoInternalData *getIsoData(kernelDisk *theDisk)
{
	// This function reads the filesystem parameters from the disk.

	int status = 0;
	int len = 0;
	isoInternalData *isoData = theDisk->filesystem.filesystemData;

	// Have we already read the parameters for this filesystem?
	if (isoData)
		return (isoData);

	// We must allocate some new memory to hold information about the filesystem
	isoData = kernelMalloc(sizeof(isoInternalData));
	if (!isoData)
		return (isoData = NULL);

	// Attach the disk structure to the isoData structure
	isoData->disk = theDisk;

	// Read the primary volume descriptor into our isoInternalData buffer
	status = readPrimaryVolDesc(isoData);
	if (status < 0)
	{
		kernelFree((void *) isoData);
		return (isoData = NULL);
	}

	// Make sure it's a primary volume descriptor
	if (isoData->volDesc.type != (unsigned char) ISO_DESCRIPTORTYPE_PRIMARY)
	{
		kernelError(kernel_error, "Primary volume descriptor not found");
		kernelFree((void *) isoData);
		return (isoData = NULL);
	}

	// Get the root directory record
	readDirRecord((isoDirectoryRecord *) &isoData->volDesc.rootDirectoryRecord,
		theDisk->filesystem.filesystemRoot, isoData->volDesc.blockSize);

	// Attach our new FS data to the filesystem structure
	theDisk->filesystem.filesystemData = (void *) isoData;

	// Save the volume label
	strncpy((char *) theDisk->filesystem.label,
		(char *) isoData->volDesc.volumeIdentifier, 32);

	// Remove unnecessary whitespace at the end
	while ((len = strlen((char *) theDisk->filesystem.label)) &&
		(theDisk->filesystem.label[len - 1] == ' '))
	{
		theDisk->filesystem.label[len - 1] = '\0';
	}

	// Specify the filesystem block size
	theDisk->filesystem.blockSize = isoData->volDesc.blockSize;

	// 'minSectors' and 'maxSectors' are the same as the current sectors,
	// since we don't support resizing.
	theDisk->filesystem.minSectors = theDisk->numSectors;
	theDisk->filesystem.maxSectors = theDisk->numSectors;

	return (isoData);
}


static int scanDirectory(isoInternalData *isoData, kernelFileEntry *dirEntry)
{
	int status = 0;
	isoFileData *scanDirRec = NULL;
	unsigned bufferSize = 0;
	void *buffer = NULL;
	void *ptr = NULL;
	kernelFileEntry *fileEntry = NULL;

	// Make sure it's really a directory, and not a regular file
	if (dirEntry->type != dirT)
	{
		kernelError(kernel_error, "Entry to scan is not a directory");
		return (status = ERR_NOTADIR);
	}

	// Make sure it's not zero-length
	if (!dirEntry->blocks || !isoData->volDesc.blockSize)
	{
		kernelError(kernel_error, "Directory or blocksize is NULL");
		return (status = ERR_NODATA);
	}

	// Manufacture some "." and ".." entries
	status = kernelFileMakeDotDirs(dirEntry->parentDirectory, dirEntry);
	if (status < 0)
		kernelError(kernel_warn, "Unable to create '.' and '..' directory "
 			"entries");

	scanDirRec = (isoFileData *) dirEntry->driverData;
	if (!scanDirRec)
	{
		kernelError(kernel_error, "Directory \"%s\" has no private data",
			dirEntry->name);
		return (status = ERR_NODATA);
	}

	bufferSize = (dirEntry->blocks * isoData->volDesc.blockSize);
	if (bufferSize < dirEntry->size)
	{
		kernelError(kernel_error, "Wrong buffer size for directory!");
		return (status = ERR_BADDATA);
	}

	// Get a buffer for the directory
	buffer = kernelMalloc(bufferSize);
	if (!buffer)
	{
		kernelError(kernel_error, "Unable to get memory for directory buffer");
		return (status = ERR_MEMORY);
	}

	status = kernelDiskReadSectors((char *) isoData->disk->name,
		scanDirRec->dirRec.blockNumber, dirEntry->blocks, buffer);
	if (status < 0)
	{
		kernelFree(buffer);
		return (status);
	}

	// Loop through the contents
	ptr = buffer;
	while (ptr < (buffer + bufferSize))
	{
		if (!((unsigned char *) ptr)[0])
		{
			// This is a NULL entry.  If the next entry doesn't fit within
			// the same logical sector, it is placed in the next one.  Thus,
			// if we are not within the last sector we read, skip to the next
			// one.
			if ((((ptr - buffer) / isoData->volDesc.blockSize) + 1) <
				dirEntry->blocks)
			{
				ptr += (isoData->volDesc.blockSize -
					((ptr - buffer) % isoData->volDesc.blockSize));
				continue;
			}
			else
			{
				break;
			}
		}

		fileEntry = kernelFileNewEntry(dirEntry->disk);
		if (!fileEntry || !fileEntry->driverData)
		{
			kernelError(kernel_error, "Unable to get new filesystem entry or "
				"entry has no private data");
			kernelFree(buffer);
			return (status = ERR_NOCREATE);
		}

		readDirRecord((isoDirectoryRecord *) ptr, fileEntry,
			isoData->volDesc.blockSize);

		if ((fileEntry->name[0] < 32) || (fileEntry->name[0] > 126))
		{
			if (fileEntry->name[0] && (fileEntry->name[0] != 1))
				// Not the current directory, or the parent directory.  Warn
				// about funny ones like this.
				kernelError(kernel_warn, "Unknown directory entry type in %s",
					dirEntry->name);
			kernelFileReleaseEntry(fileEntry);
			ptr += (unsigned)((unsigned char *) ptr)[0];
			continue;
		}

		// Normal entry

		// Add it to the directory
		status = kernelFileInsertEntry(fileEntry, dirEntry);
		if (status < 0)
		{
			kernelFree(buffer);
			return (status);
		}

		ptr += (unsigned)((unsigned char *) ptr)[0];
	}

	kernelFree(buffer);
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
	// This function is used to determine whether the data on a disk structure
	// is using an ISO filesystem.  It just looks for a 'magic number' on the
	// disk to identify ISO.  Any data that it gathers is discarded when the
	// call terminates.  It returns 1 for true, 0 for false, and negative if
	// it encounters an error

	int status = 0;
	isoInternalData isoData;

	// Check params
	if (!theDisk)
	{
		kernelError(kernel_error, "NULL parameter");
		return (status = ERR_NULLPARAMETER);
	}

	// Clear stack memory
	memset((void * ) &isoData, 0, sizeof(isoInternalData));

	isoData.disk = theDisk;

	// Read the primary volume descriptor
	status = readPrimaryVolDesc(&isoData);
	if (status < 0)
		return (status);

	// Check for the standard identifier
	if (strncmp((char *) isoData.volDesc.identifier, ISO_STANDARD_IDENTIFIER,
		strlen(ISO_STANDARD_IDENTIFIER)))
	{
		// Not ISO
		return (status = 0);
	}

	strcpy((char *) theDisk->fsType, FSNAME_ISO);
	strncpy((char *) theDisk->filesystem.label,
		(char *) isoData.volDesc.volumeIdentifier, 32);

	theDisk->filesystem.blockSize = isoData.volDesc.blockSize;
	theDisk->filesystem.minSectors = 0;
	theDisk->filesystem.maxSectors = 0;

	return (status = 1);
}


static int mount(kernelDisk *theDisk)
{
	// This function initializes the filesystem driver by gathering all of
	// the required information from the boot sector.  In addition, it
	// dynamically allocates memory space for the "used" and "free" file and
	// directory structure arrays.

	int status = 0;
	isoInternalData *isoData = NULL;

	// Check params
	if (!theDisk)
	{
		kernelError(kernel_error, "NULL parameter");
		return (status = ERR_NULLPARAMETER);
	}

	// The filesystem data cannot exist
	theDisk->filesystem.filesystemData = NULL;

	// Get the ISO data for the requested filesystem.  We don't need the info
	// right now -- we just want to collect it.
	isoData = getIsoData(theDisk);
	if (!isoData)
		return (status = ERR_BADDATA);

	// Read the filesystem's root directory
	status = scanDirectory(isoData, theDisk->filesystem.filesystemRoot);
	if (status < 0)
	{
		kernelError(kernel_error, "Unable to read the filesystem's root "
			"directory");
		return (status = ERR_BADDATA);
	}

	// Set the proper filesystem type name on the disk structure
	strcpy((char *) theDisk->fsType, FSNAME_ISO);

	// Read-only
	theDisk->filesystem.readOnly = 1;

	return (status = 0);
}


static int unmount(kernelDisk *theDisk)
{
	// This function releases all of the stored information about a given
	// filesystem.

	int status = 0;

	// Check params
	if (!theDisk)
	{
		kernelError(kernel_error, "NULL parameter");
		return (status = ERR_NULLPARAMETER);
	}

	// Free the filesystem data
	if (theDisk->filesystem.filesystemData)
		status = kernelFree(theDisk->filesystem.filesystemData);

	return (status);
}


static int newEntry(kernelFileEntry *entry)
{
	// This function gets called when there's a new kernelFileEntry in the
	// filesystem (either because a file was created or because some existing
	// thing has been newly read from disk).  This gives us an opportunity
	// to attach ISO-specific data to the file entry

	int status = 0;

	// Check params
	if (!entry)
	{
		kernelError(kernel_error, "NULL parameter");
		return (status = ERR_NULLPARAMETER);
	}

	// Make sure there isn't already some sort of data attached to this
	// file entry, and that there is a filesystem attached
	if (entry->driverData)
	{
		kernelError(kernel_error, "Entry already has private filesystem data");
		return (status = ERR_ALREADY);
	}

	// Make sure there's an associated disk
	if (!entry->disk)
	{
		kernelError(kernel_error, "Entry has no associated filesystem");
		return (status = ERR_NOCREATE);
	}

	entry->driverData = kernelMalloc(sizeof(isoFileData));
	if (!entry->driverData)
	{
		kernelError(kernel_error, "Error allocating memory for ISO "
			"directory record");
		return (status = ERR_MEMORY);
	}

	return (status = 0);
}


static int inactiveEntry(kernelFileEntry *entry)
{
	// This function gets called when a kernelFileEntry is about to be
	// deallocated by the system (either because a file was deleted or because
	// the entry is simply being unbuffered).  This gives us an opportunity
	// to deallocate our ISO-specific data from the file entry

	int status = 0;

	// Check params
	if (!entry)
	{
		kernelError(kernel_error, "NULL parameter");
		return (status = ERR_NULLPARAMETER);
	}

	if (entry->driverData)
	{
		// Erase all of the data in this entry
		memset(entry->driverData, 0, sizeof(isoFileData));

		// Need to actually deallocate memory here.
		kernelFree(entry->driverData);

		// Remove the reference
		entry->driverData = NULL;
	}

	return (status = 0);
}


static int resolveLink(kernelFileEntry *linkEntry)
{
	// This is called by the kernelFile.c code when we have registered a
	// file or directory as a link, but not resolved it, and now it needs
	// to be resolved.  By default this driver never resolves ISO symbolic
	// links until they are needed, so this will get called when the system
	// wants to read/write the symbolically-linked directory or file.

	int status = 0;

	// Check params
	if (!linkEntry)
	{
		kernelError(kernel_error, "NULL parameter");
		return (status = ERR_NULLPARAMETER);
	}

	return (status = 0);
}


static int readFile(kernelFileEntry *theFile, unsigned blockNum,
	unsigned blocks, unsigned char *buffer)
{
	// This function is the "read file" function that the filesystem
	// driver exports to the world.  Returns 0 on success, negative otherwise.

	int status = 0;
	isoInternalData *isoData = NULL;
	isoFileData *dirRec = NULL;

	// Check params
	if (!theFile || !buffer)
	{
		kernelError(kernel_error, "NULL parameter");
		return (status = ERR_NULLPARAMETER);
	}

	// Make sure there's a directory record  attached
	dirRec = (isoFileData *) theFile->driverData;
	if (!dirRec)
	{
		kernelError(kernel_error, "File \"%s\" has no private data",
			theFile->name);
		return (status = ERR_NODATA);
	}

	// Get the ISO data for the filesystem.
	isoData = getIsoData(theFile->disk);
	if (!isoData)
		return (status = ERR_BADDATA);

	status =
		kernelDiskReadSectors((char *) isoData->disk->name,
			(dirRec->dirRec.blockNumber + blockNum), blocks, buffer);
	return (status);
}


static int readDir(kernelFileEntry *directory)
{
	// This function receives an emtpy file entry structure, which represents
	// a directory whose contents have not yet been read.  This will fill the
	// directory structure with its appropriate contents.  Returns 0 on
	// success, negative otherwise.

	int status = 0;
	isoInternalData *isoData = NULL;

	// Check params
	if (!directory)
	{
		kernelError(kernel_error, "NULL parameter");
		return (status = ERR_NULLPARAMETER);
	}

	// Make sure there's a directory record  attached
	if (!directory->driverData)
	{
		kernelError(kernel_error, "Directory \"%s\" has no private data",
			directory->name);
		return (status = ERR_NODATA);
	}

	// Get the ISO data for the filesystem.
	isoData = getIsoData(directory->disk);
	if (!isoData)
		return (status = ERR_BADDATA);

	return (scanDirectory(isoData, directory));
}


static kernelFilesystemDriver fsDriver = {
	FSNAME_ISO, // Driver name
	detect,
	NULL,	// driverFormat
	NULL,	// driverClobber
	NULL,	// driverCheck
	NULL,	// driverDefragment
	NULL,	// driverStat
	NULL,	// getFreeBytes
	NULL,	// driverResizeConstraints
	NULL,	// driverResize
	mount,
	unmount,
	newEntry,
	inactiveEntry,
	resolveLink,
	readFile,
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

int kernelFilesystemIsoInitialize(void)
{
	// Register our driver
	return (kernelSoftwareDriverRegister(isoDriver, &fsDriver));
}

