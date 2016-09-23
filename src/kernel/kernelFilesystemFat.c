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
//  kernelFilesystemFat.c
//

// This file contains the routines designed to interpret the FAT filesystem
// (commonly found on DOS(TM) and Windows(R) disks)

#include "kernelFilesystemFat.h"
#include "kernelDebug.h"
#include "kernelDriver.h"
#include "kernelError.h"
#include "kernelFile.h"
#include "kernelFilesystem.h"
#include "kernelLocale.h"
#include "kernelLock.h"
#include "kernelLog.h"
#include "kernelMalloc.h"
#include "kernelMultitasker.h"
#include "kernelRtc.h"
#include "kernelSysTimer.h"
#include <ctype.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/msdos.h>
#include <sys/progress.h>

#define _(string) kernelGetText(string)

static int initialized = 0;
int makeFatFreePid = -1;
fatInternalData *makingFatFree = NULL;


static int readBootSector(kernelDisk *theDisk, fatBPB *bpb)
{
	// Read a disk's boot sector into the supplied BPB structure.

	int status = 0;

	// Initialize the structure
	memset(bpb, 0, sizeof(fatBPB));

	// Read the boot sector
	status = kernelDiskReadSectors((char *) theDisk->name, 0, 1, bpb);
	if (status < 0)
		return (status);

	// Return success
	return (status = 0);
}


static int readFSInfo(fatInternalData *fatData)
{
	// The FAT32 filesystem contains a block called FSInfo, which (generally)
	// occurs right after the boot sector, and which is part of the reserved
	// area of the disk.  This data structure contains a few bits of
	// information which are pretty useful for maintaining large filesystems,
	// specifically the 'free cluster count' and 'first free cluster' values.

	// Read a disk's fsInfo sector into the fatInternalData buffer and ensure
	// that it is (at least trivially) valid.

	int status = 0;

	// Read the FSInfo sector
	status = kernelDiskReadSectors((char *) fatData->disk->name,
		fatData->bpb.fat32.fsInfo, 1, (fatFsInfo *) &fatData->fsInfo);
	if (status < 0)
	{
		// Couldn't read the FSInfo sector.
		kernelDebugError("Unable to read the FAT32 FSInfo structure");
		return (status);
	}

	// It MUST be true that the signature dword 0xAA550000 occurs at offset
	// 0x1FC of the FSInfo sector (regardless of the sector size of this
	// device).  If it does not, then this is not a valid FSInfo sector.  It
	// must also be true that we find two signature dwords in the sector:
	// 0x41615252 at offset 0 and 0x61417272 at offset 0x1E4.
	if ((fatData->fsInfo.leadSig != 0x41615252) ||
		(fatData->fsInfo.structSig != 0x61417272) ||
		(fatData->fsInfo.trailSig != 0xAA550000))
	{
		kernelError(kernel_error, "Not a valid FSInfo sector");
		return (status = ERR_BADDATA);
	}

	fatData->freeClusters = fatData->fsInfo.freeCount;

	// Return success
	return (status = 0);
}


static int writeFSInfo(fatInternalData *fatData)
{
	// Write back a disk's fsInfo sector (read by readFSInfo()) from the
	// fatInternalData's buffer.

	int status = 0;

	kernelDebug(debug_fs, "FAT flushing FS info");

	fatData->fsInfo.freeCount = fatData->freeClusters;

	// Write the updated FSInfo block back to the disk
	status = kernelDiskWriteSectors((char *) fatData->disk->name,
		fatData->bpb.fat32.fsInfo, 1, (fatFsInfo *) &fatData->fsInfo);
	if (status < 0)
	{
		kernelDebugError("Unable to write the FAT32 FSInfo structure");
		return (status);
	}

	// Return success
	return (status = 0);
}


static int readVolumeInfo(fatInternalData *fatData)
{
	// This function reads information about the filesystem from the boot
	// sector info block.  It assumes that the requirement to do so has been
	// met (i.e. it does not check whether the update is necessary, and does
	// so unquestioningly).

	int status = 0;
	int assumeFat32 = 0;

	// Call the function that reads the boot sector
	status = readBootSector(fatData->disk, (fatBPB *) &fatData->bpb);
	if (status < 0)
		return (status);

	// Check some things about the filesystem

	// The bytes-per-sector field may only contain one of the following
	// values: 512, 1024, 2048 or 4096.  Anything else is illegal, according
	// to MS.  512 is almost always the value found here.
	if ((fatData->bpb.bytesPerSect != 512) &&
		(fatData->bpb.bytesPerSect != 1024) &&
		(fatData->bpb.bytesPerSect != 2048) &&
		(fatData->bpb.bytesPerSect != 4096))
	{
		// Not a legal value for FAT
		kernelError(kernel_error, "Illegal bytes-per-sector value");
		return (status = ERR_BADDATA);
	}

	// Also, the bytes-per-sector field should match the value for the
	// disk we're using.
	if (fatData->bpb.bytesPerSect != fatData->disk->physical->sectorSize)
	{
		// Not a legal value for FAT
		kernelError(kernel_error, "Bytes-per-sector does not match disk");
		return (status = ERR_BADDATA);
	}

	// The combined (bytes-per-sector * sectors-per-cluster) value should not
	// exceed 32K, according to MS.  Apparently, some think 64K is OK, but MS
	// says it's not.  32K it is.
	if (fatClusterBytes(fatData) > 32768)
	{
		// Not a legal value in our FAT driver
		kernelError(kernel_error, "Illegal sectors-per-cluster value");
		return (status = ERR_BADDATA);
	}

	// The number of reserved sectors must be one or more
	if (fatData->bpb.rsvdSectCount < 1)
	{
		// Not a legal value for FAT
		kernelError(kernel_error, "Illegal reserved sectors");
		return (status = ERR_BADDATA);
	}

	// The number of FAT tables must be one or more
	if (fatData->bpb.numFats < 1)
	{
		// Not a legal value in our FAT driver
		kernelError(kernel_error, "Illegal number of FATs");
		return (status = ERR_BADDATA);
	}

	// The number of root directory entries should either be 0 (must be for
	// FAT32) or, for FAT12 and FAT16, should result in an even multiple of
	// the bytes-per-sector value when multiplied by 32; however, this is not
	// a requirement.

	// The 16-bit total number of sectors value is linked to the 32-bit value
	// we will find further down in the boot sector (there are requirements
	// here) but we will save our evaluation of this field until we have
	// gathered the 32-bit value.

	// There is a list of legal values for the media type field: 0xF0, and
	// 0xF8-0xFF.  We don't actually use this value for anything, but we will
	// ensure that the value is legal.
	if ((fatData->bpb.media < 0xF8) && (fatData->bpb.media != 0xF0))
	{
		// Oops, not a legal value for FAT
		kernelError(kernel_error, "Illegal media type byte");
		return (status = ERR_BADDATA);
	}

	// OK, there's a little bit of a paradox here.  If this happens to be a
	// FAT32 volume, then the sectors-per-fat value must be zero in this
	// field.  To determine for certain whether this IS a FAT32 volume, we
	// need to know how many data clusters there are (it's used in the
	// "official" MS calculation that determines FAT type).  The problem is
	// that the data-cluster-count calculation depends on the sectors-per-fat
	// value.  Oopsie, Microsoft.  Unfortunately, this means that if the
	// sectors-per-fat happens to be zero, we must momentarily ASSUME that
	// this is indeed a FAT32, and read the 32-bit sectors-per-fat in advance
	// from another part of the bootsector.  There's a data corruption waiting
	// to happen here.  Consider the case where this is a non-FAT32 volume,
	// but this field has been zeroed by mistake; so, we read the 32-bit
	// sectors-per-fat value from the FAT32-specific portion of the boot
	// sector.  What if this field contains some random, non-zero value?  Then
	// the data clusters calculation will probably be wrong, and we could end
	// up determining the wrong FAT type.  That would lead to data corruption
	// for sure.

	fatData->fatSects = fatData->bpb.fatSize16;
	if (!fatData->fatSects)
	{
		// The FAT32 number of sectors per FAT.  Doubleword value.
		fatData->fatSects = fatData->bpb.fat32.fatSize32;
		assumeFat32 = 1;
	}

	// The sectors-per-fat value must now be non-zero
	if (!fatData->fatSects)
	{
		// Oops, not a legal value for FAT32
		kernelError(kernel_error, "Illegal FAT32 sectors per fat");
		return (status = ERR_BADDATA);
	}

	// The 16-bit number of sectors per track/cylinder field is not always
	// relevant.  We won't check it here.

	// Like the sectors-per-track field, above, the number of heads field is
	// not always relevant.  We won't check it here.

	// Hmm, I'm not too sure what to make of the hidden-sectors field.
	// The way I read the documentation is that it describes the number of
	// sectors in any physical partitions that preceed this one.  However,
	// we already get this value from the master boot record where needed.
	// We will ignore the value of this field.

	fatData->totalSects = fatData->bpb.totalSects16;
	if (!fatData->totalSects)
		// Get the 32-bit value instead.  Doubleword value.
		fatData->totalSects = fatData->bpb.totalSects32;

	// Ensure that we have a non-zero total-sectors value
	if (!fatData->totalSects)
	{
		// Oops, this combination is not legal for FAT
		kernelError(kernel_error, "Illegal total sectors");
		return (status = ERR_BADDATA);
	}

	// This ends the portion of the boot sector (bytes 0 through 35) that is
	// consistent between all 3 variations of FAT filesystem we're supporting.
	// Now we must determine which type of filesystem we've got here.  We
	// must do a few calculations to finish gathering the information we
	// need to make the determination.

	if (fatData->bpb.rootEntCount)
	{
		// Figure out the actual number of directory sectors.  We have already
		// ensured that the bytes-per-sector value is non-zero (don't worry
		// about a divide-by-zero error here)
		fatData->rootDirSects = (((fatData->bpb.rootEntCount *
			FAT_BYTES_PER_DIR_ENTRY) + (fatData->bpb.bytesPerSect - 1)) /
			fatData->bpb.bytesPerSect);
	}
	else
	{
		// This is a sign of FAT32
		assumeFat32 = 1;
	}

	// We have already ensured that the sectors-per-cluster value is
	// non-zero (don't worry about a divide-by-zero error here)
	fatData->dataSects = (fatData->totalSects - (fatData->bpb.rsvdSectCount +
		(fatData->bpb.numFats * fatData->fatSects) + fatData->rootDirSects));

	fatData->dataClusters = (fatData->dataSects / fatData->bpb.sectsPerClust);

	// OK.  Now we now have enough data to determine the type of this FAT
	// filesystem.  According to the Microsoft white paper, the following is
	// the only TRUE determination of specific FAT filesystem type.  We will
	// examine the "data clusters" value.  There are specific cluster limits
	// which constrain these filesystems.

	if (!assumeFat32 && (fatData->dataClusters < 4085))
	{
		// FAT12 filesystem.
		fatData->fsType = fat12;
		fatData->terminalClust = 0x0FF8; // (or above)
	}
	else if (!assumeFat32 && (fatData->dataClusters < 65525))
	{
		// FAT16 filesystem.
		fatData->fsType = fat16;
		fatData->terminalClust = 0xFFF8; // (or above)
	}
	else
	{
		// Any larger value of data clusters denotes a FAT32 filesystem
		fatData->fsType = fat32;
		fatData->terminalClust = 0x0FFFFFF8; // (or above)
	}

	// Now we know the type of the FAT filesystem.  From here, the data
	// gathering we do must diverge.

	if (fatData->fsType == fat32)
	{
		// FAT32.  There is some additional information we need to gather from
		// the disk that is specific to this type of filesystem.

		// The next value is the "extended flags" field for FAT32.  This
		// value indicates the number of "active" FATS.  Active FATS are
		// significant when FAT mirroring is active.  While our filesystem
		// driver does not do runtime mirroring per se, it does update all
		// FATs whenever the master FAT is changed on disk.  Thus, at least on
		// disk, all FATs are generally synchronized.  We will ignore the
		// field for now.

		// We have already read the FAT32 sectors-per-fat value.  This is
		// necessitated by a "logic bug" in the filesystem specification.
		// See the rather lengthy explanation nearer the beginning of this
		// function.

		// Now we have the FAT32 version field.  The FAT32 version we are
		// supporting here is 0.0.  We will not mount a FAT volume that has
		// a higher version number than that (but we don't need to save
		// the version number anywhere).
		if (fatData->bpb.fat32.fsVersion != (short) 0)
		{
			kernelError(kernel_error, "Unsupported FAT32 version");
			return (status = ERR_BADDATA);
		}

		// The starting cluster number of the root directory must be >= 2,
		// and <= (data-clusters + 1)
		if ((fatData->bpb.fat32.rootClust < 2) ||
			(fatData->bpb.fat32.rootClust > (fatData->dataClusters + 1)))
		{
			kernelError(kernel_error, "Illegal FAT32 root dir cluster %u",
			fatData->bpb.fat32.rootClust);
			return (status = ERR_BADDATA);
		}

		// The sector number (in the reserved area) of the FSInfo structure
		// number must be greater than 1, and less than the number of reserved
		// sectors in the volume
		if ((fatData->bpb.fat32.fsInfo < 1) ||
			(fatData->bpb.fat32.fsInfo >= fatData->bpb.rsvdSectCount))
		{
			kernelError(kernel_error, "Illegal FAT32 FSInfo sector");
			return (status = ERR_BADDATA);
		}

		// Now we will read some additional information, not included in the
		// boot sector.  This FSInfo sector contains more information that
		// will be useful in managing large FAT32 volumes.  We previously
		// gathered the sector number of this structure from the boot sector.

		// Call the function that will read the FSInfo block
		status = readFSInfo(fatData);
		if (status < 0)
			return (status);

		// This free cluster count value can be zero, but it cannot be greater
		// than data-clusters -- with one exeption.  It can be 0xFFFFFFFF
		// (meaning the free cluster count is unknown).
		if ((fatData->fsInfo.freeCount > fatData->dataClusters) &&
			(fatData->fsInfo.freeCount != 0xFFFFFFFF))
		{
			kernelError(kernel_error, "Illegal FAT32 free cluster count (%x)",
			fatData->fsInfo.freeCount);
			return (status = ERR_BADDATA);
		}

		// This first free cluster value must be >= 2, but it cannot be
		// greater than data-clusters, unless it is 0xFFFFFFFF (not known)
		if ((fatData->fsInfo.nextFree < 2) ||
			((fatData->fsInfo.nextFree > fatData->dataClusters) &&
			 	(fatData->fsInfo.nextFree != 0xFFFFFFFF)))
		{
			kernelError(kernel_error, "Illegal FAT32 first free cluster");
			return (status = ERR_BADDATA);
		}
	}

	// Return success
	return (status = 0);
}


static int writeVolumeInfo(fatInternalData *fatData)
{
	// This function writes back information about the filesystem read by
	// readVolumeInfo() to the boot sector info block.

	int status = 0;

	kernelDebug(debug_fs, "FAT flushing volume info");

	// Set a couple of BPB values that are based on up-to-date ones in the
	// main fatInternalData structure.
	fatData->bpb.totalSects16 = fatData->totalSects;
	fatData->bpb.totalSects32 = fatData->totalSects;
	if (fatData->fsType == fat32)
	{
		fatData->bpb.totalSects16 = 0;
	}
	else
	{
		if (fatData->totalSects <= 0xFFFF)
			fatData->bpb.totalSects32 = 0;
		else
			fatData->bpb.totalSects16 = 0;
	}

	fatData->bpb.fatSize16 = fatData->fatSects;
	if (fatData->fsType == fat32)
	{
		fatData->bpb.fatSize16 = 0;
		fatData->bpb.fat32.fatSize32 = fatData->fatSects;
	}

	status = kernelDiskWriteSectors((char *) fatData->disk->name, 0, 1,
		(fatBPB *) &fatData->bpb);

	// If FAT32 and backupBootF32 is non-zero, make a backup copy of the
	// boot sector
	if ((fatData->fsType == fat32) && fatData->bpb.fat32.backupBootSect)
		kernelDiskWriteSectors((char *) fatData->disk->name,
			fatData->bpb.fat32.backupBootSect, 1, (fatBPB *) &fatData->bpb);

	return (status);
}


static void setVolumeLabel(kernelDisk *theDisk, char *label)
{
	int count;

	for (count = 0; ((count < FAT_8_3_NAME_LEN) &&
		(label[count] && (label[count] != ' '))); count ++)
	{
		theDisk->filesystem.label[count] = label[count];
	}
}


static fatInternalData *getFatData(kernelDisk *theDisk)
{
	// Reads the filesystem parameters from the control structures on disk.

	int status = 0;
	fatInternalData *fatData = theDisk->filesystem.filesystemData;
	unsigned freeBitmapSize = 0;

	// Have we already read the parameters for this filesystem?
	if (fatData)
		return (fatData);

	// We must allocate some new memory to hold information about the
	// filesystem
	fatData = kernelMalloc(sizeof(fatInternalData));
	if (!fatData)
	{
		kernelError(kernel_error, "Unable to allocate FAT data memory");
		return (fatData = NULL);
	}

	// Attach the disk structure to the fatData structure
	fatData->disk = theDisk;

	// Get the disk's boot sector info
	status = readVolumeInfo(fatData);
	if (status < 0)
	{
		kernelDebugError("Unable to get FAT volume info");
		// Attempt to free the fatData memory.
		kernelFree((void *) fatData);
		return (fatData = NULL);
	}

	// Set the proper filesystem type name on the disk structure
	switch (fatData->fsType)
	{
		case fat12:
			strcpy((char *) theDisk->fsType, FSNAME_FAT"12");
			setVolumeLabel(theDisk, (char *) fatData->bpb.fat.volumeLabel);
			break;

		case fat16:
			strcpy((char *) theDisk->fsType, FSNAME_FAT"16");
			setVolumeLabel(theDisk, (char *) fatData->bpb.fat.volumeLabel);
			break;

		case fat32:
			strcpy((char *) theDisk->fsType, FSNAME_FAT"32");
			setVolumeLabel(theDisk, (char *) fatData->bpb.fat32.volumeLabel);
			break;

		default:
			strcpy((char *) theDisk->fsType, FSNAME_FAT);
	}

	freeBitmapSize = (((fatData->dataClusters + 2) + 7) / 8);

	// Get memory for the free list
	fatData->freeClusterBitmap = kernelMalloc(freeBitmapSize);
	if (!fatData->freeClusterBitmap)
	{
		// Oops.  Something went wrong.
		kernelError(kernel_error, "Unable to allocate FAT data memory");
		// Attempt to free the memory
		kernelFree(fatData->freeClusterBitmap);
		return (fatData = NULL);
	}

	// Set them all used for the moment
	memset(fatData->freeClusterBitmap, 0xFF, freeBitmapSize);

	// Everything went right.  Looks like we will have a legitimate new
	// bouncing baby FAT filesystem.

	// Attach our new FS data to the filesystem structure
	theDisk->filesystem.filesystemData = (void *) fatData;

	// Specify the filesystem block size
	theDisk->filesystem.blockSize = fatClusterBytes(fatData);

	// 'minSectors' and 'maxSectors' are the same as the current sectors,
	// since we don't yet support resizing.
	theDisk->filesystem.minSectors = theDisk->numSectors;
	theDisk->filesystem.maxSectors = theDisk->numSectors;

	return (fatData);
}


static void freeFatData(kernelDisk *theDisk)
{
	// Deallocate the FAT data structure from a disk.

	fatInternalData *fatData = theDisk->filesystem.filesystemData;

	if (fatData)
	{
		if (fatData->freeClusterBitmap)
			kernelFree(fatData->freeClusterBitmap);

		memset((void *) fatData, 0, sizeof(fatInternalData));
		kernelFree((void *) fatData);
	}

	theDisk->filesystem.filesystemData = NULL;
}


static void progressConfirmError(progress *prog, const char *message)
{
	if (!prog)
		return;

	if (kernelLockGet(&prog->progLock) >= 0)
	{
		strcpy((char *) prog->statusMessage, message);
		prog->error = 1;
		kernelLockRelease(&prog->progLock);
	}

	while (prog->error)
		kernelMultitaskerYield();
}


static unsigned calcFatSects(fatInternalData *fatData, uquad_t blocks)
{
	// Figure out the required number of FAT sectors based on a "clever bit
	// of math" provided by Microsoft.

	unsigned tmp1 = 0, tmp2 = 0;

	tmp1 = (blocks - (fatData->bpb.rsvdSectCount + fatData->rootDirSects));
	tmp2 = ((256 * fatData->bpb.sectsPerClust) + fatData->bpb.numFats);

	if (fatData->fsType == fat32)
		tmp2 /= 2;

	return ((tmp1 + (tmp2 - 1)) / tmp2);
}


static unsigned char *readFatSectors(fatInternalData *fatData, unsigned sector,
	unsigned numSectors)
{
	// Allocates memory for the requested FAT sector(s) and reads them.

	int status = 0;
	unsigned char *fatSect = NULL;

	fatSect = kernelMalloc(fatData->disk->physical->sectorSize * numSectors);
	if (!fatSect)
		return (fatSect);

	status = kernelDiskReadSectors((char *) fatData->disk->name,
		(fatData->bpb.rsvdSectCount + sector), numSectors, fatSect);
	if (status < 0)
	{
		kernelFree(fatSect);
		return (fatSect = NULL);
	}

	return (fatSect);
}


static int writeFatSectors(fatInternalData *fatData, unsigned sector,
	unsigned numSectors, unsigned char *fatSect)
{
	// Writes the supplied FAT sector(s), both to the main FAT and the backup
	// FAT(s), if any.

	int status = 0;
	unsigned count;

	kernelDebug(debug_fs, "FAT writing %d FAT sectors at %d", numSectors,
		sector);

	if ((sector + (numSectors - 1)) >= fatData->fatSects)
	{
		kernelError(kernel_error, "FAT sector(s) are outside the permissable "
			"range");
		return (status = ERR_RANGE);
	}

	for (count = 0; count < fatData->bpb.numFats; count ++)
	{
		status = kernelDiskWriteSectors((char *) fatData->disk->name,
			(fatData->bpb.rsvdSectCount + (count * fatData->fatSects) +
			sector), numSectors, fatSect);
		if (status < 0)
			break;
	}

	return (status);
}


static int getFatEntries(fatInternalData *fatData, unsigned firstEntry,
	unsigned numEntries, unsigned *entries)
{
	// Given a range of FAT entries to read, return them in the supplied
	// array

	int status = 0;
	unsigned lastEntry = (firstEntry + (numEntries - 1));
	unsigned firstOffset = 0;
	unsigned lastOffset = 0;
	unsigned fatStartSector = 0;
	unsigned numFatSectors = 0;
	unsigned char *fatSects = NULL;
	unsigned entryOffset = 0;
	unsigned count;

	//kernelDebug(debug_fs, "FAT read FAT entries %u->%u", firstEntry,
	//	((firstEntry + numEntries) - 1));

	// Check to make sure there is such a range
	if ((firstEntry >= (fatData->dataClusters + 2)) ||
		(lastEntry >= (fatData->dataClusters + 2)))
	{
		kernelError(kernel_error, "Requested FAT range (%u->%u) is beyond the "
			"limits of the table (%u)", firstEntry, lastEntry,
			(fatData->dataClusters + 2));
		return (status = ERR_BUG);
	}

	// Determine the entries' byte offsets in the FAT, and the number of FAT
	// sectors to read.
	switch (fatData->fsType)
	{
		case fat12:
			// FAT 12 entries are 3 nybbles each.  Thus, we need to take the
			// entry numbers we were given and multiply them by 3/2 to get the
			// byte offsets.
			firstOffset = (firstEntry + (firstEntry >> 1));
			lastOffset = (lastEntry + (lastEntry >> 1));
			break;

		case fat16:
			// FAT 16 entries are 2 bytes each.  Thus, we need to take the
			// entry numbers we were given and multiply them by 2 to get the
			// byte offsets.
			firstOffset = (firstEntry * 2);
			lastOffset = (lastEntry * 2);
			break;

		case fat32:
			// FAT 32 entries are 4 bytes each.  Thus, we need to take the
			// entry numbers we were given and multiply them by 4 to get the
			// byte offsets.
			firstOffset = (firstEntry * 4);
			lastOffset = (lastEntry * 4);
			break;

		default:
			kernelError(kernel_error, "Unknown FAT type");
			return (status = ERR_INVALID);
	}

	fatStartSector = (firstOffset / fatData->disk->physical->sectorSize);
	numFatSectors = (((lastOffset / fatData->disk->physical->sectorSize) -
		fatStartSector) + 1);

	// If it's FAT12, the last entry might overlap into the next sector
	if ((fatData->fsType == fat12) &&
		((lastOffset % fatData->disk->physical->sectorSize) >
			(fatData->disk->physical->sectorSize - 2)))
	{
		numFatSectors += 1;
	}

	// Read the FAT sector(s)
	fatSects = readFatSectors(fatData, fatStartSector, numFatSectors);
	if (!fatSects)
		return (status = ERR_MEMORY);

	entryOffset = (firstOffset % fatData->disk->physical->sectorSize);

	for (count = 0; count < numEntries; count ++)
	{
		switch (fatData->fsType)
		{
			case fat12:
				// FAT 12 entries are 3 nybbles each.  entryOffset is the index
				// of the WORD value that contains the value we're looking for.
				entries[count] = *((unsigned short *)(fatSects + entryOffset));

				// We need to get rid of the extra nybble of information
				// contained in the word value.  If the extra nybble is in the
				// most-significant spot, we need to mask it out.  If it's in
				// the least-significant spot, we need to shift the word right
				// by 4 bits.
				if ((firstEntry + count) % 2)
				{
					entries[count] >>= 4;
					entryOffset += 2;
				}
				else
				{
					entries[count] &= 0x0FFF;
					entryOffset += 1;
				}
				break;

			case fat16:
				// FAT 16 entries are 2 bytes each.
				entries[count] = *((unsigned short *)(fatSects + entryOffset));
				entryOffset += 2;
				break;

			case fat32:
				// FAT 32 entries are 4 bytes each.  Really only the bottom 28
				// bits of this value are relevant.
				entries[count] =
					(*((unsigned *)(fatSects + entryOffset)) & 0x0FFFFFFF);
				entryOffset += 4;
				break;

			default:
				// This will have been handled in the switch above
				break;
		}
	}

	kernelFree(fatSects);
	return (status = 0);
}


static int setFatEntry(fatInternalData *fatData, unsigned entryNumber,
	unsigned value)
{
	// This function is internal, and takes as its parameters the number of
	// the FAT entry to be written and the value to set.

	int status = 0;
	unsigned entryOffset = 0;
	unsigned numFatSectors = 1;
	unsigned fatStartSector = 0;
	unsigned char *fatSects = NULL;
	unsigned oldValue = 0;
	unsigned entryValue = 0;

	// Check the entry number
	if (entryNumber >= (fatData->dataClusters + 2))
	{
		kernelError(kernel_error, "Requested FAT entry (%u) is beyond the "
			"limits of the table (%u)", entryNumber,
			(fatData->dataClusters + 2));
		return (status = ERR_BUG);
	}

	// Determine the entry number's byte offset in the FAT, and the number of
	// sectors to read if applicable.
	switch (fatData->fsType)
	{
		case fat12:
			// FAT 12 entries are 3 nybbles each.  Thus, we need to take the
			// entry number we were given and multiply it by 3/2 to get the
			// starting byte.
			entryOffset = (entryNumber + (entryNumber >> 1));
			if ((entryOffset % fatData->disk->physical->sectorSize) >
				(fatData->disk->physical->sectorSize - 2))
			{
				numFatSectors = 2;
			}
			break;

		case fat16:
			// FAT 16 entries are 2 bytes each.  Thus, we need to take the
			// entry number we were given and multiply it by 2 to get the
			// starting byte.
			entryOffset = (entryNumber * 2);
			break;

		case fat32:
			// FAT 32 entries are 4 bytes each.  Thus, we need to take the
			// entry number we were given and multiply it by 4 to get the
			// starting byte.
			entryOffset = (entryNumber * 4);
			break;

		default:
			kernelError(kernel_error, "Unknown FAT type");
			return (status = ERR_INVALID);
	}

	fatStartSector = (entryOffset / fatData->disk->physical->sectorSize);

	// Read the FAT sector(s)
	fatSects = readFatSectors(fatData, fatStartSector, numFatSectors);
	if (!fatSects)
		return (status = ERR_MEMORY);

	entryOffset %= fatData->disk->physical->sectorSize;

	switch (fatData->fsType)
	{
		case fat12:
			// entryOffset is the index of the WORD value that contains the 3
			// nybbles we want to set.  Read the current word value
			entryValue = *((short *)(fatSects + entryOffset));
			if (entryNumber % 2)
			{
				entryValue &= 0x000F;
				entryValue |= ((value & 0x0FFF) << 4);
			}
			else
			{
				entryValue &= 0xF000;
				entryValue |= (value & 0x0FFF);
			}
			*((short *)(fatSects + entryOffset)) = entryValue;
			break;

		case fat16:
			// FAT 16 entries are 2 bytes each.
			*((short *)(fatSects + entryOffset)) = value;
			break;

		case fat32:
			// FAT 32 entries are 4 bytes each.
			oldValue = *((unsigned *)(fatSects + entryOffset));
			// Make sure we preserve the top 4 bits of the previous entry
			entryValue = (value | (oldValue & 0xF0000000));
			*((unsigned *)(fatSects + entryOffset)) = entryValue;
			break;

		default:
			// This will have been handled in the switch above
			break;
	}

	// Write back the FAT sector(s)
	status = writeFatSectors(fatData, fatStartSector, numFatSectors, fatSects);
	kernelFree(fatSects);
	return (status);
}


static void makeFreeBitmapThread(void)
{
	// This function examines the FAT and fills out the bitmap of free
	// clusters.  The advantage of doing this is that knowledge of the entire
	// FAT is very handy for preventing fragmentation, and is otherwise good
	// for easy management.  In order to keep the memory footprint as small as
	// possible, and to (hopefully) maximize speed, we keep the data as one
	// large bitmap.  The function must be spawned as a new thread for itself
	// to run in.  This way, building a free list (especially for a large FAT
	// volume) can proceed without hanging up the system.  Note that when it
	// is finished, it doesn't "return": it kills its own process.

	int status = 0;
	unsigned entriesPerLoop = (256 * 1024);
	unsigned *entries = NULL;
	unsigned entryNum = 0;
	unsigned count;

	kernelDebug(debug_fs, "FAT making free cluster bitmap");

	// Lock the free list so nobody tries to use it or change it while it's in
	// an inconsistent state
	status = kernelLockGet(&makingFatFree->freeBitmapLock);
	if (status < 0)
	{
		kernelDebugError("Couldn't lock the free list");
		makingFatFree = NULL;
		kernelMultitaskerTerminate(status);
	}

	// Get some memory for entries
	entries = kernelMalloc(entriesPerLoop * sizeof(unsigned));
	if (!entries)
	{
		kernelDebugError("No memory for FAT entries");
		status = ERR_MEMORY;
		goto out;
	}

	makingFatFree->freeClusters = 0;

	for (entryNum = 2; entryNum < (makingFatFree->dataClusters + 2); )
	{
		if ((entryNum + entriesPerLoop) > (makingFatFree->dataClusters + 2))
			entriesPerLoop = ((makingFatFree->dataClusters + 2) - entryNum);

		// Read a batch of FAT table entries.
		status = getFatEntries(makingFatFree, entryNum, entriesPerLoop,
			entries);
		if (status < 0)
		{
			kernelDebugError("Couldn't read FAT entry");
			goto out;
		}

		for (count = 0; count < entriesPerLoop; count ++)
		{
			if (!entries[count])
			{
				// The entry is free.  Clear the bit in the bitmap.
				makingFatFree->freeClusterBitmap[entryNum / 8] &=
					~(1 << (entryNum % 8));

				// Update the free clusters value.
				makingFatFree->freeClusters += 1;
			}

			entryNum += 1;
		}
	}

	kernelDebug(debug_fs, "FAT finished making free cluster bitmap");
	status = 0;

out:
	// Unlock the free list
	kernelLockRelease(&makingFatFree->freeBitmapLock);

	// We are finished
	makingFatFree = NULL;
	makeFatFreePid = -1;

	kernelMultitaskerTerminate(status);
}


static int makeFreeBitmap(fatInternalData *fatData)
{
	// Start to build the free cluster list.

	int status = 0;

	// Don't do more than one of these at a time
	if (makingFatFree)
		kernelMultitaskerBlock(makeFatFreePid);

	makingFatFree = fatData;

	status = kernelMultitaskerSpawn(makeFreeBitmapThread, "make free bitmap",
		0, NULL);
	if (status < 0)
	{
		makingFatFree = NULL;
		return (status);
	}

	makeFatFreePid = status;

	// Give the free-bitmap thread a little head start on us.
	kernelMultitaskerYield();

	return (status = 0);
}


static int getNumClusters(fatInternalData *fatData, unsigned startCluster,
	unsigned *clusters)
{
	// This function is internal, and takes as a parameter the starting
	// cluster of a file/directory.  The function will traverse the FAT table
	// entries belonging to the file/directory, and return the number of
	// clusters used by that item.

	int status = 0;
	unsigned currentCluster = 0;
	unsigned newCluster = 0;

	// Zero clusters by default
	*clusters = 0;

	if (!startCluster)
		// This file has no allocated clusters.  Return size zero.
		return (status = 0);

	// Save the starting value
	currentCluster = startCluster;

	// Now we go through a loop to gather the cluster numbers, adding 1 to the
	// total each time.  A value of terminalClust or more means that there
	// are no more sectors
	while (1)
	{
		if ((currentCluster < 2) || (currentCluster >= fatData->terminalClust))
		{
			kernelError(kernel_error, "Invalid cluster number %u (start "
				"cluster %u)", currentCluster, startCluster);
			return (status = ERR_BADDATA);
		}

		status = getFatEntries(fatData, currentCluster, 1, &newCluster);
		if (status < 0)
		{
			kernelDebugError("Error reading FAT table");
			return (status = ERR_BADDATA);
		}

		*clusters += 1;

		currentCluster = newCluster;

		// Finished?
		if (currentCluster >= fatData->terminalClust)
			break;
	}

	return (status = 0);
}


static int releaseClusterChain(fatInternalData *fatData, unsigned startCluster)
{
	// This function returns an unused cluster or sequence of clusters back
	// to the free list, and marks them as unused in the volume's FAT table
	// Returns 0 on success, negative otherwise

	int status = 0;
	unsigned currentCluster = 0;
	unsigned nextCluster = 0;

	if (!startCluster || (startCluster == fatData->terminalClust))
		// Nothing to do
		return (status = 0);

	// Attempt to lock the free cluster list
	status = kernelLockGet(&fatData->freeBitmapLock);
	if (status < 0)
	{
		kernelDebugError("Unable to lock the free cluster bitmap");
		return (status);
	}

	currentCluster = startCluster;

	// Loop through each of the unwanted clusters in the chain.  Change each
	// one in both the free list and the FAT table
	while (1)
	{
		// Get the next thing in the chain
		status = getFatEntries(fatData, currentCluster, 1, &nextCluster);
		if (status)
		{
			kernelDebugError("Unable to follow cluster chain");
			kernelLockRelease(&fatData->freeBitmapLock);
			return (status);
		}

		// Deallocate the current cluster in the chain
		status = setFatEntry(fatData, currentCluster, 0);
		if (status < 0)
		{
			kernelDebugError("Unable to deallocate cluster");
			kernelLockRelease(&fatData->freeBitmapLock);
			return (status);
		}

		// Mark the cluster as free in the free cluster bitmap (mask it off)
		fatData->freeClusterBitmap[currentCluster / 8] &=
			~(1 << (currentCluster % 8));

		// Adjust the free cluster count
		fatData->freeClusters += 1;

		// Any more to do?
		if (nextCluster >= fatData->terminalClust)
			break;

		currentCluster = nextCluster;
	}

	// Unlock the list and return success
	kernelLockRelease(&fatData->freeBitmapLock);
	return (status = 0);
}


static int getUnusedClusters(fatInternalData *fatData, unsigned requested,
	unsigned *startCluster)
{
	// Allocates a chain of free disk clusters to the calling program.  It
	// uses a "first fit" algorithm to make the decision, looking for the
	// first free block that is big enough to fully accommodate the request.
	// This is good because if there IS a block big enough to fit the entire
	// request, there is no fragmentation.  If a contiguous block that is big
	// enough can't be found, allocate (parts of) the largest available chunks
	// until the request can be satisfied.

	int status = 0;
	unsigned quotient = 0, remainder = 0;
	unsigned terminate = 0;
	unsigned biggestSize = 0;
	unsigned biggestLocation = 0;
	unsigned consecutive = 0;
	unsigned lastCluster = 0;
	unsigned count;

	*startCluster = 0;

	// Make sure the request is bigger than zero
	if (!requested)
		// This isn't an "error" per se, we just won't do anything
		return (status = 0);

	if (makingFatFree == fatData)
		kernelMultitaskerBlock(makeFatFreePid);

	// Make sure that there are enough free clusters to satisfy the request
	if (fatData->freeClusters < requested)
	{
		kernelError(kernel_error, "Not enough free space to complete "
			"operation");
		return (status = ERR_NOFREE);
	}

	// Attempt to lock the free-block bitmap
	status = kernelLockGet(&fatData->freeBitmapLock);
	if (status < 0)
	{
		kernelDebugError("Unable to lock the free-cluster bitmap");
		goto out;
	}

	// We will roll through the free cluster bitmap, looking for the first
	// free chunk that is big enough to accommodate the request.  We also keep
	// track of the biggest (but not big enough) block that we have encountered
	// so far.  This enables us to select the next-biggest available block
	// if no single block is large enough to accommodate the whole request.

	terminate = (fatData->dataClusters + 2);

	for (count = 2; count < terminate; count ++)
	{
		// We are searching a bitmap for a sequence of one or more zero bits,
		// which signify that the disk clusters are unused.

		// Calculate the current quotient and remainder for this operation so
		// we only have to do it once
		quotient = (count / 8);
		remainder = (count % 8);

		// There will be a couple of little tricks we can do to speed up this
		// search.  If this iteration of "count" is divisible by 8, then we can
		// let the processor scan the whole byte ahead looking for ANY unused
		// clusters.
		if (!remainder && (count < (terminate - 8)) &&
			(fatData->freeClusterBitmap[quotient] == 0xFF))
		{
			// The next 8 clusters are used
			consecutive = 0;
			count += 7;
			continue;
		}

		if (fatData->freeClusterBitmap[quotient] & (1 << remainder))
		{
			// This cluster is used
			consecutive = 0;
			continue;
		}
		else
		{
			// This cluster is free.
			consecutive += 1;
		}

		// Are we at the biggest consecutive string so far?
		if (consecutive > biggestSize)
		{
			// We set a new big record
			biggestSize = consecutive;
			biggestLocation = (count - (biggestSize - 1));

			// Do we now have enough consecutive clusters to grant the request?
			if (biggestSize >= requested)
				break;
		}
	}

	if (!biggestSize)
	{
		kernelError(kernel_error, "Not enough free space to complete "
			"operation");
		status = ERR_NOFREE;
		goto out;
	}

	if (biggestSize > requested)
		biggestSize = requested;

	terminate = (biggestLocation + biggestSize);

	// Change all of the FAT table entries for the allocated clusters
	for (count = biggestLocation; count < terminate; count ++)
	{
		if (count < (terminate - 1))
		{
			status = setFatEntry(fatData, count, (count + 1));
		}
		else
		{
			// Last cluster
			lastCluster = count;
			status = setFatEntry(fatData, count, fatData->terminalClust);
		}

		if (status < 0)
		{
			// Attempt to get rid of all the ones we changed
			releaseClusterChain(fatData, biggestLocation);
			goto out;
		}

		// Mark the cluster as used in the free bitmap.
		fatData->freeClusterBitmap[count / 8] |= (1 << (count % 8));
	}

	// Adjust the free cluster count by whatever number we found
	fatData->freeClusters -= biggestSize;
	kernelDebug(debug_fs, "FAT free clusters now %d", fatData->freeClusters);

	// If we didn't find enough clusters in the main loop, that means there's
	// no single block of clusters large enough.  We'll do a little recursion
	// to fill out the request.
	if (biggestSize < requested)
	{
		status = getUnusedClusters(fatData, (requested - biggestSize), &count);
		if (status < 0)
		{
			kernelDebugError("Cluster allocation error");
			// Deallocate all of the clusters we reserved previously
			releaseClusterChain(fatData, biggestLocation);
			goto out;
		}

		// We need to attach this new allocation on to the end of the one that
		// we did in this function call.
		status = setFatEntry(fatData, lastCluster, count);
		if (status < 0)
		{
			kernelDebugError("FAT table could not be modified");
			// Attempt to get rid of all the ones we changed, plus the one
			// created with our function call.
			releaseClusterChain(fatData, biggestLocation);
			releaseClusterChain(fatData, count);
			goto out;
		}
	}

	// Success.  Set the caller's variable
	*startCluster = biggestLocation;
	status = 0;

out:
	// Unlock the list and return success
	kernelLockRelease(&fatData->freeBitmapLock);
	return (status);
}


static int getLastCluster(fatInternalData *fatData, unsigned startCluster,
	unsigned *lastCluster)
{
	// This function is internal, and takes as a parameter the starting
	// cluster of a file/directory.  The function will traverse the FAT table
	// entries belonging to the file/directory, and return the number of
	// the last cluster used by that item.

	int status = 0;
	unsigned currentCluster = 0;
	unsigned newCluster = 0;

	if (!startCluster)
	{
		// This file has no allocated clusters.  Return zero (which is not a
		// legal cluster number), however this is not an error
		*lastCluster = 0;
		return (status = 0);
	}

	// Save the starting value
	currentCluster = startCluster;

	// Now we go through a loop to step through the cluster numbers.  A
	// value of terminalClust or more means that there are no more clusters.
	while (1)
	{
		if ((currentCluster < 2) || (currentCluster >= fatData->terminalClust))
		{
			kernelError(kernel_error, "Invalid cluster number %u",
				currentCluster);
			return (status = ERR_BADDATA);
		}

		status = getFatEntries(fatData, currentCluster, 1, &newCluster);
		if (status < 0)
		{
			kernelDebugError("Error reading FAT table");
			return (status = ERR_BADDATA);
		}

		if (newCluster < fatData->terminalClust)
			currentCluster = newCluster;
		else
			break;
	}

	*lastCluster = currentCluster;
	return (status = 0);
}


static int getNthCluster(fatInternalData *fatData, unsigned startCluster,
	unsigned *nthCluster)
{
	// This function is internal, and takes as a parameter the starting
	// cluster of a file/directory.  The function will traverse the FAT table
	// entries belonging to the file/directory, and return the number of
	// the requested cluster used by that item.  Zero-based.

	int status = 0;
	unsigned currentCluster = 0;
	unsigned newCluster = 0;
	unsigned clusterCount = 0;

	if (!startCluster)
	{
		// This is an error, because the file has no clusters, so even a
		// nthCluster request of zero is wrong.
		*nthCluster = 0;
		return (status = ERR_INVALID);
	}

	// Save the starting value
	currentCluster = startCluster;

	// Now we go through a loop to step through the cluster numbers.  A
	// value of terminalClust or more means that there are no more clusters.
	while (1)
	{
		if (clusterCount == *nthCluster)
		{
			// currentCluster is the one requested
			*nthCluster = currentCluster;
			return (status = 0);
		}

		if ((currentCluster < 2) || (currentCluster >= fatData->terminalClust))
		{
			kernelError(kernel_error, "Invalid cluster number %u",
				currentCluster);
			return (status = ERR_BADDATA);
		}

		status = getFatEntries(fatData, currentCluster, 1, &newCluster);
		if (status < 0)
		{
			kernelDebugError("Error reading FAT table");
			return (status = ERR_BADDATA);
		}

		clusterCount++;

		if (newCluster < fatData->terminalClust)
		{
			currentCluster = newCluster;
		}
		else
		{
			// The Nth cluster requested does not exist.
			*nthCluster = 0;
			return (status = ERR_INVALID);
		}
	}
}


static int lengthenFile(fatInternalData *fatData, kernelFileEntry *entry,
	unsigned newClusters)
{
	// Expand a file entry to the requested number of clusters

	int status = 0;
	fatEntryData *entryData = NULL;
	unsigned needClusters = 0;
	unsigned gotClusters = 0;
	unsigned lastCluster = 0;

	// Check params
	if (!entry)
		return (status = ERR_NULLPARAMETER);

	kernelDebug(debug_fs, "FAT lengthening file \"%s\": entry->blocks=%d "
		"newClusters=%d", entry->name, entry->blocks, newClusters);

	if (entry->blocks >= newClusters)
		// Nothing to do
		return (status = 0);

	// Get the private FAT data structure attached to this file entry
	entryData = (fatEntryData *) entry->driverData;
	if (!entryData)
		return (status = ERR_NODATA);

	needClusters = (newClusters - entry->blocks);

	kernelDebug(debug_fs, "FAT getting %u new clusters for \"%s\"",
		needClusters, entry->name);

	// We will need to allocate some more clusters
	status = getUnusedClusters(fatData, needClusters, &gotClusters);
	if (status < 0)
		return (status);

	kernelDebug(debug_fs, "FAT got %u new clusters for \"%s\" at %u",
		needClusters, entry->name, gotClusters);

	// Get the number of the current last cluster
	status = getLastCluster(fatData, entryData->startCluster, &lastCluster);
	if (status < 0)
	{
		kernelDebugError("Unable to determine file's last cluster");
		releaseClusterChain(fatData, gotClusters);
		return (status);
	}

	// If the last cluster is zero, then the file currently has no clusters
	// and we should set entryData->startCluster to the value returned from
	// getUnusedClusters.  Otherwise, the value from getUnusedClusters should
	// be assigned to lastCluster

	if (lastCluster)
	{
		// Attach these new clusters to the file's chain
		status = setFatEntry(fatData, lastCluster, gotClusters);
		if (status < 0)
		{
			kernelDebugError("Error connecting new clusters");
			releaseClusterChain(fatData, gotClusters);
			return (status);
		}
	}
	else
	{
		entryData->startCluster = gotClusters;
	}

	// Adjust the size of the file
	status = getNumClusters(fatData, entryData->startCluster,
		(unsigned *) &entry->blocks);
	if (status < 0)
	{
		kernelDebugError("Error Getting new file length");
		// Don't release the clusters, as we've already attached them to the
		// file entry.
		return (status);
	}

	entry->size = (entry->blocks * fatClusterBytes(fatData));

	return (status = 0);
}


static int shortenFile(fatInternalData *fatData, kernelFileEntry *entry,
	unsigned newBlocks)
{
	// Truncate a file entry to the requested number of blocks

	int status = 0;
	fatEntryData *entryData = NULL;
	unsigned newLastCluster = (newBlocks - 1);
	unsigned firstReleasedCluster = 0;

	// Check params
	if (!entry)
		return (status = ERR_NULLPARAMETER);

	if (entry->blocks <= newBlocks)
		// Nothing to do
		return (status = 0);

	// Get the private FAT data structure attached to this file entry
	entryData = (fatEntryData *) entry->driverData;
	if (!entryData)
		return (status = ERR_NODATA);

	// Get the entry that will be the new last cluster
	status = getNthCluster(fatData, entryData->startCluster, &newLastCluster);
	if (status < 0)
		return (status);

	// Save the value this entry points to.  That's where we start deleting
	// stuff in a second.
	status = getFatEntries(fatData, newLastCluster, 1, &firstReleasedCluster);
	if (status < 0)
		return (status);

	// Mark the last cluster as last
	status = setFatEntry(fatData, newLastCluster, fatData->terminalClust);
	if (status < 0)
		return (status);

	// Release the rest of the cluster chain
	status = releaseClusterChain(fatData, firstReleasedCluster);
	if (status < 0)
		return (status);

	entry->blocks = newBlocks;
	entry->size = (newBlocks * fatClusterBytes(fatData));

	return (status = 0);
}


static int read(fatInternalData *fatData, kernelFileEntry *theFile,
	unsigned skipClusters, unsigned readClusters, unsigned char *buffer)
{
	// Read the requested sectors of the file into the buffer.  The function
	// assumes that the buffer is large enough to hold the entire file.  It
	// doesn't double-check this.  On success, it returns the number of
	// clusters actually read.

	int status = 0;
	fatEntryData *entryData = NULL;
	unsigned fileClusters = 0;
	unsigned clusterSize = 0;
	unsigned currentCluster = 0;
	unsigned nextCluster = 0;
	unsigned savedClusters = 0;
	unsigned startSavedClusters = 0;
	unsigned count;

	// Get the entry's data
	entryData = (fatEntryData *) theFile->driverData;
	if (!entryData)
	{
		kernelError(kernel_error, "Entry has no data");
		return (status = ERR_BUG);
	}

	// Calculate cluster size
	clusterSize = (unsigned) fatClusterBytes(fatData);

	currentCluster = entryData->startCluster;

	// Skip through the FAT entries until we've used up our 'skip' clusters
	for ( ; skipClusters > 0; skipClusters--)
	{
		status = getFatEntries(fatData, currentCluster, 1, &currentCluster);
		if (status < 0)
		{
			kernelDebugError("Error reading FAT entry");
			return (status);
		}
	}

	// Now, it's possible that the file actually contains fewer clusters
	// than the 'readClusters' value.  If so, replace our readClusters value
	// with that value
	status = getNumClusters(fatData, currentCluster, &fileClusters);
	if (status < 0)
		return (status);

	if (fileClusters < readClusters)
		readClusters = fileClusters;

	// We already know the first cluster
	startSavedClusters = currentCluster;
	savedClusters = 1;

	// Now we go through a loop, reading the cluster numbers from the FAT,
	// then reading the sector info the buffer.

	for (count = 0; count < readClusters; count ++)
	{
		// At the start of this loop, we know the current cluster.  If this
		// is not the last cluster we're reading, peek at the next one
		if (count < (readClusters - 1))
		{
			status = getFatEntries(fatData, currentCluster, 1, &nextCluster);
			if (status < 0)
			{
				kernelDebugError("Error reading FAT entry");
				return (status);
			}

			// We want to minimize the number of read operations, so if we get
			// clusters with consecutive numbers we should read them all in a
			// single operation
			if (nextCluster == (currentCluster + 1))
			{
				if (!savedClusters)
					startSavedClusters = currentCluster;

				currentCluster = nextCluster;
				savedClusters += 1;
				continue;
			}
		}

		// Read the cluster into the buffer.
		status = kernelDiskReadSectors((char *) fatData->disk->name,
			fatClusterToLogical(fatData, startSavedClusters),
			(fatData->bpb.sectsPerClust * savedClusters), buffer);
		if (status < 0)
		{
			kernelDebugError("Error reading file");
			return (status);
		}

		// Increment the buffer pointer
		buffer += (clusterSize * savedClusters);

		// Move to the next cluster
		currentCluster = nextCluster;

		// Reset our counts
		startSavedClusters = currentCluster;
		savedClusters = 1;
	}

	return (count);
}


static int write(fatInternalData *fatData, kernelFileEntry *writeFile,
	unsigned skipClusters, unsigned writeClusters, unsigned char *buffer)
{
	// Write the file to the disk.  Allocate new clusters if the file needs to
	// grow.  It assumes the buffer is big enough and contains the appropriate
	// amount of data (it doesn't double-check this).  On success, it returns
	// the number of clusters actually written.

	int status = 0;
	fatEntryData *entryData = NULL;
	unsigned clusterSize = 0;
	unsigned existingClusters = 0;
	unsigned needClusters = 0;
	unsigned currentCluster = 0;
	unsigned nextCluster = 0;
	unsigned savedClusters = 0;
	unsigned startSavedClusters = 0;
	unsigned count;

	kernelDebug(debug_fs, "FAT writing file \"%s\": skipClusters=%d "
		"writeClusters=%d", writeFile->name, skipClusters, writeClusters);

	// Get the entry's data
	entryData = (fatEntryData *) writeFile->driverData;
	if (!entryData)
	{
		kernelError(kernel_error, "Entry has no data");
		return (status = ERR_NODATA);
	}

	// Calculate cluster size
	clusterSize = (unsigned) fatClusterBytes(fatData);

	// How many clusters are currently allocated to this file?  Are there
	// already enough clusters to complete this operation (including any
	// clusters we're skipping)?

	needClusters = (skipClusters + writeClusters);

	status = getNumClusters(fatData, entryData->startCluster,
		&existingClusters);
	if (status < 0)
	{
		kernelDebugError("Unable to determine cluster count of file or "
			"directory \"%s\"", writeFile->name);
		return (status = ERR_BADDATA);
	}

	if (existingClusters < needClusters)
	{
		status = lengthenFile(fatData, writeFile, needClusters);
		if (status < 0)
		{
			kernelDebugError("Unable to get new clusters for file or "
				"directory \"%s\"", writeFile->name);
			return (status = ERR_NOFREE);
		}
	}

	// Get the starting cluster of the file
	currentCluster = entryData->startCluster;

	// Skip through the FAT entries until we've used up our 'skip' clusters
	for ( ; skipClusters > 0; skipClusters--)
	{
		status = getFatEntries(fatData, currentCluster, 1, &currentCluster);
		if (status < 0)
		{
			kernelDebugError("Error reading FAT entry while skipping "
				"clusters");
			return (status);
		}
	}

	// We already know the first cluster
	startSavedClusters = currentCluster;
	savedClusters = 1;

	kernelDebug(debug_fs, "FAT writing clusters");

	// This is the loop where we write the clusters
	for (count = 0; count < writeClusters; count ++)
	{
		// At the start of this loop, we know the current cluster.  If this
		// is not the last cluster we're reading, peek at the next one
		if (count < (writeClusters - 1))
		{
			status = getFatEntries(fatData, currentCluster, 1, &nextCluster);
			if (status < 0)
			{
				kernelDebugError("Error reading FAT entry in existing chain");
				return (status);
			}

			// We want to minimize the number of write operations, so if we
			// get clusters with consecutive numbers we should read them all
			// in a single operation
			if (nextCluster == (currentCluster + 1))
			{
				if (!savedClusters)
					startSavedClusters = currentCluster;

				currentCluster = nextCluster;
				savedClusters += 1;
				continue;
			}
		}

		// Alright, we can write the clusters we were saving up
		status = kernelDiskWriteSectors((char *) fatData->disk->name,
			fatClusterToLogical(fatData, startSavedClusters),
			(fatData->bpb.sectsPerClust * savedClusters), buffer);
		if (status < 0)
		{
			kernelDebugError("Error writing to disk %s", fatData->disk->name);
			return (status);
		}

		// Increment the buffer pointer
		buffer += (clusterSize * savedClusters);

		// Move to the next cluster
		currentCluster = nextCluster;

		// Reset our counts
		startSavedClusters = currentCluster;
		savedClusters = 1;
	}

	return (count);
}


static inline unsigned makeSystemTime(unsigned theTime)
{
	// This function takes a packed-BCD time value in DOS format and returns
	// the equivalent in packed-BCD system format.

	// The time we get is almost right, except that FAT seconds format only
	// has a granularity of 2 seconds, so we multiply by 2 to get the final
	// value.  The quick way to fix all of this is to simply shift the whole
	// thing left by 1 bit, which results in a time with the correct number
	// of bits, but always an even number of seconds.

	return (theTime << 1);
}


static unsigned makeSystemDate(unsigned date)
{
	// This function takes a packed-BCD date value in DOS format and returns
	// the equivalent in packed-BCD system format.

	unsigned temp = 0;
	unsigned returnedDate = 0;

	returnedDate = date;

	// Unfortunately, FAT dates don't work exactly the same way as system
	// dates.  The DOS year value is a number between 0 and 127, representing
	// the number of years since 1980.  It's found in bits 9-15.

	// Extract the year
	temp = ((returnedDate & 0x0000FE00) >> 9);
	temp += 1980;

	// Clear the year and reset it.  Year should be in places 9->
	returnedDate &= 0x000001FF;
	returnedDate |= (temp << 9);

	return (returnedDate);
}


static int scanDirectory(fatInternalData *fatData, kernelDisk *theDisk,
	kernelFileEntry *currentDir, void *dirBuffer, unsigned dirBufferSize)
{
	// This function takes a pointer to a directory buffer and installs files
	// and directories in the file/directory list for each thing it finds.
	// Basically, this is the FAT directory scanner.

	int status = 0;
	unsigned char *dirEntry;
	unsigned char *subEntry;
	int longFilename = 0;
	int longFilenamePos = 0;
	unsigned count1, count2, count3;

	kernelFileEntry *newItem = NULL;
	fatEntryData *entryData = NULL;

	// Manufacture some "." and ".." entries
	status = kernelFileMakeDotDirs(currentDir->parentDirectory, currentDir);
	if (status < 0)
		kernelError(kernel_warn, "Unable to create '.' and '..' directory "
	 		"entries");

	for (count1 = 0; count1 < (dirBufferSize / FAT_BYTES_PER_DIR_ENTRY);
		count1 ++)
	{
		// Make dirEntry point to the current entry in the dirBuffer
		dirEntry = (dirBuffer + (count1 * FAT_BYTES_PER_DIR_ENTRY));

		// Now we must determine whether this is a valid, undeleted file.

		if (dirEntry[0] == 0xE5)
		{
			// E5 means this is a deleted entry
			continue;
		}
		else if (dirEntry[0] == 0x05)
		{
			// 05 means that the first character is REALLY E5
			dirEntry[0] = 0xE5;
		}
		else if (!dirEntry[0])
		{
			// 00 means that there are no more entries
			break;
		}
		else if (dirEntry[0x0B] == 0x0F)
		{
			// It's a long filename entry.  Skip it until we get to the regular
			// entry
			continue;
		}
		else if (!strncmp((char *) dirEntry, ".          ",
			FAT_8_3_NAME_LEN) || !strncmp((char *) dirEntry, "..         ",
			FAT_8_3_NAME_LEN))
		{
			// Skip '.' and '..' entries
			continue;
		}

		// Peek ahead and get the attributes (byte value).  Figure out the
		// type of the file
		if ((unsigned) dirEntry[0x0B] & FAT_ATTRIB_VOLUMELABEL)
		{
			// It's a volume label.  If this is the root directory, remember
			// this entry so we can re-write it later.
			if (currentDir == theDisk->filesystem.filesystemRoot)
			{
				memcpy((unsigned char *) fatData->rootDirLabel, dirEntry,
					FAT_BYTES_PER_DIR_ENTRY);
				setVolumeLabel(theDisk, (char *) dirEntry);
			}

			continue;
		}

		// If we fall through to here, it must be a good file or directory.

		// Now we should create a new entry in the "used" list for this item

		// Get a free file entry structure.
		newItem = kernelFileNewEntry(theDisk);
		if (!newItem)
		{
			kernelError(kernel_error, "Not enough free file structures");
			return (status = ERR_NOFREE);
		}

		// Get the entry data structure.  This should have been created by
		// a call to our NewEntry function by the kernelFileNewEntry call.
		entryData = (fatEntryData *) newItem->driverData;
		if (!entryData)
		{
			kernelError(kernel_error, "Entry has no private filesystem data");
			return (status = ERR_NODATA);
		}

		// Check for a long filename entry by looking at the attributes of the
		// entry that occurs before this one.  Check whether the appropriate
		// attribute bits are set.  Also make sure it isn't a deleted long
		// filename entry

		subEntry = (dirEntry - 32);

		if ((count1 > 0) && (subEntry[0x0B] == 0x0F))
		{
			longFilename = 1;
			longFilenamePos = 0;

			while (1)
			{
				// Get the first five 2-byte characters from this entry
				for (count3 = 1; count3 < 10; count3 += 2)
					newItem->name[longFilenamePos++] = subEntry[count3];

				// Get the next six 2-byte characters
				for (count3 = 14; count3 < 26; count3 += 2)
					newItem->name[longFilenamePos++] = subEntry[count3];

				// Get the last two 2-byte characters
				for (count3 = 28; count3 < 32; count3 += 2)
					newItem->name[longFilenamePos++] = subEntry[count3];

				// Determine whether this was the last long filename entry for
				// this file.  If not, we subtract 32 from subEntry and loop
				if (subEntry[0] & 0x40)
					break;
				else
					subEntry -= 32;
			}

			newItem->name[longFilenamePos] = NULL;
		}
		else
		{
			longFilename = 0;
		}

		// Now go through the regular (DOS short) entry for this file.

		// Copy short alias into the shortAlias field of the file structure
		strncpy((char *) entryData->shortAlias, (char *) dirEntry,
				FAT_8_3_NAME_LEN);
		entryData->shortAlias[FAT_8_3_NAME_LEN] = '\0';

		// If there's no long filename, set the filename to be the same as the
		// short alias we just extracted.  We'll need to construct it from the
		// drain-bamaged format used by DOS(TM)
		if (!longFilename)
		{
			strncpy((char *) newItem->name, (char *) entryData->shortAlias, 8);
			newItem->name[8] = '\0';

			// Insert a NULL if there's a [space] character anywhere in
			// this part
			for (count2 = 0; count2 < strlen((char *) newItem->name);
				count2 ++)
			{
				if (newItem->name[count2] == ' ')
					newItem->name[count2] = NULL;
			}

			// If the extension is non-empty, insert a '.' character in the
			// middle between the filename and extension
			if (entryData->shortAlias[8] != ' ')
				strncat((char *) newItem->name, ".", 1);

			// Copy the filename extension
			strncat((char *) newItem->name,
				(char *)(entryData->shortAlias + 8), 3);

			// Insert a NULL if there's a [space] character anywhere in
			// the name
			for (count2 = 0; count2 < strlen((char *) newItem->name);
				count2 ++)
			{
				if (newItem->name[count2] == ' ')
					newItem->name[count2] = NULL;
			}

			// Short filenames are case-insensitive, and are usually
			// represented by all-uppercase letters.  This looks silly in the
			// modern world, so we convert them all to lowercase as a matter
			// of preference.
			for (count2 = 0; count2 < strlen((char *) newItem->name);
				count2 ++)
			{
				newItem->name[count2] = tolower(newItem->name[count2]);
			}
		}

		kernelDebug(debug_fs, "FAT scanning directory entry for %s",
			newItem->name);

		// Get the entry's various other information

		// Attributes (byte value)
		entryData->attributes = (unsigned) dirEntry[0x0B];

		if (entryData->attributes & FAT_ATTRIB_SUBDIR)
			// Then it's a subdirectory
			newItem->type = dirT;
		else
			// It's a regular file
			newItem->type = fileT;

		// reserved (byte value)
		entryData->res = (unsigned) dirEntry[0x0C];

		// timeTenth (byte value)
		entryData->timeTenth = (unsigned) dirEntry[0x0D];

		// Creation time (word value)
		newItem->creationTime =
			makeSystemTime(((unsigned) dirEntry[0x0F] << 8) +
				(unsigned) dirEntry[0x0E]);

		// Creation date (word value)
		newItem->creationDate =
			makeSystemDate(((unsigned) dirEntry[0x11] << 8) +
				(unsigned) dirEntry[0x10]);

		// Last access date (word value)
		newItem->accessedDate =
			makeSystemDate(((unsigned) dirEntry[0x13] << 8) +
				(unsigned) dirEntry[0x12]);

		// High word of startCluster (word value)
		entryData->startCluster = (((unsigned) dirEntry[0x15] << 24) +
			((unsigned) dirEntry[0x14] << 16));

		// Last modified time (word value)
		newItem->modifiedTime =
			makeSystemTime(((unsigned) dirEntry[0x17] << 8) +
				(unsigned) dirEntry[0x16]);

		// Last modified date (word value)
		newItem->modifiedDate =
			makeSystemDate(((unsigned) dirEntry[0x19] << 8) +
				(unsigned) dirEntry[0x18]);

		// Low word of startCluster (word value)
		entryData->startCluster |= (((unsigned) dirEntry[0x1B] << 8) +
			(unsigned) dirEntry[0x1A]);

		// Now we get the size.  If it's a directory we have to actually call
		// getNumClusters() to get the size in clusters

		status = getNumClusters(fatData, entryData->startCluster,
			(unsigned *) &newItem->blocks);
		if (status < 0)
		{
			kernelDebugError("Couldn't determine the number of clusters for "
				"entry %s", newItem->name);
			return (status);
		}

		if (entryData->attributes & FAT_ATTRIB_SUBDIR)
			newItem->size = (newItem->blocks * fatClusterBytes(fatData));
		else
			// (doubleword value)
			newItem->size = *((unsigned *)(dirEntry + 0x1C));

		// Add our new entry to the existing file chain.  Don't panic and/or
		// quit if we have a problem of some sort
		kernelFileInsertEntry(newItem, currentDir);
	}

	return (status = 0);
}


static int readRootDir(fatInternalData *fatData, kernelDisk *theDisk)
{
	// Reads the root directory.  It assumes that the requirement to do so has
	// been met (i.e. it does not check whether the update is necessary, and
	// does so unquestioningly).

	int status = 0;
	kernelFileEntry *rootDir = NULL;
	unsigned rootDirStart = 0;
	unsigned char *dirBuffer = NULL;
	unsigned dirBufferSize = 0;
	fatEntryData *rootDirData = NULL;
	fatEntryData dummyEntryData;
	unsigned rootDirBlocks = 0;
	kernelFileEntry dummyEntry;

	// The root directory scheme is different depending on whether this is
	// a FAT32 volume or a FAT12/16 volume.  If it is not FAT32, then the
	// root directory is in a fixed location, with a fixed size (which we
	// can determine using values in the fatData structure).  If the volume
	// is FAT32, then we can treat it like a regular directory, with the
	// starting cluster number in the fatData->rootDirClusterF32 field.

	if ((fatData->fsType == fat12) || (fatData->fsType == fat16))
	{
		// Allocate a directory buffer based on the number of bytes per
		// sector and the number of FAT sectors on the volume.  Since
		// we are working with a FAT12 or FAT16 volume, the root
		// directory size is fixed.
		dirBufferSize = (fatData->bpb.bytesPerSect * fatData->rootDirSects);
	}
	else // if (fatData->fsType == fat32)
	{
		// We need to take the starting cluster of the FAT32 root directory,
		// and determine the size of the directory.
		status = getNumClusters(fatData, fatData->bpb.fat32.rootClust,
			&dirBufferSize);
		if (status < 0)
			return (status);

		dirBufferSize *= fatClusterBytes(fatData);
	}

	dirBuffer = kernelMalloc(dirBufferSize);
	if (!dirBuffer)
	{
		kernelError(kernel_error, "NULL directory buffer");
		return (status = ERR_MEMORY);
	}

	// Here again, we diverge depending on whether this is FAT12/16
	// or FAT32.  For FAT12/16, we will be reading consecutive sectors
	// from the front part of the disk.  For FAT32, we will be treating
	// the root directory just like any other old directory.

	if ((fatData->fsType == fat12) || (fatData->fsType == fat16))
	{
		// This is not FAT32, so we have to calculate the starting SECTOR of
		// the root directory.
		rootDirStart = (fatData->bpb.rsvdSectCount + (fatData->bpb.numFats *
			fatData->fatSects));

		// Now we need to read some consecutive sectors from the disk which
		// make up the root directory
		status = kernelDiskReadSectors((char *) fatData->disk->name,
			rootDirStart, fatData->rootDirSects, dirBuffer);
		if (status < 0)
		{
			kernelFree(dirBuffer);
			return (status);
		}

		rootDirBlocks = (fatData->rootDirSects / fatData->bpb.sectsPerClust);
	}
	else // if (fatData->fsType == fat32)
	{
		// We need to read in the FAT32 root directory now.  This is just a
		// regular directory -- we already know the size and starting cluster,
		// so we need to fill out a dummy kernelFileEntry structure so that
		// the read() routine can go get it for us.

		status = getNumClusters(fatData, fatData->bpb.fat32.rootClust,
			&rootDirBlocks);
		if (status < 0)
		{
			kernelFree(dirBuffer);
			return (status);
		}

		// The only thing the read routine needs in this data structure
		// is the starting cluster number.
		dummyEntryData.startCluster = fatData->bpb.fat32.rootClust;
		dummyEntry.driverData = (void *) &dummyEntryData;

		status = read(fatData, &dummyEntry, 0, rootDirBlocks, dirBuffer);
		if (status < 0)
		{
			kernelFree(dirBuffer);
			return (status);
		}
	}

	// The whole root directory should now be in our buffer.  We can proceed
	// to make the applicable data structures

	rootDir = theDisk->filesystem.filesystemRoot;

	// Get the entry data structure.  This should have been created by a call
	// to the kernelFileNewEntry call.

	rootDirData = (fatEntryData *) rootDir->driverData;
	if (!rootDirData)
	{
		kernelError(kernel_error, "Entry has no private data");
		kernelFileReleaseEntry(rootDir);
		kernelFree(dirBuffer);
		return (status = ERR_NODATA);
	}

	// Fill out some starting values in the file entry structure

	rootDir->blocks = rootDirBlocks;

	if ((fatData->fsType == fat12) || (fatData->fsType == fat16))
	{
		rootDir->size = (fatData->rootDirSects * fatData->bpb.bytesPerSect);
		rootDirData->startCluster = 0;
	}
	else
	{
		rootDir->size = (rootDir->blocks * fatClusterBytes(fatData));
		rootDirData->startCluster = fatData->bpb.fat32.rootClust;
	}

	// Fill out some values in the directory's private data
	strncpy((char *) rootDirData->shortAlias, "/", 2);
	rootDirData->attributes = (FAT_ATTRIB_SUBDIR | FAT_ATTRIB_SYSTEM);

	// We have to read the directory and fill out the chain of its
	// files/subdirectories in the lists.
	status = scanDirectory(fatData, theDisk, rootDir, dirBuffer,
		dirBufferSize);

	kernelFree(dirBuffer);

	if (status < 0)
	{
		kernelDebugError("Error parsing root directory");
		kernelFileReleaseEntry(rootDir);
		return (status);
	}

	// Return success
	return (status = 0);
}


static int dirRequiredEntries(fatInternalData *fatData,
	kernelFileEntry *directory)
{
	// This function is internal, and is used to determine how many 32-byte
	// entries will be required to hold the requested directory.

	int entries = 0;
	kernelFileEntry *listItemPointer = NULL;

	// Make sure directory is a directory
	if (directory->type != dirT)
	{
		kernelError(kernel_error,
			"Directory structure to count is not a directory");
		return (entries = ERR_NOTADIR);
	}

	// We have to count the files in the directory to figure out how
	// many items to fill in
	if (!directory->contents)
	{
		// This directory is currently empty.  No way this should ever happen
		kernelError(kernel_error, "Directory structure to count is empty");
		return (entries = ERR_BUG);
	}

	listItemPointer = directory->contents;

	while (listItemPointer)
	{
		entries += 1;

		// '.' and '..' do not have long filename entries
		if (strcmp((char *) listItemPointer->name, ".") &&
			strcmp((char *) listItemPointer->name, ".."))
		{
			// All other entries have long filenames

			// We can fit 13 characters into each long filename slot
			entries += (strlen((char *) listItemPointer->name) / 13);
			if (strlen((char *) listItemPointer->name) % 13)
				entries += 1;
		}

		listItemPointer = listItemPointer->nextEntry;
	}

	// If this is the root directory, and it needs an entry for the volume
	// label, add one.
	if ((directory == directory->disk->filesystem.filesystemRoot) &&
		fatData->rootDirLabel[0])
	{
		entries += 1;
	}

	// Add 1 for the NULL entry at the end
	entries += 1;

	return (entries);
}


static inline unsigned makeDosTime(unsigned theTime)
{
	// This function takes a packed-BCD time value in system format and returns
	// the equivalent in packed-BCD DOS format.

	// The time we get is almost right, except that FAT seconds format only
	// has a granularity of 2 seconds, so we divide by 2 to get the final
	// value. The quick way to fix all of this is to simply shift the whole
	// thing by 1 bit, creating a nice 16-bit DOS time.

	return (theTime >> 1);
}


static unsigned makeDosDate(unsigned date)
{
	// This function takes a packed-BCD date value in system format and returns
	// the equivalent in packed-BCD DOS format.

	unsigned temp = 0;
	unsigned returnedDate = 0;

	returnedDate = date;

	// This date is almost okay.  The RTC function returns a year value that's
	// nice.  It returns an absolute year, with no silly monkey business.  For
	// example, 1999 is represented as 1999.  2011 is 2011, etc. in bits 7->.
	// Unfortunately, FAT dates don't work exactly the same way.  Year is a
	// value between 0 and 127, representing the number of years since 1980.

	// Extract the year
	temp = ((returnedDate & 0xFFFFFE00) >> 9);
	temp -= 1980;

	// Clear the year and reset it
	// Year should be 7 bits in places 9-15
	returnedDate &= 0x000001FF;
	returnedDate |= (temp << 9);

	return (returnedDate);
}


static int fillDirectory(fatInternalData *fatData, kernelFileEntry *currentDir,
	void *dirBuffer)
{
	// This function takes a directory structure and writes it to the
	// appropriate directory on disk.

	int status = 0;
	char shortAlias[12];
	int fileNameLength = 0;
	int longFilenameSlots = 0;
	int longFilenamePos = 0;
	unsigned char fileCheckSum = 0;
	char *dirEntry = NULL;
	char *subEntry = NULL;
	kernelFileEntry *listItemPointer = NULL;
	kernelFileEntry *realEntry = NULL;
	fatEntryData *entryData = NULL;
	unsigned temp;
	int count, count2;

	// Don't try to fill in a directory that's really a link
	if (currentDir->type == linkT)
	{
		kernelError(kernel_error, "Cannot fill in a link directory");
		return (status = ERR_INVALID);
	}

	dirEntry = dirBuffer;
	listItemPointer = currentDir->contents;

	while (listItemPointer)
	{
		// Skip things like mount points that don't really belong to this
		// filesystem.
		if (listItemPointer->disk != currentDir->disk)
		{
			listItemPointer = listItemPointer->nextEntry;
			continue;
		}

		realEntry = listItemPointer;
		if (listItemPointer->type == linkT)
			// Resolve links
			realEntry = kernelFileResolveLink(listItemPointer);

		// Get the entry's data
		entryData = (fatEntryData *) realEntry->driverData;
		if (!entryData)
		{
			kernelError(kernel_error, "File entry has no private filesystem "
				"data");
			return (status = ERR_BUG);
		}

		if (!strcmp((char *) listItemPointer->name, ".") ||
			!strcmp((char *) listItemPointer->name, ".."))
		{
			// Don't write '.' and '..' entries in the root directory of a
			// filesystem
			if (currentDir == currentDir->disk->filesystem.filesystemRoot)
			{
				listItemPointer = listItemPointer->nextEntry;
				continue;
			}

			// Get the appropriate short alias.
			if (!strcmp((char *) listItemPointer->name, "."))
				strcpy(shortAlias, ".          ");
			else if (!strcmp((char *) listItemPointer->name, ".."))
				strcpy(shortAlias, "..         ");
		}
		else
		{
			strcpy(shortAlias, (char *) entryData->shortAlias);
		}

		// Calculate this file's 8.3 checksum.  We need this in advance for
		// associating the long filename entries
		fileCheckSum = 0;
		for (count = 0; count < FAT_8_3_NAME_LEN; count++)
		{
			fileCheckSum = (unsigned char)((((fileCheckSum & 0x01) << 7) |
				((fileCheckSum & 0xFE) >> 1)) + shortAlias[count]);
		}

		// All files except '.' and '..' (and any volume label) will have at
		// least one long filename entry, just because that's the only kind we
		// use in Visopsys.  Short aliases are only generated for
		// compatibility.

		if (strcmp((char *) listItemPointer->name, ".") &&
			strcmp((char *) listItemPointer->name, ".."))
		{
			// Figure out how many long filename slots we need
			fileNameLength = strlen((char *) listItemPointer->name);
			longFilenameSlots = (fileNameLength / 13);
			if (fileNameLength % 13)
				longFilenameSlots += 1;

			// We must do a loop backwards through the directory slots
			// before this one, writing the characters of this long filename
			// into the appropriate slots

			dirEntry += ((longFilenameSlots - 1) * FAT_BYTES_PER_DIR_ENTRY);
			subEntry = dirEntry;
			longFilenamePos = 0;

			for (count = 0; count < longFilenameSlots; count++)
			{
				// Put the "counter" byte into the first slot
				subEntry[0] = (count + 1);
				if (count == (longFilenameSlots - 1))
					subEntry[0] = (subEntry[0] | 0x40);

				// Put the first five 2-byte characters into this entry
				for (count2 = 1; count2 < 10; count2 += 2)
				{
					if (longFilenamePos > fileNameLength)
					{
						subEntry[count2] = (unsigned char) 0xFF;
						subEntry[count2 + 1] = (unsigned char) 0xFF;
					}
					else
					{
						subEntry[count2] = (unsigned char)
							listItemPointer->name[longFilenamePos++];
					}
				}

				// Put the "long filename entry" attribute byte into
				// the attribute slot
				subEntry[0x0B] = 0x0F;

				// Put the file's 8.3 checksum into the 0x0Dth spot
				subEntry[0x0D] = (unsigned char) fileCheckSum;

				// Put the next six 2-byte characters
				for (count2 = 14; count2 < 26; count2 += 2)
				{
					if (longFilenamePos > fileNameLength)
					{
						subEntry[count2] = (unsigned char) 0xFF;
						subEntry[count2 + 1] = (unsigned char) 0xFF;
					}
					else
					{
						subEntry[count2] = (unsigned char)
							listItemPointer->name[longFilenamePos++];
					}
				}

				// Put the last two 2-byte characters
				for (count2 = 28; count2 < 32; count2 += 2)
				{
					if (longFilenamePos > fileNameLength)
					{
						subEntry[count2] = (unsigned char) 0xFF;
						subEntry[count2 + 1] = (unsigned char) 0xFF;
					}
					else
					{
						subEntry[count2] = (unsigned char)
							listItemPointer->name[longFilenamePos++];
					}
				}

				// Determine whether this was the last long filename
				// entry for this file.  If not, we subtract
				// FAT_BYTES_PER_DIR_ENTRY from subEntry and loop
				if (count == (longFilenameSlots - 1))
					break;
				else
					subEntry -= FAT_BYTES_PER_DIR_ENTRY;
			}

			// Move to the next free directory entry
			dirEntry +=  FAT_BYTES_PER_DIR_ENTRY;
		}

		// Copy the short alias into the entry.
		dirEntry[0] = NULL;
		strncpy(dirEntry, shortAlias, FAT_8_3_NAME_LEN);

		// attributes (byte value)
		dirEntry[0x0B] = (unsigned char) entryData->attributes;

		// reserved (byte value)
		dirEntry[0x0C] = (unsigned char) entryData->res;

		// timeTenth (byte value)
		dirEntry[0x0D] = (unsigned char) entryData->timeTenth;

		// Creation time (word value)
		temp = makeDosTime(realEntry->creationTime);
		dirEntry[0x0E] = (unsigned char)(temp & 0x000000FF);
		dirEntry[0x0F] = (unsigned char)(temp >> 8);

		// Creation date (word value)
		temp = makeDosDate(realEntry->creationDate);
		dirEntry[0x10] = (unsigned char)(temp & 0x000000FF);
		dirEntry[0x11] = (unsigned char)(temp >> 8);

		// Accessed date (word value)
		temp = makeDosDate(realEntry->accessedDate);
		dirEntry[0x12] = (unsigned char)(temp & 0x000000FF);
		dirEntry[0x13] = (unsigned char)(temp >> 8);

		// startClusterHi (word value)
		dirEntry[0x14] =
			(unsigned char)((entryData->startCluster & 0x00FF0000) >> 16);
		dirEntry[0x15] =
			(unsigned char)	((entryData->startCluster & 0xFF000000) >> 24);

		// lastWriteTime (word value)
		temp = makeDosTime(realEntry->modifiedTime);
		dirEntry[0x16] = (unsigned char)(temp & 0x000000FF);
		dirEntry[0x17] = (unsigned char)(temp >> 8);

		// lastWriteDate (word value)
		temp = makeDosDate(realEntry->modifiedDate);
		dirEntry[0x18] = (unsigned char)(temp & 0x000000FF);
		dirEntry[0x19] = (unsigned char)(temp >> 8);

		// startCluster (word value)
		dirEntry[0x1A] = (unsigned char)(entryData->startCluster & 0xFF);
		dirEntry[0x1B] =
			(unsigned char)((entryData->startCluster  & 0xFF00) >> 8);

		// Now we get the size.  If it's a directory we write zero for the size
		// (doubleword value)
		if (entryData->attributes & FAT_ATTRIB_SUBDIR)
			*((unsigned *)(dirEntry + 0x1C)) = 0;
		else
			*((unsigned *)(dirEntry + 0x1C)) = realEntry->size;

		// Increment to the next directory entry spot
		dirEntry += FAT_BYTES_PER_DIR_ENTRY;

		// Increment to the next file structure
		listItemPointer = listItemPointer->nextEntry;
	}

	// If this is the root directory, and there was a volume label entry,
	// replace it.
	if ((currentDir == currentDir->disk->filesystem.filesystemRoot) &&
		fatData->rootDirLabel[0])
	{
		memcpy(dirEntry, (unsigned char *) fatData->rootDirLabel,
			FAT_BYTES_PER_DIR_ENTRY);
		dirEntry += FAT_BYTES_PER_DIR_ENTRY;
	}

	// Put a NULL entry in the last spot.
	dirEntry[0] = '\0';

	return (status = 0);
}


static int readDir(kernelFileEntry *directory)
{
	// This function receives an emtpy file entry structure, which represents
	// a directory whose contents have not yet been read.  This will fill the
	// directory structure with its appropriate contents.

	int status = 0;
	kernelDisk *theDisk = NULL;
	fatInternalData *fatData = NULL;
	unsigned char *dirBuffer = NULL;
	unsigned dirBufferSize = 0;

	if (!initialized)
		return (status = ERR_NOTINITIALIZED);

	// Check params
	if (!directory)
	{
		kernelError(kernel_error, "NULL parameter");
		return (status = ERR_NULLPARAMETER);
	}

	// Make sure that there's a private FAT data structure attached to this
	// file entry
	if (!directory->driverData)
	{
		kernelError(kernel_error, "File entry has no private filesystem data");
		return (status = ERR_NODATA);
	}

	theDisk = directory->disk;

	// Get the FAT data for the requested filesystem
	fatData = getFatData(theDisk);
	if (!fatData)
		return (status = ERR_BADDATA);

	// Make sure it's really a directory, and not a regular file
	if (directory->type != dirT)
		return (status = ERR_NOTADIR);

	// Now we can go about scanning the directory.

	dirBufferSize = (directory->blocks * fatClusterBytes(fatData));

	dirBuffer = kernelMalloc(dirBufferSize);
	if (!dirBuffer)
	{
		kernelError(kernel_error, "Memory allocation error");
		return (status = ERR_MEMORY);
	}

	// Now we read all of the sectors of the directory
	status = read(fatData, directory, 0, directory->blocks, dirBuffer);
	if (status < 0)
	{
		kernelDebugError("Error reading directory");
		kernelFree(dirBuffer);
		return (status);
	}

	// Call the routine to interpret the directory data
	status = scanDirectory(fatData, theDisk, directory, dirBuffer,
		dirBufferSize);

	// Free the directory buffer we used
	kernelFree(dirBuffer);

	if (status < 0)
	{
		kernelDebugError("Error parsing directory");
		return (status);
	}

	// Return success
	return (status = 0);
}


static int writeDir(kernelFileEntry *directory)
{
	// This function takes a directory entry structure and updates it
	// appropriately on the disk volume.

	int status = 0;
	kernelDisk *theDisk = NULL;
	fatInternalData *fatData = NULL;
	fatEntryData *entryData = NULL;
	unsigned clusterSize = 0;
	unsigned char *dirBuffer = NULL;
	unsigned dirBufferSize = 0;
	unsigned directoryEntries = 0;
	unsigned blocks = 0;

	if (!initialized)
		return (status = ERR_NOTINITIALIZED);

	// Check params
	if (!directory)
	{
		kernelError(kernel_error, "NULL parameter");
		return (status = ERR_NULLPARAMETER);
	}

	kernelDebug(debug_fs, "FAT writing directory \"%s\"", directory->name);

	// Get the private FAT data structure attached to this file entry
	entryData = (fatEntryData *) directory->driverData;
	if (!entryData)
	{
		kernelError(kernel_error, "NULL private file data");
		return (status = ERR_NODATA);
	}

	theDisk = directory->disk;

	// Get the FAT data for the requested filesystem
	fatData = getFatData(theDisk);
	if (!fatData)
	{
		kernelError(kernel_error, "Unable to find FAT filesystem data");
		return (status = ERR_BADDATA);
	}

	// Make sure it's really a directory, and not a regular file
	if (directory->type != dirT)
	{
		kernelError(kernel_error, "Directory to write is not a directory");
		return (status = ERR_NOTADIR);
	}

	// Figure out the size of the buffer we need to allocate to hold the
	// directory
	if ((fatData->fsType != fat32) &&
		(directory == theDisk->filesystem.filesystemRoot))
	{
		dirBufferSize = (fatData->rootDirSects * fatData->bpb.bytesPerSect);
		blocks = fatData->rootDirSects;
	}
	else
	{
		// Figure out how many directory entries there are
		directoryEntries = dirRequiredEntries(fatData, directory);

		// Calculate the size of a cluster in this filesystem
		clusterSize = fatClusterBytes(fatData);

		if (!clusterSize)
		{
			// This volume would appear to be corrupted
			kernelError(kernel_error, "The FAT volume is corrupt");
			return (status = ERR_BADDATA);
		}

		dirBufferSize = (directoryEntries * FAT_BYTES_PER_DIR_ENTRY);
		if (dirBufferSize % clusterSize)
			dirBufferSize += (clusterSize - (dirBufferSize % clusterSize));

		// Calculate the new number of blocks that will be occupied by
		// this directory
		blocks = ((dirBufferSize + (fatClusterBytes(fatData) - 1)) /
			fatClusterBytes(fatData));

		// If the new number of blocks is less than the previous value, we
		// should deallocate all of the extra clusters at the end

		if (blocks < directory->blocks)
		{
			status = shortenFile(fatData, directory, blocks);
			if (status < 0)
				// Not fatal.  Just warn
				kernelError(kernel_warn, "Unable to shorten directory");
		}
	}

	// Allocate temporary space for the directory buffer
	dirBuffer = kernelMalloc(dirBufferSize);
	if (!dirBuffer)
	{
		kernelError(kernel_error, "Memory allocation error writing directory");
		return (status = ERR_MEMORY);
	}

	// Fill in the directory entries
	status = fillDirectory(fatData, directory, dirBuffer);
	if (status < 0)
	{
		kernelDebugError("Error filling directory structure");
		kernelFree(dirBuffer);
		return (status);
	}

	// Write the directory "file".  If it's the root dir of a non-FAT32
	// filesystem we do a special version of this write.
	if ((fatData->fsType != fat32) &&
		(directory == theDisk->filesystem.filesystemRoot))
	{
		status = kernelDiskWriteSectors((char *) fatData->disk->name,
			(fatData->bpb.rsvdSectCount + (fatData->fatSects *
				fatData->bpb.numFats)), blocks, dirBuffer);
	}
	else
	{
		status = write(fatData, directory, 0, blocks, dirBuffer);
	}

	// De-allocate the directory buffer
	kernelFree(dirBuffer);

	if (status == ERR_NOWRITE)
	{
		kernelError(kernel_warn, "File system is read-only");
		theDisk->filesystem.readOnly = 1;
	}
	else if (status < 0)
	{
		kernelError(kernel_error, "Error writing directory \"%s\"",
			directory->name);
	}

	return (status);
}


static int checkFileChain(fatInternalData *fatData, kernelFileEntry *checkFile)
{
	// This function is used to make sure (as much as possible) that the
	// cluster allocation chain of a file is sane.  This is important so that
	// we don't end up (for example) deleting clusters that belong to other
	// files, etc.

	int status = 0;
	fatEntryData *entryData = NULL;
	unsigned clusterSize = 0;
	unsigned expectedClusters = 0;
	unsigned allocatedClusters = 0;

	// Get the entry's data
	entryData = (fatEntryData *) checkFile->driverData;
	if (!entryData)
	{
		kernelError(kernel_error, "File entry has no private filesystem data");
		return (status = ERR_BUG);
	}

	// Make sure there is supposed to be a starting cluster for this file.
	// If not, make sure that the file's size is supposed to be zero
	if (!entryData->startCluster)
	{
		if (!checkFile->size)
		{
			// Nothing to do for a zero-length file.
			return (status = 0);
		}
		else
		{
			kernelError(kernel_error, "Non-zero-length file \"%s\" has no "
				"clusters allocated", checkFile->name);
			return (status = ERR_BADDATA);
		}
	}

	// Calculate the cluster size for this filesystem
	clusterSize = fatClusterBytes(fatData);

	// Calculate the number of clusters we would expect this file to have
	if (clusterSize)
	{
		expectedClusters = (checkFile->size / clusterSize);
		if (checkFile->size % clusterSize)
			expectedClusters += 1;
	}
	else
	{
		// This volume would appear to be corrupted
		kernelError(kernel_error, "The FAT volume is corrupt");
		return (status = ERR_BADDATA);
	}

	// We count the number of clusters used by this file, according to the
	// allocation chain
	status = getNumClusters(fatData, entryData->startCluster,
		&allocatedClusters);
	if (status < 0)
		return (status);

	// Now, just reconcile the expected size against the number of expected
	// clusters
	if (allocatedClusters == expectedClusters)
	{
		return (status = 0);
	}
	else if (allocatedClusters > expectedClusters)
	{
		kernelError(kernel_error, "Clusters allocated exceeds nominal size");
		return (status = ERR_BADDATA);
	}
	else
	{
		kernelError(kernel_error, "Clusters allocated are less than nominal "
			"size");
		return (status = ERR_BADDATA);
	}
}


static int releaseEntryClusters(fatInternalData *fatData,
	kernelFileEntry *deallocateFile)
{
	// Deallocate the cluster chain associated with a file entry.

	int status = 0;
	fatEntryData *entryData = NULL;

	// Get the entry's data
	entryData = (fatEntryData *) deallocateFile->driverData;
	if (!entryData)
	{
		kernelError(kernel_error, "File entry has no private filesystem data");
		return (status = ERR_BUG);
	}

	if (entryData->startCluster)
	{
		// Deallocate the clusters belonging to the file
		status = releaseClusterChain(fatData, entryData->startCluster);
		if (status)
		{
			kernelError(kernel_error, "Unable to deallocate file's clusters");
			return (status);
		}

		// Assign '0' to the file's entry's startcluster
		entryData->startCluster = 0;
	}

	// Update the size of the file
	deallocateFile->blocks = 0;
	deallocateFile->size = 0;

	return (status = 0);
}


static int defragFile(fatInternalData *fatData, kernelFileEntry *entry,
	int check, progress *prog)
{
	// Returns 1 if the supplied file is fragmented

	int status = 0;
	unsigned numClusters = 0;
	fatEntryData *entryData = entry->driverData;
	unsigned clusterNumber = 0;
	unsigned nextClusterNumber = 0;
	int fragged = 0;
	void *fileData = NULL;
	unsigned count;

	kernelDebug(debug_fs, "FAT defragging file %s", entry->name);

	status = getNumClusters(fatData, entryData->startCluster, &numClusters);
	if (status < 0)
		return (status);

	// If there are 1 or fewer clusters, obviously there is no defrag to do.
	if (numClusters <= 1)
		return (fragged = 0);

	// Make sure the chain of clusters is not corrupt
	status = checkFileChain(fatData, entry);
	if (status)
		return (status);

	clusterNumber = entryData->startCluster;
	for (count = 1; count < numClusters; count ++)
	{
		status = getFatEntries(fatData, clusterNumber, 1, &nextClusterNumber);
		if (status < 0)
			return (status);

		if (nextClusterNumber > (clusterNumber + 1))
		{
			// This file is fragmented.

			fragged = 1;

			if (check)
				break;

			// Read it into memory, then re-write it, and delete the existing
			// cluster chain.

			if (prog && (kernelLockGet(&prog->progLock) >= 0))
			{
				snprintf((char *) prog->statusMessage, PROGRESS_MAX_MESSAGELEN,
					_("Defragmenting %llu/%llu: %s"), (prog->numFinished + 1),
					prog->numTotal, entry->name);
				kernelLockRelease(&prog->progLock);
			}

			fileData = kernelMalloc(numClusters * fatClusterBytes(fatData));
			if (!fileData)
				return (status = ERR_MEMORY);

			// Read the file
			status = read(fatData, entry, 0, numClusters, fileData);
			if (status < 0)
			{
				kernelFree(fileData);
				return (status);
			}

			// Deallocate clusters belonging to the item.
			status = releaseEntryClusters(fatData, entry);
			if (status < 0)
			{
				kernelFree(fileData);
				return (status);
			}

			// Write the file
			status = write(fatData, entry, 0, numClusters, fileData);

			kernelFree(fileData);

			if ((fatData->fsType == fat32) && !strcmp((char *) entry->name,
				"/"))
			{
				fatData->bpb.fat32.rootClust = entryData->startCluster;
			}

			if (status < 0)
			{
				kernelFree(fileData);
				return (status);
			}

			if (prog && (kernelLockGet(&prog->progLock) >= 0))
			{
				prog->numFinished += 1;
				prog->percentFinished = ((prog->numFinished * 100) /
					prog->numTotal);
				kernelLockRelease(&prog->progLock);
			}

			break;
		}

		clusterNumber = nextClusterNumber;
	}

	return (fragged);
}


static int defragRecursive(fatInternalData *fatData, kernelFileEntry *entry,
	int check, progress *prog)
{
	int status = 0;
	int numFragged = 0;
	kernelFileEntry *tmpEntry = NULL;

	// A few things we never defragment
	if (!strcmp((char *) entry->name, ".") ||
		!strcmp((char *) entry->name, "..") ||
		!strcmp((char *) entry->name, "vloader"))
	{
		return (numFragged = 0);
	}

	// If it's a directory, do its contents first.
	if (entry->type == dirT)
	{
		if (!entry->contents)
		{
			status = readDir(entry);
			if (status < 0)
				return (status);
		}

		for (tmpEntry = entry->contents; tmpEntry;
			tmpEntry = tmpEntry->nextEntry)
		{
			status = defragRecursive(fatData, tmpEntry, check, prog);
			if (status < 0)
				return (status);

			numFragged += status;
		}

		if (!check)
			// Update the directory on disk
			writeDir(entry);
	}

	// If this item is not the FAT12/FAT16 root directory, defrag it.
	if ((fatData->fsType == fat32) || strcmp((char *) entry->name, "/"))
	{
		status = defragFile(fatData, entry, check, prog);
		if (status < 0)
			return (status);

		numFragged += status;
	}

	return (numFragged);
}


static unsigned getLastUsedCluster(fatInternalData *fatData)
{
	// Finds the last used cluster in the volume's free cluster bitmap.
	// Created for the resizing function.

	int status = 0;
	unsigned last = (fatData->dataClusters + 1);

	kernelDebug(debug_fs, "FAT get last used cluster");

	if (makingFatFree == fatData)
		kernelMultitaskerBlock(makeFatFreePid);

	// Attempt to lock the free-block bitmap
	status = kernelLockGet(&fatData->freeBitmapLock);
	if (status < 0)
	{
		kernelDebugError("Unable to lock the free-cluster bitmap");
		return (last);
	}

	// Move backwards through the free cluster bitmap.  When we find a non-free
	// cluster, return the cluster number.

	for (last = (fatData->dataClusters + 1); last >= 2; last --)
	{
		if (fatData->freeClusterBitmap[last / 8] & (1 << (last % 8)))
			// This cluster is used
			break;
	}

	kernelDebug(debug_fs, "FAT last used cluster %u/%u", last,
		(fatData->dataClusters + 1));

	// Unlock the list and return success
	kernelLockRelease(&fatData->freeBitmapLock);
	return (last);
}


static int moveData(fatInternalData *fatData, unsigned oldStartSector,
	unsigned newStartSector, unsigned numSectors, progress *prog)
{
	// Move sectors from one place to another.  Created for the resizing
	// function.

	int status = 0;
	int moveLeft = 0;
	unsigned sectorsPerOp = 0;
	unsigned srcSector = 0;
	unsigned destSector = 0;
	unsigned char *buffer = NULL;
	int startSeconds = 0;
	int remainingSeconds = 0;

	// Need to check this because we divide by it
	if (!numSectors)
	{
		kernelDebugError("numSectors is 0");
		return (status = ERR_NULLPARAMETER);
	}

	// Which direction?
	if (newStartSector < oldStartSector)
		moveLeft = 1;

	sectorsPerOp = numSectors;

	// Cap the sectorsPerOp at 1MB
	if ((sectorsPerOp * fatData->bpb.bytesPerSect) > 1048576)
		sectorsPerOp = (1048576 / fatData->bpb.bytesPerSect);

	if (moveLeft && ((oldStartSector - newStartSector) < sectorsPerOp))
		sectorsPerOp = (oldStartSector - newStartSector);
	else if (!moveLeft && ((newStartSector - oldStartSector) < sectorsPerOp))
		sectorsPerOp = (newStartSector - oldStartSector);

	if (moveLeft)
	{
		srcSector = oldStartSector;
		destSector = newStartSector;
	}
	else
	{
		srcSector = (oldStartSector + (numSectors - sectorsPerOp));
		destSector = (newStartSector + (numSectors - sectorsPerOp));
	}

	// Get a memory buffer to copy data to/from
	buffer = kernelMalloc(sectorsPerOp * fatData->bpb.bytesPerSect);
	if (!buffer)
		return (status = ERR_MEMORY);

	if (prog && (kernelLockGet(&prog->progLock) >= 0))
	{
		memset((void *) prog, 0, sizeof(progress));
		prog->numTotal = numSectors;
		snprintf((char *) prog->statusMessage, PROGRESS_MAX_MESSAGELEN,
			"Moving %u MB: ?? hours ?? minutes", (unsigned)(prog->numTotal /
				(1048576 / fatData->bpb.bytesPerSect)));
	}

	startSeconds = kernelRtcUptimeSeconds();

	// Copy the data
	while (numSectors > 0)
	{
		// Read from source
		status = kernelDiskReadSectors((char *) fatData->disk->name, srcSector,
			sectorsPerOp, buffer);
		if (status < 0)
			goto out;

		// Write to destination
		status = kernelDiskWriteSectors((char *) fatData->disk->name,
			destSector, sectorsPerOp, buffer);
		if (status < 0)
			goto out;

		if (prog && (kernelLockGet(&prog->progLock) >= 0))
		{
			prog->numFinished += sectorsPerOp;
			if (prog->numTotal >= 100)
			{
				prog->percentFinished = (prog->numFinished /
					(prog->numTotal / 100));
			}
			else
			{
				prog->percentFinished = ((prog->numFinished * 100) /
					prog->numTotal);
			}

			remainingSeconds = (((kernelRtcUptimeSeconds() - startSeconds) *
				(numSectors / sectorsPerOp)) /
					(prog->numFinished / sectorsPerOp));

			snprintf((char *) prog->statusMessage, PROGRESS_MAX_MESSAGELEN,
				"Moving %u MB: ", (unsigned)(prog->numTotal /
					(1048576 / fatData->bpb.bytesPerSect)));

			if (remainingSeconds >= 7200)
			{
				sprintf(((char *) prog->statusMessage +
					strlen((char *) prog->statusMessage)), "%d hours ",
					(remainingSeconds / 3600));
			}
			else if (remainingSeconds > 3600)
			{
				sprintf(((char *) prog->statusMessage +
					strlen((char *) prog->statusMessage)), "1 hour ");
			}

			if (remainingSeconds >= 60)
			{
				sprintf(((char *) prog->statusMessage +
					strlen((char *) prog->statusMessage)), "%d minutes",
					((remainingSeconds % 3600) / 60));
			}
			else
			{
				sprintf(((char *) prog->statusMessage +
					strlen((char *) prog->statusMessage)),
					"less than 1 minute");
			}

			kernelLockRelease(&prog->progLock);
		}

		numSectors -= sectorsPerOp;

		if (moveLeft)
		{
			srcSector += sectorsPerOp;
			destSector += sectorsPerOp;

			if (numSectors < sectorsPerOp)
				sectorsPerOp = numSectors;
		}
		else
		{
			if (numSectors < sectorsPerOp)
				sectorsPerOp = numSectors;

			srcSector -= sectorsPerOp;
			destSector -= sectorsPerOp;
		}
	}

	// Success
	status = 0;

out:
	// Release memory
	kernelFree(buffer);

	return (status);
}


static int markFsClean(fatInternalData *fatData, int clean)
{
	int wasClean = 0;
	unsigned tmp;

	#define CLEAN_FAT12 0x0800 // Fake
	#define CLEAN_FAT16 0x8000
	#define CLEAN_FAT32 0x08000000

	getFatEntries(fatData, 1, 1, &tmp);

	if (((fatData->fsType == fat12) && (tmp & CLEAN_FAT12)) ||
		((fatData->fsType == fat16) && (tmp & CLEAN_FAT16)) ||
		((fatData->fsType == fat32) && (tmp & CLEAN_FAT32)))
	{
		wasClean = 1;
	}

	// Don't try to mark it if read-only.
	if (fatData->disk->physical->flags & DISKFLAG_READONLY)
		return (wasClean);

	if (clean)
	{
		if (fatData->fsType == fat12)
			setFatEntry(fatData, 1, (tmp | CLEAN_FAT12));
		else if (fatData->fsType == fat16)
			setFatEntry(fatData, 1, (tmp | CLEAN_FAT16));
		else if (fatData->fsType == fat32)
			setFatEntry(fatData, 1, (tmp | CLEAN_FAT32));
	}
	else
	{
		if (fatData->fsType == fat12)
			setFatEntry(fatData, 1, (tmp & ~CLEAN_FAT12));
		else if (fatData->fsType == fat16)
			setFatEntry(fatData, 1, (tmp & ~CLEAN_FAT16));
		else if (fatData->fsType == fat32)
			setFatEntry(fatData, 1, (tmp & ~CLEAN_FAT32));
	}

	return (wasClean);
}


static int checkFilename(volatile char *fileName)
{
	// Ensure that new file names are legal in the FAT filesystem.  It scans
	// the filename string looking for anything illegal.

	int fileNameOK = 0;
	int nameLength = 0;
	int count;

	// Get the length of the filename
	nameLength = strlen((char *) fileName);

	// Make sure the length of the filename is under the limit
	if (nameLength > MAX_NAME_LENGTH)
	{
		kernelError(kernel_error, "File name is too long");
		return (fileNameOK = ERR_BOUNDS);
	}

	// Make sure there's not a ' ' in the first position
	if (fileName[0] == (char) 0x20)
	{
		kernelError(kernel_error, "File name cannot start with ' '");
		return (fileNameOK = ERR_INVALID);
	}

	// Loop through the entire filename, looking for any illegal
	// characters
	for (count = 0; count < nameLength; count ++)
	{
		if ((fileName[count] == (char) 0x22) ||
			(fileName[count] == (char) 0x2a) ||
			(fileName[count] == (char) 0x2f) ||
			(fileName[count] == (char) 0x3a) ||
			(fileName[count] == (char) 0x3c) ||
			(fileName[count] == (char) 0x3e) ||
			(fileName[count] == (char) 0x3f) ||
			(fileName[count] == (char) 0x5c) ||
			(fileName[count] == (char) 0x7c))
		{
			kernelError(kernel_error, "Invalid character '%c' in file name",
				fileName[count]);
			fileNameOK = ERR_INVALID;
			break;
		}
	}

	return (fileNameOK = 0);
}


static char xlateShortAliasChar(char theChar)
{
	// Translate characters from long filenames into characters valid for
	// short aliases

	char returnChar = NULL;

	if (theChar < (char) 0x20)
	{
		// Unprintable control characters turn into '_'
		return (returnChar = '_');
	}
	else if ((theChar == '"') || (theChar == '*') || (theChar == '+') ||
		(theChar == ',') || (theChar == '/') || (theChar == ':') ||
		(theChar == ';') || (theChar == '<') || (theChar == '=') ||
		(theChar == '>') || (theChar == '?') || (theChar == '[') ||
		(theChar == '\\') || (theChar == ']') || (theChar == '|'))
	{
		// Likewise, these illegal characters turn into '_'
		return (returnChar = '_');
	}
	else if ((theChar >= (char) 0x61) && (theChar <= (char) 0x7A))
	{
		// Capitalize any lowercase alphabetical characters
		return (returnChar = (theChar - (char) 0x20));
	}
	else
	{
		// Everything else is okay
		return (returnChar = theChar);
	}
}


static int makeShortAlias(kernelFileEntry *theFile)
{
	// Create the short filename alias in a file structure.

	int status = 0;
	fatEntryData *entryData = NULL;
	kernelFileEntry *listItemPointer = NULL;
	fatEntryData *listItemData = NULL;
	char nameCopy[MAX_NAME_LENGTH];
	char aliasName[9];
	char aliasExt[4];
	unsigned lastDot = 0;
	int shortened = 0;
	int tildeSpot = 0;
	int tildeNumber = 1;
	unsigned count;

	// Get the entry's data
	entryData = (fatEntryData *) theFile->driverData;
	if (!entryData)
	{
		kernelError(kernel_error, "File has no private filesystem data");
		return (status = ERR_BUG);
	}

	// The short alias field of the file structure is a 13-byte field with
	// room for the 8 filename characters, a '.', the 3 extension characters,
	// and a NULL.  It must be in all capital letters, and there is a
	// particular shortening algorithm used.  This algorithm is defined in the
	// Microsoft FAT Filesystem Specification.

	// Initialize the shortAlias name and extension, since they both need to
	// be padded with [SPACE] characters

	strcpy(aliasName, "        ");
	strcpy(aliasExt, "   ");

	// Now, this is a little tricky.  We have to examine the first six
	// characters of the long filename, and interpret them according to the
	// predefined format.
	// - All capitals
	// - There are a bunch of illegal characters we have to check for
	// - Illegal characters are replaced with underscores
	// - "~#" is placed in the last two slots of the filename, if either the
	//   name or extension has been shortened.  # is based on the number of
	//   files in the same directory that conflict.  We have to search the
	//   directory for name clashes of this sort
	// - The file extension (at least the "last" file extension) is kept,
	//   except that it is truncated to 3 characters if necessary.

	// Loop through the file name, translating characters to ones that are
	// legal for short aliases, and put them into a copy of the file name
	// Make a copy of the name
	memset(nameCopy, 0, MAX_NAME_LENGTH);
	int tmpCount = 0;
	for (count = 0; count < (MAX_NAME_LENGTH - 1); count ++)
	{
		if (theFile->name[count] == 0x20)
			// Skip spaces
			continue;

		if (theFile->name[count] == '\0')
		{
			// Finished
			nameCopy[tmpCount] = '\0';
			break;
		}
		else
		{
			// Copy the character, but make sure it's legal.
			nameCopy[tmpCount++] = xlateShortAliasChar(theFile->name[count]);
		}
	}

	// Find the last '.'  This will determine what makes up the name and what
	// makes up the extension.
	for (count = (strlen(nameCopy) - 1); count > 0; count --)
	if (nameCopy[count] == '.')
	{
		lastDot = count;
		break;
	}

	if (!lastDot)
	{
		// No extension.  Just copy the name up to 8 chars.
		for (count = 0; ((count < 8) && (count < strlen(nameCopy))); count ++)
			aliasName[count] = nameCopy[count];

		lastDot = strlen(nameCopy);

		if (strlen(nameCopy) > 8)
			shortened = 1;
	}
	else
	{
		// Now, if we actually found a '.' in something other than the first
		// spot...

		// Copy the name up to 8 chars.
		for (count = 0; ((count < 8) && (count < lastDot) &&
			(count < strlen(nameCopy))); count ++)
		{
			aliasName[count] = nameCopy[count];
		}

		if (lastDot > 7)
			shortened = 1;

		// There's an extension.  Copy it.
		for (count = 0; ((count < 3) &&
			(count < strlen(nameCopy + lastDot + 1))); count ++)
		{
			aliasExt[count] = nameCopy[lastDot + 1 + count];
		}

		if (strlen(nameCopy + lastDot + 1) > 3)
			// We are shortening the extension part
			shortened = 1;
	}

	tildeSpot = lastDot;
	if (tildeSpot > 6)
		tildeSpot = 6;

	// If we shortened anything, we have to stick that goofy tilde thing on
	// the end.  Yay for Microsoft; that's a very innovative solution to the
	// long filename problem.  Isn't that innovative?  Innovating is an
	// important way to innovate the innovation -- that's what I always say.
	// Microsoft is innovating into the future of innovation.  (That's 7 points
	// for me, plus I get an extra one for using four different forms of the
	// word)
	if (shortened)
	{
		// We start with this default configuration before we go looking for
		// conflicts
		aliasName[tildeSpot] = '~';
		aliasName[tildeSpot + 1] = '1';
	}

	// Concatenate the name and extension
	strncpy((char *) entryData->shortAlias, aliasName, 8);
	strncpy((char *)(entryData->shortAlias + 8), aliasExt, 3);

	// Make sure there aren't any name conflicts in the file's directory
	listItemPointer = theFile->parentDirectory->contents;

	while (listItemPointer)
	{
		if (listItemPointer->disk != theFile->disk)
		{
			listItemPointer = listItemPointer->nextEntry;
			continue;
		}

		if (listItemPointer != theFile)
		{
			// Get the list item's data
			listItemData = (fatEntryData *) listItemPointer->driverData;
			if (!listItemData)
			{
				kernelError(kernel_error, "File \"%s\" has no private "
					"filesystem data", listItemPointer->name);
				return (status = ERR_BUG);
			}

			if (!strcmp((char *) listItemData->shortAlias,
				(char *) entryData->shortAlias))
			{
				// Conflict.  Up the ~# thing we're using

				tildeNumber += 1;
				if (tildeNumber >= 100)
				{
					// Too many name clashes
					kernelError(kernel_error, "Too many short alias name "
						"clashes");
					return (status = ERR_NOFREE);
				}

				if (tildeNumber >= 10)
				{
					entryData->shortAlias[tildeSpot - 1] = '~';
					entryData->shortAlias[tildeSpot] =
						((char) 48 + (char)(tildeNumber / 10));
				}

				entryData->shortAlias[tildeSpot + 1] =
					((char) 48 + (char)(tildeNumber % 10));

				listItemPointer = theFile->parentDirectory->contents;
				continue;
			}
		}

		listItemPointer = listItemPointer->nextEntry;
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
	// Determine whether the data on a disk structure is using a FAT
	// filesystem.  It uses a simple test or two to determine simply whether
	// this is a FAT volume.  Any data that it gathers is discarded when the
	// call terminates.

	int status = 0;
	fatBPB bpb;
	fatInternalData *fatData = NULL;

	if (!initialized)
		return (status = ERR_NOTINITIALIZED);

	// Check params
	if (!theDisk)
	{
		kernelError(kernel_error, "NULL parameter");
		return (status = ERR_NULLPARAMETER);
	}

	// We can start by reading the first sector of the volume (the "boot
	// sector").

	status = readBootSector(theDisk, &bpb);
	if (status < 0)
		// Couldn't read the boot sector, or it was bad
		return (status);

	// It MUST be true that the signature word MSDOS_BOOT_SIGNATURE occurs at
	// offset 510 of the boot sector (regardless of the sector size of this
	// device).  If it does not, then this is not only NOT a FAT boot sector,
	// but may not be a valid boot sector at all.
	if (bpb.signature != MSDOS_BOOT_SIGNATURE)
		return (status = 0);

	// What if we cannot be sure that this is a FAT filesystem?  In the
	// interest of data integrity, we will decline the invitation to use this
	// as FAT.  It must pass a few tests here.

	// The bytes-per-sector field may only contain one of the following values:
	// 512, 1024, 2048 or 4096.  Anything else is illegal according to MS.
	// 512 is almost always the value found here.
	if ((bpb.bytesPerSect != 512) && (bpb.bytesPerSect != 1024) &&
		(bpb.bytesPerSect != 2048) && (bpb.bytesPerSect != 4096))
	{
		// Not a legal value for FAT
		return (status = 0);
	}

	// Check the media type byte.  There are only a small number of legal
	// values that can occur here (so it's a reasonabe test to determine
	// whether this is a FAT)
	if ((bpb.media < 0xF8) && (bpb.media != 0xF0))
		// Not a legal value for FAT
		return (status = 0);

	// Look for the extended boot block signatures.  If we find them, we
	// should be able to find the substring "FAT" in the first 3 bytes of
	// the fileSysType field.
	if (((bpb.fat.bootSig == 0x29) &&
			(strncmp(bpb.fat.fileSysType, "FAT", 3))) ||
		((bpb.fat32.bootSig == 0x29) &&
			(strncmp(bpb.fat32.fileSysType, "FAT", 3))))
	{
		// We will say this is not a FAT filesystem.  We might be wrong,
		// but we can't be sure otherwise.
		return (status = 0);
	}

	// NTFS $Boot files look a lot like FAT, and we in fact can reach
	// this point without failing.  Check for NTFS signature.
	if (!strncmp(bpb.oemName, "NTFS    ", 8))
		// Maybe NTFS
		return (status = 0);

	// We will accept this as a FAT filesystem.  This will set some more
	// information on the disk structure

	fatData = getFatData(theDisk);
	if (!fatData)
		return (status = 0);

	freeFatData(theDisk);

	return (status = 1);
}


static int format(kernelDisk *theDisk, const char *type, const char *label,
	int longFormat, progress *prog)
{
	// Format the supplied disk as a FAT volume.

	int status = 0;
	unsigned clearSectors = 0;
	unsigned doSectors = 0;
	unsigned char *sectorBuff = NULL;
	fatInternalData fatData;
	unsigned count;
	#define BUFFERSIZE 1048576

	// For calculating cluster sizes, below.
	typedef struct {
		unsigned diskSize;
		unsigned secPerClust;
	} sizeToSecsPerClust;

	sizeToSecsPerClust f16Tab[] = {
		{ 32680, 2 },		// Up to 16M, 1K cluster
		{ 262144, 4 },		// Up to 128M, 2K cluster
		{ 524288, 8 },		// Up to 256M, 4K cluster
		{ 1048576, 16 },	// Up to 512M, 8K cluster
		{ 2097152, 32 },	// Up to 1G, 16K cluster
		{ 4194304, 64 },	// Up to 2G, 32K cluster
		{ 0xFFFFFFFF, 64 }	// Above 2G, 32K cluster
	};

	sizeToSecsPerClust f32Tab[] = {
		{ 532480, 1 },		// Up to 260M, .5K cluster
		{ 16777216, 8 },	// Up to 8G, 4K cluster
		{ 33554432, 16 },	// Up to 16G, 8K cluster
		{ 67108864, 32 },	// Up to 32G, 16K cluster
		{ 0xFFFFFFFF, 64 }	// Above 32G, 32K cluster
	};

	if (!initialized)
	{
		status = ERR_NOTINITIALIZED;
		goto out;
	}

	kernelDebug(debug_fs, "FAT formatting disk %s", theDisk->name);

	// Check params
	if (!theDisk || !type || !label)
	{
		status = ERR_NULLPARAMETER;
		goto out;
	}

	// Only format a disk with 512-byte sectors
	if (theDisk->physical->sectorSize != 512)
	{
		kernelError(kernel_error, "Cannot format a disk with sector size of "
			"%u (512 only)", theDisk->physical->sectorSize);
		status = ERR_INVALID;
		goto out;
	}

	// Clear out our new FAT data structure
	memset((void *) &fatData, 0, sizeof(fatInternalData));

	if (prog && (kernelLockGet(&prog->progLock) >= 0))
	{
		strcpy((char *) prog->statusMessage, "Calculating parameters");
		kernelLockRelease(&prog->progLock);
	}

	// Set the disk structure
	fatData.disk = theDisk;

	fatData.bpb.jmpBoot[0] = 0xEB;	// JMP inst
	fatData.bpb.jmpBoot[1] = 0x3C;	// JMP inst
	fatData.bpb.jmpBoot[2] = 0x90;	// No op
	strncpy((char *) fatData.bpb.oemName, "Visopsys", 8);
	fatData.bpb.sectsPerTrack = theDisk->physical->sectorsPerCylinder;
	fatData.bpb.numHeads = theDisk->physical->heads;
	fatData.bpb.bytesPerSect = theDisk->physical->sectorSize;
	fatData.bpb.hiddenSects = 0;
	fatData.bpb.numFats = 2;
	fatData.totalSects = theDisk->numSectors;
	fatData.bpb.sectsPerClust = 1;

	if (theDisk->physical->type & DISKTYPE_FIXED)
		fatData.bpb.media = 0xF8;
	else
		fatData.bpb.media = 0xF0;

	if (!strncasecmp(type, FSNAME_FAT"12", 5))
	{
		fatData.fsType = fat12;
	}
	else if (!strncasecmp(type, FSNAME_FAT"16", 5))
	{
		fatData.fsType = fat16;
	}
	else if (!strncasecmp(type, FSNAME_FAT"32", 5))
	{
		fatData.fsType = fat32;
	}
	else if ((theDisk->physical->type & DISKTYPE_FLOPPY) ||
		(fatData.totalSects < 8400))
	{
		fatData.fsType = fat12;
	}
	else if (fatData.totalSects < 66600)
	{
		fatData.fsType = fat16;
	}
	else
	{
		fatData.fsType = fat32;
	}

	if ((fatData.fsType == fat12) || (fatData.fsType == fat16))
	{
		// FAT12 or FAT16

		fatData.bpb.rsvdSectCount = 1;

		if (fatData.fsType == fat12)
		{
			// FAT12

			fatData.bpb.rootEntCount = 224;

			// Calculate the sectors per cluster.
			while ((theDisk->numSectors / fatData.bpb.sectsPerClust) >= 4085)
			{
				if (fatData.bpb.sectsPerClust >= 64)
				{
					// We cannot create a volume this size using FAT12
					char *tmpMessage =
						"Disk is too large for a FAT12 filesystem";
					kernelError(kernel_error, "%s", tmpMessage);
					progressConfirmError(prog, tmpMessage);
					status = ERR_BOUNDS;
					goto out;
				}

				fatData.bpb.sectsPerClust *= 2;
			}

			fatData.terminalClust = 0x0FF8;
			strncpy((char *) fatData.bpb.fat.fileSysType, "FAT12   ", 8);
		}
		else
		{
			// FAT16

			fatData.bpb.rootEntCount = 512;

			// Calculate the sectors per cluster based on a Microsoft table
			for (count = 0; ; count ++)
			{
				if (f16Tab[count].diskSize >= fatData.totalSects)
				{
					fatData.bpb.sectsPerClust = f16Tab[count].secPerClust;
					break;
				}
			}

			fatData.terminalClust = 0xFFF8;
			strncpy((char *) fatData.bpb.fat.fileSysType, "FAT16   ", 8);
		}
	}
	else if (fatData.fsType == fat32)
	{
		fatData.bpb.rsvdSectCount = 32;
		fatData.bpb.rootEntCount = 0;

		// Calculate the sectors per cluster based on a Microsoft table
		for (count = 0; ; count ++)
		if (f32Tab[count].diskSize >= fatData.totalSects)
		{
			fatData.bpb.sectsPerClust = f32Tab[count].secPerClust;
			break;
		}

		fatData.terminalClust = 0x0FFFFFF8;
		strncpy((char *) fatData.bpb.fat32.fileSysType, "FAT32   ", 8);
	}

	if (theDisk->physical->type & DISKTYPE_FLOPPY)
	{
		fatData.rootDirSects = 14;
		fatData.fatSects = 9;
	}
	else
	{
		fatData.rootDirSects =
			(((FAT_BYTES_PER_DIR_ENTRY * fatData.bpb.rootEntCount) +
				(fatData.bpb.bytesPerSect - 1)) / fatData.bpb.bytesPerSect);

		// Calculate the number of FAT sectors
		fatData.fatSects = calcFatSects(&fatData, fatData.totalSects);
	}

	fatData.dataSects = (fatData.totalSects - (fatData.bpb.rsvdSectCount +
		(fatData.bpb.numFats * fatData.fatSects) + fatData.rootDirSects));
	fatData.dataClusters = (fatData.dataSects / fatData.bpb.sectsPerClust);
	fatData.freeClusters = fatData.dataClusters;

	if ((fatData.fsType == fat12) || (fatData.fsType == fat16))
	{
		fatData.bpb.fat.biosDriveNum = theDisk->physical->deviceNumber;
		if (theDisk->physical->type & DISKTYPE_FIXED)
			fatData.bpb.fat.biosDriveNum |= 0x80;
		fatData.bpb.fat.bootSig = 0x29; // Volume id, label, etc., valid
		fatData.bpb.fat.volumeId = kernelSysTimerRead();
		strncpy((char *) fatData.bpb.fat.volumeLabel, label, FAT_8_3_NAME_LEN);
		for (count = strlen((char *) fatData.bpb.fat.volumeLabel);
			count < FAT_8_3_NAME_LEN; count ++)
		fatData.bpb.fat.volumeLabel[count] = ' ';
	}
	else if (fatData.fsType == fat32)
	{
		fatData.bpb.fat32.biosDriveNum = theDisk->physical->deviceNumber;
		if (theDisk->physical->type & DISKTYPE_FIXED)
			fatData.bpb.fat32.biosDriveNum |= 0x80;
		fatData.bpb.fat32.bootSig = 0x29; // Volume id, label, etc., valid
		fatData.bpb.fat32.volumeId = kernelSysTimerRead();
		strncpy((char *) fatData.bpb.fat32.volumeLabel, label,
			FAT_8_3_NAME_LEN);
		for (count = strlen((char *) fatData.bpb.fat32.volumeLabel);
			count < FAT_8_3_NAME_LEN; count ++)
		fatData.bpb.fat32.volumeLabel[count] = ' ';
	}

	fatData.bpb.signature = MSDOS_BOOT_SIGNATURE;

	// Get a decent-sized empty buffer for clearing sectors
	sectorBuff = kernelMalloc(BUFFERSIZE);
	if (!sectorBuff)
	{
		kernelError(kernel_error, "Unable to allocate sector data memory");
		status = ERR_MEMORY;
		goto out;
	}

	// How many empty sectors to write?  If we are doing a long format, write
	// every sector.  Otherwise, just the system areas
	if (longFormat)
		clearSectors = fatData.totalSects;
	else
		clearSectors = (fatData.bpb.rsvdSectCount +
			(fatData.bpb.numFats * fatData.fatSects) + fatData.rootDirSects);

	if (prog && (kernelLockGet(&prog->progLock) >= 0))
	{
		strcpy((char *) prog->statusMessage, "Clearing control sectors");
		kernelLockRelease(&prog->progLock);
	}

	for (count = 0; count < clearSectors; )
	{
		doSectors = min((clearSectors - count), (unsigned)(BUFFERSIZE /
			fatData.bpb.bytesPerSect));

		status = kernelDiskWriteSectors((char *) theDisk->name, count,
			doSectors, sectorBuff);
		if (status < 0)
		{
			kernelFree(sectorBuff);
			goto out;
		}

		count += doSectors;

		if (prog && (prog->percentFinished < 70) &&
			(kernelLockGet(&prog->progLock) >= 0))
		{
			prog->percentFinished = ((count * 100) / clearSectors);
			kernelLockRelease(&prog->progLock);
		}
	}

	if (prog && (kernelLockGet(&prog->progLock) >= 0))
	{
		prog->percentFinished = 80;
		strcpy((char *) prog->statusMessage, "Writing FATs");
		kernelLockRelease(&prog->progLock);
	}

	// Set first two FAT table entries
	if (fatData.fsType == fat12)
	{
		status = setFatEntry(&fatData, 0, (0x0F00 | fatData.bpb.media));
		if (status >= 0)
			status = setFatEntry(&fatData, 1, 0x0FFF);
	}
	else if (fatData.fsType == fat16)
	{
		status = setFatEntry(&fatData, 0, (0xFF00 | fatData.bpb.media));
		if (status >= 0)
			status = setFatEntry(&fatData, 1, 0xFFFF);
	}
	else if (fatData.fsType == fat32)
	{
		status = setFatEntry(&fatData, 0, (0x0FFFFF00 | fatData.bpb.media));
		if (status >= 0)
			status = setFatEntry(&fatData, 1, 0x0FFFFFFF);
	}
	if (status < 0)
	{
		kernelDebugError("Error writing FAT entries");
		goto out;
	}

	if (fatData.fsType == fat32)
	{
		// These fields are specific to the FAT32 filesystem type, and
		// are not applicable to FAT12 or FAT16
		fatData.bpb.fat32.rootClust = 2;
		fatData.bpb.fat32.fsInfo = 1;
		fatData.bpb.fat32.backupBootSect = 6;
		fatData.fsInfo.leadSig = 0x41615252;
		fatData.fsInfo.structSig = 0x61417272;
		fatData.fsInfo.nextFree = 3;
		fatData.fsInfo.trailSig = 0xAA550000;

		// Write an empty root directory cluster
		status = kernelDiskWriteSectors((char *) theDisk->name,
			(fatData.bpb.rsvdSectCount + (fatData.bpb.numFats *
				fatData.fatSects)), fatData.bpb.sectsPerClust, sectorBuff);
		if (status < 0)
		{
			kernelFree(sectorBuff);
			goto out;
		}

		// Used one cluster for the root directory.
		fatData.freeClusters -= 1;

		// Mark the root directory cluster as used in the FAT
		status = setFatEntry(&fatData, fatData.bpb.fat32.rootClust,
			fatData.terminalClust);
		if (status < 0)
		{
			kernelFree(sectorBuff);
			goto out;
		}
	}

	kernelFree(sectorBuff);

	if (prog && (kernelLockGet(&prog->progLock) >= 0))
	{
		prog->percentFinished = 85;
		strcpy((char *) prog->statusMessage, "Writing volume info");
		kernelLockRelease(&prog->progLock);
	}

	status = writeVolumeInfo(&fatData);
	if (status < 0)
	{
		kernelDebugError("Error writing volume info");
		goto out;
	}

	if (fatData.fsType == fat32)
	{
		status = writeFSInfo(&fatData);
		if (status < 0)
		{
			kernelDebugError("Error writing filesystem info block");
			goto out;
		}
	}

	if (prog && (kernelLockGet(&prog->progLock) >= 0))
	{
		prog->percentFinished = 90;
		kernelLockRelease(&prog->progLock);
	}

	// Set the proper filesystem type name on the disk structure
	switch (fatData.fsType)
	{
		case fat12:
			strcpy((char *) theDisk->fsType, FSNAME_FAT"12");
			break;

		case fat16:
			strcpy((char *) theDisk->fsType, FSNAME_FAT"16");
			break;

		case fat32:
			strcpy((char *) theDisk->fsType, FSNAME_FAT"32");
			break;

		default:
			strcpy((char *) theDisk->fsType, FSNAME_FAT);
	}

	if (prog && (kernelLockGet(&prog->progLock) >= 0))
	{
		prog->percentFinished = 95;
		strcpy((char *) prog->statusMessage, "Syncing disk");
		kernelLockRelease(&prog->progLock);
	}

	kernelLog("Format: Type: %s  Total Sectors: %u  Bytes Per Sector: "
		"%u  Sectors Per Cluster: %u  Root Directory Sectors: "
		"%u  Fat Sectors: %u  Data Clusters: %u", theDisk->fsType,
		fatData.totalSects, fatData.bpb.bytesPerSect,
		fatData.bpb.sectsPerClust, fatData.rootDirSects,
		fatData.fatSects, fatData.dataClusters);

	status = 0;

out:
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
	// detected as having an FAT filesystem.

	int status = 0;
	fatBPB bpb;

	if (!initialized)
		return (status = ERR_NOTINITIALIZED);

	kernelDebug(debug_fs, "FAT clobbering disk %s", theDisk->name);

	// Check params.
	if (!theDisk)
	{
		kernelError(kernel_error, "NULL parameter");
		return (status = ERR_NULLPARAMETER);
	}

	status = readBootSector(theDisk, &bpb);
	if (status < 0)
		return (status);

	// In the case of FAT, we clear out the 'fileSysType' fields for both FAT
	// and FAT32, and remove the MSDOS_BOOT_SIGNATURE signature
	strncpy(bpb.fat.fileSysType, "        ", 8);
	strncpy(bpb.fat32.fileSysType, "        ", 8);
	bpb.signature = 0;

	status = kernelDiskWriteSectors((char *) theDisk->name, 0, 1, &bpb);
	return (status);
}


static int defragment(kernelDisk *theDisk, progress *prog)
{
	// This function defragments a FAT filesystem.  It's pretty primitive in
	// that it simply re-writes files that are fragmented, which will tend to
	// consolidate them given our cluster allocation algorithm.

	int status = 0;
	fatInternalData *fatData = NULL;

	// Check params
	if (!theDisk)
	{
		kernelError(kernel_error, "NULL parameter");
		status = ERR_NULLPARAMETER;
		goto out;
	}

	if (prog && (kernelLockGet(&prog->progLock) >= 0))
	{
		strcpy((char *) prog->statusMessage, _("Reading filesystem info"));
		kernelLockRelease(&prog->progLock);
	}

	fatData = getFatData(theDisk);
	if (!fatData)
	{
		status = ERR_BADDATA;
		goto out;
	}

	// Build the free-cluster list
	status = makeFreeBitmap(fatData);
	if (status < 0)
	{
		kernelDebugError("Unable to create the free cluster bitmap");
		goto out;
	}

	// Get a new file entry for the filesystem's root directory
	theDisk->filesystem.filesystemRoot = kernelFileNewEntry(theDisk);
	if (!theDisk->filesystem.filesystemRoot)
	{
		// Not enough free file structures
		status = ERR_NOFREE;
		goto out;
	}

	strcpy((char *) theDisk->filesystem.filesystemRoot->name, "/");
	theDisk->filesystem.filesystemRoot->type = dirT;
	theDisk->filesystem.filesystemRoot->disk = theDisk;

	status = readRootDir(fatData, theDisk);
	if (status < 0)
		goto out;

	if (prog && (kernelLockGet(&prog->progLock) >= 0))
	{
		snprintf((char *) prog->statusMessage, PROGRESS_MAX_MESSAGELEN,
			"%s", _("Analyzing"));
		kernelLockRelease(&prog->progLock);
	}

	// Check fragmentation
	status = defragRecursive(fatData, theDisk->filesystem.filesystemRoot, 1,
		prog);
	if (status < 0)
		goto out;

	if (prog && (kernelLockGet(&prog->progLock) >= 0))
	{
		prog->numTotal = status;
		snprintf((char *) prog->statusMessage, PROGRESS_MAX_MESSAGELEN,
			_("%llu files need defragmentation"), prog->numTotal);
		kernelLockRelease(&prog->progLock);

		if (!prog->numTotal)
		{
			status = 0;
			goto out;
		}
	}

	// Do the actual defrag
	status = defragRecursive(fatData, theDisk->filesystem.filesystemRoot, 0,
		prog);
	if (status < 0)
		goto out;

	// Unbuffer all of the files
	kernelFileUnbufferRecursive(theDisk->filesystem.filesystemRoot);

	// If this is a FAT32 filesystem, we need to flush the extended filesystem
	// data back to the FSInfo block
	if (fatData->fsType == fat32)
	{
		status = writeFSInfo(fatData);
		if (status < 0)
			goto out;
	}

	status = 0;

out:
	freeFatData(theDisk);

	if (prog && (kernelLockGet(&prog->progLock) >= 0))
	{
		prog->complete = 1;
		kernelLockRelease(&prog->progLock);
	}

	return (status);
}


static uquad_t getFreeBytes(kernelDisk *theDisk)
{
	// This function returns the amount of free disk space, in bytes.

	fatInternalData *fatData = NULL;

	if (!initialized)
		return (0);

	// Check params
	if (!theDisk)
	{
		kernelError(kernel_error, "NULL parameter");
		return (0);
	}

	// Get the FAT data for the requested filesystem
	fatData = getFatData(theDisk);
	if (!fatData)
		return (0);

	// Is the free cluster bitmap being generated?
	if (makingFatFree == fatData)
	{
		// If this is a FAT32 filesystem, we can cheat by passing the value
		// from the fsInfo structure
		if ((fatData->fsType == fat32) &&
			(fatData->fsInfo.freeCount != 0xFFFFFFFF))
		{
			return ((uquad_t) fatData->fsInfo.freeCount *
				fatClusterBytes(fatData));
		}
		else
		{
			// Otherwise we have to wait until we've got the data
			kernelMultitaskerBlock(makeFatFreePid);
		}
	}

	return ((uquad_t) fatData->freeClusters * fatClusterBytes(fatData));
}


static int resizeConstraints(kernelDisk *theDisk, uquad_t *minBlocks,
	uquad_t *maxBlocks, progress *prog)
{
	// Return the minimum and maximum numbers of sectors we can have for this
	// volume.

	int status = 0;
	fatInternalData *fatData = NULL;
	unsigned minDataSects = 0;
	unsigned minFatSects = 0;
	unsigned minSysSects = 0;
	uquad_t maxDataSects = 0;
	unsigned maxFatSects = 0;
	unsigned maxSysSects = 0;

	// Check params
	if (!theDisk || !minBlocks || !maxBlocks)
	{
		kernelError(kernel_error, "NULL parameter");
		return (status = ERR_NULLPARAMETER);
	}

	if (prog && (kernelLockGet(&prog->progLock) >= 0))
	{
		strncpy((char *) prog->statusMessage, _("Checking constraints"),
			PROGRESS_MAX_MESSAGELEN);
		kernelLockRelease(&prog->progLock);
	}

	fatData = getFatData(theDisk);
	if (!fatData)
		return (status = ERR_BADDATA);

	// Build the free-cluster list
	status = makeFreeBitmap(fatData);
	if (status < 0)
	{
		kernelDebugError("Unable to create the free cluster bitmap");
		return (status);
	}

	// How many data sectors are currently used?
	minDataSects = (fatData->dataSects - (getFreeBytes(theDisk) /
		fatData->bpb.bytesPerSect));

	// How many FAT sectors do these require?
	minFatSects = calcFatSects(fatData, minDataSects);

	// Add up the minimum 'system' sectors
	minSysSects = (fatData->bpb.rsvdSectCount +
		(fatData->bpb.numFats * minFatSects) + fatData->rootDirSects);

	kernelDebug(debug_fs, "FAT minDataSects=%u minFatSects=%u minSysSects=%u",
		minDataSects, minFatSects, minSysSects);

	// The minimum value is the sum of the minimum 'system' sectors and the
	// number of used data sectors.
	*minBlocks = (minSysSects + minDataSects);

	// What is the maximum number of data sectors, based on the maximum cluster
	// number for the FAT size?
	maxDataSects = ((fatData->terminalClust - 2) * fatData->bpb.sectsPerClust);

	// How many FAT sectors would these require?
	maxFatSects = calcFatSects(fatData, maxDataSects);

	// Add up the maximum 'system' sectors
	maxSysSects = (fatData->bpb.rsvdSectCount +
		(fatData->bpb.numFats * maxFatSects) + fatData->rootDirSects);

	kernelDebug(debug_fs, "FAT maxDataSects=%llu maxFatSects=%u "
		"maxSysSects=%u", maxDataSects, maxFatSects, maxSysSects);

	// The maximum value is the sum of the maxiumum 'system' sectors and the
	// maximum number of data sectors.
	*maxBlocks = (maxSysSects + maxDataSects);

	freeFatData(theDisk);

	kernelDebug(debug_fs, "FAT minBlocks=%llu maxBlocks=%llu", *minBlocks,
		*maxBlocks);

	return (status = 0);
}


static int resize(kernelDisk *theDisk, uquad_t blocks, progress *prog)
{
	// Resize the filesystem.

	int status = 0;
	fatInternalData *fatData = NULL;
	uquad_t minBlocks, maxBlocks;
	unsigned lastUsedCluster = 0;
	unsigned newFatSects = 0;
	int diffFatSects = 0;
	uquad_t newDataSects = 0;
	quad_t diffDataSects = 0;
	unsigned char *buffer = NULL;
	char tmpMessage[160];
	int count;

	kernelDebug(debug_fs, "FAT resize to %llu blocks", blocks);

	// Check params
	if (!theDisk)
	{
		kernelError(kernel_error, "NULL parameter");
		return (status = ERR_NULLPARAMETER);
	}

	// Make sure that the new number of blocks isn't outside the permissable
	// range
	status = resizeConstraints(theDisk, &minBlocks, &maxBlocks, prog);
	if (status < 0)
	{
		progressConfirmError(prog, _("Error getting resizing constraints"));
		return (status);
	}

	if ((blocks < minBlocks) || (blocks > maxBlocks))
	{
		if (blocks < minBlocks)
			sprintf(tmpMessage, _("Filesystem cannot resize to %llu blocks "
				"(minimum is %llu)"), blocks, minBlocks);
		else
			sprintf(tmpMessage, _("Filesystem cannot resize to %llu blocks "
				"(maximum is %llu)"), blocks, maxBlocks);
		kernelError(kernel_error, "%s", tmpMessage);
		progressConfirmError(prog, tmpMessage);
		return (status = ERR_RANGE);
	}

	if (prog && (kernelLockGet(&prog->progLock) >= 0))
	{
		strncpy((char *) prog->statusMessage, _("Reading filesystem info"),
			PROGRESS_MAX_MESSAGELEN);
		kernelLockRelease(&prog->progLock);
	}

	fatData = getFatData(theDisk);
	if (!fatData)
	{
		progressConfirmError(prog, _("Error reading filesystem info"));
		return (status = ERR_BADDATA);
	}

	// Build the free-cluster list
	status = makeFreeBitmap(fatData);
	if (status < 0)
	{
		progressConfirmError(prog, _("Unable to create the free cluster "
			"bitmap"));
		return (status);
	}

	// Get the number of the last used cluster
	lastUsedCluster = getLastUsedCluster(fatData);

	// Calculate the new number of FAT sectors
	newFatSects = calcFatSects(fatData, blocks);

	// This number will be negative if the volume is shrinking
	diffFatSects = (newFatSects - fatData->fatSects);

	// Calculate some other new values
	newDataSects = (blocks - (fatData->bpb.rsvdSectCount +
		(fatData->bpb.numFats * newFatSects) + fatData->rootDirSects));

	// This number will be negative if the volume is shrinking
	diffDataSects = (newDataSects - fatData->dataSects);

	// If we're shrinking, is there data whose cluster number falls outside of
	// the new bound?
	if ((diffFatSects < 0) &&
		((fatClusterToLogical(fatData, lastUsedCluster) +
			fatData->bpb.sectsPerClust +
			(fatData->bpb.numFats * diffFatSects)) >= blocks))
	{
		// Defragment the filesystem

		if (prog && (kernelLockGet(&prog->progLock) >= 0))
		{
			strncpy((char *) prog->statusMessage, _("Defragmenting"),
				PROGRESS_MAX_MESSAGELEN);
			kernelLockRelease(&prog->progLock);
		}

		status = defragment(theDisk, NULL);
		if (status < 0)
		{
			progressConfirmError(prog, _("Error defragmenting filesystem"));
			return (status);
		}

		// Need to re-get the filesystem data since defragment will discard it.
		fatData = getFatData(theDisk);
		if (!fatData)
		{
			progressConfirmError(prog, _("Error reading filesystem info"));
			status = ERR_BADDATA;
			goto out;
		}

		// Build the free-cluster list
		status = makeFreeBitmap(fatData);
		if (status < 0)
		{
			progressConfirmError(prog, _("Unable to create the free cluster "
				"bitmap"));
			return (status);
		}

		// Get the number of the last used cluster again
		lastUsedCluster = getLastUsedCluster(fatData);

		if ((fatClusterToLogical(fatData, lastUsedCluster) +
			fatData->bpb.sectsPerClust +
			(fatData->bpb.numFats * diffFatSects)) >= blocks)
		{
			sprintf(tmpMessage,
				_("Data exists outside the new size (%llu >= %llu)"),
				(fatClusterToLogical(fatData, lastUsedCluster) +
					fatData->bpb.sectsPerClust +
					(fatData->bpb.numFats * diffFatSects)), blocks);
			kernelError(kernel_error, "%s", tmpMessage);
			progressConfirmError(prog, tmpMessage);
			status = ERR_NOTIMPLEMENTED;
			goto out;
		}
	}

	// If the new and old numbers of FAT sectors are different (likely) then
	// we have to move all of the used data left or right depending on whether
	// the volume is shrinking or expanding.
	if (diffFatSects)
	{
		unsigned oldStartSector = (fatData->bpb.rsvdSectCount +
			(fatData->bpb.numFats * fatData->fatSects));
		unsigned newStartSector = (fatData->bpb.rsvdSectCount +
			(fatData->bpb.numFats * newFatSects));
		unsigned moveSectors = (fatData->rootDirSects +
			((fatClusterToLogical(fatData, lastUsedCluster) +
				fatData->bpb.sectsPerClust) - oldStartSector));

		status = moveData(fatData, oldStartSector, newStartSector, moveSectors,
			prog);
		if (status < 0)
		{
			progressConfirmError(prog, _("Error moving data"));
			goto out;
		}

		// If we are expanding the filesystem, we need to clear the new
		// FAT sectors
		if (diffFatSects > 0)
		{
			if (prog && (kernelLockGet(&prog->progLock) >= 0))
			{
				strncpy((char *) prog->statusMessage,
					_("Clearing new FAT sectors"), PROGRESS_MAX_MESSAGELEN);
				kernelLockRelease(&prog->progLock);
			}

			buffer = kernelMalloc(fatData->bpb.bytesPerSect);
			if (!buffer)
			{
				progressConfirmError(prog, _("Error getting memory"));
				status = ERR_MEMORY;
				goto out;
			}

			for (count = 0; count < diffFatSects; count ++)
			{
				status = kernelDiskWriteSectors((char *) theDisk->name,
					(fatData->bpb.rsvdSectCount + fatData->fatSects + count),
					1, buffer);
				if (status < 0)
					break;
			}

			kernelFree(buffer);

			if (status < 0)
			{
				progressConfirmError(prog,
					_("Error clearing new FAT sectors"));
				goto out;
			}
		}

		// Now synch up the FAT copies
		for (count = 1; count < fatData->bpb.numFats; count ++)
		{
			status = moveData(fatData, fatData->bpb.rsvdSectCount,
				(fatData->bpb.rsvdSectCount + (count * newFatSects)),
				newFatSects, NULL);
			if (status < 0)
			{
				progressConfirmError(prog, _("Error synching FAT copies"));
				goto out;
			}
		}
	}

	// Update the filesystem metadata to reflect the new size

	if (prog && (kernelLockGet(&prog->progLock) >= 0))
	{
		strncpy((char *) prog->statusMessage, _("Updating filesystem info"),
			PROGRESS_MAX_MESSAGELEN);
		kernelLockRelease(&prog->progLock);
	}

	// Update FAT data
	fatData->totalSects = blocks;
	fatData->fatSects = newFatSects;
	fatData->dataSects = newDataSects;
	fatData->dataClusters = (newDataSects / fatData->bpb.sectsPerClust);
	fatData->freeClusters += (diffDataSects / fatData->bpb.sectsPerClust);

	status = writeVolumeInfo(fatData);
	if (status < 0)
	{
		progressConfirmError(prog, _("Error updating filesystem info"));
		goto out;
	}

	if (fatData->fsType == fat32)
	{
		status = writeFSInfo(fatData);
		if (status < 0)
		{
			progressConfirmError(prog, _("Error updating filesystem info"));
			goto out;
		}
	}

	// Mark the filesystem as 'dirty' so that Windows will check it
	markFsClean(fatData, 0);

	status = 0;

out:
	freeFatData(theDisk);
	return (status);
}


static int mount(kernelDisk *theDisk)
{
	// This function initializes the filesystem driver by gathering all of the
	// required information from the boot sector.  In addition, it dynamically
	// allocates memory space for the "used" and "free" file and directory
	// structure arrays.

	int status = 0;
	fatInternalData *fatData = NULL;

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

	// Get the FAT data for the requested filesystem.  We don't need the info
	// right now -- we just want to collect it.
	fatData = getFatData(theDisk);
	if (!fatData)
		return (status = ERR_BADDATA);

	// Build the free-cluster list
	status = makeFreeBitmap(fatData);
	if (status < 0)
	{
		kernelDebugError("Unable to create the free cluster bitmap");
		return (status = ERR_BADDATA);
	}

	// Read the disk's root directory and attach it to the filesystem structure
	status = readRootDir(fatData, theDisk);
	if (status < 0)
	{
		kernelDebugError("Unable to read the filesystem's root directory");
		return (status = ERR_BADDATA);
	}

	kernelDebug(debug_fs, "FAT mounted %s as %s", theDisk->name,
		theDisk->fsType);

	// Mark the filesystem as 'dirty'
	if (!markFsClean(fatData, 0))
		kernelLog("\"%s\" filesystem was not unmounted cleanly",
			theDisk->filesystem.mountPoint);

	// FAT filesystems are case preserving, but case insensitive.  Yuck.
	theDisk->filesystem.caseInsensitive = 1;

	if (theDisk->physical->flags & DISKFLAG_READONLY)
		theDisk->filesystem.readOnly = 1;
	else
		// Normally, read-write.
		theDisk->filesystem.readOnly = 0;

	// Return success
	return (status = 0);
}


static int unmount(kernelDisk *theDisk)
{
	// This function releases all of the stored information about a given
	// filesystem.

	int status = 0;
	fatInternalData *fatData = NULL;

	if (!initialized)
		return (status = ERR_NOTINITIALIZED);

	// Check params
	if (!theDisk)
	{
		kernelError(kernel_error, "NULL parameter");
		return (status = ERR_NULLPARAMETER);
	}

	// Get the FAT data for the requested filesystem
	fatData = getFatData(theDisk);
	if (!fatData)
		return (status = ERR_BADDATA);

	if (!theDisk->filesystem.readOnly)
	{
		// Mark the filesystem as 'clean'
		markFsClean(fatData, 1);

		// If this is a FAT32 filesystem, we need to flush the extended
		// filesystem data back to the FSInfo block
		if (fatData->fsType == fat32)
		{
			status = writeFSInfo(fatData);
			if (status < 0)
			{
				kernelDebugError("Error flushing FSInfo data block");
				return (status);
			}
		}
	}

	// Everything should be cozily tucked away now.  We can safely
	// discard the information we have cached about this filesystem.
	freeFatData(theDisk);

	return (status = 0);
}


static int newEntry(kernelFileEntry *entry)
{
	// This function gets called when there's a new kernelFileEntry in the
	// filesystem (either because a file was created or because some existing
	// thing has been newly read from disk).  This gives us an opportunity
	// to attach FAT-specific data to the file entry

	int status = 0;

	if (!initialized)
		return (status = ERR_NOTINITIALIZED);

	// Check params
	if (!entry)
	{
		kernelError(kernel_error, "NULL parameter");
		return (status = ERR_NULLPARAMETER);
	}

	// Make sure there isn't already some sort of data attached to this
	// file entry
	if (entry->driverData)
	{
		kernelError(kernel_error, "Entry already has private filesystem data");
		return (status = ERR_ALREADY);
	}

	// Get a private data structure for FAT-specific information.
	entry->driverData = kernelMalloc(sizeof(fatEntryData));
	if (!entry->driverData)
		return (status = ERR_MEMORY);

	// Return success
	return (status = 0);
}


static int inactiveEntry(kernelFileEntry *entry)
{
	// This function gets called when a kernelFileEntry is about to be
	// deallocated by the system (either because a file was deleted or because
	// the entry is simply being unbuffered).  This gives us an opportunity
	// to deallocate our FAT-specific data from the file entry

	int status = 0;

	if (!initialized)
		return (status = ERR_NOTINITIALIZED);

	// Check params
	if (!entry)
	{
		kernelError(kernel_error, "NULL parameter");
		return (status = ERR_NULLPARAMETER);
	}

	if (entry->driverData)
	{
		// Erase all of the data in this entry
		memset(entry->driverData, 0, sizeof(fatEntryData));

		kernelFree(entry->driverData);

		// Remove the reference
		entry->driverData = NULL;
	}

	// Return success
	return (status = 0);
}


static int readFile(kernelFileEntry *theFile, unsigned blockNum,
	unsigned blocks, unsigned char *buffer)
{
	// This function is the "read file" function that the filesystem driver
	// exports to the world.  It is mainly a wrapper for the internal function
	// of the same purpose, but with some additional parameter checking.

	int status = 0;
	fatInternalData *fatData = NULL;

	if (!initialized)
		return (status = ERR_NOTINITIALIZED);

	// Check params
	if (!theFile || !buffer)
	{
		kernelError(kernel_error, "NULL parameter");
		return (status = ERR_NULLPARAMETER);
	}

	// Make sure the requested read size is more than 0 blocks
	if (!blocks)
		// Don't return an error, just finish
		return (status = 0);

	// Make sure that there's a private FAT data structure attached to this
	// file entry
	if (!theFile->driverData)
	{
		kernelError(kernel_error, "File entry has no private filesystem data");
		return (status = ERR_NODATA);
	}

	// Get the FAT data for the requested filesystem
	fatData = getFatData(theFile->disk);
	if (!fatData)
		return (status = ERR_BADDATA);

	// Make sure it's really a file, and not a directory
	if (theFile->type != fileT)
		return (status = ERR_NOTAFILE);

	// Make sure the file is not corrupted
	status = checkFileChain(fatData, theFile);
	if (status < 0)
		return (status);

	// Ok, now we will call the internal function to read the file
	status = read(fatData, theFile, blockNum, blocks, buffer);

	return (status);
}


static int writeFile(kernelFileEntry *theFile, unsigned blockNum,
	unsigned blocks, unsigned char *buffer)
{
	// This function is the "write file" function that the filesystem driver
	// exports to the world.  It is mainly a wrapper for the internal function
	// of the same purpose, but with some additional argument checking.

	int status = 0;
	kernelDisk *theDisk = NULL;
	fatInternalData *fatData = NULL;

	if (!initialized)
		return (status = ERR_NOTINITIALIZED);

	kernelDebug(debug_fs, "FAT writing file \"%s\" blockNum=%d blocks=%d",
		theFile->name, blockNum, blocks);

	// Make sure the file entry and buffer aren't NULL
	if (!theFile || !buffer)
	{
		kernelError(kernel_error, "NULL parameter");
		return (status = ERR_NULLPARAMETER);
	}

	// Make sure the requested write size is more than 0 blocks
	if (!blocks)
		// Don't return an error, just finish
		return (status = 0);

	// Make sure that there's a private FAT data structure attached to this
	// file entry
	if (!theFile->driverData)
	{
		kernelError(kernel_error, "File entry has no private filesystem data");
		return (status = ERR_NODATA);
	}

	theDisk = theFile->disk;

	// Get the FAT data for the requested filesystem
	fatData = getFatData(theDisk);
	if (!fatData)
		return (status = ERR_BADDATA);

	// Make sure it's really a file, and not a directory
	if (theFile->type != fileT)
		return (status = ERR_NOTAFILE);

	// Make sure the file is not corrupted
	status = checkFileChain(fatData, theFile);
	if (status < 0)
		return (status);

	// Ok, now we will call the internal function to read the file
	status = write(fatData, theFile, blockNum, blocks, buffer);
	if (status == ERR_NOWRITE)
	{
		kernelError(kernel_warn, "File system is read-only");
		theDisk->filesystem.readOnly = 1;
	}

	return (status);
}


static int createFile(kernelFileEntry *theFile)
{
	// This function does the FAT-specific initialization of a new file.
	// There's not much more to this than getting a new entry data structure
	// and attaching it.

	int status = 0;

	if (!initialized)
		return (status = ERR_NOTINITIALIZED);

	// Check params
	if (!theFile)
		return (status = ERR_NULLPARAMETER);

	// Make sure that there's a private FAT data structure attached to this
	// file entry
	if (!theFile->driverData)
	{
		kernelError(kernel_error, "File entry has no private filesystem data");
		return (status = ERR_NODATA);
	}

	// Make sure the file's name is legal for FAT
	status = checkFilename(theFile->name);
	if (status < 0)
	{
		kernelDebugError("File name is illegal in FAT filesystems");
		return (status);
	}

	// Install the short alias for this file.  This is directory-dependent
	// because it assigns short names based on how many files in the directory
	// share common characters in the initial part of the filename.  Don't do
	// it for '.' or '..' entries, however
	if (strcmp((char *) theFile->name, ".") &&
		strcmp((char *) theFile->name, ".."))
	{
		status = makeShortAlias(theFile);
		if (status < 0)
			return (status);
	}

	// Return success
	return (status = 0);
}


static int deleteFile(kernelFileEntry *theFile)
{
	// Deletes a file.

	int status = 0;
	fatInternalData *fatData = NULL;
	fatEntryData *entryData = NULL;

	if (!initialized)
		return (status = ERR_NOTINITIALIZED);

	// Check params
	if (!theFile)
	{
		kernelError(kernel_error, "NULL parameter");
		return (status = ERR_NULLPARAMETER);
	}

	// Get the FAT data for the requested filesystem
	fatData = getFatData(theFile->disk);
	if (!fatData)
		return (status = ERR_BADDATA);

	// Get the private FAT data structure attached to this file entry
	entryData = (fatEntryData *) theFile->driverData;
	if (!entryData)
	{
		kernelError(kernel_error, "File has no private filesystem data");
		return (status = ERR_NODATA);
	}

	// Make sure the chain of clusters is not corrupt
	status = checkFileChain(fatData, theFile);
	if (status)
	{
		kernelError(kernel_error, "File to delete appears to be corrupt");
		return (status);
	}

	// Deallocate all clusters belonging to the item.
	status = releaseEntryClusters(fatData, theFile);
	if (status < 0)
	{
		kernelDebugError("Error deallocating file clusters");
		return (status);
	}

	// Return success
	return (status = 0);
}


static int fileMoved(kernelFileEntry *entry)
{
	// This function is called by the filesystem manager whenever a file
	// has been moved from one place to another.  This allows us the chance
	// do to any FAT-specific things to the file that are necessary.  In our
	// case, we need to re-create the file's short alias, since this is
	// directory-dependent.

	int status = 0;

	if (!initialized)
		return (status = ERR_NOTINITIALIZED);

	// Check params
	if (!entry)
	{
		kernelError(kernel_error, "NULL parameter");
		return (status = ERR_NULLPARAMETER);
	}

	// Make sure that there's a private FAT data structure attached to this
	// file entry
	if (!entry->driverData)
	{
		kernelError(kernel_error, "File has no private filesystem data");
		return (status = ERR_NODATA);
	}

	// Generate a new short alias for the moved file
	status = makeShortAlias(entry);
	if (status < 0)
	{
		kernelDebugError("Unable to generate new short filename alias");
		return (status);
	}

	// Return success
	return (status = 0);
}


static int makeDir(kernelFileEntry *directory)
{
	// This function is used to create a directory on disk.  The caller will
	// create the file entry data structures, and it is simply the
	// responsibility of this function to make the on-disk structures reflect
	// the new entry.

	int status = 0;
	fatInternalData *fatData = NULL;
	fatEntryData *dirData = NULL;
	unsigned newCluster = 0;

	if (!initialized)
		return (status = ERR_NOTINITIALIZED);

	// Check params
	if (!directory)
	{
		kernelError(kernel_error, "NULL parameter");
		return (status = ERR_NULLPARAMETER);
	}

	// Make sure that there's a private FAT data structure attached to this
	// file entry
	if (!directory->driverData)
	{
		kernelError(kernel_error, "File entry has no private filesystem data");
		return (status = ERR_NODATA);
	}

	// Get the FAT data for the requested filesystem
	fatData = getFatData(directory->disk);
	if (!fatData)
		return (status = ERR_BADDATA);

	// Make sure the file name is legal for FAT
	status = checkFilename(directory->name);
	if (status < 0)
	{
		kernelDebugError("File name is illegal in FAT filesystems");
		return (status);
	}

	// Allocate a new, single cluster for this new directory
	status = getUnusedClusters(fatData, 1, &newCluster);
	if (status < 0)
	{
		kernelDebugError("No more free clusters");
		return (status);
	}

	// Set the size on the new directory entry
	directory->blocks = 1;
	directory->size = fatClusterBytes(fatData);

	// Set all the appropriate attributes in the directory's private data
	dirData = directory->driverData;
	dirData->attributes = (FAT_ATTRIB_ARCHIVE | FAT_ATTRIB_SUBDIR);
	dirData->res = 0;
	dirData->timeTenth = 0;
	dirData->startCluster = newCluster;

	// Make the short alias
	status = makeShortAlias(directory);
	if (status < 0)
		return (status);

	return (status = 0);
}


static int removeDir(kernelFileEntry *directory)
{
	// This function deletes a directory, but only if it is empty.

	int status = 0;
	fatInternalData *fatData = NULL;
	fatEntryData *entryData = NULL;

	if (!initialized)
		return (status = ERR_NOTINITIALIZED);

	// Check params
	if (!directory)
	{
		kernelError(kernel_error, "NULL parameter");
		return (status = ERR_NULLPARAMETER);
	}

	// Get the FAT data for the requested filesystem
	fatData = getFatData(directory->disk);
	if (!fatData)
		return (status = ERR_BADDATA);

	// Get the private FAT data structure attached to this file entry
	entryData = (fatEntryData *) directory->driverData;
	if (!entryData)
	{
		kernelError(kernel_error, "Directory has no private filesystem data");
		return (status = ERR_NODATA);
	}

	// Make sure the chain of clusters is not corrupt
	status = checkFileChain(fatData, directory);
	if (status)
	{
		kernelError(kernel_error, "Directory to delete appears to be corrupt");
		return (status);
	}

	// Deallocate all of the clusters belonging to this directory.
	status = releaseEntryClusters(fatData, directory);
	if (status < 0)
	{
		kernelDebugError("Error deallocating directory clusters");
		return (status);
	}

	// Return success
	return (status = 0);
}


static int timestamp(kernelFileEntry *theFile)
{
	// This function does FAT-specific stuff for time stamping a file.

	int status = 0;
	fatInternalData *fatData = NULL;
	fatEntryData *entryData = NULL;

	if (!initialized)
		return (status = ERR_NOTINITIALIZED);

	// Check params
	if (!theFile)
	{
		kernelError(kernel_error, "NULL parameter");
		return (status = ERR_NULLPARAMETER);
	}

	// Make sure that there's a private FAT data structure attached to this
	// file entry
	if (!theFile->driverData)
	{
		kernelError(kernel_error, "File entry has no private filesystem data");
		return (status = ERR_NODATA);
	}

	// Get the FAT data for the requested filesystem
	fatData = getFatData(theFile->disk);
	if (!fatData)
		return (status = ERR_BADDATA);

	// Get the entry's data
	entryData = (fatEntryData *) theFile->driverData;

	// The only FAT-specific thing we're doing here is setting the
	// 'archive' bit.
	entryData->attributes = (entryData->attributes | FAT_ATTRIB_ARCHIVE);

	return (status = 0);
}


static int setBlocks(kernelFileEntry *theFile, unsigned blocks)
{
	// This function does FAT-specific stuff for allocating or deallocating
	// blocks (clusters) to or from a file.

	int status = 0;
	fatInternalData *fatData = NULL;

	if (!initialized)
		return (status = ERR_NOTINITIALIZED);

	// Check params
	if (!theFile)
	{
		kernelError(kernel_error, "NULL parameter");
		return (status = ERR_NULLPARAMETER);
	}

	// Make sure that there's a private FAT data structure attached to this
	// file entry
	if (!theFile->driverData)
	{
		kernelError(kernel_error, "File entry has no private filesystem data");
		return (status = ERR_NODATA);
	}

	// Get the FAT data for the requested filesystem
	fatData = getFatData(theFile->disk);
	if (!fatData)
		return (status = ERR_BADDATA);

	if (theFile->blocks > blocks)
		status = lengthenFile(fatData, theFile, blocks);
	else if (theFile->blocks < blocks)
		status = shortenFile(fatData, theFile, blocks);

	return (status);
}


static kernelFilesystemDriver fsDriver = {
	FSNAME_FAT,	// Driver name
	detect,
	format,
	clobber,
	NULL,		// driverCheck
	defragment,
	NULL,		// driverStat
	getFreeBytes,
	resizeConstraints,
	resize,
	mount,
	unmount,
	newEntry,
	inactiveEntry,
	NULL,		// driverResolveLink,
	readFile,
	writeFile,
	createFile,
	deleteFile,
	fileMoved,
	readDir,
	writeDir,
	makeDir,
	removeDir,
	timestamp,
	setBlocks
};


/////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////
//
//  Below here, the functions are exported for external use
//
/////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////

int kernelFilesystemFatInitialize(void)
{
	// Initialize the driver

	initialized = 1;

	// Register our driver
	return (kernelSoftwareDriverRegister(fatDriver, &fsDriver));
}

