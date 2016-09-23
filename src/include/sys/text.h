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
//  text.h
//

// This file contains definitions and structures for using and manipulating
// text in Visopsys.

#if !defined(_TEXT_H)

#include <sys/color.h>

// Definitions
#define TEXT_STREAMSIZE					32768
#define TEXT_DEFAULT_TAB				8
#define TEXT_DEFAULT_SCROLLBACKLINES	256

#define TEXT_DEFAULT_FOREGROUND			COLOR_LIGHTGRAY
#define TEXT_DEFAULT_BACKGROUND			COLOR_BLUE

// Flag values for the 'flags' field in the textAttrs structure
#define TEXT_ATTRS_NOFORMAT				0x10
#define TEXT_ATTRS_FOREGROUND			0x08
#define TEXT_ATTRS_BACKGROUND			0x04
#define TEXT_ATTRS_REVERSE				0x02
#define TEXT_ATTRS_BLINKING				0x01

typedef struct {
	int flags;
	color foreground;
	color background;

} textAttrs;

typedef struct {
	int column;
	int row;
	unsigned char *data;

} textScreen;

#define _TEXT_H
#endif

