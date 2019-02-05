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
//  vshPrintTime.c
//

// This contains some useful functions written for the shell

#include <stdio.h>
#include <sys/vsh.h>


_X_ void vshPrintTime(char *buffer, struct tm *theTime)
{
	// Desc: Return the time value, specified by the tm structure 'time' -- such as that found in the file.modified field -- into 'buffer' in a (for now, arbitrary) human-readable format.
	sprintf(buffer, "%02u:%02u:%02u", theTime->tm_hour, theTime->tm_min,
		theTime->tm_sec);
}

