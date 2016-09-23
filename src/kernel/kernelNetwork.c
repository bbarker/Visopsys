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
//  kernelNetwork.c
//

#include "kernelNetwork.h"
#include "kernelCpu.h"
#include "kernelError.h"
#include "kernelLog.h"
#include "kernelMalloc.h"
#include "kernelMultitasker.h"
#include "kernelNetworkDevice.h"
#include "kernelNetworkStream.h"
#include "kernelRandom.h"
#include "kernelRtc.h"
#include "kernelVariableList.h"
#include <stdlib.h>
#include <string.h>
#include <sys/processor.h>

static char *hostName = NULL;
static char *domainName = NULL;
static kernelNetworkDevice *adapters[NETWORK_MAX_ADAPTERS];
static int numDevices = 0;
static int netThreadPid = 0;
static int networkStop = 0;
static int initialized = 0;

// This broadcast address works for both ethernet and IP
static networkAddress broadcastAddress = {
	{ 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0, 0 }
};


static kernelNetworkDevice *getDevice(networkAddress *dest)
{
	// Given a destination address, determine the best/appropriate network
	// adapter to use for the connection.

	kernelNetworkDevice *adapter = NULL;
	int count;

	for (count = 0; count < numDevices; count ++)
	{
		if (dest)
		{
			adapter = adapters[count];
			break;
		}
	}

	// If we found an appropriate adapter, the pointer will be set.
	return (adapter);
}


static unsigned short ipChecksum(networkIpHeader *ipHeader)
{
	// Calculate the checksum for the supplied IP packet header.  This is done
	// as a 1's complement sum of each 16-bit word in the header.

	int headerWords = ((ipHeader->versionHeaderLen & 0xF) * 2);
	unsigned short *words = (unsigned short *) ipHeader;
	unsigned checksum = 0;
	int count;

	for (count = 0; count < headerWords; count ++)
	{
		// Skip the checksum word itself
		if (count != 5)
			checksum += processorSwap16(words[count]);
	}

	return ((unsigned short)((~checksum & 0xFFFF) - (checksum >> 16)));
}


static unsigned short icmpChecksum(networkIcmpHeader *header, int length)
{
	// Calculate the checksum for the supplied packet.  This is done
	// as a 1's complement sum of the ICMP header + the data

	unsigned short *words = (void *) header;
	unsigned checksum = 0;
	int count;

	for (count = 0; count < (length / 2); count ++)
	{
		// Skip the checksum word itself
		if (count != 1)
			checksum += processorSwap16(words[count]);
	}

	return ((unsigned short)((~checksum & 0xFFFF) - (checksum >> 16)));
}


static unsigned short tcpChecksum(networkIpHeader *ipHeader)
{
	// Calculate the TCP checksum for the supplied packet.  This is done
	// as a 1's complement sum of:
	//
	// "the 16 bit one's complement of the one's complement sum of all 16
	// bit words in the header and text.  If a segment contains an odd number
	// of header and text octets to be checksummed, the last octet is padded
	// on the right with zeros to form a 16 bit word for checksum purposes.
	// The pad is not transmitted as part of the segment.  While computing
	// the checksum, the checksum field itself is replaced with zeros.
	//
	// The checksum also covers a 96 bit pseudo header conceptually
	// prefixed to the TCP header:
	//
	//		 0      7 8     15 16    23 24    31
	//		+--------+--------+--------+--------+
	//		|           Source Address          |
	//		+--------+--------+--------+--------+
	//		|         Destination Address       |
	//		+--------+--------+--------+--------+
	//		|  zero  |  PTCL  |    TCP Length   |
	//		+--------+--------+--------+--------+
	//
	// The TCP Length is the TCP header length plus the data length in
	// octets (this is not an explicitly transmitted quantity, but is
	// computed), and it does not count the 12 octets of the pseudo header."

	unsigned checksum = 0;
	networkTcpHeader *tcpHeader = NULL;
	unsigned short tcpLength = 0;
	unsigned short *wordPtr = NULL;
	int count;

	tcpHeader = (((void *) ipHeader) +
		((ipHeader->versionHeaderLen & 0x0F) << 2));
	tcpLength = (processorSwap16(ipHeader->totalLength) -
		sizeof(networkIpHeader));

	// IP source and destination addresses
	wordPtr = (unsigned short *) ipHeader;
	for (count = 6; count < 10; count ++)
		checksum += processorSwap16(wordPtr[count]);

	// Protocol
	checksum += ipHeader->protocol;

	// TCP length
	checksum += tcpLength;

	// The TCP header and data
	wordPtr = (unsigned short *) tcpHeader;
	for (count = 0; count < (tcpLength / 2); count ++)
	{
		if (count != 8)
			// Skip the checksum field itself
			checksum += processorSwap16(wordPtr[count]);
	}

	if (tcpLength % 2)
		checksum += processorSwap16(wordPtr[tcpLength / 2] & 0x00FF);

	return ((unsigned short)((~checksum & 0xFFFF) - (checksum >> 16)));
}


static unsigned short udpChecksum(networkIpHeader *ipHeader)
{
	// Calculate the UDP checksum for the supplied packet.  This is done
	// as a 1's complement sum of:
	//
	// "a pseudo header of information from the IP header, the UDP header,
	// and the data, padded with zero octets at the end (if necessary) to make
	// a multiple of two octets.
	//
	// The pseudo  header  conceptually prefixed to the UDP header contains
	// the source address, the destination address, the protocol, and the UDP
	// length.  This information gives protection against misrouted datagrams.
	// This checksum procedure is the same as is used in TCP.
	//
	//		 0      7 8     15 16    23 24    31
	//		+--------+--------+--------+--------+
	//		|          source address           |
	//		+--------+--------+--------+--------+
	//		|        destination address        |
	//		+--------+--------+--------+--------+
	//		|  zero  |protocol|   UDP length    |
	//		+--------+--------+--------+--------+

	unsigned checksum = 0;
	networkUdpHeader *udpHeader = NULL;
	unsigned short udpLength = 0;
	unsigned short *wordPtr = NULL;
	int count;

	udpHeader = (((void *) ipHeader) +
		((ipHeader->versionHeaderLen & 0x0F) << 2));
	udpLength = processorSwap16(udpHeader->length);

	// IP source and destination addresses
	wordPtr = (unsigned short *) ipHeader;
	for (count = 6; count < 10; count ++)
		checksum += processorSwap16(wordPtr[count]);

	// Protocol
	checksum += ipHeader->protocol;

	// UDP length
	checksum += udpLength;

	// The UDP header and data
	wordPtr = (unsigned short *) udpHeader;
	for (count = 0; count < (udpLength / 2); count ++)
	{
		if (count != 3)
			// Skip the checksum field itself
			checksum += processorSwap16(wordPtr[count]);
	}

	if (udpLength % 2)
		checksum += processorSwap16(wordPtr[udpLength / 2] & 0x00FF);

	return ((unsigned short)((~checksum & 0xFFFF) - (checksum >> 16)));
}


static int prependEthernetHeader(kernelNetworkDevice *adapter,
	kernelNetworkPacket *packet)
{
	// Create the ethernet header for this packet and adjust the packet data
	// pointer and size appropriately

	int status = 0;
	networkEthernetHeader *header = (networkEthernetHeader *) packet->data;

	// If the IP destination address is broadcast, we make the ethernet
	// destination be broadcast as well
	if (networkAddressesEqual(&packet->destAddress, &broadcastAddress,
		NETWORK_ADDRLENGTH_IP))
	{
		// Destination is the ethernet broadcast address FF:FF:FF:FF:FF:FF
		memcpy(&header->dest, &broadcastAddress,
			NETWORK_ADDRLENGTH_ETHERNET);
	}
	else
	{
		// Get the 6-byte ethernet destination address
		status = kernelNetworkDeviceGetAddress((char *) adapter->device.name,
			&packet->destAddress, (networkAddress *) &header->dest);
		if (status < 0)
			// Can't find the destination host, we guess
			return (status);
	}

	// Copy the 6-byte ethernet source address
	memcpy(&header->source, (void *) &adapter->device.hardwareAddress,
		NETWORK_ADDRLENGTH_ETHERNET);

	// Always the same for our purposes
	header->type = processorSwap16(NETWORK_ETHERTYPE_IP);

	// Adjust the packet structure
	packet->linkHeader = header;
	packet->data += sizeof(networkEthernetHeader);
	packet->dataLength -= sizeof(networkEthernetHeader);

	// Data must fit within an ethernet frame
	if (packet->dataLength > NETWORK_MAX_ETHERDATA_LENGTH)
		packet->dataLength = NETWORK_MAX_ETHERDATA_LENGTH;

	return (status = 0);
}


static int prependIpHeader(kernelNetworkPacket *packet)
{
	// Create the IP header for this packet and adjust the packet data
	// pointer and size appropriately

	networkIpHeader *header = (networkIpHeader *) packet->data;

	// Version 4, header length 5 dwords
	header->versionHeaderLen = 0x45;
	// Type of service: Normal everything.  Routine = 000, delay = 0,
	// throughput = 0, reliability = 0
	header->typeOfService = 0;
	header->totalLength = processorSwap16(packet->dataLength);
	// Fragmentation allowed, but off by default
	header->flagsFragOffset = 0;
	header->timeToLive = 64;
	header->protocol = packet->transProtocol;
	// Wait until the end for the checksum.  Copy the source and destination
	// IP addresses
	memcpy(&header->srcAddress, &packet->srcAddress, 4);
	memcpy(&header->destAddress, &packet->destAddress, 4);
	// Do the checksum.
	header->headerChecksum = processorSwap16(ipChecksum(header));

	// Adjust the packet structure
	packet->netHeader = header;
	packet->data += sizeof(networkIpHeader);
	packet->dataLength -= sizeof(networkIpHeader);

	return (0);
}


static int prependTcpHeader(kernelNetworkPacket *packet)
{
	networkTcpHeader *header = (networkTcpHeader *) packet->data;
	unsigned short hdrSize = 20;

	header->srcPort = processorSwap16(packet->srcPort);
	header->destPort = processorSwap16(packet->destPort);
	// We have to defer the checksum and other things until later.
	header->ackNum = 0;
	networkSetTcpHdrSize(header, hdrSize);
	header->window = NETWORK_DATASTREAM_LENGTH;
	header->checksum = 0;
	header->urgentPointer = 0;

	// Adjust the packet structure
	packet->transHeader = header;
	packet->data += hdrSize;
	packet->dataLength -= hdrSize;

	return (0);
}


static int prependUdpHeader(kernelNetworkPacket *packet)
{
	networkUdpHeader *header = (networkUdpHeader *) packet->data;

	header->srcPort = processorSwap16(packet->srcPort);
	header->destPort = processorSwap16(packet->destPort);
	header->length = processorSwap16(packet->dataLength);
	// We have to defer the checksum until the data is in the packet.
	header->checksum = 0;

	// Adjust the packet structure
	packet->transHeader = header;
	packet->data += sizeof(networkUdpHeader);
	packet->dataLength -= sizeof(networkUdpHeader);

	return (0);
}


static int setupReceivedPacket(kernelNetworkPacket *packet)
{
	// This takes a semi-raw 'received' packet, as from the network adapter's
	// packet input stream, which must be already recognised/configured
	// appropriately by the link layer (currently, as an IP packet).  Tries
	// to interpret the rest and set up the remainder of the packet's fields.

	int status = 0;
	networkIpHeader *ipHeader = NULL;
	networkIcmpHeader *icmpHeader = NULL;
	networkTcpHeader *tcpHeader = NULL;
	networkUdpHeader *udpHeader = NULL;

	ipHeader = packet->netHeader;

	// Check the checksum
	if (processorSwap16(ipHeader->headerChecksum) !=
		ipChecksum(ipHeader))
	{
		kernelError(kernel_error, "IP header checksum mismatch");
		return (status = ERR_INVALID);
	}

	// Copy the source and destination addresses
	memcpy(&packet->srcAddress, &ipHeader->srcAddress, NETWORK_ADDRLENGTH_IP);
	memcpy(&packet->destAddress, &ipHeader->destAddress,
		NETWORK_ADDRLENGTH_IP);

	packet->transProtocol = ipHeader->protocol;

	// The rest depends upon the transport protocol
	switch (packet->transProtocol)
	{
		case NETWORK_TRANSPROTOCOL_ICMP:
			icmpHeader = packet->transHeader;

			// Check the checksum
			if (processorSwap16(icmpHeader->checksum) !=
				icmpChecksum(icmpHeader,
					(processorSwap16(ipHeader->totalLength) -
						sizeof(networkIpHeader))))
			{
				kernelError(kernel_error, "ICMP checksum mismatch");
				return (status = ERR_INVALID);
			}

			// Update the data pointer and length
			packet->data += sizeof(networkIcmpHeader);
			packet->dataLength -= sizeof(networkIcmpHeader);
			break;

		case NETWORK_TRANSPROTOCOL_TCP:
			tcpHeader = packet->transHeader;

			// Check the checksum
			if (processorSwap16(tcpHeader->checksum) !=
				tcpChecksum(ipHeader))
			{
				kernelError(kernel_error, "TCP header checksum mismatch");
				return (status = ERR_INVALID);
			}

			// Source and destination ports
			packet->srcPort = processorSwap16(tcpHeader->srcPort);
			packet->destPort = processorSwap16(tcpHeader->destPort);

			// Update the data pointer and length
			packet->data += networkGetTcpHdrSize(tcpHeader);
			packet->dataLength -= networkGetTcpHdrSize(tcpHeader);
			break;

		case NETWORK_TRANSPROTOCOL_UDP:
			udpHeader = packet->transHeader;

			// Check the checksum
			if (processorSwap16(udpHeader->checksum) !=
				udpChecksum(ipHeader))
			{
				kernelError(kernel_error, "UDP header checksum mismatch");
				return (status = ERR_INVALID);
			}

			// Source and destination ports
			packet->srcPort = processorSwap16(udpHeader->srcPort);
			packet->destPort = processorSwap16(udpHeader->destPort);

			// Update the data pointer and length
			packet->data += sizeof(networkUdpHeader);
			packet->dataLength -= sizeof(networkUdpHeader);
			break;

		default:
			kernelError(kernel_error, "Unsupported transport protocol %d",
				packet->transProtocol);
			return (status = ERR_NOTIMPLEMENTED);
	}

	return (status = 0);
}


static int setupSendPacket(kernelNetworkConnection *connection,
	kernelNetworkPacket *packet)
{
	// This takes a packet structure.

	int status = 0;

	memset(packet, 0, sizeof(kernelNetworkPacket));

	// Set up the packet info
	memcpy(&packet->srcAddress,
		(void *) &connection->adapter->device.hostAddress,
		sizeof(networkAddress));
	packet->srcPort = connection->filter.localPort;
	memcpy(&packet->destAddress, (void *) &connection->address,
		sizeof(networkAddress));
	packet->destPort = connection->filter.remotePort;
	// Get memory for the packet data
	packet->length = NETWORK_PACKET_MAX_LENGTH;
	packet->memory = kernelMalloc(packet->length);
	if (!packet->memory)
		return (status = ERR_MEMORY);
	packet->data = packet->memory;
	packet->dataLength = packet->length;

	// The packet's link protocol header is the protocol of the network adapter
	packet->linkProtocol = connection->adapter->device.linkProtocol;

	// Prepent the link protocol header
	switch (packet->linkProtocol)
	{
		case NETWORK_LINKPROTOCOL_ETHERNET:
			status = prependEthernetHeader(connection->adapter, packet);
			break;

		default:
			kernelError(kernel_error, "Device %s has an unknown link protocol "
				"%d", connection->adapter->device.name, packet->linkProtocol);
			status = ERR_INVALID;
	}

	if (status < 0)
	{
		kernelFree(packet->memory);
		return (status);
	}

	// Set the packet's transport protocol in case the network protocol needs
	// it to construct its header (a la the protocol field in an IP header)
	packet->transProtocol = connection->filter.transProtocol;

	// Prepend the network protocol header based on the requested transport
	// protocol
	switch (packet->transProtocol)
	{
		case NETWORK_TRANSPROTOCOL_ICMP:
		case NETWORK_TRANSPROTOCOL_UDP:
		case NETWORK_TRANSPROTOCOL_TCP:
			packet->netProtocol = NETWORK_NETPROTOCOL_IP;
			status = prependIpHeader(packet);
			break;

		default:
			kernelError(kernel_error, "Unknown transport protocol %d",
				connection->filter.transProtocol);
			status = ERR_INVALID;
			break;
	}

	if (status < 0)
	{
		kernelFree(packet->memory);
		return (status);
	}

	// Prepend the transport protocol header, if applicable
	switch (packet->transProtocol)
	{
		case NETWORK_TRANSPROTOCOL_TCP:
			status = prependTcpHeader(packet);
			break;

		case NETWORK_TRANSPROTOCOL_UDP:
			status = prependUdpHeader(packet);
			break;
	}

	if (status < 0)
		kernelFree(packet->memory);

	return (status);
}


static void finalizeSendPacket(kernelNetworkConnection *connection,
	kernelNetworkPacket *packet)
{
	// This does any required finalizing and checksumming of a packet before
	// it is to be sent.

	// If the network protocol needs to post-process the packet, do that
	// now

	if (packet->netProtocol == NETWORK_NETPROTOCOL_IP)
	{
		networkIpHeader *ipHeader = packet->netHeader;

		ipHeader->identification =
			processorSwap16(connection->ip.identification);
		connection->ip.identification += 1;

		// Make sure the length field matches the actual size of the
		// IP header+data
		ipHeader->totalLength =
			processorSwap16((((unsigned) packet->data -
				(unsigned) packet->netHeader) + packet->dataLength));

		ipHeader->headerChecksum =
			processorSwap16(ipChecksum(packet->netHeader));
	}

	// If the transport protocol needs to post-process the packet, do that
	// now

	if (packet->transProtocol == NETWORK_TRANSPROTOCOL_TCP)
	{
		networkTcpHeader *tcpHeader = packet->transHeader;

		tcpHeader->sequenceNum =
			processorSwap32(connection->tcp.sendNext);

		// Update the window size
		tcpHeader->window = processorSwap16(connection->inputStream.size -
			connection->inputStream.count);

		// Now we can do the checksum
		tcpHeader->checksum =
			processorSwap16(tcpChecksum(packet->netHeader));

		networkIpHeader *ipHeader = packet->netHeader;
		kernelTextPrintLine("IP packet length=%d dataLength=%d",
			processorSwap16(ipHeader->totalLength), packet->dataLength);
		kernelNetworkIpDebug(packet->netHeader);

		// If this is a SYN or FIN packet, advance the sequence number by 1
		int flags = networkGetTcpHdrFlags(tcpHeader);
		if ((flags & NETWORK_TCPFLAG_SYN) || (flags & NETWORK_TCPFLAG_FIN))
			connection->tcp.sendNext += 1;
		else
			connection->tcp.sendNext += packet->dataLength;
	}

	else if (packet->transProtocol == NETWORK_TRANSPROTOCOL_UDP)
	{
		networkUdpHeader *udpHeader = packet->transHeader;

		// Make sure the length field matches the actual size of the
		// UDP header+data

		udpHeader->length = processorSwap16(packet->length -
			(packet->transHeader - packet->memory));

		// Now we can do the checksum
		udpHeader->checksum =
			processorSwap16(udpChecksum(packet->netHeader));
	}
}


static void addTcpAck(kernelNetworkConnection *connection,
	kernelNetworkPacket *packet, unsigned ackNum)
{
	// Adds the specified ack to a packet.

	networkTcpHeader *packetHeader = packet->transHeader;
	unsigned flags = networkGetTcpHdrFlags(packetHeader);

	flags |= NETWORK_TCPFLAG_ACK;
	networkSetTcpHdrFlags(packetHeader, flags);
	packetHeader->ackNum = processorSwap32(ackNum + 1);

	// Update the last ack sent value
	connection->tcp.recvAcked = ackNum;

	return;
}


static int send(kernelNetworkConnection *connection, unsigned char *buffer,
	unsigned bufferSize, int immediate, int freeMem)
{
	// This is the "guts" function for sending network data.  The caller
	// specifies the adapter, the destination address and ports (if
	// applicable), transport-layer protocol, the raw data, and whether or not
	// the transmission should be immediate or queued.

	int status = 0;
	kernelNetworkPacket packet;
	unsigned maxDataLength = 0;
	int count;

	if (!bufferSize)
		// Nothing to do, we guess.  Should be an error, we suppose.
		return (status = ERR_NODATA);

	// Set up a basic packet structure we can use to iterate through the data.
	status = setupSendPacket(connection, &packet);
	if (status < 0)
		return (status);

	// Remember the max. length of data we can pack into this type of packet.
	maxDataLength = packet.dataLength;

	// The remaining packet data size is how much we can send per transmission,
	// So loop for each packet while there's still data in the buffer
	for (count = 0; (bufferSize > 0); count ++)
	{
		packet.dataLength = min(maxDataLength, bufferSize);

		// Copy in the packet data
		memcpy(packet.data, (buffer + (count * maxDataLength)),
			packet.dataLength);

		packet.length = (((unsigned) packet.data - (unsigned) packet.memory) +
			packet.dataLength);
		if (packet.length % 2)
			packet.length += 1;

		// If this is a TCP packet, add an ACK to it
		if (packet.transProtocol == NETWORK_TRANSPROTOCOL_TCP)
			addTcpAck(connection, &packet, connection->tcp.recvLast);

		// Finalize checksums, etc.
		finalizeSendPacket(connection, &packet);

		// Now, we either send it or queue it.
		if (immediate)
		{
			status = kernelNetworkDeviceSend((char *)
				connection->adapter->device.name, packet.memory,
				packet.length);
		}
		else
		{
			status = kernelNetworkPacketStreamWrite(
				&connection->adapter->outputStream, &packet);
		}

		if (status < 0)
			break;

		bufferSize -= packet.dataLength;
	}

	// Don't release the packet memory if the packets were queued, and only
	// if we were told to do so.
	if (immediate && freeMem)
		kernelFree(packet.memory);

	// Return the status from the last 'send' operation
	return (status);
}


static int openTcpConnection(kernelNetworkConnection *connection)
{
	// Given a connection structure, try to set up the TCP connection with
	// the destination host

	int status = 0;
	kernelNetworkPacket sendPacket;
	networkTcpHeader *sendTcpHeader = NULL;

	memset(&sendPacket, 0, sizeof(kernelNetworkPacket));

	// Set up a generic sending packet to use for intitializing
	status = setupSendPacket(connection, &sendPacket);
	if (status < 0)
		return (status);

	// No data in the packet
	sendPacket.length -= sendPacket.dataLength;
	sendPacket.dataLength = 0;

	sendTcpHeader = sendPacket.transHeader;

	while (connection->tcp.state != tcp_established)
	{
		// Get random numbers for any intitial TCP sequence numbers
		connection->tcp.sendNext = kernelRandomFormatted(0, 0x7FFFFFFF);
		connection->tcp.sendUnAcked = connection->tcp.sendNext;

		kernelTextPrintLine("Send SYN packet %x", connection->tcp.sendNext);

		// Send the initial SYN packet.
		networkSetTcpHdrFlags(sendTcpHeader, NETWORK_TCPFLAG_SYN);
		finalizeSendPacket(connection, &sendPacket);
		connection->tcp.state = tcp_syn_sent;
		status = kernelNetworkDeviceSend((char *)
			connection->adapter->device.name, sendPacket.memory,
			sendPacket.length);
		if (status < 0)
		{
			kernelFree(sendPacket.memory);
			return (status);
		}

		// Wait for the connection to become established.
		while (connection->tcp.state != tcp_established)
		{
			// If there was an old connection hanging about, the
			// processTcpPacket function will reset the connection and return
			// it to the closed state so we should resend
			if (connection->tcp.state == tcp_closed)
				break;

			kernelMultitaskerYield();
		}
	}

	kernelTextPrintLine("Opened TCP connection");

	kernelFree(sendPacket.memory);

	return (status = 0);
}


static int closeTcpConnection(kernelNetworkConnection *connection)
{
	// Given a connection structure, try to shut down the TCP connection with
	// the destination host

	int status = 0;
	kernelNetworkPacket sendPacket;
	networkTcpHeader *sendTcpHeader = NULL;

	if (connection->tcp.state == tcp_established)
	{
		// Set up a generic sending packet to use for intitializing
		status = setupSendPacket(connection, &sendPacket);
		if (status < 0)
			return (status);

		// No data in the packet
		sendPacket.length -= sendPacket.dataLength;
		sendPacket.dataLength = 0;

		sendTcpHeader = sendPacket.transHeader;

		// Send the FIN packet.
		networkSetTcpHdrFlags(sendTcpHeader, NETWORK_TCPFLAG_FIN);
		addTcpAck(connection, &sendPacket, connection->tcp.recvLast);
		finalizeSendPacket(connection, &sendPacket);
		kernelTextPrintLine("Send FIN packet");
		connection->tcp.state = tcp_fin_wait1;
		status = kernelNetworkDeviceSend((char *)
			connection->adapter->device.name, sendPacket.memory,
			sendPacket.length);
		if (status < 0)
		{
			kernelFree(sendPacket.memory);
			return (status);
		}

		kernelFree(sendPacket.memory);

		// Wait for the connection to be closed
		while (connection->tcp.state != tcp_closed)
			kernelMultitaskerYield();
	}

	connection->tcp.state = tcp_closed;

	kernelTextPrintLine("Closed TCP connection");

	return (status = 0);
}


static int ipPortInUse(kernelNetworkDevice *adapter, int portNumber)
{
	// Returns 1 if there is a connection using the specified local IP port
	// number

	kernelNetworkConnection *connection = adapter->connections;

	while (connection)
	{
		if (connection->filter.localPort == portNumber)
			return (0);

		connection = connection->next;
	}

	return (0);
}


static kernelNetworkConnection *connectionOpen(kernelNetworkDevice *adapter,
	int mode, networkAddress *address, networkFilter *filter)
{
	// This function opens up a connection.  A connection is necessary for
	// nearly any kind of network communication.  Thus, there will eventually
	// be plenty of checking here before we allow the connection.

	kernelNetworkConnection *connection = NULL;

	if (!adapter)
	{
		// Find the network adapter that's suitable for this destination
		// address
		adapter = getDevice(address);
		if (!adapter)
		{
			kernelError(kernel_error, "No appropriate adapter for desintation "
				"address");
			return (connection = NULL);
		}
	}

	// Allocate memory for the connection structure
	connection = kernelMalloc(sizeof(kernelNetworkConnection));
	if (!connection)
		return (connection);

	// Set the process ID and mode
	connection->processId = kernelMultitaskerGetCurrentProcessId();
	connection->mode = mode;

	// If the network address was specified, copy it.
	if (address)
		memcpy((void *) &connection->address, address, sizeof(networkAddress));

	// If this is an IP connection, check/find the local port number
	if (filter->netProtocol == NETWORK_NETPROTOCOL_IP)
	{
		// If a local port number has been specified, make sure it is not in
		// use
		if (filter->localPort)
		{
			if (ipPortInUse(adapter, filter->localPort))
			{
				kernelError(kernel_error, "Local IP port %d is in use",
					filter->localPort);
				kernelFree((void *) connection);
				return (connection = NULL);
			}
		}
		else
		{
			// Find a port that is free
			while (!(filter->localPort) ||
				ipPortInUse(adapter, filter->localPort))
			filter->localPort = kernelRandomFormatted(1025, 0xFFFF);
		}
	}

	// Copy the network filter
	memcpy((void *) &connection->filter, filter, sizeof(networkFilter));

	// The ID of the IP packets is the lower 16 bits of the connection pointer
	connection->ip.identification = (((unsigned) connection) >> 16);

	// Set the TCP state to closed
	connection->tcp.state = tcp_closed;
	if ((connection->filter.transProtocol == NETWORK_TRANSPROTOCOL_TCP) &&
		(mode & NETWORK_MODE_LISTEN))
	{
		connection->tcp.state = tcp_listen;
	}

	// Add the connection to the adapter's list
	connection->adapter = adapter;
	connection->next = adapter->connections;
	adapter->connections = connection;

	return (connection);
}


static int connectionClose(kernelNetworkConnection *connection)
{
	// Closes and deallocates the specified network connection.

	int status = 0;

	// Remove the connection from the adapter's list.

	if (connection->adapter->connections == connection)
		connection->adapter->connections = connection->next;

	if (connection->next)
		connection->next->previous = connection->previous;
	if (connection->previous)
		connection->previous->next = connection->next;

	// Deallocate it
	kernelFree((void *) connection);

	return (status = 0);
}


static int waitDhcpReply(kernelNetworkDevice *adapter,
	kernelNetworkPacket *packet)
{
	// Wait for a DHCP packet to appear in our input queue

	int status = 0;
	uquad_t timeout = (kernelCpuGetMs() + 1500);
	kernelDhcpPacket *dhcpPacket = NULL;

	// Time out after ~1.5 seconds
	while (kernelCpuGetMs() <= timeout)
	{
		if (adapter->inputStream.count < (sizeof(kernelNetworkPacket) /
			sizeof(unsigned)))
		{
			continue;
		}

		// Read the packet from the stream
		status = kernelNetworkPacketStreamRead(&adapter->inputStream, packet);
		if (status < 0)
			continue;

		// It should be an IP packet
		if (packet->netProtocol != NETWORK_NETPROTOCOL_IP)
			continue;

		//kernelNetworkIpDebug(packet->netHeader);

		// Set up the received packet for further interpretation
		status = setupReceivedPacket(packet);
		if (status < 0)
			continue;

		// See if the input and output ports are appropriate for BOOTP/DHCP
		if ((packet->srcPort != NETWORK_PORT_BOOTPSERVER) ||
			(packet->destPort != NETWORK_PORT_BOOTPCLIENT))
		{
			continue;
		}

		dhcpPacket = (kernelDhcpPacket *) packet->data;

		// Check for DHCP cookie
		if (processorSwap32(dhcpPacket->cookie) != NETWORK_DHCP_COOKIE)
			continue;

		// Looks okay to us
		return (status = 0);
	}

	// No response from the server
	return (status = ERR_NODATA);
}


static kernelDhcpOption *getDhcpOption(kernelDhcpPacket *packet, int idx)
{
	// Returns the indexed DHCP option

	kernelDhcpOption *option = (kernelDhcpOption *) packet->options;
	int count;

	// Loop through the options until we get to the one that's wanted
	for (count = 0; count < idx; count ++)
	{
		if (option->code == NETWORK_DHCPOPTION_END)
			// Because 'count' is less than the requested index, the caller is
			// requesting an option that doesn't exist
			return (option = NULL);

		option = ((void *) option + 2 + option->length);
	}

	return (option);
}


static kernelDhcpOption *getSpecificDhcpOption(kernelDhcpPacket *packet,
	unsigned char optionNumber)
{
	// Returns the requested option, if present.

	kernelDhcpOption *option = (kernelDhcpOption *) packet->options;
	int count;

	// Loop through the options until we either get the requested one, or else
	// hit the end of the list
	for (count = 0; ; count ++)
	{
		if (option->code == optionNumber)
			return (option);

		if (option->code == NETWORK_DHCPOPTION_END)
			return (option = NULL);

		option = ((void *) option + 2 + option->length);
	}

	return (option = NULL);
}


static void setDhcpOption(kernelDhcpPacket *packet, int code, int length,
	unsigned char *data)
{
	// Adds the supplied DHCP option to the packet

	kernelDhcpOption *option = NULL;
	int count;

	// Check whether the option is already present
	if (!(option = getSpecificDhcpOption(packet, code)))
	{
		// Not present.
		option = (kernelDhcpOption *) packet->options;

		// Loop through the options until we find the end
		while (1)
		{
			if (option->code == NETWORK_DHCPOPTION_END)
				break;

			option = ((void *) option + 2 + option->length);
		}
	}

	option->code = code;
	option->length = length;
	for (count = 0; count < length; count ++)
		option->data[count] = data[count];
	option->data[length] = NETWORK_DHCPOPTION_END;

	return;
}


static int configureDhcp(kernelNetworkDevice *adapter, unsigned timeout)
{
	// This function attempts to configure the supplied adapter via the DHCP
	// protocol.  The adapter needs to be stopped since it expects to be able
	// to poll the adapter's packet input stream, and not be interfered with
	// by the network thread.

	int status = 0;
	networkFilter filter;
	kernelNetworkConnection *connection = NULL;
	unsigned startTime = kernelRtcUptimeSeconds();
	kernelDhcpPacket sendDhcpPacket;
	kernelNetworkPacket packet;
	kernelDhcpOption *option = NULL;
	kernelDhcpPacket *recvDhcpPacket = NULL;
	int count;

	// Make sure the adapter is stopped, and yield the timeslice to make sure
	// the network thread is not in the middle of anything
	adapter->device.flags &= ~NETWORK_ADAPTERFLAG_RUNNING;
	kernelMultitaskerYield();

	// Get a connection for sending and receiving
	memset(&filter, 0, sizeof(networkFilter));
	filter.transProtocol = NETWORK_TRANSPROTOCOL_UDP;
	filter.localPort = NETWORK_PORT_BOOTPCLIENT;
	filter.remotePort = NETWORK_PORT_BOOTPSERVER;
	connection = connectionOpen(adapter, NETWORK_MODE_WRITE, &broadcastAddress,
		&filter);
	if (!connection)
		return (status = ERR_INVALID);

	// If this adapter already has an existing DHCP configuration,
	// attempt to renew it.
	if (adapter->device.flags & NETWORK_ADAPTERFLAG_AUTOCONF)
	{
		memcpy(&sendDhcpPacket, (void *) &adapter->dhcpConfig.dhcpPacket,
			sizeof(kernelDhcpPacket));
	}
	else
	{
	sendDhcpDiscover:
		// The code jumps back to here if our DHCP request response to a DHCP
		// offer fails (for example if the server subsequently gives the address
		// to someone else)

		// Clear our packet
		memset(&sendDhcpPacket, 0, sizeof(kernelDhcpPacket));

		// Set up our DCHP payload

		// Opcode is boot request
		sendDhcpPacket.opCode = NETWORK_DHCPOPCODE_BOOTREQUEST;
		// Hardware address space is ethernet=1
		sendDhcpPacket.hardwareType = NETWORK_DHCPHARDWARE_ETHERNET;
		sendDhcpPacket.hardwareAddrLen = NETWORK_ADDRLENGTH_ETHERNET;
		sendDhcpPacket.transactionId =
		processorSwap32(kernelRandomUnformatted());
		// Our ethernet hardware address
		memcpy(&sendDhcpPacket.clientHardwareAddr,
			(void *) &adapter->device.hardwareAddress,
			NETWORK_ADDRLENGTH_ETHERNET);
		// Magic DHCP cookie
		sendDhcpPacket.cookie = processorSwap32(NETWORK_DHCP_COOKIE);
		// Options.  First one is mandatory message type
		sendDhcpPacket.options[0] = NETWORK_DHCPOPTION_END;
		setDhcpOption(&sendDhcpPacket, NETWORK_DHCPOPTION_MSGTYPE, 1,
			(unsigned char[]){ NETWORK_DHCPMSG_DHCPDISCOVER });
		// Request an infinite lease time
		unsigned tmpLeaseTime = 0xFFFFFFFF;
		setDhcpOption(&sendDhcpPacket, NETWORK_DHCPOPTION_LEASETIME, 4,
			(unsigned char *) &tmpLeaseTime);
		// Request some paramters
		setDhcpOption(&sendDhcpPacket, NETWORK_DHCPOPTION_PARAMREQ, 7,
			(unsigned char[]){ NETWORK_DHCPOPTION_SUBNET,
				NETWORK_DHCPOPTION_ROUTER,
				NETWORK_DHCPOPTION_DNSSERVER,
				NETWORK_DHCPOPTION_HOSTNAME,
				NETWORK_DHCPOPTION_DOMAIN,
				NETWORK_DHCPOPTION_BROADCAST,
				NETWORK_DHCPOPTION_LEASETIME });

		status = send(connection, (unsigned char *) &sendDhcpPacket,
			sizeof(kernelDhcpPacket), 1, 1);
		if (status < 0)
		{
			connectionClose(connection);
			return (status);
		}

	waitDhcpOffer:
		// The code jumps back to here if the DHCP packet we receive isn't the
		// one we were waiting for

		// Wait for a DHCP server reply
		status = waitDhcpReply(adapter, &packet);
		if (status < 0)
		{
			if (kernelRtcUptimeSeconds() <= (startTime + timeout))
			{
				// Not timed out.  Try starting from scratch
				goto sendDhcpDiscover;
			}
			else
			{
				// No DHCP for you
				kernelError(kernel_error, "Auto-configuration of network "
					"adapter %s timed out", adapter->device.name);
				connectionClose(connection);
				return (status);
			}
		}

		recvDhcpPacket = (kernelDhcpPacket *) packet.data;

		// Should be a DHCP reply, and the first option should be a DHCP "offer"
		// message type
		if ((recvDhcpPacket->opCode != NETWORK_DHCPOPCODE_BOOTREPLY) ||
			(getDhcpOption(recvDhcpPacket, 0)->data[0] !=
				NETWORK_DHCPMSG_DHCPOFFER))
		{
			kernelFree(packet.memory);
			goto waitDhcpOffer;
		}

		// Good enough.  Send the reply back to the server as a DHCP request.

		// Copy the supplied data into our send packet
		memcpy(&sendDhcpPacket, recvDhcpPacket, sizeof(kernelDhcpPacket));

		// Free the old packet data memory.
		kernelFree(packet.memory);
	}

	// Re-set the message type
	sendDhcpPacket.opCode = NETWORK_DHCPOPCODE_BOOTREQUEST;
	sendDhcpPacket.options[2] = NETWORK_DHCPMSG_DHCPREQUEST;

	// Add an option to request the supplied address
	setDhcpOption(&sendDhcpPacket, NETWORK_DHCPOPTION_ADDRESSREQ,
		NETWORK_ADDRLENGTH_IP, (void *) &sendDhcpPacket.yourLogicalAddr);

	// If the server did not specify a host name to us, specify one to it.
	if (!getSpecificDhcpOption(&sendDhcpPacket, NETWORK_DHCPOPTION_HOSTNAME))
		setDhcpOption(&sendDhcpPacket, NETWORK_DHCPOPTION_HOSTNAME,
			(strlen(hostName) + 1), (unsigned char *) hostName);

	// If the server did not specify a domain name to us, specify one to it.
	if (!getSpecificDhcpOption(&sendDhcpPacket, NETWORK_DHCPOPTION_DOMAIN))
		setDhcpOption(&sendDhcpPacket, NETWORK_DHCPOPTION_DOMAIN,
			(strlen(domainName) + 1), (unsigned char *) domainName);

	// Clear the 'your address' field
	memset(&sendDhcpPacket.yourLogicalAddr, 0, NETWORK_ADDRLENGTH_IP);

	status = send(connection, (unsigned char *) &sendDhcpPacket,
		sizeof(kernelDhcpPacket), 1, 1);
	if (status < 0)
	{
		connectionClose(connection);
		return (status);
	}

waitDhcpAck:
	// The code jumps back to here if the packet we receive is not the DHCP
	// ACK we were expecting

	// Wait for a DHCP ACK
	status = waitDhcpReply(adapter, &packet);
	if (status < 0)
	{
		if (kernelRtcUptimeSeconds() <= (startTime + timeout))
		{
			// Not timed out.  Try starting from scratch
			goto sendDhcpDiscover;
		}
		else
		{
			// No DHCP for you
			kernelError(kernel_error, "Auto-configuration of network "
				"adapter %s timed out", adapter->device.name);
			connectionClose(connection);
			return (status);
		}
	}

	recvDhcpPacket = (kernelDhcpPacket *) packet.data;

	// Should be a DHCP reply, and the first option should be a DHCP ACK
	// message type.  If the reply is a DHCP NACK, then perhaps the
	// previously-supplied address has already been allocated to someone else.
	// Shouldn't happen.  But possible we suppose.
	if ((recvDhcpPacket->opCode != NETWORK_DHCPOPCODE_BOOTREPLY) ||
		(getDhcpOption(recvDhcpPacket, 0)->data[0] != NETWORK_DHCPMSG_DHCPACK))
	{
		if (getDhcpOption(recvDhcpPacket, 0)->data[0] ==
			NETWORK_DHCPMSG_DHCPNAK)
		{
			// Start again.
			kernelFree(packet.memory);
			goto sendDhcpDiscover;
		}
		else
		{
			kernelFree(packet.memory);
			goto waitDhcpAck;
		}
	}

	// Okay, communication should be finished.  Gather up the information.

	// Copy the host address
	memcpy((void *) &adapter->device.hostAddress,
		&recvDhcpPacket->yourLogicalAddr, NETWORK_ADDRLENGTH_IP);

	// Loop through all of the options
	for (count = 0; ; count ++)
	{
		option = getDhcpOption(recvDhcpPacket, count);

		if (option->code == NETWORK_DHCPOPTION_END)
			// That's the end of the options
			break;

		// Look for the options we desired
		switch (option->code)
		{
			case NETWORK_DHCPOPTION_SUBNET:
				// The server supplied the subnet mask
				memcpy((void *) &adapter->device.netMask, option->data,
					min(option->length, NETWORK_ADDRLENGTH_IP));
				break;

			case NETWORK_DHCPOPTION_ROUTER:
				// The server supplied the gateway address
				memcpy((void *) &adapter->device.gatewayAddress,
					option->data, min(option->length, NETWORK_ADDRLENGTH_IP));
				break;

			case NETWORK_DHCPOPTION_DNSSERVER:
				// The server supplied the DNS server address
				break;

			case NETWORK_DHCPOPTION_HOSTNAME:
				// The server supplied the host name
				strncpy(hostName, (char *) option->data,
					min(option->length, (NETWORK_MAX_HOSTNAMELENGTH - 1)));
				break;

			case NETWORK_DHCPOPTION_DOMAIN:
				// The server supplied the domain name
				strncpy(domainName, (char *) option->data,
					min(option->length, (NETWORK_MAX_DOMAINNAMELENGTH - 1)));
				break;

			case NETWORK_DHCPOPTION_BROADCAST:
				// The server supplied the broadcast address
				memcpy((void *) &adapter->device.broadcastAddress,
					option->data, min(option->length, NETWORK_ADDRLENGTH_IP));
				break;

			case NETWORK_DHCPOPTION_LEASETIME:
				// The server specified the lease time
				adapter->dhcpConfig.leaseExpiry =
					(kernelRtcUptimeSeconds() +
				 		processorSwap32(*((unsigned * ) option->data)));
				//kernelTextPrintLine("Lease expiry at %d seconds",
				//	adapter->dhcpConfig.leaseExpiry);
				break;

			default:
				// Unknown/unwanted information
				break;
		}
	}

	// Copy the DHCP packet into our config structure, so that we can renew,
	// release, etc., the configuration later
	memcpy((void *) &adapter->dhcpConfig.dhcpPacket, recvDhcpPacket,
		sizeof(kernelDhcpPacket));

	kernelFree(packet.memory);

	// Set the adapter 'auto config' flag and mark it as running
	adapter->device.flags |= (NETWORK_ADAPTERFLAG_RUNNING |
		NETWORK_ADAPTERFLAG_AUTOCONF);

	connectionClose(connection);
	return (status = 0);
}


static int releaseDhcp(kernelNetworkDevice *adapter)
{
	// Tell the DHCP server we're finished with our lease

	int status = 0;
	networkFilter filter;
	kernelNetworkConnection *connection = NULL;
	kernelDhcpPacket sendDhcpPacket;

	// Get a connection for sending and receiving
	memset(&filter, 0, sizeof(networkFilter));
	filter.transProtocol = NETWORK_TRANSPROTOCOL_UDP;
	filter.localPort = NETWORK_PORT_BOOTPCLIENT;
	filter.remotePort = NETWORK_PORT_BOOTPSERVER;
	connection = connectionOpen(adapter, NETWORK_MODE_WRITE, &broadcastAddress,
		&filter);
	if (!connection)
		return (status = ERR_INVALID);

	// Copy the saved configuration data into our send packet
	memcpy(&sendDhcpPacket, (void *) &adapter->dhcpConfig.dhcpPacket,
		sizeof(kernelDhcpPacket));

	// Re-set the message type
	sendDhcpPacket.opCode = NETWORK_DHCPOPCODE_BOOTREQUEST;
	sendDhcpPacket.options[2] = NETWORK_DHCPMSG_DHCPRELEASE;

	// Send it.
	status = send(connection, (unsigned char *) &sendDhcpPacket,
		sizeof(kernelDhcpPacket), 1, 1);

	connectionClose(connection);
	return (status);
}


static kernelNetworkConnection *findMatchFilter(
	kernelNetworkConnection *connection, kernelNetworkPacket *packet)
{
	// Given a starting connection, loop through them until we find one whose
	// filter matches the supplied packet.

	networkIcmpHeader *icmpHeader = packet->transHeader;

	while (connection)
	{
		if (connection->filter.linkProtocol &&
			(packet->linkProtocol != connection->filter.linkProtocol))
		{
			connection = connection->next;
			continue;
		}

		if (connection->filter.netProtocol &&
			(packet->netProtocol != connection->filter.netProtocol))
		{
			connection = connection->next;
			continue;
		}

		if (connection->filter.transProtocol &&
			(packet->transProtocol != connection->filter.transProtocol))
		{
			connection = connection->next;
			continue;
		}

		if (connection->filter.subProtocol)
		{
			switch (connection->filter.transProtocol)
			{
				case NETWORK_TRANSPROTOCOL_ICMP:
					if (icmpHeader->type != connection->filter.subProtocol)
					{
						connection = connection->next;
						continue;
					}
			}
		}

		if (connection->filter.localPort &&
			(packet->destPort != connection->filter.localPort))
		{
			connection = connection->next;
			continue;
		}

		// The packet patches the filter.
		return (connection);
	}

	// If we fall through, we found none.
	return (connection = NULL);
}


static int matchFilters(kernelNetworkDevice *adapter,
	kernelNetworkPacket *packet)
{
	// Given an adapter and a packet, match the packet to any applicable
	// filters in the list of connections and write the packet data for any
	// matches into the connections' input streams.

	int matchedFilters = 0;
	kernelNetworkConnection *connection = NULL;
	void *copyPtr = NULL;
	unsigned length = 0;

	connection = findMatchFilter(adapter->connections, packet);

	while (connection)
	{
		matchedFilters += 1;

		// If it's not a 'read' connection with an input stream, skip the rest.
		if (!(connection->mode & NETWORK_MODE_READ) ||
			!connection->inputStream.buffer)
		{
			connection = findMatchFilter(connection->next, packet);
			continue;
		}

		// Copy the packet into the input stream.

		copyPtr = packet->data;
		length = packet->dataLength;

		if (connection->filter.headers == NETWORK_HEADERS_RAW)
		{
			copyPtr = packet->linkHeader;
			length += ((unsigned) packet->data - (unsigned) packet->linkHeader);
		}
		else if (connection->filter.headers == NETWORK_HEADERS_NET)
		{
			copyPtr = packet->netHeader;
			length += ((unsigned) packet->data - (unsigned) packet->netHeader);
		}
		else if (connection->filter.headers == NETWORK_HEADERS_TRANSPORT)
		{
			copyPtr = packet->transHeader;
			length += ((unsigned) packet->data -
				(unsigned) packet->transHeader);
		}

		connection->inputStream.appendN(&connection->inputStream, length,
			copyPtr);

		connection = findMatchFilter(connection->next, packet);
	}

	return (matchedFilters);
}


static void processIcmpPacket(kernelNetworkDevice *adapter,
	kernelNetworkPacket *packet)
{
	// Take the appropriate action for whatever ICMP message we received.

	networkFilter filter;
	kernelNetworkConnection *connection = NULL;
	networkIpHeader *ipHeader = packet->netHeader;
	networkIcmpHeader *icmpHeader = packet->transHeader;
	unsigned short checksum = 0;

	switch (icmpHeader->type)
	{
		case NETWORK_ICMP_ECHO:
			// This is a ping.  We change the type to an 'echo reply' (ping
			// reply), recompute the checksum, and send it back.

			// Get a connection for sending
			memset(&filter, 0, sizeof(networkFilter));
			filter.transProtocol = NETWORK_TRANSPROTOCOL_ICMP;
			connection = connectionOpen(adapter, NETWORK_MODE_WRITE,
				(networkAddress *) &ipHeader->srcAddress, &filter);
			if (!connection)
				return;
			icmpHeader->type = NETWORK_ICMP_ECHOREPLY;
			checksum = icmpChecksum(icmpHeader,
				(processorSwap16(ipHeader->totalLength) -
					sizeof(networkIpHeader)));
			icmpHeader->checksum = processorSwap16(checksum);
			// Send, but only queue it for output so that ICMP packets don't
			// tie up the processing of the input queue
			send(connection, (void *) icmpHeader,
				(processorSwap16(ipHeader->totalLength) -
					sizeof(networkIpHeader)), 0, 0);
			connectionClose(connection);
			break;

		default:
			// Not supported yet, or we don't deal with it here.  Not an error
			// or anything.
			break;
	}
}


static int sendTcpAck(kernelNetworkConnection *connection, unsigned ackNum)
{
	// Send an ack with the given sequence number

	int status = 0;
	kernelNetworkPacket ackPacket;

	memset(&ackPacket, 0, sizeof(kernelNetworkPacket));

	status = setupSendPacket(connection, &ackPacket);
	if (status < 0)
		return (status);

	// No data in the packet
	ackPacket.length -= ackPacket.dataLength;
	ackPacket.dataLength = 0;

	kernelTextPrintLine("Send pure ACK for packet %x", ackNum);

	addTcpAck(connection, &ackPacket, ackNum);
	finalizeSendPacket(connection, &ackPacket);

	status = kernelNetworkDeviceSend((char *) connection->adapter->device.name,
		ackPacket.memory, ackPacket.length);

	kernelFree(ackPacket.memory);

	return (status);
}


static void sendTcpReset(kernelNetworkDevice *adapter,
	kernelNetworkPacket *packet)
{
	// Given an incoming, errant packet, send a TCP reset for it.

	kernelNetworkConnection connection;
	kernelNetworkPacket resetPacket;
	networkIpHeader *packetIpHeader = packet->netHeader;
	networkTcpHeader *packetTcpHeader = packet->transHeader;

	memset((void *) &connection, 0, sizeof(kernelNetworkConnection));
	memset(&resetPacket, 0, sizeof(kernelNetworkPacket));

	// Fabricate a partial connection based on this packet
	connection.mode = NETWORK_MODE_WRITE;
	memcpy((void *) &connection.address, &packet->srcAddress,
		NETWORK_ADDRLENGTH_IP);
	connection.filter.linkProtocol = packet->linkProtocol;
	connection.filter.netProtocol = packet->netProtocol;
	connection.filter.transProtocol = packet->transProtocol;
	connection.filter.localPort = packet->destPort;
	connection.filter.remotePort = packet->srcPort;
	connection.adapter = adapter;
	connection.ip.identification =
		processorSwap16(packetIpHeader->identification);
	connection.tcp.sendNext = processorSwap32(packetTcpHeader->ackNum);

	if (setupSendPacket(&connection, &resetPacket) < 0)
		return;

	// No data in the packet
	resetPacket.length -= resetPacket.dataLength;
	resetPacket.dataLength = 0;

	// Set the reset flag.
	networkSetTcpHdrFlags((networkTcpHeader *) resetPacket.transHeader,
		NETWORK_TCPFLAG_RST);

	kernelTextPrintLine("Send RST for packet from %d.%d.%d.%d",
		connection.address.bytes[0], connection.address.bytes[1],
		connection.address.bytes[2], connection.address.bytes[3]);

	finalizeSendPacket(&connection, &resetPacket);

	kernelNetworkDeviceSend((char *) adapter->device.name, resetPacket.memory,
		resetPacket.length);

	kernelFree(resetPacket.memory);

	return;
}


static int processTcpPacket(kernelNetworkDevice *adapter,
	kernelNetworkPacket *packet)
{
	// This function does any required TCP state transitions and ACKs appropriate
	// to this packet and any filters that match it.

	int status = 0;
	kernelNetworkConnection *connection = NULL;
	networkIpHeader *ipHeader = packet->netHeader;
	networkTcpHeader *tcpHeader = packet->transHeader;
	int sendAck = 0;
	int flags = 0;
	unsigned sequenceNum, ackNum;
	unsigned short window;

	flags = networkGetTcpHdrFlags(tcpHeader);
	sequenceNum = processorSwap32(tcpHeader->sequenceNum);
	ackNum = (processorSwap32(tcpHeader->ackNum) - 1);
	window = processorSwap16(tcpHeader->window);
	kernelTextPrintLine("IP packet length=%d dataLength=%d",
		processorSwap16(ipHeader->totalLength), packet->dataLength);
	kernelNetworkIpDebug(packet->netHeader);

	// Find a connection that matches this packet

	connection = findMatchFilter(adapter->connections, packet);
	if (!connection)
	{
		// This packet is bogus for our current list of connection.  Send a
		// reset packet
		sendTcpReset(adapter, packet);
		return (status = ERR_RANGE);
	}

	while (connection)
	{
		// See if the packet's sequence numbers fall within the acceptable
		// ranges.
		if (((connection->tcp.state == tcp_established) &&
				((sequenceNum < connection->tcp.recvLast) ||
				(sequenceNum > (connection->tcp.recvLast + window)))) ||
			((ackNum < connection->tcp.sendUnAcked) ||
		 		(ackNum > connection->tcp.sendNext)))
		{
			// This seems to be a packet for an old, dead connection.  If we
			// are trying to establish a connection right now, we
			kernelTextPrintLine("Received out-of-sequence packet %x %x",
				connection->tcp.sendUnAcked, connection->tcp.sendNext);
			if (!(flags & NETWORK_TCPFLAG_RST))
			{
				kernelTextPrintLine("Resetting");
				sendTcpReset(adapter, packet);
			}
			connection->tcp.state = tcp_closed;

			return (status = ERR_RANGE);
		}

		sendAck = 0;

		// Update the last ack received and window size values
		connection->tcp.recvLast = sequenceNum;
		connection->tcp.recvWindow = window;

		// If the packet contains a SYN and this connection is waiting for one..
		if ((flags & NETWORK_TCPFLAG_SYN) &&
			(connection->tcp.state == tcp_syn_sent))
		{
			kernelTextPrintLine("Received SYN packet %x", sequenceNum);
			connection->tcp.state = tcp_syn_received;
			sendAck = 1;
		}

		// If the packet contains an ACK, update the packet window.
		if (flags & NETWORK_TCPFLAG_ACK)
		{
			connection->tcp.sendUnAcked = ackNum;
			kernelTextPrintLine("Received ACK packet %x for %x", sequenceNum,
				ackNum);

			if (connection->tcp.state == tcp_fin_wait1)
			{
				kernelTextPrintLine("Received FIN ACK packet 1");
				connection->tcp.state = tcp_fin_wait2;
			}
			else if (connection->tcp.state == tcp_closing)
			{
				kernelTextPrintLine("Received FIN ACK packet 2");
				connection->tcp.state = tcp_time_wait;
				// Probably need to do something else here
				connection->tcp.state = tcp_closed;
			}
		}

		// If the packet contains a FIN...
		if (flags & NETWORK_TCPFLAG_FIN)
		{
			kernelTextPrintLine("Received FIN packet");

			if (connection->tcp.state == tcp_established)
			{
				connection->tcp.state = tcp_close_wait;
				sendAck = 1;
			}
			else if ((connection->tcp.state == tcp_fin_wait1) ||
				(connection->tcp.state == tcp_fin_wait2))
			{
				sendAck = 1;
			}
		}

		// Do we need to send a return ACK?
		if (sendAck)
		{
			if (sendTcpAck(connection, sequenceNum) < 0)
			{
				kernelTextPrintLine("ACK send failed");
				connection = findMatchFilter(connection->next, packet);
				continue;
			}

			if (connection->tcp.state == tcp_fin_wait1)
			{
				kernelTextPrintLine("TCP connection FIN ACK sent 1");
				connection->tcp.state = tcp_closing;
			}
			else if (connection->tcp.state == tcp_fin_wait2)
			{
				kernelTextPrintLine("TCP connection FIN ACK sent 2");
				connection->tcp.state = tcp_time_wait;
				connection->tcp.state = tcp_closed;
			}
		}

		// If we've received a SYN, and the packet contained an ack, then
		// our connection is established.
		if ((flags & NETWORK_TCPFLAG_ACK) &&
			(connection->tcp.state == tcp_syn_received))
		{
			kernelTextPrintLine("Received SYN ACK, TCP connection "
				"established");
			connection->tcp.state = tcp_established;
		}

		connection = findMatchFilter(connection->next, packet);
	}

	return (status = 0);
}


static void networkThread(void)
{
	// This is the thread that processes our raw kernel packet input and
	// output streams

	int status = 0;
	kernelNetworkDevice *adapter = NULL;
	kernelNetworkPacket packet;
	int count;

	while (!networkStop)
	{
		// Loop for each adapter
		for (count = 0; count < numDevices; count ++)
		{
			adapter = adapters[count];

			if (!(adapter->device.flags & NETWORK_ADAPTERFLAG_RUNNING))
				continue;

			// If the adapter is dynamically configured, and there are fewer
			// than 60 seconds remaining on the lease, try to renew it
			if ((adapter->device.flags & NETWORK_ADAPTERFLAG_AUTOCONF) &&
				((int) kernelRtcUptimeSeconds() >=
					(adapter->dhcpConfig.leaseExpiry - 60)))
			{
				status = configureDhcp(adapter, NETWORK_DHCP_DEFAULT_TIMEOUT);
				if (status < 0)
				{
					kernelError(kernel_error, "Attempt to renew DHCP "
						"configuration of network adapter %s failed",
						adapter->device.name);
					// Turn it off, sorry.
					adapter->device.flags &= ~NETWORK_ADAPTERFLAG_RUNNING;
					continue;
				}

				kernelLog("Renewed DHCP configuration for network adapter %s",
					adapter->device.name);
			}

			// Process the adapter's input packet stream.

			while (adapter->inputStream.count >=
				(sizeof(kernelNetworkPacket) / sizeof(unsigned)))
			{
				// Try to get a lock on the stream
				if (kernelLockGet(&adapter->inputStreamLock) < 0)
					// Stop for this timeslice
					break;

				status = kernelNetworkPacketStreamRead(&adapter->inputStream,
					&packet);

				kernelLockRelease(&adapter->inputStreamLock);

				if (status < 0)
					// Eh.  Dunno.  Stop for this timeslice.
					break;

				// Set up the received packet
				status = setupReceivedPacket(&packet);
				if (status < 0)
					break;

				// Look for connections with applicable filters into which to
				// copy the packet data
				matchFilters(adapter, &packet);

				// If this is an ICMP message, take the appropriate action
				if (packet.transProtocol == NETWORK_TRANSPROTOCOL_ICMP)
					processIcmpPacket(adapter, &packet);

				else if (packet.transProtocol == NETWORK_TRANSPROTOCOL_TCP)
					processTcpPacket(adapter, &packet);

				kernelFree(packet.memory);
			}

			// Process the adapter's output packet stream.

			while (adapter->outputStream.count >=
				(sizeof(kernelNetworkPacket) / sizeof(unsigned)))
			{
				// Try to get a lock on the stream
				if (kernelLockGet(&adapter->outputStreamLock) < 0)
					// Stop for this timeslice
					break;

				status = kernelNetworkPacketStreamRead(&adapter->outputStream,
					&packet);

				kernelLockRelease(&adapter->outputStreamLock);

				if (status < 0)
					// Eh.  Dunno.  Stop for this timeslice.
					break;

				// Send it.
				status = kernelNetworkDeviceSend((char *) adapter->device.name,
					packet.memory, packet.length);

				// Free the packet memory
				kernelFree(packet.memory);

				if (status < 0)
					break;
			}
		}

		// Finished for this time slice
		kernelMultitaskerYield();
	}

	// Finished
	kernelMultitaskerTerminate(0);
}


static void checkSpawnNetworkThread(void)
{
	// Check the status of the network thread, and spawn a new one if it is
	// not running

	processState tmpState;

	if (!initialized)
		return;

	if (!netThreadPid ||
		(kernelMultitaskerGetProcessState(netThreadPid, &tmpState) < 0))
	{
		netThreadPid =
			kernelMultitaskerSpawnKernelThread(networkThread, "network thread",
				0, NULL);
	}
}


/////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////
//
//  Below here, the functions are exported for external use
//
/////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////

int kernelNetworkRegister(kernelNetworkDevice *adapter)
{
	// Called by the kernelNetworkDevice code to register a network adapter
	// for configuration (such as DHCP) and use.  That stuff is done through
	// the kernelNetworkInitialize function.

	int status = 0;

	// Check params
	if (!adapter)
	{
		kernelError(kernel_error, "NULL parameter");
		return (status = ERR_NULLPARAMETER);
	}

	adapters[numDevices++] = adapter;
	return (status = 0);
}


int kernelNetworkInitialized(void)
{
	// Returns 1 if networking is currently enabled
	return (initialized);
}


int kernelNetworkInitialize(void)
{
	// Initialize global networking stuff.

	int status = 0;
	kernelNetworkDevice *adapter = NULL;
	int count;

	extern variableList *kernelVariables;

	if (!hostName)
	{
		hostName = kernelMalloc(NETWORK_MAX_HOSTNAMELENGTH);
		if (!hostName)
			return (status = ERR_MEMORY);

		// By default
		strcpy(hostName, "visopsys");

		if (kernelVariables)
			// Check for a user-specified host name.
			strncpy(hostName, kernelVariableListGet(kernelVariables,
				"network.hostname"), NETWORK_MAX_HOSTNAMELENGTH);
		}

	if (!domainName)
	{
		domainName = kernelMalloc(NETWORK_MAX_DOMAINNAMELENGTH);
		if (!domainName)
			return (status = ERR_MEMORY);

		// By default, empty
		domainName[0] = '\0';

		if (kernelVariables)
			// Check for a user-specified domain name.
			strncpy(domainName, kernelVariableListGet(kernelVariables,
				"network.domainname"), NETWORK_MAX_DOMAINNAMELENGTH);
	}

	// Configure all the network adapters
	for (count = 0; count < numDevices; count ++)
	{
		adapter = adapters[count];

		if (!(adapter->device.flags & NETWORK_ADAPTERFLAG_LINK))
			continue;

		if (!(adapter->device.flags & NETWORK_ADAPTERFLAG_INITIALIZED))
		{
			// Initialize the adapter's network packet input and output streams
			status = kernelNetworkPacketStreamNew(&adapter->inputStream);
			if (status < 0)
				continue;
			status = kernelNetworkPacketStreamNew(&adapter->outputStream);
			if (status < 0)
				continue;

			adapter->device.flags |= NETWORK_ADAPTERFLAG_INITIALIZED;
		}

		status = configureDhcp(adapter, NETWORK_DHCP_DEFAULT_TIMEOUT);
		if (status < 0)
		{
			kernelError(kernel_error, "DHCP configuration of network adapter "
				"%s failed.", adapter->device.name);
			continue;
		}

		kernelLog("Network adapter %s configured with IP=%d.%d.%d.%d "
			"netmask=%d.%d.%d.%d", adapter->device.name,
			adapter->device.hostAddress.bytes[0],
			adapter->device.hostAddress.bytes[1],
			adapter->device.hostAddress.bytes[2],
			adapter->device.hostAddress.bytes[3],
			adapter->device.netMask.bytes[0], adapter->device.netMask.bytes[1],
			adapter->device.netMask.bytes[2], adapter->device.netMask.bytes[3]);
	}

	initialized = 1;

	// Start the regular processing of the packet input and output streams
	networkStop = 0;
	checkSpawnNetworkThread();

	kernelLog("Networking initialized.  Host name is \"%s\".", hostName);
	return (status = 0);
}


int kernelNetworkShutdown(void)
{
	// Perform a nice, orderly network shutdown.

	int status = 0;
	kernelNetworkDevice *adapter = NULL;
	int count;

	if (!initialized)
		// Don't return an error code, just pretend.
		return (status = 0);

	// Set the 'network stop' flag and yield to let the network thread finish
	// whatever it might have been doing
	networkStop = 1;
	kernelMultitaskerYield();

	// Stop each network adapter
	for (count = 0; count < numDevices; count ++)
	{
		adapter = adapters[count];

		if (!(adapter->device.flags & NETWORK_ADAPTERFLAG_LINK) ||
			!(adapter->device.flags & NETWORK_ADAPTERFLAG_RUNNING))
		{
			continue;
		}

		// If the device was configured with DHCP, tell the server we're
		// relinquishing the address.
		if (adapter->device.flags & NETWORK_ADAPTERFLAG_AUTOCONF)
			releaseDhcp(adapter);

		// The device is still initialized, but no longer running.
		adapter->device.flags &= ~NETWORK_ADAPTERFLAG_RUNNING;
	}

	initialized = 0;

	return (status = 0);
}


kernelNetworkConnection *kernelNetworkOpen(int mode, networkAddress *address,
	networkFilter *filter)
{
	// This function is a wrapper for the connectionOpen function, above, but
	// also adds the input stream if the connection mode is read-enabled, and
	// opens the TCP connection if that is the network protocol.

	kernelNetworkDevice *adapter = NULL;
	kernelNetworkConnection *connection = NULL;

	if (!initialized)
		return (connection = NULL);

	// Make sure the network thread is running
	checkSpawnNetworkThread();

	// Check params.

	if (!mode)
	{
		kernelError(kernel_error, "A connection mode must be specified");
		return (connection = NULL);
	}

	if (!filter)
	{
		kernelError(kernel_error, "NULL parameter");
		return (connection = NULL);
	}

	adapter = getDevice(address);
	if (!adapter)
	{
		kernelError(kernel_error, "No appropriate adapter for desintation "
			"address");
		return (connection = NULL);
	}

	connection = connectionOpen(adapter, mode, address, filter);
	if (!connection)
		return (connection);

	if (mode & NETWORK_MODE_READ)
	{
		// Get an input stream
		if (kernelStreamNew(&connection->inputStream,
			NETWORK_DATASTREAM_LENGTH, 1) < 0)
		{
			kernelFree((void *) connection);
			return (connection = NULL);
		}
	}

	// If this is a TCP connection, initiate it with the destination host
	if (connection->filter.transProtocol == NETWORK_TRANSPROTOCOL_TCP)
	{
		if (openTcpConnection(connection) < 0)
		{
			if (connection->inputStream.buffer)
				kernelStreamDestroy(&connection->inputStream);
			kernelFree((void *) connection);
			return (connection = NULL);
		}
	}

	return (connection);
}


int kernelNetworkAlive(kernelNetworkConnection *connection)
{
	// Returns 1 if the connection exists and is alive

	kernelNetworkDevice *adapter = NULL;
	kernelNetworkConnection *tmpConnection = NULL;
	int count;

	for (count = 0; count < numDevices; count ++)
	{
		adapter = adapters[count];

		tmpConnection = adapter->connections;
		while (tmpConnection)
		{
			if (tmpConnection == connection)
				break;
			else
				tmpConnection = connection->next;
		}

		if (tmpConnection)
			break;
	}

	if (!tmpConnection)
		return (0);

	// If this is TCP, the connection must be established
	if ((connection->filter.transProtocol == NETWORK_TRANSPROTOCOL_TCP) &&
		(connection->tcp.state != tcp_established))
	{
		return (0);
	}

	return (1);
}


int kernelNetworkClose(kernelNetworkConnection *connection)
{
	// This is just a wrapper for the connectionClose function, but also
	// closes any TCP connection (if that is the network protocol) and
	// deallocates the input stream, if applicable.

	int status = 0;

	if (!initialized)
		return (status = ERR_NOTINITIALIZED);

	// Make sure the network thread is running
	checkSpawnNetworkThread();

	// Check params
	if (!connection)
	{
		kernelError(kernel_error, "NULL parameter");
		return (status = ERR_NULLPARAMETER);
	}

	// Make sure the connection exists.  If not, no worries, just return.
	if (!kernelNetworkAlive(connection))
		return (status = 0);

	// If this is a TCP connection, close it with the destination host
	if (connection->filter.transProtocol == NETWORK_TRANSPROTOCOL_TCP)
		closeTcpConnection(connection);

	// If there's an input stream, deallocate it
	if (connection->inputStream.buffer)
		kernelStreamDestroy(&connection->inputStream);

	// Close the connection
	return (connectionClose(connection));
}


int kernelNetworkCloseAll(int processId)
{
	// Search for and close all network streams owned by the supplied process
	// ID

	int status = 0;
	kernelNetworkConnection *connection = NULL;
	int count;

	for (count = 0; count < numDevices; count ++)
	{
		connection = adapters[count]->connections;

		while (connection)
		{
			if (connection->processId == processId)
			{
				status = kernelNetworkClose(connection);
				if (status < 0)
					break;

				// Re-start for this adapter, since the list of connections
				// has now changed
				connection = adapters[count]->connections;
			}
			else
			{
				connection = connection->next;
			}
		}
	}

	return (status);
}


int kernelNetworkCount(kernelNetworkConnection *connection)
{
	// Returns the number of bytes currently in the connection's input stream

	if (!initialized)
		return (ERR_NOTINITIALIZED);

	// Make sure the network thread is running
	checkSpawnNetworkThread();

	// Check params
	if (!connection)
	{
		kernelError(kernel_error, "NULL parameter");
		return (ERR_NULLPARAMETER);
	}

	// Make sure the connection exists
	if (!kernelNetworkAlive(connection))
	{
		kernelError(kernel_error, "Connection is not alive");
		return (ERR_IO);
	}

	return (connection->inputStream.count);
}


int kernelNetworkRead(kernelNetworkConnection *connection,
	unsigned char *buffer, unsigned bufferSize)
{
	// Given a network connection, read up to 'bufferSize' bytes into the
	// buffer from the connection's input stream.

	int status = 0;

	if (!initialized)
		return (status = ERR_NOTINITIALIZED);

	// Make sure the network thread is running
	checkSpawnNetworkThread();

	// Check params
	if (!connection || !buffer)
	{
		kernelError(kernel_error, "NULL parameter");
		return (status = ERR_NULLPARAMETER);
	}

	// Make sure the connection exists
	if (!kernelNetworkAlive(connection))
	{
		kernelError(kernel_error, "Connection is not alive");
		return (status = ERR_IO);
	}

	// Make sure we're reading
	if (!(connection->mode & NETWORK_MODE_READ))
	{
		kernelError(kernel_error, "Network connection is not open for "
			"reading");
		return (status = ERR_INVALID);
	}

	// Anything to do?
	if (!bufferSize)
		return (status = 0);

	// Use the smaller of the buffer size, or the bytes available to read
	bufferSize = min(connection->inputStream.count, bufferSize);

	// Lock the input stream
	status = kernelLockGet(&connection->inputStreamLock);
	if (status < 0)
		return (status);

	// Read into the buffer
	status = connection->inputStream.popN(&connection->inputStream,	bufferSize,
		buffer);

	kernelLockRelease(&connection->inputStreamLock);

	// If this is a TCP connection and we have un-ACKed data, ACK it now.
	if ((connection->filter.transProtocol == NETWORK_TRANSPROTOCOL_TCP) &&
		(connection->tcp.recvAcked < connection->tcp.recvLast))
	{
		sendTcpAck(connection, connection->tcp.recvLast);
	}

	return (status);
}


int kernelNetworkWrite(kernelNetworkConnection *connection,
	unsigned char *buffer, unsigned bufferSize)
{
	// Given a network connection, write up to 'bufferSize' bytes from the
	// buffer to the connection's output.

	int status = 0;

	if (!initialized)
		return (status = ERR_NOTINITIALIZED);

	// Make sure the network thread is running
	checkSpawnNetworkThread();

	// Check params
	if (!connection || !buffer)
	{
		kernelError(kernel_error, "NULL parameter");
		return (status = ERR_NULLPARAMETER);
	}

	// Make sure the connection exists
	if (!kernelNetworkAlive(connection))
	{
		kernelError(kernel_error, "Connection is not alive");
		return (status = ERR_IO);
	}

	// Make sure we're writing
	if (!(connection->mode & NETWORK_MODE_WRITE))
	{
		kernelError(kernel_error, "Network connection is not open for "
			"writing");
		return (status = ERR_INVALID);
	}

	return (send(connection, buffer, bufferSize, 1, 1));
}


int kernelNetworkPing(kernelNetworkConnection *connection, int sequenceNum,
	unsigned char *buffer, unsigned bufferSize)
{
	// Send a ping.

	int status = 0;
	networkPingPacket pingPacket;
	unsigned packetSize = 0;

	if (!initialized)
		return (status = ERR_NOTINITIALIZED);

	// Make sure the network thread is running
	checkSpawnNetworkThread();

	// Check params
	if (!connection)
	{
		kernelError(kernel_error, "NULL parameter");
		return (status = ERR_NULLPARAMETER);
	}

	// Make sure the connection exists
	if (!kernelNetworkAlive(connection))
	{
		kernelError(kernel_error, "Connection is not alive");
		return (status = ERR_IO);
	}

	if (bufferSize > NETWORK_PING_DATASIZE)
		bufferSize = NETWORK_PING_DATASIZE;

	packetSize =
		(sizeof(networkPingPacket) - (NETWORK_PING_DATASIZE - bufferSize));

	// Clear our ping packet
	memset(&pingPacket, 0, sizeof(networkPingPacket));

	pingPacket.icmpHeader.type = NETWORK_ICMP_ECHO;
	pingPacket.sequenceNum = processorSwap16(sequenceNum);

	// Fill out our data.
	memcpy(pingPacket.data, buffer, bufferSize);

	// Do the checksum after everything else is set
	pingPacket.icmpHeader.checksum =
		processorSwap16(icmpChecksum(&pingPacket.icmpHeader, packetSize));

	return (send(connection, (unsigned char *) &pingPacket, packetSize, 1, 1));
}


int kernelNetworkGetHostName(char *buffer, int bufferSize)
{
	// Get the system's network hostname

	int status = 0;

	if (!initialized)
		return (status = ERR_NOTINITIALIZED);

	// Check params
	if (!buffer)
		return (status = ERR_NULLPARAMETER);

	strncpy(buffer, hostName, min(bufferSize, NETWORK_MAX_HOSTNAMELENGTH));
	return (status = 0);
}


int kernelNetworkSetHostName(const char *buffer, int bufferSize)
{
	// Set the system's network hostname

	int status = 0;

	if (!initialized)
		return (status = ERR_NOTINITIALIZED);

	// Check params
	if (!buffer)
		return (status = ERR_NULLPARAMETER);

	strncpy(hostName, buffer, min(bufferSize, NETWORK_MAX_HOSTNAMELENGTH));
	return (status = 0);
}


int kernelNetworkGetDomainName(char *buffer, int bufferSize)
{
	// Get the system's network domain name

	int status = 0;

	if (!initialized)
		return (status = ERR_NOTINITIALIZED);

	// Check params
	if (!buffer)
		return (status = ERR_NULLPARAMETER);

	strncpy(buffer, domainName, min(bufferSize, NETWORK_MAX_DOMAINNAMELENGTH));
	return (status = 0);
}


int kernelNetworkSetDomainName(const char *buffer, int bufferSize)
{
	// Set the system's network domain name

	int status = 0;

	if (!initialized)
		return (status = ERR_NOTINITIALIZED);

	// Check params
	if (!buffer)
		return (status = ERR_NULLPARAMETER);

	strncpy(domainName, buffer, min(bufferSize, NETWORK_MAX_DOMAINNAMELENGTH));
	return (status = 0);
}


void kernelNetworkIpDebug(unsigned char *buffer)
{
	networkIpHeader *ipHeader = NULL;
	networkTcpHeader *tcpHeader = NULL;
	networkUdpHeader *udpHeader = NULL;
	networkAddress *srcAddr = NULL;
	networkAddress *destAddr = NULL;

	// Check params
	if (!buffer)
	{
		kernelError(kernel_error, "NULL parameter");
		return;
	}

	ipHeader = (networkIpHeader *) buffer;
	tcpHeader = (((void *) ipHeader) +
		((ipHeader->versionHeaderLen & 0x0F) << 2));
	udpHeader = (((void *) ipHeader) +
		((ipHeader->versionHeaderLen & 0x0F) << 2));

	if ((ipHeader->versionHeaderLen < 0x45) ||
		(ipHeader->versionHeaderLen > 0x4F))
	{
		kernelError(kernel_error, "Illegal IP header length");
		return;
	}

	srcAddr = (networkAddress *) &ipHeader->srcAddress;
	destAddr = (networkAddress *) &ipHeader->destAddress;

	kernelTextPrintLine("versionHeaderLen=%x, identification=%x, protocol=%d\n"
		"source=%d.%d.%d.%d, dest=%d.%d.%d.%d",
		ipHeader->versionHeaderLen,
		processorSwap16(ipHeader->identification),
		ipHeader->protocol, srcAddr->bytes[0],
		srcAddr->bytes[1], srcAddr->bytes[2],
		srcAddr->bytes[3], destAddr->bytes[0],
		destAddr->bytes[1], destAddr->bytes[2],
		destAddr->bytes[3]);

	if (ipChecksum(ipHeader) !=
		processorSwap16(ipHeader->headerChecksum))
	{
		kernelTextPrintLine("IP checksum DOES NOT MATCH (%x != %x)",
			ipChecksum(ipHeader),
			processorSwap16(ipHeader->headerChecksum));
	}

	if (ipHeader->protocol == NETWORK_TRANSPROTOCOL_TCP)
	{
		kernelTextPrintLine("TCP src=%d, dest=%d, seq=%x ack=%x wnd=%x "
			"flgs=%x hdsz=%d chksum=%x",
			processorSwap16(tcpHeader->srcPort),
			processorSwap16(tcpHeader->destPort),
			processorSwap32(tcpHeader->sequenceNum),
			processorSwap32(tcpHeader->ackNum),
			processorSwap16(tcpHeader->window),
			networkGetTcpHdrFlags(tcpHeader), networkGetTcpHdrSize(tcpHeader),
			processorSwap16(tcpHeader->checksum));
		if (tcpChecksum(ipHeader) !=
			processorSwap16(tcpHeader->checksum))
		{
			kernelTextPrintLine("TCP checksum DOES NOT MATCH (%x != %x)",
				tcpChecksum(ipHeader),
				processorSwap16(tcpHeader->checksum));
		}
	}

	if (ipHeader->protocol == NETWORK_TRANSPROTOCOL_UDP)
	{
		kernelTextPrintLine("UDP srcPort=%d, destPort=%d, length=%d, "
			"chksum=%x", processorSwap16(udpHeader->srcPort),
			processorSwap16(udpHeader->destPort),
			processorSwap16(udpHeader->length),
			processorSwap16(udpHeader->checksum));
		if (udpChecksum(ipHeader) !=
			processorSwap16(udpHeader->checksum))
		{
			kernelTextPrintLine("UDP checksum DOES NOT MATCH (%x != %x)",
				udpChecksum(ipHeader),
				processorSwap16(udpHeader->checksum));
		}
	}
}

