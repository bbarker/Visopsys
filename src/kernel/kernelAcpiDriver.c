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
//  kernelAcpiDriver.c
//

#include "kernelAcpiDriver.h"
#include "kernelCpu.h"
#include "kernelDebug.h"
#include "kernelDevice.h"
#include "kernelDriver.h" // Contains my prototypes
#include "kernelError.h"
#include "kernelLog.h"
#include "kernelMalloc.h"
#include "kernelMemory.h"
#include "kernelPage.h"
#include "kernelParameters.h"
#include "kernelPower.h"
#include "kernelSystemDriver.h"
#include <string.h>
#include <sys/processor.h>

static int acpiEnabled = 0;


/*
static int acpiEnable(kernelAcpi *acpi)
{
	int status = 0;
	uquad_t timeout = (kernelCpuGetMs() + 3000); // Timout after 3 seconds
	unsigned short dataWord = 0;

	acpiEnabled = 0;

	// Check the things we need to see if ACPI is enabled
	if (!acpi->fadt || !acpi->fadt->pm1aCtrlBlock)
	{
		kernelDebugError("ACPI data structures are incomplete");
		status = ERR_NOTIMPLEMENTED;
		goto out;
	}

	// See whether ACPI is already enabled
	processorInPort16(acpi->fadt->pm1aCtrlBlock, dataWord);
	if ((dataWord & ACPI_PMCTRL_SCI_EN) == 1)
	{
		// Already enabled
		kernelDebugError("ACPI already enabled");
		acpiEnabled = 1;
		goto out;
	}

	// Now check the things we need to enable ACPI
	if (!acpi->fadt->sciCmdPort || !acpi->fadt->acpiEnable)
	{
		kernelDebugError("ACPI data structures are incomplete");
		status = ERR_NOTIMPLEMENTED;
		goto out;
	}

	// Try to enable ACPI
	processorOutPort8(acpi->fadt->sciCmdPort, acpi->fadt->acpiEnable);
	kernelDebug(debug_power, "ACPI enable port=%02x enable=%02x disable=%02x",
		acpi->fadt->sciCmdPort, acpi->fadt->acpiEnable,
		acpi->fadt->acpiDisable);

	acpiEnabled = 0;
	do
	{
		processorInPort16(acpi->fadt->pm1aCtrlBlock, dataWord);
		if ((dataWord & ACPI_PMCTRL_SCI_EN) == 1)
		{
			acpiEnabled = 1;
			break;
		}

	} while (kernelCpuGetMs() < timeout);

	if (acpi->fadt->pm1bCtrlBlock)
	{
		acpiEnabled = 0;
		do
		{
			processorInPort16(acpi->fadt->pm1bCtrlBlock, dataWord);
			if ((dataWord & ACPI_PMCTRL_SCI_EN) == 1)
			{
				acpiEnabled = 1;
				break;
			}

		} while (kernelCpuGetMs() < timeout);
	}

	status = 0;

out:
	if (acpiEnabled)
		kernelLog("ACPI enabled");
	else
		kernelError(kernel_error, "ACPI could not be enabled");

	return (status);
}
*/


static int checkChecksum(char *data, int len)
{
	// Check the checksum (signed chars, should sum to zero)

	char checkSum = 0;
	int count;

	for (count = 0; count < len; count ++)
		checkSum += data[count];

	if (checkSum)
	{
		kernelDebugError("ACPI checksum failed (%d)", checkSum);
		return (-1);
	}
	else
		return (0);
}


static int parseMadt(kernelAcpi *acpi)
{
	int status = 0;
	acpiApicHeader *apicHeader = (acpiApicHeader *) acpi->madt->entry;
	int count;

	if (!acpi->madt)
		return (status = ERR_NOTIMPLEMENTED);

	for (count = sizeof(acpiMadt); count < (int) acpi->madt->header.length; )
	{
		switch (apicHeader->type)
		{
			case ACPI_APICTYPE_LAPIC:
			{
				#if defined(DEBUG)
				acpiLocalApic *apic = (acpiLocalApic *) apicHeader;
				#endif
				kernelDebug(debug_power, "ACPI MADT local APIC procId=%02x "
					"lapicId=%02x", apic->procId, apic->lapicId);
				break;
			}

			case ACPI_APICTYPE_IOAPIC:
			{
				#if defined(DEBUG)
				acpiIoApic *apic = (acpiIoApic *) apicHeader;
				#endif
				kernelDebug(debug_power, "ACPI MADT I/O APIC ioApicId=%02x "
					"ioApicAddr=0x%08x", apic->ioApicId, apic->ioApicAddr);
				break;
			}

			case ACPI_APICTYPE_ISOVER:
			{
				#if defined(DEBUG)
				acpiIsOver *over = (acpiIsOver *) apicHeader;
				#endif
				kernelDebug(debug_power, "ACPI MADT int source override "
					"bus=%02x source=%02x GSI=%08x flags=%04x", over->bus,
					over->source, over->gsi, over->flags);
				break;
			}

			default:
				kernelDebug(debug_power, "ACPI MADT entry type=%d",
					apicHeader->type);
				break;
		}

		count += apicHeader->length;
		apicHeader = ((void *) apicHeader + apicHeader->length);
	}

	return (status = 0);
}


static int driverPowerOff(kernelDevice *dev)
{
	// Use ACPI to power off the system

	int status = 0;
	kernelAcpi *acpi = dev->data;
	unsigned short slpTypA = 0;
	unsigned short slpTypB = 0;
	int found = 0;
	int count;

	// This is a hack, since we're not interested in implementing most of
	// ACPI here.  We just get the bit we need from the differential
	// definitions (DSDT).

	if (!acpiEnabled || !acpi || !acpi->dsdt)
		return (status = ERR_NOTIMPLEMENTED);

	for (count = sizeof(acpiSysDescHeader);
		count < (int) acpi->dsdt->header.length; count ++)
	{
		if (!memcmp((acpi->dsdt->data + count), "_S5_", 4))
		{
			if (((acpi->dsdt->data[count - 1] == 0x08) ||
				((acpi->dsdt->data[count - 2] == 0x08) &&
				(acpi->dsdt->data[count - 1] == '\\'))) &&
				(acpi->dsdt->data[count + 4] == 0x12))
			{
				// Skip past the _S5_ and packageOp
				count += 5;

				// Calculate pkgLength size
				count += (((acpi->dsdt->data[count] & 0xC0) >> 6) + 2);

				if (acpi->dsdt->data[count] == 0x0A)
					// Skip byte prefix
					count += 1;

				slpTypA = (acpi->dsdt->data[count] << 10);
				count += 1;

				if (acpi->dsdt->data[count] == 0x0A)
					// Skip byte prefix
					count += 1;

				slpTypB = (acpi->dsdt->data[count] << 10);

				found = 1;
			}

			break;
		}
	}

	if (found)
	{
		// We got the value to write to the port(s)
		processorOutPort16(acpi->fadt->pm1aCtrlBlock,
			(ACPI_PMCTRL_SLP_EN | slpTypA));

		if (acpi->fadt->pm1bCtrlBlock)
			processorOutPort16(acpi->fadt->pm1bCtrlBlock,
				(ACPI_PMCTRL_SLP_EN | slpTypB));
	}

	return (status = 0);
}


static int driverDetectAcpi(void *parent, kernelDriver *driver)
{
	int status = 0;
	void *rom = NULL;
	char *ptr = NULL;
	acpiRsdp *dataStruct = NULL;
	acpiRsdt *rsdt = NULL;
	kernelAcpi *acpi = NULL;
	int numEntries = 0;
	acpiSysDescHeader *header = NULL;
	kernelDevice *dev = NULL;
	char sig[5];
	int count;

	// Map the designated area for the BIOS into memory so we can scan it.
	status = kernelPageMapToFree(KERNELPROCID, BIOSROM_START, &rom,
		BIOSROM_SIZE);
	if (status < 0)
		goto out;

	for (ptr = rom ;
		ptr <= (char *)(rom + (BIOSROM_SIZE - sizeof(acpiRsdp)));
		ptr += 16)
	{
		if (!strncmp(ptr, ACPI_SIG_RSDP, strlen(ACPI_SIG_RSDP)))
		{
			dataStruct = (acpiRsdp *) ptr;
			break;
		}
	}

	if (!dataStruct)
		goto out;

	// Check the checksum
	if (checkChecksum(ptr, sizeof(acpiRsdp)))
		goto out;

	kernelDebug(debug_power, "ACPI found at 0x%08x, RSDT at 0x%08x",
		(BIOSROM_START + ((void *) dataStruct - rom)),
		dataStruct->rsdtAddr);

	status = kernelPageMapToFree(KERNELPROCID, dataStruct->rsdtAddr,
		(void **) &rsdt, sizeof(acpiRsdt));
	if (status < 0)
	{
		kernelError(kernel_error, "ACPI RSDT physical address 0x%08x can't be "
			"mapped (%d)", dataStruct->rsdtAddr, status);
		goto out;
	}

	if (strncmp(rsdt->header.signature, ACPI_SIG_RSDT, strlen(ACPI_SIG_RSDT)))
	{
		kernelDebugError("ACPI RSDT signature invalid");
		goto out;
	}

	// Check the checksum
	if (checkChecksum((char *) rsdt, rsdt->header.length))
		goto out;

	acpi = kernelMalloc(sizeof(kernelAcpi));
	if (!acpi)
	{
		status = ERR_MEMORY;
		goto out;
	}

	// ACPI version
	acpi->revision = rsdt->header.revision;

	// How many entries?
	numEntries = ((rsdt->header.length - sizeof(acpiRsdt)) / sizeof(void *));

	kernelDebug(debug_power, "ACPI RSDT length=%d revision=%d",
		rsdt->header.length, rsdt->header.revision);

	for (count = 0; count < numEntries; count ++)
	{
		// Map the physical memory to our generic header pointer
		status = kernelPageMapToFree(KERNELPROCID, rsdt->entry[count],
			(void **) &header, MEMORY_PAGE_SIZE);
		if (status < 0)
		{
			kernelError(kernel_error, "ACPI RSDT physical address 0x%08x "
				"can't be mapped (%d)", rsdt->entry[count], status);
			continue;
		}

		strncpy(sig, header->signature, 4);
		sig[4] = '\0';
		kernelDebug(debug_power, "ACPI RSDT entry 0x%08x type %s",
			rsdt->entry[count], sig);

		if (!strncmp(sig, ACPI_SIG_APIC, strlen(ACPI_SIG_APIC)))
		{
			acpi->madt = (acpiMadt *) header;

			// Check the checksum
			if (checkChecksum((char *) acpi->madt, acpi->madt->header.length))
				goto out;

			kernelDebug(debug_power, "ACPI MADT localApicAddr=0x%08x",
				acpi->madt->localApicAddr);

			parseMadt(acpi);
		}
		else if (!strncmp(sig, ACPI_SIG_FADT, strlen(ACPI_SIG_FADT)))
		{
			acpi->fadt = (acpiFadt *) header;

			// Check the checksum
			if (checkChecksum((char *) acpi->fadt, acpi->fadt->header.length))
				goto out;

			kernelDebug(debug_power, "ACPI FADT revision=%02x facsAddr=0x%08x "
				"dsdtAddr=0x%08x", acpi->fadt->header.revision,
				acpi->fadt->facsAddr, acpi->fadt->dsdtAddr);

			if (acpi->fadt->header.revision >= 2)
			{
				kernelDebug(debug_power, "ACPI FADT IA-PC bootArch flags=%04x",
					acpi->fadt->bootArch);
				kernelDebug(debug_power, "ACPI FADT IA-PC legacy=%s "
					"keyboard=%s VGA=%s",
					((acpi->fadt->bootArch & 0x0001)? "yes" : "no"),
					((acpi->fadt->bootArch & 0x0002)? "yes" : "no"),
					((acpi->fadt->bootArch & 0x0004)? "yes" : "no"));
			}
		}
		else
			kernelPageUnmap(KERNELPROCID, header, MEMORY_PAGE_SIZE);
	}

	// Get any additional structures we want, if possible

	if (acpi->fadt && acpi->fadt->facsAddr)
	{
		status = kernelPageMapToFree(KERNELPROCID, acpi->fadt->facsAddr,
			(void **) &acpi->facs, MEMORY_PAGE_SIZE);
		if (status < 0)
		{
			kernelError(kernel_error, "ACPI FACS physical address 0x%08x "
				"can't be mapped (%d)", acpi->fadt->facsAddr, status);
			goto out;
		}

		kernelDebug(debug_power, "ACPI FACS version=%02x hardwareSig=0x%08x "
			"wakingVector=0x%08x", acpi->facs->version,
			acpi->facs->hardwareSig, acpi->facs->wakingVector);

		if (acpi->facs->version >= 1)
			kernelDebug(debug_power, "ACPI FACS xWakingVector=0x%016llx",
				acpi->facs->xWakingVector);
	}

	if (acpi->fadt && acpi->fadt->dsdtAddr)
	{
		status = kernelPageMapToFree(KERNELPROCID, acpi->fadt->dsdtAddr,
			(void **) &acpi->dsdt, MEMORY_PAGE_SIZE);
		if (status < 0)
		{
			kernelError(kernel_error, "ACPI DSDT physical address 0x%08x "
				"can't be mapped (%d)", acpi->fadt->dsdtAddr, status);
			goto out;
		}

		// Does the table length exceed the initial memory page we mapped
		// for it?
		if (acpi->dsdt->header.length > MEMORY_PAGE_SIZE)
		{
			// Map the proper length
			unsigned newLength = acpi->dsdt->header.length;

			kernelPageUnmap(KERNELPROCID, acpi->dsdt, MEMORY_PAGE_SIZE);

			status = kernelPageMapToFree(KERNELPROCID, acpi->fadt->dsdtAddr,
				(void **) &acpi->dsdt, newLength);
			if (status < 0)
			{
				kernelError(kernel_error, "ACPI DSDT physical address 0x%08x "
					"can't be mapped (%d)", acpi->fadt->dsdtAddr, status);
				goto out;
			}
		}

		// Check the checksum
		if (checkChecksum((char *) acpi->dsdt, acpi->dsdt->header.length))
			goto out;
	}

	// Allocate memory for the device
	dev = kernelMalloc(sizeof(kernelDevice));
	if (!dev)
		goto out;

	dev->device.class = kernelDeviceGetClass(DEVICECLASS_POWER);
	dev->device.subClass = kernelDeviceGetClass(DEVICESUBCLASS_POWER_ACPI);
	dev->driver = driver;
	dev->data = acpi;

	// Add the kernel device
	status = kernelDeviceAdd(parent, dev);
	if (status < 0)
		goto out;

	/* Disable this for the time being.  Seems to interfere with (IDE disk
	 interrupts?) but the power off still seems to work without it.

	 // Try to enable ACPI
	 status = acpiEnable(acpi);
	 if (status < 0)
		 goto out;
	*/
	acpiEnabled = 1;

	// Initialize power management
	status = kernelPowerInitialize(dev);
	if (status < 0)
		goto out;

	status = 0;

out:
	if (rsdt)
		kernelPageUnmap(KERNELPROCID, rsdt, sizeof(acpiRsdt));

	if (rom)
		kernelPageUnmap(KERNELPROCID, rom, BIOSROM_SIZE);

	return (status);
}


static kernelPowerOps powerOps = {
	driverPowerOff
};


/////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////
//
// Below here, the functions are exported for external use
//
/////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////

void kernelAcpiDriverRegister(kernelDriver *driver)
{
	// Device driver registration.
	driver->driverDetect = driverDetectAcpi;
	driver->ops = &powerOps;
	return;
}

