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
//  kernelNetworkDevice.c
//

// This file contains functions for abstracting and managing network devices.
// This is the portion of the link layer that is not a hardware driver, but
// which does all the interfacing with the hardware drivers.

#include "kernelNetworkDevice.h"
#include "kernelDebug.h"
#include "kernelError.h"
#include "kernelInterrupt.h"
#include "kernelLog.h"
#include "kernelMalloc.h"
#include "kernelMultitasker.h"
#include "kernelNetwork.h"
#include "kernelNetworkArp.h"
#include "kernelNetworkDhcp.h"
#include "kernelNetworkStream.h"
#include "kernelPic.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>
#include <sys/processor.h>

// An array of pointers to all network devices.
static kernelDevice *devices[NETWORK_MAX_DEVICES];
static int numDevices = 0;

// Saved old interrupt handlers
static void **oldIntHandlers = NULL;
static int numOldHandlers = 0;


static void poolPacketRelease(kernelNetworkPacket *packet)
{
	// This is called by kernelNetworkPacketRelease to release packets
	// allocated from the device's packet pool.

	kernelNetworkDevice *netDev = NULL;

	// Check params
	if (!packet)
	{
		kernelError(kernel_error, "NULL parameter");
		return;
	}

	netDev = packet->context;
	if (!netDev)
	{
		kernelError(kernel_error, "No packet device context");
		return;
	}

	if (netDev->packetPool.freePackets >= NETWORK_PACKETS_PER_STREAM)
	{
		kernelError(kernel_error, "Too many free packets");
		return;
	}

	netDev->packetPool.packet[netDev->packetPool.freePackets] = packet;
	netDev->packetPool.freePackets += 1;
}


static kernelNetworkPacket *poolPacketGet(kernelNetworkDevice *netDev)
{
	// Get a packet from the device's packet pool, and add an initial
	// reference count

	kernelNetworkPacket *packet = NULL;

	if (netDev->packetPool.freePackets <= 0)
	{
		kernelError(kernel_error, "No free packets");
		return (packet = NULL);
	}

	netDev->packetPool.freePackets -= 1;
	packet = netDev->packetPool.packet[netDev->packetPool.freePackets];

	if (!packet)
	{
		kernelError(kernel_error, "Free packet is NULL");
		return (packet);
	}

	memset(packet, 0, sizeof(kernelNetworkPacket));
	packet->release = &poolPacketRelease;
	packet->context = (void *) netDev;
	packet->refCount = 1;

	return (packet);
}


static void processHooks(kernelNetworkDevice *netDev,
	kernelNetworkPacket *packet, int input)
{
	kernelLinkedList *list = NULL;
	kernelNetworkPacketStream *theStream = NULL;
	kernelLinkedListItem *iter = NULL;

	// If there are hooks on this device, emit the raw packet data

	if (input)
		list = (kernelLinkedList *) &netDev->inputHooks;
	else
		list = (kernelLinkedList *) &netDev->outputHooks;

	theStream = kernelLinkedListIterStart(list, &iter);
	if (!theStream)
		return;

	while (theStream)
	{
		kernelNetworkPacketStreamWrite(theStream, packet);
		theStream = kernelLinkedListIterNext(list, &iter);
	}
}


static int processLoop(kernelNetworkDevice *netDev __attribute__((unused)),
	kernelNetworkPacket *packet)
{
	// Interpret the link protocol header for loopback (but the loopback
	// protocol has no link header)

	kernelDebug(debug_net, "NETDEV receive %d: loopback msgsz %u",
		netDev->device.recvPackets, packet->length);

	// Assume IP v4 for the time being
	packet->netProtocol = NETWORK_NETPROTOCOL_IP4;

	return (0);
}


static int processEthernet(kernelNetworkDevice *netDev
	 __attribute__((unused)), kernelNetworkPacket *packet)
{
	// Interpret the link protocol header for ethernet

	networkEthernetHeader *header = NULL;
	unsigned short type = 0;

	header = (networkEthernetHeader *) packet->memory;
	type = ntohs(header->type);

	// If the packet is not ethernet IP v4 or ARP, we are finished
	if ((type != NETWORK_ETHERTYPE_IP4) && (type != NETWORK_ETHERTYPE_ARP))
		return (ERR_NOTIMPLEMENTED);

	kernelDebug(debug_net, "NETDEV receive %d: ethernet type=%x "
		"%02x:%02x:%02x:%02x:%02x:%02x -> %02x:%02x:%02x:%02x:%02x:%02x "
		"msgsz %u", netDev->device.recvPackets, ntohs(header->type),
		header->source[0], header->source[1], header->source[2],
		header->source[3], header->source[4], header->source[5],
		header->dest[0], header->dest[1], header->dest[2], header->dest[3],
		header->dest[4], header->dest[5], packet->length);

	if (type == NETWORK_ETHERTYPE_IP4)
		packet->netProtocol = NETWORK_NETPROTOCOL_IP4;
	else if (type == NETWORK_ETHERTYPE_ARP)
		packet->netProtocol = NETWORK_NETPROTOCOL_ARP;

	packet->netHeaderOffset = (packet->linkHeaderOffset +
		sizeof(networkEthernetHeader));

	return (0);
}


static int readData(kernelDevice *dev)
{
	int status = 0;
	kernelNetworkDevice *netDev = NULL;
	kernelNetworkDeviceOps *ops = dev->driver->ops;
	unsigned char buffer[NETWORK_PACKET_MAX_LENGTH];
	kernelNetworkPacket *packet = NULL;

	netDev = dev->data;

	kernelDebug(debug_net, "NETDEV read data from %s", netDev->device.name);

	if (!(netDev->device.flags & NETWORK_DEVICEFLAG_INITIALIZED))
	{
		// We can't process this data, but we can service the device
		if (ops->driverReadData)
			ops->driverReadData(netDev, buffer);

		return (status = 0);
	}

	netDev->device.recvPackets += 1;

	packet = poolPacketGet(netDev);
	if (!packet)
		return (status = ERR_MEMORY);

	if (ops->driverReadData)
		packet->length = ops->driverReadData(netDev, packet->memory);

	// If there's no data, we are finished
	if (!packet->length)
	{
		kernelError(kernel_error, "Packet has no data");
		poolPacketRelease(packet);
		return (status = 0);
	}

	// If there are input hooks on this device, emit the raw packet data
	processHooks(netDev, packet, 1 /* input */);

	// Set up the the packet structure's link and network protocol fields

	packet->linkProtocol = netDev->device.linkProtocol;
	packet->linkHeaderOffset = 0;

	switch (netDev->device.linkProtocol)
	{
		case NETWORK_LINKPROTOCOL_LOOP:
			status = processLoop(netDev, packet);
			break;

		case NETWORK_LINKPROTOCOL_ETHERNET:
			status = processEthernet(netDev, packet);
			break;

		default:
			status = ERR_NOTIMPLEMENTED;
			break;
	}

	if (status < 0)
	{
		kernelNetworkPacketRelease(packet);
		return (status);
	}

	// Set the data section to start at the network header
	packet->dataOffset = packet->netHeaderOffset;
	packet->dataLength = (packet->length - packet->dataOffset);

	// Insert it into the input packet stream
	status = kernelNetworkPacketStreamWrite(&netDev->inputStream, packet);

	kernelNetworkPacketRelease(packet);

	if (status < 0)
	{
		// It would be good if we had a collection of 'deferred packets' for
		// cases like this, so we can try to insert them next time, since by
		// doing this we actually drop the packet
		kernelError(kernel_error, "Couldn't write input stream; packet "
			"dropped");
		netDev->device.recvDropped += 1;
		return (status);
	}

	return (status = 0);
}


static void networkInterrupt(void)
{
	// This is the network interrupt handler.  It calls the network driver
	// for the device in order to actually service the interrupt

	void *address = NULL;
	int interruptNum = 0;
	kernelDevice *dev = NULL;
	kernelNetworkDevice *netDev = NULL;
	kernelNetworkDeviceOps *ops = NULL;
	int serviced = 0;
	int count;

	processorIsrEnter(address);

	// Which interrupt number is active?
	interruptNum = kernelPicGetActive();
	if (interruptNum < 0)
		goto out;

	kernelInterruptSetCurrent(interruptNum);

	// Find the devices that use this interrupt
	for (count = 0; (count < numDevices) && !serviced; count ++)
	{
		if (((kernelNetworkDevice *)
			devices[count]->data)->device.interruptNum == interruptNum)
		{
			dev = devices[count];
			netDev = dev->data;
			ops = dev->driver->ops;

			if (ops->driverInterruptHandler)
			{
				// Call the driver function.
				if (ops->driverInterruptHandler(netDev) >= 0)
				{
					// Read the data from all queued packets
					while (netDev->device.recvQueued)
					{
						if (readData(dev) < 0)
							break;
					}

					serviced = 1;
				}
			}
		}
	}

	if (serviced)
		kernelPicEndOfInterrupt(interruptNum);

	kernelInterruptClearCurrent();

	if (!serviced)
	{
		if (oldIntHandlers[interruptNum])
		{
			// We didn't service this interrupt, and we're sharing this PCI
			// interrupt with another device whose handler we saved.  Call it.
			kernelDebug(debug_net, "NETDEV interrupt not serviced - "
				"chaining");
			processorIsrCall(oldIntHandlers[interruptNum]);
		}
		else
		{
			// We'd better acknowledge the interrupt, or else it wouldn't be
			// cleared, and our controllers using this vector wouldn't receive
			// any more.
			kernelDebugError("Interrupt not serviced and no saved ISR");
			kernelPicEndOfInterrupt(interruptNum);
		}
	}

out:
	processorIsrExit(address);
}


static kernelDevice *findDeviceByName(const char *deviceName)
{
	// Find the named device

	kernelNetworkDevice *netDev = NULL;
	int count;

	for (count = 0; count < numDevices; count ++)
	{
		netDev = devices[count]->data;
		if (!strcmp((char *) netDev->device.name, deviceName))
			return (devices[count]);
	}

	// Not found
	return (NULL);
}


/////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////
//
//  Below here, the functions are exported for external use
//
/////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////

int kernelNetworkDeviceRegister(kernelDevice *dev)
{
	// This function is called by the network drivers' detection functions
	// to tell us about a new device.

	int status = 0;
	kernelNetworkDevice *netDev = NULL;

	// Check params
	if (!dev || !dev->data || !dev->driver || !dev->driver->ops)
	{
		kernelError(kernel_error, "NULL parameter");
		return (status = ERR_NULLPARAMETER);
	}

	netDev = dev->data;

	if (netDev->device.linkProtocol == NETWORK_LINKPROTOCOL_LOOP)
		strcpy((char *) netDev->device.name, "loop");
	else
		sprintf((char *) netDev->device.name, "net%d", numDevices);

	if (netDev->device.interruptNum >= 0)
	{
		// Save any existing handler for the interrupt we're hooking

		if (numOldHandlers <= netDev->device.interruptNum)
		{
			numOldHandlers = (netDev->device.interruptNum + 1);

			oldIntHandlers = kernelRealloc(oldIntHandlers,
				(numOldHandlers * sizeof(void *)));
			if (!oldIntHandlers)
				return (status = ERR_MEMORY);
		}

		if (!oldIntHandlers[netDev->device.interruptNum] &&
			(kernelInterruptGetHandler(netDev->device.interruptNum) !=
				networkInterrupt))
		{
			oldIntHandlers[netDev->device.interruptNum] =
				kernelInterruptGetHandler(netDev->device.interruptNum);
		}

		// Register our interrupt handler for this device
		status = kernelInterruptHook(netDev->device.interruptNum,
			&networkInterrupt, NULL);
		if (status < 0)
			return (status);
	}

	devices[numDevices++] = dev;

	if (netDev->device.interruptNum >= 0)
	{
		// Turn on the interrupt
		status = kernelPicMask(netDev->device.interruptNum, 1);
		if (status < 0)
			return (status);
	}

	// Register the device with the upper-level kernelNetwork functions
	status = kernelNetworkRegister(netDev);
	if (status < 0)
		return (status);

	kernelLog("Added network device %s, link=%s", netDev->device.name,
		((netDev->device.flags & NETWORK_DEVICEFLAG_LINK)? "UP" : "DOWN"));

	return (status = 0);
}


int kernelNetworkDeviceStart(const char *name, int reconfigure)
{
	int status = 0;
	kernelDevice *dev = NULL;
	kernelNetworkDevice *netDev = NULL;
	char hostName[NETWORK_MAX_HOSTNAMELENGTH];
	char domainName[NETWORK_MAX_DOMAINNAMELENGTH];

	// Check params
	if (!name)
	{
		kernelError(kernel_error, "NULL parameter");
		return (status = ERR_NULLPARAMETER);
	}

	// Find the device by name
	dev = findDeviceByName(name);
	if (!dev)
	{
		kernelError(kernel_error, "No such network device \"%s\"", name);
		return (status = ERR_NOSUCHENTRY);
	}

	netDev = dev->data;

	kernelDebug(debug_net, "NETDEV start device %s", netDev->device.name);

	if ((netDev->device.flags & NETWORK_DEVICEFLAG_RUNNING) && !reconfigure)
		// Nothing to do
		return (status = 0);

	if (!(netDev->device.flags & NETWORK_DEVICEFLAG_LINK))
		// No network link
		return (status = ERR_IO);

	// If the device is disabled, don't start it
	if (netDev->device.flags & NETWORK_DEVICEFLAG_DISABLED)
	{
		kernelError(kernel_error, "Network device %s is disabled",
			netDev->device.name);
		return (status = ERR_INVALID);
	}

	// Do we need to (re-)obtain a network address?
	if (networkAddressEmpty(&netDev->device.hostAddress,
		sizeof(networkAddress)) || reconfigure)
	{
		kernelDebug(debug_net, "NETDEV configure %s using DHCP",
			netDev->device.name);

		kernelNetworkGetHostName(hostName, NETWORK_MAX_HOSTNAMELENGTH);
		kernelNetworkGetDomainName(domainName, NETWORK_MAX_DOMAINNAMELENGTH);

		status = kernelNetworkDhcpConfigure(netDev, hostName, domainName,
			NETWORK_DHCP_DEFAULT_TIMEOUT);
		if (status < 0)
		{
			kernelError(kernel_error, "DHCP configuration of network device "
				"%s failed.", netDev->device.name);
			return (status);
		}
	}

	netDev->device.flags |= NETWORK_DEVICEFLAG_RUNNING;

	kernelLog("Network device %s started with IP=%d.%d.%d.%d "
		"netmask=%d.%d.%d.%d", netDev->device.name,
		netDev->device.hostAddress.byte[0],
		netDev->device.hostAddress.byte[1],
		netDev->device.hostAddress.byte[2],
		netDev->device.hostAddress.byte[3],
		netDev->device.netMask.byte[0], netDev->device.netMask.byte[1],
		netDev->device.netMask.byte[2], netDev->device.netMask.byte[3]);

	kernelDebug(debug_net, "NETDEV device %s started", netDev->device.name);

	return (status = 0);
}


int kernelNetworkDeviceStop(const char *name)
{
	int status = 0;
	kernelDevice *dev = NULL;
	kernelNetworkDevice *netDev = NULL;

	// Check params
	if (!name)
	{
		kernelError(kernel_error, "NULL parameter");
		return (status = ERR_NULLPARAMETER);
	}

	// Find the device by name
	dev = findDeviceByName(name);
	if (!dev)
	{
		kernelError(kernel_error, "No such network device \"%s\"", name);
		return (status = ERR_NOSUCHENTRY);
	}

	netDev = dev->data;

	kernelDebug(debug_net, "NETDEV stop device %s", netDev->device.name);

	if ((netDev->device.flags & NETWORK_DEVICEFLAG_LINK) &&
		(netDev->device.flags & NETWORK_DEVICEFLAG_RUNNING))
	{
		// If the device was configured with DHCP, tell the server we're
		// relinquishing the address.
		if (netDev->device.flags & NETWORK_DEVICEFLAG_AUTOCONF)
		{
			kernelNetworkDhcpRelease(netDev);

			// Clear out the things we got from DHCP
			memset((unsigned char *) &netDev->device.hostAddress, 0,
				sizeof(networkAddress));
			memset((unsigned char *) &netDev->device.netMask, 0,
				sizeof(networkAddress));
			memset((unsigned char *) &netDev->device.broadcastAddress, 0,
				sizeof(networkAddress));
			memset((unsigned char *) &netDev->device.gatewayAddress, 0,
				sizeof(networkAddress));
			memset((unsigned char *) &netDev->device.dnsAddress, 0,
				sizeof(networkAddress));
		}
	}

	netDev->device.flags &= ~NETWORK_DEVICEFLAG_RUNNING;

	kernelDebug(debug_net, "NETDEV device %s stopped", netDev->device.name);

	return (status = 0);
}


int kernelNetworkDeviceEnable(const char *name)
{
	int status = 0;
	kernelDevice *dev = NULL;
	kernelNetworkDevice *netDev = NULL;

	// Check params
	if (!name)
	{
		kernelError(kernel_error, "NULL parameter");
		return (status = ERR_NULLPARAMETER);
	}

	// Find the device by name
	dev = findDeviceByName(name);
	if (!dev)
	{
		kernelError(kernel_error, "No such network device \"%s\"", name);
		return (status = ERR_NOSUCHENTRY);
	}

	netDev = dev->data;

	kernelDebug(debug_net, "NETDEV enable device %s", netDev->device.name);

	// If the device was disabled, remove that
	netDev->device.flags &= ~NETWORK_DEVICEFLAG_DISABLED;

	// Try to start it
	status = kernelNetworkDeviceStart(name, 0 /* not reconfiguring */);

	return (status);
}


int kernelNetworkDeviceDisable(const char *name)
{
	int status = 0;
	kernelDevice *dev = NULL;
	kernelNetworkDevice *netDev = NULL;

	// Check params
	if (!name)
	{
		kernelError(kernel_error, "NULL parameter");
		return (status = ERR_NULLPARAMETER);
	}

	// Find the device by name
	dev = findDeviceByName(name);
	if (!dev)
	{
		kernelError(kernel_error, "No such network device \"%s\"", name);
		return (status = ERR_NOSUCHENTRY);
	}

	netDev = dev->data;

	kernelDebug(debug_net, "NETDEV disable device %s", netDev->device.name);

	// Try to stop it
	status = kernelNetworkDeviceStop(name);

	// Mark the device as disabled
	netDev->device.flags |= NETWORK_DEVICEFLAG_DISABLED;

	return (status);
}


int kernelNetworkDeviceSetFlags(const char *name, unsigned flags, int onOff)
{
	// Changes any user-settable flags associated with a network device

	int status = 0;
	kernelDevice *dev = NULL;
	kernelNetworkDevice *netDev = NULL;
	kernelNetworkDeviceOps *ops = NULL;

	// Check params
	if (!name)
	{
		kernelError(kernel_error, "NULL parameter");
		return (status = ERR_NULLPARAMETER);
	}

	// Find the device by name
	dev = findDeviceByName(name);
	if (!dev)
	{
		kernelError(kernel_error, "No such network device \"%s\"", name);
		return (status = ERR_NOSUCHENTRY);
	}

	netDev = dev->data;
	ops = dev->driver->ops;

	// Lock the device
	status = kernelLockGet(&netDev->lock);
	if (status < 0)
		return (status);

	if (ops->driverSetFlags)
		// Call the driver flag-setting function.
		status = ops->driverSetFlags(netDev, flags, onOff);

	// Release the lock
	kernelLockRelease(&netDev->lock);

	return (status);
}


int kernelNetworkDeviceGetAddress(const char *name,
	networkAddress *logicalAddress, networkAddress *physicalAddress)
{
	// This function attempts to use the named network device to determine
	// the physical address of the host with the supplied logical address.
	// The Address Resolution Protocol (ARP) is used for this.

	int status = 0;
	kernelDevice *dev = NULL;
	kernelNetworkDevice *netDev = NULL;
	int arpPosition = 0;
	int count;

	// Check params
	if (!name || !logicalAddress || !physicalAddress)
	{
		kernelError(kernel_error, "NULL parameter");
		return (status = ERR_NULLPARAMETER);
	}

	// Find the device by name
	dev = findDeviceByName(name);
	if (!dev)
	{
		kernelError(kernel_error, "No such network device \"%s\"", name);
		return (status = ERR_NOSUCHENTRY);
	}

	netDev = dev->data;

	// Shortcut (necessary for loopback) if the address is the address of the
	// device itself
	if (networkAddressesEqual(logicalAddress, &netDev->device.hostAddress,
		sizeof(networkAddress)))
	{
		networkAddressCopy(physicalAddress, &netDev->device.hardwareAddress,
			sizeof(networkAddress));
		return (status = 0);
	}

	// Test whether the logical address is in this device's network, using
	// the netmask.  If it's a different network, substitute the address of
	// the default gateway.
	if (!networksEqualIp4(logicalAddress, &netDev->device.netMask,
		&netDev->device.hostAddress))
	{
		kernelDebug(debug_net, "NETDEV routing via default gateway");
		logicalAddress = (networkAddress *) &netDev->device.gatewayAddress;
	}

	// Try up to 6 attempts to get an address.  This is arbitrary.  Is it
	// right?  From network activity, it looks like Linux tries approx 6
	// times, when we don't reply to it; once per second.
	for (count = 0; count < 6; count ++)
	{
		// Is the address in the device's ARP cache?
		arpPosition = kernelNetworkArpSearchCache(netDev, logicalAddress);
		if (arpPosition >= 0)
		{
			// Found it.
			kernelDebug(debug_net, "NETDEV found ARP cache request");
			networkAddressCopy(physicalAddress,
				&netDev->arpCache[arpPosition].physicalAddress,
				sizeof(networkAddress));
			return (status = 0);
		}

		// Construct and send our ethernet packet with the ARP request
		// (not queued; immediately)
		status = kernelNetworkArpSend(netDev, logicalAddress, NULL,
			NETWORK_ARPOP_REQUEST, 1 /* immediate */);
		if (status < 0)
			return (status);

		// Expect a quick reply the first time
		if (!count)
			kernelMultitaskerYield();
		else
			// Delay for 1/2 second
			kernelMultitaskerWait(500);
	}

	// If we fall through, we didn't find it.
	return (status = ERR_NOSUCHENTRY);
}


int kernelNetworkDeviceSend(const char *name, kernelNetworkPacket *packet)
{
	// Send a prepared packet using the named network device

	int status = 0;
	kernelDevice *dev = NULL;
	kernelNetworkDevice *netDev = NULL;
	kernelNetworkDeviceOps *ops = NULL;

	// Check params
	if (!name || !packet)
	{
		kernelError(kernel_error, "NULL parameter");
		return (status = ERR_NULLPARAMETER);
	}

	kernelDebug(debug_net, "NETDEV send %u on %s", packet->length, name);

	// Find the device by name
	dev = findDeviceByName(name);
	if (!dev)
	{
		kernelError(kernel_error, "No such network device \"%s\"", name);
		return (status = ERR_NOSUCHENTRY);
	}

	if (!packet->length)
		// Nothing to do?  Hum.
		return (status = 0);

	netDev = dev->data;
	ops = dev->driver->ops;

	// If there are output hooks on this device, emit the raw packet data
	processHooks(netDev, packet, 0 /* output */);

	// Lock the device
	status = kernelLockGet(&netDev->lock);
	if (status < 0)
		return (status);

	if (ops->driverWriteData)
	{
		// Call the driver transmit function.
		status = ops->driverWriteData(netDev, packet->memory,
			packet->length);
	}

	// Release the lock
	kernelLockRelease(&netDev->lock);

	// Wait until all packets are transmitted before returning, since the
	// memory is needed by the device
	while (netDev->device.transQueued)
		kernelMultitaskerYield();

	if (status >= 0)
	{
		netDev->device.transPackets += 1;

		switch (netDev->device.linkProtocol)
		{
			case NETWORK_LINKPROTOCOL_LOOP:
			{
				kernelDebug(debug_net, "NETDEV send %d: loopback msgsz %d",
					netDev->device.transPackets, packet->length);
				break;
			}

			case NETWORK_LINKPROTOCOL_ETHERNET:
			{
			#if defined(DEBUG)
				networkEthernetHeader *header = (networkEthernetHeader *)
					packet->memory;

				kernelDebug(debug_net, "NETDEV send %d: ethernet type=%x "
					"%02x:%02x:%02x:%02x:%02x:%02x -> "
					"%02x:%02x:%02x:%02x:%02x:%02x msgsz %d",
					netDev->device.transPackets, ntohs(header->type),
					header->source[0], header->source[1], header->source[2],
					header->source[3], header->source[4], header->source[5],
					header->dest[0], header->dest[1], header->dest[2],
					header->dest[3], header->dest[4], header->dest[5],
					packet->length);
			#endif
				break;
			}
		}
	}

	// If the device is a loop device, attempt to process the input now
	if (netDev->device.linkProtocol == NETWORK_LINKPROTOCOL_LOOP)
		readData(dev);

	return (status);
}


int kernelNetworkDeviceGetCount(void)
{
	// Returns the count of real network devices (not including loopback)

	int devCount = 0;
	kernelNetworkDevice *netDev = NULL;
	int count;

	for (count = 0; count < numDevices; count ++)
	{
		netDev = devices[count]->data;

		if (netDev->device.linkProtocol != NETWORK_LINKPROTOCOL_LOOP)
			devCount += 1;
	}

	return (devCount);
}


int kernelNetworkDeviceGet(const char *name, networkDevice *dev)
{
	// Returns the user-space portion of the requested (by name) network
	// device.

	int status = 0;
	kernelDevice *kernelDev = NULL;
	kernelNetworkDevice *netDev = NULL;

	// Check params
	if (!name || !dev)
	{
		kernelError(kernel_error, "NULL parameter");
		return (status = ERR_NULLPARAMETER);
	}

	// Find the device by name
	kernelDev = findDeviceByName(name);
	if (!kernelDev)
	{
		kernelError(kernel_error, "No such network device \"%s\"", name);
		return (status = ERR_NOSUCHENTRY);
	}

	netDev = kernelDev->data;

	memcpy(dev, (networkDevice *) &netDev->device, sizeof(networkDevice));

	return (status = 0);
}


int kernelNetworkDeviceHook(const char *name, void **streamPtr, int input)
{
	// Allocates a new network packet stream and associates it with the
	// named device, 'hooking' either the input or output, and returning a
	// pointer to the stream.

	int status = 0;
	kernelDevice *kernelDev = NULL;
	kernelNetworkDevice *netDev = NULL;
	kernelNetworkPacketStream *theStream = NULL;
	kernelLinkedList *list = NULL;

	// Check params
	if (!name || !streamPtr)
	{
		kernelError(kernel_error, "NULL parameter");
		return (status = ERR_NULLPARAMETER);
	}

	// Find the device by name
	kernelDev = findDeviceByName(name);
	if (!kernelDev)
	{
		kernelError(kernel_error, "No such network device \"%s\"", name);
		return (status = ERR_NOSUCHENTRY);
	}

	netDev = kernelDev->data;

	*streamPtr = kernelMalloc(sizeof(kernelNetworkPacketStream));
	if (!*streamPtr)
	{
		kernelError(kernel_error, "Couldn't allocate network packet stream");
		return (status = ERR_MEMORY);
	}

	theStream = *streamPtr;

	// Try to get a new network packet stream
	status = kernelNetworkPacketStreamNew(theStream);
	if (status < 0)
	{
		kernelError(kernel_error, "Couldn't allocate network packet stream");
		kernelFree(*streamPtr);
		return (status);
	}

	// Which list are we adding to?
	if (input)
		list = (kernelLinkedList *) &netDev->inputHooks;
	else
		list = (kernelLinkedList *) &netDev->outputHooks;

	// Add it to the list
	status = kernelLinkedListAdd(list, *streamPtr);
	if (status < 0)
	{
		kernelError(kernel_error, "Couldn't link network packet stream");
		kernelNetworkPacketStreamDestroy(theStream);
		kernelFree(*streamPtr);
		return (status);
	}

	return (status = 0);
}


int kernelNetworkDeviceUnhook(const char *name, void *streamPtr, int input)
{
	// 'Unhooks' the supplied network packet stream from the input or output
	// of the named device and deallocates the stream.

	int status = 0;
	kernelDevice *kernelDev = NULL;
	kernelNetworkDevice *netDev = NULL;
	kernelNetworkPacketStream *theStream = streamPtr;
	kernelLinkedList *list = NULL;

	// Check params
	if (!name || !streamPtr)
	{
		kernelError(kernel_error, "NULL parameter");
		return (status = ERR_NULLPARAMETER);
	}

	// Find the device by name
	kernelDev = findDeviceByName(name);
	if (!kernelDev)
	{
		kernelError(kernel_error, "No such network device \"%s\"", name);
		return (status = ERR_NOSUCHENTRY);
	}

	netDev = kernelDev->data;

	// Which list are we removing from?
	if (input)
		list = (kernelLinkedList *) &netDev->inputHooks;
	else
		list = (kernelLinkedList *) &netDev->outputHooks;

	// Remove it from the list
	status = kernelLinkedListRemove(list, streamPtr);
	if (status < 0)
	{
		kernelError(kernel_error, "Couldn't unlink network packet stream");
		return (status);
	}

	kernelNetworkPacketStreamDestroy(theStream);
	kernelFree(streamPtr);

	return (status = 0);
}


unsigned kernelNetworkDeviceSniff(void *streamPtr, unsigned char *buffer,
	unsigned len)
{
	// Given a pointer to a network packet stream 'hooked' to the input or
	// output of a device, attempt to retrieve a packet, and copy at most the
	// requested number of bytes to the buffer.

	unsigned bytes = 0;
	kernelNetworkPacketStream *theStream = streamPtr;
	kernelNetworkPacket *packet = NULL;

	// Check params
	if (!streamPtr || !buffer)
	{
		kernelError(kernel_error, "NULL parameter");
		return (bytes = 0);
	}

	// Try to read a packet
	if (kernelNetworkPacketStreamRead(theStream, &packet) < 0)
		return (bytes = 0);

	bytes = min(len, packet->length);

	// Copy data
	memcpy(buffer, packet->memory, bytes);

	kernelNetworkPacketRelease(packet);

	return (bytes);
}

