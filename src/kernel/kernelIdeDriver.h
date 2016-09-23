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
//  kernelIdeDriver.h
//

// This header file contains definitions for the kernel's standard IDE/
// ATA/ATAPI driver

#if !defined(_KERNELIDEDRIVER_H)

#include "kernelDisk.h"
#include "kernelLock.h"
#include "kernelMemory.h"

#define IDE_MAX_DISKS			4
#define IDE_MAX_CONTROLLERS		(DISK_MAXDEVICES / IDE_MAX_DISKS)

// Error codes
#define IDE_ADDRESSMARK			0
#define IDE_CYLINDER0			1
#define IDE_INVALIDCOMMAND		2
#define IDE_MEDIAREQ			3
#define IDE_SECTNOTFOUND		4
#define IDE_MEDIACHANGED		5
#define IDE_BADDATA				6
#define IDE_BADSECTOR			7
#define IDE_UNKNOWN				8
#define IDE_TIMEOUT				9

typedef struct {
	int featureFlags;
	int packetMaster;
	char *dmaMode;
	kernelPhysicalDisk physical;

} ideDisk;

typedef struct {
	unsigned data;
	unsigned featErr;
	unsigned sectorCount;
	unsigned lbaLow;
	unsigned lbaMid;
	unsigned lbaHigh;
	unsigned device;
	unsigned comStat;
	unsigned altComStat;

} idePorts;

typedef volatile struct {
	unsigned physicalAddress;
	unsigned short count;
	unsigned short EOT;

} __attribute__((packed)) idePrd;

typedef volatile struct {
	idePorts ports;
	int compatibility;
	int interrupt;
	unsigned char intStatus;
	ideDisk disk[2];
	kernelIoMemory prds;
	int prdEntries;
	int expectInterrupt;
	int gotInterrupt;
	int ints, acks;
	lock lock;

} ideChannel;

typedef volatile struct {
	ideChannel channel[2];
	int busMaster;
	int pciInterrupt;
	unsigned busMasterIo;

} ideController;

#define _KERNELIDEDRIVER_H
#endif

