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
//  network.h
//

// This file contains definitions and structures for using network functions
// in Visopsys.

#if !defined(_NETWORK_H)

#define NETWORK_MAX_HOSTNAMELENGTH			32
#define NETWORK_MAX_DOMAINNAMELENGTH		(256 - NETWORK_MAX_HOSTNAMELENGTH)
#define NETWORK_MAX_DEVICES					16
#define NETWORK_DEVICE_MAX_NAMELENGTH		32

// Flags for network devices
#define NETWORK_DEVICEFLAG_INITIALIZED		0x10000
#define NETWORK_DEVICEFLAG_DISABLED			0x08000
#define NETWORK_DEVICEFLAG_RUNNING			0x04000
#define NETWORK_DEVICEFLAG_LINK				0x02000
#define NETWORK_DEVICEFLAG_AUTOCONF			0x01000
#define NETWORK_DEVICEFLAG_PROMISCUOUS		0x00008
#define NETWORK_DEVICEFLAG_AUTOSTRIP		0x00004
#define NETWORK_DEVICEFLAG_AUTOPAD			0x00002
#define NETWORK_DEVICEFLAG_AUTOCRC			0x00001

// Flags for network filter fields
#define NETWORK_FILTERFLAG_HEADERS			0x40
#define NETWORK_FILTERFLAG_LINKPROTOCOL		0x20
#define NETWORK_FILTERFLAG_NETPROTOCOL		0x10
#define NETWORK_FILTERFLAG_TRANSPROTOCOL	0x08
#define NETWORK_FILTERFLAG_SUBPROTOCOL		0x04
#define NETWORK_FILTERFLAG_LOCALPORT		0x02
#define NETWORK_FILTERFLAG_REMOTEPORT		0x01

// Since for now, we only support ethernet at the link layer, max packet
// size is the upper size limit of an ethernet frame.
#define NETWORK_PACKET_MAX_LENGTH			1518
#define NETWORK_MAX_ETHERDATA_LENGTH		1500

// Lengths of addresses for protocols
#define NETWORK_ADDRLENGTH_ETHERNET			6
#define NETWORK_ADDRLENGTH_IP4				4
#define NETWORK_ADDRLENGTH_IP6				16

// Supported link layer protocols
#define NETWORK_LINKPROTOCOL_LOOP			1
#define NETWORK_LINKPROTOCOL_ETHERNET		2

// Supported network layer protocols
#define NETWORK_NETPROTOCOL_ARP				1
#define NETWORK_NETPROTOCOL_IP4				2

// Transport layer protocols we care about, or network layer ones that have no
// corresponding transport protocol.  Where applicable, these match the IANA
// assigned IP protocol numbers.
#define NETWORK_TRANSPROTOCOL_ICMP			1
#define NETWORK_TRANSPROTOCOL_IGMP			2
#define NETWORK_TRANSPROTOCOL_IP4ENCAP		4
#define NETWORK_TRANSPROTOCOL_TCP			6
#define NETWORK_TRANSPROTOCOL_UDP			17
#define NETWORK_TRANSPROTOCOL_RDP			27
#define NETWORK_TRANSPROTOCOL_IRTP			28
#define NETWORK_TRANSPROTOCOL_DCCP			33
#define NETWORK_TRANSPROTOCOL_IP6ENCAP		41
#define NETWORK_TRANSPROTOCOL_RSVP			46
#define NETWORK_TRANSPROTOCOL_SCTP			132
#define NETWORK_TRANSPROTOCOL_UDPLITE		136

// Ethernet frame types we care about
#define NETWORK_ETHERTYPE_IEEE802_3			0x05DC	// If <= is 802.3 length
#define NETWORK_ETHERTYPE_IP4				0x0800	// Internet Protocol v4
#define NETWORK_ETHERTYPE_ARP				0x0806	// Address Res. Protocol
#define NETWORK_ETHERTYPE_RARP				0x8035	// Reverse ARP
#define NETWORK_ETHERTYPE_APPLETALK			0x809B	// AppleTalk
#define NETWORK_ETHERTYPE_APPLEARP			0x80F3	// AppleTalk ARP
#define NETWORK_ETHERTYPE_IP6				0x86DD	// Internet Protocol v6
#define NETWORK_ETHERTYPE_LLDP				0x88CC	// Link-layer discovery

// Some constants for Address Resolution Protocol (ARP)
#define NETWORK_ARPHARDWARE_ETHERNET		1
#define NETWORK_ARPOP_REQUEST				1
#define NETWORK_ARPOP_REPLY					2

// TCP/UDP port numbers we care about
#define NETWORK_PORT_FTPDATA				20	// TCP: FTP data
#define NETWORK_PORT_FTP					21	// TCP: FTP
#define NETWORK_PORT_SSH					22	// TCP/UDP: SSH server
#define NETWORK_PORT_TELNET					23	// TCP/UDP: telnet server
#define NETWORK_PORT_SMTP					25	// TCP: SMTP mail server
#define NETWORK_PORT_DNS					53	// TCP/UDP: DNS server
#define NETWORK_PORT_BOOTPSERVER			67  // TCP/UDP: BOOTP/DHCP server
#define NETWORK_PORT_BOOTPCLIENT			68  // TCP/UDP: BOOTP/DHCP client
#define NETWORK_PORT_HTTP					80	// TCP/UDP: HTTP www server
#define NETWORK_PORT_POP3					110	// TCP/UDP: POP3 mail server
#define NETWORK_PORT_NTP					123	// TCP/UDP: NTP time server
#define NETWORK_PORT_IMAP3					220	// TCP/UDP: IMAP mail server
#define NETWORK_PORT_LDAP					389	// TCP/UDP: LDAP dir server
#define NETWORK_PORT_HTTPS					443	// TCP/UDP: secure HTTP
#define NETWORK_PORT_FTPSDATA				989	// TCP: secure FTP data
#define NETWORK_PORT_FTPS					990	// TCP: secure FTP
#define NETWORK_PORT_TELNETS				992	// TCP/UDP: secure telnet
#define NETWORK_PORT_IMAPS					993	// TCP/UDP: secure IMAP
#define NETWORK_PORT_POP3S					995	// TCP/UDP: secure POP3

// IANA name strings for the above
#define NETWORK_PORTNAME_FTPDATA			"ftp-data"
#define NETWORK_PORTNAME_FTP				"ftp"
#define NETWORK_PORTNAME_SSH				"ssh"
#define NETWORK_PORTNAME_TELNET				"telnet"
#define NETWORK_PORTNAME_SMTP				"smtp"
#define NETWORK_PORTNAME_DNS				"domain"
#define NETWORK_PORTNAME_BOOTPSERVER		"bootps"
#define NETWORK_PORTNAME_BOOTPCLIENT		"bootpc"
#define NETWORK_PORTNAME_HTTP				"http"
#define NETWORK_PORTNAME_POP3				"pop3"
#define NETWORK_PORTNAME_NTP				"ntp"
#define NETWORK_PORTNAME_IMAP3				"imap3"
#define NETWORK_PORTNAME_LDAP				"ldap"
#define NETWORK_PORTNAME_HTTPS				"https"
#define NETWORK_PORTNAME_FTPSDATA			"ftps-data"
#define NETWORK_PORTNAME_FTPS				"ftps"
#define NETWORK_PORTNAME_TELNETS			"telnets"
#define NETWORK_PORTNAME_IMAPS				"imaps"
#define NETWORK_PORTNAME_POP3S				"pop3s"

// Types of network connections, in order of ascending abstraction
#define NETWORK_HEADERS_NONE				0
#define NETWORK_HEADERS_TRANSPORT			1
#define NETWORK_HEADERS_NET					2
#define NETWORK_HEADERS_LINK				3
#define NETWORK_HEADERS_RAW					4

// Mode flags for network connections
#define NETWORK_MODE_LISTEN					0x04
#define NETWORK_MODE_READ					0x02
#define NETWORK_MODE_WRITE					0x01
#define NETWORK_MODE_READWRITE				\
	(NETWORK_MODE_READ | NETWORK_MODE_WRITE)

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

// TCP header flags
#define NETWORK_TCPFLAG_URG					0x20
#define NETWORK_TCPFLAG_ACK					0x10
#define NETWORK_TCPFLAG_PSH					0x08
#define NETWORK_TCPFLAG_RST					0x04
#define NETWORK_TCPFLAG_SYN					0x02
#define NETWORK_TCPFLAG_FIN					0x01

// DHCP constants
#define NETWORK_DHCP_COOKIE					0x63825363
#define NETWORK_DHCPHARDWARE_ETHERNET		1
#define NETWORK_DHCPOPCODE_BOOTREQUEST		1
#define NETWORK_DHCPOPCODE_BOOTREPLY		2
#define NETWORK_DHCPOPTION_PAD				0
#define NETWORK_DHCPOPTION_SUBNET			1
#define NETWORK_DHCPOPTION_ROUTER			3
#define NETWORK_DHCPOPTION_DNSSERVER		6
#define NETWORK_DHCPOPTION_HOSTNAME			12
#define NETWORK_DHCPOPTION_DOMAIN			15
#define NETWORK_DHCPOPTION_BROADCAST		28
#define NETWORK_DHCPOPTION_ADDRESSREQ		50
#define NETWORK_DHCPOPTION_LEASETIME		51
#define NETWORK_DHCPOPTION_MSGTYPE			53
#define NETWORK_DHCPOPTION_SERVERID			54
#define NETWORK_DHCPOPTION_PARAMREQ			55
#define NETWORK_DHCPOPTION_END				255
#define NETWORK_DHCPMSG_DHCPDISCOVER		1
#define NETWORK_DHCPMSG_DHCPOFFER			2
#define NETWORK_DHCPMSG_DHCPREQUEST			3
#define NETWORK_DHCPMSG_DHCPDECLINE			4
#define NETWORK_DHCPMSG_DHCPACK				5
#define NETWORK_DHCPMSG_DHCPNAK				6
#define NETWORK_DHCPMSG_DHCPRELEASE			7
#define NETWORK_DHCPMSG_DHCPINFORM			8

// Ping
#define NETWORK_PING_DATASIZE				56

// Empty address
#define NETWORK_ADDR_EMPTY \
	((networkAddress){ { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 } } )

// Broadcast address for ethernet
#define NETWORK_BROADCAST_ADDR_ETHERNET \
	((networkAddress){ { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, \
		0, 0, 0, 0, 0, 0, 0, 0, 0, 0 } } )

// Broadcast address for IP v4
#define NETWORK_BROADCAST_ADDR_IP4 \
	((networkAddress){ { 0xFF, 0xFF, 0xFF, 0xFF, \
		0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 } } )

// Loopback address and netmask for IP v4
#define NETWORK_LOOPBACK_ADDR_IP4 \
	((networkAddress){ { 0x7F, 0x00, 0x00, 0x01, \
		0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 } } )
#define NETWORK_LOOPBACK_NETMASK_IP4 \
	((networkAddress){ { 0xFF, 0, 0, 0, \
		0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 } } )

#define networkAddressesEqual(addr1, addr2, addrSize) \
	(!memcmp((const void *)(addr1), (const void *)(addr2), (addrSize)))

#define networkAddressEmpty(addr, addrSize) \
	(networkAddressesEqual((addr), &NETWORK_ADDR_EMPTY, (addrSize)))

#define networkAddressCopy(addr1, addr2, addrSize) \
	memcpy((void *)(addr1), (const void *)(addr2), (addrSize))

#define networksEqualIp4(addr1, netmask, addr2)	\
	(((addr1)->dword[0] & (netmask)->dword[0]) ==	\
		((addr2)->dword[0] & (netmask)->dword[0]))

// Generic 128-bit network address; logical or physical, addressable by
// different size-granularities.  Actual length used is obviously protocol-
// specific.
typedef union {
	unsigned char byte[16];
	unsigned short word[8];
	unsigned dword[4];
	unsigned long long quad[2];

} __attribute__((packed)) networkAddress;

// A network device
typedef struct {
	char name[NETWORK_DEVICE_MAX_NAMELENGTH];
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
	// DNS server address
	networkAddress dnsAddress;
	// Link protocol
	int linkProtocol;
	// Interrupt number
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
	int flags;
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
	unsigned short hardwareAddressSpace;
	unsigned short protocolAddressSpace;
	unsigned char hardwareAddrLen;
	unsigned char protocolAddrLen;
	unsigned short opCode;
	// The rest of these are only valid for IPv4 over ethernet
	unsigned char srcHardwareAddress[NETWORK_ADDRLENGTH_ETHERNET];
	unsigned char srcLogicalAddress[NETWORK_ADDRLENGTH_IP4];
	unsigned char destHardwareAddress[NETWORK_ADDRLENGTH_ETHERNET];
	unsigned char destLogicalAddress[NETWORK_ADDRLENGTH_IP4];
	// Padding to bring us up to the mininum 46 byte ethernet packet size.
	// Some devices can't automatically pad it for us.
	char pad[18];
	// Space for the ethernet FCS checksum.  Some devices can't automatically
	// add it for us.
	unsigned Fcs;

} __attribute__((packed)) networkArpHeader;

typedef struct {
	networkEthernetHeader ethHeader;
	networkArpHeader arpHeader;

} __attribute__((packed)) networkArpPacket;

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

} __attribute__((packed)) networkIp4Header;

typedef struct {
	unsigned char versionClassLo;
	unsigned char classHiFlowLo;
	unsigned short flowHi;
	unsigned short payloadLen;
	unsigned char nextHeader;
	unsigned char hopLimit;
	unsigned char srcAddress[NETWORK_ADDRLENGTH_IP6];
	unsigned char destAddress[NETWORK_ADDRLENGTH_IP6];

} __attribute__((packed)) networkIp6Header;

typedef struct {
	unsigned char type;
	unsigned char code;
	unsigned short checksum;

} __attribute__((packed)) networkIcmpHeader;

typedef enum {
	tcp_closed = 0, tcp_listen = 1, tcp_syn_sent = 2, tcp_syn_received = 3,
	tcp_established = 4, tcp_close_wait = 5, tcp_last_ack = 6,
	tcp_fin_wait1 = 7, tcp_closing = 8, tcp_fin_wait2 = 9, tcp_time_wait = 10

} networkTcpState;

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
	unsigned char code;
	unsigned char length;
	unsigned char data[];

}  __attribute__((packed)) networkDhcpOption;

typedef struct {												// RFC names:
	unsigned char opCode;										// op
	unsigned char hardwareType;									// htype
	unsigned char hardwareAddrLen;								// hlen
	unsigned char hops;											// hops
	unsigned transactionId;										// xid
	unsigned short seconds;										// secs
	unsigned short flags;										// flags
	unsigned char clientLogicalAddr[NETWORK_ADDRLENGTH_IP4];	// ciaddr
	unsigned char yourLogicalAddr[NETWORK_ADDRLENGTH_IP4];		// yiaddr
	unsigned char serverLogicalAddr[NETWORK_ADDRLENGTH_IP4];	// siaddr
	unsigned char relayLogicalAddr[NETWORK_ADDRLENGTH_IP4];		// giaddr
	unsigned char clientHardwareAddr[16];						// chaddr
	char serverName[64];										// sname
	char bootFile[128];											// file
	unsigned cookie;											// \ options
	unsigned char options[308];									// /

} __attribute__((packed)) networkDhcpPacket;

typedef struct {
	networkIcmpHeader icmpHeader;
	unsigned short identifier;
	unsigned short sequenceNum;
	unsigned char data[NETWORK_PING_DATASIZE];

} __attribute__((packed)) networkPingPacket;

#define _NETWORK_H
#endif

