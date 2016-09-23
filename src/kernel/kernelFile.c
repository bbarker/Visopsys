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
//  kernelFile.c
//

// This file contains the routines designed for managing the file
// system tree.

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

#define ISSEPARATOR(foo) ((foo == '/') || (foo == '\\'))

// The root directory
static kernelFileEntry *rootDirectory = NULL;

// Memory for free file entries
static kernelFileEntry *freeFileEntries = NULL;
static unsigned numFreeEntries = 0;

static int initialized = 0;


static int allocateFileEntries(void)
{
	// This function is used to allocate more memory for the freeFileEntries
	// list.

	int status = 0;
	kernelFileEntry *newFileEntries = NULL;
	int count;

	// Allocate memory for file entries
	newFileEntries = kernelMalloc(sizeof(kernelFileEntry) * MAX_BUFFERED_FILES);
	if (!newFileEntries)
		return (status = ERR_MEMORY);

	// Initialize the new kernelFileEntry structures.

	for (count = 0; count < (MAX_BUFFERED_FILES - 1); count ++)
		newFileEntries[count].nextEntry = &newFileEntries[count + 1];

	// The free file entries are the new memory
	freeFileEntries = newFileEntries;

	// Add the number of new file entries
	numFreeEntries = MAX_BUFFERED_FILES;

	return (status = 0);
}


static int isLeafDir(kernelFileEntry *dir)
{
	// This function will determine whether the supplied directory entry
	// is a 'leaf' directory.  A leaf directory is defined as a directory
	// which contains no other subdirectories with buffered contents.
	// Returns 1 if the directory is a leaf, 0 otherwise.

	int status = 0;
	kernelFileEntry *listItemPointer = NULL;

	listItemPointer = dir->contents;

	while (listItemPointer)
	{
		if ((listItemPointer->type == dirT) &&
			(listItemPointer->contents))
		{
			break;
		}
		else
		{
			listItemPointer = listItemPointer->nextEntry;
		}
	}

	if (!listItemPointer)
		// It is a leaf directory
		return (status = 1);
	else
		// It's not a leaf directory
		return (status = 0);
}


static int unbufferDirectory(kernelFileEntry *dir)
{
	// This function is internal, and is called when the tree of file
	// and directory entries becomes too full.  It will "un-buffer" all of
	// one directory entry's contents (sub-entries) from memory.
	// The decision about which entry to un-buffer is based on LRU (least
	// recently used).  Returns 0 on success, negative otherwise.

	int status = 0;
	kernelFileEntry *listItemPointer = NULL;
	kernelFileEntry *nextListItem = NULL;

	// We should have a directory that is safe to unbuffer.  We can return
	// this directory's contents (sub-entries) to the list of free entries.

	listItemPointer = dir->contents;

	// Step through the list of directory entries
	while (listItemPointer)
	{
		nextListItem = listItemPointer->nextEntry;
		kernelFileReleaseEntry(listItemPointer);
		listItemPointer = nextListItem;
	}

	dir->contents = NULL;

	// This directory now looks to the system as if it had not yet been read
	// from disk.

	// Return success
	return (status = 0);
}


static void fileEntry2File(kernelFileEntry *fileEntry, file *theFile)
{
	// This little function will copy the applicable parts from a file
	// entry structure to an external 'file' structure.

	strncpy(theFile->name, (char *) fileEntry->name, MAX_NAME_LENGTH);
	theFile->name[MAX_NAME_LENGTH - 1] = '\0';
	theFile->handle = (void *) fileEntry;
	theFile->type = fileEntry->type;

	strncpy(theFile->filesystem, (char *) fileEntry->disk->filesystem.mountPoint,
			MAX_PATH_LENGTH);
	theFile->filesystem[MAX_PATH_LENGTH - 1] = '\0';

	kernelRtcDateTime2Tm(fileEntry->creationDate, fileEntry->creationTime,
		&theFile->created);
	kernelRtcDateTime2Tm(fileEntry->accessedDate, fileEntry->accessedTime,
		&theFile->accessed);
	kernelRtcDateTime2Tm(fileEntry->modifiedDate, fileEntry->modifiedTime,
		&theFile->modified);

	theFile->size = fileEntry->size;
	theFile->blocks = fileEntry->blocks;
	theFile->blockSize = ((kernelDisk *) fileEntry->disk)->filesystem.blockSize;

	return;
}


static int dirIsEmpty(kernelFileEntry *theDir)
{
	// This function is internal, and is used to determine whether there
	// are any files in a directory (aside from the non-entries '.' and
	// '..'.  Returns 1 if the directory is empty.  Returns 0 if the
	// directory contains entries.  Returns negative on error.

	kernelFileEntry *listItemPointer = NULL;
	int isEmpty = 0;

	// Make sure the directory is really a directory
	if (theDir->type != dirT)
	{
		kernelError(kernel_error, "Directory to check is not a directory");
		return (isEmpty = ERR_NOTADIR);
	}

	// Assign the first item in the directory to our iterator pointer
	listItemPointer = theDir->contents;

	while (listItemPointer)
	{
		if (!strcmp((char *) listItemPointer->name, ".") ||
			!strcmp((char *) listItemPointer->name, ".."))
		{
			listItemPointer = listItemPointer->nextEntry;
		}
		else
		{
			return (isEmpty = 0);
		}
	}

	return (isEmpty = 1);
}


static int isDescendent(kernelFileEntry *leaf, kernelFileEntry *node)
{
	// This function is internal, and can be used to determine whether
	// the "leaf" entry is a descendent of the "node" entry.  This is
	// important to check during move operations so that directories cannot
	// be "put inside themselves" (therefore disconnecting them from the
	// filesystem entirely).  Returns 1 if true (is a descendent), 0 if
	// false, and negative on error

	kernelFileEntry *listItemPointer = NULL;
	int status = 0;

	// Check params
	if (!node || !leaf)
		return (status = ERR_NULLPARAMETER);

	// If "leaf" and "node" are the same, return true
	if (node == leaf)
		return (status = 1);

	// If "node" is not a directory, then it obviously has no descendents.
	// Return false
	if (node->type != dirT)
	{
		kernelError(kernel_error, "Node is not a directory");
		return (status = 0);
	}

	listItemPointer = leaf;

	// Do a loop to step upward from the "leaf" through its respective
	// ancestor directories.  If the parent is NULL at any point, we return
	// false.  If the parent ever equals "node", return true.
	while (listItemPointer)
	{
		if (listItemPointer->parentDirectory == listItemPointer)
			break;

		listItemPointer = listItemPointer->parentDirectory;

		if (listItemPointer == node)
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
	return;
}


static inline void updateModifiedTime(kernelFileEntry *entry)
{
	// This will update the modified date and time of a file entry.

	entry->modifiedDate = kernelRtcPackedDate();
	entry->modifiedTime = kernelRtcPackedTime();
	entry->lastAccess = kernelCpuTimestamp();
	return;
}


static inline void updateAccessedTime(kernelFileEntry *entry)
{
	// This will update the accessed date and time of a file entry.

	entry->accessedDate = kernelRtcPackedDate();
	entry->accessedTime = kernelRtcPackedTime();
	entry->lastAccess = kernelCpuTimestamp();
	return;
}


static void updateAllTimes(kernelFileEntry *entry)
{
	// This will update all the dates and times of a file entry.

	updateCreationTime(entry);
	updateModifiedTime(entry);
	updateAccessedTime(entry);
	return;
}


static void buildFilenameRecursive(kernelFileEntry *theFile, char *buffer,
	int buffLen)
{
	// Head-recurse back to the root of the filesystem, constructing the full
	// pathname of the file

	int isRoot = 0;

	if (!strcmp((char *) theFile->name, "/"))
		isRoot = 1;

	if ((theFile->parentDirectory) && !isRoot)
		buildFilenameRecursive(theFile->parentDirectory, buffer, buffLen);

	if (!isRoot && (buffer[strlen(buffer) - 1] != '/'))
		strncat(buffer, "/", (buffLen - (strlen(buffer) + 1)));

	strncat(buffer, (char *) theFile->name, (buffLen - (strlen(buffer) + 1)));
}


static char *fixupPath(const char *originalPath)
{
	// This function will take a path string, possibly add the CWD as a prefix,
	// remove any unneccessary characters, and resolve any '.' or '..'
	// components to their real targets.  It allocates memory for the result,
	// so it is the responsibility of the caller to free it.

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
				// It's dotdot.  We must skip backward in the new path
				// until the next-but-one separator.  If we're at the
				// root level, simply copy (it will probably fail later
				// as a 'no such file')
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
	// This function is called by other subroutines to resolve pathnames and
	// files to kernelFileEntry structures.  On success, it returns
	// the kernelFileEntry of the deepest item of the path it was given.  The
	// target path can resolve either to a subdirectory or a file.

	int status = 0;
	const char *itemName = NULL;
	int itemLength = 0;
	int caseInsensitive = 0;
	int found = 0;
	kernelDisk *theDisk = NULL;
	kernelFilesystemDriver *theDriver = NULL;
	kernelFileEntry *listItem = NULL;
	int count;

	// We step through the directory structure, looking for the appropriate
	// directories based on the path we were given.

	// Start with the root directory
	listItem = rootDirectory;

	if (!strcmp(fixedPath, "/"))
		// The root directory is being requested
		return (listItem);

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
			return (listItem = NULL);

		// Find the first item in the "current" directory
		if (listItem->contents)
			listItem = listItem->contents;
		else
			// Nothing in the directory
			return (listItem = NULL);

		caseInsensitive =
			((kernelDisk *) listItem->disk)->filesystem.caseInsensitive;

		found = 0;

		while (listItem)
		{
			// Update the access time on this directory
			listItem->lastAccess = kernelCpuTimestamp();

			if ((int) strlen((char *) listItem->name) == itemLength)
			{
				// First, try a case-sensitive comparison, whether or not the
				// filesystem is case-sensitive.  If that fails and the
				// filesystem is case-insensitive, try that kind of comparison
				// also.
				if (!strncmp((char *) listItem->name, itemName, itemLength) ||
					(caseInsensitive && !strncasecmp((char *) listItem->name,
						itemName, itemLength)))
				{
					// Found it.
					found = 1;
					break;
				}
			}

			// Move to the next Item
			listItem = listItem->nextEntry;
		}

		if (found)
		{
			// If this is a link, use the target of the link instead
			if (listItem->type == linkT)
			{
				listItem = kernelFileResolveLink(listItem);
				if (!listItem)
					// Unresolved link.
					return (listItem = NULL);
			}

			// Determine whether the requested item is really a directory, and
			// if so, whether the directory's files have been read
			if ((listItem->type == dirT) && !listItem->contents)
			{
				// We have to read this directory from the disk.  Allocate a new
				// directory buffer based on the number of bytes per sector and
				// the number of sectors used by the directory

				// Get the filesystem based on the filesystem number in the
				// file entry structure
				theDisk = listItem->disk;
				if (!theDisk)
				{
					kernelError(kernel_error, "Entry has a NULL disk pointer");
					return (listItem = NULL);
				}

				theDriver = theDisk->filesystem.driver;

				// Increase the open count on the directory's entry while we're
				// reading it.  This will prevent the filesystem manager from
				// trying to unbuffer it while we're working
				listItem->openCount++;

				// Lastly, we can call our target function
				if (theDriver->driverReadDir)
					status = theDriver->driverReadDir(listItem);

				listItem->openCount--;

				if (status < 0)
					return (listItem = NULL);
			}

			if (itemName[itemLength] == '\0')
			{
				listItem->lastAccess = kernelCpuTimestamp();
				return (listItem);
			}
		}
		else
		{
			// Not found
			return (listItem = NULL);
		}

		// Do the next item in the path
		itemName += (itemLength + 1);
	}
}


static int fileCreate(const char *path)
{
	// This is internal, and gets called by the open() routine when the
	// file in question needs to be created.

	int status = 0;
	char prefix[MAX_PATH_LENGTH];
	char name[MAX_NAME_LENGTH];
	kernelDisk *theDisk = NULL;
	kernelFilesystemDriver *theDriver = NULL;
	kernelFileEntry *directory = NULL;
	kernelFileEntry *createItem = NULL;

	// Now make sure that the requested file does NOT exist
	createItem = fileLookup(path);
	if (createItem)
	{
		kernelError(kernel_error, "File to create already exists");
		return (status = ERR_ALREADY);
	}

	// We have to find the directory where the user wants to create the
	// file.  That's all but the last part of the "fileName" argument to
	// you and I.  We have to call this function to separate the two parts.
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
	directory = fileLookup(prefix);
	if (!directory)
	{
		// The directory does not exist
		kernelError(kernel_error, "Parent directory (%s) of \"%s\" does not "
			"exist", prefix, name);
		return (status = ERR_NOSUCHDIR);
	}

	// OK, the directory exists.  Get the filesystem of the parent directory.
	theDisk = (kernelDisk *) directory->disk;
	if (!theDisk)
	{
		kernelError(kernel_error, "Unable to determine target disk");
		return (status = ERR_BADDATA);
	}

	// Not allowed in read-only file system
	if (theDisk->filesystem.readOnly)
	{
		kernelError(kernel_error, "Filesystem is read-only");
		return (status = ERR_NOWRITE);
	}

	theDriver = theDisk->filesystem.driver;

	// We can create the file in the directory.
	// Get a free file entry structure.
	createItem = kernelFileNewEntry(theDisk);
	if (!createItem)
		return (status = ERR_NOFREE);

	// Set up the appropriate data items in the new entry that aren't done
	// by default in the kernelFileNewEntry() function
	strncpy((char *) createItem->name, name, MAX_NAME_LENGTH);
	((char *) createItem->name)[MAX_NAME_LENGTH - 1] = '\0';
	createItem->type = fileT;

	// Add the file to the directory
	status = kernelFileInsertEntry(createItem, directory);
	if (status < 0)
	{
		kernelFileReleaseEntry(createItem->nextEntry);
		return (status);
	}

	// Check the filesystem driver function for creating files.  If it
	// exists, call it.
	if (theDriver->driverCreateFile)
	{
		status = theDriver->driverCreateFile(createItem);
		if (status < 0)
			return (status);
	}

	// Update the timestamps on the parent directory
	updateModifiedTime(directory);
	updateAccessedTime(directory);

	// Update the directory
	if (theDriver->driverWriteDir)
	{
		status = theDriver->driverWriteDir(directory);
		if (status < 0)
			return (status);
	}

	// Return success
	return (status = 0);
}


static int fileOpen(kernelFileEntry *openItem, int openMode)
{
	// This is mostly a wrapper function for the equivalent function
	// in the requested filesystem's own driver.  It takes nearly-identical
	// arguments and returns the same status as the driver function.

	int status = 0;
	kernelDisk *theDisk = NULL;
	kernelFilesystemDriver *theDriver = NULL;

	// Make sure the item is really a file, and not a directory or anything else
	if (openItem->type != fileT)
	{
		kernelError(kernel_error, "Item to open (%s) is not a file",
			openItem->name);
		return (status = ERR_NOTAFILE);
	}

	// Get the filesystem that the file belongs to
	theDisk = (kernelDisk *) openItem->disk;
	if (!theDisk)
	{
		kernelError(kernel_error, "NULL disk pointer");
		return (status = ERR_BADADDRESS);
	}

	// Not allowed in read-only file system
	if ((openMode & OPENMODE_WRITE) && theDisk->filesystem.readOnly)
	{
		kernelError(kernel_error, "Filesystem is read-only");
		return (status = ERR_NOWRITE);
	}

	theDriver = theDisk->filesystem.driver;

	// There are extra things we need to do if this will be a write operation

	if (openMode & OPENMODE_WRITE)
	{
		// Are we supposed to truncate the file first?
		if (openMode & OPENMODE_TRUNCATE)
		{
			// Call the filesystem driver and ask it to delete the file.
			// Then, we ask it to create the file again.

			// Check the driver functions we want to use
			if (!theDriver->driverDeleteFile ||
				!theDriver->driverCreateFile)
			{
				kernelError(kernel_error, "The requested filesystem operation "
					"is not supported");
				return (status = ERR_NOSUCHFUNCTION);
			}

			status = theDriver->driverDeleteFile(openItem);
			if (status < 0)
				return (status);

			status = theDriver->driverCreateFile(openItem);
			if (status < 0)
				return (status);
		}

		// Put a write lock on the file
		status = kernelLockGet(&openItem->lock);
		if (status < 0)
			return (status);

		// Update the modified dates/times on the file
		updateModifiedTime(openItem);
	}

	// Increment the open count on the file
	openItem->openCount += 1;

	// Update the access times/dates on the file
	updateAccessedTime(openItem);

	// Return success
	return (status = 0);
}


static int fileDelete(kernelFileEntry *theFile)
{
	// This is *somewhat* a wrapper function for the underlying function
	// in the filesystem's own driver, except it does do some general
	// housekeeping as well.

	int status = 0;
	kernelFileEntry *parentDir = NULL;
	kernelDisk *theDisk = NULL;
	kernelFilesystemDriver *theDriver = NULL;

	// Record the parent directory before we nix the file
	parentDir = theFile->parentDirectory;

	// Make sure the item is really a file, and not a directory.  We
	// have to do different things for removing directory
	if (theFile->type != fileT)
	{
		kernelError(kernel_error, "Item to delete is not a file");
		return (status = ERR_NOTAFILE);
	}

	// Figure out which filesystem we're using
	theDisk = (kernelDisk *) theFile->disk;
	if (!theDisk)
	{
		kernelError(kernel_error, "NULL disk pointer");
		return (status = ERR_BADADDRESS);
	}

	// Not allowed in read-only file system
	if (theDisk->filesystem.readOnly)
	{
		kernelError(kernel_error, "Filesystem is read-only");
		return (status = ERR_NOWRITE);
	}

	theDriver = theDisk->filesystem.driver;

	// If the filesystem driver has a 'delete' function, call it.
	if (theDriver->driverDeleteFile)
		status = theDriver->driverDeleteFile(theFile);
	if (status < 0)
		return (status);

	// Remove the entry for this file from its parent directory
	status = kernelFileRemoveEntry(theFile);
	if (status < 0)
		return (status);

	// Deallocate the data structure
	kernelFileReleaseEntry(theFile);

	// Update the times on the directory
	updateModifiedTime(parentDir);
	updateAccessedTime(parentDir);

	// Update the directory
	if (theDriver->driverWriteDir)
	{
		status = theDriver->driverWriteDir(parentDir);
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
	kernelDisk *theDisk = NULL;
	kernelFilesystemDriver *theDriver = NULL;
	kernelFileEntry *parent = NULL;
	kernelFileEntry *newDir = NULL;

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

	// Make sure "name" isn't empty.  This could happen if there WAS no
	// last item in the path to separate (like 'mkdir /', which would of
	// course be invalid)
	if (!name[0])
	{
		// Basically, we have been given an invalid directory name to create
		kernelError(kernel_error, "Path of directory to create is invalid");
		status = ERR_NOSUCHFILE;
		goto out;
	}

	// Now make sure that the requested parent directory exists
	parent = fileLookup(prefix);
	if (!parent)
	{
		// The directory does not exist
		kernelError(kernel_error, "Parent directory does not exist");
		status = ERR_NOSUCHDIR;
		goto out;
	}

	// Make sure it's a directory
	if (parent->type != dirT)
	{
		kernelError(kernel_error, "Parent directory is not a directory");
		status = ERR_NOSUCHDIR;
		goto out;
	}

	// Figure out which filesystem we're using
	theDisk = (kernelDisk *) parent->disk;
	if (!theDisk)
	{
		kernelError(kernel_error, "NULL disk pointer");
		status = ERR_BADADDRESS;
		goto out;
	}

	// Not allowed in read-only file system
	if (theDisk->filesystem.readOnly)
	{
		kernelError(kernel_error, "Filesystem is read-only");
		status = ERR_NOWRITE;
		goto out;
	}

	theDriver = theDisk->filesystem.driver;

	// Allocate a new file entry
	newDir = kernelFileNewEntry(theDisk);
	if (!newDir)
	{
		status = ERR_NOFREE;
		goto out;
	}

	// Now, set some fields for our new item

	strncpy((char *) newDir->name, name, MAX_NAME_LENGTH);
	((char *) newDir->name)[MAX_NAME_LENGTH - 1] = '\0';

	newDir->type = dirT;

	// Set the creation, modification times, etc.
	updateCreationTime(newDir);
	updateModifiedTime(newDir);
	updateAccessedTime(newDir);

	// Now add the new directory to its parent
	status = kernelFileInsertEntry(newDir, parent);
	if (status < 0)
	{
		// We couldn't add the directory for whatever reason.  In that case
		// we must attempt to deallocate the cluster we allocated, and return
		// the three files we created to the free list
		kernelFileReleaseEntry(newDir);
		goto out;
	}

	// Create the '.' and '..' entries inside the directory
	kernelFileMakeDotDirs(parent, newDir);

	// If the filesystem driver has a 'make dir' routine, call it.
	if (theDriver->driverMakeDir)
	{
		status = theDriver->driverMakeDir(newDir);
		if (status < 0)
			goto out;
	}

	// Update the timestamps on the parent directory
	updateModifiedTime(parent);
	updateAccessedTime(parent);

	// Write both directories
	if (theDriver->driverWriteDir)
	{
		status = theDriver->driverWriteDir(newDir);
		if (status < 0)
			goto out;

		status = theDriver->driverWriteDir(parent);
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


static int fileRemoveDir(kernelFileEntry *theDir)
{
	// Remove an empty directory.

	int status = 0;
	kernelFileEntry *parentDir = NULL;
	kernelDisk *theDisk = NULL;
	kernelFilesystemDriver *theDriver = NULL;

	// Ok, do NOT remove the root directory
	if (theDir == rootDirectory)
	{
		kernelError(kernel_error, "Cannot remove the root directory under any "
			"circumstances");
		return (status = ERR_NODELETE);
	}

	// Make sure the item to delete is really a directory
	if (theDir->type != dirT)
	{
		kernelError(kernel_error, "Item to delete is not a directory");
		return (status = ERR_NOTADIR);
	}

	// Make sure the directory to delete is empty
	if (!dirIsEmpty(theDir))
	{
		kernelError(kernel_error, "Directory to delete is not empty");
		return (status = ERR_NOTEMPTY);
	}

	// Record the parent directory
	parentDir = theDir->parentDirectory;

	// Figure out which filesystem we're using
	theDisk = (kernelDisk *) theDir->disk;
	if (!theDisk)
	{
		kernelError(kernel_error, "NULL disk pointer");
		return (status = ERR_BADADDRESS);
	}

	// Not allowed in read-only file system
	if (theDisk->filesystem.readOnly)
	{
		kernelError(kernel_error, "Filesystem is read-only");
		return (status = ERR_NOWRITE);
	}

	theDriver = theDisk->filesystem.driver;

	// If the filesystem driver has a 'remove dir' function, call it.
	if (theDriver->driverRemoveDir)
	{
		status = theDriver->driverRemoveDir(theDir);
		if (status < 0)
			return (status);
	}

	// Now remove the '.' and '..' entries from the directory
	while (theDir->contents)
	{
		kernelFileEntry *dotEntry = theDir->contents;

		status = kernelFileRemoveEntry(dotEntry);
		if (status < 0)
			return (status);

		kernelFileReleaseEntry(dotEntry);
	}

	// Now remove the directory from its parent
	status = kernelFileRemoveEntry(theDir);
	if (status < 0)
		return (status);

	kernelFileReleaseEntry(theDir);

	// Update the times on the parent directory
	updateModifiedTime(parentDir);
	updateAccessedTime(parentDir);

	// Write the directory
	if (theDriver->driverWriteDir)
	{
		status = theDriver->driverWriteDir(parentDir);
		if (status < 0)
			return (status);
	}

	// Return success
	return (status = 0);
}


static int fileDeleteRecursive(kernelFileEntry *delEntry)
{
	// Calls the fileDelete and fileRemoveDir functions, recursive-wise.

	int status = 0;
	kernelFileEntry *currEntry = NULL;
	kernelFileEntry *nextEntry = NULL;

	if (delEntry->type == dirT)
	{
		// Get the first file in the source directory
		currEntry = delEntry->contents;

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

		status = fileRemoveDir(delEntry);
	}
	else
	{
		status = fileDelete(delEntry);
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


static int fileMove(kernelFileEntry *sourceItem, kernelFileEntry *destDir)
{
	// This function is used to move or rename one file or directory to
	// another location or name.  Of course, this involves moving no actual
	// data, but rather moving the references to the data from one location
	// to another.  If the target location is the name of a directory, the
	// specified item is moved to that directory and will have the same
	// name as before.  If the target location is a file name, the item
	// will be moved and renamed.  Returns 0 on success, negtive otherwise.

	int status = 0;
	kernelDisk *theDisk = NULL;
	kernelFilesystemDriver *theDriver = NULL;
	kernelFileEntry *sourceDir = NULL;

	// Save the source directory
	sourceDir = sourceItem->parentDirectory;

	// Get the filesystem we're using
	theDisk = (kernelDisk *) sourceItem->disk;
	if (!theDisk)
	{
		kernelError(kernel_error, "%s has a NULL source disk pointer",
			sourceItem->name);
		return (status = ERR_BADADDRESS);
	}

	theDriver = theDisk->filesystem.driver;

	// Not allowed in read-only file system
	if (theDisk->filesystem.readOnly)
	{
		kernelError(kernel_error, "%s filesystem is read-only",
			theDisk->filesystem.mountPoint);
		return (status = ERR_NOWRITE);
	}

	// Now check the filesystem of the destination directory.  Here's the
	// trick: moves can only occur within a single filesystem.
	if ((kernelDisk *) destDir->disk != theDisk)
	{
		kernelError(kernel_error, "Can only move items within a single "
			"filesystem");
		return (status = ERR_INVALID);
	}

	// If the source item is a directory, make sure that the destination
	// directory is not a descendent of the source.  Moving a directory tree
	// into one of its own subtrees is paradoxical.  Also make sure that the
	// directory being moved is not the same as the destination directory
	if (sourceItem->type == dirT)
	{
		if (isDescendent(destDir, sourceItem))
		{
			// The destination is a descendant of the source
			kernelError(kernel_error, "Cannot move directory into one of its "
				"own subdirectories");
			return (status = ERR_PARADOX);
		}
	}

	// Remove the source item from its old directory
	status = kernelFileRemoveEntry(sourceItem);
	if (status < 0)
		return (status);

	// Place the file in its new directory
	status = kernelFileInsertEntry(sourceItem, destDir);
	if (status < 0)
	{
		// We were unable to place it in the new directory.  Better at
		// least try to put it back where it belongs in its old directory,
		// with its old name.  Whether this succeeds or fails, we need to try
		kernelFileInsertEntry(sourceItem, sourceDir);
		return (status);
	}

	// Update some of the data items in the file entries
	updateAccessedTime(sourceItem);
	updateModifiedTime(sourceDir);
	updateAccessedTime(sourceDir);
	updateModifiedTime(destDir);
	updateAccessedTime(destDir);

	// If the filesystem driver has a driverFileMoved function, call it
	if (theDriver->driverFileMoved)
	{
		status = theDriver->driverFileMoved(sourceItem);
		if (status < 0)
			return (status);
	}

	// Write both directories
	if (theDriver->driverWriteDir)
	{
		status = theDriver->driverWriteDir(destDir);
		if (status < 0)
			return (status);

		status = theDriver->driverWriteDir(sourceDir);
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


int kernelFileSetRoot(kernelFileEntry *rootDir)
{
	// This just sets the root filesystem pointer

	int status = 0;

	// Make sure 'rootDir' isn't NULL
	if (!rootDir)
	{
		kernelError(kernel_error, "NULL parameter");
		return (status = ERR_NULLPARAMETER);
	}

	// Assign it to the variable
	rootDirectory = rootDir;

	initialized = 1;

	// Return success
	return (status = 0);
}


kernelFileEntry *kernelFileNewEntry(kernelDisk *theDisk)
{
	// This function will find an unused file entry and return it to the
	// calling function.

	int status = 0;
	kernelFileEntry *theEntry = NULL;
	kernelFilesystemDriver *theDriver = NULL;

	// Check params
	if (!theDisk)
	{
		kernelError(kernel_error, "NULL parameter");
		return (theEntry = NULL);
	}

	// Make sure there is a free file entry available
	if (!numFreeEntries)
	{
		status = allocateFileEntries();
		if (status < 0)
			return (theEntry = NULL);
	}

	// Get a free file entry.  Grab it from the first spot
	theEntry = freeFileEntries;
	freeFileEntries = theEntry->nextEntry;
	numFreeEntries -= 1;

	// Clear it
	memset((void *) theEntry, 0, sizeof(kernelFileEntry));

	// Set some default time/date values
	updateAllTimes(theEntry);

	// We need to call the appropriate filesystem so that it can attach
	// its private data structure to the file

	// Make note of the assigned disk in the entry
	theEntry->disk = theDisk;

	// Get a pointer to the filesystem driver
	theDriver = theDisk->filesystem.driver;

	// OK, we just have to call the filesystem driver function
	if (theDriver->driverNewEntry)
	{
		status = theDriver->driverNewEntry(theEntry);
		if (status < 0)
			return (theEntry = NULL);
	}

	return (theEntry);
}


void kernelFileReleaseEntry(kernelFileEntry *theEntry)
{
	// This function takes a file entry to release, and puts it back
	// in the pool of free file entries.

	kernelDisk *theDisk = NULL;
	kernelFilesystemDriver *theDriver = NULL;

	// Check params
	if (!theEntry)
	{
		kernelError(kernel_error, "NULL parameter");
		return;
	}

	// If there's some private filesystem data attached to the file entry
	// structure, we will need to release it.
	if (theEntry->driverData)
	{
		// Get the filesystem pointer from the file entry structure
		theDisk = theEntry->disk;
		if (theDisk)
		{
			// Get a pointer to the filesystem driver
			theDriver = theDisk->filesystem.driver;

			// OK, we just have to check on the filesystem driver function
			if (theDriver->driverInactiveEntry)
				theDriver->driverInactiveEntry(theEntry);
		}
	}

	// Clear it out
	memset((void *) theEntry, 0, sizeof(kernelFileEntry));

	// Put the entry back into the pool of free entries.
	theEntry->nextEntry = freeFileEntries;
	freeFileEntries = theEntry;
	numFreeEntries += 1;

	// Cool.
	return;
}


int kernelFileInsertEntry(kernelFileEntry *theFile, kernelFileEntry *directory)
{
	// This function is used to add a new entry to a target directory.  The
	// function will verify that the file does not already exist.

	int status = 0;
	int numberFiles = 0;
	kernelFileEntry *listItemPointer = NULL;
	kernelFileEntry *previousItemPointer = NULL;

	// Check params
	if (!directory || !theFile)
	{
		kernelError(kernel_error, "NULL parameter");
		return (status = ERR_NULLPARAMETER);
	}

	// Make sure directory is really a directory
	if (directory->type != dirT)
	{
		kernelError(kernel_error, "Entry in which to insert file is not "
			"a directory");
		return (status = ERR_NOTADIR);
	}

	// Make sure the entry does not already exist
	listItemPointer = directory->contents;
	while (listItemPointer)
	{
		// We do a case-sensitive comparison here, regardless of whether
		// the filesystem driver cares about case.  We are worried about
		// exact matches.
		if (!strcmp((char *) listItemPointer->name, (char *) theFile->name))
		{
			kernelError(kernel_error, "A file by the name \"%s\" already exists "
				"in the directory \"%s\"", theFile->name,
				directory->name);
			return (status = ERR_ALREADY);
		}

		listItemPointer = listItemPointer->nextEntry;
	}

	// Make sure that the number of entries in this directory has not
	// exceeded (and is not about to exceed) either the maximum number of
	// legal directory entries

	numberFiles = kernelFileCountDirEntries(directory);

	if (numberFiles >= MAX_DIRECTORY_ENTRIES)
	{
		// Make an error that the directory is full
		kernelError(kernel_error, "The directory is full; can't create new "
			"entry");
		return (status = ERR_NOCREATE);
	}

	// Set the parent directory
	theFile->parentDirectory = directory;

	// Set the "list item" pointer to the start of the file chain
	listItemPointer = directory->contents;

	// For each file in the file chain, we loop until we find a
	// filename that is alphabetically greater than our new entry.
	// At that point, we insert our new entry in the previous spot.

	while (listItemPointer)
	{
		// Make sure we don't scan the '.' or '..' entries, if there are any
		if (!strcmp((char *) listItemPointer->name, ".") ||
			!strcmp((char *) listItemPointer->name, ".."))
		{
			// Move to the next filename.
			previousItemPointer = listItemPointer;
			listItemPointer = listItemPointer->nextEntry;
			continue;
		}

		if (strcmp((char *) listItemPointer->name, (char *) theFile->name) < 0)
		{
			// This item is alphabetically "less than" the one we're
			// inserting.  Move to the next filename.
			previousItemPointer = listItemPointer;
			listItemPointer = listItemPointer->nextEntry;
		}

		else
			break;
	}

	// When we fall through to this point, there are a few possible
	// scenarios:
	// 1. the filenames we now have (of the new entry and an existing
	//	entry in the list) are identical
	// 2. The list item is alphabetically "after" our new entry.
	// 3. The directory is empty, or we reached the end of the file chain
	//	without finding anything that's alphabetically "after" our new entry

	// If the directory is empty, or we've reached the end of the list,
	// we should insert the new entry there
	if (!listItemPointer)
	{
		if (!directory->contents)
		{
			// It's the first file
			directory->contents = theFile;
		}
		else
		{
			// It's the last file
			previousItemPointer->nextEntry = theFile;
			theFile->previousEntry = previousItemPointer;
		}

		// Either way, there's no next entry
		theFile->nextEntry = NULL;
	}

	// If the filenames are the same it's an error.  We are only worried
	// about exact matches, so do a case-sensitive comparison regardless of
	// whether the filesystem driver cares about case.
	else if (!strcmp((char *) theFile->name, (char *) listItemPointer->name))
	{
		// Oops, the file exists
		kernelError(kernel_error, "A file named '%s' exists in the directory",
			theFile->name);
		return (status = ERR_ALREADY);
	}

	// Otherwise, listItemPointer points to an entry that should come AFTER
	// our new entry.  We insert our entry into the previous slot.  Watch
	// out just in case we're BECOMING the first item in the list!
	else
	{
		if (previousItemPointer)
		{
			previousItemPointer->nextEntry = theFile;
			theFile->previousEntry = previousItemPointer;
		}
		else
		{
			// We're the new first item in the list
			directory->contents = theFile;
			theFile->previousEntry = NULL;
		}

		theFile->nextEntry = listItemPointer;
		listItemPointer->previousEntry = theFile;
	}

	// Update the access time on the directory
	directory->lastAccess = kernelCpuTimestamp();

	// Don't mark the directory as dirty; that is the responsibility of the
	// caller, since this function is used in building initial directory
	// structures by the filesystem drivers.

	// Return success
	return (status = 0);
}


int kernelFileRemoveEntry(kernelFileEntry *entry)
{
	// This function is internal, and is used to delete an entry from its
	// parent directory.  It DOES NOT deallocate the file entry structure itself.
	// That must be done, if applicable, by the calling routine.

	kernelFileEntry *directory = NULL;
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
	directory = entry->parentDirectory;
	if (!directory)
	{
		kernelError(kernel_error, "File entry %s has a NULL parent directory",
			entry->name);
		return (status = ERR_NOSUCHFILE);
	}

	// Make sure the parent dir is really a directory
	if (directory->type != dirT)
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
	if (entry == directory->contents)
		directory->contents = nextEntry;

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
	directory->lastAccess = kernelCpuTimestamp();

	// Don't mark the directory as dirty; that is the responsibility of the
	// caller, since this function is used for things like unbuffering
	// directories.

	return (status = 0);
}


int kernelFileGetFullName(kernelFileEntry *theFile, char *buffer, int buffLen)
{
	// Given a kernelFileEntry, construct the fully qualified name

	// Check params
	if (!theFile || !buffer)
	{
		kernelError(kernel_error, "NULL parameter");
		return (ERR_NULLPARAMETER);
	}

	buffer[0] = '\0';
	buildFilenameRecursive(theFile, buffer, buffLen);
	return (0);
}


kernelFileEntry *kernelFileLookup(const char *origPath)
{
	// This is an external wrapper for our internal fileLookup function.

	char *fixedPath = NULL;
	kernelFileEntry *lookupItem = NULL;

	// Check params
	if (!origPath)
	{
		kernelError(kernel_error, "NULL parameter");
		return (lookupItem = NULL);
	}

	// Fix up the path
	fixedPath = fixupPath(origPath);
	if (!fixedPath)
		return (lookupItem = NULL);

	lookupItem = fileLookup(fixedPath);

	kernelFree(fixedPath);

	return (lookupItem);
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


int kernelFileCountDirEntries(kernelFileEntry *theDirectory)
{
	// This function is used to count the current number of directory entries
	// in a given directory.  On success it returns the number of entries.
	// Returns negative on error

	int fileCount = 0;
	kernelFileEntry *listItemPointer = NULL;

	// Check params
	if (!theDirectory)
	{
		kernelError(kernel_error, "NULL parameter");
		return (fileCount = ERR_NULLPARAMETER);
	}

	// Make sure the directory is really a directory
	if (theDirectory->type != dirT)
	{
		kernelError(kernel_error, "Entry in which to count entries is not a "
			"directory");
		return (fileCount = ERR_NOTADIR);
	}

	// Assign the first item in the directory to our iterator pointer
	listItemPointer = theDirectory->contents;

	while (listItemPointer)
	{
		listItemPointer = listItemPointer->nextEntry;
		fileCount += 1;
	}

	return (fileCount);
}


int kernelFileMakeDotDirs(kernelFileEntry *parentDir, kernelFileEntry *dir)
{
	// This just makes the '.' and '..' links in a new directory

	int status = 0;
	kernelFileEntry *dotDir = NULL;
	kernelFileEntry *dotDotDir = NULL;

	// Check params
	if (!dir)
	{
		kernelError(kernel_error, "NULL parameter");
		return (status = ERR_NULLPARAMETER);
	}

	dotDir = kernelFileNewEntry(dir->disk);
	if (!dotDir)
		return (status = ERR_NOFREE);

	strcpy((char *) dotDir->name, ".");
	dotDir->type = linkT;
	dotDir->contents = dir;

	status = kernelFileInsertEntry(dotDir, dir);

	if (parentDir)
	{
		dotDotDir = kernelFileNewEntry(dir->disk);
		if (!dotDotDir)
		{
			kernelFileReleaseEntry(dotDir);
			return (status = ERR_NOFREE);
		}

		strcpy((char *) dotDotDir->name, "..");
		dotDotDir->type = linkT;
		dotDotDir->contents = dir->parentDirectory;

		status = kernelFileInsertEntry(dotDotDir, dir);
	}

	return (status);
}


int kernelFileUnbufferRecursive(kernelFileEntry *dir)
{
	// This function can be used to unbuffer a directory tree recursively.
	// Mostly, this will be useful only when unmounting filesystems.
	// Returns 0 on success, negative otherwise.

	int status = 0;
	kernelFileEntry *listItemPointer = NULL;

	// Is dir a directory?
	if (dir->type != dirT)
		return (status = ERR_NOTADIR);

	// Is the current directory a leaf directory?
	if (!isLeafDir(dir))
	{
		// It's not a leaf directory.  We will need to walk through each of
		// the entries and do a recursion for any subdirectories that have
		// buffered content.
		listItemPointer = dir->contents;
		while (listItemPointer)
		{
			if ((listItemPointer->type == dirT) &&
				(listItemPointer->contents))
			{
				status = kernelFileUnbufferRecursive(listItemPointer);
				if (status < 0)
					return (status);
			}
			else
			{
				listItemPointer = listItemPointer->nextEntry;
			}
		}
	}

	// Now this directory should be a leaf directory.  Do any of its files
	// currently have a valid lock?
	listItemPointer = dir->contents;
	while (listItemPointer)
	{
		if (kernelLockVerify(&listItemPointer->lock))
			break;
		else
			listItemPointer = listItemPointer->nextEntry;
	}

	if (listItemPointer)
		// There are open files here
		return (status = ERR_BUSY);

	// This directory is safe to unbuffer
	status = unbufferDirectory(dir);

	// Return the status from that call
	return (status);
}


int kernelFileEntrySetSize(kernelFileEntry *entry, unsigned newSize)
{
	// This file allows the caller to specify the real size of a file, since
	// the other routines here must assume that the file consumes all of the
	// space in all of its blocks (they have no way to know otherwise).

	int status = 0;
	kernelDisk *theDisk = NULL;
	unsigned newBlocks = 0;

	// Check parameters
	if (!entry)
		return (status = ERR_NULLPARAMETER);

	theDisk = (kernelDisk *) entry->disk;

	// Not allowed in read-only file system
	if (theDisk->filesystem.readOnly)
	{
		kernelError(kernel_error, "Filesystem is read-only");
		return (status = ERR_NOWRITE);
	}

	newBlocks = ((newSize + (theDisk->filesystem.blockSize - 1)) /
		 theDisk->filesystem.blockSize);

	// Does the new size change the number of blocks?
	if (entry->blocks != newBlocks)
	{
		if (theDisk->filesystem.driver->driverSetBlocks)
		{
			status = theDisk->filesystem.driver->driverSetBlocks(entry,
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

	if (theDisk->filesystem.driver->driverWriteDir)
	{
		status =
			theDisk->filesystem.driver->driverWriteDir(entry->parentDirectory);
		if (status < 0)
			return (status);
	}

	// Return success
	return (status = 0);
}


int kernelFileFixupPath(const char *originalPath, char *newPath)
{
	// This function is a user-accessible wrapper for our internal
	// fixupPath() funtion, above.

	int status = 0;
	char *tmpPath = NULL;

	// Check params
	if (!originalPath || !newPath)
	{
		kernelError(kernel_error, "NULL parameter");
		return (status = ERR_NULLPARAMETER);
	}

	tmpPath = fixupPath(originalPath);
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
	// separate the two.  The user will pass in the "combined" string
	// along with two pre-allocated char arrays to hold the resulting
	// separated elements.

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

	// Do a loop backwards from the end of the combined until we hit
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
//  Below here, the functions are basically external wrappers for
//  functions above, which are exported outside the kernel and accept
//  text strings for file names, etc.
//
/////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////

int kernelFileGetDisk(const char *path, disk *userDisk)
{
	// Given a filename, return the the disk it resides on.

	int status = 0;
	kernelFileEntry *item = NULL;

	if (!initialized)
		return (status = ERR_NOTINITIALIZED);

	// Check parameters.
	if (!path || !userDisk)
	{
		kernelError(kernel_error, "NULL parameter");
		return (status = ERR_NULLPARAMETER);
	}

	item = kernelFileLookup(path);
	if (!item)
		// There is no such item
		return (status = ERR_NOSUCHFILE);

	status = kernelDiskFromLogical(item->disk, userDisk);
	return (status);
}


int kernelFileCount(const char *path)
{
	// This is a user-accessible wrapper function for the
	// kernelFileCountDirEntries function, above.

	int status = 0;
	kernelFileEntry *directory = NULL;

	if (!initialized)
		return (status = ERR_NOTINITIALIZED);

	// Check params
	if (!path)
	{
		kernelError(kernel_error, "NULL parameter");
		return (status = ERR_NULLPARAMETER);
	}

	// Make the starting directory correspond to the path we were given
	directory = kernelFileLookup(path);
	// Make sure it's good, and that it's really a subdirectory
	if (!directory || (directory->type != dirT))
	{
		kernelError(kernel_error, "Invalid directory \"%s\" for lookup",
			path);
		return (status = ERR_NOSUCHFILE);
	}

	return (kernelFileCountDirEntries(directory));
}


int kernelFileFirst(const char *path, file *fileStructure)
{
	// This is merely a wrapper function for the equivalent function
	// in the requested filesystem's own driver.  It takes nearly-identical
	// arguments and returns the same status as the driver function.

	int status = 0;
	kernelFileEntry *directory = NULL;

	if (!initialized)
		return (status = ERR_NOTINITIALIZED);

	// Check params
	if (!path || !fileStructure)
	{
		kernelError(kernel_error, "NULL parameter");
		return (status = ERR_NULLPARAMETER);
	}

	// Make the starting directory correspond to the path we were given
	directory = kernelFileLookup(path);
	if (!directory)
	{
		kernelError(kernel_error, "No such directory \"%s\" for lookup",
			path);
		return (status = ERR_NOSUCHFILE);
	}
	if (directory->type != dirT)
	{
		kernelError(kernel_error, "\"%s\" is not a directory", path);
		return (status = ERR_INVALID);
	}

	fileStructure->handle = NULL;  // INVALID

	if (!directory->contents)
		// The directory is empty
		return (status = ERR_NOSUCHFILE);

	fileEntry2File(directory->contents, fileStructure);

	// Return success
	return (status = 0);
}


int kernelFileNext(const char *path, file *fileStructure)
{
	// This is merely a wrapper function for the equivalent function
	// in the requested filesystem's own driver.  It takes nearly-identical
	// arguments and returns the same status as the driver function.

	int status = 0;
	kernelFileEntry *directory = NULL;
	kernelFileEntry *listItem = NULL;

	if (!initialized)
		return (status = ERR_NOTINITIALIZED);

	// Check params
	if (!path || !fileStructure)
	{
		kernelError(kernel_error, "NULL parameter");
		return (status = ERR_NULLPARAMETER);
	}

	// Make the starting directory correspond to the path we were given
	directory = kernelFileLookup(path);
	// Make sure it's good, and that it's really a subdirectory
	if (!directory || (directory->type != dirT))
	{
		kernelError(kernel_error, "Invalid directory for lookup");
		return (status = ERR_NOSUCHFILE);
	}

	// Find the previously accessed file in the current directory.  Start
	// with the first entry.
	if (directory->contents)
	{
		listItem = directory->contents;
	}
	else
	{
		kernelError(kernel_error, "No file entries in directory");
		return (status = ERR_NOSUCHFILE);
	}

	while (strcmp((char *) listItem->name, fileStructure->name)
		&& (listItem->nextEntry))
	{
		listItem = listItem->nextEntry;
	}

	if (!strcmp((char *) listItem->name, fileStructure->name)
		&& (listItem->nextEntry))
	{
		// Now we've found that last item.  Move one more down the list
		listItem = listItem->nextEntry;

		fileEntry2File(listItem, fileStructure);
		fileStructure->handle = NULL;  // INVALID
	}
	else
	{
		// There are no more files
		return (status = ERR_NOSUCHFILE);
	}

	// Make sure the directory and files have their "last accessed"
	// fields updated

	// Return success
	return (status = 0);
}


int kernelFileFind(const char *path, file *fileStructure)
{
	// This is merely a wrapper function for the equivalent function
	// in the requested filesystem's own driver.  It takes nearly-identical
	// arguments and returns the same status as the driver function.

	int status = 0;
	kernelFileEntry *item = NULL;

	if (!initialized)
		return (status = ERR_NOTINITIALIZED);

	// Check params.  It's OK for the file structure to be NULL.
	if (!path)
	{
		kernelError(kernel_error, "NULL parameter");
		return (status = ERR_NULLPARAMETER);
	}

	// Call the routine that actually finds the item
	item = kernelFileLookup(path);
	if (!item)
		// There is no such item
		return (status = ERR_NOSUCHFILE);

	// Otherwise, we are OK, we got a good file.  If a file structure pointer
	// was supplied, we should now copy the relevant information from the
	// kernelFileEntry structure into the user structure.
	if (fileStructure)
	{
		fileEntry2File(item, fileStructure);
		fileStructure->handle = NULL;  // INVALID UNTIL OPENED
	}

	// Return success
	return (status = 0);
}


int kernelFileOpen(const char *fileName, int openMode, file *fileStructure)
{
	// This is an externalized version of the 'fileOpen' function above.  It
	// Accepts a string filename and returns a filled user-space file
	// structure;

	int status = 0;
	char *fixedName = NULL;
	kernelFileEntry *openItem = NULL;

	if (!initialized)
		return (status = ERR_NOTINITIALIZED);

	// Check params
	if (!fileName || !fileStructure)
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
	openItem = kernelFileLookup(fixedName);
	if (!openItem)
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
		openItem = fileLookup(fixedName);
		if (!openItem)
		{
			// Something is really wrong
			kernelFree(fixedName);
			return (status = ERR_NOCREATE);
		}
	}

	kernelFree(fixedName);

	// Call our internal openFile function
	status = fileOpen(openItem, openMode);
	if (status < 0)
		return (status);

	// Set up the file handle
	fileEntry2File(openItem, fileStructure);

	// Set the "open mode" flags on the opened file
	fileStructure->openMode = openMode;

	// Return success
	return (status = 0);
}


int kernelFileClose(file *fileStructure)
{
	// This is merely a wrapper function for the equivalent function
	// in the requested filesystem's own driver.  It takes nearly-identical
	// arguments and returns the same status as the driver function.

	int status = 0;
	kernelFileEntry *theFile = NULL;

	if (!initialized)
		return (status = ERR_NOTINITIALIZED);

	// Check params
	if (!fileStructure)
	{
		kernelError(kernel_error, "NULL parameter");
		return (status = ERR_NULLPARAMETER);
	}

	// Make sure the file handle refers to a valid file
	theFile = (kernelFileEntry *) fileStructure->handle;
	if (!theFile)
		// Probably wasn't opened first.  No big deal.
		return (status = ERR_NULLPARAMETER);

	// Reduce the open count on the file in question
	if (theFile->openCount > 0)
		theFile->openCount -= 1;

	// If the file was locked by this PID, we should unlock it
	kernelLockRelease(&theFile->lock);

	// Return success
	return (status = 0);
}


int kernelFileRead(file *fileStructure, unsigned blockNum,
	unsigned blocks, void *fileBuffer)
{
	// This function is responsible for all reading of files.  It takes a
	// file structure of an opened file, the number of pages to skip before
	// commencing the read, the number of pages to read, and a buffer to use.
	// Returns negative on error, otherwise it returns the same status as
	// the driver function.

	int status = 0;
	kernelDisk *theDisk = NULL;
	kernelFilesystemDriver *theDriver = NULL;

	if (!initialized)
		return (status = ERR_NOTINITIALIZED);

	// Check params
	if (!fileStructure || !fileBuffer)
	{
		kernelError(kernel_error, "NULL parameter");
		return (status = ERR_NULLPARAMETER);
	}

	if (!fileStructure->handle)
	{
		kernelError(kernel_error, "NULL file handle for read.  Not opened "
			"first?");
		return (status = ERR_NULLPARAMETER);
	}

	// Make sure the number of blocks to read is non-zero
	if (!blocks)
		// This isn't an error exactly, there just isn't anything to do.
		return (status = 0);

	// Get the filesystem based on the filesystem number in the
	// file structure
	theDisk = ((kernelFileEntry *) fileStructure->handle)->disk;
	if (!theDisk)
	{
		kernelError(kernel_error, "NULL disk pointer");
		return (status = ERR_BADADDRESS);
	}

	theDriver = theDisk->filesystem.driver;

	// OK, we just have to check on the filesystem driver function we want
	// to call
	if (!theDriver->driverReadFile)
	{
		kernelError(kernel_error, "The requested filesystem operation is not "
			"supported");
		return (status = ERR_NOSUCHFUNCTION);
	}

	// Now we can call our target function
	status = theDriver->driverReadFile(fileStructure->handle, blockNum,
		blocks, fileBuffer);

	// Make sure the file structure is up to date after the call
	fileEntry2File(fileStructure->handle, fileStructure);

	// Return the same error status that the driver function returned
	return (status);
}


int kernelFileWrite(file *fileStructure, unsigned blockNum,
	unsigned blocks, void *fileBuffer)
{
	// This function is responsible for all writing of files.  It takes a
	// file structure of an opened file, the number of blocks to skip before
	// commencing the write, the number of blocks to write, and a buffer to use.
	// Returns negative on error, otherwise it returns the same status as
	// the driver function.

	int status = 0;
	kernelDisk *theDisk = NULL;
	kernelFilesystemDriver *theDriver = NULL;

	if (!initialized)
		return (status = ERR_NOTINITIALIZED);

	// Check params
	if (!fileStructure || !fileBuffer)
	{
		kernelError(kernel_error, "NULL parameter");
		return (status = ERR_NULLPARAMETER);
	}

	if (!fileStructure->handle)
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
	if (!(fileStructure->openMode & OPENMODE_WRITE))
	{
		kernelError(kernel_error, "File has not been opened for writing");
		return (status = ERR_INVALID);
	}

	// Get the filesystem based on the filesystem number in the
	// file structure
	theDisk = ((kernelFileEntry *) fileStructure->handle)->disk;
	if (!theDisk)
	{
		kernelError(kernel_error, "NULL disk pointer");
		return (status = ERR_BADADDRESS);
	}

	// Not allowed in read-only file system
	if (theDisk->filesystem.readOnly)
	{
		kernelError(kernel_error, "Filesystem is read-only");
		return (status = ERR_NOWRITE);
	}

	theDriver = theDisk->filesystem.driver;

	// OK, we just have to check on the filesystem driver function we want
	// to call
	if (!theDriver->driverWriteFile)
	{
		kernelError(kernel_error, "The requested filesystem operation is not "
			"supported");
		return (status = ERR_NOSUCHFUNCTION);
	}

	// Now we can call our target function
	status = theDriver->driverWriteFile(fileStructure->handle, blockNum,
		blocks, fileBuffer);
	if (status < 0)
		return (status);

	// Update the directory
	if (theDriver->driverWriteDir)
	{
		status = theDriver->driverWriteDir(((kernelFileEntry *)
			fileStructure->handle)->parentDirectory);
		if (status < 0)
			return (status);
	}

	// Make sure the file structure is up to date after the call
	fileEntry2File(fileStructure->handle, fileStructure);

	// Return the same error status that the driver function returned
	return (status = 0);
}


int kernelFileDelete(const char *fileName)
{
	// Given a file name, delete the file.

	int status = 0;
	kernelFileEntry *theFile = NULL;

	if (!initialized)
		return (status = ERR_NOTINITIALIZED);

	// Check params
	if (!fileName)
	{
		kernelError(kernel_error, "NULL parameter");
		return (status = ERR_NULLPARAMETER);
	}

	// Get the file to delete
	theFile = kernelFileLookup(fileName);
	if (!theFile)
	{
		// The directory does not exist
		kernelError(kernel_error, "File %s to delete does not exist", fileName);
		return (status = ERR_NOSUCHFILE);
	}

	// OK, the file exists.  Call our internal 'delete' function
	return (fileDelete(theFile));
}


int kernelFileDeleteRecursive(const char *itemName)
{
	int status = 0;
	kernelFileEntry *delItem = NULL;

	if (!initialized)
		return (status = ERR_NOTINITIALIZED);

	// Check params
	if (!itemName)
	{
		kernelError(kernel_error, "NULL parameter");
		return (status = ERR_NULLPARAMETER);
	}

	// Get the file to delete
	delItem = kernelFileLookup(itemName);
	if (!delItem)
	{
		// The directory does not exist
		kernelError(kernel_error, "Item %s to delete does not exist", itemName);
		return (status = ERR_NOSUCHFILE);
	}

	return (fileDeleteRecursive(delItem));
}


int kernelFileDeleteSecure(const char *fileName, int passes)
{
	// This function deletes a file, but first does a number of passes of
	// writing random data over top of the file, followed by a pass of NULLs,
	// and finally does a normal file deletion.

	int status = 0;
	kernelFileEntry *theFile = NULL;
	kernelFilesystemDriver *theDriver = NULL;
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
	theFile = kernelFileLookup(fileName);
	if (!theFile)
	{
		// The directory does not exist
		kernelError(kernel_error, "File %s to delete does not exist", fileName);
		return (status = ERR_NOSUCHFILE);
	}

	// Not allowed in read-only file system
	if (theFile->disk->filesystem.readOnly)
	{
		kernelError(kernel_error, "Filesystem is read-only");
		return (status = ERR_NOWRITE);
	}

	theDriver = theFile->disk->filesystem.driver;
	if (!theDriver->driverWriteFile)
	{
		kernelError(kernel_error, "The requested filesystem operation is not "
			"supported");
		return (status = ERR_NOSUCHFUNCTION);
	}

	// Get a buffer the as big as all of the blocks allocated to the file.
	blockSize = theFile->disk->filesystem.blockSize;
	bufferSize = (theFile->blocks * blockSize);
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

			for (count2 = 1; count2 < theFile->blocks; count2 ++)
				memcpy((buffer + (count2 * blockSize)), buffer, blockSize);
		}
		else
		{
			// Clear the buffer with NULLs
			memset(buffer, 0, bufferSize);
		}

		// Write the file
		status = theDriver->driverWriteFile(theFile, 0, theFile->blocks, buffer);

		// Sync the disk to make sure the data has been written out
		kernelDiskSync((char *) theFile->disk->name);

		if (status < 0)
			break;
	}

	kernelFree(buffer);

	if (status < 0)
		return (status);

	// Now do a normal file deletion.
	return (status = fileDelete(theFile));
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
	kernelFileEntry *theDir = NULL;

	if (!initialized)
		return (status = ERR_NOTINITIALIZED);

	// Check params
	if (!path)
	{
		kernelError(kernel_error, "NULL parameter");
		return (status = ERR_NULLPARAMETER);
	}

	// Look for the requested directory
	theDir = kernelFileLookup(path);
	if (!theDir)
	{
		// The directory does not exist
		kernelError(kernel_error, "Directory %s to delete does not exist",
			path);
		return (status = ERR_NOSUCHDIR);
	}

	return (fileRemoveDir(theDir));
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
	kernelFileEntry *destFile = NULL;

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
	destFile = fileLookup(fixedDestName);

	if (destFile)
	{
		// It already exists.  Is it a directory?
		if (destFile->type == dirT)
		{
			// It's a directory, so what we really want to do is make a
			// destination file in that directory that shares the same filename
			// as the source.  Construct the new name.
			strcat(fixedDestName, "/");
			strcat(fixedDestName, srcFileStruct.name);

			destFile = fileLookup(fixedDestName);
			if (destFile)
			{
				// Remove the existing file
				status = fileDelete(destFile);
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
	uquad_t freeSpace = kernelFilesystemGetFreeBytes(destFileStruct.filesystem);
	if (srcFileStruct.size > freeSpace)
	{
		kernelError(kernel_error, "Not enough space (%llu < %u) in destination "
			"filesystem", freeSpace, srcFileStruct.size);
		status = ERR_NOFREE;
		goto out;
	}

	destFile = (kernelFileEntry *) destFileStruct.handle;

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
	kernelFileEntry *src = NULL;
	kernelFileEntry *dest = NULL;
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
	src = kernelFileLookup(srcPath);
	if (!src)
	{
		kernelError(kernel_error, "File to copy does not exist");
		return (status = ERR_NOSUCHENTRY);
	}

	// It exists.  Is it a directory?
	if (src->type == dirT)
	{
		// It's a directory, so we create the destination directory if it doesn't
		// already exist, then we loop through the entries in the source
		// directory.  If an entry is a file, copy it.  If it is a directory,
		// recurse.

		dest = kernelFileLookup(destPath);
		if (dest)
		{
			// If the destination directory exists, but has a different filename
			// than the source directory, that means we might need to create the
			// new destination directory inside it with the original's name.
			if (strcmp((char *) dest->name, (char *) src->name))
			{
				tmpDestName = fixupPath(destPath);
				if (tmpDestName)
				{
					strcat(tmpDestName, "/");
					strcat(tmpDestName, (char *) src->name);

					dest = kernelFileLookup(tmpDestName);

					if (dest && (dest->type != dirT))
					{
						// Some non-directory item is sitting there using our
						// desired destination name, blocking us.  Try to delete
						// it.
						fileDelete(dest);
						dest = NULL;
					}

					if (!dest)
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

			dest = kernelFileLookup(destPath);
			if (!dest)
				return (status = ERR_NOSUCHENTRY);
		}

		// Get the first file in the source directory
		src = src->contents;

		while (src)
		{
			if (strcmp((char *) src->name, ".") &&
				strcmp((char *) src->name, ".."))
			{
				// Add the file's name to the directory's name
				tmpSrcName = fixupPath(srcPath);
				if (tmpSrcName)
				{
					strcat(tmpSrcName, "/");
					strcat(tmpSrcName, (const char *) src->name);
					// Add the file's name to the destination file name
					tmpDestName = fixupPath(destPath);
					if (tmpDestName)
					{
						strcat(tmpDestName, "/");
						strcat(tmpDestName, (const char *) src->name);

						status =
							kernelFileCopyRecursive(tmpSrcName, tmpDestName);

						kernelFree(tmpSrcName);
						kernelFree(tmpDestName);

						if (status < 0)
							return (status);
					}
				}
			}

			src = src->nextEntry;
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
	kernelFileEntry *srcItem = NULL;
	kernelFileEntry *destDir = NULL;
	kernelFileEntry *destItem = NULL;

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
		if (fixedSrcName)
			kernelFree(fixedSrcName);
		if (fixedDestName)
			kernelFree(fixedDestName);
		return (status = ERR_NOSUCHENTRY);
	}

	// Now make sure that the requested source item exists.  Whether it is a
	// file or directory is unimportant.
	srcItem = fileLookup(fixedSrcName);
	if (!srcItem)
	{
		// The directory does not exist
		kernelError(kernel_error, "Source item %s does not exist", fixedSrcName);
		kernelFree(fixedSrcName);
		kernelFree(fixedDestName);
		return (status = ERR_NOSUCHFILE);
	}

	// Don't need this after here.
	kernelFree(fixedSrcName);

	// Save the source directory
	srcDir = srcItem->parentDirectory;
	if (!srcDir)
	{
		kernelError(kernel_error, "Source item \"%s\" to move has a NULL parent "
			"directory!", fixedSrcName);
		kernelFree(fixedDestName);
		return (status = ERR_NULLPARAMETER);
	}

	// Save the source file's original name, just in case we encounter
	// problems later
	strncpy(origName, (char *) srcItem->name, MAX_NAME_LENGTH);

	// Check whether the destination file exists.  If it exists and it is
	// a file, we need to delete it first.
	destItem = fileLookup(fixedDestName);

	if (destItem)
	{
		// It already exists.  Is it a directory?
		if (destItem->type == dirT)
		{
			// It's a directory, so what we really want to do is set our dest
			// dir to that.
			destDir = destItem;

			// Append the source name and see if a file exists by that name
			strcat(fixedDestName, (char *) srcItem->name);
			destItem = fileLookup(fixedDestName);
		}

		if (destItem)
		{
			// There already exists an item at that location, and it is not
			// a directory.  It needs to go away.  Really, we should keep it
			// until the other one is moved successfully, but...

			// Save the pointer to the parent directory
			destDir = destItem->parentDirectory;

			// Set the dest file's name from the old one
			strcpy((char *) srcItem->name, (char *) destItem->name);

			status = fileDelete(destItem);
			if (status < 0)
			{
				kernelFree(fixedDestName);
				return (status);
			}
		}
	}
	else
	{
		// No such file exists.  We need to get the destination directory
		// from the destination path we were supplied.
		status = kernelFileSeparateLast(fixedDestName, destDirName,
			(char *) srcItem->name);
		if (status < 0)
		{
			kernelFree(fixedDestName);
			return (status);
		}

		destDir = kernelFileLookup(destDirName);
		if (!destDir)
		{
			kernelError(kernel_error, "Destination directory does not exist");
			kernelFree(fixedDestName);
			return (status = ERR_NOSUCHDIR);
		}
	}

	kernelFree(fixedDestName);

	// Place the file in its new directory
	status = fileMove(srcItem, destDir);
	if (status < 0)
	{
		// We were unable to place it in the new directory.  Better at
		// least try to put it back where it belongs in its old directory,
		// with its old name.  Whether this succeeds or fails, we need to try
		strcpy((char *) srcItem->name, origName);
		kernelFileInsertEntry(srcItem, srcDir);
		return (status);
	}

	// Return success
	return (status = 0);
}


int kernelFileTimestamp(const char *path)
{
	// This is merely a wrapper function for the equivalent function
	// in the requested filesystem's own driver.  It takes nearly-identical
	// arguments and returns the same status as the driver function.

	int status = 0;
	char *fileName = NULL;
	kernelFileEntry *theFile = NULL;
	kernelDisk *theDisk = NULL;
	kernelFilesystemDriver *theDriver = NULL;

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
		theFile = kernelFileLookup(fileName);

		kernelFree(fileName);

		if (!theFile)
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
	updateModifiedTime(theFile);
	updateAccessedTime(theFile);

	// Now allow the underlying filesystem driver to do anything that's
	// specific to that filesystem type.
	theDisk = (kernelDisk *) theFile->disk;
	if (!theDisk)
	{
		kernelError(kernel_error, "NULL disk pointer");
		return (status = ERR_BADADDRESS);
	}

	// Not allowed in read-only file system
	if (theDisk->filesystem.readOnly)
	{
		kernelError(kernel_error, "Filesystem is read-only");
		return (status = ERR_NOWRITE);
	}

	theDriver = theDisk->filesystem.driver;

	// Call the filesystem driver function, if it exists
	if (theDriver->driverTimestamp)
	{
		status = theDriver->driverTimestamp(theFile);
		if (status < 0)
			return (status);
	}

	// Write the directory
	if (theDriver->driverWriteDir)
	{
		status = theDriver->driverWriteDir(theFile->parentDirectory);
		if (status < 0)
			return (status);
	}

	return (status = 0);
}


int kernelFileSetSize(file *fileStructure, unsigned newSize)
{
	// This function is an externally-accessible wrapper for our
	// kernelFileEntrySetSize function.

	int status = 0;

	if (!initialized)
		return (status = ERR_NOTINITIALIZED);

	// Check params
	if (!fileStructure)
	{
		kernelError(kernel_error, "NULL parameter");
		return (status = ERR_NULLPARAMETER);
	}

	if (!fileStructure->handle)
	{
		kernelError(kernel_error, "NULL file handle for set size.  Not opened "
			"first?");
		return (status = ERR_NULLPARAMETER);
	}

	// Has the file been opened properly for writing?
	if (!(fileStructure->openMode & OPENMODE_WRITE))
	{
		kernelError(kernel_error, "File %s has not been opened for writing %x",
			fileStructure->name, fileStructure->openMode);
		return (status = ERR_INVALID);
	}

	status = kernelFileEntrySetSize((kernelFileEntry *) fileStructure->handle,
		newSize);
	if (status < 0)
		return (status);

	// Make sure the file structure is up to date after the call
	fileEntry2File(fileStructure->handle, fileStructure);

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
			kernelCurrentProcess->processId, (unsigned) kernelCpuTimestamp());

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

	if (!rootDirectory ||
		((kernelDisk *) rootDirectory->disk)->filesystem.readOnly)
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


int kernelFileGetFullPath(file *theFile, char *buffer, int buffLen)
{
	// Returns the full path name of a file.  This is an exported wrapper for the
	// kernelFileGetFullName function.

	int status = 0;

	// Check params
	if (!theFile || !buffer)
	{
		kernelError(kernel_error, "NULL parameter");
		return (status = ERR_NULLPARAMETER);
	}

	if (!theFile->handle)
	{
		kernelError(kernel_error, "NULL file handle");
		return (status = ERR_NULLPARAMETER);
	}

	status = kernelFileGetFullName(theFile->handle, buffer, buffLen);

	return (status);
}

