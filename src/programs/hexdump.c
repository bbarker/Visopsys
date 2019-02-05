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
//  hexdump.c
//

// A program for viewing files as hexadecimal listings.

/* This is the text that appears when a user requests help about this program
<help>

 -- hexdump --

A program for viewing files as hexadecimal listings.

Usage:
  hexdump <file_name>

Example:

This command is of only marginal usefulness to most users.  It is primarily
intended for developers who want to look at binary files in detail.

</help>
*/

#include <errno.h>
#include <libintl.h>
#include <locale.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/env.h>

#define _(string) gettext(string)


static void usage(char *name)
{
	printf("%s", _("usage:\n"));
	printf(_("%s <file_name>\n"), name);
	return;
}


int main(int argc, char *argv[])
{
	int status = 0;
	FILE *dumpFile = NULL;
	size_t read = 0;
	unsigned offset = 0;
	unsigned char fileBuff[16];
	char lineBuff[160];
	unsigned count;

	setlocale(LC_ALL, getenv(ENV_LANG));
	textdomain("hexdump");

	if (argc < 2)
	{
		usage(argv[0]);
		errno = ERR_ARGUMENTCOUNT;
		return (status = errno);
	}

	dumpFile = fopen(argv[argc - 1], "r");
	if (!dumpFile)
	{
		perror(argv[0]);
		return (status = errno);
	}

	while ((read = fread(fileBuff, 1, 16, dumpFile)))
	{
		sprintf(lineBuff, "%08x  ", offset);

		for (count = 0; count < 16; count ++)
		{
			if (count < read)
			{
				sprintf((lineBuff + strlen(lineBuff)), "%02x ",
					fileBuff[count]);
			}
			else
			{
				strcat(lineBuff, "   ");
			}

			if ((count == 7) || (count == 15))
				strcat(lineBuff, " ");
		}

		strcat(lineBuff, "|");
		for (count = 0; count < 16; count ++)
		{
			if ((count < read) && (fileBuff[count] >= 32) &&
				(fileBuff[count] <= 126))
			{
				sprintf((lineBuff + strlen(lineBuff)), "%c", fileBuff[count]);
			}
			else
				strcat(lineBuff, ".");
		}
		strcat(lineBuff, "|");

		printf("%s\n", lineBuff);
		offset += read;
	}

	fclose(dumpFile);

	return (status);
}

