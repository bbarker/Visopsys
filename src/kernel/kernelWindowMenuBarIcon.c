//
//  Visopsys
//  Copyright (C) 1998-2016 J. Andrew McLaughlin
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
//  kernelWindowMenuBarIcon.c
//

// This code is for managing kernelWindowMenuBarIcon objects.  These are icons
// that occur inside of kernelWindowMenuBar components.  They're just like
// regular icons, but they only have an image, no label.

#include "kernelWindow.h"	// Our prototypes are here
#include "kernelDebug.h"
#include "kernelError.h"


/////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////
//
// Below here, the functions are exported for external use
//
/////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////

kernelWindowComponent *kernelWindowNewMenuBarIcon(objectKey parent,
	image *imageCopy, componentParameters *params)
{
	// Formats a kernelWindowComponent as a kernelWindowMenuBarIcon

	kernelWindowComponent *component = NULL;
	kernelWindowComponent *menuBarComponent = parent;

	// Check params
	if (!parent || !imageCopy || !params)
	{
		kernelError(kernel_error, "NULL parameter");
		return (component = NULL);
	}

	// Parent must be a menu bar
	if (menuBarComponent->type != menuBarComponentType)
	{
		kernelError(kernel_error, "Parent is not a menu bar");
		return (component = NULL);
	}

	kernelDebug(debug_gui, "WindowMenuBarIcon new menuBar icon");

	// Call kernelWindowNewIcon() to create the icon.
	component = kernelWindowNewIcon(menuBarComponent, imageCopy,
		NULL /* label */, params);
	if (!component)
		return (component);

	return (component);
}

