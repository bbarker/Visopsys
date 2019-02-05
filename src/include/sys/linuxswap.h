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
//  linux-swap.h
//

// This file contains definitions and structures for using and manipulating
// the Linux swap filesystem in Visopsys.

#if !defined(_LINUXSWAP_H)

#define LINUXSWAP_MAGIC1	"SWAP-SPACE"
#define LINUXSWAP_MAGIC2	"SWAPSPACE2"
#define LINUXSWAP_MAXPAGES	(~0UL << 8)

typedef union  {
	struct {
		unsigned char reserved[MEMORY_PAGE_SIZE - 10];
		char magic[10];					// SWAP-SPACE or SWAPSPACE2

	} magic;

	struct {
		unsigned char bootbits[1024];	// Space for disk label etc.
		unsigned version;
		unsigned lastPage;
		unsigned numBadPages;
		unsigned char uuid[16];
		char volumeLabel[16];
		unsigned padding[117];
		unsigned badPages[1];

	} info;

} __attribute__((packed)) linuxSwapHeader;

#define _LINUXSWAP_H
#endif

