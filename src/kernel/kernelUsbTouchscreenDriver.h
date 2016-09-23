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
//  kernelUsbTouchscreenDriver.h
//

#if !defined(_KERNELUSBTOUCHSCREENDRIVER_H)

#include "kernelDevice.h"
#include "kernelTouch.h"
#include "kernelUsbDriver.h"

// The maximum number of different reports we'll accept
#define MAX_TOUCH_REPORTS		16

// Describes a generic report field of a USB HID touchscreen; where to find
// it in the interrupt data, and its size
typedef struct {
	int set;
	unsigned byteOffset;
	unsigned bitPosition;
	unsigned bitLength;
	int minimum;
	int maximum;

} reportFieldDesc;

// A generic descriptor of a USB HID report (the fields we're interested in)
typedef struct {
	unsigned reportId;
	reportFieldDesc touch;
	reportFieldDesc x;
	reportFieldDesc y;
	reportFieldDesc z;

} genericReportDesc;

typedef struct {
	usbDevice *usbDev;
	kernelDevice dev;
	kernelTouchReport prevReport;
	genericReportDesc reports[MAX_TOUCH_REPORTS];
	int numReports;

} touchDevice;

#define _KERNELUSBTOUCHSCREENDRIVER_H
#endif

