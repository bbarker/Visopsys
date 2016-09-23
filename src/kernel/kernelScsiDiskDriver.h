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
//  kernelScsiDiskDriver.h
//

#if !defined(_KERNELSCSIDISKDRIVER_H)

#include "kernelBus.h"
#include "kernelUsbDriver.h"

#define SCSI_MAX_DISKS	16

typedef struct {
	kernelBusTarget *busTarget;
	kernelDevice dev;
	char vendorId[9];
	char productId[17];
	char vendorProductId[26];
	unsigned numSectors;
	unsigned sectorSize;
	struct {
		usbDevice *usbDev;
		unsigned char bulkInEndpoint;
		unsigned char bulkOutEndpoint;
		unsigned tag;
	} usb;

} kernelScsiDisk;

#define _KERNELSCSIDISKDRIVER_H
#endif

