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
//  message.h
//

// This file describes the MO message format of the GNU gettext library

#if !defined(_MESSAGE_H)

#include <locale.h>
#include <sys/file.h>

#define MESSAGE_MAGIC		0x950412DE
#define MESSAGE_VERSION		0

typedef struct {
	int length;
	unsigned offset;

} __attribute__((packed)) messageStringEntry;

typedef struct {
	unsigned magic;
	int version;
	int numStrings;
	unsigned origTableOffset;
	unsigned transTableOffset;
	unsigned hashTableSize;
	unsigned hashTableOffset;

} __attribute__((packed)) messageFileHeader;

// A structure for storing and referencing a message file
typedef struct {
	char domain[MAX_NAME_LENGTH + 1];
	char locale[LOCALE_MAX_NAMELEN + 1];
	void *buffer;
	messageFileHeader *header;
	messageStringEntry *origTable;
	messageStringEntry *transTable;

} messages;

#define _MESSAGE_H
#endif

