// 
//  Visopsys
//  Copyright (C) 1998-2006 J. Andrew McLaughlin
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
//  Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
//
//  ramdisk.h
//

// Davide Airaghi
// This file contains definitions and structures for using and manipulating
// ramdisk in Visopsys.

#if !defined(_RAMDISK_H)

#include <sys/disk.h>
#include <sys/file.h>

typedef struct {

    char name[DISK_MAX_NAMELENGTH];
    int created;
    unsigned size;
    int sectorSize;
    int readOnly;
    int mounted;
    char mountPoint[MAX_PATH_LENGTH];
    char fsType[FSTYPE_MAX_NAMELENGTH];
} ramdisk;

#define _RAMDISK_H
#endif
