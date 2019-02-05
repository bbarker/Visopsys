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
//  iconwin.c
//

// This is a generic program for creating customized graphical windows
// with just a text configuration file.

/* This is the text that appears when a user requests help about this program
<help>

 -- iconwin --

A program for displaying custom icon windows.

Usage:
  iconwin <config_file>

The iconwin program is interactive, and may only be used in graphics mode.
It creates a window with icons, as specified in the named configuration file.
The 'Administration' icon, for example, on the default Visopsys desktop
uses the iconwin program to display the relevant administration tasks with
custom icons for each.

</help>
*/

#include <errno.h>
#include <libgen.h>
#include <libintl.h>
#include <locale.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/api.h>
#include <sys/ascii.h>
#include <sys/deskconf.h>
#include <sys/env.h>
#include <sys/paths.h>
#include <sys/vsh.h>
#include <sys/window.h>

#define _(string) gettext(string)

#define DEFAULT_ROWS		4
#define DEFAULT_COLUMNS		5
#define EXECICON_FILE		PATH_SYSTEM_ICONS "/execable.ico"

typedef struct {
	char name[WINDOW_MAX_LABEL_LENGTH + 1];
	char imageFile[MAX_PATH_NAME_LENGTH];
	char command[MAX_PATH_NAME_LENGTH];

} iconInfo;

static const char *configFile = NULL;
static int processId = 0;
static int privilege = 0;
static char windowTitle[WINDOW_MAX_TITLE_LENGTH];
static int rows = 0;
static int columns = 0;
static int numIcons = 0;
static listItemParameters *iconParams = NULL;
static iconInfo *icons = NULL;
static objectKey window = NULL;
static objectKey iconList = NULL;


__attribute__((format(printf, 1, 2)))
static void error(const char *format, ...)
{
	// Generic error message code

	va_list list;
	char output[MAXSTRINGLENGTH];

	va_start(list, format);
	vsnprintf(output, MAXSTRINGLENGTH, format, list);
	va_end(list);

	windowNewErrorDialog(window, _("Error"), output);
}


static int readConfig(const char *fileName, variableList *config)
{
	int status = 0;
	char langFileName[MAX_PATH_NAME_LENGTH];
	variableList langConfig;
	const char *variable = NULL;
	const char *value = NULL;
	int count;

	// Try to read the standard configuration file
	status = configRead(fileName, config);
	if (status < 0)
	{
		error(_("Can't locate configuration file %s"), fileName);
		return (status);
	}

	// If the 'LANG' environment variable is set, see whether there's another
	// language-specific config file that matches it.
	if (getenv(ENV_LANG))
	{
		sprintf(langFileName, "%s/%s/%s", PATH_SYSTEM_CONFIG,
			getenv(ENV_LANG), basename((char *) fileName));

		status = fileFind(langFileName, NULL);
		if (status >= 0)
		{
			status = configRead(langFileName, &langConfig);
			if (status >= 0)
			{
				// We got one.  Override values in the original.
				for (count = 0; count < langConfig.numVariables; count ++)
				{
					variable = variableListGetVariable(&langConfig, count);
					if (variable)
					{
						value = variableListGet(&langConfig, variable);
						if (value)
							variableListSet(config, variable, value);
					}
				}
			}
		}
	}

	return (status = 0);
}


static int processConfig(variableList *config)
{
	// window.title=xxx
	// list.rows=xxx
	// list.columns=xxx
	// icon.name.xxx=<text to display>
	// icon.xxx.image=<icon image>
	// icon.xxx.command=<command to run>

	int status = 0;
	const char *variable = NULL;
	const char *value = NULL;
	const char *name = NULL;
	char fullCommand[MAX_PATH_NAME_LENGTH];
	char tmp[128];
	int count;

	// Is the window title specified?
	value = variableListGet(config, "window.title");
	if (value)
		strncpy(windowTitle, value, WINDOW_MAX_TITLE_LENGTH);
	else
		strcpy(windowTitle, _("Icon Window"));

	// Are the number of rows specified?
	rows = DEFAULT_ROWS;
	value = variableListGet(config, "list.rows");
	if (value && (atoi(value) > 0))
		rows = atoi(value);

	// Are the number of columns specified?
	columns = DEFAULT_COLUMNS;
	value = variableListGet(config, "list.columns");
	if (value && (atoi(value) > 0))
		columns = atoi(value);

	// Figure out how many icons we *might* have
	for (count = 0; count < config->numVariables; count ++)
	{
		variable = variableListGetVariable(config, count);
		if (variable && !strncmp(variable, DESKVAR_ICON_NAME,
			strlen(DESKVAR_ICON_NAME)))
		{
			numIcons += 1;
		}
	}

	// Allocate memory for our list of listItemParameters structures and the
	// commands for each icon
	if (iconParams)
	{
		free(iconParams);
		iconParams = NULL;
	}

	if (icons)
	{
		free(icons);
		icons = NULL;
	}

	if (numIcons)
	{
		iconParams = malloc(numIcons * sizeof(listItemParameters));
		icons = malloc(numIcons * sizeof(iconInfo));
		if (!iconParams || !icons)
		{
			error("%s", _("Memory allocation error"));
			return (status = ERR_MEMORY);
		}
	}

	// Try to gather the information for the icons
	numIcons = 0;
	for (count = 0; count < config->numVariables; count ++)
	{
		variable = variableListGetVariable(config, count);
		if (variable && !strncmp(variable, DESKVAR_ICON_NAME,
			strlen(DESKVAR_ICON_NAME)))
		{
			name = (variable + 10);

			// Get the text
			value = variableListGet(config, variable);
			strncpy(icons[numIcons].name, name, WINDOW_MAX_LABEL_LENGTH);
			strncpy(iconParams[numIcons].text, gettext(value),
				WINDOW_MAX_LABEL_LENGTH);

			// Get the image name
			sprintf(tmp, DESKVAR_ICON_IMAGE, name);
			value = variableListGet(config, tmp);
			if (value)
			{
				strncpy(icons[numIcons].imageFile, value,
					MAX_PATH_NAME_LENGTH);
			}

			if (!value || (fileFind(icons[numIcons].imageFile, NULL) < 0) ||
				(imageLoad(icons[numIcons].imageFile, 64, 64,
					&iconParams[numIcons].iconImage) < 0))
			{
				// Try the standard 'program' icon
				if ((fileFind(EXECICON_FILE, NULL) < 0) ||
					(imageLoad(EXECICON_FILE, 64, 64,
						&iconParams[numIcons].iconImage) < 0))
				{
					// Can't load an icon.  We won't be showing this one.
					continue;
				}
			}

			// Get the command string
			sprintf(tmp, DESKVAR_ICON_COMMAND, name);
			value = variableListGet(config, tmp);
			if (value)
				strncpy(icons[numIcons].command, value, MAX_PATH_NAME_LENGTH);
			else
				// Can't get the command.  We won't be showing this one.
				continue;

			strncpy(fullCommand, icons[numIcons].command,
				MAX_PATH_NAME_LENGTH);

			// See whether the command exists
			if (loaderCheckCommand(fullCommand) < 0)
				// Command doesn't exist.  We won't be showing this one.
				continue;

			// OK.
			numIcons += 1;
		}
	}

	return (status = 0);
}


static void execProgram(int argc, char *argv[])
{
	windowSwitchPointer(window, MOUSE_POINTER_BUSY);

	// Exec the command, no block
	if (argc == 2)
		loaderLoadAndExec(argv[1], privilege, 0);

	windowSwitchPointer(window, MOUSE_POINTER_DEFAULT);
	multitaskerTerminate(0);
}


static void refreshWindow(void)
{
	// We got a 'window refresh' event (probably because of a language
	// switch), so we need to update things

	variableList tmpConfig;
	const char *variable = NULL;
	const char *value = NULL;
	const char *name = NULL;
	int count1, count2;

	// Re-get the language setting
	setlocale(LC_ALL, getenv(ENV_LANG));
	textdomain("iconwin");

	// Re-get the character set
	if (getenv(ENV_CHARSET))
		windowSetCharSet(window, getenv(ENV_CHARSET));

	memset(&tmpConfig, 0, sizeof(variableList));

	// Try to read the config file(s)
	if (readConfig(configFile, &tmpConfig) >= 0)
	{
		value = variableListGet(&tmpConfig, "window.title");
		if (value)
			strncpy(windowTitle, value,	WINDOW_MAX_TITLE_LENGTH);

		// Refresh the window title
		windowSetTitle(window, windowTitle);

		// Update the icons
		for (count1 = 0; count1 < tmpConfig.numVariables; count1 ++)
		{
			variable = variableListGetVariable(&tmpConfig, count1);
			if (variable && !strncmp(variable, DESKVAR_ICON_NAME,
				strlen(DESKVAR_ICON_NAME)))
			{
				name = (variable + 10);

				for (count2 = 0; count2 < numIcons; count2 ++)
				{
					if (!strncmp(icons[count2].name, name,
						WINDOW_MAX_LABEL_LENGTH))
					{
						// Get the text
						value = variableListGet(&tmpConfig, variable);

						// Set the new (localized) icon text
						strncpy(iconParams[count2].text, gettext(value),
							WINDOW_MAX_LABEL_LENGTH);

						break;
					}
				}
			}
		}

		windowComponentSetData(iconList, iconParams, numIcons, 1 /* redraw */);

		variableListDestroy(&tmpConfig);
	}
}


static void eventHandler(objectKey key, windowEvent *event)
{
	int clickedIcon = -1;

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

	// Check for events in our icon list.  We consider the icon 'clicked'
	// if it is a mouse click selection, or an ENTER key selection
	else if ((key == iconList) && (event->type & EVENT_SELECTION) &&
		((event->type & EVENT_MOUSE_LEFTUP) ||
		((event->type & EVENT_KEY_DOWN) && (event->key == keyEnter))))
	{
		// Get the selected item
		windowComponentGetSelected(iconList, &clickedIcon);
		if (clickedIcon < 0)
			return;

		if (multitaskerSpawn(&execProgram, "exec program", 1,
			(void *[]){ icons[clickedIcon].command } ) < 0)
		{
			error(_("Couldn't execute command \"%s\""),
				icons[clickedIcon].command);
		}
	}
}


static int constructWindow(void)
{
	int status = 0;
	componentParameters params;

	// Create a new window, with small, arbitrary size and location
	window = windowNew(processId, windowTitle);
	if (!window)
		return (status = ERR_NOTINITIALIZED);

	memset(&params, 0, sizeof(componentParameters));
	params.gridWidth = 1;
	params.gridHeight = 1;
	params.padTop = 5;
	params.padBottom = 5;
	params.padLeft = 5;
	params.padRight = 5;
	params.orientationX = orient_center;
	params.orientationY = orient_middle;

	// Create a window list to hold the icons
	iconList = windowNewList(window, windowlist_icononly, rows, columns, 0,
		iconParams, numIcons, &params);
	windowRegisterEventHandler(iconList, &eventHandler);
	windowComponentFocus(iconList);

	// Register an event handler to catch window close events
	windowRegisterEventHandler(window, &eventHandler);

	windowSetVisible(window, 1);

	return (status = 0);
}


static void deallocateMemory(void)
{
	int count;

	if (iconParams)
	{
		for (count = 0; count < numIcons; count ++)
			imageFree(&iconParams[count].iconImage);
		free(iconParams);
	}

	if (icons)
		free(icons);
}


int main(int argc, char *argv[])
{
	int status = 0;
	variableList config;

	setlocale(LC_ALL, getenv(ENV_LANG));
	textdomain("iconwin");

	// Only work in graphics mode
	if (!graphicsAreEnabled())
	{
		fprintf(stderr, _("\nThe \"%s\" command only works in graphics "
			"mode\n"), (argc? argv[0] : ""));
		return (errno = ERR_NOTINITIALIZED);
	}

	// What is my process id?
	processId = multitaskerGetCurrentProcessId();

	// What is my privilege level?
	privilege = multitaskerGetProcessPrivilege(processId);

	// Make sure our config file has been specified
	if (argc != 2)
	{
		printf(_("usage:\n%s <config_file>\n"), (argc? argv[0] : ""));
		return (errno = ERR_INVALID);
	}

	configFile = argv[argc - 1];

	memset(&config, 0, sizeof(variableList));

	// Try to read the config file(s)
	status = readConfig(configFile, &config);
	if (status < 0)
	{
		variableListDestroy(&config);
		deallocateMemory();
		return (errno = status);
	}

	// Process the configuration
	status = processConfig(&config);

	variableListDestroy(&config);

	if (status < 0)
	{
		deallocateMemory();
		return (errno = status);
	}

	// Make sure there were some icons successfully specified.
	if (numIcons <= 0)
	{
		error(_("Config file %s specifies no valid icons"), argv[argc - 1]);
		return (errno = ERR_INVALID);
	}

	status = constructWindow();
	if (status < 0)
	{
		deallocateMemory();
		return (errno = status);
	}

	// Run the GUI
	windowGuiRun();

	// We're back.
	windowDestroy(window);

	// Deallocate memory
	deallocateMemory();

	return (status = 0);
}

