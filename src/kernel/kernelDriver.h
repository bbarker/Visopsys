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
//  kernelDriver.h
//

// Describes the generic interface for hardware device drivers.

#if !defined(_KERNELDRIVER_H)

// The generic device driver structure
typedef struct _kernelDriver {
	int class;
	int subClass;

	// The registration and detection functions, which all drivers must implement
	void (*driverRegister)(struct _kernelDriver *);
	int (*driverDetect)(void *, struct _kernelDriver *);
	int (*driverHotplug)(void *, int, int, int, struct _kernelDriver *);

	// Device class-specific operations
	void *ops;

} kernelDriver;

// An enumeration of software driver types
typedef enum {
	extDriver, fatDriver, isoDriver, linuxSwapDriver, ntfsDriver, udfDriver,
	textConsoleDriver, graphicConsoleDriver

} kernelSoftwareDriverType;

// Structures

// Functions exported by kernelDriver.c
int kernelConsoleDriversInitialize(void);
int kernelFilesystemDriversInitialize(void);
int kernelSoftwareDriverRegister(kernelSoftwareDriverType, void *);
void *kernelSoftwareDriverGet(kernelSoftwareDriverType);

// Registration routines for our built-in drivers
void kernelAcpiDriverRegister(kernelDriver *);
void kernelApicDriverRegister(kernelDriver *);
void kernelBios32DriverRegister(kernelDriver *);
void kernelBiosPnpDriverRegister(kernelDriver *);
void kernelCpuDriverRegister(kernelDriver *);
void kernelDmaDriverRegister(kernelDriver *);
void kernelFloppyDriverRegister(kernelDriver *);
void kernelFramebufferGraphicDriverRegister(kernelDriver *);
void kernelIdeDriverRegister(kernelDriver *);
void kernelIsaBridgeDriverRegister(kernelDriver *);
void kernelPcNetDriverRegister(kernelDriver *);
void kernelMemoryDriverRegister(kernelDriver *);
void kernelMultiProcDriverRegister(kernelDriver *);
void kernelPciDriverRegister(kernelDriver *);
void kernelPicDriverRegister(kernelDriver *);
void kernelPs2KeyboardDriverRegister(kernelDriver *);
void kernelPs2MouseDriverRegister(kernelDriver *);
void kernelRamDiskDriverRegister(kernelDriver *);
void kernelRtcDriverRegister(kernelDriver *);
void kernelSataAhciDriverRegister(kernelDriver *);
void kernelScsiDiskDriverRegister(kernelDriver *);
void kernelSysTimerDriverRegister(kernelDriver *);
void kernelUsbAtapiDriverRegister(kernelDriver *);
void kernelUsbDriverRegister(kernelDriver *);
void kernelUsbGenericDriverRegister(kernelDriver *);
void kernelUsbHubDriverRegister(kernelDriver *);
void kernelUsbKeyboardDriverRegister(kernelDriver *);
void kernelUsbMouseDriverRegister(kernelDriver *);
void kernelUsbTouchscreenDriverRegister(kernelDriver *);

#define _KERNELDRIVER_H
#endif

