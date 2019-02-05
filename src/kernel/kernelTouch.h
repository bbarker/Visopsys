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
//  kernelTouch.h
//

// This goes with the file kernelTouch.c

#if !defined(_KERNELTOUCH_H)

#define TOUCH_POINTER_SIZE		21
#define TOUCH_SCALING_FACTOR	10

typedef struct {
	int x;
	int y;
	int z;
	unsigned flags;
	int maxX;
	int maxY;
	int maxZ;

} kernelTouchReport;

// Functions exported by kernelTouch.c
int kernelTouchInitialize(void);
int kernelTouchShutdown(void);
void kernelTouchDetected(void);
int kernelTouchAvailable(void);
void kernelTouchInput(kernelTouchReport *);

#define _KERNELTOUCH_H
#endif

