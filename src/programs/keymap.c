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
//  keymap.c
//

// This is a program for showing and changing the keyboard mapping.  Works
// in both text and graphics modes.

/* This is the text that appears when a user requests help about this program
<help>

 -- keymap --

View or change the current keyboard mapping

Usage:
  keymap [-T] [-p] [-s file_name] [keymap_name]

The keymap program can be used to view the available keyboard mapping, or
set the current map.  It works in both text and graphics modes:

A particular keymap can be selected by supplying a file name for keymap_name,
or else using its descriptive name (with double quotes (") around it if it
contains space characters).

If no keymap is specified on the command line, the current default one will
be selected.

In text mode:

  The -p option will print a detailed listing of the selected keymap.

  The -s option will save the selected keymap using the supplied file name.

  The -x option will convert a version 1 keymap to version 2.

  If a keymap is specified without the -p or -s options, then the keymap will
  be set as the current default.

  With no options, all available mappings are listed, with the current default
  indicated.

In graphics mode, the program is interactive and the user can select and
manipulate keymaps visually.

Options:
-p  : Print a detailed listing of the keymap (text mode).
-s  : Save the specified keymap to the supplied file name (text mode).
-T  : Force text mode operation
-x  : Convert a version 1 keymap to version 2 (text mode).

</help>
*/

#include <ctype.h>
#include <libgen.h>
#include <libintl.h>
#include <locale.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/api.h>
#include <sys/ascii.h>
#include <sys/charset.h>
#include <sys/env.h>
#include <sys/font.h>
#include <sys/kernconf.h>
#include <sys/keyboard.h>
#include <sys/paths.h>

#define _(string) gettext(string)

#define WINDOW_TITLE		_("Keyboard Map")
#define CURRENT				_("Current:")
#define NAME				_("Name:")
#define LANGUAGE			_("Language:")
#define SAVE				_("Save")
#define SET_DEFAULT			_("Set as default")
#define CLOSE				_("Close")
#define KEYVAL_FIELDWIDTH	5

static int graphics = 0;
static char *cwd = NULL;
static char currentName[KEYMAP_NAMELEN];
static keyMap *selectedMap = NULL;
static listItemParameters *mapListParams = NULL;
static int numMapNames = 0;
static objectKey window = NULL;
static objectKey mapList = NULL;
static objectKey currentLabel = NULL;
static objectKey currentNameLabel = NULL;
static objectKey nameLabel = NULL;
static objectKey nameField = NULL;
static objectKey langLabel = NULL;
static objectKey langField = NULL;
static windowKeyboard *keyboard = NULL;
static objectKey saveButton = NULL;
static objectKey defaultButton = NULL;
static objectKey closeButton = NULL;

static const char *scan2String[KEYBOARD_SCAN_CODES] = {
	"LCtrl", "A0", "LAlt", "SpaceBar",							// 00-03
	"A2", "A3", "A4", "RCtrl",									// 04-07
	"LeftArrow", "DownArrow", "RightArrow", "Zero",				// 08-0B
	"Period", "Enter", "LShift", "B0",							// 0C-0F
	"B1", "B2", "B3", "B4",										// 10-13
	"B5", "B6", "B7", "B8",										// 14-17
	"B9", "B10", "RShift", "UpArrow",							// 18-1B
	"One", "Two", "Three", "CapsLock",							// 1C-1F
	"C1", "C2", "C3", "C4",										// 20-23
	"C5", "C6", "C7", "C8",										// 24-27
	"C9", "C10", "C11", "C12",									// 28-2B
	"Four", "Five", "Six", "Plus",								// 2C-2F
	"Tab", "D1", "D2", "D3",									// 30-33
	"D4", "D5", "D6", "D7",										// 34-37
	"D8", "D9", "D10", "D11",									// 38-3B
	"D12", "D13", "Del", "End",									// 3C-3F
	"PgDn", "Seven", "Eight", "Nine",							// 40-43
	"E0", "E1", "E2", "E3",										// 44-47
	"E4", "E5", "E6", "E7", "E8", "E9", "E10", "E11",			// 48-4F
	"E12", "BackSpace", "Ins", "Home",							// 50-53
	"PgUp", "NLck", "Slash", "Asterisk",						// 54-57
	"Minus", "Esc", "F1", "F2", "F3", "F4", "F5", "F6",			// 58-5F
	"F7", "F8", "F9", "F10",									// 60-63
	"F11", "F12", "Print", "SLck", "Pause"						// 64-68
};


static void usage(char *name)
{
	printf("%s", _("usage:\n"));
	printf(_("%s [-T] [-p] [-s file_name] [map_name]\n"), name);
	return;
}


__attribute__((format(printf, 1, 2)))
static void error(const char *format, ...)
{
	// Generic error message code for either text or graphics modes

	va_list list;
	char output[MAXSTRINGLENGTH];

	va_start(list, format);
	vsnprintf(output, MAXSTRINGLENGTH, format, list);
	va_end(list);

	if (graphics)
		windowNewErrorDialog(window, _("Error"), output);
	else
		fprintf(stderr, _("\n\nERROR: %s\n\n"), output);
}


static int readMap(const char *fileName, keyMap *map)
{
	int status = 0;
	fileStream theStream;

	memset(&theStream, 0, sizeof(fileStream));

	status = fileStreamOpen(fileName, OPENMODE_READ, &theStream);
	if (status < 0)
	{
		error(_("Couldn't open file %s"), fileName);
		return (status);
	}

	status = fileStreamRead(&theStream, sizeof(keyMap), (char *) map);

	fileStreamClose(&theStream);

	if (status < 0)
	{
		error(_("Couldn't read file %s"), fileName);
		return (status);
	}

	// Check the magic number
	if (strncmp(map->magic, KEYMAP_MAGIC, sizeof(KEYMAP_MAGIC)))
		return (status = ERR_BADDATA);

	return (status = 0);
}


static int findMapFile(const char *mapName, char *fileName)
{
	// Look in the current directory for the keymap file with the supplied map
	// name

	int status = 0;
	file theFile;
	keyMap *map = NULL;
	int count;

	map = malloc(sizeof(keyMap));
	if (!map)
		return (status = ERR_MEMORY);

	memset(&theFile, 0, sizeof(file));

	// Loop through the files in the keymap directory
	for (count = 0; ; count ++)
	{
		if (count)
			status = fileNext(cwd, &theFile);
		else
			status = fileFirst(cwd, &theFile);

		if (status < 0)
			// No more files.
			break;

		if (theFile.type != fileT)
			continue;

		if (strcmp(cwd, "/"))
			snprintf(fileName, MAX_PATH_NAME_LENGTH, "%s/%s", cwd, theFile.name);
		else
			snprintf(fileName, MAX_PATH_NAME_LENGTH, "/%s", theFile.name);

		status = readMap(fileName, map);
		if (status < 0)
			continue;

		if (!strncmp(map->name, mapName, sizeof(map->name)))
		{
			status = 0;
			goto out;
		}
	}

	// If we fall through to here, it wasn't found.
	fileName[0] = '\0';
	status = ERR_NOSUCHENTRY;

out:
	free(map);
	return (status);
}


static int setMap(const char *mapName)
{
	// Change the current mapping in the kernel, and also change the config for
	// persistence at the next reboot

	int status = 0;
	char *fileName = NULL;
	disk confDisk;

	fileName = malloc(MAX_PATH_NAME_LENGTH);
	if (!fileName)
		return (status = ERR_MEMORY);

	status = findMapFile(mapName, fileName);
	if (status < 0)
	{
		error(_("Couldn't find keyboard map %s"), mapName);
		goto out;
	}

	status = keyboardSetMap(fileName);
	if (status < 0)
	{
		error(_("Couldn't set keyboard map to %s"), fileName);
		goto out;
	}

	status = keyboardGetMap(selectedMap);
	if (status < 0)
	{
		error("%s", _("Couldn't get current keyboard map"));
		goto out;
	}

	strncpy(currentName, selectedMap->name, KEYMAP_NAMELEN);

	// Find out whether the kernel config file is on a read-only filesystem
	memset(&confDisk, 0, sizeof(disk));
	if (!fileGetDisk(KERNEL_DEFAULT_CONFIG, &confDisk) && !confDisk.readOnly)
	{
		status = configSet(KERNEL_DEFAULT_CONFIG, KERNELVAR_KEYBOARD_MAP,
			fileName);
		if (status < 0)
			error("%s", _("Couldn't write keyboard map setting"));
	}

out:
	free(fileName);
	return (status);
}


static int loadMap(const char *mapName)
{
	int status = 0;
	char *fileName = NULL;
	keyMapV1 *oldMap = NULL;
	keyMap *newMap = NULL;
	int count;

	fileName = malloc(MAX_PATH_NAME_LENGTH);
	if (!fileName)
		return (status = ERR_MEMORY);

	// Find the map by name
	status = findMapFile(mapName, fileName);
	if (status < 0)
	{
		error(_("Couldn't find keyboard map %s"), mapName);
		goto out;
	}

	// Read it in
	status = readMap(fileName, selectedMap);

	if (selectedMap->version != 0x0200)
	{
		// Convert an old map file to a new one

		oldMap = (keyMapV1 *) selectedMap;
		newMap = malloc(sizeof(keyMap));
		if (!newMap)
		{
			status = ERR_MEMORY;
			goto out;
		}

		memcpy(newMap->magic, oldMap->magic, 8);
		newMap->version = 0x0200;
		memcpy(newMap->name, oldMap->name, KEYMAP_NAMELEN);
		newMap->language[0] = tolower(oldMap->name[0]);
		newMap->language[1] = tolower(oldMap->name[1]);

		for (count = 0; count < KEYBOARD_SCAN_CODES; count ++)
		{
			newMap->regMap[count] = charsetToUnicode(CHARSET_NAME_ISO_8859_15,
				oldMap->regMap[count]);
			newMap->shiftMap[count] =
				charsetToUnicode(CHARSET_NAME_ISO_8859_15,
					oldMap->shiftMap[count]);
			newMap->controlMap[count] =
				charsetToUnicode(CHARSET_NAME_ISO_8859_15,
					oldMap->controlMap[count]);
			newMap->altGrMap[count] =
				charsetToUnicode(CHARSET_NAME_ISO_8859_15,
					oldMap->altGrMap[count]);
			newMap->shiftAltGrMap[count] =
				charsetToUnicode(CHARSET_NAME_ISO_8859_15,
					oldMap->shiftMap[count]);
		}

		memcpy(selectedMap, newMap, sizeof(keyMap));
	}

out:
	if (newMap)
		free(newMap);
	free(fileName);
	return (status);
}


static int saveMap(const char *fileName)
{
	int status = 0;
	disk mapDisk;
	fileStream theStream;

	memset(&mapDisk, 0, sizeof(disk));
	memset(&theStream, 0, sizeof(fileStream));

	// Find out whether the file is on a read-only filesystem
	if (!fileGetDisk(fileName, &mapDisk) && mapDisk.readOnly)
	{
		error(_("Can't write %s:\nFilesystem is read-only"), fileName);
		return (status = ERR_NOWRITE);
	}

	if (graphics && nameField)
		// Get the map name
		windowComponentGetData(nameField, selectedMap->name, KEYMAP_NAMELEN);

	if (graphics && langField)
		// Get the map language
		windowComponentGetData(langField, selectedMap->language, 2);

	status = fileStreamOpen(fileName, (OPENMODE_CREATE | OPENMODE_WRITE |
		OPENMODE_TRUNCATE), &theStream);
	if (status < 0)
	{
		error(_("Couldn't open file %s"), fileName);
		return (status);
	}

	status = fileStreamWrite(&theStream, sizeof(keyMap), (char *) selectedMap);

	fileStreamClose(&theStream);

	if (status < 0)
		error(_("Couldn't write file %s"), fileName);

	return (status);
}


static int getMapNames(char *nameBuffer)
{
	// Look in the keymap directory for keymap files.

	int status = 0;
	file theFile;
	char *fileName = NULL;
	keyMap *map = NULL;
	int bufferChar = 0;
	int count;

	fileName = malloc(MAX_PATH_NAME_LENGTH);
	map = malloc(sizeof(keyMap));
	if (!fileName || !map)
		return (status = ERR_MEMORY);

	nameBuffer[0] = '\0';
	numMapNames = 0;

	// Loop through the files in the keymap directory
	for (count = 0; ; count ++)
	{
		if (count)
			status = fileNext(cwd, &theFile);
		else
			status = fileFirst(cwd, &theFile);

		if (status < 0)
			break;

		if (theFile.type != fileT)
			continue;

		if (strcmp(cwd, "/"))
		{
			snprintf(fileName, MAX_PATH_NAME_LENGTH, "%s/%s", cwd,
				theFile.name);
		}
		else
		{
			snprintf(fileName, MAX_PATH_NAME_LENGTH, "/%s", theFile.name);
		}

		status = readMap(fileName, map);
		if (status < 0)
			// Not a keymap file we can read.
			continue;

		strncpy((nameBuffer + bufferChar), map->name, sizeof(map->name));
		bufferChar += (strlen(map->name) + 1);
		numMapNames += 1;
	}

	free(fileName);
	free(map);
	return (status = 0);
}


static int getMapNameParams(void)
{
	// Get the list of keyboard map names from the kernel

	int status = 0;
	char *nameBuffer = NULL;
	char *buffPtr = NULL;
	int count;

	nameBuffer = malloc(1024);
	if (!nameBuffer)
		return (status = ERR_MEMORY);

	status = getMapNames(nameBuffer);
	if (status < 0)
		goto out;

	if (mapListParams)
		free(mapListParams);

	mapListParams = malloc(numMapNames * sizeof(listItemParameters));
	if (!mapListParams)
	{
		status = ERR_MEMORY;
		goto out;
	}

	buffPtr = nameBuffer;

	for (count = 0; count < numMapNames; count ++)
	{
		strncpy(mapListParams[count].text, buffPtr, WINDOW_MAX_LABEL_LENGTH);
		buffPtr += (strlen(mapListParams[count].text) + 1);
	}

	status = 0;

out:
	free(nameBuffer);
	return (status);
}


static void refreshWindow(void)
{
	// We got a 'window refresh' event (probably because of a language
	// switch), so we need to update things

	// Re-get the language setting
	setlocale(LC_ALL, getenv(ENV_LANG));
	textdomain("keymap");

	// Re-get the character set
	if (getenv(ENV_CHARSET))
		windowSetCharSet(window, getenv(ENV_CHARSET));

	// Refresh the 'current' label
	windowComponentSetData(currentLabel, CURRENT, strlen(CURRENT),
		1 /* redraw */);

	// Refresh the 'name' field
	windowComponentSetData(nameLabel, NAME, strlen(NAME), 1 /* redraw */);

	// Refresh the 'language' field
	windowComponentSetData(langLabel, LANGUAGE, strlen(LANGUAGE),
		1 /* redraw */);

	// Refresh the 'save' button
	windowComponentSetData(saveButton, SAVE, strlen(SAVE), 1 /* redraw */);

	// Refresh the 'set as default' button
	windowComponentSetData(defaultButton, SET_DEFAULT, strlen(SET_DEFAULT),
		1 /* redraw */);

	// Refresh the 'close' button
	windowComponentSetData(closeButton, CLOSE, strlen(CLOSE), 1 /* redraw */);

	// Refresh the window title
	windowSetTitle(window, WINDOW_TITLE);
}


static void selectMap(const char *mapName)
{
	int count;

	// Select the current map
	for (count = 0; count < numMapNames; count ++)
	{
		if (!strcmp(mapListParams[count].text, mapName))
		{
			windowComponentSetSelected(mapList, count);
			break;
		}
	}
}


static void eventHandler(objectKey key, windowEvent *event)
{
	int status = 0;
	int selected = 0;
	char charsetName[CHARSET_NAME_LEN];
	char *fullName = NULL;
	char *dirName = NULL;

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

	else if ((key == mapList) && (event->type & EVENT_SELECTION) &&
		(event->type & EVENT_MOUSE_DOWN))
	{
		if (windowComponentGetSelected(mapList, &selected) < 0)
			return;

		if (loadMap(mapListParams[selected].text) < 0)
			return;

		windowComponentSetData(nameField, selectedMap->name, KEYMAP_NAMELEN,
			1 /* redraw */);

		windowComponentSetData(langField, selectedMap->language, 2,
			1 /* redraw */);

		keyboard->setMap(keyboard, selectedMap);

		// Try to get the character set for the keymap language
		if (configGet(PATH_SYSTEM_CONFIG "/charset.conf",
			selectedMap->language, charsetName, CHARSET_NAME_LEN) < 0)
		{
			strncpy(charsetName, CHARSET_NAME_ISO_8859_15, CHARSET_NAME_LEN);
		}

		keyboard->setCharset(keyboard, charsetName);
	}

	else if (key == keyboard->canvas)
	{
		// The event is for our keyboard widget
		keyboard->eventHandler(keyboard, event);
	}

	else if ((key == saveButton) && (event->type == EVENT_MOUSE_LEFTUP))
	{
		fullName = malloc(MAX_PATH_NAME_LENGTH);
		if (!fullName)
			return;

		findMapFile(selectedMap->name, fullName);

		status = windowNewFileDialog(window, _("Save as"),
			_("Choose the output file:"), cwd, fullName, MAX_PATH_NAME_LENGTH,
			fileT, 0 /* no thumbnails */);
		if (status != 1)
		{
			free(fullName);
			return;
		}

		status = saveMap(fullName);
		if (status < 0)
		{
			free(fullName);
			return;
		}

		// Are we working in a new directory?
		dirName = dirname(fullName);
		if (dirName)
		{
			strncpy(cwd, dirName, MAX_PATH_LENGTH);
			free(dirName);
		}

		free(fullName);

		if (getMapNameParams() < 0)
			return;

		windowComponentSetData(mapList, mapListParams, numMapNames,
			1 /* redraw */);

		selectMap(selectedMap->name);

		windowNewInfoDialog(window, _("Saved"), _("Map saved"));
	}

	else if ((key == defaultButton) && (event->type == EVENT_MOUSE_LEFTUP))
	{
		if (windowComponentGetSelected(mapList, &selected) < 0)
			return;
		if (setMap(mapListParams[selected].text) < 0)
			return;
		windowComponentSetData(currentNameLabel, mapListParams[selected].text,
			strlen(mapListParams[selected].text), 1 /* redraw */);
	}

	// Check for the window being closed by a GUI event.
	else if ((key == closeButton) && (event->type == EVENT_MOUSE_LEFTUP))
		windowGuiStop();
}


static int selectCharDialog(objectKey parentWindow)
{
	int selected = 0;
	objectKey dialogWindow = NULL;
	objectKey largeFont = NULL;
	objectKey smallFont = NULL;
	int charWidth = 0;
	int charHeight = 0;
	int smallHeight = 0;
	objectKey canvas = NULL;
	componentParameters params;
	windowDrawParameters drawParams;
	char string[80];
	int charVal = 0;
	char keyChar[4];
	windowEvent event;
	int rowCount, columnCount;

	sprintf(string, "%s (%s)", _("Select character"), keyboard->charsetName);
	dialogWindow = windowNewDialog(parentWindow, string);
	if (!dialogWindow)
		return (selected = ERR_NOCREATE);

	// Try to load a larger font for displaying the charset characters
	largeFont = fontGet(FONT_FAMILY_ARIAL, (FONT_STYLEFLAG_BOLD |
		FONT_STYLEFLAG_FIXED), 20, keyboard->charsetName);

	// Try to load a smaller font for displaying charset values
	smallFont = fontGet(FONT_FAMILY_LIBMONO, FONT_STYLEFLAG_FIXED, 8, NULL);

	if (!largeFont || !smallFont)
	{
		selected = ERR_NOCREATE;
		goto out;
	}

	charWidth = fontGetPrintedWidth(largeFont, NULL, "@");
	charHeight = fontGetHeight(largeFont);
	smallHeight = fontGetHeight(smallFont);
	if ((charWidth <= 0) || (charHeight <= 0) || (smallHeight <= 0))
	{
		selected = ERR_NOCREATE;
		goto out;
	}

	memset(&params, 0, sizeof(componentParameters));
	params.gridWidth = 1;
	params.gridHeight = 1;
	params.padLeft = params.padRight = params.padTop = params.padBottom = 5;
	params.orientationX = orient_center;
	params.orientationY = orient_middle;
	params.flags = WINDOW_COMPFLAG_CUSTOMBACKGROUND;
	windowGetColor(COLOR_SETTING_DESKTOP, &params.background);

	// Make a canvas for drawing characters on
	canvas = windowNewCanvas(dialogWindow, (charWidth * 16), (charHeight * 16),
		&params);
	if (!canvas)
	{
		selected = ERR_NOCREATE;
		goto out;
	}

	// Set the correct character set
	windowComponentSetCharSet(canvas, keyboard->charsetName);

	// Draw the characters

	memset(&drawParams, 0, sizeof(windowDrawParameters));
	drawParams.mode = draw_normal;
	drawParams.operation = draw_text;
	drawParams.foreground = COLOR_WHITE;
	windowGetColor(COLOR_SETTING_DESKTOP, &drawParams.background);

	drawParams.width = charWidth;
	drawParams.height = charHeight;
	drawParams.thickness = 1;
	drawParams.fill = 1;

	for (rowCount = 0; rowCount < 16; rowCount ++)
	{
		for (columnCount = 0; columnCount < 16; columnCount ++)
		{
			drawParams.xCoord1 = (columnCount * charWidth);
			drawParams.yCoord1 = (rowCount * charHeight);

			charVal = ((rowCount * 16) + columnCount);

			if (isgraph(charVal))
			{
				drawParams.font = largeFont;
				sprintf(keyChar, "%c", charVal);
			}
			else
			{
				drawParams.font = smallFont;

				switch (charVal)
				{
					case ASCII_NULL:
						strcpy(keyChar, "NUL");
						break;
					case ASCII_BEL:
						strcpy(keyChar, "BEL");
						break;
					case ASCII_BS:
						strcpy(keyChar, "BS");
						break;
					case ASCII_TAB:
						strcpy(keyChar, "HT");
						break;
					case ASCII_ENTER:
						strcpy(keyChar, "LF");
						break;
					case ASCII_VT:
						strcpy(keyChar, "VT");
						break;
					case ASCII_FF:
						strcpy(keyChar, "FF");
						break;
					case ASCII_CR:
						strcpy(keyChar, "CR");
						break;
					case ASCII_ESC:
						strcpy(keyChar, "ESC");
						break;
					case ASCII_SPACE:
						strcpy(keyChar, "SPC");
						break;
					case ASCII_DEL:
						strcpy(keyChar, "DEL");
						break;

					default:
						sprintf(keyChar, "%d",
							charsetToUnicode(keyboard->charsetName, charVal));
						break;
				}

				drawParams.xCoord1 += ((charWidth -
					fontGetPrintedWidth(smallFont, NULL, keyChar)) / 2);
				drawParams.yCoord1 += ((charHeight - smallHeight) / 2);
			}

			drawParams.data = keyChar;
			windowComponentSetData(canvas, &drawParams, 1,
				((rowCount == 15) && (columnCount == 15)));
		}
	}

	windowCenterDialog(parentWindow, dialogWindow);
	windowSetVisible(dialogWindow, 1);

	while (1)
	{
		// Check for window close events
		if ((windowComponentEventGet(dialogWindow, &event) > 0) &&
			(event.type == EVENT_WINDOW_CLOSE))
		{
			selected = ERR_CANCELLED;
			break;
		}

		else if ((windowComponentEventGet(canvas, &event) > 0) &&
			(event.type == EVENT_MOUSE_LEFTUP))
		{
			selected = (((event.yPosition / charHeight) * 16) +
				(event.xPosition / charWidth));
			break;
		}

		// Not finished yet
		multitaskerYield();
	}

out:
	windowDestroy(dialogWindow);

	if (selected >= 0)
		return (charsetToUnicode(keyboard->charsetName, selected));
	else
		return (selected);
}


static void selectKeyValue(objectKey parentWindow, objectKey field,
	objectKey label)
{
	int charVal = 0;
	char string[KEYVAL_FIELDWIDTH + 1];

	charVal = selectCharDialog(parentWindow);
	if (charVal >= 0)
	{
		sprintf(string, "%d", charVal);
		windowComponentSetData(field, string, strlen(string), 1 /* redraw */);

		charVal = charsetFromUnicode(keyboard->charsetName, charVal);
		if ((charVal < 0) || (charVal > 255))
			charVal = 0;

		sprintf(string, "%c", charVal);

		if (string[0] == '\n')
			string[0] = ' ';

		windowComponentSetData(label, string, strlen(string), 1 /* redraw */);
	}
}


static void typedKeyValue(objectKey field, objectKey label)
{
	int charVal = 0;
	char string[KEYVAL_FIELDWIDTH + 1];

	windowComponentGetData(field, string, KEYVAL_FIELDWIDTH);

	charVal = atoi(string);
	if (charVal >= 0)
	{
		charVal = charsetFromUnicode(keyboard->charsetName, charVal);
		if ((charVal < 0) || (charVal > 255))
			charVal = 0;

		sprintf(string, "%c", charVal);

		if (string[0] == '\n')
			string[0] = ' ';

		windowComponentSetData(label, string, strlen(string), 1 /* redraw */);
	}
}


static int changeKeyDialog(keyScan scanCode)
{
	int status = 0;
	objectKey dialogWindow = NULL;
	objectKey largeFont = NULL;
	objectKey smallFont = NULL;
	objectKey regCharLabel = NULL;
	objectKey shiftCharLabel = NULL;
	objectKey altGrCharLabel = NULL;
	objectKey shiftAltGrCharLabel = NULL;
	objectKey ctrlCharLabel = NULL;
	objectKey regField = NULL;
	objectKey shiftField = NULL;
	objectKey altGrField = NULL;
	objectKey shiftAltGrField = NULL;
	objectKey ctrlField = NULL;
	objectKey buttonContainer = NULL;
	objectKey _okButton = NULL;
	objectKey _cancelButton = NULL;
	int commit = 0;
	char string[80];
	color foreground = { 255, 255, 255 };
	windowEvent event;
	componentParameters params;

	dialogWindow = windowNewDialog(window, _("Change key settings"));
	if (!dialogWindow)
		return (status = ERR_NOCREATE);

	// Try to load a larger for for displaying the characters
	largeFont = fontGet(FONT_FAMILY_ARIAL, (FONT_STYLEFLAG_BOLD |
		FONT_STYLEFLAG_FIXED), 20, keyboard->charsetName);

	// And a smaller one for the labels
	smallFont = fontGet(FONT_FAMILY_ARIAL, FONT_STYLEFLAG_BOLD, 10,
		keyboard->charsetName);

	memset(&params, 0, sizeof(componentParameters));
	params.gridWidth = 5;
	params.gridHeight = 1;
	params.padTop = 5;
	params.padLeft = 5;
	params.padRight = 5;
	params.orientationX = orient_center;
	params.orientationY = orient_middle;
	params.foreground = foreground;
	windowGetColor(COLOR_SETTING_DESKTOP, &params.background);

	// Show the current scan code
	snprintf(string, 80, _("Scan code: 0x%02x (%s)"), scanCode,
		scan2String[scanCode]);
	windowNewTextLabel(dialogWindow, string, &params);

	// Tell the user these are Unicode values.
	params.gridY += 1;
	windowNewTextLabel(dialogWindow, _("Unicode"), &params);

	// Labels for each of the map types

	params.gridY += 1;
	params.gridWidth = 1;
	params.orientationY = orient_bottom;
	params.font = smallFont;
	windowNewTextLabel(dialogWindow, _("Normal"), &params);

	params.gridX += 1;
	windowNewTextLabel(dialogWindow, _("Shift"), &params);

	params.gridX += 1;
	windowNewTextLabel(dialogWindow, _("AltGr"), &params);

	params.gridX += 1;
	sprintf(string, "%s-\n%s", _("Shift"), _("AltGr"));
	windowNewTextLabel(dialogWindow, string, &params);

	params.gridX += 1;
	windowNewTextLabel(dialogWindow, _("Ctrl"), &params);

	// Labels to show the current ASCII value for each map type

	params.gridX = 0;
	params.gridY += 1;
	params.orientationY = orient_middle;
	params.font = largeFont;
	params.flags |= (WINDOW_COMPFLAG_CUSTOMFOREGROUND |
		WINDOW_COMPFLAG_CUSTOMBACKGROUND | WINDOW_COMPFLAG_HASBORDER);
	regCharLabel = windowNewTextLabel(dialogWindow, "@", &params);
	windowComponentSetCharSet(regCharLabel, keyboard->charsetName);
	sprintf(string, "%c", charsetFromUnicode(keyboard->charsetName,
		selectedMap->regMap[scanCode]));
	windowComponentSetData(regCharLabel, string, strlen(string),
		0 /* no redraw */);

	params.gridX += 1;
	shiftCharLabel = windowNewTextLabel(dialogWindow, "@", &params);
	windowComponentSetCharSet(shiftCharLabel, keyboard->charsetName);
	sprintf(string, "%c", charsetFromUnicode(keyboard->charsetName,
		selectedMap->shiftMap[scanCode]));
	windowComponentSetData(shiftCharLabel, string, strlen(string),
		0 /* no redraw */);

	params.gridX += 1;
	altGrCharLabel = windowNewTextLabel(dialogWindow, "@", &params);
	windowComponentSetCharSet(altGrCharLabel, keyboard->charsetName);
	sprintf(string, "%c", charsetFromUnicode(keyboard->charsetName,
		selectedMap->altGrMap[scanCode]));
	windowComponentSetData(altGrCharLabel, string, strlen(string),
		0 /* no redraw */);

	params.gridX += 1;
	shiftAltGrCharLabel = windowNewTextLabel(dialogWindow, "@", &params);
	windowComponentSetCharSet(shiftAltGrCharLabel, keyboard->charsetName);
	sprintf(string, "%c", charsetFromUnicode(keyboard->charsetName,
		selectedMap->shiftAltGrMap[scanCode]));
	windowComponentSetData(shiftAltGrCharLabel, string, strlen(string),
		0 /* no redraw */);

	params.gridX += 1;
	ctrlCharLabel = windowNewTextLabel(dialogWindow, "@", &params);
	windowComponentSetCharSet(ctrlCharLabel, keyboard->charsetName);
	sprintf(string, "%c", charsetFromUnicode(keyboard->charsetName,
		selectedMap->controlMap[scanCode]));
	windowComponentSetData(ctrlCharLabel, string, strlen(string),
		0 /* no redraw */);

	// Text fields for entering new values for each map type

	params.gridX = 0;
	params.gridY += 1;
	params.font = NULL;
	params.flags = 0;
	regField = windowNewTextField(dialogWindow, KEYVAL_FIELDWIDTH, &params);
	snprintf(string, KEYVAL_FIELDWIDTH, "%u", selectedMap->regMap[scanCode]);
	windowComponentSetData(regField, string, KEYVAL_FIELDWIDTH,
		1 /* redraw */);

	params.gridX += 1;
	shiftField = windowNewTextField(dialogWindow, KEYVAL_FIELDWIDTH, &params);
	snprintf(string, KEYVAL_FIELDWIDTH, "%u", selectedMap->shiftMap[scanCode]);
	windowComponentSetData(shiftField, string, KEYVAL_FIELDWIDTH,
		1 /* redraw */);

	params.gridX += 1;
	altGrField = windowNewTextField(dialogWindow, KEYVAL_FIELDWIDTH, &params);
	snprintf(string, KEYVAL_FIELDWIDTH, "%u", selectedMap->altGrMap[scanCode]);
	windowComponentSetData(altGrField, string, KEYVAL_FIELDWIDTH,
		1 /* redraw */);

	params.gridX += 1;
	shiftAltGrField = windowNewTextField(dialogWindow, KEYVAL_FIELDWIDTH,
		&params);
	snprintf(string, KEYVAL_FIELDWIDTH, "%u",
		selectedMap->shiftAltGrMap[scanCode]);
	windowComponentSetData(shiftAltGrField, string, KEYVAL_FIELDWIDTH,
		1 /* redraw */);

	params.gridX += 1;
	ctrlField = windowNewTextField(dialogWindow, KEYVAL_FIELDWIDTH, &params);
	snprintf(string, KEYVAL_FIELDWIDTH, "%u",
		selectedMap->controlMap[scanCode]);
	windowComponentSetData(ctrlField, string, KEYVAL_FIELDWIDTH,
		1 /* redraw */);

	// A container for buttons
	params.gridX = 0;
	params.gridY += 1;
	params.gridWidth = 5;
	params.padBottom = 5;
	params.flags = WINDOW_COMPFLAG_FIXEDWIDTH;
	params.font = NULL;
	buttonContainer = windowNewContainer(dialogWindow, "buttonContainer",
		&params);

	// Ok button
	params.gridY = 0;
	params.gridWidth = 1;
	params.padTop = 0;
	params.padLeft = 2;
	params.padRight = 2;
	params.padBottom = 0;
	params.orientationX = orient_right;
	_okButton = windowNewButton(buttonContainer, _("OK"), NULL, &params);

	// Cancel button
	params.gridX += 1;
	params.orientationX = orient_left;
	_cancelButton = windowNewButton(buttonContainer, _("Cancel"), NULL,
		&params);
	windowComponentFocus(_cancelButton);

	windowCenterDialog(window, dialogWindow);
	windowSetVisible(dialogWindow, 1);

	while (1)
	{
		// Check for Cancel button or window close events
		if (((windowComponentEventGet(_cancelButton, &event) > 0) &&
				(event.type == EVENT_MOUSE_LEFTUP)) ||
			((windowComponentEventGet(dialogWindow, &event) > 0) &&
				(event.type == EVENT_WINDOW_CLOSE)))
		{
			break;
		}

		// Check for the OK button
		else if ((windowComponentEventGet(_okButton, &event) > 0) &&
			(event.type == EVENT_MOUSE_LEFTUP))
		{
			commit = 1;
		}

		// Clicks in the 'normal' character label
		else if ((windowComponentEventGet(regCharLabel, &event) > 0) &&
			(event.type == EVENT_MOUSE_LEFTUP))
		{
			selectKeyValue(dialogWindow, regField, regCharLabel);
		}

		// Clicks in the 'shifted' character label
		else if ((windowComponentEventGet(shiftCharLabel, &event) > 0) &&
			(event.type == EVENT_MOUSE_LEFTUP))
		{
			selectKeyValue(dialogWindow, shiftField, shiftCharLabel);
		}

		// Clicks in the 'AltGr' character label
		else if ((windowComponentEventGet(altGrCharLabel, &event) > 0) &&
			(event.type == EVENT_MOUSE_LEFTUP))
		{
			selectKeyValue(dialogWindow, altGrField, altGrCharLabel);
		}

		// Clicks in the 'Shift-AltGr' character label
		else if ((windowComponentEventGet(shiftAltGrCharLabel, &event) > 0) &&
			(event.type == EVENT_MOUSE_LEFTUP))
		{
			selectKeyValue(dialogWindow, shiftAltGrField, shiftAltGrCharLabel);
		}

		// Clicks in the 'control' character label
		else if ((windowComponentEventGet(ctrlCharLabel, &event) > 0) &&
			(event.type == EVENT_MOUSE_LEFTUP))
		{
			selectKeyValue(dialogWindow, ctrlField, ctrlCharLabel);
		}

		// Key presses in the 'normal' field
		else if ((windowComponentEventGet(regField, &event) > 0) &&
			(event.type == EVENT_KEY_DOWN))
		{
			if (event.key == keyEnter)
				commit = 1;
			else
				typedKeyValue(regField, regCharLabel);
		}

		// Key presses in the 'shifted' field
		else if ((windowComponentEventGet(shiftField, &event) > 0) &&
			(event.type == EVENT_KEY_DOWN))
		{
			if (event.key == keyEnter)
				commit = 1;
			else
				typedKeyValue(shiftField, shiftCharLabel);
		}

		// Key presses in the 'AltGr' field
		else if ((windowComponentEventGet(altGrField, &event) > 0) &&
			(event.type == EVENT_KEY_DOWN))
		{
			if (event.key == keyEnter)
				commit = 1;
			else
				typedKeyValue(altGrField, altGrCharLabel);
		}

		// Key presses in the 'Shift-AltGr' field
		else if ((windowComponentEventGet(shiftAltGrField, &event) > 0) &&
			(event.type == EVENT_KEY_DOWN))
		{
			if (event.key == keyEnter)
				commit = 1;
			else
				typedKeyValue(shiftAltGrField, shiftAltGrCharLabel);
		}

		// Key presses in the 'control' field
		else if ((windowComponentEventGet(ctrlField, &event) > 0) &&
			(event.type == EVENT_KEY_DOWN))
		{
			if (event.key == keyEnter)
				commit = 1;
			else
				typedKeyValue(ctrlField, ctrlCharLabel);
		}

		// Finished?
		if (commit)
			break;

		// Not finished yet
		multitaskerYield();
	}

	if (commit)
	{
		windowComponentGetData(regField, string, KEYVAL_FIELDWIDTH);
		selectedMap->regMap[scanCode] = atoi(string);

		windowComponentGetData(shiftField, string, KEYVAL_FIELDWIDTH);
		selectedMap->shiftMap[scanCode] = atoi(string);

		windowComponentGetData(altGrField, string, KEYVAL_FIELDWIDTH);
		selectedMap->altGrMap[scanCode] = atoi(string);

		windowComponentGetData(shiftAltGrField, string, KEYVAL_FIELDWIDTH);
		selectedMap->shiftAltGrMap[scanCode] = atoi(string);

		windowComponentGetData(ctrlField, string, KEYVAL_FIELDWIDTH);
		selectedMap->controlMap[scanCode] = atoi(string);

		keyboard->setMap(keyboard, selectedMap);
	}

	windowDestroy(dialogWindow);
	return (0);
}


static int keyCallback(int eventType, keyScan scanCode)
{
	if (eventType == EVENT_KEY_UP)
	{
		switch (scanCode)
		{
			case keySLck:
			case keyNLck:
			case keyCapsLock:
			case keyLShift:
			case keyRShift:
			case keyLCtrl:
			case keyLAlt:
			case keyA2:
			case keyRCtrl:
				break;

			default:
				changeKeyDialog(scanCode);
				break;
		}
	}

	return (0);
}


static void constructWindow(void)
{
	// If we are in graphics mode, make a window rather than operating on the
	// command line.

	objectKey rightContainer = NULL;
	objectKey nameContainer = NULL;
	objectKey bottomContainer = NULL;
	componentParameters params;

	// Create a new window
	window = windowNew(multitaskerGetCurrentProcessId(), WINDOW_TITLE);
	if (!window)
		return;

	memset(&params, 0, sizeof(componentParameters));
	params.gridWidth = 1;
	params.gridHeight = 1;
	params.padTop = 5;
	params.padLeft = 5;
	params.orientationX = orient_left;
	params.orientationY = orient_top;

	// Create a list component for the keymap names
	mapList = windowNewList(window, windowlist_textonly, 5, 1, 0,
		mapListParams, numMapNames, &params);
	windowRegisterEventHandler(mapList, &eventHandler);
	windowComponentFocus(mapList);

	// Select the map
	selectMap(selectedMap->name);

	// Make a container for the current keymap labels
	params.gridX += 1;
	params.padRight = 5;
	rightContainer = windowNewContainer(window, "rightContainer", &params);

	// Create labels for the current keymap
	params.gridX = 0;
	params.gridY = 0;
	params.flags |= (WINDOW_COMPFLAG_FIXEDWIDTH | WINDOW_COMPFLAG_FIXEDHEIGHT);
	currentLabel = windowNewTextLabel(rightContainer, CURRENT, &params);
	params.gridY += 1;
	currentNameLabel = windowNewTextLabel(rightContainer, currentName, &params);

	// Make a container for the name and language
	params.gridX = 0;
	params.gridWidth = 2;
	nameContainer = windowNewContainer(window, "nameContainer", &params);

	// The name label and field
	params.gridWidth = 1;
	params.padLeft = 0;
	params.orientationY = orient_middle;
	nameLabel = windowNewTextLabel(nameContainer, NAME, &params);
	params.gridX += 1;
	params.padLeft = 5;
	nameField = windowNewTextField(nameContainer, (KEYMAP_NAMELEN + 1),
		&params);
	windowComponentSetData(nameField, selectedMap->name, KEYMAP_NAMELEN,
		1 /* redraw */);

	// The language label and field
	params.gridX += 1;
	langLabel = windowNewTextLabel(nameContainer, LANGUAGE, &params);
	params.gridX += 1;
	langField = windowNewTextField(nameContainer, 3, &params);
	windowComponentSetData(langField, selectedMap->language, 2,
		1 /* redraw */);

	// Create the keyboard widget for the selected map
	params.gridX = 0;
	params.gridY += 1;
	params.gridWidth = 2;
	params.orientationX = orient_center;
	keyboard = windowNewKeyboard(window, 0 /* min width */, 0 /* min height */,
		&keyCallback, &params);

	// Register an event handler to catch keyboard events
	windowRegisterEventHandler(keyboard->canvas, &eventHandler);

	params.gridY += 1;
	params.padBottom = 5;
	params.flags |= (WINDOW_COMPFLAG_FIXEDWIDTH | WINDOW_COMPFLAG_FIXEDHEIGHT);
	bottomContainer = windowNewContainer(window, "bottomContainer", &params);

	// Create a 'Save' button
	params.gridY = 0;
	params.gridWidth = 1;
	params.padTop = 0;
	params.padBottom = 0;
	params.padLeft = 0;
	params.padRight = 5;
	params.orientationX = orient_right;
	saveButton = windowNewButton(bottomContainer, SAVE, NULL, &params);
	windowRegisterEventHandler(saveButton, &eventHandler);

	// Create a 'Set as default' button
	params.gridX += 1;
	params.padLeft = 0;
	params.padRight = 0;
	params.orientationX = orient_center;
	defaultButton =	windowNewButton(bottomContainer, SET_DEFAULT, NULL,
		&params);
	windowRegisterEventHandler(defaultButton, &eventHandler);

	// Create a 'Close' button
	params.gridX += 1;
	params.padLeft = 5;
	params.orientationX = orient_left;
	closeButton = windowNewButton(bottomContainer, CLOSE, NULL, &params);
	windowRegisterEventHandler(closeButton, &eventHandler);

	// Register an event handler to catch window close events
	windowRegisterEventHandler(window, &eventHandler);

	windowSetVisible(window, 1);

	return;
}


static void printRow(int start, int end, unsigned *map, char *charsetName)
{
	int printed = 0;
	int count;

	printf("  ");
	for (count = start; count <= end; count ++)
	{
		printf("%s=", scan2String[count]);
		if (isgraph(map[count]))
			printf("'%c' ", charsetFromUnicode(charsetName, map[count]));
		else
			printf("%x ", map[count]);

		// Only print 8 on a line
		if (printed && !(printed % 8))
		{
			printed = 0;
			printf("\n  ");
		}
		else
		{
			printed += 1;
		}
	}
	printf("\n");
}


static void printMap(unsigned *map, char *charsetName)
{
	printf("%s\n", _("1st row"));
	printRow(keyEsc, keyPause, map, charsetName);
	printf("%s\n", _("2nd row"));
	printRow(keyE0, keyMinus, map, charsetName);
	printf("%s\n", _("3rd row"));
	printRow(keyTab, keyNine, map, charsetName);
	printf("%s\n", _("4th row"));
	printRow(keyCapsLock, keyPlus, map, charsetName);
	printf("%s\n", _("5th row"));
	printRow(keyLShift, keyThree, map, charsetName);
	printf("%s\n", _("6th row"));
	printRow(keyLCtrl, keyEnter, map, charsetName);
	printf("\n");
}


static void printKeyboard(void)
{
	// Print out the detail of the selected keymap

	char charsetName[CHARSET_NAME_LEN];

	// Try to get the character set for the keymap language
	if (configGet(PATH_SYSTEM_CONFIG "/charset.conf",
		selectedMap->language, charsetName, CHARSET_NAME_LEN) < 0)
	{
		strncpy(charsetName, CHARSET_NAME_ISO_8859_15, CHARSET_NAME_LEN);
	}

	printf(_("\nPrinting out keymap \"%s\"\n\n"), selectedMap->name);
	printf("-- %s --\n", _("Regular map"));
	printMap(selectedMap->regMap, charsetName);
	printf("-- %s --\n", _("Shift map"));
	printMap(selectedMap->shiftMap, charsetName);
	printf("-- %s --\n", _("Ctrl map"));
	printMap(selectedMap->controlMap, charsetName);
	printf("-- %s --\n", _("AltGr map"));
	printMap(selectedMap->altGrMap, charsetName);
	printf("-- %s --\n", _("Shift-AltGr map"));
	printMap(selectedMap->shiftAltGrMap, charsetName);
}


int main(int argc, char *argv[])
{
	int status = 0;
	int print = 0;
	int convert = 0;
	char *mapName = NULL;
	char *saveName = NULL;
	char *dirName = NULL;
	char opt;
	int count;

	setlocale(LC_ALL, getenv(ENV_LANG));
	textdomain("keymap");

	// Graphics enabled?
	graphics = graphicsAreEnabled();

	// Check options
	while (strchr("psTx:?", (opt = getopt(argc, argv, "ps:Tx"))))
	{
		switch (opt)
		{
			case 'p':
				// Just print out the map, if we're in text mode
				print = 1;
				break;

			case 's':
				// Save the map to a file
				if (!optarg)
				{
					fprintf(stderr, "%s", _("Missing filename argument for -s "
						"option\n"));
					usage(argv[0]);
					return (status = ERR_NULLPARAMETER);
				}
				saveName = optarg;
				break;

			case 'T':
				// Force text mode
				graphics = 0;
				break;

			case 'x':
				// Convert from version 1 to version 2
				convert = 1;
				break;

			case ':':
				fprintf(stderr, _("Missing parameter for %s option\n"),
					argv[optind - 1]);
				usage(argv[0]);
				return (status = ERR_NULLPARAMETER);

			default:
				fprintf(stderr, _("Unknown option '%c'\n"), optopt);
				usage(argv[0]);
				return (status = ERR_INVALID);
		}
	}

	cwd = malloc(MAX_PATH_LENGTH);
	selectedMap = malloc(sizeof(keyMap));
	if (!cwd || !selectedMap)
	{
		status = ERR_MEMORY;
		goto out;
	}

	strncpy(cwd, PATH_SYSTEM_KEYMAPS, MAX_PATH_LENGTH);

	// Get the current map
	status = keyboardGetMap(selectedMap);
	if (status < 0)
		goto out;

	strncpy(currentName, selectedMap->name, KEYMAP_NAMELEN);
	mapName = selectedMap->name;

	// Did the user supply either a map name or a key map file name?
	if ((argc > 1) && (optind < argc))
	{
		// Is it a file name?
		status = fileFind(argv[optind], NULL);
		if (status >= 0)
		{
			status = readMap(argv[optind], selectedMap);
			if (status < 0)
				goto out;

			mapName = selectedMap->name;

			if (convert)
				saveName = argv[optind];

			dirName = dirname(argv[optind]);
			if (dirName)
			{
				strncpy(cwd, dirName, MAX_PATH_LENGTH);
				free(dirName);
			}
		}
		else
		{
			// Assume we've been given a map name.
			mapName = argv[optind];
		}

		if (!graphics && !saveName && !print && !convert)
		{
			// The user wants to set the current keyboard map to the supplied
			// name.
			status = setMap(mapName);
			goto out;
		}

		// Load the supplied map name
		status = loadMap(mapName);
		if (status < 0)
			goto out;
	}

	if (saveName)
	{
		// The user wants to save the current keyboard map to the supplied
		// file name.
		status = saveMap(saveName);
		goto out;
	}

	status = getMapNameParams();
	if (status < 0)
		goto out;

	if (graphics)
	{
		// Make our window
		constructWindow();

		// Run the GUI
		windowGuiRun();

		// ...and when we come back...
		windowDestroy(window);
	}
	else
	{
		if (print)
		{
			// Print out the whole keyboard for the selected map
			printKeyboard();
		}
		else
		{
			// Just print the list of map names
			printf("\n");

			for (count = 0; count < numMapNames; count ++)
				printf("%s%s\n", mapListParams[count].text,
					(!strcmp(mapListParams[count].text, selectedMap->name)?
						_(" (current)") : ""));
		}
	}

	status = 0;

out:
	if (cwd)
		free(cwd);
	if (selectedMap)
		free(selectedMap);
	if (mapListParams)
		free(mapListParams);

	return (status);
}

