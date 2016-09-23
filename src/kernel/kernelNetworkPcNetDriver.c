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
//  kernelNetworkPcNetDriver.c
//

// Driver for PcNet ethernet network adapters.  Based in part on a driver
// contributed by Jonas Zaddach: See the files in the directory
// contrib/jonas-net

#include "kernelDriver.h" // Contains my prototypes
#include "kernelNetworkPcNetDriver.h"
#include "kernelBus.h"
#include "kernelError.h"
#include "kernelMalloc.h"
#include "kernelMemory.h"
#include "kernelNetworkDevice.h"
#include "kernelPage.h"
#include "kernelParameters.h"
#include "kernelPciDriver.h"
#include "kernelVariableList.h"
#include <string.h>
#include <sys/processor.h>

static struct {
	unsigned version;
	char *vendor;
	char *model;

} pcNetVendorModel[] = {
	{ 0x2420, "AMD", "PCnet/PCI 79C970" },		// PCI
	{ 0x2621, "AMD", "PCnet/PCI II 79C970A" },	// PCI
	{ 0x2623, "AMD", "PCnet/FAST 79C971" },		// PCI
	{ 0x2624, "AMD", "PCnet/FAST+ 79C972" },	// PCI
	{ 0x2625, "AMD", "PCnet/FAST III 79C973" },	// PCI
	{ 0x2626, "AMD", "PCnet/Home 79C978" },		// PCI
	{ 0x2627, "AMD", "PCnet/FAST III 79C975" },	// PCI
	{ 0x2628, "AMD", "PCnet/PRO 79C976" },
	{ 0, NULL, NULL }
};


static void reset(pcNetDevice *pcNet)
{
	unsigned tmp;

	// 32-bit reset, by doing a 32-bit read from the 32-bit reset port.
	processorInPort32((pcNet->ioAddress + PCNET_PORTOFFSET32_RESET), tmp);

	// Then 16-bit reset, by doing a 16-bit read from the 16-bit reset port,
	// so the chip is reset and in 16-bit mode.
	processorInPort16((pcNet->ioAddress + PCNET_PORTOFFSET16_RESET), tmp);

	// The NE2100 PCNET card needs an extra write access to follow
	processorOutPort16((pcNet->ioAddress + PCNET_PORTOFFSET16_RESET), tmp);
}


static unsigned readCSR(pcNetDevice *pcNet, int idx)
{
	// Read the indexed 16-bit control status register (CSR)

	unsigned short data = 0;

	processorOutPort16((pcNet->ioAddress + PCNET_PORTOFFSET16_RAP), idx);
	processorInPort16((pcNet->ioAddress + PCNET_PORTOFFSET_RDP), data);
	return (data);
}


static void writeCSR(pcNetDevice *pcNet, int idx, unsigned data)
{
	// Write the indexed 16-bit control status register (CSR)

	// 16-bit only
	data &= 0xFFFF;

	processorOutPort16((pcNet->ioAddress + PCNET_PORTOFFSET16_RAP), idx);
	processorOutPort16((pcNet->ioAddress + PCNET_PORTOFFSET_RDP), data);
	return;
}


static void modifyCSR(pcNetDevice *pcNet, int idx, unsigned data, opType op)
{
	// Read the indexed 16-bit control status register (CSR), then do logical
	// AND or OR with the supplied data, and write it back.

	unsigned short contents = 0;

	// 16-bit only
	data &= 0xFFFF;

	processorOutPort16((pcNet->ioAddress + PCNET_PORTOFFSET16_RAP), idx);
	processorInPort16((pcNet->ioAddress + PCNET_PORTOFFSET_RDP), contents);

	if (op == op_or)
		contents |= data;
	else if (op == op_and)
		contents &= data;

	processorOutPort16((pcNet->ioAddress + PCNET_PORTOFFSET_RDP), contents);
	return;
}


static unsigned readBCR(pcNetDevice *pcNet, int idx)
{
	// Read the indexed 16-bit bus configuration register (BCR)

	unsigned short data = 0;

	processorOutPort16((pcNet->ioAddress + PCNET_PORTOFFSET16_RAP), idx);
	processorInPort16((pcNet->ioAddress + PCNET_PORTOFFSET16_BDP), data);
	return (data);
}


static void writeBCR(pcNetDevice *pcNet, int idx, unsigned data)
{
	// Write the indexed 16-bit bus configuration register (BCR)

	processorOutPort16((pcNet->ioAddress + PCNET_PORTOFFSET16_RAP), idx);
	processorOutPort16((pcNet->ioAddress + PCNET_PORTOFFSET16_BDP), data);
	return;
}


static void driverInterruptHandler(kernelNetworkDevice *adapter)
{
	// This is the 'body' of the interrupt handler for pcNet devices.  Called
	// from the netorkInterrupt() function in kernelNetwork.c

	unsigned csr0 = 0;
	pcNetDevice *pcNet = NULL;
	int head = 0;
	int *tail = NULL;
	unsigned short flags;

	// Check params
	if (!adapter)
		return;

	pcNet = adapter->data;

	// Get the contents of the status register
	csr0 = readCSR(pcNet, PCNET_CSR_STATUS);

	// Disable pcNet interrupts and clear interrupt status
	csr0 &= ~PCNET_CSR_STATUS_IENA;
	writeCSR(pcNet, PCNET_CSR_STATUS, csr0); // Should this be done later?

	// Check for collision errors
	if (csr0 & PCNET_CSR_STATUS_CERR)
		adapter->device.collisions += 1;

	// Why the interrupt, bub?
	if (csr0 & PCNET_CSR_STATUS_RINT)
	{
		// Received

		// If there were general errors in reception, update the error
		// statistics
		if (csr0 & PCNET_CSR_STATUS_ERR)
		{
			adapter->device.recvErrors += 1;
			if (csr0 & PCNET_CSR_STATUS_MISS)
				adapter->device.recvOverruns += 1;
		}

		// Count the number of queued receive packets
		head = pcNet->recvRing.head;
		while ((adapter->device.recvQueued < adapter->device.recvQueueLen) &&
			!(pcNet->recvRing.desc.recv[head].flags & PCNET_DESCFLAG_OWN))
		{
			flags = pcNet->recvRing.desc.recv[head].flags;

			// Check for receive errors with this packet
			if (flags & PCNET_DESCFLAG_ERR)
			{
				adapter->device.recvErrors += 1;
				if ((flags & PCNET_DESCFLAG_RECV_FRAM) ||
					(flags & PCNET_DESCFLAG_RECV_OFLO) ||
					(flags & PCNET_DESCFLAG_RECV_CRC))
				{
					adapter->device.recvDropped += 1;
				}
			}

			// Increase the count of packets queued for receiving
			adapter->device.recvQueued += 1;

			// Move to the next receive descriptor
			head += 1;
			if (head >= adapter->device.recvQueueLen)
				head = 0;

			if (head == pcNet->recvRing.head)
				// We wrapped all the way around.
				break;
		}
	}

	if (csr0 & PCNET_CSR_STATUS_TINT)
	{
		// Transmitted

		// If there were general errors in tranmission, update the error
		// statistics
		if (csr0 & PCNET_CSR_STATUS_ERR)
		{
			adapter->device.transErrors += 1;
			if (csr0 & PCNET_CSR_STATUS_MISS)
				adapter->device.transOverruns += 1;
		}

		// Loop for each transmitted packet
		tail = &pcNet->transRing.tail;
		while (adapter->device.transQueued &&
			!(pcNet->transRing.desc.trans[*tail].flags & PCNET_DESCFLAG_OWN))
		{
			flags = pcNet->transRing.desc.trans[*tail].flags;

			// Check for transmit errors with this packet
			if (flags & PCNET_DESCFLAG_ERR)
			{
				adapter->device.transErrors += 1;
				if ((flags & PCNET_DESCFLAG_TRANS_UFLO) ||
					(flags & PCNET_DESCFLAG_TRANS_LCOL) ||
					(flags & PCNET_DESCFLAG_TRANS_LCAR) ||
					(flags & PCNET_DESCFLAG_TRANS_RTRY))
				{
					adapter->device.transDropped += 1;
				}
			}

			// Reduce the counter of packets queued for transmission
			adapter->device.transQueued -= 1;

			// Move to the next transmit descriptor
			*tail += 1;
			if (*tail >= adapter->device.transQueueLen)
				*tail = 0;
		}
	}

	// Reenable pcNet interrupts
	modifyCSR(pcNet, PCNET_CSR_STATUS, PCNET_CSR_STATUS_IENA, op_or);

	return;
}


static int driverSetFlags(kernelNetworkDevice *adapter, unsigned flags,
	int onOff)
{
	// Changes any user-settable flags associated with the adapter

	int status = 0;
	pcNetDevice *pcNet = NULL;

	// Check params
	if (!adapter)
		return (status = ERR_NULLPARAMETER);

	pcNet = adapter->data;

	// Change any flags that are settable for this NIC.  Ignore any that aren't
	// supported.

	if (flags & NETWORK_ADAPTERFLAG_AUTOSTRIP)
	{
		if (onOff)
		{
			modifyCSR(pcNet, PCNET_CSR_FEAT, PCNET_CSR_FEAT_ASTRPRCV, op_or);
			adapter->device.flags |= NETWORK_ADAPTERFLAG_AUTOSTRIP;
		}
		else
		{
			modifyCSR(pcNet, PCNET_CSR_FEAT, ~PCNET_CSR_FEAT_ASTRPRCV, op_and);
			adapter->device.flags &= ~NETWORK_ADAPTERFLAG_AUTOSTRIP;
		}
	}

	if (flags & NETWORK_ADAPTERFLAG_AUTOPAD)
	{
		if (onOff)
		{
			modifyCSR(pcNet, PCNET_CSR_FEAT, PCNET_CSR_FEAT_APADXMT, op_or);
			adapter->device.flags |= NETWORK_ADAPTERFLAG_AUTOPAD;
		}
		else
		{
			modifyCSR(pcNet, PCNET_CSR_FEAT, ~PCNET_CSR_FEAT_APADXMT, op_and);
			adapter->device.flags &= ~NETWORK_ADAPTERFLAG_AUTOPAD;
		}
	}

	if (flags & NETWORK_ADAPTERFLAG_AUTOCRC)
	{
	}

	return (status = 0);
}


static unsigned driverReadData(kernelNetworkDevice *adapter,
	unsigned char *buffer)
{
	// This routine copies 1 network packet's worth data from our ring buffer
	// to the supplied frame pointer, if any are currently queued.  Decrements
	// the count of queued packets, and returns the number of bytes copied into
	// the frame pointer.

	unsigned messageLength = 0;
	pcNetDevice *pcNet = NULL;
	int *head = NULL;
	pcNetRecvDesc16 *recvDesc = NULL;

	// Check params
	if (!adapter || !buffer)
		return (messageLength = 0);

	pcNet = adapter->data;
	head = &pcNet->recvRing.head;
	recvDesc = pcNet->recvRing.desc.recv;

	if (adapter->device.recvQueued &&
		!(recvDesc[*head].flags & PCNET_DESCFLAG_OWN))
	{
		messageLength = (unsigned) recvDesc[*head].messageSize;

		memcpy(buffer, pcNet->recvRing.buffers[*head], messageLength);

		adapter->device.recvQueued -= 1;

		// Return ownership to the controller
		recvDesc[*head].flags |= PCNET_DESCFLAG_OWN;

		*head += 1;
		if (*head >= adapter->device.recvQueueLen)
			*head = 0;
	}

	return (messageLength);
}


static int driverWriteData(kernelNetworkDevice *adapter,
	unsigned char *buffer, unsigned bufferSize)
{
	// This routine writes network packet data

	int status = 0;
	pcNetDevice *pcNet = NULL;
	pcNetTransDesc16 *transDesc = NULL;
	int *head = NULL;
	unsigned bufferPhysical = NULL;

	// Check params
	if (!adapter || !buffer)
		return (status = ERR_NULLPARAMETER);

	pcNet = adapter->data;
	transDesc = pcNet->transRing.desc.trans;
	head = &pcNet->transRing.head;

	// Make sure we've got room for another packet
	if (adapter->device.transQueued >= adapter->device.transQueueLen)
		return (status = ERR_NOFREE);

	if (!(transDesc[*head].flags & PCNET_DESCFLAG_OWN))
	{
		// Get the physical address of the buffer.
		bufferPhysical = kernelPageGetPhysical(KERNELPROCID, buffer);
		if (!bufferPhysical)
		{
			kernelError(kernel_error, "Unable to get memory physical address.");
			return (status = ERR_MEMORY);
		}

		transDesc[*head].buffAddrLow =
			(unsigned short)((unsigned) bufferPhysical & 0xFFFF);
		transDesc[*head].buffAddrHigh =
			(unsigned short)(((unsigned) bufferPhysical & 0x00FF0000) >> 16);
		transDesc[*head].bufferSize =
			(unsigned short)(0xF000 | (((short) -bufferSize) & 0x0FFF));
		transDesc[*head].transFlags = 0;

		adapter->device.transQueued += 1;

		// Set the start packet and end packet bits, and give the descriptor
		// to the controller for transmitting.
		transDesc[*head].flags = (PCNET_DESCFLAG_OWN | PCNET_DESCFLAG_STP |
			PCNET_DESCFLAG_ENP);

		*head += 1;
		if (*head >= adapter->device.transQueueLen)
			*head = 0;
	}

	return (status = 0);
}


static int driverDetect(void *parent __attribute__((unused)),
	kernelDriver *driver)
{
	// This routine is used to detect and initialize each device, as well as
	// registering each one with any higher-level interfaces.  Also issues the
	// appropriate commands to the netork adapter to initialize it.

	int status = 0;
	kernelBusTarget *busTargets = NULL;
	int numBusTargets = 0;
	pciDeviceInfo pciDevInfo;
	kernelDevice *dev = NULL;
	kernelNetworkDevice *adapter = NULL;
	pcNetDevice *pcNet = NULL;
	unsigned ioSpaceSize = 0;
	kernelIoMemory recvBuff;
	void *receiveBuffer = NULL;
	unsigned receiveBufferPhysical = 0;
	kernelIoMemory recvRing;
	kernelIoMemory transRing;
	kernelIoMemory init;
	pcNetInitBlock16 *initBlock = NULL;
	int deviceCount, count, shift;

	// Search the PCI bus(es) for devices
	numBusTargets = kernelBusGetTargets(bus_pci, &busTargets);
	if (numBusTargets <= 0)
		return (status = ERR_NODATA);

	// Search the bus targets for ethernet adapters
	for (deviceCount = 0; deviceCount < numBusTargets; deviceCount ++)
	{
		// If it's not an ethernet device, skip it
		if (!busTargets[deviceCount].class ||
			(busTargets[deviceCount].class->class != DEVICECLASS_NETWORK) ||
			!busTargets[deviceCount].subClass ||
			(busTargets[deviceCount].subClass->class !=
				DEVICESUBCLASS_NETWORK_ETHERNET))
		{
			continue;
		}

		// Get the PCI device header
		status = kernelBusGetTargetInfo(&busTargets[deviceCount], &pciDevInfo);
		if (status < 0)
			continue;

		// Check for our vendor and device IDs
		if ((pciDevInfo.device.vendorID != PCNET_VENDOR_ID) ||
			(pciDevInfo.device.deviceID != PCNET_DEVICE_ID))
		{
			continue;
		}

		// Make sure it's a non-bridge header
		if (pciDevInfo.device.headerType != PCI_HEADERTYPE_NORMAL)
			continue;

		// After this point, we know we have a supported device.

		// Enable the device on the PCI bus as a bus master
		if ((kernelBusDeviceEnable(&busTargets[deviceCount],
				PCI_COMMAND_IOENABLE) < 0) ||
			(kernelBusSetMaster(&busTargets[deviceCount], 1) < 0))
		{
			continue;
		}

		// Check the first base address for I/O and memory addresses.
		// For the time being, we are only implementing I/O mapping, as opposed
		// to memory sharing.  Therefore we expect the first base address
		// register to contain an I/O address, which is signified by bit 0
		// being set.
		if (!(pciDevInfo.device.nonBridge.baseAddress[0] & 1))
		{
			kernelError(kernel_error, "Unknown adapter I/O address");
			continue;
		}

		// Allocate memory for the device
		dev = kernelMalloc(sizeof(kernelDevice));
		if (!dev)
		{
			kernelFree(busTargets);
			return (status = ERR_MEMORY);
		}

		adapter = kernelMalloc(sizeof(kernelNetworkDevice));
		if (!adapter)
		{
			kernelFree(busTargets);
			kernelFree(dev);
			return (status = ERR_MEMORY);
		}

		pcNet = kernelMalloc(sizeof(pcNetDevice));
		if (!pcNet)
		{
			kernelFree(busTargets);
			kernelFree(dev);
			kernelFree((void *) adapter);
			return (status = ERR_MEMORY);
		}

		adapter->data = pcNet;

		pcNet->ioAddress =
			(void *)(pciDevInfo.device.nonBridge.baseAddress[0] & 0xFFFFFFFC);

		// Determine the I/O space size.  Write all 1s to the register.
		kernelBusWriteRegister(&busTargets[deviceCount],
			PCI_CONFREG_BASEADDRESS0_32, 32, 0xFFFFFFFF);

		shift = 2;
		pcNet->ioSpaceSize = 4;
		ioSpaceSize = kernelBusReadRegister(&busTargets[deviceCount],
			PCI_CONFREG_BASEADDRESS0_32, 32);
		while (!((ioSpaceSize >> shift++) & 1))
			pcNet->ioSpaceSize *= 2;

		// Restore the register we clobbered.
		kernelBusWriteRegister(&busTargets[deviceCount],
			PCI_CONFREG_BASEADDRESS0_32, 32,
			pciDevInfo.device.nonBridge.baseAddress[0]);

		adapter->device.flags =
			(NETWORK_ADAPTERFLAG_AUTOPAD | NETWORK_ADAPTERFLAG_AUTOSTRIP |
				NETWORK_ADAPTERFLAG_AUTOCRC);
		adapter->device.linkProtocol = NETWORK_LINKPROTOCOL_ETHERNET;
		adapter->device.interruptNum =
			pciDevInfo.device.nonBridge.interruptLine;
		adapter->device.recvQueueLen = PCNET_NUM_RINGBUFFERS;
		adapter->device.transQueueLen = PCNET_NUM_RINGBUFFERS;

		// Reset it
		reset(pcNet);

		// Stop the adapter
		writeCSR(pcNet, PCNET_CSR_STATUS, PCNET_CSR_STATUS_STOP);

		// Get the ethernet address
		for (count = 0; count < 6; count ++)
			processorInPort8((pcNet->ioAddress + count),
				adapter->device.hardwareAddress.bytes[count]);

		// Get chip version and set the model name
		pcNet->chipVersion = ((readCSR(pcNet, PCNET_CSR_MODEL0) & 0x0FFF) << 4);
		pcNet->chipVersion |=
			((readCSR(pcNet, PCNET_CSR_MODEL1) & 0xF000) >> 12);

		kernelVariableListCreate(&dev->device.attrs);
		kernelVariableListSet(&dev->device.attrs, DEVICEATTRNAME_VENDOR,
			"unknown");
		kernelVariableListSet(&dev->device.attrs, DEVICEATTRNAME_MODEL,
			"PCNET");
		for (count = 0; pcNetVendorModel[count].version; count ++)
		{
			if (pcNetVendorModel[count].version == pcNet->chipVersion)
			{
				kernelVariableListSet(&dev->device.attrs,
					DEVICEATTRNAME_VENDOR, pcNetVendorModel[count].vendor);
				kernelVariableListSet(&dev->device.attrs,
					DEVICEATTRNAME_MODEL, pcNetVendorModel[count].model);
				break;
			}
		}

		// Get space for the buffers
		status = kernelMemoryGetIo((PCNET_NUM_RINGBUFFERS *
			PCNET_RINGBUFFER_SIZE), 0, &recvBuff);
		if (status < 0)
			continue;

		receiveBuffer = recvBuff.virtual;
		receiveBufferPhysical = recvBuff.physical;

		// Set up the receive ring descriptors
		pcNet->recvRing.head = 0;
		pcNet->recvRing.tail = 0;

		status = kernelMemoryGetIo((PCNET_NUM_RINGBUFFERS *
			sizeof(pcNetRecvDesc16)), 0, &recvRing);
		if (status < 0)
		{
			kernelMemoryReleaseIo(&recvBuff);
			continue;
		}

		pcNet->recvRing.desc.recv = recvRing.virtual;
		for (count = 0; count < PCNET_NUM_RINGBUFFERS; count ++)
		{
			pcNet->recvRing.desc.recv[count].buffAddrLow = (unsigned short)
				(receiveBufferPhysical & 0xFFFF);
			pcNet->recvRing.desc.recv[count].buffAddrHigh = (unsigned short)
				((receiveBufferPhysical & 0x00FF0000) >> 16);
			pcNet->recvRing.desc.recv[count].flags = PCNET_DESCFLAG_OWN;
			pcNet->recvRing.desc.recv[count].bufferSize = (unsigned short)
				(0xF000 | (((short) -PCNET_RINGBUFFER_SIZE) & 0x0FFF));
			receiveBufferPhysical += PCNET_RINGBUFFER_SIZE;
			pcNet->recvRing.buffers[count] = receiveBuffer;
			receiveBuffer += PCNET_RINGBUFFER_SIZE;
		}

		// Set up the transmit ring descriptors
		pcNet->transRing.head = 0;
		pcNet->transRing.tail = 0;

		status = kernelMemoryGetIo((PCNET_NUM_RINGBUFFERS *
			sizeof(pcNetTransDesc16)), 0, &transRing);
		if (status < 0)
		{
			kernelMemoryReleaseIo(&recvRing);
			kernelMemoryReleaseIo(&recvBuff);
			continue;
		}

		pcNet->transRing.desc.trans = transRing.virtual;

		// Set up the initialization registers.

		// Set the software style as 0 == 16-bit PCNET
		writeCSR(pcNet, PCNET_CSR_STYLE, 0);

		status = kernelMemoryGetIo(sizeof(pcNetInitBlock16), 0, &init);
		if (status < 0)
		{
			kernelMemoryReleaseIo(&transRing);
			kernelMemoryReleaseIo(&recvRing);
			kernelMemoryReleaseIo(&recvBuff);
			continue;
		}

		initBlock = init.virtual;

		// Mode zero is 'normal' mode
		initBlock->mode = 0; //PCNET_CSR_MODE_PROM;

		for (count = 0; count < 6; count ++)
			initBlock->physAddr[count] =
				adapter->device.hardwareAddress.bytes[count];

		// Accept all multicast packets for now.
		for (count = 0; count < 4; count ++)
			initBlock->addressFilter[count] = 0xFFFF;

		initBlock->recvDescLow = ((unsigned) recvRing.physical & 0xFFFF);
		initBlock->recvDescHigh = ((unsigned) recvRing.physical >> 16);
		initBlock->recvRingLen = (PCNET_NUM_RINGBUFFERS_CODE << 5);
		initBlock->transDescLow = ((unsigned) transRing.physical & 0xFFFF);
		initBlock->transDescHigh = ((unsigned) transRing.physical >> 16);
		initBlock->transRingLen = (PCNET_NUM_RINGBUFFERS_CODE << 5);

		// Interrupt mask and deferral control: enable everything except
		// initialization interrupt
		writeCSR(pcNet, PCNET_CSR_IMASK, PCNET_CSR_IMASK_IDONM);

		// Test and features control register.  Turn on 'DMA plus', auto
		// transmit padding, and auto receive stripping
		modifyCSR(pcNet, PCNET_CSR_FEAT, (PCNET_CSR_FEAT_DMAPLUS |
			PCNET_CSR_FEAT_APADXMT | PCNET_CSR_FEAT_ASTRPRCV), op_or);

		// Turn on burst-mode reading and writing
		writeBCR(pcNet, PCNET_BCR_BURST,
			(readBCR(pcNet, PCNET_BCR_BURST) | 0x0260));

		// Load init block address registers
		writeCSR(pcNet, PCNET_CSR_IADR0, ((unsigned) init.physical & 0xFFFF));
		writeCSR(pcNet, PCNET_CSR_IADR1, ((unsigned) init.physical >> 16));

		// Start the init
		writeCSR(pcNet, PCNET_CSR_STATUS, PCNET_CSR_STATUS_INIT);

		// Wait until done
		while (!(readCSR(pcNet, PCNET_CSR_STATUS) & PCNET_CSR_STATUS_IDON));

		kernelMemoryReleaseIo(&init);

		// Start it and enable device interrupts
		writeCSR(pcNet, PCNET_CSR_STATUS,
			(PCNET_CSR_STATUS_STRT | PCNET_CSR_STATUS_IENA));

		// Set link status
		if (readBCR(pcNet, PCNET_BCR_LINK) & PCNET_BCR_LINK_LEDOUT)
			adapter->device.flags |= NETWORK_ADAPTERFLAG_LINK;

		dev->device.class = kernelDeviceGetClass(DEVICECLASS_NETWORK);
		dev->device.subClass =
			kernelDeviceGetClass(DEVICESUBCLASS_NETWORK_ETHERNET);
		dev->driver = driver;
		dev->data = (void *) adapter;

		// Claim the controller device in the list of PCI targets.
		kernelBusDeviceClaim(&busTargets[deviceCount], driver);

		// Register the network adapter device
		status = kernelNetworkDeviceRegister(dev);
		if (status < 0)
		{
			kernelFree(busTargets);
			kernelFree(dev);
			return (status);
		}

		// Add the kernel device
		status = kernelDeviceAdd(busTargets[deviceCount].bus->dev, dev);
		if (status < 0)
		{
			kernelFree(busTargets);
			kernelFree(dev);
			return (status);
		}
	}

	kernelFree(busTargets);
	return (status = 0);
}


static kernelNetworkDeviceOps networkOps = {
	driverInterruptHandler,
	driverSetFlags,
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

void kernelPcNetDriverRegister(kernelDriver *driver)
{
	 // Device driver registration.

	driver->driverDetect = driverDetect;
	driver->ops = &networkOps;

	return;
}

