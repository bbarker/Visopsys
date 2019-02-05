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
//  unzip.c
//

// This is the unzip command, for decompressing archive file

/* This is the text that appears when a user requests help about this program
<help>

 -- unzip --

Decompress and extract files from a compressed archive file.

Usage:
  unzip [-p] <file1> [file2] [...]

Each archive file name listed after the command name will be extracted in
sequence.

Options:
-p  : Show a progress indicator

</help>
*/

#include <errno.h>
#include <libintl.h>
#include <locale.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/compress.h>
#include <sys/env.h>
#include <sys/progress.h>
#include <sys/vsh.h>

#define _(string) gettext(string)
#define gettext_noop(string) (string)


static void usage(char *name)
{
	printf(_("usage:\n%s [-p] <file1> [file2] [...]\n"), name);
	return;
}


int main(int argc, char *argv[])
{
	int status = 0;
	char opt;
	int showProgress = 0;
	progress prog;
	int count;

	setlocale(LC_ALL, getenv(ENV_LANG));
	textdomain("unzip");

	// Check options
	while (strchr("p?", (opt = getopt(argc, argv, "p"))))
	{
		switch (opt)
		{
			case 'p':
				// Show a progress indicator
				showProgress = 1;
				break;

			default:
				fprintf(stderr, _("Unknown option '%c'\n"), optopt);
				usage(argv[0]);
				return (status = ERR_INVALID);
		}
	}

	for (count = optind; count < argc; count ++)
	{
		if (showProgress)
		{
			memset((void *) &prog, 0, sizeof(progress));
			vshProgressBar(&prog);
		}

		status = archiveExtract(argv[count], (showProgress? &prog : NULL));

		if (showProgress)
			vshProgressBarDestroy(&prog);

		if (status < 0)
			break;
	}

	return (status);
}

