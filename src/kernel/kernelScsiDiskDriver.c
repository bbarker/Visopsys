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
//  kernelScsiDiskDriver.c
//

// Driver for standard and USB SCSI disks

#include "kernelScsiDiskDriver.h"
#include "kernelCpu.h"
#include "kernelDebug.h"
#include "kernelDisk.h"
#include "kernelError.h"
#include "kernelFilesystem.h"
#include "kernelMalloc.h"
#include "kernelRandom.h"
#include "kernelScsiDriver.h"
#include "kernelVariableList.h"
#include <stdio.h>
#include <string.h>
#include <sys/processor.h>

static kernelPhysicalDisk *disks[SCSI_MAX_DISKS];
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

	kernelDebug(debug_scsi, "SCSI debug inquiry data:\n"
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

static inline void debugSense(scsiSenseData *senseData)
{
	kernelDebug(debug_scsi, "SCSI debug sense data:\n"
		"  validErrCode=0x%02x\n"
		"  segment=%d\n"
		"  flagsKey=0x%02x\n"
		"  info=0x%08x\n"
		"  addlLength=%d\n"
		"  cmdSpecific=0x%08x\n"
		"  addlCode=0x%02x\n"
		"  addlCodeQual=0x%02x", senseData->validErrCode,
		senseData->segment, senseData->flagsKey, senseData->info,
		senseData->addlLength, senseData->cmdSpecific, senseData->addlCode,
		senseData->addlCodeQual);
}
#else
	#define debugInquiry(inquiryData) do { } while (0)
	#define debugSense(senseData) do { } while (0)
#endif // DEBUG


static int usbMassStorageReset(kernelScsiDisk *scsiDisk)
{
	// Send the USB "mass storage reset" command to the first interface of
	// the device

	int status = 0;

	kernelDebug(debug_scsi, "SCSI USB MS reset");

	// Do the control transfer to send the reset command
	status = kernelUsbControlTransfer(scsiDisk->usb.usbDev,
		USB_MASSSTORAGE_RESET, 0, 0 /* interface */, 0, 0, NULL, NULL);
	if (status < 0)
		kernelDebug(debug_scsi, "SCSI USB MS reset failed");

	return (status);
}


static int usbClearHalt(kernelScsiDisk *scsiDisk, unsigned char endpoint)
{
	int status = 0;

	kernelDebug(debug_scsi, "SCSI USB MS clear halt, endpoint %d", endpoint);

	// Do the control transfer to send the 'clear (halt) feature' to the
	// endpoint
	status = kernelUsbControlTransfer(scsiDisk->usb.usbDev, USB_CLEAR_FEATURE,
		USB_FEATURE_ENDPOINTHALT, endpoint, 0, 0, NULL, NULL);
	if (status < 0)
		kernelError(kernel_error, "Clear halt failed");

	return (status);
}


static int usbMassStorageResetRecovery(kernelScsiDisk *scsiDisk)
{
	// Send the USB "mass storage reset" command to the first interface of
	// the device, and clear any halt conditions on the bulk-in and bulk-out
	// endpoints.

	int status = 0;

	kernelDebug(debug_scsi, "SCSI USB MS reset recovery");

	status = usbMassStorageReset(scsiDisk);
	if (status < 0)
		goto out;

	status = usbClearHalt(scsiDisk, scsiDisk->usb.bulkInEndpoint);
	if (status < 0)
		goto out;

	status = usbClearHalt(scsiDisk, scsiDisk->usb.bulkOutEndpoint);

out:
	if (status < 0)
		kernelError(kernel_error, "Reset recovery failed");

	return (status);
}


static int usbScsiCommand(kernelScsiDisk *scsiDisk, unsigned char lun,
	unsigned char *cmd, unsigned char cmdLength, void *data,
	unsigned dataLength, unsigned *bytes, unsigned timeout, int read)
{
	// Wrap a SCSI command in a USB command block wrapper and send it to
	// the device.

	int status = 0;
	usbCmdBlockWrapper cmdWrapper;
	usbCmdStatusWrapper statusWrapper;
	usbTransaction trans[3];
	usbTransaction *cmdTrans = NULL;
	usbTransaction *dataTrans = NULL;
	usbTransaction *statusTrans = NULL;
	int transCount = 0;

	kernelDebug(debug_scsi, "SCSI USB MS command 0x%02x datalength %d", cmd[0],
		dataLength);

	memset(&cmdWrapper, 0, sizeof(usbCmdBlockWrapper));
	memset(&statusWrapper, 0, sizeof(usbCmdStatusWrapper));
	memset((void *) trans, 0, (3 * sizeof(usbTransaction)));

	// Set up the command wrapper
	cmdWrapper.signature = USB_CMDBLOCKWRAPPER_SIG;
	cmdWrapper.tag = ++(scsiDisk->usb.tag);
	cmdWrapper.dataLength = dataLength;
	cmdWrapper.flags = (read << 7);
	cmdWrapper.lun = lun;
	cmdWrapper.cmdLength = cmdLength;

	// Copy the command data into the wrapper
	memcpy(cmdWrapper.cmd, cmd, cmdLength);
	kernelDebug(debug_scsi, "SCSI USB MS command length %d",
		cmdWrapper.cmdLength);

	// Set up the USB transaction to send the command
	cmdTrans = &trans[transCount++];
	cmdTrans->type = usbxfer_bulk;
	cmdTrans->address = scsiDisk->usb.usbDev->address;
	cmdTrans->endpoint = scsiDisk->usb.bulkOutEndpoint;
	cmdTrans->pid = USB_PID_OUT;
	cmdTrans->length = sizeof(usbCmdBlockWrapper);
	cmdTrans->buffer = &cmdWrapper;
	cmdTrans->pid = USB_PID_OUT;
	cmdTrans->timeout = timeout;

	if (dataLength)
	{
		if (bytes)
			*bytes = 0;

		// Set up the USB transaction to read or write the data
		dataTrans = &trans[transCount++];
		dataTrans->type = usbxfer_bulk;
		dataTrans->address = scsiDisk->usb.usbDev->address;
		dataTrans->length = dataLength;
		dataTrans->buffer = data;
		dataTrans->timeout = timeout;

		if (read)
		{
			dataTrans->endpoint = scsiDisk->usb.bulkInEndpoint;
			dataTrans->pid = USB_PID_IN;
		}
		else
		{
			dataTrans->endpoint = scsiDisk->usb.bulkOutEndpoint;
			dataTrans->pid = USB_PID_OUT;
		}

		kernelDebug(debug_scsi, "SCSI USB MS datalength=%u", dataLength);
	}

	// Set up the USB transaction to read the status
	statusTrans = &trans[transCount++];
	statusTrans->type = usbxfer_bulk;
	statusTrans->address = scsiDisk->usb.usbDev->address;
	statusTrans->endpoint = scsiDisk->usb.bulkInEndpoint;
	statusTrans->pid = USB_PID_IN;
	statusTrans->length = sizeof(usbCmdStatusWrapper);
	statusTrans->buffer = &statusWrapper;
	statusTrans->pid = USB_PID_IN;
	statusTrans->timeout = timeout;

	kernelDebug(debug_scsi, "SCSI USB MS status length=%u",
		statusTrans->length);

	// Write the transactions
	status = kernelBusWrite(scsiDisk->busTarget,
		(transCount * sizeof(usbTransaction)), (void *) &trans);
	if (status < 0)
	{
		kernelError(kernel_error, "Transaction error %d", status);

		// Try to clear the stall
		if (usbClearHalt(scsiDisk, scsiDisk->usb.bulkInEndpoint) < 0)
			// Try a reset
			usbMassStorageResetRecovery(scsiDisk);

		return (status);
	}

	if (dataLength)
	{
		if (!dataTrans->bytes)
		{
			kernelError(kernel_error, "USB MS data transaction - no data "
				"error");
			return (status = ERR_NODATA);
		}

		if (bytes)
			*bytes = (unsigned) dataTrans->bytes;
	}

	if ((statusWrapper.signature != USB_CMDSTATUSWRAPPER_SIG) ||
		(statusWrapper.tag != cmdWrapper.tag))
	{
		// We didn't get the status packet back
		kernelError(kernel_error, "USB MS invalid status packet returned");
		return (status = ERR_IO);
	}

	if (statusWrapper.status != USB_CMDSTATUS_GOOD)
	{
		kernelError(kernel_error, "USB MS command error status %02x",
			statusWrapper.status);
		return (status = ERR_IO);
	}
	else
	{
		kernelDebug(debug_scsi, "SCSI USB MS command successful");
		return (status = 0);
	}
}


static int scsiInquiry(kernelScsiDisk *scsiDisk, unsigned char lun,
	scsiInquiryData *inquiryData)
{
	// Do a SCSI 'inquiry' command.

	int status = 0;
	scsiCmd6 cmd6;
	unsigned bytes = 0;

	kernelDebug(debug_scsi, "SCSI inquiry");

	memset(&cmd6, 0, sizeof(scsiCmd6));
	cmd6.byte[0] = SCSI_CMD_INQUIRY;
	cmd6.byte[1] = (lun << 5);
	cmd6.byte[4] = sizeof(scsiInquiryData);

	if (scsiDisk->busTarget->bus->type == bus_usb)
	{
		// Set up the USB transaction, with the SCSI 'inquiry' command.
		status = usbScsiCommand(scsiDisk, lun, (unsigned char *) &cmd6,
			sizeof(scsiCmd6), inquiryData, sizeof(scsiInquiryData), &bytes,
			0 /* default timeout */, 1 /* read */);
		if ((status < 0) || (bytes < 36))
		{
			kernelError(kernel_error, "SCSI inquiry failed");
			return (status);
		}
	}
	else
	{
		kernelDebugError("Non-USB SCSI not supported");
		return (status = ERR_NOTIMPLEMENTED);
	}

	kernelDebug(debug_scsi, "SCSI inquiry successful");
	debugInquiry(inquiryData);
	return (status = 0);
}


static int scsiReadWrite(kernelScsiDisk *scsiDisk, unsigned char lun,
	unsigned logicalSector, unsigned short numSectors, void *buffer, int read)
{
	// Do a SCSI 'read' or 'write' command

	int status = 0;
	unsigned dataLength = 0;
	scsiCmd10 cmd10;
	unsigned bytes = 0;

	dataLength = (numSectors * scsiDisk->sectorSize);

	kernelDebug(debug_scsi, "SCSI %s %u bytes sectorsize %u",
		(read? "read" : "write"), dataLength, scsiDisk->sectorSize);

	memset(&cmd10, 0, sizeof(scsiCmd10));
	if (read)
		cmd10.byte[0] = SCSI_CMD_READ10;
	else
		cmd10.byte[0] = SCSI_CMD_WRITE10;
	cmd10.byte[1] = (lun << 5);
	*((unsigned *) &cmd10.byte[2]) = processorSwap32(logicalSector);
	*((unsigned short *) &cmd10.byte[7]) = processorSwap16(numSectors);

	if (scsiDisk->busTarget->bus->type == bus_usb)
	{
		// Set up the USB transaction, with the SCSI 'read' or 'write' command.
		status = usbScsiCommand(scsiDisk, lun, (unsigned char *) &cmd10,
			sizeof(scsiCmd10), buffer, dataLength, &bytes,
			(USB_STD_TIMEOUT_MS + (10 * numSectors)), read);
		if ((status < 0) || (bytes < dataLength))
		{
			kernelError(kernel_error, "SCSI %s failed",
				(read? "read" : "write"));
			return (status);
		}
	}
	else
	{
		kernelDebugError("Non-USB SCSI not supported");
		return (status = ERR_NOTIMPLEMENTED);
	}

	kernelDebug(debug_scsi, "SCSI %s successful %u bytes",
		(read? "read" : "write"), bytes);
	return (status = 0);
}


static int scsiReadCapacity(kernelScsiDisk *scsiDisk, unsigned char lun,
	scsiCapacityData *capacityData)
{
	// Do a SCSI 'read capacity' command.

	int status = 0;
	scsiCmd10 cmd10;
	unsigned bytes = 0;

	kernelDebug(debug_scsi, "SCSI read capacity");
	memset(&cmd10, 0, sizeof(scsiCmd10));
	cmd10.byte[0] = SCSI_CMD_READCAPACITY;
	cmd10.byte[1] = (lun << 5);

	if (scsiDisk->busTarget->bus->type == bus_usb)
	{
		// Set up the USB transaction, with the SCSI 'read capacity' command.
		status = usbScsiCommand(scsiDisk, lun, (unsigned char *) &cmd10,
			sizeof(scsiCmd10), capacityData, sizeof(scsiCapacityData),
			&bytes, 0 /* default timeout */, 1 /* read */);
		if ((status < 0) || (bytes < sizeof(scsiCapacityData)))
		{
			kernelError(kernel_error, "SCSI read capacity failed");
			return (status);
		}
	}
	else
	{
		kernelDebugError("Non-USB SCSI not supported");
		return (status = ERR_NOTIMPLEMENTED);
	}

	// Swap bytes around
	capacityData->blockNumber = processorSwap32(capacityData->blockNumber);
	capacityData->blockLength = processorSwap32(capacityData->blockLength);

	kernelDebug(debug_scsi, "SCSI read capacity successful");
	return (status = 0);
}


static int scsiRequestSense(kernelScsiDisk *scsiDisk, unsigned char lun,
	scsiSenseData *senseData)
{
	// Do a SCSI 'request sense' command.

	int status = 0;
	scsiCmd6 cmd6;
	unsigned bytes = 0;

	kernelDebug(debug_scsi, "SCSI request sense");
	memset(&cmd6, 0, sizeof(scsiCmd6));
	cmd6.byte[0] = SCSI_CMD_REQUESTSENSE;
	cmd6.byte[1] = (lun << 5);
	cmd6.byte[4] = sizeof(scsiSenseData);

	if (scsiDisk->busTarget->bus->type == bus_usb)
	{
		// Set up the USB transaction, with the SCSI 'request sense' command.
		status = usbScsiCommand(scsiDisk, lun, (unsigned char *) &cmd6,
			sizeof(scsiCmd6), senseData, sizeof(scsiSenseData), &bytes,
			0 /* default timeout */, 1 /* read */);
		if ((status < 0) || (bytes < sizeof(scsiSenseData)))
		{
			kernelError(kernel_error, "SCSI request sense failed");
			return (status);
		}
	}
	else
	{
		kernelDebugError("Non-USB SCSI not supported");
		return (status = ERR_NOTIMPLEMENTED);
	}

	// Swap bytes around
	senseData->info = processorSwap32(senseData->info);
	senseData->cmdSpecific = processorSwap32(senseData->cmdSpecific);

	kernelDebug(debug_scsi, "SCSI request sense successful");
	debugSense(senseData);
	return (status = 0);
}


static int scsiStartStopUnit(kernelScsiDisk *scsiDisk, unsigned char lun,
	unsigned char start, unsigned char loadEject)
{
	// Do a SCSI 'start/stop unit' command.

	int status = 0;
	scsiCmd6 cmd6;

	kernelDebug(debug_scsi, "SCSI %s unit", (start? "start" : "stop"));
	memset(&cmd6, 0, sizeof(scsiCmd6));
	cmd6.byte[0] = SCSI_CMD_STARTSTOPUNIT;
	cmd6.byte[1] = (lun << 5);
	cmd6.byte[4] = (((loadEject & 0x01) << 1) | (start & 0x01));

	if (scsiDisk->busTarget->bus->type == bus_usb)
	{
		// Set up the USB transaction, with the SCSI 'start/stop unit' command.
		// Give it a longer timeout, since some disks seem to take a while.
		status = usbScsiCommand(scsiDisk, lun, (unsigned char *) &cmd6,
			sizeof(scsiCmd6), NULL,	0, NULL, (USB_STD_TIMEOUT_MS * 5),
			0 /* write */);
		if (status < 0)
		{
			kernelError(kernel_error, "SCSI %s unit failed",
				(start? "start" : "stop"));
			return (status);
		}
	}
	else
	{
		kernelDebugError("Non-USB SCSI not supported");
		return (status = ERR_NOTIMPLEMENTED);
	}

	kernelDebug(debug_scsi, "SCSI %s unit successful",
		(start? "start" : "stop"));
	return (status = 0);
}


static int scsiTestUnitReady(kernelScsiDisk *scsiDisk, unsigned char lun)
{
	// Do a SCSI 'test unit ready' command.

	int status = 0;
	scsiCmd6 cmd6;

	kernelDebug(debug_scsi, "SCSI test unit ready");
	memset(&cmd6, 0, sizeof(scsiCmd6));
	cmd6.byte[0] = SCSI_CMD_TESTUNITREADY;
	cmd6.byte[1] = (lun << 5);

	if (scsiDisk->busTarget->bus->type == bus_usb)
	{
		// Set up the USB transaction, with the SCSI 'test unit ready' command.
		status = usbScsiCommand(scsiDisk, lun, (unsigned char *) &cmd6,
			sizeof(scsiCmd6), NULL,	0, NULL, 0 /* default timeout */,
			0 /* write */);
		if (status < 0)
		{
			kernelError(kernel_error, "SCSI test unit ready failed");
			return (status);
		}
	}
	else
	{
		kernelDebugError("Non-USB SCSI not supported");
		return (status = ERR_NOTIMPLEMENTED);
	}

	kernelDebug(debug_scsi, "SCSI test unit ready successful");
	return (status = 0);
}


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


static void guessDiskGeom(kernelPhysicalDisk *physicalDisk)
{
	// Given a disk with the number of sectors field set, try to figure out
	// some geometry values that make sense

	struct {
		unsigned heads;
		unsigned sectors;
	} guesses[] = {
		{ 255, 63 },
		{ 16, 63 },
		{ 255, 32 },
		{ 16, 32 },
		{ 0, 0 }
	};
	int count;

	// See if any of our guesses match up with the number of sectors.
	for (count = 0; guesses[count].heads; count ++)
	{
		if (!(physicalDisk->numSectors %
			(guesses[count].heads * guesses[count].sectors)))
		{
			physicalDisk->heads = guesses[count].heads;
			physicalDisk->sectorsPerCylinder = guesses[count].sectors;
			physicalDisk->cylinders =
				(physicalDisk->numSectors /
					(guesses[count].heads * guesses[count].sectors));
			goto out;
		}
	}

	// Nothing yet.  Instead, try to calculate something on the fly.
	physicalDisk->heads = 16;
	physicalDisk->sectorsPerCylinder = 32;
	while (physicalDisk->heads < 256)
	{
		if (!(physicalDisk->numSectors %
			(physicalDisk->heads * physicalDisk->sectorsPerCylinder)))
		{
			physicalDisk->cylinders =
				(physicalDisk->numSectors /
					(physicalDisk->heads * physicalDisk->sectorsPerCylinder));
			goto out;
		}

		physicalDisk->heads += 1;
	}

	kernelError(kernel_warn, "Unable to guess disk geometry");
	return;

out:
	kernelDebug(debug_scsi, "SCSI guess geom %u/%u/%u",
		physicalDisk->cylinders, physicalDisk->heads,
		physicalDisk->sectorsPerCylinder);
	return;
}


static kernelPhysicalDisk *detectTarget(void *parent, int busType,
	int targetId, void *driver)
{
	// Given a bus type and a bus target number, see if the device is a
	// SCSI disk

	int status = 0;
	kernelScsiDisk *scsiDisk = NULL;
	kernelPhysicalDisk *physicalDisk = NULL;
	usbInterface *interface = NULL;
	scsiSenseData senseData;
	scsiInquiryData inquiryData;
	scsiCapacityData capacityData;
	int retries = 0;
	int count;

	kernelDebug(debug_scsi, "SCSI detect target 0x%08x", targetId);

	scsiDisk = kernelMalloc(sizeof(kernelScsiDisk));
	if (!scsiDisk)
		goto err_out;

	scsiDisk->busTarget = kernelBusGetTarget(busType, targetId);
	if (!scsiDisk->busTarget)
		goto err_out;

	physicalDisk = kernelMalloc(sizeof(kernelPhysicalDisk));
	if (!physicalDisk)
		goto err_out;

	if (scsiDisk->busTarget->bus->type == bus_usb)
	{
		// Try to get the USB device for the target
		scsiDisk->usb.usbDev = kernelUsbGetDevice(targetId);
		if (!scsiDisk->usb.usbDev)
			goto err_out;

		interface = (usbInterface *) &scsiDisk->usb.usbDev->interface[0];

		// Record the bulk-in and bulk-out endpoints
		kernelDebug(debug_scsi, "SCSI USB MS search for bulk endpoints");
		for (count = 0; count < interface->numEndpoints; count ++)
		{
			switch (interface->endpoint[count].attributes & USB_ENDP_ATTR_MASK)
			{
				case USB_ENDP_ATTR_BULK:
				{
					if (interface->endpoint[count].number & 0x80)
					{
						scsiDisk->usb.bulkInEndpoint =
							interface->endpoint[count].number;
						kernelDebug(debug_scsi, "SCSI USB MS bulk in "
							"endpoint 0x%02x", scsiDisk->usb.bulkInEndpoint);
					}

					if (!(interface->endpoint[count].number & 0x80))
					{
						scsiDisk->usb.bulkOutEndpoint =
							interface->endpoint[count].number;
						kernelDebug(debug_scsi, "SCSI USB MS bulk out "
							"endpoint 0x%02x", scsiDisk->usb.bulkOutEndpoint);
					}

					break;
				}
			}
		}

		// We must have both bulk-in and bulk-out endpoints
		if (!scsiDisk->usb.bulkInEndpoint || !scsiDisk->usb.bulkOutEndpoint)
		{
			kernelError(kernel_error, "Missing bulk-in or bulk-out endpoint");
			goto err_out;
		}

		kernelDebug(debug_scsi, "SCSI USB MS mass storage device detected");
		physicalDisk->type |= DISKTYPE_FLASHDISK;

		// Set the device configuration
		if (kernelUsbSetDeviceConfig(scsiDisk->usb.usbDev) < 0)
			goto err_out;
	}
	else
	{
		kernelDebugError("Non-USB SCSI not supported");
		goto err_out;
	}

	// Send a 'request sense' command
	status = scsiRequestSense(scsiDisk, 0, &senseData);
	if (status < 0)
		goto err_out;

	if ((senseData.flagsKey & 0x0F) != SCSI_SENSE_NOSENSE)
	{
		kernelError(kernel_error, "SCSI sense error - sense key=0x%02x "
			"asc=0x%02x ascq=0x%02x", (senseData.flagsKey & 0x0F),
			senseData.addlCode, senseData.addlCodeQual);
	}

	// Send an 'inquiry' command
	status = scsiInquiry(scsiDisk, 0, &inquiryData);
	if (status < 0)
		goto err_out;

	if ((scsiDisk->busTarget->bus->type == bus_usb) ||
		(inquiryData.byte1.removable & 0x80))
	{
		physicalDisk->type |= DISKTYPE_REMOVABLE;
	}
	else
	{
		physicalDisk->type |= DISKTYPE_FIXED;
	}

	// Set up the vendor and product ID strings

	strncpy(scsiDisk->vendorId, inquiryData.vendorId, 8);
	scsiDisk->vendorId[8] = '\0';
	for (count = 7; count >= 0; count --)
	{
		if (scsiDisk->vendorId[count] != ' ')
		{
			scsiDisk->vendorId[count + 1] = '\0';
			break;
		}
		else if (!count)
		{
			scsiDisk->vendorId[0] = '\0';
		}
	}

	strncpy(scsiDisk->productId, inquiryData.productId, 16);
	scsiDisk->productId[16] = '\0';
	for (count = 15; count >= 0; count --)
	{
		if (scsiDisk->productId[count] != ' ')
		{
			scsiDisk->productId[count + 1] = '\0';
			break;
		}
		else if (!count)
		{
			scsiDisk->productId[0] = '\0';
		}
	}
	snprintf(scsiDisk->vendorProductId, 26, "%s%s%s", scsiDisk->vendorId,
		(scsiDisk->vendorId[0]? " " : ""), scsiDisk->productId);

	for (retries = 0; retries < 50; retries ++)
	{
		status = scsiTestUnitReady(scsiDisk, 0);
		if (status < 0)
		{
			if (scsiRequestSense(scsiDisk, 0, &senseData) >= 0)
			{
				kernelError(kernel_error, "SCSI sense error key=0x%02x "
					"asc=0x%02x ascq=0x%02x", (senseData.flagsKey & 0x0F),
					senseData.addlCode, senseData.addlCodeQual);
			}
		}
		else
		{
			break;
		}

		kernelCpuSpinMs(250);
	}

	if (status < 0)
		goto err_out;

	// Spin up the new target by sending 'start unit' command
	status = scsiStartStopUnit(scsiDisk, 0, 1, 0);
	if (status < 0)
		goto err_out;

	// Send a 'read capacity' command
	status = scsiReadCapacity(scsiDisk, 0, &capacityData);
	if (status < 0)
		goto err_out;

	scsiDisk->numSectors = (capacityData.blockNumber + 1);
	scsiDisk->sectorSize = capacityData.blockLength;

	if ((scsiDisk->sectorSize <= 0) || (scsiDisk->sectorSize > 4096))
	{
		kernelError(kernel_error, "Unsupported sector size %u",
			scsiDisk->sectorSize);
		goto err_out;
	}

	kernelDebug(debug_scsi, "SCSI disk \"%s\" sectors %u sectorsize %u",
		scsiDisk->vendorProductId, scsiDisk->numSectors, scsiDisk->sectorSize);

	physicalDisk->deviceNumber = getNewDiskNumber();
	kernelDebug(debug_scsi, "SCSI disk %d detected",
		physicalDisk->deviceNumber);
	physicalDisk->description = scsiDisk->vendorProductId;
	physicalDisk->type |= (DISKTYPE_PHYSICAL | DISKTYPE_SCSIDISK);
	physicalDisk->flags = DISKFLAG_MOTORON;
	physicalDisk->numSectors = scsiDisk->numSectors;
	guessDiskGeom(physicalDisk);
	physicalDisk->sectorSize = scsiDisk->sectorSize;
	physicalDisk->driverData = (void *) scsiDisk;
	physicalDisk->driver = driver;
	disks[numDisks++] = physicalDisk;

	// Set up the kernelDevice
	scsiDisk->dev.device.class = kernelDeviceGetClass(DEVICECLASS_DISK);
	scsiDisk->dev.device.subClass =
		kernelDeviceGetClass(DEVICESUBCLASS_DISK_SCSI);
	if (scsiDisk->usb.usbDev)
		kernelUsbSetDeviceAttrs(scsiDisk->usb.usbDev, 0, &scsiDisk->dev);
	else
		kernelVariableListCreate(&scsiDisk->dev.device.attrs);
	kernelVariableListSet((variableList *) &scsiDisk->dev.device.attrs,
		DEVICEATTRNAME_VENDOR, scsiDisk->vendorId);
	kernelVariableListSet((variableList *) &scsiDisk->dev.device.attrs,
		DEVICEATTRNAME_MODEL, scsiDisk->productId);
	scsiDisk->dev.driver = driver;
	scsiDisk->dev.data = (void *) physicalDisk;

	// Tell the bus that we're claiming this device.
	kernelBusDeviceClaim(scsiDisk->busTarget, driver);

	// Register the disk
	status = kernelDiskRegisterDevice(&scsiDisk->dev);
	if (status < 0)
		goto err_out;

	// Add the kernel device
	status = kernelDeviceAdd(parent, &scsiDisk->dev);
	if (status < 0)
		goto err_out;

	return (physicalDisk);

err_out:

	kernelError(kernel_error, "Error detecting %sSCSI disk",
		((scsiDisk && scsiDisk->busTarget && scsiDisk->busTarget->bus &&
			scsiDisk->busTarget->bus->type == bus_usb)? "USB " : ""));

	if (physicalDisk)
		kernelFree((void *) physicalDisk);

	if (scsiDisk)
	{
		if (scsiDisk->busTarget)
			kernelFree(scsiDisk->busTarget);

		kernelFree(scsiDisk);
	}

	return (physicalDisk = NULL);
}


static kernelPhysicalDisk *findBusTarget(kernelBusType busType, int target)
{
	// Try to find a disk in our list.

	kernelScsiDisk *scsiDisk = NULL;
	int count;

	for (count = 0; count < numDisks; count ++)
	{
		if (disks[count] && disks[count]->driverData)
		{
			scsiDisk = (kernelScsiDisk *) disks[count]->driverData;

			if (scsiDisk->busTarget && scsiDisk->busTarget->bus &&
				(scsiDisk->busTarget->bus->type == busType) &&
				(scsiDisk->busTarget->id == target))
			{
				return (disks[count]);
			}
		}
	}

	// Not found
	return (NULL);
}


static void removeDisk(kernelPhysicalDisk *physicalDisk)
{
	// Remove a disk from our list.

	int position = -1;
	int count;

	// Find its position
	for (count = 0; count < numDisks; count ++)
	{
		if (disks[count] == physicalDisk)
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


static kernelScsiDisk *findDiskByNumber(int driveNum)
{
	int count = 0;

	for (count = 0; count < numDisks; count ++)
	{
		if (disks[count]->deviceNumber == driveNum)
			return ((kernelScsiDisk *) disks[count]->driverData);
	}

	// Not found
	return (NULL);
}


static int readWriteSectors(int driveNum, uquad_t logicalSector,
	uquad_t numSectors, void *buffer, int read)
{
	// Read or write sectors.

	int status = 0;
	kernelScsiDisk *scsiDisk = NULL;

	// Check params
	if (!buffer)
	{
		kernelError(kernel_error, "NULL parameter");
		return (status = ERR_NULLPARAMETER);
	}

	if (!numSectors)
		// Not an error we guess, but nothing to do
		return (status = 0);

	// Find the disk based on the disk number
	scsiDisk = findDiskByNumber(driveNum);
	if (!scsiDisk)
	{
		kernelError(kernel_error, "No such disk, device number %d", driveNum);
		return (status = ERR_NOSUCHENTRY);
	}

	// Send a 'test unit ready' command
	status = scsiTestUnitReady(scsiDisk, 0);
	if (status < 0)
		return (status);

	kernelDebug(debug_scsi, "SCSI %s %llu sectors on \"%s\" at %llu sectorsize "
		"%u", (read? "read" : "write"), numSectors, scsiDisk->vendorProductId,
		logicalSector, scsiDisk->sectorSize);

	status = scsiReadWrite(scsiDisk, 0, logicalSector, numSectors, buffer,
		read);

	return (status);
}


static int driverReadSectors(int driveNum, uquad_t logicalSector,
	uquad_t numSectors, void *buffer)
{
	// This function is a wrapper for the readWriteSectors function.

	kernelDebug(debug_scsi, "SCSI driveNum %d read %llu sectors at %llu",
		driveNum, numSectors, logicalSector);

	return (readWriteSectors(driveNum, logicalSector, numSectors, buffer,
		1));  // Read operation
}


static int driverWriteSectors(int driveNum, uquad_t logicalSector,
	uquad_t numSectors, const void *buffer)
{
	// This function is a wrapper for the readWriteSectors function.

	kernelDebug(debug_scsi, "SCSI driveNum %d write %llu sectors at %llu",
		driveNum, numSectors, logicalSector);

	return (readWriteSectors(driveNum, logicalSector, numSectors,
		(void *) buffer, 0));  // Write operation
}


static int driverDetect(void *parent __attribute__((unused)),
	kernelDriver *driver)
{
	// Try to detect SCSI disks.

	int status = 0;
	kernelBusTarget *busTargets = NULL;
	int numBusTargets = 0;
	int deviceCount = 0;
	usbDevice usbDev;

	kernelDebug(debug_scsi, "SCSI search for devices");

	// Search the USB bus(es) for devices
	numBusTargets = kernelBusGetTargets(bus_usb, &busTargets);
	if (numBusTargets > 0)
	{
		// Search the bus targets for SCSI disk devices
		for (deviceCount = 0; deviceCount < numBusTargets; deviceCount ++)
		{
			// Try to get the USB information about the target
			status = kernelBusGetTargetInfo(&busTargets[deviceCount],
				(void *) &usbDev);
			if (status < 0)
				continue;

			// If the USB class is 0x08 and the subclass is 0x06 then we
			// believe we have a SCSI device
			if ((usbDev.classCode != 0x08) || (usbDev.subClassCode != 0x06) ||
				(usbDev.protocol != 0x50))
			{
				continue;
			}

			// Already claimed?
			if (busTargets[deviceCount].claimed)
				continue;

			kernelDebug(debug_scsi, "SCSI found possible USB mass storage "
				"device");
			detectTarget(usbDev.controller->dev, bus_usb,
				busTargets[deviceCount].id, driver);
		}

		kernelFree(busTargets);
	}

	return (status = 0);
}


static int driverHotplug(void *parent, int busType, int target, int connected,
	kernelDriver *driver)
{
	// This function is used to detect whether a newly-connected, hotplugged
	// device is supported by this driver during runtime, and if so to do the
	// appropriate device setup and registration.  Alternatively if the device
	// is disconnected a call to this function lets us know to stop trying
	// to communicate with it.

	int status = 0;
	kernelPhysicalDisk *physicalDisk = NULL;
	kernelDisk *logicalDisk = NULL;
	kernelScsiDisk *scsiDisk = NULL;
	int count;

	kernelDebug(debug_scsi, "SCSI device hotplug %sconnection",
		(connected? "" : "dis"));

	if (connected)
	{
		// Determine whether any new SCSI disks have appeared on the USB bus
		physicalDisk = detectTarget(parent, busType, target, driver);
		if (physicalDisk)
			kernelDiskReadPartitions((char *) physicalDisk->name);
	}
	else
	{
		// Try to find the disk in our list
		physicalDisk = findBusTarget(busType, target);
		if (!physicalDisk)
		{
			// This can happen if SCSI initialization did not complete
			// successfully.  In that case, it could be that we're still the
			// registered driver for the device, but we never added it to our
			// list.
			kernelDebugError("No such SCSI device 0x%08x", target);
			return (status = ERR_NOSUCHENTRY);
		}

		// Found it.
		kernelDebug(debug_scsi, "SCSI device removed");

		// If there are filesystems mounted on this disk, remove them
		for (count = 0; count < physicalDisk->numLogical; count ++)
		{
			logicalDisk = &physicalDisk->logical[count];

			if (logicalDisk->filesystem.mounted)
			{
				kernelDebug(debug_scsi, "SCSI unmount %s", logicalDisk->name);
				kernelFilesystemRemoved((char *)
					logicalDisk->filesystem.mountPoint);
			}
		}

		scsiDisk = (kernelScsiDisk *) physicalDisk->driverData;

		// Remove it from the system's disks
		kernelDebug(debug_scsi, "SCSI remove %s", physicalDisk->name);
		kernelDiskRemoveDevice(&scsiDisk->dev);

		// Remove it from the device tree
		kernelDebug(debug_scsi, "SCSI remove device");
		kernelDeviceRemove(&scsiDisk->dev);

		// Delete.
		removeDisk(physicalDisk);

		if (scsiDisk->busTarget)
			kernelFree(scsiDisk->busTarget);

		kernelFree(scsiDisk);
	}

	return (status = 0);
}


static kernelDiskOps scsiOps = {
	NULL,	// driverSetMotorState
	NULL,	// driverSetLockState
	NULL,	// driverSetDoorState
	NULL,	// driverMediaPresent
	NULL,	// driverMediaChanged
	driverReadSectors,
	driverWriteSectors,
	NULL	// driverFlush
};


/////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////
//
// Below here, the functions are exported for external use
//
/////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////

void kernelScsiDiskDriverRegister(kernelDriver *driver)
{
	// Device driver registration.

	driver->driverDetect = driverDetect;
	driver->driverHotplug = driverHotplug;
	driver->ops = &scsiOps;

	return;
}

