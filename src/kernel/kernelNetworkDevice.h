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
//  kernelNetworkDevice.h
//

#if !defined(_KERNELNETWORKDEVICE_H)

#include "kernelNetwork.h"
#include "kernelDevice.h"

// Some constants for Address Resolution Protocol (ARP)
#define NETWORK_ARPHARDWARE_ETHERNET	1
#define NETWORK_ARPOP_REQUEST			1
#define NETWORK_ARPOP_REPLY				2

typedef struct {
	void (*driverInterruptHandler)(kernelNetworkDevice *);
	int (*driverSetFlags)(kernelNetworkDevice *, unsigned, int);
	unsigned (*driverReadData)(kernelNetworkDevice *, unsigned char *);
	int (*driverWriteData)(kernelNetworkDevice *, unsigned char *, unsigned);

} kernelNetworkDeviceOps;

typedef struct {
	networkEthernetHeader header;
	unsigned short hardwareAddressSpace;
	unsigned short protocolAddressSpace;
	unsigned char hardwareAddrLen;
	unsigned char protocolAddrLen;
	unsigned short opCode;
	// The rest of these are only valid for IP over ethernet
	unsigned char srcHardwareAddress[NETWORK_ADDRLENGTH_ETHERNET];
	unsigned char srcLogicalAddress[NETWORK_ADDRLENGTH_IP];
	unsigned char destHardwareAddress[NETWORK_ADDRLENGTH_ETHERNET];
	unsigned char destLogicalAddress[NETWORK_ADDRLENGTH_IP];
	// Padding to bring us up to the mininum 46 byte ethernet packet size.
	// Some adapters can't automatically pad it for us.
	char pad[18];
	// Space for the ethernet FCS checksum.  Some adapters can't automatically
	// add it for us.
	unsigned Fcs;

} __attribute__((packed)) kernelArpPacket;

int kernelNetworkDeviceRegister(kernelDevice *);
int kernelNetworkDeviceSetFlags(const char *, unsigned, int);
int kernelNetworkDeviceGetAddress(const char *, networkAddress *,
	networkAddress *);
int kernelNetworkDeviceSend(const char *, unsigned char *, unsigned);
int kernelNetworkDeviceGetCount(void);
int kernelNetworkDeviceGet(const char *, networkDevice *);

#define _KERNELNETWORKDEVICE_H
#endif

