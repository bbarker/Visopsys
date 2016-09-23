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
//  abspath.c
//

// This is code that can be shared between multiple libraries.  It won't
// compile on its own; it needs to be #included inside a function
// definition.


// Prototype:
//   void abspath(const char *orig, char *new)
{
	int status = 0;

	// Check params
	if (!orig || !new)
	{
		errno = ERR_NULLPARAMETER;
		return;
	}

	if ((orig[0] != '/') && (orig[0] != '\\'))
	{
		// Get the current directory
		status = multitaskerGetCurrentDirectory(new, MAX_PATH_LENGTH);
		if (status < 0)
		{
			errno = status;
			return;
		}

		if ((new[strlen(new) - 1] != '/') && (new[strlen(new) - 1] != '\\'))
			strcat(new, "/");

		strcat(new, orig);
	}

	else
		strcpy(new, orig);

	return;
}

