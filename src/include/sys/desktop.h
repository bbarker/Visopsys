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
//  desktop.h
//

// This file describes variables and settings used by Visopsys's window shell
// to manage the desktop environment.

#if !defined(_DESKTOP_H)

#define DESKTOP_CONFIGFILE					"desktop.conf"
#define DESKTOP_BACKGROUND					"background.image"
#define DESKTOP_BACKGROUND_NONE				"none"
#define DESKTOP_TASKBAR_MENU				"taskBar.menu."
#define DESKTOP_TASKBAR_MENUITEM			"taskBar.%s.item."
#define DESKTOP_TASKBAR_MENUITEM_COMMAND	"taskBar.%s.%s.command"
#define DESKTOP_TASKBAR_WINDOWMENU			"window"
#define DESKTOP_ICON_NAME					"icon.name."
#define DESKTOP_ICON_COMMAND				"icon.%s.command"
#define DESKTOP_ICON_IMAGE					"icon.%s.image"
#define DESKTOP_PROGRAM						"program."

#define _DESKTOP_H
#endif

