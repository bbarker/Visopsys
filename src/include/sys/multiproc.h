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
//  multiproc.h
//

// This file contains definitions and structures defined by the Intel
// multiprocessor standard.

#if !defined(_MULTIPROC_H)

#define MULTIPROC_SIG_FLOAT				"_MP_"
#define MULTIPROC_SIG_CONFIG			"PCMP"

// Configuration table entry types
#define MULTIPROC_ENTRY_CPU				0
#define MULTIPROC_ENTRY_BUS				1
#define MULTIPROC_ENTRY_IOAPIC			2
#define MULTIPROC_ENTRY_IOINTASSMT		3
#define MULTIPROC_ENTRY_LOCINTASSMT		4

// Bus type strings
#define MULTIPROC_BUSTYPE_CBUS			"CBUS  " // Corollary CBus
#define MULTIPROC_BUSTYPE_CBUSII		"CBUSII" // Corollary CBUS II
#define MULTIPROC_BUSTYPE_EISA			"EISA  " // Extended ISA
#define MULTIPROC_BUSTYPE_FUTURE		"FUTURE" // IEEE FutureBus
#define MULTIPROC_BUSTYPE_INTERN		"INTERN" // Internal bus
#define MULTIPROC_BUSTYPE_ISA			"ISA   " // Industry Standard Arch
#define MULTIPROC_BUSTYPE_MBI			"MBI   " // Multibus I
#define MULTIPROC_BUSTYPE_MBII			"MBII  " // Multibus II
#define MULTIPROC_BUSTYPE_MCA			"MCA   " // Micro Channel Arch
#define MULTIPROC_BUSTYPE_MPI			"MPI   " // MPI
#define MULTIPROC_BUSTYPE_MPSA			"MPSA  " // MPSA
#define MULTIPROC_BUSTYPE_NUBUS			"NUBUS " // Apple Macintosh NuBus
#define MULTIPROC_BUSTYPE_PCI			"PCI   " // PCI
#define MULTIPROC_BUSTYPE_PCMCIA		"PCMCIA" // PCMCIA
#define MULTIPROC_BUSTYPE_TCDEC			"TC DEC" // TurboChannel
#define MULTIPROC_BUSTYPE_VL			"VL    " // VESA Local Bus
#define MULTIPROC_BUSTYPE_VME			"VME   " // VMEbus
#define MULTIPROC_BUSTYPE_XPRESS		"XPRESS" // Express System Bus

// Interrupt types
#define MULTIPROC_INTTYPE_INT			0
#define MULTIPROC_INTTYPE_NMI			1
#define MULTIPROC_INTTYPE_SMI			2
#define MULTIPROC_INTTYPE_EXTINT		3

// Interrupt polarity
#define MULTIPROC_INTPOLARITY_CONFORMS	0x00
#define MULTIPROC_INTPOLARITY_ACTIVEHI	0x01
#define MULTIPROC_INTPOLARITY_RESERVED	0x02
#define MULTIPROC_INTPOLARITY_ACTIVELO	0x03
#define MULTIPROC_INTPOLARITY_MASK		0x03

// Interrupt trigger mode
#define MULTIPROC_INTTRIGGER_CONFORMS	0x00
#define MULTIPROC_INTTRIGGER_EDGE		0x04
#define MULTIPROC_INTTRIGGER_RESERVED	0x08
#define MULTIPROC_INTTRIGGER_LEVEL		0x0C
#define MULTIPROC_INTTRIGGER_MASK		0x0C

// The multiprocessor spec floating pointer structure
typedef struct {
	char signature[4];
	unsigned tablePhysical;
	unsigned char length;
	unsigned char version;
	unsigned char checksum;
	unsigned char features[5];

} __attribute__((packed)) multiProcFloatingPointer;

// The multiprocessor configuration table header
typedef struct {
	char signature[4];
	unsigned short length;
	unsigned char version;
	unsigned char checksum;
	char oemId[8];
	char productId[12];
	unsigned oemTablePhysical;
	unsigned short oemTableLength;
	unsigned short numEntries;
	unsigned localApicPhysical;
	unsigned short extLength;
	unsigned char extChecksum;
	unsigned char res;
	unsigned char entries[];

} __attribute__((packed)) multiProcConfigHeader;

// Multiprocessor processor entry
typedef struct {
	unsigned char entryType;
	unsigned char localApicId;
	unsigned char localApicVersion;
	unsigned char cpuFlags;
	unsigned cpuSignature;
	unsigned featureFlags;
	unsigned res[2];

} __attribute__((packed)) multiProcCpuEntry;

// Multiprocessor bus entry
typedef struct {
	unsigned char entryType;
	unsigned char busId;
	char type[6];

} __attribute__((packed)) multiProcBusEntry;

// Multiprocessor I/O APIC entry
typedef struct {
	unsigned char entryType;
	unsigned char apicId;
	unsigned char apicVersion;
	unsigned char apicFlags;
	unsigned apicPhysical;

} __attribute__((packed)) multiProcIoApicEntry;

// Multiprocessor I/O interrupt assignment entry
typedef struct {
	unsigned char entryType;
	unsigned char intType;
	unsigned short intFlags;
	unsigned char busId;
	unsigned char busIrq;
	unsigned char ioApicId;
	unsigned char ioApicIntPin;

} __attribute__((packed)) multiProcIoIntAssEntry;

// Multiprocessor local interrupt assignment entry
typedef struct {
	unsigned char entryType;
	unsigned char intType;
	unsigned short intFlags;
	unsigned char busId;
	unsigned char busIrq;
	unsigned char localApicId;
	unsigned char localApicLint;

} __attribute__((packed)) multiProcLocalIntAssEntry;

#define _MULTIPROC_H
#endif

