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
//  cat.c
//

// This is the UNIX-style command for dumping files

/* This is the text that appears when a user requests help about this program
<help>

 -- cat --

Print a file's contents on the screen.

Synonym:
  type

Usage:
  cat <file1> [file2] [file3] [...]

Each file name listed after the command name will be printed in sequence.

</help>
*/

#include <errno.h>
#include <libintl.h>
#include <locale.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
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
	int status = 0;
	int count;

	setlocale(LC_ALL, getenv(ENV_LANG));
	textdomain("cat");

	if (argc < 2)
	{
		usage(argv[0]);
		return (status = ERR_ARGUMENTCOUNT);
	}

	for (count = 1; count < argc; count ++)
	{
		status = vshDumpFile(argv[count]);
		if (status < 0)
			perror(argv[0]);
	}

	// Return success
	return (status = 0);
}

