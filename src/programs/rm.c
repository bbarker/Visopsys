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
//  rm.c
//

// This is the UNIX-style command for removing files

/* This is the text that appears when a user requests help about this program
<help>

 -- rm --

Remove (delete) one or more files.

Synonym:
  del

Usage:
  rm [-R] [-S#] <file1> [file2] [...]

This command will remove one or more files.  Normally it will not remove
directories.  To remove a directory, use the 'rmdir' command.

Options:
-R              : Force recursive deletion, including directories.
-S[number]      : Securely delete the file by overwriting it with random
                  data (number minus 1 times) and then NULLs, and then
                  deleting the file.  The default number value is 5 if no
                  value is supplied.

Note the -S option is not allowed if the -R option is used.

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
#include <sys/vsh.h>

#define _(string) gettext(string)


static void usage(char *name)
{
	printf("%s", _("usage:\n"));
	printf(_("%s [-R] [-S#] <file1> [file2] [...]\n"), name);
	return;
}


int main(int argc, char *argv[])
{
	int status = 0;
	char opt;
	int recurse = 0;
	int secure = 0;
	int count;

	setlocale(LC_ALL, getenv(ENV_LANG));
	textdomain("rm");

	if (argc < 2)
	{
		usage(argv[0]);
		return (status = ERR_ARGUMENTCOUNT);
	}

	count = 1;

	// Check options
	while (strchr("rRS?", (opt = getopt(argc, argv, "rRS::"))))
	{
		switch (opt)
		{
			case 'r':
			case 'R':
				// Recurse
				recurse = 1;
				break;

			case 'S':
				// Secure
				secure = 5;
				if (optarg && (atoi(optarg) >= 0))
					secure = atoi(optarg);
				break;

			default:
				fprintf(stderr, _("Unknown option '%c'\n"), optopt);
				usage(argv[0]);
				return (status = ERR_INVALID);
		}
	}

	// We can't do a secure recursive delete
	if (recurse && secure)
	{
		fprintf(stderr, "%s", _("Can't both recursively and securely delete\n"));
		return (status = ERR_NOTIMPLEMENTED);
	}

	if (optind >= argc)
	{
		fprintf(stderr, "%s", _("No file names to delete\n"));
		return (status = ERR_NULLPARAMETER);
	}

	// Loop through all of our file name arguments
	for (count = optind ; count < argc; count ++)
	{
		// Attempt to remove the file
		if (recurse)
			status = fileDeleteRecursive(argv[count]);
		else if (secure)
			status = fileDeleteSecure(argv[count], secure);
		else
			status = fileDelete(argv[count]);

		if (status < 0)
		{
			errno = status;
			perror(argv[0]);
			return (status);
		}
	}

	// Return success
	return (status = 0);
}

