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
//  kernelSataAhciDriver.h
//

// This header file contains definitions for the kernel's AHCI SATA driver

#if !defined(_KERNELSATAAHCIDRIVER_H)

#include "kernelBus.h"
#include "kernelDisk.h"
#include "kernelSataDriver.h"

#define AHCI_VERSION_1_1	0x00010100
#define AHCI_VERSION_1_2	0x00010200
#define AHCI_MAX_PORTS		32
#define AHCI_CMDLIST_SIZE	0x400
#define AHCI_CMDLIST_ALIGN	AHCI_CMDLIST_SIZE
#define AHCI_RECVFIS_SIZE	0x100
#define AHCI_RECVFIS_ALIGN	AHCI_RECVFIS_SIZE
#define AHCI_PRD_MAXDATA	0x00400000
#define AHCI_CMDTABLE_ALIGN	0x80

// Bit definitions for HBA registers that we're interested in

// HBA capabilities (CAP)
#define AHCI_CAP_S64A		(1 << 31)
#define AHCI_CAP_SNCQ		(1 << 30)
#define AHCI_CAP_SSNTF		(1 << 29)	// AHCI 1.1
#define AHCI_CAP_SIS		(1 << 28)	// AHCI < 1.1
#define AHCI_CAP_SMPS		(1 << 28)	// AHCI 1.1
#define AHCI_CAP_SSS		(1 << 27)
#define AHCI_CAP_SALP		(1 << 26)
#define AHCI_CAP_SAL		(1 << 25)
#define AHCI_CAP_SCLO		(1 << 24)
#define AHCI_CAP_ISS		(0xF << 20)
#define AHCI_CAP_SNZO		(1 << 19)	// AHCI < 1.2
#define AHCI_CAP_SAM		(1 << 18)
#define AHCI_CAP_SPM		(1 << 17)
#define AHCI_CAP_FBSS		(1 << 16)	// AHCI 1.1
#define AHCI_CAP_PMD		(1 << 15)
#define AHCI_CAP_SSC		(1 << 14)
#define AHCI_CAP_PSC		(1 << 13)
#define AHCI_CAP_NCS		(0x1F << 8)
#define AHCI_CAP_CCCS		(1 << 7)	// AHCI 1.1
#define AHCI_CAP_EMS		(1 << 6)	// AHCI 1.1
#define AHCI_CAP_SXS		(1 << 5)	// AHCI 1.1
#define AHCI_CAP_NP			(0x1F << 0)

// Global HBA control (GHC)
#define AHCI_GHC_AE			(1 << 31)
#define AHCI_GHC_IE			(1 << 1)
#define AHCI_GHC_HR			(1 << 0)

// BIOS/OS handoff control and status (AHCI 1.2)
#define AHCI_BOHC_BB		(1 << 4)
#define AHCI_BOHC_OOC		(1 << 3)
#define AHCI_BOHC_SOOE		(1 << 2)
#define AHCI_BOHC_OOS		(1 << 1)
#define AHCI_BOHC_BOS		(1 << 0)

// Bit definitions for port registers that we're interested in

// Port X interrupt status (IS)
#define AHCI_PXIS_CPDS		(1 << 31)
#define AHCI_PXIS_TFES		(1 << 30)
#define AHCI_PXIS_HBFS		(1 << 29)
#define AHCI_PXIS_HBDS		(1 << 28)
#define AHCI_PXIS_IFS		(1 << 27)
#define AHCI_PXIS_INFS		(1 << 26)
#define AHCI_PXIS_OFS		(1 << 24)
#define AHCI_PXIS_IPMS		(1 << 23)
#define AHCI_PXIS_PRCS		(1 << 22)
#define AHCI_PXIS_DIS		(1 << 7)
#define AHCI_PXIS_PCS		(1 << 6)
#define AHCI_PXIS_DPS		(1 << 5)
#define AHCI_PXIS_UFS		(1 << 4)
#define AHCI_PXIS_SDBS		(1 << 3)
#define AHCI_PXIS_DSS		(1 << 2)
#define AHCI_PXIS_PSS		(1 << 1)
#define AHCI_PXIS_DHRS		(1 << 0)
#define AHCI_PXIS_RWCBITS	(AHCI_PXIS_CPDS | AHCI_PXIS_TFES | \
	AHCI_PXIS_HBFS | AHCI_PXIS_HBDS | AHCI_PXIS_IFS | \
	AHCI_PXIS_INFS | AHCI_PXIS_OFS | AHCI_PXIS_IPMS | \
	AHCI_PXIS_DIS | AHCI_PXIS_DPS | AHCI_PXIS_SDBS | \
	AHCI_PXIS_DSS | AHCI_PXIS_PSS | AHCI_PXIS_DHRS)
#define AHCI_PXIS_ERROR		(AHCI_PXIS_TFES | AHCI_PXIS_HBFS | \
	AHCI_PXIS_HBDS | AHCI_PXIS_IFS | AHCI_PXIS_INFS | AHCI_PXIS_OFS | \
	AHCI_PXIS_IPMS | AHCI_PXIS_UFS)
#define AHCI_PXIS_FIS		(AHCI_PXIS_UFS | AHCI_PXIS_SDBS | AHCI_PXIS_DSS | \
	AHCI_PXIS_PSS | AHCI_PXIS_DHRS)

// Port X interrupt enable (IE)
#define AHCI_PXIE_CPDE		(1 << 31)
#define AHCI_PXIE_TFEE		(1 << 30)
#define AHCI_PXIE_HBFE		(1 << 29)
#define AHCI_PXIE_HBDE		(1 << 28)
#define AHCI_PXIE_IFE		(1 << 27)
#define AHCI_PXIE_INFE		(1 << 26)
#define AHCI_PXIE_OFE		(1 << 24)
#define AHCI_PXIE_IPME		(1 << 23)
#define AHCI_PXIE_PRCE		(1 << 22)
#define AHCI_PXIE_DIE		(1 << 7)
#define AHCI_PXIE_PCE		(1 << 6)
#define AHCI_PXIE_DPE		(1 << 5)
#define AHCI_PXIE_UFE		(1 << 4)
#define AHCI_PXIE_SDBE		(1 << 3)
#define AHCI_PXIE_DSE		(1 << 2)
#define AHCI_PXIE_PSE		(1 << 1)
#define AHCI_PXIE_DHRE		(1 << 0)
#define AHCI_PXIE_ALL		(AHCI_PXIE_CPDE | AHCI_PXIE_TFEE | \
	AHCI_PXIE_HBFE | AHCI_PXIE_HBDE | AHCI_PXIE_IFE | \
	AHCI_PXIE_INFE | AHCI_PXIE_OFE | AHCI_PXIE_IPME | \
	AHCI_PXIE_PRCE | AHCI_PXIE_DIE | AHCI_PXIE_PCE | \
	AHCI_PXIE_DPE |  AHCI_PXIE_UFE | AHCI_PXIE_SDBE | \
	AHCI_PXIE_DSE | AHCI_PXIE_PSE | AHCI_PXIE_DHRE)

// Port X command port (CMD)
#define AHCI_PXCMD_ICC		(0xF << 28)
#define AHCI_PXCMD_ASP		(1 << 27)
#define AHCI_PXCMD_ALPE		(1 << 26)
#define AHCI_PXCMD_DLAE		(1 << 25)
#define AHCI_PXCMD_ATAPI	(1 << 24)
#define AHCI_PXCMD_CPD		(1 << 20)
#define AHCI_PXCMD_ISP		(1 << 19)
#define AHCI_PXCMD_HPCP		(1 << 18)
#define AHCI_PXCMD_PMA		(1 << 17)
#define AHCI_PXCMD_CPS		(1 << 16)
#define AHCI_PXCMD_CR		(1 << 15)
#define AHCI_PXCMD_FR		(1 << 14)
#define AHCI_PXCMD_ISS		(1 << 13)
#define AHCI_PXCMD_CCS		(0x1F << 8)
#define AHCI_PXCMD_FRE		(1 << 4)
#define AHCI_PXCMD_CLO		(1 << 3)
#define AHCI_PXCMD_POD		(1 << 2)
#define AHCI_PXCMD_SUD		(1 << 1)
#define AHCI_PXCMD_ST		(1 << 0)

// Port X task file data (TFD)
#define AHCI_PXTFD_ERR		(0xFF << 8)
#define AHCI_PXTFD_STS_BSY	(1 << 7)
#define AHCI_PXTFD_STS_DRQ	(1 << 3)
#define AHCI_PXTFD_STS_ERR	(1 << 0)

// Port X SATA status (SSTS)
#define AHCI_PXSSTS_IPM		(0xF << 8)
#define AHCI_PXSSTS_SPD		(0xF << 4)
#define AHCI_PXSSTS_DET		(0xF << 0)

// Port X SATA control (SCTL)
#define AHCI_PXSCTL_IPM		(0xF << 8)
#define AHCI_PXSCTL_SPD		(0xF << 4)
#define AHCI_PXSCTL_DET		(0xF << 0)

// Port X error (SERR)
#define AHCI_PXSERR_DIAG_X	(1 << 26)
#define AHCI_PXSERR_DIAG_F	(1 << 25)
#define AHCI_PXSERR_DIAG_T	(1 << 24)
#define AHCI_PXSERR_DIAG_S	(1 << 23)
#define AHCI_PXSERR_DIAG_H	(1 << 22)
#define AHCI_PXSERR_DIAG_C	(1 << 21)
#define AHCI_PXSERR_DIAG_D	(1 << 20)
#define AHCI_PXSERR_DIAG_B	(1 << 19)
#define AHCI_PXSERR_DIAG_W	(1 << 18)
#define AHCI_PXSERR_DIAG_I	(1 << 17)
#define AHCI_PXSERR_DIAG_N	(1 << 16)
#define AHCI_PXSERR_ALLDIAG	(AHCI_PXSERR_DIAG_X | AHCI_PXSERR_DIAG_F | \
	AHCI_PXSERR_DIAG_T | AHCI_PXSERR_DIAG_S | AHCI_PXSERR_DIAG_H | \
	AHCI_PXSERR_DIAG_C | AHCI_PXSERR_DIAG_D | AHCI_PXSERR_DIAG_B | \
	AHCI_PXSERR_DIAG_W | AHCI_PXSERR_DIAG_I | AHCI_PXSERR_DIAG_N)
#define AHCI_PXSERR_ERR_E	(1 << 11)
#define AHCI_PXSERR_ERR_P	(1 << 10)
#define AHCI_PXSERR_ERR_C	(1 << 9)
#define AHCI_PXSERR_ERR_T	(1 << 8)
#define AHCI_PXSERR_ERR_M	(1 << 1)
#define AHCI_PXSERR_ERR_I	(1 << 0)
#define AHCI_PXSERR_ALLERR	\
	(AHCI_PXSERR_ERR_E | AHCI_PXSERR_ERR_P | AHCI_PXSERR_ERR_C | \
	AHCI_PXSERR_ERR_T | AHCI_PXSERR_ERR_M | AHCI_PXSERR_ERR_I)
#define AHCI_PXSERR_ALL		(AHCI_PXSERR_ALLDIAG | AHCI_PXSERR_ALLERR)

typedef volatile struct {
	// Per-port registers
	unsigned CLB;				// 0x00
	unsigned CLBU;				// 0x04
	unsigned FB;				// 0x08
	unsigned FBU;				// 0x0C
	unsigned IS;				// 0x10
	unsigned IE;				// 0x14
	unsigned CMD;				// 0x18
	unsigned res1;				// 0x1C
	unsigned TFD;				// 0x20
	unsigned SIG;				// 0x24
	unsigned SSTS;				// 0x28
	unsigned SCTL;				// 0x2C
	unsigned SERR;				// 0x30
	unsigned SACT;				// 0x34
	unsigned CI;				// 0x38
	unsigned SNTF;				// 0x3C AHCI 1.1
	unsigned res2;				// 0x40
	unsigned res3[11];			// 0x44
	unsigned VS[4];				// 0x70

} __attribute__((packed)) ahciPortRegs;

typedef volatile struct {
	// General host controller registers
	unsigned CAP;				// 0x00
	unsigned GHC;				// 0x04
	unsigned IS;				// 0x08
	unsigned PI;				// 0x0C
	unsigned VS;				// 0x10
	unsigned CCC_CTL;			// 0x14 AHCI 1.1
	unsigned CCC_PORTS;			// 0x18 AHCI 1.1
	unsigned EM_LOC;			// 0x1C AHCI 1.1
	unsigned EM_CTL;			// 0x20 AHCI 1.1
	unsigned CAP2;				// 0x24 AHCI 1.2
	unsigned BOHC;				// 0x28 AHCI 1.2

	// Stuff we don't use
	unsigned res[29];
	unsigned vendSpec[24];

	// Port registers
	ahciPortRegs port[AHCI_MAX_PORTS];

} __attribute__((packed)) ahciRegs;

typedef volatile struct {
	unsigned physAddr;
	unsigned physAddrHi;
	unsigned res;
	unsigned intrCount;

} __attribute__((packed)) ahciPrd;

typedef volatile struct {
	unsigned char commandFis[64];
	unsigned char atapiCommand[32];
	unsigned char res[32];
	ahciPrd prd[];

} __attribute__((packed)) ahciCommandTable;

typedef volatile struct {
	unsigned short fisLen:5;
	unsigned short atapi:1;
	unsigned short write:1;
	unsigned short prefetchable:1;
	unsigned short reset:1;
	unsigned short bist:1;
	unsigned short clearBusy:1;
	unsigned short res1:1;
	unsigned short portMulti:4;
	unsigned short prdDescTableEnts;
	unsigned prdByteCount;
	unsigned cmdTablePhysAddr;
	unsigned cmdTablePhysAddrHi;
	unsigned char res2[16];

} __attribute__((packed)) ahciCommandHeader;

typedef volatile struct {
	ahciCommandHeader command[32];

} __attribute__((packed)) ahciCommandList;

typedef volatile struct {
	sataFisDmaSetup dmaSetup;
	unsigned char res1[4];
	sataFisPioSetup pioSetup;
	unsigned char res2[12];
	sataFisRegD2H regD2h;
	unsigned char res3[4];
	sataFisDevBits devBits;
	unsigned char unknownFis[64];
	unsigned char res4[96];

} __attribute__((packed)) ahciReceivedFises;

typedef volatile struct {
	ahciCommandList *commandList;
	ahciReceivedFises *recvFis;
	int waitProcess;
	unsigned interruptStatus;
	lock lock;

} ahciPort;

typedef struct {
	int portNum;
	kernelPhysicalDisk physical;
	int featureFlags;
	char *dmaMode;

} ahciDisk;

typedef volatile struct {
	int num;
	kernelBusTarget busTarget;
	int interrupt;
	ahciRegs *regs;
	ahciPort port[AHCI_MAX_PORTS];
	unsigned portInterrupts;
	ahciDisk *disk[AHCI_MAX_PORTS];

} ahciController;

#define _KERNELSATAAHCIDRIVER_H
#endif

