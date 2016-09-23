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
//  nm.c
//

// This is the UNIX-style command for reporting information about a shared
// library

/* This is the text that appears when a user requests help about this program
<help>

 -- nm --

Show information about a dynamic library file.

Usage:
  nm <file1> [file2] [...]

This command is useful to software developers.  For each name listed after
the command, representing a shared library file (usually ending with a .so
extension) or dynamically-linked executable, nm will print a catalogue of
information about its symbols.  Data symbols, functions, sections, and other
items are shown, along with their bindings (such as 'local', 'global', or
'weak').

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
#include <sys/vsh.h>

#define _(string) gettext(string)
#define gettext_noop(string) (string)

char *bindings[] = {
	gettext_noop("local"),
	gettext_noop("global"),
	gettext_noop("weak")
};

char *types[] = {
	gettext_noop("none"),
	gettext_noop("object"),
	gettext_noop("function"),
	gettext_noop("section"),
	gettext_noop("file")
};


static void usage(char *name)
{
	printf(_("usage:\n%s <file1> [file2] [...]\n"), name);
	return;
}


int main(int argc, char *argv[])
{
	void *fileData = NULL;
	file theFile;
	loaderFileClass class;
	loaderSymbolTable *symTable = NULL;
	int count1, count2;

	setlocale(LC_ALL, getenv(ENV_LANG));
	textdomain("nm");

	// Need at least one argument
	if (argc < 2)
	{
		usage(argv[0]);
		return (errno = ERR_ARGUMENTCOUNT);
	}

	errno = 0;

	for (count1 = 1; count1 < argc; count1 ++)
	{
		// Initialize the file and file class structures
		memset(&theFile, 0, sizeof(file));
		memset(&class, 0, sizeof(loaderFileClass));

		// Load the file
		fileData = loaderLoad(argv[count1], &theFile);
		if (!fileData)
		{
			errno = ERR_NODATA;
			printf(_("Can't load file \"%s\"\n"), argv[count1]);
			continue;
		}

		// Make sure it's a dynamic library or executable
		if (!loaderClassify(argv[count1], fileData, theFile.size, &class))
		{
			printf(_("File type of \"%s\" is unknown\n"), argv[count1]);
			continue;
		}
		if (!(class.class & (LOADERFILECLASS_EXEC | LOADERFILECLASS_LIB)) ||
			!(class.subClass & LOADERFILESUBCLASS_DYNAMIC))
		{
			errno = ERR_INVALID;
			printf(_("\"%s\" is not a dynamic library or executable\n"),
				argv[count1]);
			continue;
		}

		// Free the file data now.  We want the symbol information.
		memoryRelease(fileData);
		fileData = NULL;

		// Get the symbol table
		symTable = loaderGetSymbols(argv[count1]);
		if (!symTable)
		{
			printf(_("Unable to get dynamic symbols from \"%s\".\n"),
				argv[count1]);
			continue;
		}

		for (count2 = 0; count2 < symTable->numSymbols; count2 ++)
		{
			if (symTable->symbols[count2].name[0])
			printf("%08x  %s  %s,%s\n", (unsigned)
				symTable->symbols[count2].value, symTable->symbols[count2].name,
				_(bindings[symTable->symbols[count2].binding]),
				_(types[symTable->symbols[count2].type]));
		}
	}

	return (errno);
}

