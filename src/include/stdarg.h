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
//  stdarg.h
//

// This is the Visopsys version of the standard header file stdarg.h

#if !defined(_STDARG_H)

#include <stddef.h>

typedef void * va_list;

#define va_start(list, lastpar) ((list) = &lastpar)
#define va_arg(list, type) *((type *)((list) += sizeof(int)))
#define va_end(list) do {} while (0)

#define _STDARG_H
#endif

