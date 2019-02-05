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
//  archman.c
//

// This is a graphical program for managing archive files.

/* This is the text that appears when a user requests help about this program
<help>

 -- archman --

A graphical program for managing archive files.

Usage:
  archman [archive]

The archman program is interactive, and may only be used in graphics
mode.  It displays a window with icons representing archive menbers.

</help>
*/

#include <errno.h>
#include <libgen.h>
#include <libintl.h>
#include <locale.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/api.h>
#include <sys/compress.h>
#include <sys/env.h>
#include <sys/window.h>

#define _(string) gettext(string)
#define gettext_noop(string) (string)

#define WINDOW_TITLE		_("Archive Manager")
#define NEW_BUTTON			_("New")
#define OPEN_BUTTON			_("Open")
#define EXTRACTALL_BUTTON	_("Extract all")
#define EXTRACT_BUTTON		_("Extract")
#define ADD_BUTTON			_("Add")
#define DELETE_BUTTON		_("Delete")

#define BUTTONIMAGE_SIZE	16

typedef struct _archive {
	int num;
	char *fileName;
	int parent;
	char *memberName;
	archiveMemberInfo *members;
	int numMembers;
	char *tempDir;

} archive;

// General things
static int tempArchive = 0;
static char *extractDir = NULL;
static archive *archives = NULL;
static int numArchives = 0;
static archive *current = NULL;
static int selectedMember = 0;
static int modified = 0;

// Window objects
static objectKey window = NULL;
static char *locationString = NULL;
static objectKey upButton = NULL;
static objectKey locationField = NULL;
static windowArchiveList *archList = NULL;
static objectKey newButton = NULL;
static objectKey openButton = NULL;
static objectKey extractAllButton = NULL;
static objectKey extractButton = NULL;
static objectKey addButton = NULL;
static objectKey deleteButton = NULL;


__attribute__((format(printf, 1, 2)))
static void error(const char *format, ...)
{
	// Generic error message code

	va_list list;
	char *output = NULL;

	output = malloc(MAXSTRINGLENGTH);
	if (!output)
		return;

	va_start(list, format);
	vsnprintf(output, MAXSTRINGLENGTH, format, list);
	va_end(list);

	windowNewErrorDialog(window, _("Error"), output);
	free(output);
}


static int makeTempArchive(char *fileName, int len)
{
	// Get a temporary file name for a new archive (the openArchive() function
	// will create the initial empty file)

	int status = 0;

	status = fileGetTempName(fileName, len);
	if (status < 0)
	{
		error("%s", _("Couldn't create temporary file"));
		return (status);
	}

	tempArchive = 1;

	return (status = 0);
}


static int getArchiveInfo(void)
{
	// Fill in the archive structure's members array and numMembers by calling
	// the compression library

	int status = 0;
	objectKey bannerDialog = NULL;

	windowSwitchPointer(window, MOUSE_POINTER_BUSY);

	if (current->members)
		// Free the old archive info
		archiveInfoFree(current->members, current->numMembers);

	current->members = NULL; current->numMembers = 0;

	bannerDialog = windowNewBannerDialog(window, _("Getting info"),
		_("Reading archive info"));

	// Get the new archive info
	current->numMembers = archiveInfo(current->fileName,
		&current->members, NULL /* progress */);

	if (bannerDialog)
		windowDestroy(bannerDialog);

	windowSwitchPointer(window, MOUSE_POINTER_DEFAULT);

	if (current->numMembers < 0)
	{
		error("%s", _("Couldn't read archive contents"));
		return (status = current->numMembers);
	}

	return (status = 0);
}


static void setLocationStringRecursive(archive *arch)
{
	// Recursively construct a string for the current 'location' inside the
	// archive

	if (arch->memberName)
	{
		setLocationStringRecursive(&archives[arch->parent]);

		locationString = realloc(locationString, (strlen(locationString) +
			strlen(arch->memberName) + 2));
		if (locationString)
		{
			strcat(locationString, "/");
			strcat(locationString, arch->memberName);
		}
	}
	else
	{
		if (locationString)
			free(locationString);

		locationString = malloc(strlen(arch->fileName) + 1);
		if (locationString)
			strcpy(locationString, arch->fileName);
	}
}


static int isArchiveFile(const char *fileName)
{
	// Returns 1 if fileName points to an archive file

	loaderFileClass class;

	memset(&class, 0, sizeof(loaderFileClass));

	// Try to classify it
	if (!loaderClassifyFile(fileName, &class))
		return (0);

	// Is it an archive?
	if (class.type & LOADERFILECLASS_ARCHIVE)
		return (1);
	else
		return (0);
}


static int openArchive(const char *fileName, int parent,
	const char *memberName)
{
	// Given a fileName (and possibly references to where this archive is
	// contained within another) does nuts and bolts of 'opening' the archive
	// so we can manipulate it.

	int status = 0;
	file tmpFile;

	archives = realloc(archives, ((numArchives + 1) * sizeof(archive)));
	if (!archives)
	{
		status = ERR_MEMORY;
		goto out;
	}

	current = &archives[numArchives];

	memset(current, 0, sizeof(archive));

	current->num = numArchives;

	current->fileName = malloc(MAX_PATH_NAME_LENGTH);
	if (!current->fileName)
	{
		status = ERR_MEMORY;
		goto out;
	}

	status = fileFixupPath(fileName, current->fileName);
	if (status < 0)
		goto out;

	// If this archive is a member of another, remember its member name, in
	// case we modify it and have to re-add it to its parent.
	if (memberName)
	{
		current->parent = parent;

		current->memberName = malloc(MAX_PATH_NAME_LENGTH);
		if (!current->memberName)
		{
			status = ERR_MEMORY;
			goto out;
		}

		strncpy(current->memberName, memberName, MAX_PATH_NAME_LENGTH);
	}

	// See whether the file exists
	status = fileFind(current->fileName, NULL /* file */);
	if (status < 0)
	{
		// Create a new, empty file
		status = fileOpen(current->fileName, OPENMODE_CREATE, &tmpFile);
		if (status < 0)
		{
			error(_("Error %d creating new archive file"), status);
			goto out;
		}

		fileClose(&tmpFile);
	}
	else
	{
		if (!isArchiveFile(fileName))
		{
			error(_("%s is not an archive file"), fileName);
			status = ERR_INVALID;
			goto out;
		}

		status = getArchiveInfo();
		if (status < 0)
			goto out;
	}

	setLocationStringRecursive(current);

	numArchives += 1;
	status = 0;

out:
	if (status < 0)
	{
		if (current->memberName)
			free(current->memberName);
		if (current->fileName)
			free(current->fileName);
	}

	return (status);
}


static void closeArchive(archive *arch)
{
	// 'Close' the archive when we're finished with it

	if (arch->fileName)
		free(arch->fileName);

	if (arch->memberName)
		free(arch->memberName);

	archiveInfoFree(arch->members, arch->numMembers);

	if (arch->tempDir)
	{
		fileDeleteRecursive(arch->tempDir);
		free(arch->tempDir);
	}

	memset(arch, 0, sizeof(archive));
}


static int getTempDir(void)
{
	// Get a temporary directory for extracting archive members

	int status = 0;

	if (!current->tempDir)
	{
		// Get memory for a temporary working directory name
		current->tempDir = malloc(MAX_PATH_NAME_LENGTH);
		if (!current->tempDir)
		{
			status = ERR_MEMORY;
			goto out;
		}

		// Get a temporary directory name
		status = fileGetTempName(current->tempDir, MAX_PATH_NAME_LENGTH);
		if (status < 0)
			goto out;

		status = fileMakeDir(current->tempDir);
		if (status < 0)
			goto out;
	}

	status = 0;

out:
	if (status < 0)
	{
		if (current->tempDir)
			free(current->tempDir);

		error("%s", _("Couldn't create working directory"));
	}

	return (status);
}


static int extractTemp(int memberNum)
{
	// Extract an archive member to the temporary directory

	int status = 0;
	char cwd[MAX_PATH_LENGTH];

	status = multitaskerGetCurrentDirectory(cwd, MAX_PATH_LENGTH);
	if (status < 0)
		return (status);

	status = getTempDir();
	if (status < 0)
		return (status);

	status = multitaskerSetCurrentDirectory(current->tempDir);
	if (status < 0)
		return (status);

	status = archiveExtractMember(current->fileName,
		current->members[memberNum].name, 0 /* memberIndex */,
		NULL /* outFileName */, NULL /* progress */);

	multitaskerSetCurrentDirectory(cwd);

	return (status);
}


static void doMemberSelection(int memberNum)
{
	// Called when the user selects an archive member in the archive list

	int status = 0;
	char *tmpFileName = NULL;

	selectedMember = memberNum;

	windowSwitchPointer(window, MOUSE_POINTER_BUSY);

	// Try to extract it.  We want to know whether it is itself an archive.
	status = extractTemp(memberNum);
	if (status < 0)
		goto out;

	tmpFileName = malloc(MAX_PATH_NAME_LENGTH);
	if (!tmpFileName)
		goto out;

	sprintf(tmpFileName, "%s/%s", current->tempDir,
		current->members[memberNum].name);

	// Is the archive member itself an archive?
	if (isArchiveFile(tmpFileName))
	{
		status = openArchive(tmpFileName, current->num,
			current->members[memberNum].name);
		if (status < 0)
			goto out;

		// Refresh the location field
		windowComponentSetData(locationField, locationString,
			strlen(locationString), 1 /* redraw */);

		// Refresh the archive list view
		if (archList->update)
			archList->update(archList, current->members, current->numMembers);

		windowComponentSetEnabled(upButton, 1);

		selectedMember = 0;
	}

out:
	windowSwitchPointer(window, MOUSE_POINTER_DEFAULT);

	if (tmpFileName)
		free(tmpFileName);
}


static void refreshWindow(void)
{
	// We got a 'window refresh' event (probably because of a language
	// switch), so we need to update things

	// Re-get the language setting
	setlocale(LC_ALL, getenv(ENV_LANG));
	textdomain("archman");

	// Re-get the character set
	if (getenv(ENV_CHARSET))
		windowSetCharSet(window, getenv(ENV_CHARSET));

	// Refresh the 'new' button
	windowComponentSetData(newButton, NEW_BUTTON, strlen(NEW_BUTTON),
		1 /* redraw */);

	// Refresh the 'open' button
	windowComponentSetData(openButton, OPEN_BUTTON, strlen(OPEN_BUTTON),
		1 /* redraw */);

	// Refresh the 'extract all' button
	windowComponentSetData(extractAllButton, EXTRACTALL_BUTTON,
		strlen(EXTRACTALL_BUTTON), 1 /* redraw */);

	// Refresh the 'extract' button
	windowComponentSetData(extractButton, EXTRACT_BUTTON,
		strlen(EXTRACT_BUTTON), 1 /* redraw */);

	// Refresh the 'add' button
	windowComponentSetData(addButton, ADD_BUTTON, strlen(ADD_BUTTON),
		1 /* redraw */);

	// Refresh the 'delete' button
	windowComponentSetData(deleteButton, DELETE_BUTTON, strlen(DELETE_BUTTON),
		1 /* redraw */);

	// Refresh the window title
	windowSetTitle(window, WINDOW_TITLE);
}


static void setExtension(char *fileName, const char *extension)
{
	char *baseName = NULL;
	char *end = NULL;
	char *dirName = NULL;

	baseName = basename(fileName);
	if (baseName)
	{
		end = strstr(baseName, extension);
		if (end)
		{
			dirName = dirname(fileName);
			if (dirName)
			{
				strcpy(end, extension);
				sprintf(fileName, "%s%s%s", dirName, (strcmp(dirName, "/")?
					"/" : ""), baseName);
				free(dirName);
			}
		}
		else
		{
			strcat(fileName, extension);
		}

		free(baseName);
	}
}


static int querySaveModified(void)
{
	// The user is closing an archive that's been modified.  Query whether
	// (and possibly where) they'd like to save it.

	int status = 0;
	int compress = 0;
	char *fileName = NULL;
	char *dirName = NULL;
	char *baseName = NULL;
	char *tmpName = NULL;
	char cwd[MAX_PATH_LENGTH];
	objectKey progressDialog = NULL;
	progress prog;

	memset((void *) &prog, 0, sizeof(progress));

	if (windowNewChoiceDialog(window, _("Save changes?"),
		_("Archive has been modified.  Save changes?"),
		(char *[]){ _("Save"), _("Discard") }, 2, 1) == 1)
	{
		return (status = 0);
	}

	status = windowNewRadioDialog(window, _("Choose archive type"),
		_("What format should the archive be saved as?"),
		(char *[]){ _("TAR archive (.tar)"),
		_("Gzip-compressed TAR archive (.tar.gz)") }, 2, 0);
	if (status < 0)
		return (status = ERR_CANCELLED);

	if (status == 1)
		compress = 1;

	fileName = malloc(MAX_PATH_NAME_LENGTH);
	if (!fileName)
		return (status = ERR_MEMORY);

	// Ask for a file name to save with
	status = windowNewFileDialog(window, _("Save as"),
		_("Please enter a destination file:"), NULL /* startDir */, fileName,
		 MAX_PATH_NAME_LENGTH, fileT, 0 /* no thumbnails */);
	if (status != 1)
	{
		free(fileName);
		return (status = ERR_CANCELLED);
	}

	tempArchive = 0;

	// This will have to be more sophisticated when we support more archive
	// types
	setExtension(fileName, ".tar");

	// Try to temporarily rename the file, leaving it in its temporary
	// directory
	dirName = dirname(archives[0].fileName);
	if (dirName)
	{
		baseName = basename(fileName);
		if (baseName)
		{
			tmpName = malloc(MAX_PATH_NAME_LENGTH);
			if (tmpName)
				sprintf(tmpName, "%s/%s", dirName, baseName);

			free(baseName);
		}

		if (tmpName)
		{
			status = fileMove(archives[0].fileName, tmpName);
			if (status >= 0)
			{
				free(archives[0].fileName);
				archives[0].fileName = tmpName;
			}
			else
			{
				free(tmpName);
			}
		}

		free(dirName);
	}

	if (compress)
	{
		// This will have to be more sophisticated when we support more
		// archive types
		setExtension(fileName, ".gz");

		dirName = dirname(archives[0].fileName);
		if (dirName)
		{
			if ((multitaskerGetCurrentDirectory(cwd, MAX_PATH_LENGTH) >= 0) &&
				(multitaskerSetCurrentDirectory(dirName) >= 0))
			{
				baseName = basename(archives[0].fileName);
				if (baseName)
				{
					strcat(archives[0].fileName, ".compressed");

					windowSwitchPointer(window, MOUSE_POINTER_BUSY);

					progressDialog = windowNewProgressDialog(window,
						_("Compressing"), &prog);

					status = gzipCompressFile(baseName, archives[0].fileName,
						NULL /* comment */, 0 /* append */, &prog);

					windowSwitchPointer(window, MOUSE_POINTER_DEFAULT);

					if (progressDialog)
						windowProgressDialogDestroy(progressDialog);

					if (status < 0)
					{
						error(_("Error %d compressing archive"), status);
						free(baseName);
						free(dirName);
						free(fileName);
						return (status);
					}

					status = fileMove(archives[0].fileName, fileName);
					if (status >= 0)
					{
						fileDelete(baseName);
						free(archives[0].fileName);
						archives[0].fileName = fileName;
					}

					free(baseName);
				}

				multitaskerSetCurrentDirectory(cwd);
			}

			free(dirName);
		}
	}
	else
	{
		status = fileMove(archives[0].fileName, fileName);
		if (status < 0)
		{
			free(fileName);
			return (status);
		}

		free(archives[0].fileName);
		archives[0].fileName = fileName;
	}

	return (status = 0);
}


static void closeAll(void)
{
	// Close all the archives

	// Was a temporary archive modified?
	if (tempArchive)
	{
		if (modified && archives[0].numMembers)
			querySaveModified();

		if (tempArchive)
		{
			if (fileFind(archives[0].fileName, NULL /* file */) >= 0)
				fileDelete(archives[0].fileName);

			tempArchive = 0;
		}
	}

	while (numArchives)
	{
		closeArchive(&archives[numArchives - 1]);
		numArchives -= 1;
	}

	free(archives);
	archives = NULL;

	modified = 0;
}


static int up(void)
{
	// The user clicked the upButton.  Go up to the parent archive.

	if (!current->memberName)
		return (ERR_ALREADY);

	current = &archives[current->parent];

	setLocationStringRecursive(current);

	// Refresh the location field
	windowComponentSetData(locationField, locationString,
		strlen(locationString), 1 /* redraw */);

	// Refresh the archive list view
	if (archList->update)
		archList->update(archList, current->members, current->numMembers);

	windowComponentSetEnabled(upButton, (current->memberName != NULL));

	return (0);
}


static int new(void)
{
	// The user clicked the newButton.  Set up a new, empty archive.

	int status = 0;
	char *fileName = NULL;

	// Close all the archives
	closeAll();

	fileName = malloc(MAX_PATH_NAME_LENGTH);
	if (!fileName)
		return (status = ERR_MEMORY);

	status = makeTempArchive(fileName, MAX_PATH_NAME_LENGTH);
	if (status < 0)
		goto out;

	// By default, we extract in the same directory as the archive

	if (extractDir)
		free(extractDir);

	extractDir = dirname(fileName);
	if (!extractDir)
	{
		status = ERR_MEMORY;
		goto out;
	}

	status = openArchive(fileName, 0 /* parent */, NULL /* memberName */);
	if (status < 0)
		goto out;

	// Refresh the location field
	windowComponentSetData(locationField, locationString,
		strlen(locationString), 1 /* redraw */);

	// Refresh the archive list view
	if (archList->update)
		archList->update(archList, current->members, current->numMembers);

	windowComponentSetEnabled(upButton, 0);

	status = 0;

out:
	if (fileName)
		free(fileName);

	return (status);
}


static int open(void)
{
	// The user clicked the openButton.  Query the user to find out what
	// archive they'd like to open, and open it, after closing any existing
	// open ones

	int status = 0;
	char *fileName = NULL;

	fileName = malloc(MAX_PATH_NAME_LENGTH);
	if (!fileName)
		return (status = ERR_MEMORY);

	// Ask for an archive to open
	status = windowNewFileDialog(window, _("Choose file"),
		_("Please enter a file to open:"), NULL /* startDir */,
		fileName, MAX_PATH_NAME_LENGTH, fileT, 0 /* no thumbnails */);
	if (status != 1)
	{
		// Cancelled
		status = 0;
		goto out;
	}

	// Close all the archives
	closeAll();

	// By default, we extract in the same directory as the archive

	if (extractDir)
		free(extractDir);

	extractDir = dirname(fileName);
	if (!extractDir)
	{
		status = ERR_MEMORY;
		goto out;
	}

	status = openArchive(fileName, 0 /* parent */, NULL /* memberName */);
	if (status < 0)
		goto out;

	// Refresh the location field
	windowComponentSetData(locationField, locationString,
		strlen(locationString), 1 /* redraw */);

	// Refresh the archive list view
	if (archList->update)
		archList->update(archList, current->members, current->numMembers);

	windowComponentSetEnabled(upButton, 0);

	status = 0;

out:
	if (fileName)
		free(fileName);

	return (status);
}


static int queryExtractDir(void)
{
	// Query the user to find out where they'd like to extract files to

	int status = 0;
	char *newExtractDir = NULL;

	newExtractDir = malloc(MAX_PATH_LENGTH);
	if (!newExtractDir)
		return (status = ERR_MEMORY);

	strncpy(newExtractDir, extractDir, MAX_PATH_LENGTH);

	// Ask for a directory to extract to
	status = windowNewFileDialog(window, _("Enter directory"),
		_("Please enter a directory to extract to:"), extractDir,
		newExtractDir, MAX_PATH_LENGTH, dirT, 0 /* no thumbnails */);
	if (status != 1)
	{
		free(newExtractDir);
		return (status = ERR_CANCELLED);
	}

	free(extractDir);
	extractDir = newExtractDir;

	return (status = 0);
}


static int extract(int all)
{
	// The user clicked the extractButton or extractAllButton.  Extract a
	// member of the archive, or the whole archive.

	int status = 0;
	char cwd[MAX_PATH_LENGTH];
	objectKey progressDialog = NULL;
	progress prog;

	memset((void *) &prog, 0, sizeof(progress));

	if (current->numMembers < 1)
		return (status = 0);

	status = queryExtractDir();
	if (status < 0)
	{
		if (status == ERR_CANCELLED)
			return (status = 0);

		return (status);
	}

	status = multitaskerGetCurrentDirectory(cwd, MAX_PATH_LENGTH);
	if (status < 0)
		return (status);

	status = multitaskerSetCurrentDirectory(extractDir);
	if (status < 0)
		return (status);

	progressDialog = windowNewProgressDialog(window, _("Extracting"), &prog);

	if (all)
	{
		// Extract everything
		status = archiveExtract(current->fileName, &prog);
	}
	else
	{
		// Extract the selected member
		status = archiveExtractMember(current->fileName,
			current->members[selectedMember].name, 0 /* memberIndex */,
			NULL /* outFileName */, &prog);
	}

	if (progressDialog)
		windowProgressDialogDestroy(progressDialog);

	multitaskerSetCurrentDirectory(cwd);

	return (status);
}


static int reAddToParentRecursive(archive *arch)
{
	// Remove an archive from its parent and re-add it

	int status = 0;
	char cwd[MAX_PATH_LENGTH];
	archive *parent = &archives[arch->parent];

	status = multitaskerGetCurrentDirectory(cwd, MAX_PATH_LENGTH);
	if (status < 0)
		return (status);

	status = multitaskerSetCurrentDirectory(parent->tempDir);
	if (status < 0)
		return (status);

	// Just try to delete it.  If all the members had previously been deleted,
	// then a previous call to this function might mean it doesn't currently
	// exist as a member of its parent archive (see next step).
	archiveDeleteMember(parent->fileName, arch->memberName,
		0 /* memberIndex */, NULL /* progress */);

	// If all the members have now been deleted, the archive file will no
	// longer exist, so only try to add it back if the file is there.
	if (fileFind(arch->memberName, NULL /* file */) >= 0)
	{
		status = archiveAddMember(arch->memberName, parent->fileName,
			0 /* type */, NULL /* comment */, NULL /* progress */);
		if (status < 0)
			return (status);
	}

	multitaskerSetCurrentDirectory(cwd);

	if (parent->memberName)
		status = reAddToParentRecursive(parent);

	return (status);
}


static int add(void)
{
	// The user clicked the addButton.  Query for a file or directory to add,
	// and add it.

	int status = 0;
	char *addItem = NULL;
	objectKey progressDialog = NULL;
	progress prog;

	memset((void *) &prog, 0, sizeof(progress));

	addItem = malloc(MAX_PATH_NAME_LENGTH);
	if (!addItem)
		return (status = ERR_MEMORY);

	// Ask for a file or directory to add
	status = windowNewFileDialog(window, _("Choose item"),
		_("Please enter a file or directory to add:"), NULL /* startDir */,
		addItem, MAX_PATH_NAME_LENGTH, unknownT, 0 /* no thumbnails */);
	if (status != 1)
	{
		// Cancelled
		status = 0;
		goto out;
	}

	windowSwitchPointer(window, MOUSE_POINTER_BUSY);

	progressDialog = windowNewProgressDialog(window, _("Adding"), &prog);

	status = archiveAddRecursive(addItem, current->fileName,
		LOADERFILESUBCLASS_TAR, NULL /* comment */, &prog);

	if (status < 0)
	{
		windowSwitchPointer(window, MOUSE_POINTER_DEFAULT);
		error(_("Error %d adding %s"), status, addItem);
	}

	// We (possibly) modified the archive
	modified = 1;

	// Is this archive inside another?
	if ((status >= 0) && current->memberName)
	{
		status = reAddToParentRecursive(current);
		if (status < 0)
		{
			windowSwitchPointer(window, MOUSE_POINTER_DEFAULT);
			error(_("Error %d adding to parent archive"), status);
		}
	}

	if (progressDialog)
		windowProgressDialogDestroy(progressDialog);

	status = getArchiveInfo();
	if (status < 0)
		goto out;

	// Refresh the archive list view
	if (archList->update)
		archList->update(archList, current->members, current->numMembers);

	status = 0;

out:
	if (addItem)
		free(addItem);

	return (status);
}


static int delete(void)
{
	// The user clicked the deleteButton.  Delete any selected member.

	int status = 0;
	objectKey progressDialog = NULL;
	progress prog;

	memset((void *) &prog, 0, sizeof(progress));

	if (current->numMembers < 1)
		return (status = 0);

	windowSwitchPointer(window, MOUSE_POINTER_BUSY);

	progressDialog = windowNewProgressDialog(window, _("Deleting"), &prog);

	status = archiveDeleteMember(current->fileName,
		current->members[selectedMember].name, 0 /* memberIndex */, &prog);

	if (status < 0)
	{
		windowSwitchPointer(window, MOUSE_POINTER_DEFAULT);
		error(_("Error %d deleting %s"), status,
			current->members[selectedMember].name);
		return (status);
	}

	// We modified the archive
	modified = 1;

	// Is this archive inside another?
	if (current->memberName)
	{
		status = reAddToParentRecursive(current);
		if (status < 0)
		{
			windowSwitchPointer(window, MOUSE_POINTER_DEFAULT);
			error(_("Error %d adding to parent archive"), status);
		}
	}

	if (progressDialog)
		windowProgressDialogDestroy(progressDialog);

	if (current->numMembers <= 1)
	{
		// Nothing left, although the archive theoretically still exists, if
		// the user wants to add new members.
		windowSwitchPointer(window, MOUSE_POINTER_DEFAULT);
		archiveInfoFree(current->members, current->numMembers);
		current->members = NULL, current->numMembers = 0;
	}
	else
	{
		status = getArchiveInfo();
		if (status < 0)
			goto out;
	}

	// Refresh the archive list view
	if (archList->update)
		archList->update(archList, current->members, current->numMembers);

	status = 0;

out:
	return (status);
}


static void eventHandler(objectKey key, windowEvent *event)
{
	int status = 0;

	// Check for window events.
	if (key == window)
	{
		// Check for window refresh
		if (event->type == EVENT_WINDOW_REFRESH)
			refreshWindow();

		// Check for the window being closed
		else if (event->type == EVENT_WINDOW_CLOSE)
			windowGuiStop();
	}

	// Check for events to be passed to the archive list widget
	else if (key == archList->key)
	{
		archList->eventHandler(archList, event);
	}

	else if ((key == upButton) && (event->type == EVENT_MOUSE_LEFTUP))
	{
		status = up();
		if (status < 0)
			error("%s", strerror(status));
	}

	else if ((key == newButton) && (event->type == EVENT_MOUSE_LEFTUP))
	{
		status = new();
		if (status < 0)
			error("%s", strerror(status));
	}

	else if ((key == openButton) && (event->type == EVENT_MOUSE_LEFTUP))
	{
		status = open();
		if (status < 0)
			error("%s", strerror(status));
	}

	else if ((key == extractAllButton) && (event->type == EVENT_MOUSE_LEFTUP))
	{
		status = extract(1 /* all */);
		if (status < 0)
			error("%s", strerror(status));
	}

	else if ((key == extractButton) && (event->type == EVENT_MOUSE_LEFTUP))
	{
		status = extract(0 /* current member */);
		if (status < 0)
			error("%s", strerror(status));
	}

	else if ((key == addButton) && (event->type == EVENT_MOUSE_LEFTUP))
	{
		status = add();
		if (status < 0)
			error("%s", strerror(status));
	}

	else if ((key == deleteButton) && (event->type == EVENT_MOUSE_LEFTUP))
	{
		status = delete();
		if (status < 0)
			error("%s", strerror(status));
	}
}


static int constructWindow(void)
{
	int status = 0;
	objectKey topContainer = NULL;
	image buttonImage;
	objectKey buttonContainer = NULL;
	componentParameters params;

	// Create a new window
	window = windowNew(multitaskerGetCurrentProcessId(), WINDOW_TITLE);
	if (!window)
		return (status = ERR_NOCREATE);

	memset(&params, 0, sizeof(componentParameters));

	params.gridWidth = 2;
	params.gridHeight = 1;
	params.padTop = params.padLeft = params.padRight = 5;
	params.orientationX = orient_left;
	params.orientationY = orient_top;
	params.flags = WINDOW_COMPFLAG_FIXEDHEIGHT;

	// Create a container for the top components
	topContainer = windowNewContainer(window, "topContainer", &params);
	if (!topContainer)
		return (status = ERR_NOCREATE);

	// Create the up button
	params.gridWidth = 1;
	params.padTop = params.padLeft = params.padRight = 0;
	params.flags |= WINDOW_COMPFLAG_FIXEDWIDTH;
	imageLoad(PATH_SYSTEM_ICONS "/arrowup.ico" , BUTTONIMAGE_SIZE,
		BUTTONIMAGE_SIZE, &buttonImage);
	upButton = windowNewButton(topContainer, (buttonImage.data? NULL :
		_("Up")), (buttonImage.data? &buttonImage : NULL), &params);
	imageFree(&buttonImage);
	if (!upButton)
		return (status = ERR_NOCREATE);
	windowComponentSetEnabled(upButton, 0);

	// Register the event handler for the up button
	windowRegisterEventHandler(upButton, &eventHandler);

	// Create the location text field
	params.gridX += 1;
	params.padLeft = 5;
	params.orientationY = orient_middle;
	params.flags &= ~WINDOW_COMPFLAG_FIXEDWIDTH;
	locationField = windowNewTextField(topContainer, 40, &params);
	if (!locationField)
		return (status = ERR_NOCREATE);
	windowComponentSetData(locationField, locationString,
		strlen(locationString), 1 /* redraw */);
	windowComponentSetEnabled(locationField, 0);

	// Create the archive list widget
	params.gridX = 0;
	params.gridY += 1;
	params.padTop = params.padLeft = params.padBottom = 5;
	params.padRight = 0;
	params.orientationY = orient_top;
	params.flags &= ~WINDOW_COMPFLAG_FIXEDHEIGHT;
	archList = windowNewArchiveList(window, windowlist_textonly, 20, 1,
		current->members, current->numMembers, &doMemberSelection, &params);
	if (!archList)
		return (status = ERR_NOCREATE);

	// Register the event handler for the archive list
	windowRegisterEventHandler(archList->key, &eventHandler);

	// Make the archive list have the focus
	windowComponentFocus(archList->key);

	// Create a container for the side buttons
	params.gridX += 1;
	params.padRight = 5;
	params.flags = (WINDOW_COMPFLAG_FIXEDWIDTH | WINDOW_COMPFLAG_FIXEDHEIGHT);
	buttonContainer = windowNewContainer(window, "buttonContainer", &params);
	if (!buttonContainer)
		return (status = ERR_NOCREATE);

	// Create a 'new' button
	params.gridX = params.gridY = 0;
	params.padTop = params.padBottom = params.padLeft = params.padRight = 0;
	params.flags = 0;
	newButton = windowNewButton(buttonContainer, NEW_BUTTON, NULL /* image */,
		&params);
	if (!newButton)
		return (status = ERR_NOCREATE);

	// Register the event handler for the new button
	windowRegisterEventHandler(newButton, &eventHandler);

	// Create an 'open' button
	params.gridY += 1;
	params.padTop = 5;
	openButton = windowNewButton(buttonContainer, OPEN_BUTTON,
		NULL /* image */, &params);
	if (!openButton)
		return (status = ERR_NOCREATE);

	// Register the event handler for the open button
	windowRegisterEventHandler(openButton, &eventHandler);

	// Create an 'extract all' button
	params.gridY += 1;
	extractAllButton = windowNewButton(buttonContainer, EXTRACTALL_BUTTON,
		NULL /* image */, &params);
	if (!extractAllButton)
		return (status = ERR_NOCREATE);

	// Register the event handler for the extract all button
	windowRegisterEventHandler(extractAllButton, &eventHandler);

	// Create an 'extract' button
	params.gridY += 1;
	extractButton = windowNewButton(buttonContainer, EXTRACT_BUTTON,
		NULL /* image */, &params);
	if (!extractButton)
		return (status = ERR_NOCREATE);

	// Register the event handler for the extract button
	windowRegisterEventHandler(extractButton, &eventHandler);

	// Create an 'add' button
	params.gridY += 1;
	addButton = windowNewButton(buttonContainer, ADD_BUTTON, NULL /* image */,
		&params);
	if (!addButton)
		return (status = ERR_NOCREATE);

	// Register the event handler for the add button
	windowRegisterEventHandler(addButton, &eventHandler);

	// Create a 'delete' button
	params.gridY += 1;
	deleteButton = windowNewButton(buttonContainer, DELETE_BUTTON,
		NULL /* image */, &params);
	if (!deleteButton)
		return (status = ERR_NOCREATE);

	// Register the event handler for the delete button
	windowRegisterEventHandler(deleteButton, &eventHandler);

	// Register an event handler to catch window close events
	windowRegisterEventHandler(window, &eventHandler);

	// Set the window size
	windowSetSize(window, (graphicGetScreenWidth() / 2),
		(graphicGetScreenHeight() / 2));

	// Show the window
	windowSetVisible(window, 1);

	return (status = 0);
}


int main(int argc, char *argv[])
{
	int status = 0;
	char fileName[MAX_PATH_NAME_LENGTH];

	setlocale(LC_ALL, getenv(ENV_LANG));
	textdomain("archman");

	// Only work in graphics mode
	if (!graphicsAreEnabled())
	{
		fprintf(stderr, _("\nThe \"%s\" command only works in graphics "
			"mode\n"), (argc? argv[0] : ""));
		return (status = ERR_NOTINITIALIZED);
	}

	// If an archive file was not specified, create a temporary one
	if (argc < 2)
	{
		status = makeTempArchive(fileName, MAX_PATH_NAME_LENGTH);
		if (status < 0)
			goto out;
	}
	else
	{
		strncpy(fileName, argv[argc - 1], MAX_PATH_NAME_LENGTH);
	}

	// By default, we extract in the same directory as the archive
	extractDir = dirname(fileName);
	if (!extractDir)
	{
		status = ERR_MEMORY;
		goto out;
	}

	status = openArchive(fileName, 0 /* parent */, NULL /* memberName */);
	if (status < 0)
		goto out;

	status = constructWindow();
	if (status >= 0)
	{
		// Run the GUI
		windowGuiRun();

		if (archList)
			archList->destroy(archList);
	}

	// Close all the archives
	closeAll();

	if (window)
	{
		windowDestroy(window);
		window = NULL;
	}

	// Return success
	status = 0;

out:
	if (locationString)
		free(locationString);

	if (extractDir)
		free(extractDir);

	return (status);
}

