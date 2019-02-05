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
//  kernelNetworkPcNetDriver.h
//

// Definitions for the driver for PcNet ethernet network devices.  Based in
// part on a driver contributed by Jonas Zaddach: See the files in the
// directory contrib/jonas-net

#if !defined(_KERNELNETWORKPCNETDRIVER_H)

// The standard PCI device identifiers
#define PCNET_VENDOR_ID					0x1022
#define PCNET_DEVICE_ID					0x2000

// General constants
// Code for the number of ringbuffers:
// 2^PCNET_NUM_RINGBUFFERS_CODE == PCNET_NUM_RINGBUFFERS
#define PCNET_NUM_RINGBUFFERS_CODE		0x6  // 64 ring buffers
#define PCNET_NUM_RINGBUFFERS			(1 << PCNET_NUM_RINGBUFFERS_CODE)
#define PCNET_RINGBUFFER_SIZE			1536

// Port offsets in PC I/O space
#define PCNET_PORTOFFSET_PROM			0x00
#define PCNET_PORTOFFSET_RDP			0x10
#define PCNET_PORTOFFSET16_RAP			0x12
#define PCNET_PORTOFFSET16_RESET		0x14
#define PCNET_PORTOFFSET16_BDP			0x16
#define PCNET_PORTOFFSET16_VENDOR		0x18
#define PCNET_PORTOFFSET32_RAP			0x14
#define PCNET_PORTOFFSET32_RESET		0x18
#define PCNET_PORTOFFSET32_BDP			0x1C

// Control status register (CSR) and bus control register (BCR) numbers
// we care about
#define PCNET_CSR_STATUS				0
#define PCNET_CSR_IADR0					1
#define PCNET_CSR_IADR1					2
#define PCNET_CSR_IMASK					3
#define PCNET_CSR_FEAT					4
#define PCNET_CSR_EXTCTRL				5
#define PCNET_CSR_MODE					15
#define PCNET_CSR_STYLE					58
#define PCNET_CSR_MODEL1				88
#define PCNET_CSR_MODEL0				89
#define PCNET_BCR_MISC					2
#define PCNET_BCR_LINK					4
#define PCNET_BCR_BURST					18

// CSR0 status bits
#define PCNET_CSR_STATUS_ERR			0x8000
#define PCNET_CSR_STATUS_BABL			0x4000
#define PCNET_CSR_STATUS_CERR			0x2000
#define PCNET_CSR_STATUS_MISS			0x1000
#define PCNET_CSR_STATUS_MERR			0x0800
#define PCNET_CSR_STATUS_RINT			0x0400
#define PCNET_CSR_STATUS_TINT			0x0200
#define PCNET_CSR_STATUS_IDON			0x0100
#define PCNET_CSR_STATUS_INTR			0x0080
#define PCNET_CSR_STATUS_IENA			0x0040
#define PCNET_CSR_STATUS_RXON			0x0020
#define PCNET_CSR_STATUS_TXON			0x0010
#define PCNET_CSR_STATUS_TDMD			0x0008
#define PCNET_CSR_STATUS_STOP			0x0004
#define PCNET_CSR_STATUS_STRT			0x0002
#define PCNET_CSR_STATUS_INIT			0x0001

// CSR3 interrupt mask and deferral control bits
#define PCNET_CSR_IMASK_BABLM			0x4000
#define PCNET_CSR_IMASK_MISSM			0x1000
#define PCNET_CSR_IMASK_MERRM			0x0800
#define PCNET_CSR_IMASK_RINTM			0x0400
#define PCNET_CSR_IMASK_TINTM			0x0200
#define PCNET_CSR_IMASK_IDONM			0x0100
#define PCNET_CSR_IMASK_DXMT2PD			0x0010
#define PCNET_CSR_IMASK_EMBA			0x0008

// CSR4 test and features control bits
#define PCNET_CSR_FEAT_EN124			0x8000
#define PCNET_CSR_FEAT_DMAPLUS			0x4000
#define PCNET_CSR_FEAT_TIMER			0x2000
#define PCNET_CSR_FEAT_DPOLL			0x1000
#define PCNET_CSR_FEAT_APADXMT			0x0800
#define PCNET_CSR_FEAT_ASTRPRCV			0x0400
#define PCNET_CSR_FEAT_MFCO				0x0200
#define PCNET_CSR_FEAT_MFCOM			0x0100
#define PCNET_CSR_FEAT_UINTCMD			0x0080
#define PCNET_CSR_FEAT_UINT				0x0040
#define PCNET_CSR_FEAT_RCVCCO			0x0020
#define PCNET_CSR_FEAT_RCVCCOM			0x0010
#define PCNET_CSR_FEAT_TXSTRT			0x0008
#define PCNET_CSR_FEAT_TXSTRTM			0x0004
#define PCNET_CSR_FEAT_JAB				0x0002
#define PCNET_CSR_FEAT_JABM				0x0001

// CSR15 mode bits
#define PCNET_CSR_MODE_PROM				0x8000
#define PCNET_CSR_MODE_DRCVBC			0x4000
#define PCNET_CSR_MODE_DRCVPA			0x2000
#define PCNET_CSR_MODE_DLNKTST			0x1000
#define PCNET_CSR_MODE_DAPC				0x0800
#define PCNET_CSR_MODE_MENDECL			0x0400
#define PCNET_CSR_MODE_LRTTSEL			0x0200
#define PCNET_CSR_MODE_PORTSEL1			0x0100
#define PCNET_CSR_MODE_PORTSEL0			0x0080
#define PCNET_CSR_MODE_INTL				0x0040
#define PCNET_CSR_MODE_DRTY				0x0020
#define PCNET_CSR_MODE_FCOLL			0x0010
#define PCNET_CSR_MODE_DXMTFCS			0x0008
#define PCNET_CSR_MODE_LOOP				0x0004
#define PCNET_CSR_MODE_DTX				0x0002
#define PCNET_CSR_MODE_DRX				0x0001

// BCR20 led status bits we care about
#define PCNET_BCR_LINK_LEDOUT			0x0080

// Flags in transmit/receive ring descriptors
#define PCNET_DESCFLAG_OWN				0x80
#define PCNET_DESCFLAG_ERR				0x40
#define PCNET_DESCFLAG_TRANS_ADD		0x20
#define PCNET_DESCFLAG_RECV_FRAM		0x20
#define PCNET_DESCFLAG_TRANS_MORE		0x10
#define PCNET_DESCFLAG_RECV_OFLO		0x10
#define PCNET_DESCFLAG_TRANS_ONE		0x08
#define PCNET_DESCFLAG_RECV_CRC			0x08
#define PCNET_DESCFLAG_TRANS_DEF		0x04
#define PCNET_DESCFLAG_RECV_BUFF		0x04
#define PCNET_DESCFLAG_STP				0x02
#define PCNET_DESCFLAG_ENP				0x01
// More flags from transmit descriptors only
#define PCNET_DESCFLAG_TRANS_UFLO		0x40
#define PCNET_DESCFLAG_TRANS_LCOL		0x10
#define PCNET_DESCFLAG_TRANS_LCAR		0x80
#define PCNET_DESCFLAG_TRANS_RTRY		0x40

#define PCNET_DESCFLAG_RCV_DROPPED		\
	(PCNET_DESCFLAG_RECV_FRAM | PCNET_DESCFLAG_RECV_OFLO | \
		PCNET_DESCFLAG_RECV_CRC)
#define PCNET_DESCFLAG_TRANS_DROPPED	\
	(PCNET_DESCFLAG_TRANS_UFLO | PCNET_DESCFLAG_TRANS_LCOL | \
		PCNET_DESCFLAG_TRANS_LCAR | PCNET_DESCFLAG_TRANS_RTRY)

typedef enum {
	op_or, op_and
} opType;

typedef volatile struct {
	unsigned short buffAddrLow;
	unsigned char buffAddrHigh;
	unsigned char flags;
	short bufferSize;
	unsigned short messageSize;

} __attribute__((packed)) pcNetRecvDesc16;

typedef volatile struct {
	unsigned short buffAddrLow;
	unsigned char buffAddrHigh;
	unsigned char flags;
	short bufferSize;
	unsigned short transFlags;

} __attribute__((packed)) pcNetTransDesc16;

typedef struct {
	int next;
	union {
		pcNetRecvDesc16 *recv;
		pcNetTransDesc16 *trans;
	} desc;
	unsigned char *buffers[PCNET_NUM_RINGBUFFERS];

} pcNetRing;

typedef struct {
	void *ioAddress;
	unsigned ioSpaceSize;
	void *memoryAddress;
	unsigned memorySize;
	unsigned chipVersion;
	pcNetRing recvRing;
	pcNetRing transRing;

} pcNetDevice;

typedef struct {
	unsigned short mode;				// 0x00
	unsigned char physAddr[6];			// 0x02
	unsigned short addressFilter[4];	// 0x08
	unsigned short recvDescLow;			// 0x10
	unsigned char recvDescHigh;			// 0x12
	unsigned char recvRingLen;			// 0x13
	unsigned short transDescLow;		// 0x14
	unsigned char transDescHigh;		// 0x16
	unsigned char transRingLen;			// 0x17

} __attribute__((packed)) pcNetInitBlock16;

#define _KERNELNETWORKPCNETDRIVER_H
#endif

