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
//  disprops.c
//

// Sets display properties for the kernel's window manager

/* This is the text that appears when a user requests help about this program
<help>

 -- disprops --

Control the display properties

Usage:
  disprops

The disprops program is interactive, and may only be used in graphics mode.
It can be used to change display settings, such as the screen resolution,
the background wallpaper, and the base colors used by the window manager.

</help>
*/

#include <libintl.h>
#include <locale.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/api.h>
#include <sys/color.h>
#include <sys/desktop.h>
#include <sys/env.h>
#include <sys/paths.h>
#include <sys/user.h>
#include <sys/window.h>

#define _(string) gettext(string)

#define WINDOW_TITLE			_("Display Settings")
#define SCREEN_RESOLUTION		_("Screen resolution:")
#define COLORS					_("Colors:")
#define FOREGROUND				_("Foreground")
#define BACKGROUND				_("Background")
#define DESKTOP					_("Desktop")
#define CHANGE					_("Change")
#define BACKGROUND_WALLPAPER	_("Background wallpaper:")
#define CHOOSE					_("Choose")
#define USE_WALLPAPER			_("Use background wallpaper")
#define MISCELLANEOUS			_("Miscellaneous:")
#define BOOT_GRAPHICS			_("Boot in graphics mode")
#define SHOW_CLOCK				_("Show a clock on the desktop")
#define OK						_("OK")
#define CANCEL					_("Cancel")
#define CLOCK_VARIABLE			DESKTOP_PROGRAM "clock"
#define CLOCK_PROGRAM			PATH_PROGRAMS "/clock"
#define WALLPAPER_PROGRAM		PATH_PROGRAMS "/wallpaper"
#define MAX_IMAGE_DIMENSION		128

typedef struct {
	char description[32];
	videoMode mode;

} modeInfo;

static int readOnly = 1;
static int processId = 0;
static int privilege = 0;
static char currentUser[USER_MAX_NAMELENGTH + 1];
static int numberModes = 0;
static int showingClock = 0;
static videoMode currentMode;
static videoMode videoModes[MAXVIDEOMODES];
static listItemParameters *listItemParams = NULL;
static int wallpaperImageWidth = MAX_IMAGE_DIMENSION;
static objectKey window = NULL;
static objectKey resolutionLabel = NULL;
static objectKey modeList = NULL;
static objectKey colorsLabel = NULL;
static objectKey colorsRadio = NULL;
static objectKey canvas = NULL;
static objectKey changeColorsButton = NULL;
static objectKey wallpaperLabel;
static objectKey wallpaperImage = NULL;
static objectKey wallpaperButton = NULL;
static objectKey wallpaperCheckbox = NULL;
static objectKey miscLabel = NULL;
static objectKey bootGraphicsCheckbox = NULL;
static objectKey showClockCheckbox = NULL;
static objectKey okButton = NULL;
static objectKey cancelButton = NULL;
static color foreground = COLOR_DEFAULT_FOREGROUND;
static color background = COLOR_DEFAULT_BACKGROUND;
static color desktop = COLOR_DEFAULT_DESKTOP;
static int colorsChanged = 0;

#if 0
static void chooseVideoMode(void)
{
	// This is the original C version of the algorithm used to automatically
	// select a video mode in src/osloader/loaderVideo.s, in case we ever need
	// to debug it.

	unsigned screenArea = 0;
	unsigned bestScreenArea = 0;
	int fallbackMode = -1;
	unsigned aspect = 0;
	unsigned bestAspect = 0;
	int bestMode = -1;
	int count;

	for (count = 0; count < numberModes; count ++)
	{
		if ((videoModes[count].xRes < 640) || (videoModes[count].yRes < 480))
			continue;

		if ((fallbackMode >= 0) && (videoModes[count].xRes > 1500))
			continue;

		screenArea = (videoModes[count].xRes * videoModes[count].yRes);

		if ((fallbackMode < 0) ||
			(videoModes[fallbackMode].xRes > 1500) ||
			(screenArea > bestScreenArea) ||
			((screenArea >= bestScreenArea) &&
				(videoModes[count].bitsPerPixel >
					videoModes[fallbackMode].bitsPerPixel)))
		{
			bestScreenArea = screenArea;
			fallbackMode = count;
		}
	}

	if (fallbackMode < 0)
		return;

	for (count = 0; count < numberModes; count ++)
	{
		if ((videoModes[count].xRes < 800) || (videoModes[count].yRes < 600))
			continue;

		aspect = ((videoModes[count].xRes << 8) / videoModes[count].yRes);

		if (aspect > bestAspect)
			bestAspect = aspect;
	}

	bestScreenArea = 0;
	for (count = 0; count < numberModes; count ++)
	{
		aspect = ((videoModes[count].xRes << 8) / videoModes[count].yRes);
		if (aspect != bestAspect)
			continue;

		if ((bestMode >= 0) && (videoModes[count].xRes > 1500))
			continue;

		screenArea = (videoModes[count].xRes * videoModes[count].yRes);

		if ((bestMode < 0) ||
			(videoModes[bestMode].xRes > 1500) ||
			(screenArea > bestScreenArea) ||
			((screenArea >= bestScreenArea) &&
				(videoModes[count].bitsPerPixel >
					videoModes[bestMode].bitsPerPixel)))
		{
			bestScreenArea = screenArea;
			bestMode = count;
		}
	}

	if (bestMode < 0)
		bestMode = fallbackMode;

	printf("Chose %dx%d %dbpp\n", videoModes[bestMode].xRes,
		videoModes[bestMode].yRes, videoModes[bestMode].bitsPerPixel);
}
#endif


static int getVideoModes(void)
{
	int status = 0;
	int count;

	// Try to get the supported video modes from the kernel
	numberModes = graphicGetModes(videoModes, sizeof(videoModes));
	if (numberModes <= 0)
		return (status = ERR_NODATA);

	if (listItemParams)
		free(listItemParams);

	listItemParams = malloc(numberModes * sizeof(listItemParameters));
	if (!listItemParams)
		return (status = ERR_MEMORY);

	// Construct the mode strings
	for (count = 0; count < numberModes; count ++)
	{
		snprintf(listItemParams[count].text, WINDOW_MAX_LABEL_LENGTH,
			_(" %d x %d, %d bit "),  videoModes[count].xRes,
			videoModes[count].yRes, videoModes[count].bitsPerPixel);
	}

	// Get the current mode
	graphicGetMode(&currentMode);

	return (status = 0);
}


static void getFileColors(const char *fileName)
{
	// Get the current color scheme from the requested config file.

	variableList list;
	const char *value = NULL;

	memset(&list, 0, sizeof(variableList));

	if ((fileFind(fileName, NULL) >= 0) &&
		(configRead(fileName, &list) >= 0) && list.memory)
	{
		if ((value = variableListGet(&list, COLOR_FOREGROUND_RED)))
			foreground.red = atoi(value);
		if ((value = variableListGet(&list, COLOR_FOREGROUND_GREEN)))
			foreground.green = atoi(value);
		if ((value = variableListGet(&list, COLOR_FOREGROUND_BLUE)))
			foreground.blue = atoi(value);
		if ((value = variableListGet(&list, COLOR_BACKGROUND_RED)))
			background.red = atoi(value);
		if ((value = variableListGet(&list, COLOR_BACKGROUND_GREEN)))
			background.green = atoi(value);
		if ((value = variableListGet(&list, COLOR_BACKGROUND_BLUE)))
			background.blue = atoi(value);
		if ((value = variableListGet(&list, COLOR_DESKTOP_RED)))
			desktop.red = atoi(value);
		if ((value = variableListGet(&list, COLOR_DESKTOP_GREEN)))
			desktop.green = atoi(value);
		if ((value = variableListGet(&list, COLOR_DESKTOP_BLUE)))
			desktop.blue = atoi(value);

		variableListDestroy(&list);
	}
}


static void getColors(void)
{
	// Get the current color scheme from the window config(s).

	char fileName[MAX_PATH_NAME_LENGTH];

	// First read the values from the system config.
	sprintf(fileName, PATH_SYSTEM_CONFIG "/" WINDOW_CONFIGFILE);
	getFileColors(fileName);

	if (strcmp(currentUser, USER_ADMIN))
	{
		// Now, if the user has their own config, read that too (overrides
		// any values we read previously)
		sprintf(fileName, PATH_USERS_CONFIG "/" WINDOW_CONFIGFILE,
			currentUser);
		getFileColors(fileName);
	}
}


static void setColors(void)
{
	// Set the current color scheme.

	char fileName[MAX_PATH_NAME_LENGTH];
	variableList list;
	char value[16];

	// Set the colors in the window system for the current session
	windowSetColor(COLOR_SETTING_FOREGROUND, &foreground);
	windowSetColor(COLOR_SETTING_BACKGROUND, &background);
	windowSetColor(COLOR_SETTING_DESKTOP, &desktop);
	windowResetColors();

	if (!readOnly)
	{
		// Set the colors in the window configuration.

		if (!strcmp(currentUser, USER_ADMIN))
		{
			// The user 'admin' doesn't have user settings.  Use the system
			// one.
			sprintf(fileName, PATH_SYSTEM_CONFIG "/" WINDOW_CONFIGFILE);
		}
		else
		{
			// Does the user have a config dir?
			sprintf(fileName, PATH_USERS_CONFIG, currentUser);
			if (fileFind(fileName, NULL) < 0)
			{
				// No, try to create it.
				if (fileMakeDir(fileName) < 0)
					return;
			}

			// Use the user's window config file?
			sprintf(fileName, PATH_USERS_CONFIG "/" WINDOW_CONFIGFILE,
				currentUser);
		}

		if (fileFind(fileName, NULL) < 0)
		{
			// Doesn't exist.  Create an empty list.
			if (variableListCreate(&list) < 0)
				return;
		}
		else
		{
			// There's a file.  Try to read it.
			if (configRead(fileName, &list) < 0)
				return;
		}

		if (list.memory)
		{
			sprintf(value, "%d", foreground.red);
			variableListSet(&list, COLOR_FOREGROUND_RED, value);
			sprintf(value, "%d", foreground.green);
			variableListSet(&list, COLOR_FOREGROUND_GREEN, value);
			sprintf(value, "%d", foreground.blue);
			variableListSet(&list, COLOR_FOREGROUND_BLUE, value);
			sprintf(value, "%d", background.red);
			variableListSet(&list, COLOR_BACKGROUND_RED, value);
			sprintf(value, "%d", background.green);
			variableListSet(&list, COLOR_BACKGROUND_GREEN, value);
			sprintf(value, "%d", background.blue);
			variableListSet(&list, COLOR_BACKGROUND_BLUE, value);
			sprintf(value, "%d", desktop.red);
			variableListSet(&list, COLOR_DESKTOP_RED, value);
			sprintf(value, "%d", desktop.green);
			variableListSet(&list, COLOR_DESKTOP_GREEN, value);
			sprintf(value, "%d", desktop.blue);
			variableListSet(&list, COLOR_DESKTOP_BLUE, value);

			configWrite(fileName, &list);

			variableListDestroy(&list);
		}
	}
}


static color *getSelectedColor(void)
{
	int selected = 0;

	windowComponentGetSelected(colorsRadio, &selected);

	switch (selected)
	{
		case 0:
		default:
			return (&foreground);

		case 1:
			return (&background);

		case 2:
			return (&desktop);
	}
}


static void refreshWindow(void)
{
	// We got a 'window refresh' event (probably because of a language switch),
	// so we need to update things

	// Re-get the language setting
	setlocale(LC_ALL, getenv(ENV_LANG));
	textdomain("disprops");

	// Re-get the character set
	if (getenv(ENV_CHARSET))
		windowSetCharSet(window, getenv(ENV_CHARSET));

	// Refresh the 'screen resolution' label
	windowComponentSetData(resolutionLabel, SCREEN_RESOLUTION,
		strlen(SCREEN_RESOLUTION), 1 /* redraw */);

	// Refresh the 'colors' label
	windowComponentSetData(colorsLabel, COLORS,	strlen(COLORS),
		1 /* redraw */);

	// Refresh the 'colors' radio button
	windowComponentSetData(colorsRadio,
		(char *[]){ FOREGROUND, BACKGROUND, DESKTOP }, 3, 1 /* redraw */);

	// Refresh the 'change color' button
	windowComponentSetData(changeColorsButton, CHANGE, strlen(CHANGE),
		1 /* redraw */);

	// Refresh the 'background wallpaper' label
	windowComponentSetData(wallpaperLabel, BACKGROUND_WALLPAPER,
		strlen(BACKGROUND_WALLPAPER), 1 /* redraw */);

	// Refresh the 'choose wallpaper' button
	windowComponentSetData(wallpaperButton, CHOOSE,	strlen(CHOOSE),
		1 /* redraw */);

	// Refresh the 'use wallpaper' checkbox
	windowComponentSetData(wallpaperCheckbox, USE_WALLPAPER,
		strlen(USE_WALLPAPER), 1 /* redraw */);

	// Refresh the 'miscellaneous items' label
	windowComponentSetData(miscLabel, MISCELLANEOUS, strlen(MISCELLANEOUS),
		1 /* redraw */);

	// Refresh the 'boot in graphics mode' checkbox
	windowComponentSetData(bootGraphicsCheckbox, BOOT_GRAPHICS,
		strlen(BOOT_GRAPHICS), 1 /* redraw */);

	// Refresh the 'show a clock' checkbox
	windowComponentSetData(showClockCheckbox, SHOW_CLOCK, strlen(SHOW_CLOCK),
		1 /* redraw */);

	// Refresh the 'ok' button
	windowComponentSetData(okButton, OK, strlen(OK), 1 /* redraw */);

	// Refresh the 'cancel' button
	windowComponentSetData(cancelButton, CANCEL, strlen(CANCEL),
		1 /* redraw */);

	// Refresh the window title
	windowSetTitle(window, WINDOW_TITLE);
}


static void drawColor(color *draw)
{
	// Draw the current color on the canvas

	windowDrawParameters drawParams;

	memset(&drawParams, 0, sizeof(windowDrawParameters));
	drawParams.operation = draw_rect;
	drawParams.mode = draw_normal;
	drawParams.foreground.red = draw->red;
	drawParams.foreground.green = draw->green;
	drawParams.foreground.blue = draw->blue;
	drawParams.xCoord1 = 0;
	drawParams.yCoord1 = 0;
	drawParams.width = windowComponentGetWidth(canvas);
	drawParams.height = windowComponentGetHeight(canvas);
	drawParams.thickness = 1;
	drawParams.fill = 1;
	windowComponentSetData(canvas, &drawParams, sizeof(windowDrawParameters),
		1 /* redraw */);
}


static int readDesktopVariable(const char *variable, char *value, int len)
{
	// Get the variable from the desktop config.

	int status = 0;
	char fileName[MAX_PATH_NAME_LENGTH];

	if (strcmp(currentUser, USER_ADMIN))
	{
		// First try the user's desktop config file
		sprintf(fileName, PATH_USERS_CONFIG "/" DESKTOP_CONFIGFILE,
			currentUser);

		status = fileFind(fileName, NULL);
		if (status >= 0)
		{
			status = configGet(fileName, variable, value, len);
			if (status >= 0)
				return (status);
		}
	}

	// Now try to read from the system desktop config
	sprintf(fileName, PATH_SYSTEM_CONFIG "/" DESKTOP_CONFIGFILE);

	status = configGet(fileName, variable, value, len);

	return (status);
}


static int writeDesktopVariable(const char *variable, char *value)
{
	// Get the variable from the desktop config.

	int status = 0;
	char fileName[MAX_PATH_NAME_LENGTH];
	file f;

	if (readOnly)
		return (status = ERR_NOWRITE);

	memset(&f, 0, sizeof(file));

	if (!strcmp(currentUser, USER_ADMIN))
	{
		// The user 'admin' doesn't have user settings.  Use the system one.
		sprintf(fileName, PATH_SYSTEM_CONFIG "/" DESKTOP_CONFIGFILE);
	}
	else
	{
		// Does the user have a config dir?
		sprintf(fileName, PATH_USERS_CONFIG, currentUser);

		status = fileFind(fileName, NULL);
		if (status < 0)
		{
			// No, try to create it.
			status = fileMakeDir(fileName);
			if (status < 0)
				return (status);
		}

		// Write the user's window config file
		sprintf(fileName, PATH_USERS_CONFIG "/" DESKTOP_CONFIGFILE,
			currentUser);

		status = fileFind(fileName, NULL);
		if (status < 0)
		{
			// The file doesn't exist.  Try to create it.
			status = fileOpen(fileName, (OPENMODE_WRITE | OPENMODE_CREATE), &f);
			if (status < 0)
				return (status);

			// Now close the file
			fileClose(&f);
		}
	}

	if (value)
		status = configSet(fileName, variable, value);
	else
		status = configUnset(fileName, variable);

	return (status);
}


static void eventHandler(objectKey key, windowEvent *event)
{
	color *selectedColor = getSelectedColor();
	int mode = 0;
	int clockSelected = 0;
	char string[160];
	int selected = 0;
	file f;

	// Check for window events.
	if (key == window)
	{
		// Check for window refresh
		if (event->type == EVENT_WINDOW_REFRESH)
			refreshWindow();

		// Check for window resize
		else if (event->type == EVENT_WINDOW_RESIZE)
			// Redraw the canvas
			drawColor(selectedColor);

		// Check for the window being closed
		else if (event->type == EVENT_WINDOW_CLOSE)
			windowGuiStop();
	}

	else if ((key == wallpaperCheckbox) && (event->type == EVENT_MOUSE_LEFTUP))
	{
		windowComponentGetSelected(wallpaperCheckbox, &selected);
		windowComponentSetEnabled(wallpaperButton, selected);
		if (selected)
		{
			if ((readDesktopVariable(DESKTOP_BACKGROUND, string,
					sizeof(string)) >= 0) &&
				(fileFind(string, NULL) >= 0))
			{
				windowThumbImageUpdate(wallpaperImage, string,
					wallpaperImageWidth, MAX_IMAGE_DIMENSION, 1 /* stretch */,
					NULL /* no background color */);
			}
		}
		else
		{
			windowThumbImageUpdate(wallpaperImage, NULL, wallpaperImageWidth,
				MAX_IMAGE_DIMENSION, 0 /* no stretch */, &desktop);
		}
	}

	else if ((key == wallpaperButton) && (event->type == EVENT_MOUSE_LEFTUP))
	{
		loaderLoadAndExec(WALLPAPER_PROGRAM, privilege, 1);

		if ((readDesktopVariable(DESKTOP_BACKGROUND, string,
				sizeof(string)) >= 0) &&
			(fileFind(string, NULL) >= 0))
		{
			windowThumbImageUpdate(wallpaperImage, string, wallpaperImageWidth,
				MAX_IMAGE_DIMENSION, 1 /* stretch */,
				NULL /* no background color */);
		}
	}

	else if ((key == colorsRadio) || (key == changeColorsButton))
	{
		if ((key == changeColorsButton) && (event->type == EVENT_MOUSE_LEFTUP))
		{
			windowNewColorDialog(window, selectedColor);
			colorsChanged = 1;
		}

		if (((key == changeColorsButton) &&
				(event->type == EVENT_MOUSE_LEFTUP)) ||
			((key == colorsRadio) && (event->type & EVENT_SELECTION)))
		{
			drawColor(selectedColor);
		}
	}

	else if ((key == okButton) && (event->type == EVENT_MOUSE_LEFTUP))
	{
		// Does the user not want to boot in graphics mode?
		windowComponentGetSelected(bootGraphicsCheckbox, &selected);
		if (!selected)
		{
			// Try to create the /nograph file
			memset(&f, 0, sizeof(file));
			fileOpen("/nograph", (OPENMODE_WRITE | OPENMODE_CREATE |
				OPENMODE_TRUNCATE), &f);
			fileClose(&f);
		}

		// Does the user want to show a clock on the desktop?
		windowComponentGetSelected(showClockCheckbox, &clockSelected);
		if ((!showingClock && clockSelected) ||
			(showingClock && !clockSelected))
		{
			if (!showingClock && clockSelected)
			{
				// Run the clock program now.  No block.
				loaderLoadAndExec(CLOCK_PROGRAM, privilege, 0);

				// Set the variable for the clock
				writeDesktopVariable(CLOCK_VARIABLE, CLOCK_PROGRAM);
			}
			else
			{
				// Try to kill any clock program(s) currently running
				multitaskerKillByName("clock", 0);

				// Remove any clock variable
				writeDesktopVariable(CLOCK_VARIABLE, NULL);
			}
		}

		// Did the user choose a different graphics mode?
		windowComponentGetSelected(modeList, &mode);
		if ((mode >= 0) && (videoModes[mode].mode != currentMode.mode))
		{
			if (!graphicSetMode(&videoModes[mode]))
			{
				sprintf(string, _("The resolution has been changed to %dx%d, "
					"%dbpp\nThis will take effect after you reboot."),
					videoModes[mode].xRes, videoModes[mode].yRes,
					videoModes[mode].bitsPerPixel);
				windowNewInfoDialog(window, _("Changed"), string);
			}
			else
			{
				sprintf(string, _("Error %d setting mode"), mode);
				windowNewErrorDialog(window, _("Error"), string);
			}
		}

		// Did the user choose not to use desktop wallpaper?
		windowComponentGetSelected(wallpaperCheckbox, &selected);
		if (!selected && (fileFind(WALLPAPER_PROGRAM, NULL) >= 0))
			system(WALLPAPER_PROGRAM " none");

		// Did the user change the default colors?
		if (colorsChanged)
		{
			// Set the colors
			setColors();
			colorsChanged = 0;
		}

		windowGuiStop();
	}

	else if ((key == cancelButton) && (event->type == EVENT_MOUSE_LEFTUP))
		windowGuiStop();

	return;
}


static void constructWindow(void)
{
	// If we are in graphics mode, make a window rather than operating on the
	// command line.

	componentParameters params;
	objectKey container = NULL;
	float scale = 1;
	process tmpProc;
	char value[128];
	int count;

	// Create a new window, with small, arbitrary size and location
	window = windowNew(processId, WINDOW_TITLE);
	if (!window)
		return;

	memset(&params, 0, sizeof(componentParameters));

	// Make a container for the left hand side components
	params.gridWidth = 1;
	params.gridHeight = 1;
	params.orientationX = orient_left;
	params.orientationY = orient_top;
	container = windowNewContainer(window, "leftContainer", &params);

	// Make a label for the graphics modes
	params.gridWidth = 2;
	params.padTop = 5;
	params.padLeft = 5;
	params.padRight = 5;
	params.flags |= WINDOW_COMPFLAG_FIXEDHEIGHT;
	resolutionLabel = windowNewTextLabel(container, SCREEN_RESOLUTION,
		&params);

	// Make a list with all the available graphics modes
	params.gridY++;
	params.flags &= ~WINDOW_COMPFLAG_FIXEDHEIGHT;
	modeList = windowNewList(container, windowlist_textonly, 5, 1, 0,
		listItemParams, numberModes, &params);

	// Select the current mode
	for (count = 0; count < numberModes; count ++)
	{
		if (videoModes[count].mode == currentMode.mode)
		{
			windowComponentSetSelected(modeList, count);
			break;
		}
	}

	if (readOnly || privilege)
		windowComponentSetEnabled(modeList, 0);

	// A label for the colors
	params.gridY++;
	params.padTop = 10;
	params.flags |= WINDOW_COMPFLAG_FIXEDHEIGHT;
	colorsLabel = windowNewTextLabel(container, COLORS, &params);

	// Create the colors radio button
	params.gridY++;
	params.gridWidth = 1;
	params.gridHeight = 2;
	params.padTop = 5;
	params.flags |= WINDOW_COMPFLAG_FIXEDWIDTH;
	colorsRadio = windowNewRadioButton(container, 3, 1,
		(char *[]){ FOREGROUND, BACKGROUND, DESKTOP }, 3 , &params);
	windowRegisterEventHandler(colorsRadio, &eventHandler);

	// The canvas to show the current color
	params.gridX++;
	params.gridHeight = 1;
	params.flags &= ~(WINDOW_COMPFLAG_FIXEDWIDTH |
		WINDOW_COMPFLAG_FIXEDHEIGHT);
	params.flags |= WINDOW_COMPFLAG_HASBORDER;
	canvas = windowNewCanvas(container, 50, 50, &params);

	// Create the change color button
	params.gridY++;
	params.flags &= ~WINDOW_COMPFLAG_HASBORDER;
	changeColorsButton = windowNewButton(container, CHANGE, NULL, &params);
	windowRegisterEventHandler(changeColorsButton, &eventHandler);

	// Adjust the canvas width so that it matches the width of the button.
	windowComponentSetWidth(canvas,
		windowComponentGetWidth(changeColorsButton));

	// A little divider
	params.gridX = 1;
	params.gridY = 0;
	params.orientationX = orient_center;
	params.flags |= WINDOW_COMPFLAG_FIXEDWIDTH;
	windowNewDivider(window, divider_vertical, &params);

	// Make a container for the right hand side components
	params.gridX = 2;
	params.padTop = 0;
	params.padLeft = 0;
	params.padRight = 0;
	params.orientationX = orient_left;
	params.flags &= ~WINDOW_COMPFLAG_FIXEDWIDTH;
	container = windowNewContainer(window, "rightContainer", &params);

	// A label for the background wallpaper
	params.gridX = 0;
	params.padTop = 5;
	params.padLeft = 5;
	params.padRight = 5;
	params.flags |= WINDOW_COMPFLAG_FIXEDHEIGHT;
	wallpaperLabel = windowNewTextLabel(container, BACKGROUND_WALLPAPER,
		&params);

	// Create the thumbnail image for the background wallpaper, with the width
	// scaled to the X resolution of the current graphics mode.  Start with a
	// blank one and update it in a minute.

	if (currentMode.yRes)
		scale = ((float) MAX_IMAGE_DIMENSION / (float) currentMode.yRes);
	if (currentMode.xRes)
		wallpaperImageWidth = (int)((float) currentMode.xRes * scale);

	params.gridY++;
	params.flags |= WINDOW_COMPFLAG_HASBORDER;
	wallpaperImage = windowNewThumbImage(container, NULL, wallpaperImageWidth,
		MAX_IMAGE_DIMENSION, 1 /* stretch */, &params);

	// Create the background wallpaper button
	params.gridY++;
	params.flags &= ~WINDOW_COMPFLAG_HASBORDER;
	params.flags |= WINDOW_COMPFLAG_FIXEDWIDTH;
	wallpaperButton = windowNewButton(container, CHOOSE, NULL, &params);
	windowRegisterEventHandler(wallpaperButton, &eventHandler);

	// Create the checkbox for whether to use background wallpaper
	params.gridY++;
	wallpaperCheckbox = windowNewCheckbox(container, USE_WALLPAPER, &params);
	windowComponentSetSelected(wallpaperCheckbox, 1);
	windowRegisterEventHandler(wallpaperCheckbox, &eventHandler);

	// Try to get the wallpaper image name
	if ((readDesktopVariable(DESKTOP_BACKGROUND, value, 128) >= 0) &&
		(fileFind(value, NULL) >= 0))
	{
		windowThumbImageUpdate(wallpaperImage, value, wallpaperImageWidth,
			MAX_IMAGE_DIMENSION, 1 /* stretch */,
			NULL /* no background color */);
	}
	else
	{
		windowThumbImageUpdate(wallpaperImage, NULL, wallpaperImageWidth,
			MAX_IMAGE_DIMENSION, 0 /* no stretch */, &desktop);

		windowComponentSetSelected(wallpaperCheckbox, 0);
		windowComponentSetEnabled(wallpaperButton, 0);
	}

	if (fileFind(WALLPAPER_PROGRAM, NULL) < 0)
	{
		windowComponentSetEnabled(wallpaperButton, 0);
		windowComponentSetEnabled(wallpaperCheckbox, 0);
	}

	// A little divider
	params.gridY++;
	params.flags &= ~WINDOW_COMPFLAG_FIXEDWIDTH;
	windowNewDivider(container, divider_horizontal, &params);

	// A label for the miscellaneous stuff
	params.gridY++;
	params.flags |= WINDOW_COMPFLAG_FIXEDWIDTH;
	miscLabel = windowNewTextLabel(container, MISCELLANEOUS, &params);

	// Make a checkbox for whether to boot in graphics mode
	params.gridY++;
	bootGraphicsCheckbox = windowNewCheckbox(container, BOOT_GRAPHICS,
		&params);
	windowComponentSetSelected(bootGraphicsCheckbox, 1);
	if (readOnly)
		windowComponentSetEnabled(bootGraphicsCheckbox, 0);

	// Make a checkbox for whether to show the clock on the desktop
	params.gridY++;
	showClockCheckbox = windowNewCheckbox(container, SHOW_CLOCK, &params);

	// Are we currently set to show one?
	memset(&tmpProc, 0, sizeof(process));
	if (!multitaskerGetProcessByName("clock", &tmpProc))
	{
		showingClock = 1;
		windowComponentSetSelected(showClockCheckbox, showingClock);
	}

	if (fileFind(CLOCK_PROGRAM, NULL) < 0)
		windowComponentSetEnabled(showClockCheckbox, 0);

	// Make a container for the bottom buttons
	params.gridX = 0;
	params.gridY = 1;
	params.gridWidth = 3;
	params.padTop = 0;
	params.padLeft = 0;
	params.padRight = 0;
	params.orientationX = orient_center;
	params.flags |= (WINDOW_COMPFLAG_FIXEDWIDTH | WINDOW_COMPFLAG_FIXEDHEIGHT);
	container = windowNewContainer(window, "bottomContainer", &params);

	// Create the OK button
	params.gridY = 0;
	params.gridWidth = 1;
	params.padTop = 5;
	params.padLeft = 5;
	params.padRight = 5;
	params.padBottom = 5;
	params.orientationX = orient_right;
	okButton = windowNewButton(container, OK, NULL, &params);
	windowRegisterEventHandler(okButton, &eventHandler);

	// Create the Cancel button
	params.gridX++;
	params.orientationX = orient_left;
	cancelButton = windowNewButton(container, CANCEL, NULL, &params);
	windowRegisterEventHandler(cancelButton, &eventHandler);
	windowComponentFocus(cancelButton);

	// Register an event handler to catch window close events
	windowRegisterEventHandler(window, &eventHandler);

	windowSetVisible(window, 1);

	drawColor(&foreground);

	return;
}


int main(int argc __attribute__((unused)), char *argv[])
{
	int status = 0;
	disk sysDisk;

	setlocale(LC_ALL, getenv(ENV_LANG));
	textdomain("disprops");

	memset(&sysDisk, 0, sizeof(disk));

	// Only work in graphics mode
	if (!graphicsAreEnabled())
	{
		printf(_("\nThe \"%s\" command only works in graphics mode\n"),
			argv[0]);
		return (status = ERR_NOTINITIALIZED);
	}

	// Find out whether we are currently running on a read-only filesystem
	if (!fileGetDisk(PATH_SYSTEM, &sysDisk))
		readOnly = sysDisk.readOnly;

	// We need our process ID and privilege to create the windows
	processId = multitaskerGetCurrentProcessId();
	privilege = multitaskerGetProcessPrivilege(processId);

	// Need the user name for saving settings
	userGetCurrent(currentUser, USER_MAX_NAMELENGTH);

	// Get the list of supported video modes
	status = getVideoModes();
	if (status < 0)
		return (status);

	// Get the current color scheme
	getColors();

	// Make the window
	constructWindow();

	// Run the GUI
	windowGuiRun();
	windowDestroy(window);

	if (listItemParams)
		free(listItemParams);

	return (status);
}

