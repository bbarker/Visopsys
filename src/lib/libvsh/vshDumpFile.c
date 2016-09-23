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
//  vshDumpFile.c
//

// This contains some useful functions written for the shell

#include <stdlib.h>
#include <errno.h>
#include <sys/api.h>
#include <sys/vsh.h>


_X_ int vshDumpFile(const char *fileName)
{
	// Desc: Print the contents of the file, specified by 'fileName', to standard output.  'fileName' must be an absolute pathname, beginning with '/'.

	int status = 0;
	unsigned count;
	file theFile;
	char *fileBuffer = NULL;

	// Make sure file name isn't NULL
	if (!fileName)
		return (errno = ERR_NULLPARAMETER);

	memset(&theFile, 0, sizeof(file));

	// Call the "find file" routine to see if we can get the first file
	status = fileFind(fileName, &theFile);
	if (status < 0)
		return (errno = status);

	// Make sure the file isn't empty.  We don't want to try reading
	// data from a nonexistent place on the disk.
	if (!theFile.size)
		// It is empty, so just return
		return (status = 0);

	// The file exists and is non-empty.  That's all we care about (we don't
	// care at this point, for example, whether it's a file or a directory.
	// Read it into memory and print it on the screen.

	// Allocate a buffer to store the file contents in
	fileBuffer = malloc((theFile.blocks * theFile.blockSize) + 1);
	if (!fileBuffer)
		return (errno = ERR_MEMORY);

	status = fileOpen(fileName, OPENMODE_READ, &theFile);
	if (status < 0)
	{
		free(fileBuffer);
		return (errno = status);
	}

	status = fileRead(&theFile, 0, theFile.blocks, fileBuffer);
	if (status < 0)
	{
		free(fileBuffer);
		return (errno = status);
	}

	// Print the file
	for (count = 0; count < theFile.size; count ++)
	{
		// Look out for tab characters
		if (fileBuffer[count] == (char) 9)
			textTab();

		// Look out for newline characters
		else if (fileBuffer[count] == (char) 10)
			textNewline();

		else
			textPutc(fileBuffer[count]);
	}

	// If the file did not end with a newline character...
	if (fileBuffer[count - 1] != '\n')
		textNewline();

	// Free the memory
	free(fileBuffer);

	return (status = 0);
}

