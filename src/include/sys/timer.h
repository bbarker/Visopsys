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
//  timer.h
//

// This file contains definitions and structures for using the performance
// timer in Visopsys.

#if !defined(_TIMER_H)

#include <sys/types.h>

#define TIMER_MAX_FUNCTIONS	100

typedef struct _timerFunctionEntry {
	const char *function;
	int calls;
	uquad_t entered;
	uquad_t totalTime;
	struct _timerFunctionEntry *interrupted;
	uquad_t pausedTime;

} timerFunctionEntry;

#define TIMER_ENTER timerEnter(__FUNCTION__)
#define TIMER_EXIT timerExit(__FUNCTION__)

// Functions exported by timer.c
void timerSetup(void);
void timerEnter(const char *);
void timerExit(const char *);
void timerGetSummary(timerFunctionEntry *, int );
void timerPrintSummary(int);

#define _TIMER_H
#endif

