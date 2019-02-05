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
//  stat.h
//

// This file is the Visopsys implementation of the standard <sys/stat.h>
// file found in Unix.

#if !defined(_STAT_H)

// Contains the time_t definition
#include <time.h>
// Contains the rest of the definitions
#include <sys/types.h>

// mode_t values for st_mode - values in octal
#define S_IFMT		0170000	// bit mask for the file type
#define S_IFSOCK	0140000	// socket
#define S_IFLNK		0120000	// symbolic link
#define S_IFREG		0100000	// regular file
#define S_IFBLK		0060000	// block device
#define S_IFDIR		0040000	// directory
#define S_IFCHR		0020000	// character device
#define S_IFIFO		0010000	// FIFO
#define S_ISUID		0004000	// set UID bit
#define S_ISGID		0002000	// set-group-ID bit
#define S_ISVTX		0001000	// sticky bit

// Macros for interpreting st_mode
#define S_ISREG(m)	(((m) & S_IFREG) == S_IFREG)	// regular file?
#define S_ISDIR(m)  (((m) & S_IFDIR) == S_IFDIR)	// directory?
#define S_ISCHR(m)  (((m) & S_IFCHR) == S_IFCHR)	// character device?
#define S_ISBLK(m)  (((m) & S_IFBLK) == S_IFBLK)	// block device?
#define S_ISFIFO(m) (((m) & S_IFIFO) == S_IFIFO)	// FIFO (named pipe)?
#define S_ISLNK(m)  (((m) & S_IFLNK) == S_IFLNK)	// symbolic link?
#define S_ISSOCK(m) (((m) & S_IFSOCK) == S_IFSOCK)	// socket?

struct stat {
	dev_t st_dev;			// device
	ino_t st_ino;			// inode
	mode_t st_mode;			// protection
	nlink_t st_nlink;		// number of hard links
	uid_t st_uid;			// user ID of owner
	gid_t st_gid;			// group ID of owner
	dev_t st_rdev;			// device type (if inode device)
	off_t st_size;			// total size, in bytes
	blksize_t st_blksize;	// blocksize for filesystem I/O
	blkcnt_t st_blocks;		// number of blocks allocated
	time_t st_atime;		// time of last access
	time_t st_mtime;		// time of last modification
	time_t st_ctime;		// time of last change
};

int mkdir(const char *, mode_t);
int stat(const char *, struct stat *);

#define _STAT_H
#endif

