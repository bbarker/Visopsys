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
//  bindtextdomain.c
//

// This is the Visopsys version of the function from the GNU gettext library

#include <errno.h>
#include <libintl.h>
#include <stdlib.h>
#include <string.h>

char *_getDirName(void);

char *_gettext_dirname = NULL;


char *_getDirName(void)
{
	return (_gettext_dirname);
}


char *bindtextdomain(const char *domainname, const char *dirname)
{
	// Sets the 'domain' and message file directory for messages.  This means
	// the filename of the messages file and the directory it can be found in.

	if (!domainname || !dirname)
	{
		errno = ERR_NULLPARAMETER;
		return (NULL);
	}

	// Call our companion function to set the domain name
	if (!textdomain(domainname))
		return (NULL);

	// If we previously allocated memory for a dirname, free it.
	if (_gettext_dirname)
	{
		free(_gettext_dirname);
		_gettext_dirname = NULL;
	}

	if (strcmp(dirname, ""))
	{
		_gettext_dirname = malloc(strlen(dirname) + 1);
		if (!_gettext_dirname)
		{
			errno = ERR_MEMORY;
			return (_gettext_dirname);
		}

		strcpy(_gettext_dirname, dirname);
	}

	return (_gettext_dirname);
}

