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
//  kernelUsbUhciDriver.h
//

#if !defined(_KERNELUSBUHCIDRIVER_H)

#include "kernelLinkedList.h"
#include "kernelMemory.h"

// USB UHCI Host controller port offsets
#define UHCI_PORTOFFSET_CMD			0x00
#define UHCI_PORTOFFSET_STAT		0x02
#define UHCI_PORTOFFSET_INTR		0x04
#define UHCI_PORTOFFSET_FRNUM		0x06
#define UHCI_PORTOFFSET_FLBASE		0x08
#define UHCI_PORTOFFSET_SOF			0x0C
#define UHCI_PORTOFFSET_PORTSC1		0x10
#define UHCI_PORTOFFSET_PORTSC2		0x12

// Bitfields for the USB UHCI command register
#define UHCI_CMD_MAXP				0x80
#define UHCI_CMD_CF					0x40
#define UHCI_CMD_SWDBG				0x20
#define UHCI_CMD_FGR				0x10
#define UHCI_CMD_EGSM				0x08
#define UHCI_CMD_GRESET				0x04
#define UHCI_CMD_HCRESET			0x02
#define UHCI_CMD_RUNSTOP			0x01

// Bitfields for the USB UHCI status register
#define UHCI_STAT_HCHALTED			0x20
#define UHCI_STAT_HCPERROR			0x10
#define UHCI_STAT_HSERROR			0x08
#define UHCI_STAT_RESDET			0x04
#define UHCI_STAT_ERRINT			0x02
#define UHCI_STAT_USBINT			0x01

// Bitfields for the USB UHCI interrupt enable register
#define UHCI_INTR_SPD				0x08
#define UHCI_INTR_IOC				0x04
#define UHCI_INTR_RESUME			0x02
#define UHCI_INTR_TIMEOUTCRC		0x01

// Bitfields for the 2 USB UHCI port registers
#define UHCI_PORT_SUSPEND			0x1000
#define UHCI_PORT_RESET				0x0200
#define UHCI_PORT_LSDA				0x0100
#define UHCI_PORT_RESDET			0x0040
#define UHCI_PORT_LINESTAT			0x0030
#define UHCI_PORT_ENABCHG			0x0008
#define UHCI_PORT_ENABLED			0x0004
#define UHCI_PORT_CONNCHG			0x0002
#define UHCI_PORT_CONNSTAT			0x0001
#define UHCI_PORT_RWC_BITS			(UHCI_PORT_ENABCHG | UHCI_PORT_CONNCHG)

// Bitfields for link pointers
#define UHCI_LINKPTR_DEPTHFIRST		0x00000004
#define UHCI_LINKPTR_QHEAD			0x00000002
#define UHCI_LINKPTR_TERM			0x00000001

// Bitfields for transfer descriptors
#define UHCI_TDCONTSTAT_SPD			0x20000000
#define UHCI_TDCONTSTAT_ERRCNT		0x18000000
#define UHCI_TDCONTSTAT_LSPEED		0x04000000
#define UHCI_TDCONTSTAT_ISOC		0x02000000
#define UHCI_TDCONTSTAT_IOC			0x01000000
#define UHCI_TDCONTSTAT_STATUS		0x00FF0000
#define UHCI_TDCONTSTAT_ACTIVE		0x00800000
#define UHCI_TDCONTSTAT_ERROR		0x007E0000
#define UHCI_TDCONTSTAT_ESTALL		0x00400000
#define UHCI_TDCONTSTAT_EDBUFF		0x00200000
#define UHCI_TDCONTSTAT_EBABBLE		0x00100000
#define UHCI_TDCONTSTAT_ENAK		0x00080000
#define UHCI_TDCONTSTAT_ECRCTO		0x00040000
#define UHCI_TDCONTSTAT_EBSTUFF		0x00020000
#define UHCI_TDCONTSTAT_ACTLEN		0x000007FF
#define UHCI_TDTOKEN_MAXLEN			0xFFE00000
#define UHCI_TDTOKEN_DATATOGGLE		0x00080000
#define UHCI_TDTOKEN_ENDPOINT		0x00078000
#define UHCI_TDTOKEN_ADDRESS		0x00007F00
#define UHCI_TDTOKEN_PID			0x000000FF
#define UHCI_TD_NULLDATA			0x000007FF

// For the queue heads array
#define UHCI_QH_INT128				0
#define UHCI_QH_INT64				1
#define UHCI_QH_INT32				2
#define UHCI_QH_INT16				3
#define UHCI_QH_INT8				4
#define UHCI_QH_INT4				5
#define UHCI_QH_INT2				6
#define UHCI_QH_INT1				7
#define UHCI_QH_CONTROL				8
#define UHCI_QH_BULK				9
#define UHCI_QH_TERM				10

// Data structure memory sizes.  UHCI_QUEUEHEADS_MEMSIZE is below.
#define UHCI_NUM_FRAMES				1024
#define UHCI_FRAMELIST_MEMSIZE		(UHCI_NUM_FRAMES * sizeof(unsigned))
#define UHCI_NUM_QUEUEHEADS			11

typedef volatile struct _uhciTransDesc {
	unsigned linkPointer;
	unsigned contStatus;
	unsigned tdToken;
	unsigned buffer;
	// The last 4 dwords are reserved for our use, also helps ensure 16-byte
	// alignment.
	void *buffVirtual;
	unsigned buffSize;
	volatile struct _uhciTransDesc *prev;
	volatile struct _uhciTransDesc *next;

} __attribute__((packed)) __attribute__((aligned(16))) uhciTransDesc;

typedef volatile struct {
	unsigned linkPointer;
	unsigned element;
	// Our use, also helps ensure 16-byte alignment.
	unsigned saveElement;
	uhciTransDesc *transDescs;

} __attribute__((packed)) __attribute__((aligned(16))) uhciQueueHead;

// One memory page worth of queue heads
#define UHCI_QUEUEHEADS_MEMSIZE (UHCI_NUM_QUEUEHEADS * sizeof(uhciQueueHead))

typedef struct {
	usbDevice *usbDev;
	int interface;
	uhciQueueHead *queueHead;
	uhciTransDesc *transDesc;
	unsigned char endpoint;
	int interval;
	unsigned maxLen;
	void (*callback)(usbDevice *, int, void *, unsigned);

} uhciIntrReg;

typedef struct {
	void *ioAddress;
	kernelIoMemory frameList;
	uhciQueueHead *queueHeads[UHCI_NUM_QUEUEHEADS];
	uhciTransDesc *termTransDesc;
	kernelLinkedList intrRegs;

} uhciData;

#define _KERNELUSBUHCIDRIVER_H
#endif

