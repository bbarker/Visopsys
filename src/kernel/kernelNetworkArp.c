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
//  kernelNetworkArp.c
//

#include "kernelNetworkArp.h"
#include "kernelDebug.h"
#include "kernelError.h"
#include "kernelNetwork.h"
#include <stdio.h>
#include <string.h>
#include <arpa/inet.h>


#if defined(DEBUG)

static void addr2String(networkAddress *address, char *dest, int addrSize,
	int hex)
{
	char separator = ':';
	int count;

	if (addrSize == NETWORK_ADDRLENGTH_IP4)
		separator = '.';

	dest[0] = '\0';

	for (count = 0; count < addrSize; count ++)
	{
		if (hex)
		{
			sprintf((dest + strlen(dest)), "%02x%c", address->byte[count],
				((count < (addrSize - 1))? separator : '\0'));
		}
		else
		{
			sprintf((dest + strlen(dest)), "%d%c", address->byte[count],
				((count < (addrSize - 1))? separator : '\0'));
		}
	}
}


static void debugArp(networkArpHeader *arp)
{
	char srcHardwareAddress[18];
	char srcLogicalAddress[16];
	char destHardwareAddress[18];
	char destLogicalAddress[16];

	addr2String((networkAddress *) &arp->srcHardwareAddress,
		srcHardwareAddress, NETWORK_ADDRLENGTH_ETHERNET, 1);
	addr2String((networkAddress *) &arp->srcLogicalAddress,
		srcLogicalAddress, NETWORK_ADDRLENGTH_IP4, 0);
	addr2String((networkAddress *) &arp->destHardwareAddress,
		destHardwareAddress, NETWORK_ADDRLENGTH_ETHERNET, 1);
	addr2String((networkAddress *) &arp->destLogicalAddress,
		destLogicalAddress, NETWORK_ADDRLENGTH_IP4, 0);

	kernelDebug(debug_net, "ARP hardAddrSpc=%x protAddrSpc=%x "
		"hardAddrLen=%d, protAddrLen=%d opCode=%d",
		ntohs(arp->hardwareAddressSpace), ntohs(arp->protocolAddressSpace),
		arp->hardwareAddrLen, arp->protocolAddrLen, ntohs(arp->opCode));
	kernelDebug(debug_net, "ARP srcHardAddr=%s srcLogAddr=%s",
		srcHardwareAddress, srcLogicalAddress);
	kernelDebug(debug_net, "ARP dstHardAddr=%s dstLogAddr=%s",
		destHardwareAddress, destLogicalAddress);
}

#else
	#define debugArp(arp) do { } while (0)
#endif


static void addArpCache(kernelNetworkDevice *netDev,
	networkAddress *logicalAddress, networkAddress *physicalAddress)
{
	// Add the supplied entry to our ARP cache

	// We always put the most recent entry at the start of the list.  If the
	// list grows to its maximum size, the oldest entries fall off the bottom.

	// Shift all down
	memmove((void *) &netDev->arpCache[1], (void *) &netDev->arpCache[0],
		((NETWORK_ARPCACHE_SIZE - 1) * sizeof(kernelArpCacheItem)));

	networkAddressCopy(&netDev->arpCache[0].logicalAddress, logicalAddress,
		sizeof(networkAddress));
	networkAddressCopy(&netDev->arpCache[0].physicalAddress, physicalAddress,
		sizeof(networkAddress));

	if (netDev->numArpCaches < NETWORK_ARPCACHE_SIZE)
		netDev->numArpCaches += 1;
}


/////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////
//
//  Below here, the functions are exported for internal use
//
/////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////

int kernelNetworkArpSearchCache(kernelNetworkDevice *netDev,
	networkAddress *logicalAddress)
{
	// Search the device's ARP cache for an entry corresponding to the
	// supplied logical address, and if found, copy the physical address into
	// the supplied pointer.

	int status = 0;
	int count;

	for (count = 0; count < netDev->numArpCaches; count ++)
	{
		if (networkAddressesEqual(logicalAddress,
			&netDev->arpCache[count].logicalAddress, NETWORK_ADDRLENGTH_IP4))
		{
			return (count);
		}
	}

	// If we fall through, not found
	return (status = ERR_NOSUCHENTRY);
}


int kernelNetworkArpSetupReceivedPacket(kernelNetworkPacket *packet)
{
	int status = 0;
	networkArpPacket *arpPacket = (networkArpPacket *) packet->memory;
	networkArpHeader *arp = &arpPacket->arpHeader;

	kernelDebug(debug_net, "ARP setup received packet");

	if (arp->hardwareAddrLen != NETWORK_ADDRLENGTH_ETHERNET)
	{
		kernelError(kernel_error, "ARP invalid hardware address length");
		return (status = ERR_BOUNDS);
	}

	// Copy the source and destination addresses
	networkAddressCopy(&packet->srcAddress, &arpPacket->ethHeader.source,
		arp->hardwareAddrLen);
	networkAddressCopy(&packet->destAddress, &arpPacket->ethHeader.dest,
		arp->hardwareAddrLen);

	return (status = 0);
}


int kernelNetworkArpProcessPacket(kernelNetworkDevice *netDev,
	kernelNetworkPacket *packet)
{
	// This gets called anytime we receive an ARP packet (request or reply)

	int status = 0;
	networkArpPacket *arpPacket = (networkArpPacket *) packet->memory;
	networkArpHeader *arp = &arpPacket->arpHeader;
	int arpPosition = 0;

	debugArp(arp);

	// Make sure it's ethernet ARP
	if (ntohs(arp->hardwareAddressSpace) != NETWORK_ARPHARDWARE_ETHERNET)
		return (status = ERR_NOTIMPLEMENTED);

	// See whether the sender is in our cache
	arpPosition = kernelNetworkArpSearchCache(netDev, (networkAddress *)
		&arp->srcLogicalAddress);

	if (arpPosition >= 0)
	{
		// Update the entry in our cache
		networkAddressCopy(&netDev->arpCache[arpPosition].physicalAddress,
			&arp->srcHardwareAddress, NETWORK_ADDRLENGTH_ETHERNET);
	}
	else
	{
		// Add an entry to our cache.  Perhaps we shouldn't do this unless the
		// ARP packet is for us, but we suppose for the moment it can't hurt
		// too badly to have a few extras in our table.
		addArpCache(netDev, (networkAddress *) &arp->srcLogicalAddress,
			(networkAddress *) &arp->srcHardwareAddress);
	}

	// Now if this wasn't for us, ignore it.
	if (!networkAddressesEqual(&netDev->device.hostAddress,
		&arp->destLogicalAddress, NETWORK_ADDRLENGTH_IP4))
	{
		return (status = 0);
	}

	if (ntohs(arp->opCode) == NETWORK_ARPOP_REQUEST)
	{
		// Someone is asking for us.  Send a reply, but it should be queued
		// instead of immediate.
		kernelNetworkArpSend(netDev,
			(networkAddress *) &arp->srcLogicalAddress,
			(networkAddress *) &arp->srcHardwareAddress, NETWORK_ARPOP_REPLY,
			0 /* not immediate */);
	}

	return (status = 0);
}


int kernelNetworkArpSend(kernelNetworkDevice *netDev,
	networkAddress *destLogicalAddress, networkAddress *destPhysicalAddress,
	int opCode, int immediate)
{
	// Send an ARP request or reply

	int status = 0;
	kernelNetworkPacket *packet = NULL;
	networkArpPacket *arpPacket = NULL;
	networkArpHeader *arp = NULL;

	packet = kernelNetworkPacketGet();
	if (!packet)
		return (status = ERR_MEMORY);

	arpPacket = (networkArpPacket *) packet->memory;
	arp = &arpPacket->arpHeader;

	packet->length = sizeof(networkArpPacket);

	// We will construct this ethernet-ARP packet by hand, rather than calling
	// kernelNetworkEthernetPrependHeader() which assumes an IP packet and
	// might generate its own ARP request.

	if ((opCode == NETWORK_ARPOP_REPLY) && destPhysicalAddress)
	{
		// Destination is the supplied physical address
		networkAddressCopy(&arpPacket->ethHeader.dest, destPhysicalAddress,
			NETWORK_ADDRLENGTH_ETHERNET);
	}
	else
	{
		// Destination is the ethernet broadcast address FF:FF:FF:FF:FF:FF
		networkAddressCopy(&arpPacket->ethHeader.dest,
			&NETWORK_BROADCAST_ADDR_ETHERNET, NETWORK_ADDRLENGTH_ETHERNET);
	}

	// Source is the device hardware address
	networkAddressCopy(&arpPacket->ethHeader.source,
		&netDev->device.hardwareAddress, NETWORK_ADDRLENGTH_ETHERNET);

	// Ethernet type is ARP
	arpPacket->ethHeader.type = htons(NETWORK_ETHERTYPE_ARP);

	// Hardware address space is ethernet=1
	arp->hardwareAddressSpace = htons(NETWORK_ARPHARDWARE_ETHERNET);
	// Protocol address space is IP=0x0800
	arp->protocolAddressSpace = htons(NETWORK_ETHERTYPE_IP4);
	// Hardware address length is 6
	arp->hardwareAddrLen = NETWORK_ADDRLENGTH_ETHERNET;
	// Protocol address length is 4 for IP
	arp->protocolAddrLen = NETWORK_ADDRLENGTH_IP4;
	// Operation code.  Request or reply.
	arp->opCode = htons(opCode);

	// Our source hardware address
	networkAddressCopy(&arp->srcHardwareAddress,
		&netDev->device.hardwareAddress, NETWORK_ADDRLENGTH_ETHERNET);

	// Our source logical address
	networkAddressCopy(&arp->srcLogicalAddress, &netDev->device.hostAddress,
		NETWORK_ADDRLENGTH_IP4);

	// Our desired logical address
	networkAddressCopy(&arp->destLogicalAddress, destLogicalAddress,
		NETWORK_ADDRLENGTH_IP4);

	if ((opCode == NETWORK_ARPOP_REPLY) && destPhysicalAddress)
	{
		// The target's hardware address
		networkAddressCopy(&arp->destHardwareAddress, destPhysicalAddress,
			NETWORK_ADDRLENGTH_ETHERNET);
	}

	debugArp(arp);

	status = kernelNetworkSendPacket(netDev, packet, immediate);

	kernelNetworkPacketRelease(packet);

	return (status);
}

