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
//  kernelNetworkLoopDriver.c
//

// Driver for the loopback virtual network device

#include "kernelNetworkLoopDriver.h" // Contains my prototypes
#include "kernelDebug.h"
#include "kernelMalloc.h"
#include "kernelNetwork.h"
#include "kernelNetworkDevice.h"
#include <string.h>
#include <sys/network.h>


static unsigned driverReadData(kernelNetworkDevice *netDev,
	unsigned char *buffer)
{
	// Copies 1 network packet's worth data from our packet ring to the
	// supplied pointer, if any are currently queued.  Advances the head of
	// queued packets, and returns the number of bytes copied.

	unsigned messageLen = 0;
	loopDevice *loop = NULL;
	loopPacket *packet = NULL;

	// Check params
	if (!netDev || !buffer)
		return (messageLen = 0);

	loop = netDev->data;

	if (loop->tail != loop->head)
	{
		packet = &loop->packets[loop->head];

		if (packet->len && packet->data)
		{
			messageLen = packet->len;
			memcpy(buffer, packet->data, messageLen);

			kernelFree(packet->data);
		}

		packet->len = 0;
		packet->data = NULL;

		// Advance the head
		loop->head += 1;
		if (loop->head >= LOOP_QUEUE_LEN)
			loop->head = 0;
	}

	if (messageLen)
		kernelDebug(debug_net, "NETLOOP read data, %u bytes", messageLen);

	return (messageLen);
}


static int driverWriteData(kernelNetworkDevice *netDev, unsigned char *buffer,
	unsigned bufferLen)
{
	// Copies 1 network packet's worth data from the supplied pointer to our
	// packet ring.  Advances the tail of queued packets, and also the head
	// if the queue is full.

	int status = 0;
	loopDevice *loop = NULL;
	loopPacket *packet = NULL;

	// Check params
	if (!netDev || !buffer)
		return (status = ERR_NULLPARAMETER);

	kernelDebug(debug_net, "NETLOOP write data, %u bytes", bufferLen);

	loop = netDev->data;
	packet = &loop->packets[loop->tail];

	packet->len = bufferLen;

	if (bufferLen)
	{
		// Allocate memory; packets are not 'queued' for sending here, as with
		// real devices.  Here, the data is (virtually speaking) 'on the wire'
		// by the time this function returns, and the caller is allowed to
		// free/reuse the buffer.

		packet->data = kernelMalloc(bufferLen);
		if (!packet->data)
			return (status = ERR_MEMORY);

		memcpy(packet->data, buffer, bufferLen);
	}

	// Advance the tail
	loop->tail += 1;
	if (loop->tail >= LOOP_QUEUE_LEN)
		loop->tail = 0;

	// If the tail met the head, advance the head also
	if (loop->tail == loop->head)
	{
		packet = &loop->packets[loop->head];

		if (packet->data)
			kernelFree(packet->data);

		packet->len = 0;
		packet->data = NULL;

		// Advance the head
		loop->head += 1;
		if (loop->head >= LOOP_QUEUE_LEN)
			loop->head = 0;
	}

	return (status = 0);
}


static kernelNetworkDeviceOps networkOps = {
	NULL, /* driverInterruptHandler */
	NULL, /* driverSetFlags */
	driverReadData,
	driverWriteData,
};


/////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////
//
// Below here, the functions are exported for external use
//
/////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////

int kernelNetworkLoopDeviceRegister(void)
{
	// Set up and register a loopback virtual network device

	int status = 0;
	kernelDevice *dev = NULL;
	kernelNetworkDevice *netDev = NULL;
	loopDevice *loop = NULL;

	kernelDebug(debug_net, "NETLOOP register device");

	// Allocate memory for the device
	dev = kernelMalloc(sizeof(kernelDevice));
	if (!dev)
		return (status = ERR_MEMORY);

	dev->driver = kernelMalloc(sizeof(kernelDriver));
	if (!dev->driver)
	{
		kernelFree(dev);
		return (status = ERR_MEMORY);
	}

	dev->driver->ops = &networkOps;

	netDev = kernelMalloc(sizeof(kernelNetworkDevice));
	if (!netDev)
	{
		kernelFree(dev->driver);
		kernelFree(dev);
		return (status = ERR_MEMORY);
	}

	dev->data = (void *) netDev;

	loop = kernelMalloc(sizeof(loopDevice));
	if (!loop)
	{
		kernelFree((void *) netDev);
		kernelFree(dev->driver);
		kernelFree(dev);
		return (status = ERR_MEMORY);
	}

	netDev->data = loop;

	netDev->device.flags = (NETWORK_DEVICEFLAG_LINK |
		NETWORK_DEVICEFLAG_PROMISCUOUS | NETWORK_DEVICEFLAG_AUTOSTRIP |
		NETWORK_DEVICEFLAG_AUTOPAD | NETWORK_DEVICEFLAG_AUTOCRC);
	networkAddressCopy(&netDev->device.hostAddress,
		&NETWORK_LOOPBACK_ADDR_IP4, NETWORK_ADDRLENGTH_IP4);
	networkAddressCopy(&netDev->device.netMask,
		&NETWORK_LOOPBACK_NETMASK_IP4, NETWORK_ADDRLENGTH_IP4);
	netDev->device.linkProtocol = NETWORK_LINKPROTOCOL_LOOP;
	netDev->device.interruptNum = -1;

	// Register the network device
	status = kernelNetworkDeviceRegister(dev);
	if (status < 0)
	{
		kernelFree(loop);
		kernelFree((void *) netDev);
		kernelFree(dev->driver);
		kernelFree(dev);
		return (status);
	}

	return (status = 0);
}

