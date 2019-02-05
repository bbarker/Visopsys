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
//  login.c
//

// This is the current login process for Visopsys.

/* This is the text that appears when a user requests help about this program
<help>

 -- login --

Log in to the system.

Usage:
  login [-T] [-f user_name]

This program is interactive, and works in both text and graphics modes.  By
default, it is the program launched by the kernel after initialization has
completed.  It prompts for a user name and password, and after successful
authentication, launches either a shell process (text mode) or a window
system thread (graphics mode).

In graphics mode, 'login' also displays 'reboot' and 'shut down' buttons.
If the login program has crashed or been killed, you can start a new instance
using the [F1] key.

Options:
-T              : Force text mode operation
-f <user_name>  : Login as this user, no password.

</help>
*/

#include <errno.h>
#include <libintl.h>
#include <locale.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/api.h>
#include <sys/ascii.h>
#include <sys/env.h>
#include <sys/errors.h>
#include <sys/lang.h>
#include <sys/paths.h>
#include <sys/window.h>

#define _(string) gettext(string)

#define LOGIN_SHELL	PATH_PROGRAMS "/vsh"
#define AUTHFAILED	_("Authentication failed")
#define LOGINNAME	_("Please enter your login name:")
#define LOGINPASS	_("Please enter your password:")
#define READONLY	_("You are running the system from a read-only device.\n" \
	"You will not be able to alter settings, or generally\nchange anything.")
#define MAX_LOGIN_LENGTH	64

// The following are only used if we are running a graphics mode login window.
static int graphics = 0;
static int readOnly = 1;
static image splashImage;
static objectKey window = NULL;
static objectKey textLabel = NULL;
static objectKey loginField = NULL;
static objectKey passwordField = NULL;
static objectKey rebootButton = NULL;
static objectKey shutdownButton = NULL;

static char login[MAX_LOGIN_LENGTH];
static char password[MAX_LOGIN_LENGTH];

typedef enum {
	halt, reboot

} shutdownType;


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
	textdomain("login");
}


static void printPrompt(void)
{
	// Print the login: prompt
	printf("%s", _("login: "));
	return;
}


static void processChar(char *buffer, unsigned char bufferChar, int echo)
{
	int currentCharacter = 0;
	char *tooLong = NULL;
	char *loginTooLong = _("That login name is too long.");
	char *passwordTooLong = _("That password is too long.");

	if (buffer == login)
		tooLong = loginTooLong;
	else if (buffer == password)
		tooLong = passwordTooLong;

	currentCharacter = strlen(buffer);

	// Make sure our buffer isn't full
	if (currentCharacter >= (MAX_LOGIN_LENGTH - 1))
	{
		buffer[0] = '\0';
		printf("\n");

		if (graphics)
		{
			windowNewErrorDialog(window, _("Error"), tooLong);
		}
		else
		{
			printf("%s\n", tooLong);
			printPrompt();
		}

		return;
	}

	if (bufferChar == (unsigned char) 8)
	{
		if (currentCharacter > 0)
		{
			buffer[currentCharacter - 1] = '\0';
			textBackSpace();
		}
	}

	else if (bufferChar == (unsigned char) 10)
	{
		printf("\n");
	}

	else
	{
		// Add the current character to the login buffer
		buffer[currentCharacter] = bufferChar;
		buffer[currentCharacter + 1] = '\0';

		if (echo)
			textPutc(bufferChar);
		else
			textPutc((int) '*');
	}

	return;
}


static void eventHandler(objectKey key, windowEvent *event)
{
	static int stage = 0;

	if (event->type == EVENT_MOUSE_LEFTUP)
	{
		if (key == rebootButton)
			systemShutdown(reboot, 0);

		else if (key == shutdownButton)
			systemShutdown(halt, 0);
	}

	else if ((event->type == EVENT_KEY_DOWN) && (event->key == keyEnter))
	{
		// Get the data from our field
		if (!stage)
		{
			windowComponentGetData(loginField, login, MAX_LOGIN_LENGTH);
			windowComponentSetData(loginField, "", 0, 1 /* redraw */);
			if (!strcmp(login, ""))
				return;
			windowComponentSetData(textLabel, LOGINPASS, strlen(LOGINPASS),
				1 /* redraw */);
			windowComponentSetVisible(loginField, 0);
			windowComponentSetVisible(passwordField, 1);
			windowComponentFocus(passwordField);
			stage = 1;
		}
		else
		{
			windowComponentGetData(passwordField, password, MAX_LOGIN_LENGTH);
			stage = 0;

			// Now we interpret the login
			windowGuiStop();
		}
	}
}


static void constructWindow(int myProcessId)
{
	// If we are in graphics mode, make a window rather than operating on the
	// command line.

	color background = { COLOR_DEFAULT_DESKTOP_BLUE,
		COLOR_DEFAULT_DESKTOP_GREEN, COLOR_DEFAULT_DESKTOP_RED };
	static char *splashName = PATH_SYSTEM "/visopsys.jpg";
	componentParameters params;

	// This function can be called multiple times.  Clear any event handlers
	// from previous calls
	windowClearEventHandlers();

	// Create a new window, with small, arbitrary size and location
	window = windowNew(myProcessId, _("Login"));
	if (!window)
		return;

	// No title bar or border for the login window
	windowSetHasTitleBar(window, 0);
	windowSetHasBorder(window, 0);

	// Background color same as the desktop
	windowGetColor(COLOR_SETTING_DESKTOP, &background);
	windowSetBackgroundColor(window, &background);

	memset(&params, 0, sizeof(componentParameters));
	params.gridWidth = 2;
	params.gridHeight = 1;
	params.padLeft = 5;
	params.padRight = 5;
	params.padTop = 5;
	params.orientationX = orient_center;
	params.orientationY = orient_top;

	if (!splashImage.data && (fileFind(splashName, NULL) >= 0))
		// Try to load a splash image to go at the top of the window
		imageLoad(splashName, 0, 0, &splashImage);

	if (splashImage.data)
		// Create an image component from it, and add it to the window
		windowNewImage(window, &splashImage, draw_normal, &params);

	// Put text labels in the window to prompt the user
	params.gridY += 1;
	params.flags = (WINDOW_COMPFLAG_CUSTOMFOREGROUND |
		WINDOW_COMPFLAG_CUSTOMBACKGROUND);
	params.foreground = COLOR_WHITE;
	memcpy(&params.background, &background, sizeof(color));
	textLabel = windowNewTextLabel(window, LOGINNAME, &params);

	// Add a login field
	params.gridY += 1;
	params.flags = (WINDOW_COMPFLAG_FIXEDHEIGHT | WINDOW_COMPFLAG_FIXEDWIDTH);
	params.font = fontGet(FONT_FAMILY_LIBMONO, (FONT_STYLEFLAG_BOLD |
		FONT_STYLEFLAG_FIXED), 10, NULL);
	loginField = windowNewTextField(window, 30, &params);
	windowRegisterEventHandler(loginField, &eventHandler);

	// Add a password field
	passwordField = windowNewPasswordField(window, 30, &params);
	windowComponentSetVisible(passwordField, 0);
	windowRegisterEventHandler(passwordField, &eventHandler);

	// Create a 'reboot' button
	params.gridY += 1;
	params.gridWidth = 1;
	params.padBottom = 5;
	params.orientationX = orient_right;
	params.font = NULL;
	rebootButton = windowNewButton(window, _("Reboot"), NULL, &params);
	windowRegisterEventHandler(rebootButton, &eventHandler);

	// Create a 'shutdown' button
	params.gridX = 1;
	params.orientationX = orient_left;
	shutdownButton = windowNewButton(window, _("Shut down"), NULL, &params);
	windowRegisterEventHandler(shutdownButton, &eventHandler);

	return;
}


static void getLogin(void)
{
	char bufferCharacter = '\0';

	// Clear the login name and password buffers
	login[0] = '\0';
	password[0] = '\0';

	if (graphics)
	{
		windowComponentSetVisible(passwordField, 0);
		windowComponentSetData(passwordField, "", 0, 1 /* redraw */);
		windowComponentSetData(textLabel, LOGINNAME, strlen(LOGINNAME),
			1 /* redraw */);
		windowComponentSetData(loginField, "", 0, 1 /* redraw */);
		windowComponentSetVisible(loginField, 1);
		windowComponentFocus(loginField);
		windowGuiRun();
	}
	else
	{
		// Turn keyboard echo off
		textInputSetEcho(0);

		printf("\n");
		printPrompt();

		// This loop grabs characters
		while (1)
		{
			bufferCharacter = getchar();
			processChar(login, bufferCharacter, 1);

			if (bufferCharacter == (unsigned char) 10)
			{
				if (strcmp(login, ""))
				{
					// Now we interpret the login
					break;
				}
				else
				{
					// The user hit 'enter' without typing anything.
					// Make a new prompt
					if (!graphics)
						printPrompt();

					continue;
				}
			}
		}

		printf("%s", _("password: "));

		// This loop grabs characters
		while (1)
		{
			bufferCharacter = getchar();
			processChar(password, bufferCharacter, 0);

			if (bufferCharacter == (unsigned char) 10)
				break;
		}

		// Turn keyboard echo back on
		textInputSetEcho(1);
	}
}


__attribute__((noreturn))
int main(int argc, char *argv[])
{
	int status = 0;
	char opt = '\0';
	int skipLogin = 0;
	int myPid = 0;
	int shellPid = 0;
	disk sysDisk;

	// Default language, character set, etc.
	setDefaults();

	// A lot of what we do is different depending on whether we're in graphics
	// mode or not.
	graphics = graphicsAreEnabled();

	if (graphics)
		memset(&splashImage, 0, sizeof(image));

	// Check options
	while (strchr("fT:?", (opt = getopt(argc, argv, "f:T"))))
	{
		switch (opt)
		{
			case 'f':
				// Login using the supplied user name and no password
				strncpy(login, optarg, MAX_LOGIN_LENGTH);
				skipLogin = 1;
				break;

			case 'T':
				// Force text mode
				graphics = 0;
				break;

			case ':':
				fprintf(stderr, _("Missing parameter for %s option\n"),
					argv[optind - 1]);
				goto skipOpts;

			default:
				fprintf(stderr, _("Unknown option '%c'\n"), optopt);
				goto skipOpts;
		}
	}

skipOpts:

	// Find out whether we are currently running on a read-only filesystem
	if (!fileGetDisk("/", &sysDisk))
		readOnly = sysDisk.readOnly;

	myPid = multitaskerGetCurrentProcessId();

	// Outer loop, from which we never exit
	while (1)
	{
		// Default language, character set, etc.
		setDefaults();

		if (graphics)
		{
			constructWindow(myPid);

			if (!skipLogin)
				windowSetVisible(window, 1);
		}

		// Inner loop, which goes until we authenticate successfully
		while (1)
		{
			if (!skipLogin)
				getLogin();

			skipLogin = 0;

			// We have a login name to process.  Authenticate the user and
			// log them into the system
			status = userLogin(login, password);
			if (status < 0)
			{
				if (graphics)
					windowNewErrorDialog(window, _("Error"), AUTHFAILED);
				else
					printf("\n*** %s ***\n\n", AUTHFAILED);

				if (graphics)
					windowSetVisible(window, 1);

				continue;
			}

			break;
		}

		if (graphics)
		{
			if (window)
				// Get rid of the login window.
				windowDestroy(window);

			// Log the user into the window manager.
			shellPid = windowLogin(login);
			if (shellPid < 0)
			{
				windowNewErrorDialog(window, _("Login Failed"),
					_("Unable to log in to the Window Manager!"));
				continue;
			}

			// Set the PID to the window manager thread.
			userSetPid(login, shellPid);

			if (readOnly)
				windowNewInfoDialog(NULL, _("Read Only"), READONLY);

			// Block on the window manager thread PID we were passed.
			multitaskerBlock(shellPid);

			// If we return to here, the login session is over.  Log the user
			// out of the window manager.
			windowLogout();
		}
		else
		{
			// Load a shell process
			shellPid = loaderLoadProgram(LOGIN_SHELL, userGetPrivilege(login));
			if (shellPid < 0)
			{
				printf(_("Couldn't load login shell %s!"), LOGIN_SHELL);
				continue;
			}

			// Set the PID to the shell process.
			userSetPid(login, shellPid);

			printf(_("\nWelcome %s\n"), login);
			if (readOnly)
				printf("\n%s\n", READONLY);

			// Run the text shell and block on it.
			loaderExecProgram(shellPid, 1 /* block */);

			// If we return to here, the login session is over.
		}

		// Log the user out of the system.
		userLogout(login);
	}

	// This function never returns under normal conditions.
}

