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
//  kernelFilesystemFat.h
//

#if !defined(_KERNELFILESYSTEMFAT_H)

#include "kernelDisk.h"
#include "kernelLock.h"
#include <sys/fat.h>

// Definitions

// Structures used internally by the filesystem driver to keep track
// of files and directories

typedef enum {
	fat12, fat16, fat32, fatUnknown

} fatType;

typedef volatile struct {
	// These are taken directly from directory entries
	char shortAlias[12];
	unsigned attributes;
	unsigned res;
	unsigned timeTenth;
	unsigned startCluster;

} fatEntryData;

// This structure will contain all of the internal global data
// for a particular filesystem on a particular volume
typedef volatile struct {
	fatBPB bpb;
	fatFsInfo fsInfo;
	unsigned char rootDirLabel[FAT_BYTES_PER_DIR_ENTRY];

	// Things that need to be calculated after we have all of the FAT
	// volume data from the boot block (see above)
	fatType fsType;
	unsigned totalSects;
	unsigned rootDirSects;
	unsigned fatSects;
	unsigned dataSects;
	unsigned dataClusters;
	unsigned terminalClust;

	// Bitmap of free clusters
	unsigned char *freeClusterBitmap;
	unsigned freeClusters;
	lock freeBitmapLock;

	// Miscellany
	kernelDisk *disk;

} fatInternalData;

#define fatClusterToLogical(fatData, clusterNum) \
	((uquad_t)(((clusterNum) - 2) * (fatData)->bpb.sectsPerClust) + \
	(fatData)->bpb.rsvdSectCount + \
	((fatData)->bpb.numFats * (fatData)->fatSects) + (fatData)->rootDirSects)

#define fatClusterBytes(fatData) \
	((fatData)->bpb.bytesPerSect * (fatData)->bpb.sectsPerClust)

#define _KERNELFILESYSTEMFAT_H
#endif

