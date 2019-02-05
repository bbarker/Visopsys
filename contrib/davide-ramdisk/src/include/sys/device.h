// 
//  Visopsys
//  Copyright (C) 1998-2006 J. Andrew McLaughlin
//  
//  This library is free software; you can redistribute it and/or modify it
//  under the terms of the GNU Lesser General Public License as published by
//  the Free Software Foundation; either version 2.1 of the License, or (at
//  your option) any later version.
//
//  This library is distributed in the hope that it will be useful, but
//  WITHOUT ANY WARRANTY; without even the implied warranty of
//  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU Lesser
//  General Public License for more details.
//
//  You should have received a copy of the GNU Lesser General Public License
//  along with this library; if not, write to the Free Software Foundation,
//  Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
//
//  device.h
//

// This file contains definitions and structures for using Visopsys hardware
// devices

#if !defined(_DEVICE_H)

#include <sys/variable.h>

#define DEV_CLASSNAME_MAX                   32

// Hardware device classes and subclasses
#define DEVICECLASS_NONE                    0
#define DEVICECLASS_SYSTEM                  0x0100
#define DEVICECLASS_CPU                     0x0200
#define DEVICECLASS_MEMORY                  0x0300
#define DEVICECLASS_BUS                     0x0400
#define DEVICECLASS_PIC                     0x0500
#define DEVICECLASS_SYSTIMER                0x0600
#define DEVICECLASS_RTC                     0x0700
#define DEVICECLASS_DMA                     0x0800
#define DEVICECLASS_KEYBOARD                0x0900
#define DEVICECLASS_MOUSE                   0x0A00
#define DEVICECLASS_DISK                    0x0B00
#define DEVICECLASS_GRAPHIC                 0x0C00
#define DEVICECLASS_NETWORK                 0x0D00
#define DEVICECLASS_HUB                     0x0E00
#define DEVICECLASS_STORAGE                 0x0F00
#define DEVICECLASS_UNKNOWN                 0xFFFF

// Device sub-classes

#define DEVICESUBCLASS_NONE                 0
#define DEVICESUBCLASS_UNKNOWN              (DEVICECLASS_UNKNOWN | 0x01)

// System device subclasses                 
#define DEVICESUBCLASS_SYSTEM_BIOS          (DEVICECLASS_SYSTEM | 0x01)

// Sub-classes of CPUs
#define DEVICESUBCLASS_CPU_X86              (DEVICECLASS_CPU | 0x01)

// Sub-classes of buses
#define DEVICESUBCLASS_BUS_PCI              (DEVICECLASS_BUS | 0x01)
#define DEVICESUBCLASS_BUS_USB              (DEVICECLASS_BUS | 0x02)

// Sub-classes of keyboards
#define DEVICESUBCLASS_KEYBOARD_USB         (DEVICECLASS_KEYBOARD | 0x01)

// Sub-classes of mice
#define DEVICESUBCLASS_MOUSE_PS2            (DEVICECLASS_MOUSE | 0x01)
#define DEVICESUBCLASS_MOUSE_SERIAL         (DEVICECLASS_MOUSE | 0x02)
#define DEVICESUBCLASS_MOUSE_USB            (DEVICECLASS_MOUSE | 0x03)

// Sub-classes of disks
#define DEVICESUBCLASS_DISK_FLOPPY          (DEVICECLASS_DISK | 0x01)
#define DEVICESUBCLASS_DISK_IDE             (DEVICECLASS_DISK | 0x02)
#define DEVICESUBCLASS_DISK_SCSI            (DEVICECLASS_DISK | 0x03)
#define DEVICESUBCLASS_DISK_CDDVD           (DEVICECLASS_DISK | 0x04)
// added by Davide Airaghi
#define DEVICESUBCLASS_DISK_RAM           (DEVICECLASS_DISK | 0x05)

// Sub-classes of graphics adapters
#define DEVICESUBCLASS_GRAPHIC_FRAMEBUFFER  (DEVICECLASS_GRAPHIC | 0x01)

// Sub-classes of network adapters
#define DEVICESUBCLASS_NETWORK_ETHERNET     (DEVICECLASS_NETWORK | 0x01)

// Sub-classes of hubs
#define DEVICESUBCLASS_HUB_USB              (DEVICECLASS_HUB | 0x01)

// Sub-classes of storage
#define DEVICESUBCLASS_STORAGE_FLASH        (DEVICECLASS_STORAGE | 0x01)
#define DEVICESUBCLASS_STORAGE_TAPE         (DEVICECLASS_STORAGE | 0x02)

// For masking off class/subclass
#define DEVICECLASS_MASK                    0xFF00
#define DEVICESUBCLASS_MASK                 0x00FF

// A list of standard device attribute names
#define DEVICEATTRNAME_VENDOR               "vendor.name"
#define DEVICEATTRNAME_MODEL                "model.name"

// A structure for device classes and subclasses, which just allows us to
// associate the different types with string names.
typedef struct {
  int class;
  char name[DEV_CLASSNAME_MAX];

} deviceClass;

// The generic hardware device structure
typedef struct {
  // Device class and subclass.  Subclass optional.
  deviceClass class;
  deviceClass subClass;

  // Optional list of text attributes
  variableList attrs;

  // Used for maintaining the list of devices as a tree
  void *parent;
  void *firstChild;
  void *previous;
  void *next;

} device;

#define _DEVICE_H
#endif
