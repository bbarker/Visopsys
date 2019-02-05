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
//  cp.c
//

// Yup, it's the UNIX-style command for copying files

/* This is the text that appears when a user requests help about this program
<help>

 -- cp --

Copy files.

Synonym:
  copy

Usage:
  cp [-R] <item1> [item2] ... <new_name | detination_directory>

This command will copy one or more files or directories.  If one source
item is specified, then the last argument can be either the new name to
copy to, or else can be a destination directory -- in which case the new
item will have the same name as the source item.  If multiple source items
are specified, then the last argument must be a directory name and all
copies will have the same names as their source items.

The -R flag means copy recursively.  The -R flag must be used if any of
the source items are directories.  If none of the source items are
directories then the flag has no effect.

</help>
*/

#include <errno.h>
#include <libintl.h>
#include <locale.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/api.h>
#include <sys/env.h>

#define _(string) gettext(string)


static void usage(char *name)
{
	fprintf(stderr, "%s", _("usage:\n"));
	fprintf(stderr, _("%s [-R] <source1> [source2] ... <destination>\n"),
		name);
	return;
}


int main(int argc, char *argv[])
{
	int status = 0;
	char opt;
	int recurse = 0;
	int count;

	setlocale(LC_ALL, getenv(ENV_LANG));
	textdomain("cp");

	// There need to be at least a source and destination name
	if (argc < 3)
	{
		usage(argv[0]);
		return (status = ERR_ARGUMENTCOUNT);
	}

	// Check options
	while (strchr("Rr?", (opt = getopt(argc, argv, "Rr"))))
	{
		switch (opt)
		{
			case 'r':
			case 'R':
				// Recurse
				recurse = 1;
				break;

			default:
				fprintf(stderr, _("Unknown option '%c'\n"), optopt);
				usage(argv[0]);
				return (status = ERR_INVALID);
		}
	}

	for (count = optind; count < (argc - 1); count ++)
	{
		if (recurse)
			status = fileCopyRecursive(argv[count], argv[argc - 1]);
		else
			status = fileCopy(argv[count], argv[argc - 1]);

		if (status < 0)
		{
			fprintf(stderr, "%s: ", argv[count]);
			errno = status;
			perror(argv[0]);
		}
	}

	return (status);
}

