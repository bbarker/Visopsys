//
//  Visopsys
//  Copyright (C) 1998-2014 J. Andrew McLaughlin
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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/api.h>
#include <sys/ascii.h>

static int processId = 0;
static int privilege = 0;
static int readOnly = 1;
static listItemParameters *userListParams = NULL;
static int numUserNames = 0;
static objectKey window = NULL;
static objectKey userList = NULL;
static objectKey addUserButton = NULL;
static objectKey deleteUserButton = NULL;
static objectKey setPasswordButton = NULL;


__attribute__((format(printf, 1, 2)))
static void error(const char *format, ...)
{
	// Generic error message code for either text or graphics modes

	va_list list;
	char output[MAXSTRINGLENGTH];

	va_start(list, format);
	vsnprintf(output, MAXSTRINGLENGTH, format, list);
	va_end(list);

	windowNewErrorDialog(window, "Error", output);
}


static int getUserNames(void)
{
	// Get the list of user names from the kernel

	int status = 0;
	char userBuffer[1024];
	char *bufferPointer = NULL;
	int count;

	bzero(userBuffer, 1024);

	numUserNames = userGetNames(userBuffer, 1024);
	if (numUserNames < 0)
	{
		error("Error getting user names");
		return (numUserNames);
	}

	userListParams = malloc(numUserNames * sizeof(listItemParameters));
	if (userListParams == NULL)
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
	objectKey ShortPasswordLabel = NULL;
	objectKey okButton = NULL;
	objectKey cancelButton = NULL;
	windowEvent event;
	char confirmPassword[17];
	char oldPassword[17];
	char newPassword[17];

	bzero(&params, sizeof(componentParameters));

	// Create the dialog
	if (window)
		dialogWindow = windowNewDialog(window, "Set Password");
	else
		dialogWindow = windowNew(processId, "Set Password");
	if (dialogWindow == NULL)
		return (status = ERR_NOCREATE);

	params.gridWidth = 1;
	params.gridHeight = 1;
	params.padLeft = 5;
	params.padRight = 5;
	params.padTop = 5;
	params.orientationX = orient_center;
	params.orientationY = orient_middle;

	char labelText[64];
	sprintf(labelText, "User name: %s", userListParams[userNumber].text);
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
		windowNewTextLabel(dialogWindow, "Old password:", &params);

		params.gridX = 1;
		params.orientationX = orient_left;
		params.padRight = 5;
		oldPasswordField = windowNewPasswordField(dialogWindow, 17, &params);
		}

	params.gridX = 0;
	params.gridY = 2;
	params.gridWidth = 1;
	params.padRight = 0;
	params.orientationX = orient_right;
	windowNewTextLabel(dialogWindow, "New password:", &params);

	params.gridX = 1;
	params.padRight = 5;
	params.orientationX = orient_left;
	passwordField1 = windowNewPasswordField(dialogWindow, 17, &params);

	if (oldPasswordField)
		windowComponentFocus(oldPasswordField);
	else
		windowComponentFocus(passwordField1);

	params.gridX = 0;
	params.gridY = 3;
	params.padRight = 0;
	params.orientationX = orient_right;
	windowNewTextLabel(dialogWindow, "Confirm password:", &params);

	params.gridX = 1;
	params.orientationX = orient_left;
	params.padRight = 5;
	passwordField2 = windowNewPasswordField(dialogWindow, 17, &params);

	///create passwords do not match label and hide it
	params.gridX = 0;
	params.gridY = 4;
	params.gridWidth = 2;
	params.orientationX = orient_center;
	noMatchLabel = windowNewTextLabel(dialogWindow, "Passwords do not "
					"match", &params);
	windowComponentSetVisible(noMatchLabel, 0);

	/// Create Password Too Short label and Hide it
	params.gridX = 0;
	params.gridY = 4;
	params.gridWidth = 2;
	params.orientationX = orient_center;
	ShortPasswordLabel = windowNewTextLabel(dialogWindow, "Password should"
					" be longer", &params);
	windowComponentSetVisible(ShortPasswordLabel, 0);

	// Create the OK button
	params.gridY = 5;
	params.gridWidth = 1;
	params.padBottom = 5;
	params.padLeft = 5;
	params.padRight = 5;
	params.orientationX = orient_right;
	params.flags |= WINDOW_COMPFLAG_FIXEDWIDTH;
	okButton = windowNewButton(dialogWindow, "OK", NULL, &params);

	// Create the Cancel button
	params.gridX = 1;
	params.orientationX = orient_left;
	cancelButton = windowNewButton(dialogWindow, "Cancel", NULL, &params);

	windowCenterDialog(window, dialogWindow);
	windowSetVisible(dialogWindow, 1);

	while(1)
	{
		// Check for the OK button
		status = windowComponentEventGet(okButton, &event);
		if (status < 0)
			goto out;
		else if ((status > 0) && (event.type == EVENT_MOUSE_LEFTUP))
			break;

		// Check for the Cancel button
		status = windowComponentEventGet(cancelButton, &event);
		if ((status < 0) || ((status > 0) && (event.type == EVENT_MOUSE_LEFTUP)))
		{
			windowDestroy(dialogWindow);
			return (status = ERR_NODATA);
		}

		// Check for window close events
		status = windowComponentEventGet(dialogWindow, &event);
		if ((status < 0) || ((status > 0) && (event.type == EVENT_WINDOW_CLOSE)))
		{
			windowDestroy(dialogWindow);
			return (status = ERR_NODATA);
		}

		// Check for keyboard events
		if (oldPasswordField)
		{
			status = windowComponentEventGet(oldPasswordField, &event);
			if ((status > 0) && (event.type == EVENT_KEY_DOWN) &&
				(event.key == (unsigned char) ASCII_ENTER))
			{
				break;
			}
		}

		///read the old password field and check for changes
		status = windowComponentEventGet(passwordField1, &event);
		if ((status > 0) && (event.type == EVENT_KEY_DOWN))
		{
			if (event.key == (unsigned char) ASCII_ENTER)
				break;
			else
			///first of all, clear all existing labels
			windowComponentSetVisible(ShortPasswordLabel, 0);
            windowComponentSetVisible(noMatchLabel, 0);
			{
				///read data from the password fields
				windowComponentGetData(passwordField1, newPassword, 16);
				windowComponentGetData(passwordField2, confirmPassword, 16);

				///test to see if passwords match
				if (strncmp(newPassword, confirmPassword, 16))
				{
					///if passwords do not match
					///show the no match label and disable ok button
					windowComponentSetVisible(noMatchLabel, 1);
					windowComponentSetEnabled(okButton, 0);
				}
				else
				{
					///if passwords are matched enable ok button and check for password lenght.
					///The button is enabled because password lenght is not enforced

					windowComponentSetEnabled(okButton, 1);
					if (strlen(newPassword) <= 7)
					{
					windowComponentSetVisible(ShortPasswordLabel, 1);
					}

				}
			}
		}

		///read the new password field and check for changes
		status = windowComponentEventGet(passwordField2, &event);
		if ((status > 0) && (event.type == EVENT_KEY_DOWN))
		{
			if (event.key == (unsigned char) ASCII_ENTER)
				break;
			else
			{
				///first of all, clear all existing labels
                windowComponentSetVisible(ShortPasswordLabel, 0);
                windowComponentSetVisible(noMatchLabel, 0);

                ///read data from the pàssword fields
				windowComponentGetData(passwordField1, newPassword, 16);
				windowComponentGetData(passwordField2, confirmPassword, 16);

				///test to see if passwords match
				if (strncmp(newPassword, confirmPassword, 16))
				{
					///if passwords do not match
					///show the no match label and disable ok button
					windowComponentSetVisible(noMatchLabel, 1);
					windowComponentSetEnabled(okButton, 0);
				}
				else
				{
					///if passwords are matched enable ok button and check for password lenght.
					///The button is enabled because password lenght is not enforced

					windowComponentSetEnabled(okButton, 1);
					if (strlen(newPassword) <= 7)
					{
					windowComponentSetVisible(ShortPasswordLabel, 1);
					}
				}
			}
		}

		// Done
		multitaskerYield();
	}

	if (oldPasswordField)
		windowComponentGetData(oldPasswordField, oldPassword, 16);
	else
		oldPassword[0] = '\0';
	windowComponentGetData(passwordField1, newPassword, 16);
	windowComponentGetData(passwordField2, confirmPassword, 16);

out:
	windowDestroy(dialogWindow);

	// Make sure the new password and confirm passwords match
	if (!strncmp(newPassword, confirmPassword, 16))
	{
		status =
			setPassword(userListParams[userNumber].text, oldPassword, newPassword);
		if (status == ERR_PERMISSION)
			error("Permission denied");
		else if (status < 0)
			error("Error setting password");
	}
	else
	{
		error("Passwords do not match");
		status = ERR_INVALID;
	}

	return (status);
}


static int addUser(const char *userName, const char *password)
{
	// Tells the kernel to add the requested user name and password

	int status = 0;

	// With the user name, we try to authenticate with no password
	status = userAuthenticate(userName, "");
	if (!status || (status == ERR_PERMISSION))
	{
		error("User \"%s\" already exists.", userName);
		return (status = ERR_ALREADY);
	}

	// Tell the kernel to add the user
	status = userAdd(userName, password);
	if (status < 0)
	{
		error("Error adding user");
		return (status);
	}

	// Refresh our list of user names
	status = getUserNames();
	if (status < 0)
		return (status);

	// Re-populate our list component
	status = windowComponentSetData(userList, userListParams, numUserNames);
	if (status < 0)
		return (status);

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
			error("Permission denied");
		else
			error("Error deleting user");
		return (status);
	}

	// Refresh our list of user names
	status = getUserNames();
	if (status < 0)
		return (status);

	// Re-populate our list component
	status = windowComponentSetData(userList, userListParams, numUserNames);
	if (status < 0)
		return (status);

	return (status = 0);
}


static void eventHandler(objectKey key, windowEvent *event)
{
	char userName[17];
	int userNumber = 0;

	// Check for the window being closed by a GUI event.
	if ((key == window) && (event->type == EVENT_WINDOW_CLOSE))
		windowGuiStop();

	else if ((key == addUserButton) && (event->type == EVENT_MOUSE_LEFTUP))
	{
		if (windowNewPromptDialog(window, "Add User", "Enter the user name:",
			1, 16, userName) > 0)
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
			sprintf(question, "Delete user %s?",
				userListParams[userNumber].text);
			if (windowNewQueryDialog(window, "Delete?", question))
				deleteUser(userListParams[userNumber].text);
		}
		else
			error("Can't delete the last user");
	}

	else if ((key == setPasswordButton) && (event->type == EVENT_MOUSE_LEFTUP))
	{
		windowComponentGetSelected(userList, &userNumber);
		if (userNumber < 0)
			return;

		setPasswordDialog(userNumber);
	}
}


static void constructWindow(void)
{
	// If we are in graphics mode, make a window rather than operating on the
	// command line.

	componentParameters params;
	objectKey container = NULL;

	// Create a new window
	window = windowNew(processId, "User Manager");
	if (window == NULL)
		return;

	bzero(&params, sizeof(componentParameters));
	params.gridWidth = 1;
	params.gridHeight = 1;
	params.padLeft = 5;
	params.padTop = 5;
	params.padBottom = 5;
	params.orientationX = orient_left;
	params.orientationY = orient_top;
	userList = windowNewList(window, windowlist_textonly, 5, 1, 0,
		userListParams, numUserNames, &params);
	windowComponentFocus(userList);

	// A container for the buttons
	params.gridX = 1;
	params.padRight = 5;
	params.flags |= WINDOW_COMPFLAG_FIXEDHEIGHT;
	container = windowNewContainer(window, "button container", &params);

	// Create an 'add user' button
	params.gridX = 0;
	params.padLeft = 0;
	params.padRight = 0;
	params.padTop = 0;
	params.flags &= ~WINDOW_COMPFLAG_FIXEDHEIGHT;
	addUserButton = windowNewButton(container, "Add User", NULL, &params);
	windowRegisterEventHandler(addUserButton, &eventHandler);
	if (privilege || readOnly)
		windowComponentSetEnabled(addUserButton, 0);

	// Create a 'delete user' button
	params.gridY = 1;
	deleteUserButton = windowNewButton(container, "Delete User", NULL, &params);
	windowRegisterEventHandler(deleteUserButton, &eventHandler);
	if (privilege || readOnly)
		windowComponentSetEnabled(deleteUserButton, 0);

	// Create a 'set password' button
	params.gridY = 2;
	setPasswordButton =
		windowNewButton(container, "Set Password", NULL, &params);
	windowRegisterEventHandler(setPasswordButton, &eventHandler);
	if (readOnly)
		windowComponentSetEnabled(setPasswordButton, 0);

	// Register an event handler to catch window close events
	windowRegisterEventHandler(window, &eventHandler);

	windowSetVisible(window, 1);

	return;
}


int main(int argc, char *argv[])
{
	int status = 0;
	char userName[17];
	int setPass = 0;
	disk sysDisk;
	int count;

	// Only work in graphics mode
	if (!graphicsAreEnabled())
	{
		printf("\nThe \"%s\" command only works in graphics mode\n", argv[0]);
		errno = ERR_NOTINITIALIZED;
		return (status = errno);
	}

	// Check options
	if (getopt(argc, argv, "p:") == 'p')
	{
		strncpy(userName, optarg, 17);
		setPass = 1;
	}

	// Find out whether we are currently running on a read-only filesystem
	bzero(&sysDisk, sizeof(disk));
	if (!fileGetDisk("/system", &sysDisk))
		readOnly = sysDisk.readOnly;

	processId = multitaskerGetCurrentProcessId();
	privilege = multitaskerGetProcessPrivilege(processId);

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
			error("No such user \"%s\"", userName);
		else
		{
			if (!setPasswordDialog(userNumber))
				windowNewInfoDialog(window, "Done", "Password set");
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
	free(userListParams);
	return (errno = status);
}
