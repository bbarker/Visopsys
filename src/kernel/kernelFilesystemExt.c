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
//  kernelFilesystemExt.c
//

// This file contains the routines designed to interpret the EXT2 filesystem
// (commonly found on Linux disks)

#include "kernelFilesystemExt.h"
#include "kernelDebug.h"
#include "kernelDisk.h"
#include "kernelDriver.h"
#include "kernelError.h"
#include "kernelFile.h"
#include "kernelFilesystem.h"
#include "kernelLog.h"
#include "kernelMalloc.h"
#include "kernelMisc.h"
#include "kernelSysTimer.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/progress.h>
#include <sys/types.h>

static int initialized = 0;

#ifdef DEBUG
static inline void debugSuperblock(extSuperblock *superblock)
{
	char volumeName[17];

	strncpy(volumeName, superblock->volume_name, 16);
	volumeName[16] = '\0';

	kernelDebug(debug_fs, "EXT superblock:\n"
		"  inodes_count=%u\n"
		"  blocks_count=%u\n"
		"  r_blocks_count=%u\n"
		"  free_blocks_count=%u\n"
		"  free_inodes_count=%u\n"
		"  first_data_block=%u\n"
		"  log_block_size=%u\n"
		"  log_cluster_size=%u\n"
		"  blocks_per_group=%u\n"
		"  clusters_per_group=%u\n"
		"  inodes_per_group=%u\n"
		"  mtime=%u wtime=%u\n"
		"  mnt_count=%u\n"
		"  max_mnt_count=%u\n"
		"  magic=0x%04x\n"
		"  state=%u errors=%u\n"
		"  minor_rev_level=%u\n"
		"  lastcheck=%u checkinterval=%u\n"
		"  creator_os=%u\n"
		"  rev_level=%u\n"
		"  first_ino=%u\n"
		"  inode_size=%u\n"
		"  block_group_nr=%u\n"
		"  feature_compat=0x%08x\n"
		"  feature_incompat=0x%08x\n"
		"  feature_ro_compat=0x%08x\n"
		"  volume_name=%s\n"
		"  log_groups_per_flex=%u", superblock->inodes_count,
		superblock->blocks_count, superblock->r_blocks_count,
		superblock->free_blocks_count, superblock->free_inodes_count,
		superblock->first_data_block, superblock->log_block_size,
		superblock->log_cluster_size, superblock->blocks_per_group,
		superblock->clusters_per_group, superblock->inodes_per_group,
		superblock->mtime, superblock->wtime, superblock->mnt_count,
		superblock->max_mnt_count, superblock->magic, superblock->state,
		superblock->errors, superblock->minor_rev_level, superblock->lastcheck,
		superblock->checkinterval, superblock->creator_os,
		superblock->rev_level, superblock->first_ino, superblock->inode_size,
		superblock->block_group_nr, superblock->feature_compat,
		superblock->feature_incompat, superblock->feature_ro_compat,
		volumeName, superblock->log_groups_per_flex);
}

static inline void debugGroupDesc(extGroupDesc *groupDesc)
{
	kernelDebug(debug_fs, "EXT group descriptor:\n"
		"  block_bitmap=%u\n"
		"  inode_bitmap=%u\n"
		"  inode_table=%u\n"
		"  free_blocks_count=%u\n"
		"  free_inodes_count=%u\n"
		"  used_dirs_count=%u\n"
		"  flags=0x%04x\n"
		"  exclude_bitmap=%u\n"
		"  block_bitmap_csum=0x%04x\n"
		"  inode_bitmap_csum=0x%04x\n"
		"  itable_unused=%u\n"
		"  checksum=0x%04x",
		groupDesc->block_bitmap, groupDesc->inode_bitmap,
		groupDesc->inode_table, groupDesc->free_blocks_count,
		groupDesc->free_inodes_count, groupDesc->used_dirs_count,
		groupDesc->flags, groupDesc->exclude_bitmap,
		groupDesc->block_bitmap_csum, groupDesc->inode_bitmap_csum,
		groupDesc->itable_unused, groupDesc->checksum);
}

static inline void debugInode(extInode *inode)
{
	char tmp[1024];
	int count;

	tmp[0] = '\0';
	for (count = 0; count < 15; count ++)
	{
		if (count && !(count % 3))
			strcat(tmp, "\n  ");

		sprintf((tmp + strlen(tmp)), "block[%d]=%u ", count,
			inode->u.block[count]);
	}

	kernelDebug(debug_fs, "EXT inode:\n"
		"  mode=0x%04x uid=%u size=%u\n"
		"  atime=%u ctime=%u mtime=%u dtime=%u\n"
		"  gid=%u links_count=%u blocks512=%u flags=0x%08x\n  %s\n"
		"  file_acl=%u dir_acl=%u", inode->mode, inode->uid, inode->size,
		inode->atime, inode->ctime, inode->mtime, inode->dtime, inode->gid,
		inode->links_count, inode->blocks512, inode->flags, tmp,
		inode->file_acl, inode->dir_acl);
}

static inline void debugExtentNode(extExtent *extent)
{
	char tmp[1024];
	int count;

	tmp[0] = '\0';
	for (count = 0; count < extent->header.entries; count ++)
	{
		if (!extent->header.depth)
		{
			sprintf((tmp + strlen(tmp)), "\n  leaf:\n"
				"    block=%u\n"
				"    len=%u\n"
				"    start_hi=%u\n"
				"    start_lo=%u",
				extent->node[count].leaf.block,
				extent->node[count].leaf.len,
				extent->node[count].leaf.start_hi,
				extent->node[count].leaf.start_lo);
		}
		else
		{
			sprintf((tmp + strlen(tmp)), "\n  index:\n"
				"    block=%u\n"
				"    leaf_lo=%u\n"
				"    leaf_hi=%u",
				extent->node[count].idx.block,
				extent->node[count].idx.leaf_lo,
				extent->node[count].idx.leaf_hi);
		}
	}

	kernelDebug(debug_fs, "EXT extent:\n"
		"  magic=0x%04x\n"
		"  entries=%u\n"
		"  max=%u\n"
		"  depth=%u\n"
		"  generation=%u%s",
		extent->header.magic, extent->header.entries, extent->header.max,
		extent->header.depth, extent->header.generation, tmp);
}
#else
	#define debugSuperblock(superblock) do { } while (0)
	#define debugGroupDesc(groupDesc) do { } while (0)
	#define debugInode(inode) do { } while (0)
	#define debugExtentNode(extent) do { } while (0)
#endif // DEBUG


static int readSuperblock(const kernelDisk *theDisk, extSuperblock *superblock)
{
	// This simple function will read the superblock into the supplied buffer
	// and ensure that it is (at least trivially) valid.  Returns 0 on success,
	// negative on error.

	int status = 0;
	kernelPhysicalDisk *physicalDisk = theDisk->physical;

	kernelDebug(debug_fs, "EXT read superblock");

	// The sector size must be non-zero
	if (!physicalDisk->sectorSize)
	{
		kernelError(kernel_error, "Disk sector size is zero");
		return (status = ERR_INVALID);
	}

	// Initialize the buffer we were given
	memset(superblock, 0, sizeof(extSuperblock));

	// Read the superblock
	status = kernelDiskReadSectors((char *) theDisk->name,
		(EXT_SUPERBLOCK_OFFSET / physicalDisk->sectorSize),
		(sizeof(extSuperblock) / physicalDisk->sectorSize), superblock);
	if (status < 0)
		return (status);

	// Check for the EXT magic number
	if (superblock->magic != (unsigned short) EXT_SUPERBLOCK_MAGIC)
		// Not EXT2
		return (status = ERR_BADDATA);

	return (status = 0);
}


static int writeSuperblock(const kernelDisk *theDisk,
	extSuperblock *superblock)
{
	// This simple function will write the superblock from the supplied buffer.

	int status = 0;
	kernelPhysicalDisk *physicalDisk = theDisk->physical;

	kernelDebug(debug_fs, "EXT write superblock");

	// The sector size must be non-zero
	if (!physicalDisk->sectorSize)
	{
		kernelError(kernel_error, "Disk sector size is zero");
		return (status = ERR_INVALID);
	}

	// Write the superblock
	status = kernelDiskWriteSectors((char *) theDisk->name,
		(EXT_SUPERBLOCK_OFFSET / physicalDisk->sectorSize),
		(sizeof(extSuperblock) / physicalDisk->sectorSize), superblock);

	return (status);
}


static int isSuperGroup(int groupNumber)
{
	// Returns 1 if the supplied block group number should have a superblock
	// (or superblock backup) under the SPARSE_SUPER scheme.  Block groups 0
	// and 1, and powers of 3, 5, and 7.

	int do3 = 1, do5 = 1, do7 = 1;
	int count, tmp3, tmp5, tmp7;

	// Shortcut some little ones.
	if ((groupNumber == 0) || (groupNumber == 1) || (groupNumber == 3) ||
		(groupNumber == 5) || (groupNumber == 7))
	{
		return (1);
	}

	for (count = 2; (do3 || do5 || do7) ; count ++)
	{
		if (do3)
		{
			tmp3 = POW(3, count);
			if (tmp3 == groupNumber)
				return (1);
			if (tmp3 > groupNumber)
				do3 = 0;
		}

		if (do5)
		{
			tmp5 = POW(5, count);
			if (tmp5 == groupNumber)
				return (1);
			if (tmp5 > groupNumber)
				do5 = 0;
		}

		if (do7)
		{
			tmp7 = POW(7, count);
			if (tmp7 == groupNumber)
				return (1);
			if (tmp7 > groupNumber)
				do7 = 0;
		}
	}

	return (0);
}


static inline void setBitmap(unsigned char *bitmap, int idx, int onOff)
{
	if (onOff)
		bitmap[idx / 8] |= (0x01 << (idx % 8));
	else
		bitmap[idx / 8] &= ~(0x01 << (idx % 8));
}


static inline unsigned getSectorNumber(extInternalData *extData,
	unsigned blockNum)
{
	// Given a filesystem and a block number, calculate the sector number
	// (which is dependent on filesystem block size)
	return (blockNum * extData->sectorsPerBlock);
}


static extInternalData *getExtData(kernelDisk *theDisk)
{
	// This function reads the filesystem parameters from the superblock

	int status = 0;
	extInternalData *extData = theDisk->filesystem.filesystemData;
	kernelPhysicalDisk *physicalDisk = theDisk->physical;
	unsigned groupDescBlocks = 0;

	// Have we already read the parameters for this filesystem?
	if (extData)
		return (extData);

	kernelDebug(debug_fs, "EXT get filesystem data");

	// We must allocate some new memory to hold information about
	// the filesystem
	extData = kernelMalloc(sizeof(extInternalData));
	if (!extData)
		return (extData = NULL);

	// Read the superblock into our extInternalData buffer
	status = readSuperblock(theDisk, (extSuperblock *) &extData->superblock);
	if (status < 0)
	{
		kernelFree((void *) extData);
		return (extData = NULL);
	}

	debugSuperblock((extSuperblock *) &extData->superblock);

	// Check that the inode size is the same size as our structure
	if (extData->superblock.inode_size != sizeof(extInode))
		kernelError(kernel_warn, "Inode size (%u) does not match structure "
			"size (%u)", extData->superblock.inode_size, sizeof(extInode));

	extData->blockSize = (1024 << extData->superblock.log_block_size);

	// Save the sectors per block so we don't have to keep calculating it
	extData->sectorsPerBlock = (extData->blockSize / physicalDisk->sectorSize);

	// Calculate the number of block groups
	extData->numGroups = ((extData->superblock.blocks_count +
		(extData->superblock.blocks_per_group - 1)) /
			extData->superblock.blocks_per_group);

	// Calculate the number of blocks per group descriptor
	groupDescBlocks = (((extData->numGroups * sizeof(extGroupDesc)) +
		(extData->blockSize - 1)) / extData->blockSize);

	kernelDebug(debug_fs, "EXT blockSize=%u sectorsPerBlock=%u numGroups=%u "
		"groupDescBlocks=%u", extData->blockSize, extData->sectorsPerBlock,
		extData->numGroups, groupDescBlocks);

	// Get some memory for our array of group descriptors
	extData->groups = kernelMalloc(groupDescBlocks * extData->blockSize);
	if (!extData->groups)
	{
		kernelFree((void *) extData);
		return (extData = NULL);
	}

	// Read the group descriptors into our structures
	status = kernelDiskReadSectors((char *) theDisk->name,
		getSectorNumber(extData, ((extData->blockSize == 1024)? 2 : 1)),
		(groupDescBlocks * extData->sectorsPerBlock), extData->groups);
	if (status < 0)
	{
		kernelError(kernel_error, "Unable to read EXT group descriptors");
		kernelFree(extData->groups);
		kernelFree((void *) extData);
		return (extData = NULL);
	}

	// Attach the disk structure to the extData structure
	extData->disk = theDisk;

	// Attach our new FS data to the filesystem structure
	theDisk->filesystem.filesystemData = (void *) extData;

	// The volume label
	strncpy((char *) theDisk->filesystem.label,
		(char *) extData->superblock.volume_name, 16);

	// Specify the filesystem block size
	theDisk->filesystem.blockSize = extData->blockSize;

	// 'minSectors' and 'maxSectors' are the same as the current sectors,
	// since we don't yet support resizing.
	theDisk->filesystem.minSectors = theDisk->numSectors;
	theDisk->filesystem.maxSectors = theDisk->numSectors;

	return (extData);
}


static int readInode(extInternalData *extData, unsigned number,
	extInode *inode)
{
	// Reads the requested inode structure from disk

	int status = 0;
	unsigned groupNumber = 0;
	unsigned inodeTableBlock = 0;
	unsigned char *buffer = NULL;

	kernelDebug(debug_fs, "EXT read inode %u", number);

	if ((number < 1) || (number > extData->superblock.inodes_count))
	{
		kernelError(kernel_error, "Invalid inode number %u", number);
		return (status = ERR_BOUNDS);
	}

	// We use the number as a base-zero but the filesystem counts from 1.
	number--;

	kernelDebug(debug_fs, "EXT inodes per group %u",
		extData->superblock.inodes_per_group);

	// Calculate the group number
	groupNumber = (number / extData->superblock.inodes_per_group);

	kernelDebug(debug_fs, "EXT group number %u", groupNumber);

	// Calculate the relevant sector of the inode table
	inodeTableBlock = (((number % extData->superblock.inodes_per_group) *
		extData->superblock.inode_size) / extData->blockSize);

	kernelDebug(debug_fs, "EXT inode table block %u", inodeTableBlock);

	// Get a new temporary buffer to read the inode table block
	buffer = kernelMalloc(extData->blockSize);
	if (!buffer)
		return (status = ERR_MEMORY);

	debugGroupDesc(&extData->groups[groupNumber]);

	kernelDebug(debug_fs, "EXT read inode table block %u, sector %u",
		(extData->groups[groupNumber].inode_table + inodeTableBlock),
		getSectorNumber(extData, (extData->groups[groupNumber].inode_table +
			inodeTableBlock)));

	// Read the applicable inode table block
	status = kernelDiskReadSectors((char *) extData->disk->name,
		getSectorNumber(extData, (extData->groups[groupNumber].inode_table +
			inodeTableBlock)), extData->sectorsPerBlock, buffer);
	if (status < 0)
	{
		kernelError(kernel_error, "Unable to read inode table for group %u",
			groupNumber);
		kernelFree(buffer);
		return (status);
	}

	// Copy the inode structure.
	memcpy(inode, (buffer + (((number % extData->superblock.inodes_per_group) *
		extData->superblock.inode_size) % extData->blockSize)),
		sizeof(extInode));

	kernelFree(buffer);

	kernelDebug(debug_fs, "EXT got inode:");
	debugInode(inode);

	return (status = 0);
}


static int readExtent(extInternalData *extData, extInode *inode,
	unsigned startBlock, unsigned numBlocks, void *buffer)
{
	int status = 0;
	void *dataPointer = NULL;
	extExtentLeaf *extentLeaf = NULL;
	unsigned readBlocks = 0;
	int count;

	dataPointer = buffer;

	if (!inode->u.extent.header.depth)
	{
		kernelDebug(debug_fs, "EXT extent leaf node");
		debugExtentNode(&inode->u.extent);

		for (count = 0; (numBlocks &&
			(count < inode->u.extent.header.entries)); count ++)
		{
			extentLeaf = &inode->u.extent.node[count].leaf;

			if ((extentLeaf->block <= startBlock) &&
				((extentLeaf->block + extentLeaf->len) > startBlock))
			{
				kernelDebug(debug_fs, "EXT read from leaf node entry %d",
					count);

				readBlocks = min(numBlocks, (extentLeaf->len - (startBlock -
					extentLeaf->block)));

				status = kernelDiskReadSectors((char *) extData->disk->name,
					getSectorNumber(extData, (extentLeaf->start_lo +
						(startBlock - extentLeaf->block))),
					(readBlocks * extData->sectorsPerBlock), dataPointer);
				if (status < 0)
					return (status);

				startBlock += readBlocks;
				numBlocks -= readBlocks;
				dataPointer += (readBlocks * extData->blockSize);
			}
		}
	}
	else
	{
		kernelError(kernel_error, "Deep file extents are currently "
			"unsupported");
		return (status = ERR_NOTIMPLEMENTED);
	}

	return (status = 0);
}


static int readIndirectBlocks(extInternalData *extData, unsigned indirectBlock,
	unsigned *skipBlocks, unsigned *numBlocks, void **buffer,
	int indirectionLevel)
{
	// This function will read indirect blocks, starting with startBlock.
	// the indirectionLevel parameter being greater than 1 causes a recursion.

	int status = 0;
	unsigned *indexBuffer = NULL;
	unsigned count;

	kernelDebug(debug_fs, "EXT read %u indirect blocks at %u", *numBlocks,
		indirectBlock);

	// Get memory to hold a block
	indexBuffer = kernelMalloc(extData->blockSize);
	if (!indexBuffer)
		return (status = ERR_MEMORY);

	// Read the indirect block number we've been passed
	status = kernelDiskReadSectors((char *) extData->disk->name,
		getSectorNumber(extData, indirectBlock), extData->sectorsPerBlock,
		indexBuffer);
	if (status < 0)
		goto out;

	// Now, if the indirection level is 1, this is an index of blocks to read.
	// Otherwise, it is an index of indexes, and we need to recurse
	if (indirectionLevel > 1)
	{
		// Do a recursion for every index in this block
		for (count = 0; (*numBlocks &&
			(count < (extData->blockSize / sizeof(unsigned)))); count++)
		{
			if (indexBuffer[count] < 2)
			{
				status = 0;
				goto out;
			}

			status = readIndirectBlocks(extData, indexBuffer[count],
				skipBlocks, numBlocks, buffer, (indirectionLevel - 1));
			if (status < 0)
				goto out;
		}
	}
	else
	{
		// Read the blocks in our index block into the buffer
		for (count = 0; (*numBlocks &&
			(count < (extData->blockSize / sizeof(unsigned)))); count++)
		{
			if (indexBuffer[count] < 2)
			{
				status = 0;
				goto out;
			}

			if (*skipBlocks)
			{
				*skipBlocks -= 1;
				continue;
			}

			status = kernelDiskReadSectors((char *) extData->disk->name,
				getSectorNumber(extData, indexBuffer[count]),
				extData->sectorsPerBlock, *buffer);
			if (status < 0)
				goto out;

			*numBlocks -= 1;
			*buffer += extData->blockSize;
		}
	}

	status = 0;

out:
	kernelFree(indexBuffer);
	return (status);
}


static int readBlockList(extInternalData *extData, extInode *inode,
	unsigned startBlock, unsigned numBlocks, void *buffer)
{
	int status = 0;
	unsigned skipBlocks = startBlock;
	void *dataPointer = buffer;
	int count;

	// Read (up to) the first 12 direct blocks
	for (count = 0; (numBlocks && (count < 12) &&
		(dataPointer < (buffer + inode->size))); count ++)
	{
		if (inode->u.block[count] < 2)
			return (status = 0);

		if (skipBlocks)
		{
			skipBlocks -= 1;
			continue;
		}

		status = kernelDiskReadSectors((char *) extData->disk->name,
			getSectorNumber(extData, inode->u.block[count]),
			extData->sectorsPerBlock, dataPointer);
		if (status < 0)
			return (status);

		numBlocks -= 1;
		dataPointer += extData->blockSize;
	}

	// Now if there are any indirect blocks...
	if (numBlocks && inode->u.block[12])
	{
		status = readIndirectBlocks(extData, inode->u.block[12], &skipBlocks,
			&numBlocks, &dataPointer, 1);
		if (status < 0)
			return (status);
	}

	// Double-indirect blocks...
	if (numBlocks && inode->u.block[13])
	{
		status = readIndirectBlocks(extData, inode->u.block[13], &skipBlocks,
			&numBlocks, &dataPointer, 2);
		if (status < 0)
			return (status);
	}

	// Triple-indirect blocks
	if (numBlocks && inode->u.block[14])
	{
		status = readIndirectBlocks(extData, inode->u.block[14], &skipBlocks,
			&numBlocks, &dataPointer, 3);
		if (status < 0)
			return (status);
	}

	return (status = 0);
}


static int read(extInternalData *extData, kernelFileEntry *fileEntry,
	unsigned startBlock, unsigned numBlocks, void *buffer)
{
	// Read numBlocks blocks of a file (or directory) starting at startBlock
	// into buffer.

	int status = 0;
	extInode *inode = NULL;

	inode = (extInode *) fileEntry->driverData;

	// If numBlocks is zero, that means read the whole file
	if (!numBlocks)
		numBlocks = (inode->blocks512 / (extData->blockSize >> 9));

	kernelDebug(debug_fs, "EXT read %u blocks of \"%s\" at %u", numBlocks,
		fileEntry->name, startBlock);

	if ((extData->superblock.feature_incompat & EXT_INCOMPAT_EXTENTS) &&
		(inode->flags & EXT_EXTENTS_FL))
	{
		// This inode uses the newer 'extents' feature
		kernelDebug(debug_fs, "EXT inode uses extents");
		status = readExtent(extData, inode, startBlock, numBlocks, buffer);
	}
	else
	{
		// This inode uses the older block list feature
		kernelDebug(debug_fs, "EXT inode uses block lists");
		status = readBlockList(extData, inode, startBlock, numBlocks, buffer);
	}

	return (status);
}


static unsigned makeSystemTime(unsigned theTime)
{
	// This function takes a UNIX time value and returns the equivalent in
	// packed-BCD system format.

	unsigned temp = 0;
	unsigned returnedTime = 0;

	// Unix time is seconds since 00:00:00 January 1, 1970

	// Remove all but the current day
	theTime %= 86400;

	// The hour
	temp = (theTime / 3600);
	returnedTime |= ((temp & 0x0000003F) << 12);
	theTime %= 3600;

	// The minute
	temp = (theTime / 60);
	returnedTime |= ((temp & 0x0000003F) << 6);
	theTime %= 60;

	// The second
	returnedTime |= (temp & 0x0000003F);

	return (returnedTime);
}


static unsigned makeSystemDate(unsigned date)
{
	// This function takes a UNIX time value and returns the equivalent in
	// packed-BCD system format.

	unsigned temp = 0;
	unsigned returnedDate = 0;

	// Unix time is seconds since 00:00:00 January 1, 1970

	// Figure out the year
	temp = (date / 31536000);
	returnedDate |= ((temp + 1970) << 9);
	date %= 31536000;

	// The month (1-12)
	temp = (date / 2678400);
	returnedDate |= (((temp + 1) & 0x0000000F) << 5);
	date %= 2678400;

	// Day of the month (1-31)
	temp = (date / 86400);
	returnedDate |= ((temp + 1) & 0x0000001F);

	return (returnedDate);
}


static int scanDirectory(extInternalData *extData, kernelFileEntry *dirEntry)
{
	int status = 0;
	extInode *dirInode = NULL;
	unsigned bufferSize = 0;
	void *buffer = NULL;
	void *entry = NULL;
	extDirEntry realEntry;
	kernelFileEntry *fileEntry = NULL;
	extInode *inode = NULL;

	kernelDebug(debug_fs, "EXT scan directory %s", dirEntry->name);

	// Make sure it's really a directory, and not a regular file
	if (dirEntry->type != dirT)
	{
		kernelError(kernel_error, "Entry to scan is not a directory");
		return (status = ERR_NOTADIR);
	}

	dirInode = (extInode *) dirEntry->driverData;

	// Make sure it's not zero-length.  Shouldn't ever happen.
	if (!dirInode->blocks512)
	{
		kernelError(kernel_error, "Directory \"%s\" has no data",
			dirEntry->name);
		return (status = ERR_NODATA);
	}

	bufferSize = (dirInode->blocks512 << 9);
	if (bufferSize < dirEntry->size)
	{
		kernelError(kernel_error, "Invalid buffer size for directory!");
		return (status = ERR_BADDATA);
	}

	// Get a buffer for the directory
	buffer = kernelMalloc(bufferSize);
	if (!buffer)
		return (status = ERR_MEMORY);

	status = read(extData, dirEntry, 0, 0, buffer);
	if (status < 0)
	{
		kernelError(kernel_error, "Unable to read directory data");
		goto out;
	}

	entry = buffer;

	while ((unsigned) entry < (unsigned)(buffer + bufferSize - 1))
	{
		realEntry.inode = *((unsigned *) entry);
		realEntry.rec_len = *((unsigned short *)(entry + 4));

		if (!realEntry.inode)
		{
			if (!realEntry.rec_len)
				// End of entries, we must suppose
				break;

			// Deleted file perhaps?
			entry += realEntry.rec_len;
			continue;
		}

		// Copy the rest of the structure
		memcpy(&realEntry, entry, sizeof(extDirEntry));

		// Make sure the name is NULL-terminated
		if (extData->superblock.feature_incompat & EXT_INCOMPAT_FILETYPE)
			realEntry.name[realEntry.u.lenType.name_len] = '\0';
		else
			realEntry.name[realEntry.u.name_len] = '\0';

		kernelDebug(debug_fs, "EXT reading directory entry %s",
			realEntry.name);

		fileEntry = kernelFileNewEntry(dirEntry->disk);
		if (!fileEntry)
		{
			status = ERR_NOCREATE;
			goto out;
		}

		inode = (extInode *) fileEntry->driverData;
		if (!inode)
		{
			kernelError(kernel_error, "New entry has no private data");
			status = ERR_BUG;
			goto out;
		}

		// Read the inode
		status = readInode(extData, realEntry.inode, inode);
		if (status < 0)
		{
			kernelError(kernel_error, "Unable to read inode for directory "
				"entry \"%s\"", realEntry.name);
			goto out;
		}

		strncpy((char *) fileEntry->name, (char *) realEntry.name,
			MAX_NAME_LENGTH);

		switch (inode->mode & EXT_S_IFMT)
		{
			case EXT_S_IFDIR:
				fileEntry->type = dirT;
				break;

			case EXT_S_IFLNK:
				fileEntry->type = linkT;
				break;

			case EXT_S_IFREG:
			default:
				fileEntry->type = fileT;
				break;
		}

		fileEntry->creationTime = makeSystemTime(inode->ctime);
		fileEntry->creationDate = makeSystemDate(inode->ctime);
		fileEntry->accessedTime = makeSystemTime(inode->atime);
		fileEntry->accessedDate = makeSystemDate(inode->atime);
		fileEntry->modifiedTime = makeSystemTime(inode->mtime);
		fileEntry->modifiedDate = makeSystemDate(inode->mtime);
		fileEntry->size = inode->size;
		fileEntry->blocks = (inode->blocks512 / (extData->blockSize >> 9));
		fileEntry->lastAccess = kernelSysTimerRead();

		// Add it to the directory
		status = kernelFileInsertEntry(fileEntry, dirEntry);
		if (status < 0)
			goto out;

		// Prevent a situation of getting into a bad loop if the rec_len field
		// isn't some positive number.
		if (realEntry.rec_len <= 0)
		{
			kernelError(kernel_error, "Corrupt directory record \"%s\" in "
				"directory \"%s\" has a NULL record length",
				fileEntry->name, dirEntry->name);
			status = ERR_BADDATA;
			goto out;
		}

		entry += realEntry.rec_len;
	}

	status = 0;

out:
	kernelFree(buffer);
	return (status);
}


static int readRootDir(extInternalData *extData, kernelDisk *theDisk)
{
	// This function reads the root directory (which uses a reserved inode
	// number) and attaches the root directory kernelFileEntry pointer to the
	// filesystem structure.

	int status = 0;
	kernelFileEntry *rootEntry = theDisk->filesystem.filesystemRoot;
	extInode *rootInode = (extInode *) rootEntry->driverData;

	kernelDebug(debug_fs, "EXT read root directory");

	if (!rootInode)
	{
		kernelError(kernel_error, "Root entry has no private data");
		return (status = ERR_NODATA);
	}

	// Read the inode for the root directory
	status = readInode(extData, EXT_ROOT_INO, rootInode);
	if (status < 0)
	{
		kernelError(kernel_error, "Unable to read root inode");
		return (status);
	}

	return (status = scanDirectory(extData, rootEntry));
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
	// is using a EXT filesystem.  It uses a simple test or two to determine
	// simply whether this is a EXT volume.  Any data that it gathers is
	// discarded when the call terminates.  It returns 1 for true, 0 for false,
	// and negative if it encounters an error

	int status = 0;
	extSuperblock superblock;

	if (!initialized)
		return (status = ERR_NOTINITIALIZED);

	// Check params
	if (!theDisk)
	{
		kernelError(kernel_error, "NULL parameter");
		return (status = ERR_NULLPARAMETER);
	}

	// Try to load the superblock
	status = readSuperblock(theDisk, &superblock);

	if (!status)
	{
		// EXT
		strcpy((char *) theDisk->fsType, FSNAME_EXT);
		strncpy((char *) theDisk->filesystem.label, superblock.volume_name, 16);
		return (status = 1);
	}
	else
	{
		// Not EXT
		return (status = 0);
	}
}


static int format(kernelDisk *theDisk, const char *type, const char *label,
	int longFormat __attribute__((unused)), progress *prog)
{
	// This function does a basic format of an EXT2 filesystem.

	int status = 0;
	kernelPhysicalDisk *physicalDisk = NULL;
	extSuperblock superblock;
	int blockSize = 0;
	int sectsPerBlock = 0;
	int blockGroups = 0;
	int inodeTableBlocks = 0;
	int groupDescBlocks = 0;
	unsigned char *bitmaps = NULL;
	extGroupDesc *groupDescs = NULL;
	extInode *inodeTable = NULL;
	void *dirBuffer = NULL;
	extDirEntry *dirEntry = NULL;
	int count1;
	unsigned count2;

	if (!initialized)
	{
		status = ERR_NOTINITIALIZED;
		goto out;
	}

	// Check params.  It's okay for all other params to be NULL.
	if (!theDisk || !type)
	{
		kernelError(kernel_error, "NULL parameter");
		status = ERR_NULLPARAMETER;
		goto out;
	}

	kernelDebug(debug_fs, "EXT format disk %s", theDisk->name);

	if (strncasecmp(type, FSNAME_EXT, strlen(FSNAME_EXT)) ||
		((strlen(type) > strlen(FSNAME_EXT)) &&
			strcasecmp(type, FSNAME_EXT"2")))
	{
		kernelError(kernel_error, "Filesystem type %s not supported", type);
		status = ERR_INVALID;
		goto out;
	}

	physicalDisk = theDisk->physical;

	// Only format disk with 512-byte sectors
	if (physicalDisk->sectorSize != 512)
	{
		kernelError(kernel_error, "Cannot format a disk with sector size of "
			"%u (512 only)", physicalDisk->sectorSize);
		status = ERR_INVALID;
		goto out;
	}

	if (prog && (kernelLockGet(&prog->progLock) >= 0))
	{
		strcpy((char *) prog->statusMessage, "Calculating parameters");
		kernelLockRelease(&prog->progLock);
	}

	// Clear memory
	memset(&superblock, 0, sizeof(extSuperblock));

	// "Heuristically" determine the block size.  Everything else flows from
	// this, but it is rubbish.  Not based on anything sensible really: If it's
	// a floppy, use 1K blocks.  Otherwise use 4K blocks.
	if (physicalDisk->type & DISKTYPE_FLOPPY)
		blockSize = 1024;
	else
		blockSize = 4096;

	sectsPerBlock = (blockSize / physicalDisk->sectorSize);

	superblock.blocks_count = (theDisk->numSectors / sectsPerBlock);
	superblock.r_blocks_count = ((superblock.blocks_count / 100) * 5); // 5%
	superblock.first_data_block = (1024 / blockSize); // Always 1 or 0
	for (count1 = 0; ((blockSize >> count1) >= 1024); count1 ++)
		superblock.log_block_size = count1;
	superblock.log_cluster_size = superblock.log_block_size;
	superblock.blocks_per_group = (blockSize * 8); // Bits in 1-block bitmap.
	superblock.clusters_per_group = superblock.blocks_per_group;
	superblock.mtime = kernelUnixTime();
	superblock.wtime = superblock.mtime;
	superblock.max_mnt_count = 25;
	superblock.magic = EXT_SUPERBLOCK_MAGIC;
	superblock.state = EXT_VALID_FS;
	superblock.errors = EXT_ERRORS_DEFAULT;
	superblock.lastcheck = superblock.mtime;
	superblock.checkinterval = (SECPERDAY * 180); // 180 days, in seconds
	superblock.creator_os = EXT_OS_VISOPSYS;
	superblock.rev_level = EXT_DYNAMIC_REV;
	superblock.first_ino = EXT_GOOD_OLD_FIRST_INODE;
	superblock.inode_size = EXT_GOOD_OLD_INODE_SIZE;
	superblock.feature_incompat = EXT_INCOMPAT_FILETYPE;
	superblock.feature_ro_compat = EXT_ROCOMPAT_SPARSESUPER;
	kernelGuidGenerate((guid *) &superblock.uuid);
	if (label)
		strncpy(superblock.volume_name, label, 16);

	blockGroups = ((superblock.blocks_count +
		(superblock.blocks_per_group - 1)) / superblock.blocks_per_group);

	superblock.inodes_per_group = (((((superblock.blocks_count / blockGroups) *
		EXT_GOOD_OLD_INODE_SIZE) / blockSize) * blockSize) /
			EXT_GOOD_OLD_INODE_SIZE);

	superblock.inodes_count = (blockGroups * superblock.inodes_per_group);

	groupDescBlocks = (((blockGroups * sizeof(extGroupDesc)) +
		(blockSize - 1)) / blockSize);

	inodeTableBlocks = (((superblock.inodes_per_group *
		superblock.inode_size) + (blockSize - 1)) / blockSize);

	// Get buffers for group descriptors and bitmaps
	groupDescs = kernelMalloc(groupDescBlocks * blockSize);
	bitmaps = kernelMalloc(2 * blockSize);
	inodeTable = kernelMalloc(inodeTableBlocks * blockSize);
	if (!groupDescs || !bitmaps || !inodeTable)
	{
		status = ERR_MEMORY;
		goto out;
	}

	if (prog && (kernelLockGet(&prog->progLock) >= 0))
	{
		strcpy((char *) prog->statusMessage, "Creating group descriptors");
		kernelLockRelease(&prog->progLock);
	}

	// Create the group descriptors
	for (count1 = 0; count1 < blockGroups; count1 ++)
	{
		unsigned currentBlock = (count1 * superblock.blocks_per_group);

		if (count1 < (blockGroups - 1))
		{
			groupDescs[count1].free_blocks_count = superblock.blocks_per_group;
			groupDescs[count1].free_inodes_count = superblock.inodes_per_group;
		}
		else
		{
			groupDescs[count1].free_blocks_count =
				(superblock.blocks_count -
					(count1 * superblock.blocks_per_group));
			groupDescs[count1].free_inodes_count =
				(superblock.inodes_count -
					(count1 * superblock.inodes_per_group));
		}

		if (isSuperGroup(count1))
		{
			// The superblock and group descriptors backups
			groupDescs[count1].free_blocks_count -= (1 + groupDescBlocks);
			currentBlock += (1 + groupDescBlocks);
		}

		groupDescs[count1].block_bitmap = currentBlock++;
		groupDescs[count1].inode_bitmap = currentBlock++;
		groupDescs[count1].free_blocks_count -= 2;

		groupDescs[count1].inode_table = currentBlock;
		groupDescs[count1].free_blocks_count -= inodeTableBlocks;
		currentBlock += inodeTableBlocks;

		if (!count1)
		{
			// Subtract the reserved inodes, plus 1 for the lost+found
			// directory, plus 1 block each for the root and lost+found
			// directories
			groupDescs[count1].free_blocks_count -= 2;
			groupDescs[count1].free_inodes_count -= superblock.first_ino;

			// Also mention the root and lost+found directories
			groupDescs[count1].used_dirs_count = 2;
		}

		superblock.free_blocks_count += groupDescs[count1].free_blocks_count;
		superblock.free_inodes_count += groupDescs[count1].free_inodes_count;
	}

	if (prog && (kernelLockGet(&prog->progLock) >= 0))
	{
		strcpy((char *) prog->statusMessage, "Writing block groups");
		kernelLockRelease(&prog->progLock);
	}

	// Clear/write the blocks of all control sectors, block groups, etc
	for (count1 = 0; count1 < blockGroups; count1 ++)
	{
		unsigned currentBlock = (count1 * superblock.blocks_per_group);
		#define CURRENTSECTOR ((currentBlock) * sectsPerBlock)

		if (isSuperGroup(count1))
		{
			// Superblock

			superblock.block_group_nr = count1;

			if (!currentBlock)
			{
				status = kernelDiskWriteSectors((char *) theDisk->name,
					(1024 / physicalDisk->sectorSize), (sizeof(extSuperblock) /
						physicalDisk->sectorSize), &superblock);
			}
			else
			{
				status = kernelDiskWriteSectors((char *) theDisk->name,
					CURRENTSECTOR, (sizeof(extSuperblock) /
						physicalDisk->sectorSize), &superblock);
			}

			if (status < 0)
				goto out;

			currentBlock += 1;

			// Group descriptors
			status = kernelDiskWriteSectors((char *) theDisk->name,
				CURRENTSECTOR, (groupDescBlocks * sectsPerBlock), groupDescs);
			if (status < 0)
				goto out;

			currentBlock += groupDescBlocks;

			// Set up the block and inode bitmaps
			memset(bitmaps, 0, (2 * blockSize));
			for (count2 = 0; count2 < (unsigned)
				(1 + groupDescBlocks + 2 + inodeTableBlocks); count2 ++)
			{
				setBitmap(bitmaps, count2, 1);
			}
		}
		else
		{
			// Set up the block and inode bitmaps
			memset(bitmaps, 0, (2 * blockSize));
			for (count2 = 0; count2 < (unsigned)(2 + inodeTableBlocks);
				count2 ++)
			{
				setBitmap(bitmaps, count2, 1);
			}
		}

		if (!count1)
		{
			// Mark the blocks for the root and lost+found directories
			setBitmap(bitmaps,
				(1 + groupDescBlocks + 2 + inodeTableBlocks), 1);
			setBitmap(bitmaps,
				(1 + groupDescBlocks + 2 + inodeTableBlocks + 1), 1);

			// Mark the reserved inodes, plus 1 for the lost+found directory
			for (count2 = 0; count2 < superblock.first_ino; count2 ++)
				setBitmap((bitmaps + blockSize), count2, 1);
		}

		if (count1 == (blockGroups - 1))
		{
			// Mark any nonexistant blocks as used.
			for (count2 = (superblock.blocks_count -
					(count1 * superblock.blocks_per_group));
				count2 < superblock.blocks_per_group; count2 ++)
			{
				setBitmap(bitmaps, count2, 1);
			}
		}

		kernelDiskWriteSectors((char *) theDisk->name, CURRENTSECTOR,
			(2 * sectsPerBlock), bitmaps);

		currentBlock += 2;

		// Clear the inode table
		kernelDiskWriteSectors((char *) theDisk->name, CURRENTSECTOR,
			(inodeTableBlocks * sectsPerBlock), inodeTable);

		if (prog && (kernelLockGet(&prog->progLock) >= 0))
		{
			prog->percentFinished = ((count1 * 100) / blockGroups);
			kernelLockRelease(&prog->progLock);
		}
	}

	if (prog && (kernelLockGet(&prog->progLock) >= 0))
	{
		strcpy((char *) prog->statusMessage, "Initializing inodes");
		kernelLockRelease(&prog->progLock);
	}

	// Create the root inode
	inodeTable[EXT_ROOT_INO - 1].mode =
		(EXT_S_IFDIR | EXT_S_IRUSR | EXT_S_IWUSR | EXT_S_IXUSR |
			EXT_S_IRGRP | EXT_S_IXGRP | EXT_S_IROTH | EXT_S_IXOTH);
	inodeTable[EXT_ROOT_INO - 1].size = blockSize;
	inodeTable[EXT_ROOT_INO - 1].atime = superblock.mtime;
	inodeTable[EXT_ROOT_INO - 1].ctime = superblock.mtime;
	inodeTable[EXT_ROOT_INO - 1].mtime = superblock.mtime;
	inodeTable[EXT_ROOT_INO - 1].links_count = 3;
	inodeTable[EXT_ROOT_INO - 1].blocks512 = (blockSize >> 9);
	inodeTable[EXT_ROOT_INO - 1].u.block[0] =
		(1 + groupDescBlocks + 2 + inodeTableBlocks);

	// Create the lost+found inode
	inodeTable[superblock.first_ino - 1].mode =
		(EXT_S_IFDIR | EXT_S_IRUSR | EXT_S_IWUSR | EXT_S_IXUSR);
	inodeTable[superblock.first_ino - 1].size = blockSize;
	inodeTable[superblock.first_ino - 1].atime = superblock.mtime;
	inodeTable[superblock.first_ino - 1].ctime = superblock.mtime;
	inodeTable[superblock.first_ino - 1].mtime = superblock.mtime;
	inodeTable[superblock.first_ino - 1].links_count = 2;
	inodeTable[superblock.first_ino - 1].blocks512 = (blockSize >> 9);
	inodeTable[superblock.first_ino - 1].u.block[0] =
		(inodeTable[EXT_ROOT_INO - 1].u.block[0] + 1);

	kernelDiskWriteSectors((char *) theDisk->name,
		((3 + groupDescBlocks) * sectsPerBlock), sectsPerBlock, inodeTable);

	if (prog && (kernelLockGet(&prog->progLock) >= 0))
	{
		strcpy((char *) prog->statusMessage, "Creating directories");
		kernelLockRelease(&prog->progLock);
	}

	// Create the root directory

	dirBuffer = kernelMalloc(inodeTable[EXT_ROOT_INO - 1].size);
	if (!dirBuffer)
	{
		status = ERR_MEMORY;
		goto out;
	}

	dirEntry = dirBuffer;
	dirEntry->inode = EXT_ROOT_INO;
	strcpy(dirEntry->name, ".");
	dirEntry->u.lenType.name_len = strlen(dirEntry->name);
	dirEntry->u.lenType.file_type = EXT_FT_DIR;
	dirEntry->rec_len = 12;

	dirEntry = ((void *) dirEntry + dirEntry->rec_len);
	dirEntry->inode = EXT_ROOT_INO;
	strcpy(dirEntry->name, "..");
	dirEntry->u.lenType.name_len = strlen(dirEntry->name);
	dirEntry->u.lenType.file_type = EXT_FT_DIR;
	dirEntry->rec_len = 12;

	dirEntry = ((void *) dirEntry + dirEntry->rec_len);
	dirEntry->inode = superblock.first_ino;
	strcpy(dirEntry->name, "lost+found");
	dirEntry->u.lenType.name_len = strlen(dirEntry->name);
	dirEntry->u.lenType.file_type = EXT_FT_DIR;
	dirEntry->rec_len = (inodeTable[EXT_ROOT_INO - 1].size -
		((unsigned) dirEntry - (unsigned) dirBuffer));

	kernelDiskWriteSectors((char *) theDisk->name,
		(inodeTable[EXT_ROOT_INO - 1].u.block[0] * sectsPerBlock),
		(inodeTable[EXT_ROOT_INO - 1].size / physicalDisk->sectorSize),
		dirBuffer);

	// Create the lost+found directory

	memset(dirBuffer, 0, inodeTable[EXT_ROOT_INO - 1].size);

	dirEntry = dirBuffer;
	dirEntry->inode = superblock.first_ino;
	strcpy(dirEntry->name, ".");
	dirEntry->u.lenType.name_len = strlen(dirEntry->name);
	dirEntry->u.lenType.file_type = EXT_FT_DIR;
	dirEntry->rec_len = 12;

	dirEntry = ((void *) dirEntry + dirEntry->rec_len);
	dirEntry->inode = EXT_ROOT_INO;
	strcpy(dirEntry->name, "..");
	dirEntry->u.lenType.name_len = strlen(dirEntry->name);
	dirEntry->u.lenType.file_type = EXT_FT_DIR;
	dirEntry->rec_len = (inodeTable[EXT_ROOT_INO - 1].size -
		((unsigned) dirEntry - (unsigned) dirBuffer));

	kernelDiskWriteSectors((char *) theDisk->name,
		(inodeTable[superblock.first_ino - 1].u.block[0] * sectsPerBlock),
		(inodeTable[superblock.first_ino - 1].size / physicalDisk->sectorSize),
		dirBuffer);

	strcpy((char *) theDisk->fsType, FSNAME_EXT"2");

	if (prog && (kernelLockGet(&prog->progLock) >= 0))
	{
		strcpy((char *) prog->statusMessage, "Syncing disk");
		kernelLockRelease(&prog->progLock);
	}

	kernelLog("Format: Type: %s  Total blocks: %u  Bytes per block: %u  "
		"Sectors per block: %u  Block group size: %u  Block groups: %u",
		theDisk->fsType, superblock.blocks_count, blockSize, sectsPerBlock,
		superblock.blocks_per_group, blockGroups);

	status = 0;

out:
	if (dirBuffer)
		kernelFree(dirBuffer);
	if (inodeTable)
		kernelFree(inodeTable);
	if (bitmaps)
		kernelFree(bitmaps);
	if (groupDescs)
		kernelFree(groupDescs);

	if (prog && (kernelLockGet(&prog->progLock) >= 0))
	{
		prog->complete = 1;
		kernelLockRelease(&prog->progLock);
	}

	return (status);
}


static int clobber(kernelDisk *theDisk)
{
	// This function destroys anything that might cause this disk to be detected
	// as having an EXT filesystem.

	int status = 0;
	extSuperblock superblock;

	// Check params.
	if (!theDisk)
	{
		kernelError(kernel_error, "NULL parameter");
		return (status = ERR_NULLPARAMETER);
	}

	kernelDebug(debug_fs, "EXT clobber disk %s", theDisk->name);

	// In the case of EXT, we simply remove the EXT signature from where the
	// superblock would be.
	status = readSuperblock(theDisk, &superblock);
	if (status < 0)
		// Not EXT
		return (status = 0);

	superblock.magic = 0;

	status = writeSuperblock(theDisk, &superblock);
	return (status);
}


static uquad_t getFreeBytes(kernelDisk *theDisk)
{
	// This function returns the amount of free disk space, in bytes.

	extInternalData *extData = NULL;

	if (!initialized)
		return (0);

	// Check params
	if (!theDisk)
	{
		kernelError(kernel_error, "NULL parameter");
		return (0);
	}

	kernelDebug(debug_fs, "EXT get free bytes on disk %s", theDisk->name);

	// Get the EXT data for the requested filesystem
	extData = getExtData(theDisk);
	if (!extData)
		return (0);

	return (extData->superblock.free_blocks_count * extData->blockSize);
}


static int mount(kernelDisk *theDisk)
{
	// This function initializes the filesystem driver by gathering all of
	// the required information from the boot sector.  In addition, it
	// dynamically allocates memory space for the "used" and "free" file and
	// directory structure arrays.

	int status = 0;
	extInternalData *extData = NULL;
	unsigned incompatFlags = 0;

	if (!initialized)
		return (status = ERR_NOTINITIALIZED);

	// Check params
	if (!theDisk)
	{
		kernelError(kernel_error, "NULL parameter");
		return (status = ERR_NULLPARAMETER);
	}

	kernelDebug(debug_fs, "EXT mount disk %s", theDisk->name);

	// The filesystem data cannot exist
	theDisk->filesystem.filesystemData = NULL;

	// Get the EXT data for the requested filesystem.  We don't need the info
	// right now -- we just want to collect it.
	extData = getExtData(theDisk);
	if (!extData)
		return (status = ERR_BADDATA);

	// Check for fatal incompatibilities
	incompatFlags = extData->superblock.feature_incompat;

	// Warn about any flags we want to warn about
	if (incompatFlags & EXT_INCOMPAT_RECOVER)
		kernelError(kernel_warn, "Filesystem was not unmounted cleanly");

	// Mask out any flags we do support, or don't care about
	incompatFlags &= ~(EXT_INCOMPAT_FLEXBG | EXT_INCOMPAT_EXTENTS |
		EXT_INCOMPAT_RECOVER | EXT_INCOMPAT_FILETYPE);

	// The rest cause a failure
	if (incompatFlags)
	{
		kernelError(kernel_error, "Filesystem requires unsupported features "
			"(0x%x)", incompatFlags);

		if (incompatFlags & EXT_INCOMPAT_INLINEDATA)
			kernelDebugError("Data in inode structures not supported");
		if (incompatFlags & EXT_INCOMPAT_LARGEDIR)
			kernelDebugError("Large directories not supported");
		if (incompatFlags & EXT_INCOMPAT_DIRDATA)
			kernelDebugError("Data in directory entries not supported");
		if (incompatFlags & EXT_INCOMPAT_EAINODE)
			kernelDebugError("Extended attributes in inodes not supported");
		if (incompatFlags & EXT_INCOMPAT_MMP)
			kernelDebugError("Multiple mount protection not supported");
		if (incompatFlags & EXT_INCOMPAT_64BIT)
			kernelDebugError("64-bit block counts not supported");
		if (incompatFlags & EXT_INCOMPAT_METABG	)
			kernelDebugError("Meta block groups not supported");
		if (incompatFlags & EXT_INCOMPAT_JOURNALDEV)
			kernelDebugError("Separate journal device not supported");
		if (incompatFlags & EXT_INCOMPAT_COMPRESSION)
			kernelDebugError("Compression not supported");

		return (status = ERR_NOTIMPLEMENTED);
	}

	// Read the filesystem's root directory and attach it to the filesystem
	// structure
	status = readRootDir(extData, theDisk);
	if (status < 0)
	{
		kernelError(kernel_error, "Unable to read the filesystem's root "
			"directory");
		return (status = ERR_BADDATA);
	}

	// Set the proper filesystem type name on the disk structure
	strcpy((char *) theDisk->fsType, FSNAME_EXT"2");

	// Read-only for now
	theDisk->filesystem.readOnly = 1;

	kernelDebug(debug_fs, "EXT mounted disk %s", theDisk->name);

	return (status = 0);
}


static int unmount(kernelDisk *theDisk)
{
	// This function releases all of the stored information about a given
	// filesystem.

	int status = 0;
	extInternalData *extData = NULL;

	if (!initialized)
		return (status = ERR_NOTINITIALIZED);

	// Check params
	if (!theDisk)
	{
		kernelError(kernel_error, "NULL parameter");
		return (status = ERR_NULLPARAMETER);
	}

	kernelDebug(debug_fs, "EXT unmount disk %s", theDisk->name);

	// Get the EXT data for the requested filesystem
	extData = getExtData(theDisk);
	if (!extData)
		return (status = ERR_BADDATA);

	// Deallocate any global filesystem memory
	kernelFree(extData->groups);
	kernelFree((void *) extData);

	// Finally, remove the reference from the filesystem structure
	theDisk->filesystem.filesystemData = NULL;

	return (status = 0);
}


static int newEntry(kernelFileEntry *entry)
{
	// This function gets called when there's a new kernelFileEntry in the
	// filesystem (either because a file was created or because some existing
	// thing has been newly read from disk).  This gives us an opportunity
	// to attach EXT-specific data to the file entry

	int status = 0;

	if (!initialized)
		return (status = ERR_NOTINITIALIZED);

	// Check params
	if (!entry)
	{
		kernelError(kernel_error, "NULL parameter");
		return (status = ERR_NULLPARAMETER);
	}

	kernelDebug(debug_fs, "EXT get new file entry");

	// Make sure there isn't already some sort of data attached to this
	// file entry, and that there is a filesystem attached
	if (entry->driverData)
	{
		kernelError(kernel_error, "Entry already has private filesystem data");
		return (status = ERR_ALREADY);
	}

	// Make sure there's an associated filesystem
	if (!entry->disk)
	{
		kernelError(kernel_error, "Entry has no associated disk");
		return (status = ERR_NOCREATE);
	}

	entry->driverData = kernelMalloc(sizeof(extInode));
	if (!entry->driverData)
		return (status = ERR_MEMORY);

	return (status = 0);
}


static int inactiveEntry(kernelFileEntry *entry)
{
	// This function gets called when a kernelFileEntry is about to be
	// deallocated by the system (either because a file was deleted or because
	// the entry is simply being unbuffered).  This gives us an opportunity
	// to deallocate our EXT-specific data from the file entry

	int status = 0;

	if (!initialized)
		return (status = ERR_NOTINITIALIZED);

	// Check params
	if (!entry)
	{
		kernelError(kernel_error, "NULL parameter");
		return (status = ERR_NULLPARAMETER);
	}

	kernelDebug(debug_fs, "EXT release file entry");

	if (!entry->driverData)
	{
		kernelError(kernel_error, "File entry has no private filesystem data");
		return (status = ERR_ALREADY);
	}

	// Erase all of the data in this entry
	memset(entry->driverData, 0, sizeof(extInode));

	// Release the inode data structure attached to this file entry.
	kernelFree(entry->driverData);

	// Remove the reference
	entry->driverData = NULL;

	return (status = 0);
}


static int resolveLink(kernelFileEntry *linkEntry)
{
	// This is called by the kernelFile.c code when we have registered a
	// file or directory as a link, but not resolved it, and now it needs
	// to be resolved.  By default this driver never resolves EXT symbolic
	// links until they are needed, so this will get called when the system
	// wants to read/write the symbolically-linked directory or file.

	int status = 0;
	extInternalData *extData = NULL;
	extInode *inode = NULL;
	char fileName[MAX_PATH_NAME_LENGTH];
	char tmpName[MAX_PATH_NAME_LENGTH];
	kernelFileEntry *targetEntry = NULL;
	char *buffer = NULL;

	if (!initialized)
		return (status = ERR_NOTINITIALIZED);

	// Check params
	if (!linkEntry)
	{
		kernelError(kernel_error, "NULL parameter");
		return (status = ERR_NULLPARAMETER);
	}

	kernelDebug(debug_fs, "EXT resolve link %s", linkEntry->name);

	// Get the EXT data for the requested filesystem
	extData = getExtData(linkEntry->disk);
	if (!extData)
		return (0);

	inode = (extInode *) linkEntry->driverData;
	if (!inode)
	{
		kernelError(kernel_error, "Link entry has no private data");
		return (status = ERR_NODATA);
	}

	// If the file size is 64 bytes or less, the name will be stored right
	// in the inode.
	if (inode->size <= 64)
	{
		strncpy(fileName, (char *) inode->u.block, inode->size);
	}
	else
	{
		// We have to read the first data block in order to get the filename.

		buffer = kernelMalloc(extData->blockSize);
		if (!buffer)
			return (status = ERR_MEMORY);

		status = kernelDiskReadSectors((char *) extData->disk->name,
			getSectorNumber(extData, inode->u.block[0]),
			extData->sectorsPerBlock, buffer);
		if (status < 0)
		{
			kernelFree(buffer);
			return (status);
		}

		// Copy the data into the fileName buffer
		strncpy(fileName, buffer, inode->size);
		kernelFree(buffer);
	}

	fileName[inode->size] = '\0';

	// Need to make sure it's an absolute pathname.
	if (fileName[0] != '/')
	{
		char tmpPath[MAX_PATH_LENGTH];
		kernelFileGetFullName(linkEntry->parentDirectory, tmpPath,
			MAX_PATH_LENGTH);
		sprintf(tmpName, "%s/%s", tmpPath, fileName);
		kernelFileFixupPath(tmpName, fileName);
	}

	// Try to get the entry for the specified pathname
	targetEntry = kernelFileLookup(fileName);
	if (!targetEntry)
	{
		// Not found.  We'll try one more thing.  If the fileName *is* an
		// absolute path, we will try prepending the mount point in case
		// it's supposed to be relative to the start of the filesystem.

		if (fileName[0] != '/')
			return (status = ERR_NOSUCHFILE);

		sprintf(tmpName, "%s/%s", ((kernelDisk *) linkEntry->disk)->
			filesystem.mountPoint, fileName);
		kernelFileFixupPath(tmpName, fileName);

		// Try again
		targetEntry = kernelFileLookup(fileName);
		if (!targetEntry)
			return (status = ERR_NOSUCHFILE);
	}

	linkEntry->contents = targetEntry;
	return (status = 0);
}


static int readFile(kernelFileEntry *theFile, unsigned blockNum,
	unsigned blocks, unsigned char *buffer)
{
	// This function is the "read file" function that the filesystem
	// driver exports to the world.  It is mainly a wrapper for the
	// internal function of the same purpose, but with some additional
	// argument checking.  Returns 0 on success, negative otherwise.

	int status = 0;
	extInternalData *extData = NULL;
	extInode *inode = NULL;

	if (!initialized)
		return (status = ERR_NOTINITIALIZED);

	// Check params
	if (!theFile || !buffer)
	{
		kernelError(kernel_error, "NULL parameter");
		return (status = ERR_NULLPARAMETER);
	}

	kernelDebug(debug_fs, "EXT read file %s", theFile->name);

	// Get the EXT data for the requested filesystem
	extData = getExtData(theFile->disk);
	if (!extData)
		return (status = ERR_BADDATA);

	inode = (extInode *) theFile->driverData;
	if (!inode)
	{
		kernelError(kernel_error, "File \"%s\" has no private data",
			theFile->name);
		return (status = ERR_NODATA);
	}

	return (read(extData, theFile, blockNum, blocks, buffer));
}


static int readDir(kernelFileEntry *directory)
{
	// This function receives an emtpy file entry structure, which represents
	// a directory whose contents have not yet been read.  This will fill the
	// directory structure with its appropriate contents.  Returns 0 on
	// success, negative otherwise.

	int status = 0;
	extInternalData *extData = NULL;

	if (!initialized)
		return (status = ERR_NOTINITIALIZED);

	// Check params
	if (!directory)
	{
		kernelError(kernel_error, "NULL parameter");
		return (status = ERR_NULLPARAMETER);
	}

	kernelDebug(debug_fs, "EXT read directory %s", directory->name);

	// Make sure there's an inode attached
	if (!directory->driverData)
	{
		kernelError(kernel_error, "Directory \"%s\" has no private data",
			directory->name);
		return (status = ERR_NODATA);
	}

	// Get the EXT data for the requested filesystem
	extData = getExtData(directory->disk);
	if (!extData)
		return (status = ERR_BADDATA);

	return (status = scanDirectory(extData, directory));
}


static kernelFilesystemDriver fsDriver = {
	FSNAME_EXT,	// Driver name
	detect,
	format,
	clobber,
	NULL,		// driverCheck
	NULL,		// driverDefragment
	NULL,		// driverStat
	getFreeBytes,
	NULL,		// driverResizeConstraints
	NULL,		// driverResize
	mount,
	unmount,
	newEntry,
	inactiveEntry,
	resolveLink,
	readFile,
	NULL,		// driverWriteFile
	NULL,		// driverCreateFile
	NULL,		// driverDeleteFile
	NULL,		// driverFileMoved
	readDir,
	NULL,		// driverWriteDir
	NULL,		// driverMakeDir
	NULL,		// driverRemoveDir
	NULL,		// driverTimestamp
	NULL		// driverSetBlocks
};


/////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////
//
//  Below here, the functions are exported for external use
//
/////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////

int kernelFilesystemExtInitialize(void)
{
	// Initialize the driver

	int status = 0;

	// Register our driver
	status = kernelSoftwareDriverRegister(extDriver, &fsDriver);

	initialized = 1;

	return (status);
}

