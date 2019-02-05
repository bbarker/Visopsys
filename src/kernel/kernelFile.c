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
//  kernelFile.c
//

// This file contains the code for managing the abstract (filesystem-agnostic)
// directory and file tree.

#include "kernelFile.h"
#include "kernelCpu.h"
#include "kernelDebug.h"
#include "kernelDisk.h"
#include "kernelError.h"
#include "kernelFilesystem.h"
#include "kernelLock.h"
#include "kernelMalloc.h"
#include "kernelMemory.h"
#include "kernelMultitasker.h"
#include "kernelRandom.h"
#include "kernelRtc.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/paths.h>

#define ISSEPARATOR(foo) (((foo) == '/') || ((foo) == '\\'))

// The root directory
static kernelFileEntry *rootEntry = NULL;

// Memory for free file entries
static kernelFileEntry *freeEntries = NULL;
static unsigned numFreeEntries = 0;

static int initialized = 0;


static int allocateFileEntries(void)
{
	// This function is used to allocate more memory for the freeEntries
	// list.

	int status = 0;
	kernelFileEntry *entries = NULL;
	int count;

	// Allocate memory for file entries
	entries = kernelMalloc(sizeof(kernelFileEntry) * MAX_BUFFERED_FILES);
	if (!entries)
		return (status = ERR_MEMORY);

	// Initialize the new kernelFileEntry structures.

	for (count = 0; count < (MAX_BUFFERED_FILES - 1); count ++)
		entries[count].nextEntry = &entries[count + 1];

	// The free file entries are the new memory
	freeEntries = entries;

	// Add the number of new file entries
	numFreeEntries = MAX_BUFFERED_FILES;

	return (status = 0);
}


static int isLeafDir(kernelFileEntry *entry)
{
	// This function will determine whether the supplied directory entry
	// is a 'leaf' directory.  A leaf directory is defined as a directory
	// which contains no other subdirectories with buffered contents.
	// Returns 1 if the directory is a leaf, 0 otherwise.

	int status = 0;
	kernelFileEntry *listEntry = NULL;

	listEntry = entry->contents;

	while (listEntry)
	{
		if ((listEntry->type == dirT) && listEntry->contents)
			break;
		else
			listEntry = listEntry->nextEntry;
	}

	if (!listEntry)
		// It is a leaf directory
		return (status = 1);
	else
		// It's not a leaf directory
		return (status = 0);
}


static void unbufferDirectory(kernelFileEntry *entry)
{
	// This function is internal, and is called when the tree of file
	// and directory entries becomes too full.  It will "un-buffer" all of
	// one directory entry's contents (sub-entries) from memory.
	// The decision about which entry to un-buffer is based on LRU (least
	// recently used).

	kernelFileEntry *listEntry = NULL;
	kernelFileEntry *nextListEntry = NULL;

	// We should have a directory that is safe to unbuffer.  We can return
	// this directory's contents (sub-entries) to the list of free entries.

	listEntry = entry->contents;

	// Step through the list of directory entries
	while (listEntry)
	{
		nextListEntry = listEntry->nextEntry;
		kernelFileReleaseEntry(listEntry);
		listEntry = nextListEntry;
	}

	entry->contents = NULL;

	// This directory now looks to the system as if it had not yet been read
	// from disk.
}


static void fileEntry2File(kernelFileEntry *entry, file *fileStruct)
{
	// This will copy the applicable parts from a kernelFileEntry structure
	// to an external 'file' structure.

	strncpy(fileStruct->name, (char *) entry->name, MAX_NAME_LENGTH);
	fileStruct->name[MAX_NAME_LENGTH - 1] = '\0';
	fileStruct->handle = (void *) entry;
	fileStruct->type = entry->type;

	strncpy(fileStruct->filesystem,
		(char *) entry->disk->filesystem.mountPoint, MAX_PATH_LENGTH);
	fileStruct->filesystem[MAX_PATH_LENGTH - 1] = '\0';

	kernelRtcDateTime2Tm(entry->creationDate, entry->creationTime,
		&fileStruct->created);
	kernelRtcDateTime2Tm(entry->accessedDate, entry->accessedTime,
		&fileStruct->accessed);
	kernelRtcDateTime2Tm(entry->modifiedDate, entry->modifiedTime,
		&fileStruct->modified);

	fileStruct->size = entry->size;
	fileStruct->blocks = entry->blocks;
	fileStruct->blockSize = ((kernelDisk *) entry->disk)->filesystem.blockSize;

	return;
}


static int dirIsEmpty(kernelFileEntry *entry)
{
	// This determines whether there are any files in a directory (aside from
	// the non-entries '.' and '..'.  Returns 1 if the entry is a directory and
	// is empty, otherwise 0.

	int isEmpty = 0;
	kernelFileEntry *listEntry = NULL;

	// Make sure the directory is really a directory
	if (entry->type != dirT)
	{
		kernelError(kernel_error, "Directory to check is not a directory");
		return (isEmpty = 0);
	}

	// Assign the first item in the directory to our iterator pointer
	listEntry = entry->contents;

	while (listEntry)
	{
		if (!strcmp((char *) listEntry->name, ".") ||
			!strcmp((char *) listEntry->name, ".."))
		{
			listEntry = listEntry->nextEntry;
		}
		else
		{
			return (isEmpty = 0);
		}
	}

	// It's an empty directory
	return (isEmpty = 1);
}


static int isDescendent(kernelFileEntry *leafEntry, kernelFileEntry *nodeEntry)
{
	// This can be used to determine whether the "leaf" entry is a descendent
	// of the "node" entry.  This is important to check during move operations
	// so that directories cannot be "put inside themselves" (thereby
	// disconnecting them from the filesystem entirely).  Returns 1 if true
	// (is a descendent), 0 otherwise.

	int status = 0;
	kernelFileEntry *listEntry = NULL;

	// If "leaf" and "node" are the same, return true
	if (nodeEntry == leafEntry)
		return (status = 1);

	// If "nodeEntry" is not a directory, then it obviously has no descendents.
	// Return false
	if (nodeEntry->type != dirT)
	{
		kernelError(kernel_error, "Node entry is not a directory");
		return (status = 0);
	}

	listEntry = leafEntry;

	// Do a loop to step upward from the "leaf" through its respective
	// ancestor directories.  If the parent is NULL at any point, we return
	// false.  If the parent ever equals "node", return true.
	while (listEntry)
	{
		if (listEntry->parentDirectory == listEntry)
			break;

		listEntry = listEntry->parentDirectory;

		if (listEntry == nodeEntry)
			// It is a descendant
			return (status = 1);
	}

	// It is not a descendant
	return (status = 0);
}


static inline void updateCreationTime(kernelFileEntry *entry)
{
	// This will update the creation date and time of a file entry.

	entry->creationDate = kernelRtcPackedDate();
	entry->creationTime = kernelRtcPackedTime();
	entry->lastAccess = kernelCpuTimestamp();
}


static inline void updateModifiedTime(kernelFileEntry *entry)
{
	// This will update the modified date and time of a file entry.

	entry->modifiedDate = kernelRtcPackedDate();
	entry->modifiedTime = kernelRtcPackedTime();
	entry->lastAccess = kernelCpuTimestamp();
}


static inline void updateAccessedTime(kernelFileEntry *entry)
{
	// This will update the accessed date and time of a file entry.

	entry->accessedDate = kernelRtcPackedDate();
	entry->accessedTime = kernelRtcPackedTime();
	entry->lastAccess = kernelCpuTimestamp();
}


static void updateAllTimes(kernelFileEntry *entry)
{
	// This will update all the dates and times of a file entry.

	updateCreationTime(entry);
	updateModifiedTime(entry);
	updateAccessedTime(entry);
}


static void buildFilenameRecursive(kernelFileEntry *entry, char *buffer,
	int buffLen)
{
	// Head-recurse back to the root of the filesystem, constructing the full
	// pathname of the file

	int isRoot = 0;

	if (!strcmp((char *) entry->name, "/"))
		isRoot = 1;

	if (entry->parentDirectory && !isRoot)
		buildFilenameRecursive(entry->parentDirectory, buffer, buffLen);

	if (!isRoot && (buffer[strlen(buffer) - 1] != '/'))
		strncat(buffer, "/", (buffLen - (strlen(buffer) + 1)));

	strncat(buffer, (char *) entry->name, (buffLen - (strlen(buffer) + 1)));
}


static char *fixupPath(const char *originalPath)
{
	// This will take a path string, possibly add the CWD as a prefix, remove
	// any unneccessary characters, and resolve any '.' or '..' components to
	// their real targets.  It allocates memory for the result, so it is the
	// responsibility of the caller to free it.

	char *newPath = NULL;
	int originalLength = 0;
	int newLength = 0;
	int count;

	if (!strlen(originalPath))
		return (newPath = NULL);

	newPath = kernelMalloc(MAX_PATH_NAME_LENGTH);
	if (!newPath)
		return (newPath);

	originalLength = strlen(originalPath);

	if (!ISSEPARATOR(originalPath[0]))
	{
		// The original path doesn't appear to be an absolute path.  We will
		// try prepending the CWD.
		if (kernelCurrentProcess)
		{
			strcpy(newPath, (char *) kernelCurrentProcess->currentDirectory);
			newLength += strlen(newPath);
		}

		if (!newLength || (newPath[newLength - 1] != '/'))
			// Append a '/', which if multitasking is not enabled will just
			// make the CWD '/'
			newPath[newLength++] = '/';
	}

	// OK, we step through the original path, dealing with the various
	// possibilities
	for (count = 0; count < originalLength; count ++)
	{
		// Deal with slashes
		if (ISSEPARATOR(originalPath[count]))
		{
			if (newLength && ISSEPARATOR(newPath[newLength - 1]))
			{
				continue;
			}
			else
			{
				newPath[newLength++] = '/';
				continue;
			}
		}

		// Deal with '.' and '..' between separators
		if ((originalPath[count] == '.') && (newPath[newLength - 1] == '/'))
		{
			// We must determine whether this will be dot or dotdot.  If it
			// is dot, we simply remove this path level.  If it is dotdot,
			// we have to remove the previous path level
			if (ISSEPARATOR(originalPath[count + 1]) ||
				!originalPath[count + 1])
			{
				// It's only dot.  Skip this one level
				count += 1;
				continue;
			}

			else if ((originalPath[count + 1] == '.') &&
				(ISSEPARATOR(originalPath[count + 2]) ||
					!originalPath[count + 2]))
			{
				// It's dotdot.  We must skip backward in the new path until
				// the next-but-one separator.  If we're at the root level,
				// simply copy (it will probably fail later as a 'no such
				// file')
				if (newLength > 1)
				{
					newLength -= 1;
					while ((newPath[newLength - 1] != '/') && (newLength > 1))
						newLength -= 1;
				}
				else
				{
					newPath[newLength++] = originalPath[count];
					newPath[newLength++] = originalPath[count + 1];
					newPath[newLength++] = originalPath[count + 2];
				}

				count += 2;
				continue;
			}
		}

		// Other possibilities, just copy
		newPath[newLength++] = originalPath[count];
	}

	// If not exactly '/', remove any trailing slashes
	if ((newLength > 1) && (newPath[newLength - 1] == '/'))
		newLength -= 1;

	// Stick the NULL on the end
	newPath[newLength] = NULL;

	// Return success;
	return (newPath);
}


static kernelFileEntry *fileLookup(const char *fixedPath)
{
	// This resolves pathnames and files to kernelFileEntry structures.  On
	// success, it returns the kernelFileEntry of the deepest item of the path
	// it was given.  The target path can resolve either to a directory or a
	// file.

	int status = 0;
	const char *itemName = NULL;
	int itemLength = 0;
	int found = 0;
	kernelDisk *fsDisk = NULL;
	kernelFilesystemDriver *driver = NULL;
	kernelFileEntry *listEntry = NULL;
	int count;

	// We step through the directory structure, looking for the appropriate
	// directories based on the path we were given.

	// Start with the root directory
	listEntry = rootEntry;

	if (!strcmp(fixedPath, "/"))
		// The root directory is being requested
		return (listEntry);

	itemName = (fixedPath + 1);

	while (1)
	{
		itemLength = strlen(itemName);

		for (count = 0; count < itemLength; count ++)
		{
			if (ISSEPARATOR(itemName[count]))
			{
				itemLength = count;
				break;
			}
		}

		// Make sure there's actually some content here
		if (!itemLength)
			return (listEntry = NULL);

		// Find the first item in the "current" directory
		if (listEntry->contents)
			listEntry = listEntry->contents;
		else
			// Nothing in the directory
			return (listEntry = NULL);

		found = 0;

		while (listEntry)
		{
			// Update the access time on this directory
			listEntry->lastAccess = kernelCpuTimestamp();

			// Get the logical disk from the file entry structure
			fsDisk = listEntry->disk;
			if (!fsDisk)
			{
				kernelError(kernel_error, "Entry has a NULL disk pointer");
				return (listEntry = NULL);
			}

			if ((int) strlen((char *) listEntry->name) == itemLength)
			{
				// First, try a case-sensitive comparison, whether or not the
				// filesystem is case-sensitive.  If that fails and the
				// filesystem is case-insensitive, try that kind of comparison
				// also.
				if (!strncmp((char *) listEntry->name, itemName, itemLength) ||
					(fsDisk->filesystem.caseInsensitive &&
						!strncasecmp((char *) listEntry->name, itemName,
							itemLength)))
				{
					// Found it.
					found = 1;
					break;
				}
			}

			// Move to the next item
			listEntry = listEntry->nextEntry;
		}

		if (found)
		{
			// If this is a link, use the target of the link instead
			if (listEntry->type == linkT)
			{
				listEntry = kernelFileResolveLink(listEntry);
				if (!listEntry)
					// Unresolved link.
					return (listEntry = NULL);

				// Re-get the logical disk from the resolved link
				fsDisk = listEntry->disk;
				if (!fsDisk)
				{
					kernelError(kernel_error, "Entry has a NULL disk pointer");
					return (listEntry = NULL);
				}
			}

			// Determine whether the requested item is really a directory, and
			// if so, whether the directory's files have been read
			if ((listEntry->type == dirT) && !listEntry->contents)
			{
				// We have to read this directory from the disk.

				driver = fsDisk->filesystem.driver;

				// Increase the open count on the directory's entry while we're
				// reading it.  This will prevent the filesystem manager from
				// trying to unbuffer it while we're working
				listEntry->openCount++;

				// Lastly, we can call our target function
				if (driver->driverReadDir)
					status = driver->driverReadDir(listEntry);

				listEntry->openCount--;

				if (status < 0)
					return (listEntry = NULL);
			}

			if (!itemName[itemLength])
			{
				listEntry->lastAccess = kernelCpuTimestamp();
				return (listEntry);
			}
		}
		else
		{
			// Not found
			return (listEntry = NULL);
		}

		// Do the next item in the path
		itemName += (itemLength + 1);
	}
}


static int fileCreate(const char *path)
{
	// This gets called by the open() function when the file in question needs
	// to be created.

	int status = 0;
	char prefix[MAX_PATH_LENGTH];
	char name[MAX_NAME_LENGTH];
	kernelDisk *fsDisk = NULL;
	kernelFilesystemDriver *driver = NULL;
	kernelFileEntry *dirEntry = NULL;
	kernelFileEntry *createEntry = NULL;

	// Make sure that the requested file does NOT exist
	createEntry = fileLookup(path);
	if (createEntry)
	{
		kernelError(kernel_error, "File to create already exists");
		return (status = ERR_ALREADY);
	}

	// We have to find the directory where the user wants to create the file.
	// That's all but the last part of the "fileName" argument to you and I.
	// We have to call this function to separate the two parts.
	status = kernelFileSeparateLast(path, prefix, name);
	if (status < 0)
		return (status);

	// Make sure "name" isn't empty.  This could happen if there WAS no
	// last item in the path to separate
	if (!name[0])
	{
		// Basically, we have been given an invalid name
		kernelError(kernel_error, "File to create (%s) has an invalid path",
			path);
		return (status = ERR_NOSUCHFILE);
	}

	// Now make sure that the requested directory exists
	dirEntry = fileLookup(prefix);
	if (!dirEntry)
	{
		// The directory does not exist
		kernelError(kernel_error, "Parent directory (%s) of \"%s\" does not "
			"exist", prefix, name);
		return (status = ERR_NOSUCHDIR);
	}

	// OK, the directory exists.  Get the logical disk of the parent directory.
	fsDisk = (kernelDisk *) dirEntry->disk;
	if (!fsDisk)
	{
		kernelError(kernel_error, "Unable to determine logical disk");
		return (status = ERR_BADDATA);
	}

	// Not allowed in read-only file system
	if (fsDisk->filesystem.readOnly)
	{
		kernelError(kernel_error, "Filesystem is read-only");
		return (status = ERR_NOWRITE);
	}

	driver = fsDisk->filesystem.driver;

	// We can create the file in the directory.  Get a free file entry
	// structure.
	createEntry = kernelFileNewEntry(fsDisk);
	if (!createEntry)
		return (status = ERR_NOFREE);

	// Set up the appropriate data items in the new entry that aren't done
	// by default in the kernelFileNewEntry() function
	strncpy((char *) createEntry->name, name, MAX_NAME_LENGTH);
	((char *) createEntry->name)[MAX_NAME_LENGTH - 1] = '\0';
	createEntry->type = fileT;

	// Add the file to the directory
	status = kernelFileInsertEntry(createEntry, dirEntry);
	if (status < 0)
	{
		kernelFileReleaseEntry(createEntry->nextEntry);
		return (status);
	}

	// Check the filesystem driver function for creating files.  If it exists,
	// call it.
	if (driver->driverCreateFile)
	{
		status = driver->driverCreateFile(createEntry);
		if (status < 0)
			return (status);
	}

	// Update the timestamps on the parent directory
	updateModifiedTime(dirEntry);
	updateAccessedTime(dirEntry);

	// Update the directory
	if (driver->driverWriteDir)
	{
		status = driver->driverWriteDir(dirEntry);
		if (status < 0)
			return (status);
	}

	// Return success
	return (status = 0);
}


static int fileOpen(kernelFileEntry *entry, int openMode)
{
	// This is mostly a wrapper function for the equivalent function in the
	// appropriate filesystem's own driver.  It takes nearly-identical
	// arguments and returns the same status as the driver function.

	int status = 0;
	kernelDisk *fsDisk = NULL;
	kernelFilesystemDriver *driver = NULL;

	// Make sure the item is really a file, and not a directory or anything
	// else
	if (entry->type != fileT)
	{
		kernelError(kernel_error, "Item to open (%s) is not a file",
			entry->name);
		return (status = ERR_NOTAFILE);
	}

	// Get the filesystem that the file belongs to
	fsDisk = (kernelDisk *) entry->disk;
	if (!fsDisk)
	{
		kernelError(kernel_error, "NULL disk pointer");
		return (status = ERR_BADADDRESS);
	}

	// Not allowed in read-only file system
	if ((openMode & OPENMODE_WRITE) && fsDisk->filesystem.readOnly)
	{
		kernelError(kernel_error, "Filesystem is read-only");
		return (status = ERR_NOWRITE);
	}

	driver = fsDisk->filesystem.driver;

	// There are extra things we need to do if this will be a write operation

	if (openMode & OPENMODE_WRITE)
	{
		// Are we supposed to truncate the file first?
		if (openMode & OPENMODE_TRUNCATE)
		{
			// Call the filesystem driver and ask it to delete the file.
			// Then, we ask it to create the file again.

			// Check the driver functions we want to use
			if (!driver->driverDeleteFile || !driver->driverCreateFile)
			{
				kernelError(kernel_error, "The requested filesystem operation "
					"is not supported");
				return (status = ERR_NOSUCHFUNCTION);
			}

			status = driver->driverDeleteFile(entry);
			if (status < 0)
				return (status);

			status = driver->driverCreateFile(entry);
			if (status < 0)
				return (status);
		}

		// Put a write lock on the file
		status = kernelLockGet(&entry->lock);
		if (status < 0)
			return (status);

		// Update the modified dates/times on the file
		updateModifiedTime(entry);
	}

	// Increment the open count on the file
	entry->openCount += 1;

	// Update the access times/dates on the file
	updateAccessedTime(entry);

	// Return success
	return (status = 0);
}


static int fileDelete(kernelFileEntry *entry)
{
	// This is *somewhat* a wrapper function for the underlying function in the
	// filesystem's own driver, except it does do some general housekeeping as
	// well.

	int status = 0;
	kernelFileEntry *dirEntry = NULL;
	kernelDisk *fsDisk = NULL;
	kernelFilesystemDriver *driver = NULL;

	// Record the parent directory before we nix the file
	dirEntry = entry->parentDirectory;

	// Make sure the item is really a file, and not a directory.  We have to do
	// different things for removing directory
	if (entry->type != fileT)
	{
		kernelError(kernel_error, "Item to delete is not a file");
		return (status = ERR_NOTAFILE);
	}

	// Figure out which filesystem we're using
	fsDisk = (kernelDisk *) entry->disk;
	if (!fsDisk)
	{
		kernelError(kernel_error, "NULL disk pointer");
		return (status = ERR_BADADDRESS);
	}

	// Not allowed in read-only file system
	if (fsDisk->filesystem.readOnly)
	{
		kernelError(kernel_error, "Filesystem is read-only");
		return (status = ERR_NOWRITE);
	}

	driver = fsDisk->filesystem.driver;

	// If the filesystem driver has a 'delete' function, call it.
	if (driver->driverDeleteFile)
		status = driver->driverDeleteFile(entry);
	if (status < 0)
		return (status);

	// Remove the entry for this file from its parent directory
	status = kernelFileRemoveEntry(entry);
	if (status < 0)
		return (status);

	// Deallocate the data structure
	kernelFileReleaseEntry(entry);

	// Update the times on the directory
	updateModifiedTime(dirEntry);
	updateAccessedTime(dirEntry);

	// Update the directory
	if (driver->driverWriteDir)
	{
		status = driver->driverWriteDir(dirEntry);
		if (status < 0)
			return (status);
	}

	// Return success
	return (status = 0);
}


static int fileMakeDir(const char *path)
{
	// Create a directory, given a path name

	int status = 0;
	char *prefix = NULL;
	char *name = NULL;
	kernelDisk *fsDisk = NULL;
	kernelFilesystemDriver *driver = NULL;
	kernelFileEntry *parentEntry = NULL;
	kernelFileEntry *entry = NULL;

	prefix = kernelMalloc(MAX_PATH_LENGTH);
	if (!prefix)
		return (status = ERR_MEMORY);

	name = kernelMalloc(MAX_NAME_LENGTH);
	if (!name)
		return (status = ERR_MEMORY);

	// We must separate the name of the new file from the requested path
	status = kernelFileSeparateLast(path, prefix, name);
	if (status < 0)
		goto out;

	// Make sure "name" isn't empty.  This could happen if there WAS no last
	// item in the path to separate (like 'mkdir /', which would of course be
	// invalid)
	if (!name[0])
	{
		// Basically, we have been given an invalid directory name to create
		kernelError(kernel_error, "Path of directory to create is invalid");
		status = ERR_NOSUCHFILE;
		goto out;
	}

	// Now make sure that the requested parent directory exists
	parentEntry = fileLookup(prefix);
	if (!parentEntry)
	{
		// The directory does not exist
		kernelError(kernel_error, "Parent directory does not exist");
		status = ERR_NOSUCHDIR;
		goto out;
	}

	// Make sure it's a directory
	if (parentEntry->type != dirT)
	{
		kernelError(kernel_error, "Parent directory is not a directory");
		status = ERR_NOSUCHDIR;
		goto out;
	}

	// Figure out which filesystem we're using
	fsDisk = (kernelDisk *) parentEntry->disk;
	if (!fsDisk)
	{
		kernelError(kernel_error, "NULL disk pointer");
		status = ERR_BADADDRESS;
		goto out;
	}

	// Not allowed in read-only file system
	if (fsDisk->filesystem.readOnly)
	{
		kernelError(kernel_error, "Filesystem is read-only");
		status = ERR_NOWRITE;
		goto out;
	}

	driver = fsDisk->filesystem.driver;

	// Allocate a new file entry
	entry = kernelFileNewEntry(fsDisk);
	if (!entry)
	{
		status = ERR_NOFREE;
		goto out;
	}

	// Now, set some fields for our new item

	strncpy((char *) entry->name, name, MAX_NAME_LENGTH);
	((char *) entry->name)[MAX_NAME_LENGTH - 1] = '\0';

	entry->type = dirT;

	// Set the creation, modification times, etc.
	updateAllTimes(entry);

	// Now add the new directory to its parent
	status = kernelFileInsertEntry(entry, parentEntry);
	if (status < 0)
	{
		// We couldn't add the directory for whatever reason.
		kernelFileReleaseEntry(entry);
		goto out;
	}

	// Create the '.' and '..' entries inside the directory
	kernelFileMakeDotDirs(parentEntry, entry);

	// If the filesystem driver has a 'make dir' function, call it.
	if (driver->driverMakeDir)
	{
		status = driver->driverMakeDir(entry);
		if (status < 0)
			goto out;
	}

	// Update the timestamps on the parent directory
	updateModifiedTime(parentEntry);
	updateAccessedTime(parentEntry);

	// Write both directories
	if (driver->driverWriteDir)
	{
		status = driver->driverWriteDir(entry);
		if (status < 0)
			goto out;

		status = driver->driverWriteDir(parentEntry);
		if (status < 0)
			goto out;
	}

	// Return success
	status = 0;

out:
	kernelFree(prefix);
	kernelFree(name);
	return (status);
}


static int fileRemoveDir(kernelFileEntry *entry)
{
	// Remove an empty directory.

	int status = 0;
	kernelFileEntry *parentEntry = NULL;
	kernelDisk *fsDisk = NULL;
	kernelFilesystemDriver *driver = NULL;

	// Ok, do NOT remove the root directory
	if (entry == rootEntry)
	{
		kernelError(kernel_error, "Cannot remove the root directory under any "
			"circumstances");
		return (status = ERR_NODELETE);
	}

	// Make sure the item to delete is really a directory
	if (entry->type != dirT)
	{
		kernelError(kernel_error, "Item to delete is not a directory");
		return (status = ERR_NOTADIR);
	}

	// Make sure the directory to delete is empty
	if (!dirIsEmpty(entry))
	{
		kernelError(kernel_error, "Directory to delete is not empty");
		return (status = ERR_NOTEMPTY);
	}

	// Record the parent directory
	parentEntry = entry->parentDirectory;

	// Figure out which filesystem we're using
	fsDisk = (kernelDisk *) entry->disk;
	if (!fsDisk)
	{
		kernelError(kernel_error, "NULL disk pointer");
		return (status = ERR_BADADDRESS);
	}

	// Not allowed in read-only file system
	if (fsDisk->filesystem.readOnly)
	{
		kernelError(kernel_error, "Filesystem is read-only");
		return (status = ERR_NOWRITE);
	}

	driver = fsDisk->filesystem.driver;

	// If the filesystem driver has a 'remove dir' function, call it.
	if (driver->driverRemoveDir)
	{
		status = driver->driverRemoveDir(entry);
		if (status < 0)
			return (status);
	}

	// Now remove the '.' and '..' entries from the directory
	while (entry->contents)
	{
		kernelFileEntry *dotEntry = entry->contents;

		status = kernelFileRemoveEntry(dotEntry);
		if (status < 0)
			return (status);

		kernelFileReleaseEntry(dotEntry);
	}

	// Now remove the directory from its parent
	status = kernelFileRemoveEntry(entry);
	if (status < 0)
		return (status);

	kernelFileReleaseEntry(entry);

	// Update the times on the parent directory
	updateModifiedTime(parentEntry);
	updateAccessedTime(parentEntry);

	// Write the directory
	if (driver->driverWriteDir)
	{
		status = driver->driverWriteDir(parentEntry);
		if (status < 0)
			return (status);
	}

	// Return success
	return (status = 0);
}


static int fileDeleteRecursive(kernelFileEntry *entry)
{
	// Calls the fileDelete and fileRemoveDir functions, recursive-wise.

	int status = 0;
	kernelFileEntry *currEntry = NULL;
	kernelFileEntry *nextEntry = NULL;

	if (entry->type == dirT)
	{
		// Get the first file in the source directory
		currEntry = entry->contents;

		while (currEntry)
		{
			// Skip any '.' and '..' entries
			while (currEntry &&
				(!strcmp((char *) currEntry->name, ".") ||
					!strcmp((char *) currEntry->name, "..")))
			{
				currEntry = currEntry->nextEntry;
			}

			if (!currEntry)
				break;

			nextEntry = currEntry->nextEntry;

			if (currEntry->type == dirT)
			{
				status = fileDeleteRecursive(currEntry);
				if (status < 0)
					break;
			}
			else if (currEntry->type == fileT)
			{
				status = fileDelete(currEntry);
				if (status < 0)
					break;
			}

			currEntry = nextEntry;
		}

		status = fileRemoveDir(entry);
	}
	else
	{
		status = fileDelete(entry);
	}

	return (status);
}


static int fileCopy(file *sourceFile, file *destFile)
{
	// This function is used to copy the data of one (open) file to another
	// (open for creation/writing) file.  Returns 0 on success, negtive
	// otherwise.

	int status = 0;
	unsigned srcBlocks = 0;
	unsigned destBlocks = 0;
	unsigned bufferSize = 0;
	unsigned char *copyBuffer = NULL;
	unsigned srcBlocksPerOp = 0;
	unsigned destBlocksPerOp = 0;
	unsigned currentSrcBlock = 0;
	unsigned currentDestBlock = 0;

	// Any data to copy?
	if (!sourceFile->blocks)
		return (status = 0);

	srcBlocks = sourceFile->blocks;
	destBlocks = max(1, ((sourceFile->size / destFile->blockSize) +
		((sourceFile->size % destFile->blockSize) != 0)));

	kernelDebug(debug_fs, "File copy %s (%u blocks @ %u) to %s (%u blocks "
		"@ %u)", sourceFile->name, srcBlocks, sourceFile->blockSize,
		destFile->name, destBlocks, destFile->blockSize);

	// Try to allocate the largest copy buffer that we can.

	bufferSize = max((srcBlocks * sourceFile->blockSize),
		(destBlocks * destFile->blockSize));

	// (but no bigger than the maximum disk cache size)
	bufferSize = max(bufferSize, DISK_MAX_CACHE);

	while (!(copyBuffer = kernelMemoryGet(bufferSize, "file copy buffer")))
	{
		if ((bufferSize <= sourceFile->blockSize) ||
			(bufferSize <= destFile->blockSize))
		{
			kernelError(kernel_error, "Not enough memory to copy file %s",
				sourceFile->name);
			return (status = ERR_MEMORY);
		}

		bufferSize >>= 1;
	}

	srcBlocksPerOp = (bufferSize / sourceFile->blockSize);
	destBlocksPerOp = (bufferSize / destFile->blockSize);

	destFile->blocks = 0;

	// Copy the data.

	while (srcBlocks)
	{
		srcBlocksPerOp = min(srcBlocks, srcBlocksPerOp);
		destBlocksPerOp = min(destBlocks, destBlocksPerOp);

		// Read from the source file
		kernelDebug(debug_fs, "File read %u blocks from source",
			srcBlocksPerOp);
		status = kernelFileRead(sourceFile, currentSrcBlock, srcBlocksPerOp,
			copyBuffer);
		if (status < 0)
		{
			kernelMemoryRelease(copyBuffer);
			return (status);
		}

		// Write to the destination file
		kernelDebug(debug_fs, "File write %u blocks to dest", destBlocksPerOp);
		status = kernelFileWrite(destFile, currentDestBlock, destBlocksPerOp,
			copyBuffer);
		if (status < 0)
		{
			kernelMemoryRelease(copyBuffer);
			return (status);
		}

		// Blocks remaining
		srcBlocks -= srcBlocksPerOp;
		destBlocks -= destBlocksPerOp;

		// Block we're on
		currentSrcBlock += srcBlocksPerOp;
		currentDestBlock += destBlocksPerOp;
	}

	kernelMemoryRelease(copyBuffer);
	return (status = 0);
}


static int fileMove(kernelFileEntry *sourceEntry, kernelFileEntry *destDir)
{
	// This function is used to move or rename one file or directory to
	// another location or name.  Of course, this involves moving no actual
	// data, but rather moving the references to the data from one location
	// to another.  If the target location is the name of a directory, the
	// specified item is moved to that directory and will have the same
	// name as before.  If the target location is a file name, the item
	// will be moved and renamed.  Returns 0 on success, negtive otherwise.

	int status = 0;
	kernelDisk *fsDisk = NULL;
	kernelFilesystemDriver *driver = NULL;
	kernelFileEntry *sourceDir = NULL;

	// Save the source directory
	sourceDir = sourceEntry->parentDirectory;

	// Get the filesystem we're using
	fsDisk = (kernelDisk *) sourceEntry->disk;
	if (!fsDisk)
	{
		kernelError(kernel_error, "%s has a NULL source disk pointer",
			sourceEntry->name);
		return (status = ERR_BADADDRESS);
	}

	driver = fsDisk->filesystem.driver;

	// Not allowed in read-only file system
	if (fsDisk->filesystem.readOnly)
	{
		kernelError(kernel_error, "%s filesystem is read-only",
			fsDisk->filesystem.mountPoint);
		return (status = ERR_NOWRITE);
	}

	// Now check the filesystem of the destination directory.  Here's the rub:
	// moves can only occur within a single filesystem.
	if ((kernelDisk *) destDir->disk != fsDisk)
	{
		kernelError(kernel_error, "Can only move items within a single "
			"filesystem");
		return (status = ERR_INVALID);
	}

	// If the source item is a directory, make sure that the destination
	// directory is not a descendent of the source.  Moving a directory tree
	// into one of its own subtrees is paradoxical.  Also make sure that the
	// directory being moved is not the same as the destination directory
	if (sourceEntry->type == dirT)
	{
		if (isDescendent(destDir, sourceEntry))
		{
			// The destination is a descendant of the source
			kernelError(kernel_error, "Cannot move directory into one of its "
				"own subdirectories");
			return (status = ERR_PARADOX);
		}
	}

	// Remove the source item from its old directory
	status = kernelFileRemoveEntry(sourceEntry);
	if (status < 0)
		return (status);

	// Place the file in its new directory
	status = kernelFileInsertEntry(sourceEntry, destDir);
	if (status < 0)
	{
		// We were unable to place it in the new directory.  Better at
		// least try to put it back where it belongs in its old directory,
		// with its old name.  Whether this succeeds or fails, we need to try
		kernelFileInsertEntry(sourceEntry, sourceDir);
		return (status);
	}

	// Update some of the data items in the file entries
	updateAccessedTime(sourceEntry);
	updateModifiedTime(sourceDir);
	updateAccessedTime(sourceDir);
	updateModifiedTime(destDir);
	updateAccessedTime(destDir);

	// If the filesystem driver has a driverFileMoved function, call it
	if (driver->driverFileMoved)
	{
		status = driver->driverFileMoved(sourceEntry);
		if (status < 0)
			return (status);
	}

	// Write both directories
	if (driver->driverWriteDir)
	{
		status = driver->driverWriteDir(destDir);
		if (status < 0)
			return (status);

		status = driver->driverWriteDir(sourceDir);
		if (status < 0)
			return (status);
	}

	// Return success
	return (status = 0);
}


/////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////
//
//  Below here, the functions are exported for external use
//
/////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////

int kernelFileInitialize(void)
{
	// Nothing to do, but we're not initialized until the root directory
	// has been set, below
	return (0);
}


int kernelFileSetRoot(kernelFileEntry *_rootEntry)
{
	// This just sets the root filesystem pointer

	int status = 0;

	// Check params
	if (!_rootEntry)
	{
		kernelError(kernel_error, "NULL parameter");
		return (status = ERR_NULLPARAMETER);
	}

	// Assign it to the variable
	rootEntry = _rootEntry;

	initialized = 1;

	// Return success
	return (status = 0);
}


kernelFileEntry *kernelFileNewEntry(kernelDisk *fsDisk)
{
	// This function will find an unused file entry and return it to the
	// calling function.

	int status = 0;
	kernelFileEntry *entry = NULL;
	kernelFilesystemDriver *driver = NULL;

	// Check params
	if (!fsDisk)
	{
		kernelError(kernel_error, "NULL parameter");
		return (entry = NULL);
	}

	// Make sure there is a free file entry available
	if (!numFreeEntries)
	{
		status = allocateFileEntries();
		if (status < 0)
			return (entry = NULL);
	}

	// Get a free file entry.  Grab it from the first spot
	entry = freeEntries;
	freeEntries = entry->nextEntry;
	numFreeEntries -= 1;

	// Clear it
	memset((void *) entry, 0, sizeof(kernelFileEntry));

	// Set some default time/date values
	updateAllTimes(entry);

	// We need to call the appropriate filesystem so that it can attach its
	// private data structure to the file

	// Make note of the assigned disk in the entry
	entry->disk = fsDisk;

	// Get a pointer to the filesystem driver
	driver = fsDisk->filesystem.driver;

	// OK, we just have to call the filesystem driver function
	if (driver->driverNewEntry)
	{
		status = driver->driverNewEntry(entry);
		if (status < 0)
			return (entry = NULL);
	}

	return (entry);
}


void kernelFileReleaseEntry(kernelFileEntry *entry)
{
	// This function takes a file entry to release, and puts it back in the
	// pool of free file entries.

	kernelDisk *fsDisk = NULL;
	kernelFilesystemDriver *driver = NULL;

	// Check params
	if (!entry)
	{
		kernelError(kernel_error, "NULL parameter");
		return;
	}

	// If there's some private filesystem data attached to the file entry
	// structure, we will need to release it.
	if (entry->driverData)
	{
		// Get the filesystem pointer from the file entry structure
		fsDisk = entry->disk;
		if (fsDisk)
		{
			// Get a pointer to the filesystem driver
			driver = fsDisk->filesystem.driver;

			// OK, we just have to check on the filesystem driver function
			if (driver->driverInactiveEntry)
				driver->driverInactiveEntry(entry);
		}
	}

	// Clear it out
	memset((void *) entry, 0, sizeof(kernelFileEntry));

	// Put the entry back into the pool of free entries.
	entry->nextEntry = freeEntries;
	freeEntries = entry;
	numFreeEntries += 1;
}


int kernelFileInsertEntry(kernelFileEntry *entry, kernelFileEntry *dirEntry)
{
	// This function is used to add a new entry to a target directory.  The
	// function will verify that the file does not already exist.

	int status = 0;
	int numberFiles = 0;
	kernelFileEntry *listEntry = NULL;
	kernelFileEntry *previousEntry = NULL;

	// Check params
	if (!entry || !dirEntry)
	{
		kernelError(kernel_error, "NULL parameter");
		return (status = ERR_NULLPARAMETER);
	}

	// Make sure directory is really a directory
	if (dirEntry->type != dirT)
	{
		kernelError(kernel_error, "Entry in which to insert file is not a "
			"directory");
		return (status = ERR_NOTADIR);
	}

	// Make sure the entry does not already exist
	listEntry = dirEntry->contents;
	while (listEntry)
	{
		// We do a case-sensitive comparison here, regardless of whether
		// the filesystem driver cares about case.  We are worried about
		// exact matches.
		if (!strcmp((char *) listEntry->name, (char *) entry->name))
		{
			kernelError(kernel_error, "A file by the name \"%s\" already "
				"exists in the directory \"%s\"", entry->name, dirEntry->name);
			return (status = ERR_ALREADY);
		}

		listEntry = listEntry->nextEntry;
	}

	// Make sure that the number of entries in this directory has not
	// exceeded (and is not about to exceed) the maximum number of legal
	// directory entries

	numberFiles = kernelFileCountDirEntries(dirEntry);

	if (numberFiles >= MAX_DIRECTORY_ENTRIES)
	{
		// Make an error that the directory is full
		kernelError(kernel_error, "The directory is full; can't create new "
			"entry");
		return (status = ERR_NOCREATE);
	}

	// Set the parent directory
	entry->parentDirectory = dirEntry;

	// Set the "list item" pointer to the start of the file chain
	listEntry = dirEntry->contents;

	// For each file in the file chain, we loop until we find a filename that
	// is alphabetically greater than our new entry.  At that point, we insert
	// our new entry in the previous spot.

	while (listEntry)
	{
		// Make sure we don't scan the '.' or '..' entries, if there are any
		if (!strcmp((char *) listEntry->name, ".") ||
			!strcmp((char *) listEntry->name, ".."))
		{
			// Move to the next filename.
			previousEntry = listEntry;
			listEntry = listEntry->nextEntry;
			continue;
		}

		if (strcmp((char *) listEntry->name, (char *) entry->name) < 0)
		{
			// This item is alphabetically "less than" the one we're
			// inserting.  Move to the next filename.
			previousEntry = listEntry;
			listEntry = listEntry->nextEntry;
		}
		else
		{
			break;
		}
	}

	// When we fall through to this point, there are a few possible scenarios:
	// 1. the filenames we now have (of the new entry and an existing
	//	entry in the list) are identical
	// 2. The list item is alphabetically "after" our new entry.
	// 3. The directory is empty, or we reached the end of the file chain
	//	without finding anything that's alphabetically "after" our new entry

	// If the directory is empty, or we've reached the end of the list,
	// we should insert the new entry there
	if (!listEntry)
	{
		if (!dirEntry->contents)
		{
			// It's the first file
			dirEntry->contents = entry;
		}
		else
		{
			// It's the last file
			previousEntry->nextEntry = entry;
			entry->previousEntry = previousEntry;
		}

		// Either way, there's no next entry
		entry->nextEntry = NULL;
	}

	// If the filenames are the same it's an error.  We are only worried
	// about exact matches, so do a case-sensitive comparison regardless of
	// whether the filesystem driver cares about case.
	else if (!strcmp((char *) entry->name, (char *) listEntry->name))
	{
		// Oops, the file exists
		kernelError(kernel_error, "A file named '%s' exists in the directory",
			entry->name);
		return (status = ERR_ALREADY);
	}

	// Otherwise, listEntry points to an entry that should come AFTER
	// our new entry.  We insert our entry into the previous slot.  Watch
	// out just in case we're BECOMING the first item in the list!
	else
	{
		if (previousEntry)
		{
			previousEntry->nextEntry = entry;
			entry->previousEntry = previousEntry;
		}
		else
		{
			// We're the new first item in the list
			dirEntry->contents = entry;
			entry->previousEntry = NULL;
		}

		entry->nextEntry = listEntry;
		listEntry->previousEntry = entry;
	}

	// Update the access time on the directory
	dirEntry->lastAccess = kernelCpuTimestamp();

	// Don't mark the directory as dirty; that is the responsibility of the
	// caller, since this function is used in building initial directory
	// structures by the filesystem drivers.

	// Return success
	return (status = 0);
}


int kernelFileRemoveEntry(kernelFileEntry *entry)
{
	// This function is internal, and is used to delete an entry from its
	// parent directory.  It DOES NOT deallocate the file entry structure
	// itself.  That must be done, if applicable, by the calling function.

	kernelFileEntry *parentEntry = NULL;
	kernelFileEntry *previousEntry = NULL;
	kernelFileEntry *nextEntry = NULL;
	int status = 0;

	// Make sure that the pointer we're receiving is not NULL
	if (!entry)
	{
		kernelError(kernel_error, "NULL parameter");
		return (status = ERR_NOSUCHFILE);
	}

	// The directory to delete from is the entry's parent directory
	parentEntry = entry->parentDirectory;
	if (!parentEntry)
	{
		kernelError(kernel_error, "File entry %s has a NULL parent directory",
			entry->name);
		return (status = ERR_NOSUCHFILE);
	}

	// Make sure the parent dir is really a directory
	if (parentEntry->type != dirT)
	{
		kernelError(kernel_error, "Parent entry of %s is not a directory",
			entry->name);
		return (status = ERR_NOTADIR);
	}

	// Remove the item from its place in the directory.

	// Get the item's previous and next pointers
	previousEntry = entry->previousEntry;
	nextEntry = entry->nextEntry;

	// If we're deleting the first file of the directory, we have to
	// change the parent directory's "first file" pointer (In addition to
	// the other things we do) to point to the following file, if any.
	if (entry == parentEntry->contents)
		parentEntry->contents = nextEntry;

	// Make this item's 'previous' item refer to this item's 'next' as its
	// own 'next'.  Did that sound bitchy?
	if (previousEntry)
		previousEntry->nextEntry = nextEntry;

	if (nextEntry)
		nextEntry->previousEntry = previousEntry;

	// Remove references to its position from this entry
	entry->parentDirectory = NULL;
	entry->previousEntry = NULL;
	entry->nextEntry = NULL;

	// Update the access time on the directory
	parentEntry->lastAccess = kernelCpuTimestamp();

	// Don't mark the directory as dirty; that is the responsibility of the
	// caller, since this function is used for things like unbuffering
	// directories.

	return (status = 0);
}


int kernelFileGetFullName(kernelFileEntry *entry, char *buffer, int buffLen)
{
	// Given a kernelFileEntry, construct the fully qualified name

	// Check params
	if (!entry || !buffer)
	{
		kernelError(kernel_error, "NULL parameter");
		return (ERR_NULLPARAMETER);
	}

	buffer[0] = '\0';
	buildFilenameRecursive(entry, buffer, buffLen);
	return (0);
}


kernelFileEntry *kernelFileLookup(const char *origPath)
{
	// This is an external wrapper for our internal fileLookup function.

	char *fixedPath = NULL;
	kernelFileEntry *entry = NULL;

	// Check params
	if (!origPath)
	{
		kernelError(kernel_error, "NULL parameter");
		return (entry = NULL);
	}

	// Fix up the path
	fixedPath = fixupPath(origPath);
	if (!fixedPath)
		return (entry = NULL);

	entry = fileLookup(fixedPath);

	kernelFree(fixedPath);

	return (entry);
}


kernelFileEntry *kernelFileResolveLink(kernelFileEntry *entry)
{
	// Take a file entry, and if it is a link, resolve the link.

	kernelFilesystemDriver *driver = NULL;

	if (!entry || (entry->contents == entry))
		return (entry);

	if ((entry->type == linkT) && !entry->contents)
	{
		driver = entry->disk->filesystem.driver;

		if (driver->driverResolveLink)
			// Try to get the filesystem driver to resolve the link
			driver->driverResolveLink(entry);

		entry = entry->contents;
		if (!entry)
			return (entry);
	}

	// If this is a link, recurse
	if (entry->type == linkT)
		entry = kernelFileResolveLink(entry->contents);

	return (entry);
}


int kernelFileCountDirEntries(kernelFileEntry *entry)
{
	// This function is used to count the current number of directory entries
	// in a given directory.  On success it returns the number of entries.
	// Returns negative on error

	int fileCount = 0;
	kernelFileEntry *listEntry = NULL;

	// Check params
	if (!entry)
	{
		kernelError(kernel_error, "NULL parameter");
		return (fileCount = ERR_NULLPARAMETER);
	}

	// Make sure the directory is really a directory
	if (entry->type != dirT)
	{
		kernelError(kernel_error, "Entry in which to count entries is not a "
			"directory");
		return (fileCount = ERR_NOTADIR);
	}

	// Assign the first item in the directory to our iterator pointer
	listEntry = entry->contents;

	while (listEntry)
	{
		listEntry = listEntry->nextEntry;
		fileCount += 1;
	}

	return (fileCount);
}


int kernelFileMakeDotDirs(kernelFileEntry *parentEntry, kernelFileEntry *entry)
{
	// This just makes the '.' and '..' links in a new directory

	int status = 0;
	kernelFileEntry *dotEntry = NULL;
	kernelFileEntry *dotDotEntry = NULL;

	// Check params
	if (!entry)
	{
		kernelError(kernel_error, "NULL parameter");
		return (status = ERR_NULLPARAMETER);
	}

	dotEntry = kernelFileNewEntry(entry->disk);
	if (!dotEntry)
		return (status = ERR_NOFREE);

	strcpy((char *) dotEntry->name, ".");
	dotEntry->type = linkT;
	dotEntry->contents = entry;

	status = kernelFileInsertEntry(dotEntry, entry);

	if (parentEntry)
	{
		dotDotEntry = kernelFileNewEntry(entry->disk);
		if (!dotDotEntry)
		{
			kernelFileReleaseEntry(dotEntry);
			return (status = ERR_NOFREE);
		}

		strcpy((char *) dotDotEntry->name, "..");
		dotDotEntry->type = linkT;
		dotDotEntry->contents = entry->parentDirectory;

		status = kernelFileInsertEntry(dotDotEntry, entry);
	}

	return (status);
}


int kernelFileUnbufferRecursive(kernelFileEntry *entry)
{
	// This function can be used to unbuffer a directory tree recursively.
	// Mostly, this will be useful only when unmounting filesystems.
	// Returns 0 on success, negative otherwise.

	int status = 0;
	kernelFileEntry *listEntry = NULL;

	// Is entry a directory?
	if (entry->type != dirT)
		return (status = ERR_NOTADIR);

	// Is the current directory a leaf directory?
	if (!isLeafDir(entry))
	{
		// It's not a leaf directory.  We will need to walk through each of
		// the entries and do a recursion for any subdirectories that have
		// buffered content.
		listEntry = entry->contents;
		while (listEntry)
		{
			if ((listEntry->type == dirT) && listEntry->contents)
			{
				status = kernelFileUnbufferRecursive(listEntry);
				if (status < 0)
					return (status);
			}
			else
			{
				listEntry = listEntry->nextEntry;
			}
		}
	}

	// Now this directory should be a leaf directory.  Do any of its files
	// currently have a valid lock?
	listEntry = entry->contents;
	while (listEntry)
	{
		if (kernelLockVerify(&listEntry->lock))
			break;
		else
			listEntry = listEntry->nextEntry;
	}

	if (listEntry)
		// There are open files here
		return (status = ERR_BUSY);

	// This directory is safe to unbuffer
	unbufferDirectory(entry);

	return (status = 0);
}


int kernelFileEntrySetSize(kernelFileEntry *entry, unsigned newSize)
{
	// This file allows the caller to specify the real size of a file, since
	// the other functions here must assume that the file consumes all of the
	// space in all of its blocks (they have no way to know otherwise).

	int status = 0;
	kernelDisk *fsDisk = NULL;
	unsigned newBlocks = 0;

	// Check parameters
	if (!entry)
		return (status = ERR_NULLPARAMETER);

	fsDisk = (kernelDisk *) entry->disk;

	// Not allowed in read-only file system
	if (fsDisk->filesystem.readOnly)
	{
		kernelError(kernel_error, "Filesystem is read-only");
		return (status = ERR_NOWRITE);
	}

	newBlocks = ((newSize + (fsDisk->filesystem.blockSize - 1)) /
		 fsDisk->filesystem.blockSize);

	// Does the new size change the number of blocks?
	if (entry->blocks != newBlocks)
	{
		if (fsDisk->filesystem.driver->driverSetBlocks)
		{
			status = fsDisk->filesystem.driver->driverSetBlocks(entry,
				newBlocks);
			if (status < 0)
				return (status);
		}
		else
		{
			kernelError(kernel_error, "Filesystem driver for %s cannot change "
				"the number of blocks", entry->name);
			return (status = ERR_NOSUCHFUNCTION);
		}
	}

	entry->size = newSize;
	entry->blocks = newBlocks;

	if (fsDisk->filesystem.driver->driverWriteDir)
	{
		status = fsDisk->filesystem.driver->
			driverWriteDir(entry->parentDirectory);
		if (status < 0)
			return (status);
	}

	// Return success
	return (status = 0);
}


int kernelFileFixupPath(const char *origPath, char *newPath)
{
	// This function is a user-accessible wrapper for our internal
	// fixupPath() funtion, above.

	int status = 0;
	char *tmpPath = NULL;

	// Check params
	if (!origPath || !newPath)
	{
		kernelError(kernel_error, "NULL parameter");
		return (status = ERR_NULLPARAMETER);
	}

	tmpPath = fixupPath(origPath);
	if (!tmpPath)
		return (status = ERR_NOSUCHENTRY);

	strcpy(newPath, tmpPath);

	kernelFree(tmpPath);

	// Return success;
	return (status = 0);
}


int kernelFileSeparateLast(const char *origPath, char *pathName,
	char *fileName)
{
	// This function will take a combined pathname/filename string and
	// separate the two.  The caller will pass in the "combined" string along
	// with two pre-allocated char arrays to hold the resulting separated
	// elements.

	int status = 0;
	char *fixedPath = NULL;
	int count, combinedLength;

	// Check params
	if (!origPath || !pathName || !fileName)
	{
		kernelError(kernel_error, "NULL parameter");
		return (status = ERR_NULLPARAMETER);
	}

	// Initialize the fileName and pathName strings
	fileName[0] = NULL;
	pathName[0] = NULL;

	// Fix up the path
	fixedPath = fixupPath(origPath);
	if (!fixedPath)
		return (status = ERR_NOSUCHENTRY);

	if (!strcmp(fixedPath, "/"))
	{
		strcpy(pathName, fixedPath);
		kernelFree(fixedPath);
		return (status = 0);
	}

	combinedLength = strlen(fixedPath);

	// Make sure there's something there, and that we're not exceeding the
	// limit for the combined path and filename
	if ((combinedLength <= 0) || (combinedLength >= MAX_PATH_NAME_LENGTH))
	{
		kernelFree(fixedPath);
		return (status = ERR_RANGE);
	}

	// Do a loop backwards from the end of the combined path until we hit
	// a(nother) '/' or '\' character.  Of course, that might be the '/'
	// or '\' of root directory fame.  We know we'll hit at least one.
	for (count = (combinedLength - 1);
		((count >= 0) && !ISSEPARATOR(fixedPath[count])); count --)
	{
		// (empty loop body is deliberate)
	}

	// Now, count points at the offset of the last '/' or '\' character before
	// the final component of the path name

	// Make sure neither the path or the combined exceed the maximum
	// lengths
	if ((count > MAX_PATH_LENGTH) ||
		((combinedLength - count) > MAX_NAME_LENGTH))
	{
		kernelError(kernel_error, "File path exceeds maximum length");
		kernelFree(fixedPath);
		return (status = ERR_BOUNDS);
	}

	// Copy everything before count into the path string.  We skip the
	// trailing '/' or '\' unless it's the first character
	if (!count)
	{
		pathName[0] = fixedPath[0];
		pathName[1] = '\0';
	}
	else
	{
		strncpy(pathName, fixedPath, count);
		pathName[count] = '\0';
	}

	// Copy everything after it into the name string
	strncpy(fileName, (fixedPath + (count + 1)),
		(combinedLength - (count + 1)));
	fileName[combinedLength - (count + 1)] = '\0';

	kernelFree(fixedPath);
	return (status = 0);
}


/////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////
//
//  Below here, the functions are largely external wrappers for
//  functions above, which are exported outside the kernel and accept
//  text strings for file names, etc.
//
/////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////

int kernelFileGetDisk(const char *path, disk *userDisk)
{
	// Given a filename, return the the disk it resides on.

	int status = 0;
	kernelFileEntry *entry = NULL;

	if (!initialized)
		return (status = ERR_NOTINITIALIZED);

	// Check parameters.
	if (!path || !userDisk)
	{
		kernelError(kernel_error, "NULL parameter");
		return (status = ERR_NULLPARAMETER);
	}

	entry = kernelFileLookup(path);
	if (!entry)
		// There is no such item
		return (status = ERR_NOSUCHFILE);

	return (status = kernelDiskFromLogical(entry->disk, userDisk));
}


int kernelFileCount(const char *path)
{
	// This is a user-accessible wrapper function for the
	// kernelFileCountDirEntries function, above.

	int status = 0;
	kernelFileEntry *entry = NULL;

	if (!initialized)
		return (status = ERR_NOTINITIALIZED);

	// Check params
	if (!path)
	{
		kernelError(kernel_error, "NULL parameter");
		return (status = ERR_NULLPARAMETER);
	}

	// Make the starting directory correspond to the path we were given
	entry = kernelFileLookup(path);

	// Make sure it's good, and that it's really a directory
	if (!entry || (entry->type != dirT))
	{
		kernelError(kernel_error, "Invalid directory \"%s\" for lookup", path);
		return (status = ERR_NOSUCHFILE);
	}

	return (kernelFileCountDirEntries(entry));
}


int kernelFileFirst(const char *path, file *fileStruct)
{
	// Finds the first entry in a directory, and if found, converts it to a
	// userspace file structure.

	int status = 0;
	kernelFileEntry *entry = NULL;

	if (!initialized)
		return (status = ERR_NOTINITIALIZED);

	// Check params
	if (!path || !fileStruct)
	{
		kernelError(kernel_error, "NULL parameter");
		return (status = ERR_NULLPARAMETER);
	}

	// Make the starting directory correspond to the path we were given
	entry = kernelFileLookup(path);

	if (!entry)
	{
		kernelError(kernel_error, "No such directory \"%s\" for lookup", path);
		return (status = ERR_NOSUCHFILE);
	}

	if (entry->type != dirT)
	{
		kernelError(kernel_error, "\"%s\" is not a directory", path);
		return (status = ERR_INVALID);
	}

	fileStruct->handle = NULL;  // INVALID

	if (!entry->contents)
		// The directory is empty
		return (status = ERR_NOSUCHFILE);

	fileEntry2File(entry->contents, fileStruct);

	// Return success
	return (status = 0);
}


int kernelFileNext(const char *path, file *fileStruct)
{
	// Finds the next entry in a directory, and if found, converts it to a
	// userspace file structure.

	int status = 0;
	kernelFileEntry *entry = NULL;
	kernelFileEntry *listEntry = NULL;

	if (!initialized)
		return (status = ERR_NOTINITIALIZED);

	// Check params
	if (!path || !fileStruct)
	{
		kernelError(kernel_error, "NULL parameter");
		return (status = ERR_NULLPARAMETER);
	}

	// Make the starting directory correspond to the path we were given
	entry = kernelFileLookup(path);

	// Make sure it's good, and that it's really a directory
	if (!entry || (entry->type != dirT))
	{
		kernelError(kernel_error, "Invalid directory for lookup");
		return (status = ERR_NOSUCHFILE);
	}

	// Find the previously accessed file in the current directory.  Start
	// with the first entry.
	if (entry->contents)
	{
		listEntry = entry->contents;
	}
	else
	{
		kernelError(kernel_error, "No file entries in directory");
		return (status = ERR_NOSUCHFILE);
	}

	while (strcmp((char *) listEntry->name, fileStruct->name) &&
		listEntry->nextEntry)
	{
		listEntry = listEntry->nextEntry;
	}

	if (!strcmp((char *) listEntry->name, fileStruct->name) &&
		listEntry->nextEntry)
	{
		// Now we've found that last item.  Move one more down the list
		listEntry = listEntry->nextEntry;

		fileEntry2File(listEntry, fileStruct);
		fileStruct->handle = NULL;  // INVALID
	}
	else
	{
		// There are no more files
		return (status = ERR_NOSUCHFILE);
	}

	// Return success
	return (status = 0);
}


int kernelFileFind(const char *path, file *fileStruct)
{
	// This is a wrapper for our kernelFileLookup() function.

	int status = 0;
	kernelFileEntry *entry = NULL;

	if (!initialized)
		return (status = ERR_NOTINITIALIZED);

	// Check params.  It's OK for the file structure to be NULL.
	if (!path)
	{
		kernelError(kernel_error, "NULL parameter");
		return (status = ERR_NULLPARAMETER);
	}

	// Call the function that actually finds the item
	entry = kernelFileLookup(path);
	if (!entry)
		// There is no such item
		return (status = ERR_NOSUCHFILE);

	// Otherwise, we are OK, we got a good file.  If a file structure pointer
	// was supplied, we should now copy the relevant information from the
	// kernelFileEntry structure into the user structure.
	if (fileStruct)
	{
		fileEntry2File(entry, fileStruct);
		fileStruct->handle = NULL;  // INVALID UNTIL OPENED
	}

	// Return success
	return (status = 0);
}


int kernelFileOpen(const char *fileName, int openMode, file *fileStruct)
{
	// This is an externalized version of the 'fileOpen' function above.  It
	// Accepts a string filename and returns a filled user-space file
	// structure;

	int status = 0;
	char *fixedName = NULL;
	kernelFileEntry *entry = NULL;

	if (!initialized)
		return (status = ERR_NOTINITIALIZED);

	// Check params
	if (!fileName || !fileStruct)
	{
		kernelError(kernel_error, "NULL parameter");
		return (status = ERR_NULLPARAMETER);
	}

	// Fix up the path name
	fixedName = fixupPath(fileName);
	if (!fixedName)
		return (status = ERR_NOSUCHENTRY);

	// First thing's first.  We might be able to short-circuit this whole
	// process if the file exists already.  Look for it.
	entry = kernelFileLookup(fixedName);
	if (!entry)
	{
		// The file doesn't currently exist.  Were we told to create it?
		if (!(openMode & OPENMODE_CREATE))
		{
			// We aren't supposed to create the file, so return an error
			kernelError(kernel_error, "File %s does not exist", fileName);
			kernelFree(fixedName);
			return (status = ERR_NOSUCHFILE);
		}

		// We're going to create the file.
		status = fileCreate(fixedName);
		if (status < 0)
		{
			kernelFree(fixedName);
			return (status);
		}

		// Now find it.
		entry = fileLookup(fixedName);
		if (!entry)
		{
			// Something is really wrong
			kernelFree(fixedName);
			return (status = ERR_NOCREATE);
		}
	}

	kernelFree(fixedName);

	// Call our internal openFile function
	status = fileOpen(entry, openMode);
	if (status < 0)
		return (status);

	// Set up the file handle
	fileEntry2File(entry, fileStruct);

	// Set the "open mode" flags on the opened file
	fileStruct->openMode = openMode;

	// Return success
	return (status = 0);
}


int kernelFileClose(file *fileStruct)
{
	// Reduce the open count of the file entry, and release any lock

	int status = 0;
	kernelFileEntry *entry = NULL;

	if (!initialized)
		return (status = ERR_NOTINITIALIZED);

	// Check params
	if (!fileStruct)
	{
		kernelError(kernel_error, "NULL parameter");
		return (status = ERR_NULLPARAMETER);
	}

	// Make sure the file handle refers to a valid file
	entry = (kernelFileEntry *) fileStruct->handle;
	if (!entry)
		// Probably wasn't opened first.  No big deal.
		return (status = ERR_NULLPARAMETER);

	// Reduce the open count on the file in question
	if (entry->openCount > 0)
		entry->openCount -= 1;

	// If the file was locked by this PID, we should unlock it
	kernelLockRelease(&entry->lock);

	// If we were asked to delete the file on closure, attempt to comply
	if (fileStruct->openMode & OPENMODE_DELONCLOSE)
		fileDelete(entry);

	// Return success
	return (status = 0);
}


int kernelFileRead(file *fileStruct, unsigned blockNum, unsigned blocks,
	void *fileBuffer)
{
	// This function is responsible for all reading of files.  It takes a
	// file structure of an opened file, the number of blocks to skip before
	// commencing the read, the number of blocks to read, and a buffer to use.
	// Returns negative on error, otherwise it returns the same status as
	// the driver function.

	int status = 0;
	kernelDisk *fsDisk = NULL;
	kernelFilesystemDriver *driver = NULL;

	if (!initialized)
		return (status = ERR_NOTINITIALIZED);

	// Check params
	if (!fileStruct || !fileBuffer)
	{
		kernelError(kernel_error, "NULL parameter");
		return (status = ERR_NULLPARAMETER);
	}

	if (!fileStruct->handle)
	{
		kernelError(kernel_error, "NULL file handle for read.  Not opened "
			"first?");
		return (status = ERR_NULLPARAMETER);
	}

	// Make sure the number of blocks to read is non-zero
	if (!blocks)
		// This isn't an error exactly, there just isn't anything to do.
		return (status = 0);

	// Get the logical disk from the file structure
	fsDisk = ((kernelFileEntry *) fileStruct->handle)->disk;
	if (!fsDisk)
	{
		kernelError(kernel_error, "NULL disk pointer");
		return (status = ERR_BADADDRESS);
	}

	driver = fsDisk->filesystem.driver;

	// OK, we just have to check on the filesystem driver function we want
	// to call
	if (!driver->driverReadFile)
	{
		kernelError(kernel_error, "The requested filesystem operation is not "
			"supported");
		return (status = ERR_NOSUCHFUNCTION);
	}

	// Now we can call our target function
	status = driver->driverReadFile(fileStruct->handle, blockNum, blocks,
		fileBuffer);

	// Make sure the file structure is up to date after the call
	fileEntry2File(fileStruct->handle, fileStruct);

	// Return the same error status that the driver function returned
	return (status);
}


int kernelFileWrite(file *fileStruct, unsigned blockNum, unsigned blocks,
	void *fileBuffer)
{
	// This function is responsible for all writing of files.  It takes a
	// file structure of an opened file, the number of blocks to skip before
	// commencing the write, the number of blocks to write, and a buffer to
	// use.  Returns negative on error, otherwise it returns the same status as
	// the driver function.

	int status = 0;
	kernelDisk *fsDisk = NULL;
	kernelFilesystemDriver *driver = NULL;

	if (!initialized)
		return (status = ERR_NOTINITIALIZED);

	// Check params
	if (!fileStruct || !fileBuffer)
	{
		kernelError(kernel_error, "NULL parameter");
		return (status = ERR_NULLPARAMETER);
	}

	if (!fileStruct->handle)
	{
		kernelError(kernel_error, "NULL file handle for write.  Not opened "
			"first?");
		return (status = ERR_NULLPARAMETER);
	}

	// Make sure the number of blocks to write is non-zero
	if (!blocks)
	{
		// Eek.  Nothing to do.  Why were we called?
		kernelError(kernel_error, "File blocks to write is zero");
		return (status = ERR_NODATA);
	}

	// Has the file been opened properly for writing?
	if (!(fileStruct->openMode & OPENMODE_WRITE))
	{
		kernelError(kernel_error, "File has not been opened for writing");
		return (status = ERR_INVALID);
	}

	// Get the logical disk from the file structure
	fsDisk = ((kernelFileEntry *) fileStruct->handle)->disk;
	if (!fsDisk)
	{
		kernelError(kernel_error, "NULL disk pointer");
		return (status = ERR_BADADDRESS);
	}

	// Not allowed in read-only file system
	if (fsDisk->filesystem.readOnly)
	{
		kernelError(kernel_error, "Filesystem is read-only");
		return (status = ERR_NOWRITE);
	}

	driver = fsDisk->filesystem.driver;

	// OK, we just have to check on the filesystem driver function we want
	// to call
	if (!driver->driverWriteFile)
	{
		kernelError(kernel_error, "The requested filesystem operation is not "
			"supported");
		return (status = ERR_NOSUCHFUNCTION);
	}

	// Now we can call our target function
	status = driver->driverWriteFile(fileStruct->handle, blockNum, blocks,
		fileBuffer);
	if (status < 0)
		return (status);

	// Update the directory
	if (driver->driverWriteDir)
	{
		status = driver->driverWriteDir(((kernelFileEntry *)
			fileStruct->handle)->parentDirectory);
		if (status < 0)
			return (status);
	}

	// Make sure the file structure is up to date after the call
	fileEntry2File(fileStruct->handle, fileStruct);

	// Return the same error status that the driver function returned
	return (status = 0);
}


int kernelFileDelete(const char *fileName)
{
	// Given a file name, delete the file.

	int status = 0;
	kernelFileEntry *entry = NULL;

	if (!initialized)
		return (status = ERR_NOTINITIALIZED);

	// Check params
	if (!fileName)
	{
		kernelError(kernel_error, "NULL parameter");
		return (status = ERR_NULLPARAMETER);
	}

	// Get the file to delete
	entry = kernelFileLookup(fileName);
	if (!entry)
	{
		// The directory does not exist
		kernelError(kernel_error, "File %s to delete does not exist",
			fileName);
		return (status = ERR_NOSUCHFILE);
	}

	// OK, the file exists.  Call our internal 'delete' function
	return (fileDelete(entry));
}


int kernelFileDeleteRecursive(const char *itemName)
{
	int status = 0;
	kernelFileEntry *entry = NULL;

	if (!initialized)
		return (status = ERR_NOTINITIALIZED);

	// Check params
	if (!itemName)
	{
		kernelError(kernel_error, "NULL parameter");
		return (status = ERR_NULLPARAMETER);
	}

	// Get the file to delete
	entry = kernelFileLookup(itemName);
	if (!entry)
	{
		// The directory does not exist
		kernelError(kernel_error, "Item %s to delete does not exist",
			itemName);
		return (status = ERR_NOSUCHFILE);
	}

	return (fileDeleteRecursive(entry));
}


int kernelFileDeleteSecure(const char *fileName, int passes)
{
	// This function deletes a file, but first does a number of passes of
	// writing random data over top of the file, followed by a pass of NULLs,
	// and finally does a normal file deletion.

	int status = 0;
	kernelFileEntry *entry = NULL;
	kernelFilesystemDriver *driver = NULL;
	unsigned blockSize = 0;
	unsigned bufferSize = 0;
	unsigned char *buffer = NULL;
	int count1;
	unsigned count2;

	if (!initialized)
		return (status = ERR_NOTINITIALIZED);

	// Check params
	if (!fileName)
	{
		kernelError(kernel_error, "NULL parameter");
		return (status = ERR_NULLPARAMETER);
	}

	// Now make sure that the requested file exists
	entry = kernelFileLookup(fileName);
	if (!entry)
	{
		// The directory does not exist
		kernelError(kernel_error, "File %s to delete does not exist",
			fileName);
		return (status = ERR_NOSUCHFILE);
	}

	// Not allowed in read-only file system
	if (entry->disk->filesystem.readOnly)
	{
		kernelError(kernel_error, "Filesystem is read-only");
		return (status = ERR_NOWRITE);
	}

	driver = entry->disk->filesystem.driver;
	if (!driver->driverWriteFile)
	{
		kernelError(kernel_error, "The requested filesystem operation is not "
			"supported");
		return (status = ERR_NOSUCHFUNCTION);
	}

	// Get a buffer the as big as all of the blocks allocated to the file.
	blockSize = entry->disk->filesystem.blockSize;
	bufferSize = (entry->blocks * blockSize);
	buffer = kernelMalloc(bufferSize);
	if (!buffer)
	{
		kernelError(kernel_error, "Unable to obtain enough memory to "
			"securely delete %s", fileName);
		return (status = ERR_MEMORY);
	}

	for (count1 = 0; count1 < passes; count1 ++)
	{
		if (count1 < (passes - 1))
		{
			// Fill the buffer with semi-random data
			for (count2 = 0; count2 < blockSize; count2 ++)
				buffer[count2] = kernelRandomFormatted(0, 255);

			for (count2 = 1; count2 < entry->blocks; count2 ++)
				memcpy((buffer + (count2 * blockSize)), buffer, blockSize);
		}
		else
		{
			// Clear the buffer with NULLs
			memset(buffer, 0, bufferSize);
		}

		// Write the file
		status = driver->driverWriteFile(entry, 0, entry->blocks, buffer);

		// Sync the disk to make sure the data has been written out
		kernelDiskSync((char *) entry->disk->name);

		if (status < 0)
			break;
	}

	kernelFree(buffer);

	if (status < 0)
		return (status);

	// Now do a normal file deletion.
	return (status = fileDelete(entry));
}


int kernelFileMakeDir(const char *name)
{
	// This is an externalized version of the fileMakeDir function, above.

	int status = 0;
	char *fixedName = NULL;

	if (!initialized)
		return (status = ERR_NOTINITIALIZED);

	// Check params
	if (!name)
	{
		kernelError(kernel_error, "NULL parameter");
		return (status = ERR_NULLPARAMETER);
	}

	// Fix up the path name
	fixedName = fixupPath(name);
	if (!fixedName)
		return (status = ERR_NOSUCHENTRY);

	// Make sure it doesn't exist
	if (fileLookup(fixedName))
	{
		kernelError(kernel_error, "Entry named %s already exists", fixedName);
		kernelFree(fixedName);
		return (status = ERR_ALREADY);
	}

	status = fileMakeDir(fixedName);

	kernelFree(fixedName);

	return (status);
}


int kernelFileRemoveDir(const char *path)
{
	// This is an externalized version of the fileRemoveDir function, above.

	int status = 0;
	kernelFileEntry *entry = NULL;

	if (!initialized)
		return (status = ERR_NOTINITIALIZED);

	// Check params
	if (!path)
	{
		kernelError(kernel_error, "NULL parameter");
		return (status = ERR_NULLPARAMETER);
	}

	// Look for the requested directory
	entry = kernelFileLookup(path);
	if (!entry)
	{
		// The directory does not exist
		kernelError(kernel_error, "Directory %s to delete does not exist",
			path);
		return (status = ERR_NOSUCHDIR);
	}

	return (fileRemoveDir(entry));
}


int kernelFileCopy(const char *srcName, const char *destName)
{
	// Given source and destination file paths, copy the file pointed to by
	// 'srcPath', and copy it to either the directory or file name pointed
	// to by 'destPath'

	int status = 0;
	char *fixedSrcName = NULL;
	char *fixedDestName = NULL;
	file srcFileStruct;
	file destFileStruct;
	kernelFileEntry *entry = NULL;
	uquad_t freeSpace = 0;

	if (!initialized)
		return (status = ERR_NOTINITIALIZED);

	// Check params
	if (!srcName || !destName)
	{
		kernelError(kernel_error, "NULL parameter");
		return (status = ERR_NULLPARAMETER);
	}

	// Fix up the two pathnames

	fixedSrcName = fixupPath(srcName);
	if (!fixedSrcName)
		return (status = ERR_INVALID);

	fixedDestName = fixupPath(destName);
	if (!fixedDestName)
	{
		kernelFree(fixedSrcName);
		return (status = ERR_INVALID);
	}

	// Attempt to open the source file for reading (the open function will
	// check the source name argument for us)
	status = kernelFileOpen(fixedSrcName, OPENMODE_READ, &srcFileStruct);
	if (status < 0)
		goto out;

	// Determine whether the destination file exists, and if so whether it
	// is a directory.
	entry = fileLookup(fixedDestName);

	if (entry)
	{
		// It already exists.  Is it a directory?
		if (entry->type == dirT)
		{
			// It's a directory, so what we really want to do is make a
			// destination file in that directory that shares the same filename
			// as the source.  Construct the new name.
			strcat(fixedDestName, "/");
			strcat(fixedDestName, srcFileStruct.name);

			entry = fileLookup(fixedDestName);
			if (entry)
			{
				// Remove the existing file
				status = fileDelete(entry);
				if (status < 0)
					goto out;
			}
		}
	}

	// Attempt to open the destination file for writing.
	status = kernelFileOpen(fixedDestName, (OPENMODE_WRITE | OPENMODE_CREATE |
		OPENMODE_TRUNCATE), &destFileStruct);
	if (status < 0)
		goto out;

	// Is there enough space in the destination filesystem for the copied
	// file?
	freeSpace = kernelFilesystemGetFreeBytes(destFileStruct.filesystem);
	if (srcFileStruct.size > freeSpace)
	{
		kernelError(kernel_error, "Not enough space (%llu < %u) in "
			"destination filesystem", freeSpace, srcFileStruct.size);
		status = ERR_NOFREE;
		goto out;
	}

	entry = (kernelFileEntry *) destFileStruct.handle;

	// Make sure the destination file's block size isn't zero (this would
	// lead to a divide-by-zero error at writing time)
	if (!destFileStruct.blockSize)
	{
		kernelError(kernel_error, "Destination file has zero blocksize");
		status = ERR_DIVIDEBYZERO;
		goto out;
	}

	status = fileCopy(&srcFileStruct, &destFileStruct);

	if (status >= 0)
	{
		// Set the size of the destination file so that it matches that of
		// the source file (as opposed to a multiple of the block size and
		// the number of blocks it consumes)
		kernelFileEntrySetSize((kernelFileEntry *) destFileStruct.handle,
			 srcFileStruct.size);
	}

out:
	kernelFree(fixedSrcName);
	kernelFree(fixedDestName);
	kernelFileClose(&srcFileStruct);
	kernelFileClose(&destFileStruct);
	return (status);
}


int kernelFileCopyRecursive(const char *srcPath, const char *destPath)
{
	// This is a function to copy directories recursively.  The source name
	// can be a regular file as well; it will just copy the single file.

	int status = 0;
	kernelFileEntry *srcEntry = NULL;
	kernelFileEntry *destEntry = NULL;
	char *tmpSrcName = NULL;
	char *tmpDestName = NULL;

	if (!initialized)
		return (status = ERR_NOTINITIALIZED);

	// Check params
	if (!srcPath || !destPath)
	{
		kernelError(kernel_error, "NULL parameter");
		return (status = ERR_NULLPARAMETER);
	}

	// Determine whether the source item exists, and if so whether it
	// is a directory.
	srcEntry = kernelFileLookup(srcPath);
	if (!srcEntry)
	{
		kernelError(kernel_error, "File to copy does not exist");
		return (status = ERR_NOSUCHENTRY);
	}

	// It exists.  Is it a directory?
	if (srcEntry->type == dirT)
	{
		// It's a directory, so we create the destination directory if it
		// doesn't already exist, then we loop through the entries in the
		// source directory.  If an entry is a file, copy it.  If it is a
		// directory, recurse.

		destEntry = kernelFileLookup(destPath);

		if (destEntry)
		{
			// If the destination directory exists, but has a different
			// filename than the source directory, that means we might need to
			// create the new destination directory inside it with the
			// original's name.
			if (strcmp((char *) destEntry->name, (char *) srcEntry->name))
			{
				tmpDestName = fixupPath(destPath);
				if (tmpDestName)
				{
					strcat(tmpDestName, "/");
					strcat(tmpDestName, (char *) srcEntry->name);

					destEntry = kernelFileLookup(tmpDestName);

					if (destEntry && (destEntry->type != dirT))
					{
						// Some non-directory item is sitting there using our
						// desired destination name, blocking us.  Try to
						// delete it.
						fileDelete(destEntry);
						destEntry = NULL;
					}

					if (!destEntry)
					{
						status = kernelFileMakeDir(tmpDestName);
						if (status < 0)
						{
							kernelFree(tmpDestName);
							return (status);
						}
					}

					status = kernelFileCopyRecursive(srcPath, tmpDestName);

					kernelFree(tmpDestName);

					return (status);
				}
			}
		}

		else
		{
			// Create the destination directory
			status = kernelFileMakeDir(destPath);
			if (status < 0)
				return (status);

			destEntry = kernelFileLookup(destPath);
			if (!destEntry)
				return (status = ERR_NOSUCHENTRY);
		}

		// Get the first file in the source directory
		srcEntry = srcEntry->contents;

		while (srcEntry)
		{
			if (strcmp((char *) srcEntry->name, ".") &&
				strcmp((char *) srcEntry->name, ".."))
			{
				// Add the file's name to the directory's name
				tmpSrcName = fixupPath(srcPath);
				if (tmpSrcName)
				{
					strcat(tmpSrcName, "/");
					strcat(tmpSrcName, (const char *) srcEntry->name);

					// Add the file's name to the destination file name
					tmpDestName = fixupPath(destPath);
					if (tmpDestName)
					{
						strcat(tmpDestName, "/");
						strcat(tmpDestName, (const char *) srcEntry->name);

						status = kernelFileCopyRecursive(tmpSrcName,
							tmpDestName);

						kernelFree(tmpSrcName);
						kernelFree(tmpDestName);

						if (status < 0)
							return (status);
					}
				}
			}

			srcEntry = srcEntry->nextEntry;
		}
	}
	else
	{
		// Just copy the file using the existing copy function
		return (kernelFileCopy(srcPath, destPath));
	}

	// Return success
	return (status = 0);
}


int kernelFileMove(const char *srcName, const char *destName)
{
	// This function is an externalized version of the fileMove function,
	// above.

	int status = 0;
	char *fixedSrcName = NULL;
	char *fixedDestName = NULL;
	char origName[MAX_NAME_LENGTH];
	char destDirName[MAX_PATH_LENGTH];
	kernelFileEntry *srcDir = NULL;
	kernelFileEntry *srcEntry = NULL;
	kernelFileEntry *destDir = NULL;
	kernelFileEntry *destEntry = NULL;

	if (!initialized)
		return (status = ERR_NOTINITIALIZED);

	// Check params
	if (!srcName || !destName)
	{
		kernelError(kernel_error, "NULL parameter");
		return (status = ERR_NULLPARAMETER);
	}

	// Fix up the source and destination path names
	fixedSrcName = fixupPath(srcName);
	fixedDestName = fixupPath(destName);
	if (!fixedSrcName || !fixedDestName)
	{
		status = ERR_NOSUCHENTRY;
		goto out;
	}

	// Now make sure that the requested source item exists.  Whether it is a
	// file or directory is unimportant.
	srcEntry = fileLookup(fixedSrcName);
	if (!srcEntry)
	{
		// The directory does not exist
		kernelError(kernel_error, "Source item %s does not exist",
			fixedSrcName);
		status = ERR_NOSUCHFILE;
		goto out;
	}

	// Save the source directory
	srcDir = srcEntry->parentDirectory;
	if (!srcDir)
	{
		kernelError(kernel_error, "Source item \"%s\" to move has a NULL "
			"parent directory!", fixedSrcName);
		status = ERR_NULLPARAMETER;
		goto out;
	}

	// Save the source file's original name, just in case we encounter
	// problems later
	strncpy(origName, (char *) srcEntry->name, MAX_NAME_LENGTH);

	// Check whether the destination file exists.  If it exists and it is
	// a file, we need to delete it first.
	destEntry = fileLookup(fixedDestName);

	if (destEntry)
	{
		// It already exists.  Is it a directory?
		if (destEntry->type == dirT)
		{
			// It's a directory, so what we really want to do is set our dest
			// dir to that.
			destDir = destEntry;

			// Append the source name and see if a file exists by that name
			strcat(fixedDestName, (char *) srcEntry->name);
			destEntry = fileLookup(fixedDestName);
		}

		if (destEntry)
		{
			// There already exists an item at that location, and it is not
			// a directory.  It needs to go away.  Really, we should keep it
			// until the other one is moved successfully, but...

			// Save the pointer to the parent directory
			destDir = destEntry->parentDirectory;

			// Set the dest file's name from the old one
			strcpy((char *) srcEntry->name, (char *) destEntry->name);

			status = fileDelete(destEntry);
			if (status < 0)
				goto out;
		}
	}
	else
	{
		// No such file exists.  We need to get the destination directory
		// from the destination path we were supplied.
		status = kernelFileSeparateLast(fixedDestName, destDirName,
			(char *) srcEntry->name);
		if (status < 0)
			goto out;

		destDir = kernelFileLookup(destDirName);
		if (!destDir)
		{
			kernelError(kernel_error, "Destination directory does not exist");
			status = ERR_NOSUCHDIR;
			goto out;
		}
	}

	// Place the file in its new directory
	status = fileMove(srcEntry, destDir);
	if (status < 0)
	{
		// We were unable to place it in the new directory.  Better at
		// least try to put it back where it belongs in its old directory,
		// with its old name.  Whether this succeeds or fails, we need to try
		strcpy((char *) srcEntry->name, origName);
		kernelFileInsertEntry(srcEntry, srcDir);
		goto out;
	}

	// Return success
	status = 0;

out:
	if (fixedSrcName)
		kernelFree(fixedSrcName);
	if (fixedDestName)
		kernelFree(fixedDestName);

	return (status);
}


int kernelFileTimestamp(const char *path)
{
	// This is mostly a wrapper function for the equivalent function in the
	// filesystem driver.  Sets the file or directory's "last modified" and
	// "last accessed" times.

	int status = 0;
	char *fileName = NULL;
	kernelFileEntry *entry = NULL;
	kernelDisk *fsDisk = NULL;
	kernelFilesystemDriver *driver = NULL;

	if (!initialized)
		return (status = ERR_NOTINITIALIZED);

	// Check params
	if (!path)
	{
		kernelError(kernel_error, "NULL parameter");
		return (status = ERR_NULLPARAMETER);
	}

	// Fix up the path name
	fileName = fixupPath(path);
	if (fileName)
	{
		// Now make sure that the requested file exists
		entry = kernelFileLookup(fileName);

		kernelFree(fileName);

		if (!entry)
		{
			kernelError(kernel_error, "File to timestamp does not exist");
			return (status = ERR_NOSUCHFILE);
		}
	}
	else
	{
		return (status = ERR_NOSUCHENTRY);
	}

	// Now we change the internal data fields of the file's data structure
	// to match the current date and time (Don't touch the creation date/time)

	// Set the file's "last modified" and "last accessed" times
	updateModifiedTime(entry);
	updateAccessedTime(entry);

	fsDisk = (kernelDisk *) entry->disk;
	if (!fsDisk)
	{
		kernelError(kernel_error, "NULL disk pointer");
		return (status = ERR_BADADDRESS);
	}

	// Not allowed in read-only file system
	if (fsDisk->filesystem.readOnly)
	{
		kernelError(kernel_error, "Filesystem is read-only");
		return (status = ERR_NOWRITE);
	}

	// Now allow the underlying filesystem driver to do anything that's
	// specific to that filesystem type.

	driver = fsDisk->filesystem.driver;

	// Call the filesystem driver function, if it exists
	if (driver->driverTimestamp)
	{
		status = driver->driverTimestamp(entry);
		if (status < 0)
			return (status);
	}

	// Write the directory
	if (driver->driverWriteDir && entry->parentDirectory)
	{
		status = driver->driverWriteDir(entry->parentDirectory);
		if (status < 0)
			return (status);
	}

	return (status = 0);
}


int kernelFileSetSize(file *fileStruct, unsigned newSize)
{
	// This function is an externally-accessible wrapper for our
	// kernelFileEntrySetSize function.

	int status = 0;

	if (!initialized)
		return (status = ERR_NOTINITIALIZED);

	// Check params
	if (!fileStruct)
	{
		kernelError(kernel_error, "NULL parameter");
		return (status = ERR_NULLPARAMETER);
	}

	if (!fileStruct->handle)
	{
		kernelError(kernel_error, "NULL file handle for set size.  Not opened "
			"first?");
		return (status = ERR_NULLPARAMETER);
	}

	// Has the file been opened properly for writing?
	if (!(fileStruct->openMode & OPENMODE_WRITE))
	{
		kernelError(kernel_error, "File %s has not been opened for writing %x",
			fileStruct->name, fileStruct->openMode);
		return (status = ERR_INVALID);
	}

	status = kernelFileEntrySetSize((kernelFileEntry *) fileStruct->handle,
		newSize);
	if (status < 0)
		return (status);

	// Make sure the file structure is up to date after the call
	fileEntry2File(fileStruct->handle, fileStruct);

	// Return success
	return (status = 0);
}


int kernelFileGetTempName(char *buffer, unsigned bufferLen)
{
	int status = 0;

	if (!initialized)
		return (status = ERR_NOTINITIALIZED);

	// Check params
	if (!buffer || !bufferLen)
	{
		kernelError(kernel_error, "NULL parameter");
		return (status = ERR_NULLPARAMETER);
	}

	while (1)
	{
		// Construct the file name
		snprintf(buffer, bufferLen, PATH_TEMP "/%03d-%08x.tmp",
			kernelCurrentProcess->processId, kernelRandomUnformatted());

		// Make sure it doesn't already exist
		if (!fileLookup(buffer))
			break;
	}

	return (status = 0);
}


int kernelFileGetTemp(file *tmpFile)
{
	// This will create and open a temporary file in write mode.

	int status = 0;
	char *fileName = NULL;

	if (!initialized)
		return (status = ERR_NOTINITIALIZED);

	// Check params
	if (!tmpFile)
	{
		kernelError(kernel_error, "NULL parameter");
		return (status = ERR_NULLPARAMETER);
	}

	if (!rootEntry || ((kernelDisk *) rootEntry->disk)->filesystem.readOnly)
	{
		kernelError(kernel_error, "Filesystem is read-only");
		return (status = ERR_NOWRITE);
	}

	fileName = kernelMalloc(MAX_PATH_NAME_LENGTH);
	if (!fileName)
		return (status = ERR_MEMORY);

	status = kernelFileGetTempName(fileName, MAX_PATH_NAME_LENGTH);
	if (status < 0)
		return (status);

	// Create and open it for reading/writing
	status = kernelFileOpen(fileName, (OPENMODE_CREATE | OPENMODE_TRUNCATE |
		OPENMODE_READWRITE), tmpFile);

	kernelFree(fileName);

	return (status);
}


int kernelFileGetFullPath(file *fileStruct, char *buffer, int buffLen)
{
	// Returns the full path name of a file.  This is an exported wrapper for
	// the kernelFileGetFullName function.

	int status = 0;

	// Check params
	if (!fileStruct || !buffer)
	{
		kernelError(kernel_error, "NULL parameter");
		return (status = ERR_NULLPARAMETER);
	}

	if (!fileStruct->handle)
	{
		kernelError(kernel_error, "NULL file handle");
		return (status = ERR_NULLPARAMETER);
	}

	status = kernelFileGetFullName(fileStruct->handle, buffer, buffLen);

	return (status);
}

