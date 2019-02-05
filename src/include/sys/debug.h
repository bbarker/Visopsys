//
//  Visopsys
//  Copyright (C) 1998-2018 J. Andrew McLaughlin
//
//  This library is free software; you can redistribute it and/or modify it
//  under the terms of the GNU Lesser General Public License as published by
//  the Free Software Foundation; either version 2.1 of the License, or (at
//  your option) any later version.
//
//  This library is distributed in the hope that it will be useful, but
//  WITHOUT ANY WARRANTY; without even the implied warranty of
//  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU Lesser
//  General Public License for more details.
//
//  You should have received a copy of the GNU Lesser General Public License
//  along with this library; if not, write to the Free Software Foundation,
//  Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
//
//  debug.h
//

// This file contains definitions and structures for using kernel debugging
// in Visopsys.

#if !defined(_DEBUG_H)

typedef enum {
	debug_all, debug_api, debug_font, debug_fs, debug_gui, debug_io,
	debug_loader, debug_memory, debug_misc, debug_multitasker, debug_net,
	debug_pci, debug_power, debug_scsi, debug_usb, debug_device

} debug_category;

#define _DEBUG_H
#endif

