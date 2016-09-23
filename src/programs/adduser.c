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
//  adduser.c
//

// This is the UNIX-style command for adding a user

/* This is the text that appears when a user requests help about this program
<help>

 -- adduser --

Add a user account to the system

Usage:
  adduser <user_name>

The adduser program is a very simple method of adding a user account.  The
resulting account has no password assigned (you can use the passwd command
to set the password).

</help>
*/

#include <errno.h>
#include <libintl.h>
#include <locale.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/api.h>
#include <sys/env.h>
#include <sys/paths.h>

#define _(string) gettext(string)


static void usage(char *name)
{
	printf("%s", _("usage:\n"));
	printf(_("%s <username>\n"), name);
	return;
}


int main(int argc, char *argv[])
{
	int status = 0;
	char userDir[MAX_PATH_NAME_LENGTH];
	file f;

	setlocale(LC_ALL, getenv(ENV_LANG));
	textdomain("adduser");

	if (argc != 2)
	{
		usage(argv[0]);
		return (status = ERR_ARGUMENTCOUNT);
	}

	// Make sure the user doesn't already exist
	if (userExists(argv[1]))
	{
		fprintf(stderr, _("User %s already exists.\n"), argv[1]);
		return (status = ERR_ALREADY);
	}

	status = userAdd(argv[1], "");
	if (status < 0)
	{
		errno = status;
		return (status);
	}

	// Try to create the user directory
	snprintf(userDir, MAX_PATH_NAME_LENGTH, PATH_USERS "/%s", argv[1]);
	if (fileFind(userDir, &f) < 0)
	{
		status = fileMakeDir(userDir);
		if (status < 0)
			fprintf(stderr, "Warning: couldn't create user directory.\n");
	}
	else
	{
		fprintf(stderr, "User directory already exists.\n");
	}

	printf("%s", _("User added.\n"));

	// Done
	return (status = 0);
}

