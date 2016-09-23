//
//  Visopsys
//  Copyright (C) 1998-2016 J. Andrew McLaughlin
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
//  network.h
//

// This file contains definitions and structures for using network
// functions in Visopsys.

#if !defined(_NETWORK_H)

#define NETWORK_MAX_HOSTNAMELENGTH			32
#define NETWORK_MAX_DOMAINNAMELENGTH		(256 - NETWORK_MAX_HOSTNAMELENGTH)
#define NETWORK_MAX_ADAPTERS				16
#define NETWORK_ADAPTER_MAX_NAMELENGTH		32

// Flags for network devices
#define NETWORK_ADAPTERFLAG_INITIALIZED		0x8000
#define NETWORK_ADAPTERFLAG_RUNNING			0x4000
#define NETWORK_ADAPTERFLAG_LINK			0x2000
#define NETWORK_ADAPTERFLAG_AUTOCONF		0x1000
#define NETWORK_ADAPTERFLAG_PROMISCUOUS		0x0008
#define NETWORK_ADAPTERFLAG_AUTOSTRIP		0x0004
#define NETWORK_ADAPTERFLAG_AUTOPAD			0x0002
#define NETWORK_ADAPTERFLAG_AUTOCRC			0x0001

// Since for now, we only support ethernet at the link layer, max packet
// size is the upper size limit of an ethernet frame.
#define NETWORK_PACKET_MAX_LENGTH			1518
#define NETWORK_MAX_ETHERDATA_LENGTH		1500

// Lengths of addresses for protocols
#define NETWORK_ADDRLENGTH_ETHERNET			6
#define NETWORK_ADDRLENGTH_IP				4

// Supported protocols.  Link layer protocols:
#define NETWORK_LINKPROTOCOL_ETHERNET		1
// Supported network layer protocols:
#define NETWORK_NETPROTOCOL_IP				1
// Supported top-level (transport layer) protocols, or network layer ones
// that have no corresponding transport protocol.  Where applicable,
// these match the IANA assigned IP protocol numbers:
#define NETWORK_TRANSPROTOCOL_ICMP			1
#define NETWORK_TRANSPROTOCOL_TCP			6
#define NETWORK_TRANSPROTOCOL_UDP			17

// TCP/UDP port numbers we care about
#define NETWORK_PORT_BOOTPSERVER			67  // TCP/UDP: BOOTP/DHCP Server
#define NETWORK_PORT_BOOTPCLIENT			68  // TCP/UDP: BOOTP/DHCP Client

// Types of network connections, in order of ascending abstraction
#define NETWORK_HEADERS_NONE				0
#define NETWORK_HEADERS_TRANSPORT			1
#define NETWORK_HEADERS_NET					2
#define NETWORK_HEADERS_RAW					3

// Mode flags for network connections
#define NETWORK_MODE_LISTEN					0x04
#define NETWORK_MODE_READ					0x02
#define NETWORK_MODE_WRITE					0x01
#define NETWORK_MODE_READWRITE				(NETWORK_MODE_READ | \
											NETWORK_MODE_WRITE)
// ICMP message types
#define NETWORK_ICMP_ECHOREPLY				0
#define NETWORK_ICMP_DESTUNREACHABLE		3
#define NETWORK_ICMP_SOURCEQUENCH			4
#define NETWORK_ICMP_REDIRECT				5
#define NETWORK_ICMP_ECHO					8
#define NETWORK_ICMP_TIMEEXCEEDED			11
#define NETWORK_ICMP_PARAMPROBLEM			12
#define NETWORK_ICMP_TIMESTAMP				13
#define NETWORK_ICMP_TIMESTAMPREPLY			14
#define NETWORK_ICMP_INFOREQUEST			15
#define NETWORK_ICMP_INFOREPLY				16

#define NETWORK_PING_DATASIZE				56

#define networkAddressesEqual(addr1, addr2, addrSize) \
	((((addr1)->quad) & (0xFFFFFFFFFFFFFFFFULL >> (8 * (8 - (addrSize))))) == \
	(((addr2)->quad) & (0xFFFFFFFFFFFFFFFFULL >> (8 * (8 - (addrSize))))))

// Generic 64-bit, byte-addressable network address, logical or physical.
// Actual length is obviously protocol-specific
typedef union {
	unsigned char bytes[8];
	unsigned long long quad;

} __attribute__((packed)) networkAddress;

// A network adapter device
typedef struct {
	char name[NETWORK_ADAPTER_MAX_NAMELENGTH];
	unsigned flags;
	// Physical network address
	networkAddress hardwareAddress;
	// Logical host address
	networkAddress hostAddress;
	// Net mask
	networkAddress netMask;
	// Broadcast address
	networkAddress broadcastAddress;
	// Gateway address
	networkAddress gatewayAddress;
	// Link protocol
	int linkProtocol;
	// Interrupt lint
	int interruptNum;
	// Queues
	int recvQueued;
	int recvQueueLen;
	int transQueued;
	int transQueueLen;
	// Statistics
	unsigned recvPackets;
	unsigned recvErrors;
	unsigned recvDropped;
	unsigned recvOverruns;
	unsigned transPackets;
	unsigned transErrors;
	unsigned transDropped;
	unsigned transOverruns;
	unsigned collisions;

} networkDevice;

// This structure is used to filter packets to network connections.
typedef struct {
	int headers;
	int linkProtocol;
	int netProtocol;
	int transProtocol;
	int subProtocol;
	int localPort;
	int remotePort;

} networkFilter;

// Protocol things

typedef struct {
	unsigned char dest[NETWORK_ADDRLENGTH_ETHERNET];
	unsigned char source[NETWORK_ADDRLENGTH_ETHERNET];
	unsigned short type;

} __attribute__((packed)) networkEthernetHeader;

typedef struct {
	unsigned char versionHeaderLen;
	unsigned char typeOfService;
	unsigned short totalLength;
	unsigned short identification;
	unsigned short flagsFragOffset;
	unsigned char timeToLive;
	unsigned char protocol;
	unsigned short headerChecksum;
	unsigned srcAddress;
	unsigned destAddress;

} __attribute__((packed)) networkIpHeader;

typedef struct {
	unsigned char type;
	unsigned char code;
	unsigned short checksum;

} __attribute__((packed)) networkIcmpHeader;

typedef struct {
	unsigned short srcPort;
	unsigned short destPort;
	unsigned sequenceNum;
	unsigned ackNum;
	unsigned short dataOffsetFlags;
	unsigned short window;
	unsigned short checksum;
	unsigned short urgentPointer;

} __attribute__((packed)) networkTcpHeader;

typedef struct {
	unsigned short srcPort;
	unsigned short destPort;
	unsigned short length;
	unsigned short checksum;

} __attribute__((packed)) networkUdpHeader;

typedef struct {
	networkIcmpHeader icmpHeader;
	unsigned short identifier;
	unsigned short sequenceNum;
	unsigned char data[NETWORK_PING_DATASIZE];

} __attribute__((packed)) networkPingPacket;

#define _NETWORK_H
#endif

