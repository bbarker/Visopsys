//
//  Visopsys
//  Copyright (C) 1998-2016 J. Andrew McLaughlin
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
//  signal.h
//

// This is the Visopsys version of the standard header file signal.h

#if !defined(_SIGNAL_H)

#define SIGNALS_MAX 32

// Signal numbers.  Compatible with solaris codes.

// SIGABRT: Abnormal termination, such as is initiated by the abort function
#define SIGABRT     6
// SIGFPE: An erroneous arithmetic operation, such as zero divide or an
// operation resulting in overflow
#define SIGFPE      8
// SIGILL: Detection of an invalid function image, such as an invalid
// instruction
#define SIGILL      4
// SIGINT: Receipt of an interactive attention signal
#define SIGINT      2
// SIGSEGV: An invalid access to storage
#define SIGSEGV     11
// SIGTERM: A termination request sent to the program
#define SIGTERM     15

// A type for a signal handler
typedef void (*sighandler_t)(int);

// Built-in signal handing macros.
#define SIG_DFL ((sighandler_t) 0)  // Do the default action.
#define SIG_ERR ((sighandler_t) 1)  // Signal handling error.
#define SIG_IGN ((sighandler_t) 2)  // Ignore this signal.

// Signal handling functions
sighandler_t signal(int, sighandler_t);

#define _SIGNAL_H
#endif

