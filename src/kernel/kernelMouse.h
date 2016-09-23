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
//  kernelMouse.h
//

// This goes with the file kernelMouse.c

#if !defined(_KERNELMOUSE_H)

#include <sys/image.h>
#include <sys/mouse.h>

typedef struct {
	char name[MOUSE_POINTER_NAMELEN];
	image pointerImage;

} kernelMousePointer;

// Functions exported by kernelMouse.c
int kernelMouseInitialize(void);
int kernelMouseShutdown(void);
int kernelMouseLoadPointer(const char *, const char *);
kernelMousePointer *kernelMouseGetPointer(const char *);
int kernelMouseSetPointer(kernelMousePointer *);
void kernelMouseDraw(void);
void kernelMouseHide(void);
void kernelMouseMove(int, int);
void kernelMouseButtonChange(int, int);
void kernelMouseScroll(int);
int kernelMouseGetX(void);
int kernelMouseGetY(void);

#define _KERNELMOUSE_H
#endif

