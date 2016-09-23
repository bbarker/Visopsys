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
//  fat.h
//

// This file contains definitions and structures for using and manipulating
// the Microsoft(R) FAT filesystems in Visopsys.

#if !defined(_FAT_H)

// FAT-specific Filesystem things
#define FAT_MAX_SECTORSIZE				4096
#define FAT_BYTES_PER_DIR_ENTRY			32
#define FAT_MAX_DIRTY_FATSECTS			32
#define FAT_8_3_NAME_LEN				11

// File attributes
#define FAT_ATTRIB_READONLY				0x01
#define FAT_ATTRIB_HIDDEN				0x02
#define FAT_ATTRIB_SYSTEM				0x04
#define FAT_ATTRIB_VOLUMELABEL			0x08
#define FAT_ATTRIB_SUBDIR				0x10
#define FAT_ATTRIB_ARCHIVE				0x20

typedef struct {
	unsigned char jmpBoot[3];
	char oemName[8];					// 03 - 0A OEM Name
	unsigned short bytesPerSect;		// 0B - 0C Bytes per sector
	unsigned char sectsPerClust;		// 0D - 0D Sectors per cluster
	unsigned short rsvdSectCount;		// 0E - 0F Reserved sectors
	unsigned char numFats;				// 10 - 10 Copies of FAT
	unsigned short rootEntCount;		// 11 - 12 Max root dir entries
	unsigned short totalSects16;		// 13 - 14 Number of sectors
	unsigned char media;				// 15 - 15 Media descriptor byte
	unsigned short fatSize16;			// 16 - 17 Sectors per FAT
	unsigned short sectsPerTrack;		// 18 - 19 Sectors per track
	unsigned short numHeads;			// 1A - 1B Number of heads
	unsigned hiddenSects;				// 1C - 1F Hidden sectors
	unsigned totalSects32;				// 20 - 23 Number of sectors (32)
	// From here, the BPB for FAT and VFAT differ
	union {
		struct {
			unsigned char biosDriveNum;	// 24 - 24 BIOS drive number
			unsigned char reserved1;	// 25 - 25 ?
			unsigned char bootSig;		// 26 - 26 Signature
			unsigned volumeId;			// 27 - 2A Volume ID
			char volumeLabel[FAT_8_3_NAME_LEN]; // 2B - 35 Volume name
			char fileSysType[8];		// 36 - 3D Filesystem type
			unsigned char bootCode[448];

		} __attribute__((packed)) fat;

		struct {
			unsigned fatSize32;			// 24 - 27 Sectors per FAT (32)
			unsigned short extFlags;	// 28 - 29 Flags
			unsigned short fsVersion;	// 2A - 2B FS version number
			unsigned rootClust;			// 2C - 2F Root directory cluster
			unsigned short fsInfo;		// 30 - 31 FSInfo struct sector
			unsigned short backupBootSect; // 32 - 33 Backup boot sector
			unsigned char reserved[12];	// 34 - 3F ?
			unsigned char biosDriveNum;	// 40 - 40 BIOS drive number
			unsigned char reserved1;	// 41 - 41 ?
			unsigned char bootSig;		// 42 - 42 Signature
			unsigned volumeId;			// 43 - 46 Volume ID
			char volumeLabel[FAT_8_3_NAME_LEN]; // 47 - 51 Volume name
			char fileSysType[8];		// 52 - 59 Filesystem type
			unsigned char bootCode[414];

		} __attribute__((packed)) fat32;
	};
	unsigned short signature;

} __attribute__((packed)) fatBPB;

typedef struct {
	unsigned leadSig;
	unsigned char reserved1[480];
	unsigned structSig;
	unsigned freeCount;
	unsigned nextFree;
	unsigned char reserved2[12];
	unsigned trailSig;

} __attribute__((packed)) fatFsInfo;

#define _FAT_H
#endif

