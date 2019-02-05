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
//  windowCenterDialog.c
//

// This contains functions for user programs to operate GUI components.

#include <stdlib.h>
#include <sys/api.h>

extern int libwindow_initialized;
extern void libwindowInitialize(void);



/////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////
//
// Below here, the functions are exported for external use
//
/////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////

_X_ void windowCenterDialog(objectKey parentWindow, objectKey dialogWindow)
{
	// Desc: Center a dialog window.  The first object key is the parent window, and the second is the dialog window.  This function can be used to center a regular window on the screen if the first objectKey argument is NULL.

	int parentX = 0, parentY = 0;
	int parentWidth = 0, parentHeight = 0;
	int myWidth = 0, myHeight = 0;
	int diffWidth, diffHeight;

	if (!libwindow_initialized)
		libwindowInitialize();

	if (parentWindow)
	{
		// Get the size and location of the parent window
		windowGetLocation(parentWindow, &parentX, &parentY);
		windowGetSize(parentWindow, &parentWidth, &parentHeight);
	}
	else
	{
		parentWidth = graphicGetScreenWidth();
		parentHeight = graphicGetScreenHeight();
	}

	// Get our size
	windowGetSize(dialogWindow, &myWidth, &myHeight);

	diffWidth = (parentWidth - myWidth);
	diffHeight = (parentHeight - myHeight);

	// Set our location
	windowSetLocation(dialogWindow, max(0, (parentX + (diffWidth / 2))),
		max(0, (parentY + (diffHeight / 2))));
}

