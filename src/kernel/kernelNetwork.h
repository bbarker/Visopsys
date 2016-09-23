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
//  kernelNetwork.h
//

#if !defined(_KERNELNETWORK_H)

#include "kernelStream.h"
#include "kernelLock.h"
#include <sys/network.h>

#define NETWORK_DATASTREAM_LENGTH		0xFFFF

// Ethernet frame types we care about
#define NETWORK_ETHERTYPE_IP			0x800  // Internet Protocol (IP)
#define NETWORK_ETHERTYPE_ARP			0x806  // Address Resolution Protocol

// Constants for DHCP
#define NETWORK_DHCP_DEFAULT_TIMEOUT	5
#define NETWORK_DHCP_COOKIE				0x63825363
#define NETWORK_DHCPHARDWARE_ETHERNET	1
#define NETWORK_DHCPOPCODE_BOOTREQUEST	1
#define NETWORK_DHCPOPCODE_BOOTREPLY	2
#define NETWORK_DHCPOPTION_PAD			0
#define NETWORK_DHCPOPTION_SUBNET		1
#define NETWORK_DHCPOPTION_ROUTER		3
#define NETWORK_DHCPOPTION_DNSSERVER	6
#define NETWORK_DHCPOPTION_HOSTNAME		12
#define NETWORK_DHCPOPTION_DOMAIN		15
#define NETWORK_DHCPOPTION_BROADCAST	28
#define NETWORK_DHCPOPTION_ADDRESSREQ	50
#define NETWORK_DHCPOPTION_LEASETIME	51
#define NETWORK_DHCPOPTION_MSGTYPE		53
#define NETWORK_DHCPOPTION_SERVERID		54
#define NETWORK_DHCPOPTION_PARAMREQ		55
#define NETWORK_DHCPOPTION_END			255
#define NETWORK_DHCPMSG_DHCPDISCOVER	1
#define NETWORK_DHCPMSG_DHCPOFFER		2
#define NETWORK_DHCPMSG_DHCPREQUEST		3
#define NETWORK_DHCPMSG_DHCPDECLINE		4
#define NETWORK_DHCPMSG_DHCPACK			5
#define NETWORK_DHCPMSG_DHCPNAK			6
#define NETWORK_DHCPMSG_DHCPRELEASE		7
#define NETWORK_DHCPMSG_DHCPINFORM		8

// Number of ARP items cached per network adapter.
#define NETWORK_ARPCACHE_SIZE			64

// TCP header flags
#define NETWORK_TCPFLAG_URG				0x20
#define NETWORK_TCPFLAG_ACK				0x10
#define NETWORK_TCPFLAG_PSH				0x08
#define NETWORK_TCPFLAG_RST				0x04
#define NETWORK_TCPFLAG_SYN				0x02
#define NETWORK_TCPFLAG_FIN				0x01

#define networkGetTcpHdrSize(tcpHdrP) \
	(((tcpHdrP)->dataOffsetFlags & 0xF0) >> 2)

#define networkSetTcpHdrSize(tcpHdrP, sz) \
	((tcpHdrP)->dataOffsetFlags = (((tcpHdrP)->dataOffsetFlags & 0xFF0F) | \
		(((sz) & 0x3C) << 2)))

#define networkGetTcpHdrFlags(tcpHdrP) \
	(((tcpHdrP)->dataOffsetFlags & 0x3F00) >> 8)

#define networkSetTcpHdrFlags(tcpHdrP, flgs) \
	((tcpHdrP)->dataOffsetFlags = (((tcpHdrP)->dataOffsetFlags & 0xC0FF) | \
 		(((flgs) & 0x3F) << 8)))

typedef enum {
	tcp_closed, tcp_listen, tcp_syn_sent, tcp_syn_received, tcp_established,
	tcp_close_wait, tcp_last_ack, tcp_fin_wait1, tcp_closing, tcp_fin_wait2,
	tcp_time_wait

} kernelNetworkTcpState;

// A structure to describe and point to sections inside a buffer of packet
// data
typedef struct {
	networkAddress srcAddress;
	int srcPort;
	networkAddress destAddress;
	int destPort;
	int linkProtocol;
	int netProtocol;
	int transProtocol;
	void *memory;
	unsigned length;
	void *linkHeader;
	void *netHeader;
	void *transHeader;
	unsigned char *data;
	unsigned dataLength;

} kernelNetworkPacket;

// Items in the network adapter's ARP cache
typedef struct {
	networkAddress logicalAddress;
	networkAddress physicalAddress;

} kernelArpCacheItem;

// This represents a queue of network packets as a stream.
typedef stream kernelNetworkPacketStream;

typedef struct {
	unsigned char code;
	unsigned char length;
	unsigned char data[];

}  __attribute__((packed)) kernelDhcpOption;

typedef struct {											// RFC field names:
	unsigned char opCode;									// op
	unsigned char hardwareType;								// htype
	unsigned char hardwareAddrLen;							// hlen
	unsigned char hops;										// hops
	unsigned transactionId;									// xid
	unsigned short seconds;									// secs
	unsigned short flags;									// flags
	unsigned char clientLogicalAddr[NETWORK_ADDRLENGTH_IP];	// ciaddr
	unsigned char yourLogicalAddr[NETWORK_ADDRLENGTH_IP];	// yiaddr
	unsigned char serverLogicalAddr[NETWORK_ADDRLENGTH_IP];	// siaddr
	unsigned char relayLogicalAddr[NETWORK_ADDRLENGTH_IP];	// giaddr
	unsigned char clientHardwareAddr[16];					// chaddr
	char serverName[64];									// sname
	char bootFile[128];										// file
	unsigned cookie;										// \ options
	unsigned char options[308];								// /

} __attribute__((packed)) kernelDhcpPacket;

typedef struct {
	int leaseExpiry;
	kernelDhcpPacket dhcpPacket;

} kernelDhcpConfig;

// The network adapter structure
typedef volatile struct {
	networkDevice device;
	kernelDhcpConfig dhcpConfig;
	lock adapterLock;
	kernelArpCacheItem arpCache[NETWORK_ARPCACHE_SIZE];
	int numArpCaches;
	kernelNetworkPacketStream inputStream;
	lock inputStreamLock;
	kernelNetworkPacketStream outputStream;
	lock outputStreamLock;
	volatile struct _kernelNetworkConnection *connections;
	unsigned char buffer[NETWORK_PACKET_MAX_LENGTH];

	// Driver-specific private data.
	void *data;

} kernelNetworkDevice;

// This structure holds everything that's needed to keep track of a
// linked list of network 'connections', including a key code to identify
// it and packet input and output streams.
typedef volatile struct _kernelNetworkConnection {
	int processId;
	int mode;
	networkAddress address;
	networkFilter filter;
	lock inputStreamLock;
	networkStream inputStream;
	kernelNetworkDevice *adapter;
	struct {
		unsigned short identification;
	} ip;
	struct {
		kernelNetworkTcpState state;
		unsigned sendUnAcked;
		unsigned sendNext;
		unsigned recvAcked;
		unsigned recvLast;
		unsigned recvWindow;
		kernelNetworkPacket *sentUnAckedPackets;
	} tcp;

	volatile struct _kernelNetworkConnection *previous;
	volatile struct _kernelNetworkConnection *next;

} kernelNetworkConnection;

// Functions exported from kernelNetwork.c
int kernelNetworkRegister(kernelNetworkDevice *);
int kernelNetworkInitialized(void);
int kernelNetworkInitialize(void);
int kernelNetworkShutdown(void);
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
void kernelNetworkIpDebug(unsigned char *);

#define _KERNELNETWORK_H
#endif

