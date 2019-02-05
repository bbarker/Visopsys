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
//  kernelPic.h
//

#if !defined(_KERNELPIC_H)

#include "kernelDriver.h"

#define MAX_PICS			8

typedef enum {
	pic_8259, pic_ioapic

} kernelPicType;

typedef struct {
	kernelPicType type;
	int enabled;
	int startIrq;
	int numIrqs;
	kernelDriver *driver;
	void *driverData;

} kernelPic;

typedef struct {
	int (*driverGetIntNumber)(kernelPic *, unsigned char, unsigned char);
	int (*driverGetVector)(kernelPic *, int);
	int (*driverEndOfInterrupt)(kernelPic *, int);
	int (*driverMask)(kernelPic *, int, int);
	int (*driverGetActive)(kernelPic *);
	int (*driverDisable)(kernelPic *);

} kernelPicOps;

// Functions exported by kernelPic.c
int kernelPicAdd(kernelPic *);
int kernelPicGetIntNumber(unsigned char, unsigned char);
int kernelPicGetVector(int);
int kernelPicEndOfInterrupt(int);
int kernelPicMask(int, int);
int kernelPicGetActive(void);

#define _KERNELPIC_H
#endif

