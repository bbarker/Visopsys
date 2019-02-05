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
//  dirent.h
//

// This is the Visopsys version of the standard header file dirent.h

#if !defined(_DIRENT_H)

// Contains the fileType and dirStream definitions
#include <sys/file.h>

// Contains the ino_t definition
#include <sys/types.h>

// Values for the dirent.d_type field, mapped to the Visopsys fileType enum in
// <sys/file.h>
#define DT_BLK		unknownT	// block device (not supported)
#define DT_CHR      unknownT	// character device (not supported)
#define DT_DIR      dirT		// directory
#define DT_FIFO		unknownT	// named pipe (FIFO) (not supported)
#define DT_LNK		linkT		// symbolic link
#define DT_REG		fileT		// regular file
#define DT_SOCK     unknownT	// Unix domain socket (not supported)
#define DT_UNKNOWN	unknownT	// type is unknown

typedef dirStream DIR;

struct dirent {
	ino_t d_ino;					// inode
	unsigned char d_type;			// file type
	char d_name[MAX_NAME_LENGTH];	// name
};

int closedir(DIR *);
DIR *opendir(const char *);
struct dirent *readdir(DIR *);
int readdir_r(DIR *, struct dirent *, struct dirent **);
void rewinddir(DIR *);

#define _DIRENT_H
#endif

