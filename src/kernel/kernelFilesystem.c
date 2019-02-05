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
//  kernelFilesystem.c
//

// This file contains the functions designed to manage file systems

#include "kernelFilesystem.h"
#include "kernelFile.h"
#include "kernelMultitasker.h"
#include "kernelLock.h"
#include "kernelMalloc.h"
#include "kernelLog.h"
#include "kernelError.h"
#include <string.h>

static kernelFilesystemDriver *driverArray[MAX_FILESYSTEMS];
static int driverCounter = 0;

static void populateDriverArray(void)
{
	driverArray[driverCounter++] = kernelSoftwareDriverGet(extDriver);
	driverArray[driverCounter++] = kernelSoftwareDriverGet(fatDriver);
	driverArray[driverCounter++] = kernelSoftwareDriverGet(isoDriver);
	driverArray[driverCounter++] = kernelSoftwareDriverGet(linuxSwapDriver);
	driverArray[driverCounter++] = kernelSoftwareDriverGet(ntfsDriver);
	driverArray[driverCounter++] = kernelSoftwareDriverGet(udfDriver);
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
	// will be more-or-less hard-coded into this function.  Of course this
	// isn't desirable and should/will be fixed to be more flexible in
	// the future.  The function returns an enumeration value reflecting
	// the type it found (including possibly "unknown").

	int status = 0;
	kernelFilesystemDriver *tmpDriver = NULL;
	kernelFilesystemDriver *driver = NULL;
	int count;

	// We will assume that the detection functions being called will do
	// all of the necessary checking of the kernelDisk before using
	// it.  Since we're not actually using it here, we won't duplicate
	// that work.

	if (!driverCounter)
		populateDriverArray();

	// If it's a CD-ROM, only check UDF and ISO.
	if (theDisk->physical->type & DISKTYPE_CDROM)
	{
		// Do UDF first because DVDs can have apparently-valid ISO filesystems
		// on them as well.

		tmpDriver = getDriver(FSNAME_UDF);
		if (tmpDriver)
		{
			status = tmpDriver->driverDetect(theDisk);
			if (status < 0)
				goto finished;

			if (status == 1)
			{
				driver = tmpDriver;
				goto finished;
			}
		}

		tmpDriver = getDriver(FSNAME_ISO);
		if (tmpDriver)
		{
			status = tmpDriver->driverDetect(theDisk);
			if (status < 0)
				goto finished;

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
			if (status < 0)
				goto finished;

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


static void checkRemovable(kernelDisk *theDisk)
{
	// Check whether a removable disk has had its media changed, etc., and if
	// so, re-scan the filesystem

	kernelPhysicalDisk *physicalDisk = NULL;

	physicalDisk = theDisk->physical;

	if (!(physicalDisk->type & DISKTYPE_REMOVABLE))
		return;

	if (!kernelDiskMediaPresent((char *) physicalDisk->name))
		goto changed;

	if (kernelDiskMediaChanged((char *) physicalDisk->name))
		goto changed;

	return;

changed:
	memset((void *) &theDisk->filesystem, 0, sizeof(theDisk->filesystem));
	strcpy((char *) theDisk->fsType, "unknown");
	return;
}


static int unmount(const char *path, int removed)
{
	// This function takes the filesystem mount point name and removes the
	// filesystem structure and its driver from the lists.

	int status = 0;
	char mountPointName[MAX_PATH_LENGTH];
	kernelDisk *theDisk = NULL;
	kernelFilesystemDriver *theDriver = NULL;
	kernelFileEntry *mountPoint = NULL;
	kernelFileEntry *parentDir = NULL;
	kernelPhysicalDisk *physicalDisk = NULL;

	// Make sure the path name isn't NULL
	if (!path)
		return (status = ERR_NULLPARAMETER);

	// Fix up the path of the mount point
	status = kernelFileFixupPath(path, mountPointName);
	if (status < 0)
		return (status);

	// Get the file entry for the mount point
	mountPoint = kernelFileLookup(mountPointName);
	if (!mountPoint)
	{
		kernelError(kernel_error, "Unable to locate the mount point entry");
		return (status = ERR_NOSUCHDIR);
	}

	theDisk = mountPoint->disk;
	theDriver = theDisk->filesystem.driver;

	if (!theDisk->filesystem.mounted)
	{
		kernelError(kernel_error, "Disk %s is not mounted", theDisk->name);
		return (status = ERR_ALREADY);
	}

	// Unless the device has been removed, do not attempt to unmount the
	// filesystem if there are child mounts
	if (!removed && theDisk->filesystem.childMounts)
	{
		kernelError(kernel_error, "Cannot unmount %s when child filesystems "
			"are still mounted", mountPointName);
		return (status = ERR_BUSY);
	}

	// Starting at the mount point, unbuffer all files from the file entry tree
	status = kernelFileUnbufferRecursive(mountPoint);
	if (status < 0)
		return (status);

	// Do a couple of additional things if this is not the root directory
	if (strcmp(mountPointName, "/"))
	{
		if (mountPoint->parentDirectory)
		{
			parentDir = mountPoint->parentDirectory;
			((kernelDisk *) parentDir->disk)->filesystem.childMounts -= 1;
		}

		// Remove the mount point's file entry from its parent directory
		kernelFileRemoveEntry(mountPoint);
	}

	// If the the device is still present, call the filesystem driver's unmount
	// function
	if (!removed && theDriver->driverUnmount)
		theDriver->driverUnmount(theDisk);

	// It doesn't matter whether the unmount call was "successful".  If it
	// wasn't, there's really nothing we can do about it from here.

	// Clear some filesystem info
	theDisk->filesystem.mounted = 0;
	theDisk->filesystem.mountPoint[0] = '\0';
	theDisk->filesystem.filesystemRoot = NULL;
	theDisk->filesystem.childMounts = 0;
	theDisk->filesystem.filesystemData = NULL;
	theDisk->filesystem.caseInsensitive = 0;
	theDisk->filesystem.readOnly = 0;

	// If it's a removable device, clear everything
	if (theDisk->physical->type & DISKTYPE_REMOVABLE)
	{
		memset((void *) &theDisk->filesystem, 0,
			sizeof(theDisk->filesystem));
		strcpy((char *) theDisk->fsType, "unknown");
	}

	if (!removed)
	{
		// Sync the disk cache
		status = kernelDiskSync((char *) theDisk->name);
		if (status < 0)
			kernelError(kernel_warn, "Unable to sync disk \"%s\" after unmount",
				theDisk->name);

		physicalDisk = theDisk->physical;

		// If this is a removable disk, invalidate the disk cache
		if (physicalDisk->type & DISKTYPE_REMOVABLE)
		{
			status = kernelDiskInvalidateCache((char *) physicalDisk->name);
			if (status < 0)
				kernelError(kernel_warn, "Unable to invalidate \"%s\" disk "
					"cache after unmount", theDisk->name);

			// If it has an 'unlock' function, unlock it
			if (((kernelDiskOps *) physicalDisk->driver->ops)
				->driverSetLockState)
			{
				((kernelDiskOps *) physicalDisk->driver->ops)
					->driverSetLockState(physicalDisk->deviceNumber, 0);
			}
		}
	}

	return (status);
}


/////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////
//
//  Below here, the functions are exported for external use
//
/////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////

int kernelFilesystemScan(const char *diskName)
{
	// Scan a logical disk and see if we can determine the filesystem type

	int status = 0;
	kernelDisk *theDisk = NULL;
	kernelPhysicalDisk *physicalDisk = NULL;

	// Check params
	if (!diskName)
	{
		kernelError(kernel_error, "NULL parameter");
		return (status = ERR_NULLPARAMETER);
	}

	theDisk = kernelDiskGetByName(diskName);
	if (!theDisk)
	{
		kernelError(kernel_error, "No such disk \"%s\"", diskName);
		return (status = ERR_NULLPARAMETER);
	}

	memset((void *) &theDisk->filesystem, 0, sizeof(theDisk->filesystem));

	strcpy((char *) theDisk->fsType, "unknown");

	physicalDisk = theDisk->physical;

	// Is it removable?  If so, make sure there's media
	if (physicalDisk->type & DISKTYPE_REMOVABLE)
	{
		if (!kernelDiskMediaPresent((char *) physicalDisk->name))
			return (status = ERR_NOMEDIA);
	}

	// Scan a disk to determine its filesystem type, etc.
	theDisk->filesystem.driver = detectType(theDisk);

	if (theDisk->filesystem.driver)
		return (status = 0);
	else
		return (status = ERR_INVALID);
}


int kernelFilesystemFormat(const char *diskName, const char *type,
	const char *label, int longFormat, progress *prog)
{
	// This function is a wrapper for the filesystem driver's 'format'
	// function, if applicable.

	int status = 0;
	kernelDisk *theDisk = NULL;
	kernelFilesystemDriver *theDriver = NULL;

	// Check params
	if (!diskName)
		return (status = ERR_NULLPARAMETER);

	theDisk = kernelDiskGetByName(diskName);
	if (!theDisk)
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

	if (!theDriver)
	{
		kernelError(kernel_error, "Invalid filesystem type \"%s\" for format!",
			type);
		return (status = ERR_NOSUCHENTRY);
	}

	// Make sure the driver's formatting function is not NULL
	if (!theDriver->driverFormat)
	{
		kernelError(kernel_error, "The filesystem driver does not support the "
			"'format' operation");
		return (status = ERR_NOSUCHFUNCTION);
	}

	// Create the filesystem
	status = theDriver->driverFormat(theDisk, type, label, longFormat, prog);

	// Re-scan the filesystem
	kernelFilesystemScan((char *) theDisk->name);

	return (status);
}


int kernelFilesystemClobber(const char *diskName)
{
	// This function destroys anything that might cause this disk to be
	// detected as any filesystem we know about.

	int status = 0;
	kernelDisk *theDisk = NULL;
	int count;

	// Check params
	if (!diskName)
		return (status = ERR_NULLPARAMETER);

	theDisk = kernelDiskGetByName(diskName);
	if (!theDisk)
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
	kernelFilesystemScan((char *) theDisk->name);

	// Finished
	return (status = 0);
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
	if (!diskName)
		return (status = ERR_NULLPARAMETER);

	theDisk = kernelDiskGetByName(diskName);
	if (!theDisk)
	{
		kernelError(kernel_error, "No such disk \"%s\"", diskName);
		return (status = ERR_NULLPARAMETER);
	}

	if (theDisk->physical->type & DISKTYPE_REMOVABLE)
		checkRemovable(theDisk);

	if (!theDisk->filesystem.driver)
	{
		// Try a scan before we error out.
		if (kernelFilesystemScan((char *) theDisk->name) < 0)
		{
			kernelError(kernel_error, "The filesystem type of disk \"%s\" is "
				"unknown", theDisk->name);
			return (status = ERR_NOTIMPLEMENTED);
		}
	}

	theDriver = theDisk->filesystem.driver;

	// Make sure the driver's checking function is not NULL
	if (!theDriver->driverCheck)
	{
		kernelError(kernel_error, "The filesystem driver does not support the "
			"'check' operation");
		return (status = ERR_NOSUCHFUNCTION);
	}

	// Check the filesystem
	return (status = theDriver->driverCheck(theDisk, force, repair, prog));
}


int kernelFilesystemDefragment(const char *diskName, progress *prog)
{
	// This function is a wrapper for the filesystem driver's 'defragment'
	// function, if applicable.

	int status = 0;
	kernelDisk *theDisk = NULL;
	kernelFilesystemDriver *theDriver = NULL;

	// Check params
	if (!diskName)
		return (status = ERR_NULLPARAMETER);

	theDisk = kernelDiskGetByName(diskName);
	if (!theDisk)
	{
		kernelError(kernel_error, "No such disk \"%s\"", diskName);
		return (status = ERR_NULLPARAMETER);
	}

	if (theDisk->physical->type & DISKTYPE_REMOVABLE)
		checkRemovable(theDisk);

	if (!theDisk->filesystem.driver)
	{
		// Try a scan before we error out.
		if (kernelFilesystemScan((char *) theDisk->name) < 0)
		{
			kernelError(kernel_error, "The filesystem type of disk \"%s\" is "
				"unknown", theDisk->name);
			return (status = ERR_NOTIMPLEMENTED);
		}
	}

	theDriver = theDisk->filesystem.driver;

	// Make sure the driver's checking function is not NULL
	if (!theDriver->driverDefragment)
	{
		kernelError(kernel_error, "The filesystem driver does not support the "
			"'defragment' operation");
		return (status = ERR_NOSUCHFUNCTION);
	}

	// Defrag the filesystem
	return (status = theDriver->driverDefragment(theDisk, prog));
}


int kernelFilesystemStat(const char *diskName, kernelFilesystemStats *stat)
{
	// This function is a wrapper for the filesystem driver's 'stat' function,
	// if applicable.

	int status = 0;
	kernelDisk *theDisk = NULL;
	kernelFilesystemDriver *theDriver = NULL;

	// Check params
	if (!diskName || !stat)
		return (status = ERR_NULLPARAMETER);

	theDisk = kernelDiskGetByName(diskName);
	if (!theDisk)
	{
		kernelError(kernel_error, "No such disk \"%s\"", diskName);
		return (status = ERR_NULLPARAMETER);
	}

	if (theDisk->physical->type & DISKTYPE_REMOVABLE)
		checkRemovable(theDisk);

	if (!theDisk->filesystem.driver)
	{
		// Try a scan before we error out.
		if (kernelFilesystemScan((char *) theDisk->name) < 0)
		{
			kernelError(kernel_error, "The filesystem type of disk \"%s\" is "
				"unknown", theDisk->name);
			return (status = ERR_NOTIMPLEMENTED);
		}
	}

	theDriver = theDisk->filesystem.driver;

	// Make sure the driver's stat function is not NULL
	if (!theDriver->driverStat)
	{
		kernelError(kernel_error, "The filesystem driver does not support the "
			"'stat' operation");
		return (status = ERR_NOSUCHFUNCTION);
	}

	// Stat the filesystem
	return (status = theDriver->driverStat(theDisk, stat));
}


int kernelFilesystemResizeConstraints(const char *diskName, uquad_t *minBlocks,
	uquad_t *maxBlocks, progress *prog)
{
	// This function is a wrapper for the filesystem driver's 'get resize
	// constraints' function, if applicable.

	int status = 0;
	kernelDisk *theDisk = NULL;
	kernelFilesystemDriver *theDriver = NULL;

	// Check params
	if (!diskName || !minBlocks || !maxBlocks)
		return (status = ERR_NULLPARAMETER);

	theDisk = kernelDiskGetByName(diskName);
	if (!theDisk)
	{
		kernelError(kernel_error, "No such disk \"%s\"", diskName);
		return (status = ERR_NULLPARAMETER);
	}

	if (theDisk->physical->type & DISKTYPE_REMOVABLE)
		checkRemovable(theDisk);

	if (!theDisk->filesystem.driver)
	{
		// Try a scan before we error out.
		if (kernelFilesystemScan((char *) theDisk->name) < 0)
		{
			kernelError(kernel_error, "The filesystem type of disk \"%s\" is "
				"unknown", theDisk->name);
			return (status = ERR_NOTIMPLEMENTED);
		}
	}

	theDriver = theDisk->filesystem.driver;

	// Make sure the driver's resizing constraints function is not NULL
	if (!theDriver->driverResizeConstraints)
	{
		kernelError(kernel_error, "The filesystem driver does not support the "
			"'resize constraints' operation");
		return (status = ERR_NOSUCHFUNCTION);
	}

	// Get the constraints from the driver
	return (status = theDriver->driverResizeConstraints(theDisk, minBlocks,
		maxBlocks, prog));
}


int kernelFilesystemResize(const char *diskName, uquad_t blocks,
	progress *prog)
{
	// This function is a wrapper for the filesystem driver's 'resize'
	// function, if applicable.

	int status = 0;
	kernelDisk *theDisk = NULL;
	kernelFilesystemDriver *theDriver = NULL;

	// Check params
	if (!diskName)
		return (status = ERR_NULLPARAMETER);

	theDisk = kernelDiskGetByName(diskName);
	if (!theDisk)
	{
		kernelError(kernel_error, "No such disk \"%s\"", diskName);
		return (status = ERR_NULLPARAMETER);
	}

	if (theDisk->physical->type & DISKTYPE_REMOVABLE)
		checkRemovable(theDisk);

	if (!theDisk->filesystem.driver)
	{
		// Try a scan before we error out.
		if (kernelFilesystemScan((char *) theDisk->name) < 0)
		{
			kernelError(kernel_error, "The filesystem type of disk \"%s\" is "
				"unknown", theDisk->name);
			return (status = ERR_NOTIMPLEMENTED);
		}
	}

	theDriver = theDisk->filesystem.driver;

	// Make sure the driver's resizing function is not NULL
	if (!theDriver->driverResize)
	{
		kernelError(kernel_error, "The filesystem driver does not support the "
			"'resize' operation");
		return (status = ERR_NOSUCHFUNCTION);
	}

	// Resize the filesystem
	return (status = theDriver->driverResize(theDisk, blocks, prog));
}


int kernelFilesystemMount(const char *diskName, const char *path)
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
	if (!diskName || !path)
		return (status = ERR_NULLPARAMETER);

	theDisk = kernelDiskGetByName(diskName);
	if (!theDisk)
	{
		kernelError(kernel_error, "No such disk \"%s\"", diskName);
		return (status = ERR_NULLPARAMETER);
	}

	// Make sure that the disk hasn't already been mounted
	if (theDisk->filesystem.mounted)
	{
		kernelError(kernel_error, "The disk is already mounted at %s",
			theDisk->filesystem.mountPoint);
		return (status = ERR_ALREADY);
	}

	if (theDisk->physical->type & DISKTYPE_REMOVABLE)
		checkRemovable(theDisk);

	if (!theDisk->filesystem.driver)
	{
		// Try a scan before we error out.
		if (kernelFilesystemScan((char *) theDisk->name) < 0)
		{
			kernelError(kernel_error, "The filesystem type of disk \"%s\" is "
				"unknown", theDisk->name);
			return (status = ERR_NOTIMPLEMENTED);
		}
	}

	theDriver = theDisk->filesystem.driver;

	// Make sure the driver's mounting function is not NULL
	if (!theDriver->driverMount)
	{
		kernelError(kernel_error, "The filesystem driver does not support the "
			"'mount' operation");
			return (status = ERR_NOSUCHFUNCTION);
	}

	// Fix up the path of the mount point
	status = kernelFileFixupPath(path, mountPoint);
	if (status < 0)
		return (status);

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
		status = kernelFileSeparateLast(mountPoint, parentDirName,
			mountDirName);
		if (status < 0)
		{
			kernelError(kernel_error, "Bad path to mount point");
			return (status);
		}

		parentDir = kernelFileLookup(parentDirName);
		if (!parentDir)
		{
			kernelError(kernel_error, "Mount point parent directory doesn't "
				"exist");
			return (status = ERR_NOCREATE);
		}
	}

	kernelLog("Mounting %s filesystem on disk %s", mountPoint, theDisk->name);

	// Fill in any information that we already know for this filesystem

	// Make "mountPoint" be the filesystem's mount point
	strncpy((char *) theDisk->filesystem.mountPoint, mountPoint,
		MAX_PATH_LENGTH);

	// Get a new file entry for the filesystem's root directory
	theDisk->filesystem.filesystemRoot = kernelFileNewEntry(theDisk);
	if (!theDisk->filesystem.filesystemRoot)
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
		status = kernelFileInsertEntry(theDisk->filesystem.filesystemRoot,
			parentDir);
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
	if ((theDisk->physical->type & DISKTYPE_REMOVABLE) &&
		(((kernelDiskOps *) theDisk->physical->driver->ops)
			->driverSetLockState))
	{
		((kernelDiskOps *) theDisk->physical->driver->ops)
			->driverSetLockState(theDisk->physical->deviceNumber, 1);
	}

	return (status = 0);
}


int kernelFilesystemUnmount(const char *path)
{
	// This is a wrapper for the unmount() function, used for a normal
	// software-driven unmount when the hardware device is still present.
	return (unmount(path, 0 /* not removed */));
}


int kernelFilesystemRemoved(const char *path)
{
	// This is a wrapper for the unmount() function, used for a forced unmount
	// when the hardware device has been removed.
	return (unmount(path, 1 /* removed */));
}


uquad_t kernelFilesystemGetFreeBytes(const char *path)
{
	// This is merely a wrapper function for the equivalent function
	// in the requested filesystem's own driver.  It takes nearly-identical
	// arguments and returns the same status as the driver function.

	int status = 0;
	uquad_t freeSpace = 0;
	char mountPoint[MAX_PATH_LENGTH];
	kernelFileEntry *fileEntry = NULL;
	kernelDisk *theDisk = NULL;
	kernelFilesystemDriver *theDriver = NULL;

	// Make sure the path name isn't NULL
	if (!path)
		return (freeSpace = 0);

	// Fix up the path of the mount point
	status = kernelFileFixupPath(path, mountPoint);
	if (status < 0)
		return (freeSpace = 0);

	// Locate the mount point
	fileEntry = kernelFileLookup(mountPoint);
	if (!fileEntry)
	{
		kernelError(kernel_error, "No filesystem mounted at %s", mountPoint);
		return (freeSpace = 0);
	}

	// Find the filesystem structure based on its name
	theDisk = fileEntry->disk;
	if (!theDisk)
	{
		kernelError(kernel_error, "No disk for mount point \"%s\"", mountPoint);
		return (status = ERR_BUG);
	}

	if (theDisk->physical->type & DISKTYPE_REMOVABLE)
		checkRemovable(theDisk);

	if (!theDisk->filesystem.driver)
	{
		// Try a scan before we error out.
		if (kernelFilesystemScan((char *) theDisk->name) < 0)
		{
			kernelError(kernel_error, "The filesystem type of disk \"%s\" is "
				"unknown", theDisk->name);
			return (status = ERR_NOTIMPLEMENTED);
		}
	}

	theDriver = theDisk->filesystem.driver;

	// OK, we just have to check on the filsystem driver function we want
	// to call
	if (!theDriver->driverGetFreeBytes)
		// Report NO free space
		return (freeSpace = 0);

	// Lastly, we can call our target function
	return (freeSpace = theDriver->driverGetFreeBytes(theDisk));
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
	if (!path)
		return (blockSize = 0);

	// Fix up the path of the mount point
	status = kernelFileFixupPath(path, fixedPath);
	if (status < 0)
		return (blockSize = 0);

	// Locate the mount point
	fileEntry = kernelFileLookup(fixedPath);
	if (!fileEntry)
	{
		kernelError(kernel_error, "No filesystem mounted at %s", fixedPath);
		return (blockSize = 0);
	}

	// Return the block size
	return (blockSize = ((kernelDisk *) fileEntry->disk)->filesystem.blockSize);
}

