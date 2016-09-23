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
//  kernelLocale.c
//

// A file for functions related to localization.

#include "kernelLocale.h"
#include "kernelEnvironment.h"
#include "kernelError.h"
#include "kernelFile.h"
#include "kernelMalloc.h"
#include <libintl.h>
#include <stdio.h>
#include <string.h>
#include <sys/message.h>

// Locale categories
static char _lc_all[16];
static char _lc_collate[16];
static char _lc_ctype[16];
static char _lc_messages[16];
static char _lc_monetary[16];
static char _lc_numeric[16];
static char _lc_time[16];

static messages *msgFile = NULL;


static char *setCategory(const char *name, char *category, const char *locale)
{
	int status = 0;
	char buffer[LOCALE_MAX_NAMELEN + 1];
	category[0] = '\0';

	if (!strcmp(locale, ""))
	{
		// If locale is "", the locale is modified according to environment
		// variables.
		status = kernelEnvironmentGet(name, buffer, LOCALE_MAX_NAMELEN);
		if (status >= 0)
			strncpy(category, buffer, LOCALE_MAX_NAMELEN);
		else
			strcpy(category, C_LOCALE_NAME);
	}
	else
		strncpy(category, locale, LOCALE_MAX_NAMELEN);

	category[LOCALE_MAX_NAMELEN] = '\0';
	return (category);
}


static void freeMessageFile(void)
{
	if (msgFile)
	{
		if (msgFile->buffer)
			kernelFree(msgFile->buffer);
		kernelFree(msgFile);
		msgFile = NULL;
	}
}


static int loadMessageFile(void)
{
	int status = 0;
	const char *domain = "kernel";
	char *path = NULL;
	file fileStructure;

	memset(&fileStructure, 0, sizeof(file));

	// Allocate memory for the full pathname
	path = kernelMalloc(MAX_PATH_NAME_LENGTH);
	if (!path)
	{
		status = ERR_MEMORY;
		goto out;
	}

	// Get the path to the appropriate file
	sprintf(path, "%s/%s/LC_MESSAGES/%s.mo", GETTEXT_LOCALEDIR_PREFIX,
		_lc_messages, domain);

	// Does it exist?
	status = kernelFileOpen(path, OPENMODE_READ, &fileStructure);
	if (status < 0)
		goto out;

	// Free any old message file
	freeMessageFile();

	// Get memory for the msgfile structure and its buffer
	msgFile = kernelMalloc(sizeof(messages));
	if (msgFile)
		msgFile->buffer =
			kernelMalloc(fileStructure.blocks * fileStructure.blockSize);
	if (!msgFile || !msgFile->buffer)
	{
		status = ERR_MEMORY;
		goto out;
	}

	strcpy(msgFile->domain, domain);
	strcpy(msgFile->locale, _lc_messages);

	status = kernelFileRead(&fileStructure, 0, fileStructure.blocks,
		msgFile->buffer);
	if (status < 0)
		goto out;

	msgFile->header = msgFile->buffer;

	if (msgFile->header->magic != MESSAGE_MAGIC)
	{
		status = ERR_BADDATA;
		goto out;
	}

	if (msgFile->header->version != MESSAGE_VERSION)
	{
		status = ERR_BADDATA;
		goto out;
	}

	msgFile->origTable = (msgFile->buffer + msgFile->header->origTableOffset);
	msgFile->transTable = (msgFile->buffer + msgFile->header->transTableOffset);

	status = 0;

out:
	if (path)
		kernelFree(path);

	if (fileStructure.handle)
		kernelFileClose(&fileStructure);

	if (status)
		freeMessageFile();

	return (status);
}


/////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////
//
//  Below here, the functions are exported for external use
//
/////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////


char *kernelSetLocale(int category, const char *locale)
{
	char *returnLocale = NULL;

	if (!locale)
		return (returnLocale = NULL);

	// Note that these calls are not redundant since each one of these
	// flags can result in copying into different categories

	if ((category & LC_ALL) == LC_ALL)
		returnLocale = setCategory("LC_ALL", _lc_all, locale);
	else
		strcpy(_lc_all, C_LOCALE_NAME);

	if (category & LC_COLLATE)
		returnLocale = setCategory("LC_COLLATE", _lc_collate, locale);
	else
		strcpy(_lc_collate, C_LOCALE_NAME);

	if (category & LC_CTYPE)
		returnLocale = setCategory("LC_CTYPE", _lc_ctype, locale);
	else
		strcpy(_lc_ctype, C_LOCALE_NAME);

	if (category & LC_MESSAGES)
		returnLocale = setCategory("LC_MESSAGES", _lc_messages, locale);
	else
		strcpy(_lc_messages, C_LOCALE_NAME);

	if (category & LC_MONETARY)
		returnLocale = setCategory("LC_MONETARY", _lc_monetary, locale);
	else
		strcpy(_lc_monetary, C_LOCALE_NAME);

	if (category & LC_NUMERIC)
		returnLocale = setCategory("LC_NUMERIC", _lc_numeric, locale);
	else
		strcpy(_lc_numeric, C_LOCALE_NAME);

	if (category & LC_TIME)
		returnLocale = setCategory("LC_TIME", _lc_time, locale);
	else
		strcpy(_lc_time, C_LOCALE_NAME);

	// Since there's only ever one message file, load it.
	if (loadMessageFile() < 0)
		return (returnLocale = NULL);

	return (returnLocale);
}


char *kernelGetText(const char *msgid)
{
	char *trans = (char *) msgid;
	int count;

	if (!msgFile)
		return (trans);

	// No hashing, just loop and search.  If we can ensure later that they're
	// ordered alphabetically, then we can replace this with a binary search for
	// speed.
	if (!strcmp(msgFile->locale, _lc_messages))
	{
		for (count = 0; count < msgFile->header->numStrings; count ++)
		{
			if (!strcmp((msgFile->buffer + msgFile->origTable[count].offset),
				msgid))
			{
				trans = (msgFile->buffer + msgFile->transTable[count].offset);
				break;
			}
		}
	}

	return (trans);
}

