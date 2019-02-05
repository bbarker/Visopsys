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
//  Davide Airaghi
//  kernelIdeDriver.h
//

// This header file contains definitions for the kernel's standard RamDisk driver

#if !defined(_KERNELRAMDISKDRIVER_H)

#include "kernelLock.h"
#include "kernelDisk.h"

#define MAX_RAM_DISKS 7
#define RAM_DISK_SECTOR_SIZE 512
#define MAX_RAM_DISK_SIZE 64*1024*2*RAM_DISK_SECTOR_SIZE
#define RAM_DISK_MULTI_SECTORS 8
#define RAM_DISK_UNINITIALIZED_SECTORS 0

typedef struct {

  unsigned len;
  void * data;

} kernelRamDiskData;

typedef struct {
    char name[DISK_MAX_NAMELENGTH];
    int created;
    unsigned size;
    int sectorSize;
    int readOnly;
    int mounted;
    char mountPoint[MAX_PATH_LENGTH];
    char fsType[FSTYPE_MAX_NAMELENGTH];
} kernelRamDiskInfoData;


int kernelRamDiskCreate(int numDisk, unsigned size);
int kernelRamDiskDestroy(int numDisk);
int kernelRamDiskInfo(int numDisk, kernelRamDiskInfoData * rdinfo);

#define _KERNELRAMDISKDRIVER_
#endif
