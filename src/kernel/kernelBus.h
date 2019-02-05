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
//  kernelBus.h
//

#if !defined(_KERNELBUS_H)

#include "kernelDevice.h"

// Forward declarations, where necessary
struct _kernelBus;

typedef enum {
	bus_pci = 1, bus_usb = 2
} kernelBusType;

typedef struct {
	struct _kernelBus *bus;
	int id;
	kernelDeviceClass *class;
	kernelDeviceClass *subClass;
	kernelDriver *claimed;

} kernelBusTarget;

typedef struct {
	int (*driverGetTargets)(struct _kernelBus *, kernelBusTarget **);
	int (*driverGetTargetInfo)(kernelBusTarget *, void *);
	unsigned (*driverReadRegister)(kernelBusTarget *, int, int);
	int (*driverWriteRegister)(kernelBusTarget *, int, int, unsigned);
	void (*driverDeviceClaim)(kernelBusTarget *, kernelDriver *);
	int (*driverDeviceEnable)(kernelBusTarget *, int);
	int (*driverSetMaster)(kernelBusTarget *, int);
	int (*driverRead)(kernelBusTarget *, unsigned, void *);
	int (*driverWrite)(kernelBusTarget *, unsigned, void *);

} kernelBusOps;

typedef struct _kernelBus {
	kernelBusType type;
	kernelDevice *dev;
	kernelBusOps *ops;

} kernelBus;

// Functions exported by kernelBus.c
int kernelBusRegister(kernelBus *);
int kernelBusGetTargets(kernelBusType, kernelBusTarget **);
kernelBusTarget *kernelBusGetTarget(kernelBusType, int);
int kernelBusGetTargetInfo(kernelBusTarget *, void *);
unsigned kernelBusReadRegister(kernelBusTarget *, int, int);
int kernelBusWriteRegister(kernelBusTarget *, int, int, unsigned);
void kernelBusDeviceClaim(kernelBusTarget *, kernelDriver *);
int kernelBusDeviceEnable(kernelBusTarget *, int);
int kernelBusSetMaster(kernelBusTarget *, int);
int kernelBusRead(kernelBusTarget *, unsigned, void *);
int kernelBusWrite(kernelBusTarget *, unsigned, void *);

#define _KERNELBUS_H
#endif

