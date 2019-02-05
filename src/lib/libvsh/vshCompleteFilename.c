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
//  vshCompleteFilename.c
//

// This contains some useful functions written for the shell

#include <string.h>
#include <errno.h>
#include <stdlib.h>
#include <sys/vsh.h>
#include <sys/api.h>

static char *prefixPath = NULL;
static char *fileName = NULL;
static char *matchName = NULL;


static void freeMemory(void)
{
	if (prefixPath)
		free(prefixPath);
	if (fileName)
		free(fileName);
	if (matchName)
		free(matchName);
}


/////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////
//
// Below here, the functions are exported for external use
//
/////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////

_X_ void vshCompleteFilename(char *buffer)
{
	// Desc: Attempts to complete a portion of a filename, 'buffer'.  The function will append either the remainder of the complete filename, or if possible, some portion thereof.  The result simply depends on whether a good completion or partial completion exists.  'buffer' must of course be large enough to contain any potential filename completion.

	int status = 0;
	int filenameLength = 0;
	int lastSeparator = -1;
	file aFile;
	int match = 0;
	int longestMatch = 0;
	int longestIsDir = 0;
	int prefixLength;
	int count;

	// Check params
	if (!buffer)
	{
		errno = ERR_NULLPARAMETER;
		return;
	}

	// Get memory
	prefixPath = malloc(MAX_PATH_LENGTH);
	fileName = malloc(MAX_NAME_LENGTH);
	matchName = malloc(MAX_NAME_LENGTH);

	if (!prefixPath || !fileName || !matchName)
	{
		errno = ERR_MEMORY;
		freeMemory();
		return;
	}

	// Does the buffer name begin with a separator?  If not, we need to
	// prepend the cwd
	if ((buffer[0] != '/') && (buffer[0] != '\\'))
	{
		// Get the current directory
		status = multitaskerGetCurrentDirectory(prefixPath, MAX_PATH_LENGTH);
		if (status < 0)
		{
			errno = status;
			freeMemory();
			return;
		}

		prefixLength = strlen(prefixPath);
		if ((prefixPath[prefixLength - 1] != '/') &&
			(prefixPath[prefixLength - 1] != '\\'))
		{
			strncat(prefixPath, "/", 1);
		}
	}

	// We should now have an absolute path up to the cwd

	// Find the last occurrence of a separator character
	for (count = (strlen(buffer) - 1); count >= 0 ; count --)
	{
		if ((buffer[count] == '/') ||
			(buffer[count] == '\\'))
		{
			lastSeparator = count;
			break;
		}
	}

	// If there was a separator, append it and everything before it to
	// prefixPath and copy everything after it into filename
	if (count >= 0)
	{
		strncat(prefixPath, buffer, (lastSeparator + 1));
		strcpy(fileName, (buffer + lastSeparator + 1));
	}
	else
	{
		// Copy the whole buffer into the filename string
		strcpy(fileName, buffer);
	}

	filenameLength = strlen(fileName);

	// Now, prefixPath must have something in it.  Preferably this is the
	// name of the last directory of the path we're searching.  Try to look
	// it up
	memset(&aFile, 0, sizeof(file));
	status = fileFind(prefixPath, &aFile);
	if (status < 0)
	{
		// The directory doesn't exist
		errno = status;
		freeMemory();
		return;
	}

	// Get the first file of the directory
	status = fileFirst(prefixPath, &aFile);
	if (status < 0)
	{
		// No files in the directory
		errno = status;
		freeMemory();
		return;
	}

	// If filename is empty, and there is only one non-'.' or '..' entry,
	// complete that one
	if (!filenameLength)
	{
		while (!strcmp(aFile.name, ".") || !strcmp(aFile.name, ".."))
		{
			status = fileNext(prefixPath, &aFile);
			if (status < 0)
			{
				errno = status;
				freeMemory();
				return;
			}
		}

		file tmpFile;
		memcpy(&tmpFile, &aFile, sizeof(file));
		if (fileNext(prefixPath, &tmpFile) < 0)
		{
			strcpy((buffer + lastSeparator + 1), aFile.name);
			if (aFile.type == dirT)
				strcat((buffer + lastSeparator + 1), "/");
		}

		freeMemory();
		return;
	}

	while (1)
	{
		match = strspn(fileName, aFile.name);

		// File match some part of our current file (but not if the thing to
		// complete is longer than the filename)?
		if (match && (match >= filenameLength))
		{
			if (match == longestMatch)
			{
				// We have a multiple substring match.  This file matches a
				// substring of equal length to that of another file, and thus
				// there are multiple filenames that can complete this filename.
				// Terminate the match string after the point that matches
				// multiple files and quit.
				int tmp = strspn(matchName, aFile.name);
				strncpy(matchName, aFile.name, tmp);
				matchName[tmp] = '\0';
				longestIsDir = 0;
			}
			else if (match > longestMatch)
			{
				// This is the mew longest match so far
				longestMatch = match;
				strcpy(matchName, aFile.name);
				if (aFile.type == dirT)
					longestIsDir = 1;
				else
					longestIsDir = 0;
			}
		}

		// Get the next file of the directory
		status = fileNext(prefixPath, &aFile);
		if (status < 0)
			break;
	}

	// If we fall through, then the longest match so far wins.
	if (longestMatch)
	{
		strcpy((buffer + lastSeparator + 1), matchName);
		if (longestIsDir)
			strcat((buffer + lastSeparator + 1), "/");
	}

	freeMemory();
	return;
}

