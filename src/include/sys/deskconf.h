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
//  deskconf.h
//

// This file contains definitions for accessing standard variables in the
// various Visopsys desktop.conf files

#if !defined(_DESKCONF_H)

#define DESKTOP_CONFIG					"desktop.conf"

#define DESKVAR_BACKGROUND				"background"
#define DESKVAR_IMAGE					"image"
#define DESKVAR_NONE					"none"
#define DESKVAR_TASKBAR					"taskBar"
#define DESKVAR_MENU					"menu"
#define DESKVAR_ITEM					"item"
#define DESKVAR_COMMAND					"command"
#define DESKVAR_WINDOW					"window"
#define DESKVAR_ICON					"icon"
#define DESKVAR_NAME					"name"
#define DESKVAR_IMAGE					"image"
#define DESKVAR_PROGRAM					"program" "."
#define DESKVAR_BACKGROUND_IMAGE		DESKVAR_BACKGROUND "." DESKVAR_IMAGE
#define DESKVAR_TASKBAR_MENU			DESKVAR_TASKBAR "." DESKVAR_MENU "."
#define DESKVAR_TASKBAR_MENUITEM		DESKVAR_TASKBAR ".%s." DESKVAR_ITEM "."
#define DESKVAR_TASKBAR_MENUITEM_CMD	DESKVAR_TASKBAR ".%s.%s." \
	DESKVAR_COMMAND
#define DESKVAR_TASKBAR_WINDOWMENU		DESKVAR_WINDOW
#define DESKVAR_ICON_NAME				DESKVAR_ICON "." DESKVAR_NAME "."
#define DESKVAR_ICON_COMMAND			DESKVAR_ICON ".%s." DESKVAR_COMMAND
#define DESKVAR_ICON_IMAGE				DESKVAR_ICON ".%s." DESKVAR_IMAGE
#define DESKVAR_BACKGROUND_NONE			DESKVAR_NONE

#define _DESKCONF_H
#endif

