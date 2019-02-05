//
//  Visopsys
//  Copyright (C) 1998-2006 J. Andrew McLaughlin
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
//  59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
//
//  kernelScsiDiskDriver.c
//

// Driver for standard and USB SCSI disks

#include "kernelScsiDiskDriver.h"
#include "kernelScsiDriver.h"
#include "kernelDebug.h"
#include "kernelDisk.h"
#include "kernelError.h"
#include "kernelFilesystem.h"
#include "kernelLog.h"
#include "kernelMalloc.h"
#include "kernelMisc.h"
#include "kernelProcessorX86.h"
#include "kernelRandom.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static kernelPhysicalDisk *disks[SCSI_MAX_DISKS];
static int numDisks = 0;


static int usbClearHalt(kernelScsiDisk *dsk, unsigned char endpoint)
{
  usbTransaction usbTrans;

  // Set up the USB transaction to send the 'clear (halt) feature' to the
  // endpoint
  kernelMemClear((void *) &usbTrans, sizeof(usbTrans));
  usbTrans.type = usbxfer_control;
  usbTrans.address = dsk->usb.usbDev.address;
  usbTrans.control.request = USB_CLEAR_FEATURE;
  usbTrans.control.value = USB_FEATURE_ENDPOINTHALT;
  usbTrans.control.index = endpoint;

  // Write the command
  kernelDebug(debug_scsi, "USB mass storage clear halt");
  return (kernelBusWrite(dsk->busType, dsk->target, (void *) &usbTrans));
}


static int usbMassStorageReset(kernelScsiDisk *dsk)
{
  // Send the USB "mass storage reset" command to the first interface of
  // the device

  int status = 0;
  usbTransaction usbTrans;

  // Set up the USB transaction to send the reset command
  kernelMemClear((void *) &usbTrans, sizeof(usbTrans));
  usbTrans.type = usbxfer_control;
  usbTrans.address = dsk->usb.usbDev.address;
  usbTrans.control.requestType =
    (USB_DEVREQTYPE_CLASS | USB_DEVREQTYPE_INTERFACE);
  usbTrans.control.request = USB_MASSSTORAGE_RESET;
  usbTrans.control.index = dsk->usb.usbDev.interDesc[0]->interNum;

  // Write the command
  kernelDebug(debug_scsi, "USB mass storage reset");
  status = kernelBusWrite(dsk->busType, dsk->target, (void *) &usbTrans);
  if (status < 0)
    return (status);

  usbClearHalt(dsk, dsk->usb.bulkOutEndpoint);

  kernelDebug(debug_scsi, "USB mass storage reset complete");
  return (status = 0);
}


static int usbScsiCommand(kernelScsiDisk *dsk, unsigned char lun, void *cmd,
			  unsigned char cmdLength, void *data,
			  unsigned dataLength, unsigned *bytes, int read)
{
  // Wrap a SCSI command in a USB command block wrapper and send it to
  // the device.
  
  int status = 0;
  usbCmdBlockWrapper cmdWrapper;
  usbCmdStatusWrapper statusWrapper;
  usbTransaction usbTrans;

  kernelDebug(debug_scsi, "USB mass storage command %02x datalength %d",
	      ((scsiCmd6 *) cmd)->byte[0], dataLength);

  // Set up the command wrapper
  kernelMemClear(&cmdWrapper, sizeof(usbCmdBlockWrapper));
  cmdWrapper.signature = USB_CMDBLOCKWRAPPER_SIG;
  cmdWrapper.tag = kernelRandomUnformatted();
  cmdWrapper.dataLength = dataLength;
  cmdWrapper.flags = (read << 7);
  cmdWrapper.lun = lun;
  cmdWrapper.cmdLength = cmdLength;
  // Copy the command data into the wrapper
  kernelMemCopy(cmd, cmdWrapper.cmd, cmdLength);

  // Set up the USB transaction to send the command
  kernelMemClear((void *) &usbTrans, sizeof(usbTrans));
  usbTrans.type = usbxfer_bulk;
  usbTrans.address = dsk->usb.usbDev.address;
  usbTrans.endpoint = dsk->usb.bulkOutEndpoint;
  usbTrans.pid = USB_PID_OUT;
  usbTrans.length = sizeof(usbCmdBlockWrapper);
  usbTrans.buffer = &cmdWrapper;

  // Write the command
  kernelDebug(debug_scsi, "USB mass storage write command length %d",
	      cmdWrapper.cmdLength);
  status = kernelBusWrite(dsk->busType, dsk->target, (void *) &usbTrans);
  if (status < 0)
    return (status);

  if (dataLength)
    {
      if (bytes)
	*bytes = 0;

      // Set up the USB transaction to read or write the data

      kernelMemClear((void *) &usbTrans, sizeof(usbTrans));
      usbTrans.type = usbxfer_bulk;
      usbTrans.address = dsk->usb.usbDev.address;
      usbTrans.length = dataLength;
      usbTrans.buffer = data;

      if (read)
	{
	  usbTrans.endpoint = dsk->usb.bulkInEndpoint;
	  usbTrans.pid = USB_PID_IN;
	}
      else
	{
	  usbTrans.endpoint = dsk->usb.bulkOutEndpoint;
	  usbTrans.pid = USB_PID_OUT;
	}

      // Write the command
      kernelDebug(debug_scsi, "USB mass storage data %s %d bytes to %p",
		  (read? "read" : "write"), dataLength, data);
      status = kernelBusWrite(dsk->busType, dsk->target, (void *) &usbTrans);
      if ((status < 0) && !usbTrans.bytes)
	return (status);

      /* Eh?!?!  I don't think we really want this, but don't delete the code
	 for a little while.
      if (usbTrans.bytes < usbTrans.length)
	{
	  if (read)
	    dsk->usb.bulkIn->maxPacketSize = usbTrans.bytes;
	  else
	    dsk->usb.bulkOut->maxPacketSize = usbTrans.bytes;
	}
      */

      if (bytes)
	*bytes = (unsigned) usbTrans.bytes;
    }

  // Now read the status
  kernelMemClear((void *) &usbTrans, sizeof(usbTrans));
  usbTrans.type = usbxfer_bulk;
  usbTrans.address = dsk->usb.usbDev.address;
  usbTrans.endpoint = dsk->usb.bulkInEndpoint;
  usbTrans.pid = USB_PID_IN;
  usbTrans.length = sizeof(usbCmdStatusWrapper);
  usbTrans.buffer = &statusWrapper;

  // Write the command
  kernelDebug(debug_scsi, "USB mass storage read status");
  status = kernelBusWrite(dsk->busType, dsk->target, (void *) &usbTrans);
  if (status < 0)
    return (status);

  if (!(statusWrapper.status & SCSI_STAT_MASK))
    kernelDebug(debug_scsi, "USB mass storage command successful");
  else
    kernelDebug(debug_scsi, "USB mass storage command error status %02x",
		(statusWrapper.status & SCSI_STAT_MASK));

  return (status = 0);
}


static void debugInquiry(scsiInquiryData *inquiryData)
{
  char vendorId[9];
  char productId[17];

  strncpy(vendorId, inquiryData->vendorId, 8);
  vendorId[8] = '\0';
  strncpy(productId, inquiryData->productId, 16);
  productId[16] = '\0';

  kernelDebug(debug_scsi, "Debug inquiry data:\n"
	      "    byte0=%02x\n"
	      "    byte1=%02x\n"
	      "    byte2=%02x\n"
	      "    byte3=%02x\n"
	      "    byte4=%02x\n"
	      "    byte7=%02x\n"
	      "    vendorId=%s\n"
	      "    productId=%s\n"
	      "    productRev=%08x", inquiryData->byte0.periQual,
	      inquiryData->byte1.removable, inquiryData->byte2.isoVersion,
	      inquiryData->byte3.aenc, inquiryData->byte4.addlLength,
	      inquiryData->byte7.relAdr, vendorId, productId,
	      inquiryData->productRev);
}


static int scsiInquiry(kernelScsiDisk *dsk, unsigned char lun,
		       scsiInquiryData *inquiryData)
{
  // Do a SCSI 'inquiry' command.

  int status = 0;
  scsiCmd6 cmd6;
  unsigned bytes = 0;

  if (dsk->busType == bus_usb)
    {
      // Set up the USB transaction, with the SCSI 'inquiry' command.
      kernelDebug(debug_scsi, "USB mass storage SCSI inquiry");
      kernelMemClear(&cmd6, sizeof(scsiCmd6));
      cmd6.byte[0] = SCSI_CMD_INQUIRY;
      cmd6.byte[1] = (lun << 5);
      cmd6.byte[4] = sizeof(scsiInquiryData);

      status = usbScsiCommand(dsk, lun, &cmd6, sizeof(scsiCmd6), inquiryData,
			      sizeof(scsiInquiryData), &bytes, 1);
      if ((status < 0) && (bytes < 36))
	return (status);

      debugInquiry(inquiryData);
    }
  else
    return (status = ERR_NOTIMPLEMENTED);

  return (status = 0);
}


/*
static int scsiModeSense(kernelScsiDisk *dsk, unsigned char lun,
			 scsiModeSenseData *modeSenseData)
{
  // Do a SCSI 'mode sense' command.

  int status = 0;
  scsiCmd6 cmd6;
  unsigned bytes = 0;

  if (dsk->busType == bus_usb)
    {
      // Set up the USB transaction, with the SCSI 'mode sense' command.
      kernelDebug(debug_scsi, "USB mass storage SCSI mode sense");
      kernelMemClear(&cmd6, sizeof(scsiCmd6));
      cmd6.byte[0] = SCSI_CMD_MODESENSE6;
      cmd6.byte[1] = (lun << 5);
      cmd6.byte[4] = sizeof(scsiModeSenseData);

      status = usbScsiCommand(dsk, lun, &cmd6, sizeof(scsiCmd6), modeSenseData,
			      dsk->usb.bulkIn->maxPacketSize, &bytes, 1);
      if ((status < 0) && (bytes < sizeof(scsiModeSenseData)))
	return (status);
    }
  else
    return (status = ERR_NOTIMPLEMENTED);

  return (status = 0);
}
*/


static int scsiRead(kernelScsiDisk *dsk, unsigned char lun,
		    unsigned logicalSector, unsigned short numSectors,
		    void *buffer)
{
  // Do a SCSI 'read' command

  int status = 0;
  unsigned dataLength = 0;
  scsiCmd10 cmd10;
  unsigned bytes = 0;

  dataLength = (numSectors * dsk->sectorSize);
  kernelDebug(debug_scsi, "Read %u bytes sectorsize %u", dataLength,
	      dsk->sectorSize);

  if (dsk->busType == bus_usb)
    {
      // Set up the USB transaction, with the SCSI 'read' command.
      kernelDebug(debug_scsi, "USB mass storage read");
      kernelMemClear(&cmd10, sizeof(scsiCmd10));
      cmd10.byte[0] = SCSI_CMD_READ10;
      cmd10.byte[1] = (lun << 5);
      *((unsigned *) &cmd10.byte[2]) = kernelProcessorSwap32(logicalSector);
      *((unsigned short *) &cmd10.byte[7]) = kernelProcessorSwap16(numSectors);
      
      status = usbScsiCommand(dsk, lun, &cmd10, sizeof(scsiCmd10), buffer,
			      dataLength, &bytes, 1);
      if ((status < 0) && (bytes < dataLength))
	return (status);
    }
  else
    return (status = ERR_NOTIMPLEMENTED);

  kernelDebug(debug_scsi, "Read successul %u bytes", bytes);
  return (status = 0);
}


static int scsiReadCapacity(kernelScsiDisk *dsk, unsigned char lun,
			    scsiCapacityData *capacityData)
{
  // Do a SCSI 'read capacity' command.

  int status = 0;
  scsiCmd10 cmd10;
  unsigned bytes = 0;

  if (dsk->busType == bus_usb)
    {
      // Set up the USB transaction, with the SCSI 'read capacity' command.
      kernelDebug(debug_scsi, "USB mass storage SCSI read capacity");
      kernelMemClear(&cmd10, sizeof(scsiCmd10));
      cmd10.byte[0] = SCSI_CMD_READCAPACITY;
      cmd10.byte[1] = (lun << 5);

      status =
	usbScsiCommand(dsk, lun, &cmd10, sizeof(scsiCmd10), capacityData,
		       sizeof(scsiCapacityData), &bytes, 1);
      if ((status < 0) && (bytes < sizeof(scsiCapacityData)))
	return (status);
    }
  else
    return (status = ERR_NOTIMPLEMENTED);

  // Swap bytes around
  capacityData->blockNumber = kernelProcessorSwap32(capacityData->blockNumber);
  capacityData->blockLength = kernelProcessorSwap32(capacityData->blockLength);

  return (status = 0);
}


static int scsiStartStopUnit(kernelScsiDisk *dsk, unsigned char lun,
			     unsigned char startStop, unsigned char loadEject)
{
  // Do a SCSI 'start/stop unit' command.

  int status = 0;
  scsiCmd6 cmd6;

  if (dsk->busType == bus_usb)
    {
      // Set up the USB transaction, with the SCSI 'start/stop unit' command.
      kernelDebug(debug_scsi, "USB mass storage SCSI %s unit",
		  (startStop? "start" : "stop"));
      kernelMemClear(&cmd6, sizeof(scsiCmd6));
      cmd6.byte[0] = SCSI_CMD_STARTSTOPUNIT;
      cmd6.byte[1] = (lun << 5);
      cmd6.byte[4] = (((loadEject & 0x01) << 1) | (startStop & 0x01));

      status =
	usbScsiCommand(dsk, lun, &cmd6, sizeof(scsiCmd6), NULL, 0, NULL, 0);
      if (status < 0)
	return (status);
    }
  else
    return (status = ERR_NOTIMPLEMENTED);

  return (status = 0);
}


static int scsiTestUnitReady(kernelScsiDisk *dsk, unsigned char lun)
{
  // Do a SCSI 'test unit ready' command.

  int status = 0;
  scsiCmd6 cmd6;

  if (dsk->busType == bus_usb)
    {
      // Set up the USB transaction, with the SCSI 'test unit ready' command.
      kernelDebug(debug_scsi, "USB mass storage SCSI test unit ready");
      kernelMemClear(&cmd6, sizeof(scsiCmd6));
      cmd6.byte[0] = SCSI_CMD_TESTUNITREADY;
      cmd6.byte[1] = (lun << 5);

      status =
	usbScsiCommand(dsk, lun, &cmd6, sizeof(scsiCmd6), NULL, 0, NULL, 0);
      if (status < 0)
	return (status);
    }
  else
    return (status = ERR_NOTIMPLEMENTED);

  return (status = 0);
}


static int scsiWrite(kernelScsiDisk *dsk, unsigned char lun,
		     unsigned logicalSector, unsigned short numSectors,
		     void *buffer)
{
  // Do a SCSI 'write' command

  int status = 0;
  unsigned dataLength = 0;
  scsiCmd10 cmd10;
  unsigned bytes = 0;

  dataLength = (numSectors * dsk->sectorSize);
  kernelDebug(debug_scsi, "Write %u bytes sectorsize %u", dataLength,
	      dsk->sectorSize);

  if (dsk->busType == bus_usb)
    {
      // Set up the USB transaction, with the SCSI 'write' command.
      kernelDebug(debug_scsi, "USB mass storage write");
      kernelMemClear(&cmd10, sizeof(scsiCmd10));
      cmd10.byte[0] = SCSI_CMD_WRITE10;
      cmd10.byte[1] = (lun << 5);
      *((unsigned *) &cmd10.byte[2]) = kernelProcessorSwap32(logicalSector);
      *((unsigned short *) &cmd10.byte[7]) = kernelProcessorSwap16(numSectors);
      
      status = usbScsiCommand(dsk, lun, &cmd10, sizeof(scsiCmd10), buffer,
			      dataLength, &bytes, 0);
      if ((status < 0) && (bytes < dataLength))
	return (status);
    }
  else
    return (status = ERR_NOTIMPLEMENTED);

  kernelDebug(debug_scsi, "Write successul %u bytes", bytes);
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


static kernelPhysicalDisk *detectTarget(void *parent, int busType, int target,
					void *driver)
{
  // Given a bus type and a bus target number, see if the device is a
  // SCSI disk

  int status = 0;
  kernelDevice *dev = NULL;
  kernelPhysicalDisk *physicalDisk = NULL;
  kernelScsiDisk *dsk = NULL;
  scsiInquiryData inquiryData;
  scsiCapacityData capacityData;
  //scsiModeSenseData modeSenseData;
  int count;

  dev = kernelMalloc(sizeof(kernelDevice));
  if (dev == NULL)
    goto err_out;

  physicalDisk = kernelMalloc(sizeof(kernelPhysicalDisk));
  if (physicalDisk == NULL)
    goto err_out;

  dsk = kernelMalloc(sizeof(kernelScsiDisk));
  if (dsk == NULL)
    goto err_out;

  dsk->busType = busType;
  dsk->target = target;
  dsk->dev = dev;

  if (dsk->busType == bus_usb)
    {
      // Try to get the USB information about the target
      status =
	kernelBusGetTargetInfo(busType, target, (void *) &(dsk->usb.usbDev));
      if (status < 0)
	goto err_out;

      // If the USB class is 0x08 and the subclass is 0x06 then we believe
      // we have a SCSI device
      if ((dsk->usb.usbDev.classCode != 0x08) ||
	  (dsk->usb.usbDev.subClassCode != 0x06))
	goto err_out;

      // Record the bulk-in and bulk-out endpoints
      for (count = 0; count < dsk->usb.usbDev.interDesc[0]->numEndpoints;
	   count ++)
	{
	  if (dsk->usb.usbDev.endpointDesc[count]->attributes != 0x02)
	    continue;

	  if (!dsk->usb.bulkInEndpoint &&
	      (dsk->usb.usbDev.endpointDesc[count]->endpntAddress & 0x80))
	    {
	      dsk->usb.bulkIn = dsk->usb.usbDev.endpointDesc[count];
	      dsk->usb.bulkInEndpoint = (dsk->usb.bulkIn->endpntAddress & 0xF);
	    }

	  if (!dsk->usb.bulkOutEndpoint &&
	      (!(dsk->usb.usbDev.endpointDesc[count]->endpntAddress & 0x80)))
	    {
	      dsk->usb.bulkOut = dsk->usb.usbDev.endpointDesc[count];
	      dsk->usb.bulkOutEndpoint =
		(dsk->usb.bulkOut->endpntAddress & 0xF);
	    }
	}

      kernelDebug(debug_scsi, "USB SCSI device detected");
      physicalDisk->flags |= DISKFLAG_FLASHDISK;

      status = usbMassStorageReset(dsk);
      if (status < 0)
	goto err_out;
    }

  // Try to communicate with the new target by sending 'start unit' command
  status = scsiStartStopUnit(dsk, 0, 1, 0);
  if (status < 0)
    {
      // Try a reset and then try again

      if (dsk->busType == bus_usb)
	{
	  status = usbMassStorageReset(dsk);
	  if (status < 0)
	    goto err_out;
	}

      status = scsiStartStopUnit(dsk, 0, 1, 0);
      if (status < 0)
	goto err_out;
    }

  // Send a 'test unit ready' command
  status = scsiTestUnitReady(dsk, 0);
  if (status < 0)
    goto err_out;

  // Send an 'inquiry' command
  status = scsiInquiry(dsk, 0, &inquiryData);
  if (status < 0)
    goto err_out;

  if (inquiryData.byte1.removable & 0x80)
    physicalDisk->flags |= DISKFLAG_REMOVABLE;

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
      else if (count == 0)
	dsk->vendorId[0] = '\0';
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
      else if (count == 0)
	dsk->productId[0] = '\0';
    }
  snprintf(dsk->vendorProductId, 26, "%s%s%s", dsk->vendorId,
	   (dsk->vendorId[0]? " " : ""), dsk->productId);

  // Send a 'read capacity' command
  status = scsiReadCapacity(dsk, 0, &capacityData);
  if (status < 0)
    goto err_out;

  dsk->numSectors = capacityData.blockNumber;
  dsk->sectorSize = capacityData.blockLength;

  if ((dsk->sectorSize <= 0) || (dsk->sectorSize > 4096))
    {
      kernelError(kernel_error, "Unsupported sector size %u", dsk->sectorSize);
      goto err_out;
    }

  kernelDebug(debug_scsi, "Disk \"%s\" sectors %u sectorsize %u ptr %p",
	      dsk->vendorProductId, dsk->numSectors, dsk->sectorSize, dsk);

  /*
  // Send a 'mode sense' command
  status = scsiModeSense(dsk, 0, &modeSenseData);
  if (status < 0)
    goto err_out;
  */

  physicalDisk->deviceNumber = getNewDiskNumber();
  sprintf((char *) physicalDisk->name, "sd%d", physicalDisk->deviceNumber);
  kernelDebug(debug_scsi, "Disk %s detected, number %d", physicalDisk->name,
	      physicalDisk->deviceNumber);
  physicalDisk->description = dsk->vendorProductId;
  physicalDisk->flags |= (DISKFLAG_PHYSICAL | DISKFLAG_SCSIDISK);
  physicalDisk->numSectors = dsk->numSectors;
  physicalDisk->sectorSize = dsk->sectorSize;
  physicalDisk->motorState = 1;
  physicalDisk->driverData = (void *) dsk;
  physicalDisk->driver = driver;
  // next two added by Davide Airaghi                                                                                                                                          
  physicalDisk->skip_cache = 0; 
  physicalDisk->extra = NULL;
  
  disks[numDisks++] = physicalDisk;

  dev->device.class = kernelDeviceGetClass(DEVICECLASS_DISK);
  dev->device.subClass = kernelDeviceGetClass(DEVICESUBCLASS_DISK_SCSI);
  kernelVariableListCreate(&(dev->device.attrs));
  kernelVariableListSet(&(dev->device.attrs), DEVICEATTRNAME_MODEL,
			dsk->vendorId);
  kernelVariableListSet(&(dev->device.attrs), DEVICEATTRNAME_MODEL,
			dsk->productId);
  dev->driver = driver;
  dev->data = (void *) physicalDisk;

  status = kernelDiskRegisterDevice(dev);
  if (status < 0)
    goto err_out;

  status = kernelDeviceAdd(parent, dev);
  if (status < 0)
    goto err_out;

  return (physicalDisk);

 err_out:
  if (dev)
    kernelFree(dev);
  if (physicalDisk)
    kernelFree((void *) physicalDisk);
  if (dsk)
    kernelFree(dsk);
  return (physicalDisk = NULL);
}


static kernelPhysicalDisk *findBusTarget(kernelBusType busType, int target)
{
  // Try to find a disk in our list.
  
  kernelScsiDisk *dsk = NULL;
  int count;

  for (count = 0; count < numDisks; count ++)
    {
      dsk = (kernelScsiDisk *) disks[count]->driverData;

      if ((dsk->busType == busType) && (dsk->target == target))
	return (disks[count]);
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


static int readWriteSectors(int driveNum, unsigned logicalSector,
			    unsigned numSectors, void *buffer, int read)
{
  // Read or write sectors.

  int status = 0;
  kernelScsiDisk *dsk = NULL;

  // Check params
  if (buffer == NULL)
    {
      kernelError(kernel_error, "NULL buffer parameter");
      return (status = ERR_NULLPARAMETER);
    }

  if (numSectors == 0)
    // Not an error we guess, but nothing to do
    return (status = 0);

  // Find the disk based on the disk number
  dsk = findDiskByNumber(driveNum);
  if (dsk == NULL)
    {
      kernelError(kernel_error, "No such disk, device number %d", driveNum);
      return (status = ERR_NOSUCHENTRY);
    }

  kernelDebug(debug_scsi, "%s %u sectors to %p on \"%s\" at %u sectorsize %u "
	      "ptr %p", (read? "read" : "write"), numSectors, buffer,
	      dsk->vendorProductId, logicalSector, dsk->sectorSize, dsk);

  if (read)
    status = scsiRead(dsk, 0, logicalSector, numSectors, buffer);
  else
    status = scsiWrite(dsk, 0, logicalSector, numSectors, buffer);

  if (status < 0)
    return (status);

  return (status = 0);
}


static int driverReadSectors(int driveNum, unsigned logicalSector,
			     unsigned numSectors, void *buffer)
{
  // This routine is a wrapper for the readWriteSectors routine.
  return (readWriteSectors(driveNum, logicalSector, numSectors, buffer,
			   1));  // Read operation
}


static int driverWriteSectors(int driveNum, unsigned logicalSector,
			      unsigned numSectors, const void *buffer)
{
  // This routine is a wrapper for the readWriteSectors routine.
  return (readWriteSectors(driveNum, logicalSector, numSectors,
			   (void *) buffer, 0));  // Write operation
}


static int driverDetect(void *parent __attribute__((unused)),
			kernelDriver *driver)
{
  // Try to detect SCSI disks

  int status = 0;
  kernelDevice *usbDev = NULL;
  kernelBusTarget *busTargets = NULL;
  int numBusTargets = 0;
  int deviceCount = 0;

  // Look for USB SCSI disks
  status = kernelDeviceFindType(kernelDeviceGetClass(DEVICECLASS_BUS),
				kernelDeviceGetClass(DEVICESUBCLASS_BUS_USB),
				&usbDev, 1);
  if (status > 0)
    {
      // Search the USB bus(es) for devices
      numBusTargets = kernelBusGetTargets(bus_usb, &busTargets);
      if (numBusTargets <= 0)
	return (status = 0);

      // Search the bus targets for SCSI disk devices
      for (deviceCount = 0; deviceCount < numBusTargets; deviceCount ++)
	{
	  // If it's not a SCSI disk device, skip it
	  if ((busTargets[deviceCount].class == NULL) ||
	      (busTargets[deviceCount].class->class != DEVICECLASS_DISK) ||
	      (busTargets[deviceCount].subClass == NULL) ||
	      (busTargets[deviceCount].subClass->class !=
	       DEVICESUBCLASS_DISK_SCSI))
	    continue;

	  detectTarget(usbDev, bus_usb, busTargets[deviceCount].target,
		       driver);
	}

      kernelFree(busTargets);
    }

  return (status = 0);
}


static int driverHotplug(void *parent, int busType, int target, int connected,
			 kernelDriver *driver)
{
  // This routine is used to detect whether a newly-connected, hotplugged
  // device is supported by this driver during runtime, and if so to do the
  // appropriate device setup and registration.  Alternatively if the device
  // is disconnected a call to this function lets us know to stop trying
  // to communicate with it.

  int status = 0;
  kernelPhysicalDisk *physicalDisk = NULL;
  kernelDevice *dev = NULL;
  int count;

  if (connected)
    {
      // Determine whether any new SCSI disks have appeared on the USB bus
      physicalDisk = detectTarget(parent, busType, target, driver);
      if (physicalDisk == NULL)
	return (status = 0);

      kernelDiskReadPartitions((char *) physicalDisk->name);
    }
  else
    {
      // Try to find the disk in our list

      physicalDisk = findBusTarget(busType, target);
      if (physicalDisk == NULL)
	return (status = ERR_NOSUCHENTRY);

      kernelDebug(debug_scsi, "USB SCSI device removed");

      // If there are filesystems mounted on this disk, try to unmount them
      for (count = 0; count < physicalDisk->numLogical; count ++)
	{
	  if (physicalDisk->logical[count].filesystem.mounted)
	    kernelFilesystemUnmount((char *) physicalDisk->logical[count]
				    .filesystem.mountPoint);
	}

      dev = ((kernelScsiDisk *) physicalDisk->driverData)->dev;

      // Found it.  Remove it from the system's disks
      kernelDiskRemoveDevice(dev);

      // Remove it from the device tree
      kernelDeviceRemove(dev);

      // Delete.
      removeDisk(physicalDisk);
      kernelFree(dev);
    }

  return (status = 0);
}


static kernelDiskOps scsiOps = {
  NULL, // driverReset
  NULL, // driverRecalibrate
  NULL, // driverSetMotorState
  NULL, // driverSetLockState
  NULL, // driverSetDoorState
  NULL, // driverDiskChanged
  driverReadSectors,
  driverWriteSectors
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
