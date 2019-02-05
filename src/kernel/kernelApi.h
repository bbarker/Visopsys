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
//  kernelApi.h
//

#if !defined(_KERNELAPI_H)

// Pointer argument types
#define API_ARG_NONNULLPTR	0x04
#define API_ARG_USERPTR		0x02
#define API_ARG_KERNPTR		0x01
#define API_ARG_ANYPTR		0x00

// Value argument types
#define API_ARG_NONZEROVAL	0x02
#define API_ARG_POSINTVAL	0x01
#define API_ARG_ANYVAL		0x00

typedef enum {
	type_void, type_ptr, type_val

} kernelArgRetType;

typedef struct {
	int dwords;
	kernelArgRetType type;
	int content;

} kernelArgInfo;

typedef struct {
	int functionNumber;
	void *functionPointer;
	int privilege;
	int argCount;
	kernelArgInfo *args;
	kernelArgRetType returnType;

} kernelFunctionIndex;

// Functions exported from kernelApi.c
void kernelApi(unsigned, unsigned *);

#define _KERNELAPI_H
#endif

