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
//  kernelUsbAtapiDriver.c
//

// Driver for USB ATAPI (CD/DVD) disks

#include "kernelUsbAtapiDriver.h"
#include "kernelAtaDriver.h"
#include "kernelCpu.h"
#include "kernelDebug.h"
#include "kernelDisk.h"
#include "kernelError.h"
#include "kernelFilesystem.h"
#include "kernelMalloc.h"
#include "kernelMultitasker.h"
#include "kernelScsiDriver.h"
#include "kernelVariableList.h"
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <sys/processor.h>

static kernelPhysicalDisk *disks[USBATAPI_MAX_DISKS];
static int numDisks = 0;


#ifdef DEBUG
static inline void debugInquiry(scsiInquiryData *inquiryData)
{
	char vendorId[9];
	char productId[17];
	char productRev[17];

	strncpy(vendorId, inquiryData->vendorId, 8);
	vendorId[8] = '\0';
	strncpy(productId, inquiryData->productId, 16);
	productId[16] = '\0';
	strncpy(productRev, inquiryData->productRev, 4);
	productRev[4] = '\0';

	kernelDebug(debug_usb, "USB ATAPI debug inquiry data:\n"
		"  qual/devType=%02x\n"
		"  removable=%02x\n"
		"  version=%02x\n"
		"  normACA/hiSup/format=%02x\n"
		"  addlLength=%02x\n"
		"  byte5Flags=%02x\n"
		"  byte6Flags=%02x\n"
		"  relAddr=%02x\n"
		"  vendorId=%s\n"
		"  productId=%s\n"
		"  productRev=%s", inquiryData->byte0.periQual,
		inquiryData->byte1.removable, inquiryData->byte2.ansiVersion,
		inquiryData->byte3.dataFormat, inquiryData->byte4.addlLength,
		inquiryData->byte5, inquiryData->byte6,
		inquiryData->byte7.relAdr, vendorId, productId, productRev);
}

static inline void debugSense(atapiSenseData *senseData)
{
	kernelDebug(debug_usb, "USB ATAPI debug sense data:\n"
		"  error=0x%02x\n"
		"  segNum=%d\n"
		"  senseKey=0x%02x\n"
		"  info=0x%08x\n"
		"  addlLength=%d\n"
		"  commandSpecInfo=0x%08x\n"
		"  addlSenseCode=0x%02x\n"
		"  addlSenseCodeQual=0x%02x\n"
		"  unitCode=0x%02x", senseData->error, senseData->segNum,
		senseData->senseKey, senseData->info, senseData->addlLength,
		senseData->commandSpecInfo, senseData->addlSenseCode,
		senseData->addlSenseCodeQual, senseData->unitCode);
}
#else
	#define debugInquiry(inquiryData) do { } while (0)
	#define debugSense(senseData) do { } while (0)
#endif // DEBUG


static int getNewDiskNumber(void)
{
	// Return an unused disk number

	int diskNumber = 0;
	int count;

	for (count = 0; count < numDisks ; count ++)
	{
		if (disks[count]->deviceNumber == diskNumber)
		{
			diskNumber += 1;
			count = -1;
			continue;
		}
	}

	return (diskNumber);
}


static int usbCommand(kernelUsbAtapiDisk *dsk, scsiCmd12 *cmd12, void *data,
	unsigned dataLength, unsigned *bytes, int read, int silent)
{
	// Wrap a SCSI/ATAPI command in a USB command block wrapper and send it to
	// the device.

	int status = 0;
	usbCmdBlockWrapper cmdWrapper;
	usbCmdStatusWrapper statusWrapper;
	usbTransaction trans[3];
	usbTransaction *cmdTrans = NULL;
	usbTransaction *dataTrans = NULL;
	usbTransaction *statusTrans = NULL;
	int transCount = 0;

	kernelDebug(debug_usb, "USB ATAPI command 0x%02x dataLength=%d",
		cmd12->byte[0],	dataLength);

	memset(&cmdWrapper, 0, sizeof(usbCmdBlockWrapper));
	memset(&statusWrapper, 0, sizeof(usbCmdStatusWrapper));
	memset((void *) trans, 0, (3 * sizeof(usbTransaction)));

	// Set up the command wrapper
	cmdWrapper.signature = USB_CMDBLOCKWRAPPER_SIG;
	cmdWrapper.tag = ++(dsk->tag);
	cmdWrapper.dataLength = dataLength;
	cmdWrapper.flags = (read << 7);
	cmdWrapper.cmdLength = sizeof(scsiCmd12);

	// Copy the command data into the wrapper
	memcpy(cmdWrapper.cmd, cmd12, sizeof(scsiCmd12));

	// Set up the USB transaction to send the command
	cmdTrans = &trans[transCount++];
	cmdTrans->type = usbxfer_bulk;
	cmdTrans->address = dsk->usbDev->address;
	cmdTrans->endpoint = dsk->bulkOutEndpoint;
	cmdTrans->pid = USB_PID_OUT;
	cmdTrans->length = sizeof(usbCmdBlockWrapper);
	cmdTrans->buffer = &cmdWrapper;
	cmdTrans->pid = USB_PID_OUT;
	cmdTrans->timeout = USB_STD_TIMEOUT_MS;

	if (dataLength)
	{
		if (bytes)
			*bytes = 0;

		// Set up the USB transaction to read or write the data
		dataTrans = &trans[transCount++];
		dataTrans->type = usbxfer_bulk;
		dataTrans->address = dsk->usbDev->address;
		dataTrans->length = dataLength;
		dataTrans->buffer = data;
		dataTrans->timeout = USB_STD_TIMEOUT_MS;

		if (read)
		{
			dataTrans->endpoint = dsk->bulkInEndpoint;
			dataTrans->pid = USB_PID_IN;
		}
		else
		{
			dataTrans->endpoint = dsk->bulkOutEndpoint;
			dataTrans->pid = USB_PID_OUT;
		}
	}

	// Set up the USB transaction to read the status
	statusTrans = &trans[transCount++];
	statusTrans->type = usbxfer_bulk;
	statusTrans->address = dsk->usbDev->address;
	statusTrans->endpoint = dsk->bulkInEndpoint;
	statusTrans->pid = USB_PID_IN;
	statusTrans->length = sizeof(usbCmdStatusWrapper);
	statusTrans->buffer = &statusWrapper;
	statusTrans->pid = USB_PID_IN;
	statusTrans->timeout = USB_STD_TIMEOUT_MS;

	// Write the transactions
	status = kernelBusWrite(dsk->busTarget,
		(transCount * sizeof(usbTransaction)), (void *) &trans);
	if (status < 0)
	{
		if (silent)
			kernelDebug(debug_usb, "USB ATAPI transaction error %d", status);
		else
			kernelError(kernel_error, "Transaction error %d", status);

		return (status);
	}

	if (dataLength)
	{
		if (!dataTrans->bytes)
		{
			kernelError(kernel_error, "Data transaction - no data error");
			return (status = ERR_NODATA);
		}

		if (bytes)
			*bytes = (unsigned) dataTrans->bytes;
	}

	if ((statusWrapper.signature != USB_CMDSTATUSWRAPPER_SIG) ||
		(statusWrapper.tag != cmdWrapper.tag))
	{
		// We didn't get the status packet back
		kernelError(kernel_error, "Invalid status packet returned");
		return (status = ERR_IO);
	}

	if (statusWrapper.status != USB_CMDSTATUS_GOOD)
	{
		if (silent)
			kernelDebug(debug_usb, "USB ATAPI command error status %02x",
				statusWrapper.status);
		else
			kernelError(kernel_error, "Command error status %02x",
				statusWrapper.status);

		return (status = ERR_IO);
	}
	else
	{
		kernelDebug(debug_usb, "USB ATAPI command successful");
		return (status = 0);
	}
}


static int atapiRequestSense(kernelUsbAtapiDisk *dsk, atapiSenseData *senseData,
	int silent)
{
	// Do a SCSI/ATAPI 'request sense' command.

	int status = 0;
	scsiCmd12 cmd12;	// ATAPI uses full 12-byte SCSI commands
	unsigned bytes = 0;

	kernelDebug(debug_usb, "USB ATAPI request sense");

	memset(&cmd12, 0, sizeof(scsiCmd12));
	cmd12.byte[0] = SCSI_CMD_REQUESTSENSE;
	cmd12.byte[4] = sizeof(atapiSenseData);

	// Set up the USB transaction, with the SCSI 'request sense' command.
	status = usbCommand(dsk, &cmd12, senseData, sizeof(atapiSenseData),
		&bytes, 1 /* read */, silent);
	if ((status < 0) || (bytes < sizeof(atapiSenseData)))
	{
		if (silent)
			kernelDebug(debug_usb, "USB ATAPI request sense failed");
		else
			kernelError(kernel_error, "Request sense failed");

		return (status);
	}

	// Swap bytes around
	senseData->info = processorSwap32(senseData->info);
	senseData->commandSpecInfo = processorSwap32(senseData->commandSpecInfo);

	kernelDebug(debug_usb, "USB ATAPI request sense successful");

	if (!silent)
		debugSense(senseData);

	return (status = 0);
}


static int atapiInquiry(kernelUsbAtapiDisk *dsk, scsiInquiryData *inquiryData,
	int silent)
{
	// Do a SCSI/ATAPI 'inquiry' command.

	int status = 0;
	scsiCmd12 cmd12;	// ATAPI uses full 12-byte SCSI commands
	unsigned bytes = 0;

	kernelDebug(debug_usb, "USB ATAPI inquiry");

	memset(&cmd12, 0, sizeof(scsiCmd12));
	cmd12.byte[0] = SCSI_CMD_INQUIRY;
	cmd12.byte[4] = sizeof(scsiInquiryData);

	// Set up the USB transaction, with the SCSI 'inquiry' command.
	status = usbCommand(dsk, &cmd12, inquiryData, sizeof(scsiInquiryData),
		&bytes, 1 /* read */, silent);
	if ((status < 0) || (bytes < 36))
	{
		if (silent)
			kernelDebug(debug_usb, "USB ATAPI inquiry failed");
		else
			kernelError(kernel_error, "Inquiry failed");

		return (status);
	}

	kernelDebug(debug_usb, "USB ATAPI inquiry successful");
	debugInquiry(inquiryData);

	return (status = 0);
}


static int atapiStartStopUnit(kernelUsbAtapiDisk *dsk, unsigned char start,
	unsigned char loadEject, int silent)
{
	// Do a SCSI/ATAPI 'start/stop unit' command.

	int status = 0;
	scsiCmd12 cmd12;	// ATAPI uses full 12-byte SCSI commands

	kernelDebug(debug_usb, "USB ATAPI %s unit", (start? "start" : "stop"));

	memset(&cmd12, 0, sizeof(scsiCmd12));
	cmd12.byte[0] = SCSI_CMD_STARTSTOPUNIT;
	cmd12.byte[4] = (((loadEject & 0x01) << 1) | (start & 0x01));

	// Set up the USB transaction, with the SCSI 'start/stop unit' command.
	status = usbCommand(dsk, &cmd12, NULL, 0, NULL, 0 /* no read */, silent);
	if (status < 0)
	{
		if (silent)
			kernelDebug(debug_usb, "USB ATAPI %s unit failed",
				(start? "start" : "stop"));
		else
			kernelError(kernel_error, "%s unit failed",
				(start? "Start" : "Stop"));

		return (status);
	}

	kernelDebug(debug_usb, "USB ATAPI %s unit successful",
		(start? "start" : "stop"));

	return (status = 0);
}


static int atapiTestUnitReady(kernelUsbAtapiDisk *dsk, int silent)
{
	// Do a SCSI/ATAPI 'test unit ready' command.

	int status = 0;
	scsiCmd12 cmd12;	// ATAPI uses full 12-byte SCSI commands

	kernelDebug(debug_usb, "USB ATAPI test unit ready");

	memset(&cmd12, 0, sizeof(scsiCmd12));
	cmd12.byte[0] = SCSI_CMD_TESTUNITREADY;

	// Set up the USB transaction, with the SCSI 'test unit ready' command.
	status = usbCommand(dsk, &cmd12, NULL, 0, NULL, 0 /* no read */, silent);
	if (status < 0)
	{
		if (silent)
			kernelDebug(debug_usb, "USB ATAPI test unit ready failed");
		else
			kernelError(kernel_error, "Test unit ready failed");

		return (status);
	}

	kernelDebug(debug_usb, "USB ATAPI test unit ready successful");

	return (status = 0);
}


static int atapiReadWrite(kernelUsbAtapiDisk *dsk, unsigned logicalSector,
	unsigned short numSectors, void *buffer, int read, int silent)
{
	// Do a SCSI/ATAPI 'read' or 'write' command

	int status = 0;
	unsigned dataLength = 0;
	scsiCmd12 cmd12;	// ATAPI uses full 12-byte SCSI commands
	unsigned bytes = 0;

	dataLength = (numSectors * ATAPI_SECTORSIZE);

	kernelDebug(debug_usb, "USB ATAPI %s %u bytes",	(read? "read" : "write"),
		dataLength);

	memset(&cmd12, 0, sizeof(scsiCmd12));
	if (read)
		cmd12.byte[0] = SCSI_CMD_READ10;
	else
		cmd12.byte[0] = SCSI_CMD_WRITE10;
	*((unsigned *) &cmd12.byte[2]) = processorSwap32(logicalSector);
	*((unsigned short *) &cmd12.byte[7]) = processorSwap16(numSectors);

	// Set up the USB transaction, with the SCSI 'read' or 'write' command.
	status = usbCommand(dsk, &cmd12, buffer, dataLength, &bytes, read, silent);
	if ((status < 0) || (bytes < dataLength))
	{
		if (silent)
			kernelDebug(debug_usb, "USB ATAPI %s failed",
				(read? "read" : "write"));
		else
			kernelError(kernel_error, "%s failed", (read? "Read" : "Write"));

		return (status);
	}

	kernelDebug(debug_usb, "USB ATAPI %s successful %u bytes",
		(read? "read" : "write"), bytes);

	return (status = 0);
}


static int atapiPreventRemoval(kernelUsbAtapiDisk *dsk, int prevent, int silent)
{
	// Do an ATAPI 'prevent allow medium removal' command

	int status = 0;
	scsiCmd12 cmd12;	// ATAPI uses full 12-byte SCSI commands

	kernelDebug(debug_usb, "USB ATAPI %s removal",
		(prevent? "prevent" : "allow"));

	memset(&cmd12, 0, sizeof(scsiCmd12));
	cmd12.byte[0] = ATAPI_PERMITREMOVAL;
	cmd12.byte[4] = (prevent & 0x01);

	// Set up the USB transaction, with the ATAPI 'prevent allow medium
	// removal' command.
	status = usbCommand(dsk, &cmd12, NULL, 0, NULL, 0 /* no read */, silent);
	if (status < 0)
	{
		if (silent)
			kernelDebug(debug_usb, "USB ATAPI %s removal failed",
				(prevent? "prevent" : "allow"));
		else
			kernelError(kernel_error, "%s medium removal failed",
				(prevent? "Prevent" : "Allow"));

		return (status);
	}

	kernelDebug(debug_usb, "USB ATAPI %s removal successful",
		(prevent? "prevent" : "allow"));

	return (status = 0);
}


static int atapiReadCapacity(kernelUsbAtapiDisk *dsk,
	scsiCapacityData *capacityData, int silent)
{
	// Do a SCSI/ATAPI 'read capacity' command.

	int status = 0;
	scsiCmd12 cmd12;	// ATAPI uses full 12-byte SCSI commands
	unsigned bytes = 0;

	kernelDebug(debug_usb, "USB ATAPI read capacity");

	memset(&cmd12, 0, sizeof(scsiCmd12));
	cmd12.byte[0] = SCSI_CMD_READCAPACITY;

	// Set up the USB transaction, with the SCSI 'read capacity' command.
	status = usbCommand(dsk, &cmd12, capacityData, sizeof(scsiCapacityData),
		&bytes, 1 /* read */, silent);
	if ((status < 0) || (bytes < sizeof(scsiCapacityData)))
	{
		if (silent)
			kernelDebug(debug_usb, "USB ATAPI read capacity failed");
		else
			kernelError(kernel_error, "Read capacity failed");

		return (status);
	}

	// Swap bytes around
	capacityData->blockNumber = processorSwap32(capacityData->blockNumber);
	capacityData->blockLength = processorSwap32(capacityData->blockLength);

	kernelDebug(debug_usb, "USB ATAPI read capacity successful");

	return (status = 0);
}


static int atapiReadToc(kernelUsbAtapiDisk *dsk, atapiTocData *tocData,
	int silent)
{
	// Do an ATAPI 'read TOC' (read Table Of Contents) command

	int status = 0;
	scsiCmd12 cmd12;	// ATAPI uses full 12-byte SCSI commands
	unsigned bytes = 0;

	kernelDebug(debug_usb, "USB ATAPI read TOC");

	memset(&cmd12, 0, sizeof(scsiCmd12));
	cmd12.byte[0] = ATAPI_READTOC;
	cmd12.byte[2] = 0x01;
	cmd12.byte[8] = sizeof(atapiTocData);
	cmd12.byte[9] = (0x01 << 6);

	// Set up the USB transaction, with the ATAPI 'prevent allow medium
	// removal' command.
	status = usbCommand(dsk, &cmd12, tocData, sizeof(atapiTocData), &bytes,
		1 /* read */, silent);
	if (status < 0)
	{
		if (silent)
			kernelDebug(debug_usb, "USB ATAPI read TOC failed");
		else
			kernelError(kernel_error, "Read TOC failed");

		return (status);
	}

	// Swap bytes around
	tocData->length = processorSwap16(tocData->length);
	tocData->lastSessionLba = processorSwap32(tocData->lastSessionLba);

	kernelDebug(debug_usb, "USB ATAPI read TOC successful");

	return (status = 0);
}


static inline kernelPhysicalDisk *findDiskByNumber(int diskNum)
{
	int count = 0;

	for (count = 0; count < numDisks; count ++)
	{
		if (disks[count]->deviceNumber == diskNum)
			return (disks[count]);
	}

	// Not found
	return (NULL);
}


static int atapiStartup(kernelPhysicalDisk *physical)
{
	// Start up the ATAPI device and (assuming there's media present) read
	// the TOC, etc.

	int status = 0;
	kernelUsbAtapiDisk *dsk = NULL;
	atapiSenseData senseData;
	scsiCapacityData capacityData;
	atapiTocData tocData;
	uquad_t timeout = (kernelCpuGetMs() + (10 * MS_PER_SEC)); // timout 10 secs

	dsk = (kernelUsbAtapiDisk *) physical->driverData;

	// Try for several seconds to start the device.  If there is no media,
	// or if the media has just been inserted, this command can return
	// various error codes.
	do {
		status = atapiStartStopUnit(dsk, 1 /* start */, 0 /* no load */,
			1 /* silent */);
		if (status < 0)
		{
			// Request sense data
			if (atapiRequestSense(dsk, &senseData, 1 /* silent */) < 0)
				break;

			// Check sense responses
			if (senseData.senseKey == SCSI_SENSE_NOSENSE)
			{
				// No error reported, try again
				kernelMultitaskerWait(5);
				continue;
			}
			else if (senseData.senseKey == SCSI_SENSE_RECOVEREDERROR)
			{
				// Recovered error.  Hmm, some error happened, but the
				// device thinks it handled it.  We shouldn't get this,
				// in other words.
				kernelMultitaskerWait(5);
				continue;
			}
			else if ((senseData.senseKey == SCSI_SENSE_NOTREADY) &&
				(senseData.addlSenseCode == 0x04))
			{
				// The drive may be in the process of becoming ready
				kernelMultitaskerWait(5);
				continue;
			}
			else if ((senseData.senseKey == SCSI_SENSE_UNITATTENTION) &&
				(senseData.addlSenseCode == 0x29))
			{
				// This happens after a reset
				kernelMultitaskerWait(5);
				continue;
			}
			else
			{
				// Assume we shouldn't retry
				break;
			}
		}
		else
		{
			break;
		}

	} while (kernelCpuGetMs() < timeout);

	// Start successful?
	if (status < 0)
	{
		kernelError(kernel_error, "ATAPI startup failed");
		return (status);
	}

	status = atapiReadCapacity(dsk, &capacityData, 0 /* not silent */);
	if (status < 0)
	{
		atapiRequestSense(dsk, &senseData, 0 /* not silent */);
		return (status);
	}

	// The number of sectors
	physical->numSectors = capacityData.blockNumber;

	// The sector size
	physical->sectorSize = capacityData.blockLength;

	// If there's no disk, the number of sectors will be illegal.	Set
	// to the maximum value and quit
	if (!physical->numSectors || (physical->numSectors == 0xFFFFFFFF))
	{
		physical->numSectors = 0xFFFFFFFF;
		physical->sectorSize = ATAPI_SECTORSIZE;
		kernelError(kernel_error, "No media in drive %s", physical->name);
		return (status = ERR_NOMEDIA);
	}

	physical->logical[0].numSectors = physical->numSectors;

	// Read the TOC (Table Of Contents)
	status = atapiReadToc(dsk, &tocData, 0 /* not silent */);
	if (status < 0)
	{
		atapiRequestSense(dsk, &senseData, 0 /* not silent */);
		return (status);
	}

	// Read the LBA of the start of the last session
	physical->lastSession = tocData.lastSessionLba;

	status = atapiTestUnitReady(dsk, 0 /* not silent */);
	if (status < 0)
	{
		atapiRequestSense(dsk, &senseData, 0 /* not silent */);
		return (status);
	}

	physical->flags |= DISKFLAG_MOTORON;

	return (status = 0);
}


static int readWriteSectors(int diskNum, uquad_t logicalSector,
	uquad_t numSectors, void *buffer, int read)
{
	// Read or write sectors.

	int status = 0;
	kernelPhysicalDisk *physical = NULL;
	kernelUsbAtapiDisk *dsk = NULL;

	// Check params
	if (!buffer)
	{
		kernelError(kernel_error, "NULL parameter");
		return (status = ERR_NULLPARAMETER);
	}

	if (!numSectors)
		// Not an error we guess, but nothing to do
		return (status = 0);

	// Find the physical disk based on the disk number
	physical = findDiskByNumber(diskNum);
	if (!physical)
	{
		kernelError(kernel_error, "No such disk, device number %d", diskNum);
		return (status = ERR_NOSUCHENTRY);
	}

	dsk = (kernelUsbAtapiDisk *) physical->driverData;

	// Send a 'test unit ready' command
	status = atapiTestUnitReady(dsk, 1 /* silent */);
	if (status < 0)
	{
		// Try to start up the device, read the TOC, etc.
		if (atapiStartup(physical) < 0)
			return (status);
	}

	kernelDebug(debug_usb, "USB ATAPI %s %llu sectors on \"%s\" at %llu",
		(read? "read" : "write"), numSectors, dsk->vendorProductId,
		logicalSector);

	status = atapiReadWrite(dsk, logicalSector, numSectors, buffer, read,
		0 /* not silent */);

	return (status);
}


static int driverSetLockState(int diskNum, int locked)
{
	// This will lock or unlock the CD-ROM door

	int status = 0;
	kernelPhysicalDisk *physical = NULL;
	kernelUsbAtapiDisk *dsk = NULL;

	kernelDebug(debug_usb, "USB ATAPI %slock", (locked? "" : "un"));

	// Find the physical disk based on the disk number
	physical = findDiskByNumber(diskNum);
	if (!physical)
	{
		kernelError(kernel_error, "No such disk, device number %d", diskNum);
		return (status = ERR_NOSUCHENTRY);
	}

	dsk = (kernelUsbAtapiDisk *) physical->driverData;

	status = atapiPreventRemoval(dsk, locked, 0 /* not silent */);

	if (status >= 0)
	{
		if (locked)
			physical->flags |= DISKFLAG_DOORLOCKED;
		else
			physical->flags &= ~DISKFLAG_DOORLOCKED;
	}

	return (status);
}


static int driverSetDoorState(int diskNum, int open)
{
	// This will open or close the CD-ROM door

	int status = 0;
	kernelPhysicalDisk *physical = NULL;
	kernelUsbAtapiDisk *dsk = NULL;

	kernelDebug(debug_usb, "USB ATAPI %s", (open? "open" : "close"));

	// Find the physical disk based on the disk number
	physical = findDiskByNumber(diskNum);
	if (!physical)
	{
		kernelError(kernel_error, "No such disk, device number %d", diskNum);
		return (status = ERR_NOSUCHENTRY);
	}

	dsk = (kernelUsbAtapiDisk *) physical->driverData;

	if (open && (physical->flags & DISKFLAG_DOORLOCKED))
	{
		// Don't try to open the door if it is locked
		kernelError(kernel_error, "Disk door is locked");
		return (status = ERR_PERMISSION);
	}

	status = atapiStartStopUnit(dsk, (open? 0 : 1), (open? 1 : 0),
		0 /* not silent */);

	if (status >= 0)
	{
		if (open)
			physical->flags |= DISKFLAG_DOOROPEN;
		else
			physical->flags &= ~DISKFLAG_DOOROPEN;
	}

	return (status);
}


static int driverMediaPresent(int diskNum)
{
	int present = 0;
	kernelPhysicalDisk *physical = NULL;
	kernelUsbAtapiDisk *dsk = NULL;

	kernelDebug(debug_usb, "USB ATAPI check media present");

	// Find the physical disk based on the disk number
	physical = findDiskByNumber(diskNum);
	if (!physical)
	{
		kernelError(kernel_error, "No such disk, device number %d", diskNum);
		return (present = ERR_NOSUCHENTRY);
	}

	// If it's not removable, say media is present
	if (!(physical->type & DISKTYPE_REMOVABLE))
		return (present = 1);

	dsk = (kernelUsbAtapiDisk *) physical->driverData;

	// Send a 'test unit ready' command
	if (atapiTestUnitReady(dsk, 1 /* silent */) >= 0)
	{
		present = 1;
	}
	else
	{
		// Try to start up the device, read the TOC, etc.
		if (atapiStartup(physical) >= 0)
			present = 1;
	}

	kernelDebug(debug_usb, "USB ATAPI media %spresent", (present? "" : "not "));

	return (present);
}


static int driverReadSectors(int diskNum, uquad_t logicalSector,
	uquad_t numSectors, void *buffer)
{
	// This function is a wrapper for the readWriteSectors function.
	return (readWriteSectors(diskNum, logicalSector, numSectors, buffer,
		1));  // Read operation
}


static kernelPhysicalDisk *detectTarget(void *parent, int targetId,
	void *driver)
{
	// Given a bus type and a bus target number, see if the device is a USB
	// ATAPI disk

	int status = 0;
	kernelUsbAtapiDisk *dsk = NULL;
	kernelPhysicalDisk *physical = NULL;
	usbInterface *interface = NULL;
	scsiInquiryData inquiryData;
	int count;

	kernelDebug(debug_usb, "USB ATAPI detect target 0x%08x", targetId);

	dsk = kernelMalloc(sizeof(kernelUsbAtapiDisk));
	if (!dsk)
		goto err_out;

	dsk->busTarget = kernelBusGetTarget(bus_usb, targetId);
	if (!dsk->busTarget)
		goto err_out;

	// Try to get the USB device for the target
	dsk->usbDev = kernelUsbGetDevice(targetId);
	if (!dsk->usbDev)
		goto err_out;

	physical = kernelMalloc(sizeof(kernelPhysicalDisk));
	if (!physical)
		goto err_out;

	interface = (usbInterface *) &dsk->usbDev->interface[0];

	// Record the bulk-in and bulk-out endpoints, and any interrupt endpoint
	kernelDebug(debug_usb, "USB ATAPI search for bulk endpoints");
	for (count = 0; count < interface->numEndpoints; count ++)
	{
		switch (interface->endpoint[count].attributes & USB_ENDP_ATTR_MASK)
		{
			case USB_ENDP_ATTR_BULK:
			{
				if (interface->endpoint[count].number & 0x80)
				{
					dsk->bulkInEndpoint = interface->endpoint[count].number;
					kernelDebug(debug_usb, "USB ATAPI bulk in endpoint "
						"0x%02x", dsk->bulkInEndpoint);
				}

				if (!(interface->endpoint[count].number & 0x80))
				{
					dsk->bulkOutEndpoint = interface->endpoint[count].number;
					kernelDebug(debug_usb, "USB ATAPI bulk out endpoint "
						"0x%02x", dsk->bulkOutEndpoint);
				}

				break;
			}

			case USB_ENDP_ATTR_INTERRUPT:
			{
				kernelDebug(debug_usb, "USB ATAPI interrupt endpoint 0x%02x",
					interface->endpoint[count].number);
				break;
			}
		}
	}

	kernelDebug(debug_usb, "USB ATAPI mass storage device detected");

	// Set the device configuration
	if (kernelUsbSetDeviceConfig(dsk->usbDev) < 0)
		goto err_out;

	physical->deviceNumber = getNewDiskNumber();
	physical->description = "USB CD/DVD";
	physical->type = (DISKTYPE_PHYSICAL | DISKTYPE_CDROM);

	// Send an 'inquiry' command
	status = atapiInquiry(dsk, &inquiryData, 0 /* not silent */);
	if (status < 0)
		goto err_out;

	if (inquiryData.byte1.removable & 0x80)
		physical->type |= DISKTYPE_REMOVABLE;
	else
		physical->type |= DISKTYPE_FIXED;

	// Set up the vendor and product ID strings

	strncpy(dsk->vendorId, inquiryData.vendorId, 8);
	dsk->vendorId[8] = '\0';
	for (count = 7; count >= 0; count --)
	{
		if (dsk->vendorId[count] != ' ')
		{
			dsk->vendorId[count + 1] = '\0';
			break;
		}
		else if (!count)
		{
			dsk->vendorId[0] = '\0';
		}
	}

	strncpy(dsk->productId, inquiryData.productId, 16);
	dsk->productId[16] = '\0';
	for (count = 15; count >= 0; count --)
	{
		if (dsk->productId[count] != ' ')
		{
			dsk->productId[count + 1] = '\0';
			break;
		}
		else if (!count)
		{
			dsk->productId[0] = '\0';
		}
	}
	snprintf(dsk->vendorProductId, 26, "%s%s%s", dsk->vendorId,
		(dsk->vendorId[0]? " " : ""), dsk->productId);

	kernelDebug(debug_usb, "USB ATAPI disk \"%s\"", dsk->vendorProductId);

	physical->numSectors = 0xFFFFFFFF;
	physical->sectorSize = ATAPI_SECTORSIZE;
	physical->driverData = (void *) dsk;
	physical->driver = driver;

	disks[numDisks++] = physical;

	// Set up the kernelDevice
	dsk->dev.device.class = kernelDeviceGetClass(DEVICECLASS_DISK);
	dsk->dev.device.subClass = kernelDeviceGetClass(DEVICESUBCLASS_DISK_CDDVD);
	kernelVariableListSet((variableList *) &dsk->dev.device.attrs,
		DEVICEATTRNAME_VENDOR, dsk->vendorId);
	kernelVariableListSet((variableList *) &dsk->dev.device.attrs,
		DEVICEATTRNAME_MODEL, dsk->productId);
	dsk->dev.driver = driver;
	dsk->dev.data = (void *) physical;

	// Tell USB that we're claiming this device.
	kernelBusDeviceClaim(dsk->busTarget, driver);

	// Register the disk
	status = kernelDiskRegisterDevice(&dsk->dev);
	if (status < 0)
		goto err_out;

	// Add the kernel device
	status = kernelDeviceAdd(parent, &dsk->dev);
	if (status < 0)
		goto err_out;

	return (physical);

err_out:

	kernelError(kernel_error, "Error detecting USB ATAPI disk");

	if (physical)
		kernelFree((void *) physical);

	if (dsk)
	{
		if (dsk->busTarget)
			kernelFree(dsk->busTarget);

		kernelFree(dsk);
	}

	return (physical = NULL);
}


static kernelPhysicalDisk *findBusTarget(kernelBusType busType, int target)
{
	// Try to find a disk in our list.

	kernelUsbAtapiDisk *dsk = NULL;
	int count;

	for (count = 0; count < numDisks; count ++)
	{
		if (disks[count] && disks[count]->driverData)
		{
			dsk = (kernelUsbAtapiDisk *) disks[count]->driverData;

			if (dsk->busTarget && dsk->busTarget->bus &&
				(dsk->busTarget->bus->type == busType) &&
				(dsk->busTarget->id == target))
			{
				return (disks[count]);
			}
		}
	}

	// Not found
	return (NULL);
}


static void removeDisk(kernelPhysicalDisk *physical)
{
	// Remove a disk from our list.

	int position = -1;
	int count;

	// Find its position
	for (count = 0; count < numDisks; count ++)
	{
		if (disks[count] == physical)
		{
			position = count;
			break;
		}
	}

	if (position >= 0)
	{
		if ((numDisks > 1) && (position < (numDisks - 1)))
		{
			for (count = position; count < (numDisks - 1); count ++)
				disks[count] = disks[count + 1];
		}

		numDisks -= 1;
	}
}


static int driverDetect(void *parent __attribute__((unused)),
	kernelDriver *driver)
{
	// Try to detect ATAPI disks.

	int status = 0;
	kernelBusTarget *busTargets = NULL;
	int numBusTargets = 0;
	int deviceCount = 0;
	usbDevice usbDev;

	kernelDebug(debug_usb, "USB ATAPI search for devices");

	// Search the USB bus(es) for devices
	numBusTargets = kernelBusGetTargets(bus_usb, &busTargets);
	if (numBusTargets > 0)
	{
		// Search the bus targets for ATAPI disk devices
		for (deviceCount = 0; deviceCount < numBusTargets; deviceCount ++)
		{
			// Try to get the USB information about the target
			status = kernelBusGetTargetInfo(&busTargets[deviceCount],
				(void *) &usbDev);
			if (status < 0)
				continue;

			// If the USB class is 0x08 and the subclass is 0x02 then we
			// believe we have an ATAPI device
			if ((usbDev.classCode != 0x08) || (usbDev.subClassCode != 0x02))
				continue;

			// Already claimed?
			if (busTargets[deviceCount].claimed)
				continue;

			kernelDebug(debug_usb, "USB ATAPI found possible ATAPI device");
			detectTarget(usbDev.controller->dev, busTargets[deviceCount].id,
				driver);
		}

		kernelFree(busTargets);
	}

	return (status = 0);
}


static int driverHotplug(void *parent, int busType __attribute__((unused)),
	int target, int connected, kernelDriver *driver)
{
	// This function is used to detect whether a newly-connected, hotplugged
	// device is supported by this driver during runtime, and if so to do the
	// appropriate device setup and registration.  Alternatively if the device
	// is disconnected a call to this function lets us know to stop trying
	// to communicate with it.

	int status = 0;
	kernelPhysicalDisk *physical = NULL;
	kernelUsbAtapiDisk *dsk = NULL;
	int count;

	kernelDebug(debug_usb, "USB ATAPI device hotplug %sconnection",
		(connected? "" : "dis"));

	if (connected)
	{
		// Determine whether any new ATAPI disks have appeared on the USB bus
		physical = detectTarget(parent, target, driver);
		if (physical)
			kernelDiskReadPartitions((char *) physical->name);
	}
	else
	{
		// Try to find the disk in our list
		physical = findBusTarget(busType, target);
		if (!physical)
		{
			// This can happen if ATAPI initialization did not complete
			// successfully.  In that case, it could be that we're still the
			// registered driver for the device, but we never added it to our
			// list.
			kernelDebugError("No such ATAPI device 0x%08x", target);
			return (status = ERR_NOSUCHENTRY);
		}

		// Found it.
		kernelDebug(debug_usb, "USB ATAPI device removed");

		// If there are filesystems mounted on this disk, try to unmount them
		for (count = 0; count < physical->numLogical; count ++)
		{
			if (physical->logical[count].filesystem.mounted)
				kernelFilesystemUnmount((char *) physical->logical[count]
					.filesystem.mountPoint);
		}

		dsk = (kernelUsbAtapiDisk *) physical->driverData;

		if (dsk)
		{
			// Remove it from the system's disks
			kernelDiskRemoveDevice(&dsk->dev);

			// Remove it from the device tree
			kernelDeviceRemove(&dsk->dev);

			// Free the device's attributes list
			kernelVariableListDestroy(&dsk->dev.device.attrs);

			// Delete.
			removeDisk(physical);

			if (dsk->busTarget)
				kernelFree(dsk->busTarget);

			kernelFree(dsk);
		}

		kernelFree((void *) physical);
	}

	return (status = 0);
}


static kernelDiskOps usbAtapiOps = {
	NULL,	// driverSetMotorState
	driverSetLockState,
	driverSetDoorState,
	driverMediaPresent,
	NULL,	// driverMediaChanged
	driverReadSectors,
	NULL,	// driverWriteSectors
	NULL	// driverFlush
};


/////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////
//
// Below here, the functions are exported for external use
//
/////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////

void kernelUsbAtapiDriverRegister(kernelDriver *driver)
{
	// Device driver registration.

	driver->driverDetect = driverDetect;
	driver->driverHotplug = driverHotplug;
	driver->ops = &usbAtapiOps;

	return;
}

