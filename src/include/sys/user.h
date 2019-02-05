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
//  user.h
//

// Definitions for user accounts

#if !defined(_USER_H)

#include <sys/paths.h>

#define USER_PASSWORDFILE			PATH_SYSTEM "/password"
#define USER_PASSWORDFILE_BLANK		PATH_SYSTEM "/password.blank"
#define USER_MAX_NAMELENGTH			16
#define USER_MAX_PASSWDLENGTH		16
#define USER_ADMIN					"admin"

#define _USER_H
#endif

