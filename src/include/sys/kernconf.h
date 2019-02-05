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
//  kernconf.h
//

// This file contains definitions for accessing standard variables in the
// Visopsys kernel.conf file

#if !defined(_KERNCONF_H)

#include <sys/paths.h>

// The kernel's config file
#define KERNEL_DEFAULT_CONFIG		PATH_SYSTEM_CONFIG "/kernel.conf"

// Variables names in the kernel config file

// Colors
#define KERNELVAR_COLOR				"color"
#define KERNELVAR_FOREGROUND		"foreground"
#define KERNELVAR_BACKGROUND		"background"
#define KERNELVAR_DESKTOP			"desktop"
#define KERNELVAR_RED				"red"
#define KERNELVAR_GREEN				"green"
#define KERNELVAR_BLUE				"blue"
#define KERNELVAR_COLOR_FG			KERNELVAR_COLOR "." KERNELVAR_FOREGROUND
#define KERNELVAR_COLOR_BG			KERNELVAR_COLOR "." KERNELVAR_BACKGROUND
#define KERNELVAR_COLOR_DT			KERNELVAR_COLOR "." KERNELVAR_DESKTOP
#define KERNELVAR_COLOR_FG_RED		KERNELVAR_COLOR_FG "." KERNELVAR_RED
#define KERNELVAR_COLOR_FG_GREEN	KERNELVAR_COLOR_FG "." KERNELVAR_GREEN
#define KERNELVAR_COLOR_FG_BLUE		KERNELVAR_COLOR_FG "." KERNELVAR_BLUE
#define KERNELVAR_COLOR_BG_RED		KERNELVAR_COLOR_BG "." KERNELVAR_RED
#define KERNELVAR_COLOR_BG_GREEN	KERNELVAR_COLOR_BG "." KERNELVAR_GREEN
#define KERNELVAR_COLOR_BG_BLUE		KERNELVAR_COLOR_BG "." KERNELVAR_BLUE
#define KERNELVAR_COLOR_DT_RED		KERNELVAR_COLOR_DT "." KERNELVAR_RED
#define KERNELVAR_COLOR_DT_GREEN	KERNELVAR_COLOR_DT "." KERNELVAR_GREEN
#define KERNELVAR_COLOR_DT_BLUE		KERNELVAR_COLOR_DT "." KERNELVAR_BLUE

// Locale
#define KERNELVAR_LOCALE			"locale"
#define KERNELVAR_MESSAGES			"messages"
#define KERNELVAR_LOCALE_MESSAGES	KERNELVAR_LOCALE "." KERNELVAR_MESSAGES

// Keyboard
#define KERNELVAR_KEYBOARD			"keyboard"
#define KERNELVAR_MAP				"map"
#define KERNELVAR_KEYBOARD_MAP		KERNELVAR_KEYBOARD "." KERNELVAR_MAP

// Mouse
#define KERNELVAR_MOUSE				"mouse"
#define KERNELVAR_POINTER			"pointer"
#define KERNELVAR_DEFAULT			"default"
#define KERNELVAR_BUSY				"busy"
#define KERNELVAR_RESIZEH			"resizeh"
#define KERNELVAR_RESIZEV			"resizev"
#define KERNELVAR_MOUSEPTR			KERNELVAR_MOUSE "." KERNELVAR_POINTER
#define KERNELVAR_MOUSEPTR_DEFAULT	KERNELVAR_MOUSEPTR "." KERNELVAR_DEFAULT
#define KERNELVAR_MOUSEPTR_BUSY		KERNELVAR_MOUSEPTR "." KERNELVAR_BUSY
#define KERNELVAR_MOUSEPTR_RESIZEH	KERNELVAR_MOUSEPTR "." KERNELVAR_RESIZEH
#define KERNELVAR_MOUSEPTR_RESIZEV	KERNELVAR_MOUSEPTR "." KERNELVAR_RESIZEV

// Start program
#define KERNELVAR_START				"start"
#define KERNELVAR_PROGRAM			"program"
#define KERNELVAR_START_PROGRAM		KERNELVAR_START "." KERNELVAR_PROGRAM

// Network
#define KERNELVAR_NETWORK			"network"
#define KERNELVAR_HOSTNAME			"hostname"
#define KERNELVAR_DOMAINNAME		"domainname"
#define KERNELVAR_NET_HOSTNAME		KERNELVAR_NETWORK "." KERNELVAR_HOSTNAME
#define KERNELVAR_NET_DOMAINNAME	KERNELVAR_NETWORK "." KERNELVAR_DOMAINNAME

#define _KERNCONF_H
#endif

