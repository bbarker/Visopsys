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
//  tar.h
//

// This file contains definitions and structures used by the TAR archive
// file format.
//
// Info gathered from
// https://www.gnu.org/software/tar/manual/html_node/Standard.html

#if !defined(_TAR_H)

#define TAR_MAGIC				"ustar"
#define TAR_OLDMAGIC			"ustar  "
#define TAR_MAX_NAMELEN			100
#define TAR_MAX_PREFIX			155
#define TAR_OLD_SPARSES			4

// The mode field (file permissions) - values in octal
#define TAR_MODE_SUID			04000	// set UID on execution
#define TAR_MODE_SGID			02000	// set GID on execution
#define TAR_MODE_SVTX			01000	// save text (sticky bit)
#define TAR_MODE_UREAD			00400	// read by owner
#define TAR_MODE_UWRITE			00200	// write by owner
#define TAR_MODE_UEXEC			00100	// execute/search by owner
#define TAR_MODE_GREAD			00040	// read by group
#define TAR_MODE_GWRITE			00020	// write by group
#define TAR_MODE_GEXEC			00010	// execute/search by group
#define TAR_MODE_OREAD			00004	// read by other
#define TAR_MODE_OWRITE			00002	// write by other
#define TAR_MODE_OEXEC			00001	// execute/search by other

// The typeFlag (linkflag) indicates the type of file
#define TAR_TYPEFLAG_OLDNORMAL	'\0'	// normal file, Unix compatible
#define TAR_TYPEFLAG_NORMAL		'0'		// normal file
#define TAR_TYPEFLAG_LINK		'1'		// link to previously dumped file
#define TAR_TYPEFLAG_SYMLINK	'2'		// symbolic link
#define TAR_TYPEFLAG_CHR		'3'		// character special file
#define TAR_TYPEFLAG_BLK		'4'		// block special file
#define TAR_TYPEFLAG_DIR		'5'		// directory
#define TAR_TYPEFLAG_FIFO		'6'		// FIFO special file
#define TAR_TYPEFLAG_CONTIG		'7'		// contiguous file

typedef struct {
	char pad1[345];				// 0
	char atime[12];				// 345
	char ctime[12];				// 357
	char offset[12];			// 369 (multivolume archive)
	char longNames[4];			// 381
	char unused;				// 385
	struct {					// 386
		char offset[12];
		char numbytes[12];
	} sparse[TAR_OLD_SPARSES];
	char isExtended;			// 482
	char realSize[12];			// 483
	char pad2[17];				// 495

} tarOldHeader;

typedef struct {
	char name[TAR_MAX_NAMELEN];	// 0
	char mode[8];				// 100
	char uid[8];				// 108
	char gid[8];				// 116
	char size[12];				// 124
	char modTime[12];			// 136
	char checksum[8];			// 148
	char typeFlag;				// 156
	char linkName[100];			// 157
	char magic[6];				// 257
	char version[2];			// 263
	char uname[32];				// 265
	char gname[32];				// 297
	char devMajor[8];			// 329
	char devMinor[8];			// 337
	char prefix[TAR_MAX_PREFIX];// 345
	char pad[12];				// 500

} tarHeader;

#define _TAR_H
#endif

