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
//  filebrowse.c
//

// This is a graphical program for navigating the file system.

/* This is the text that appears when a user requests help about this program
<help>

 -- filebrowse --

A graphical program for navigating the file system.

Usage:
  filebrowse [start_dir]

The filebrowse program is interactive, and may only be used in graphics
mode.  It displays a window with icons representing files and directories.
Clicking on a directory (folder) icon will change to that directory and
repopulate the window with its contents.  Clicking on any other icon will
cause filebrowse to attempt to 'use' the file in a default way, which will
be a different action depending on the file type.  For example, if you
click on an image or document, filebrowse will attempt to display it using
the 'view' command.  In the case of clicking on an executable program,
filebrowse will attempt to execute it -- etc.

</help>
*/

#include <libintl.h>
#include <locale.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/api.h>
#include <sys/env.h>
#include <sys/lock.h>
#include <sys/paths.h>
#include <sys/vsh.h>
#include <sys/window.h>

#define _(string) gettext(string)
#define gettext_noop(string) (string)

#define WINDOW_TITLE		_("File Browser")
#define FILE_MENU			_("File")
#define VIEW_MENU			_("View")
#define EXECPROG_ARCHMAN	PATH_PROGRAMS "/archman"
#define EXECPROG_CONFEDIT	PATH_PROGRAMS "/confedit"
#define EXECPROG_FONTUTIL	PATH_PROGRAMS "/fontutil"
#define EXECPROG_KEYMAP		PATH_PROGRAMS "/keymap"
#define EXECPROG_VIEW		PATH_PROGRAMS "/view"

#define FILEMENU_QUIT 0
windowMenuContents fileMenuContents = {
	1,
	{
		{ gettext_noop("Quit"), NULL }
	}
};

#define VIEWMENU_REFRESH 0
windowMenuContents viewMenuContents = {
	1,
	{
		{ gettext_noop("Refresh"), NULL }
	}
};

typedef struct {
	char name[MAX_PATH_LENGTH];
	int selected;

} dirRecord;

static int processId = 0;
static int privilege = 0;
static objectKey window = NULL;
static objectKey fileMenu = NULL;
static objectKey viewMenu = NULL;
static objectKey locationField = NULL;
static windowFileList *fileList = NULL;
static dirRecord *dirStack = NULL;
static int dirStackCurr = 0;
static lock dirStackLock;
static time_t cwdModified = 0;
static int stop = 0;


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


static void changeDir(file *theFile, char *fullName)
{
	file cwdFile;

	while (lockGet(&dirStackLock) < 0)
		multitaskerYield();

	if (!strcmp(theFile->name, ".."))
	{
		if (dirStackCurr > 0)
		{
			dirStackCurr -= 1;
			windowComponentSetSelected(fileList->key,
				dirStack[dirStackCurr].selected);
		}
		else
		{
			strncpy(dirStack[dirStackCurr].name, fullName, MAX_PATH_LENGTH);
			dirStack[dirStackCurr].selected = 0;
		}
	}
	else
	{
		dirStackCurr += 1;
		strncpy(dirStack[dirStackCurr].name, fullName, MAX_PATH_LENGTH);
		dirStack[dirStackCurr].selected = 0;
	}

	if (multitaskerSetCurrentDirectory(dirStack[dirStackCurr].name) >= 0)
	{
		// Look up the directory and save the modified date and time, so we
		// can rescan it if it gets modified
		if (fileFind(dirStack[dirStackCurr].name, &cwdFile) >= 0)
			cwdModified = mktime(&cwdFile.modified);

		windowComponentSetData(locationField, dirStack[dirStackCurr].name,
			strlen(fullName), 1 /* redraw */);
	}

	lockRelease(&dirStackLock);
}


static void execProgram(int argc, char *argv[])
{
	// Exec the command, no block
	if (argc == 2)
		loaderLoadAndExec(argv[1], privilege, 0);

	multitaskerTerminate(0);
}


static void doFileSelection(file *theFile, char *fullName,
	loaderFileClass *loaderClass)
{
	char command[MAX_PATH_NAME_LENGTH];
	int pid = 0;

	switch (theFile->type)
	{
		case fileT:
		{
			if (loaderClass->class & LOADERFILECLASS_EXEC)
			{
				strcpy(command, fullName);
			}
			else if ((loaderClass->class & LOADERFILECLASS_ARCHIVE) &&
				!fileFind(EXECPROG_ARCHMAN, NULL))
			{
				sprintf(command, EXECPROG_ARCHMAN " \"%s\"", fullName);
			}
			else if (((loaderClass->class & LOADERFILECLASS_DATA) &&
				(loaderClass->subClass & LOADERFILESUBCLASS_CONFIG)) &&
					!fileFind(EXECPROG_CONFEDIT, NULL))
			{
				sprintf(command, EXECPROG_CONFEDIT " \"%s\"", fullName);
			}
			else if ((loaderClass->class & LOADERFILECLASS_FONT) &&
				!fileFind(EXECPROG_FONTUTIL, NULL))
			{
				sprintf(command, EXECPROG_FONTUTIL " \"%s\"", fullName);
			}
			else if ((loaderClass->class & LOADERFILECLASS_KEYMAP) &&
				!fileFind(EXECPROG_KEYMAP, NULL))
			{
				sprintf(command, EXECPROG_KEYMAP " \"%s\"", fullName);
			}
			else if ((loaderClass->class & LOADERFILECLASS_TEXT) &&
				!fileFind(EXECPROG_VIEW, NULL))
			{
				sprintf(command, EXECPROG_VIEW " \"%s\"", fullName);
			}
			else if ((loaderClass->class & LOADERFILECLASS_IMAGE) &&
				!fileFind(EXECPROG_VIEW, NULL))
			{
				sprintf(command, EXECPROG_VIEW " \"%s\"", fullName);
			}
			else
			{
				return;
			}

			windowSwitchPointer(window, MOUSE_POINTER_BUSY);

			// Run a thread to execute the command
			pid = multitaskerSpawn(&execProgram, "exec program", 1,
				(void *[]){ command } );
			if (pid < 0)
				error(_("Couldn't execute command \"%s\""), command);
			else
				while (multitaskerProcessIsAlive(pid));

			windowSwitchPointer(window, MOUSE_POINTER_DEFAULT);

			break;
		}

		case dirT:
			changeDir(theFile, fullName);
			break;

		case linkT:
			if (!strcmp(theFile->name, ".."))
				changeDir(theFile, fullName);
			break;

		default:
			break;
	}
}


static void initMenuContents(windowMenuContents *contents)
{
	int count;

	for (count = 0; count < contents->numItems; count ++)
	{
		strncpy(contents->items[count].text, _(contents->items[count].text),
			WINDOW_MAX_LABEL_LENGTH);
		contents->items[count].text[WINDOW_MAX_LABEL_LENGTH - 1] = '\0';
	}
}


static void refreshMenuContents(windowMenuContents *contents)
{
	int count;

	initMenuContents(contents);

	for (count = 0; count < contents->numItems; count ++)
		windowComponentSetData(contents->items[count].key,
			contents->items[count].text, strlen(contents->items[count].text),
			(count == (contents->numItems - 1)));
}


static void refreshWindow(void)
{
	// We got a 'window refresh' event (probably because of a language switch),
	// so we need to update things

	// Re-get the language setting
	setlocale(LC_ALL, getenv(ENV_LANG));
	textdomain("filebrowse");

	// Re-get the character set
	if (getenv(ENV_CHARSET))
		windowSetCharSet(window, getenv(ENV_CHARSET));

	// Refresh the 'file' menu
	refreshMenuContents(&fileMenuContents);
	windowSetTitle(fileMenu, FILE_MENU);

	// Refresh the 'view' menu
	refreshMenuContents(&viewMenuContents);
	windowSetTitle(viewMenu, VIEW_MENU);

	// Refresh the window title
	windowSetTitle(window, WINDOW_TITLE);
}


static void eventHandler(objectKey key, windowEvent *event)
{
	// Check for window events.
	if (key == window)
	{
		// Check for window refresh
		if (event->type == EVENT_WINDOW_REFRESH)
			refreshWindow();

		// Check for the window being closed
		else if (event->type == EVENT_WINDOW_CLOSE)
			stop = 1;
	}

	// Check for 'file' menu events
	else if (key == fileMenuContents.items[FILEMENU_QUIT].key)
	{
		if (event->type & EVENT_SELECTION)
			stop = 1;
	}

	// Check for 'view' menu events
	else if (key == viewMenuContents.items[VIEWMENU_REFRESH].key)
	{
		if (event->type & EVENT_SELECTION)
			// Manual refresh request
			fileList->update(fileList);
	}

	// Check for events to be passed to the file list widget
	else if (key == fileList->key)
	{
		if ((event->type & EVENT_MOUSE_DOWN) || (event->type & EVENT_KEY_DOWN))
			windowComponentGetSelected(fileList->key,
				&dirStack[dirStackCurr].selected);

		fileList->eventHandler(fileList, event);
	}
}


static void handleMenuEvents(windowMenuContents *contents)
{
	int count;

	for (count = 0; count < contents->numItems; count ++)
		windowRegisterEventHandler(contents->items[count].key, &eventHandler);
}


static int constructWindow(const char *directory)
{
	int status = 0;
	componentParameters params;

	// Create a new window, with small, arbitrary size and location
	window = windowNew(processId, WINDOW_TITLE);
	if (!window)
		return (status = ERR_NOTINITIALIZED);

	memset(&params, 0, sizeof(componentParameters));

	// Create the top menu bar
	objectKey menuBar = windowNewMenuBar(window, &params);

	// Create the top 'file' menu
	initMenuContents(&fileMenuContents);
	fileMenu = windowNewMenu(window, menuBar, FILE_MENU, &fileMenuContents,
		&params);
	handleMenuEvents(&fileMenuContents);

	// Create the top 'view' menu
	initMenuContents(&viewMenuContents);
	viewMenu = windowNewMenu(window, menuBar, VIEW_MENU, &viewMenuContents,
		&params);
	handleMenuEvents(&viewMenuContents);

	params.gridWidth = 1;
	params.gridHeight = 1;
	params.padTop = 5;
	params.padLeft = 5;
	params.padRight = 5;
	params.orientationX = orient_center;
	params.orientationY = orient_middle;

	// Create the location text field
	locationField = windowNewTextField(window, 40, &params);
	windowComponentSetData(locationField, (char *) directory,
		strlen(directory), 1 /* redraw */);
	windowRegisterEventHandler(locationField, &eventHandler);
	windowComponentSetEnabled(locationField, 0); // For now

	// Create the file list widget
	params.gridY += 1;
	params.padBottom = 5;
	fileList = windowNewFileList(window, windowlist_icononly, 5, 8, directory,
		WINFILEBROWSE_ALL, doFileSelection, &params);
	if (!fileList)
		return (status = ERR_NOTINITIALIZED);

	windowRegisterEventHandler(fileList->key, &eventHandler);
	windowComponentFocus(fileList->key);

	// Register an event handler to catch window close events
	windowRegisterEventHandler(window, &eventHandler);

	windowSetVisible(window, 1);

	return (status = 0);
}


int main(int argc, char *argv[])
{
	int status = 0;
	int guiThreadPid = 0;
	file cwdFile;

	setlocale(LC_ALL, getenv(ENV_LANG));
	textdomain("filebrowse");

	// Only work in graphics mode
	if (!graphicsAreEnabled())
	{
		fprintf(stderr, _("\nThe \"%s\" command only works in graphics mode\n"),
			(argc? argv[0] : ""));
		return (status = ERR_NOTINITIALIZED);
	}

	// What is my process id?
	processId = multitaskerGetCurrentProcessId();

	// What is my privilege level?
	privilege = multitaskerGetProcessPrivilege(processId);

	dirStack = malloc(MAX_PATH_LENGTH * sizeof(dirRecord));
	if (!dirStack)
	{
		error("%s", _("Memory allocation error"));
		status = ERR_MEMORY;
		goto out;
	}

	// Set the starting directory.  If one was specified on the command line,
	// try to use that.  Otherwise, default to '/'

	strcpy(dirStack[dirStackCurr].name, "/");
	if (argc > 1)
		fileFixupPath(argv[argc - 1], dirStack[dirStackCurr].name);

	status = multitaskerSetCurrentDirectory(dirStack[dirStackCurr].name);
	if (status < 0)
	{
		error(_("Can't change to directory \"%s\""),
			dirStack[dirStackCurr].name);

		status = multitaskerGetCurrentDirectory(dirStack[dirStackCurr].name,
			MAX_PATH_LENGTH);
		if (status < 0)
		{
			error("%s", _("Can't determine current directory"));
			goto out;
		}
	}

	status = constructWindow(dirStack[dirStackCurr].name);
	if (status < 0)
		goto out;

	// Run the GUI as a thread because we want to keep checking for directory
	// updates.
	guiThreadPid = windowGuiThread();

	if (fileFind(dirStack[dirStackCurr].name, &cwdFile) >= 0)
		cwdModified = mktime(&cwdFile.modified);

	// Loop, looking for changes in the current directory
	while (!stop && multitaskerProcessIsAlive(guiThreadPid))
	{
		while (lockGet(&dirStackLock) < 0)
			multitaskerYield();

		if (fileFind(dirStack[dirStackCurr].name, &cwdFile) >= 0)
		{
			if (mktime(&cwdFile.modified) != cwdModified)
			{
				fileList->update(fileList);
				windowComponentSetSelected(fileList->key,
					dirStack[dirStackCurr].selected);

				cwdModified = mktime(&cwdFile.modified);
			}

			lockRelease(&dirStackLock);
		}
		else
		{
			// Filesystem unmounted or something?  Quit.
			lockRelease(&dirStackLock);
			break;
		}

		multitaskerYield();
	}

	// We're back.
	status = 0;

out:
	windowGuiStop();

	if (fileList)
		fileList->destroy(fileList);

	if (window)
		windowDestroy(window);

	if (dirStack)
		free(dirStack);

	return (status);
}

