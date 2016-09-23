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
//  find.c
//

// This is the UNIX-style command for recursively traversing file trees
// and doing optional, conditional operations on the files/directories.

/* This is the text that appears when a user requests help about this program
<help>

 -- find --

A command for traversing directory trees.

Usage:
  find [start_dir]

This command is designed to recursively descend through directory trees.
The (optional) starting directory parameter can be supplied, or else the
current directory will be used.

This command is very limited at the moment, and has no filtering ability,
execution ability, or anything really.  It will be developed more fully in
the future, but was added at the present time to facilitate the development
of filesystem drivers (as a testing mechanism)

</help>
*/

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/api.h>
#include <sys/vsh.h>


static void recurseDirectory(const char *dirPath)
{
	int status = 0;
	file theFile;
	char *newDirPath = NULL;

	// Initialize the file structure
	memset(&theFile, 0, sizeof(file));

	// Get the first item in the directory
	status = fileFirst(dirPath, &theFile);
	if (status < 0)
		return;

	// Loop through the contents of the directory
	while (1)
	{
		if (strcmp(theFile.name, ".") && strcmp(theFile.name, ".."))
		{
			// Print the item
			printf("%s/%s\n", dirPath, theFile.name);

			if (theFile.type == dirT)
			{
				newDirPath = malloc(strlen(dirPath) + strlen(theFile.name) +
					2);

				if (newDirPath)
				{
					// Construct the relative pathname for this directory
					sprintf(newDirPath, "%s/%s", dirPath, theFile.name);
					recurseDirectory(newDirPath);

					free(newDirPath);
				}
			}
		}

		// Move to the next item
		status = fileNext(dirPath, &theFile);
		if (status < 0)
			break;
	}
}


int main(int argc, char *argv[])
{
	int status = 0;
	file theFile;
	char *fileName = NULL;
	int lastChar = 0;

	// Initialize the file structure
	memset(&theFile, 0, sizeof(file));

	fileName = malloc(MAX_PATH_NAME_LENGTH);
	if (!fileName)
	{
		errno = ERR_MEMORY;
		perror(argv[0]);
		return (status);
	}

	if (argc == 1)
		// No args.  Assume current directory
		strcpy(fileName, ".");
	else
		strcpy(fileName, argv[1]);

	// Remove any trailing separators
	lastChar = (strlen(fileName) - 1);
	while ((fileName[lastChar] == '/') || (fileName[lastChar] == '\\'))
	{
		fileName[lastChar] = '\0';
		lastChar--;
	}

	// Call the "find file" routine to see if the file exists
	status = fileFind(fileName, &theFile);
	if (status < 0)
	{
		errno = status;
		perror(argv[0]);
		return (status);
	}

	// Print this item
	printf("%s\n", fileName);

	if (theFile.type == dirT)
		// If it's a directory, we start our recursion.  Otherwise just print it
		recurseDirectory(fileName);

	free(fileName);

	// Return success
	return (status = 0);
}

