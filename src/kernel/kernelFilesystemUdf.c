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
//  kernelFilesystemUdf.c
//

// This file contains the functions designed to interpret the UDF filesystem
// (commonly found on DVD disks)

#include "kernelFilesystemUdf.h"
#include "kernelDebug.h"
#include "kernelDriver.h"
#include "kernelError.h"
#include "kernelFilesystem.h"
#include "kernelMalloc.h"
#include <stdlib.h>
#include <string.h>
#include <sys/iso.h>


static void decodeDstring(char *dest, const char *src, int length)
{
	// Decode a UDF dstring from 'src' to the ASCII character string 'dest'

	int count;

	kernelDebugHex((char *) src, length);

	// Not interested in the byte at the end.
	length -= 1;

	if ((src[0] != 8) && (src[0] != 16))
	{
		kernelError(kernel_error, "Unsupported dstring char length %d", src[0]);
		return;
	}

	if (src[0] == 16)
		length /= 2;

	for (count = 0; count < length; count ++)
	{
		switch (src[0])
		{
			case 8:
				dest[count] = src[count + 1];
				break;

			case 16:
				dest[count] = (char)((short *) src)[count + 1];
				break;
		}

		if (!dest[count])
			break;
	}

	dest[length] = '\0';
}


static void makeSystemTime(udfTimestamp *timestamp, unsigned *sysDate,
	unsigned *sysTime)
{
	// This function takes a UDF date/time value and returns the equivalent in
	// packed-BCD system format.

	// The year
	*sysDate = (timestamp->year << 9);
	// The month (1-12)
	*sysDate |= (timestamp->month << 5);
	// Day of the month (1-31)
	*sysDate |= timestamp->day;
	// The hour
	*sysTime = (timestamp->hour << 12);
	// The minute
	*sysTime |= (timestamp->minute << 6);
	// The second
	*sysTime |= timestamp->second;

	return;
}


static udfInternalData *getUdfData(kernelDisk *theDisk)
{
	// This function reads the filesystem parameters from the disk.

	int status = 0;
	udfInternalData *udfData = theDisk->filesystem.filesystemData;
	unsigned char *buffer = NULL;
	unsigned primVolDescSeqLocation = 0;
	unsigned primVolDescSeqBytes = 0;
	unsigned primVolDescSeqSectors = 0;
	udfDescTag *tag = NULL;
	udfPrimaryVolDesc *primDesc = NULL;
	udfPartitionDesc *partDesc = NULL;
	udfFileSetDesc *fileSetDesc = NULL;
	int len = 0;
	unsigned count;

	// Have we already read the parameters for this filesystem?
	if (udfData)
		return (udfData);

	// We must allocate some new memory to hold information about the filesystem
	udfData = kernelMalloc(sizeof(udfInternalData));
	if (!udfData)
		return (udfData);

	// Attach the disk structure to the udfData structure
	udfData->disk = theDisk;

	// Read the anchor volume descriptor
	buffer = kernelMalloc(theDisk->physical->sectorSize);
	if (!buffer)
	{
		status = ERR_MEMORY;
		goto out;
	}

	kernelDebug(debug_fs, "UDF: Read anchor vol desc at %u",
		(theDisk->physical->lastSession + UDF_ANCHOR_VOLDESC_SECTOR));
	status = kernelDiskReadSectors((char *) theDisk->name,
		(theDisk->physical->lastSession + UDF_ANCHOR_VOLDESC_SECTOR),
		1, buffer);
	if (status < 0)
		goto out;

	// Check the anchor volume descriptor tag identifier
	if (((udfAnchorVolDesc *) buffer)->tag.tagId != UDF_TAGID_ANCHORVOLDESC)
	{
		kernelError(kernel_warn, "Anchor vol descriptor tag ID is %d not %d",
			((udfAnchorVolDesc *) buffer)->tag.tagId, UDF_TAGID_ANCHORVOLDESC);
		status = ERR_BADDATA;
		goto out;
	}

	primVolDescSeqLocation =
		((udfAnchorVolDesc *) buffer)->primVolDescExt.location;
	primVolDescSeqBytes =
		((udfAnchorVolDesc *) buffer)->primVolDescExt.byteLength;
	primVolDescSeqSectors =
		(primVolDescSeqBytes / theDisk->physical->sectorSize);

	kernelFree(buffer);

	// Read the prim volume descriptor sequence
	buffer = kernelMalloc(primVolDescSeqBytes);
	if (!buffer)
	{
		status = ERR_MEMORY;
		goto out;
	}

	kernelDebug(debug_fs, "UDF: Read prim vol desc seq %u bytes (%u sectors) "
		"at %u", primVolDescSeqBytes, primVolDescSeqSectors,
		primVolDescSeqLocation);
	status =
		kernelDiskReadSectors((char *) theDisk->name, primVolDescSeqLocation,
			primVolDescSeqSectors, buffer);
	if (status < 0)
		goto out;

	// Scan the prim volume desriptor sequence
	for (count = 0; count < primVolDescSeqSectors; count ++)
	{
		tag = (udfDescTag *)(buffer + (count * theDisk->physical->sectorSize));

		if (tag->tagId == UDF_TAGID_PRIMARYVOLDESC)
		{
			primDesc = (udfPrimaryVolDesc *)
				(buffer + (count * theDisk->physical->sectorSize));
			decodeDstring((char *) theDisk->filesystem.label,
				primDesc->identifier, 32);

			// Remove unnecessary whitespace at the end
			while ((len = strlen((char *) theDisk->filesystem.label)) &&
				(theDisk->filesystem.label[len - 1] == ' '))
			{
				theDisk->filesystem.label[len - 1] = '\0';
			}

			makeSystemTime(&primDesc->recordTime,
				(unsigned *) &udfData->recordDate,
				(unsigned *) &udfData->recordTime);

			kernelDebug(debug_fs, "UDF: Volume label \"%s\"",
				theDisk->filesystem.label);
		}

		if (tag->tagId == UDF_TAGID_PARTDESC)
		{
			partDesc = (udfPartitionDesc *)
				(buffer + (count * theDisk->physical->sectorSize));
			udfData->partLogical = partDesc->startLocation;
			udfData->partSectors = partDesc->length;
			kernelDebug(debug_fs, "UDF: Partition start %u length %u",
				udfData->partLogical, udfData->partSectors);
		}
	}

	kernelFree(buffer);

	// Read the file set descriptor
	buffer = kernelMalloc(theDisk->physical->sectorSize);
	if (!buffer)
	{
		status = ERR_MEMORY;
		goto out;
	}

	status = kernelDiskReadSectors((char *) theDisk->name, udfData->partLogical,
		1, buffer);
	if (status < 0)
		goto out;

	fileSetDesc = (udfFileSetDesc *) buffer;

	// Check the file set descriptor tag identifier
	if (fileSetDesc->tag.tagId != UDF_TAGID_FILESETDESC)
	{
		kernelError(kernel_warn, "File set descriptor tag ID is %d not %d",
		fileSetDesc->tag.tagId, UDF_TAGID_FILESETDESC);
		status = ERR_BADDATA;
		goto out;
	}

	kernelDebug(debug_fs, "UDF: logicalVolIdCharSet %d fileSetCharSet %d",
		fileSetDesc->logicalVolIdCharSet.type,
		fileSetDesc->fileSetCharSet.type);

	// Get the root directory location
	udfData->rootIcbLogical =
		(udfData->partLogical + fileSetDesc->rootDirIcb.location);

	kernelDebug(debug_fs, "UDF: Root dir start %u", udfData->rootIcbLogical);

	// Attach our new FS data to the filesystem structure
	theDisk->filesystem.filesystemData = (void *) udfData;

	// Specify the filesystem block size
	theDisk->filesystem.blockSize = theDisk->physical->sectorSize;

	// 'minSectors' and 'maxSectors' are the same as the current sectors,
	// since we don't support resizing.
	theDisk->filesystem.minSectors = theDisk->numSectors;
	theDisk->filesystem.maxSectors = theDisk->numSectors;

	kernelFree(buffer);
	buffer = NULL;

out:
	if (status < 0)
	{
		if (udfData)
			kernelFree((void *) udfData);

		udfData = NULL;
	}

	if (buffer)
		kernelFree(buffer);

	return (udfData);
}


static void fillEntry(udfInternalData *udfData, udfFileEntry *udfEntry,
	kernelFileEntry *entry)
{
	// Given pointers to a UDF 'file entry' structure and a kernelFileEntry,
	// fill in the kernelFileEntry.

	switch(udfEntry->icbTag.fileType)
	{
		case 3:
			entry->type = linkT;
			break;
		case 4:
			entry->type = dirT;
			break;
		case 5:
			entry->type = fileT;
			break;
		default:
			entry->type = unknownT;
			break;
	}

	entry->creationTime = udfData->recordTime;
	entry->creationDate = udfData->recordDate;

	makeSystemTime(&udfEntry->accessTime, (unsigned *) &entry->accessedDate,
		(unsigned *) &entry->accessedTime);

	makeSystemTime(&udfEntry->modifiedTime, (unsigned *) &entry->modifiedDate,
		(unsigned *) &entry->modifiedTime);

	entry->size = udfEntry->length;
	entry->blocks = udfEntry->blocks;
}


static int readEntry(udfInternalData *udfData, unsigned icbLogical,
	udfFileEntry *udfEntry, kernelFileEntry *entry)
{
	// Read the UDF file entry located at sector 'icbLogical' and call fillEntry
	// to save the relevant data in the kernelFileEntry

	int status = 0;
	udfFileData *fileData = NULL;
	udfShortAllocDesc *allocDesc = NULL;

	kernelDebug(debug_fs, "UDF: Read ICB for %s at %u", entry->name,
		icbLogical);

	status = kernelDiskReadSectors((char *) udfData->disk->name, icbLogical, 1,
		udfEntry);
	if (status < 0)
		return (status);

	// Make sure that we've loaded an ICB file entry
	if (udfEntry->tag.tagId != UDF_TAGID_FILEENTRYDESC)
	{
		kernelError(kernel_error, "File entry for %s is not valid "
			"(tag %d != %d)", entry->name, udfEntry->tag.tagId,
			UDF_TAGID_FILEENTRYDESC);
		return (status = ERR_BADDATA);
	}

	fillEntry(udfData, udfEntry, entry);

	if (udfEntry->allocDescsLength != sizeof(udfShortAllocDesc))
	{
		kernelError(kernel_warn, "File %s has alloc desc length %u not %u",
			entry->name, udfEntry->allocDescsLength, sizeof(udfShortAllocDesc));
		kernelDebug(debug_fs, "UDF: FileEntry\n"
			"  tag %u maxEntries %u linkCount %u recordLength %u\n"
			"  length %llu blocks %llu",
			udfEntry->tag.tagId, udfEntry->icbTag.maxEntries,
			udfEntry->linkCount, udfEntry->recordLength,
			udfEntry->length, udfEntry->blocks);
	}

	if (!entry->driverData)
	{
		kernelError(kernel_error, "File %s has no private data", entry->name);
		return (status = ERR_NODATA);
	}

	fileData = (udfFileData *) entry->driverData;
	allocDesc = (udfShortAllocDesc *)(udfEntry->extdAttrs +
		udfEntry->extdAttrsLength);

	fileData->blockNumber = (udfData->partLogical + allocDesc->location);

	return (status);
}


static int scanDirectory(udfInternalData *udfData, kernelFileEntry *dirEntry)
{
	int status = 0;
	void *buffer = NULL;
	udfFileEntry *udfEntry = NULL;
	udfFileIdDesc *fileId = NULL;
	kernelFileEntry *entry = NULL;
	unsigned pad = 0;
	unsigned count;

	// Make sure it's really a directory, and not a regular file
	if (dirEntry->type != dirT)
	{
		kernelError(kernel_error, "Entry to scan is not a directory");
		return (status = ERR_NOTADIR);
	}

	// Make sure it's not zero-length
	if (!dirEntry->blocks)
	{
		kernelError(kernel_error, "Directory has no blocks");
		return (status = ERR_NODATA);
	}

	// Allocate a buffer for the directory contents
	buffer = kernelMalloc(dirEntry->blocks *
		udfData->disk->physical->sectorSize);
	if (!buffer)
		return (status = ERR_MEMORY);

	// Read the directory contents
	status = kernelDiskReadSectors((char *) udfData->disk->name,
		((udfFileData *) dirEntry->driverData)->blockNumber, dirEntry->blocks,
		buffer);
	if (status < 0)
		goto out;

	// Manufacture some "." and ".." entries
	status = kernelFileMakeDotDirs(dirEntry->parentDirectory, dirEntry);
	if (status < 0)
		kernelError(kernel_warn, "Unable to create '.' and '..' directory "
 			"entries");

	udfEntry = kernelMalloc(udfData->disk->physical->sectorSize);
	if (!udfEntry)
	{
		status = ERR_MEMORY;
		goto out;
	}

	// Loop through the entries
	fileId = (udfFileIdDesc *) buffer;
	for (count = 0; (((unsigned) fileId - (unsigned) buffer) <
		(dirEntry->blocks * udfData->disk->physical->sectorSize));
		count ++)
	{
		// Make sure this is a file identifier
		if (fileId->tag.tagId != UDF_TAGID_FILEIDDESC)
		{
			// NULL (terminating) entry?
			if (!fileId->tag.tagId)
			{
				break;
			}
			else
			{
				kernelError(kernel_error, "File identifier for %s is not valid "
					"(tag %d != %d)", dirEntry->name, fileId->tag.tagId,
					UDF_TAGID_FILEIDDESC);
				return (status = ERR_BADDATA);
			}
		}

		// If 'characteristics' bit 0 (existence), 2 (deleted), or 3 (parent)
		// are set, skip this entry.
		if (fileId->charx & 0x0D)
			goto next;

		// Get a new file entry
		entry = kernelFileNewEntry((kernelDisk *) udfData->disk);
		if (!entry)
		{
			status = ERR_NOFREE;
			goto out;
		}

		// Copy the name
		decodeDstring((char *) entry->name,
			((void *) fileId + offsetof(udfFileIdDesc, implUse) +
				fileId->implUseLength), fileId->idLength);

		kernelDebug(debug_fs, "UDF: New entry \"%s\" implOff %lu implLen %u "
			"idOff %lu idLen %u charx %02x", entry->name,
			offsetof(udfFileIdDesc, implUse), fileId->implUseLength,
			(offsetof(udfFileIdDesc, implUse) + fileId->implUseLength),
			fileId->idLength, fileId->charx);

		// Read the entry
		status = readEntry(udfData, (udfData->partLogical +
			fileId->icb.location), udfEntry, entry);
		if (status < 0)
			goto out;

		// Insert the entry
		status = kernelFileInsertEntry(entry, dirEntry);
		if (status < 0)
			goto out;

	next:
		// Move to the next entry
		pad = ((4 * ((fileId->idLength + fileId->implUseLength +
			offsetof(udfFileIdDesc, implUse) + 3) / 4)) -
			(fileId->idLength + fileId->implUseLength +
			offsetof(udfFileIdDesc, implUse)));
		fileId = (udfFileIdDesc *)
			((void *) fileId + offsetof(udfFileIdDesc, implUse) +
			fileId->implUseLength + fileId->idLength + pad);
	}

	status = 0;

 out:
	if (buffer)
		kernelFree(buffer);
	if (udfEntry)
		kernelFree(udfEntry);

	return (status);
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
	// is using an UDF filesystem.  It just looks for a 'magic number' on the
	// disk to identify UDF.  Any data that it gathers is discarded when the
	// call terminates.  It returns 1 for true, 0 for false, and negative if
	// it encounters an error

	int status = 0;
	void *buffer = NULL;
	udfBeaDesc *beaDesc = NULL;
	udfVolSeqDesc *volSeqDesc = NULL;
	udfTeaDesc *teaDesc = NULL;
	unsigned count;

	kernelDebug(debug_fs, "UDF: attempt detection");

	// Check params
	if (!theDisk)
	{
		kernelError(kernel_error, "NULL parameter");
		return (status = ERR_NULLPARAMETER);
	}

	// Look for the BEA, Volume Sequence Descriptor, and TEA
	buffer = kernelMalloc(theDisk->physical->sectorSize * 16);
	if (!buffer)
		return (status = ERR_MEMORY);

	// Do a dummy read to ensure that the TOC has been properly read, and
	// therefore the information for the last session is available.
	status = kernelDiskReadSectors((char *) theDisk->name,
		ISO_PRIMARY_VOLDESC_SECTOR, 1, buffer);
	if (status < 0)
	{
		kernelFree(buffer);
		return (status);
	}

	kernelDebug(debug_fs, "UDF: sector size %u last session at %u",
		theDisk->physical->sectorSize, theDisk->physical->lastSession);

	// The sector size must be non-zero
	if (!theDisk->physical->sectorSize)
	{
		kernelError(kernel_error, "Disk sector size is zero");
		kernelFree(buffer);
		return (status = ERR_INVALID);
	}

	// Load 16 sectors starting where we think our BEA descriptor should be.
	status = kernelDiskReadSectors((char *) theDisk->name,
		(theDisk->physical->lastSession + ISO_PRIMARY_VOLDESC_SECTOR),
		16, buffer);
	if (status < 0)
	{
		kernelFree(buffer);
		return (status);
	}

	// Loop through the sectors we loaded.
	for (count = 0; count < 16; count ++)
	{
		beaDesc = (buffer + (count * theDisk->physical->sectorSize));
		volSeqDesc = (buffer + ((count + 1) * theDisk->physical->sectorSize));
		teaDesc = (buffer + ((count + 2) * theDisk->physical->sectorSize));

		// Is this our BEA descriptor?  If it is, or else if it's not an ISO
		// descriptor, stop looking.
		if (!strncmp(beaDesc->identifier, UDF_STANDARD_IDENTIFIER_BEA, 5) ||
			strncmp(beaDesc->identifier, ISO_STANDARD_IDENTIFIER , 5))
		{
			break;
		}
	}

	if (strncmp(beaDesc->identifier, UDF_STANDARD_IDENTIFIER_BEA, 5) ||
		(strncmp(volSeqDesc->identifier, UDF_STANDARD_IDENTIFIER_VOLSEQ2, 5) &&
		strncmp(volSeqDesc->identifier, UDF_STANDARD_IDENTIFIER_VOLSEQ3, 5)) ||
		strncmp(teaDesc->identifier, UDF_STANDARD_IDENTIFIER_TEA, 5))
	{
		kernelDebug(debug_fs, "UDF: identifiers not found (%s, %s, %s)",
		beaDesc->identifier, volSeqDesc->identifier,
		teaDesc->identifier);
		kernelFree(buffer);
		return (status = 0);
	}

	kernelFree(buffer);

	strcpy((char *) theDisk->fsType, FSNAME_UDF);

	theDisk->filesystem.blockSize = theDisk->physical->sectorSize;
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
	udfInternalData *udfData = NULL;
	udfFileEntry *udfEntry = NULL;

	// Check params
	if (!theDisk)
	{
		kernelError(kernel_error, "NULL parameter");
		return (status = ERR_NULLPARAMETER);
	}

	// The filesystem data cannot exist
	theDisk->filesystem.filesystemData = NULL;

	// Get the UDF data for the requested filesystem.  We don't need the info
	// right now -- we just want to collect it.
	udfData = getUdfData(theDisk);
	if (!udfData)
		return (status = ERR_BADDATA);

	udfEntry = kernelMalloc(theDisk->physical->sectorSize);
	if (!udfEntry)
		return (status = ERR_MEMORY);

	kernelDebug(debug_fs, "UDF: Read root directory ICB");

	// Read the entry for the root directory
	status = readEntry(udfData, udfData->rootIcbLogical, udfEntry,
		theDisk->filesystem.filesystemRoot);

	kernelFree(udfEntry);

	if (status < 0)
		return (status);

	// Read the filesystem's root directory
	status = scanDirectory(udfData, theDisk->filesystem.filesystemRoot);
	if (status < 0)
	{
		kernelError(kernel_error, "Unable to read the filesystem's root "
			"directory");
		return (status = ERR_BADDATA);
	}

	// Set the proper filesystem type name on the disk structure
	strcpy((char *) theDisk->fsType, FSNAME_UDF);

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
	// to attach filesystem-specific data to the file entry

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

	entry->driverData = kernelMalloc(sizeof(udfFileData));
	if (!entry->driverData)
	{
		kernelError(kernel_error, "Error allocating memory for UDF "
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
	// to deallocate our filesystem-specific data from the file entry

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
		memset(entry->driverData, 0, sizeof(udfFileData));

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
	// to be resolved.  By default this driver never resolves UDF symbolic
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
	udfInternalData *udfData = NULL;
	udfFileData *dirRec = NULL;

	// Check params
	if (!theFile || !buffer)
	{
		kernelError(kernel_error, "NULL parameter");
		return (status = ERR_NULLPARAMETER);
	}

	// Make sure there's a directory record attached
	dirRec = (udfFileData *) theFile->driverData;
	if (!dirRec)
	{
		kernelError(kernel_error, "File \"%s\" has no private data",
			theFile->name);
		return (status = ERR_NODATA);
	}

	// Get the UDF data for the filesystem.
	udfData = getUdfData(theFile->disk);
	if (!udfData)
		return (status = ERR_BADDATA);

	status = kernelDiskReadSectors((char *) udfData->disk->name,
		(dirRec->blockNumber + blockNum), blocks, buffer);

	return (status);
}


static int readDir(kernelFileEntry *directory)
{
	// This function receives an emtpy file entry structure, which represents
	// a directory whose contents have not yet been read.  This will fill the
	// directory structure with its appropriate contents.  Returns 0 on
	// success, negative otherwise.

	int status = 0;
	udfInternalData *udfData = NULL;

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

	// Get the UDF data for the filesystem.
	udfData = getUdfData(directory->disk);
	if (!udfData)
		return (status = ERR_BADDATA);

	return (scanDirectory(udfData, directory));
}


static kernelFilesystemDriver defaultUdfDriver = {
	FSNAME_UDF, // Driver name
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

int kernelFilesystemUdfInitialize(void)
{
	// Register our driver
	return (kernelSoftwareDriverRegister(udfDriver, &defaultUdfDriver));
}

