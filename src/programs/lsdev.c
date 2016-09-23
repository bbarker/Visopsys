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
//  lsdev.c
//

// Displays a tree of the system's hardware devices.

/* This is the text that appears when a user requests help about this program
<help>

 -- lsdev --

Display devices.

Usage:
  lsdev [-T]

This command will show a listing of the system's hardware devices.

Options:
-T              : Force text mode operation

</help>
*/

#include <libintl.h>
#include <locale.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/api.h>
#include <sys/env.h>
#include <sys/font.h>
#include <sys/paths.h>

#define _(string) gettext(string)

#define WINDOW_TITLE		_("System Device Information")

static int graphics = 0;
static objectKey window = NULL;
static objectKey tree = NULL;
static windowTreeItem *treeItems = NULL;


__attribute__((noreturn))
static void quit(int status)
{
	if (graphics)
	{
		windowGuiStop();

		if (window)
			windowDestroy(window);
	}

	exit(status);
}


static void refreshWindow(void)
{
	// We got a 'window refresh' event (probably because of a language switch),
	// so we need to update things

	// Re-get the language setting
	setlocale(LC_ALL, getenv(ENV_LANG));
	textdomain("lsdev");

	// Re-get the character set
	if (getenv(ENV_CHARSET))
		windowSetCharSet(window, getenv(ENV_CHARSET));

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
			quit(0);
	}
}


static void constructWindow(void)
{
	componentParameters params;

	// Create a new window
	window = windowNew(multitaskerGetCurrentProcessId(), WINDOW_TITLE);
	if (!window)
		return;

	// Create a tree to show our stuff
	memset(&params, 0, sizeof(componentParameters));
	params.gridWidth = 1;
	params.gridHeight = 1;
	params.padLeft = 1;
	params.padRight = 1;
	params.padTop = 1;
	params.padBottom = 1;
	params.orientationX = orient_center;
	params.orientationY = orient_middle;
	tree = windowNewTree(window, NULL, 600, 400, &params);
	if (!tree)
		quit(ERR_NOCREATE);

	// Make sure it has the focus
	windowComponentFocus(tree);

	// Register an event handler to catch window close events
	windowRegisterEventHandler(window, &eventHandler);

	return;
}


static int makeItemsRecursive(device *dev, windowTreeItem *item)
{
	int status = 0;
	const char *vendor = NULL;
	const char *model = NULL;
	windowTreeItem *childItem = NULL;
	const char *variable = NULL;
	const char *value = NULL;
	device child;
	int count;

	// Construct the main item string for this device

	item->text[0] = '\0';

	vendor = variableListGet(&dev->attrs, DEVICEATTRNAME_VENDOR);
	model = variableListGet(&dev->attrs, DEVICEATTRNAME_MODEL);

	if (vendor || model)
	{
		if (vendor && vendor[0] && model && model[0])
			sprintf(item->text, "\"%s %s\" ", vendor, model);
		else if (vendor && vendor[0])
			sprintf(item->text, "\"%s\" ", vendor);
		else if (model && model[0])
			sprintf(item->text, "\"%s\" ", model);
	}

	if (dev->subClass.name[0])
		sprintf((item->text + strlen(item->text)), "%s ", dev->subClass.name);

	strcat(item->text, dev->devClass.name);

	// Add any additional attributes
	for (count = 0; count < dev->attrs.numVariables; count ++)
	{
		variable = variableListGetVariable(&dev->attrs, count);

		if (strcmp(variable, DEVICEATTRNAME_VENDOR) &&
			strcmp(variable, DEVICEATTRNAME_MODEL))
		{
			value = variableListGet(&dev->attrs, variable);
			if (value)
			{
				if (childItem)
				{
					childItem->next = malloc(sizeof(windowTreeItem));
					childItem = childItem->next;
				}
				else
				{
					childItem = malloc(sizeof(windowTreeItem));
					item->firstChild = childItem;
				}

				if (!childItem)
					return (status = ERR_MEMORY);

				sprintf(childItem->text, "%s=%s", variable, value);
				childItem->subItem = 1;
			}
		}
	}

	if (deviceTreeGetChild(dev, &child) >= 0)
	{
		if (childItem)
		{
			childItem->next = malloc(sizeof(windowTreeItem));
			childItem = childItem->next;
		}
		else
		{
			childItem = malloc(sizeof(windowTreeItem));
			item->firstChild = childItem;
		}

		if (!childItem)
			return (status = ERR_MEMORY);

		makeItemsRecursive(&child, childItem);
	}

	if (deviceTreeGetNext(dev) >= 0)
	{
		item->next = malloc(sizeof(windowTreeItem));
		if (!item->next)
			return (status = ERR_MEMORY);

		makeItemsRecursive(dev, item->next);
	}

	return (status = 0);
}


static void printTreeRecursive(windowTreeItem *item, int level)
{
	int count;

	for (count = 0; count < level; count ++)
		printf("   ");

	printf("%s%s\n", (item->subItem? "- " : ""), item->text);

	if (item->firstChild)
		printTreeRecursive(item->firstChild, (level + 1));

	if (item->next)
		printTreeRecursive(item->next, level);

	return;
}


__attribute__((noreturn))
int main(int argc, char *argv[])
{
	int status = 0;
	char opt;
	device dev;

	setlocale(LC_ALL, getenv(ENV_LANG));
	textdomain("lsdev");

	// Are graphics enabled?
	graphics = graphicsAreEnabled();

	// Check options
	while (strchr("T?", (opt = getopt(argc, argv, "T"))))
	{
		switch (opt)
		{
			case 'T':
				// Force text mode
				graphics = 0;
				break;

			default:
				fprintf(stderr, _("Unknown option '%c'\n"), optopt);
				quit(status = ERR_INVALID);
		}
	}

	if (graphics)
		constructWindow();

	status = deviceTreeGetRoot(&dev);
	if (status < 0)
		quit(status);

	treeItems = malloc(sizeof(windowTreeItem));
	if (!treeItems)
		quit(status = ERR_MEMORY);

	status = makeItemsRecursive(&dev, treeItems);
	if (status < 0)
		quit(status);

	// Expand the first 'system' tree item
	treeItems->expanded = 1;

	if (graphics)
	{
		windowComponentSetData(tree, treeItems, sizeof(windowTreeItem),
			1 /* render */);
		windowSetVisible(window, 1);
		windowGuiRun();
	}
	else
	{
		printTreeRecursive(treeItems, 0);
		printf("\n");
	}

	quit(0);
}

