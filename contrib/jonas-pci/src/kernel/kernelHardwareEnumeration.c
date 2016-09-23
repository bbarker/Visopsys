//
//  Visopsys
//  Copyright (C) 1998-2005 J. Andrew McLaughlin
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
//  kernelHardwareEnumeration.c
//

// These routines enumerate all of the hardware devices in the system based
// on the hardware data structure passed to the kernel by the os loader.

#include "kernelHardwareEnumeration.h"
#include "kernelParameters.h"
#include "kernelDriverManagement.h"
#include "kernelPageManager.h"
#include "kernelProcessorX86.h"
#include "kernelText.h"
#include "kernelMiscFunctions.h"
#include "kernelLog.h"
#include "kernelError.h"
#include "kernelBusPCI.h"
#include "kernelMemoryManager.h"
#include <stdio.h>
#include <string.h>

loaderInfoStruct *systemInfo = NULL;

static kernelPic picDevice;
static kernelSysTimer systemTimerDevice;
static kernelRtc rtcDevice;
static kernelDma dmaDevice;
static kernelKeyboard keyboardDevice;
static kernelMouse mouseDevice;
static kernelPhysicalDisk floppyDevices[MAXFLOPPIES];  
static int numberFloppies = 0;
static kernelPhysicalDisk hardDiskDevices[MAXHARDDISKS];  
static int numberHardDisks = 0;
static kernelPhysicalDisk cdRomDevices[MAXHARDDISKS];  
static int numberCdRoms = 0;
static kernelGraphicAdapter graphicAdapterDevice;

static void *biosData = NULL;


static int enumeratePicDevice(void)
{
  // This routine enumerates the system's Programmable Interrupt Controller
  // device.  It doesn't really need enumeration; this really just registers 
  // the device and initializes the functions in the abstracted driver.

  int status = 0;

  kernelInstallPicDriver(&picDevice);

  status = kernelPicRegisterDevice(&picDevice);
  if (status < 0)
    return (status);

  status = kernelPicInitialize();
  if (status < 0)
    return (status);

  return (status = 0);
}


static int enumerateSysTimerDevice(void)
{
  // This routine enumerates the system timer device.  It doesn't really
  // need enumeration; this really just registers the device and initializes
  // the functions in the abstracted driver.

  int status = 0;

  kernelInstallSysTimerDriver(&systemTimerDevice);

  status = kernelSysTimerRegisterDevice(&systemTimerDevice);
  if (status < 0)
    return (status);

  // Initialize the system timer functions
  status = kernelSysTimerInitialize();
  if (status < 0)
    return (status);

  return (status = 0);
}


static int enumerateRtcDevice(void)
{
  // This routine enumerates the system's Real-Time clock device.  
  // It doesn't really need enumeration; this really just registers the 
  // device and initializes the functions in the abstracted driver.

  int status = 0;

  kernelInstallRtcDriver(&rtcDevice);

  status = kernelRtcRegisterDevice(&rtcDevice);
  if (status < 0)
    return (status);

  // Initialize the real-time clock functions
  status = kernelRtcInitialize();
  if (status < 0)
    return (status);

  return (status = 0);
}


static int enumerateDmaDevice(void)
{
  // This routine enumerates the system's DMA controller device(s).  
  // They doesn't really need enumeration; this really just registers the 
  // device and initializes the functions in the abstracted driver.

  int status = 0;

  kernelInstallDmaDriver(&dmaDevice);

  status = kernelDmaRegisterDevice(&dmaDevice);
  if (status < 0)
    return (status);

  // Initialize the DMA controller functions
  status = kernelDmaInitialize();
  if (status < 0)
    return (status);

  return (status = 0);
}


static int enumerateKeyboardDevice(void)
{
  // This routine enumerates the system's keyboard device.  
  // They doesn't really need enumeration; this really just registers the 
  // device and initializes the functions in the abstracted driver.

  int status = 0;

  kernelInstallKeyboardDriver(&keyboardDevice);

  status = kernelKeyboardRegisterDevice(&keyboardDevice);
  if (status < 0)
    return (status);

  // Get the flags from the BIOS data area
  keyboardDevice.flags = (unsigned) *((unsigned char *)(biosData + 0x417));

  // Initialize the keyboard functions
  status = kernelKeyboardInitialize();
  if (status < 0)
    return (status);

  // Set the default keyboard data stream to be the console input
  status =
    kernelKeyboardSetStream((stream *) &(kernelTextGetConsoleInput()->s));
  if (status < 0)
    return (status);

  return (status = 0);
}


static int enumerateFloppyDevices(void)
{
  // This routine enumerates floppy drives, and their types, and creates
  // kernelPhysicalDisks to store the information.  If successful, it 
  // returns the number of floppy devices it discovered.  It has a 
  // complementary routine which will return a pointer to the data
  // structure it has created.

  int status = 0;
  int count;

  // Reset the number of floppy devices 
  numberFloppies = systemInfo->floppyDisks;

  // We know the types.  We can fill out some data values in the
  // physical disk structure(s)
  for (count = 0; count < numberFloppies; count ++)
    {
      kernelInstallFloppyDriver(&floppyDevices[count]);

      // The device name and filesystem type
      sprintf((char *) floppyDevices[count].name, "fd%d", count);

      // The head, track and sector values we got from the loader
      floppyDevices[count].heads = systemInfo->fddInfo[count].heads;
      floppyDevices[count].cylinders = systemInfo->fddInfo[count].tracks;
      floppyDevices[count].sectorsPerCylinder =
	systemInfo->fddInfo[count].sectors;
      floppyDevices[count].numSectors = 
	(floppyDevices[count].heads * floppyDevices[count].cylinders *
	 floppyDevices[count].sectorsPerCylinder);
      floppyDevices[count].biosType = systemInfo->fddInfo[count].type;

      // Some additional universal default values
      floppyDevices[count].flags =
	(DISKFLAG_PHYSICAL | DISKFLAG_REMOVABLE | DISKFLAG_FLOPPY);
      floppyDevices[count].deviceNumber = count;
      floppyDevices[count].sectorSize = 512;
      floppyDevices[count].dmaChannel = 2;
      // Assume motor off for now

      // Register the floppy disk device
      status = kernelDiskRegisterDevice(&floppyDevices[count]);
      if (status < 0)
	return (status);
    }

  return (numberFloppies);
}


static int enumerateHardDiskDevices(void)
{
  // This routine enumerates hard disks, and creates kernelPhysicalDisks
  // to store the information.  If successful, it returns the number of 
  // devices it enumerated.

  int status = 0;
  int deviceNumber = 0;
  kernelPhysicalDisk physicalDisk;

  // Reset the number of physical hard disk devices we've actually
  // examined, and reset the number of logical disks we've created
  numberHardDisks = 0;

  // Make a message
  kernelLog("Examining hard disks...");

  for (deviceNumber = 0; (deviceNumber < MAXHARDDISKS); deviceNumber ++)
    {
      // Clear our disk structure
      kernelMemClear((void *) &physicalDisk, sizeof(kernelPhysicalDisk));

      // Install the ATA/ATAPI/IDE driver
      kernelInstallIdeDriver(&physicalDisk);

      // Call the detect routine
      if (physicalDisk.driver
	  ->driverDetect(deviceNumber, (void *) &physicalDisk) == 1)
	{
	  if (physicalDisk.flags & DISKFLAG_IDEDISK)
	    {
	      // In some cases, we are detecting hard disks that don't seem
	      // to actually exist.  Check whether the number of cylinders
	      // passed by the loader is non-NULL.
	      if (!systemInfo->hddInfo[numberHardDisks].cylinders)
		continue;

	      kernelLog("Disk %d is an IDE disk", deviceNumber);
	      
	      // Hard disk.  Put it into our hard disks array
	      kernelMemCopy((void *) &physicalDisk,
			    (void *) &(hardDiskDevices[numberHardDisks]),
			    sizeof(kernelPhysicalDisk));
	      
	      // The device name
	      sprintf((char *) hardDiskDevices[numberHardDisks].name,
		      (char *) "hd%d", numberHardDisks);
	      
	      // We get more hard disk info from the physical disk info we were
	      // passed.
	      hardDiskDevices[numberHardDisks].heads = 
		systemInfo->hddInfo[numberHardDisks].heads;
	      hardDiskDevices[numberHardDisks].cylinders = 
		systemInfo->hddInfo[numberHardDisks].cylinders;
	      hardDiskDevices[numberHardDisks].sectorsPerCylinder = 
		systemInfo->hddInfo[numberHardDisks].sectorsPerCylinder;
	      hardDiskDevices[numberHardDisks].numSectors = (unsigned)
		systemInfo->hddInfo[numberHardDisks].totalSectors;
	      hardDiskDevices[numberHardDisks].sectorSize = 
		systemInfo->hddInfo[numberHardDisks].bytesPerSector;
	      // Sometimes 0?  We can't have that as we are about to use it to
	      // perform a division operation.
	      if (hardDiskDevices[numberHardDisks].sectorSize == 0)
		{
		  kernelError(kernel_warn, "Physical disk %d sector size 0; "
			      "assuming 512", deviceNumber);
		  hardDiskDevices[numberHardDisks].sectorSize = 512;
		}
	      hardDiskDevices[numberHardDisks].motorState = 1;
	      
	      // Register the hard disk device
	      status =
		kernelDiskRegisterDevice(&hardDiskDevices[numberHardDisks]);
	      if (status < 0)
		return (status);
	      
	      // Increase the number of logical hard disk devices
	      numberHardDisks++;
	    }
	  
	  else if (physicalDisk.flags & DISKFLAG_IDECDROM)
	    {
	      kernelLog("Disk %d is an IDE CD-ROM", deviceNumber);
	      
	      // Hard disk.  Put it into our hard disks array
	      kernelMemCopy((void *) &physicalDisk,
			    (void *) &(cdRomDevices[numberCdRoms]),
			    sizeof(kernelPhysicalDisk));
	      
	      // The device name
	      sprintf((char *) cdRomDevices[numberCdRoms].name,
		      (char *) "cd%d", numberCdRoms);
	      
	      // Register the CDROM device
	      status = kernelDiskRegisterDevice(&cdRomDevices[numberCdRoms]);
	      if (status < 0)
		return (status);
	      
	      // Increase the number of logical hard disk devices
	      numberCdRoms++;
	    }
	}
      
      else
	kernelLog("Disk %d type is unknown", deviceNumber);
    }
  
  return (numberHardDisks + numberCdRoms);
}


static int enumerateGraphicDevice(void)
{
  // This routine enumerates the system's graphic adapter device.  
  // They doesn't really need enumeration; this really just registers the 
  // device and initializes the functions in the abstracted driver.

  int status = 0;

  // Set up the device parameters
  graphicAdapterDevice.videoMemory = systemInfo->graphicsInfo.videoMemory;
  graphicAdapterDevice.framebuffer = systemInfo->graphicsInfo.framebuffer;
  graphicAdapterDevice.mode = systemInfo->graphicsInfo.mode;
  graphicAdapterDevice.xRes = systemInfo->graphicsInfo.xRes;
  graphicAdapterDevice.yRes = systemInfo->graphicsInfo.yRes;
  graphicAdapterDevice.bitsPerPixel = systemInfo->graphicsInfo.bitsPerPixel;
  if (graphicAdapterDevice.bitsPerPixel == 15)
    graphicAdapterDevice.bytesPerPixel = 2;
  else
    graphicAdapterDevice.bytesPerPixel =
      (graphicAdapterDevice.bitsPerPixel / 8);
  graphicAdapterDevice.numberModes = systemInfo->graphicsInfo.numberModes;
  kernelMemCopy(&(systemInfo->graphicsInfo.supportedModes),
		&(graphicAdapterDevice.supportedModes),
		(sizeof(videoMode) * MAXVIDEOMODES));    
  
  kernelInstallGraphicDriver(&graphicAdapterDevice);

  // If we are in a graphics mode, initialize the graphics functions
  if (graphicAdapterDevice.mode != 0)
    {
      // Map the supplied physical linear framebuffer address into kernel
      // memory
      status = kernelPageMapToFree(KERNELPROCID,
				   graphicAdapterDevice.framebuffer, 
				   &(graphicAdapterDevice.framebuffer),
				   (graphicAdapterDevice.xRes *
				    graphicAdapterDevice.yRes *
				    graphicAdapterDevice.bytesPerPixel));
      if (status < 0)
	{
	  kernelError(kernel_error, "Unable to map linear framebuffer");
	  return (status);
	}

      status = kernelGraphicRegisterDevice(&graphicAdapterDevice);
      if (status < 0)
	return (status);

      status = kernelGraphicInitialize();
      if (status < 0)
	return (status);
    }

  return (status = 0);
}


static int enumerateMouseDevice(void)
{
  // This routine enumerates the system's mouse device.  For the time
  // being it assumes that the mouse is a PS2 type

  int status = 0;

  kernelInstallMouseDriver(&mouseDevice);

  status = kernelMouseRegisterDevice(&mouseDevice);
  if (status < 0)
    return (status);

  // Initialize the mouse functions
  status = kernelMouseInitialize();
  if (status < 0)
    return (status);

  return (status = 0);
}

static int enumeratePCIDevices(void)
{
	//Enumerates all devices on the PCI bus.
	//I only use PCI configuration mechanism #1, because mechnism #2 is deprecated since 1997 
	//and uncomfortable, buggy, etc.
	
	int status = 0;
	
	int bus, device, function;
	
	kernelBusPCIDevice * pciDevice;
	
	unsigned int i;
	
	char * classname;
	
	char * subclassname;
	
	//Check for a PCI controller first
	if(kernelBusPCIFindController() < 0)
	{
	   kernelLog("No PCI controller found on port 0xcf8! Perhaps configuration mechanism #2 must be used!\n");
	   
	   return (status = -1);
	}
	
	kernelLog("PCI controller found\n");
	
	pciDevice = kernelMemoryGet(sizeof(kernelBusPCIDevice), "PCI-device");
	
	//for every possible PCI device
	   
	for(bus = 0; bus < BUS_PCI_MAX_BUSES; bus++) 
	{
		for(device = 0; device < BUS_PCI_MAX_DEVICES; device++)
		{
			for(function = 0; function < BUS_PCI_MAX_FUNCTIONS; function++) 
			{
				
   
   				for(i = 0; i < sizeof(kernelBusPCIDevice) / sizeof(DWORD); i++)
   				{
      					kernelBusPCIReadConfig32(bus, device, function, i * sizeof(DWORD),
					(DWORD *) &(pciDevice->header[i]));
   				}
				
				//See if this is really a device, or if this device header is unoccupied.
				if((*pciDevice).device.vendorID == 0xffff || (*pciDevice).device.deviceID == 0xffff)
				{
					//No device here, so try next one
					continue;
				}
				
				//I didn't find a vendor with ID 0x0000 at http://www.pcidatabase.com
				if((*pciDevice).device.vendorID == 0x0000)
				{
					//Not a valid device, try next one
					continue;
				} 
				
				kernelBusPCIGetClassName((*pciDevice).device.class_code, (*pciDevice).device.subclass_code, &classname, &subclassname);
			
				
				//if here, we found a PCI device
				//TODO: substitute this message by a driver installation routine
				kernelLog("%u:%u:%u -> device: %x, vendor: %x, class: %x, subclass: %u\n", bus, device, function, (*pciDevice).device.deviceID, (*pciDevice).device.vendorID, (*pciDevice).device.class_code, (*pciDevice).device.subclass_code); 
				kernelLog(classname);
				kernelLog(subclassname);
				kernelLog("---------------------------------------"); 
				
			}
		}
	}
	
	kernelMemoryRelease((void *) pciDevice);
	
	return status;
}


/////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////
//
// Below here, the functions are exported for external use
//
/////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////


int kernelHardwareEnumerate(loaderInfoStruct *info)
{
  // Just calls all of the above hardware enumeration routines.  Used during
  // kernel initialization.  Returns 0 unless any of the other routines
  // return an error (negative), in which case it relays the error code.

  int status = 0;

  // Make sure the info structure isn't NULL
  if (info == NULL)
    return (status = ERR_NULLPARAMETER);

  // Save the pointer to the data structure that describes the hardware
  systemInfo = info;

  // Initialize the memory for the various structures we're managing
  kernelMemClear(&picDevice, sizeof(kernelPic));
  kernelMemClear(&systemTimerDevice, sizeof(kernelSysTimer));
  kernelMemClear(&rtcDevice, sizeof(kernelRtc));
  kernelMemClear(&dmaDevice, sizeof(kernelDma));
  kernelMemClear(&keyboardDevice, sizeof(kernelKeyboard));
  kernelMemClear(&mouseDevice, sizeof(kernelMouse));
  kernelMemClear((void *) floppyDevices, 
			(sizeof(kernelPhysicalDisk) * MAXFLOPPIES));
  kernelMemClear((void *) hardDiskDevices, 
			(sizeof(kernelPhysicalDisk) * MAXHARDDISKS));
  kernelMemClear(&graphicAdapterDevice, sizeof(kernelGraphicAdapter));

  // Map the BIOS data area into our memory so we can get hardware information
  // from it.
  status = kernelPageMapToFree(KERNELPROCID, (void *) 0, &biosData, 0x1000);
  if (status < 0)
    {
      kernelError(kernel_error, "Error mapping BIOS data area");
      return (status);
    }

  // Start enumerating devices

  // The PIC device needs to go first
  status = enumeratePicDevice();
  if (status < 0)
    return (status);

  // The system timer device
  status = enumerateSysTimerDevice();
  if (status < 0)
    return (status);

  // The Real-Time clock device
  status = enumerateRtcDevice();
  if (status < 0)
    return (status);

  // The DMA controller device
  status = enumerateDmaDevice();
  if (status < 0)
    return (status);

  // The keyboard device
  status = enumerateKeyboardDevice();
  if (status < 0)
    return (status);

  // Enable interrupts now.
  kernelProcessorEnableInts();

  // Enumerate the floppy disk devices
  status = enumerateFloppyDevices();
  if (status < 0)
    return (status);

  // Enumerate the hard disk devices
  status = enumerateHardDiskDevices();
  if (status < 0)
    return (status);

  // Enumerate the graphic adapter
  status = enumerateGraphicDevice();
  if (status < 0)
    return (status);

  // Do the mouse device after the graphic device so we can get screen
  // parameters, etc.  Also needs to be after the keyboard driver since
  // PS2 mouses use the keyboard controller.
  status = enumerateMouseDevice();
  if (status < 0)
    return (status);
    
  status = enumeratePCIDevices();
  if(status < 0)
    return (status);

  // Unmap BIOS data
  kernelPageUnmap(KERNELPROCID, biosData, 0x1000);

  // Return success
  return (status = 0);
}
