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
//  font.h
//

#if !defined(_FONT_H)

#include <sys/ascii.h>
#include <sys/image.h>

#define FONT_FAMILY_ARIAL			"arial"
#define FONT_FAMILY_LIBMONO			"libmono"
#define FONT_FAMILY_XTERM			"xterm"

#define FONT_STYLEFLAG_ITALIC		0x00000004
#define FONT_STYLEFLAG_BOLD			0x00000002
#define FONT_STYLEFLAG_FIXED		0x00000001
#define FONT_STYLEFLAG_NORMAL		0x00000000

#define _FONT_H
#endif

