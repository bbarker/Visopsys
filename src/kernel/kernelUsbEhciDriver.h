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
//  kernelUsbEhciDriver.h
//

#if !defined(_KERNELUSBEHCIDRIVER_H)

#include "kernelLinkedList.h"
#include "kernelUsbDriver.h"
#include <sys/types.h>

// Global definitions
#define EHCI_PCI_PROGIF				0x20
#define EHCI_MAX_ROOTPORTS			15
#define EHCI_NUM_FRAMES				1024
#define EHCI_FRAMELIST_MEMSIZE		(EHCI_NUM_FRAMES * sizeof(unsigned))
#define EHCI_NUM_INTQUEUEHEADS		9
#define EHCI_MAX_QTD_BUFFERSIZE		4096
#define EHCI_MAX_QTD_BUFFERS		5
#define EHCI_MAX_QTD_DATA			(EHCI_MAX_QTD_BUFFERS * \
									EHCI_MAX_QTD_BUFFERSIZE)

// Bitfields for the EHCI HCSPARAMS register
#define EHCI_HCSP_DEBUGPORT			0x00F00000
#define EHCI_HCSP_PORTINICATORS		0x00010000
#define EHCI_HCSP_NUMCOMPANIONS		0x0000F000
#define EHCI_HCSP_PORTSPERCOMP		0x00000F00
#define EHCI_HCSP_PORTRTERULES		0x00000080
#define EHCI_HCSP_PORTPOWERCTRL		0x00000010
#define EHCI_HCSP_NUMPORTS			0x0000000F

// Bitfields for the EHCI HCCPARAMS register
#define EHCI_HCCP_EXTCAPPTR			0x0000FF00
#define EHCI_HCCP_ISOCSCHDTHRES		0x000000F0
#define EHCI_HCCP_ASYNCSCHDPARK		0x00000004
#define EHCI_HCCP_PROGFRAMELIST		0x00000002
#define EHCI_HCCP_ADDR64			0x00000001

// Extended capability codes
#define EHCI_EXTCAP_RESERVED		0
#define EHCI_EXTCAP_HANDOFFSYNC		1

// Bitfields for the legacy support registers
#define EHCI_LEGSUPCAP_OSOWNED		0x01000000	/* RW */
#define EHCI_LEGSUPCAP_BIOSOWND		0x00010000	/* RW */
#define EHCI_LEGSUPCAP_NEXTEXTCAP	0x0000FF00	/* RO */
#define EHCI_LEGSUPCAP_CAPID		0x000000FF	/* RO */
#define EHCI_LEGSUPCAP_RO			(EHCI_LEGSUPCAP_NEXTEXTCAP | \
									EHCI_LEGSUPCAP_CAPID)
#define EHCI_LEGSUPCONT_SMIBAR		0x80000000	/* RWC */
#define EHCI_LEGSUPCONT_SMICMD		0x40000000	/* RWC */
#define EHCI_LEGSUPCONT_SMIOSOWN	0x20000000	/* RWC */
#define EHCI_LEGSUPCONT_SMIASYNC	0x00200000	/* RO */
#define EHCI_LEGSUPCONT_SMIHOST		0x00100000	/* RO */
#define EHCI_LEGSUPCONT_SMIFRAME	0x00080000	/* RO */
#define EHCI_LEGSUPCONT_SMIPORT		0x00040000	/* RO */
#define EHCI_LEGSUPCONT_SMIERR		0x00020000	/* RO */
#define EHCI_LEGSUPCONT_SMIINT		0x00010000	/* RO */
#define EHCI_LEGSUPCONT_SMIRWC		(EHCI_LEGSUPCONT_SMIBAR | \
									EHCI_LEGSUPCONT_SMICMD | \
									EHCI_LEGSUPCONT_SMIOSOWN)
#define EHCI_LEGSUPCONT_SMIRO		(EHCI_LEGSUPCONT_SMIASYNC | \
									EHCI_LEGSUPCONT_SMIHOST | \
									EHCI_LEGSUPCONT_SMIFRAME | \
									EHCI_LEGSUPCONT_SMIPORT | \
									EHCI_LEGSUPCONT_SMIERR | \
									EHCI_LEGSUPCONT_SMIINT)

// Bitfields for the EHCI command register
#define EHCI_CMD_INTTHRESCTL		0x00FF0000
#define EHCI_CMD_ASYNCSPME			0x00000800
#define EHCI_CMD_ASYNCSPMC			0x00000300
#define EHCI_CMD_LIGHTHCRESET		0x00000080
#define EHCI_CMD_INTASYNCADVRST		0x00000040
#define EHCI_CMD_ASYNCSCHEDENBL		0x00000020
#define EHCI_CMD_PERSCHEDENBL		0x00000010
#define EHCI_CMD_FRAMELISTSIZE		0x0000000C
#define EHCI_CMD_HCRESET			0x00000002
#define EHCI_CMD_RUNSTOP			0x00000001

// Bitfields for the EHCI status register
#define EHCI_STAT_RES1				0xFFFF0000	// RO
#define EHCI_STAT_ASYNCSCHED		0x00008000	// RO
#define EHCI_STAT_PERIODICSCHED		0x00004000	// RO
#define EHCI_STAT_RECLAMATION		0x00002000	// RO
#define EHCI_STAT_HCHALTED			0x00001000	// RO
#define EHCI_STAT_RES2				0x00000FC0	// RO
#define EHCI_STAT_ASYNCADVANCE		0x00000020	// RWC
#define EHCI_STAT_HOSTSYSERROR		0x00000010	// RWC
#define EHCI_STAT_FRLISTROLLOVR		0x00000008	// RWC
#define EHCI_STAT_PORTCHANGE		0x00000004	// RWC
#define EHCI_STAT_USBERRORINT		0x00000002	// RWC
#define EHCI_STAT_USBINTERRUPT		0x00000001	// RWC
#define EHCI_STAT_ROMASK			(EHCI_STAT_RES1 | \
									EHCI_STAT_ASYNCSCHED | \
									EHCI_STAT_PERIODICSCHED | \
									EHCI_STAT_RECLAMATION | \
									EHCI_STAT_HCHALTED | \
									EHCI_STAT_RES2)
#define EHCI_STAT_RWCMASK			(EHCI_STAT_ASYNCADVANCE | \
									EHCI_STAT_HOSTSYSERROR | \
									EHCI_STAT_FRLISTROLLOVR | \
									EHCI_STAT_PORTCHANGE | \
									EHCI_STAT_USBERRORINT | \
									EHCI_STAT_USBINTERRUPT)

// Bitfields for the EHCI interrupt register
#define EHCI_INTR_ASYNCADVANCE		0x00000020
#define EHCI_INTR_HOSTSYSERROR		0x00000010
#define EHCI_INTR_FRLISTROLLOVR		0x00000008
#define EHCI_INTR_PORTCHANGE		0x00000004
#define EHCI_INTR_USBERRORINT		0x00000002
#define EHCI_INTR_USBINTERRUPT		0x00000001

// Bitfields for the EHCI port status/control registers
#define EHCI_PORTSC_RES1			0xFF800000	// RO
#define EHCI_PORTSC_WKOCE			0x00400000	// RW
#define EHCI_PORTSC_WKDSCNNTE		0x00200000	// RW
#define EHCI_PORTSC_WKCNNTE			0x00100000	// RW
#define EHCI_PORTSC_PORTTSTCTRL		0x000F0000	// RW
#define EHCI_PORTSC_PORTINDCTRL		0x0000C000	// RW
#define EHCI_PORTSC_PORTOWNER		0x00002000	// RW
#define EHCI_PORTSC_PORTPOWER		0x00001000	// RW
#define EHCI_PORTSC_LINESTATUS		0x00000C00	// RO
#define EHCI_PORTSC_LINESTAT_LS		0x00000400	// RO
#define EHCI_PORTSC_RES2			0x00000200	// RO
#define EHCI_PORTSC_PORTRESET		0x00000100	// RW
#define EHCI_PORTSC_PORTSUSPEND		0x00000080	// RW
#define EHCI_PORTSC_FRCEPORTRES		0x00000040	// RW
#define EHCI_PORTSC_OVRCURRCHG		0x00000020	// RWC
#define EHCI_PORTSC_OVRCURRACTV		0x00000010	// RO
#define EHCI_PORTSC_PORTENBLCHG		0x00000008	// RWC
#define EHCI_PORTSC_PORTENABLED		0x00000004	// RW
#define EHCI_PORTSC_CONNCHANGE		0x00000002	// RWC
#define EHCI_PORTSC_CONNECTED		0x00000001	// RO
#define EHCI_PORTSC_ROMASK			(EHCI_PORTSC_RES1 | \
									EHCI_PORTSC_LINESTATUS | \
									EHCI_PORTSC_RES2 | \
									EHCI_PORTSC_OVRCURRACTV | \
									EHCI_PORTSC_CONNECTED)
#define EHCI_PORTSC_RWCMASK			(EHCI_PORTSC_OVRCURRCHG | \
									EHCI_PORTSC_PORTENBLCHG | \
									EHCI_PORTSC_CONNCHANGE)

// Bitfields for EHCI link pointer fields
#define EHCI_LINKTYP_MASK			0x00000006
#define EHCI_LINKTYP_FSTN			0x00000006
#define EHCI_LINKTYP_SITD			0x00000004
#define EHCI_LINKTYP_QH				0x00000002
#define EHCI_LINKTYP_ITD			0x00000000
#define EHCI_LINK_TERM				0x00000001

// Bitfields for EHCI Queue Heads
#define EHCI_QHDW1_NAKCNTRELOAD		0xF0000000
#define EHCI_QHDW1_CTRLENDPOINT		0x08000000
#define EHCI_QHDW1_MAXPACKETLEN		0x07FF0000
#define EHCI_QHDW1_RECLLISTHEAD		0x00008000
#define EHCI_QHDW1_DATATOGGCTRL		0x00004000
#define EHCI_QHDW1_ENDPTSPEED		0x00003000
#define EHCI_QHDW1_ENDPTSPDHIGH		0x00002000
#define EHCI_QHDW1_ENDPTSPDLOW		0x00001000
#define EHCI_QHDW1_ENDPTSPDFULL		0x00000000
#define EHCI_QHDW1_ENDPOINT			0x00000F00
#define EHCI_QHDW1_INACTONNEXT		0x00000080
#define EHCI_QHDW1_DEVADDRESS		0x0000007F
#define EHCI_QHDW2_HISPEEDMULT		0xC0000000
#define EHCI_QHDW2_HISPEEDMULT3		0xC0000000
#define EHCI_QHDW2_HISPEEDMULT2		0x80000000
#define EHCI_QHDW2_HISPEEDMULT1		0x40000000
#define EHCI_QHDW2_PORTNUMBER		0x3F800000
#define EHCI_QHDW2_HUBADDRESS		0x007F0000
#define EHCI_QHDW2_SPLTCOMPMASK		0x0000FF00
#define EHCI_QHDW2_INTSCHEDMASK		0x000000FF

// Bitfields for EHCI Queue Element Transfer Descriptors
#define EHCI_QTDTOKEN_DATATOGG		0x80000000
#define EHCI_QTDTOKEN_TOTBYTES		0x7FFF0000
#define EHCI_QTDTOKEN_IOC			0x00008000
#define EHCI_QTDTOKEN_CURRPAGE		0x00007000
#define EHCI_QTDTOKEN_ERRCOUNT		0x00000C00
#define EHCI_QTDTOKEN_PID			0x00000300
#define EHCI_QTDTOKEN_PID_SETUP		0x00000200
#define EHCI_QTDTOKEN_PID_IN		0x00000100
#define EHCI_QTDTOKEN_PID_OUT		0x00000000
#define EHCI_QTDTOKEN_STATMASK		0x000000FF
#define EHCI_QTDTOKEN_ACTIVE		0x00000080
#define EHCI_QTDTOKEN_ERROR			0x0000007C
#define EHCI_QTDTOKEN_ERRHALT		0x00000040
#define EHCI_QTDTOKEN_ERRDATBUF		0x00000020
#define EHCI_QTDTOKEN_ERRBABBLE		0x00000010
#define EHCI_QTDTOKEN_ERRXACT		0x00000008
#define EHCI_QTDTOKEN_ERRMISSMF		0x00000004
#define EHCI_QTDTOKEN_SPLTXSTAT		0x00000002
#define EHCI_QTDTOKEN_PINGSTATE		0x00000001

// EHCI data structure for Queue Transfer Descriptor
typedef volatile struct {
	unsigned nextQtd;
	unsigned altNextQtd;
	unsigned token;
	unsigned buffPage[EHCI_MAX_QTD_BUFFERS];
	unsigned extBuffPage[EHCI_MAX_QTD_BUFFERS];
	char pad[12]; // pad to 32-byte boundary

} __attribute__((packed)) ehciQtd;

// Structure for managing lists of ehciQtds.
typedef struct _ehciQtdItem {
	ehciQtd *qtd;
	unsigned physical;
	void *buffer;
	struct _ehciQtdItem *nextQtdItem;

} ehciQtdItem;

// EHCI data structure for a Queue Head
typedef volatile struct {
	unsigned horizLink;
	unsigned endpointChars;
	unsigned endpointCaps;
	unsigned currentQtd;
	ehciQtd overlay;
	char pad[16]; // pad to 32-byte boundary

} __attribute__((packed)) ehciQueueHead;

// Structure for managing lists of ehciQueueHeads.
typedef struct _ehciQueueHeadItem {
	void *usbDev;
	unsigned char endpoint;
	ehciQueueHead *queueHead;
	unsigned physical;
	ehciQtdItem *firstQtdItem;
	struct _ehciQueueHeadItem *devNext;
	struct _ehciQueueHeadItem *listNext;

} ehciQueueHeadItem;

typedef struct {
	ehciQueueHeadItem *queueHeadItem;
	int numQtds;
	int numDataQtds;
	ehciQtdItem **qtdItems;
	unsigned bytesRemaining;

} ehciTransQueue;

// Extended capability pointer register
typedef volatile struct {
	unsigned char id;
	unsigned char next;
	unsigned short capSpec;

} __attribute__((packed)) ehciExtendedCaps;

// Legacy support capability register set
typedef volatile struct {
	unsigned legSuppCap;
	unsigned legSuppContStat;

} __attribute__((packed)) ehciLegacySupport;

// Capability registers
typedef volatile struct {
	unsigned char capslen;
	unsigned char res;
	unsigned short hciver;
	unsigned hcsparams;
	unsigned hccparams;
	uquad_t hcsp_portroute;

} __attribute__((packed)) ehciCapRegs;

// Operational registers
typedef volatile struct {
	unsigned cmd;
	unsigned stat;
	unsigned intr;
	unsigned frindex;
	unsigned ctrldsseg;
	unsigned perlstbase;
	unsigned asynclstaddr;
	unsigned res[9];
	unsigned configflag;
	unsigned portsc[];

} __attribute__((packed)) ehciOpRegs;

typedef struct {
	usbDevice *usbDev;
	int interface;
	unsigned char endpoint;
	unsigned maxLen;
	int interval;
	ehciTransQueue transQueue;
	unsigned bufferPhysical;
	void (*callback)(usbDevice *, int, void *, unsigned);

} ehciIntrReg;

typedef struct {
	ehciCapRegs *capRegs;
	ehciOpRegs *opRegs;
	int numPorts;
	int debugPort;
	kernelLinkedList freeQueueHeadItems;
	kernelLinkedList freeQtdItems;
	ehciQueueHeadItem *devHeads;
	ehciQueueHeadItem *asyncHeads;
	unsigned *periodicList;
	ehciQueueHeadItem *intQueue[EHCI_NUM_INTQUEUEHEADS];
	kernelLinkedList intrRegs;

} usbEhciData;

#define _KERNELUSBEHCIDRIVER_H
#endif

