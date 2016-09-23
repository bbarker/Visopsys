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
//  loaderInfo.h
//

// This header file contains information about the information structure
// passed to the kernel by the OS loader at startup.

#if !defined(_LOADERINFO_H)

#include <sys/graphic.h>

// The data structure created by the loader (actually, by the BIOS) to
// describe a memory range

// The types of memory ranges described by the memoryInfoBlock structure,
// below
typedef enum {
	available = 1,
	reserved  = 2,
	acpi_reclaim = 3,
	acpi_nvs = 4,
	bad = 5

} memoryRangeType;

typedef struct {
	long long start;
	long long size;
	memoryRangeType type;

} memoryInfoBlock;

// The data structure created by the loader to describe the particulars
// about the current graphics environment to the kernel
typedef struct {
	unsigned videoMemory;
	void *framebuffer;
	int mode;
	int xRes;
	int yRes;
	int bitsPerPixel;
	int scanLineBytes;
	int numberModes;
	videoMode supportedModes[MAXVIDEOMODES];

} graphicsInfoBlock;

// The data structure created by the loader to describe the particulars
// about a floppy disk drive to the kernel
typedef struct {
	int type;
	int heads;
	int tracks;
	int sectors;

} fddInfoBlock;

// The data structure created by the loader to describe the particulars
// about a hard disk drive to the kernel
typedef struct {
	unsigned heads;
	unsigned cylinders;
	unsigned sectorsPerCylinder;
	unsigned bytesPerSector;
	unsigned totalSectors;

} hddInfoBlock;

// The data structure created by the loader to hold info about the serial
// ports
typedef struct {
	unsigned port1;
	unsigned port2;
	unsigned port3;
	unsigned port4;

} serialInfoBlock;

// The data structure created by the loader to describe the system's hardware
// to the kernel
typedef struct {
	int cpuType;
	char cpuVendor[16];
	int mmxExtensions;
	unsigned extendedMemory;
	memoryInfoBlock memoryMap[50];
	graphicsInfoBlock graphicsInfo;
	unsigned bootSectorSig;
	int bootCd;
	int floppyDisks;
	fddInfoBlock fddInfo[2];
	serialInfoBlock serialPorts;

} __attribute__((packed)) loaderInfoStruct;

#define _LOADERINFO_H
#endif

