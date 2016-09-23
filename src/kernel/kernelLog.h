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
//  kernelLog.h
//

#if !defined(_KERNELLOG_H)

#include <sys/paths.h>

// Definitions
#define LOG_STREAM_SIZE		32768
#define DEFAULT_LOGFILE		PATH_SYSTEM "/kernel.log"

// Functions exported from kernelLog.c
int kernelLogInitialize(void);
int kernelLogSetFile(const char *);
int kernelLogGetToConsole(void);
void kernelLogSetToConsole(int);
int kernelLogShutdown(void);
int kernelLog(const char *, ...) __attribute__((format(printf, 1, 2)));

#define _KERNELLOG_H
#endif

