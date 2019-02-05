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
//  file.h
//

// This file contains definitions and structures for using and manipulating
// files in Visopsys.

#if !defined(_FILE_H)

#include <time.h>

// File open modes
#define OPENMODE_READ			0x01
#define OPENMODE_WRITE			0x02
#define OPENMODE_READWRITE		(OPENMODE_READ | OPENMODE_WRITE)
#define OPENMODE_CREATE			0x04
#define OPENMODE_TRUNCATE		0x08
#define OPENMODE_DELONCLOSE		0x10

// Pathname limits
#define MAX_NAME_LENGTH			512
#define MAX_PATH_LENGTH			512
#define MAX_PATH_NAME_LENGTH	(MAX_PATH_LENGTH + MAX_NAME_LENGTH)

#define OPENMODE_ISREADONLY(mode) \
	(((mode) & OPENMODE_READ) && !((mode) & OPENMODE_WRITE))
#define OPENMODE_ISWRITEONLY(mode) \
	(((mode) & OPENMODE_WRITE) && !((mode) & OPENMODE_READ))

// Typedef a file handle
typedef void* fileHandle;

typedef enum {
	unknownT, fileT, dirT, linkT, volT

} fileType;

// This is the structure used to store universal information about a file
typedef struct {
	fileHandle handle;
	char name[MAX_NAME_LENGTH];
	fileType type;
	char filesystem[MAX_PATH_LENGTH];
	struct tm created;
	struct tm accessed;
	struct tm modified;
	unsigned size;
	unsigned blocks;
	unsigned blockSize;
	int openMode;

} file;

// A file 'stream', for character-based file IO
typedef struct {
	file f;
	unsigned offset;
	unsigned block;
	unsigned size;
	int dirty;
	unsigned char *buffer;

} fileStream;

// A directory 'stream', for iterating through directory entries
typedef struct {
	char *name;
	file f;
	void *entry;

} dirStream;

#define _FILE_H
#endif

