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
//  kernelMain.h
//

#if !defined(_KERNELMAIN_H)

#include "loaderInfo.h"

// So that hardware detection, etc., can access the kernel's copy of the
// system info provided by the loader.
extern loaderInfoStruct *kernelOsLoaderInfo;

// The only function exported by kernelMain.c
void kernelMain(unsigned, void *, unsigned, loaderInfoStruct *)
  __attribute__((noreturn));

#define _KERNELMAIN_H
#endif

