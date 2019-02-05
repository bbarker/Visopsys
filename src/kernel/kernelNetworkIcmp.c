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
//  kernelNetworkIcmp.c
//

#include "kernelNetworkIcmp.h"
#include "kernelDebug.h"
#include "kernelError.h"
#include "kernelMalloc.h"
#include "kernelNetwork.h"
#include <string.h>
#include <arpa/inet.h>


static unsigned short icmpChecksum(networkIcmpHeader *header, int length)
{
	// Calculate the checksum for the supplied packet.  This is done
	// as a 1's complement sum of the ICMP header + the data

	unsigned short *words = (void *) header;
	unsigned checksum = 0;
	int count;

	for (count = 0; count < (length >> 1); count ++)
	{
		// Skip the checksum word itself
		if (count != 1)
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

int kernelNetworkIcmpSetupReceivedPacket(kernelNetworkPacket *packet)
{
	// This takes a semi-raw 'received' ICMP packet, as from the network
	// device's packet input stream,  and tries to interpret the rest and
	// set up the remainder of the packet's fields.

	int status = 0;
	networkIp4Header *ip4Header = NULL;
	networkIcmpHeader *icmpHeader = NULL;

	ip4Header = (networkIp4Header *)(packet->memory +
		packet->netHeaderOffset);
	icmpHeader = (networkIcmpHeader *)(packet->memory +
		packet->transHeaderOffset);

	// Check the checksum
	if (ntohs(icmpHeader->checksum) != icmpChecksum(icmpHeader,
		(ntohs(ip4Header->totalLength) - sizeof(networkIp4Header))))
	{
		kernelError(kernel_error, "ICMP checksum mismatch");
		return (status = ERR_INVALID);
	}

	// Update the data pointer and length
	packet->dataOffset += sizeof(networkIcmpHeader);
	packet->dataLength -= sizeof(networkIcmpHeader);

	return (status = 0);
}


void kernelNetworkIcmpProcessPacket(kernelNetworkDevice *netDev,
	kernelNetworkPacket *packet)
{
	// Take the appropriate action for whatever ICMP message we received.

	networkFilter filter;
	kernelNetworkConnection *connection = NULL;
	networkIp4Header *ip4Header = NULL;
	networkIcmpHeader *icmpHeader = NULL;
	int icmpLen = 0;
	void *icmpReply = NULL;
	unsigned short checksum = 0;

	ip4Header = (networkIp4Header *)(packet->memory +
		packet->netHeaderOffset);
	icmpHeader = (networkIcmpHeader *)(packet->memory +
		packet->transHeaderOffset);

	icmpLen = (ntohs(ip4Header->totalLength) - sizeof(networkIp4Header));

	switch (icmpHeader->type)
	{
		case NETWORK_ICMP_ECHO:
		{
			// This is a ping.  We create an 'echo reply' (ping reply) and
			// send it back.

			// Get a connection for sending

			memset(&filter, 0, sizeof(networkFilter));
			filter.flags = (NETWORK_FILTERFLAG_NETPROTOCOL |
				NETWORK_FILTERFLAG_TRANSPROTOCOL);
			filter.netProtocol = NETWORK_NETPROTOCOL_IP4;
			filter.transProtocol = NETWORK_TRANSPROTOCOL_ICMP;

			connection = kernelNetworkConnectionOpen(netDev,
				NETWORK_MODE_WRITE, (networkAddress *) &ip4Header->srcAddress,
				&filter, 0 /* no input stream */);
			if (!connection)
				return;

			// Get memory for our reply
			icmpReply = kernelMalloc(icmpLen);
			if (!icmpReply)
				return;

			memcpy(icmpReply, icmpHeader, icmpLen);
			icmpHeader = icmpReply;
			icmpHeader->type = NETWORK_ICMP_ECHOREPLY;
			checksum = icmpChecksum(icmpHeader, icmpLen);
			icmpHeader->checksum = htons(checksum);

			kernelDebug(debug_net, "ICMP echo reply %u bytes of data",
				(icmpLen - sizeof(networkIcmpHeader) - 4));

			// Send, but only queue it for output so that ICMP packets don't
			// tie up the processing of the input queue
			kernelNetworkSendData(connection, (void *) icmpHeader, icmpLen,
				0 /* not immediate */);

			kernelFree(icmpReply);
			kernelNetworkConnectionClose(connection, 0 /* not polite */);

			break;
		}

		default:
		{
			// Not supported yet, or we don't deal with it here.  Not an error
			// or anything.
			break;
		}
	}
}


int kernelNetworkIcmpPing(kernelNetworkConnection *connection,
	int sequenceNum, unsigned char *buffer, unsigned bufferSize)
{
	// Send a ping.

	networkPingPacket pingPacket;
	unsigned packetSize = 0;

	kernelDebug(debug_net, "ICMP echo %u bytes of data", bufferSize);

	if (bufferSize > NETWORK_PING_DATASIZE)
		bufferSize = NETWORK_PING_DATASIZE;

	packetSize = (sizeof(networkPingPacket) - (NETWORK_PING_DATASIZE -
		bufferSize));

	// Clear our ping packet
	memset(&pingPacket, 0, sizeof(networkPingPacket));

	pingPacket.icmpHeader.type = NETWORK_ICMP_ECHO;
	pingPacket.sequenceNum = htons(sequenceNum);

	// Fill out our data.
	memcpy(pingPacket.data, buffer, bufferSize);

	// Do the checksum after everything else is set
	pingPacket.icmpHeader.checksum =
		htons(icmpChecksum(&pingPacket.icmpHeader, packetSize));

	return (kernelNetworkSendData(connection, (unsigned char *) &pingPacket,
		packetSize, 1 /* immediate */));
}

