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
//  tar.c
//

// This is the tar command, for manipulating .tar archives

/* This is the text that appears when a user requests help about this program
<help>

 -- tar --

Create or manage archive files using TAR format.

Usage:
  tar [-p] -a <archive> <file1> [file2] [...]
    Each file name listed will be added to the archive file.

  tar [-p] -d <archive> <member1> [member2] [...]
    Each member name listed will be deleted from the archive file.

  tar [-p] -i <archive>
    Print info about the members of the archive file.

Options:
-a  : Add files to an archive
-d  : Delete members from an archive
-i  : Print info about the archive members
-p  : Show a progress indicator
-x  : Extract all files from the archive

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
#include <sys/loader.h>
#include <sys/progress.h>
#include <sys/vsh.h>

#define _(string) gettext(string)
#define gettext_noop(string) (string)


static void usage(char *name)
{
	printf(_("usage:\n%s [-p] -a <archive> <file1> [file2] [...]\n"
		"%s [-p] -d <archive> <member1> [member2] [...]\n"
		"%s [-p] -i <archive>\n"
		"%s [-p] -x <archive1> [archive2] [...]\n"), name, name, name, name);
	return;
}


static int doShowInfo(const char *archive, progress *prog)
{
	int memberCount = 0;
	archiveMemberInfo *info = NULL;
	int count;

	if (prog)
	{
		memset((void *) prog, 0, sizeof(progress));
		vshProgressBar(prog);
	}

	memberCount = archiveInfo(archive, &info, prog);
	if (memberCount < 0)
		return (memberCount);

	if (prog)
		vshProgressBarDestroy(prog);

	for (count = 0; count < memberCount; count ++)
	{
		printf(_("Member name: %s\n"),
			(info[count].name? info[count].name : ""));
		printf(_("Comment: %s\n"), (info[count].comment?
			info[count].comment : ""));
		printf(_("Modification time: %s\n"), ctime(info[count].modTime));
		printf(_("Offset: %u\n"), info[count].startOffset);
		printf(_("Size: %u\n"), info[count].totalSize);
		printf(_("Data offset: %u\n"), info[count].dataOffset);
		printf(_("Compressed size: %u\n"), info[count].compressedDataSize);
		printf(_("Decompressed size: %u\n\n"),
			info[count].decompressedDataSize);
	}

	archiveInfoFree(info, memberCount);
	return (0);
}


int main(int argc, char *argv[])
{
	int status = 0;
	char opt;
	int add = 0, delete = 0, info = 0, extract = 0;
	char *archive = NULL;
	int showProgress = 0;
	progress prog;
	int count;

	setlocale(LC_ALL, getenv(ENV_LANG));
	textdomain("tar");

	// Check options
	while (strchr("adipx:?", (opt = getopt(argc, argv, "a:d:i:px:"))))
	{
		switch (opt)
		{
			case 'a':
				// Add files to the named archive
				if (!optarg)
				{
					fprintf(stderr, "%s", _("Missing archive argument for "
						"'-a' option\n"));
					usage(argv[0]);
					return (status = ERR_NULLPARAMETER);
				}
				add = 1;
				archive = optarg;
				break;

			case 'd':
				// Delete members from the named archive(s)
				if (!optarg)
				{
					fprintf(stderr, "%s", _("Missing archive argument for "
						"'-d' option\n"));
					usage(argv[0]);
					return (status = ERR_NULLPARAMETER);
				}
				delete = 1;
				archive = optarg;
				break;

			case 'i':
				// Just print info about the archive members
				if (!optarg)
				{
					fprintf(stderr, "%s", _("Missing archive argument for "
						"'-i' option\n"));
					usage(argv[0]);
					return (status = ERR_NULLPARAMETER);
				}
				info = 1;
				archive = optarg;
				break;

			case 'p':
				// Show a progress indicator
				showProgress = 1;
				break;

			case 'x':
				// Extract the named archive(s)
				if (!optarg)
				{
					fprintf(stderr, "%s", _("Missing archive argument for "
						"'-x' option\n"));
					usage(argv[0]);
					return (status = ERR_NULLPARAMETER);
				}
				extract = 1;
				archive = optarg;
				break;

			case ':':
				fprintf(stderr, _("Missing parameter for %s option\n"),
					argv[optind - 1]);
				usage(argv[0]);
				return (status = ERR_NULLPARAMETER);

			default:
				fprintf(stderr, _("Unknown option '%c'\n"), optopt);
				usage(argv[0]);
				return (status = ERR_INVALID);
		}
	}

	if (!(add + delete + info + extract))
	{
		fprintf(stderr, "%s", _("Must specify -a, -d, -i, or -x option\n"));
		usage(argv[0]);
		return (status = ERR_INVALID);
	}

	if ((add + delete + info + extract) > 1)
	{
		fprintf(stderr, "%s", _("The -a, -d, -i, and -x options are mutually "
			"exclusive\n"));
		usage(argv[0]);
		return (status = ERR_INVALID);
	}

	if (info)
	{
		status = doShowInfo(archive, (showProgress? &prog : NULL));
	}
	else
	{
		if (extract)
		{
			if (showProgress)
			{
				memset((void *) &prog, 0, sizeof(progress));
				vshProgressBar(&prog);
			}

			status = archiveExtract(archive, (showProgress? &prog : NULL));

			if (showProgress)
				vshProgressBarDestroy(&prog);

			if (status < 0)
				return (status);
		}

		for (count = optind; count < argc; count ++)
		{
			if (showProgress)
			{
				memset((void *) &prog, 0, sizeof(progress));
				vshProgressBar(&prog);
			}

			if (add)
			{
				status = archiveAddRecursive(argv[count], archive,
					LOADERFILESUBCLASS_TAR, NULL /* comment */, (showProgress?
						&prog : NULL));
			}
			else if (delete)
			{
				status = archiveDeleteMember(archive, argv[count],
					0 /* index not supplied */, (showProgress? &prog : NULL));
			}
			else if (extract)
			{
				status = archiveExtract(argv[count], (showProgress? &prog :
					NULL));
			}

			if (showProgress)
				vshProgressBarDestroy(&prog);

			if (status < 0)
				break;
		}
	}

	return (status);
}

