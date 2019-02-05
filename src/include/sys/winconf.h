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
//  winconf.h
//

// This file contains definitions for accessing standard variables in the
// various Visopsys window.conf files

#if !defined(_WINCONF_H)

#include <sys/paths.h>

// The window config file
#define WINDOW_CONFIG				"window.conf"

// Variables names in the window config file

// Colors
#define WINVAR_COLOR				"color"
#define WINVAR_FOREGROUND			"foreground"
#define WINVAR_BACKGROUND			"background"
#define WINVAR_DESKTOP				"desktop"
#define WINVAR_RED					"red"
#define WINVAR_GREEN				"green"
#define WINVAR_BLUE					"blue"
#define WINVAR_COLOR_FOREGROUND		WINVAR_COLOR "." WINVAR_FOREGROUND
#define WINVAR_COLOR_BACKGROUND		WINVAR_COLOR "." WINVAR_BACKGROUND
#define WINVAR_COLOR_DESKTOP		WINVAR_COLOR "." WINVAR_DESKTOP
#define WINVAR_COLOR_FG_RED			WINVAR_COLOR_FOREGROUND "." WINVAR_RED
#define WINVAR_COLOR_FG_GREEN		WINVAR_COLOR_FOREGROUND "." WINVAR_GREEN
#define WINVAR_COLOR_FG_BLUE		WINVAR_COLOR_FOREGROUND "." WINVAR_BLUE
#define WINVAR_COLOR_BG_RED			WINVAR_COLOR_BACKGROUND "." WINVAR_RED
#define WINVAR_COLOR_BG_GREEN		WINVAR_COLOR_BACKGROUND "." WINVAR_GREEN
#define WINVAR_COLOR_BG_BLUE		WINVAR_COLOR_BACKGROUND "." WINVAR_BLUE
#define WINVAR_COLOR_DT_RED			WINVAR_COLOR_DESKTOP "." WINVAR_RED
#define WINVAR_COLOR_DT_GREEN		WINVAR_COLOR_DESKTOP "." WINVAR_GREEN
#define WINVAR_COLOR_DT_BLUE		WINVAR_COLOR_DESKTOP "." WINVAR_BLUE

// Window settings
#define WINVAR_WINDOW				"window"
#define WINVAR_MINWIDTH				"minwidth"
#define WINVAR_MINHEIGHT			"minheight"
#define WINVAR_MINREST				"minrest"
#define WINVAR_TRACERS				"tracers"
#define WINVAR_TITLEBAR				"titlebar"
#define WINVAR_HEIGHT				"height"
#define WINVAR_BORDER				"border"
#define WINVAR_THICKNESS			"thickness"
#define WINVAR_SHADINGINCREMENT		"shadingincrement"
#define WINVAR_RADIOBUTTON			"radiobutton"
#define WINVAR_SIZE					"size"
#define WINVAR_CHECKBOX				"checkbox"
#define WINVAR_SLIDER				"slider"
#define WINVAR_WIDTH				"width"
#define WINVAR_FONT					"font"
#define WINVAR_FIXWIDTH				"fixwidth"
#define WINVAR_VARWIDTH				"varwidth"
#define WINVAR_SMALL				"small"
#define WINVAR_MEDIUM				"medium"
#define WINVAR_FAMILY				"family"
#define WINVAR_FLAGS				"flags"
#define WINVAR_POINTS				"points"
#define WINVAR_FLAG					"flag"
#define WINVAR_BOLD					"bold"
#define WINVAR_WINDOW_MINWIDTH		WINVAR_WINDOW "." WINVAR_MINWIDTH
#define WINVAR_WINDOW_MINHEIGHT		WINVAR_WINDOW "." WINVAR_MINHEIGHT
#define WINVAR_MINREST_TRACERS		WINVAR_WINDOW "." WINVAR_MINREST "." \
	WINVAR_TRACERS
#define WINVAR_TITLEBAR_HEIGHT		WINVAR_TITLEBAR "." WINVAR_HEIGHT
#define WINVAR_TITLEBAR_MINWIDTH	WINVAR_TITLEBAR "." WINVAR_MINWIDTH
#define WINVAR_BORDER_THICKNESS		WINVAR_BORDER "." WINVAR_THICKNESS
#define WINVAR_BORDER_SHADINGINCR	WINVAR_BORDER "." WINVAR_SHADINGINCREMENT
#define WINVAR_RADIOBUTTON_SIZE		WINVAR_RADIOBUTTON "." WINVAR_SIZE
#define WINVAR_CHECKBOX_SIZE		WINVAR_CHECKBOX "." WINVAR_SIZE
#define WINVAR_SLIDER_WIDTH			WINVAR_SLIDER "." WINVAR_WIDTH

// Font settings
#define WINVAR_FONT_FIXWIDTH		WINVAR_FONT "." WINVAR_FIXWIDTH
#define WINVAR_FONT_VARWIDTH		WINVAR_FONT "." WINVAR_VARWIDTH
#define WINVAR_FONT_FIXW_SMALL		WINVAR_FONT_FIXWIDTH "." WINVAR_SMALL
#define WINVAR_FONT_FIXW_MEDIUM		WINVAR_FONT_FIXWIDTH "." WINVAR_MEDIUM
#define WINVAR_FONT_VARW_SMALL		WINVAR_FONT_VARWIDTH "." WINVAR_SMALL
#define WINVAR_FONT_VARW_MEDIUM		WINVAR_FONT_VARWIDTH "." WINVAR_MEDIUM
#define WINVAR_FONT_FIXW_SM_FAMILY	WINVAR_FONT_FIXW_SMALL "." WINVAR_FAMILY
#define WINVAR_FONT_FIXW_SM_FLAGS	WINVAR_FONT_FIXW_SMALL "." WINVAR_FLAGS
#define WINVAR_FONT_FIXW_SM_POINTS	WINVAR_FONT_FIXW_SMALL "." WINVAR_POINTS
#define WINVAR_FONT_FIXW_MD_FAMILY	WINVAR_FONT_FIXW_MEDIUM "." WINVAR_FAMILY
#define WINVAR_FONT_FIXW_MD_FLAGS	WINVAR_FONT_FIXW_MEDIUM "." WINVAR_FLAGS
#define WINVAR_FONT_FIXW_MD_POINTS	WINVAR_FONT_FIXW_MEDIUM "." WINVAR_POINTS
#define WINVAR_FONT_VARW_SM_FAMILY	WINVAR_FONT_VARW_SMALL "." WINVAR_FAMILY
#define WINVAR_FONT_VARW_SM_FLAGS	WINVAR_FONT_VARW_SMALL "." WINVAR_FLAGS
#define WINVAR_FONT_VARW_SM_POINTS	WINVAR_FONT_VARW_SMALL "." WINVAR_POINTS
#define WINVAR_FONT_VARW_MD_FAMILY	WINVAR_FONT_VARW_MEDIUM "." WINVAR_FAMILY
#define WINVAR_FONT_VARW_MD_FLAGS	WINVAR_FONT_VARW_MEDIUM "." WINVAR_FLAGS
#define WINVAR_FONT_VARW_MD_POINTS	WINVAR_FONT_VARW_MEDIUM "." WINVAR_POINTS
#define WINVAR_FONT_FLAG_BOLD		WINVAR_BOLD

#define _WINCONF_H
#endif

