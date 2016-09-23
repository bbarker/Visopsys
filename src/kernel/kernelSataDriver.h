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
//  kernelSataDriver.h
//

// This header file contains generic definitions for SATA drivers

#if !defined(_KERNELSATADRIVER_H)

// FIS types
#define SATA_FIS_REGH2D		0x27
#define SATA_FIS_REGD2H		0x34
#define SATA_FIS_DMAACT		0x39
#define SATA_FIS_DMASETUP	0x41
#define SATA_FIS_DATA		0x46
#define SATA_FIS_BIST		0x58
#define SATA_FIS_PIOSETUP	0x5F
#define SATA_FIS_DEVBITS	0xA1

// Device type signatures
#define SATA_SIG_ATA		0x00000101
#define SATA_SIG_PM			0x96690101
#define SATA_SIG_EMB		0xC33C0101
#define SATA_SIG_ATAPI		0xEB140101

typedef volatile union {
	struct {
		unsigned char fisType;
		unsigned char portMulti:5;
		unsigned char res1:2;
		unsigned char isCommand:1;
		unsigned char command;
		unsigned char features7_0;

		unsigned char lba7_0;
		unsigned char lba15_8;
		unsigned char lba23_16;
		unsigned char device;

		unsigned char lba31_24;
		unsigned char lba39_32;
		unsigned char lba47_40;
		unsigned char features15_8;

		unsigned char count7_0;
		unsigned char count15_8;
		unsigned char icc;
		unsigned char control;

		unsigned char res2[4];

	} fields;

	unsigned dwords[5];

} __attribute__((packed)) sataFisRegH2D;

typedef volatile union {
	struct {
		unsigned char fisType;
		unsigned char intrWrite;
		unsigned char status;
		unsigned char error;

		unsigned char lba7_0;
		unsigned char lba15_8;
		unsigned char lba23_16;
		unsigned char device;

		unsigned char lba31_24;
		unsigned char lba39_32;
		unsigned char lba47_40;
		unsigned char res1;

		unsigned char count7_0;
		unsigned char count15_8;
		unsigned char res2[2];

		unsigned char res3[4];

	} fields;

	unsigned dwords[5];

} __attribute__((packed)) sataFisRegD2H;

typedef volatile struct {
	unsigned char res[28];

} __attribute__((packed)) sataFisDmaSetup;

typedef volatile struct {
	unsigned char res[20];

} __attribute__((packed)) sataFisPioSetup;

typedef volatile struct {
	unsigned char fisType;
	unsigned char intr;
	unsigned char status;
	unsigned char error;
	unsigned res;

} __attribute__((packed)) sataFisDevBits;

#define _KERNELSATADRIVER_H
#endif

