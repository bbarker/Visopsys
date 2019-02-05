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
//  kernelWindowDivider.c
//

// This code is for managing kernelWindowDivider objects.
// These are just horizontal or vertical lines on the screen.

#include "kernelWindow.h"	// Our prototypes are here
#include "kernelError.h"


static int draw(kernelWindowComponent *component)
{
	// First draw the line.

	int status = 0;
	color tmpColor;

	tmpColor.red = (component->params.background.red / 2);
	tmpColor.green = (component->params.background.green / 2);
	tmpColor.blue = (component->params.background.blue / 2);

	if (component->width > component->height)
		// Horizontal line
		kernelGraphicDrawLine(component->buffer, &tmpColor, draw_normal,
			component->xCoord, component->yCoord,
			(component->xCoord + component->width - 2), component->yCoord);
	else
		// Vertical line
		kernelGraphicDrawLine(component->buffer, &tmpColor, draw_normal,
			component->xCoord, component->yCoord,
			component->xCoord, (component->yCoord + component->height - 2));

	tmpColor.red += (component->params.background.red / 3);
	tmpColor.green += (component->params.background.green / 3);
	tmpColor.blue += (component->params.background.blue / 3);

	if (component->width > component->height)
		// Horizontal line
		kernelGraphicDrawLine(component->buffer, &tmpColor, draw_normal,
			(component->xCoord + 1), (component->yCoord + 1),
			(component->xCoord + component->width - 1),
			(component->yCoord + 1));
	else
		// Vertical line
		kernelGraphicDrawLine(component->buffer, &tmpColor, draw_normal,
			(component->xCoord + 1), (component->yCoord + 1),
			(component->xCoord + 1),
			(component->yCoord + component->height - 1));

	return (status = 0);
}


/////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////
//
// Below here, the functions are exported for external use
//
/////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////

kernelWindowComponent *kernelWindowNewDivider(objectKey parent,
	dividerType type, componentParameters *params)
{
	// Formats a kernelWindowComponent as a kernelWindowDivider.

	kernelWindowComponent *component = NULL;

	// Check params
	if (!parent || !params)
	{
		kernelError(kernel_error, "NULL parameter");
		return (component = NULL);
	}

	if ((type != divider_horizontal) && (type != divider_vertical))
		return (component = NULL);

	// Get a new window component
	component = kernelWindowComponentNew(parent, params);
	if (!component)
		return (component);

	// Now override some bits
	if (type == divider_horizontal)
	{
		component->width = 3;
		component->height = 2;
		component->flags |= WINFLAG_RESIZABLEX;
	}
	else
	{
		component->width = 2;
		component->height = 3;
		component->flags |= WINFLAG_RESIZABLEY;
	}

	// The functions
	component->draw = &draw;

	return (component);
}

