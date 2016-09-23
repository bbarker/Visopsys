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
//  kernelRtc.h
//

#if !defined(_KERNELRTC_H)

#include "kernelDevice.h"
#include <time.h>

// Structures for the Real-Time Clock device

typedef struct {
  int (*driverReadSeconds)(void);
  int (*driverReadMinutes)(void);
  int (*driverReadHours)(void);
  int (*driverReadDayOfMonth)(void);
  int (*driverReadMonth)(void);
  int (*driverReadYear)(void);

} kernelRtcOps;

// Functions exported by kernelRtc.c
int kernelRtcInitialize(kernelDevice *);
int kernelRtcReadSeconds(void);
int kernelRtcReadMinutes(void);
int kernelRtcReadHours(void);
int kernelRtcReadDayOfMonth(void);
int kernelRtcReadMonth(void);
int kernelRtcReadYear(void);
unsigned kernelRtcUptimeSeconds(void);
unsigned kernelRtcPackedDate(void);
unsigned kernelRtcPackedTime(void);
int kernelRtcDayOfWeek(unsigned, unsigned, unsigned);
int kernelRtcDateTime(struct tm *);
int kernelRtcDateTime2Tm(unsigned, unsigned, struct tm *);

#define _KERNELRTC_H
#endif

