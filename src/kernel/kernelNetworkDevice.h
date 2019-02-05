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
//  kernelNetworkDevice.h
//

#if !defined(_KERNELNETWORKDEVICE_H)

#include "kernelNetwork.h"
#include "kernelDevice.h"

typedef struct {
	int (*driverInterruptHandler)(kernelNetworkDevice *);
	int (*driverSetFlags)(kernelNetworkDevice *, unsigned, int);
	unsigned (*driverReadData)(kernelNetworkDevice *, unsigned char *);
	int (*driverWriteData)(kernelNetworkDevice *, unsigned char *, unsigned);

} kernelNetworkDeviceOps;

// Functions exported from kernelNetworkDevice.c
int kernelNetworkDeviceRegister(kernelDevice *);
int kernelNetworkDeviceStart(const char *, int);
int kernelNetworkDeviceStop(const char *);
int kernelNetworkDeviceEnable(const char *);
int kernelNetworkDeviceDisable(const char *);
int kernelNetworkDeviceSetFlags(const char *, unsigned, int);
int kernelNetworkDeviceGetAddress(const char *, networkAddress *,
	networkAddress *);
int kernelNetworkDeviceSend(const char *, kernelNetworkPacket *);
int kernelNetworkDeviceGetCount(void);
int kernelNetworkDeviceGet(const char *, networkDevice *);
int kernelNetworkDeviceHook(const char *, void **, int);
int kernelNetworkDeviceUnhook(const char *, void *, int);
unsigned kernelNetworkDeviceSniff(void *, unsigned char *, unsigned);

#define _KERNELNETWORKDEVICE_H
#endif

