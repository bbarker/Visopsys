//
//  Visopsys
//  Copyright (C) 1998-2016 J. Andrew McLaughlin
//
//  This library is free software; you can redistribute it and/or modify it
//  under the terms of the GNU Lesser General Public License as published by
//  the Free Software Foundation; either version 2.1 of the License, or (at
//  your option) any later version.
//
//  This library is distributed in the hope that it will be useful, but
//  WITHOUT ANY WARRANTY; without even the implied warranty of
//  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU Lesser
//  General Public License for more details.
//
//  You should have received a copy of the GNU Lesser General Public License
//  along with this library; if not, write to the Free Software Foundation,
//  Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
//
//  vshFileList.c
//

// This contains some useful functions written for the shell

#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <sys/vsh.h>
#include <sys/api.h>


_X_ int vshFileList(const char *itemName)
{
	// Desc: Print a listing of a file or directory named 'itemName'.  'itemName' must be an absolute pathname, beginning with '/'.

	int status = 0;
	char lineBuffer[256];
	int numberFiles = 0;
	file theFile;
	unsigned bytesFree = 0;

	// Make sure file name isn't NULL
	if (!itemName)
		return (errno = ERR_NULLPARAMETER);

	// Initialize the file structure
	memset(&theFile, 0, sizeof(file));

	// Call the "find file" routine to see if the file exists
	status = fileFind(itemName, &theFile);
	if (status < 0)
		return (errno = status);

	// Get the bytes free for the filesystem
	bytesFree = filesystemGetFreeBytes(theFile.filesystem);

	// We do things differently depending upon whether the target is a
	// file or a directory

	if (theFile.type == fileT)
	{
		// This means the itemName is a single file.  We just output
		// the appropriate information for that file
		memset(lineBuffer, 0, 256);
		strcpy(lineBuffer, theFile.name);

		if (strlen(theFile.name) < 24)
			memset((lineBuffer + strlen(lineBuffer)), ' ',
				(26 - (int) strlen(theFile.name)));
		else
			strcat(lineBuffer, "  ");

		// The date and time
		vshPrintDate((lineBuffer + strlen(lineBuffer)), &theFile.modified);
		strcat(lineBuffer, " ");
		vshPrintTime((lineBuffer + strlen(lineBuffer)), &theFile.modified);
		strcat(lineBuffer, "    ");

		// The file size
		printf("%s%u\n", lineBuffer, theFile.size);
	}

	else
	{
		printf("\n  Directory of %s\n", (char *) itemName);

		// Get the first file
		status = fileFirst(itemName, &theFile);
		if ((status != ERR_NOSUCHFILE) && (status < 0))
			return (errno = status);

		else if (status >= 0) while (1)
		{
			memset(lineBuffer, 0, 256);
			strcpy(lineBuffer, theFile.name);

			if (theFile.type == dirT)
				strcat(lineBuffer, "/");
			else
				strcat(lineBuffer, " ");

			if (strlen(theFile.name) < 23)
				memset((lineBuffer + strlen(lineBuffer)), ' ',
					(25 - (int) strlen(theFile.name)));
			else
				strcat(lineBuffer, "  ");

			// The date and time
			vshPrintDate((lineBuffer + strlen(lineBuffer)), &theFile.modified);
			strcat(lineBuffer, " ");
			vshPrintTime((lineBuffer + strlen(lineBuffer)), &theFile.modified);
			strcat(lineBuffer, "    ");

			// The file size
			printf("%s%u\n", lineBuffer, theFile.size);

			numberFiles += 1;

			status = fileNext(itemName, &theFile);
			if (status < 0)
				break;
		}

		printf("  ");

		if (!numberFiles)
			printf("No");
		else
			printf("%u", numberFiles);
		printf(" file");

		if (!numberFiles || (numberFiles > 1))
			putchar('s');

		printf("\n  %u bytes free\n\n", bytesFree);
	}

	return (status = 0);
}

