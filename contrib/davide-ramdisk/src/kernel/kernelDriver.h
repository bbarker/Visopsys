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
//  kernelDriver.h
//

// Describes the generic interface for hardware device drivers.

#if !defined(_KERNELDRIVER_H)

// The generic device driver structure
typedef struct _kernelDriver {
  int class;
  int subClass;

  // The registration and detection functions, which all drivers must implement
  void (*driverRegister) (struct _kernelDriver *);
  int (*driverDetect) (void *, struct _kernelDriver *);
  int (*driverHotplug) (void *, int, int, int, struct _kernelDriver *);

  // Device class-specific operations
  void *ops;

} kernelDriver;

// An enumeration of driver types
typedef enum {
  extDriver, fatDriver, isoDriver, linuxSwapDriver, ntfsDriver,
  textConsoleDriver, graphicConsoleDriver
} kernelDriverType;

// Structures

// Functions exported by kernelDriver.c
int kernelConsoleDriversInitialize(void);
int kernelFilesystemDriversInitialize(void);
int kernelDriverRegister(kernelDriverType type, void *);
void *kernelDriverGet(kernelDriverType);

// Registration routines for our built-in drivers
void kernelBiosDriverRegister(kernelDriver *);
void kernelCpuDriverRegister(kernelDriver *);
void kernelMemoryDriverRegister(kernelDriver *);
void kernelPicDriverRegister(kernelDriver *);
void kernelSysTimerDriverRegister(kernelDriver *);
void kernelRtcDriverRegister(kernelDriver *);
void kernelDmaDriverRegister(kernelDriver *);
void kernelKeyboardDriverRegister(kernelDriver *);
void kernelFloppyDriverRegister(kernelDriver *);
void kernelIdeDriverRegister(kernelDriver *);
void kernelScsiDiskDriverRegister(kernelDriver *);
void kernelFramebufferGraphicDriverRegister(kernelDriver *);
void kernelPS2MouseDriverRegister(kernelDriver *);
void kernelPciDriverRegister(kernelDriver *);
void kernelUsbDriverRegister(kernelDriver *);
void kernelUsbMouseDriverRegister(kernelDriver *);
void kernelLanceDriverRegister(kernelDriver *);
//next one added by Davide Airaghi
void kernelRamDiskDriverRegister(kernelDriver *);

#define _KERNELDRIVER_H
#endif
