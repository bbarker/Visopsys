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
//  kernelRamDiskDriver.c
//

// Driver for RAMDisk , Davide Airaghi

#include "kernelRamDiskDriver.h"
#include "kernelDisk.h"
#include "kernelProcessorX86.h"
#include "kernelSysTimer.h"
#include "kernelMalloc.h"
#include "kernelMain.h"
#include "kernelMisc.h"
#include "kernelLog.h"
#include "kernelError.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static kernelPhysicalDisk disks[MAX_RAM_DISKS];

static lock ram_lock[MAX_RAM_DISKS];

static int readWriteSectors(int driveNum, unsigned logicalSector,
			    unsigned numSectors, void *buffer, int read)
{
  // This routine reads or writes sectors to/from the drive.  Returns 0 on
  // success, negative otherwise.
  
  int status = 0;
  unsigned start = 0;
  unsigned len = 0;
  kernelRamDiskData * krdata = NULL;
  
  if (driveNum >= MAX_RAM_DISKS || driveNum < 0) {
    kernelError(kernel_error,"RamDisk ram%d doesn't exists",driveNum);
    return ERR_NOSUCHENTRY;
  }

  if (!disks[driveNum].name[0])
    {
      kernelError(kernel_error, "No such ram drive %d", driveNum);
      return (status = ERR_NOSUCHENTRY);
    }

  if (disks[driveNum].extra == NULL) {
      kernelError(kernel_error, "ram drive %d without data!", driveNum);
      return (status = ERR_NOMEDIA);
  }

  start = logicalSector*RAM_DISK_SECTOR_SIZE;
  len = numSectors*RAM_DISK_SECTOR_SIZE;

  krdata = ((kernelRamDiskData *)(disks[driveNum].extra));
  if (krdata->len == 0 || krdata->len < start || krdata->len < (start+len-1)) {
      kernelError(kernel_error, "request to ram drive %d out of bounds!", driveNum);
      return (status = ERR_BOUNDS);
  }

  // Wait for a lock 
  status = kernelLockGet(&ram_lock[driveNum]);
  if (status < 0)
    return (status);
  
  // read/write

  // kernelError(kernel_error,"start-len-end max: %u %u %u %u\n",(unsigned)&(krdata->data[start]), len, (unsigned)&(krdata->data[start])+len-1, (unsigned)(&(krdata->data[krdata->len-1])));
  
  if (read) {
    kernelMemCopy(&(krdata->data[start]), buffer, len);
  }
  else  {
    kernelMemCopy(buffer, &(krdata->data[start]), len);
  }
  // We are finished.  The data should be transferred.
  
  // Unlock 
  kernelLockRelease(&ram_lock[driveNum]);
  
  return (status = 0);
}


static int driverReadSectors(int driveNum, unsigned logicalSector,
			     unsigned numSectors, void *buffer)
{
  // This routine is a wrapper for the readWriteSectors routine.
  return (readWriteSectors(driveNum, logicalSector, numSectors, buffer,
			   1));  // Read operation
}


static int driverWriteSectors(int driveNum, unsigned logicalSector,
			      unsigned numSectors, const void *buffer)
{
  // This routine is a wrapper for the readWriteSectors routine.
  return (readWriteSectors(driveNum, logicalSector, numSectors,
			   (void *) buffer, 0));  // Write operation
}


static int driverDetect(void *parent, kernelDriver *driver)
{
  // This routine is used to detect and initialize each device, as well as
  // registering each one with any higher-level interfaces.  Also does
  // general driver initialization.
  
  int status = 0;
  int driveNum = 0;
  int numberRamDisks = 0;
  kernelDevice *devices = NULL;

  kernelLog("Examining Ram Disks...");

  for (driveNum = 0; (driveNum < MAX_RAM_DISKS); driveNum ++)
  {

      disks[driveNum].description = "RAM Disk";
      disks[driveNum].readOnly = 0;
      disks[driveNum].deviceNumber = driveNum;
      disks[driveNum].dmaChannel = -1;
      disks[driveNum].extra = NULL;      
      disks[driveNum].driver = driver;
  
      kernelLog("Disk %d is RAM Disk", driveNum);
	      
      sprintf((char *) disks[driveNum].name, "ram%d", driveNum);
      //disks[driveNum].flags = DISKFLAG_LOGICALPHYSICAL | DISKFLAG_HARDDISK;
      disks[driveNum].flags = DISKFLAG_LOGICAL | DISKFLAG_REMOVABLE;
      
      disks[driveNum].heads = 0;
      disks[driveNum].cylinders = 0;
      disks[driveNum].sectorsPerCylinder = 0;
      disks[driveNum].numSectors = RAM_DISK_UNINITIALIZED_SECTORS;
      disks[driveNum].sectorSize = 0;
      disks[driveNum].motorState = 0;
      disks[driveNum].skip_cache = 1;
      
      disks[driveNum].sectorSize = RAM_DISK_SECTOR_SIZE;

      disks[driveNum].multiSectors = RAM_DISK_MULTI_SECTORS;

      numberRamDisks += 1;

  }
     
  // Allocate memory for the device(s)
  devices = kernelMalloc(numberRamDisks * (sizeof(kernelDevice) +
					   sizeof(kernelPhysicalDisk)));
  if (devices == NULL)
    return (status = 0);

  for (driveNum = 0; (driveNum < MAX_RAM_DISKS); driveNum ++)
    {
      if (disks[driveNum].name[0])
	{
	  devices[driveNum].device.class =
	    kernelDeviceGetClass(DEVICECLASS_DISK);
	  devices[driveNum].device.subClass =
	    kernelDeviceGetClass(DEVICESUBCLASS_DISK_RAM);
	  devices[driveNum].driver = driver;
	  devices[driveNum].data = (void *) &disks[driveNum];

	  // Register the disk
	  status = kernelDiskRegisterDevice(&devices[driveNum]);
	  if (status < 0)
	    return (status);

	  status = kernelDeviceAdd(parent, &devices[driveNum]);
	  if (status < 0)
	    return (status);
	}
    }

  return (status = 0);
}


static kernelDiskOps ideOps = {
  NULL,
  NULL,
  NULL, 
  NULL,
  NULL,
  NULL, 
  driverReadSectors,
  driverWriteSectors,
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
  driver->ops = &ideOps;

  return;
}

/////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////
//
// Below here, the functions are exported for user space use
//
/////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////

int kernelRamDiskCreate(int numDisk, unsigned size) {

  char name[16];
  char num[3];
  kernelDisk * disc;
  kernelRamDiskData * krd = NULL;
  int status;
  
  if (numDisk >= MAX_RAM_DISKS || numDisk < 0) {
    kernelError(kernel_error,"RamDisk ram%d doesn't exists",numDisk);
    return ERR_NOSUCHENTRY;
  }
  
  if ((size % RAM_DISK_SECTOR_SIZE)!=0 || size > MAX_RAM_DISK_SIZE) {
    kernelError(kernel_error,"Invalid size given for RamDisk ram%d, value passed must be a positive number multiple of %d",numDisk,RAM_DISK_SECTOR_SIZE);
    return ERR_INVALID;
  }
  
  // Wait for a lock 
  status = kernelLockGet(&ram_lock[numDisk]);
  if (status < 0)
    return (status);

  strcpy(name,"ram");
  itoa(numDisk,num);
  strcat(name,num);

  disc = kernelDiskGetByName(name);
  if (disc == NULL) {
    kernelError(kernel_error,"Ramdisk ram%d is null!",numDisk);
    kernelLockRelease(&ram_lock[numDisk]);
    return ERR_NOTINITIALIZED;
  }

  if (disc->physical->extra != NULL) {
    kernelError(kernel_error,"Ramdisk ram%d is already created. Destroy it first",numDisk);
    kernelLockRelease(&ram_lock[numDisk]);
    return ERR_BUSY;
  }

  krd = kernelMalloc(sizeof(kernelRamDiskData));
  if (krd == NULL) {
    kernelError(kernel_error,"Ramdisk ram%d: unable to get memory for main data",numDisk);
    kernelLockRelease(&ram_lock[numDisk]);
    return ERR_MEMORY;
  }
  
  krd->data = kernelMalloc(size);
  if (krd->data == NULL) {
    kernelError(kernel_error,"Ramdisk ram%d: unable to get memory for sectors' data",numDisk);
    kernelFree(krd);
    kernelLockRelease(&ram_lock[numDisk]);
    return ERR_MEMORY;
  }  
  
  krd->len = size;
  disc->physical->extra = (void *)krd;
  disc->numSectors = disc->physical->numSectors = size / RAM_DISK_SECTOR_SIZE;
  disc->physical->heads = 1;                                                                                                                                                          
  disc->physical->cylinders = 1;
  disc->physical->sectorsPerCylinder = disc->numSectors;
        
  kernelLockRelease(&ram_lock[numDisk]);
  
  return 0;

}

int kernelRamDiskDestroy(int numDisk) {
  char name[16];
  char num[3];
  kernelDisk * disc;
  kernelRamDiskData * krd = NULL;
  int status;
  
  if (numDisk >= MAX_RAM_DISKS || numDisk < 0) {
    kernelError(kernel_error,"RamDisk ram%d doesn't exists",numDisk);
    return ERR_NOSUCHENTRY;
  }
  
  // Wait for a lock 
  status = kernelLockGet(&ram_lock[numDisk]);
  if (status < 0)
    return (status);

  strcpy(name,"ram");
  itoa(numDisk,num);
  strcat(name,num);

  disc = kernelDiskGetByName(name);
  if (disc == NULL) {
    kernelError(kernel_error,"Ramdisk ram%d is null!",numDisk);
    kernelLockRelease(&ram_lock[numDisk]);
    return ERR_NOTINITIALIZED;
  }
  
  if (disc->filesystem.mounted > 0) {
    // Don't destroy a mounted disk
    kernelError(kernel_error,"Unable to destroy Ramdisk ram%d because it's mounted",numDisk);
    kernelLockRelease(&ram_lock[numDisk]);
    return ERR_BUSY;   
  }

  if (disc->physical->extra == NULL) {
    // destroy an already destroyed disk, no problem! ^_^
    kernelLockRelease(&ram_lock[numDisk]);
    return 0; 
  }

  krd = (kernelRamDiskData *) disc->physical->extra;
  if (krd->data != NULL)
    kernelFree(krd->data);
    
  kernelFree(disc->physical->extra);

  disc->physical->extra = NULL;
  disc->numSectors = disc->physical->numSectors = RAM_DISK_UNINITIALIZED_SECTORS;
  disc->physical->heads = 0;
  disc->physical->cylinders = 0;
  disc->physical->sectorsPerCylinder = 0; 
  disc->physical->readOnly = 0;
  
  kernelLockRelease(&ram_lock[numDisk]);
    
  return 0;
}

int kernelRamDiskInfo(int numDisk, kernelRamDiskInfoData * rdinfo) {
  char name[16];
  char num[3];
  kernelDisk * disc;
  int status;
    
  if (rdinfo == NULL) {
    kernelError(kernel_error,"No pointer given to store data");
    return ERR_NULLPARAMETER;
  }
  
  if (numDisk >= MAX_RAM_DISKS || numDisk < 0) {
    kernelError(kernel_error,"RamDisk ram%d doesn't exists",numDisk);
    return ERR_NOSUCHENTRY;
  }
  
  // Wait for a lock 
  status = kernelLockGet(&ram_lock[numDisk]);
  if (status < 0)
    return (status);

  strcpy(name,"ram");
  itoa(numDisk,num);
  strcat(name,num);

  disc = kernelDiskGetByName(name);
  if (disc == NULL) {
    kernelError(kernel_error,"Ramdisk ram%d is null!",numDisk);
    kernelLockRelease(&ram_lock[numDisk]);
    return ERR_NOTINITIALIZED;
  }
  
  strcpy(rdinfo->name,disc->name);
  rdinfo->created = (disc->physical->extra == NULL ? 0 : 1);
  rdinfo->size = (rdinfo->created ? ((kernelRamDiskData*)disc->physical->extra)->len : 0);
  rdinfo->sectorSize = disc->physical->sectorSize;
  rdinfo->readOnly = disc->physical->readOnly;
  rdinfo->mounted = disc->filesystem.mounted;
  strcpy(rdinfo->mountPoint,(rdinfo->mounted ? disc->filesystem.mountPoint: "none"));
  strcpy(rdinfo->fsType,(rdinfo->mounted ? disc->fsType: "none"));
  
  kernelLockRelease(&ram_lock[numDisk]);
    
  return 0;
}


