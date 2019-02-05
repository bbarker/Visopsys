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
//  kernelSysTimer.h
//

#if !defined(_KERNELSYSTIMER_H)

#include "kernelDevice.h"

#define SYSTIMER_FREQ_HZ			1193180
#define SYSTIMER_FULLCOUNT			0x10000 // 65536

// 18.206481934 full counts per second
#define SYSTIMER_FULLCOUNT_FREQ		(SYSTIMER_FREQ_HZ / SYSTIMER_FULLCOUNT)

typedef struct {
	void (*driverTick)(void);
	int (*driverRead)(void);
	int (*driverReadValue)(int);
	int (*driverSetupTimer)(int, int, int);
	int (*driverGetOutput)(int);

} kernelSysTimerOps;

// Functions exported by kernelSysTimer.c
int kernelSysTimerInitialize(kernelDevice *);
void kernelSysTimerTick(void);
unsigned kernelSysTimerRead(void);
int kernelSysTimerReadValue(int);
int kernelSysTimerSetupTimer(int, int, int);
int kernelSysTimerGetOutput(int);
void kernelSysTimerWaitTicks(int);

#define _KERNELSYSTIMER_H
#endif

