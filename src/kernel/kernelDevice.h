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
//  kernelDevice.h
//

// Describes the generic description/classification mechanism for hardware
// devices.

#if !defined(_KERNELDEVICE_H)

#include "kernelDriver.h"
#include <sys/device.h>

// A structure for device classes and subclasses, which just allows us to
// associate the different types with string names.
typedef struct {
	int class;
	char *name;

} kernelDeviceClass;

// The generic hardware device structure
typedef struct _kernelDevice {
	struct {
		// Device class and subclass.  Subclass optional.
		kernelDeviceClass *class;
		kernelDeviceClass *subClass;

		// Optional list of text attributes
		variableList attrs;

		// Used for maintaining the list of devices as a tree
		struct _kernelDevice *parent;
		struct _kernelDevice *firstChild;
		struct _kernelDevice *previous;
		struct _kernelDevice *next;

	} device;

	// Driver
	kernelDriver *driver;

	// Device class-specific structure
	void *data;

} kernelDevice;

// Functions exported from kernelDevice.c
int kernelDeviceInitialize(void);
int kernelDeviceDetectDisplay(void);
int kernelDeviceDetect(void);
kernelDeviceClass *kernelDeviceGetClass(int);
int kernelDeviceFindType(kernelDeviceClass *, kernelDeviceClass *,
	kernelDevice *[], int);
int kernelDeviceHotplug(kernelDevice *, int, int, int, int);
int kernelDeviceAdd(kernelDevice *, kernelDevice *);
int kernelDeviceRemove(kernelDevice *);
// These ones are exported outside the kernel
int kernelDeviceTreeGetRoot(device *);
int kernelDeviceTreeGetChild(device *, device *);
int kernelDeviceTreeGetNext(device *);

#define _KERNELDEVICE_H
#endif

