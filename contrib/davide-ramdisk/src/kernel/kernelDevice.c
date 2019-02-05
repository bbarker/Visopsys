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
//  kernelDevice.c
//

#include "kernelDevice.h"
#include "kernelMalloc.h"
#include "kernelLog.h"
#include "kernelMisc.h"
#include "kernelError.h"
#include "kernelText.h"
#include <stdio.h>
#include <string.h>

// An array of device classes, with names
static kernelDeviceClass allClasses[] = {
  { DEVICECLASS_SYSTEM,   "system"                },
  { DEVICECLASS_CPU,      "CPU"                   },
  { DEVICECLASS_MEMORY,   "memory"                },
  { DEVICECLASS_BUS,      "bus"                   },
  { DEVICECLASS_PIC,      "PIC"                   },
  { DEVICECLASS_SYSTIMER, "system timer"          },
  { DEVICECLASS_RTC,      "real-time clock (RTC)" },
  { DEVICECLASS_DMA,      "DMA controller"        },
  { DEVICECLASS_KEYBOARD, "keyboard"              },
  { DEVICECLASS_MOUSE,    "mouse"                 },
  { DEVICECLASS_DISK,     "disk"                  },
  { DEVICECLASS_GRAPHIC,  "graphic adapter"       },
  { DEVICECLASS_NETWORK,  "network adapter"       },
  { DEVICECLASS_HUB,      "hub"                   },
  { DEVICECLASS_UNKNOWN,  "unknown"               },
  { 0, NULL }
};

// An array of device subclasses, with names
static kernelDeviceClass allSubClasses[] = {
  { DEVICESUBCLASS_SYSTEM_BIOS,         "BIOS"        },
  { DEVICESUBCLASS_CPU_X86,             "x86"         },
  { DEVICESUBCLASS_BUS_PCI,             "PCI"         },
  { DEVICESUBCLASS_BUS_USB,             "USB"         },
  { DEVICESUBCLASS_KEYBOARD_USB,        "USB"         },
  { DEVICESUBCLASS_MOUSE_PS2,           "PS/2"        },
  { DEVICESUBCLASS_MOUSE_SERIAL,        "serial"      },
  { DEVICESUBCLASS_MOUSE_USB,           "USB"         },
  { DEVICESUBCLASS_DISK_FLOPPY,         "floppy"      },
  { DEVICESUBCLASS_DISK_IDE,            "IDE"         },
  { DEVICESUBCLASS_DISK_SCSI,           "SCSI"        },
  { DEVICESUBCLASS_GRAPHIC_FRAMEBUFFER, "framebuffer" },
  { DEVICESUBCLASS_NETWORK_ETHERNET,    "ethernet"    },
  { DEVICESUBCLASS_HUB_USB,             "USB"         },
  { DEVICESUBCLASS_UNKNOWN,             "unknown"     },
  // next one added by Davide Airaghi
  { DEVICESUBCLASS_DISK_RAM,             "RamDisk" } ,
  { 0, NULL }
};

// Our static list of built-in display drivers
static kernelDriver displayDrivers[] = {
  { DEVICECLASS_GRAPHIC, DEVICESUBCLASS_GRAPHIC_FRAMEBUFFER,
    kernelFramebufferGraphicDriverRegister, NULL, NULL, NULL           }
};

// Our static list of built-in drivers
static kernelDriver deviceDrivers[] = {
  { DEVICECLASS_SYSTEM, DEVICESUBCLASS_SYSTEM_BIOS,
    kernelBiosDriverRegister, NULL, NULL, NULL                         },
  { DEVICECLASS_CPU, DEVICESUBCLASS_CPU_X86,
    kernelCpuDriverRegister, NULL, NULL, NULL                          },
  { DEVICECLASS_MEMORY, 0,
    kernelMemoryDriverRegister, NULL, NULL, NULL                       },
  // PIC must be before most drivers so that other ones can unmask
  // interrupts
  { DEVICECLASS_PIC, 0, kernelPicDriverRegister, NULL, NULL, NULL      },
  { DEVICECLASS_SYSTIMER, 0,
    kernelSysTimerDriverRegister, NULL, NULL, NULL                     },
  { DEVICECLASS_RTC, 0, kernelRtcDriverRegister, NULL, NULL, NULL      },
  { DEVICECLASS_DMA, 0, kernelDmaDriverRegister, NULL, NULL, NULL      },
  // Do buses before most non-motherboard devices, so that other
  // drivers can find their devices on the buses.
  { DEVICECLASS_BUS, DEVICESUBCLASS_BUS_PCI,
    kernelPciDriverRegister, NULL, NULL, NULL                          },
  { DEVICECLASS_BUS, DEVICESUBCLASS_BUS_USB,
    kernelUsbDriverRegister, NULL, NULL, NULL                          },
  { DEVICECLASS_KEYBOARD, 0,
    kernelKeyboardDriverRegister, NULL, NULL, NULL                     },
  { DEVICECLASS_DISK, DEVICESUBCLASS_DISK_FLOPPY,
    kernelFloppyDriverRegister, NULL, NULL, NULL                       },
  { DEVICECLASS_DISK, DEVICESUBCLASS_DISK_IDE,
    kernelIdeDriverRegister, NULL, NULL, NULL                          },
  { DEVICECLASS_DISK, DEVICESUBCLASS_DISK_SCSI,
    kernelScsiDiskDriverRegister, NULL, NULL, NULL                     },
  // next one added by Davide Airaghi
  { DEVICECLASS_DISK, DEVICESUBCLASS_DISK_RAM,
    kernelRamDiskDriverRegister, NULL, NULL, NULL                     },
  // Do the mouse devices after the graphic device so we can get screen
  // parameters, etc.  Also needs to be after the keyboard driver since
  // PS2 mouses use the keyboard controller.
  { DEVICECLASS_MOUSE, DEVICESUBCLASS_MOUSE_PS2,
    kernelPS2MouseDriverRegister, NULL, NULL, NULL                     },
  { DEVICECLASS_MOUSE, DEVICESUBCLASS_MOUSE_USB,
    kernelUsbMouseDriverRegister, NULL, NULL, NULL                     },
  { DEVICECLASS_NETWORK, DEVICESUBCLASS_NETWORK_ETHERNET,
    kernelLanceDriverRegister, NULL, NULL, NULL                        },
  { 0, 0, NULL, NULL, NULL, NULL                                       }
};

// Our device tree
static kernelDevice *deviceTree = NULL;
static int numTreeDevices = 0;


static int isDevInTree(kernelDevice *root, kernelDevice *dev)
{
  // This is for checking device pointers passed in from user space to make
  // sure that they point to devices in our tree.

  while (root)
    {
      if (root == dev)
	return (1);
      
      if (root->device.firstChild)
	if (isDevInTree(root->device.firstChild, dev) == 1)
	  return (1);

      root = root->device.next;
    }

  return (0);
}


static int findDeviceType(kernelDevice *dev, kernelDeviceClass *class,
			  kernelDeviceClass *subClass,
			  kernelDevice *devPointers[], int maxDevices,
			  int numDevices)
{
  // Recurses through the device tree rooted at the supplied device and
  // returns the all instances of devices of the requested type

  while (dev)
    {
      if (numDevices >= maxDevices)
	return (numDevices);

      if ((dev->device.class == class) &&
	  (!subClass || (dev->device.subClass == subClass)))
	devPointers[numDevices++] = dev;

      if (dev->device.firstChild)
	numDevices += findDeviceType(dev->device.firstChild, class, subClass,
				     devPointers, maxDevices, numDevices);

      dev = dev->device.next;
    }

  return (numDevices);
}


static void device2user(kernelDevice *kernel, device *user)
{
  // Convert a kernelDevice structure to the user version

  int count;

  kernelMemClear(user, sizeof(device));

  if (kernel->device.class)
    {
      user->class.class = kernel->device.class->class;
      strncpy(user->class.name, kernel->device.class->name, DEV_CLASSNAME_MAX);
    }

  if (kernel->device.subClass)
    {
      user->subClass.class = kernel->device.subClass->class;
      strncpy(user->subClass.name, kernel->device.subClass->name,
	      DEV_CLASSNAME_MAX);
    }

  kernelVariableListCreate(&(user->attrs));
  for (count = 0; count < kernel->device.attrs.numVariables; count ++)
    kernelVariableListSet(&(user->attrs),
			  kernel->device.attrs.variables[count],
			  kernel->device.attrs.values[count]);

  user->parent = kernel->device.parent;
  user->firstChild = kernel->device.firstChild;
  user->previous = kernel->device.previous;
  user->next = kernel->device.next;
}


/////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////
//
//  Below here, the functions are exported for external use
//
/////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////


int kernelDeviceInitialize(void)
{
  // This function is called during startup so we can call the
  // driverRegister() functions of all our drivers

  int status = 0;
  int driverCount = 0;

  // Allocate a NULL 'system' device to build our device tree from
  deviceTree = kernelMalloc(sizeof(kernelDevice));
  if (deviceTree == NULL)
    return (status = ERR_MEMORY);

  deviceTree->device.class = kernelDeviceGetClass(DEVICECLASS_SYSTEM);
  numTreeDevices = 1;

  // Loop through our static structures of built-in device drivers and
  // initialize them.
  for (driverCount = 0; (displayDrivers[driverCount].class != 0);
       driverCount ++)
    {
      if (displayDrivers[driverCount].driverRegister)
	displayDrivers[driverCount]
	  .driverRegister(&displayDrivers[driverCount]);
    }
  for (driverCount = 0; (deviceDrivers[driverCount].class != 0);
       driverCount ++)
    {
      if (deviceDrivers[driverCount].driverRegister)
	deviceDrivers[driverCount].driverRegister(&deviceDrivers[driverCount]);
    }

  return (status = 0);
}


int kernelDeviceDetectDisplay(void)
{
  // This function is called during startup so we can call the detect()
  // functions of all our display drivers

  int status = 0;
  kernelDeviceClass *class = NULL;
  kernelDeviceClass *subClass = NULL;
  char driverString[128];
  int driverCount = 0;

  // Loop for each hardware driver, and see if it has any devices for us
  for (driverCount = 0; (displayDrivers[driverCount].class != 0);
       driverCount ++)
    {
      class = NULL;
      class = kernelDeviceGetClass(displayDrivers[driverCount].class);

      subClass = NULL;
      if (displayDrivers[driverCount].subClass)
	subClass = kernelDeviceGetClass(displayDrivers[driverCount].subClass);

      driverString[0] = '\0';
      if (subClass)
	sprintf(driverString, "%s ", subClass->name);
      if (class)
	strcat(driverString, class->name);

      if (displayDrivers[driverCount].driverDetect == NULL)
	{
	  kernelError(kernel_error, "Device driver for \"%s\" has no 'detect' "
		      "function", driverString);
	  continue;
	}

      status = displayDrivers[driverCount]
	.driverDetect(deviceTree, &displayDrivers[driverCount]);
      if (status < 0)
	kernelError(kernel_error, "Error %d detecting \"%s\" devices",
		    status, driverString);
    }

  return (status = 0);
}


int kernelDeviceDetect(void)
{
  // This function is called during startup so we can call the detect()
  // functions of all our general drivers

  int status = 0;
  int textNumColumns = 0;
  kernelDeviceClass *class = NULL;
  kernelDeviceClass *subClass = NULL;
  char driverString[128];
  int driverCount = 0;
  int count;

  kernelTextPrintLine("");
  textNumColumns = kernelTextGetNumColumns();

  // Loop for each hardware driver, and see if it has any devices for us
  for (driverCount = 0; (deviceDrivers[driverCount].class != 0);
       driverCount ++)
    {
      class = NULL;
      class = kernelDeviceGetClass(deviceDrivers[driverCount].class);

      subClass = NULL;
      if (deviceDrivers[driverCount].subClass)
	subClass = kernelDeviceGetClass(deviceDrivers[driverCount].subClass);

      driverString[0] = '\0';
      if (subClass)
	sprintf(driverString, "%s ", subClass->name);
      if (class)
	strcat(driverString, class->name);

      // Clear the current line
      kernelTextSetColumn(0);
      for (count = 0; count < (textNumColumns - 1); count ++)
	kernelTextPutc(' ');
      kernelTextSetColumn(0);
      // Print a message
      kernelTextPrint("Detecting hardware: %s ", driverString);

      if (deviceDrivers[driverCount].driverDetect == NULL)
	{
	  kernelError(kernel_error, "Device driver for \"%s\" has no 'detect' "
		      "function", driverString);
	  continue;
	}

      status = deviceDrivers[driverCount]
	.driverDetect(deviceTree, &deviceDrivers[driverCount]);
      if (status < 0)
	kernelError(kernel_error, "Error %d detecting \"%s\" devices",
		    status, driverString);
    }

  kernelTextSetColumn(0);
  for (count = 0; count < (textNumColumns - 1); count ++)
    kernelTextPutc(' ');
  kernelTextSetColumn(0);
  return (status = 0);
}


kernelDeviceClass *kernelDeviceGetClass(int classNum)
{
  // Given a device (sub)class number, return a pointer to the static class
  // description

  kernelDeviceClass *classList = allClasses;
  int count;

  // Looking for a subclass?
  if ((classNum & DEVICESUBCLASS_MASK) != 0)
    classList = allSubClasses;

  // Loop through the list
  for (count = 0; (classList[count].class != 0) ; count ++)
    if (classList[count].class == classNum)
      return (&classList[count]);

  // Not found
  return (NULL);
}


int kernelDeviceFindType(kernelDeviceClass *class, kernelDeviceClass *subClass,
			 kernelDevice *devPointers[], int maxDevices)
{
  // Calls findDevice to return the first device it finds, with the
  // requested device class and subclass

  int status = 0;

  // Check params.  subClass can be NULL.
  if ((class == NULL) || (devPointers == NULL))
    return (status = ERR_NULLPARAMETER);

  status =
    findDeviceType(deviceTree, class, subClass, devPointers, maxDevices, 0);
  return (status);
}


int kernelDeviceHotplug(kernelDevice *parent, int classNum, int busType,
			int target, int connected)
{
  // Call the hotplug detection routine for any driver that matches the
  // supplied class (and subclass).  This was added to support i.e.
  // USB devices that can be added or removed at any time.

  int status = 0;
  int count;

  for (count = 0; (deviceDrivers[count].class != 0); count ++)
    {
      if ((classNum & DEVICECLASS_MASK) == deviceDrivers[count].class)
	{
	  if (!(classNum & DEVICESUBCLASS_MASK) ||
	      (classNum == deviceDrivers[count].subClass))
	    {
	      //kernelTextPrintLine("Hotplug for %04x devices", classNum);
	      if (deviceDrivers[count].driverHotplug)
		status = deviceDrivers[count]
		  .driverHotplug(parent, busType, target, connected,
				 &deviceDrivers[count]);
	    }
	}
    }

  return (status);
}


int kernelDeviceAdd(kernelDevice *parent, kernelDevice *new)
{
  // Given a parent device, add the new device as a child

  int status = 0;
  kernelDevice *listPointer = NULL;
  char vendor[64];
  char model[64];
  char driverString[128];

  // Check params
  if (new == NULL)
    {
      kernelError(kernel_error, "Device to add is NULL");
      return (status = ERR_NULLPARAMETER);
    }

  // NULL parent means use the root system device.
  if (parent == NULL)
    parent = deviceTree;

  new->device.parent = parent;

  driverString[0] = '\0';

  vendor[0] = '\0';
  model[0] = '\0';
  kernelVariableListGet(&(new->device.attrs), DEVICEATTRNAME_VENDOR, vendor,
			64);
  kernelVariableListGet(&(new->device.attrs), DEVICEATTRNAME_MODEL, model,
			64);
  if (vendor[0] || model[0])
    {
      if (vendor[0] && model[0])
	sprintf(driverString, "\"%s %s\"", vendor, model);
      else if (vendor[0])
	sprintf(driverString, "\"%s\"", vendor);
      else if (model[0])
	sprintf(driverString, "\"%s\"", model);
    }

  if (new->device.subClass)
    sprintf((driverString + strlen(driverString)), "%s ",
	    new->device.subClass->name);
  if (new->device.class)
    strcat(driverString, new->device.class->name);

  // If the parent has no children, make this the first one.
  if (parent->device.firstChild == NULL)
    parent->device.firstChild = new;

  else
    {
      // The parent has at least one child.  Follow the linked list to the
      // last child.
      listPointer = parent->device.firstChild;
      while (listPointer->device.next != NULL)
	listPointer = listPointer->device.next;

      // listPointer points to the last child.
      listPointer->device.next = new;
      new->device.previous = listPointer;
    }

  kernelLog("%s device detected", driverString);

  numTreeDevices += 1;
  return (status = 0);
}


int kernelDeviceRemove(kernelDevice *old)
{
  // Given a device, remove it from our tree

  int status = 0;
  kernelDevice *parent = NULL;
  kernelDevice *previous = NULL;
  kernelDevice *next = NULL;

  // Check params
  if (old == NULL)
    {
      kernelError(kernel_error, "Device to remove is NULL");
      return (status = ERR_NULLPARAMETER);
    }

  // Cannot remove devices that have children
  if (old->device.firstChild)
    {
      kernelError(kernel_error, "Cannot remove devices that have children");
      return (status = ERR_NULLPARAMETER);
    }
  
  parent = old->device.parent;
  previous = old->device.previous;
  next = old->device.next;
  
  // If this is the parent's first child, substitute the next device pointer
  // (whether or not it's NULL)
  if (parent && (parent->device.firstChild == old))
    parent->device.firstChild = next;

  // Connect our 'previous' and 'next' devices, as applicable.
  if (previous)
    previous->device.next = next;
  if (next)
    next->device.previous = previous;

  numTreeDevices -= 1;
  return (status = 0);
}


int kernelDeviceTreeGetCount(void)
{
  // Returns the number of devices in the kernel's device tree.
  return (numTreeDevices);
}


int kernelDeviceTreeGetRoot(device *rootDev)
{
  // Returns the user-space portion of the device tree root device

  int status = 0;

  // Are we initialized?
  if (deviceTree == NULL)
    return (status = ERR_NOTINITIALIZED);

  // Check params
  if (rootDev == NULL)
    {
      kernelError(kernel_error, "Device pointer is NULL");
      return (status = ERR_NULLPARAMETER);
    }

  device2user(&deviceTree[0], rootDev);
  return (status = 0);
}


int kernelDeviceTreeGetChild(device *parentDev, device *childDev)
{
  // Returns the user-space portion of the supplied device's first child
  // device

  int status = 0;

  // Are we initialized?
  if (deviceTree == NULL)
    return (status = ERR_NOTINITIALIZED);

  // Check params
  if ((parentDev == NULL) || (childDev == NULL))
    {
      kernelError(kernel_error, "Device pointer is NULL");
      return (status = ERR_NULLPARAMETER);
    }
  
  if ((parentDev->firstChild == NULL) ||
      !isDevInTree(deviceTree, parentDev->firstChild))
    return (status = ERR_NOSUCHENTRY);

  device2user(parentDev->firstChild, childDev);
  return (status = 0);
}


int kernelDeviceTreeGetNext(device *siblingDev)
{
  // Returns the user-space portion of the supplied device's 'next' (sibling)
  // device

  int status = 0;

  // Are we initialized?
  if (deviceTree == NULL)
    return (status = ERR_NOTINITIALIZED);

  // Check params
  if (siblingDev == NULL)
    {
      kernelError(kernel_error, "Device pointer is NULL");
      return (status = ERR_NULLPARAMETER);
    }

  if ((siblingDev->next == NULL) || !isDevInTree(deviceTree, siblingDev->next))
    return (status = ERR_NOSUCHENTRY);

  device2user(siblingDev->next, siblingDev);
  return (status = 0);
}
