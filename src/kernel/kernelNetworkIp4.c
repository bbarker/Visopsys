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
//  kernelNetworkIp4.c
//

#include "kernelNetworkIp4.h"
#include "kernelDebug.h"
#include "kernelError.h"
#include "kernelNetwork.h"
#include "kernelRandom.h"
#include <string.h>
#include <arpa/inet.h>


static int ipPortInUse(kernelNetworkDevice *netDev, int portNumber)
{
	// Returns 1 if there is a connection using the specified local IP port
	// number

	kernelLinkedListItem *iter = NULL;
	kernelNetworkConnection *connection = NULL;

	connection = kernelLinkedListIterStart((kernelLinkedList *)
		&netDev->connections, &iter);

	while (connection)
	{
		if ((connection->filter.flags & NETWORK_FILTERFLAG_LOCALPORT) &&
			(connection->filter.localPort == portNumber))
		{
			return (1);
		}

		connection = kernelLinkedListIterNext((kernelLinkedList *)
			&netDev->connections, &iter);
	}

	return (0);
}


static unsigned short ipChecksum(networkIp4Header *header)
{
	// Calculate the checksum for the supplied IP packet header.  This is done
	// as a 1's complement sum of each 16-bit word in the header.

	unsigned headerWords = ((header->versionHeaderLen & 0xF) << 1);
	unsigned short *words = (unsigned short *) header;
	unsigned checksum = 0;
	unsigned count;

	for (count = 0; count < headerWords; count ++)
	{
		// Skip the checksum word itself
		if (count != 5)
			checksum += ntohs(words[count]);
	}

	return ((unsigned short)((~checksum & 0xFFFF) - (checksum >> 16)));
}


/////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////
//
//  Below here, the functions are exported for internal use
//
/////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////

int kernelNetworkIp4GetLocalPort(kernelNetworkDevice *netDev, int portNum)
{
	// If a local port number has been specified, make sure it is not in use
	if (portNum)
	{
		if (ipPortInUse(netDev, portNum))
		{
			kernelError(kernel_error, "Local IP port %d is in use", portNum);
			return (portNum = ERR_BUSY);
		}
	}
	else
	{
		// Find a random port > 1024 that is free
		while (!portNum || ipPortInUse(netDev, portNum))
			portNum = kernelRandomFormatted(1025, 0xFFFF);
	}

	return (portNum);
}


int kernelNetworkIp4SetupReceivedPacket(kernelNetworkPacket *packet)
{
	int status = 0;
	networkIp4Header *header = NULL;
	unsigned headerBytes = 0;

	header = (networkIp4Header *)(packet->memory + packet->netHeaderOffset);
	headerBytes = ((header->versionHeaderLen & 0x0F) << 2);

	if ((headerBytes < 20) || ((packet->netHeaderOffset + headerBytes) >
		packet->length))
	{
		kernelError(kernel_error, "IP4 header invalid length");
		return (status = ERR_BOUNDS);
	}

	// Check the checksum
	if (ntohs(header->headerChecksum) != ipChecksum(header))
	{
		kernelError(kernel_error, "IP4 header checksum mismatch");
		return (status = ERR_BADDATA);
	}

	// Copy the source and destination addresses
	networkAddressCopy(&packet->srcAddress, &header->srcAddress,
		NETWORK_ADDRLENGTH_IP4);
	networkAddressCopy(&packet->destAddress, &header->destAddress,
		NETWORK_ADDRLENGTH_IP4);

	// Some devices can return more data than is actually contained in the
	// packet (e.g. rounded up from 58 to 64).  We should correct it here.
	packet->length = (packet->netHeaderOffset + ntohs(header->totalLength));

	// Set up the packet fields for the transport header
	packet->transProtocol = header->protocol;
	packet->transHeaderOffset = (packet->netHeaderOffset + headerBytes);

	// Set the data section to start at the transport header
	packet->dataOffset = packet->transHeaderOffset;
	packet->dataLength = (packet->length - packet->dataOffset);

	return (status = 0);
}


void kernelNetworkIp4PrependHeader(kernelNetworkPacket *packet)
{
	// Create the IP header for this packet and adjust the packet data
	// pointer and size appropriately

	networkIp4Header *header = NULL;

	header = (networkIp4Header *)(packet->memory + packet->dataOffset);

	// Version 4, header length 5 dwords
	header->versionHeaderLen = 0x45;

	// Type of service: Normal everything.  Routine = 000, delay = 0,
	// throughput = 0, reliability = 0
	header->typeOfService = 0;
	header->totalLength = htons(packet->dataLength);

	// Fragmentation allowed, but off by default
	header->flagsFragOffset = 0;
	header->timeToLive = 64;
	header->protocol = packet->transProtocol;

	// Wait until the end for the checksum.  Copy the source and destination
	// IP addresses
	networkAddressCopy(&header->srcAddress, &packet->srcAddress,
		NETWORK_ADDRLENGTH_IP4);
	networkAddressCopy(&header->destAddress, &packet->destAddress,
		NETWORK_ADDRLENGTH_IP4);

	// Do the checksum.
	header->headerChecksum = htons(ipChecksum(header));

	// Adjust the packet structure
	packet->netHeaderOffset = packet->dataOffset;
	packet->dataOffset += sizeof(networkIp4Header);
	packet->dataLength -= sizeof(networkIp4Header);
}


void kernelNetworkIp4FinalizeSendPacket(kernelNetworkIpState *ip,
	kernelNetworkPacket *packet)
{
	networkIp4Header *header = NULL;

	header = (networkIp4Header *)(packet->memory + packet->netHeaderOffset);

	header->identification = htons(ip->identification);
	ip->identification += 1;

	// Make sure the length field matches the actual size of the IP
	// header + data
	header->totalLength = htons(((packet->dataOffset -
		packet->netHeaderOffset) + packet->dataLength));

	header->headerChecksum = htons(ipChecksum(header));
}

