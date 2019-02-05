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
//  kernelUsbOhciDriver.h
//

#if !defined(_KERNELUSBOHCIDRIVER_H)

#include "kernelLinkedList.h"
#include "kernelUsbDriver.h"

#define OHCI_PCI_PROGIF				0x10
#define OHCI_NUM_FRAMES				32
#define OHCI_MAX_TD_BUFFERSIZE		8192
#define OHCI_DEFAULT_MAXPKTSZ		0x2778
#define OHCI_DEFAULT_FRAMEINT		0x2EDF

// Bitfields for the hcControl regiser
#define OHCI_HCCTRL_RWE				0x00000400
#define OHCI_HCCTRL_RWC				0x00000200
#define OHCI_HCCTRL_IR				0x00000100
#define OHCI_HCCTRL_HCFS			0x000000C0
#define OHCI_HCCTRL_HCFS_RESET		0x00000000
#define OHCI_HCCTRL_HCFS_RESUME		0x00000040
#define OHCI_HCCTRL_HCFS_OPERATE	0x00000080
#define OHCI_HCCTRL_HCFS_SUSPEND	0x000000C0
#define OHCI_HCCTRL_BLE				0x00000020
#define OHCI_HCCTRL_CLE				0x00000010
#define OHCI_HCCTRL_IE				0x00000008
#define OHCI_HCCTRL_PLE				0x00000004
#define OHCI_HCCTRL_CBSR			0x00000003

// Bitfields for the hcCommandStatus register
#define OHCI_HCCMDSTAT_SOC			0x00030000
#define OHCI_HCCMDSTAT_OCR			0x00000008
#define OHCI_HCCMDSTAT_BLF			0x00000004
#define OHCI_HCCMDSTAT_CLF			0x00000002
#define OHCI_HCCMDSTAT_HCR			0x00000001

// Bitfields for the hcInterruptEnable, hcInterruptDisable, and
// hcInterruptStatus registers
#define OHCI_HCINT_MIE				0x80000000
#define OHCI_HCINT_OC				0x40000000
#define OHCI_HCINT_RHSC				0x00000040
#define OHCI_HCINT_FNO				0x00000020
#define OHCI_HCINT_UE				0x00000010
#define OHCI_HCINT_RD				0x00000008
#define OHCI_HCINT_SF				0x00000004
#define OHCI_HCINT_WDH				0x00000002
#define OHCI_HCINT_SO				0x00000001

// Bitfields for the hcFmInterval register
#define OHCI_HCFMINT_FIT			0x80000000
#define OHCI_HCFMINT_FSMPS			0x7FFF0000
#define OHCI_HCFMINT_FI				0x00003FFF

// Bitfields for the hcRhDescriptorA register
#define OHCI_ROOTDESCA_POTPGT		0xFF000000
#define OHCI_ROOTDESCA_NOCP			0x00001000
#define OHCI_ROOTDESCA_OCPM			0x00000800
#define OHCI_ROOTDESCA_DT			0x00000400
#define OHCI_ROOTDESCA_NPS			0x00000200
#define OHCI_ROOTDESCA_PSM			0x00000100
#define OHCI_ROOTDESCA_NDP			0x000000FF

// Bitfields for the hcRhDescriptorB register
#define OHCI_ROOTDESCB_PPCM			0xFFFF0000
#define OHCI_ROOTDESCB_DR			0x0000FFFF

// Bitfields for the hcRhStatus register
#define OHCI_RHSTAT_CRWE			0x80000000
#define OHCI_RHSTAT_OCIC			0x00020000
#define OHCI_RHSTAT_LPSC			0x00010000
#define OHCI_RHSTAT_DRWE			0x00008000
#define OHCI_RHSTAT_OCI				0x00000002
#define OHCI_RHSTAT_LPS				0x00000001

// Bitfields for the hcRhPortStatus registers
#define OHCI_PORTSTAT_PRSC			0x00100000
#define OHCI_PORTSTAT_OCIC			0x00080000
#define OHCI_PORTSTAT_PSSC			0x00040000
#define OHCI_PORTSTAT_PESC			0x00020000
#define OHCI_PORTSTAT_CSC			0x00010000
#define OHCI_PORTSTAT_LSDA			0x00000200
#define OHCI_PORTSTAT_PPS			0x00000100
#define OHCI_PORTSTAT_PRS			0x00000010
#define OHCI_PORTSTAT_POCI			0x00000008
#define OHCI_PORTSTAT_PSS			0x00000004
#define OHCI_PORTSTAT_PES			0x00000002
#define OHCI_PORTSTAT_CCS			0x00000001

// Bitfields for the transfer descriptor 'flags' field
#define OHCI_TDFLAGS_CONDCODE		0xF0000000
#define OHCI_TDFLAGS_ERRCOUNT		0x0C000000
#define OHCI_TDFLAGS_DATATOGGLE		0x03000000
#define OHCI_TDFLAGS_DELAYINT		0x00E00000
#define OHCI_TDFLAGS_DIRPID			0x00180000
#define OHCI_TDFLAGS_ROUNDING		0x00040000

// Bitfields for the endpoint descriptor 'flags' field
#define OHCI_EDFLAGS_MAXPACKET		0x07FF0000
#define OHCI_EDFLAGS_FORMAT			0x00008000
#define OHCI_EDFLAGS_SKIP			0x00004000
#define OHCI_EDFLAGS_SPEED			0x00002000
#define OHCI_EDFLAGS_DIRECTION		0x00001800
#define OHCI_EDFLAGS_ENDPOINT		0x00000780
#define OHCI_EDFLAGS_ADDRESS		0x0000007F

// Bitfields for the endpoint descriptor TD queue head field
#define OHCI_EDHEADPTR_TOGCARRY		0x00000002
#define OHCI_EDHEADPTR_HALTED		0x00000001

// For the periodic schedule
#define OHCI_ED_INT32				0
#define OHCI_ED_INT16				1
#define OHCI_ED_INT8				2
#define OHCI_ED_INT4				3
#define OHCI_ED_INT2				4
#define OHCI_ED_INT1				5
#define OHCI_ED_CONTROL				6
#define OHCI_ED_BULK				7
#define OHCI_NUM_QUEUEDESCS			8

// Transfer descriptor (TD)
typedef volatile struct _ohciTransDesc {
	// Controller use (defined by the spec)
	unsigned flags;
	unsigned currBuffPtr;
	unsigned nextPhysical;
	unsigned bufferEnd;
	// Our use (defined by us)
	unsigned physical;
	void *buffer;
	unsigned buffSize;
	volatile struct _ohciTransDesc *next;

} __attribute__((packed)) __attribute__((aligned(16))) ohciTransDesc;

// 'Endpoint descriptor' (ED - really like an EHCI Queue Head)
typedef volatile struct _ohciEndpDesc {
	// Controller use (defined by the spec)
	unsigned flags;
	unsigned tailPhysical;
	unsigned headPhysical;
	unsigned nextPhysical;
	// Our use (defined by us)
	usbDevice *usbDev;
	int endpoint;
	ohciTransDesc *head;
	volatile struct _ohciEndpDesc *next;

} __attribute__((packed)) __attribute__((aligned(16))) ohciEndpDesc;

// The Host Controller Communications Area (HCCA)
typedef volatile struct {
	unsigned intTable[OHCI_NUM_FRAMES]; // 0x00-0x7F
	unsigned short frameNum;		// 0x80-0x81
	unsigned short pad1;			// 0x82-0x83
	unsigned doneHead;				// 0x84-0x87
	unsigned char reserved[116];	// 0x88-0xFB
	unsigned pad2;					// 0xFC-0xFF

} __attribute__((packed)) ohciHcca;

// Operational registers
typedef volatile struct {
	unsigned hcRevision;			// 0x00-0x03
	unsigned hcControl;				// 0x04-0x07
	unsigned hcCommandStatus;		// 0x08-0x0B
	unsigned hcInterruptStatus;		// 0x0C-0x0F
	unsigned hcInterruptEnable;		// 0x10-0x13
	unsigned hcInterruptDisable;	// 0x14-0x17
	unsigned hcHcca;				// 0x18-0x1B
	unsigned hcPeriodicCurrentEd;	// 0x1C-0x1F
	unsigned hcControlHeadEd;		// 0x20-0x23
	unsigned hcControlCurrentEd;	// 0x24-0x27
	unsigned hcBulkHeadEd;			// 0x28-0x2B
	unsigned hcBulkCurrentEd;		// 0x2C-0x2F
	unsigned hcDoneHead;			// 0x30-0x33
	unsigned hcFmInterval;			// 0x34-0x37
	unsigned hcFmRemaining;			// 0x38-0x3B
	unsigned hcFmNumber;			// 0x3C-0x3F
	unsigned hcPeriodicStart;		// 0x40-0x43
	unsigned hcLsThreshold;			// 0x44-0x47
	unsigned hcRhDescriptorA;		// 0x48-0x4B
	unsigned hcRhDescriptorB;		// 0x4C-0x4F
	unsigned hcRhStatus;			// 0x50-0x53
	unsigned hcRhPortStatus[];		// 0x54-

} __attribute__((packed)) ohciOpRegs;

typedef struct {
	usbDevice *usbDev;
	int interface;
	int endpoint;
	int interval;
	unsigned maxLen;
	void (*callback)(usbDevice *, int, void *, unsigned);
	ohciEndpDesc *endpDesc;
	ohciTransDesc *transDesc;
	unsigned bufferPhysical;

} ohciIntrReg;

typedef struct {
	ohciOpRegs *opRegs;
	int numPorts;
	kernelLinkedList usedEndpDescs;
	kernelLinkedList freeEndpDescs;
	ohciEndpDesc *queueEndpDescs[OHCI_NUM_QUEUEDESCS];
	ohciHcca *hcca;
	kernelLinkedList intrRegs;

} ohciData;

#define _KERNELUSBOHCIDRIVER_H
#endif

