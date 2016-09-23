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
//  kernelSystemDriver.h
//

#if !defined(_KERNELSYSTEMDRIVER_H)

#include "kernelDevice.h"

#define BIOSROM_START		0x000E0000
#define BIOSROM_END			0x000FFFFF
#define BIOSROM_SIZE		((BIOSROM_END - BIOSROM_START) + 1)
#define BIOSROM_SIG_32		"_32_"
#define BIOSROM_SIG_PNP		"$PnP"
#define BIOS_PNP_VERSION	0x10

// The header for a 32-bit BIOS interface
typedef struct {
	char signature[4];
	void *entryPoint;
	unsigned char revision;
	unsigned char structLen;
	unsigned char checksum;
	unsigned char reserved[5];

} __attribute__((packed)) kernelBios32Header;

// The header for a Plug and Play BIOS
typedef struct {
	char signature[4];
	unsigned char version;
	unsigned char length;
	unsigned short control;
	unsigned char checksum;
	unsigned eventFlagAddr;
	unsigned short realModeEntry;
	unsigned short realModeCodeSeg;
	unsigned short protModeEntry;
	unsigned protModeCodeSeg;
	unsigned oemDevId;
	unsigned short realModeDataSeg;
	unsigned protModeDataSeg;

} __attribute__((packed)) kernelBiosPnpHeader;

typedef struct {
	void *(*driverGetEntry)(kernelDevice *, unsigned char, int);

} kernelMultiProcOps;

#define _KERNELSYSTEMDRIVER_H
#endif

