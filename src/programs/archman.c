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
//  archman.c
//

// This is a graphical program for managing archive files.

/* This is the text that appears when a user requests help about this program
<help>

 -- archman --

A graphical program for managing archive files.

Usage:
  filebrowse [archive]

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
#define EXTRACTALL_BUTTON	_("Extract all")
#define EXTRACT_BUTTON		_("Extract")

typedef struct {
	char *fileName;
	archiveMemberInfo *members;
	int numMembers;
	char *tempDir;

} archive;

static char *extractDir = NULL;
static archive *archives = NULL;
static int numArchives = 0;
static archive *current = NULL;
static int selectedMember = 0;
static objectKey window = NULL;
static windowArchiveList *archList = NULL;
static objectKey extractAllButton = NULL;
static objectKey extractButton = NULL;


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


static int openArchive(char *fileName)
{
	int status = 0;
	file tmpFile;

	windowSwitchPointer(window, MOUSE_POINTER_BUSY);

	archives = realloc(archives, ((numArchives + 1) * sizeof(archive)));
	if (!archives)
	{
		status = ERR_MEMORY;
		goto out;
	}

	current = &archives[numArchives++];

	memset(current, 0, sizeof(archive));

	current->fileName = malloc(MAX_PATH_NAME_LENGTH);
	if (!current->fileName)
	{
		status = ERR_MEMORY;
		goto out;
	}

	status = fileFixupPath(fileName, current->fileName);
	if (status < 0)
		goto out;

	// See whether the file exists
	status = fileFind(current->fileName, &tmpFile);
	if (status < 0)
	{
		status = fileOpen(current->fileName, OPENMODE_CREATE, &tmpFile);
		if (status < 0)
		{
			error(_("Error %d creating new archive file."), status);
			goto out;
		}

		fileClose(&tmpFile);
	}
	else
	{
		// Get the archive info
		current->numMembers = archiveInfo(current->fileName, &current->members,
			NULL /* progress */);

		if (current->numMembers < 0)
		{
			error("%s", _("Couldn't read archive contents"));
			status = current->numMembers;
			goto out;
		}
	}

	status = 0;

out:
	windowSwitchPointer(window, MOUSE_POINTER_DEFAULT);

	if (status < 0)
	{
		if (current->fileName)
			free(current->fileName);
	}

	return (status);
}


static void closeArchive(archive *arch)
{
	free(arch->fileName);

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
	// Extract an archive member to a temporary directory

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
	loaderFileClass class;

	memset(&class, 0, sizeof(loaderFileClass));

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

	if (!loaderClassifyFile(tmpFileName, &class))
		goto out;

	if (class.class & LOADERFILECLASS_ARCHIVE)
	{
		status = openArchive(tmpFileName);
		if (status < 0)
			goto out;

		if (archList->update)
			archList->update(archList, current->members, current->numMembers);

		selectedMember = 0;
	}

out:
	windowSwitchPointer(window, MOUSE_POINTER_DEFAULT);

	if (tmpFileName)
		free(tmpFileName);
}


static void refreshWindow(void)
{
	// We got a 'window refresh' event (probably because of a language switch),
	// so we need to update things

	// Re-get the language setting
	setlocale(LC_ALL, getenv(ENV_LANG));
	textdomain("archman");

	// Re-get the character set
	if (getenv(ENV_CHARSET))
		windowSetCharSet(window, getenv(ENV_CHARSET));

	// Refresh the 'extract all' button
	windowComponentSetData(extractAllButton, EXTRACTALL_BUTTON,
		strlen(EXTRACTALL_BUTTON), 1 /* redraw */);

	// Refresh the 'extract' button
	windowComponentSetData(extractButton, EXTRACT_BUTTON,
		strlen(EXTRACT_BUTTON), 1 /* redraw */);

	// Refresh the window title
	windowSetTitle(window, WINDOW_TITLE);
}


static int getExtractDir(void)
{
	// Query the user to find out where they'd like to extract files to

	int status = 0;
	char *newExtractDir = NULL;

	newExtractDir = malloc(MAX_PATH_LENGTH);
	if (!newExtractDir)
		return (status = ERR_MEMORY);

	strncpy(newExtractDir, extractDir, MAX_PATH_LENGTH);

	// Ask for a directory to extract to
	status = windowNewFileDialog(NULL, _("Enter directory"),
		_("Please enter a directory to extract to:"), extractDir,
		newExtractDir, MAX_PATH_LENGTH, dirT, 0 /* no thumbnails */);
	if (status != 1)
	{
		free(newExtractDir);
		return (status = ERR_CANCELLED);
	}

	strncpy(extractDir, newExtractDir, MAX_PATH_LENGTH);
	free(newExtractDir);

	return (status = 0);
}


static int extract(int all)
{
	// Extract a member of the archive, or the whole archive

	int status = 0;
	char cwd[MAX_PATH_LENGTH];
	objectKey progressDialog = NULL;
	progress prog;

	memset((void *) &prog, 0, sizeof(progress));

	status = multitaskerGetCurrentDirectory(cwd, MAX_PATH_LENGTH);
	if (status < 0)
		return (status);

	status = getExtractDir();
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
}


static int constructWindow(void)
{
	int status = 0;
	objectKey buttonContainer = NULL;
	componentParameters params;

	// Create a new window
	window = windowNew(multitaskerGetCurrentProcessId(), WINDOW_TITLE);
	if (!window)
		return (status = ERR_NOCREATE);

	memset(&params, 0, sizeof(componentParameters));

	params.gridWidth = 1;
	params.gridHeight = 1;
	params.padTop = 5;
	params.padLeft = 5;
	params.orientationX = orient_center;
	params.orientationY = orient_top;

	// Create the archive list widget
	params.padBottom = 5;
	archList = windowNewArchiveList(window, windowlist_textonly, 10, 1,
		current->members, current->numMembers, doMemberSelection, &params);
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

	// Create an 'extract all ' button
	params.gridX = 0;
	params.padTop = params.padBottom = params.padLeft = params.padRight = 0;
	params.flags = 0;
	extractAllButton = windowNewButton(buttonContainer, EXTRACTALL_BUTTON,
		NULL /* image */, &params);
	if (!extractAllButton)
		return (status = ERR_NOCREATE);

	// Register the event handler for the extract all button
	windowRegisterEventHandler(extractAllButton, &eventHandler);

	// Create an 'extract' button
	params.gridY += 1;
	params.padTop = 5;
	extractButton = windowNewButton(buttonContainer, EXTRACT_BUTTON,
		NULL /* image */, &params);
	if (!extractButton)
		return (status = ERR_NOCREATE);

	// Register the event handler for the extract button
	windowRegisterEventHandler(extractButton, &eventHandler);

	// Register an event handler to catch window close events
	windowRegisterEventHandler(window, &eventHandler);

	// Set the window size
	windowSetSize(window, (graphicGetScreenWidth() / 3),
		(graphicGetScreenHeight() / 3));

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
		fprintf(stderr, _("\nThe \"%s\" command only works in graphics mode\n"),
			(argc? argv[0] : ""));
		return (status = ERR_NOTINITIALIZED);
	}

	// If an archive file was not specified, ask for it
	if (argc < 2)
	{
		status = windowNewFileDialog(NULL, _("Enter filename"),
			_("Please enter an archive file to manage:"), NULL, fileName,
			 MAX_PATH_NAME_LENGTH, fileT, 0 /* no thumbnails */);
		if (status != 1)
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

	status = openArchive(fileName);
	if (status < 0)
		goto out;

	status = constructWindow();
	if (status >= 0)
	{
		// Run the GUI
		windowGuiRun();

		if (archList)
			archList->destroy(archList);

		windowDestroy(window);
	}

	while (numArchives)
	{
		closeArchive(&archives[numArchives - 1]);
		numArchives -= 1;
	}

	// Return success
	status = 0;

out:
	if (extractDir)
		free(extractDir);

	return (status);
}

