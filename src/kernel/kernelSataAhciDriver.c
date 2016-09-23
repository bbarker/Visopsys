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
//  kernelSataAhciDriver.c
//

// Driver for standard AHCI SATA controllers

#include "kernelSataAhciDriver.h"
#include "kernelAtaDriver.h"
#include "kernelCpu.h"
#include "kernelDebug.h"
#include "kernelDevice.h"
#include "kernelDriver.h"
#include "kernelError.h"
#include "kernelInterrupt.h"
#include "kernelLog.h"
#include "kernelMalloc.h"
#include "kernelMemory.h"
#include "kernelMultitasker.h"
#include "kernelPage.h"
#include "kernelParameters.h"
#include "kernelPciDriver.h"
#include "kernelPic.h"
#include "kernelVariableList.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/processor.h>

#define DISK_CTRL(diskNum) (&controllers[diskNum >> 8])
#define DISK(diskNum) (DISK_CTRL(diskNum)->disk[diskNum & 0xFF])

static ahciController *controllers = NULL;
static int numControllers = 0;

// Saved old interrupt handlers
static void **oldIntHandlers = NULL;
static int numOldHandlers = 0;


#ifdef DEBUG
static inline void debugAhciCapReg(ahciRegs *regs)
{
	char output[1024] = { 0 };
	const char *interfaceSpeed = NULL;

	sprintf(output, "AHCI capability register (0x%08x):\n"
		"  64bitAddr=%d\n"
		"  nativeCmdQueue=%d\n", regs->CAP,
		((regs->CAP & AHCI_CAP_S64A) >> 31),
		((regs->CAP & AHCI_CAP_SNCQ) >> 30));

	if (regs->VS >= AHCI_VERSION_1_1)
		sprintf((output + strlen(output)), "  sNotificationReg=%d\n",
			((regs->CAP & AHCI_CAP_SSNTF) >> 29));

	if (regs->VS >= AHCI_VERSION_1_1)
		sprintf((output + strlen(output)), "  mechPresenceSwitch=%d\n",
			((regs->CAP & AHCI_CAP_SMPS) >> 28));
	else
		sprintf((output + strlen(output)), "  interlockSwitch=%d\n",
			((regs->CAP & AHCI_CAP_SIS) >> 28));

	switch ((regs->CAP & AHCI_CAP_ISS) >> 20)
	{
		case 0x1:
			interfaceSpeed = "Gen 1 (1.5 Gbps)";
			break;
		case 0x2:
			interfaceSpeed = "Gen 2 (3 Gbps)";
			break;
		case 0x3:
			interfaceSpeed = "Gen 3 (6 Gbps)";
			break;
		default:
			interfaceSpeed = "reserved (unknown)";
			break;
	}

	sprintf((output + strlen(output)),
		"  staggeredSpinup=%d\n"
		"  aggrLinkPowerMgmt=%d\n"
		"  activityLed=%d\n"
		"  cmdListOverride=%d\n"
		"  interfaceSpeed=%d - %s\n",
		((regs->CAP & AHCI_CAP_SSS) >> 27),
		((regs->CAP & AHCI_CAP_SALP) >> 26),
		((regs->CAP & AHCI_CAP_SAL) >> 25),
		((regs->CAP & AHCI_CAP_SCLO) >> 24),
		((regs->CAP & AHCI_CAP_ISS) >> 20), interfaceSpeed);

	if (regs->VS < AHCI_VERSION_1_2)
		sprintf((output + strlen(output)), "  nonZeroDmaOffsets=%d\n",
			((regs->CAP & AHCI_CAP_SNZO) >> 19));

	sprintf((output + strlen(output)),
		"  ahciModeOnly=%d\n"
		"  portMultiplier=%d\n",
		((regs->CAP & AHCI_CAP_SAM) >> 18),
		((regs->CAP & AHCI_CAP_SPM) >> 17));

	if (regs->VS >= AHCI_VERSION_1_1)
		sprintf((output + strlen(output)), "  fisBasedSwitching=%d\n",
			((regs->CAP & AHCI_CAP_FBSS) >> 16));

	sprintf((output + strlen(output)),
		"  pioMultiDrqBlock=%d\n"
		"  slumberStateCap=%d\n"
		"  partialStateCap=%d\n"
		"  numCmdSlots=%d\n",
		((regs->CAP & AHCI_CAP_PMD) >> 15),
		((regs->CAP & AHCI_CAP_SSC) >> 14),
		((regs->CAP & AHCI_CAP_PSC) >> 13),
		(((regs->CAP & AHCI_CAP_NCS) >> 8) + 1));

	if (regs->VS >= AHCI_VERSION_1_1)
	{
		sprintf((output + strlen(output)),
			"  cmdCompCoalescing=%d\n"
			"  enclosureMgmt=%d\n"
			"  externalSata=%d\n",
			((regs->CAP & AHCI_CAP_CCCS) >> 7),
			((regs->CAP & AHCI_CAP_EMS) >> 6),
			((regs->CAP & AHCI_CAP_SXS) >> 5));
	}

	sprintf((output + strlen(output)), "  numPorts=%d",
		((regs->CAP & AHCI_CAP_NP) + 1));

	kernelDebug(debug_io, "%s", output);
}

static inline void debugAhciPortRegs(int portNum, ahciPortRegs *regs)
{
	kernelDebug(debug_io, "AHCI port %d registers:\n"
		"  CLB=0x%08x\tCLBU=0x%08x\n"
		"  FB=0x%08x\tFBU=0x%08x\n"
		"  IS=0x%08x\tIE=0x%08x\n"
		"  CMD=0x%08x\tTFD=0x%08x\n"
		"  SIG=0x%08x\tSSTS=0x%08x\n"
		"  SCTL=0x%08x\tSERR=0x%08x\n"
		"  SACT=0x%08x\tCI=0x%08x\n"
		"  SNTF=0x%08x", portNum, regs->CLB, regs->CLBU, regs->FB,
		regs->FBU, regs->IS, regs->IE, regs->CMD, regs->TFD, regs->SIG,
		regs->SSTS, regs->SCTL, regs->SERR, regs->SACT, regs->CI, regs->SNTF);
}
#else
	#define debugAhciCapReg(regs) do { } while (0)
	#define debugAhciPortRegs(portNum, regs) do { } while (0)
#endif // DEBUG


static int detectPciControllers(void)
{
	// Try to detect AHCI controllers on the PCI bus

	int status = 0;
	kernelBusTarget *pciTargets = NULL;
	int numPciTargets = 0;
	int deviceCount = 0;
	pciDeviceInfo pciDevInfo;
	unsigned physMemSpace;
	unsigned memSpaceSize;

	// See if there are any AHCI controllers on the PCI bus.  This obviously
	// depends upon PCI hardware detection occurring before AHCI detection.

	// Search the PCI bus(es) for devices
	numPciTargets = kernelBusGetTargets(bus_pci, &pciTargets);
	if (numPciTargets <= 0)
	{
		kernelDebug(debug_io, "AHCI no PCI targets");
		return (status = numPciTargets);
	}

	// Search the PCI bus targets for AHCI controllers
	for (deviceCount = 0; deviceCount < numPciTargets; deviceCount ++)
	{
		// If it's not an AHCI controller, skip it
		if (!pciTargets[deviceCount].class ||
			(pciTargets[deviceCount].class->class != DEVICECLASS_DISKCTRL) ||
			!pciTargets[deviceCount].subClass ||
			(pciTargets[deviceCount].subClass->class !=
				DEVICESUBCLASS_DISKCTRL_SATA))
		{
			continue;
		}

		// Get the PCI device header
		status = kernelBusGetTargetInfo(&pciTargets[deviceCount], &pciDevInfo);
		if (status < 0)
		{
			kernelDebug(debug_io, "AHCI error getting PCI target info");
			continue;
		}

		kernelDebug(debug_io, "AHCI check PCI device %x %x progif=%02x",
			(pciTargets[deviceCount].class?
				pciTargets[deviceCount].class->class : 0),
			(pciTargets[deviceCount].subClass?
				pciTargets[deviceCount].subClass->class : 0),
			pciDevInfo.device.progIF);

		// Make sure it's a non-bridge header
		if (pciDevInfo.device.headerType != PCI_HEADERTYPE_NORMAL)
		{
			kernelDebug(debug_io, "AHCI PCI headertype not 'normal' (%d)",
				pciDevInfo.device.headerType);
			continue;
		}

		// Make sure it's an AHCI controller (programming interface is 0x01 in
		// the PCI header)
		if (pciDevInfo.device.progIF != 0x01)
		{
			kernelDebug(debug_io, "AHCI PCI SATA controller not AHCI");
			continue;
		}

		kernelDebug(debug_io, "AHCI PCI SATA found");

		// Try to enable bus mastering
		if (!(pciDevInfo.device.commandReg & PCI_COMMAND_MASTERENABLE))
		{
			kernelBusSetMaster(&pciTargets[deviceCount], 1);

			// Re-read target info
			kernelBusGetTargetInfo(&pciTargets[deviceCount], &pciDevInfo);

			if (!(pciDevInfo.device.commandReg & PCI_COMMAND_MASTERENABLE))
				kernelDebugError("Couldn't enable PCI bus mastering");
			else
				kernelDebug(debug_io, "AHCI PCI bus mastering enabled");
		}
		else
		{
			kernelDebug(debug_io, "AHCI PCI bus mastering already enabled");
		}

		// Make sure the ABAR refers to a memory decoder
		if (pciDevInfo.device.nonBridge.baseAddress[5] & 0x00000001)
		{
			kernelError(kernel_error, "PCI ABAR register is not a memory "
				"decoder");
			continue;
		}

		// Print registers
		kernelDebug(debug_io, "AHCI PCI interrupt line=%d",
			pciDevInfo.device.nonBridge.interruptLine);
		kernelDebug(debug_io, "AHCI PCI ABAR base address reg=%08x",
			pciDevInfo.device.nonBridge.baseAddress[5]);

		// (Re)allocate memory for the controllers
		controllers = kernelRealloc((void *) controllers,
			((numControllers + 1) * sizeof(ahciController)));
		if (!controllers)
			return (status = ERR_MEMORY);

		// Set the controller number
		controllers[numControllers].num = numControllers;

		// Make a copy of the bus target
		memcpy((kernelBusTarget *) &controllers[numControllers].busTarget,
			&pciTargets[deviceCount], sizeof(kernelBusTarget));

		// Get the interrupt number
		if (pciDevInfo.device.nonBridge.interruptLine != 0xFF)
		{
			kernelDebug(debug_io, "AHCI Using PCI interrupt=%d",
				pciDevInfo.device.nonBridge.interruptLine);
			controllers[numControllers].interrupt =
				pciDevInfo.device.nonBridge.interruptLine;
		}
		else
		{
			kernelDebugError("Unknown PCI interrupt=%d",
				pciDevInfo.device.nonBridge.interruptLine);
		}

		// Get the memory range address
		physMemSpace = (pciDevInfo.device.nonBridge.baseAddress[5] &
			0xFFFFFFF0);

		kernelDebug(debug_io, "AHCI PCI registers address %08x", physMemSpace);

		// Determine the memory space size.  Write all 1s to the register.
		kernelBusWriteRegister(&pciTargets[deviceCount],
			PCI_CONFREG_BASEADDRESS5_32, 32, 0xFFFFFFFF);

		memSpaceSize = (~(kernelBusReadRegister(&pciTargets[deviceCount],
			PCI_CONFREG_BASEADDRESS5_32, 32) & ~0xF) + 1);

		kernelDebug(debug_io, "AHCI PCI memory size %08x (%d)", memSpaceSize,
			memSpaceSize);

		// Restore the register we clobbered.
		kernelBusWriteRegister(&pciTargets[deviceCount],
			PCI_CONFREG_BASEADDRESS5_32, 32,
			pciDevInfo.device.nonBridge.baseAddress[5]);

		kernelDebug(debug_io, "AHCI ABAR now %08x",
			kernelBusReadRegister(&pciTargets[deviceCount],
				PCI_CONFREG_BASEADDRESS5_32, 32));

		// Map the physical memory address of the controller's registers into
		// our virtual address space.

		// Map the physical memory space pointed to by the decoder.
		status = kernelPageMapToFree(KERNELPROCID, physMemSpace,
			(void **) &controllers[numControllers].regs, memSpaceSize);
		if (status < 0)
		{
			kernelError(kernel_error, "Error mapping memory");
			continue;
		}

		// Make it non-cacheable, since this memory represents memory-mapped
		// hardware registers.
		status = kernelPageSetAttrs(KERNELPROCID, 1 /* set */,
			PAGEFLAG_CACHEDISABLE, (void *) controllers[numControllers].regs,
			memSpaceSize);
		if (status < 0)
			kernelDebugError("Error setting page attrs");

		// Enable memory mapping access
		if (!(pciDevInfo.device.commandReg & PCI_COMMAND_MEMORYENABLE))
		{
			kernelBusDeviceEnable(&pciTargets[deviceCount],
				PCI_COMMAND_MEMORYENABLE);

			// Re-read target info
			kernelBusGetTargetInfo(&pciTargets[deviceCount], &pciDevInfo);

			if (!(pciDevInfo.device.commandReg & PCI_COMMAND_MEMORYENABLE))
			{
				kernelError(kernel_error, "Couldn't enable PCI memory access");
				continue;
			}

			kernelDebug(debug_io, "AHCI PCI memory access enabled");
		}
		else
		{
			kernelDebug(debug_io, "AHCI PCI memory access already enabled");
		}

		numControllers += 1;
	}

	kernelFree(pciTargets);
	return (status = 0);
}


static void spinUpPorts(ahciController *controller)
{
	ahciPortRegs *portRegs = NULL;
	int count;

	kernelDebug(debug_io, "AHCI spin up ports");

	for (count = 0; count < AHCI_MAX_PORTS; count ++)
	{
		// Port implemented?
		if (!(controller->regs->PI & (1 << count)))
			continue;

		portRegs = &controller->regs->port[count];

		if (!(portRegs->CMD & AHCI_PXCMD_SUD))
		{
			kernelDebug(debug_io, "AHCI spin up port %d", count);
			portRegs->CMD |= AHCI_PXCMD_SUD;

			if (!(portRegs->CMD & AHCI_PXCMD_SUD))
				kernelDebugError("Port %d not spinning", count);
		}
		else
		{
			kernelDebug(debug_io, "AHCI port %d already spinning", count);
		}
	}

	return;
}


static int startStopPortCommands(ahciController *controller, int portNum,
	int start)
{
	// Tell a port to start or stop processing the command list

	int status = 0;
	ahciPortRegs *portRegs = NULL;
	int count;

	kernelDebug(debug_io, "AHCI %s port %d commands", (start? "start" : "stop"),
		portNum);

	portRegs = &controller->regs->port[portNum];

	// If the command list is already running/not running, print a debug
	// message
	if ((start && (portRegs->CMD & AHCI_PXCMD_CR)) ||
		(!start && !(portRegs->CMD & AHCI_PXCMD_CR)))
	{
		kernelDebug(debug_io, "AHCI port %d commands already %s", portNum,
			(start? "started" : "stopped"));
	}

	// Set or clear the 'start' bit in any case
	if (start)
	{
		// If the controller supports the command list override, do that before
		// we set the 'start' bit, in order to clear any 'BSY' or 'DRQ' bits
		if (controller->regs->CAP & AHCI_CAP_SCLO)
		{
			portRegs->CMD |= AHCI_PXCMD_CLO;

			for (count = 0; count < 500; count ++)
			{
				if (!(portRegs->CMD & AHCI_PXCMD_CLO))
					break;

				kernelCpuSpinMs(1);
			}
		}

		portRegs->CMD |= AHCI_PXCMD_ST;
	}
	else
	{
		portRegs->CMD &= ~AHCI_PXCMD_ST;
	}

	if ((start && !(portRegs->CMD & AHCI_PXCMD_CR)) ||
		(!start && (portRegs->CMD & AHCI_PXCMD_CR)))
	{
		// Wait up to 500ms for 'command list running' bit to change
		for (count = 0; count < 500; count ++)
		{
			if ((start && (portRegs->CMD & AHCI_PXCMD_CR)) ||
				(!start && !(portRegs->CMD & AHCI_PXCMD_CR)))
			{
				break;
			}

			kernelCpuSpinMs(1);
		}

		if ((start && !(portRegs->CMD & AHCI_PXCMD_CR)) ||
			(!start && (portRegs->CMD & AHCI_PXCMD_CR)))
		{
			kernelError(kernel_error, "Could not %s port %d commands",
				(start? "start" : "stop"), portNum);
			return (status = ERR_TIMEOUT);
		}

		kernelDebug(debug_io, "AHCI port %d commands %s", portNum,
			(start? "started" : "stopped"));
	}

	return (status = 0);
}


static int startStopPortReceives(ahciController *controller, int portNum,
	int start)
{
	// Tell a port to start or stop receiving FISes

	int status = 0;
	ahciPortRegs *portRegs = NULL;
	int count;

	kernelDebug(debug_io, "AHCI %s port %d receives", (start? "start" : "stop"),
		portNum);

	portRegs = &controller->regs->port[portNum];

	// If port receives are already running/not running, print a debug
	// message
	if ((start && (portRegs->CMD & AHCI_PXCMD_FR)) ||
		(!start && !(portRegs->CMD & AHCI_PXCMD_FR)))
	{
		kernelDebug(debug_io, "AHCI port %d receives already %s", portNum,
			(start? "started" : "stopped"));
	}

	// Set or clear the 'receive enable' bit in any case
	if (start)
		portRegs->CMD |= AHCI_PXCMD_FRE;
	else
		portRegs->CMD &= ~AHCI_PXCMD_FRE;

	if ((start && !(portRegs->CMD & AHCI_PXCMD_FR)) ||
		(!start && (portRegs->CMD & AHCI_PXCMD_FR)))
	{
		// Wait up to 500ms for the 'receive running' bit to change
		for (count = 0; count < 500; count ++)
		{
			if ((start && !(portRegs->CMD & AHCI_PXCMD_FR)) ||
				(!start && (portRegs->CMD & AHCI_PXCMD_FR)))
			{
				break;
			}

			kernelCpuSpinMs(1);
		}

		if ((start && !(portRegs->CMD & AHCI_PXCMD_FR)) ||
			(!start && (portRegs->CMD & AHCI_PXCMD_FR)))
		{
			kernelError(kernel_error, "Could not %s port %d receives",
				(start? "start" : "stop"), portNum);
			return (status = ERR_TIMEOUT);
		}

		kernelDebug(debug_io, "AHCI port %d receives %s", portNum,
			(start? "started" : "stopped"));
	}

	return (status = 0);
}


static int setPortIdle(ahciController *controller, int portNum)
{
	// Tell the controller to put the port into an idle state

	int status = 0;

	kernelDebug(debug_io, "AHCI set port %d idle", portNum);

	// Tell the port to stop processing the command list
	status = startStopPortCommands(controller, portNum, 0);
	if (status < 0)
	{
		kernelError(kernel_error, "Could not idle port %d", portNum);
		return (status);
	}

	// Tell the port to stop receiving FISes
	status = startStopPortReceives(controller, portNum, 0);
	if (status < 0)
	{
		kernelError(kernel_error, "Could not idle port %d", portNum);
		return (status);
	}

	return (status = 0);
}


static int allocPortMemory(ahciController *controller, int portNum)
{
	int status = 0;
	ahciPortRegs *portRegs = NULL;
	kernelIoMemory cmdIoMem;
	kernelIoMemory fisIoMem;

	kernelDebug(debug_io, "AHCI allocate memory for port %d", portNum);

	portRegs = &controller->regs->port[portNum];

	// Get physical memory for the port's command list.  It is a 1Kb structure
	// that needs to reside on a 1Kb boundary

	if (sizeof(ahciCommandList) != AHCI_CMDLIST_SIZE)
	{
		kernelDebugError("ahciCommandList is not 1Kb in size");
		return (status = ERR_RANGE);
	}

	status = kernelMemoryGetIo(sizeof(ahciCommandList),
		max(AHCI_CMDLIST_ALIGN, MEMORY_BLOCK_SIZE), &cmdIoMem);
	if (status < 0)
		return (status);

	if (cmdIoMem.physical % AHCI_CMDLIST_ALIGN)
	{
		kernelError(kernel_error, "Port command list is not 1Kb-aligned");
		kernelMemoryReleaseIo(&cmdIoMem);
		return (status = ERR_ALIGN);
	}

	portRegs->CLB = cmdIoMem.physical;
	if (controller->regs->CAP & AHCI_CAP_S64A)
		portRegs->CLBU = 0;

	controller->port[portNum].commandList = cmdIoMem.virtual;

	// Get physical memory for the port's received FISes.  It is a 256b
	// structure that needs to reside on a 256b boundary

	if (sizeof(ahciReceivedFises) != AHCI_RECVFIS_SIZE)
	{
		kernelDebugError("ahciReceivedFises is not 256b in size");
		kernelMemoryReleaseIo(&cmdIoMem);
		return (status = ERR_RANGE);
	}

	status = kernelMemoryGetIo(sizeof(ahciReceivedFises),
		max(AHCI_RECVFIS_ALIGN, MEMORY_BLOCK_SIZE), &fisIoMem);
	if (status < 0)
	{
		kernelMemoryReleaseIo(&cmdIoMem);
		return (status);
	}

	if (fisIoMem.physical % AHCI_RECVFIS_ALIGN)
	{
		kernelError(kernel_error, "Port received FISes structure is not "
			"256b-aligned");
		kernelMemoryReleaseIo(&fisIoMem);
		kernelMemoryReleaseIo(&cmdIoMem);
		return (status = ERR_ALIGN);
	}

	portRegs->FB = fisIoMem.physical;
	if (controller->regs->CAP & AHCI_CAP_S64A)
		portRegs->FBU = 0;

	controller->port[portNum].recvFis = fisIoMem.virtual;

	return (status = 0);
}


static int initializePorts(ahciController *controller)
{
	int status = 0;
	ahciPortRegs *portRegs = NULL;
	int count;

	kernelDebug(debug_io, "AHCI initialize ports");

	for (count = 0; count < AHCI_MAX_PORTS; count ++)
	{
		// Port implemented?
		if (!(controller->regs->PI & (1 << count)))
			continue;

		portRegs = &controller->regs->port[count];

		// The spec says that we first have to ensure that all of the
		// implemented ports are idle.  If any of the fields ST, CR, FRE, or
		// FR are non-zero, the port needs to be put into an idle state
		if (portRegs->CMD & (AHCI_PXCMD_ST | AHCI_PXCMD_CR | AHCI_PXCMD_FRE |
			AHCI_PXCMD_FR))
		{
			kernelDebug(debug_io, "AHCI port %d not idle", count);

			status = setPortIdle(controller, count);
			if (status < 0)
				return (status);
		}
		else
		{
			kernelDebug(debug_io, "AHCI port %d already idle", count);
		}

		// Next the spec says that we should allocate memory for each
		// implemented port
		status = allocPortMemory(controller, count);
		if (status < 0)
		{
			kernelError(kernel_error, "Couldn't allocate port memory");
			return (status);
		}

		// Clear (write 1) to all implemented bits of port SERR register to
		// clear them
		portRegs->SERR |= AHCI_PXSERR_ALL;
	}

	return (status = 0);
}


static void interruptHandler(void)
{
	// This is the AHCI interrupt handler.  It will be called whenever the
	// disk controller issues its service interrupt, and will simply change a
	// data value to indicate that one has been received.  It's up to the other
	// routines to do something useful with the information.

	void *address = NULL;
	int interruptNum = 0;
	ahciController *controller = NULL;
	int serviced = 0;
	int controllerCount, portCount;

	processorIsrEnter(address);

	// Which interrupt number is active?
	interruptNum = kernelPicGetActive();
	if (interruptNum < 0)
	{
		kernelDebugError("Unknown interrupt");
		goto out;
	}

	kernelInterruptSetCurrent(interruptNum);

	kernelDebug(debug_io, "AHCI interrupt %d", interruptNum);

	// Loop through the controllers to find the one that uses this interrupt
	for (controllerCount = 0; controllerCount < numControllers;
		controllerCount ++)
	{
		controller = &controllers[controllerCount];

		if (controller->interrupt == interruptNum)
		{
			kernelDebug(debug_io, "AHCI controller %d uses interrupt %d",
				controllerCount, interruptNum);

			if (controller->regs->IS)
			{
				kernelDebug(debug_io, "AHCI controller %d interrupt",
					controllerCount);

				// Seems like, at least with some drives/controllers, a short
				// delay is required here before we start processing the
				// interrupt
				kernelCpuSpinMs(1);

				for (portCount = 0; portCount < AHCI_MAX_PORTS; portCount ++)
				{
					if (controller->regs->IS & (1 << portCount))
					{
						kernelDebug(debug_io, "AHCI controller %d port %d "
							"interrupt status=0x%08x", controllerCount,
							portCount, controller->regs->port[portCount].IS);

						// If the controller registered a PhyRdy change, clear
						// it.  Otherwise we can get a storm of interrupts.
						if (controller->regs->port[portCount].IS &
							AHCI_PXIS_PRCS)
						{
							controller->regs->port[portCount].SERR |=
								AHCI_PXSERR_DIAG_N;
						}

						// Record the port interrupt status and clear the bits
						controller->port[portCount].interruptStatus =
							controller->regs->port[portCount].IS;
						controller->regs->port[portCount].IS |=
							(controller->regs->port[portCount].IS &
								AHCI_PXIS_RWCBITS);

						// If a process is waiting for an interrupt from this
						// port, wake it up
						if (controller->port[portCount].waitProcess)
						{
							kernelMultitaskerSetProcessState(
								controller->port[portCount].waitProcess,
								proc_ioready);
							controller->port[portCount].waitProcess = 0;
						}
					}
				}

				// Record the controller interrupt status and clear the bit(s)
				controller->portInterrupts |= controller->regs->IS;
				controller->regs->IS |=	controller->regs->IS;

				serviced = 1;
				break;
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
			kernelDebug(debug_usb, "AHCI interrupt not serviced - chaining");
			processorIsrCall(oldIntHandlers[interruptNum]);
		}
		else
		{
			kernelDebugError("Interrupt not serviced and no saved ISR");
		}
	}

out:
	processorIsrExit(address);
}


static int setupController(ahciController *controller)
{
	// Set up the controller

	int status = 0;
	int count;

	if (!controller->interrupt)
	{
		kernelError(kernel_error, "Controller has no interrupt");
		return (status = ERR_NOTINITIALIZED);
	}

	if (controller->regs->CAP & AHCI_CAP_SAM)
	{
		kernelDebug(debug_io, "AHCI controller only works in native mode");
	}
	else
	{
		kernelDebug(debug_io, "AHCI controller supports legacy mode");

		// Uncomment to disable AHCI (for PATA compatibility-mode testing)
		// return (status = ERR_NOTINITIALIZED);
	}

	// Enable AHCI.
	if (controller->regs->GHC & AHCI_GHC_AE)
		kernelDebug(debug_io, "AHCI native SATA mode already enabled");
	else
		controller->regs->GHC |= AHCI_GHC_AE;

	// If the controller version is >= AHCI 1.2, try to do the BIOS/OS handoff
	if (controller->regs->VS >= AHCI_VERSION_1_2)
	{
		// Does the BIOS claim ownership?
		if (controller->regs->BOHC & AHCI_BOHC_BOS)
		{
			kernelDebug(debug_io, "AHCI performing BIOS/OS handoff");

			// Don't want an SMI
			controller->regs->BOHC &= ~AHCI_BOHC_SOOE;

			// Set the ownership bit
			controller->regs->BOHC |= AHCI_BOHC_OOS;

			// Wait up to 500ms for the OOS bit to be set, and the BOS bit to
			// clear
			for (count = 0; count < 500; count ++)
			{
				if (!(controller->regs->BOHC & AHCI_BOHC_BOS) &&
					(controller->regs->BOHC & AHCI_BOHC_OOS))
				{
					break;
				}

				kernelCpuSpinMs(1);
			}

			if (!(controller->regs->BOHC & AHCI_BOHC_BOS) &&
				(controller->regs->BOHC & AHCI_BOHC_OOS))
			{
				kernelDebug(debug_io, "AHCI BIOS/OS handoff took %dms", count);
			}
			else
			{
				kernelDebugError("BIOS/OS ownership handoff failed");
			}
		}
		else
		{
			kernelDebug(debug_io, "AHCI BIOS does not claim ownership");
		}
	}

	debugAhciCapReg(controller->regs);
	kernelDebug(debug_io, "AHCI VS=%08x (version - %d.%d%d)",
		controller->regs->VS, (controller->regs->VS >> 16),
		((controller->regs->VS >> 8) & 0xFF), (controller->regs->VS & 0xFF));

	// The number of supported ports is zero-based
	kernelDebug(debug_io, "AHCI %d ports supported",
		((controller->regs->CAP & AHCI_CAP_NP) + 1));

	// If staggered spin-up is supported, spin up each of the ports
	if (controller->regs->CAP & AHCI_CAP_SSS)
		spinUpPorts(controller);

	// Initialize the ports
	status = initializePorts(controller);
	if (status < 0)
	{
		kernelError(kernel_error, "Couldn't initialize ports");
		return (status);
	}

	// Disable controller interrupts.
	controller->regs->GHC &= ~AHCI_GHC_IE;

	// Clear any pre-existing interrupt
	if (controller->regs->IS)
	{
		kernelPicEndOfInterrupt(controller->interrupt);

		// Clear global interrupt status (IS) register by writing 1 to all
		// bits
		controller->regs->IS |= 0xFFFFFFFF;
	}

	// Save any existing handler for the interrupt we're hooking

	if (numOldHandlers <= controller->interrupt)
	{
		numOldHandlers = (controller->interrupt + 1);

		oldIntHandlers = kernelRealloc(oldIntHandlers,
			(numOldHandlers * sizeof(void *)));
		if (!oldIntHandlers)
			return (status = ERR_MEMORY);
	}

	if (!oldIntHandlers[controller->interrupt] &&
		(kernelInterruptGetHandler(controller->interrupt) !=
			&interruptHandler))
	{
		oldIntHandlers[controller->interrupt] =
			kernelInterruptGetHandler(controller->interrupt);
	}

	// Register the interrupt handler and turn on interrupts at the system
	// level.
	status = kernelInterruptHook(controller->interrupt, &interruptHandler,
		NULL);
	if (status < 0)
		return (status);

	kernelDebug(debug_io, "AHCI Turn on interrupt %d", controller->interrupt);

	status = kernelPicMask(controller->interrupt, 1);
	if (status < 0)
		return (status);

	// Enable interrupts in the controller
	controller->regs->GHC |= AHCI_GHC_IE;

	return (status = 0);
}


static inline const char *devType(unsigned sig)
{
	switch (sig)
	{
		case SATA_SIG_ATA:
			return ("ATA");
		case SATA_SIG_PM:
			return ("port multiplier");
		case SATA_SIG_EMB:
			return ("enclosure management bridge");
		case SATA_SIG_ATAPI:
			return ("ATAPI");
		default:
			return ("unknown");
	}
}


static unsigned detectAndEnableDisk(ahciController *controller, int portNum)
{
	ahciPortRegs *portRegs = NULL;

	portRegs = &controller->regs->port[portNum];

	// Perform device detection

	kernelDebug(debug_io, "AHCI port %d SSTS=%08x", portNum, portRegs->SSTS);
	kernelDebug(debug_io, "AHCI port %d SIG=%08x", portNum, portRegs->SIG);

	// Is there a device here?
	if (((portRegs->SSTS & AHCI_PXSSTS_DET) != 0x0003) ||
		((portRegs->SSTS & AHCI_PXSCTL_IPM) != 0x0100))
	{
		kernelDebug(debug_io, "AHCI port %d no device or not active", portNum);
		return (0);
	}

	kernelDebug(debug_io, "AHCI port %d SATA %s device detected", portNum,
		devType(portRegs->SIG));

	if ((portRegs->SIG == SATA_SIG_ATA) || (portRegs->SIG == SATA_SIG_ATAPI))
	{
		// Clear port interrupt status (IS) register by writing 1 to all
		// writable bits
		portRegs->IS |= AHCI_PXIS_RWCBITS;

		// Enable port interrupts
		portRegs->IE = AHCI_PXIE_ALL;

		// Tell the port to start receiving FISes
		if (startStopPortReceives(controller, portNum, 1) < 0)
			return (0);

		// Tell the port to start processing the command list
		if (startStopPortCommands(controller, portNum, 1) < 0)
			return (0);

		// Spec says these must be clear before we start the port.  If they're
		// not, at this point, there's probably something wrong.
		if (portRegs->TFD & (AHCI_PXTFD_STS_BSY | AHCI_PXTFD_STS_DRQ))
		{
			kernelDebug(debug_io, "AHCI port %d BSY or DRQ set - skipping "
				"device detection", portNum);
			return (0);
		}
	}

	return (portRegs->SIG);
}


static int findCommandSlot(ahciController *controller, int portNum)
{
	// Find a free command slot.

	int slotNum = -1;
	ahciPortRegs *portRegs = NULL;
	int commandSlots = 0;
	int count = 0;

	portRegs = &controller->regs->port[portNum];

	// How many command slots are implemented?
	commandSlots = (((controller->regs->CAP & AHCI_CAP_NCS) >> 8) + 1);

	kernelDebug(debug_io, "AHCI port %d has %d command slots", portNum,
		commandSlots);

	// Search for a slot for which the corresponding SACT and CI register bits
	// are clear
	for (count = 0; count < commandSlots; count ++)
	{
		if (!((portRegs->SACT | portRegs->CI) & (1 << count)))
		{
			slotNum = count;
			break;
		}
	}

	if (slotNum < 0)
	{
		kernelError(kernel_error, "No free command slot for port %d", portNum);
		return (slotNum = ERR_NOFREE);
	}

	kernelDebug(debug_io, "AHCI port %d chose command slot %d", portNum,
		slotNum);

	return (slotNum);
}


static unsigned allocCommandTable(int numPrds, unsigned *commandTablePhysical,
	ahciCommandTable **commandTable)
{
	// Allocate a command table structure

	unsigned commandTableSize = 0;
	kernelIoMemory ioMem;

	commandTableSize = (sizeof(ahciCommandTable) + (numPrds * sizeof(ahciPrd)));

	if (kernelMemoryGetIo(commandTableSize, DISK_CACHE_ALIGN, &ioMem) < 0)
	{
		kernelError(kernel_error, "Couldn't allocate command table memory");
		return (commandTableSize = 0);
	}

	*commandTable = ioMem.virtual;
	*commandTablePhysical = ioMem.physical;

	// Success
	return (commandTableSize);
}


static unsigned makeCommandFis(ahciCommandTable *cmdTable,
	unsigned short features, unsigned short sectorCount, unsigned short lbaLow,
	unsigned short lbaMid, unsigned short lbaHigh, unsigned char dev,
	unsigned char ataCommand)
{
	unsigned fisLen = 0;
	sataFisRegH2D *fis = (sataFisRegH2D *)(cmdTable->commandFis);

	fis->fields.fisType = SATA_FIS_REGH2D;
	fis->fields.isCommand = 1;
	fis->fields.command = ataCommand;
	fis->fields.features7_0 = (features & 0xFF);

	fis->fields.lba7_0 = (lbaLow & 0xFF);
	fis->fields.lba15_8 = (lbaLow >> 8);
	fis->fields.lba23_16 = (lbaMid & 0xFF);
	fis->fields.device = dev;

	fis->fields.lba31_24 = (lbaMid >> 8);
	fis->fields.lba39_32 = (lbaHigh & 0xFF);
	fis->fields.lba47_40 = (lbaHigh >> 8);
	fis->fields.features15_8 = (features >> 8);

	fis->fields.count7_0 = (sectorCount & 0xFF);
	fis->fields.count15_8 = (sectorCount >> 8);

	fisLen = sizeof(sataFisRegH2D);

	return (fisLen);
}


static int setupPrds(ahciPrd *prd, int numPrds, unsigned char *buffer,
	unsigned bufferLen)
{
	// Set up the array of PRDs.  It's the caller's responsibility to ensure
	// that enough of them are allocated.

	int status = 0;
	unsigned bufferPhysical = NULL;
	unsigned dataLen = 0;
	int count;

	// Get the physical address of the buffer
	bufferPhysical = (unsigned) kernelPageGetPhysical(
		(((unsigned) buffer < KERNEL_VIRTUAL_ADDRESS)?
			kernelCurrentProcess->processId : KERNELPROCID), buffer);

	if (!bufferPhysical)
	{
		kernelError(kernel_error, "Couldn't get buffer physical address");
		return (status = ERR_MEMORY);
	}

	if (bufferPhysical & 1)
	{
		kernelError(kernel_error, "Buffer physical address is not "
			"dword-aligned");
		return (status = ERR_ALIGN);
	}

	// Set up the PRDs
	for (count = 0; count < numPrds; count ++)
	{
		dataLen = min(bufferLen, AHCI_PRD_MAXDATA);

		prd[count].physAddr = bufferPhysical;
		prd[count].intrCount = (dataLen - 1);

		bufferPhysical += dataLen;
		bufferLen -= dataLen;
	}

	return (status = 0);
}


static int errorRecovery(ahciController *controller, int portNum)
{
	// All of our software error recovery is handled here

	int status = 0;
	ahciPortRegs *portRegs = &controller->regs->port[portNum];
	unsigned interruptStatus = controller->port[portNum].interruptStatus;
	unsigned char taskFileError = 0;
	int recovered = 0;
	char errorString[256];

	// Fatal error?
	if (interruptStatus &
		(AHCI_PXIS_TFES | AHCI_PXIS_HBFS | AHCI_PXIS_HBDS | AHCI_PXIS_IFS))
	{
		// Device error
		if (interruptStatus & AHCI_PXIS_TFES)
		{
			// An error message should be indicated in the 'task file' error
			// register

			taskFileError = ((portRegs->TFD >> 8) & 0xFF);

			ataError2String(taskFileError, errorString);
			kernelDebugError("Device error 0x%02x: %s", taskFileError,
				errorString);

			// Is the device in a stable state?
			if (!(portRegs->TFD & AHCI_PXTFD_STS_ERR) ||
				(portRegs->TFD & (AHCI_PXTFD_STS_BSY || AHCI_PXTFD_STS_DRQ)))
			{
				kernelDebug(debug_io, "AHCI device on port %d not stable - "
					"may need COMRESET", portNum);
			}
		}

		if (interruptStatus & AHCI_PXIS_HBFS)
		{
			kernelError(kernel_error, "Host bus fatal error");
		}

		if (interruptStatus & AHCI_PXIS_HBDS)
		{
			kernelError(kernel_error, "Host bus data error");
		}

		if (interruptStatus & AHCI_PXIS_IFS)
		{
			kernelError(kernel_error, "Interface fatal error");
		}

		// Try to restart port command processing
		startStopPortCommands(controller, portNum, 0);
		if (startStopPortCommands(controller, portNum, 1) >= 0)
			recovered = 1;
	}
	else
	{
		// Non-fatal error
		if (interruptStatus & AHCI_PXIS_INFS)
		{
			kernelError(kernel_error, "Interface non-fatal error");
		}

		if (interruptStatus & AHCI_PXIS_OFS)
		{
			kernelError(kernel_error, "Overflow error");
		}

		if (interruptStatus & AHCI_PXIS_IPMS)
		{
			kernelError(kernel_error, "Incorrect port multiplier error");
		}

		recovered = 1;
	}

	if (recovered)
		return (status = 0);
	else
		return (status = ERR_NOTIMPLEMENTED);
}


static int issueCommand(ahciController *controller, int portNum,
	unsigned short feature, unsigned short sectorCount, unsigned short lbaLow,
	unsigned short lbaMid, unsigned short lbaHigh, unsigned char dev,
	unsigned char ataCommand, unsigned char *atapiPacket,
	unsigned char *buffer, unsigned bufferLen, int write, unsigned timeout)
{
	// Issue a command on the requested port

	int status = 0;
	ahciPortRegs *portRegs = &controller->regs->port[portNum];
	int slotNum = -1;
	unsigned numPrds = 0;
	unsigned commandTableSize = 0;
	unsigned commandTablePhysical = NULL;
	ahciCommandTable *commandTable = NULL;
	unsigned fisLen = 0;
	ahciCommandHeader *commandHeader = NULL;
	uquad_t startTime = 0;
	uquad_t currTime = 0;
	int retries;

	if (!timeout)
		timeout = 1000;

	// Find a free command slot.
	slotNum = findCommandSlot(controller, portNum);
	if (slotNum < 0)
	{
		kernelError(kernel_error, "No free command slot for port %d", portNum);
		return (status = ERR_NOFREE);
	}

	kernelDebug(debug_io, "AHCI port %d sending command using command slot "
		"%d", portNum, slotNum);

	// If it's a data command, we need to construct a set of PRDs (Physical
	// Region Descriptors) to point to the buffer.  Calculate how many we're
	// going to need
	if (buffer)
		numPrds = ((bufferLen + (AHCI_PRD_MAXDATA - 1)) / AHCI_PRD_MAXDATA);

	kernelDebug(debug_io, "AHCI port %d transfer requires %d PRDs", portNum,
		numPrds);

	// Allocate a command table structure
	commandTableSize = allocCommandTable(numPrds, &commandTablePhysical,
		&commandTable);
	if (!commandTableSize)
	{
		status = ERR_MEMORY;
		goto out;
	}

	for (retries = 0; retries < 3; retries ++)
	{
		if (retries)
			memset((void *) commandTable, 0, commandTableSize);

		// Set up the command FIS in the command table
		fisLen = makeCommandFis(commandTable, feature, sectorCount, lbaLow,
			lbaMid, lbaHigh, dev, ataCommand);
		if (!fisLen)
		{
			// Don't retry
			status = ERR_NOTIMPLEMENTED;
			goto out;
		}

		if (atapiPacket)
		{
			// Copy the ATAPI packet into the command table
			memcpy((unsigned char *) commandTable->atapiCommand,
				atapiPacket, 12);
		}

		if (buffer)
		{
			status = setupPrds(commandTable->prd, numPrds, buffer, bufferLen);
			if (status < 0)
			{
				// Don't retry
				goto out;
			}
		}

		// Set up the command header
		commandHeader =
			&controller->port[portNum].commandList->command[slotNum];
		memset((void *) commandHeader, 0, sizeof(ahciCommandHeader));
		commandHeader->fisLen = ((fisLen >> 2) & 0x1F);
		commandHeader->atapi = (atapiPacket? 1 : 0);
		commandHeader->write = (write & 1);
		commandHeader->prdDescTableEnts = numPrds;
		commandHeader->cmdTablePhysAddr = commandTablePhysical;

		// Tell the controller to process the command
		kernelDebug(debug_io, "AHCI port %d issue command", portNum);
		portRegs->CI |= (1 << slotNum);

		startTime = currTime = kernelCpuGetMs();

	wait:
		while (!(controller->portInterrupts & (1 << portNum)))
		{
			currTime = kernelCpuGetMs();
			if (currTime > (startTime + timeout))
				break;

			// Record that we are waiting for an interrupt from this port, and
			// go into a waiting state.  When the interrupt comes, the
			// interrupt handler will change our state to 'IO ready' which will
			// give us high priority for a wakeup
			controller->port[portNum].waitProcess =
				kernelMultitaskerGetCurrentProcessId();
			kernelMultitaskerWait(timeout - (currTime - startTime));
		}

		if (!(controller->portInterrupts & (1 << portNum)))
		{
			// No interrupt -- timed out
			kernelError(kernel_error, "Command failed - timeout");
			// Don't retry
			status = ERR_TIMEOUT;
			goto out;
		}

		// Clear the port interrupt bit in our controller structure
		controller->portInterrupts &= ~(1 << portNum);

		kernelDebug(debug_io, "AHCI port %d interrupt status=0x%08x",
			portNum, controller->port[portNum].interruptStatus);

		if (controller->port[portNum].interruptStatus & AHCI_PXIS_ERROR)
		{
			status = ERR_IO;
			if ((errorRecovery(controller, portNum) < 0) || (retries >= 2))
			{
				// Dont't retry
				break;
			}
			else
			{
				kernelDebug(debug_io, "AHCI port %d recoverable error - "
					"retrying (attempt %d)", portNum, (retries + 2));
			}
		}
		else
		{
			// We got an interrupt, but was it the one we were hoping for?
			if (buffer &&
				(((ataCommand == ATA_ATAPIPACKET) &&
					(!(controller->port[portNum].interruptStatus &
						AHCI_PXIS_PSS) ||
					!(controller->port[portNum].interruptStatus &
						AHCI_PXIS_DHRS))) ||
				((ataCommand != ATA_ATAPIPACKET) &&
					(!(controller->port[portNum].interruptStatus &
						AHCI_PXIS_PSS) &&
					!(controller->port[portNum].interruptStatus &
						AHCI_PXIS_DHRS)))))
			{
				kernelDebug(debug_io, "AHCI port %d wait for a different "
					"interrupt", portNum);
				goto wait;
			}

			kernelDebug(debug_io, "AHCI command complete - %ums",
				(unsigned)(currTime - startTime));
			// Finished
			status = 0;
			break;
		}
	}

	if (status < 0)
		kernelError(kernel_error, "Command failed for disk %d:%d",
			controller->num, portNum);

out:
	if (commandTable)
		kernelPageUnmap(KERNELPROCID, (void *) commandTable, commandTableSize);
	if (commandTablePhysical)
		kernelMemoryReleasePhysical(commandTablePhysical);

	controller->port[portNum].interruptStatus = 0;

	return (status);
}


static int setTransferMode(ahciController *controller, int portNum,
	ataDmaMode *mode, ataIdentifyData *identData)
{
	// Try to set the transfer mode (e.g. DMA, UDMA).

	int status = 0;

	kernelDebug(debug_io, "AHCI disk on port %d set transfer mode %s (%02x)",
		portNum, mode->name, mode->val);

	status = issueCommand(controller, portNum, 0x03, mode->val, 0, 0, 0, 0,
		ATA_SETFEATURES, NULL, NULL, 0,	0, 0 /* default timeout */);
	if (status < 0)
		return (status);

	// Now we do an "identify device" to find out if we were successful
	status = issueCommand(controller, portNum, 0, 0, 0, 0, 0, 0, ATA_IDENTIFY,
		NULL, (unsigned char *) identData, sizeof(ataIdentifyData),
		0 /* read */, 0 /* default timeout */);
	if (status < 0)
		// Couldn't verify.
		return (status);

	// Verify that the requested mode has been set
	if (identData->word[mode->identWord] & mode->enabledMask)
	{
		kernelDebug(debug_io, "AHCI disk on port %d successfully set transfer "
			"mode %s", portNum, mode->name);
		return (status = 0);
	}
	else
	{
		kernelDebugError("Failed to set transfer mode %s for disk on port %d",
			mode->name, portNum);
		return (status = ERR_INVALID);
	}
}


static int detectDisks(kernelDriver *driver, kernelDevice *controllerDevice,
	ahciController *controller)
{
	int status = 0;
	unsigned sigs[AHCI_MAX_PORTS];
	ataIdentifyData identData;
	int diskNum = 0;
	kernelPhysicalDisk *physicalDisk = NULL;
	kernelDevice *diskDevice = NULL;
	ataDmaMode *dmaModes = kernelAtaGetDmaModes();
	ataFeature *features = kernelAtaGetFeatures();
	char value[80];
	int portNum, count;

	kernelDebug(debug_io, "AHCI detect disks");

	memset(sigs, 0, sizeof(sigs));

	// Loop through the ports.  For each one that's implemented, see
	// whether there's a device attached.  If so, enable the port/device.
	for (portNum = 0; portNum < AHCI_MAX_PORTS; portNum ++)
	{
		// Port implemented?
		if (controller->regs->PI & (1 << portNum))
			sigs[portNum] = detectAndEnableDisk(controller, portNum);
	}

	// Loop through the ports one more time.  For each one that has a supported
	// device, get the device information and create the disk/device structures
	// in the kernel.
	for (portNum = 0; portNum < AHCI_MAX_PORTS; portNum ++)
	{
		if (!sigs[portNum])
			// Nothing there
			continue;

		if ((sigs[portNum] != SATA_SIG_ATA) &&
			(sigs[portNum] != SATA_SIG_ATAPI))
		{
			// Not yet supported
			continue;
		}

		kernelDebug(debug_io, "AHCI identify disk on port %d", portNum);

		memset(&identData, 0, sizeof(ataIdentifyData));

		if (sizeof(ataIdentifyData) != 512)
			kernelDebugError("ATA identify structure size is %d, not 512",
				sizeof(ataIdentifyData));

		// Issue an 'identify' command
		if (sigs[portNum] == SATA_SIG_ATAPI)
		{
			status = issueCommand(controller, portNum, 0, 0, 0, 0, 0, 0,
				ATA_ATAPIIDENTIFY, NULL, (unsigned char *) &identData,
				sizeof(ataIdentifyData), 0 /* read */,
				0 /* default timeout */);
		}
		else
		{
			status = issueCommand(controller, portNum, 0, 0, 0, 0, 0, 0,
				ATA_IDENTIFY, NULL, (unsigned char *) &identData,
				sizeof(ataIdentifyData), 0 /* read */,
				0 /* default timeout */);
		}

		if (status < 0)
		{
			kernelError(kernel_error, "Identify device command failed for "
				"port %d", portNum);
			continue;
		}

		// Allocate memory for the disk structure
		controller->disk[portNum] = kernelMalloc(sizeof(ahciDisk));
		if (!controller->disk[portNum])
			continue;

		controller->disk[portNum]->portNum = portNum;

		diskNum = ((controller->num << 8) | portNum);

		physicalDisk = &DISK(diskNum)->physical;
		physicalDisk->description = "Unknown SATA disk";
		physicalDisk->deviceNumber = diskNum;
		physicalDisk->driver = driver;

		if (!(identData.field.generalConfig & 0x8000))
		{
			// This is an ATA hard disk device
			kernelLog("AHCI: Disk %d:%d is an ATA hard disk", controller->num,
				portNum);

			physicalDisk->description = "SATA hard disk";
			physicalDisk->type =
				(DISKTYPE_PHYSICAL | DISKTYPE_FIXED | DISKTYPE_SATADISK);
			physicalDisk->flags = DISKFLAG_MOTORON;

			// Get the geometry

			// Get the mandatory number of sectors field
			physicalDisk->numSectors = identData.field.totalSectors;

			// If the 64-bit location contains something larger, use that
			// instead
			if (identData.field.maxLba48 &&
				(identData.field.maxLba48 < 0x0000FFFFFFFFFFFFULL))
			{
				physicalDisk->numSectors = identData.field.maxLba48;
			}

			// Try to get the number of cylinders, heads, and sectors per
			// cylinder from the 'identify device' info
			physicalDisk->cylinders = identData.field.cylinders;
			physicalDisk->heads = identData.field.heads;
			physicalDisk->sectorsPerCylinder = identData.field.sectsPerCyl;
			// Default sector size is 512.  Don't know how to figure it out
			// short of trusting the BIOS values.
			physicalDisk->sectorSize = 512;

			// The values above don't have to be set.  If they're not, we'll
			// conjure some.
			if (!physicalDisk->heads ||	!physicalDisk->sectorsPerCylinder)
			{
				physicalDisk->heads = 255;
				physicalDisk->sectorsPerCylinder = 63;
			}

			// Make sure C*H*S is the same as the number of sectors, and if
			// not, adjust the cylinder number accordingly.
			if ((physicalDisk->cylinders * physicalDisk->heads *
					physicalDisk->sectorsPerCylinder) !=
				physicalDisk->numSectors)
			{
				kernelDebug(debug_io, "AHCI disk on port %d number of "
					"cylinders manual calculation - was %u", portNum,
					physicalDisk->cylinders);

				physicalDisk->cylinders = (physicalDisk->numSectors /
					(physicalDisk->heads * physicalDisk->sectorsPerCylinder));

				kernelDebug(debug_io, "AHCI disk on port %d number of "
					"cylinders manual calculation - now %u", portNum,
					physicalDisk->cylinders);
			}
		}
		else if ((identData.field.generalConfig & 0xC000) == 0x8000)
		{
			// This is an ATAPI device (such as a CD-ROM)
			kernelLog("AHCI: Disk %d:%d is an ATAPI CD/DVD", controller->num,
				portNum);

			physicalDisk->description = "SATA CD/DVD";
			physicalDisk->type = DISKTYPE_PHYSICAL;
			physicalDisk->driver = driver;

			// Removable?
			if (identData.field.generalConfig & 0x0080)
				physicalDisk->type |= DISKTYPE_REMOVABLE;
			else
				physicalDisk->type |= DISKTYPE_FIXED;

			// Device type: Bits 12-8 of word 0 should indicate 0x05 for
			// CDROM, but we will just warn if it isn't for now
			physicalDisk->type |= DISKTYPE_SATACDROM;
			if (((identData.field.generalConfig & 0x1F00) >> 8) != 0x05)
				kernelError(kernel_warn, "ATAPI device type may not be "
					"supported");

			if (identData.field.generalConfig & 0x0003)
				kernelError(kernel_warn, "ATAPI packet size not 12");

			// Return some information we know from our device info command
			physicalDisk->cylinders = identData.field.cylinders;
			physicalDisk->heads = identData.field.heads;
			physicalDisk->sectorsPerCylinder = identData.field.sectsPerCyl;
			physicalDisk->numSectors = 0xFFFFFFFF;
			physicalDisk->sectorSize = ATAPI_SECTORSIZE;
		}
		else
		{
			kernelDebugError("Disk %d:%d is unknown (0x%04x)", controller->num,
				portNum, identData.field.generalConfig);
			continue;
		}

		if (!physicalDisk->sectorSize)
			// Lots of things divide by this
			physicalDisk->sectorSize = 512;

		kernelDebug(debug_io, "AHCI disk on port %d cylinders=%u heads=%u "
			"sectors=%u", portNum, physicalDisk->cylinders, physicalDisk->heads,
			physicalDisk->sectorsPerCylinder);

		// Get the model string
		for (count = 0; count < (min(DISK_MAX_MODELLENGTH, 40) / 2); count ++)
		{
			((unsigned short *) physicalDisk->model)[count] =
				processorSwap16(identData.field.modelNum[count]);
		}
		physicalDisk->model[DISK_MAX_MODELLENGTH - 1] = '\0';
		for (count = (DISK_MAX_MODELLENGTH - 2);
			((count >= 0) && (physicalDisk->model[count] == ' ')); count --)
		{
			physicalDisk->model[count] = '\0';
		}

		kernelLog("AHCI Disk %d:%d model \"%s\"", controller->num, portNum,
			physicalDisk->model);

		// Allocate memory for the kernel device
		diskDevice = kernelMalloc(sizeof(kernelDevice));
		if (!diskDevice)
			continue;

		diskDevice->device.class = kernelDeviceGetClass(DEVICECLASS_DISK);
		diskDevice->device.subClass =
			kernelDeviceGetClass(DEVICESUBCLASS_DISK_SATA);
		diskDevice->driver = driver;
		diskDevice->data = (void *) physicalDisk;

		// Register the disk
		status = kernelDiskRegisterDevice(diskDevice);
		if (status < 0)
			continue;

		// Add the kernel device
		status = kernelDeviceAdd(controllerDevice, diskDevice);
		if (status < 0)
			continue;

		// Log the ATA/ATAPI standard level
		if (!identData.field.majorVersion ||
			(identData.field.majorVersion == 0xFFFF))
		{
			kernelLog("AHCI: Disk %d:%d no ATA/ATAPI version reported",
				controller->num, portNum);
		}
		else
		{
			for (count = 14; count >= 3; count --)
			{
				if ((identData.field.majorVersion >> count) & 1)
				{
					kernelLog("AHCI: Disk %d:%d supports ATA/ATAPI %d",
						controller->num, portNum, count);
					break;
				}
			}
		}

		// Now do some general feature detection (common to hard disks
		// and CD-ROMs

		// Record the current multi-sector transfer mode, if any
		physicalDisk->multiSectors = 1;
		if ((identData.field.multiSector & 0x01FF) > 0x101)
		{
			DISK(diskNum)->featureFlags |= ATA_FEATURE_MULTI;
			physicalDisk->multiSectors = (identData.field.multiSector & 0xFF);
		}

		kernelDebug(debug_io, "AHCI disk on port %d is %sin multi-mode (%d)",
			portNum, ((DISK(diskNum)->featureFlags & ATA_FEATURE_MULTI)?
			"" : "not "), physicalDisk->multiSectors);

		// See whether the disk supports various DMA transfer modes.
		//
		// word 49:	bit 8 indicates DMA supported
		// word 53:	bit 2 indicates word 88 is valid
		// word 88:	bits 0-6 indicate supported modes
		//			bits 8-14 indicate selected mode
		// word 93:	bit 13 indicates 80-pin cable for UDMA3+
		//
		if (identData.field.capabilities1 & 0x0100)
		{
			for (count = 0; dmaModes[count].name; count ++)
			{
				if ((dmaModes[count].identWord == 88) &&
					!(identData.field.validFields & 0x0004))
				{
					// Values are invalid
					continue;
				}

				if (identData.word[dmaModes[count].identWord] &
					dmaModes[count].suppMask)
				{
					kernelDebug(debug_io, "AHCI disk on port %d supports %s",
						portNum, dmaModes[count].name);

					if (!(identData.word[dmaModes[count].identWord] &
						dmaModes[count].enabledMask))
					{
						// Don't attempt to use modes UDMA3 and up if
						// there's not an 80-pin connector
						if (!(identData.field.hardResetResult & 0x2000) &&
							(dmaModes[count].identWord == 88) &&
							(dmaModes[count].suppMask > 0x04))
						{
							kernelDebug(debug_io, "AHCI skip mode, no "
								"80-pin cable detected");
							continue;
						}

						// If this is not a CD-ROM, and the mode is not
						// enabled, try to enable it.
						if (!(DISK(diskNum)->physical.type &
							DISKTYPE_SATACDROM))
						{
							if (setTransferMode(controller, portNum,
								&dmaModes[count], &identData) < 0)
							{
								continue;
							}

							// TODO: Test DMA operation
							continue;
						}
					}
					else
					{
						kernelDebug(debug_io, "AHCI disk on port %d mode %s "
							"already enabled", portNum, dmaModes[count].name);
					}

					DISK(diskNum)->featureFlags |= dmaModes[count].featureFlag;
					DISK(diskNum)->dmaMode = dmaModes[count].name;
					break;
				}
			}
		}

		kernelLog("AHCI: Disk %d:%d in %s mode %s", controller->num, portNum,
			((DISK(diskNum)->featureFlags & ATA_FEATURE_DMA)? "DMA" : "PIO"),
			((DISK(diskNum)->featureFlags & ATA_FEATURE_DMA)?
				DISK(diskNum)->dmaMode : ""));

		// Misc features
		for (count = 0; features[count].name; count ++)
		{
			if (identData.word[features[count].identWord] &
				features[count].suppMask)
			{
				// Supported.
				kernelDebug(debug_io, "AHCI disk on port %d supports %s",
					portNum, features[count].name);

				// Do we have to enable it?
				if (features[count].featureCode &&
					!(identData.word[features[count].enabledWord] &
						features[count].enabledMask))
				{
					// TODO: enable
					continue;
				}
				else
				{
					kernelDebug(debug_io, "AHCI disk on port %d feature "
						"already enabled", portNum);
				}

				DISK(diskNum)->featureFlags |= features[count].featureFlag;
			}
		}

		// Initialize the variable list for attributes of the disk.
		status = kernelVariableListCreate(&diskDevice->device.attrs);
		if (status >= 0)
		{
			kernelVariableListSet(&diskDevice->device.attrs,
				DEVICEATTRNAME_MODEL, (char *) DISK(diskNum)->physical.model);

			if (DISK(diskNum)->featureFlags & ATA_FEATURE_MULTI)
			{
				sprintf(value, "%d", DISK(diskNum)->physical.multiSectors);
				kernelVariableListSet(&diskDevice->device.attrs,
					"disk.multisectors", value);
			}

			value[0] = '\0';
			if (DISK(diskNum)->featureFlags & ATA_FEATURE_DMA)
				strcat(value, DISK(diskNum)->dmaMode);
			else
				strcat(value, "PIO");

			if (DISK(diskNum)->featureFlags & ATA_FEATURE_SMART)
				strcat(value, ",SMART");

			if (DISK(diskNum)->featureFlags & ATA_FEATURE_RCACHE)
				strcat(value, ",rcache");

			if (DISK(diskNum)->featureFlags & ATA_FEATURE_MEDSTAT)
				strcat(value, ",medstat");

			if (DISK(diskNum)->featureFlags & ATA_FEATURE_WCACHE)
				strcat(value, ",wcache");

			if (DISK(diskNum)->featureFlags & ATA_FEATURE_48BIT)
				strcat(value, ",48-bit");

			kernelVariableListSet(&diskDevice->device.attrs, "disk.features",
				value);
		}
	}

	return (status = 0);
}


static int driverDetect(void *parent __attribute__((unused)),
	kernelDriver *driver)
{
	// This routine is used to detect and initialize each device, as well as
	// registering each one with any higher-level interfaces.  Also does
	// general driver initialization.

	int status = 0;
	kernelDevice *controllerDevices = NULL;
	char value[80];
	int count;

	kernelLog("AHCI: Searching for controllers");

	// Reset controller count
	numControllers = 0;

	// See whether we have PCI controller(s)
	status = detectPciControllers();
	if (status < 0)
		kernelDebugError("PCI controller detection error");

	if (numControllers <= 0)
	{
		kernelDebug(debug_io, "AHCI no controllers detected.");
		return (status = 0);
	}

	kernelLog("AHCI: Detected %d controller%s", numControllers,
		((numControllers > 1)? "s" : ""));

	controllerDevices = kernelMalloc(numControllers * sizeof(kernelDevice));
	if (!controllerDevices)
		return (status = ERR_MEMORY);

	// Register each controller device with the kernel
	for (count = 0; count < numControllers; count ++)
	{
		// Create a device for it in the kernel.
		controllerDevices[count].device.class =
			kernelDeviceGetClass(DEVICECLASS_DISKCTRL);
		controllerDevices[count].device.subClass =
			kernelDeviceGetClass(DEVICESUBCLASS_DISKCTRL_SATA);

		// Initialize the variable list for attributes of the controller
		status = kernelVariableListCreate(
			&controllerDevices[count].device.attrs);
		if (status >= 0)
		{
			sprintf(value, "%d", controllers[count].interrupt);
			kernelVariableListSet(&controllerDevices[count].device.attrs,
				"controller.interrupt", value);
		}

		// Add the kernel device
		kernelDeviceAdd(controllers[count].busTarget.bus->dev,
			&controllerDevices[count]);
	}

	// Loop through the detected controllers, and attempt to initialize
	// them.
	for (count = 0; count < numControllers; count ++)
	{
		status = setupController(&controllers[count]);
		if (status < 0)
		{
			kernelDebugError("Controller setup error");

			if (!(controllers[count].regs->CAP & AHCI_CAP_SAM))
				// Try to set it back to legacy mode
				controllers[count].regs->GHC &= ~AHCI_GHC_AE;

			continue;
		}

		kernelLog("AHCI: Controller %d enabled in native SATA mode", count);

		// Claim the controller device in the list of PCI targets.
		kernelBusDeviceClaim((kernelBusTarget *)
			&controllers[count].busTarget, driver);

		// Detect disks
		status = detectDisks(driver, &controllerDevices[count],
			&controllers[count]);
		if (status < 0)
		{
			/* Nothing to do here on error, at the moment. */
		}
	}

	return (status = 0);
}


static int sendAtapiPacket(ahciController *controller, ahciDisk *dsk,
	unsigned char *packet, unsigned char *buffer,
	unsigned byteCount)
{
	int status = 0;

	kernelDebug(debug_io, "AHCI disk on port %d sending ATAPI packet 0x%02x %s",
		dsk->portNum, packet[0], atapiCommand2String(packet[0]));

	status = issueCommand(controller, dsk->portNum, 0, 0, 0,
		(byteCount & 0xFF), ((byteCount >> 8) & 0xFF), 0, ATA_ATAPIPACKET,
		packet, buffer,	byteCount, 0 /* read */, 10000 /* timeout 10s */);
	if (status < 0)
		return (status);

	kernelDebug(debug_io, "AHCI disk on port %d sent ATAPI packet",
		dsk->portNum);

	return (status);
}


static int atapiStartStop(ahciController *controller, ahciDisk *dsk, int start)
{
	// Start or stop an ATAPI device

	int status = 0;
	atapiCapacityData capacityData;
	atapiTocData tocData;

	if (start)
	{
		// If we know the disk door is open, try to close it
		if (dsk->physical.flags & DISKFLAG_DOOROPEN)
		{
			kernelDebug(debug_io, "AHCI disk on port %d ATAPI close",
				dsk->portNum);

			sendAtapiPacket(controller, dsk, ATAPI_PACKET_CLOSE, NULL, 0);
		}

		// Well, okay, assume this.
		dsk->physical.flags &= ~DISKFLAG_DOOROPEN;

		kernelDebug(debug_io, "AHCI disk on port %d ATAPI start",
			dsk->portNum);

		status =
			sendAtapiPacket(controller, dsk, ATAPI_PACKET_START, NULL, 0);
		if (status < 0)
			return (status);

		kernelDebug(debug_io, "AHCI disk on port %d ATAPI read capacity",
			dsk->portNum);

		status = sendAtapiPacket(controller, dsk, ATAPI_PACKET_READCAPACITY,
			(unsigned char *) &capacityData, sizeof(atapiCapacityData));
		if (status < 0)
			return (status);

		// The number of sectors
		dsk->physical.numSectors =
			processorSwap32(capacityData.blockNumber);

		// The sector size
		dsk->physical.sectorSize =
			processorSwap32(capacityData.blockLength);

		// If there's no disk, the number of sectors will be illegal.	Set
		// to the maximum value and quit
		if (!dsk->physical.numSectors ||
			(dsk->physical.numSectors == 0xFFFFFFFF))
		{
			dsk->physical.numSectors = 0xFFFFFFFF;
			dsk->physical.sectorSize = ATAPI_SECTORSIZE;
			kernelError(kernel_error, "No media in drive %s",
				dsk->physical.name);
			return (status = ERR_NOMEDIA);
		}

		dsk->physical.logical[0].numSectors = dsk->physical.numSectors;

		// Read the TOC (Table Of Contents)
		kernelDebug(debug_io, "AHCI disk on port %d ATAPI read TOC",
			dsk->portNum);

		status = sendAtapiPacket(controller, dsk, ATAPI_PACKET_READTOC,
			(unsigned char *) &tocData, sizeof(atapiTocData));
		if (status < 0)
			return (status);

		// Read the LBA of the start of the last session
		dsk->physical.lastSession =
			processorSwap32(tocData.lastSessionLba);

		dsk->physical.flags |= DISKFLAG_MOTORON;
	}
	else
	{
		kernelDebug(debug_io, "AHCI disk on port %d ATAPI stop",
			dsk->portNum);

		status =
			sendAtapiPacket(controller, dsk, ATAPI_PACKET_STOP, NULL, 0);

		dsk->physical.flags &= ~DISKFLAG_MOTORON;
	}

	return (status);
}


static int readWriteAtapi(ahciController *controller, ahciDisk *dsk,
	uquad_t logicalSector, uquad_t numSectors, void *buffer,
	int read __attribute__((unused)))
{
	int status = 0;
	unsigned atapiNumBytes = 0;

	// If it's not started, we start it
	if (!(dsk->physical.flags & DISKFLAG_MOTORON))
	{
		kernelDebug(debug_io, "AHCI disk on port %d start ATAPI",
			dsk->portNum);

		status = atapiStartStop(controller, dsk, 1);
		if (status < 0)
			return (status);
	}
	else
	{
		// Just kickstart the device
		kernelDebug(debug_io, "AHCI disk on port %d kickstart ATAPI device",
			dsk->portNum);

		status = sendAtapiPacket(controller, dsk, ATAPI_PACKET_START, NULL, 0);
		if (status < 0)
		{
			// Oops, didn't work -- try a full startup
			status = atapiStartStop(controller, dsk, 1);
			if (status < 0)
				return (status);
		}
	}

	atapiNumBytes = (numSectors * dsk->physical.sectorSize);

	status = sendAtapiPacket(controller, dsk, ((unsigned char[])
		{ ATAPI_READ12, 0,
			(unsigned char)((logicalSector >> 24) & 0xFF),
			(unsigned char)((logicalSector >> 16) & 0xFF),
			(unsigned char)((logicalSector >> 8) & 0xFF),
			(unsigned char)(logicalSector & 0xFF),
			(unsigned char)((numSectors >> 24) & 0xFF),
			(unsigned char)((numSectors >> 16) & 0xFF),
			(unsigned char)((numSectors >> 8) & 0xFF),
			(unsigned char)(numSectors & 0xFF),
			0, 0 } ), buffer, atapiNumBytes);

	return (status);
}


static int readWriteDma(ahciController *controller, ahciDisk *dsk,
	uquad_t logicalSector, uquad_t numSectors, void *buffer, int write)
{
	int status = 0;
	unsigned char command = 0;
	unsigned sectorsPerCommand = 0;
	unsigned bytesPerCommand = 0;

	// Figure out which command we're going to be sending to the controller
	if (dsk->featureFlags & ATA_FEATURE_48BIT)
	{
		if (write)
			command = ATA_WRITEDMA_EXT;
		else
			command = ATA_READDMA_EXT;
	}
	else
	{
		if (write)
			command = ATA_WRITEDMA;
		else
			command = ATA_READDMA;
	}

	// Figure out the number of sectors per command
	sectorsPerCommand = numSectors;
	if (dsk->featureFlags & ATA_FEATURE_48BIT)
	{
		if (sectorsPerCommand > 65536)
			sectorsPerCommand = 65536;
	}
	else if (sectorsPerCommand > 256)
		sectorsPerCommand = 256;

	// This outer loop is done once for each *command* we send.	Actual
	// data transfers, DMA transfers, etc. may occur more than once per command
	// and are handled by the inner loop.	The number of times we send a
	// command depends upon the maximum number of sectors we can specify per
	// command.
	while (numSectors > 0)
	{
		sectorsPerCommand = min(sectorsPerCommand, numSectors);

		kernelDebug(debug_io, "AHCI %d sectors per command", sectorsPerCommand);

		bytesPerCommand = (sectorsPerCommand * dsk->physical.sectorSize);

		// Issue the command
		if (dsk->featureFlags & ATA_FEATURE_48BIT)
		{
			// Sector count register should be set to 0 if it's 65536
			status = issueCommand(controller, dsk->portNum, 0,
				((sectorsPerCommand == 65536)? 0 : sectorsPerCommand),
				(logicalSector & 0xFFFF), ((logicalSector >> 16) & 0xFFFF),
				((logicalSector >> 32) & 0xFFFF), 0x40, command, NULL, buffer,
				bytesPerCommand, write, 0 /* default timeout */);
		}
		else
		{
			// Sector count register should be set to 0 if it's 256)
			status = issueCommand(controller, dsk->portNum, 0,
				((sectorsPerCommand == 256)? 0 : sectorsPerCommand),
				(logicalSector & 0xFFFF), ((logicalSector >> 16) & 0xFF),
				((logicalSector >> 32) & 0xFFFF),
				(0x40 | ((logicalSector >> 24) & 0xF)), command, NULL, buffer,
				bytesPerCommand, write, 0 /* default timeout */);
		}

		if (status < 0)
		{
			kernelError(kernel_error, "Disk %d:%d, %s %u at %llu failed",
				controller->num, dsk->portNum, (write? "write" : "read"),
				sectorsPerCommand, logicalSector);
			break;
		}

		buffer += bytesPerCommand;
		numSectors -= sectorsPerCommand;
		logicalSector += sectorsPerCommand;
	}

	return (status);
}


static int atapiSetLockState(ahciController *controller, ahciDisk *dsk,
	int locked)
{
	// Lock or unlock an ATAPI device

	int status = 0;

	kernelDebug(debug_io, "AHCI disk on port %d ATAPI %slock", dsk->portNum,
		(locked? "" : "un"));

	if (locked)
		status =
			sendAtapiPacket(controller, dsk, ATAPI_PACKET_LOCK, NULL, 0);
	else
		status =
			sendAtapiPacket(controller, dsk, ATAPI_PACKET_UNLOCK, NULL, 0);

	if (status < 0)
		return (status);

	if (locked)
		dsk->physical.flags |= DISKFLAG_DOORLOCKED;
	else
		dsk->physical.flags &= ~DISKFLAG_DOORLOCKED;

	return (status);
}


static int atapiSetDoorState(ahciController *controller, ahciDisk *dsk,
	int open)
{
	// Open or close the door of an ATAPI device

	int status = 0;

	if (open && (dsk->physical.flags & DISKFLAG_MOTORON))
		// Try to stop it first
		atapiStartStop(controller, dsk, 0);

	kernelDebug(debug_io, "AHCI disk on port %d ATAPI %s", dsk->portNum,
		(open? "open" : "close"));

	if (open)
		status =
			sendAtapiPacket(controller, dsk, ATAPI_PACKET_EJECT, NULL, 0);
	else
		status =
			sendAtapiPacket(controller, dsk, ATAPI_PACKET_CLOSE, NULL, 0);

	if (status < 0)
		return (status);

	if (open)
		dsk->physical.flags |= DISKFLAG_DOOROPEN;
	else
		dsk->physical.flags &= ~DISKFLAG_DOOROPEN;

	return (status);
}


static int readWriteSectors(int diskNum, uquad_t logicalSector,
	uquad_t numSectors, void *buffer, int write)
{
	// This routine reads or writes sectors to/from the disk.

	int status = 0;
	ahciController *controller = DISK_CTRL(diskNum);
	ahciDisk *dsk = DISK(diskNum);

	kernelDebug(debug_io, "AHCI disk on port %d %s %llu at %llu",
		(diskNum & 0xFF), (write? "write" : "read"), numSectors,
		logicalSector);

	if (!controller || !dsk)
	{
		kernelError(kernel_error, "No such disk %d:%d", (diskNum >> 8),
			(diskNum & 0xFF));
		return (status = ERR_NOSUCHENTRY);
	}

	// Make sure we don't try to read/write an address we can't access
	if (!(dsk->featureFlags & ATA_FEATURE_48BIT) &&
		((logicalSector + numSectors - 1) > 0x0FFFFFFF))
	{
		kernelError(kernel_error, "Can't access sectors %llu->%llu on disk "
			"%d:%d with 28-bit addressing", logicalSector,
			(logicalSector + numSectors - 1), (diskNum >> 8), (diskNum & 0xFF));
		return (status = ERR_BOUNDS);
	}

	// Wait for a lock on the port
	status = kernelLockGet(&controller->port[dsk->portNum].lock);
	if (status < 0)
		return (status);

	if (dsk->physical.type & DISKTYPE_SATACDROM)
	{
		// If it's an ATAPI device
		status = readWriteAtapi(controller, dsk, logicalSector, numSectors,
			buffer,	write);
	}
	else if ((dsk->featureFlags & ATA_FEATURE_DMA))
	{
		// Or a DMA device
		status = readWriteDma(controller, dsk, logicalSector, numSectors,
			buffer,	write);
	}
	else
	{
		// Default: A PIO device
		kernelError(kernel_error, "PIO mode not implemented");
		status = ERR_NOTIMPLEMENTED;
	}

	if (!status)
		// We are finished.  The data should be transferred.
		kernelDebug(debug_io, "AHCI transfer successful");

	// Unlock the port
	kernelLockRelease(&controller->port[dsk->portNum].lock);

	return (status);
}


static int driverSetLockState(int diskNum, int locked)
{
	// This will lock or unlock the CD-ROM door

	int status = 0;
	ahciController *controller = DISK_CTRL(diskNum);
	ahciDisk *dsk = DISK(diskNum);

	kernelDebug(debug_io, "AHCI %slock disk on port %d", (locked? "" : "un"),
		(diskNum & 0xFF));

	if (!controller || !dsk)
	{
		kernelError(kernel_error, "No such disk %d:%d", (diskNum >> 8),
			(diskNum & 0xFF));
		return (status = ERR_NOSUCHENTRY);
	}

	// Wait for a lock on the port
	status = kernelLockGet(&controller->port[dsk->portNum].lock);
	if (status < 0)
		return (status);

	if (dsk->physical.type & DISKTYPE_SATACDROM)
	{
		// It's an ATAPI device
		status = atapiSetLockState(controller, dsk, locked);
	}

	// Unlock the port
	kernelLockRelease(&controller->port[dsk->portNum].lock);

	return (status);
}


static int driverSetDoorState(int diskNum, int open)
{
	// This will open or close the CD-ROM door

	int status = 0;
	ahciController *controller = DISK_CTRL(diskNum);
	ahciDisk *dsk = DISK(diskNum);

	kernelDebug(debug_io, "AHCI %s disk on port %d", (open? "open" : "close"),
		(diskNum & 0xFF));

	if (!controller || !dsk)
	{
		kernelError(kernel_error, "No such disk %d:%d", (diskNum >> 8),
			(diskNum & 0xFF));
		return (status = ERR_NOSUCHENTRY);
	}

	if (open && (dsk->physical.flags & DISKFLAG_DOORLOCKED))
	{
		// Don't try to open the door if it is locked
		kernelError(kernel_error, "Disk door is locked");
		return (status = ERR_PERMISSION);
	}

	// Wait for a lock on the port
	status = kernelLockGet(&controller->port[dsk->portNum].lock);
	if (status < 0)
		return (status);

	if (dsk->physical.type & DISKTYPE_SATACDROM)
	{
		// It's an ATAPI device
		status = atapiSetDoorState(controller, dsk, open);
	}

	// Unlock the port
	kernelLockRelease(&controller->port[dsk->portNum].lock);

	return (status);
}


static int driverMediaPresent(int diskNum)
{
	int present = 0;
	ahciController *controller = DISK_CTRL(diskNum);
	ahciDisk *dsk = DISK(diskNum);

	kernelDebug(debug_io, "AHCI check media present");

	if (!controller || !dsk)
	{
		kernelError(kernel_error, "No such disk %d:%d", (diskNum >> 8),
			(diskNum & 0xFF));
		return (present = ERR_NOSUCHENTRY);
	}

	// If it's not removable, say media is present
	if (!(dsk->physical.type & DISKTYPE_REMOVABLE))
		return (present = 1);

	// Wait for a lock on the port
	if (kernelLockGet(&controller->port[dsk->portNum].lock) < 0)
		return (present = 0);

	kernelDebug(debug_io, "AHCI does %ssupport media status",
		((dsk->featureFlags & ATA_FEATURE_MEDSTAT)? "" : "not "));

	if (dsk->physical.type & DISKTYPE_SATACDROM)
	{
		// It's an ATAPI device

		// If it's not started, we start it
		if (!(dsk->physical.flags & DISKFLAG_MOTORON))
		{
			kernelDebug(debug_io, "AHCI disk on port %d start ATAPI",
				dsk->portNum);
			if (atapiStartStop(controller, dsk, 1) >= 0)
				present = 1;
		}
		else
		{
			// Just kickstart the device
			kernelDebug(debug_io, "AHCI disk on port %d kickstart ATAPI device",
				dsk->portNum);
			if (sendAtapiPacket(controller, dsk, ATAPI_PACKET_START,
				NULL, 0) >= 0)
			{
				present = 1;
			}
			else
			{
				// Oops, didn't work -- try a full startup
				if (atapiStartStop(controller, dsk, 1) >= 0)
					present = 1;
			}
		}
	}

	// Unlock the port
	kernelLockRelease(&controller->port[dsk->portNum].lock);

	kernelDebug(debug_io, "AHCI media %spresent", (present? "" : "not "));

	return (present);
}


static int driverReadSectors(int diskNum, uquad_t logicalSector,
	uquad_t numSectors, void *buffer)
{
	// This routine is a wrapper for the readWriteSectors routine.
	return (readWriteSectors(diskNum, logicalSector, numSectors, buffer,
		0 /* read operation */));
}


static int driverWriteSectors(int diskNum, uquad_t logicalSector,
	uquad_t numSectors, const void *buffer)
{
	// This routine is a wrapper for the readWriteSectors routine.
	return (readWriteSectors(diskNum, logicalSector, numSectors,
		(void *) buffer, 1 /* write operation */));
}


static int driverFlush(int diskNum)
{
	// If write caching is enabled for this disk, flush the cache

	int status = 0;
	ahciController *controller = DISK_CTRL(diskNum);
	ahciDisk *dsk = DISK(diskNum);
	unsigned char command = 0;

	kernelDebug(debug_io, "AHCI flush disk on port %d", (diskNum & 0xFF));

	if (!controller || !dsk)
	{
		kernelError(kernel_error, "No such disk %d:%d", (diskNum >> 8),
			(diskNum & 0xFF));
		return (status = ERR_NOSUCHENTRY);
	}

	// If write caching is not enabled, just return
	if (!(dsk->featureFlags & ATA_FEATURE_WCACHE))
		return (status = 0);

	// Wait for a lock on the port
	status = kernelLockGet(&controller->port[dsk->portNum].lock);
	if (status < 0)
		return (status);

	// Figure out which command we're going to be sending to the controller
	if (dsk->featureFlags & ATA_FEATURE_48BIT)
		command = ATA_FLUSHCACHE_EXT;
	else
		command = ATA_FLUSHCACHE;

	// Issue the command
	status = issueCommand(controller, dsk->portNum, 0, 0, 0, 0, 0, 0, command,
		NULL, NULL, 0, 0, 0 /* default timeout */);

	// Unlock the port
	kernelLockRelease(&controller->port[dsk->portNum].lock);

	return (status);
}


static kernelDiskOps ahciOps = {
	NULL,	// driverSetMotorState
	driverSetLockState,
	driverSetDoorState,
	driverMediaPresent,
	NULL,	// driverMediaChanged
	driverReadSectors,
	driverWriteSectors,
	driverFlush
};


/////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////
//
// Below here, the functions are exported for external use
//
/////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////

void kernelSataAhciDriverRegister(kernelDriver *driver)
{
	// Device driver registration.

	driver->driverDetect = driverDetect;
	driver->ops = &ahciOps;

	return;
}

