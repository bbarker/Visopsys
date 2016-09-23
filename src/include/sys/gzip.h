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
//  gzip.h
//

// This file contains definitions and structures used by the Gzip archive
// file format.

#if !defined(_GZIP_H)

#define GZIP_MAGIC			0x8B1F
#define GZIP_COMP_DEFLATE	0x08	// Deflate

// GZIP member flag fields
#define GZIP_FLG_FTEXT		0x01
#define GZIP_FLG_FHCRC		0x02
#define GZIP_FLG_FEXTRA		0x04
#define GZIP_FLG_FNAME		0x08
#define GZIP_FLG_FCOMMENT	0x10

// GZIP OS values
#define GZIP_OS_FAT			0x00
#define GZIP_OS_AMIGA		0x01
#define GZIP_OS_VMS			0x02
#define GZIP_OS_UNIX		0x03
#define GZIP_OS_VMCMS		0x04
#define GZIP_OS_ATARITOS	0x05
#define GZIP_OS_HPFS		0x06
#define GZIP_OS_MAC			0x07
#define GZIP_OS_ZSYSTEM		0x08
#define GZIP_OS_CPM			0x09
#define GZIP_OS_TOPS20		0x0A
#define GZIP_OS_NTFS		0x0B
#define GZIP_OS_QDOS		0x0C
#define GZIP_OS_ACORN		0x0D
#define GZIP_OS_UNKNOWN		0xFF

typedef struct {
	unsigned short sig;
	unsigned char compMethod;
	unsigned char flags;
	unsigned modTime;
	unsigned char extraFlags;
	unsigned char opSys;

} __attribute__((packed)) gzipMember;

typedef struct {
	unsigned short len;
	unsigned char data[];

} __attribute__((packed)) gzipExtraField;

#define _GZIP_H
#endif

