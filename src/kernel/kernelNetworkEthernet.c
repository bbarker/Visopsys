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
//  kernelNetworkEthernet.c
//

#include "kernelNetworkEthernet.h"
#include "kernelError.h"
#include "kernelNetwork.h"
#include "kernelNetworkDevice.h"
#include <string.h>
#include <arpa/inet.h>


/////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////
//
//  Below here, the functions are exported for internal use
//
/////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////

int kernelNetworkEthernetPrependHeader(kernelNetworkDevice *netDev,
	kernelNetworkPacket *packet)
{
	// Create the ethernet header for this packet and adjust the packet data
	// pointer and size appropriately

	int status = 0;
	networkEthernetHeader *header = (networkEthernetHeader *)(packet->memory +
		packet->dataOffset);

	// If the IP destination address is broadcast, we make the ethernet
	// destination be broadcast as well
	if (networkAddressesEqual(&packet->destAddress,
		&NETWORK_BROADCAST_ADDR_IP4, NETWORK_ADDRLENGTH_IP4))
	{
		// Destination is the ethernet broadcast address FF:FF:FF:FF:FF:FF
		networkAddressCopy(&header->dest, &NETWORK_BROADCAST_ADDR_ETHERNET,
			NETWORK_ADDRLENGTH_ETHERNET);
	}
	else
	{
		// Get the 6-byte ethernet destination address
		status = kernelNetworkDeviceGetAddress((char *) netDev->device.name,
			&packet->destAddress, (networkAddress *) &header->dest);
		if (status < 0)
		{
			// Can't find the destination host, we guess
			kernelError(kernel_error, "No route to host");
			return (status = ERR_NOROUTETOHOST);
		}
	}

	// Copy the 6-byte ethernet source address
	networkAddressCopy(&header->source, &netDev->device.hardwareAddress,
		NETWORK_ADDRLENGTH_ETHERNET);

	// Always the same for our purposes
	header->type = htons(NETWORK_ETHERTYPE_IP4);

	// Adjust the packet structure
	packet->linkHeaderOffset = packet->dataOffset;
	packet->dataOffset += sizeof(networkEthernetHeader);
	packet->dataLength -= sizeof(networkEthernetHeader);

	// Data must fit within an ethernet frame
	if (packet->dataLength > NETWORK_MAX_ETHERDATA_LENGTH)
		packet->dataLength = NETWORK_MAX_ETHERDATA_LENGTH;

	return (status = 0);
}

