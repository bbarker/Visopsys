//
//  Visopsys
//  Copyright (C) 1998-2018 J. Andrew McLaughlin
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
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/vsh.h>
#include <sys/api.h>


static void fileLine(file *theFile, char *lineBuffer, int bufferLen)
{
	memset(lineBuffer, 0, bufferLen);
	strncpy(lineBuffer, theFile->name, MAX_PATH_NAME_LENGTH);

	if (theFile->type == dirT)
		strcat(lineBuffer, "/");
	else
		strcat(lineBuffer, " ");

	if (strlen(theFile->name) < 23)
	{
		memset((lineBuffer + strlen(lineBuffer)), ' ',
			(25 - (int) strlen(theFile->name)));
	}
	else
	{
		strcat(lineBuffer, "  ");
	}

	// The date and time
	vshPrintDate((lineBuffer + strlen(lineBuffer)), &theFile->modified);
	strcat(lineBuffer, " ");
	vshPrintTime((lineBuffer + strlen(lineBuffer)), &theFile->modified);
	strcat(lineBuffer, "    ");

	// The file size
	sprintf(lineBuffer, "%s%u", lineBuffer, theFile->size);
}


static void bytesToHuman(uquad_t *bytes, const char **units)
{
	*units = "bytes";

	// If it's a lot of bytes, convert to KB
	if (*bytes >= 0x100000 /* 1 MB */)
	{
		*bytes >>= 10;
		*units = "KB";

		// If it's a lot of KB, convert to MB
		if (*bytes >= 0x2800 /* 10 MB */)
		{
			*bytes >>= 10;
			*units = "MB";

			// If it's a lot of MB, convert to GB
			if (*bytes >= 0x2800 /* 10 GB */)
			{
				*bytes >>= 10;
				*units = "GB";
			}
		}
	}
}


/////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////
//
// Below here, the functions are exported for external use
//
/////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////

_X_ int vshFileList(const char *itemName)
{
	// Desc: Print a listing of a file or directory named 'itemName'.  'itemName' must be an absolute pathname, beginning with '/'.

	int status = 0;
	file theFile;
	char *lineBuffer = NULL;
	int numberFiles = 0;
	uquad_t freeSpace = 0;
	const char *units = NULL;

	// Make sure file name isn't NULL
	if (!itemName)
		return (errno = ERR_NULLPARAMETER);

	// Initialize the file structure
	memset(&theFile, 0, sizeof(file));

	// Call the "find file" function to see if the file exists
	status = fileFind(itemName, &theFile);
	if (status < 0)
		return (errno = status);

	lineBuffer = malloc(MAXSTRINGLENGTH);
	if (!lineBuffer)
		return (errno = ERR_MEMORY);

	// We do things differently depending upon whether the target is a file or
	// a directory

	if (theFile.type == fileT)
	{
		// This means the itemName is a single file.  We just output the
		// appropriate information for that file.
		fileLine(&theFile, lineBuffer, MAXSTRINGLENGTH);
		printf("%s\n", lineBuffer);
	}

	else
	{
		printf("\n  Directory of %s\n", (char *) itemName);

		// Get the first file
		status = fileFirst(itemName, &theFile);
		if ((status != ERR_NOSUCHFILE) && (status < 0))
		{
			free(lineBuffer);
			return (errno = status);
		}

		while (status >= 0)
		{
			fileLine(&theFile, lineBuffer, MAXSTRINGLENGTH);
			printf("%s\n", lineBuffer);

			numberFiles += 1;

			status = fileNext(itemName, &theFile);
		}

		printf("  ");

		if (!numberFiles)
			printf("No");
		else
			printf("%u", numberFiles);
		printf(" file");

		if (!numberFiles || (numberFiles > 1))
			putchar('s');

		// Get the bytes free for the filesystem.
		freeSpace = filesystemGetFreeBytes(theFile.filesystem);
		bytesToHuman(&freeSpace, &units);
		printf("\n  %llu %s free\n\n", freeSpace, units);
	}

	free(lineBuffer);

	return (status = 0);
}

