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
//  kernelNetwork.h
//

#if !defined(_KERNELNETWORK_H)

#include "kernelStream.h"
#include "kernelLinkedList.h"
#include "kernelLock.h"
#include <sys/network.h>

#define NETWORK_DEVICE_TIMEOUT_MS			30000
#define NETWORK_PACKETS_PER_STREAM			256
#define NETWORK_DATASTREAM_LENGTH			1048576

// Number of ARP items cached per network device.
#define NETWORK_ARPCACHE_SIZE				64

// A structure to describe and point to sections inside a buffer of packet
// data
typedef struct _kernelNetworkPacket {
	// Addresses and ports
	networkAddress srcAddress;
	int srcPort;
	networkAddress destAddress;
	int destPort;

	// Protocols
	int linkProtocol;
	int netProtocol;
	int transProtocol;

	// Offsets and lengths in the packet data
	unsigned length;
	unsigned linkHeaderOffset;
	unsigned netHeaderOffset;
	unsigned transHeaderOffset;
	unsigned dataOffset;
	unsigned dataLength;

	// Accounting
	unsigned timeSent;
	unsigned timeout;

	// Memory management
	void (*release)(struct _kernelNetworkPacket *);
	void *context;
	int refCount;

	// Packet memory
	unsigned char memory[NETWORK_PACKET_MAX_LENGTH];

} kernelNetworkPacket;

// Items in the network device's ARP cache
typedef struct {
	networkAddress logicalAddress;
	networkAddress physicalAddress;

} kernelArpCacheItem;

// This represents a queue of network packets as a stream.
typedef stream kernelNetworkPacketStream;

typedef struct {
	int leaseExpiry;
	networkDhcpPacket dhcpPacket;

} kernelDhcpConfig;

typedef struct {
	kernelNetworkPacket *packet[NETWORK_PACKETS_PER_STREAM];
	int freePackets;
	void *data;

} kernelNetworkPacketPool;

// The network device structure
typedef volatile struct {
	networkDevice device;
	kernelDhcpConfig dhcpConfig;
	lock lock;
	kernelArpCacheItem arpCache[NETWORK_ARPCACHE_SIZE];
	int numArpCaches;
	kernelNetworkPacketStream inputStream;
	kernelNetworkPacketStream outputStream;
	kernelNetworkPacketPool packetPool;
	kernelLinkedList connections;
	kernelLinkedList inputHooks;
	kernelLinkedList outputHooks;

	// Driver-specific private data.
	void *data;

} kernelNetworkDevice;

typedef volatile struct {
	unsigned short identification;

} kernelNetworkIpState;

// This structure holds everything that's needed to keep track of a network
// connection, and a stream for received data.
typedef volatile struct {
	int processId;
	int mode;
	networkAddress address;
	networkFilter filter;
	networkStream inputStream;
	kernelNetworkDevice *netDev;
	kernelNetworkIpState ip;

} kernelNetworkConnection;

// Functions exported from kernelNetwork.c
int kernelNetworkRegister(kernelNetworkDevice *);
int kernelNetworkInitialize(void);
kernelNetworkConnection *kernelNetworkConnectionOpen(kernelNetworkDevice *,
	int, networkAddress *, networkFilter *, int);
int kernelNetworkConnectionClose(kernelNetworkConnection *, int);
kernelNetworkPacket *kernelNetworkPacketGet(void);
void kernelNetworkPacketHold(kernelNetworkPacket *);
void kernelNetworkPacketRelease(kernelNetworkPacket *);
int kernelNetworkSetupReceivedPacket(kernelNetworkPacket *);
void kernelNetworkDeliverData(kernelNetworkConnection *,
	kernelNetworkPacket *);
int kernelNetworkSetupSendPacket(kernelNetworkConnection *,
	kernelNetworkPacket *);
void kernelNetworkFinalizeSendPacket(kernelNetworkConnection *,
	kernelNetworkPacket *);
int kernelNetworkSendPacket(kernelNetworkDevice *, kernelNetworkPacket *,
	int);
int kernelNetworkSendData(kernelNetworkConnection *, unsigned char *,
	unsigned, int);
// More functions, but also exported to user space
int kernelNetworkEnabled(void);
int kernelNetworkEnable(void);
int kernelNetworkDisable(void);
kernelNetworkConnection *kernelNetworkOpen(int, networkAddress *,
	networkFilter *);
int kernelNetworkAlive(kernelNetworkConnection *);
int kernelNetworkClose(kernelNetworkConnection *);
int kernelNetworkCloseAll(int);
int kernelNetworkCount(kernelNetworkConnection *);
int kernelNetworkRead(kernelNetworkConnection *, unsigned char *, unsigned);
int kernelNetworkWrite(kernelNetworkConnection *, unsigned char *, unsigned);
int kernelNetworkPing(kernelNetworkConnection *, int, unsigned char *,
	unsigned);
int kernelNetworkGetHostName(char *, int);
int kernelNetworkSetHostName(const char *, int);
int kernelNetworkGetDomainName(char *, int);
int kernelNetworkSetDomainName(const char *, int);

#define _KERNELNETWORK_H
#endif

