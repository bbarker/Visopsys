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
//  cdefs.h
//

// This is the Visopsys version of the standard header file cdefs.h

#if !defined(_CDEFS_H)

#include <stdarg.h>
#include <sys/types.h>

// Internal C library file descriptor types
typedef enum {
	filedesc_unknown = 0,
	filedesc_textstream,
	filedesc_filestream,
	filedesc_socket

} fileDescType;

// Internal functions of the C library
void _dbl2str(double, char *, int);
int _digits(unsigned, int, int);
int _fdalloc(fileDescType, void *, int);
int _fdget(int, fileDescType *, void **);
int _fdset_type(int, fileDescType);
int _fdset_data(int, void *, int);
void _fdfree(int);
void _flt2str(float, char *, int);
int _fmtinpt(const char *, const char *, va_list);
int _ldigits(unsigned long long, int, int);
void _lnum2str(unsigned long long, char *, int, int);
void _num2str(unsigned, char *, int, int);
unsigned long long _str2num(const char *, unsigned, int, int *);
int _xpndfmt(char *, int, const char *, va_list);

#define _CDEFS_H
#endif

