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
//  kernelWindowShell.c
//

// This is the code that manages the 'root' window in the GUI environment.

#include "kernelWindow.h"
#include "kernelDebug.h"
#include "kernelEnvironment.h"
#include "kernelError.h"
#include "kernelFile.h"
#include "kernelGraphic.h"
#include "kernelImage.h"
#include "kernelLinkedList.h"
#include "kernelLoader.h"
#include "kernelLog.h"
#include "kernelMalloc.h"
#include "kernelMisc.h"
#include "kernelMultitasker.h"
#include "kernelParameters.h"
#include "kernelTouch.h"
#include "kernelUser.h"
#include "kernelVariableList.h"
#include "kernelWindowEventStream.h"
#include <locale.h>
#include <string.h>
#include <sys/desktop.h>
#include <sys/paths.h>

typedef struct {
	kernelWindowComponent *itemComponent;
	char command[MAX_PATH_NAME_LENGTH];
	kernelWindow *window;

} menuItemData;

typedef struct {
	int processId;
	kernelWindowComponent *component;

} menuBarComponent;

static volatile struct {
	char userName[USER_MAX_NAMELENGTH + 1];
	int privilege;
	int processId;
	kernelWindow *rootWindow;
	kernelWindowComponent *menuBar;
	kernelWindow **menus;
	int numMenus;
	kernelWindow *windowMenu;
	kernelLinkedList menuItemsList;
	kernelLinkedList winMenuItemsList;
	kernelLinkedList menuBarCompsList;
	kernelWindowComponent **icons;
	int numIcons;
	kernelWindow **windowList;
	int numberWindows;
	int refresh;

} shellData;

extern kernelWindowVariables *windowVariables;


static void menuEvent(kernelWindowComponent *component, windowEvent *event)
{
	int status = 0;
	menuItemData *itemData = NULL;
	kernelLinkedListItem *iter = NULL;

	kernelDebug(debug_gui, "WindowShell taskbar menu event");

	if (event->type & EVENT_SELECTION)
	{
		kernelDebug(debug_gui, "WindowShell taskbar menu selection");

		itemData = kernelLinkedListIterStart((kernelLinkedList *)
			&shellData.menuItemsList, &iter);

		while (itemData)
		{
			if (component == itemData->itemComponent)
			{
				if (itemData->command[0])
				{
					kernelWindowSwitchPointer(shellData.rootWindow,
						MOUSE_POINTER_BUSY);

					// Run the command, no block
					status = kernelLoaderLoadAndExec(itemData->command,
						shellData.privilege, 0);

					kernelWindowSwitchPointer(shellData.rootWindow,
						MOUSE_POINTER_DEFAULT);

					if (status < 0)
						kernelError(kernel_error, "Unable to execute program "
							"%s", itemData->command);
				}

				break;
			}

			itemData = kernelLinkedListIterNext((kernelLinkedList *)
				&shellData.menuItemsList, &iter);
		}
	}

	return;
}


static void iconEvent(kernelWindowComponent *component, windowEvent *event)
{
	int status = 0;
	kernelWindowIcon *iconComponent = component->data;
	static int dragging = 0;

	if (event->type & EVENT_MOUSE_DRAG)
		dragging = 1;

	else if (event->type & EVENT_MOUSE_LEFTUP)
	{
		if (dragging)
		{
			// Drag is finished
			dragging = 0;
			return;
		}

		kernelDebug(debug_gui, "WindowShell icon mouse click");

		kernelWindowSwitchPointer(shellData.rootWindow, MOUSE_POINTER_BUSY);

		// Run the command
		status = kernelLoaderLoadAndExec((const char *) iconComponent->command,
			shellData.privilege, 0 /* no block */);

		kernelWindowSwitchPointer(shellData.rootWindow, MOUSE_POINTER_DEFAULT);

		if (status < 0)
			kernelError(kernel_error, "Unable to execute program %s",
				iconComponent->command);
	}

	return;
}


static int readFileConfig(const char *fileName, variableList *settings)
{
	// Return a (possibly empty) variable list, filled with any desktop
	// settings we read from various config files.

	int status = 0;

	kernelDebug(debug_gui, "WindowShell read configuration %s", fileName);

	status = kernelFileFind(fileName, NULL);
	if (status < 0)
		return (status);

	status = kernelConfigRead(fileName, settings);

	return (status);
}


static int readConfig(variableList *settings)
{
	// Return a (possibly empty) variable list, filled with any desktop
	// settings we read from various config files.

	int status = 0;
	char fileName[MAX_PATH_NAME_LENGTH];
	variableList userConfig;
	const char *variable = NULL;
	const char *value = NULL;
	char language[LOCALE_MAX_NAMELEN + 1];
	variableList langConfig;
	int count;

	kernelDebug(debug_gui, "WindowShell read configuration");

	memset(&userConfig, 0, sizeof(variableList));
	memset(&langConfig, 0, sizeof(variableList));

	// First try to read the system desktop config.
	status = readFileConfig(PATH_SYSTEM_CONFIG "/" DESKTOP_CONFIGFILE,
		settings);
	if (status < 0)
	{
		// Argh.  No file?  Create an empty list for us to use
		status = kernelVariableListCreate(settings);
		if (status < 0)
			return (status);
	}

	if (strcmp((char *) shellData.userName, USER_ADMIN))
	{
		// Try to read any user-specific desktop config.
		sprintf(fileName, PATH_USERS_CONFIG "/" DESKTOP_CONFIGFILE,
			shellData.userName);

		status = readFileConfig(fileName, &userConfig);
		if (status >= 0)
		{
			// We got one.  Override values.
			for (count = 0; count < userConfig.numVariables; count ++)
			{
				variable = kernelVariableListGetVariable(&userConfig, count);
				if (variable)
				{
					value = kernelVariableListGet(&userConfig, variable);
					if (value)
						kernelVariableListSet(settings, variable, value);
				}
			}
		}
	}

	// If the 'LANG' environment variable is set, see whether there's another
	// language-specific desktop config file that matches it.
	status = kernelEnvironmentGet(ENV_LANG, language, LOCALE_MAX_NAMELEN);
	if (status >= 0)
	{
		sprintf(fileName, "%s/%s/%s", PATH_SYSTEM_CONFIG, language,
			DESKTOP_CONFIGFILE);

		status = kernelFileFind(fileName, NULL);
		if (status >= 0)
		{
			status = kernelConfigRead(fileName, &langConfig);
			if (status >= 0)
			{
				// We got one.  Override values.
				for (count = 0; count < langConfig.numVariables; count ++)
				{
					variable = kernelVariableListGetVariable(&langConfig,
						count);
					if (variable)
					{
						value = kernelVariableListGet(&langConfig, variable);
						if (value)
							kernelVariableListSet(settings, variable, value);
					}
				}
			}
		}
	}

	return (status = 0);
}


static int makeMenuBar(variableList *settings)
{
	// Make a menu bar at the top

	int status = 0;
	const char *variable = NULL;
	const char *value = NULL;
	const char *menuName = NULL;
	const char *menuLabel = NULL;
	kernelWindow *menu = NULL;
	char propertyName[128];
	const char *itemName = NULL;
	const char *itemLabel = NULL;
	menuItemData *itemData = NULL;
	componentParameters params;
	int count1, count2;

	kernelDebug(debug_gui, "WindowShell make menu bar");

	memset(&params, 0, sizeof(componentParameters));
	params.foreground.red = 255;
	params.foreground.green = 255;
	params.foreground.blue = 255;
	params.background.red = windowVariables->color.foreground.red;
	params.background.green = windowVariables->color.foreground.green;
	params.background.blue = windowVariables->color.foreground.blue;
	params.flags |= (WINDOW_COMPFLAG_CUSTOMFOREGROUND |
		WINDOW_COMPFLAG_CUSTOMBACKGROUND);
	params.font = windowVariables->font.varWidth.medium.font;

	shellData.menuBar = kernelWindowNewMenuBar(shellData.rootWindow, &params);

	// Try to load menu bar menus and menu items

	// Loop for variables starting with DESKTOP_TASKBAR_MENU
	for (count1 = 0; count1 < settings->numVariables; count1 ++)
	{
		variable = kernelVariableListGetVariable(settings, count1);
		if (variable && !strncmp(variable, DESKTOP_TASKBAR_MENU,
			strlen(DESKTOP_TASKBAR_MENU)))
		{
			menuName = (variable + 13);
			menuLabel = kernelVariableListGet(settings, variable);

			menu = kernelWindowNewMenu(shellData.rootWindow, shellData.menuBar,
				menuLabel, NULL, &params);

			// Add it to our list
			shellData.menus = kernelRealloc(shellData.menus,
				((shellData.numMenus + 1) * sizeof(kernelWindow *)));
			if (!shellData.menus)
				return (status = ERR_MEMORY);

			shellData.menus[shellData.numMenus++] = menu;

			// Now loop and get any components for this menu
			for (count2 = 0; count2 < settings->numVariables; count2 ++)
			{
				sprintf(propertyName, DESKTOP_TASKBAR_MENUITEM, menuName);

				variable = kernelVariableListGetVariable(settings, count2);
				if (!strncmp(variable, propertyName, strlen(propertyName)))
				{
					itemName = (variable + strlen(propertyName));
					itemLabel = kernelVariableListGet(settings, variable);

					if (!itemLabel)
						continue;

					// See if there's an associated command
					sprintf(propertyName, DESKTOP_TASKBAR_MENUITEM_COMMAND,
						menuName, itemName);
					value = kernelVariableListGet(settings, propertyName);
					if (!value || (kernelLoaderCheckCommand(value) < 0))
						// No such command.  Don't show this one.
						continue;

					// Get memory for menu item data
					itemData = kernelMalloc(sizeof(menuItemData));
					if (!itemData)
						continue;

					// Create the menu item
					itemData->itemComponent = kernelWindowNewMenuItem(menu,
						itemLabel, &params);
					if (!itemData->itemComponent)
					{
						kernelFree(itemData);
						continue;
					}

					strncpy(itemData->command, value, MAX_PATH_NAME_LENGTH);

					// Add it to our list
					status = kernelLinkedListAdd((kernelLinkedList *)
						&shellData.menuItemsList, itemData);
					if (status < 0)
					{
						kernelWindowComponentDestroy(itemData->itemComponent);
						kernelFree(itemData);
						return (status = ERR_MEMORY);
					}

					kernelWindowRegisterEventHandler(itemData->itemComponent,
						&menuEvent);
				}
			}

			// We treat any 'window' menu specially, since it is not usually
			// populated at startup time, only as windows are created or
			// destroyed.
			if (!strcmp(menuName, DESKTOP_TASKBAR_WINDOWMENU))
			{
				kernelDebug(debug_gui, "WindowShell created window menu");
				shellData.windowMenu = menu;
			}
		}
	}

	kernelLog("Task menu initialized");
	return (status = 0);
}


static int makeIcons(variableList *settings)
{
	// Try to load icons

	int status = 0;
	const char *variable = NULL;
	const char *iconName = NULL;
	const char *iconLabel = NULL;
	char propertyName[128];
	const char *command = NULL;
	const char *imageFile = NULL;
	kernelWindowComponent *iconComponent = NULL;
	image tmpImage;
	componentParameters params;
	int count;

	kernelDebug(debug_gui, "WindowShell make icons");

	// These parameters are the same for all icons
	memset(&params, 0, sizeof(componentParameters));
	params.gridWidth = 1;
	params.gridHeight = 1;
	params.padLeft = 5;
	params.padRight = 5;
	params.padTop = 5;
	params.flags = (WINDOW_COMPFLAG_CUSTOMFOREGROUND |
		WINDOW_COMPFLAG_CUSTOMBACKGROUND | WINDOW_COMPFLAG_CANFOCUS |
		WINDOW_COMPFLAG_FIXEDWIDTH | WINDOW_COMPFLAG_FIXEDHEIGHT);
	memcpy(&params.foreground, &COLOR_WHITE, sizeof(color));
	memcpy(&params.background, &windowVariables->color.desktop, sizeof(color));
	params.orientationX = orient_center;
	params.orientationY = orient_middle;

	// Loop for variables starting with DESKTOP_ICON_NAME
	for (count = 0; count < settings->numVariables; count ++)
	{
		variable = kernelVariableListGetVariable(settings, count);
		if (variable && !strncmp(variable, DESKTOP_ICON_NAME,
			strlen(DESKTOP_ICON_NAME)))
		{
			iconName = (variable + strlen(DESKTOP_ICON_NAME));
			iconLabel = kernelVariableListGet(settings, variable);

			// Get the rest of the recognized properties for this icon.

			// See if there's a command associated with this, and make sure
			// the program exists.
			sprintf(propertyName, DESKTOP_ICON_COMMAND, iconName);
			command = kernelVariableListGet(settings, propertyName);
			if (!command || (kernelLoaderCheckCommand(command) < 0))
				continue;

			// Get the image name, make sure it exists, and try to load it.
			sprintf(propertyName, DESKTOP_ICON_IMAGE, iconName);
			imageFile = kernelVariableListGet(settings, propertyName);
			if (!imageFile || (kernelFileFind(imageFile, NULL) < 0) ||
				(kernelImageLoad(imageFile, 64, 64, &tmpImage) < 0))
			{
				continue;
			}

			params.gridY++;
			iconComponent = kernelWindowNewIcon(shellData.rootWindow,
				&tmpImage, iconLabel, &params);

			// Release the image memory
			kernelImageFree(&tmpImage);

			if (!iconComponent)
				continue;

			// Set the command
			strncpy((char *)((kernelWindowIcon *) iconComponent->data)->
				command, command, MAX_PATH_NAME_LENGTH);

			// Add this icon to our list
			shellData.icons = kernelRealloc((void *) shellData.icons,
				((shellData.numIcons + 1) * sizeof(kernelWindowComponent *)));
			if (!shellData.icons)
				return (status = ERR_MEMORY);

			shellData.icons[shellData.numIcons++] = iconComponent;

			// Register the event handler for the icon command execution
			kernelWindowRegisterEventHandler(iconComponent,	&iconEvent);
		}
	}

	// Snap the icons to a grid
	kernelWindowSnapIcons((objectKey) shellData.rootWindow);

	kernelLog("Desktop icons loaded");
	return (status = 0);
}


static int makeRootWindow(void)
{
	// Make a main root window to serve as the background for the window
	// environment

	int status = 0;
	variableList settings;
	const char *imageFile = NULL;
	image tmpImage;

	kernelDebug(debug_gui, "WindowShell make root window");

	// Get a new window
	shellData.rootWindow = kernelWindowNew(KERNELPROCID, WINNAME_ROOTWINDOW);
	if (!shellData.rootWindow)
		return (status = ERR_NOCREATE);

	// The window will have no border, title bar or close button, is not
	// movable or resizable, and we mark it as a root window
	shellData.rootWindow->flags &= ~(WINFLAG_MOVABLE | WINFLAG_RESIZABLE);
	shellData.rootWindow->flags |= WINFLAG_ROOTWINDOW;
	kernelWindowSetHasTitleBar(shellData.rootWindow, 0);
	kernelWindowSetHasBorder(shellData.rootWindow, 0);

	// Set our background color preference
	shellData.rootWindow->background.red =
		windowVariables->color.desktop.red;
	shellData.rootWindow->background.green =
		windowVariables->color.desktop.green;
	shellData.rootWindow->background.blue =
		windowVariables->color.desktop.blue;

	// Read the desktop config file(s)
	status = readConfig(&settings);
	if (status < 0)
		return (status);

	// Try to load the background image
	imageFile = kernelVariableListGet(&settings, DESKTOP_BACKGROUND);
	if (imageFile)
	{
		kernelDebug(debug_gui, "WindowShell loading background image \"%s\"",
			imageFile);

		if (strcmp(imageFile, DESKTOP_BACKGROUND_NONE))
		{
			if ((kernelFileFind(imageFile, NULL) >= 0) &&
				(kernelImageLoad(imageFile, 0, 0, &tmpImage) >= 0))
			{
				// Put the background image into our window.
				kernelWindowSetBackgroundImage(shellData.rootWindow,
					&tmpImage);
				kernelLog("Background image loaded");
			}
			else
			{
				kernelError(kernel_error, "Error loading background image %s",
					imageFile);
			}

			// Release the image memory
			kernelImageFree(&tmpImage);
		}
	}

	// Make the top menu bar
	status = makeMenuBar(&settings);
	if (status < 0)
	{
		kernelVariableListDestroy(&settings);
		return (status);
	}

	// Make icons
	status = makeIcons(&settings);
	if (status < 0)
	{
		kernelVariableListDestroy(&settings);
		return (status);
	}

	kernelVariableListDestroy(&settings);

	// Location in the top corner
	status = kernelWindowSetLocation(shellData.rootWindow, 0, 0);
	if (status < 0)
		return (status);

	// Resize to the whole screen
	status = kernelWindowSetSize(shellData.rootWindow,
		kernelGraphicGetScreenWidth(), kernelGraphicGetScreenHeight());
	if (status < 0)
		return (status);

	// The window is always at the bottom level
	shellData.rootWindow->level = WINDOW_MAXWINDOWS;

	kernelWindowSetVisible(shellData.rootWindow, 1);

	return (status = 0);
}


static void runPrograms(void)
{
	// Get any programs we're supposed to run automatically and run them.

	variableList settings;
	const char *variable = NULL;
	const char *programName = NULL;
	int count;

	kernelDebug(debug_gui, "WindowShell run programs");

	// Read the desktop config file(s)
	if (readConfig(&settings) < 0)
		return;

	// Loop for variables starting with DESKTOP_PROGRAM
	for (count = 0; count < settings.numVariables; count ++)
	{
		variable = kernelVariableListGetVariable(&settings, count);
		if (variable && !strncmp(variable, DESKTOP_PROGRAM,
			strlen(DESKTOP_PROGRAM)))
		{
			programName = kernelVariableListGet(&settings, variable);
			if (programName)
				// Try to run the program
				kernelLoaderLoadAndExec(programName, shellData.privilege, 0);
		}
	}

	kernelVariableListDestroy(&settings);

	// If touch support is available, we will also run the virtual keyboard
	// program in 'iconified' mode
	if (kernelTouchAvailable() &&
		(kernelFileFind(PATH_PROGRAMS "/keyboard", NULL) >= 0))
	{
		kernelLoaderLoadAndExec(PATH_PROGRAMS "/keyboard -i",
			shellData.privilege, 0);
	}

	return;
}


static void scanMenuItemEvents(kernelLinkedList *list)
{
	// Scan through events in a list of our menu items

	menuItemData *itemData = NULL;
	kernelLinkedListItem *iter = NULL;
	kernelWindowComponent *component = NULL;
	windowEvent event;

	itemData = kernelLinkedListIterStart(list, &iter);

	while (itemData)
	{
		component = itemData->itemComponent;

		// Any events pending?  Any event handler?
		if (component->eventHandler &&
			(kernelWindowEventStreamRead(&component->events, &event) > 0))
		{
			kernelDebug(debug_gui, "WindowShell root menu item got event");
			component->eventHandler(component, &event);
		}

		itemData = kernelLinkedListIterNext(list, &iter);
	}
}


static void scanContainerEvents(kernelWindowContainer *container)
{
	// Recursively scan through events in components of a container

	kernelWindowComponent *component = NULL;
	windowEvent event;
	int count;

	for (count = 0; count < container->numComponents; count ++)
	{
		component = container->components[count];

		// Any events pending?  Any event handler?
		if (component->eventHandler &&
			(kernelWindowEventStreamRead(&component->events, &event) > 0))
		{
			kernelDebug(debug_gui, "WindowShell scan container got event");
			component->eventHandler(component, &event);
		}

		// If this component is a container type, recurse
		if (component->type == containerComponentType)
			scanContainerEvents(component->data);
	}

	return;
}


static void destroy(void)
{
	menuItemData *itemData = NULL;
	kernelLinkedListItem *iter = NULL;
	int count;

	// Destroy icons
	for (count = 0; count < shellData.numIcons; count ++)
		kernelWindowComponentDestroy(shellData.icons[count]);

	shellData.numIcons = 0;
	kernelFree(shellData.icons);
	shellData.icons = NULL;

	// Don't destroy menu bar components, since they're 'owned' by other
	// processes.

	// Destroy (static) menu items

	itemData = kernelLinkedListIterStart((kernelLinkedList *)
		&shellData.menuItemsList, &iter);

	while (itemData)
	{
		kernelWindowComponentDestroy(itemData->itemComponent);
		kernelFree(itemData);
		itemData = kernelLinkedListIterNext((kernelLinkedList *)
			&shellData.menuItemsList, &iter);
	}

	kernelLinkedListClear((kernelLinkedList *) &shellData.menuItemsList);

	// Destroy window menu items

	itemData = kernelLinkedListIterStart((kernelLinkedList *)
		&shellData.winMenuItemsList, &iter);

	while (itemData)
	{
		kernelWindowComponentDestroy(itemData->itemComponent);
		kernelFree(itemData);
		itemData = kernelLinkedListIterNext((kernelLinkedList *)
			&shellData.winMenuItemsList, &iter);
	}

	kernelLinkedListClear((kernelLinkedList *) &shellData.winMenuItemsList);

	// Do this before destroying menus (because menus are windows, and
	// kernelWindowShellUpdateList() will get called)
	shellData.windowMenu = NULL;

	// Destroy menus (but not the window menu)
	for (count = 0; count < shellData.numMenus; count ++)
		kernelWindowDestroy(shellData.menus[count]);

	shellData.numMenus = 0;
	kernelFree(shellData.menus);
	shellData.menus = NULL;

	// Destroy the menu bar
	kernelWindowComponentDestroy(shellData.menuBar);
}


static void refresh(void)
{
	// Refresh the desktop environment

	variableList settings;
	windowEvent event;
	int count;

	kernelDebug(debug_gui, "WindowShell refresh");

	// Reload the user environment
	if (kernelEnvironmentLoad((char *) shellData.userName) >= 0)
		// Propagate it to all of our child processes
		kernelMultitaskerPropagateEnvironment(NULL);

	// Read the desktop config file(s)
	if (readConfig(&settings) >= 0)
	{
		// Get rid of all our existing stuff
		destroy();

		// Re-create the menu bar
		makeMenuBar(&settings);

		// Re-load the icons
		makeIcons(&settings);

		kernelVariableListDestroy(&settings);

		if (shellData.rootWindow)
		{
			// Re-do root window layout.  Don't use kernelWindowLayout() for
			// now, since it automatically re-sizes and messes things up
			kernelWindowSetVisible(shellData.rootWindow, 0);

			if (shellData.rootWindow->sysContainer &&
				shellData.rootWindow->sysContainer->layout)
			{
				shellData.rootWindow->sysContainer->
					layout(shellData.rootWindow->sysContainer);
			}

			if (shellData.rootWindow->mainContainer &&
				shellData.rootWindow->mainContainer->layout)
			{
				shellData.rootWindow->mainContainer->
					layout(shellData.rootWindow->mainContainer);
			}

			kernelWindowSetVisible(shellData.rootWindow, 1);
		}
	}

	// Send a 'window refresh' event to every window
	if (shellData.windowList)
	{
		memset(&event, 0, sizeof(windowEvent));
		event.type = EVENT_WINDOW_REFRESH;

		for (count = 0; count < shellData.numberWindows; count ++)
			kernelWindowEventStreamWrite(
				&shellData.windowList[count]->events, &event);
	}

	// Let them update
	kernelMultitaskerYield();

	// Update the window menu
	kernelWindowShellUpdateList(shellData.windowList, shellData.numberWindows);

	shellData.refresh = 0;
}


__attribute__((noreturn))
static void windowShellThread(void)
{
	// This thread runs as the 'window shell' to watch for window events on
	// 'root window' GUI components, and which functions as the user's login
	// shell in graphics mode.

	int status = 0;
	menuBarComponent *menuBarComp = NULL;
	kernelLinkedListItem *iter = NULL;

	// Create the root window
	status = makeRootWindow();
	if (status < 0)
		kernelMultitaskerTerminate(status);

	// Run any programs that we're supposed to run after login
	runPrograms();

	// Now loop and process any events
	while (1)
	{
		if (shellData.refresh)
			refresh();

		scanMenuItemEvents((kernelLinkedList *) &shellData.winMenuItemsList);
		scanMenuItemEvents((kernelLinkedList *) &shellData.menuItemsList);

		scanContainerEvents(((kernelWindowMenuBar *)
			shellData.menuBar->data)->container->data);

		scanContainerEvents(shellData.rootWindow->mainContainer->data);

		// Make sure the owners of any menu bar components are still alive

		menuBarComp = kernelLinkedListIterStart((kernelLinkedList *)
			&shellData.menuBarCompsList, &iter);

		while (menuBarComp)
		{
			if (!kernelMultitaskerProcessIsAlive(menuBarComp->processId))
				kernelWindowShellDestroyTaskbarComp(menuBarComp->component);

			menuBarComp = kernelLinkedListIterNext((kernelLinkedList *)
				&shellData.menuBarCompsList, &iter);
		}

		// Done
		kernelMultitaskerYield();
	}
}


static void windowMenuEvent(kernelWindowComponent *component,
	windowEvent *event)
{
	menuItemData *itemData = NULL;
	kernelLinkedListItem *iter = NULL;

	kernelDebug(debug_gui, "WindowShell taskbar window menu event");

	if (shellData.windowMenu && (event->type & EVENT_SELECTION))
	{
		kernelDebug(debug_gui, "WindowShell taskbar window menu selection");

		itemData = kernelLinkedListIterStart((kernelLinkedList *)
			&shellData.winMenuItemsList, &iter);

		while (itemData)
		{
			if (component == itemData->itemComponent)
			{
				// Restore it
				kernelDebug(debug_gui, "WindowShell restore window %s",
					itemData->window->title);
				kernelWindowSetMinimized(itemData->window, 0);

				// If it has a dialog box, restore that too
				if (itemData->window->dialogWindow)
					kernelWindowSetMinimized(itemData->window->dialogWindow,
						0);

				break;
			}

			itemData = kernelLinkedListIterNext((kernelLinkedList *)
				&shellData.winMenuItemsList, &iter);
		}
	}

	return;
}


static void updateMenuBarComponents(void)
{
	// Re-layout the menu bar
	if (shellData.menuBar->layout)
		shellData.menuBar->layout(shellData.menuBar);

	// Re-draw the menu bar
	if (shellData.menuBar->draw)
		shellData.menuBar->draw(shellData.menuBar);

	// Re-render the menu bar on screen
	shellData.rootWindow->update(shellData.rootWindow,
		shellData.menuBar->xCoord, shellData.menuBar->yCoord,
		shellData.menuBar->width, shellData.menuBar->height);
}


static int addMenuBarComponent(kernelWindowComponent *component)
{
	int status = 0;
	menuBarComponent *menuBarComp = NULL;

	// Add the component to the shell's list of menu bar components

	menuBarComp = kernelMalloc(sizeof(menuBarComponent));
	if (!menuBarComp)
		return (status = ERR_MEMORY);

	menuBarComp->processId = kernelMultitaskerGetCurrentProcessId();
	menuBarComp->component = component;

	status = kernelLinkedListAdd((kernelLinkedList *)
		&shellData.menuBarCompsList, menuBarComp);
	if (status < 0)
	{
		kernelFree(menuBarComp);
		return (status);
	}

	// Re-draw the menu bar
	updateMenuBarComponents();

	return (status = 0);
}


/////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////
//
// Below here, the functions are exported for external use
//
/////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////

int kernelWindowShell(const char *user)
{
	// Launch the window shell thread

	kernelDebug(debug_gui, "WindowShell start");

	// Check params
	if (!user)
		return (shellData.processId = ERR_NULLPARAMETER);

	memset((void *) &shellData, 0, sizeof(shellData));

	memcpy((char *) shellData.userName, user, USER_MAX_NAMELENGTH);
	shellData.privilege = kernelUserGetPrivilege((char *) shellData.userName);

	// Spawn the window shell thread
	shellData.processId = kernelMultitaskerSpawn(windowShellThread,
		"window shell", 0, NULL);

	return (shellData.processId);
}


void kernelWindowShellUpdateList(kernelWindow *list[], int number)
{
	// When the list of open windows has changed, the window environment can
	// call this function so we can update our taskbar.

	componentParameters params;
	menuItemData *itemData = NULL;
	kernelLinkedListItem *iter = NULL;
	process windowProcess;
	int count;

	// Check params
	if (!list)
		return;

	// If the window shell is dead, don't continue
	if (!kernelMultitaskerProcessIsAlive(shellData.processId))
		return;

	kernelDebug(debug_gui, "WindowShell update window list");

	shellData.windowList = list;
	shellData.numberWindows = number;

	if (shellData.windowMenu)
	{
		// Destroy all the menu items in the menu

		itemData = kernelLinkedListIterStart((kernelLinkedList *)
			&shellData.winMenuItemsList, &iter);

		while (itemData)
		{
			kernelWindowComponentDestroy(itemData->itemComponent);

			kernelFree(itemData);

			itemData = kernelLinkedListIterNext((kernelLinkedList *)
				&shellData.winMenuItemsList, &iter);
		}

		kernelLinkedListClear((kernelLinkedList *)
			&shellData.winMenuItemsList);

		// Copy the parameters from the menu to use
		memcpy(&params, (void *) &shellData.menuBar->params,
			sizeof(componentParameters));

		for (count = 0; count < shellData.numberWindows; count ++)
		{
			// Skip windows we don't want to include

			// Skip the root window
			if (shellData.windowList[count] == shellData.rootWindow)
				continue;

			// Skip any temporary console window
			if (!strcmp((char *) shellData.windowList[count]->title,
				WINNAME_TEMPCONSOLE))
			{
				continue;
			}

			// Skip any iconified windows
			if (shellData.windowList[count]->flags & WINFLAG_ICONIFIED)
				continue;

			// Skip child windows too
			if (shellData.windowList[count]->parentWindow)
				continue;

			itemData = kernelMalloc(sizeof(menuItemData));
			if (!itemData)
				return;

			itemData->itemComponent = kernelWindowNewMenuItem(
				shellData.windowMenu,
				(char *) shellData.windowList[count]->title, &params);
			itemData->window = shellData.windowList[count];

			kernelLinkedListAdd((kernelLinkedList *)
				&shellData.winMenuItemsList, itemData);

			kernelWindowRegisterEventHandler(itemData->itemComponent,
				&windowMenuEvent);
		}
	}

	// If any windows' parent processes are no longer alive, make the window
	// shell be its parent.
	for (count = 0; count < shellData.numberWindows; count ++)
	{
		if ((shellData.windowList[count] != shellData.rootWindow) &&
			(kernelMultitaskerGetProcess(shellData.windowList[count]->
				processId, &windowProcess) >= 0))
		{
			if ((windowProcess.type != proc_thread) &&
				!kernelMultitaskerProcessIsAlive(windowProcess.
					parentProcessId))
			{
				kernelMultitaskerSetProcessParent(
					shellData.windowList[count]->processId,
						shellData.processId);
			}
		}
	}
}


void kernelWindowShellRefresh(void)
{
	// This function tells the window shell to refresh everything.

	// This was implemented in order to facilitate instantaneous language
	// switching, but it can be expanded to cover more things (e.g. desktop
	// configuration, icons, menus, etc.)

	shellData.refresh = 1;
}


int kernelWindowShellTileBackground(const char *fileName)
{
	// This will tile the supplied image as the background image of the
	// root window

	int status = 0;
	image backgroundImage;

	// Make sure we have a root window
	if (!shellData.rootWindow)
		return (status = ERR_NOTINITIALIZED);

	// If the window shell is dead, don't continue
	if (!kernelMultitaskerProcessIsAlive(shellData.processId))
		return (status = ERR_NOSUCHPROCESS);

	if (fileName)
	{
		// Try to load the new background image
		status = kernelImageLoad(fileName, 0, 0, &backgroundImage);
		if (status < 0)
		{
			kernelError(kernel_error, "Error loading background image %s",
				fileName);
			return (status);
		}

		// Put the background image into our window.
		kernelWindowSetBackgroundImage(shellData.rootWindow, &backgroundImage);

		// Release the image memory
		kernelImageFree(&backgroundImage);
	}
	else
	{
		kernelWindowSetBackgroundImage(shellData.rootWindow, NULL);
	}

	// Redraw the root window
	if (shellData.rootWindow->draw)
		shellData.rootWindow->draw(shellData.rootWindow);

	return (status = 0);
}


int kernelWindowShellCenterBackground(const char *filename)
{
	// This will center the supplied image as the background

	// If the window shell is dead, don't continue
	if (!kernelMultitaskerProcessIsAlive(shellData.processId))
		return (ERR_NOSUCHPROCESS);

	// For the moment, this is not really implemented.  The 'tile' routine
	// will automatically center the image if it's wider or higher than half
	// the screen size anyway.
	return (kernelWindowShellTileBackground(filename));
}


int kernelWindowShellRaiseWindowMenu(void)
{
	// Focus the root window and raise the window menu.  This would typically
	// be done in response to the user pressing ALT-Tab.

	int status = 0;

	// If the window shell is dead, don't continue
	if (!kernelMultitaskerProcessIsAlive(shellData.processId))
		return (status = ERR_NOSUCHPROCESS);

	kernelDebug(debug_gui, "WindowShell toggle root window menu bar");

	if (shellData.rootWindow &&
		(shellData.rootWindow->flags & WINFLAG_VISIBLE))
	{
		kernelWindowFocus(shellData.rootWindow);

		status = kernelWindowToggleMenuBar();
	}

	return (status);
}


kernelWindowComponent *kernelWindowShellNewTaskbarIcon(image *img)
{
	// Create an icon in the shell's top menu bar.

	kernelWindowComponent *iconComponent = NULL;
	componentParameters params;

	// Check params
	if (!img)
		return (iconComponent = NULL);

	// Make sure we have a root window and menu bar
	if (!shellData.rootWindow || !shellData.menuBar)
		return (iconComponent = NULL);

	memset(&params, 0, sizeof(componentParameters));
	params.flags = WINDOW_COMPFLAG_CANFOCUS;

	// Create the menu bar icon
	iconComponent = kernelWindowNewMenuBarIcon(shellData.menuBar, img,
		&params);
	if (!iconComponent)
		return (iconComponent);

	// Add it to the shell's list of menu bar components
	if (addMenuBarComponent(iconComponent) < 0)
	{
		kernelWindowComponentDestroy(iconComponent);
		return (iconComponent = NULL);
	}

	return (iconComponent);
}


kernelWindowComponent *kernelWindowShellNewTaskbarTextLabel(const char *text)
{
	// Create an label in the shell's top menu bar.

	kernelWindowComponent *labelComponent = NULL;
	componentParameters params;

	// Check params
	if (!text)
		return (labelComponent = NULL);

	// Make sure we have a root window and menu bar
	if (!shellData.rootWindow || !shellData.menuBar)
		return (labelComponent = NULL);

	memset(&params, 0, sizeof(componentParameters));
	params.foreground = shellData.menuBar->params.foreground;
	params.background = shellData.menuBar->params.background;
	params.flags |= (WINDOW_COMPFLAG_CUSTOMFOREGROUND |
		WINDOW_COMPFLAG_CUSTOMBACKGROUND);
	params.font = windowVariables->font.varWidth.small.font;

	// Create the menu bar label
	labelComponent = kernelWindowNewTextLabel(shellData.menuBar, text,
		&params);
	if (!labelComponent)
		return (labelComponent);

	// Add it to the shell's list of menu bar components
	if (addMenuBarComponent(labelComponent) < 0)
	{
		kernelWindowComponentDestroy(labelComponent);
		return (labelComponent = NULL);
	}

	return (labelComponent);
}


void kernelWindowShellDestroyTaskbarComp(kernelWindowComponent *component)
{
	// Destroy a component in the shell's top menu bar.

	menuBarComponent *menuBarComp = NULL;
	kernelLinkedListItem *iter = NULL;

	// Check params
	if (!component)
		return;

	// Make sure we have a root window and menu bar
	if (!shellData.rootWindow || !shellData.menuBar)
		return;

	// Remove it from the list

	menuBarComp = kernelLinkedListIterStart((kernelLinkedList *)
		&shellData.menuBarCompsList, &iter);

	while (menuBarComp)
	{
		if (menuBarComp->component == component)
		{
			kernelLinkedListRemove((kernelLinkedList *)
				&shellData.menuBarCompsList, menuBarComp);

			kernelFree(menuBarComp);

			break;
		}

		menuBarComp = kernelLinkedListIterNext((kernelLinkedList *)
			&shellData.menuBarCompsList, &iter);
	}

	// Destroy it
	kernelWindowComponentDestroy(component);

	// Re-draw the menu bar
	updateMenuBarComponents();
}


kernelWindowComponent *kernelWindowShellIconify(kernelWindow *window,
	int iconify, image *img)
{
	kernelWindowComponent *iconComponent = NULL;

	// Check params.  img is allowed to be NULL.
	if (!window)
		return (iconComponent = NULL);

	if (img)
	{
		iconComponent = kernelWindowShellNewTaskbarIcon(img);
		if (!iconComponent)
			return (iconComponent);
	}

	if (iconify)
		window->flags |= WINFLAG_ICONIFIED;
	else
		window->flags &= ~WINFLAG_ICONIFIED;

	kernelWindowSetVisible(window, !iconify);

	// Update the window menu
	kernelWindowShellUpdateList(shellData.windowList, shellData.numberWindows);

	return (iconComponent);
}

