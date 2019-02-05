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
//  file.c
//

// This is the UNIX-style command for reporting information about the type
// of a file.

/* This is the text that appears when a user requests help about this program
<help>

 -- file --

Show the type of a file.

Usage:
  file <file1> [file2] [...]

The file command queries the system about its idea of the data type(s) of
the named file(s).

Example:
  file /visopsys

Will produce the output:
  visopsys: ELF binary executable

</help>
*/

#include <errno.h>
#include <libintl.h>
#include <locale.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/api.h>
#include <sys/env.h>
#include <sys/vsh.h>

#define _(string) gettext(string)


static void usage(char *name)
{
	printf(_("usage:\n%s <file1> [file2] [...]\n"), name);
	return;
}


int main(int argc, char *argv[])
{
	loaderFileClass class;
	int count;

	setlocale(LC_ALL, getenv(ENV_LANG));
	textdomain("file");

	// Need at least one argument
	if (argc < 2)
	{
		usage(argv[0]);
		return (errno = ERR_ARGUMENTCOUNT);
	}

	errno = 0;

	for (count = 1; count < argc; count ++)
	{
		// Initialize the file class structure
		memset(&class, 0, sizeof(loaderFileClass));

		// Get the file class information
		if (!loaderClassifyFile(argv[count], &class))
			strcpy(class.name, _("unknown file class"));

		// Print this item
		printf("%s: %s\n", argv[count], class.name);
	}

	return (errno);
}

