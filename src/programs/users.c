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
//  users.c
//

// This is a program for managing users and passwords.

// Password length checking added by ap0r <marianopbernacki@gmail.com>

/* This is the text that appears when a user requests help about this program
<help>

 -- users --

User manager for creating/deleting user accounts

Usage:
  users [-p user_name]

The users (User Manager) program is interactive, and may only be used in
graphics mode.  It can be used to add and delete user accounts, and set
account passwords.  If '-p user_name' is specified on the command line,
this command will prompt the user to set the password for the named user.

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
#include <sys/ascii.h>
#include <sys/env.h>
#include <sys/keyboard.h>
#include <sys/paths.h>
#include <sys/user.h>

#define _(string) gettext(string)

#define WINDOW_TITLE	_("User Manager")
#define ADD_USER		_("Add User")
#define DELETE_USER		_("Delete User")
#define SET_PASSWORD	_("Set Password")
#define SET_LANGUAGE	_("Set Language")

static int processId = 0;
static int privilege = 0;
static char currentUser[USER_MAX_NAMELENGTH + 1];
static int readOnly = 1;
static listItemParameters *userListParams = NULL;
static int numUserNames = 0;
static objectKey window = NULL;
static objectKey userList = NULL;
static objectKey addUserButton = NULL;
static objectKey deleteUserButton = NULL;
static objectKey setPasswordButton = NULL;
static objectKey setLanguageButton = NULL;


__attribute__((format(printf, 1, 2)))
static void error(const char *format, ...)
{
	// Generic error message code for either text or graphics modes

	va_list list;
	char output[MAXSTRINGLENGTH];

	va_start(list, format);
	vsnprintf(output, MAXSTRINGLENGTH, format, list);
	va_end(list);

	windowNewErrorDialog(window, _("Error"), output);
}


static int getUserNames(void)
{
	// Get the list of user names from the kernel

	int status = 0;
	char userBuffer[1024];
	char *bufferPointer = NULL;
	int count;

	memset(userBuffer, 0, 1024);

	numUserNames = userGetNames(userBuffer, 1024);
	if (numUserNames < 0)
	{
		error("%s", _("Error getting user names"));
		return (numUserNames);
	}

	userListParams = malloc(numUserNames * sizeof(listItemParameters));
	if (!userListParams)
		return (status = ERR_MEMORY);

	bufferPointer = userBuffer;

	for (count = 0; count < numUserNames; count ++)
	{
		strncpy(userListParams[count].text, bufferPointer,
			WINDOW_MAX_LABEL_LENGTH);
		bufferPointer += (strlen(userListParams[count].text) + 1);
	}

	return (status = 0);
}


static int setPassword(const char *userName, const char *oldPassword,
	const char *newPassword)
{
	// Tells the kernel to set the requested password

	int status = 0;

	// Tell the kernel to add the user
	status = userSetPassword(userName, oldPassword, newPassword);
	if (status < 0)
		return (status);

	return (status = 0);
}


static int setPasswordDialog(int userNumber)
{
	// Show a 'set password' dialog box

	int status = 0;
	objectKey dialogWindow = NULL;
	componentParameters params;
	objectKey oldPasswordField = NULL;
	objectKey passwordField1 = NULL;
	objectKey passwordField2 = NULL;
	objectKey noMatchLabel = NULL;
	objectKey shortPasswordLabel = NULL;
	objectKey okButton = NULL;
	objectKey cancelButton = NULL;
	windowEvent event;
	char confirmPassword[USER_MAX_PASSWDLENGTH + 1];
	char oldPassword[USER_MAX_PASSWDLENGTH + 1];
	char newPassword[USER_MAX_PASSWDLENGTH + 1];

	memset(&params, 0, sizeof(componentParameters));

	// Create the dialog
	if (window)
		dialogWindow = windowNewDialog(window, SET_PASSWORD);
	else
		dialogWindow = windowNew(processId, SET_PASSWORD);

	if (!dialogWindow)
		return (status = ERR_NOCREATE);

	params.gridWidth = 1;
	params.gridHeight = 1;
	params.padLeft = 5;
	params.padRight = 5;
	params.padTop = 5;
	params.orientationX = orient_center;
	params.orientationY = orient_middle;

	char labelText[64];
	sprintf(labelText, _("User name: %s"), userListParams[userNumber].text);
	params.gridY = 0;
	params.gridWidth = 2;
	windowNewTextLabel(dialogWindow, labelText, &params);

	// If this user is privileged, or we can authenticate with no password,
	// don't prompt for the old password
	if (privilege && userAuthenticate(userListParams[userNumber].text, ""))
	{
		params.gridY = 1;
		params.gridWidth = 1;
		params.padRight = 0;
		params.orientationX = orient_right;
		windowNewTextLabel(dialogWindow, _("Old password:"), &params);

		params.gridX = 1;
		params.orientationX = orient_left;
		params.padRight = 5;
		oldPasswordField = windowNewPasswordField(dialogWindow,
			(USER_MAX_PASSWDLENGTH + 1), &params);
	}

	params.gridX = 0;
	params.gridY = 2;
	params.gridWidth = 1;
	params.padRight = 0;
	params.orientationX = orient_right;
	windowNewTextLabel(dialogWindow, _("New password:"), &params);

	params.gridX = 1;
	params.padRight = 5;
	params.orientationX = orient_left;
	passwordField1 = windowNewPasswordField(dialogWindow,
		(USER_MAX_PASSWDLENGTH + 1), &params);

	if (oldPasswordField)
		windowComponentFocus(oldPasswordField);
	else
		windowComponentFocus(passwordField1);

	params.gridX = 0;
	params.gridY = 3;
	params.padRight = 0;
	params.orientationX = orient_right;
	windowNewTextLabel(dialogWindow, _("Confirm password:"), &params);

	params.gridX = 1;
	params.orientationX = orient_left;
	params.padRight = 5;
	passwordField2 = windowNewPasswordField(dialogWindow,
		(USER_MAX_PASSWDLENGTH + 1), &params);

	// Create a passwords do not match label, and hide it
	params.gridX = 0;
	params.gridY = 4;
	params.gridWidth = 2;
	params.orientationX = orient_center;
	noMatchLabel = windowNewTextLabel(dialogWindow,
		_("Passwords do not match"), &params);
	windowComponentSetVisible(noMatchLabel, 0);

	// Create password too short label and hide it
	shortPasswordLabel = windowNewTextLabel(dialogWindow, _("Password should "
		"be longer"), &params);
	windowComponentSetVisible(shortPasswordLabel, 0);

	// Create the OK button
	params.gridY = 5;
	params.gridWidth = 1;
	params.padBottom = 5;
	params.padLeft = 5;
	params.padRight = 5;
	params.orientationX = orient_right;
	params.flags |= WINDOW_COMPFLAG_FIXEDWIDTH;
	okButton = windowNewButton(dialogWindow, _("OK"), NULL, &params);

	// Create the Cancel button
	params.gridX = 1;
	params.orientationX = orient_left;
	cancelButton = windowNewButton(dialogWindow, _("Cancel"), NULL, &params);

	windowCenterDialog(window, dialogWindow);
	windowSetVisible(dialogWindow, 1);

	while (1)
	{
		// Check for the OK button
		status = windowComponentEventGet(okButton, &event);
		if (status < 0)
			goto out;
		else if ((status > 0) && (event.type == EVENT_MOUSE_LEFTUP))
			break;

		// Check for the Cancel button
		status = windowComponentEventGet(cancelButton, &event);
		if ((status < 0) || ((status > 0) &&
			(event.type == EVENT_MOUSE_LEFTUP)))
		{
			windowDestroy(dialogWindow);
			return (status = ERR_NODATA);
		}

		// Check for window close events
		status = windowComponentEventGet(dialogWindow, &event);
		if ((status < 0) || ((status > 0) &&
			(event.type == EVENT_WINDOW_CLOSE)))
		{
			windowDestroy(dialogWindow);
			return (status = ERR_NODATA);
		}

		// Check for keyboard events
		if (oldPasswordField)
		{
			status = windowComponentEventGet(oldPasswordField, &event);
			if ((status > 0) && (event.type == EVENT_KEY_DOWN) &&
				(event.key == keyEnter))
			{
				break;
			}
		}

		// Read the old password field and check for changes
		status = windowComponentEventGet(passwordField1, &event);
		if ((status > 0) && (event.type == EVENT_KEY_DOWN))
		{
			if (event.key == keyEnter)
				break;

			// Clear all existing labels
			windowComponentSetVisible(shortPasswordLabel, 0);
			windowComponentSetVisible(noMatchLabel, 0);

			// Read data from the password fields
			windowComponentGetData(passwordField1, newPassword,
				USER_MAX_PASSWDLENGTH);
			windowComponentGetData(passwordField2, confirmPassword,
				USER_MAX_PASSWDLENGTH);

			// Test to see if passwords match
			if (strncmp(newPassword, confirmPassword,
				USER_MAX_PASSWDLENGTH))
			{
				// Passwords do not match.  Show the no match label and
				// disable the OK button
				windowComponentSetVisible(noMatchLabel, 1);
				windowComponentSetEnabled(okButton, 0);
			}
			else
			{
				// If passwords matched, enable the OK button and check
				// for password length.

				// The OK button is enabled because password length is not
				// enforced
				windowComponentSetEnabled(okButton, 1);
				if (strlen(newPassword) < 8)
					windowComponentSetVisible(shortPasswordLabel, 1);
			}
		}

		// Read the new password field and check for changes
		status = windowComponentEventGet(passwordField2, &event);
		if ((status > 0) && (event.type == EVENT_KEY_DOWN))
		{
			if (event.key == keyEnter)
				break;

			// Clear all existing labels
			windowComponentSetVisible(shortPasswordLabel, 0);
			windowComponentSetVisible(noMatchLabel, 0);

			// Read data from the password fields
			windowComponentGetData(passwordField1, newPassword,
				USER_MAX_PASSWDLENGTH);
			windowComponentGetData(passwordField2, confirmPassword,
				USER_MAX_PASSWDLENGTH);

			// Test to see if passwords match
			if (strncmp(newPassword, confirmPassword, USER_MAX_PASSWDLENGTH))
			{
				// Passwords do not match.  Show the no match label and
				// disable the OK button
				windowComponentSetVisible(noMatchLabel, 1);
				windowComponentSetEnabled(okButton, 0);
			}
			else
			{
				// If passwords matched, enable the OK button and check
				// for password length.

				// The OK button is enabled because password length is not
				// enforced
				windowComponentSetEnabled(okButton, 1);

				if (strlen(newPassword) < 8)
					windowComponentSetVisible(shortPasswordLabel, 1);
			}
		}

		// Done
		multitaskerYield();
	}

	if (oldPasswordField)
		windowComponentGetData(oldPasswordField, oldPassword,
			USER_MAX_PASSWDLENGTH);
	else
		oldPassword[0] = '\0';

	windowComponentGetData(passwordField1, newPassword, USER_MAX_PASSWDLENGTH);
	windowComponentGetData(passwordField2, confirmPassword,
		USER_MAX_PASSWDLENGTH);

out:
	windowDestroy(dialogWindow);

	// Make sure the new password and confirm passwords match
	if (!strncmp(newPassword, confirmPassword, USER_MAX_PASSWDLENGTH))
	{
		status = setPassword(userListParams[userNumber].text, oldPassword,
			newPassword);
		if (status == ERR_PERMISSION)
			error("%s", _("Permission denied"));
		else if (status < 0)
			error("%s", _("Error setting password"));
	}
	else
	{
		error("%s", _("Passwords do not match"));
		status = ERR_INVALID;
	}

	return (status);
}


static void enableButtons(void)
{
	// Enable or disable buttons based on current user name/privilege, the
	// current user list selection, disk writabiliy, etc.

	int userNumber = -1;
	int isAdmin = 1;
	int isCurrentUser = 0;
	file langDir;

	windowComponentGetSelected(userList, &userNumber);
	if (userNumber >= 0)
	{
		isAdmin = !strcmp(userListParams[userNumber].text, USER_ADMIN);
		isCurrentUser = !strcmp(userListParams[userNumber].text, currentUser);
	}

	windowComponentSetEnabled(addUserButton, (!readOnly && !privilege));

	windowComponentSetEnabled(deleteUserButton, (!readOnly && !privilege));

	windowComponentSetEnabled(setPasswordButton,
		(!readOnly && (!privilege || isCurrentUser)));

	windowComponentSetEnabled(setLanguageButton,
		(!isAdmin && (!privilege || isCurrentUser) &&
			(fileFind(PATH_SYSTEM_LOCALE, &langDir) >= 0)));
}


static int addUser(const char *userName, const char *password)
{
	// Tells the kernel to add the requested user name and password

	int status = 0;
	char userDir[MAX_PATH_NAME_LENGTH];
	file f;

	// Make sure the user doesn't already exist
	if (userExists(userName))
	{
		error(_("User \"%s\" already exists."), userName);
		return (status = ERR_ALREADY);
	}

	// Tell the kernel to add the user
	status = userAdd(userName, password);
	if (status < 0)
	{
		error("%s", _("Error adding user"));
		return (status);
	}

	// Try to create the user directory
	snprintf(userDir, MAX_PATH_NAME_LENGTH, PATH_USERS "/%s", userName);
	if (fileFind(userDir, &f) < 0)
	{
		status = fileMakeDir(userDir);
		if (status < 0)
			error("%s", _("Warning: couldn't create user directory"));
	}

	// Refresh our list of user names
	status = getUserNames();
	if (status < 0)
		return (status);

	// Re-populate our list component
	status = windowComponentSetData(userList, userListParams, numUserNames,
		1 /* redraw */);
	if (status < 0)
		return (status);

	// Enable/disable buttons
	enableButtons();

	return (status = 0);
}


static int deleteUser(const char *userName)
{
	// Tells the kernel to delete the requested user

	int status = 0;

	// Tell the kernel to delete the user
	status = userDelete(userName);
	if (status < 0)
	{
		if (status == ERR_PERMISSION)
			error("%s", _("Permission denied"));
		else
			error("%s", _("Error deleting user"));
		return (status);
	}

	// Refresh our list of user names
	status = getUserNames();
	if (status < 0)
		return (status);

	// Re-populate our list component
	status = windowComponentSetData(userList, userListParams, numUserNames,
		1 /* redraw */);
	if (status < 0)
		return (status);

	// Enable/disable buttons
	enableButtons();

	return (status = 0);
}


static int setLanguage(const char *userName, const char *language)
{
	// Try to set the user's language choice in its environment settings

	int status = 0;
	char fileName[MAX_PATH_NAME_LENGTH];
	file f;
	char charsetName[CHARSET_NAME_LEN];
	char keyMapName[KEYMAP_NAMELEN];
	variableList envList;

	// The user 'admin' doesn't have user settings
	if (!strcmp(userName, USER_ADMIN))
		return (status = ERR_INVALID);

	if (!readOnly)
	{
		// Does the user have a config dir?
		sprintf(fileName, PATH_USERS_CONFIG, userName);
		if (fileFind(fileName, &f) < 0)
		{
			// No, try to create it.
			status = fileMakeDir(fileName);
			if (status < 0)
				return (status);
		}

		// Does the user have an environment config file?
		sprintf(fileName, PATH_USERS_CONFIG "/environment.conf", userName);

		status = fileFind(fileName, &f);
		if (status < 0)
		{
			// Doesn't exist.  Create an empty list.
			status = variableListCreate(&envList);
			if (status < 0)
				return (status);
		}
		else
		{
			// There's a file.  Try to read it.
			status = configRead(fileName, &envList);
			if (status < 0)
				return (status);
		}

		// Set the language variable.
		status = variableListSet(&envList, ENV_LANG, language);
		if (status < 0)
			return (status);

		// Based on the language variable, try to set an appropriate character
		// set variable
		if (configGet(PATH_SYSTEM_CONFIG "/charset.conf", language,
			charsetName, CHARSET_NAME_LEN) >= 0)
		{
			status = variableListSet(&envList, ENV_CHARSET, charsetName);
			if (status < 0)
				return (status);
		}

		// Based on the language variable, try to set an appropriate keymap
		// variable
		if (configGet(PATH_SYSTEM_CONFIG "/keymap.conf", language,
			keyMapName, KEYMAP_NAMELEN) >= 0)
		{
			status = variableListSet(&envList, ENV_KEYMAP, keyMapName);
			if (status < 0)
				return (status);
		}

		// Write the config file.
		status = configWrite(fileName, &envList);

		variableListDestroy(&envList);

		if (!strcmp(userName, currentUser))
			windowRefresh();
	}

	return (status);
}


static void refreshWindow(void)
{
	// We got a 'window refresh' event (probably because of a language switch),
	// so we need to update things

	// Re-get the language setting
	setlocale(LC_ALL, getenv(ENV_LANG));
	textdomain("users");

	// Re-get the character set
	if (getenv(ENV_CHARSET))
		windowSetCharSet(window, getenv(ENV_CHARSET));

	// Refresh the window title
	windowSetTitle(window, WINDOW_TITLE);

	// Refresh the 'add user' button
	windowComponentSetData(addUserButton, ADD_USER, strlen(ADD_USER),
		1 /* redraw */);

	// Refresh the 'delete user' button
	windowComponentSetData(deleteUserButton, DELETE_USER, strlen(DELETE_USER),
		1 /* redraw */);

	// Refresh the 'set password' button
	windowComponentSetData(setPasswordButton, SET_PASSWORD,
		strlen(SET_PASSWORD), 1 /* redraw */);

	// Refresh the 'set language' button
	windowComponentSetData(setLanguageButton, SET_LANGUAGE,
		strlen(SET_LANGUAGE), 1 /* redraw */);
}


static void eventHandler(objectKey key, windowEvent *event)
{
	int status = 0;
	char userName[USER_MAX_NAMELENGTH + 1];
	int userNumber = 0;
	char pickedLanguage[6];

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

	else if ((key == userList) && (event->type & EVENT_SELECTION))
	{
		// Enable/disable buttons
		enableButtons();
	}

	else if ((key == addUserButton) && (event->type == EVENT_MOUSE_LEFTUP))
	{
		if (windowNewPromptDialog(window, _("Add User"),
			_("Enter the user name:"), 1, USER_MAX_NAMELENGTH, userName) > 0)
		{
			if (addUser(userName, "") < 0)
				return;
			setPasswordDialog(numUserNames - 1);
		}
	}

	else if ((key == deleteUserButton) && (event->type == EVENT_MOUSE_LEFTUP))
	{
		// Don't try to delete the last user
		if (numUserNames > 1)
		{
			windowComponentGetSelected(userList, &userNumber);
			if (userNumber < 0)
				return;

			char question[1024];
			sprintf(question, _("Delete user %s?"),
				userListParams[userNumber].text);
			if (windowNewQueryDialog(window, _("Delete?"), question))
				deleteUser(userListParams[userNumber].text);
		}
		else
			error("%s", _("Can't delete the last user"));
	}

	else if ((key == setPasswordButton) && (event->type == EVENT_MOUSE_LEFTUP))
	{
		windowComponentGetSelected(userList, &userNumber);
		if (userNumber < 0)
			return;

		setPasswordDialog(userNumber);
	}

	else if ((key == setLanguageButton) && (event->type == EVENT_MOUSE_LEFTUP))
	{
		windowComponentGetSelected(userList, &userNumber);
		if (userNumber < 0)
			return;

		status = windowNewLanguageDialog(window, pickedLanguage);
		if (status < 0)
			return;

		status = setLanguage(userListParams[userNumber].text, pickedLanguage);
		if (status < 0)
			error("%s", _("Couldn't save the language choice"));
	}
}


static void constructWindow(void)
{
	// If we are in graphics mode, make a window rather than operating on the
	// command line.

	componentParameters params;
	listItemParameters itemParams;
	objectKey container = NULL;

	// Create a new window
	window = windowNew(processId, WINDOW_TITLE);
	if (!window)
		return;

	// Make sure the user list is wide enough to accommodate the longest
	// possible name
	memset(&itemParams, 0, sizeof(listItemParameters));
	memset(itemParams.text, '@', USER_MAX_NAMELENGTH);

	// The list of user names
	memset(&params, 0, sizeof(componentParameters));
	params.gridWidth = 1;
	params.gridHeight = 1;
	params.padLeft = 5;
	params.padTop = 5;
	params.padBottom = 5;
	params.orientationX = orient_left;
	params.orientationY = orient_top;
	userList = windowNewList(window, windowlist_textonly, 5, 1, 0,
		&itemParams, 1, &params);
	windowRegisterEventHandler(userList, &eventHandler);
	windowComponentSetData(userList, userListParams, numUserNames,
		1 /* redraw */);
	windowComponentFocus(userList);

	// A container for the buttons
	params.gridX += 1;
	params.padRight = 5;
	params.flags |= (WINDOW_COMPFLAG_FIXEDWIDTH | WINDOW_COMPFLAG_FIXEDHEIGHT);
	container = windowNewContainer(window, "button container", &params);

	// Create an 'add user' button
	params.gridX = 0;
	params.padLeft = params.padRight = params.padTop = 0;
	params.padBottom = 2;
	params.flags &= ~WINDOW_COMPFLAG_FIXEDWIDTH;
	addUserButton = windowNewButton(container, ADD_USER, NULL, &params);
	windowRegisterEventHandler(addUserButton, &eventHandler);

	// Create a 'delete user' button
	params.gridY += 1;
	deleteUserButton = windowNewButton(container, DELETE_USER, NULL, &params);
	windowRegisterEventHandler(deleteUserButton, &eventHandler);

	// Create a 'set password' button
	params.gridY += 1;
	setPasswordButton = windowNewButton(container, SET_PASSWORD, NULL,
		&params);
	windowRegisterEventHandler(setPasswordButton, &eventHandler);

	// Create a 'set language' button
	params.gridY += 1;
	params.padBottom = 0;
	setLanguageButton = windowNewButton(container, SET_LANGUAGE, NULL,
		&params);
	windowRegisterEventHandler(setLanguageButton, &eventHandler);

	// Enable/disable buttons
	enableButtons();

	// Register an event handler to catch window close events
	windowRegisterEventHandler(window, &eventHandler);

	windowSetVisible(window, 1);

	return;
}


int main(int argc, char *argv[])
{
	int status = 0;
	char opt;
	char userName[USER_MAX_NAMELENGTH + 1];
	int setPass = 0;
	disk sysDisk;
	int count;

	setlocale(LC_ALL, getenv(ENV_LANG));
	textdomain("users");

	// Only work in graphics mode
	if (!graphicsAreEnabled())
	{
		printf(_("\nThe \"%s\" command only works in graphics mode\n"),
			argv[0]);
		errno = ERR_NOTINITIALIZED;
		return (status = errno);
	}

	// Check options
	while (strchr("p?", (opt = getopt(argc, argv, "p"))))
	{
		switch (opt)
		{
			case 'p':
				strncpy(userName, optarg, USER_MAX_NAMELENGTH);
				setPass = 1;
				break;

			default:
				fprintf(stderr, _("Unknown option '%c'\n"), optopt);
				errno = status = ERR_INVALID;
				return (status);
		}
	}

	// Find out whether we are currently running on a read-only filesystem
	memset(&sysDisk, 0, sizeof(disk));
	if (!fileGetDisk(PATH_SYSTEM, &sysDisk))
		readOnly = sysDisk.readOnly;

	processId = multitaskerGetCurrentProcessId();
	privilege = multitaskerGetProcessPrivilege(processId);
	userGetCurrent(currentUser, USER_MAX_NAMELENGTH);

	// Get the list of user names
	status = getUserNames();
	if (status < 0)
	{
		errno = status;
		perror(argv[0]);
		return (status);
	}

	if (setPass)
	{
		int userNumber = -1;

		// We're just setting the password for the requested user name.  Find
		// the user number in our list
		for (count = 0; count < numUserNames; count ++)
		{
			if (!strcmp(userListParams[count].text, userName))
			{
				userNumber = count;
				break;
			}
		}

		if (userNumber < 0)
			error(_("No such user \"%s\""), userName);
		else
		{
			if (!setPasswordDialog(userNumber))
				windowNewInfoDialog(window, _("Done"), _("Password set"));
		}
	}

	else
	{
		// Make our window
		constructWindow();

		// Run the GUI
		windowGuiRun();
		windowDestroy(window);
	}

	// Done
	if (userListParams)
		free(userListParams);

	return (errno = status);
}

