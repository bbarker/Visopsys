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
//  kernelPciDriver.c
//

// These routines allow access to PCI configuration space.  Based on an
// original version contributed by Jonas Zaddach: See the file
// contrib/jonas-pci/src/kernel/kernelBusPCI.c

#include "kernelPciDriver.h"
#include "kernelDebug.h"
#include "kernelDevice.h"
#include "kernelError.h"
#include "kernelInterrupt.h"
#include "kernelLog.h"
#include "kernelMalloc.h"
#include "kernelParameters.h"
#include "kernelPic.h"
#include "kernelSystemDriver.h"
#include <string.h>
#include <values.h>
#include <sys/processor.h>


static pciSubClass subclass_old[] = {
	{ 0x00, "other", DEVICECLASS_NONE, DEVICESUBCLASS_NONE },
	{ 0x01, "VGA", DEVICECLASS_GRAPHIC, DEVICESUBCLASS_NONE },
	{ PCI_INVALID_SUBCLASSCODE, "", DEVICECLASS_NONE, DEVICESUBCLASS_NONE }
};

static pciSubClass subclass_diskctrl[] = {
	{ 0x00, "SCSI", DEVICECLASS_DISKCTRL, DEVICESUBCLASS_DISKCTRL_SCSI },
	{ 0x01, "IDE", DEVICECLASS_DISKCTRL, DEVICESUBCLASS_DISKCTRL_IDE },
	{ 0x02, "floppy", DEVICECLASS_DISKCTRL, DEVICESUBCLASS_NONE },
	{ 0x03, "IPI", DEVICECLASS_DISKCTRL, DEVICESUBCLASS_NONE },
	{ 0x04, "RAID", DEVICECLASS_DISKCTRL, DEVICESUBCLASS_NONE },
	{ 0x05, "ATA", DEVICECLASS_DISKCTRL, DEVICESUBCLASS_NONE },
	{ 0x06, "SATA", DEVICECLASS_DISKCTRL, DEVICESUBCLASS_DISKCTRL_SATA },
	{ 0x07, "SAS", DEVICECLASS_DISKCTRL, DEVICESUBCLASS_NONE },
	{ 0x80, "other", DEVICECLASS_DISKCTRL, DEVICESUBCLASS_NONE },
	{ PCI_INVALID_SUBCLASSCODE, "", DEVICECLASS_NONE, DEVICESUBCLASS_NONE }
};

static pciSubClass subclass_net[] = {
	{ 0x00, "ethernet", DEVICECLASS_NETWORK, DEVICESUBCLASS_NETWORK_ETHERNET },
	{ 0x01, "token ring", DEVICECLASS_NETWORK, DEVICESUBCLASS_NONE },
	{ 0x02, "FDDI", DEVICECLASS_NETWORK, DEVICESUBCLASS_NONE },
	{ 0x03, "ATM", DEVICECLASS_NETWORK, DEVICESUBCLASS_NONE },
	{ 0x04, "ISDN", DEVICECLASS_NETWORK, DEVICESUBCLASS_NONE },
	{ 0x05, "WorldFip", DEVICECLASS_NETWORK, DEVICESUBCLASS_NONE },
	{ 0x06, "PICMG", DEVICECLASS_NETWORK, DEVICESUBCLASS_NONE },
	{ 0x80, "other", DEVICECLASS_NETWORK, DEVICESUBCLASS_NONE },
	{ PCI_INVALID_SUBCLASSCODE, "", DEVICECLASS_NONE, DEVICESUBCLASS_NONE }
};

static pciSubClass subclass_graphics[] = {
	{ 0x00, "VGA", DEVICECLASS_GRAPHIC, DEVICESUBCLASS_NONE },
	{ 0x01, "XGA", DEVICECLASS_GRAPHIC, DEVICESUBCLASS_NONE },
	{ 0x02, "3D", DEVICECLASS_GRAPHIC, DEVICESUBCLASS_NONE },
	{ 0x80, "other", DEVICECLASS_GRAPHIC, DEVICESUBCLASS_NONE },
	{ PCI_INVALID_SUBCLASSCODE, "", DEVICECLASS_NONE, DEVICESUBCLASS_NONE }
};

static pciSubClass subclass_multimed[] = {
	{ 0x00, "video", DEVICECLASS_NONE, DEVICESUBCLASS_NONE },
	{ 0x01, "audio", DEVICECLASS_NONE, DEVICESUBCLASS_NONE },
	{ 0x02, "telephony", DEVICECLASS_NONE, DEVICESUBCLASS_NONE },
	{ 0x03, "high-def audio", DEVICECLASS_NONE, DEVICESUBCLASS_NONE },
	{ 0x80, "other", DEVICECLASS_NONE, DEVICESUBCLASS_NONE },
	{ PCI_INVALID_SUBCLASSCODE, "", DEVICECLASS_NONE, DEVICESUBCLASS_NONE }
};

static pciSubClass subclass_mem[] = {
	{ 0x00, "RAM", DEVICECLASS_MEMORY, DEVICESUBCLASS_NONE },
	{ 0x01, "flash", DEVICECLASS_MEMORY, DEVICESUBCLASS_NONE },
	{ 0x80, "other", DEVICECLASS_MEMORY, DEVICESUBCLASS_NONE },
	{ PCI_INVALID_SUBCLASSCODE, "", DEVICECLASS_NONE, DEVICESUBCLASS_NONE }
};

static pciSubClass subclass_bridge[] = {
	{ 0x00, "host", DEVICECLASS_BRIDGE, DEVICESUBCLASS_NONE },
	{ 0x01, "ISA", DEVICECLASS_BRIDGE, DEVICESUBCLASS_BRIDGE_ISA },
	{ 0x02, "EISA", DEVICECLASS_BRIDGE, DEVICESUBCLASS_NONE },
	{ 0x03, "MCA", DEVICECLASS_BRIDGE, DEVICESUBCLASS_NONE },
	{ 0x04, "PCI/PCI", DEVICECLASS_BRIDGE, DEVICESUBCLASS_BRIDGE_PCI },
	{ 0x05, "PCMCIA", DEVICECLASS_BRIDGE, DEVICESUBCLASS_NONE },
	{ 0x06, "NuBus", DEVICECLASS_BRIDGE, DEVICESUBCLASS_NONE },
	{ 0x07, "CardBus", DEVICECLASS_BRIDGE, DEVICESUBCLASS_NONE },
	{ 0x08, "RACEway", DEVICECLASS_BRIDGE, DEVICESUBCLASS_NONE },
	{ 0x09, "PCI/PCI", DEVICECLASS_BRIDGE, DEVICESUBCLASS_NONE },
	{ 0x0A, "InfiniBand", DEVICECLASS_BRIDGE, DEVICESUBCLASS_NONE },
	{ 0x80, "other", DEVICECLASS_BRIDGE, DEVICESUBCLASS_NONE },
	{ PCI_INVALID_SUBCLASSCODE, "", DEVICECLASS_NONE, DEVICESUBCLASS_NONE }
};

static pciSubClass subclass_comm[] = {
	{ 0x00, "serial", DEVICECLASS_NONE, DEVICESUBCLASS_NONE },
	{ 0x01, "parallel", DEVICECLASS_NONE, DEVICESUBCLASS_NONE },
	{ 0x02, "multiport serial", DEVICECLASS_NONE, DEVICESUBCLASS_NONE },
	{ 0x03, "modem", DEVICECLASS_NONE, DEVICESUBCLASS_NONE },
	{ 0x04, "GPIB IEEE-488", DEVICECLASS_NONE, DEVICESUBCLASS_NONE },
	{ 0x05, "smart card", DEVICECLASS_NONE, DEVICESUBCLASS_NONE },
	{ 0x80, "other", DEVICECLASS_NONE, DEVICESUBCLASS_NONE },
	{ PCI_INVALID_SUBCLASSCODE, "", DEVICECLASS_NONE, DEVICESUBCLASS_NONE }
};

static pciSubClass subclass_sys[] = {
	{ 0x00, "(A)PIC", DEVICECLASS_INTCTRL, DEVICESUBCLASS_NONE },
	{ 0x01, "DMA", DEVICECLASS_DMA, DEVICESUBCLASS_NONE },
	{ 0x02, "timer", DEVICECLASS_SYSTIMER, DEVICESUBCLASS_NONE },
	{ 0x03, "RTC", DEVICECLASS_RTC, DEVICESUBCLASS_NONE },
	{ 0x04, "PCI hotplug", DEVICECLASS_NONE, DEVICESUBCLASS_NONE },
	{ 0x05, "SD controller", DEVICECLASS_NONE, DEVICESUBCLASS_NONE },
	{ 0x80, "other", DEVICECLASS_NONE, DEVICESUBCLASS_NONE },
	{ PCI_INVALID_SUBCLASSCODE, "", DEVICECLASS_NONE, DEVICESUBCLASS_NONE }
};

static pciSubClass subclass_input[] = {
	{ 0x00, "keyboard", DEVICECLASS_KEYBOARD, DEVICESUBCLASS_NONE },
	{ 0x01, "digitizer", DEVICECLASS_NONE, DEVICESUBCLASS_NONE },
	{ 0x02, "mouse", DEVICECLASS_MOUSE, DEVICESUBCLASS_NONE },
	{ 0x03, "scanner", DEVICECLASS_NONE, DEVICESUBCLASS_NONE },
	{ 0x04, "gameport", DEVICECLASS_NONE, DEVICESUBCLASS_NONE },
	{ 0x80, "other", DEVICECLASS_NONE, DEVICESUBCLASS_NONE },
	{ PCI_INVALID_SUBCLASSCODE, "", DEVICECLASS_NONE, DEVICESUBCLASS_NONE }
};

static pciSubClass subclass_dock[] = {
	{ 0x00, "generic", DEVICECLASS_NONE, DEVICESUBCLASS_NONE },
	{ 0x80, "other", DEVICECLASS_NONE, DEVICESUBCLASS_NONE },
	{ PCI_INVALID_SUBCLASSCODE, "", DEVICECLASS_NONE, DEVICESUBCLASS_NONE }
};

static pciSubClass subclass_cpu[] = {
	{ 0x00, "386", DEVICECLASS_CPU, DEVICESUBCLASS_CPU_X86 },
	{ 0x01, "486", DEVICECLASS_CPU, DEVICESUBCLASS_CPU_X86 },
	{ 0x02, "Pentium", DEVICECLASS_CPU, DEVICESUBCLASS_CPU_X86 },
	{ 0x03, "P6", DEVICECLASS_CPU, DEVICESUBCLASS_CPU_X86 },
	{ 0x10, "Alpha", DEVICECLASS_CPU, DEVICESUBCLASS_NONE },
	{ 0x20, "PowerPC", DEVICECLASS_CPU, DEVICESUBCLASS_NONE },
	{ 0x30, "MIPS", DEVICECLASS_CPU, DEVICESUBCLASS_NONE },
	{ 0x40, "co-processor", DEVICECLASS_CPU, DEVICESUBCLASS_NONE },
	{ PCI_INVALID_SUBCLASSCODE, "", DEVICECLASS_NONE, DEVICESUBCLASS_NONE }
};

static pciSubClass subclass_serial[] = {
	{ 0x00, "FireWire", DEVICECLASS_BUS, DEVICESUBCLASS_NONE },
	{ 0x01, "ACCESS.bus", DEVICECLASS_BUS, DEVICESUBCLASS_NONE },
	{ 0x02, "SSA", DEVICECLASS_BUS, DEVICESUBCLASS_NONE },
	{ 0x03, "USB", DEVICECLASS_BUS, DEVICESUBCLASS_BUS_USB },
	{ 0x04, "fibre channel", DEVICECLASS_BUS, DEVICESUBCLASS_NONE },
	{ 0x05, "SMBus", DEVICECLASS_BUS, DEVICESUBCLASS_NONE },
	{ 0x06, "InfiniBand", DEVICECLASS_BUS, DEVICESUBCLASS_NONE },
	{ 0x07, "IPMI", DEVICECLASS_BUS, DEVICESUBCLASS_NONE },
	{ 0x08, "SERCOS", DEVICECLASS_BUS, DEVICESUBCLASS_NONE },
	{ 0x09, "CANbus", DEVICECLASS_BUS, DEVICESUBCLASS_NONE },
	{ PCI_INVALID_SUBCLASSCODE, "", DEVICECLASS_NONE, DEVICESUBCLASS_NONE }
};

static pciSubClass subclass_wireless[] = {
	{ 0x00, "iRDA", DEVICECLASS_NETWORK, DEVICESUBCLASS_NETWORK_WIRELESS },
	{ 0x01, "infrared", DEVICECLASS_NETWORK, DEVICESUBCLASS_NETWORK_WIRELESS },
	{ 0x10, "radio", DEVICECLASS_NETWORK, DEVICESUBCLASS_NETWORK_WIRELESS },
	{ 0x11, "Bluetooth", DEVICECLASS_NETWORK, DEVICESUBCLASS_NETWORK_WIRELESS },
	{ 0x12, "broadband", DEVICECLASS_NETWORK, DEVICESUBCLASS_NETWORK_WIRELESS },
	{ 0x20, "802.11a ethernet", DEVICECLASS_NETWORK,
		DEVICESUBCLASS_NETWORK_WIRELESS },
	{ 0x21, "802.11b ethernet", DEVICECLASS_NETWORK,
		DEVICESUBCLASS_NETWORK_WIRELESS },
	{ 0x80, "other", DEVICECLASS_NETWORK, DEVICESUBCLASS_NETWORK_WIRELESS },
	{ PCI_INVALID_SUBCLASSCODE, "", DEVICECLASS_NONE, DEVICESUBCLASS_NONE }
};

static pciSubClass subclass_intelio[] = {
	{ 0x00, "I20/message", DEVICECLASS_NONE, DEVICESUBCLASS_NONE },
	{ PCI_INVALID_SUBCLASSCODE, "", DEVICECLASS_NONE, DEVICESUBCLASS_NONE }
};

static pciSubClass subclass_sat[] = {
	{ 0x01, "television", DEVICECLASS_NONE, DEVICESUBCLASS_NONE },
	{ 0x02, "audio", DEVICECLASS_NONE, DEVICESUBCLASS_NONE },
	{ 0x03, "voice", DEVICECLASS_NONE, DEVICESUBCLASS_NONE },
	{ 0x04, "data", DEVICECLASS_NONE, DEVICESUBCLASS_NONE },
	{ PCI_INVALID_SUBCLASSCODE, "", DEVICECLASS_NONE, DEVICESUBCLASS_NONE }
};

static pciSubClass subclass_encrypt[] = {
	{ 0x00, "network", DEVICECLASS_NONE, DEVICESUBCLASS_NONE },
	{ 0x10, "entertainment", DEVICECLASS_NONE, DEVICESUBCLASS_NONE },
	{ 0x80, "other", DEVICECLASS_NONE, DEVICESUBCLASS_NONE },
	{ PCI_INVALID_SUBCLASSCODE, "", DEVICECLASS_NONE, DEVICESUBCLASS_NONE }
};

static pciSubClass subclass_sigproc[] = {
	{ 0x00, "DPIO", DEVICECLASS_NONE, DEVICESUBCLASS_NONE },
	{ 0x01, "performance counter", DEVICECLASS_NONE, DEVICESUBCLASS_NONE },
	{ 0x10, "communications synch", DEVICECLASS_NONE, DEVICESUBCLASS_NONE },
	{ 0x20, "management", DEVICECLASS_NONE, DEVICESUBCLASS_NONE },
	{ 0x80, "other", DEVICECLASS_NONE, DEVICESUBCLASS_NONE },
	{ PCI_INVALID_SUBCLASSCODE, "", DEVICECLASS_NONE, DEVICESUBCLASS_NONE }
};

static pciSubClass subclass_prop[] = {
	{ 0x00, "unknown", DEVICECLASS_NONE, DEVICESUBCLASS_NONE },
	{ 0x80, "other", DEVICECLASS_NONE, DEVICESUBCLASS_NONE },
	{ PCI_INVALID_SUBCLASSCODE, "", DEVICECLASS_NONE, DEVICESUBCLASS_NONE }
};

static pciClass pciClassNames[] = {
	{ 0x00, "before PCI 2.0", subclass_old },
	{ 0x01, "disk controller", subclass_diskctrl },
	{ 0x02, "network interface", subclass_net },
	{ 0x03, "graphics adapter", subclass_graphics },
	{ 0x04, "multimedia controller", subclass_multimed },
	{ 0x05, "memory controller", subclass_mem },
	{ 0x06, "bridge device", subclass_bridge },
	{ 0x07, "communication controller", subclass_comm },
	{ 0x08, "system device", subclass_sys },
	{ 0x09, "input device", subclass_input },
	{ 0x0A, "docking station", subclass_dock },
	{ 0x0B, "CPU", subclass_cpu },
	{ 0x0C, "serial bus", subclass_serial },
	{ 0x0D, "wireless controller", subclass_wireless },
	{ 0x0E, "intelligent I/O controller", subclass_intelio },
	{ 0x0F, "satellite controller", subclass_sat },
	{ 0x10, "encryption controller", subclass_encrypt },
	{ 0x11, "signal processing controller", subclass_sigproc },
	{ 0xFF, "proprietary device", subclass_prop },
	{ PCI_INVALID_CLASSCODE, "", NULL }
};

static const char *unknownDevice = "unknown";
static const char *otherDevice = "other";

static kernelBusTarget *targets = NULL;
static int numTargets = 0;

#define headerAddress(bus, device, function, reg)					\
	(0x80000000L |  (((unsigned)((bus) & 0xFF) << 16) |			\
		(((device) & 0x1F)  << 11) | (((function) & 0x07) << 8) |	\
		(((reg) & 0x3F) << 2)))

// Make our proprietary PCI target code
#define makeTargetCode(bus, device, function)	\
	((((bus) & 0xFF) << 16) | (((device) & 0xFF) << 8) | ((function) & 0xFF))

// Translate a target code back to bus, device, function
#define makeBusDevFunc(targetCode, bus, device, function)	\
	{	(bus) = (((targetCode) >> 16) & 0xFF);				\
		(device) = (((targetCode) >> 8) & 0xFF);			\
		(function) = ((targetCode) & 0xFF);	}


static void readConfig8(int bus, int dev, int function, int reg,
			unsigned char *data)
{
	// Reads configuration byte
	unsigned address = headerAddress(bus, dev, function, (reg / 4));
	processorOutPort32(PCI_CONFIG_PORT, address);
	processorInPort8((PCI_DATA_PORT + (reg % 4)), *data);
	return;
}


static void writeConfig8(int bus, int dev, int function, int reg,
	unsigned char data)
{
	// Writes configuration byte
	unsigned address = headerAddress(bus, dev, function, (reg / 4));
	processorOutPort32(PCI_CONFIG_PORT, address);
	processorOutPort8((PCI_DATA_PORT + (reg % 4)), data);
	return;
}


static void readConfig16(int bus, int dev, int function, int reg,
	unsigned short *data)
{
	// Reads configuration word
	unsigned address = headerAddress(bus, dev, function, (reg / 2));
	processorOutPort32(PCI_CONFIG_PORT, address);
	processorInPort16((PCI_DATA_PORT + (reg % 2)), *data);
	return;
}


static void writeConfig16(int bus, int dev, int function, int reg,
	unsigned short data)
{
	// Writes configuration word
	unsigned address = headerAddress(bus, dev, function, (reg / 2));
	processorOutPort32(PCI_CONFIG_PORT, address);
	processorOutPort16((PCI_DATA_PORT + (reg % 2)), data);
	return;
}


static void readConfig32(int bus, int dev, int function, int reg,
	unsigned *data)
{
	// Reads configuration dword
	unsigned address = headerAddress(bus, dev, function, reg);
	processorOutPort32(PCI_CONFIG_PORT, address);
	processorInPort32(PCI_DATA_PORT, *data);
	return;
}


static void writeConfig32(int bus, int dev, int function, int reg,
	unsigned data)
{
	// Writes configuration dword
	unsigned address = headerAddress(bus, dev, function, reg);
	processorOutPort32(PCI_CONFIG_PORT, address);
	processorOutPort32(PCI_DATA_PORT, data);
	return;
}


static void readConfigHeader(int bus, int dev, int function,
	pciDeviceInfo *devInfo)
{
	// Fill up the supplied device info header

	unsigned address = 0;
	int reg;

	for (reg = 0; reg < (PCI_CONFIGHEADER_SIZE / 4); reg ++)
	{
		address = headerAddress(bus, dev, function, reg);
		processorOutPort32(PCI_CONFIG_PORT, address);
		processorInPort32(PCI_DATA_PORT, devInfo->header[reg]);
	}
}


static void getClass(int classCode, pciClass **class)
{
	// Return the PCI class, given the class code

	int count;

	for (count = 0; count < 256; count++)
	{
		// If no more classcodes are in the list
		if (pciClassNames[count].classCode == PCI_INVALID_CLASSCODE)
		{
			*class = NULL;
			return;
		}

		// If valid classcode is found
		if (pciClassNames[count].classCode == classCode)
		{
			*class = &pciClassNames[count];
			return;
		}
	}
}


static void getSubClass(pciClass *class, int subClassCode,
	pciSubClass **subClass)
{
	// Return the PCI subclass, given the class and subclass code

	int count;

	for (count = 0; count < 256; count++)
	{
		// If no more subclass codes are in the list
		if (class->subClasses[count].subClassCode == PCI_INVALID_SUBCLASSCODE)
		{
			*subClass = NULL;
			return;
		}

		// If valid subclass code is found
		if (class->subClasses[count].subClassCode == subClassCode)
		{
			*subClass = &class->subClasses[count];
			return;
		}
	}
}


static int getClassName(int classCode, int subClassCode, char **className,
	char **subClassName)
{
	// Returns name of the class and the subclass in human readable format.
	// Buffers classname and subclassname have to provide

	int status = 0;
	pciClass *class = NULL;
	pciSubClass *subClass = NULL;

	getClass(classCode, &class);
	if (!class)
	{
		*className = (char *) unknownDevice;
		return (status = PCI_INVALID_CLASSCODE);
	}

	*className = (char *) class->name;

	// Subclasscode 0x80 is always other
	if (subClassCode == 0x80)
	{
		*subClassName = (char *) otherDevice;
		return (status = 0);
	}

	getSubClass(class, subClassCode, &subClass);
	if (!subClass)
	{
		*subClassName = (char *) unknownDevice;
		return (status = PCI_INVALID_SUBCLASSCODE);
	}

	*subClassName = (char *) subClass->name;
	return (status = 0);
}


static void deviceInfo2BusTarget(kernelBus *bus, int busNum, int dev,
	int function, pciDeviceInfo *info, kernelBusTarget *target)
{
	// Translate a device info header to a bus target listing

	pciClass *class = NULL;
	pciSubClass *subClass = NULL;

	target->bus = bus;
	target->id = makeTargetCode(busNum, dev, function);

	getClass(info->device.classCode, &class);
	if (!class)
	{
		kernelDebugError("No class for classCode 0x%02x",
			info->device.classCode);
		return;
	}

	getSubClass(class, info->device.subClassCode, &subClass);
	if (!subClass)
	{
		kernelDebugError("No subclass for classCode 0x%02x, subClassCode "
			"0x%02x", info->device.classCode, info->device.subClassCode);
		return;
	}

	target->class = kernelDeviceGetClass(subClass->systemClassCode);
	target->subClass = kernelDeviceGetClass(subClass->systemSubClassCode);
}


static int driverGetTargets(kernelBus *bus, kernelBusTarget **pointer)
{
	// Generate the list of targets that reside on the given bus (controller).

	int status = 0;
	int targetCount = 0;
	int count;

	// Count up the number of targets that belong to the bus
	for (count = 0; count < numTargets; count ++)
	{
		if (targets[count].bus == bus)
			targetCount += 1;
	}

	if (!targetCount)
		return (targetCount = 0);

	// Allocate memory for all the targets
	*pointer = kernelMalloc(targetCount * sizeof(kernelBusTarget));
	if (!*pointer)
		return (status = ERR_MEMORY);

	// Loop again and copy targets
	for (targetCount = 0, count = 0; count < numTargets; count ++)
	{
		if (targets[count].bus == bus)
		{
			memcpy(&(*pointer)[targetCount], &targets[count],
				sizeof(kernelBusTarget));
			targetCount += 1;
		}
	}

	return (targetCount);
}


static int driverGetTargetInfo(kernelBusTarget *target, void *pointer)
{
	// Read the device's PCI header and copy it to the supplied memory
	// pointer

	int status = 0;
	int bus, dev, function;

	makeBusDevFunc(target->id, bus, dev, function);
	readConfigHeader(bus, dev, function, pointer);

	return (status = 0);
}


static unsigned driverReadRegister(kernelBusTarget *target, int reg,
	int bitWidth)
{
	// Returns the contents of a PCI configuration register

	unsigned contents = 0;
	int bus, dev, function;

	makeBusDevFunc(target->id, bus, dev, function);

	switch (bitWidth)
	{
		case 8:
			readConfig8(bus, dev, function, reg, (unsigned char *) &contents);
			break;
		case 16:
			readConfig16(bus, dev, function, reg, (unsigned short *) &contents);
			break;
		case 32:
			readConfig32(bus, dev, function, reg, &contents);
			break;
		default:
			kernelError(kernel_error, "Register width %d not supported",
				bitWidth);
	}

	return (contents);
}


static int driverWriteRegister(kernelBusTarget *target, int reg, int bitWidth,
	unsigned contents)
{
	// Write the contents of a PCI configuration register

	int status = 0;
	int bus, dev, function;

	makeBusDevFunc(target->id, bus, dev, function);

	switch (bitWidth)
	{
		case 8:
			writeConfig8(bus, dev, function, reg, (unsigned char) contents);
			break;
		case 16:
			writeConfig16(bus, dev, function, reg, (unsigned short) contents);
			break;
		case 32:
			writeConfig32(bus, dev, function, reg, contents);
			break;
		default:
			kernelError(kernel_error, "Register width %d not supported",
				bitWidth);
			status = ERR_RANGE;
			break;
	}

	return (status);
}


static void driverDeviceClaim(kernelBusTarget *target, kernelDriver *driver)
{
	// Allows a driver to claim a PCI bus device

	int count;

	// Find our copy of the target using the ID
	for (count = 0; count < numTargets; count ++)
	{
		if (targets[count].id == target->id)
		{
			kernelDebug(debug_pci, "PCI target 0x%08x claimed",
				targets[count].id);
			targets[count].claimed = driver;
		}
	}

  return;
}


static int driverDeviceEnable(kernelBusTarget *target, int enable)
{
	// Enables or disables a PCI bus device

	int bus, dev, function;
	unsigned short commandReg = 0;

	makeBusDevFunc(target->id, bus, dev, function);

	// Read the command register
	readConfig16(bus, dev, function, PCI_CONFREG_COMMAND_16, &commandReg);

	if (enable)
	{
		if (enable & PCI_COMMAND_IOENABLE)
			// Turn on I/O access
			commandReg |= PCI_COMMAND_IOENABLE;
		if (enable & PCI_COMMAND_MEMORYENABLE)
			// Turn on memory access
			commandReg |= PCI_COMMAND_MEMORYENABLE;
	}
	else
	{
		// Turn off I/O access and memory access
		commandReg &= ~(PCI_COMMAND_IOENABLE | PCI_COMMAND_MEMORYENABLE);
	}

	// Write back command register
	writeConfig16(bus, dev, function, PCI_CONFREG_COMMAND_16, commandReg);

	return (0);
}


static int driverSetMaster(kernelBusTarget *target, int master)
{
	// Sets the target device as a bus master

	int bus, dev, function;
	unsigned short commandReg = 0;
	unsigned char latency = 0;

	makeBusDevFunc(target->id, bus, dev, function);

	// Read the command register
	readConfig16(bus, dev, function, PCI_CONFREG_COMMAND_16, &commandReg);

	// Toggle busmaster bit
	if (master)
		commandReg |= PCI_COMMAND_MASTERENABLE;
	else
		commandReg &= ~PCI_COMMAND_MASTERENABLE;

	// Write back command register
	writeConfig16(bus, dev, function, PCI_CONFREG_COMMAND_16, commandReg);

	// Check latency timer
	readConfig8(bus, dev, function, PCI_CONFREG_LATENCY_8, &latency);

	if (latency < 0x10)
	{
		latency = 0x40;
		writeConfig8(bus, dev, function, PCI_CONFREG_LATENCY_8, latency);
	}

	return (0);
}


static int driverDetect(void *parent, kernelDriver *driver)
{
	// This routine is used to detect and initialize each PCI controller
	// device, as well as registering each one with any higher-level
	// interfaces.

	int status = 0;
	unsigned reply = 0;
	int busCount = 0;
	int deviceCount = 0;
	int functionCount = 0;
	char *className = NULL;
	char *subclassName = NULL;
	int intNumber = 0;
	pciDeviceInfo pciDevice;
	kernelDevice *dev = NULL;
	kernelBus *bus = NULL;

	// Check for a configuration mechanism #1 able PCI controller.
	processorOutPort32(PCI_CONFIG_PORT, 0x80000000L);
	processorInPort32(PCI_CONFIG_PORT, reply);

	if (reply != 0x80000000L)
		// No device that uses configuration mechanism #1.  Fine enough: No PCI
		// functionality for you.
		return (status = 0);

	// First count all the devices on the bus
	for (busCount = 0; busCount < PCI_MAX_BUSES; busCount ++)
	{
		for (deviceCount = 0; deviceCount < PCI_MAX_DEVICES; deviceCount ++)
		{
			for (functionCount = 0; functionCount < PCI_MAX_FUNCTIONS;
				functionCount ++)
			{
				// Just read the first dword of the header to get the device
				// and vendor IDs
				readConfig32(busCount, deviceCount, functionCount, 0,
					&pciDevice.header[0]);

				// See if this is really a device, or if this device header is
				// unoccupied.
				if (!pciDevice.device.vendorID ||
					(pciDevice.device.vendorID == 0xFFFF) ||
					(pciDevice.device.deviceID == 0xFFFF))
				{
					// No device here, so try next one
					continue;
				}

				kernelDebug(debug_pci, "PCI found device bus=%d device=%d "
					"function=%d", busCount, deviceCount, functionCount);

				// If here, we found a PCI device
				numTargets += 1;
			}
		}
	}

	// Allocate memory for the PCI bus device
	dev = kernelMalloc(sizeof(kernelDevice));
	if (!dev)
		return (status = ERR_MEMORY);

	dev->device.class = kernelDeviceGetClass(DEVICECLASS_BUS);
	dev->device.subClass = kernelDeviceGetClass(DEVICESUBCLASS_BUS_PCI);
	dev->driver = driver;

	// Allocate memory for the bus service
	bus = kernelMalloc(sizeof(kernelBus));
	if (!bus)
		return (status = ERR_MEMORY);

	bus->type = bus_pci;
	bus->dev = dev;
	bus->ops = driver->ops;

	// Allocate memory for the targets list
	targets = kernelMalloc(numTargets * sizeof(kernelBusTarget));
	if (!targets)
		return (status = ERR_MEMORY);

	// Now fill up our targets list
	for (numTargets = 0, busCount = 0; busCount < PCI_MAX_BUSES; busCount ++)
	{
		for (deviceCount = 0; deviceCount < PCI_MAX_DEVICES; deviceCount ++)
		{
			for (functionCount = 0; functionCount < PCI_MAX_FUNCTIONS;
				functionCount ++)
			{
				// Just read the first dword of the header to get the device
				// and vendor IDs
				readConfig32(busCount, deviceCount, functionCount, 0,
					&pciDevice.header[0]);

				if (!pciDevice.device.vendorID ||
					(pciDevice.device.vendorID == 0xFFFF) ||
					(pciDevice.device.deviceID == 0xFFFF))
				{
					// No device here, so try next one
					continue;
				}

				// There's a device.  Get the full device header.
				readConfigHeader(busCount, deviceCount, functionCount,
					&pciDevice);

				getClassName(pciDevice.device.classCode,
					pciDevice.device.subClassCode, &className,
					&subclassName);

				kernelDebug(debug_pci, "PCI %s %s %u:%u:%u int:%d pin=%c",
					subclassName, className, busCount, deviceCount,
					functionCount, pciDevice.device.all.interruptLine,
					(pciDevice.device.all.interruptPin?
						('@' + pciDevice.device.all.interruptPin) : ' '));

				if (pciDevice.device.all.interruptPin)
				{
					// If we have reassigned the interrupt number by
					// initializing APICs, update it
					intNumber = kernelPicGetIntNumber(busCount,
						((deviceCount << 2) |
							(pciDevice.device.all.interruptPin - 1)));

					if (intNumber >= 0)
					{
						kernelDebug(debug_pci, "PCI interrupt %d reassigned "
							"to %d", pciDevice.device.all.interruptLine,
							intNumber);

						writeConfig8(busCount, deviceCount, functionCount,
							PCI_CONFREG_INTLINE_8, intNumber);
						readConfig8(busCount, deviceCount, functionCount,
							PCI_CONFREG_INTLINE_8,
							&pciDevice.device.all.interruptLine);
					}
				}

				kernelLog("PCI: %s %s %u:%u:%u vend:0x%04x dev:0x%04x",
					subclassName, className, busCount, deviceCount,
					functionCount, pciDevice.device.vendorID,
					pciDevice.device.deviceID);
				kernelLog("  class:0x%02x sub:0x%02x int:%d pin=%c "
					"caps=%s", pciDevice.device.classCode,
					pciDevice.device.subClassCode,
					pciDevice.device.all.interruptLine,
					(pciDevice.device.all.interruptPin?
						('@' + pciDevice.device.all.interruptPin) : ' '),
					((pciDevice.device.statusReg & PCI_STATUS_CAPSLIST)?
						"yes" : "no"));

				deviceInfo2BusTarget(bus, busCount, deviceCount, functionCount,
					&pciDevice, &targets[numTargets]);
				numTargets += 1;
			}
		}
	}

	// Add the kernel device
	status = kernelDeviceAdd(parent, dev);
	if (status < 0)
	{
		kernelFree(bus);
		kernelFree(dev);
		return (status);
	}

	// Register the bus service
	status = kernelBusRegister(bus);
	if (status < 0)
	{
		kernelFree(bus);
		kernelFree(dev);
		return (status);
	}

	return (status = 0);
}


// Our driver operations structure.
static kernelBusOps pciOps = {
	driverGetTargets,
	driverGetTargetInfo,
	driverReadRegister,
	driverWriteRegister,
	driverDeviceClaim,
	driverDeviceEnable,
	driverSetMaster,
	NULL, // driverRead
	NULL  // driverWrite
};


/////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////
//
//  Below here, the functions are exported for external use
//
/////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////

void kernelPciDriverRegister(kernelDriver *driver)
{
	// Device driver registration.

	driver->driverDetect = driverDetect;
	driver->ops = &pciOps;

	return;
}


void kernelPciPrintHeader(pciDeviceInfo *devInfo)
{
	// Print out the supplied configuration header

	if (!devInfo)
	{
		kernelError(kernel_error, "NULL parameter");
		return;
	}

#ifdef DEBUG

	kernelDebug(debug_pci, "PCI --- start device header ---");
	kernelDebug(debug_pci, "   vendorID=0x%04x deviceID=0x%04x "
		"commandReg=0x%04x statusReg=0x%04x", devInfo->device.vendorID,
		devInfo->device.deviceID, devInfo->device.commandReg,
		devInfo->device.statusReg);
	kernelDebug(debug_pci, "   revisionID=0x%02x progIF=0x%02x "
		"subClassCode=0x%02x classCode=0x%02x",
		devInfo->device.revisionID, devInfo->device.progIF,
		devInfo->device.subClassCode, devInfo->device.classCode);
	kernelDebug(debug_pci, "   cachelineSize=0x%02x latency=0x%02x "
		"headerType=0x%02x BIST=0x%02x", devInfo->device.cachelineSize,
		devInfo->device.latency, devInfo->device.headerType,
		devInfo->device.BIST);

	switch (devInfo->device.headerType & ~PCI_HEADERTYPE_MULTIFUNC)
	{
		case PCI_HEADERTYPE_NORMAL:
			kernelDebug(debug_pci, "   baseAddress0=0x%08x baseAddress1=0x%08x",
				devInfo->device.nonBridge.baseAddress[0],
				devInfo->device.nonBridge.baseAddress[1]);
			kernelDebug(debug_pci, "   baseAddress2=0x%08x baseAddress3=0x%08x",
				devInfo->device.nonBridge.baseAddress[2],
				devInfo->device.nonBridge.baseAddress[3]);
			kernelDebug(debug_pci, "   baseAddress4=0x%08x baseAddress5=0x%08x",
				devInfo->device.nonBridge.baseAddress[4],
				devInfo->device.nonBridge.baseAddress[5]);
			kernelDebug(debug_pci, "   cardBusCIS=0x%08x "
				"subsystemVendorID=0x%04x",
				devInfo->device.nonBridge.cardBusCIS,
				devInfo->device.nonBridge.subsystemVendorID);
			kernelDebug(debug_pci, "   subsystemDeviceID=0x%04x "
				"expansionROM=0x%08x ",
				devInfo->device.nonBridge.subsystemDeviceID,
				devInfo->device.nonBridge.expansionROM);
			kernelDebug(debug_pci, "   capPtr=0x%02x interruptLine=%d "
				"interruptPin=%s%c%s (%d)", devInfo->device.nonBridge.capPtr,
				devInfo->device.nonBridge.interruptLine,
				(devInfo->device.nonBridge.interruptPin? "INT" : ""),
				(devInfo->device.nonBridge.interruptPin?
					('@' + devInfo->device.nonBridge.interruptPin) : ' '),
				(devInfo->device.nonBridge.interruptPin? "#" : ""),
				devInfo->device.nonBridge.interruptPin);
			kernelDebug(debug_pci, "   minGrant=0x%02x maxLatency=0x%02x",
				devInfo->device.nonBridge.minGrant,
				devInfo->device.nonBridge.maxLatency);
			kernelDebugHex(devInfo->device.nonBridge.deviceSpecific, 192);
			break;

		default:
			kernelDebugError("Unsupported header type 0x%02x",
				(devInfo->device.headerType & ~PCI_HEADERTYPE_MULTIFUNC));
			break;
	}
	kernelDebug(debug_pci, "PCI --- end device header ---");

#endif // DEBUG

	return;
}


pciCapHeader *kernelPciGetCapability(pciDeviceInfo *devInfo,
	pciCapHeader *capHeader)
{
	// Allows the caller to iterate through the capabilities of a device

	// Check params
	if (!devInfo)
	{
		kernelError(kernel_error, "NULL parameter");
		return (capHeader = NULL);
	}

	if (devInfo->device.statusReg & PCI_STATUS_CAPSLIST)
	{
		switch (devInfo->device.headerType & ~PCI_HEADERTYPE_MULTIFUNC)
		{
			case PCI_HEADERTYPE_NORMAL:
				if (capHeader)
				{
					if (capHeader->next)
						capHeader = ((void *) devInfo + capHeader->next);
					else
						capHeader = NULL;
				}
				else
				{
					capHeader = ((void *) devInfo +
						devInfo->device.nonBridge.capPtr);
				}
				break;

			default:
				kernelDebugError("Unsupported header type 0x%02x",
					(devInfo->device.headerType & ~PCI_HEADERTYPE_MULTIFUNC));
				capHeader = NULL;
				break;
		}
	}
	else
	{
		// No capabilities
		capHeader = NULL;
	}

	return (capHeader);
}


void kernelPciPrintCapabilities(pciDeviceInfo *devInfo)
{
	// Print out the supplied configuration header

	// Check params
	if (!devInfo)
	{
		kernelError(kernel_error, "NULL parameter");
		return;
	}

#ifdef DEBUG

	pciCapHeader *capHeader = NULL;
	pciMsiCap *msiCap = NULL;
	pciMsiXCap *msiXCap = NULL;

	if (devInfo->device.statusReg & PCI_STATUS_CAPSLIST)
	{
		kernelDebug(debug_pci, "PCI --- start device capabilities ---");

		capHeader = kernelPciGetCapability(devInfo, capHeader);
		while (capHeader)
		{
			kernelDebug(debug_pci, "  id=0x%02x next=%d", capHeader->id,
				capHeader->next);
			switch (capHeader->id)
			{
				case PCI_CAPABILITY_MSI:
					msiCap = (pciMsiCap *) capHeader;
					kernelDebug(debug_pci, "  MSI: msgCtrl=0x%04x "
						"msgAddr=%p msgData=0x%04x", msiCap->msgCtrl,
						msiCap->msgAddr, msiCap->msgData);
					break;

				case PCI_CAPABILITY_MSIX:
					msiXCap = (pciMsiXCap *) capHeader;
					kernelDebug(debug_pci, "  MSI-X: msgCtrl=0x%04x "
						"msgUpperAddr=%p tableOffBir=%08x", msiXCap->msgCtrl,
						msiXCap->msgUpperAddr, msiXCap->tableOffBir);
					break;

				default:
					break;
			}

			capHeader = kernelPciGetCapability(devInfo, capHeader);
		}

		kernelDebug(debug_pci, "PCI --- end device capabilities ---");
	}
	else
	{
		kernelDebug(debug_pci, "PCI no capabilities reported");
	}

#endif // DEBUG

	return;
}

