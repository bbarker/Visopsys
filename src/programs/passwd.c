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
//  passwd.c
//

// This is the UNIX-style command for setting a password

/* This is the text that appears when a user requests help about this program
<help>

 -- passwd --

Set the password on a user account.

Usage:
  passwd [user_name]

The passwd program is a very simple method of setting a password for an
account.  The program operates in either text or graphics mode, is
interactive, and requires the password to be entered twice at a prompt.

If no user name is specified, the program will assume the current user.

If the user does not have administrator privileges, they will be prompted
to enter the old password (if one exists).

</help>
*/

#include <errno.h>
#include <libintl.h>
#include <locale.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/api.h>
#include <sys/env.h>
#include <sys/user.h>
#include <sys/vsh.h>

#define _(string) gettext(string)


static void usage(char *name)
{
	printf("%s", _("usage:\n"));
	printf(_("%s [username]\n"), name);
	return;
}


int main(int argc, char *argv[])
{
	int status = 0;
	char userName[USER_MAX_NAMELENGTH + 1];
	char oldPassword[USER_MAX_PASSWDLENGTH + 1];
	char newPassword[USER_MAX_PASSWDLENGTH + 1];
	char verifyPassword[USER_MAX_PASSWDLENGTH + 1];

	setlocale(LC_ALL, getenv(ENV_LANG));
	textdomain("passwd");

	memset(userName, 0, sizeof(userName));
	memset(oldPassword, 0, sizeof(oldPassword));
	memset(newPassword, 0, sizeof(newPassword));
	memset(verifyPassword, 0, sizeof(verifyPassword));

	if (argc == 1)
	{
		strncpy(userName, getenv(ENV_USER), sizeof(userName));
	}
	else if (argc == 2)
	{
		strncpy(userName, argv[1], sizeof(userName));
	}
	else
	{
		usage(argv[0]);
		return (ERR_ARGUMENTCOUNT);
	}

	// Make sure the user exists
	if (!userExists(userName))
	{
		fprintf(stderr, _("User %s does not exist.\n"), userName);
		return (errno = ERR_NOSUCHUSER);
	}

	// With the user name, we try to authenticate with no password
	status = userAuthenticate(userName, "");
	if (status < 0)
	{
		if (status == ERR_PERMISSION)
		{
			vshPasswordPrompt(_("Enter current password: "), oldPassword);
		}
		else
		{
			errno = status;
			perror(argv[0]);
			return (status);
		}
	}

	status = userAuthenticate(userName, oldPassword);
	if (status < 0)
	{
		errno = status;

		if (status == ERR_PERMISSION)
			fprintf(stderr, "%s", _("Password incorrect\n"));
		else
			perror(argv[0]);

		return (status);
	}

	while (1)
	{
		char prompt[80];
		sprintf(prompt, _("Enter new password for %s: "), userName);
		vshPasswordPrompt(prompt, newPassword);
		vshPasswordPrompt(_("Confirm password: "), verifyPassword);

		if (!strcmp(newPassword, verifyPassword))
			break;

		fprintf(stderr, "%s", _("\nPasswords do not match.\n\n"));
	}

	status = userSetPassword(userName, oldPassword, newPassword);
	if (status < 0)
	{
		errno = status;
		perror(argv[0]);
		return (status);
	}

	printf("%s", _("Password changed.\n"));

	// Done
	return (errno = status = 0);
}

