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
//  imgboot.c
//

// This is a program to be launched first when booting from distribution
// images, such as CD-ROM ISOs

/* This is the text that appears when a user requests help about this program
<help>

 -- imgboot --

The program launched at first system boot.

Usage:
  imgboot [-T]

This program is the default 'first boot' program on Visopsys floppy or
CD-ROM image files that asks if you want to 'install' or 'run now'.

</help>
*/

#include <errno.h>
#include <libintl.h>
#include <locale.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/api.h>
#include <sys/env.h>
#include <sys/font.h>
#include <sys/keyboard.h>
#include <sys/lang.h>
#include <sys/paths.h>
#include <sys/user.h>
#include <sys/vsh.h>

#define _(string) gettext(string)
#define gettext_noop(string) (string)

#define WELCOME			_("Welcome to %s")
#define COPYRIGHT		_("Copyright (C) 1998-2016 J. Andrew McLaughlin")
#define GPL				_( \
	"  This program is free software; you can redistribute it and/or modify it\n" \
	"  under the terms of the GNU General Public License as published by the\n" \
	"  Free Software Foundation; either version 2 of the License, or (at your\n" \
	"  option) any later version.\n\n" \
	"  This program is distributed in the hope that it will be useful, but\n" \
	"  WITHOUT ANY WARRANTY; without even the implied warranty of\n" \
	"  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See\n" \
	"  the file /system/COPYING.txt for more details.")
#define INSTALLQUEST	_("Would you like to install Visopsys?\n" \
						"(Choose continue to skip installing)")
#define INSTALL			_("Install")
#define CONTINUE		_("Continue")
#define LANGUAGE		_("Language")
#define DONTASK			_("Don't ask me this again")
#define LOGINPROGRAM	PATH_PROGRAMS "/login"
#define INSTALLPROGRAM	PATH_PROGRAMS "/install"

static int processId = 0;
static int readOnly = 1;
static int haveInstall = 0;
static int passwordSet = 0;
static char *rebootQuestion	= gettext_noop("Would you like to reboot now?");
static char *adminString	= gettext_noop("Using the administrator account "
	"'admin'.\nThere is no password set.");

// GUI stuff
static int graphics = 0;
static objectKey window = NULL;
static objectKey welcomeLabel = NULL;
static objectKey copyrightLabel = NULL;
static objectKey instLabel = NULL;
static objectKey instButton = NULL;
static objectKey contButton = NULL;
static objectKey langButton = NULL;
static objectKey goAwayCheckbox = NULL;
static image flagImage;


static void setDefaults(void)
{
	char language[6];
	char charsetName[CHARSET_NAME_LEN];
	char keyMapName[KEYMAP_NAMELEN];
	char keyMapFile[MAX_PATH_NAME_LENGTH + 1];

	if (getenv(ENV_LANG))
	{
		strncpy(language, getenv(ENV_LANG), (sizeof(language) - 1));
	}
	else
	{
		if (configGet(PATH_SYSTEM_CONFIG "/environment.conf", ENV_LANG,
			language, (sizeof(language) - 1)) < 0)
		{
			strcpy(language, LANG_ENGLISH);
		}

		setenv(ENV_LANG, language, 1);
	}

	// Based on the default language, try to set an appropriate character
	// set variable
	if (configGet(PATH_SYSTEM_CONFIG "/charset.conf", language, charsetName,
		CHARSET_NAME_LEN) >= 0)
	{
		setenv(ENV_CHARSET, charsetName, 1);
	}

	// Based on the default language, try to set an appropriate keymap
	// variable
	if (configGet(PATH_SYSTEM_CONFIG "/keymap.conf", language, keyMapName,
		KEYMAP_NAMELEN) >= 0)
	{
		sprintf(keyMapFile, PATH_SYSTEM_KEYMAPS "/%s.map", keyMapName);

		if (fileFind(keyMapFile, NULL) >= 0)
		{
			keyboardSetMap(keyMapFile);
			setenv(ENV_KEYMAP, keyMapName, 1);
		}
	}

	setlocale(LC_ALL, language);
	textdomain("imgboot");
}


__attribute__((noinline)) // crashes
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
		printf(_("\n\nERROR: %s\n\n"), output);
}


__attribute__((format(printf, 2, 3))) __attribute__((noreturn))
static void quit(int status, const char *message, ...)
{
	// Shut everything down

	va_list list;
	char output[MAXSTRINGLENGTH];

	if (message)
	{
		va_start(list, message);
		vsnprintf(output, MAXSTRINGLENGTH, message, list);
		va_end(list);
	}

	if (graphics)
		windowGuiStop();

	if ((status < 0) && message)
		error(_("%s  Quitting."), output);

	if (graphics && window)
		windowDestroy(window);

	errno = status;

	exit(status);
}


static int rebootNow(void)
{
	int response = 0;
	char character;

	if (graphics)
	{
		response = windowNewChoiceDialog(window, _("Reboot?"),
			_(rebootQuestion), (char *[]){ _("Reboot"), _("Continue") },
			2, 0);
		if (!response)
			return (1);
		else
			return (0);
	}
	else
	{
		printf(_("\n%s (y/n): "), _(rebootQuestion));
		textInputSetEcho(0);

		while (1)
		{
			character = getchar();

			if ((character == 'y') || (character == 'Y'))
			{
				printf("%s", _("Yes\n"));
				textInputSetEcho(1);
				return (1);
			}
			else if ((character == 'n') || (character == 'N'))
			{
				printf("%s", _("No\n"));
				textInputSetEcho(1);
				return (0);
			}
		}
	}
}


static void doEject(void)
{
	static disk sysDisk;

	memset(&sysDisk, 0, sizeof(disk));

	if (fileGetDisk("/", &sysDisk) >= 0)
	{
		if (sysDisk.type & DISKTYPE_CDROM)
		{
			if (diskSetLockState(sysDisk.name, 0) >= 0)
			{
				if (diskSetDoorState(sysDisk.name, 1) < 0)
					// Try a second time.  Sometimes 2 attempts seems to help.
					diskSetDoorState(sysDisk.name, 1);
			}
		}
	}
}


static int runLogin(void)
{
	int pid = 0;

	if (!passwordSet)
		pid = loaderLoadProgram(LOGINPROGRAM " -f admin", 0);
	else
		pid = loaderLoadProgram(LOGINPROGRAM, 0);

	if (!graphics)
		// Give the login program a copy of the I/O streams
		multitaskerDuplicateIO(processId, pid, 0);

	loaderExecProgram(pid, 0);

	return (pid);
}


static int loadFlagImage(const char *lang, image *img)
{
	int status = 0;
	char path[MAX_PATH_LENGTH];
	file f;

	sprintf(path, "%s/flag-%s.bmp", PATH_SYSTEM_LOCALE, lang);

	status = fileFind(path, &f);
	if (status < 0)
		return (status);

	status = imageLoad(path, 30, 20, img);

	return (status);
}


static void refreshWindow(void)
{
	char versionString[32];
	char title[80];

	// Re-get the language setting
	setlocale(LC_ALL, getenv(ENV_LANG));
	textdomain("imgboot");

	// Re-get the character set
	if (getenv(ENV_CHARSET))
		windowSetCharSet(window, getenv(ENV_CHARSET));

	// Refresh the 'copyright' label
	windowComponentSetData(copyrightLabel, COPYRIGHT, strlen(COPYRIGHT),
		1 /* redraw */);

	if (haveInstall)
	{
		// Refresh the 'install' label
		windowComponentSetData(instLabel, INSTALLQUEST,	strlen(INSTALLQUEST),
			1 /* redraw */);

		// Refresh the 'install' button
		windowComponentSetData(instButton, INSTALL, strlen(INSTALL),
			1 /* redraw */);
	}

	// Refresh the 'continue' button
	windowComponentSetData(contButton, CONTINUE, strlen(CONTINUE),
		1 /* redraw */);

	if (langButton)
	{
		// Refresh the 'language' button
		if (flagImage.data)
			imageFree(&flagImage);
		if (loadFlagImage(getenv(ENV_LANG), &flagImage) >= 0)
			windowComponentSetData(langButton, &flagImage, sizeof(image),
				1 /* redraw */);
	}

	// Refresh the 'go away' checkbox
	windowComponentSetData(goAwayCheckbox, DONTASK, strlen(DONTASK),
		1 /* redraw */);

	// Refresh the window title
	getVersion(versionString, sizeof(versionString));
	sprintf(title, WELCOME, versionString);
	windowSetTitle(window, title);
}


static void chooseLanguage(void)
{
	char pickedLanguage[6];
	char charsetName[CHARSET_NAME_LEN];
	char keyMapName[KEYMAP_NAMELEN];

	if (windowNewLanguageDialog(window, pickedLanguage) >= 0)
	{
		setenv(ENV_LANG, pickedLanguage, 1);

		// Based on the chosen language, try to set an appropriate character
		// set variable
		if (configGet(PATH_SYSTEM_CONFIG "/charset.conf", pickedLanguage,
			charsetName, CHARSET_NAME_LEN) >= 0)
		{
			setenv(ENV_CHARSET, charsetName, 1);
		}

		// Based on the chosen language, try to set an appropriate keymap
		// variable
		if (configGet(PATH_SYSTEM_CONFIG "/keymap.conf", pickedLanguage,
			keyMapName, KEYMAP_NAMELEN) >= 0)
		{
			setenv(ENV_KEYMAP, keyMapName, 1);
		}

		refreshWindow();
	}
}


static void eventHandler(objectKey key, windowEvent *event)
{
	// Check for the 'install' button
	if (haveInstall && (key == instButton) &&
		(event->type == EVENT_MOUSE_LEFTUP))
	{
		// Stop the GUI here and run the install program
		windowSetVisible(window, 0);
		loaderLoadAndExec(INSTALLPROGRAM, 0, 1);
		if (rebootNow())
		{
			doEject();
			shutdown(1, 1);
		}
		else
		{
			if (runLogin() >= 0)
				windowGuiStop();
		}
	}

	// Check for the 'continue' button
	else if ((key == contButton) && (event->type == EVENT_MOUSE_LEFTUP))
	{
		// Stop the GUI here and run the login program
		if (runLogin() >= 0)
			windowGuiStop();
	}

	// Check for the 'language' button
	else if (langButton && (key == langButton) &&
		(event->type == EVENT_MOUSE_LEFTUP))
	{
		chooseLanguage();
	}
}


static void constructWindow(void)
{
	// If we are in graphics mode, make a window rather than operating on the
	// command line.

	int status = 0;
	char versionString[32];
	char welcome[80];
	color background = { COLOR_DEFAULT_DESKTOP_BLUE,
		COLOR_DEFAULT_DESKTOP_GREEN, COLOR_DEFAULT_DESKTOP_RED };
	image splashImage;
	objectKey buttonContainer = NULL;
	file langDir;
	componentParameters params;

	getVersion(versionString, sizeof(versionString));
	sprintf(welcome, WELCOME, versionString);

	// Create a new window
	window = windowNew(processId, welcome);
	if (!window)
		quit(ERR_NOCREATE, "%s", _("Can't create window!"));

	// No title bar or border for the login window
	windowSetHasTitleBar(window, 0);
	windowSetHasBorder(window, 0);

	// Background color same as the desktop
	windowGetColor(COLOR_SETTING_DESKTOP, &background);
	windowSetBackgroundColor(window, &background);

	memset(&params, 0, sizeof(componentParameters));
	params.gridWidth = 1;
	params.gridHeight = 1;
	params.padTop = 5;
	params.padLeft = 5;
	params.padRight = 5;
	params.orientationX = orient_left;
	params.orientationY = orient_middle;
	params.flags = (WINDOW_COMPFLAG_CUSTOMFOREGROUND |
		WINDOW_COMPFLAG_CUSTOMBACKGROUND);
	params.foreground = COLOR_WHITE;
	memcpy(&params.background, &background, sizeof(color));
	welcomeLabel = windowNewTextLabel(window, welcome, &params);

	params.gridY += 1;
	memcpy(&params.background, &background, sizeof(color));
	copyrightLabel = windowNewTextLabel(window, COPYRIGHT, &params);

	// Try to load a splash image to go at the top of the window
	params.orientationX = orient_center;
	memset(&splashImage, 0, sizeof(image));
	if (fileFind(PATH_SYSTEM "/visopsys.jpg", NULL) >= 0)
	{
		status = imageLoad(PATH_SYSTEM "/visopsys.jpg", 0, 0, &splashImage);
		if (status >= 0)
		{
			// Create an image component from it, and add it to the window
			params.gridY += 1;
			windowNewImage(window, &splashImage, draw_normal, &params);
		}
	}

	if (haveInstall)
	{
		params.gridY += 1;
		instLabel = windowNewTextLabel(window, INSTALLQUEST, &params);
	}

	params.gridY += 1;
	params.flags = (WINDOW_COMPFLAG_FIXEDWIDTH | WINDOW_COMPFLAG_FIXEDHEIGHT);
	buttonContainer = windowNewContainer(window, "buttonContainer", &params);
	if (buttonContainer)
	{
		if (haveInstall)
		{
			params.orientationX = orient_right;
			instButton = windowNewButton(buttonContainer, INSTALL, NULL,
				&params);
			windowRegisterEventHandler(instButton, &eventHandler);

			params.gridX += 1;
			params.orientationX = orient_center;
		}
		else
		{
			params.orientationX = orient_right;
		}

		contButton = windowNewButton(buttonContainer, CONTINUE, NULL, &params);
		windowRegisterEventHandler(contButton, &eventHandler);
		windowComponentFocus(contButton);

		// Does the 'locale' directory exist?  Anything in it?
		if (fileFind(PATH_SYSTEM_LOCALE, &langDir) >= 0)
		{
			params.gridX += 1;
			params.orientationX = orient_left;
			if (getenv(ENV_LANG))
				status = loadFlagImage(getenv(ENV_LANG), &flagImage);
			else
				status = loadFlagImage(LANG_ENGLISH, &flagImage);

			if (status >= 0)
				langButton = windowNewButton(buttonContainer, NULL, &flagImage,
					&params);
			else
				langButton = windowNewButton(buttonContainer, LANGUAGE, NULL,
					&params);

			windowRegisterEventHandler(langButton, &eventHandler);
		}
	}

	params.gridX = 0;
	params.gridY += 1;
	params.padBottom = 5;
	params.orientationX = orient_center;
	params.flags = (WINDOW_COMPFLAG_CUSTOMFOREGROUND |
		WINDOW_COMPFLAG_CUSTOMBACKGROUND);
	params.foreground = COLOR_WHITE;
	memcpy(&params.background, &background, sizeof(color));
	goAwayCheckbox = windowNewCheckbox(window, DONTASK, &params);
	if (readOnly)
		windowComponentSetEnabled(goAwayCheckbox, 0);

	windowSetVisible(window, 1);
}


static inline void changeStartProgram(void)
{
	configSet(PATH_SYSTEM_CONFIG "/kernel.conf", "start.program",
		LOGINPROGRAM);
}


__attribute__((noreturn))
int main(int argc, char *argv[])
{
	char opt;
	disk sysDisk;
	int numOptions = 0;
	int defOption = 0;
	char *instOption = gettext_noop("o Install                    ");
	char *contOption = gettext_noop("o Continue                   ");
	char *naskOption = gettext_noop("o Always continue (never ask)");
	char *optionStrings[3] = { NULL, NULL, NULL };
	int selected = 0;

	// Default language, character set, etc.
	setDefaults();

	processId = multitaskerGetCurrentProcessId();

	// Are graphics enabled?
	graphics = graphicsAreEnabled();

	// Check privilege level
	if (multitaskerGetProcessPrivilege(processId))
		quit(ERR_PERMISSION, "%s", _("This program can only be run as a "
			"privileged user.\n(Try logging in as user \"admin\")."));

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
				quit(ERR_INVALID, _("Unknown option '%c'"), optopt);
		}
	}

	// Find out whether we are currently running on a read-only filesystem
	memset(&sysDisk, 0, sizeof(disk));
	if (!fileGetDisk(PATH_SYSTEM, &sysDisk))
		readOnly = sysDisk.readOnly;

	// Find out whether we have an install program.
	if (fileFind(INSTALLPROGRAM, NULL) >= 0)
		haveInstall = 1;

	// Is there a password on the administrator account?
	if (userAuthenticate(USER_ADMIN, "") < 0)
		passwordSet = 1;

	if (graphics)
	{
		constructWindow();
		windowGuiRun();

		// If the user selected the 'go away' checkbox, change the start
		// program in the kernel's config file.
		windowComponentGetSelected(goAwayCheckbox, &selected);
		if (selected)
		{
			changeStartProgram();

			windowSetVisible(window, 0);

			if (!passwordSet)
			{
				// Tell the user about the admin account
				windowNewInfoDialog(window, _("Administrator account"),
					_(adminString));
			}
		}
	}
	else
	{
	restart:
		// Print title message, and ask whether to install or run
		printf("\n%s\n", GPL);

		numOptions = 0;

		if (haveInstall)
		{
			optionStrings[numOptions] = _(instOption);
			defOption = numOptions;
			numOptions += 1;
		}

		optionStrings[numOptions] = _(contOption);
		defOption = numOptions;
		numOptions += 1;

		if (!readOnly)
		{
			optionStrings[numOptions] = _(naskOption);
			numOptions += 1;
		}

		if (numOptions > 1)
		{
			selected = vshCursorMenu(_("\nPlease select from the following "
				"options"), optionStrings, numOptions, 10 /* max rows */,
				defOption);
		}
		else
		{
			selected = defOption;
		}

		if (selected < 0)
		{
			doEject();
			shutdown(1, 1);
		}
		else if (optionStrings[selected] == _(instOption))
		{
			// Install
			loaderLoadAndExec(INSTALLPROGRAM, 0, 1);
			if (rebootNow())
			{
				doEject();
				shutdown(1, 1);
			}
			else
			{
				if (runLogin() < 0)
					goto restart;
			}
		}
		else
		{
			if (optionStrings[selected] == _(naskOption))
			{
				changeStartProgram();
				if (!passwordSet)
					// Tell the user about the admin account
					printf("\n%s\n", _(adminString));
			}

			if (runLogin() < 0)
				goto restart;
		}
	}

	quit(0, NULL);
}

