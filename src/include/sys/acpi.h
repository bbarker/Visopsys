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
//  acpi.h
//

// This file contains definitions and structures defined by the ACPI
// power management standard.

#if !defined(_ACPI_H)

#include <sys/types.h>

//
// ACPI version 1.0 definitions
//

// Table signatures
#define ACPI_SIG_RSDP			"RSD PTR "	// Root System Description Pointer
#define ACPI_SIG_APIC			"APIC"		// Multiple APIC Descriptor Table
#define ACPI_SIG_DSDT			"DSDT"		// Differentiated System DT
#define ACPI_SIG_FADT			"FACP"		// Fixed ACPI DT
#define ACPI_SIG_FACS			"FACS"		// Firmware ACPI DT
#define ACPI_SIG_PSDT			"PSDT"		// Persistent System DT
#define ACPI_SIG_RSDT			"RSDT"		// Root System DT
#define ACPI_SIG_SSDT			"SSDT"		// Secondary System DT
#define ACPI_SIG_SBST			"SBST"		// Smart Battery Spec Table

// FACS flags
#define ACPI_FACSFL_S4BIOS		0x00000001	// Firmware S4 sleep state support

// APIC structure types
#define ACPI_APICTYPE_LAPIC		0
#define ACPI_APICTYPE_IOAPIC		1

// Power management control block commands
#define ACPI_PMCTRL_SCI_EN		0x0001
#define ACPI_PMCTRL_BM_RLD		0x0002
#define ACPI_PMCTRL_GBL_RLS		0x0004
#define ACPI_PMCTRL_SLP_TYPX	0x1C00
#define ACPI_PMCTRL_SLP_EN		0x2000

//
// ACPI version 2.0 definitions
//

// Table signatures
#define ACPI_SIG_ECDT			"ECDT"		// Embedded Boot Resources
#define ACPI_SIG_OEMX			"OEM"		// OEM-specific Information tables
#define ACPI_SIG_XSDT			"XSDT"		// Extended System Descriptor Table
#define ACPI_SIG_BOOT			"BOOT"		// Simple Boot Flag table
#define ACPI_SIG_CPEP			"CPEP"		// Corrected Platform Error Polling
#define ACPI_SIG_DBGP			"DBGP"		// Debug Port table
#define ACPI_SIG_ETDT			"ETDT"		// Event Timer DT
#define ACPI_SIG_HPET			"HPET"		// HPET table
#define ACPI_SIG_SLIT			"SLIT"		// System Locality Info table
#define ACPI_SIG_SPCR			"SPCR"		// Serial Port Console Redirection
#define ACPI_SIG_SRAT			"SRAT"		// Static Resource Affinity table
#define ACPI_SIG_SPMI			"SPMI"		// SPMI table
#define ACPI_SIG_TCPA			"TCPA"		// "Trusted Computing" capabilities

// APIC structure types
#define ACPI_APICTYPE_ISOVER	2
#define ACPI_APICTYPE_NMI		3
#define ACPI_APICTYPE_LAPIC_NMI	4
#define ACPI_APICTYPE_LAPIC_AOS	5
#define ACPI_APICTYPE_IOSAPIC	6
#define ACPI_APICTYPE_LSAPIC	7
#define ACPI_APICTYPE_PLATIS	8

//
// ACPI version 3.0 definitions
//

// Table signatures
#define ACPI_SIG_BERT			"BERT"		// Boot Error Record Table
#define ACPI_SIG_DMAR			"DMAR"		// DMA Remapping Table
#define ACPI_SIG_ERST			"ERST"		// Error Record Serialization Table
#define ACPI_SIG_HEST			"HEST"		// Hardware Error Source Table
#define ACPI_SIG_IBFT			"IBFT"		// iSCSI Boot Firmware Table
#define ACPI_SIG_MCFG			"MCFG"		// PCIe mapped config space DT
#define ACPI_SIG_UEFI			"UEFI"		// UEFI ACPI Boot Optimization Table
#define ACPI_SIG_WAET			"WAET"		// Windows ACPI Enlightenment Table
#define ACPI_SIG_WDAT			"WDAT"		// Watchdog Action Table
#define ACPI_SIG_WDRT			"WDRT"		// Watchdog Resource Table
#define ACPI_SIG_WSPT			"WSPT"		// Windows-Specific Properties Table

//
// ACPI version 4.0 definitions
//

// Table signatures
#define ACPI_SIG_EINJ			"EINJ"		// Error Injection Table
#define ACPI_SIG_MSCT			"MSCT"		// Max System Characteristics Table
#define ACPI_SIG_PSDT			"PSDT"		// Persistent System DT
#define ACPI_SIG_IVRS			"IVRS"		// I/O Virt Reporting Structure
#define ACPI_SIG_MCHI			"MCHI"		// Mgmt Ctrllr Host Interface Table

// FACS flags
#define ACPI_FACSFL_64BITWAKE	0x00000002	// 64-bit waking vector support

//
// ACPI version 2.0 structures
//

typedef struct {
	unsigned char addrSpaceId;
	unsigned char regBitWidth;
	unsigned char regBitOffset;
	unsigned char addrSize;
	uquad_t address;

} __attribute__((packed)) acpiGenAddr;

//
// ACPI version 1.0 structures
//

// Root System Description Pointer
typedef struct {
	char signature[8];
	unsigned char checksum;
	char oemId[6];
	unsigned char revision;
	unsigned rsdtAddr;

	// Fields added in ACPI 2.0
	unsigned length;
	uquad_t xsdtAddr;
	unsigned char xChecksum;
	unsigned char res[3];

} __attribute__((packed)) acpiRsdp;

// System Description Table Header
typedef struct {
	char signature[4];
	unsigned length;
	unsigned char revision;
	unsigned char checksum;
	char oemId[6];
	char oemTableId[8];
	unsigned oemRevision;
	unsigned creatorId;
	unsigned creatorRevision;

} __attribute__((packed)) acpiSysDescHeader;

// Root System Description Table
typedef struct {
	acpiSysDescHeader header;
	unsigned entry[];

} __attribute__((packed)) acpiRsdt;

typedef struct {
	unsigned char type;
	unsigned char length;

} __attribute__((packed)) acpiApicHeader;

// MADT local APIC (acpiMadt)
typedef struct {
	acpiApicHeader header;
	unsigned char procId;
	unsigned char lapicId;
	unsigned flags;

} __attribute__((packed)) acpiLocalApic;

// MADT I/O APIC (acpiMadt)
typedef struct {
	acpiApicHeader header;
	unsigned char ioApicId;
	unsigned char res;
	unsigned ioApicAddr;
	unsigned gsiBase;

} __attribute__((packed)) acpiIoApic;

// Multiple APIC Description Table
typedef struct {
	acpiSysDescHeader header;
	unsigned localApicAddr;
	unsigned flags;
	unsigned entry[];

} __attribute__((packed)) acpiMadt;

// Fixed ACPI Description Table
typedef struct {
	acpiSysDescHeader header;
	unsigned facsAddr;
	unsigned dsdtAddr;
	unsigned char intMode;
	unsigned char res1;
	unsigned short sciInt;
	unsigned sciCmdPort;
	unsigned char acpiEnable;
	unsigned char acpiDisable;
	unsigned char s4BiosReq;
	unsigned char res2;
	unsigned pm1aEventBlock;
	unsigned pm1bEventBlock;
	unsigned pm1aCtrlBlock;
	unsigned pm1bCtrlBlock;
	unsigned pm2CtrlBlock;
	unsigned pmTimerBlock;
	unsigned genEvent0Block;
	unsigned genEvent1Block;
	unsigned char pm1EventBlockLen;
	unsigned char pm1CtrlBlockLen;
	unsigned char pm2CtrlBlockLen;
	unsigned char pmTimerBlockLen;
	unsigned char genEvent0BlockLen;
	unsigned char genEvent1BlockLen;
	unsigned char genEvent1Bbase;
	unsigned char res3;
	unsigned short c2Latency;
	unsigned short c3Latency;
	unsigned short flushSize;
	unsigned short flushStride;
	unsigned char dutyOffset;
	unsigned char dutyWidth;
	unsigned char dayAlarm;
	unsigned char monthAlarm;
	unsigned char century;
	unsigned short bootArch; // added in ACPI 2.0
	unsigned char res4[1];
	unsigned flags;
	// Fields added in ACPI 2.0
	acpiGenAddr resetReg;
	unsigned char resetValue;
	unsigned char res5[3];
	uquad_t xFacsAddr;
	uquad_t xDsdtAddr;
	acpiGenAddr xPm1aEventBlock;
	acpiGenAddr xPm1bEventBlock;
	acpiGenAddr xPm1aCtrlBlock;
	acpiGenAddr xPm1bCtrlBlock;
	acpiGenAddr xPm2CtrlBlock;
	acpiGenAddr xPmTimerBlock;
	acpiGenAddr xGenEvent0Block;
	acpiGenAddr xGenEvent1Block;

} __attribute__((packed)) acpiFadt;

// Firmware ACPI Control Structure
typedef struct {
	char signature[4];
	unsigned length;
	unsigned hardwareSig;
	unsigned wakingVector;
	unsigned globalLock;
	unsigned flags;
	// Fields added in ACPI 2.0 (version field >= 1)
	uquad_t xWakingVector;
	unsigned char version;
	// Fields added in ACPI 4.0 (version field >= 2)
	unsigned char res1[3];
	unsigned ospmFlags;
	// Padding
	unsigned char res2[24];

} __attribute__((packed)) acpiFacs;

// Differentiated System Description Table
typedef struct {
	acpiSysDescHeader header;
	unsigned char data[];

} __attribute__((packed)) acpiDsdt;

//
// ACPI version 2.0 structures
//

// MADT Interrupt Source Override (acpiMadt)
typedef struct {
	acpiApicHeader header;
	unsigned char bus;
	unsigned char source;
	unsigned gsi;
	unsigned short flags;

} __attribute__((packed)) acpiIsOver;

// Extended System Description Table
typedef struct {
	acpiSysDescHeader header;
	uquad_t entry[];

} __attribute__((packed)) acpiXsdt;

// Simple Boot Flag Table
typedef struct {
	acpiSysDescHeader header;
	unsigned char cmosIndex;
	unsigned char res[3];

} __attribute__((packed)) acpiBoot;

#define _ACPI_H
#endif

