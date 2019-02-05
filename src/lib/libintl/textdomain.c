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
//  textdomain.c
//

// This is the Visopsys version of the function from the GNU gettext library

#include <errno.h>
#include <fcntl.h>
#include <libintl.h>
#include <locale.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/message.h>
#include <sys/stat.h>

extern char *_getDirName(void);
extern char *_getLocaleCategory(int);
void _getMessageFiles(messages ***, int *);

static messages **msgFiles = NULL;
static int numFiles = 0;


static char *getFilePath(const char *domain)
{
	// Return the path to the appropriate messages file for the current messages
	// category and domain name.

	char *path = NULL;

	// Allocate memory for the full pathname
	path = calloc(MAX_PATH_NAME_LENGTH, 1);
	if (!path)
		return (path);

	sprintf(path, "%s/%s/LC_MESSAGES/%s.mo",
		(_getDirName()? _getDirName() : GETTEXT_LOCALEDIR_PREFIX),
		_getLocaleCategory(LC_MESSAGES), domain);

	return (path);
}


static int findMessageFile(const char *domain)
{
	int status = ERR_NOSUCHFILE;
	int count;

	for (count = 0; count < numFiles; count ++)
	{
		if (!strcmp(msgFiles[count]->domain, domain) &&
			!strcmp(msgFiles[count]->locale, _getLocaleCategory(LC_MESSAGES)))
		{
			status = count;
			break;
		}
	}

	return (status);
}


static int loadMessageFile(const char *domain)
{
	int status = 0;
	char *path = NULL;
	struct stat pathStat;
	int fd = -1;
	messages *msgfile = NULL;
	unsigned fpos = 0;

	memset(&pathStat, 0, sizeof(struct stat));

	// Get the path to the appropriate file
	path = getFilePath(domain);
	if (!path)
	{
		status = ERR_MEMORY;
		goto out;
	}

	// Does it exist?
	status = stat(path, &pathStat);
	if (status < 0)
		goto out;

	fd = open(path, O_RDONLY);
	if (fd < 0)
	{
		status = fd;
		goto out;
	}

	// Get memory for the msgfile structure and its buffer
	msgfile = calloc(1, sizeof(messages));
	if (msgfile)
		msgfile->buffer = calloc(pathStat.st_size, 1);
	if (!msgfile || !msgfile->buffer)
	{
		status = ERR_MEMORY;
		goto out;
	}

	strncpy(msgfile->domain, domain, MAX_NAME_LENGTH);
	strncpy(msgfile->locale, _getLocaleCategory(LC_MESSAGES),
		LOCALE_MAX_NAMELEN);

	while (fpos < pathStat.st_size)
	{
		status = read(fd, msgfile->buffer, (pathStat.st_size - fpos));
		if (status < 0)
			goto out;

		if (!status)
			break;

		fpos += status;
	}

	if (fpos < pathStat.st_size)
	{
		status = ERR_NODATA;
		goto out;
	}

	msgfile->header = msgfile->buffer;

	if (msgfile->header->magic != MESSAGE_MAGIC)
	{
		status = ERR_BADDATA;
		goto out;
	}

	if (msgfile->header->version != MESSAGE_VERSION)
	{
		status = ERR_BADDATA;
		goto out;
	}

	msgfile->origTable = (msgfile->buffer + msgfile->header->origTableOffset);
	msgfile->transTable = (msgfile->buffer + msgfile->header->transTableOffset);

	msgFiles = realloc(msgFiles, ((numFiles + 1) * sizeof(messages *)));
	if (!msgFiles)
	{
		status = ERR_MEMORY;
		goto out;
	}

	msgFiles[numFiles++] = msgfile;

	status = 0;

out:
	if (path)
		free(path);

	if (fd >= 0)
		close(fd);

	if (status && msgfile)
	{
		if (msgfile->buffer)
			free(msgfile->buffer);
		free(msgfile);
	}

	return (status);
}


void _getMessageFiles(messages **msgs[], int *num)
{
	*msgs = msgFiles;
	*num = numFiles;
}


char *textdomain(const char *domain)
{
	// Sets the 'domain' for messages.  This means the filename of the messages
	// file.

	if (!domain)
	{
		errno = ERR_NULLPARAMETER;
		return (NULL);
	}

	// If the domain is "", use the default ("messages")
	if (!strcmp(domain, ""))
		domain = GETTEXT_DEFAULT_DOMAIN;

	// Have we loaded a file for this domain?
	if (findMessageFile(domain) < 0)
		loadMessageFile(domain);

	return ((char *) domain);
}

