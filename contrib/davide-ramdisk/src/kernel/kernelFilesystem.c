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
//  kernelFilesystem.c
//

// This file contains the routines designed to manage file systems

#include "kernelFilesystem.h"
#include "kernelFile.h"
#include "kernelMultitasker.h"
#include "kernelLock.h"
#include "kernelSysTimer.h"
#include "kernelMalloc.h"
#include "kernelMisc.h"
#include "kernelLog.h"
#include "kernelError.h"
#include <string.h>

static kernelFilesystemDriver *driverArray[MAX_FILESYSTEMS];
static int driverCounter = 0;


static void populateDriverArray(void)
{
  if (!driverCounter)
    {
      driverArray[driverCounter++] = kernelDriverGet(extDriver);
      driverArray[driverCounter++] = kernelDriverGet(fatDriver);
      driverArray[driverCounter++] = kernelDriverGet(isoDriver);
      driverArray[driverCounter++] = kernelDriverGet(linuxSwapDriver);
      driverArray[driverCounter++] = kernelDriverGet(ntfsDriver);
    }
}


static kernelFilesystemDriver *getDriver(const char *name)
{
  int count;

  if (!driverCounter)
    populateDriverArray();

  // First, look for exact matches
  for (count = 0; count < driverCounter; count ++)
    if (!strcasecmp(name, driverArray[count]->driverTypeName))
      return (driverArray[count]);

  // Next, look for partial matches
  for (count = 0; count < driverCounter; count ++)
    if (!strncasecmp(name, driverArray[count]->driverTypeName, strlen(name)))
      return (driverArray[count]);

  // Not found
  return (NULL);
}


static kernelFilesystemDriver *detectType(kernelDisk *theDisk)
{
  // This function takes a disk structure (initialized, with its driver
  // accounted for) and calls functions to determine its type.  At the
  // moment there will be a set number of known filesystem types that
  // will be more-or-less hard-coded into this routine.  Of course this
  // isn't desirable and should/will be fixed to be more flexible in 
  // the future.  The function returns an enumeration value reflecting
  // the type it found (including possibly "unknown").

  int status = 0;
  kernelFilesystemDriver *tmpDriver = NULL;
  kernelFilesystemDriver *driver = NULL;
  int count;

  // We will assume that the detection routines being called will do
  // all of the necessary checking of the kernelDisk before using
  // it.  Since we're not actually using it here, we won't duplicate
  // that work.

  if (!driverCounter)
    populateDriverArray();

  // If it's a CD-ROM, only check ISO
  if (theDisk->physical->flags & DISKFLAG_CDROM)
    {
      tmpDriver = getDriver(FSNAME_ISO);
      if (tmpDriver)
	{
	  status = tmpDriver->driverDetect(theDisk);
	  if (status == 1)
	    {
	      driver = tmpDriver;
	      goto finished;
	    }
	}
    }
  else
    {
      // Check them all

      for (count = 0; count < driverCounter; count ++)
	{
	  status = driverArray[count]->driverDetect(theDisk);
	  if (status == 1)
	    {
	      driver = driverArray[count];
	      goto finished;
	    }
	}
    }
  
 finished:

  if (driver)
    {
      // Copy this filesystem type name into the disk structure.  The
      // filesystem driver can change it if desired.
      strncpy((char *) theDisk->fsType, driver->driverTypeName,
	      FSTYPE_MAX_NAMELENGTH);

      // Set the operation flags based on which filesystem functions are
      // non-NULL.
      theDisk->opFlags = 0;
      if (driver->driverFormat)
	theDisk->opFlags |= FS_OP_FORMAT;
      if (driver->driverClobber)
	theDisk->opFlags |= FS_OP_CLOBBER;
      if (driver->driverCheck)
	theDisk->opFlags |= FS_OP_CHECK;
      if (driver->driverDefragment)
	theDisk->opFlags |= FS_OP_DEFRAG;
      if (driver->driverStat)
	theDisk->opFlags |= FS_OP_STAT;
      if (driver->driverResizeConstraints)
	theDisk->opFlags |= FS_OP_RESIZECONST;
      if (driver->driverResize)
	theDisk->opFlags |= FS_OP_RESIZE;
    }

  return (driver);
}


/////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////
//
//  Below here, the functions are exported for external use
//
/////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////


int kernelFilesystemScan(kernelDisk *theDisk)
{
  // Scan a logical disk and see if we can determine the filesystem type

  int status = 0;
  kernelPhysicalDisk *physicalDisk = NULL;

  // Check params
  if (theDisk == NULL)
    {
      kernelError(kernel_error, "Disk parameter is NULL");
      return (status = ERR_NULLPARAMETER);
    }

  physicalDisk = theDisk->physical;

  // Is it removable?  If so, make sure there's media
  if (physicalDisk->flags & DISKFLAG_REMOVABLE)
    {
      if (!kernelDiskGetMediaState((char *) physicalDisk->name))
	return (status = ERR_NOMEDIA);
    }

  // Scan a disk to determine its filesystem type, etc.
  strcpy((char *) theDisk->fsType, "unknown");
  theDisk->filesystem.driver = detectType(theDisk);

  if (theDisk->filesystem.driver)
    return (status = 0);
  else
    return (status = ERR_INVALID);
}


int kernelFilesystemFormat(const char *diskName, const char *type,
			   const char *label, int longFormat, progress *prog)
{
  // This function is a wrapper for the filesystem driver's 'format' function,
  // if applicable.

  int status = 0;
  kernelDisk *theDisk = NULL;
  kernelFilesystemDriver *theDriver = NULL;
 
  // Check params
  if (diskName == NULL)
    return (status = ERR_NULLPARAMETER);

  theDisk = kernelDiskGetByName(diskName);
  if (theDisk == NULL)
    {
      kernelError(kernel_error, "No such disk \"%s\"", diskName);
      return (status = ERR_NULLPARAMETER);
    }

  // Get a temporary filesystem driver to use for formatting
  if (!strncasecmp(type, FSNAME_FAT, strlen(FSNAME_FAT)))
    theDriver = getDriver(FSNAME_FAT);
  else if (!strncasecmp(type, FSNAME_EXT, strlen(FSNAME_EXT)))
    theDriver = getDriver(FSNAME_EXT);
  else if (!strncasecmp(type, FSNAME_LINUXSWAP, strlen(FSNAME_LINUXSWAP)))
    theDriver = getDriver(FSNAME_LINUXSWAP);

  if (theDriver == NULL)
    {
      kernelError(kernel_error, "Invalid filesystem type \"%s\" for format!",
		  type);
      return (status = ERR_NOSUCHENTRY);
    }

  // Make sure the driver's formatting routine is not NULL
  if (theDriver->driverFormat == NULL)
    {
      kernelError(kernel_error, "The filesystem driver does not support the "
		  "'format' operation");
      return (status = ERR_NOSUCHFUNCTION);
    }

  // Create the filesystem
  status = theDriver->driverFormat(theDisk, type, label, longFormat, prog);

  // Re-scan the filesystem
  kernelFilesystemScan(theDisk);

  return (status);
}


int kernelFilesystemClobber(const char *diskName)
{
  // This function destroys anything that might cause this disk to be detected
  // as any filesystem we know about.

  int status = 0;
  kernelDisk *theDisk = NULL;
  int count;

  // Check params
  if (diskName == NULL)
    return (status = ERR_NULLPARAMETER);

  theDisk = kernelDiskGetByName(diskName);
  if (theDisk == NULL)
    {
      kernelError(kernel_error, "No such disk \"%s\"", diskName);
      return (status = ERR_NULLPARAMETER);
    }

  if (!driverCounter)
    populateDriverArray();

  for (count = 0; count < driverCounter; count ++)
    {
      if (driverArray[count]->driverClobber)
	{
	  status = driverArray[count]->driverClobber(theDisk);
	  if (status < 0)
	    kernelError(kernel_warn, "Couldn't clobber %s",
			driverArray[count]->driverTypeName);
	}
    }

  // Re-scan the filesystem
  kernelFilesystemScan(theDisk);

  // Finished
  return (status = 0);
}


int kernelFilesystemDefragment(const char *diskName, progress *prog)
{
  // This function is a wrapper for the filesystem driver's 'defragment'
  // function, if applicable.
  
  int status = 0;
  kernelDisk *theDisk = NULL;
  kernelFilesystemDriver *theDriver = NULL;

  // Check params
  if (diskName == NULL)
    return (status = ERR_NULLPARAMETER);

  theDisk = kernelDiskGetByName(diskName);
  if (theDisk == NULL)
    {
      kernelError(kernel_error, "No such disk \"%s\"", diskName);
      return (status = ERR_NULLPARAMETER);
    }

  if (theDisk->filesystem.driver == NULL)
    {
      // Try a scan before we error out.
      if (kernelFilesystemScan(theDisk) < 0)
	{
	  kernelError(kernel_error, "The filesystem type of disk \"%s\" is "
		      "unknown", theDisk->name);
	  return (status = ERR_NOTIMPLEMENTED);
	}
    }

  theDriver = theDisk->filesystem.driver;

  // Make sure the driver's checking routine is not NULL
  if (theDriver->driverDefragment == NULL)
    {
      kernelError(kernel_error, "The filesystem driver does not support the "
		  "'defragment' operation");
      return (status = ERR_NOSUCHFUNCTION);
    }

  // Defrag the filesystem
  status = theDriver->driverDefragment(theDisk, prog);
  return (status);
}


int kernelFilesystemStat(const char *diskName, kernelFilesystemStats *stat)
{
  // This function is a wrapper for the filesystem driver's 'stat' function,
  // if applicable.

  int status = 0;
  kernelDisk *theDisk = NULL;
  kernelFilesystemDriver *theDriver = NULL;

  // Check params
  if ((diskName == NULL) || (stat == NULL))
    return (status = ERR_NULLPARAMETER);

  theDisk = kernelDiskGetByName(diskName);
  if (theDisk == NULL)
    {
      kernelError(kernel_error, "No such disk \"%s\"", diskName);
      return (status = ERR_NULLPARAMETER);
    }

  if (theDisk->filesystem.driver == NULL)
    {
      // Try a scan before we error out.
      if (kernelFilesystemScan(theDisk) < 0)
	{
	  kernelError(kernel_error, "The filesystem type of disk \"%s\" is "
		      "unknown", theDisk->name);
	  return (status = ERR_NOTIMPLEMENTED);
	}
    }

  theDriver = theDisk->filesystem.driver;

  // Make sure the driver's checking routine is not NULL
  if (theDriver->driverStat == NULL)
    {
      kernelError(kernel_error, "The filesystem driver does not support the "
		  "'stat' operation");
      return (status = ERR_NOSUCHFUNCTION);
    }

  // Resize the filesystem
  status = theDriver->driverStat(theDisk, stat);
  return (status);
}


int kernelFilesystemResizeConstraints(const char *diskName,
				      unsigned *minBlocks,
				      unsigned *maxBlocks)
{
  // This function is a wrapper for the filesystem driver's 'get resize
  // constraints' function, if applicable.

  int status = 0;
  kernelDisk *theDisk = NULL;
  kernelFilesystemDriver *theDriver = NULL;

  // Check params
  if ((diskName == NULL) || (minBlocks == NULL) || (maxBlocks == NULL))
    return (status = ERR_NULLPARAMETER);

  theDisk = kernelDiskGetByName(diskName);
  if (theDisk == NULL)
    {
      kernelError(kernel_error, "No such disk \"%s\"", diskName);
      return (status = ERR_NULLPARAMETER);
    }

  if (theDisk->filesystem.driver == NULL)
    {
      // Try a scan before we error out.
      if (kernelFilesystemScan(theDisk) < 0)
	{
	  kernelError(kernel_error, "The filesystem type of disk \"%s\" is "
		      "unknown", theDisk->name);
	  return (status = ERR_NOTIMPLEMENTED);
	}
    }

  theDriver = theDisk->filesystem.driver;

  // Make sure the driver's checking routine is not NULL
  if (theDriver->driverResizeConstraints == NULL)
    {
      kernelError(kernel_error, "The filesystem driver does not support the "
		  "'resize constraints' operation");
      return (status = ERR_NOSUCHFUNCTION);
    }

  // Resize the filesystem
  status = theDriver->driverResizeConstraints(theDisk, minBlocks, maxBlocks);
  return (status);
}


int kernelFilesystemResize(const char *diskName, unsigned blocks,
			   progress *prog)
{
  // This function is a wrapper for the filesystem driver's 'resize'
  // function, if applicable.

  int status = 0;
  kernelDisk *theDisk = NULL;
  kernelFilesystemDriver *theDriver = NULL;

  // Check params
  if (diskName == NULL)
    return (status = ERR_NULLPARAMETER);

  theDisk = kernelDiskGetByName(diskName);
  if (theDisk == NULL)
    {
      kernelError(kernel_error, "No such disk \"%s\"", diskName);
      return (status = ERR_NULLPARAMETER);
    }

  if (theDisk->filesystem.driver == NULL)
    {
      // Try a scan before we error out.
      if (kernelFilesystemScan(theDisk) < 0)
	{
	  kernelError(kernel_error, "The filesystem type of disk \"%s\" is "
		      "unknown", theDisk->name);
	  return (status = ERR_NOTIMPLEMENTED);
	}
    }

  theDriver = theDisk->filesystem.driver;

  // Make sure the driver's checking routine is not NULL
  if (theDriver->driverResize == NULL)
    {
      kernelError(kernel_error, "The filesystem driver does not support the "
		  "'resize' operation");
      return (status = ERR_NOSUCHFUNCTION);
    }

  // Resize the filesystem
  status = theDriver->driverResize(theDisk, blocks, prog);
  return (status);
}

// modified by Davide Airaghi, added "options" parameter (maybe useful later)
int kernelFilesystemMount(const char *diskName, const char *path, const char *options)
{
  // This function creates and registers (mounts) a new filesystem definition.
  // If successful, it returns the filesystem number of the filesystem it
  // mounted.  Otherwise it returns negative.

  int status = 0;
  char mountPoint[MAX_PATH_LENGTH];
  char parentDirName[MAX_PATH_LENGTH];
  char mountDirName[MAX_NAME_LENGTH];
  kernelDisk *theDisk = NULL;
  kernelFilesystemDriver *theDriver = NULL;
  kernelFileEntry *parentDir = NULL;

  // Check params
  if ((diskName == NULL) || (path == NULL))
    return (status = ERR_NULLPARAMETER);

  theDisk = kernelDiskGetByName(diskName);
  if (theDisk == NULL)
    {
      kernelError(kernel_error, "No such disk \"%s\"", diskName);
      return (status = ERR_NULLPARAMETER);
    }

  if (theDisk->filesystem.driver == NULL)
    {
      // Try a scan before we error out.
      if (kernelFilesystemScan(theDisk) < 0)
	{
	  kernelError(kernel_error, "The filesystem type of disk \"%s\" is "
		      "unknown", theDisk->name);
	  return (status = ERR_NOTIMPLEMENTED);
	}
    }

  if (options != NULL) {
     // at the moment do nothing
     // but this make gcc happier
  }

  theDriver = theDisk->filesystem.driver;

  // Fix up the path of the mount point
  status = kernelFileFixupPath(path, mountPoint);
  if (status < 0)
    return (status);

  kernelLog("Mounting %s filesystem on disk %s", mountPoint, theDisk->name);

  // Make sure that the disk hasn't already been mounted 
  if (theDisk->filesystem.mounted)
    {
      kernelError(kernel_error, "The disk is already mounted at %s",
		  theDisk->filesystem.mountPoint);
      return (status = ERR_ALREADY);
    }

  // If this is NOT the root filesystem we're mounting, we need to make
  // sure that the mount point doesn't already exist.  This is because
  // The root directory of the new filesystem will be inserted into its
  // parent directory here.  This is un-UNIXy.

  if (strcmp(mountPoint, "/"))
    {
      // Make sure the mount point doesn't currently exist
      if (kernelFileLookup(mountPoint))
	{
	  kernelError(kernel_error, "The mount point already exists.");
	  return (status = ERR_ALREADY);
	}

      // Make sure the parent directory of the mount point DOES exist.
      status = kernelFileSeparateLast(mountPoint, parentDirName, mountDirName);
      if (status < 0)
	{
	  kernelError(kernel_error, "Bad path to mount point");
	  return (status);
	}

      parentDir = kernelFileLookup(parentDirName);
      if (parentDir == NULL)
	{
	  kernelError(kernel_error, "Mount point parent directory doesn't "
		      "exist");
	  return (status = ERR_NOCREATE);
	}
    }

  // Make sure the driver's mounting routine is not NULL
  if (theDriver->driverMount == NULL)
    {
      kernelError(kernel_error, "The filesystem driver does not support the "
		  "'mount' operation");
      return (status = ERR_NOSUCHFUNCTION);
    }
  
  // Fill in any information that we already know for this filesystem

  // Make "mountPoint" be the filesystem's mount point
  strcpy((char *) theDisk->filesystem.mountPoint, mountPoint);

  // Get a new file entry for the filesystem's root directory
  theDisk->filesystem.filesystemRoot = kernelFileNewEntry(theDisk);
  if (theDisk->filesystem.filesystemRoot == NULL)
    // Not enough free file structures
    return (status = ERR_NOFREE);

  theDisk->filesystem.filesystemRoot->type = dirT;
  theDisk->filesystem.filesystemRoot->disk = theDisk;

  if (!strcmp(mountPoint, "/"))
    {
      // The root directory has no parent
      theDisk->filesystem.filesystemRoot->parentDirectory = NULL;

      // Set the root filesystem pointer
      status = kernelFileSetRoot(theDisk->filesystem.filesystemRoot);
      if (status < 0)
	{
	  kernelFileReleaseEntry(theDisk->filesystem.filesystemRoot);
	  return (status);
	}
    }
  else
    {
      // If this is not the root filesystem, insert the filesystem's
      // root directory into the file entry tree.
      status =
	kernelFileInsertEntry(theDisk->filesystem.filesystemRoot, parentDir);
      if (status < 0)
	{
	  kernelFileReleaseEntry(theDisk->filesystem.filesystemRoot);
	  return (status);
	}

      ((kernelDisk *) parentDir->disk)->filesystem.childMounts += 1;
    }

  // Mount the filesystem
  status = theDriver->driverMount(theDisk);
  if (status < 0)
    {
      if (strcmp(mountPoint, "/"))
	kernelFileRemoveEntry(theDisk->filesystem.filesystemRoot);
      kernelFileReleaseEntry(theDisk->filesystem.filesystemRoot);
      return (status);
    }

  theDisk->filesystem.mounted += 1;

  // Set the name of the mount point directory
  if (!strcmp(mountPoint, "/"))
    strcpy((char *) theDisk->filesystem.filesystemRoot->name, "/");
  else
    strcpy((char *) theDisk->filesystem.filesystemRoot->name, mountDirName);

  // If the disk is removable and has a 'lock' function, lock it
  if ((theDisk->physical->flags & DISKFLAG_REMOVABLE) &&
      (((kernelDiskOps *) theDisk->physical->driver->ops)->driverSetLockState))
    ((kernelDiskOps *) theDisk->physical->driver->ops)
      ->driverSetLockState(theDisk->physical->deviceNumber, 1);

  return (status = 0);
}


int kernelFilesystemUnmount(const char *path)
{
  // This routine will remove a filesystem structure and its driver from the 
  // lists.  It takes the filesystem mount point name and returns the new
  // number of filesystems in the array.  If the filesystem doesn't exist, 
  // it returns negative

  int status = 0;
  char mountPointName[MAX_PATH_LENGTH];
  kernelDisk *theDisk = NULL;
  kernelFilesystemDriver *theDriver = NULL;
  kernelFileEntry *mountPoint = NULL;
  kernelFileEntry *parentDir = NULL;
  kernelPhysicalDisk *physicalDisk = NULL;

  // Make sure the path name isn't NULL
  if (path == NULL)
    return (status = ERR_NULLPARAMETER);

  // Fix up the path of the mount point
  status = kernelFileFixupPath(path, mountPointName);
  if (status < 0)
    return (status);

  // Get the file entry for the mount point
  mountPoint = kernelFileLookup(mountPointName);
  if (mountPoint == NULL)
    {
      kernelError(kernel_error, "Unable to locate the mount point entry");
      return (status = ERR_NOSUCHDIR);
    }

  theDisk = mountPoint->disk;
  theDriver = theDisk->filesystem.driver;

  if (!(theDisk->filesystem.mounted))
    {
      kernelError(kernel_error, "Disk %s is not mounted", theDisk->name);
      return (status = ERR_ALREADY);
    }

  // DO NOT attempt to unmount the filesystem if there are child mounts.
  // the parent filesystem of all other filesystems
  if (theDisk->filesystem.childMounts)
    {
      kernelError(kernel_error, "Cannot unmount %s when child filesystems "
		  "are still mounted", mountPointName);
      return (status = ERR_BUSY);
    }

  // Starting at the mount point, unbuffer all of the filesystem's files
  // from the file entry tree
  status = kernelFileUnbufferRecursive(mountPoint);
  if (status < 0)
    return (status);

  // Do a couple of additional things if this is not the root directory
  if (strcmp(mountPointName, "/") != 0)
    {
      if (mountPoint->parentDirectory)
	{
	  parentDir = mountPoint->parentDirectory;
	  ((kernelDisk *) parentDir->disk)->filesystem.childMounts -= 1;
	}

      // Remove the mount point's file entry from its parent directory
      kernelFileRemoveEntry(mountPoint);
    }

  // If the driver's unmount routine is not NULL, call it
  if (theDriver->driverUnmount)
    theDriver->driverUnmount(theDisk);

  // It doesn't matter whether the unmount call was "successful".  If it
  // wasn't, there's really nothing we can do about it from here.

  theDisk->filesystem.mounted = 0;
  theDisk->filesystem.mountPoint[0] = '\0';
  theDisk->filesystem.filesystemRoot = NULL;
  theDisk->filesystem.childMounts = 0;
  theDisk->filesystem.filesystemData = NULL;
  theDisk->filesystem.caseInsensitive = 0;
  theDisk->filesystem.readOnly = 0;

  // Sync the disk cache
  status = kernelDiskSyncDisk((char *) theDisk->name);
  if (status < 0)
    kernelError(kernel_warn, "Unable to sync disk \"%s\" after unmount",
		theDisk->name);

  physicalDisk = theDisk->physical;

  // If this is a removable disk, invalidate the disk cache
  if (physicalDisk->flags & DISKFLAG_REMOVABLE)
    {
      status = kernelDiskInvalidateCache((char *) physicalDisk->name);
      if (status < 0)
	kernelError(kernel_warn, "Unable to invalidate \"%s\" disk cache "
		    "before mount", theDisk->name);

      // If it has an 'unlock' function, unlock it
      if (((kernelDiskOps *) physicalDisk->driver->ops)->driverSetLockState)
	((kernelDiskOps *) physicalDisk->driver->ops)
	  ->driverSetLockState(physicalDisk->deviceNumber, 0);
    }

  return (status);
}


int kernelFilesystemCheck(const char *diskName, int force, int repair,
			  progress *prog)
{
  // This function is a wrapper for the filesystem driver's 'check' function,
  // if applicable.

  int status = 0;
  kernelDisk *theDisk = NULL;
  kernelFilesystemDriver *theDriver = NULL;
 
  // Check params
  if (diskName == NULL)
    return (status = ERR_NULLPARAMETER);

  theDisk = kernelDiskGetByName(diskName);
  if (theDisk == NULL)
    {
      kernelError(kernel_error, "No such disk \"%s\"", diskName);
      return (status = ERR_NULLPARAMETER);
    }

  if (theDisk->filesystem.driver == NULL)
    {
      // Try a scan before we error out.
      if (kernelFilesystemScan(theDisk) < 0)
	{
	  kernelError(kernel_error, "The filesystem type of disk \"%s\" is "
		      "unknown", theDisk->name);
	  return (status = ERR_NOTIMPLEMENTED);
	}
    }

  theDriver = theDisk->filesystem.driver;

  // Make sure the driver's checking routine is not NULL
  if (theDriver->driverCheck == NULL)
    {
      kernelError(kernel_error, "The filesystem driver does not support the "
		  "'check' operation");
      return (status = ERR_NOSUCHFUNCTION);
    }

  // Check the filesystem
  status = theDriver->driverCheck(theDisk, force, repair, prog);
  return (status);
}


unsigned kernelFilesystemGetFree(const char *path)
{
  // This is merely a wrapper function for the equivalent function
  // in the requested filesystem's own driver.  It takes nearly-identical
  // arguments and returns the same status as the driver function.

  int status = 0;
  unsigned freeSpace = 0;
  char mountPoint[MAX_PATH_LENGTH];
  kernelFileEntry *fileEntry = NULL;
  kernelDisk *theDisk = NULL;
  kernelFilesystemDriver *theDriver = NULL;

  // Make sure the path name isn't NULL
  if (path == NULL)
    return (freeSpace = 0);

  // Fix up the path of the mount point
  status = kernelFileFixupPath(path, mountPoint);
  if (status < 0)
    return (freeSpace = 0);

  // Locate the mount point
  fileEntry = kernelFileLookup(mountPoint);
  if (fileEntry == NULL)
    {
      kernelError(kernel_error, "No filesystem mounted at %s", mountPoint);
      return (freeSpace = 0);
    }

  // Find the filesystem structure based on its name
  theDisk = fileEntry->disk;
  if (theDisk == NULL)
    {
      kernelError(kernel_error, "No disk for mount point \"%s\"", mountPoint);
      return (status = ERR_BUG);
    }

  if (theDisk->filesystem.driver == NULL)
    {
      // Try a scan before we error out.
      if (kernelFilesystemScan(theDisk) < 0)
	{
	  kernelError(kernel_error, "The filesystem type of disk \"%s\" is "
		      "unknown", theDisk->name);
	  return (status = ERR_NOTIMPLEMENTED);
	}
    }

  theDriver = theDisk->filesystem.driver;

  // OK, we just have to check on the filsystem driver function we want
  // to call
  if (theDriver->driverGetFree == NULL)
    {
      kernelError(kernel_error, "The filesystem driver does not support the "
		  "'getFree' operation");
      // Report NO free space
      return (freeSpace = 0);
    }

  // Lastly, we can call our target function
  freeSpace = theDriver->driverGetFree(theDisk);

  // Return the same value as the driver function.
  return (freeSpace);
}


unsigned kernelFilesystemGetBlockSize(const char *path)
{
  // This function simply returns the block size of the filesystem
  // that contains the specified path.

  int status = 0;
  unsigned blockSize = 0;
  char fixedPath[MAX_PATH_LENGTH];
  kernelFileEntry *fileEntry = NULL;

  // Make sure the path name isn't NULL
  if (path == NULL)
    return (blockSize = 0);

  // Fix up the path of the mount point
  status = kernelFileFixupPath(path, fixedPath);
  if (status < 0)
    return (blockSize = 0);

  // Locate the mount point
  fileEntry = kernelFileLookup(fixedPath);
  if (fileEntry == NULL)
    {
      kernelError(kernel_error, "No filesystem mounted at %s", fixedPath);
      return (blockSize = 0);
    }

  // Return the block size
  return (blockSize = ((kernelDisk *) fileEntry->disk)->filesystem.blockSize);
}
