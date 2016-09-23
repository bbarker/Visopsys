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
//  srchpath.c
//

// This is code that can be shared between multiple libraries.  It won't
// compile on its own; it needs to be #included inside a function
// definition.


// Prototype:
//   int srchpath(const char *orig, char *new)
{
	// Search the PATH specified in the environment variable for the specified
	// filename

	int status = 0;
	char *path = NULL;
	int pathCount = 0;
	char *pathElement = NULL;
	int pathElementCount = 0;

	// Check params
	if (!orig || !new)
		return (errno = ERR_NULLPARAMETER);

	if ((orig[0] == '/') || (orig[0] == '\\'))
		return (errno = ERR_NOSUCHFILE);

	path = malloc(MAX_PATH_LENGTH);
	pathElement = malloc(MAX_PATH_NAME_LENGTH);
	if (!path || !pathElement)
		return (errno = ERR_MEMORY);

	// Get the value of the PATH environment variable
	status = environmentGet(ENV_PATH, path, MAX_PATH_LENGTH);
	if (status < 0)
	{
		free(path);
		free(pathElement);
		return (errno = status);
	}

	pathCount = 0;

	// We need to loop once for each element in the PATH.  Elements are
	// separated by colon characters.  When we hit a NULL character we are
	// at the end.

	while (path[pathCount] != '\0')
	{
		pathElementCount = 0;

		// Copy characters from the path until we hit either a ':' or a NULL
		while ((path[pathCount] != ':') && (path[pathCount] != '\0'))
		{
			pathElement[pathElementCount++] = path[pathCount++];
			pathElement[pathElementCount] = '\0';
		}

		if (path[pathCount] == ':')
			pathCount++;

		// Append the name to the path
		strncat(pathElement, "/", 1);
		strcat(pathElement, orig);

		// Does the file exist in the PATH directory?
		status = fileFind(pathElement, NULL);
		if (status >= 0)
		{
			// Copy the full path into the buffer supplied
			strcpy(new, pathElement);

			// Return success
			free(path);
			free(pathElement);
			return (status = 0);
		}
	}

	// If we fall through, no dice
	free(path);
	free(pathElement);
	return (errno = ERR_NOSUCHFILE);
}

