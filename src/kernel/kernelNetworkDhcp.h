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
//  kernelNetworkDhcp.h
//

#if !defined(_KERNELNETWORKDHCP_H)

#include "kernelNetwork.h"

// Internal constants for DHCP
#define NETWORK_DHCP_DEFAULT_TIMEOUT	10000 /* ms */

// Functions exported from kernelNetworkDhcp.c
int kernelNetworkDhcpConfigure(kernelNetworkDevice *, const char *,
	const char *, unsigned);
int kernelNetworkDhcpRelease(kernelNetworkDevice *);

#define _KERNELNETWORKDHCP_H
#endif

