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
//  kernelNetworkUdp.c
//

#include "kernelNetworkUdp.h"
#include "kernelDebug.h"
#include "kernelError.h"
#include "kernelNetwork.h"
#include <string.h>
#include <arpa/inet.h>


static unsigned short udpChecksum(networkIp4Header *ip4Header)
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

	udpHeader = (((void *) ip4Header) +
		((ip4Header->versionHeaderLen & 0x0F) << 2));
	udpLength = ntohs(udpHeader->length);

	// IP source and destination addresses
	wordPtr = (unsigned short *) ip4Header;
	for (count = 6; count < 10; count ++)
		checksum += ntohs(wordPtr[count]);

	// Protocol
	checksum += ip4Header->protocol;

	// UDP length
	checksum += udpLength;

	// The UDP header and data
	wordPtr = (unsigned short *) udpHeader;
	for (count = 0; count < (udpLength / 2); count ++)
	{
		if (count != 3)
			// Skip the checksum field itself
			checksum += ntohs(wordPtr[count]);
	}

	if (udpLength % 2)
		checksum += ntohs(wordPtr[udpLength / 2] & 0x00FF);

	return ((unsigned short)((~checksum & 0xFFFF) - (checksum >> 16)));
}


/////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////
//
//  Below here, the functions are exported for internal use
//
/////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////

int kernelNetworkUdpSetupReceivedPacket(kernelNetworkPacket *packet)
{
	// This takes a semi-raw 'received' UDP packet, as from the network
	// device's packet input stream,  and tries to interpret the rest and
	// set up the remainder of the packet's fields.

	int status = 0;
	networkIp4Header *ip4Header = NULL;
	networkUdpHeader *udpHeader = NULL;

	ip4Header = (networkIp4Header *)(packet->memory +
		packet->netHeaderOffset);
	udpHeader = (networkUdpHeader *)(packet->memory +
		packet->transHeaderOffset);

	// Check the checksum
	if (ntohs(udpHeader->checksum) != udpChecksum(ip4Header))
	{
		kernelError(kernel_error, "UDP header checksum mismatch");
		return (status = ERR_INVALID);
	}

	// Source and destination ports
	packet->srcPort = ntohs(udpHeader->srcPort);
	packet->destPort = ntohs(udpHeader->destPort);

	// Update the data pointer and length
	packet->dataOffset += sizeof(networkUdpHeader);
	packet->dataLength -= sizeof(networkUdpHeader);

	return (status = 0);
}


void kernelNetworkUdpPrependHeader(kernelNetworkPacket *packet)
{
	networkUdpHeader *header = NULL;

	header = (networkUdpHeader *)(packet->memory + packet->dataOffset);

	header->srcPort = htons(packet->srcPort);
	header->destPort = htons(packet->destPort);
	header->length = htons(packet->dataLength);

	// We have to defer the checksum until the data is in the packet.
	header->checksum = 0;

	// Adjust the packet structure
	packet->transHeaderOffset = packet->dataOffset;
	packet->dataOffset += sizeof(networkUdpHeader);
	packet->dataLength -= sizeof(networkUdpHeader);
}


void kernelNetworkUdpFinalizeSendPacket(kernelNetworkPacket *packet)
{
	// This does any required finalizing and checksumming of a packet before
	// it is to be sent.

	networkIp4Header *ip4Header = NULL;
	networkUdpHeader *udpHeader = NULL;

	ip4Header = (networkIp4Header *)(packet->memory +
		packet->netHeaderOffset);
	udpHeader = (networkUdpHeader *)(packet->memory +
		packet->transHeaderOffset);

	// Make sure the length field matches the actual size of the
	// UDP header+data

	udpHeader->length = htons(packet->length - packet->transHeaderOffset);

	// Now we can do the checksum
	udpHeader->checksum = htons(udpChecksum(ip4Header));
}


void kernelNetworkUdpDebug(unsigned char *buffer)
{
	networkIp4Header *ip4Header = NULL;
	networkUdpHeader *udpHeader = NULL;

	ip4Header = (networkIp4Header *) buffer;
	udpHeader = (((void *) ip4Header) +
		((ip4Header->versionHeaderLen & 0x0F) << 2));

	kernelDebug(debug_net, "UDP srcPort=%d, destPort=%d, length=%d, "
		"chksum=%x", ntohs(udpHeader->srcPort), ntohs(udpHeader->destPort),
		ntohs(udpHeader->length), ntohs(udpHeader->checksum));

	if (udpChecksum(ip4Header) != ntohs(udpHeader->checksum))
	{
		kernelDebug(debug_net, "UDP checksum DOES NOT MATCH (%x != %x)",
			udpChecksum(ip4Header), ntohs(udpHeader->checksum));
	}
}

