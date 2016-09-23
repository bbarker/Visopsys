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
//  ls.c
//

// Yup, it's the UNIX-style command for viewing directory listings

/* This is the text that appears when a user requests help about this program
<help>

 -- ls --

List files.

Synonym:
  dir

Usage:
  ls [name1] [name2] [...]

This command will list (show information about) one or more files or
directories.  If no parameters are specified, 'ls' will display the contents
of the current directory.  If any of the parameters specify directories,
all of the files in those directories are displayed.  If any of the parameters
are the names of individual files, then information about those specific
files are displayed.

</help>
*/

#include <stdio.h>
#include <sys/vsh.h>
#include <sys/api.h>


int main(int argc, char *argv[])
{
	int status = 0;
	char fileName[MAX_PATH_NAME_LENGTH];
	int count;

	// If we got no arguments, then we assume we are operating on the
	// current directory.
	if (argc == 1)
	{
		// Get the current directory
		multitaskerGetCurrentDirectory(fileName, MAX_PATH_NAME_LENGTH);

		status = vshFileList(fileName);
		if (status < 0)
		{
			perror(argv[0]);
			return (status);
		}
	}
	else
	{
		for (count = 1; count < argc; count ++)
		{
			status = vshFileList(argv[count]);
			if (status < 0)
				perror(argv[0]);
		}
	}

	// Return success
	return (status = 0);
}

