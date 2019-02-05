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
//  kernelApicDriver.c
//

// Driver for standard Advanced Programmable Interrupt Controllers (APICs)

#include "kernelDriver.h" // Contains my prototypes
#include "kernelApicDriver.h"
#include "kernelDebug.h"
#include "kernelDevice.h"
#include "kernelError.h"
#include "kernelInterrupt.h"
#include "kernelMalloc.h"
#include "kernelPage.h"
#include "kernelParameters.h"
#include "kernelPic.h"
#include "kernelSystemDriver.h"
#include <string.h>
#include <sys/multiproc.h>
#include <sys/processor.h>

#define READSLOTLO(ioApic, num) readIoReg((ioApic), (0x10 + (num * 2)))
#define READSLOTHI(ioApic, num) readIoReg((ioApic), (0x10 + (num * 2) + 1))
#define WRITESLOTLO(ioApic, num, value) \
	writeIoReg((ioApic), (0x10 + (num * 2)), value)
#define WRITESLOTHI(ioApic, num, value) \
	writeIoReg((ioApic), (0x10 + (num * 2) + 1), value)

static volatile void *localApicRegs = NULL;


static unsigned readLocalReg(unsigned offset)
{
	if (localApicRegs)
		return (*((unsigned *)(localApicRegs + offset)));
	else
		return (0);
}


static void writeLocalReg(unsigned offset, unsigned value)
{
	if (localApicRegs)
		*((unsigned *)(localApicRegs + offset)) = value;
}


#ifdef DEBUG
static inline void debugLocalRegs(void)
{
	kernelDebug(debug_io, "APIC debug local APIC regs:\n"
		"  apicId=0x%08x\n"
		"  version=0x%08x\n"
		"  taskPriority=0x%08x\n"
		"  arbitrationPriority=0x%08x\n"
		"  processorPriority=0x%08x\n"
		"  eoi=0x%08x\n"
		"  logicalDestination=0x%08x\n"
		"  destinationFormat=0x%08x\n"
		"  spuriousInterrupt=0x%08x\n"
		"  errorStatus=0x%08x\n"
		"  interruptCommand=0x%08x%08x\n"
		"  localVectorTable=0x%08x\n"
		"  perfCounterLvt=0x%08x\n"
		"  lint0=0x%08x\n"
		"  lint1=0x%08x\n"
		"  error=0x%08x\n"
		"  timerInitialCount=0x%08x",
		readLocalReg(APIC_LOCALREG_APICID),
		readLocalReg(APIC_LOCALREG_VERSION),
		readLocalReg(APIC_LOCALREG_TASKPRI),
		readLocalReg(APIC_LOCALREG_ARBPRI),
		readLocalReg(APIC_LOCALREG_PROCPRI),
		readLocalReg(APIC_LOCALREG_EOI),
		readLocalReg(APIC_LOCALREG_LOGDEST),
		readLocalReg(APIC_LOCALREG_DESTFMT),
		readLocalReg(APIC_LOCALREG_SPURINT),
		readLocalReg(APIC_LOCALREG_ERRSTAT),
		readLocalReg(APIC_LOCALREG_INTCMDHI),
		readLocalReg(APIC_LOCALREG_INTCMDLO),
		readLocalReg(APIC_LOCALREG_LOCVECTBL),
		readLocalReg(APIC_LOCALREG_PERFCNT),
		readLocalReg(APIC_LOCALREG_LINT0),
		readLocalReg(APIC_LOCALREG_LINT1),
		readLocalReg(APIC_LOCALREG_ERROR),
		readLocalReg(APIC_LOCALREG_TIMERCNT));
}
#else
	#define debugLocalRegs() do { } while(0);
#endif


static int timerIrqMapped(kernelDevice *mpDevice)
{
	// Loop through the buses and I/O interrupt assignments to determine
	// whether the system timer ISA IRQ 0 is connected to an APIC.

	kernelMultiProcOps *mpOps = NULL;
	multiProcBusEntry *busEntry = NULL;
	multiProcIoIntAssEntry *intEntry = NULL;
	int count1, count2;

	mpOps = (kernelMultiProcOps *) mpDevice->driver->ops;

	for (count1 = 0; ; count1 ++)
	{
		busEntry = mpOps->driverGetEntry(mpDevice, MULTIPROC_ENTRY_BUS,
			count1);
		if (!busEntry)
			break;

		// Is it ISA?
		if (strncmp(busEntry->type, MULTIPROC_BUSTYPE_ISA, 6))
			continue;

		for (count2 = 0; ; count2 ++)
		{
			intEntry = mpOps->driverGetEntry(mpDevice,
				MULTIPROC_ENTRY_IOINTASSMT, count2);
			if (!intEntry)
				break;

			// Is it for this ISA bus?
			if (intEntry->busId != busEntry->busId)
				continue;

			// Is it the timer interrupt?
			if ((intEntry->intType == MULTIPROC_INTTYPE_INT) &&
				!intEntry->busIrq)
			{
				return (1);
			}
		}
	}

	// Not found
	return (0);
}


static unsigned readIoReg(kernelIoApic *ioApic, unsigned char offset)
{
	// Select the register
	ioApic->regs[0] = offset;

	// Read the data
	return (ioApic->regs[4]);
}


static void writeIoReg(kernelIoApic *ioApic, unsigned char offset,
	unsigned value)
{
	// Select the register
	ioApic->regs[0] = offset;

	// Write the value
	ioApic->regs[4] = value;
}


static int calcVector(int intNumber)
{
	// This looks a bit complicated, so some explanation is in order.
	//
	// For APICs, the upper 4 bits specify the priority level, with 0xF being
	// the highest.  The lower 4 bits are the index at that level.
	//
	// There should ideally be no more than 2 vectors per priority level.
	//
	// Since ISA IRQs 0-15 are numbered by priority (ish), with the highest
	// being 0, we want IRQs 0+1 at level F, IRQs 2+3 at level E, etc.  We
	// only go down to level 2, because below that are the CPU exceptions.
	// That leaves up to 14 priority levels available.  This gives us a
	// sensible distribution for up to 28 IRQs.
	//
	// After 28 IRQs, we fudge it and start back at the top, so IRQs 28+29
	// become vectors F2+F3, IRQs 30+31 become vectors E2+E3, etc.

	int vector = 0;
	int priorities = 0;

	priorities = ((0x100 - INTERRUPT_VECTORSTART) >> 4);

	vector = (((0xF - ((intNumber % (priorities * 2)) / 2)) << 4) |
		(((intNumber / (priorities * 2)) * 2) + (intNumber & 1)));

	return (vector);
}


static int calcIntNumber(int vector)
{
	// Reverse the calculation from calcVector

	int intNumber = 0;
	int priorities = 0;

	priorities = ((0x100 - INTERRUPT_VECTORSTART) >> 4);

	intNumber = ((((vector & 0xF) / 2) * (priorities * 2)) +
		(((0xF - (vector >> 4)) * 2) + (vector & 1)));

	return (intNumber);
}


static multiProcCpuEntry *getBootCpu(kernelDevice *mpDevice)
{
	// Find the multiprocessor boot CPU entry

	kernelMultiProcOps *mpOps = NULL;
	multiProcCpuEntry *cpuEntry = NULL;
	int count;

	mpOps = (kernelMultiProcOps *) mpDevice->driver->ops;

	for (count = 0; ; count ++)
	{
		cpuEntry = mpOps->driverGetEntry(mpDevice, MULTIPROC_ENTRY_CPU,
			count);

		if (!cpuEntry || (cpuEntry->cpuFlags & 0x02))
			return (cpuEntry);
	}

	// Not found
	return (cpuEntry = NULL);
}


static int setupIsaInts(kernelPic *pic, kernelDevice *mpDevice)
{
	int status = 0;
	kernelMultiProcOps *mpOps = NULL;
	multiProcCpuEntry *cpuEntry = NULL;
	multiProcBusEntry *busEntry = NULL;
	multiProcIoIntAssEntry *intEntry = NULL;
	kernelIoApic *ioApic = pic->driverData;
	unsigned slotLo = 0, slotHi = 0;
	int count1, count2;

	mpOps = (kernelMultiProcOps *) mpDevice->driver->ops;

	// Get the boot CPU entry
	cpuEntry = getBootCpu(mpDevice);
	if (!cpuEntry)
		return (status = ERR_NOSUCHENTRY);

	if (!pic->startIrq)
	{
		// For the first I/O APIC handling IRQs starting at 0, set up default,
		// identity-mapped ISA vectors
		for (count1 = 0; count1 < 16; count1 ++)
		{
			slotLo = 0, slotHi = 0;

			// Destination (boot processor)
			slotHi = (cpuEntry->localApicId << 24);

			// Mask it off
			slotLo |= (1 << 16);

			// Vector
			slotLo |= calcVector(count1);

			WRITESLOTLO(ioApic, count1, slotLo);
			WRITESLOTHI(ioApic, count1, slotHi);
		}
	}

	// Loop through the MP bus entries
	for (count1 = 0; ; count1 ++)
	{
		busEntry = mpOps->driverGetEntry(mpDevice, MULTIPROC_ENTRY_BUS,
			count1);
		if (!busEntry)
			break;

		// Is it ISA?
		if (strncmp(busEntry->type, MULTIPROC_BUSTYPE_ISA, 6))
			continue;

		kernelDebug(debug_io, "APIC processing ISA bus %d", busEntry->busId);

		// Loop through the I/O interrupt assignments
		for (count2 = 0; ; count2 ++)
		{
			intEntry = mpOps->driverGetEntry(mpDevice,
				MULTIPROC_ENTRY_IOINTASSMT,	count2);
			if (!intEntry)
				break;

			// Is it for this I/O APIC?
			if (intEntry->ioApicId != ioApic->id)
				continue;

			// Is it for this ISA bus?
			if (intEntry->busId != busEntry->busId)
				continue;

			kernelDebug(debug_io, "APIC processing ISA int entry IRQ=%d "
				"vector=%02x", intEntry->busIrq,
				calcVector(intEntry->busIrq));

			slotLo = 0, slotHi = 0;

			// Destination (boot processor)
			slotHi = (cpuEntry->localApicId << 24);

			// Mask it off
			slotLo |= (1 << 16);

			// Trigger mode.  Default for ISA is edge-triggered.
			if ((intEntry->intFlags & MULTIPROC_INTTRIGGER_MASK) ==
				MULTIPROC_INTTRIGGER_LEVEL)
			{
				slotLo |= (1 << 15);
			}

			// Polarity.  Default for ISA is active high.
			if ((intEntry->intFlags & MULTIPROC_INTPOLARITY_MASK) ==
				MULTIPROC_INTPOLARITY_ACTIVELO)
			{
				slotLo |= (1 << 13);
			}

			// Delivery mode.  Default is 000 (fixed).
			if (intEntry->intType == MULTIPROC_INTTYPE_SMI)
				slotLo |= (0x02 << 8);
			else if (intEntry->intType == MULTIPROC_INTTYPE_NMI)
				slotLo |= (0x04 << 8);
			else if (intEntry->intType == MULTIPROC_INTTYPE_EXTINT)
				slotLo |= (0x07 << 8);

			// Vector
			slotLo |= calcVector(intEntry->busIrq);

			WRITESLOTLO(ioApic, intEntry->ioApicIntPin, slotLo);
			WRITESLOTHI(ioApic, intEntry->ioApicIntPin, slotHi);
		}
	}

	return (status = 0);
}


static int setupPciInts(kernelPic *pic, kernelDevice *mpDevice)
{
	int status = 0;
	kernelMultiProcOps *mpOps = NULL;
	multiProcCpuEntry *cpuEntry = NULL;
	multiProcBusEntry *busEntry = NULL;
	multiProcIoIntAssEntry *intEntry = NULL;
	kernelIoApic *ioApic = pic->driverData;
	unsigned slotLo = 0, slotHi = 0;
	int count1, count2;

	mpOps = (kernelMultiProcOps *) mpDevice->driver->ops;

	// Get the boot CPU entry
	cpuEntry = getBootCpu(mpDevice);
	if (!cpuEntry)
		return (status = ERR_NOSUCHENTRY);

	// Loop through the MP bus entries
	for (count1 = 0; ; count1 ++)
	{
		busEntry = mpOps->driverGetEntry(mpDevice, MULTIPROC_ENTRY_BUS,
			count1);
		if (!busEntry)
			break;

		// Is it PCI?
		if (strncmp(busEntry->type, MULTIPROC_BUSTYPE_PCI, 6))
			continue;

		kernelDebug(debug_io, "APIC processing PCI bus %d", busEntry->busId);

		// Loop through the I/O interrupt assignments
		for (count2 = 0; ; count2 ++)
		{
			intEntry = mpOps->driverGetEntry(mpDevice,
				MULTIPROC_ENTRY_IOINTASSMT, count2);
			if (!intEntry)
				break;

			// Is it for this I/O APIC?
			if (intEntry->ioApicId != ioApic->id)
				continue;

			// Is it for this PCI bus?
			if (intEntry->busId != busEntry->busId)
				continue;

			kernelDebug(debug_io, "APIC processing PCI int entry %d:%c "
				"pin=%d IRQ=%d vector=%02x", ((intEntry->busIrq >> 2) & 0x1F),
				('A' + (intEntry->busIrq & 0x03)), intEntry->ioApicIntPin,
				(pic->startIrq + intEntry->ioApicIntPin),
				calcVector(pic->startIrq + intEntry->ioApicIntPin));

			slotLo = 0, slotHi = 0;

			// Destination (boot processor)
			slotHi = (cpuEntry->localApicId << 24);

			// Mask it off
			slotLo |= (1 << 16);

			// Trigger mode.  Default for PCI is level-triggered.
			if ((intEntry->intFlags & MULTIPROC_INTTRIGGER_MASK) !=
				MULTIPROC_INTTRIGGER_EDGE)
			{
				slotLo |= (1 << 15);
			}

			// Polarity.  Default for PCI is active low.
			if ((intEntry->intFlags & MULTIPROC_INTPOLARITY_MASK) !=
				MULTIPROC_INTPOLARITY_ACTIVEHI)
			{
				slotLo |= (1 << 13);
			}

			// Delivery mode.  Default is 000 (fixed).
			if (intEntry->intType == MULTIPROC_INTTYPE_SMI)
				slotLo |= (0x02 << 8);
			else if (intEntry->intType == MULTIPROC_INTTYPE_NMI)
				slotLo |= (0x04 << 8);
			else if (intEntry->intType == MULTIPROC_INTTYPE_EXTINT)
				slotLo |= (0x07 << 8);

			// Vector
			slotLo |= calcVector(pic->startIrq + intEntry->ioApicIntPin);

			WRITESLOTLO(ioApic, intEntry->ioApicIntPin, slotLo);
			WRITESLOTHI(ioApic, intEntry->ioApicIntPin, slotHi);
		}
	}

	return (status = 0);
}


static int enableLocalApic(kernelDevice *mpDevice)
{
	// Detect whether the CPU has a local APIC, and if so, enable it.

	int status = 0;
	unsigned rega = 0, regb = 0, regc = 0, regd = 0;
	int hasLocal = 0;
	int hasMsrs = 0;
	unsigned apicBase = 0;
	kernelMultiProcOps *mpOps = NULL;
	multiProcLocalIntAssEntry *intEntry = NULL;
	unsigned lint = 0;
	int count;

	// Get the first batch of CPUID regs
	processorId(0, rega, regb, regc, regd);

	// Second batch supported?
	if ((rega & 0x7FFFFFFF) < 1)
		return (status = ERR_NOTINITIALIZED);

	// Get the second batch of CPUID regs
	processorId(1, rega, regb, regc, regd);

	// Is there a local APIC?
	hasLocal = ((regd >> 9) & 1);

	kernelDebug(debug_io, "APIC CPU %s a local APIC",
		(hasLocal? "has" : "does not have"));

	if (!hasLocal)
		return (status = ERR_NOTINITIALIZED);

	// Does the CPU have model-specific registers?
	hasMsrs = ((regd >> 5) & 1);

	kernelDebug(debug_io, "APIC CPU %s MSRs",
		(hasLocal? "has" : "does not have"));

	if (!hasMsrs)
		return (status = ERR_NOTINITIALIZED);

	// Read the local APIC base MSR
	processorReadMsr(X86_MSR_APICBASE, rega, regd);

	apicBase = (rega & (X86_MSR_APICBASE_BASEADDR | X86_MSR_APICBASE_BSP));

	// Set the APIC enable bit (11)
	apicBase |= X86_MSR_APICBASE_APICENABLE;

	// Write it back
	processorWriteMsr(X86_MSR_APICBASE, apicBase, regd);

	apicBase &= X86_MSR_APICBASE_BASEADDR;

	kernelDebug(debug_io, "APIC CPU local APIC base=0x%08x", apicBase);

	localApicRegs = (void *) apicBase;

	// Identity-map the local APIC's registers (4KB)
	if (!kernelPageMapped(KERNELPROCID, (void *) localApicRegs, 0x1000))
	{
		kernelDebug(debug_io, "APIC CPU local APIC registers memory is not "
			"mapped");
		status = kernelPageMap(KERNELPROCID, apicBase, (void *) localApicRegs,
			0x1000);
		if (status < 0)
		{
			kernelError(kernel_error, "Couldn't map local APIC registers");
			return (status);
		}
	}
	else
	{
		kernelDebug(debug_io, "APIC CPU local APIC registers memory is "
			"already mapped");
	}

	// Make it non-cacheable, since this memory represents memory-mapped
	// hardware registers.
	status = kernelPageSetAttrs(KERNELPROCID, 1 /* set */,
		PAGEFLAG_CACHEDISABLE, (void *) localApicRegs, 0x1000);
	if (status < 0)
		kernelDebugError("Error setting page attrs");

	// Set the task priority register to accept all interrupts
	writeLocalReg(APIC_LOCALREG_TASKPRI, 0);

	// Set up the local interrupt vectors

	// Clear/mask them off initially
	writeLocalReg(APIC_LOCALREG_PERFCNT, (1 << 16));
	writeLocalReg(APIC_LOCALREG_LINT0, (1 << 16));
	writeLocalReg(APIC_LOCALREG_LINT1, (1 << 16));
	writeLocalReg(APIC_LOCALREG_ERROR, (1 << 16));

	mpOps = (kernelMultiProcOps *) mpDevice->driver->ops;

	// Loop through the local interrupt assignments
	for (count = 0; ; count ++)
	{
		intEntry = mpOps->driverGetEntry(mpDevice,
			MULTIPROC_ENTRY_LOCINTASSMT, count);
		if (!intEntry)
			break;

		kernelDebug(debug_io, "APIC processing local int entry lint%d",
			intEntry->localApicLint);

		lint = 0;

		// Trigger mode for 'fixed' interrupts.  NMI and ExtINT are
		// automatically level-sensitive.
		if ((intEntry->intType == MULTIPROC_INTTYPE_INT) &&
			((intEntry->intFlags & MULTIPROC_INTTRIGGER_MASK) ==
				MULTIPROC_INTTRIGGER_LEVEL))
		{
			lint |= (1 << 15);
		}

		// Polarity.
		if ((intEntry->intFlags & MULTIPROC_INTPOLARITY_MASK) ==
			MULTIPROC_INTPOLARITY_ACTIVELO)
		{
			lint |= (1 << 13);
		}

		// Delivery mode.  Default is 000 (fixed).
		if (intEntry->intType == MULTIPROC_INTTYPE_NMI)
			lint |= (0x04 << 8);
		else if (intEntry->intType == MULTIPROC_INTTYPE_EXTINT)
			lint |= (0x07 << 8);

		if (!intEntry->localApicLint)
			writeLocalReg(APIC_LOCALREG_LINT0, lint);
		else
			writeLocalReg(APIC_LOCALREG_LINT1, lint);
	}

	kernelDebug(debug_io, "APIC LINT0=0x%08x",
		readLocalReg(APIC_LOCALREG_LINT0));
	kernelDebug(debug_io, "APIC LINT1=0x%08x",
		readLocalReg(APIC_LOCALREG_LINT1));

	// Set the destination format register bits 28-31 to 0xF to set 'flat
	// model'
	writeLocalReg(APIC_LOCALREG_DESTFMT,
		(readLocalReg(APIC_LOCALREG_DESTFMT) | (0xF << 28)));

	// Set bit 8 of the spurious interrupt vector register to enable the APIC,
	// and set the spurious interrupt vector to 0xFF
	writeLocalReg(APIC_LOCALREG_SPURINT,
		(readLocalReg(APIC_LOCALREG_SPURINT) | 0x000001FF));

	return (status = 0);
}


/////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////
//
//  Standard PIC driver functions
//
/////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////

static int driverGetIntNumber(kernelPic *pic, unsigned char busId,
	unsigned char busIrq)
{
	kernelDevice *mpDevice = NULL;
	kernelMultiProcOps *mpOps = NULL;
	multiProcIoIntAssEntry *intEntry = NULL;
	kernelIoApic *ioApic = pic->driverData;
	int count;

	kernelDebug(debug_io, "APIC get interrupt for busId=%d busIrq=%d",
		busId, busIrq);

	// See whether we have a multiprocessor table
	if (kernelDeviceFindType(
		kernelDeviceGetClass(DEVICESUBCLASS_SYSTEM_MULTIPROC), NULL,
			&mpDevice, 1) < 1)
	{
		kernelDebugError("No multiprocessor support detected");
		return (ERR_NOTIMPLEMENTED);
	}

	mpOps = (kernelMultiProcOps *) mpDevice->driver->ops;

	// Loop through the I/O interrupt assignments
	for (count = 0; ; count ++)
	{
		intEntry = mpOps->driverGetEntry(mpDevice, MULTIPROC_ENTRY_IOINTASSMT,
			count);
		if (!intEntry)
			break;

		// Is it for this I/O APIC?
		if (intEntry->ioApicId != ioApic->id)
			continue;

		// Is it for the correct bus?
		if (intEntry->busId != busId)
			continue;

		// Is it the correct type?
		if (intEntry->intType != MULTIPROC_INTTYPE_INT)
			continue;

		// Is it for the correct device?
		if (intEntry->busIrq != busIrq)
			continue;

		// Found it
		return (pic->startIrq + intEntry->ioApicIntPin);
	}

	// Not found
	return (ERR_NOSUCHENTRY);
}


static int driverGetVector(kernelPic *pic __attribute__((unused)),
	int intNumber)
{
	kernelDebug(debug_io, "APIC get vector for interrupt %d (0x%02x)",
		intNumber, calcVector(intNumber));

	return (calcVector(intNumber));
}


static int driverEndOfInterrupt(kernelPic *pic __attribute__((unused)),
	int intNumber __attribute__((unused)))
{
	//kernelDebug(debug_io, "APIC EOI for interrupt %d", intNumber);

	writeLocalReg(APIC_LOCALREG_EOI, 0);

	return (0);
}


static int driverMask(kernelPic *pic, int intNumber, int on)
{
	// This masks or unmasks an interrupt.

	int found = 0;
	kernelIoApic *ioApic = pic->driverData;
	unsigned slotLo = 0, slotHi = 0;
	int count;

	kernelDebug(debug_io, "APIC mask interrupt %d %s", intNumber,
		(on? "on" : "off"));

	for (count = 0; count < pic->numIrqs; count ++)
	{
		slotLo = READSLOTLO(ioApic, count);
		slotHi = READSLOTHI(ioApic, count);

		if (((slotLo & 0x700) != 0x700) &&
			(calcIntNumber(slotLo & 0xFF) == intNumber))
		{
			found += 1;

			if (on)
				slotLo &= ~(1 << 16);
			else
				slotLo |= (1 << 16);

			WRITESLOTLO(ioApic, count, slotLo);
			WRITESLOTHI(ioApic, count, slotHi);

			kernelDebug(debug_io, "APIC slot %d %08x %08x", count,
				READSLOTHI(ioApic, count), READSLOTLO(ioApic, count));
		}
	}

	if (found)
	{
		kernelDebug(debug_io, "APIC masked %s %d sources", (on? "on" : "off"),
			found);
		return (0);
	}
	else
	{
		// Vector not found
		kernelDebugError("Vector not found for interrupt %d", intNumber);
		return (ERR_NOSUCHENTRY);
	}
}


static int driverGetActive(kernelPic *pic __attribute__((unused)))
{
	// Returns the number of the active interrupt

	int intNumber = ERR_NODATA;
	unsigned isrReg = 0;
	int vector = 0;
	int count;

	kernelDebug(debug_io, "APIC active interrupt requested");

	for (count = 112, vector = 0xFF; count >= 0; count -= 16)
	{
		isrReg = readLocalReg(APIC_LOCALREG_ISR + count);

		kernelDebug(debug_io, "APIC ISR %02x-%02x %08x", vector, (vector - 31),
			isrReg);

		if (isrReg)
		{
			while (!(isrReg & 0x80000000))
			{
				isrReg <<= 1;
				vector -= 1;
			}

			intNumber = calcIntNumber(vector);

			kernelDebug(debug_io, "APIC active vector=%02x irq=%d", vector,
				intNumber);

			break;
		}

		vector -= 32;
	}

	return (intNumber);
}


static int driverDetect(void *parent, kernelDriver *driver)
{
	// This function is used to detect and initialize each I/O APIC device, as
	// well as registering each one with the higher-level interface.

	int status = 0;
	kernelDevice *mpDevice = NULL;
	kernelMultiProcOps *mpOps = NULL;
	int haveTimerIrq = 0;
	multiProcCpuEntry *cpuEntry = NULL;
	unsigned short apicIdBitmap = 0;
	multiProcIoApicEntry *ioApicEntry = NULL;
	unsigned char newApicId = 0;
	multiProcIoIntAssEntry *intEntry = NULL;
	kernelIoApic *ioApic = NULL;
	kernelPic *pic = NULL;
	int startIrq = 0;
	kernelDevice *dev = NULL;
	int count1, count2;

	// See whether we have a multiprocessor table
	if (kernelDeviceFindType(
		kernelDeviceGetClass(DEVICESUBCLASS_SYSTEM_MULTIPROC), NULL,
			&mpDevice, 1) < 1)
	{
		kernelDebug(debug_io, "APIC no multiprocessor support detected");
		return (status = 0);
	}

	kernelDebug(debug_io, "APIC multiprocessor support is present");

	// See whether the system timer ISA IRQ 0 is connected to an APIC.  If not,
	// we will still try to detect everything and set up, but we won't enable
	// the APICs
	haveTimerIrq = timerIrqMapped(mpDevice);

	kernelDebug(debug_io, "APIC system timer IRQ is %smapped",
		(haveTimerIrq? "" : "not "));

	mpOps = (kernelMultiProcOps *) mpDevice->driver->ops;

	// Loop through the CPU entries and record their local APIC IDs
	for (count1 = 0; ; count1 ++)
	{
		cpuEntry = mpOps->driverGetEntry(mpDevice, MULTIPROC_ENTRY_CPU,
			count1);
		if (!cpuEntry)
			break;

		if (cpuEntry->localApicId < 16)
			apicIdBitmap |= (1 << cpuEntry->localApicId);
	}

	// Enable this processor's (the boot processor's) local APIC
	status = enableLocalApic(mpDevice);
	if (status < 0)
		goto out;

	// Loop through the multiprocessor entries looking for I/O APICs
	for (count1 = 0; ; count1 ++)
	{
		ioApicEntry = mpOps->driverGetEntry(mpDevice, MULTIPROC_ENTRY_IOAPIC,
			count1);

		if (!ioApicEntry)
			break;

		kernelDebug(debug_io, "APIC I/O APIC device found, apicId=%d, "
			"address=0x%08x", ioApicEntry->apicId, ioApicEntry->apicPhysical);

		// Sometimes the MP tables contain invalid I/O APIC IDs, and we need
		// to assign one.
		if ((ioApicEntry->apicId > 15) ||
			(apicIdBitmap & (1 << ioApicEntry->apicId)))
		{
			kernelDebugError("I/O APIC ID %d invalid or in use",
				ioApicEntry->apicId);

			newApicId = 0;
			while ((newApicId < 16) && (apicIdBitmap & (1 << newApicId)))
				newApicId += 2;

			if (newApicId > 15)
			{
				kernelDebugError("Couldn't find an ID for I/O APIC");
				status = ERR_NOFREE;
				break;
			}

			kernelDebug(debug_io, "APIC chose new ID %d", newApicId);

			// Loop through the I/O interrupt assignments and fix the target
			// I/O APIC IDs as appropriate
			for (count2 = 0; ; count2 ++)
			{
				intEntry = mpOps->driverGetEntry(mpDevice,
					MULTIPROC_ENTRY_IOINTASSMT, count2);
				if (!intEntry)
					break;

				// Is it for this I/O APIC?
				if (intEntry->ioApicId == ioApicEntry->apicId)
					intEntry->ioApicId = newApicId;
			}

			apicIdBitmap |= (1 << newApicId);
			ioApicEntry->apicId = newApicId;
		}

		// Allocate memory for driver data
		ioApic = kernelMalloc(sizeof(kernelIoApic));
		if (!ioApic)
		{
			status = ERR_MEMORY;
			break;
		}

		// Set up our driver data

		ioApic->id = ioApicEntry->apicId;
		ioApic->regs = (volatile unsigned *) ioApicEntry->apicPhysical;

		// Identity-map the registers
		if (!kernelPageMapped(KERNELPROCID, (void *) ioApic->regs,
			(5 * sizeof(unsigned))))
		{
			kernelDebug(debug_io, "APIC I/O APIC registers memory is not "
				"mapped");
			status = kernelPageMap(KERNELPROCID, ioApicEntry->apicPhysical,
				(void *) ioApic->regs, (5 * sizeof(unsigned)));
			if (status < 0)
				break;
		}
		else
		{
			kernelDebug(debug_io, "APIC I/O APIC registers memory is already "
				"mapped");
		}

		// Make it non-cacheable, since this memory represents memory-mapped
		// hardware registers.
		status = kernelPageSetAttrs(KERNELPROCID, 1 /* set */,
			PAGEFLAG_CACHEDISABLE, (void *) ioApic->regs,
			(5 * sizeof(unsigned)));
		if (status < 0)
			kernelDebugError("Error setting page attrs");

		// Make sure the APIC ID is correctly set
		writeIoReg(ioApic, 0, ((readIoReg(ioApic, 0) & 0xF0FFFFFF) |
			(ioApic->id << 24)));

		kernelDebug(debug_io, "APIC id=0x%08x", readIoReg(ioApic, 0));
		kernelDebug(debug_io, "APIC ver=0x%08x", readIoReg(ioApic, 1));
		kernelDebug(debug_io, "APIC arb=0x%08x", readIoReg(ioApic, 2));

		// Allocate memory for the PIC
		pic = kernelMalloc(sizeof(kernelPic));
		if (!pic)
		{
			status = ERR_MEMORY;
			break;
		}

		pic->type = pic_ioapic;
		pic->enabled = (haveTimerIrq && (ioApicEntry->apicFlags & 1));
		pic->startIrq = startIrq;
		pic->numIrqs = (((readIoReg(ioApic, 1) >> 16) & 0xFF) + 1);
		pic->driver = driver;
		pic->driverData = ioApic;

		kernelDebug(debug_io, "APIC startIrq=%d numIrqs=%d", pic->startIrq,
			pic->numIrqs);

		// The next PIC's IRQs will start where this one left off
		startIrq += pic->numIrqs;

		// Mask/clear all the slots
		for (count2 = 0; count2 < pic->numIrqs; count2 ++)
		{
			WRITESLOTLO(ioApic, count2, (1 << 16));
			WRITESLOTHI(ioApic, count2, 0);
		}

		// Set up the standard ISA interrupts
		status = setupIsaInts(pic, mpDevice);
		if (status < 0)
			break;

		// Set up the PCI interrupts
		status = setupPciInts(pic, mpDevice);
		if (status < 0)
			break;

		for (count2 = 0; count2 < pic->numIrqs; count2 ++)
		{
			kernelDebug(debug_io, "APIC slot %d %08x %08x", count2,
				READSLOTHI(ioApic, count2), READSLOTLO(ioApic, count2));
		}

		// Allocate memory for the kernel device
		dev = kernelMalloc(sizeof(kernelDevice));
		if (!dev)
		{
			status = ERR_MEMORY;
			break;
		}

		// Set up the device structure
		dev->device.class = kernelDeviceGetClass(DEVICECLASS_INTCTRL);
		dev->device.subClass =
			kernelDeviceGetClass(DEVICESUBCLASS_INTCTRL_APIC);
		dev->driver = driver;

		// Add the kernel device
		status = kernelDeviceAdd(parent, dev);
		if (status < 0)
			break;

		// Can't free this now
		dev = NULL;

		// Add the PIC to the higher-level interface
		status = kernelPicAdd(pic);
		if (status < 0)
			break;
	}

out:
	if (status < 0)
	{
		if (dev)
			kernelFree(dev);
		if (pic)
			kernelFree(pic);
		if (ioApic)
			kernelFree(ioApic);
	}

	return (status);
}


static kernelPicOps apicOps = {
	driverGetIntNumber,
	driverGetVector,
	driverEndOfInterrupt,
	driverMask,
	driverGetActive,
	NULL	// driverDisable
};


/////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////
//
//  Below here, the functions are exported for external use
//
/////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////

void kernelApicDriverRegister(kernelDriver *driver)
{
	 // Device driver registration.

	driver->driverDetect = driverDetect;
	driver->ops = &apicOps;

	return;
}


#ifdef DEBUG
void kernelApicDebug(void)
{
	unsigned isrReg = 0;
	int vector = 0;
	int count1, count2;

	debugLocalRegs();

	for (count1 = 0; count1 < 128; count1 += 32)
	{
		kernelDebug(debug_io, "APIC IRR %08x %08x",
			readLocalReg(APIC_LOCALREG_IRR + count1 + 16),
			readLocalReg(APIC_LOCALREG_IRR + count1));
	}

	for (count1 = 112, vector = 0xFF; count1 >= 0; count1 -= 16)
	{
		isrReg = readLocalReg(APIC_LOCALREG_IRR + count1);

		for (count2 = 0; count2 < 32; count2 ++)
		{
			if (isrReg & 0x80000000)
			{
				kernelDebug(debug_io, "APIC request=%02x irq=%d", vector,
					calcIntNumber(vector));
			}

			isrReg <<= 1;
			vector -= 1;
		}
	}

	for (count1 = 0; count1 < 128; count1 += 32)
	{
		kernelDebug(debug_io, "APIC ISR %08x %08x",
			readLocalReg(APIC_LOCALREG_ISR + count1 + 16),
			readLocalReg(APIC_LOCALREG_ISR + count1));
	}

	for (count1 = 112, vector = 0xFF; count1 >= 0; count1 -= 16)
	{
		isrReg = readLocalReg(APIC_LOCALREG_ISR + count1);

		for (count2 = 0; count2 < 32; count2 ++)
		{
			if (isrReg & 0x80000000)
			{
				kernelDebug(debug_io, "APIC in service=%02x irq=%d", vector,
					calcIntNumber(vector));
			}

			isrReg <<= 1;
			vector -= 1;
		}
	}
}
#endif

