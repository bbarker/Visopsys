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
//  types.h
//

// This is the Visopsys version of the header file types.h

#if !defined(_TYPES_H)

typedef int					dev_t;
typedef int					ino_t;
typedef unsigned			off_t;
typedef int					uid_t;
typedef int					gid_t;
typedef int					pid_t;
typedef int					mode_t;
typedef unsigned			nlink_t;
typedef unsigned			blksize_t;
typedef unsigned			blkcnt_t;
typedef long long			quad_t;
typedef unsigned long long	uquad_t;

#define _TYPES_H
#endif

