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
//  kernelSystemDriver.c
//

// This is a place for putting basic, generic driver initializations, including
// the one for the 'system device' itself, and any other abstract things that
// have no real hardware driver, per se.

#include "kernelSystemDriver.h"
#include "kernelBus.h"
#include "kernelDebug.h"
#include "kernelDevice.h"
#include "kernelDriver.h"
#include "kernelLog.h"
#include "kernelMain.h"
#include "kernelMalloc.h"
#include "kernelPage.h"
#include "kernelParameters.h"
#include "kernelVariableList.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/multiproc.h>


static kernelDevice *regDevice(void *parent, void *driver,
	 kernelDeviceClass *class, kernelDeviceClass *subClass)
{
	// Just collects some of the common things from the other detect routines

	int status = 0;
	kernelDevice *dev = NULL;

	// Allocate memory for the device
	dev = kernelMalloc(sizeof(kernelDevice));
	if (!dev)
		return (dev);

	dev->device.class = class;
	dev->device.subClass = subClass;
	dev->driver = driver;

	// Add the kernel device
	status = kernelDeviceAdd(parent, dev);
	if (status < 0)
	{
		kernelFree(dev);
		return (dev = NULL);
	}

	return (dev);
}


static int driverDetectMemory(void *parent, kernelDriver *driver)
{
	int status = 0;
	kernelDevice *dev = NULL;
	char value[80];

	// Register the device
	dev = regDevice(parent, driver, kernelDeviceGetClass(DEVICECLASS_MEMORY),
		NULL);
	if (!dev)
		return (status = ERR_NOCREATE);

	// Initialize the variable list for attributes of the memory
	status = kernelVariableListCreate(&dev->device.attrs);
	if (status < 0)
		return (status);

	sprintf(value, "%u Kb", (1024 + kernelOsLoaderInfo->extendedMemory));
	kernelVariableListSet(&dev->device.attrs, "memory.size", value);

	return (status = 0);
}


static int driverDetectBios32(void *parent, kernelDriver *driver)
{
	// Detect a 32-bit BIOS interface

	int status = 0;
	void *rom = NULL;
	char *ptr = NULL;
	kernelBios32Header *dataStruct = NULL;
	char checkSum = 0;
	kernelDevice *dev = NULL;
	int count;

	// Map the designated area for the BIOS into memory so we can scan it.
	status = kernelPageMapToFree(KERNELPROCID, BIOSROM_START, &rom,
		BIOSROM_SIZE);
	if (status < 0)
		goto out;

	// Search for our signature
	for (ptr = rom; ptr <= (char *)(rom + (BIOSROM_SIZE -
		sizeof(kernelBios32Header))); ptr += sizeof(kernelBios32Header))
	{
		if (!strncmp(ptr, BIOSROM_SIG_32, strlen(BIOSROM_SIG_32)))
		{
			dataStruct = (kernelBios32Header *) ptr;
			break;
		}
	}

	if (!dataStruct)
		// Not found
		goto out;

	// Check the checksum (signed chars, should sum to zero)
	for (count = 0; count < (int) sizeof(kernelBios32Header); count ++)
		checkSum += ptr[count];
	if (checkSum)
	{
		kernelDebugError("32-bit BIOS checksum failed (%d)", checkSum);
		status = ERR_BADDATA;
		goto out;
	}

	kernelLog("32-bit BIOS found at %p, entry point %p",
		(void *)(BIOSROM_START + ((void *) dataStruct - rom)),
		dataStruct->entryPoint);

	// Register the device
	dev = regDevice(parent, driver,
		kernelDeviceGetClass(DEVICESUBCLASS_SYSTEM_BIOS32), NULL);
	if (!dev)
	{
		status = ERR_NOCREATE;
		goto out;
	}

	// Allocate memory for driver data
	dev->data = kernelMalloc(sizeof(kernelBios32Header));
	if (!dev->data)
	{
		status = ERR_MEMORY;
		goto out;
	}

	// Copy the data we found into the driver's data structure
	memcpy(dev->data, dataStruct, sizeof(kernelBios32Header));

	status = 0;

out:
	if (status < 0)
	{
		if (dev && dev->data)
			kernelFree(dev->data);
	}

	if (rom)
		kernelPageUnmap(KERNELPROCID, rom, BIOSROM_SIZE);

	return (status);
}


static int driverDetectBiosPnP(void *parent, kernelDriver *driver)
{
	// Detect a Plug and Play BIOS

	int status = 0;
	void *rom = NULL;
	char *ptr = NULL;
	kernelBiosPnpHeader *dataStruct = NULL;
	char checkSum = 0;
	kernelDevice *dev = NULL;
	char value[80];
	int count;

	// Map the designated area for the BIOS into memory so we can scan it.
	status = kernelPageMapToFree(KERNELPROCID, BIOSROM_START, &rom,
		BIOSROM_SIZE);
	if (status < 0)
		goto out;

	// Search for our signature
	for (ptr = rom; ptr <= (char *)(rom + (BIOSROM_SIZE -
		sizeof(kernelBiosPnpHeader))); ptr += 16)
	{
		if (!strncmp(ptr, BIOSROM_SIG_PNP, strlen(BIOSROM_SIG_PNP)))
		{
			dataStruct = (kernelBiosPnpHeader *) ptr;
			break;
		}
	}

	if (!dataStruct)
		// Not found
		goto out;

	// Check the checksum (signed chars, should sum to zero)
	for (count = 0; count < (int) sizeof(kernelBiosPnpHeader); count ++)
		checkSum += ptr[count];
	if (checkSum)
	{
		kernelDebugError("Plug and Play BIOS checksum failed (%d)", checkSum);
		status = ERR_BADDATA;
		goto out;
	}

	kernelLog("Plug and Play BIOS found at %p",
		(void *)(BIOSROM_START + ((void *) dataStruct - rom)));

	// Register the device
	dev = regDevice(parent, driver,
		kernelDeviceGetClass(DEVICESUBCLASS_SYSTEM_BIOSPNP), NULL);
	if (!dev)
	{
		status = ERR_NOCREATE;
		goto out;
	}

	// Initialize the variable list for attributes of the Plug and Play BIOS
	status = kernelVariableListCreate(&dev->device.attrs);
	if (status < 0)
		goto out;

	sprintf(value, "%d.%d", ((dataStruct->version & 0xF0) >> 4),
		(dataStruct->version & 0x0F));
	kernelVariableListSet(&dev->device.attrs, "pnp.version", value);

	// Allocate memory for driver data
	dev->data = kernelMalloc(sizeof(kernelBiosPnpHeader));
	if (!dev->data)
	{
		status = ERR_MEMORY;
		goto out;
	}

	// Copy the data we found into the driver's data structure
	memcpy(dev->data, dataStruct, sizeof(kernelBiosPnpHeader));

	status = 0;

out:
	if (status < 0)
	{
		if (dev)
		{
			if (dev->data)
				kernelFree(dev->data);

			kernelVariableListDestroy(&dev->device.attrs);
		}
	}

	if (rom)
		kernelPageUnmap(KERNELPROCID, rom, BIOSROM_SIZE);

	return (status);
}


static void *driverMultiProcGetEntry(kernelDevice *dev, unsigned char type,
	int index)
{
	multiProcConfigHeader *config = NULL;
	unsigned char *ptr = NULL;
	int found = 0;
	int count;

	// Check Params
	if (!dev)
		return (NULL);

	config = dev->data;

	// Loop through the entries
	ptr = config->entries;

	for (count = 0; count < config->numEntries; count ++)
	{
		if (ptr[0] == type)
		{
			if (index == found)
				return (ptr);

			found += 1;
		}

		switch (ptr[0])
		{
			case MULTIPROC_ENTRY_CPU:
				ptr += sizeof(multiProcCpuEntry);
				break;

			case MULTIPROC_ENTRY_BUS:
				ptr += sizeof(multiProcBusEntry);
				break;

			case MULTIPROC_ENTRY_IOAPIC:
				ptr += sizeof(multiProcIoApicEntry);
				break;

			case MULTIPROC_ENTRY_IOINTASSMT:
				ptr += sizeof(multiProcIoIntAssEntry);
				break;

			case MULTIPROC_ENTRY_LOCINTASSMT:
				ptr += sizeof(multiProcLocalIntAssEntry);
				break;

			default:
				kernelDebugError("Multiproc config table unknown entry type "
					"(%d)", ptr[0]);
					return (NULL);
		}
	}

	// Not found
	return (NULL);
}


static int driverDetectMultiProc(void *parent, kernelDriver *driver)
{
	// Detect a multiprocessor-spec compliant system

	int status = 0;
	void *bda = NULL;
	unsigned ebdaPhysical = 0;
	void *ebda = NULL;
	char *ptr = NULL;
	void *rom = NULL;
	multiProcFloatingPointer *floater = NULL;
	char checkSum = 0;
	kernelDevice *dev = NULL;
	multiProcConfigHeader *config = NULL;
	int processors = 0, buses = 0, ioapics = 0, ioints = 0, locints = 0;
	char value[80];
	int count;

	kernelDebug(debug_device, "Multiproc searching for floating pointer "
		"structure");

	// Map the first 4K, containing the BIOS data area (BDA) into our memory
	// so we can get the address of the extended BIOS data area (EBDA)
	status = kernelPageMapToFree(KERNELPROCID, 0, &bda, 0x1000);
	if (status < 0)
		goto out;

	ebdaPhysical = ((unsigned) *((unsigned short *)(bda + 0x40E)) << 4);

	kernelDebug(debug_device, "Multiproc EBDA at %08x", ebdaPhysical);

	if (ebdaPhysical)
	{
		// Map the first 4K of the EBDA into our memory
		status = kernelPageMapToFree(KERNELPROCID, ebdaPhysical, &ebda,
			0x1000);
		if (status < 0)
			goto out;

		kernelDebug(debug_device, "Multiproc searching %08x-%08x",
			ebdaPhysical, (ebdaPhysical + (0x400 - 1)));

		// Search the first kilobyte of the EBDA for the signature of the
		// floating pointer structure
		for (ptr = ebda; ptr <= (char *)(ebda + (0x400 -
			sizeof(multiProcFloatingPointer))); ptr += 16)
		{
			if (!strncmp(ptr, MULTIPROC_SIG_FLOAT,
				strlen(MULTIPROC_SIG_FLOAT)))
			{
				floater = (multiProcFloatingPointer *) ptr;
				break;
			}
		}
	}

	if (!floater)
	{
		// Try searching the BIOS ROM area
		kernelDebug(debug_device, "Multiproc searching %08x-%08x",
			BIOSROM_START, BIOSROM_END);

		// Map the designated area for the BIOS ROM into memory so we can
		// scan it.
		status = kernelPageMapToFree(KERNELPROCID, BIOSROM_START, &rom,
			BIOSROM_SIZE);
		if (status < 0)
			goto out;

		// Search for the signature of the floating pointer structure
		for (ptr = rom; ptr <= (char *)(rom + (BIOSROM_SIZE -
			sizeof(multiProcFloatingPointer))); ptr += 16)
		{
			if (!strncmp(ptr, MULTIPROC_SIG_FLOAT,
				strlen(MULTIPROC_SIG_FLOAT)))
			{
				floater = (multiProcFloatingPointer *) ptr;
				break;
			}
		}
	}

	kernelDebug(debug_device, "Multiproc floating pointer %sfound",
		(floater? "" : "not "));

	if (!floater)
		goto out;

	// Check the checksum (signed chars, should sum to zero)
	for (count = 0; count < (int) sizeof(multiProcFloatingPointer);	count ++)
		checkSum += ptr[count];

	if (checkSum)
	{
		kernelDebugError("Multiproc floating pointer checksum failed (%d)",
			checkSum);
		status = ERR_BADDATA;
		goto out;
	}

	kernelDebug(debug_device, "Multiproc IMCR is %spresent",
		((floater->features[1] & 0x80)? "" : "not "));

	// Allocate memory for the device
	dev = regDevice(parent, driver,
		kernelDeviceGetClass(DEVICESUBCLASS_SYSTEM_MULTIPROC), NULL);
	if (!dev)
	{
		status = ERR_NOCREATE;
		goto out;
	}

	if (floater->tablePhysical)
	{
		kernelDebug(debug_device, "Multiproc config table is at %08x",
			floater->tablePhysical);

		// Map the first page of the configuration table.
		status = kernelPageMapToFree(KERNELPROCID, floater->tablePhysical,
			(void **) &config, MEMORY_PAGE_SIZE);
		if (status < 0)
			goto out;

		// Check the signature
		if (strncmp(config->signature, MULTIPROC_SIG_CONFIG,
			strlen(MULTIPROC_SIG_CONFIG)))
		{
			kernelDebugError("Multiproc config table signature invalid");
			status = ERR_BADDATA;
			goto out;
		}

		// Check the checksum (signed chars, should sum to zero)
		for (count = 0, ptr = (char *) config, checkSum = 0;
			count < config->length; count ++)
		{
			checkSum += ptr[count];
		}

		if (checkSum)
		{
			kernelDebugError("Multiproc config table checksum failed (%d)",
				checkSum);
			status = ERR_BADDATA;
			goto out;
		}

		// Allocate memory for driver data
		dev->data = kernelMalloc(min(config->length, MEMORY_PAGE_SIZE));
		if (!dev->data)
		{
			status = ERR_MEMORY;
			goto out;
		}

		// Copy the data we found into the driver's data structure
		memcpy(dev->data, config, min(config->length, MEMORY_PAGE_SIZE));

		// Find processors
		for (count = 0; ; count ++)
		{
			ptr = driverMultiProcGetEntry(dev, MULTIPROC_ENTRY_CPU, count);

			if (!ptr)
				break;

			kernelDebug(debug_device, "Multiproc CPU: apicId=%d version=%02x "
				"flags=%02x sig=%08x feat=%08x",
				((multiProcCpuEntry *) ptr)->localApicId,
				((multiProcCpuEntry *) ptr)->localApicVersion,
				((multiProcCpuEntry *) ptr)->cpuFlags,
				((multiProcCpuEntry *) ptr)->cpuSignature,
				((multiProcCpuEntry *) ptr)->featureFlags);
			processors += 1;
		}

		// Find buses
		for (count = 0; ; count ++)
		{
			ptr = driverMultiProcGetEntry(dev, MULTIPROC_ENTRY_BUS, count);

			if (!ptr)
				break;

			strncpy(value, ((multiProcBusEntry *) ptr)->type, 6);
			kernelDebug(debug_device, "Multiproc bus: %d=%s",
				((multiProcBusEntry *) ptr)->busId, value);
			buses += 1;
		}

		// Find I/O APICs
		for (count = 0; ; count ++)
		{
			ptr = driverMultiProcGetEntry(dev, MULTIPROC_ENTRY_IOAPIC, count);

			if (!ptr)
				break;

			kernelDebug(debug_device, "Multiproc I/O APIC: apicId=%d "
				"version=%02x flags=%02x addr=%08x",
				((multiProcIoApicEntry *) ptr)->apicId,
				((multiProcIoApicEntry *) ptr)->apicVersion,
				((multiProcIoApicEntry *) ptr)->apicFlags,
				((multiProcIoApicEntry *) ptr)->apicPhysical);
			ioapics += 1;
		}

		// Find I/O interrupt assignments
		for (count = 0; ; count ++)
		{
			ptr = driverMultiProcGetEntry(dev, MULTIPROC_ENTRY_IOINTASSMT,
				count);

			if (!ptr)
				break;

			kernelDebug(debug_device, "Multiproc I/O int: type=%d flags=%04x "
				"bus=%d irq=%d ioApic=%d pin=%d",
				((multiProcIoIntAssEntry *) ptr)->intType,
				((multiProcIoIntAssEntry *) ptr)->intFlags,
				((multiProcIoIntAssEntry *) ptr)->busId,
				((multiProcIoIntAssEntry *) ptr)->busIrq,
				((multiProcIoIntAssEntry *) ptr)->ioApicId,
				((multiProcIoIntAssEntry *) ptr)->ioApicIntPin);
			ioints += 1;
		}

		// Find local interrupt assignments
		for (count = 0; ; count ++)
		{
			ptr = driverMultiProcGetEntry(dev, MULTIPROC_ENTRY_LOCINTASSMT,
				count);

			if (!ptr)
				break;

			kernelDebug(debug_device, "Multiproc local int: type=%d "
				"flags=%04x bus=%d irq=%d localApic=%d lint=%d",
				((multiProcLocalIntAssEntry *) ptr)->intType,
				((multiProcLocalIntAssEntry *) ptr)->intFlags,
				((multiProcLocalIntAssEntry *) ptr)->busId,
				((multiProcLocalIntAssEntry *) ptr)->busIrq,
				((multiProcLocalIntAssEntry *) ptr)->localApicId,
				((multiProcLocalIntAssEntry *) ptr)->localApicLint);
			locints += 1;
		}

		// Initialize the variable list for attributes
		status = kernelVariableListCreate(&dev->device.attrs);
		if (status < 0)
			goto out;

		// Set the variable list attributes

		// Vendor string
		strncpy(value, config->oemId, 8);
		for (count = 7; count; count --)
			if (value[count] == ' ')
				value[count] = '\0';
		kernelVariableListSet((variableList *) &dev->device.attrs,
			DEVICEATTRNAME_VENDOR, value);

		// Product name
		strncpy(value, config->productId, 12);
		for (count = 11; count; count --)
			if (value[count] == ' ')
				value[count] = '\0';
		kernelVariableListSet((variableList *) &dev->device.attrs,
			DEVICEATTRNAME_MODEL, value);

		// Numbers of entries we found
		sprintf(value, "%d", processors);
		kernelVariableListSet((variableList *) &dev->device.attrs,
			"processors", value);
		sprintf(value, "%d", buses);
		kernelVariableListSet((variableList *) &dev->device.attrs,
			"buses", value);
		sprintf(value, "%d", ioapics);
		kernelVariableListSet((variableList *) &dev->device.attrs,
			"io.apics", value);
		sprintf(value, "%d", ioints);
		kernelVariableListSet((variableList *) &dev->device.attrs,
			"io.intAssignments", value);
		sprintf(value, "%d", locints);
		kernelVariableListSet((variableList *) &dev->device.attrs,
			"local.intAssignments", value);
	}

	status = 0;

out:
	if (status < 0)
	{
		if (dev)
		{
			if (dev->data)
				kernelFree(dev->data);

			kernelVariableListDestroy(&dev->device.attrs);
		}
	}

	if (config)
		kernelPageUnmap(KERNELPROCID, config, MEMORY_PAGE_SIZE);

	if (rom)
		kernelPageUnmap(KERNELPROCID, rom, BIOSROM_SIZE);

	if (ebda)
		kernelPageUnmap(KERNELPROCID, ebda, 0x1000);

	if (bda)
		kernelPageUnmap(KERNELPROCID, bda, 0x1000);

	return (status);
}


static int driverDetectIsaBridge(void *parent __attribute__((unused)),
	kernelDriver *driver)
{
	// Detect an ISA bridge

	int status = 0;
	kernelBusTarget *busTargets = NULL;
	int numBusTargets = 0;
	kernelDevice *dev = NULL;
	int deviceCount;

	// Search the PCI bus(es) for devices
	numBusTargets = kernelBusGetTargets(bus_pci, &busTargets);
	if (numBusTargets <= 0)
		return (status = ERR_NODATA);

	// Search the bus targets for an PCI-to-ISA bridge device.
	for (deviceCount = 0; deviceCount < numBusTargets; deviceCount ++)
	{
		// If it's not a PCI-to-ISA bridge device, skip it
		if (!busTargets[deviceCount].class ||
			(busTargets[deviceCount].class->class != DEVICECLASS_BRIDGE) ||
			!busTargets[deviceCount].subClass ||
			(busTargets[deviceCount].subClass->class !=
				DEVICESUBCLASS_BRIDGE_ISA))
		{
			continue;
		}

		kernelLog("Found PCI/ISA bridge");

		// After this point, we know we have a supported device.

		dev = regDevice(busTargets[deviceCount].bus->dev, driver,
			kernelDeviceGetClass(DEVICECLASS_BRIDGE),
			kernelDeviceGetClass(DEVICESUBCLASS_BRIDGE_ISA));
		if (!dev)
			return (status = ERR_NOCREATE);
	}

	return (status = 0);
}


static kernelMultiProcOps multiProcOps = {
	driverMultiProcGetEntry
};


/////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////
//
// Below here, the functions are exported for external use
//
/////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////

void kernelMemoryDriverRegister(kernelDriver *driver)
{
	// Device driver registration.
	driver->driverDetect = driverDetectMemory;
	return;
}


void kernelBios32DriverRegister(kernelDriver *driver)
{
	// Device driver registration.
	driver->driverDetect = driverDetectBios32;
	return;
}


void kernelBiosPnpDriverRegister(kernelDriver *driver)
{
	// Device driver registration.
	driver->driverDetect = driverDetectBiosPnP;
	return;
}


void kernelMultiProcDriverRegister(kernelDriver *driver)
{
	// Device driver registration.
	driver->driverDetect = driverDetectMultiProc;
	driver->ops = &multiProcOps;
	return;
}


void kernelIsaBridgeDriverRegister(kernelDriver *driver)
{
	// Device driver registration.
	driver->driverDetect = driverDetectIsaBridge;
	return;
}

