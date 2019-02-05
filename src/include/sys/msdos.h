//
//  Visopsys
//  Copyright (C) 1998-2018 J. Andrew McLaughlin
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
//  msdos.h
//

// This is the header for the handling of MS-DOS disk labels

#if !defined(_MSDOS_H)

#define MSDOS_BOOT_CODE_SIZE		440
#define MSDOS_BOOT_SIGNATURE		0xAA55
#define MSDOS_TABLE_OFFSET			0x01BE
#define MSDOS_TABLE_ENTRIES			4

// MS-DOS partition tags of interest.
#define MSDOSTAG_FAT12				0x01
#define MSDOSTAG_FAT16_SM			0x04
#define MSDOSTAG_EXTD				0x05
#define MSDOSTAG_FAT16				0x06
#define MSDOSTAG_HPFS_NTFS			0x07
#define MSDOSTAG_FAT32				0x0B
#define MSDOSTAG_FAT32_LBA			0x0C
#define MSDOSTAG_FAT16_LBA			0x0E
#define MSDOSTAG_EXTD_LBA			0x0F
#define MSDOSTAG_HIDDEN_FAT12		0x11
#define MSDOSTAG_HIDDEN_FAT16_SM	0x14
#define MSDOSTAG_HIDDEN_FAT16		0x16
#define MSDOSTAG_HIDDEN_HPFS_NTFS	0x17
#define MSDOSTAG_HIDDEN_FAT32		0x1B
#define MSDOSTAG_HIDDEN_FAT32_LBA	0x1C
#define MSDOSTAG_HIDDEN_FAT16_LBA	0x1E
#define MSDOSTAG_LINUX				0x83
#define MSDOSTAG_EXTD_LINUX			0x85
#define MSDOSTAG_HIDDEN_LINUX		0x93
#define MSDOSTAG_EFI_GPT_PROT		0xEE

#define MSDOSTAG_IS_EXTD(x) \
	((x == MSDOSTAG_EXTD) || (x == MSDOSTAG_EXTD_LBA) || \
	(x == MSDOSTAG_EXTD_LINUX))
#define MSDOSTAG_IS_HIDDEN(x) \
	((x == MSDOSTAG_HIDDEN_FAT12) || (x == MSDOSTAG_HIDDEN_FAT16_SM) || \
	(x == MSDOSTAG_HIDDEN_FAT16) || (x == MSDOSTAG_HIDDEN_HPFS_NTFS) || \
	(x == MSDOSTAG_HIDDEN_FAT32) || (x == MSDOSTAG_HIDDEN_FAT32_LBA) || \
	(x == MSDOSTAG_HIDDEN_FAT16_LBA) || (x == MSDOSTAG_HIDDEN_LINUX))
#define MSDOSTAG_IS_HIDEABLE(x) \
	((x == MSDOSTAG_FAT12) || (x == MSDOSTAG_FAT16_SM) || \
	(x == MSDOSTAG_FAT16) || (x == MSDOSTAG_HPFS_NTFS) || \
	(x == MSDOSTAG_FAT32) || (x == MSDOSTAG_FAT32_LBA) || \
	(x == MSDOSTAG_FAT16_LBA) || (x == MSDOSTAG_LINUX))

typedef struct {
	unsigned char driveActive;
	unsigned char startHead;
	unsigned char startCylSect;
	unsigned char startCyl;
	unsigned char tag;
	unsigned char endHead;
	unsigned char endCylSect;
	unsigned char endCyl;
	unsigned startLogical;
	unsigned sizeLogical;

} __attribute__((packed)) msdosEntry;

typedef struct {
	msdosEntry entries[MSDOS_TABLE_ENTRIES];

} __attribute__((packed)) msdosTable;

typedef struct {
	unsigned char bootcode[MSDOS_BOOT_CODE_SIZE];
	unsigned diskSig;
	unsigned short pad;
	msdosTable partTable;
	unsigned short bootSig;	// MSDOS_BOOT_SIGNATURE

} __attribute__((packed)) msdosMbr;

#define _MSDOS_H
#endif

