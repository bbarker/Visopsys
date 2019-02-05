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
//  dlfcn.h
//

// This is the Visopsys version of the dynamic linking loader header file
// dlfcn.h

#if !defined(_DLFCN_H)

// Flags
#define RTLD_LAZY      0x0040
#define RTLD_NOW       0x0020
#define RTLD_GLOBAL    0x0010
#define RTLD_LOCAL     0x0008
#define RTLD_NODELETE  0x0004
#define RTLD_NOLOAD    0x0002
#define RTLD_DEEPBIND  0x0001

void *dlopen(const char *, int);
char *dlerror(void);
void *dlsym(void *, const char *);
int dlclose(void *);

#define _DLFCN_H
#endif

