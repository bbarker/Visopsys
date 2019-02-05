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
//  process.h
//

// This file contains definitions and structures for using and manipulating
// processes in Visopsys.

#if !defined(_PROCESS_H)

#include <string.h>

#define MAX_PROCNAME_LENGTH		64
#define MAX_PROCESSES			((GDT_SIZE - RES_GLOBAL_DESCRIPTORS))

// An enumeration listing possible process states
typedef enum {
	proc_running, proc_ready, proc_ioready, proc_waiting, proc_sleeping,
	proc_stopped, proc_finished, proc_zombie

} processState;

// An enumeration listing possible process types
typedef enum {
	proc_normal, proc_thread

} processType;

typedef struct {
	void *virtualAddress;
	void *entryPoint;
	void *code;
	unsigned codeSize;
	void *data;
	unsigned dataSize;
	unsigned imageSize;
	char commandLine[MAXSTRINGLENGTH];
	int argc;
	char *argv[64];

} processImage;

typedef struct {
	char name[MAX_PROCNAME_LENGTH];
	int userId;
	int processId;
	processType type;
	int priority;
	int privilege;
	int parentProcessId;
	int descendentThreads;
	int cpuPercent;
	processState state;

} process;

#define _PROCESS_H
#endif

