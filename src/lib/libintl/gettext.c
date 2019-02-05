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
//  gettext.c
//

// This is the Visopsys version of the function from the GNU gettext library

#include <libintl.h>
#include <string.h>
#include <sys/message.h>

extern void _getMessageFiles(messages **[], int *);
extern char *_getLocaleCategory(int);


char *gettext(const char *msgid)
{
	char *trans = (char *) msgid;
	messages **msgFiles = NULL;
	int numFiles = 0;
	int count1, count2;

	_getMessageFiles(&msgFiles, &numFiles);

	// No hashing, just loop and search.  If we can ensure later that they're
	// ordered alphabetically, then we can replace this with a binary search for
	// speed.
	for (count1 = 0; count1 < numFiles; count1 ++)
	{
		if (!strcmp(msgFiles[count1]->locale, _getLocaleCategory(LC_MESSAGES)))
		{
			for (count2 = 0; count2 < msgFiles[count1]->header->numStrings;
				count2 ++)
			{
				if (!strcmp((msgFiles[count1]->buffer +
					msgFiles[count1]->origTable[count2].offset), msgid))
				{
					trans = (msgFiles[count1]->buffer +
						msgFiles[count1]->transTable[count2].offset);
					break;
				}
			}
		}
	}

	return (trans);
}

