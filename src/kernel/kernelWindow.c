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
//  kernelWindow.c
//

// This is the code that does all of the generic stuff for setting up
// GUI windows.

#include "kernelWindow.h"
#include "kernelDebug.h"
#include "kernelEnvironment.h"
#include "kernelError.h"
#include "kernelFileStream.h"
#include "kernelFilesystem.h"
#include "kernelFont.h"
#include "kernelImage.h"
#include "kernelLinkedList.h"
#include "kernelLoader.h"
#include "kernelLog.h"
#include "kernelMalloc.h"
#include "kernelMemory.h"
#include "kernelMisc.h"
#include "kernelMultitasker.h"
#include "kernelParameters.h"
#include "kernelUser.h"
#include "kernelWindowEventStream.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <values.h>
#include <sys/winconf.h>

static int initialized = 0;
static int screenWidth = 0;
static int screenHeight = 0;
static int winThreadPid = 0;

// Keeps the data for all the windows
static kernelLinkedList windowList;

// For receiving events from external sources, such as the mouse and keyboard
// drivers
static windowEventStream mouseEvents;
static windowEventStream keyEvents;

// For keeping track of where input event go
static kernelWindow *mouseInWindow = NULL;
static kernelWindow *focusWindow = NULL;
static kernelWindowComponent *draggingComponent = NULL;

// For any visible console window
kernelWindow *consoleWindow = NULL;
kernelWindowComponent *consoleTextArea = NULL;

// The whole window system uses these variables for drawing parameters,
// fonts, colors, etc.
kernelWindowVariables *windowVariables = NULL;


static inline int isAreaInside(screenArea *firstArea, screenArea *secondArea)
{
	// Return 1 if area 1 is 'inside' area 2

	if ((firstArea->leftX < secondArea->leftX) ||
		(firstArea->topY < secondArea->topY) ||
		(firstArea->rightX > secondArea->rightX) ||
		(firstArea->bottomY > secondArea->bottomY))
	{
		return (0);
	}
	else
	{
		// Yup, it's covered.
		return (1);
	}
}


static inline void getIntersectingArea(screenArea *firstArea,
	screenArea *secondArea, screenArea *overlap)
{
	// This will fill in the 'overlap' area with the coordinates shared by
	// the first two

	if (firstArea->leftX < secondArea->leftX)
		overlap->leftX = secondArea->leftX;
	else
		overlap->leftX = firstArea->leftX;

	if (firstArea->topY < secondArea->topY)
		overlap->topY = secondArea->topY;
	else
		overlap->topY = firstArea->topY;

	if (firstArea->rightX < secondArea->rightX)
		overlap->rightX = firstArea->rightX;
	else
		overlap->rightX = secondArea->rightX;

	if (firstArea->bottomY < secondArea->bottomY)
		overlap->bottomY = firstArea->bottomY;
	else
		overlap->bottomY = secondArea->bottomY;
}


static void addBorder(kernelWindow *window)
{
	// Adds the border components around the window

	componentParameters params;

	if (window->flags & WINFLAG_HASBORDER)
	{
		kernelError(kernel_error, "Window already has a border");
		return;
	}

	memset(&params, 0, sizeof(componentParameters));

	window->borders[0] = kernelWindowNewBorder(window->sysContainer,
		border_top, &params);

	window->borders[1] = kernelWindowNewBorder(window->sysContainer,
		border_left, &params);

	window->borders[2] = kernelWindowNewBorder(window->sysContainer,
		border_bottom, &params);

	window->borders[3] = kernelWindowNewBorder(window->sysContainer,
		border_right, &params);

	window->flags |= WINFLAG_HASBORDER;
}


static void removeBorder(kernelWindow *window)
{
	// Removes the borders from the window

	int count;

	if (!(window->flags & WINFLAG_HASBORDER))
	{
		kernelError(kernel_error, "Window doesn't have a border");
		return;
	}

	// Destroy the borders
	for (count = 0; count < 4; count ++)
	{
		if (window->borders[count])
		{
			kernelWindowComponentDestroy(window->borders[count]);
			window->borders[count] = NULL;
		}
	}

	window->flags &= ~WINFLAG_HASBORDER;
}


static void addTitleBar(kernelWindow *window)
{
	// Draws the title bar atop the window

	componentParameters params;

	if (window->titleBar)
	{
		kernelError(kernel_error, "Window already has a title bar");
		return;
	}

	// Standard parameters for a title bar
	memset(&params, 0, sizeof(componentParameters));

	window->titleBar = kernelWindowNewTitleBar(window, &params);
}


static void removeTitleBar(kernelWindow *window)
{
	// Removes the title bar from atop the window

	if (!window->titleBar)
	{
		kernelError(kernel_error, "Window doesn't have a title bar");
		return;
	}

	kernelWindowRemoveMinimizeButton(window);
	kernelWindowRemoveCloseButton(window);
	kernelWindowComponentDestroy(window->titleBar);
	window->titleBar = NULL;
}


static int tileBackgroundImage(kernelWindow *window)
{
	// This will tile the supplied image as the background of the window.
	// Naturally, any other components in the window's client area need to be
	// drawn after this bit.

	int status = 0;
	int clientAreaX = window->mainContainer->xCoord;
	int clientAreaY = window->mainContainer->yCoord;
	int clientAreaWidth = window->mainContainer->width;
	int clientAreaHeight = window->mainContainer->height;
	int count1, count2;

	// The window needs to have been assigned a background image
	if (!window->backgroundImage.data)
		return (status = ERR_NULLPARAMETER);

	kernelDebug(debug_gui, "Window buffer %ux%u mainContainer %d,%d %dx%d "
		"(%sdone layout)", window->buffer.width, window->buffer.height,
		clientAreaX, clientAreaY, clientAreaWidth, clientAreaHeight,
		(window->mainContainer->doneLayout? "" : "not "));

	if (((int) window->backgroundImage.width >= (clientAreaWidth / 2)) ||
		((int) window->backgroundImage.height >= (clientAreaHeight / 2)))
	{
		// Clear the main container with its background color
		kernelGraphicClearArea(&window->buffer, (color *) &window->background,
			clientAreaX, clientAreaY, clientAreaWidth, clientAreaHeight);

		// Set the image size to the same as the main container
		kernelImageResize((image *) &window->backgroundImage, clientAreaWidth,
			clientAreaHeight);

		// Draw the image over the window's main container
		status = kernelGraphicDrawImage(&window->buffer,
			(image *) &window->backgroundImage, draw_normal, clientAreaX,
			clientAreaY, 0 /* x offset */, 0 /* y offset */,
			0 /* full width */, 0 /* full height */);

		window->flags &= ~WINFLAG_BACKGROUNDTILED;
	}
	else
	{
		// Tile the image into the window's client area
		for (count1 = clientAreaY; count1 < clientAreaHeight;
			count1 += window->backgroundImage.height)
		{
			for (count2 = clientAreaX; count2 < clientAreaWidth;
				count2 += window->backgroundImage.width)
			{
				status = kernelGraphicDrawImage(&window->buffer, (image *)
					&window->backgroundImage, draw_normal, count2, count1,
					0 /* x offset */, 0 /* y offset */, 0 /* full width */,
					0 /* full height */);
			}
		}

		window->flags |= WINFLAG_BACKGROUNDTILED;
	}

	return (status);
}


static void getCoveredAreas(screenArea *visibleClip, screenArea *coveringClip,
	screenArea *coveredAreas, int *numCoveredAreas)
{
	// Utility function to simplify the renderVisiblePortions() function,
	// below

	int count;

	getIntersectingArea(visibleClip, coveringClip,
		&coveredAreas[*numCoveredAreas]);
	*numCoveredAreas += 1;

	// If the intersecting area is already covered by one of the other covered
	// areas, skip it.  Likewise if it covers another one, replace the other
	for (count = 0; count < (*numCoveredAreas - 1); count ++)
	{
		if (isAreaInside(&coveredAreas[*numCoveredAreas - 1],
			&coveredAreas[count]))
		{
			*numCoveredAreas -= 1;
			break;
		}
		else if (isAreaInside(&coveredAreas[count],
			&coveredAreas[*numCoveredAreas - 1]))
		{
			coveredAreas[count].leftX =
			coveredAreas[*numCoveredAreas - 1].leftX;
			coveredAreas[count].topY =
			coveredAreas[*numCoveredAreas - 1].topY;
			coveredAreas[count].rightX =
			coveredAreas[*numCoveredAreas - 1].rightX;
			coveredAreas[count].bottomY =
			coveredAreas[*numCoveredAreas - 1].bottomY;
			*numCoveredAreas -= 1;
			break;
		}
	}
}


static void renderVisiblePortions(kernelWindow *window,
	screenArea *bufferClip)
{
	// Takes the window supplied, and renders the portions of the supplied
	// clip which are visible (i.e. not covered by other windows).  Calls
	// kernelGraphicRenderBuffer() for all the visible bits.

	screenArea clipCopy;
	int numCoveredAreas = 0;
	screenArea coveredAreas[64];
	int numVisibleAreas = 1;
	screenArea visibleAreas[64];
	kernelLinkedListItem *iter = NULL;
	kernelWindow *listWindow = NULL;
	int count1, count2;

	// Can't put any debugging messages in here, because it will recurse.

	// Make a copy of the screen area in case we modify it
	memcpy(&clipCopy, bufferClip, sizeof(screenArea));
	bufferClip = &clipCopy;

	// Make sure we're not trying to draw outside the window buffer
	if (bufferClip->leftX < 0)
		bufferClip->leftX = 0;
	if (bufferClip->topY < 0)
		bufferClip->topY = 0;
	if (bufferClip->rightX >= window->buffer.width)
		bufferClip->rightX = (window->buffer.width - 1);
	if (bufferClip->bottomY >= window->buffer.height)
		bufferClip->bottomY = (window->buffer.height - 1);

	visibleAreas[0].leftX = (window->xCoord + bufferClip->leftX);
	visibleAreas[0].topY = (window->yCoord + bufferClip->topY);
	visibleAreas[0].rightX = (window->xCoord + bufferClip->rightX);
	visibleAreas[0].bottomY = (window->yCoord + bufferClip->bottomY);

	// Make sure we're not trying to draw outside the screen
	if (visibleAreas[0].leftX < 0)
		visibleAreas[0].leftX = 0;
	if (visibleAreas[0].topY < 0)
		visibleAreas[0].topY = 0;
	if (visibleAreas[0].rightX >= screenWidth)
		visibleAreas[0].rightX = (screenWidth - 1);
	if (visibleAreas[0].bottomY >= screenHeight)
		visibleAreas[0].bottomY = (screenHeight - 1);

	// Iterate through the window list.  Any window which intersects this area
	// and is at a higher level will reduce the visible area
	listWindow = kernelLinkedListIterStart(&windowList, &iter);
	while (listWindow)
	{
		if ((listWindow != window) && (listWindow->flags & WINFLAG_VISIBLE) &&
			(listWindow->level < window->level))
		{
			// The current window list item may be covering the supplied
			// window.  Find out if it totally covers it, in which case we
			// are finished
			if (isAreaInside(&visibleAreas[0],
				makeWindowScreenArea(listWindow)))
			{
				// Done
				return;
			}

			// Find out whether it otherwise intersects our window
			if (doAreasIntersect(&visibleAreas[0],
				makeWindowScreenArea(listWindow)))
			{
				// Yes, this window is covering ours somewhat.  We will need
				// to get the area of the windows that overlap
				getCoveredAreas(&visibleAreas[0],
					makeWindowScreenArea(listWindow), coveredAreas,
					&numCoveredAreas);
			}
		}

		listWindow = kernelLinkedListIterNext(&windowList, &iter);
	}

	// Now that we have a list of all the non-visible portions of the window,
	// we can make a list of the remaining parts that are visible

	// For each covering area, examine each visible area.  If the areas
	// intersect, then anywhere from 1 to 4 new visible areas will be
	// created
	for (count1 = 0; count1 < numCoveredAreas; count1 ++)
	{
		for (count2 = 0; count2 < numVisibleAreas; count2 ++)
		{
			if (!doAreasIntersect(&coveredAreas[count1],
				&visibleAreas[count2]))
			{
				continue;
			}

			if (visibleAreas[count2].leftX < coveredAreas[count1].leftX)
			{
				// The leftmost area of the visible area is unaffected.  Split
				// it by copying the rest to the end of the list, and
				// narrowing this area
				visibleAreas[numVisibleAreas].leftX =
					coveredAreas[count1].leftX;
				visibleAreas[numVisibleAreas].topY =
					visibleAreas[count2].topY;
				visibleAreas[numVisibleAreas].rightX =
					visibleAreas[count2].rightX;
				visibleAreas[numVisibleAreas++].bottomY =
					visibleAreas[count2].bottomY;

				visibleAreas[count2].rightX =
					(coveredAreas[count1].leftX - 1);
			}

			else if (visibleAreas[count2].topY < coveredAreas[count1].topY)
			{
				// The topmost area of the visible area is unaffected.  Split
				// it by copying the rest to the end of the list, and
				// shortening this area
				visibleAreas[numVisibleAreas].leftX =
					visibleAreas[count2].leftX;
				visibleAreas[numVisibleAreas].topY =
					coveredAreas[count1].topY;
				visibleAreas[numVisibleAreas].rightX =
				visibleAreas[count2].rightX;
				visibleAreas[numVisibleAreas++].bottomY =
					visibleAreas[count2].bottomY;

				visibleAreas[count2].bottomY =
					(coveredAreas[count1].topY - 1);
			}

			else if (visibleAreas[count2].rightX >
				coveredAreas[count1].rightX)
			{
				// The rightmost area of the visible area is unaffected.
				// Split it by copying the rest to the end of the list, and
				// narrowing this area
				visibleAreas[numVisibleAreas].leftX =
					visibleAreas[count2].leftX;
				visibleAreas[numVisibleAreas].topY =
					visibleAreas[count2].topY;
				visibleAreas[numVisibleAreas].rightX =
					coveredAreas[count1].rightX;
				visibleAreas[numVisibleAreas++].bottomY =
					visibleAreas[count2].bottomY;

				visibleAreas[count2].leftX =
					(coveredAreas[count1].rightX + 1);
			}

			else if (visibleAreas[count2].bottomY >
				coveredAreas[count1].bottomY)
			{
				// The bottom area of the visible area is unaffected.  Split
				// it by copying the rest to the end of the list, and
				// shortening this area
				visibleAreas[numVisibleAreas].leftX =
					visibleAreas[count2].leftX;
				visibleAreas[numVisibleAreas].topY =
					visibleAreas[count2].topY;
				visibleAreas[numVisibleAreas].rightX =
					visibleAreas[count2].rightX;
				visibleAreas[numVisibleAreas++].bottomY =
					coveredAreas[count1].bottomY;

				visibleAreas[count2].topY =
					(coveredAreas[count1].bottomY + 1);
			}

			else if (isAreaInside(&visibleAreas[count2],
				&coveredAreas[count1]))
			{
				// This area is not visible.  Get rid of it
				numVisibleAreas--;
				visibleAreas[count2].leftX =
					visibleAreas[numVisibleAreas].leftX;
				visibleAreas[count2].topY =
					visibleAreas[numVisibleAreas].topY;
				visibleAreas[count2].rightX =
					visibleAreas[numVisibleAreas].rightX;
				visibleAreas[count2].bottomY =
					visibleAreas[numVisibleAreas].bottomY;
				count2--;
			}
		}
	}

	// Render all of the visible portions
	for (count1 = 0; count1 < numVisibleAreas; count1 ++)
	{
		// All the clips were evaluated as absolute screen areas.  Now, adjust
		// each one so that it's a clip inside the window buffer.
		visibleAreas[count1].leftX -= window->xCoord;
		visibleAreas[count1].topY -= window->yCoord;
		visibleAreas[count1].rightX -= window->xCoord;
		visibleAreas[count1].bottomY -= window->yCoord;

		kernelGraphicRenderBuffer(&window->buffer, window->xCoord,
			window->yCoord, visibleAreas[count1].leftX,
			visibleAreas[count1].topY, (visibleAreas[count1].rightX -
				visibleAreas[count1].leftX + 1),
			(visibleAreas[count1].bottomY - visibleAreas[count1].topY + 1));
	}
}


static int drawWindowClip(kernelWindow *window, int xCoord, int yCoord,
	int width, int height)
{
	// Draws a clip of the client area of a window.  First blanks the bounded
	// area with the background color, then draws the appropriate clip of any
	// background image (which must already have been tiled, etc., from a
	// previous call to tileBackgroundImage()), then draws any visible
	// components that are entirely or partially within the bounded area (in
	// order of their 'level', lowermost to uppermost).  Finally calls
	// renderVisiblePortions() to put all the visible bits of the bounded area
	// on the screen.

	int status = 0;
	kernelWindowContainer *mainContainer = window->mainContainer->data;
	kernelWindowComponent **array = NULL;
	int numComponents = 0;
	kernelWindowComponent *component = NULL;
	int xOffset, yOffset;
	int lowestLevel = 0;
	int count1, count2;

	if (!(window->flags & WINFLAG_VISIBLE) ||
		!window->sysContainer->doneLayout)
	{
		return (status = 0);
	}

	kernelDebug(debug_gui, "Window '%s' draw clip (%d,%d %dx%d)",
		window->title, xCoord, yCoord, width, height);

	// Put the clip within the boundaries of the window
	if (xCoord < 0)
	{
		width += xCoord;
		xCoord = 0;
	}
	if (yCoord < 0)
	{
		height += yCoord;
		yCoord = 0;
	}
	if ((xCoord >= window->buffer.width) || (yCoord >= window->buffer.height))
		return (status = 0);
	if ((xCoord + width) > window->buffer.width)
		width -= ((xCoord + width) - window->buffer.width);
	if ((yCoord + height) > window->buffer.height)
		height -= ((yCoord + height) - window->buffer.height);
	if ((width <= 0) || (height <= 0))
		return (status = 0);

	// Blank the area with the window's background color
	kernelGraphicDrawRect(&window->buffer, (color *) &window->background,
		draw_normal, xCoord, yCoord, width, height, 0, 1);

	// If the window has a background image, draw it in this space
	if (window->backgroundImage.data)
	{
		if (window->flags & WINFLAG_BACKGROUNDTILED)
		{
			// If you want to study this next bit, and send me emails telling
			// me how clever I am, please do.
			yOffset = (yCoord % window->backgroundImage.height);
			for (count2 = yCoord; (count2 < (yCoord + height)); )
			{
				xOffset = (xCoord % window->backgroundImage.width);
				for (count1 = xCoord; (count1 < (xCoord + width)); )
				{
					kernelGraphicDrawImage(&window->buffer, (image *)
						&window->backgroundImage, draw_normal, count1, count2,
						xOffset, yOffset, ((xCoord + width) - count1),
						((yCoord + height) - count2));
					count1 += (window->backgroundImage.width - xOffset);
					xOffset = 0;
				}

				count2 += (window->backgroundImage.height - yOffset);
				yOffset = 0;
			}
		}
		else
		{
			// Draw the background once into our clip
			kernelGraphicDrawImage(&window->buffer, (image *)
				&window->backgroundImage, draw_normal, xCoord, yCoord,
				(xCoord - window->mainContainer->xCoord),
				(yCoord - window->mainContainer->yCoord), width, height);
		}
	}

	// Loop through all the regular window components that fall (partially)
	// within this space and draw them

	numComponents = (window->sysContainer->numComps(window->sysContainer) +
		window->mainContainer->numComps(window->mainContainer));

	array =	kernelMalloc(numComponents * sizeof(kernelWindowComponent *));
	if (!array)
		return (status = ERR_MEMORY);

	numComponents = 0;
	window->sysContainer->flatten(window->sysContainer, array, &numComponents,
		WINFLAG_VISIBLE);
	window->mainContainer->flatten(window->mainContainer, array,
		&numComponents, WINFLAG_VISIBLE);

	// NULL all components that are *not* at this location
	for (count1 = 0; count1 < numComponents; count1 ++)
	{
		component = array[count1];

		if (doAreasIntersect(&((screenArea)
			{ xCoord, yCoord, (xCoord + width - 1), (yCoord + height - 1) } ),
			&((screenArea){ (component->xCoord - 2), (component->yCoord - 2),
				(component->xCoord + component->width + 3),
				(component->yCoord + component->height + 3) } )))
		{
			if (component->level > lowestLevel)
				lowestLevel = component->level;
		}
		else
		{
			array[count1] = NULL;
		}
	}

	// Draw all the components by level, lowest to highest

	for (count1 = lowestLevel; count1 >= 0; count1 --)
	{
		for (count2 = 0; count2 < numComponents; count2 ++)
		{
			component = array[count2];

			if (component && (component->level == count1))
			{
				if (component->draw)
					component->draw(component);

				array[count2] = NULL;
			}
		}
	}

	kernelFree(array);

	if (window->flags & WINFLAG_DEBUGLAYOUT)
		mainContainer->drawGrid(window->mainContainer);

	// Only render the visible portions of the window
	renderVisiblePortions(window, &((screenArea)
		{ xCoord, yCoord, (xCoord + width - 1), (yCoord + height - 1) } ));

	// If the mouse is in this window, redraw it.
	if (isPointInside(kernelMouseGetX(), kernelMouseGetY(),
		makeWindowScreenArea(window)))
	{
		kernelMouseDraw();
	}

	return (status = 0);
}


static int drawWindow(kernelWindow *window)
{
	// Draws the whole window.  First blanks the buffer with the background
	// color, then calls tileBackgroundImage() to draw any background image,
	// then calls drawWindowClip() to draw and render the visible contents
	// on the screen.

	int status = 0;

	// Check parameters
	if (!window)
		return (status = ERR_NULLPARAMETER);

	kernelDebug(debug_gui, "Window '%s' draw", window->title);

	// Draw a blank background
	kernelGraphicDrawRect(&window->buffer, (color *) &window->background,
		draw_normal, 0, 0, window->buffer.width, window->buffer.height, 0, 1);

	// If the window has a background image, draw it
	if (window->backgroundImage.data)
		tileBackgroundImage(window);

	if (window->drawClip)
	{
		window->drawClip(window, 0, 0, window->buffer.width,
			window->buffer.height);
	}

	// Done
	return (status = 0);
}


static int windowUpdate(kernelWindow *window, int clipX, int clipY, int width,
	int height)
{
	// A component is trying to tell us that it has updated itself in the
	// supplied window, and would like the bounded area of the relevant window
	// to be redrawn on screen.  Does nothing if the window is not currently
	// visible.  Calls renderVisiblePortions() to draw only the visible
	// portions of the window clip.

	int status = 0;

	// Can't put any debugging messages in here, because it will recurse.

	// Check parameters
	if (!window)
		return (status = ERR_NULLPARAMETER);

	if (!(window->flags & WINFLAG_VISIBLE))
		// It's not currently on the screen
		return (status = 0);

	// Render the parts of this window's buffer that are currently visible.
	renderVisiblePortions(window, &((screenArea){ clipX, clipY,
		(clipX + (width - 1)), (clipY + (height - 1)) } ));

	// If the mouse is in this window, redraw it.
	if (isPointInside(kernelMouseGetX(), kernelMouseGetY(),
		makeWindowScreenArea(window)))
	{
		kernelMouseDraw();
	}

	return (status = 0);
}


static int getWindowGraphicBuffer(kernelWindow *window, int width,
	int height)
{
	// Allocate and attach memory to a kernelWindow for its graphicBuffer

	int status = 0;
	unsigned bufferBytes = 0;

	// Get the number of bytes of memory we need to reserve for this window's
	// graphicBuffer, depending on the size of the window
	bufferBytes = kernelGraphicCalculateAreaBytes(width, height);

	// Get some memory for it
	window->buffer.data = kernelMalloc(bufferBytes);
	if (!window->buffer.data)
		return (status = ERR_MEMORY);

	window->buffer.width = width;
	window->buffer.height = height;

	if (window->sysContainer && window->sysContainer->setBuffer)
	{
		window->sysContainer->setBuffer(window->sysContainer,
			&window->buffer);
	}

	if (window->mainContainer && window->mainContainer->setBuffer)
	{
		window->mainContainer->setBuffer(window->mainContainer,
			&window->buffer);
	}

	return (status = 0);
}


static int setWindowSize(kernelWindow *window, int width, int height)
{
	// Sets the size of a window

	int status = 0;
	void *oldBufferData = NULL;

	// Constrain to minimum width and height
	width = max(width, windowVariables->window.minWidth);
	height = max(height, windowVariables->window.minHeight);

	kernelDebug(debug_gui, "Window '%s' set size %dx%d", window->title, width,
		height);

	// Save the old graphic buffer data just in case
	oldBufferData = window->buffer.data;

	// Set the size.
	status = getWindowGraphicBuffer(window, width, height);
	if (status < 0)
	{
		kernelError(kernel_error, "Unable to get new window graphic buffer "
			"for resize operation");
		window->buffer.data = oldBufferData;
		return (status);
	}

	// Release the memory from the old buffer
	if (oldBufferData)
		kernelFree(oldBufferData);

	// Resize the system container.  This will also cause the main container
	// to be moved and resized appropriately
	if (window->sysContainer)
	{
		if (window->sysContainer->resize)
			window->sysContainer->resize(window->sysContainer, width, height);

		window->sysContainer->width = width;
		window->sysContainer->height = height;
	}

	// If the window is visible, redraw it
	if ((window->flags & WINFLAG_VISIBLE) && window->draw)
		window->draw(window);

	// Return success
	return (status = 0);
}


static int layoutWindow(kernelWindow *window)
{
	// (Re-)positions all the window's components based on their parameters

	int status = 0;

	// Layout the window's system container
	if (window->sysContainer)
	{
		if (window->sysContainer->layout)
		{
			kernelDebug(debug_gui, "Window '%s' layout system container",
				window->title);

			status = window->sysContainer->layout(window->sysContainer);
			if (status < 0)
				return (status);
		}
	}

	// Layout the window's main container
	if (window->mainContainer)
	{
		if (window->mainContainer->layout)
		{
			kernelDebug(debug_gui, "Window '%s' layout main container",
				window->title);

			status = window->mainContainer->layout(window->mainContainer);
			if (status < 0)
				return (status);
		}
	}

	return (status = 0);
}


static int autoSizeWindow(kernelWindow *window)
{
	// This will automatically set the size of a window based on the sizes
	// and locations of the components therein.

	int status = 0;
	int newWidth = 0;
	int newHeight = 0;

	newWidth = (window->mainContainer->xCoord + window->mainContainer->width);
	newHeight = (window->mainContainer->yCoord +
		window->mainContainer->height);

	// Adjust for right and bottom borders
	if (window->flags & WINFLAG_HASBORDER)
	{
		newWidth += windowVariables->border.thickness;
		newHeight += windowVariables->border.thickness;
	}

	if ((newWidth != window->buffer.width) ||
		(newHeight != window->buffer.height))
	{
		// Resize it
		status = setWindowSize(window, newWidth, newHeight);
		if (status < 0)
			return (status);
	}

	return (status = 0);
}


static int ensureWindowInitialLayout(kernelWindow *window)
{
	int status = 0;

	if ((window->sysContainer && !window->sysContainer->doneLayout) ||
		(window->mainContainer && !window->mainContainer->doneLayout))
	{
		kernelDebug(debug_gui, "Window '%s' do initial layout",
			window->title);

		status = layoutWindow(window);
		if (status < 0)
			return (status);

		status = autoSizeWindow(window);
		if (status < 0)
			return (status);
	}

	return (status = 0);
}


static int makeConsoleWindow(void)
{
	// Create the temporary console window

	int status = 0;
	componentParameters params;
	kernelWindowTextArea *textArea = NULL;
	kernelTextArea *oldArea = NULL;
	kernelTextArea *newArea = NULL;
	unsigned char *lineAddress = NULL;
	char lineBuffer[1024];
	int lineBufferCount = 0;
	int rowCount, columnCount;

	consoleWindow = kernelWindowNew(KERNELPROCID, WINNAME_TEMPCONSOLE);
	if (!consoleWindow)
		return (status = ERR_NOCREATE);

	memset(&params, 0, sizeof(componentParameters));
	params.gridWidth = 1;
	params.gridHeight = 1;
	params.orientationX = orient_center;
	params.orientationY = orient_middle;
	params.font = windowVariables->font.defaultFont;

	consoleTextArea = kernelWindowNewTextArea(consoleWindow, 80, 50,
		TEXT_DEFAULT_SCROLLBACKLINES, &params);
	if (!consoleTextArea)
	{
		kernelError(kernel_warn, "Unable to switch text areas to console "
			"window");
		return (status = ERR_NOCREATE);
	}

	textArea = consoleTextArea->data;
	oldArea = kernelTextGetConsoleOutput()->textArea;
	newArea = textArea->area;

	// Turn off the cursor
	newArea->cursorState = 0;

	// Redirect console and current text IO to this new area
	kernelTextSetConsoleInput(newArea->inputStream);
	kernelTextSetConsoleOutput(newArea->outputStream);

	kernelTextSetCurrentInput(newArea->inputStream);
	kernelTextSetCurrentOutput(newArea->outputStream);

	// Set the kernel's input and output streams as well
	kernelMultitaskerSetTextInput(KERNELPROCID, newArea->inputStream);
	kernelMultitaskerSetTextOutput(KERNELPROCID, newArea->outputStream);

	// Loop through contents of the current console area, and put them into
	// the buffer belonging to this new text area.  Remember that the new
	// text area might not (probably won't) have the same dimensions as the
	// previous one.  Note that this is not really important, and is mostly
	// just for showing off.
	for (rowCount = 0; ((rowCount < oldArea->rows) &&
		(rowCount < newArea->rows)); rowCount ++)
	{
		lineAddress =
			(oldArea->visibleData + (rowCount * oldArea->columns));
		lineBufferCount = 0;

		for (columnCount = 0; (columnCount < oldArea->columns) &&
			(columnCount < newArea->columns); columnCount ++)
		{
			lineBuffer[lineBufferCount++] = lineAddress[columnCount];
			if (lineAddress[columnCount] == '\n')
				break;
		}

		// Make sure there's a NULL
		lineBuffer[lineBufferCount] = '\0';

		if (lineBufferCount > 0)
			// Print the line to the new text area
			kernelTextStreamPrint(newArea->outputStream, lineBuffer);
	}

	// Deallocate the old, temporary area, but don't let it deallocate the
	// input/output streams.
	kernelWindowComponent *component = oldArea->windowComponent;
	if (component)
	{
		if (component->buffer)
			kernelFree((void *) component->buffer);

		kernelFree((void *) component);
	}

	kernelTextAreaDestroy(oldArea);

	return (status = 0);
}


static void componentToTop(kernelWindow *window,
	kernelWindowComponent *component)
{
	kernelWindowComponent **array = NULL;
	int numComponents = 0;
	int count;

	if (!window->mainContainer->numComps ||
		!(numComponents = window->mainContainer->
			numComps(window->mainContainer)))
	{
		return;
	}

	array = kernelMalloc(numComponents * sizeof(kernelWindowComponent *));
	if (!array)
		return;

	numComponents = 0;

	window->mainContainer->flatten(window->mainContainer, array,
		&numComponents, 0);

	// Find all the components at this location.  If a component's level is
	// currently 'higher', lower it.
	for (count = 0; count < numComponents; count ++)
	{
		if ((array[count] != component) &&
			(array[count]->level <= component->level))
		{
			if (doAreasIntersect(makeComponentScreenArea(component),
				makeComponentScreenArea(array[count])))
			{
				array[count]->level++;
			}
		}
	}

	kernelFree(array);

	// Insert our component at the top level.
	component->level = 0;
}


static int changeComponentFocus(kernelWindow *window,
	kernelWindowComponent *component)
{
	// Gets called when the focused component changes

	if (component && !(component->flags & WINFLAG_CANFOCUS))
	{
		kernelError(kernel_error, "Component cannot focus");
		return (ERR_INVALID);
	}

	if (window->focusComponent && (component != window->focusComponent))
	{
		kernelDebug(debug_gui, "Window unfocus component type %s",
			componentTypeString(window->focusComponent->type));

		window->focusComponent->flags &= ~WINFLAG_HASFOCUS;
		if (window->focusComponent->focus)
			window->focusComponent->focus(window->focusComponent, 0);
	}

	// This might be NULL.  That is okay.
	window->focusComponent = component;

	if (component)
	{
		kernelDebug(debug_gui, "Window focus component type %s",
			componentTypeString(component->type));

		if ((component->flags & WINFLAG_VISIBLE) &&
			(component->flags & WINFLAG_CANFOCUS))
		{
			component->flags |= WINFLAG_HASFOCUS;

			componentToTop(window, component);

			if (component->focus)
				component->focus(component, 1);
		}
	}

	return (0);
}


static void focusFirstComponent(kernelWindow *window)
{
	// Set the focus to the first focusable component

	kernelWindowComponent **array = NULL;
	int numComponents = 0;
	int count;

	if (!window->mainContainer->numComps ||
		!(numComponents = window->mainContainer->
			numComps(window->mainContainer)))
	{
		return;
	}

	// Flatten the window container so we can iterate through it
	array = kernelMalloc(numComponents * sizeof(kernelWindowComponent *));
	if (!array)
		return;

	numComponents = 0;

	window->mainContainer->flatten(window->mainContainer, array,
		&numComponents, (WINFLAG_VISIBLE | WINFLAG_ENABLED |
		WINFLAG_CANFOCUS));

	if (numComponents)
	{
		// If the window has any sort of text area or field inside it, set the
		// input/output streams to that process.
		for (count = 0; count < numComponents; count ++)
		{
			if (array[count]->type == textAreaComponentType)
			{
				window->changeComponentFocus(window, array[count]);
				break;
			}
		}

		// Still no focus?  Give it to the first component that can focus
		if (!window->focusComponent)
			window->changeComponentFocus(window, array[0]);
	}

	kernelFree(array);
}


static int focusNextComponent(kernelWindow *window)
{
	// Change the focus the next component

	int status = 0;
	kernelWindowComponent **array = NULL;
	int numComponents = 0;
	kernelWindowComponent *nextFocus = NULL;
	int count = 0;

	if (!window->focusComponent)
	{
		focusFirstComponent(window);
		return (status = 0);
	}

	// Get all the window components in a flat container
	array = kernelMalloc(window->mainContainer->
		numComps(window->mainContainer) * sizeof(kernelWindowComponent *));
	if (!array)
		return (status = ERR_MEMORY);

	window->mainContainer->flatten(window->mainContainer, array,
		&numComponents, (WINFLAG_VISIBLE | WINFLAG_ENABLED |
			WINFLAG_CANFOCUS));

	for (count = 0; count < numComponents; count ++)
	{
		if (array[count] == window->focusComponent)
		{
			if (count < (numComponents - 1))
				nextFocus = array[count + 1];
			else
				nextFocus = array[0];

			break;
		}
	}

	kernelFree(array);

	if (nextFocus)
		window->changeComponentFocus(window, nextFocus);
	else
		focusFirstComponent(window);

	return (status = 0);
}


static void windowFocus(kernelWindow *window, int focus)
{
	if (focus)
	{
		// Mark it as focused
		window->flags |= WINFLAG_HASFOCUS;

		// Draw the title bar as focused
		if (window->titleBar && window->titleBar->draw)
			window->titleBar->draw(window->titleBar);

		if (window->pointer)
			// Set the mouse pointer to that of the window
			kernelMouseSetPointer(window->pointer);

		// If there was a previously-focused component, give it the focus
		// again.
		if (window->focusComponent)
			changeComponentFocus(window, window->focusComponent);

		// Redraw the window
		windowUpdate(window, 0, 0, window->buffer.width,
			window->buffer.height);
	}
	else
	{
		// This is not the focus window any more.
		window->flags &= ~WINFLAG_HASFOCUS;

		// If there's a focused component, just tell it that it lost the
		// focus.  We'll give it back if the window regains focus.
		if (window->focusComponent)
		{
			window->focusComponent->flags &= ~WINFLAG_HASFOCUS;
			if (window->focusComponent->focus)
				window->focusComponent->focus(window->focusComponent, 0);
		}

		// Draw the title bar as unfocused
		if (window->titleBar)
		{
			if (window->titleBar->draw)
				window->titleBar->draw(window->titleBar);

			// Redraw the window's title bar area only
			windowUpdate(window, window->titleBar->xCoord,
				window->titleBar->yCoord, window->titleBar->width,
				window->titleBar->height);
		}
	}
}


static kernelWindow *getCoordinateWindow(int xCoord, int yCoord)
{
	// Find the topmost visible window that includes this coordinate

	kernelWindow *window = NULL;
	kernelWindow *listWindow = NULL;
	kernelLinkedListItem *iter = NULL;

	listWindow = kernelLinkedListIterStart(&windowList, &iter);
	while (listWindow)
	{
		if ((listWindow->flags & WINFLAG_VISIBLE) &&
			(isPointInside(xCoord, yCoord, makeWindowScreenArea(listWindow))))
		{
			// The coordinate is inside this window's coordinates.  Is it the
			// topmost such window we've found?
			if (!window || (listWindow->level < window->level))
				window = listWindow;
		}

		listWindow = kernelLinkedListIterNext(&windowList, &iter);
	}

	return (window);
}


static kernelWindowComponent *getEventComponent(kernelWindow *window,
	windowEvent *event)
{
	kernelWindowComponent *containerComponent = NULL;

	//kernelDebug(debug_gui, "Window \"%s\" get event component at %d, %d",
	//	window->title, event->xPosition, event->yPosition);

	if (window->mainContainer && isPointInside(event->xPosition,
		event->yPosition, makeComponentScreenArea(window->mainContainer)))
	{
		containerComponent = window->mainContainer;
	}

	else if (window->sysContainer && isPointInside(event->xPosition,
		event->yPosition, makeComponentScreenArea(window->sysContainer)))
	{
		containerComponent = window->sysContainer;
	}

	if (containerComponent)
	{
		// See if the container has a component to receive
		if (containerComponent->eventComp)
			return (containerComponent->eventComp(containerComponent, event));
		else
			return (containerComponent);
	}

	// Nothing found, we guess.  Should never happen.
	return (NULL);
}


static void mouseEnterExit(objectKey key, int enter)
{
	// This takes care of situations where the mouse has entered or left
	// a window or component.

	kernelWindow *window = NULL;
	kernelWindowComponent *component = NULL;
	windowEvent event;

	memset(&event, 0, sizeof(windowEvent));

	if (enter)
		// Send a "mouse enter" event to the window or component.
		event.type = EVENT_MOUSE_ENTER;
	else
		// Send a "mouse exit" event to the window or component.
		event.type = EVENT_MOUSE_EXIT;

	if (((kernelWindow *) key)->type == windowType)
	{
		window = key;
		kernelWindowEventStreamWrite(&window->events, &event);
	}
	else
	{
		component = key;

		if ((component->flags & WINFLAG_VISIBLE) &&
			(component->flags & WINFLAG_ENABLED))
		{
			if (component->mouseEvent)
				component->mouseEvent(component, &event);

			kernelWindowEventStreamWrite(&component->events, &event);
		}
	}

	if (enter)
	{
		if (window)
		{
			mouseInWindow = window;
			if (window->pointer)
				kernelMouseSetPointer(window->pointer);
		}
		else
		{
			component->window->mouseInComponent = component;
			if (component->pointer)
				kernelMouseSetPointer(component->pointer);
		}
	}
	else
	{
		if (window)
		{
			mouseInWindow = NULL;
			if (window->pointer)
				kernelMouseSetPointer(kernelMouseGetPointer(
					MOUSE_POINTER_DEFAULT));
			window->mouseInComponent = NULL;
		}
		else
		{
			component->window->mouseInComponent = NULL;
			if (component->pointer)
				kernelMouseSetPointer(component->window->pointer);
		}
	}
}


static void raiseContextMenu(kernelWindow *window,
	kernelWindowComponent *mainComponent,
	kernelWindowComponent *subComponent, windowEvent *event)
{
	// If there's a context menu, raise it.

	kernelWindow *contextMenu = NULL;

	contextMenu = window->contextMenu;

	if (mainComponent && ((mainComponent->flags & WINFLAG_VISIBLE) &&
		(mainComponent->flags & WINFLAG_ENABLED)))
	{
		if (subComponent && subComponent->contextMenu)
			contextMenu = subComponent->contextMenu;
		else if (mainComponent->contextMenu)
			contextMenu = mainComponent->contextMenu;
	}

	if (contextMenu)
	{
		kernelDebug(debug_gui, "Window show %s context menu",
			((contextMenu == window->contextMenu)? "window" : "component"));

		// Move the context menu
		contextMenu->xCoord = window->xCoord;
		contextMenu->yCoord = window->yCoord;

		// Adjust to the coordinates of the event or component
		if (event)
		{
			contextMenu->xCoord = event->xPosition;
			contextMenu->yCoord = event->yPosition;
		}
		else if (subComponent)
		{
			contextMenu->xCoord += (subComponent->xCoord +
				(subComponent->width / 2));
			contextMenu->yCoord += (subComponent->yCoord +
				(subComponent->height / 2));
		}
		else if (mainComponent)
		{
			contextMenu->xCoord += (mainComponent->xCoord +
				(mainComponent->width / 2));
			contextMenu->yCoord += (mainComponent->yCoord +
				(mainComponent->height / 2));
		}

		// Shouldn't go off the screen
		if ((contextMenu->xCoord + contextMenu->buffer.width) > screenWidth)
		{
			contextMenu->xCoord -= ((contextMenu->xCoord +
				contextMenu->buffer.width) - screenWidth);
		}
		if ((contextMenu->yCoord + contextMenu->buffer.height) > screenHeight)
		{
			contextMenu->yCoord -= ((contextMenu->yCoord +
				contextMenu->buffer.height) - screenHeight);
		}

		kernelWindowSetVisible(contextMenu, 1);
	}
}


static void processInputEvents(void)
{
	// This loops through the general mouse/keyboard event streams, and
	// generally directs events to the appropriate window and/or window
	// components.

	windowEvent event;
	windowEvent tmpEvent;
	kernelWindow *window = NULL;
	kernelWindowComponent *targetComponent = NULL;
	kernelWindowComponent *subComponent = NULL;

	while (kernelWindowEventStreamRead(&mouseEvents, &event) > 0)
	{
		// We have a mouse event

		window = NULL;
		targetComponent = NULL;

		// If it's just a "mouse move" event, we don't do the normal mouse
		// event processing but we create "mouse enter" or "mouse exit" events
		// for the windows as appropriate
		if (event.type == EVENT_MOUSE_MOVE)
		{
			// If there's another move event pending, skip to it, since we
			// don't care about where the mouse *used* to be.  We only care
			// about the current state.
			if (kernelWindowEventStreamPeek(&mouseEvents) == EVENT_MOUSE_MOVE)
				continue;

			// Figure out in which window the mouse moved, if any

			window = getCoordinateWindow(event.xPosition, event.yPosition);

			if (window != mouseInWindow)
			{
				// The mouse is not in the same window as before.

				if (mouseInWindow)
					mouseEnterExit(mouseInWindow, 0);

				if (window)
					mouseEnterExit(window, 1);
			}

			if (window)
			{
				if ((window->flags & WINFLAG_RESIZABLE) &&
					(window->flags & WINFLAG_HASBORDER))
				{
					// Is the mouse near the edges of the window?
					if ((window->flags & WINFLAG_RESIZABLEY) &&
						(event.yPosition < (window->yCoord + 3)))
					{
						targetComponent = window->borders[0];
					}
					else if ((window->flags & WINFLAG_RESIZABLEX) &&
						(event.xPosition < (window->xCoord + 5)))
					{
						targetComponent = window->borders[1];
					}
					else if ((window->flags & WINFLAG_RESIZABLEY) &&
						(event.yPosition > (window->yCoord +
							window->buffer.height - 6)))
					{
						targetComponent = window->borders[2];
					}
					else if ((window->flags & WINFLAG_RESIZABLEX) &&
						(event.xPosition > (window->xCoord +
							window->buffer.width - 6)))
					{
						targetComponent = window->borders[3];
					}
				}

				if (!targetComponent)
					// Is the mouse in any component of the window?
					targetComponent = getEventComponent(window, &event);

				if (targetComponent != window->mouseInComponent)
				{
					if (window->mouseInComponent)
						mouseEnterExit(window->mouseInComponent, 0);

					if (targetComponent)
						mouseEnterExit(targetComponent, 1);
				}
			}

			continue;
		}

		// Shortcut: If we are dragging a component, we know the target window
		// and component already
		else if (draggingComponent)
		{
			// If there's another dragging event pending, skip to it
			if (kernelWindowEventStreamPeek(&mouseEvents) == EVENT_MOUSE_DRAG)
				continue;

			window = draggingComponent->window;
			targetComponent = draggingComponent;
		}

		else
		{
			// Figure out which window this is happening to, if any

			window = getCoordinateWindow(event.xPosition, event.yPosition);
			if (!window)
				// This should never happen.  Anyway, ignore.
				continue;

			// The event was inside a window

			kernelDebug(debug_gui, "Window mouse event in window '%s'",
				window->title);

			// If it was a click and the window is not in focus, give it the
			// focus
			if (event.type & EVENT_MOUSE_DOWN)
			{
				if (window != focusWindow)
					// Give the window the focus
					kernelWindowFocus(window);

				// If the window has a dialog window, focus the dialog
				// instead, and we're finished
				if (window->dialogWindow)
				{
					if (window->dialogWindow != focusWindow)
						kernelWindowFocus(window->dialogWindow);
					return;
				}
			}

			// Find out if it was inside of any of the window's components,
			// and if so, put a windowEvent into its windowEventStream

			if ((window->flags & WINFLAG_RESIZABLE) &&
				(window->flags & WINFLAG_HASBORDER))
			{
				// Is the mouse near the edges of the window?
				if ((window->flags & WINFLAG_RESIZABLEY) &&
					(event.yPosition < (window->yCoord + 3)))
				{
					targetComponent = window->borders[0];
				}
				else if ((window->flags & WINFLAG_RESIZABLEX) &&
					(event.xPosition < (window->xCoord + 5)))
				{
					targetComponent = window->borders[1];
				}
				else if ((window->flags & WINFLAG_RESIZABLEY) &&
					(event.yPosition > (window->yCoord +
						window->buffer.height - 6)))
				{
					targetComponent = window->borders[2];
				}
				else if ((window->flags & WINFLAG_RESIZABLEX) &&
					(event.xPosition > (window->xCoord +
						window->buffer.width - 6)))
				{
					targetComponent = window->borders[3];
				}
			}

			if (!targetComponent)
				targetComponent = getEventComponent(window, &event);

			if (targetComponent)
				kernelDebug(debug_gui, "Window event component is type %s",
					componentTypeString(targetComponent->type));

			if ((event.type & EVENT_MOUSE_DOWN) &&
				(window->focusComponent != targetComponent))
			{
				if (targetComponent &&
					(targetComponent->flags & WINFLAG_CANFOCUS))
				{
					// Focus the new component.
					window->changeComponentFocus(window, targetComponent);
				}
			}
		}

		if (targetComponent)
		{
			if (targetComponent->mouseEvent)
				targetComponent->mouseEvent(targetComponent, &event);

			memcpy(&tmpEvent, &event, sizeof(windowEvent));

			// Adjust to the coordinates of the component
			tmpEvent.xPosition -= (window->xCoord + targetComponent->xCoord);
			tmpEvent.yPosition -= (window->yCoord + targetComponent->yCoord);

			// Put this mouse event into the component's windowEventStream
			kernelWindowEventStreamWrite(&targetComponent->events, &tmpEvent);

			if (event.type == EVENT_MOUSE_DRAG)
				draggingComponent = targetComponent;
		}

		if (event.type & EVENT_MOUSE_RIGHTDOWN)
		{
			// The user right-clicked.  If there's a context menu, raise it.
			raiseContextMenu(window, targetComponent,
				NULL /* no sub-component */, &event);
		}

		// If we were dragging something, have we stopped dragging it?
		if (event.type != EVENT_MOUSE_DRAG)
			draggingComponent = NULL;

		// Finally, if the window has a 'mouse event' function defined, call
		// it
		if (window->mouseEvent)
			window->mouseEvent(window, targetComponent, &event);
	}

	while (kernelWindowEventStreamRead(&keyEvents, &event) > 0)
	{
		// It was a keyboard event

		if (focusWindow)
		{
			kernelDebug(debug_gui, "Window key event window is %s",
				focusWindow->title);

			targetComponent = NULL;

			// If it was a [tab] down, focus the next component
			if ((!focusWindow->focusComponent ||
				!(focusWindow->focusComponent->params.flags &
					WINDOW_COMPFLAG_STICKYFOCUS)) &&
				((event.type == EVENT_KEY_DOWN) && (event.key == keyTab)))
			{
				focusNextComponent(focusWindow);
			}

			else
			{
				if (focusWindow->focusComponent)
					targetComponent = focusWindow->focusComponent;

				if (targetComponent)
				{
					if ((targetComponent->flags & WINFLAG_VISIBLE) &&
						(targetComponent->flags & WINFLAG_ENABLED))
					{
						if (targetComponent->keyEvent)
						{
							targetComponent->keyEvent(targetComponent,
								&event);
						}

						// Put this key event into the component's
						// windowEventStream
						kernelWindowEventStreamWrite(&targetComponent->events,
							&event);
					}
				}

				if ((event.key == keyA4) && (event.type == EVENT_KEY_DOWN))
				{
					// The user pressed the 'menu' key.  If there's a context
					// menu, raise it.
					subComponent = NULL;
					if (targetComponent && targetComponent->activeComp)
					{
						subComponent =
							targetComponent->activeComp(targetComponent);
					}

					raiseContextMenu(focusWindow, targetComponent,
						subComponent, NULL /* no event */);
				}

				// Finally, if the window has a 'key event' function defined,
				// call it
				if (focusWindow->keyEvent)
				{
					focusWindow->keyEvent(focusWindow, targetComponent,
						&event);
				}
			}
		}
		else
		{
			kernelDebug(debug_gui, "Window no window for key event");
		}
	}
}


__attribute__((noreturn))
static void windowThread(void)
{
	// This is the 'window thread' which processes the global event streams
	// for things like mouse clicks and key presses, which are dispatched to
	// the relevant windows or components, and also watches for global things
	// like refresh requests.

	kernelWindow *listWindow = NULL;
	kernelLinkedListItem *iter = NULL;
	int processId = 0;
	kernelWindowContainer *container = NULL;
	kernelWindowComponent *component = NULL;
	windowEvent event;
	int count;

	while (1)
	{
		// Process the pending input event streams, to put events into the
		// appropriate components
		processInputEvents();

		listWindow = kernelLinkedListIterStart(&windowList, &iter);
		while (listWindow)
		{
			processId = listWindow->processId;

			// Check to see whether the process that owns the window is still
			// alive.  If not, destroy the window and quit for this loop.
			if (!kernelMultitaskerProcessIsAlive(processId))
			{
				kernelWindowDestroy(listWindow);
				break;
			}

			container = listWindow->sysContainer->data;

			// Look for events in 'system' components
			for (count = 0; count < container->numComponents; count ++)
			{
				component = container->components[count];

				// Any handler for the component?  Any events pending?
				if (component->eventHandler &&
					(kernelWindowEventStreamRead(&component->events,
						&event) > 0))
				{
					component->eventHandler(component, &event);

					// Window closed?  Don't want to loop here any more.
					if (!kernelMultitaskerProcessIsAlive(processId))
						break;
				}
			}

			listWindow = kernelLinkedListIterNext(&windowList, &iter);
		}

		// Done
		kernelMultitaskerYield();
	}
}


static int spawnWindowThread(void)
{
	// Spawn the window thread
	winThreadPid = kernelMultitaskerSpawnKernelThread(windowThread,
		"window thread", 0, NULL);
	if (winThreadPid < 0)
		return (winThreadPid);

	kernelLog("Window thread started");
	return (0);
}


static kernelWindow *findTopmostWindow(unsigned flags)
{
	kernelWindow *topmostWindow = NULL;
	kernelWindow *listWindow = NULL;
	kernelLinkedListItem *iter = NULL;
	int topmostLevel = MAXINT;

	// Find the topmost window
	listWindow = kernelLinkedListIterStart(&windowList, &iter);
	while (listWindow)
	{
		if (((listWindow->flags & flags) == flags) &&
			(listWindow->level < topmostLevel))
		{
			topmostWindow = listWindow;
			topmostLevel = listWindow->level;
		}

		listWindow = kernelLinkedListIterNext(&windowList, &iter);
	}

	return (topmostWindow);
}


static int readFileVariables(const char *fileName)
{
	int status = 0;
	variableList settings;
	const char *value = NULL;

	memset(&settings, 0, sizeof(variableList));

	// Does the config file exist?
	status = kernelFileFind(fileName, NULL);
	if (status < 0)
		return (status);

	// Now read the config file to let it overrides our defaults.
	status = kernelConfigRead(fileName, &settings);
	if (status < 0)
		return (status);

	if ((value = kernelVariableListGet(&settings, WINVAR_COLOR_FG_RED)) &&
		(atoi(value) >= 0))
	{
		windowVariables->color.foreground.red = atoi(value);
	}
	if ((value = kernelVariableListGet(&settings, WINVAR_COLOR_FG_GREEN)) &&
		(atoi(value) >= 0))
	{
		windowVariables->color.foreground.green = atoi(value);
	}
	if ((value = kernelVariableListGet(&settings, WINVAR_COLOR_FG_BLUE)) &&
		(atoi(value) >= 0))
	{
		windowVariables->color.foreground.blue = atoi(value);
	}
	if ((value = kernelVariableListGet(&settings, WINVAR_COLOR_BG_RED)) &&
		(atoi(value) >= 0))
	{
		windowVariables->color.background.red = atoi(value);
	}
	if ((value = kernelVariableListGet(&settings, WINVAR_COLOR_BG_GREEN)) &&
		(atoi(value) >= 0))
	{
		windowVariables->color.background.green = atoi(value);
	}
	if ((value = kernelVariableListGet(&settings, WINVAR_COLOR_BG_BLUE)) &&
		(atoi(value) >= 0))
	{
		windowVariables->color.background.blue = atoi(value);
	}
	if ((value = kernelVariableListGet(&settings, WINVAR_COLOR_DT_RED)) &&
		(atoi(value) >= 0))
	{
		windowVariables->color.desktop.red = atoi(value);
	}
	if ((value = kernelVariableListGet(&settings, WINVAR_COLOR_DT_GREEN)) &&
		(atoi(value) >= 0))
	{
		windowVariables->color.desktop.green = atoi(value);
	}
	if ((value = kernelVariableListGet(&settings, WINVAR_COLOR_DT_BLUE)) &&
		(atoi(value) >= 0))
	{
		windowVariables->color.desktop.blue = atoi(value);
	}
	if ((value = kernelVariableListGet(&settings, WINVAR_WINDOW_MINWIDTH)) &&
		(atoi(value) >= 0))
	{
		windowVariables->window.minWidth = atoi(value);
	}
	if ((value = kernelVariableListGet(&settings, WINVAR_WINDOW_MINHEIGHT)) &&
		(atoi(value) >= 0))
	{
		windowVariables->window.minHeight = atoi(value);
	}
	if ((value = kernelVariableListGet(&settings, WINVAR_MINREST_TRACERS)) &&
		(atoi(value) >= 0))
	{
		windowVariables->window.minRestTracers = atoi(value);
	}
	if ((value = kernelVariableListGet(&settings, WINVAR_TITLEBAR_HEIGHT)) &&
		(atoi(value) >= 0))
	{
		windowVariables->titleBar.height = atoi(value);
	}
	if ((value = kernelVariableListGet(&settings, WINVAR_TITLEBAR_MINWIDTH)) &&
		(atoi(value) >= 0))
	{
		windowVariables->titleBar.minWidth = atoi(value);
	}
	if ((value = kernelVariableListGet(&settings, WINVAR_BORDER_THICKNESS)) &&
		(atoi(value) >= 0))
	{
		windowVariables->border.thickness = atoi(value);
	}
	if ((value = kernelVariableListGet(&settings,
		WINVAR_BORDER_SHADINGINCR)) && (atoi(value) >= 0))
	{
		windowVariables->border.shadingIncrement = atoi(value);
	}
	if ((value = kernelVariableListGet(&settings, WINVAR_RADIOBUTTON_SIZE)) &&
		(atoi(value) >= 0))
	{
		windowVariables->radioButton.size = atoi(value);
	}
	if ((value = kernelVariableListGet(&settings, WINVAR_CHECKBOX_SIZE)) &&
		(atoi(value) >= 0))
	{
		windowVariables->checkbox.size = atoi(value);
	}
	if ((value = kernelVariableListGet(&settings, WINVAR_SLIDER_WIDTH)) &&
		(atoi(value) >= 0))
	{
		windowVariables->slider.width = atoi(value);
	}
	if ((value = kernelVariableListGet(&settings,
		WINVAR_FONT_FIXW_SM_FAMILY)))
	{
		strncpy(windowVariables->font.fixWidth.small.family, value,
			FONT_FAMILY_LEN);
	}
	windowVariables->font.fixWidth.small.flags = FONT_STYLEFLAG_FIXED;
	if ((value = kernelVariableListGet(&settings, WINVAR_FONT_FIXW_SM_FLAGS)))
	{
		if (!strncmp(value, WINVAR_FONT_FLAG_BOLD, 128))
			windowVariables->font.fixWidth.small.flags |= FONT_STYLEFLAG_BOLD;
	}
	if ((value = kernelVariableListGet(&settings,
		WINVAR_FONT_FIXW_SM_POINTS)) && (atoi(value) >= 0))
	{
		windowVariables->font.fixWidth.small.points = atoi(value);
	}
	if ((value = kernelVariableListGet(&settings,
		WINVAR_FONT_FIXW_MD_FAMILY)))
	{
		strncpy(windowVariables->font.fixWidth.medium.family, value,
			FONT_FAMILY_LEN);
	}
	windowVariables->font.fixWidth.medium.flags = FONT_STYLEFLAG_FIXED;
	if ((value = kernelVariableListGet(&settings, WINVAR_FONT_FIXW_MD_FLAGS)))
	{
		if (!strncmp(value, WINVAR_FONT_FLAG_BOLD, 128))
		{
			windowVariables->font.fixWidth.medium.flags |=
				FONT_STYLEFLAG_BOLD;
		}
	}
	if ((value = kernelVariableListGet(&settings,
		WINVAR_FONT_FIXW_MD_POINTS)) && (atoi(value) >= 0))
	{
		windowVariables->font.fixWidth.medium.points = atoi(value);
	}
	if ((value = kernelVariableListGet(&settings,
		WINVAR_FONT_VARW_SM_FAMILY)))
	{
		strncpy(windowVariables->font.varWidth.small.family, value,
			FONT_FAMILY_LEN);
	}
	if ((value = kernelVariableListGet(&settings, WINVAR_FONT_VARW_SM_FLAGS)))
	{
		if (!strncmp(value, WINVAR_FONT_FLAG_BOLD, 128))
			windowVariables->font.varWidth.small.flags |= FONT_STYLEFLAG_BOLD;
	}
	if ((value = kernelVariableListGet(&settings,
		WINVAR_FONT_VARW_SM_POINTS)) && (atoi(value) >= 0))
	{
		windowVariables->font.varWidth.small.points = atoi(value);
	}
	if ((value = kernelVariableListGet(&settings,
		WINVAR_FONT_VARW_MD_FAMILY)))
	{
		strncpy(windowVariables->font.varWidth.medium.family, value,
			FONT_FAMILY_LEN);
	}
	if ((value = kernelVariableListGet(&settings, WINVAR_FONT_VARW_MD_FLAGS)))
	{
		if (!strncmp(value, WINVAR_FONT_FLAG_BOLD, 128))
		{
			windowVariables->font.varWidth.medium.flags |=
				FONT_STYLEFLAG_BOLD;
		}
	}
	if ((value = kernelVariableListGet(&settings,
		WINVAR_FONT_VARW_MD_POINTS)) && (atoi(value) >= 0))
	{
		windowVariables->font.varWidth.medium.points = atoi(value);
	}

	kernelVariableListDestroy(&settings);
	return (status = 0);
}


static int setupWindowVariables(void)
{
	// This allocates and sets up a global structure for window variables,
	// used by functions in this file as well as window components, etc.
	// Called once at startup time by the windowStart() function, below.

	int status = 0;

	extern color kernelDefaultForeground;
	extern color kernelDefaultBackground;
	extern color kernelDefaultDesktop;

	windowVariables = kernelMalloc(sizeof(kernelWindowVariables));
	if (!windowVariables)
		return (status = ERR_MEMORY);

	memset(windowVariables, 0, sizeof(windowVariables));

	// Set defaults

	// Variables for colors
	memcpy(&windowVariables->color.foreground, &kernelDefaultForeground,
		sizeof(color));
	memcpy(&windowVariables->color.background, &kernelDefaultBackground,
		sizeof(color));
	memcpy(&windowVariables->color.desktop, &kernelDefaultDesktop,
		sizeof(color));

	// Variables for windows
	windowVariables->window.minWidth = WINDOW_DEFAULT_MIN_WIDTH;
	windowVariables->window.minHeight = WINDOW_DEFAULT_MIN_HEIGHT;
	windowVariables->window.minRestTracers = WINDOW_DEFAULT_MINREST_TRACERS;

	// Variables for title bars
	windowVariables->titleBar.height = WINDOW_DEFAULT_TITLEBAR_HEIGHT;
	windowVariables->titleBar.minWidth = WINDOW_DEFAULT_TITLEBAR_MINWIDTH;

	// Variables for borders
	windowVariables->border.thickness = WINDOW_DEFAULT_BORDER_THICKNESS;
	windowVariables->border.shadingIncrement =
		WINDOW_DEFAULT_SHADING_INCREMENT;

	// Variables for radio buttons
	windowVariables->radioButton.size = WINDOW_DEFAULT_RADIOBUTTON_SIZE;

	// Variables for checkboxes
	windowVariables->checkbox.size = WINDOW_DEFAULT_CHECKBOX_SIZE;

	// Variables for sliders and scroll bars
	windowVariables->slider.width = WINDOW_DEFAULT_SLIDER_WIDTH;

	// The small fixed-width font
	strcpy(windowVariables->font.fixWidth.small.family,
		WINDOW_DEFAULT_FIXFONT_SMALL_FAMILY);
	windowVariables->font.fixWidth.small.flags =
		WINDOW_DEFAULT_FIXFONT_SMALL_FLAGS;
	windowVariables->font.fixWidth.small.points =
		WINDOW_DEFAULT_FIXFONT_SMALL_POINTS;

	// The medium fixed-width font
	strcpy(windowVariables->font.fixWidth.medium.family,
		WINDOW_DEFAULT_FIXFONT_MEDIUM_FAMILY);
	windowVariables->font.fixWidth.medium.flags =
		WINDOW_DEFAULT_FIXFONT_MEDIUM_FLAGS;
	windowVariables->font.fixWidth.medium.points =
		WINDOW_DEFAULT_FIXFONT_MEDIUM_POINTS;

	// The small variable-width font
	strcpy(windowVariables->font.varWidth.small.family,
		WINDOW_DEFAULT_VARFONT_SMALL_FAMILY);
	windowVariables->font.varWidth.small.flags =
		WINDOW_DEFAULT_VARFONT_SMALL_FLAGS;
	windowVariables->font.varWidth.small.points =
		WINDOW_DEFAULT_VARFONT_SMALL_POINTS;

	// The medium variable-width font
	strcpy(windowVariables->font.varWidth.medium.family,
		WINDOW_DEFAULT_VARFONT_MEDIUM_FAMILY);
	windowVariables->font.varWidth.medium.flags =
		WINDOW_DEFAULT_VARFONT_MEDIUM_FLAGS;
	windowVariables->font.varWidth.medium.points =
		WINDOW_DEFAULT_VARFONT_MEDIUM_POINTS;

	// Now read the system window config file to let it overrides our defaults
	readFileVariables(PATH_SYSTEM_CONFIG "/" WINDOW_CONFIG);

	// Load fonts.  Don't fail the whole initialization just because we can't
	// read one, or anything like that.

	windowVariables->font.fixWidth.small.font =
		kernelFontGet(windowVariables->font.fixWidth.small.family,
			windowVariables->font.fixWidth.small.flags,
			windowVariables->font.fixWidth.small.points, NULL);

	if (!windowVariables->font.fixWidth.small.font)
	{
		// Try the built-in system font
		status = kernelFontGetSystem(
			&windowVariables->font.fixWidth.small.font);
		if (status < 0)
			// This would be sort of serious
			return (status);
	}

	// Use this as our default
	windowVariables->font.defaultFont =
		windowVariables->font.fixWidth.small.font;

	windowVariables->font.fixWidth.medium.font =
		kernelFontGet(windowVariables->font.fixWidth.medium.family,
			windowVariables->font.fixWidth.medium.flags,
			windowVariables->font.fixWidth.medium.points, NULL);

	windowVariables->font.varWidth.small.font =
		kernelFontGet(windowVariables->font.varWidth.small.family,
			windowVariables->font.varWidth.small.flags,
			windowVariables->font.varWidth.small.points, NULL);
	if (!windowVariables->font.varWidth.small.font)
		windowVariables->font.varWidth.small.font =
			windowVariables->font.defaultFont;

	windowVariables->font.varWidth.medium.font =
		kernelFontGet(windowVariables->font.varWidth.medium.family,
			windowVariables->font.varWidth.medium.flags,
			windowVariables->font.varWidth.medium.points, NULL);
	if (!windowVariables->font.varWidth.medium.font)
		windowVariables->font.varWidth.medium.font =
			windowVariables->font.defaultFont;

	return (status = 0);
}


static int windowStart(void)
{
	// This does all of the startup stuff.  Gets called once during system
	// initialization.

	int status = 0;
	kernelTextOutputStream *output = NULL;

	// Set up window variables
	status = setupWindowVariables();
	if (status < 0)
		return (status);

	// Set the temporary text area to the current desktop color, for neatness'
	// sake if there are any error messages before we create the console
	// window
	output = kernelMultitaskerGetTextOutput();
	output->textArea->background.red = windowVariables->color.desktop.red;
	output->textArea->background.green = windowVariables->color.desktop.green;
	output->textArea->background.blue = windowVariables->color.desktop.blue;

	// Clear the screen with our default desktop color
	kernelGraphicClearScreen(&windowVariables->color.desktop);

	// Initialize the event streams
	if ((kernelWindowEventStreamNew(&mouseEvents) < 0) ||
		(kernelWindowEventStreamNew(&keyEvents) < 0))
	{
		return (status = ERR_NOTINITIALIZED);
	}

	// Spawn the window thread
	spawnWindowThread();

	// We're initialized
	initialized = 1;

	// Draw the console and root windows, but don't make them visible
	makeConsoleWindow();
	//kernelWindowSetLocation(consoleWindow, 0, 0);
	//kernelWindowSetVisible(consoleWindow, 1);

	// Done
	return (status = 0);
}


static kernelWindow *createWindow(int processId, const char *title)
{
	// Creates a new window using the supplied values.  Not visible by
	// default.

	int status = 0;
	kernelWindow *window = NULL;
	kernelWindow *listWindow = NULL;
	kernelLinkedListItem *iter = NULL;
	componentParameters params;
	int bottomLevel = 0;

	memset(&params, 0, sizeof(componentParameters));

	// Get some memory for window data
	window = kernelMalloc(sizeof(kernelWindow));
	if (!window)
		return (window);

	window->type = windowType;

	// Set the process Id
	window->processId = processId;

	// Try to determine what character set we'll be using for this window
	if (kernelEnvironmentGet(ENV_CHARSET, (char *) window->charSet,
		CHARSET_NAME_LEN) < 0)
	{
		strcpy((char *) window->charSet, CHARSET_NAME_DEFAULT);
	}

	kernelWindowSetCharSet(window, (char *) window->charSet);

	// The title
	strncpy((char *) window->title, title, WINDOW_MAX_TITLE_LENGTH);
	window->title[WINDOW_MAX_TITLE_LENGTH - 1] = '\0';

	// Set the coordinates to -1 initially
	window->xCoord = -1;
	window->yCoord = -1;

	// New windows get put at the bottom level until they are marked as
	// visible
	listWindow = kernelLinkedListIterStart(&windowList, &iter);
	while (listWindow)
	{
		if (!(listWindow->flags & WINFLAG_ROOTWINDOW) &&
			(listWindow->level > bottomLevel))
		{
			bottomLevel = listWindow->level;
		}

		listWindow = kernelLinkedListIterNext(&windowList, &iter);
	}

	window->level = (bottomLevel + 1);

	// By default windows are movable and resizable, and can focus
	window->flags |= (WINFLAG_MOVABLE | WINFLAG_RESIZABLE | WINFLAG_CANFOCUS);
	window->backgroundImage.data = NULL;

	// A new window doesn't have the focus until it is marked as visible,
	// and it's not visible until someone tells us to make it visible
	window->flags &= ~(WINFLAG_HASFOCUS | WINFLAG_VISIBLE);

	// Get the window's initial graphic buffer all set up (full screen width
	// and height)
	status = getWindowGraphicBuffer(window, kernelGraphicGetScreenWidth(),
		kernelGraphicGetScreenHeight());
	if (status < 0)
	{
		kernelError(kernel_error, "Couldn't get a graphic buffer");
		kernelFree((void *) window);
		return (window = NULL);
	}

	// Set the window's background color to the default
	window->background.red = windowVariables->color.background.red;
	window->background.green = windowVariables->color.background.green;
	window->background.blue = windowVariables->color.background.blue;

	// Add an event stream for the window
	status = kernelWindowEventStreamNew(&window->events);
	if (status < 0)
	{
		kernelFree((void *) window);
		return (window = NULL);
	}

	window->pointer = kernelMouseGetPointer(MOUSE_POINTER_DEFAULT);

	// Add top-level containers for other components
	window->sysContainer = kernelWindowNewSysContainer(window, &params);
	if (!window->sysContainer)
	{
		kernelError(kernel_error, "Couldn't create the system container");
		kernelFree((void *) window);
		return (window = NULL);
	}

	window->mainContainer = kernelWindowNewContainer(window, "mainContainer",
		&params);
	if (!window->mainContainer)
	{
		kernelError(kernel_error, "Couldn't create the main container");
		kernelFree((void *) window);
		return (window = NULL);
	}

	// Add the border components
	addBorder(window);

	// Add the title bar component
	addTitleBar(window);

	// Set up the functions
	window->draw = &drawWindow;
	window->drawClip = &drawWindowClip;
	window->update = &windowUpdate;
	window->changeComponentFocus = &changeComponentFocus;
	window->focus = &windowFocus;

	// Add the window to the list
	kernelLinkedListAdd(&windowList, (void *) window);

	// Return the window
	return (window);
}


/////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////
//
// Below here, the functions are exported for external use
//
/////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////

int kernelWindowInitialize(void)
{
	// Called during kernel initialization

	int status = 0;

	kernelLog("Starting window system initialization");

	// Initialize the window list
	memset((void *) &windowList, 0, sizeof(kernelWindowList));

	// Screen parameters
	screenWidth = kernelGraphicGetScreenWidth();
	screenHeight = kernelGraphicGetScreenHeight();

	status = windowStart();
	if (status < 0)
		return (status);

	// Switch to the 'default' mouse pointer
	kernelWindowSwitchPointer(NULL, MOUSE_POINTER_DEFAULT);

	kernelLog("Window system initialization complete");

	return (status = 0);
}


int kernelWindowLogin(const char *userName)
{
	// This gets called after the user has logged in.

	char fileName[MAX_PATH_NAME_LENGTH];
	int winShellPid = 0;

	// Make sure we've been initialized
	if (!initialized)
		return (ERR_NOTINITIALIZED);

	// Read any user-specific config variables
	sprintf(fileName, PATH_USERS_CONFIG "/" WINDOW_CONFIG, userName);
	readFileVariables(fileName);

	// Start the window shell thread
	winShellPid = kernelWindowShell(userName);
	if (winShellPid < 0)
		return (winShellPid);

	// Make its input and output streams be the console
	kernelMultitaskerSetTextInput(winShellPid, kernelTextGetConsoleInput());
	kernelMultitaskerSetTextOutput(winShellPid, kernelTextGetConsoleOutput());

	return (winShellPid);
}


int kernelWindowLogout(void)
{
	// This gets called after the user has logged out.

	// Loop through all the windows, closing everything except the console
	// window

	int status = 0;
	kernelWindow *listWindow = NULL;
	kernelLinkedListItem *iter = NULL;

	// Make sure we've been initialized
	if (!initialized)
		return (status = ERR_NOTINITIALIZED);

	// Destroy all the windows
	listWindow = kernelLinkedListIterStart(&windowList, &iter);
	while (listWindow)
	{
		// Skip the console window.  Also skip child windows, since they will
		// be destroyed recursively when their parents are (and they may
		// belong to other windows we're skipping)
		if ((listWindow == consoleWindow) || listWindow->parentWindow)
		{
			listWindow = kernelLinkedListIterNext(&windowList, &iter);
			continue;
		}

		if ((listWindow->processId != KERNELPROCID) &&
			kernelMultitaskerProcessIsAlive(listWindow->processId))
		{
			// Kill the process that owns the window
			kernelMultitaskerKillProcess(listWindow->processId, 0);
		}

		// Destroy the window
		status = kernelWindowDestroy(listWindow);
		if (status < 0)
		{
			// May still be in the list.  We don't want to get stuck, so carry
			// on.
			listWindow = kernelLinkedListIterNext(&windowList, &iter);
		}
		else
		{
			// Restart the loop
			listWindow = kernelLinkedListIterStart(&windowList, &iter);
		}
	}

	// Re-read the system config variables
	readFileVariables(PATH_SYSTEM_CONFIG "/" WINDOW_CONFIG);

	return (status = 0);
}


kernelWindow *kernelWindowNew(int processId, const char *title)
{
	// Creates a new window using the supplied values.  Not visible by
	// default.

	kernelWindow *window = NULL;

	// Make sure we've been initialized
	if (!initialized)
		return (window = NULL);

	// Check params
	if (!title)
		return (window = NULL);

	window = createWindow(processId, title);
	if (!window)
		return (window);

	kernelWindowShellUpdateList(&windowList);

	// Return the window
	return (window);
}


kernelWindow *kernelWindowNewChild(kernelWindow *parentWindow,
	const char *title)
{
	// Creates a new 'child' window, tied to the parent window, using the
	// supplied values.  Not visible by default.

	kernelWindow *newChild = NULL;

	// Make sure we've been initialized
	if (!initialized)
		return (newChild = NULL);

	// Check params
	if (!parentWindow || !title)
		return (newChild = NULL);

	// Don't try to make too many child windows
	if (parentWindow->numChildren >= WINDOW_MAX_CHILDREN)
	{
		kernelError(kernel_error, "Window has reached max children");
		return (newChild = NULL);
	}

	// Make a new window based on the parent
	newChild = createWindow(parentWindow->processId, title);
	if (!newChild)
		return (newChild);

	// Set the child's parent window
	newChild->parentWindow = parentWindow;

	// Attach the new child to the parent window
	parentWindow->child[parentWindow->numChildren++] = newChild;

	// Return the child window
	return (newChild);
}


kernelWindow *kernelWindowNewDialog(kernelWindow *parentWindow,
	const char *title)
{
	// Creates a new 'dialog box' window, tied to the parent window, using the
	// supplied values.  Not visible by default.

	kernelWindow *newDialog = NULL;

	// kernelWindowNewChild() will check parameters.

	// Make a new child window of the parent
	newDialog = kernelWindowNewChild(parentWindow, title);
	if (!newDialog)
		return (newDialog);

	// Attach the dialog window to the parent window
	parentWindow->dialogWindow = newDialog;

	// Dialog windows do not have minimize buttons, by default, since they
	// do not appear in the taskbar window list
	kernelWindowRemoveMinimizeButton(newDialog);

	// Return the dialog window
	return (newDialog);
}


int kernelWindowDestroy(kernelWindow *window)
{
	// Delete the window.

	int status = 0;
	kernelWindow *listWindow = NULL;
	kernelLinkedListItem *iter = NULL;
	int count;

	// Make sure we've been initialized
	if (!initialized)
		return (status = ERR_NOTINITIALIZED);

	if (!window)
		return (status = ERR_NULLPARAMETER);

	// If this window *has* children, first destroy them
	for (count = 0; count < window->numChildren; )
	{
		status = kernelWindowDestroy(window->child[count]);
		if (status < 0)
		{
			kernelError(kernel_warn, "Destroying child window of %s failed",
				window->title);
			count += 1;
		}
	}

	// If this window *is* a child window, dissociate it from its parent
	if (window->parentWindow)
	{
		// Find this window in the parent window's list of children
		for (count = 0; count < window->parentWindow->numChildren; count ++)
		{
			if (window->parentWindow->child[count] == window)
			{
				window->parentWindow->child[count] = NULL;
				window->parentWindow->numChildren -= 1;

				// If there will be at least 1 remaining child window, and
				// this window was not the last one, swap the last one into
				// the spot this one occupied
				if (window->parentWindow->numChildren &&
					(count < window->parentWindow->numChildren))
				{
					window->parentWindow->child[count] =
						window->parentWindow->
							child[window->parentWindow->numChildren];
				}

				break;
			}
		}

		if (window->parentWindow->dialogWindow == window)
			window->parentWindow->dialogWindow = NULL;
	}

	// Remove it from the list
	status = kernelLinkedListRemove(&windowList, (void *) window);
	if (status < 0)
		// No such window (any more)
		return (status = ERR_NOSUCHENTRY);

	// Not visible anymore
	kernelWindowSetVisible(window, 0);

	// Raise the levels of all windows that were below this one (except any
	// root window).
	listWindow = kernelLinkedListIterStart(&windowList, &iter);
	while (listWindow)
	{
		if (!(listWindow->flags & WINFLAG_ROOTWINDOW) && listWindow->level &&
			(listWindow->level >= window->level))
		{
			listWindow->level -= 1;
		}

		listWindow = kernelLinkedListIterNext(&windowList, &iter);
	}

	// Call the 'destroy' function for all the window's top-level components

	if (window->sysContainer)
	{
		kernelWindowComponentDestroy(window->sysContainer);
		window->sysContainer = NULL;
	}

	if (window->mainContainer)
	{
		kernelWindowComponentDestroy(window->mainContainer);
		window->mainContainer = NULL;
	}

	// If the window has a background image, free it
	if (window->backgroundImage.data)
	{
		kernelFree(window->backgroundImage.data);
		window->backgroundImage.data = NULL;
	}

	// Free the window's event stream
	kernelStreamDestroy(&window->events);

	// Free the window's graphic buffer
	if (window->buffer.data)
	{
		kernelFree(window->buffer.data);
		window->buffer.data = NULL;
	}

	// Free the window memory itself
	kernelFree((void *) window);

	kernelWindowShellUpdateList(&windowList);

	return (status = 0);
}


int kernelWindowUpdateBuffer(graphicBuffer *buffer, int clipX, int clipY,
	int width, int height)
{
	// A component is trying to tell us that it has updated itself in the
	// supplied buffer, and would like the bounded area of the relevant window
	// to be redrawn on screen.  This function finds the relevant window and
	// calls windowUpdate()

	int status = 0;
	kernelWindow *window = NULL;
	kernelWindow *listWindow = NULL;
	kernelLinkedListItem *iter = NULL;

	// Check params
	if (!buffer)
		return (status = ERR_NULLPARAMETER);

	// First try to find the window
	listWindow = kernelLinkedListIterStart(&windowList, &iter);
	while (listWindow)
	{
		if (&listWindow->buffer == buffer)
		{
			window = listWindow;
			break;
		}

		listWindow = kernelLinkedListIterNext(&windowList, &iter);
	}

	if (!window)
		return (status = ERR_NOSUCHENTRY);

	return (status = windowUpdate(window, clipX, clipY, width, height));
}


int kernelWindowSetCharSet(kernelWindow *window, const char *charSet)
{
	// Sets the character set for a window

	int status = 0;
	kernelWindowComponent **array = NULL;
	int numComponents = 0;
	int count;

	// Make sure we've been initialized
	if (!initialized)
		return (status = ERR_NOTINITIALIZED);

	// Check params
	if (!window)
		return (status = ERR_NULLPARAMETER);

	// Set the character set
	strncpy((char *) window->charSet, charSet, CHARSET_NAME_LEN);

	// Try to make sure we have the required character set.

	if (windowVariables->font.varWidth.small.font &&
		!kernelFontHasCharSet(windowVariables->font.varWidth.small.font,
			charSet))
	{
		kernelFontGet(windowVariables->font.varWidth.small.font->family,
			windowVariables->font.varWidth.small.font->flags,
			windowVariables->font.varWidth.small.font->points,
			charSet);
	}

	if (windowVariables->font.varWidth.medium.font &&
		!kernelFontHasCharSet(windowVariables->font.varWidth.medium.font,
			charSet))
	{
		kernelFontGet(windowVariables->font.varWidth.medium.font->family,
			windowVariables->font.varWidth.medium.font->flags,
			windowVariables->font.varWidth.medium.font->points,
			charSet);
	}

	// Loop through all the regular window components and set the charSet

	if (window->sysContainer)
	{
		array = kernelMalloc(window->sysContainer->
			numComps(window->sysContainer) * sizeof(kernelWindowComponent *));

		if (array)
		{
			numComponents = 0;
			window->sysContainer->flatten(window->sysContainer, array,
				&numComponents, 0);

			for (count = 0; count < numComponents; count ++)
				kernelWindowComponentSetCharSet(array[count], charSet);

			kernelFree(array);
		}
	}

	if (window->mainContainer)
	{
		array = kernelMalloc(
			window->mainContainer->numComps(window->mainContainer) *
				sizeof(kernelWindowComponent *));

		if (array)
		{
			numComponents = 0;
			window->mainContainer->flatten(window->mainContainer, array,
				&numComponents, 0);

			for (count = 0; count < numComponents; count ++)
				kernelWindowComponentSetCharSet(array[count], charSet);

			kernelFree(array);
		}
	}

	// Return success
	return (status = 0);
}


int kernelWindowSetTitle(kernelWindow *window, const char *title)
{
	// Sets the title on a window

	int status = 0;

	// Make sure we've been initialized
	if (!initialized)
		return (status = ERR_NOTINITIALIZED);

	// Check params
	if (!window)
		return (status = ERR_NULLPARAMETER);

	// Set the title
	strcpy((char *) window->title, title);

	// Redraw the title bar
	if (window->titleBar && window->titleBar->draw)
		status = window->titleBar->draw(window->titleBar);

	// If the window is visible, draw it
	if ((window->flags & WINFLAG_VISIBLE) && window->draw)
		window->draw(window);

	// Return success
	return (status = 0);
}


int kernelWindowGetSize(kernelWindow *window, int *width, int *height)
{
	// Returns the size of the supplied window.

	int status = 0;

	// Make sure we've been initialized
	if (!initialized)
		return (status = ERR_NOTINITIALIZED);

	// Check params
	if (!window || !width || !height)
		return (status = ERR_NULLPARAMETER);

	// If layout has not been done, do it now
	ensureWindowInitialLayout(window);

	*width = window->buffer.width;
	*height = window->buffer.height;

	return (status);
}


int kernelWindowSetSize(kernelWindow *window, int width, int height)
{
	// Sets the size of a window

	int status = 0;

	// Make sure we've been initialized
	if (!initialized)
		return (status = ERR_NOTINITIALIZED);

	// Check params
	if (!window || !width || !height)
		return (status = ERR_NULLPARAMETER);

	// If layout has not been done, do it now
	ensureWindowInitialLayout(window);

	// Resize and redraw, if applicable
	status = setWindowSize(window, width, height);
	if (status < 0)
		return (status);

	return (status);
}


int kernelWindowGetLocation(kernelWindow *window, int *xCoord, int *yCoord)
{
	int status = 0;

	// Make sure we've been initialized
	if (!initialized)
		return (status = ERR_NOTINITIALIZED);

	// Check params
	if (!window || !xCoord || !yCoord)
		return (status = ERR_NULLPARAMETER);

	*xCoord = window->xCoord;
	*yCoord = window->yCoord;

	// Return success
	return (status = 0);
}


int kernelWindowSetLocation(kernelWindow *window, int xCoord, int yCoord)
{
	// Sets the screen location of a window

	int status = 0;

	// Make sure we've been initialized
	if (!initialized)
		return (status = ERR_NOTINITIALIZED);

	// Check params
	if (!window)
		return (status = ERR_NULLPARAMETER);

	// Erase any visible bits of the window
	if (window->flags & WINFLAG_VISIBLE)
	{
		window->flags &= ~WINFLAG_VISIBLE;
		kernelWindowRedrawArea(window->xCoord, window->yCoord,
			window->buffer.width, window->buffer.height);
		window->flags |= WINFLAG_VISIBLE;
	}

	// Set the location
	window->xCoord = xCoord;
	window->yCoord = yCoord;

	// If the window is visible, draw it
	if ((window->flags & WINFLAG_VISIBLE) && window->draw)
		window->draw(window);

	// Return success
	return (status = 0);
}


int kernelWindowCenter(kernelWindow *window)
{
	// Centers a window on the screen

	int status = 0;
	int xCoord = 0;
	int yCoord = 0;

	// Make sure we've been initialized
	if (!initialized)
		return (status = ERR_NOTINITIALIZED);

	// Check params
	if (!window)
		return (status = ERR_NULLPARAMETER);

	if (window->buffer.width < screenWidth)
		xCoord = ((screenWidth - window->buffer.width) / 2);

	if (window->buffer.height < screenHeight)
		yCoord = ((screenHeight - window->buffer.height) / 2);

	return (kernelWindowSetLocation(window, xCoord, yCoord));
}


int kernelWindowSnapIcons(objectKey parent)
{
	// Snap all icons to a grid in the supplied window

	int status = 0;
	kernelWindow *window = NULL;
	kernelWindowComponent *containerComponent = NULL;
	kernelWindowContainer *container = NULL;
	int iconRow = 0, count1, count2;

	// Check params
	if (!parent)
	{
		kernelError(kernel_error, "NULL parameter");
		return (status = ERR_NULLPARAMETER);
	}

	if (((kernelWindow *) parent)->type == windowType)
	{
		window = parent;
		containerComponent = window->mainContainer;
		container = containerComponent->data;

		// Make sure the window has been laid out
		if (!containerComponent->doneLayout)
			layoutWindow(window);
	}
	else if (((kernelWindowComponent *) parent)->type ==
		containerComponentType)
	{
		window = getWindow(parent);
		containerComponent = parent;
		container = containerComponent->data;

		// Make sure the container has been laid out
		if (!containerComponent->doneLayout && containerComponent->layout)
			containerComponent->layout(containerComponent);
	}
	else
	{
		kernelError(kernel_error, "Parent is neither a window nor container");
		return (status = ERR_INVALID);
	}

	for (count1 = 0; count1 < container->numComponents; count1 ++)
	{
		if ((container->components[count1]->type == iconComponentType) &&
			((container->components[count1]->yCoord +
				container->components[count1]->params.padTop +
				container->components[count1]->height +
				container->components[count1]->params.padBottom) >=
			(window->buffer.height)))
		{
			iconRow = 1;

			for (count2 = count1 ; count2 < container->numComponents;
				count2 ++)
			{
				if (container->components[count2]->type == iconComponentType)
				{
					container->components[count2]->params.gridX += 1;
					container->components[count2]->params.gridY = iconRow++;
				}
			}

			// Set the new coordinates
			if (containerComponent->layout)
				containerComponent->layout(containerComponent);
		}
	}

	return (status = 0);
}


int kernelWindowSetHasBorder(kernelWindow *window, int trueFalse)
{
	// Sets the 'has border' attribute

	int status = 0;

	// Make sure we've been initialized
	if (!initialized)
		return (status = ERR_NOTINITIALIZED);

	// Check params
	if (!window)
		return (status = ERR_NULLPARAMETER);

	// Whether true or false, we need to remove any existing border
	// components, since even if true we don't want any existing ones hanging
	// around.
	if (window->flags & WINFLAG_HASBORDER)
		removeBorder(window);

	if (trueFalse)
	{
		window->flags |= WINFLAG_HASBORDER;
		addBorder(window);
	}
	else
		window->flags &= ~WINFLAG_HASBORDER;

	// Return success
	return (status = 0);
}


int kernelWindowSetHasTitleBar(kernelWindow *window, int trueFalse)
{
	// Sets the 'has title bar' attribute, and destroys the title bar
	// component if false (since new windows have them by default)

	int status = 0;

	// Make sure we've been initialized
	if (!initialized)
		return (status = ERR_NOTINITIALIZED);

	// Check params
	if (!window)
		return (status = ERR_NULLPARAMETER);

	if (trueFalse)
		addTitleBar(window);
	else
		removeTitleBar(window);

	return (status = 0);
}


int kernelWindowSetMovable(kernelWindow *window, int trueFalse)
{
	// Sets the 'is movable' attribute

	int status = 0;

	// Make sure we've been initialized
	if (!initialized)
		return (status = ERR_NOTINITIALIZED);

	// Check params
	if (!window)
		return (status = ERR_NULLPARAMETER);

	if (trueFalse)
		window->flags |= WINFLAG_MOVABLE;
	else
		window->flags &= ~WINFLAG_MOVABLE;

	// Return success
	return (status = 0);
}


int kernelWindowSetResizable(kernelWindow *window, int trueFalse)
{
	// Sets the 'is resizable' attribute

	int status = 0;

	// Make sure we've been initialized
	if (!initialized)
		return (status = ERR_NOTINITIALIZED);

	// Check params
	if (!window)
		return (status = ERR_NULLPARAMETER);

	if (trueFalse)
		window->flags |= WINFLAG_RESIZABLE;
	else
		window->flags &= ~WINFLAG_RESIZABLE;

	if (window->flags & WINFLAG_HASBORDER)
	{
		window->borders[0]->pointer = window->borders[2]->pointer =
			(trueFalse? kernelMouseGetPointer(MOUSE_POINTER_RESIZEV) : NULL);
		window->borders[1]->pointer = window->borders[3]->pointer =
			(trueFalse? kernelMouseGetPointer(MOUSE_POINTER_RESIZEH) : NULL);
	}

	// Return success
	return (status = 0);
}


int kernelWindowSetFocusable(kernelWindow *window, int trueFalse)
{
	// Sets the 'can focus' attribute

	int status = 0;

	// Make sure we've been initialized
	if (!initialized)
		return (status = ERR_NOTINITIALIZED);

	// Check params
	if (!window)
		return (status = ERR_NULLPARAMETER);

	if (trueFalse)
		window->flags |= WINFLAG_CANFOCUS;
	else
		window->flags &= ~WINFLAG_CANFOCUS;

	// Return success
	return (status = 0);
}


int kernelWindowRemoveMinimizeButton(kernelWindow *window)
{
	// Removes any minimize button component.

	int status = 0;
	kernelWindowTitleBar *titleBar = NULL;

	// Make sure we've been initialized
	if (!initialized)
		return (status = ERR_NOTINITIALIZED);

	// Check params
	if (!window)
		return (status = ERR_NULLPARAMETER);

	if (window->titleBar)
	{
		titleBar = window->titleBar->data;

		if (titleBar->minimizeButton)
		{
			kernelWindowComponentDestroy(titleBar->minimizeButton);
			titleBar->minimizeButton = NULL;
		}
	}

	// Return success
	return (status = 0);
}


int kernelWindowRemoveCloseButton(kernelWindow *window)
{
	// Removes any close button component.

	int status = 0;
	kernelWindowTitleBar *titleBar = NULL;

	// Make sure we've been initialized
	if (!initialized)
		return (status = ERR_NOTINITIALIZED);

	// Check params
	if (!window)
		return (status = ERR_NULLPARAMETER);

	if (window->titleBar)
	{
		titleBar = window->titleBar->data;

		if (titleBar->closeButton)
		{
			kernelWindowComponentDestroy(titleBar->closeButton);
			titleBar->closeButton = NULL;
		}
	}

	// Return success
	return (status = 0);
}


int kernelWindowFocus(kernelWindow *window)
{
	// Tries to change the window focus to the requested window

	int status = 0;
	kernelWindow *listWindow = NULL;
	kernelLinkedListItem *iter = NULL;

	// If there's a window to focus, but it can't focus, we're finished
	if (window && !(window->flags & WINFLAG_CANFOCUS))
		return (status = 0);

	if (focusWindow && (window != focusWindow))
		// Remove the focus from the previously focused window
		focusWindow->focus(focusWindow, 0);

	// If there's no window to focus, we're finished
	if (!window)
	{
		focusWindow = NULL;
		return (status = 0);
	}

	kernelDebug(debug_gui, "Window '%s' got focus", window->title);

	// Raise the new focus window to the top, unless it's the root window.
	if (!(window->flags & WINFLAG_ROOTWINDOW))
	{
		if (window != focusWindow)
		{
			// Decrement the levels of all windows that used to be above us
			listWindow = kernelLinkedListIterStart(&windowList, &iter);
			while (listWindow)
			{
				if ((listWindow != window) &&
					(listWindow->level <= window->level))
				{
					listWindow->level += 1;
				}

				listWindow = kernelLinkedListIterNext(&windowList, &iter);
			}
		}

		// This window becomes topmost
		window->level = 0;
	}

	// Mark it as focused
	window->focus(window, 1);
	focusWindow = window;

	return (status = 0);
}


int kernelWindowSetVisible(kernelWindow *window, int visible)
{
	// Sets a window to visible or not

	int status = 0;

	// Make sure we've been initialized
	if (!initialized)
		return (status = ERR_NOTINITIALIZED);

	// Check params
	if (!window)
		return (status = ERR_NULLPARAMETER);

	// Anything to do?  We continue in the case of the window already being
	// visible, so this function can be used to focus the window.
	if (!visible && !(window->flags & WINFLAG_VISIBLE))
		return (status = 0);

	if (visible)
	{
		// If the window is becoming visible and hasn't yet had its layout
		// done, do that now
		ensureWindowInitialLayout(window);

		// If no coordinates have been specified, center the window
		if ((window->xCoord == -1) && (window->yCoord == -1))
		{
			status = kernelWindowCenter(window);
			if (status < 0)
				return (status);
		}
	}

	// Set the visible value
	if (visible)
		window->flags |= WINFLAG_VISIBLE;
	else
		window->flags &= ~WINFLAG_VISIBLE;

	if (visible)
	{
		// Is the mouse in the window?
		if (isPointInside(kernelMouseGetX(), kernelMouseGetY(),
			makeWindowScreenArea(window)))
		{
			mouseInWindow = window;
		}

		if (window->draw)
		{
			// Draw the window
			status = window->draw(window);
			if (status < 0)
				return (status);
		}

		// Automatically give any newly-visible windows the focus.
		kernelWindowFocus(window);
	}
	else
	{
		// Take away the focus, if applicable
		if (window == focusWindow)
			kernelWindowFocus(findTopmostWindow(WINFLAG_VISIBLE |
				WINFLAG_CANFOCUS));

		// Make sure the window is not the 'mouse in' window
		if (window == mouseInWindow)
			mouseInWindow = getCoordinateWindow(kernelMouseGetX(),
				kernelMouseGetY());

		// Erase any visible bits of the window
		kernelWindowRedrawArea(window->xCoord, window->yCoord,
			 window->buffer.width, window->buffer.height);

		// If erasing the window erased the mouse pointer, redraw it
		if (isPointInside(kernelMouseGetX(), kernelMouseGetY(),
			makeWindowScreenArea(window)))
		{
			kernelMouseDraw();
		}
	}

	// Return success
	return (status = 0);
}


void kernelWindowSetMinimized(kernelWindow *window, int minimize)
{
	// Minimize or restore a window (with visuals!)

	int count1, count2;

	kernelWindowSetVisible(window, !minimize);

	if (minimize)
	{
		// Show the minimize graphically.  Draw xor'ed outlines
		for (count1 = 0; count1 < 2; count1 ++)
		{
			for (count2 = 0; count2 < windowVariables->window.minRestTracers;
				count2 ++)
			{
				kernelGraphicDrawRect(NULL, &((color){ 255, 255, 255 }),
					draw_xor,
					(window->xCoord -
						(count2 * (window->xCoord /
							windowVariables->window.minRestTracers))),
					(window->yCoord -
						(count2 * (window->yCoord /
							windowVariables->window.minRestTracers))),
					(window->buffer.width -
						(count2 * (window->buffer.width /
							windowVariables->window.minRestTracers))),
					(window->buffer.height -
						(count2 * (window->buffer.height /
							windowVariables->window.minRestTracers))),
					1, 0);
			}

			if (!count1)
				// Delay a bit
				kernelMultitaskerYield();
		}
	}
}


int kernelWindowAddConsoleTextArea(kernelWindow *window)
{
	// Moves the console text area component from our 'hidden' console window
	// to the supplied window

	int status = 0;

	// Make sure we've been initialized
	if (!initialized)
		return (status = ERR_NOTINITIALIZED);

	// Check params
	if (!window)
		return (status = ERR_NULLPARAMETER);

	// Make sure the console text area is not already assigned to some other
	// window (other than the consoleWindow)
	if (consoleTextArea->window != consoleWindow)
		return (status = ERR_ALREADY);

	kernelWindowMoveConsoleTextArea(consoleWindow, window);

	return (status = 0);
}


void kernelWindowRedrawArea(int xCoord, int yCoord, int width, int height)
{
	// This function will redraw an arbitrary area of the screen.  Initially
	// written to allow the mouse functions to erase the mouse without having
	// to know what's under it.  Could be useful for other things as well.

	screenArea area;
	kernelWindow *listWindow = NULL;
	kernelLinkedListItem *iter = NULL;
	screenArea intersectingArea;

	// Make sure we've been initialized
	if (!initialized)
		return;

	// Don't do off the screen
	if (xCoord < 0)
	{
		width += xCoord;
		xCoord = 0;
	}
	if (yCoord < 0)
	{
		height += yCoord;
		yCoord = 0;
	}

	if ((xCoord + width) > screenWidth)
		width = (screenWidth - xCoord);
	if ((yCoord + height) > screenHeight)
		height = (screenHeight - yCoord);

	// Do we still have width and height?
	if ((width <= 0) || (height <= 0))
		return;

	// Clear the area first
	kernelGraphicClearArea(NULL, &windowVariables->color.desktop, xCoord,
		yCoord, width, height);

	area.leftX = xCoord;
	area.topY = yCoord;
	area.rightX = (xCoord + (width - 1));
	area.bottomY = (yCoord + (height - 1));

	// Iterate through the window list, looking for any visible ones that
	// intersect this area
	listWindow = kernelLinkedListIterStart(&windowList, &iter);
	while (listWindow)
	{
		if ((listWindow->flags & WINFLAG_VISIBLE) &&
			doAreasIntersect(&area, makeWindowScreenArea(listWindow)))
		{
			getIntersectingArea(makeWindowScreenArea(listWindow), &area,
				&intersectingArea);
			intersectingArea.leftX -= listWindow->xCoord;
			intersectingArea.topY -= listWindow->yCoord;
			intersectingArea.rightX -= listWindow->xCoord;
			intersectingArea.bottomY -= listWindow->yCoord;
			renderVisiblePortions(listWindow, &intersectingArea);
		}

		listWindow = kernelLinkedListIterNext(&windowList, &iter);
	}
}


int kernelWindowDraw(kernelWindow *window)
{
	int status = 0;

	// Make sure we've been initialized
	if (!initialized)
		return (status = ERR_NOTINITIALIZED);

	// Check params
	if (!window)
		return (status = ERR_NULLPARAMETER);

	if ((window->flags & WINFLAG_VISIBLE) && window->draw)
		status = window->draw(window);

	return (status);
}


void kernelWindowDrawAll(void)
{
	// This function will redraw all windows.  Useful for example when the
	// user has changed the color scheme

	kernelWindow *listWindow = NULL;
	kernelLinkedListItem *iter = NULL;

	// Make sure we've been initialized
	if (!initialized)
		return;

	// Iterate through the window list
	listWindow = kernelLinkedListIterStart(&windowList, &iter);
	while (listWindow)
	{
		if ((listWindow->flags & WINFLAG_VISIBLE) && listWindow->draw)
			listWindow->draw(listWindow);

		listWindow = kernelLinkedListIterNext(&windowList, &iter);
	}
}


int kernelWindowGetColor(const char *colorName, color *getColor)
{
	// Given the color name, get it.

	int status = 0;

	if (!colorName || !getColor)
		return (status = ERR_NULLPARAMETER);

	if (!strncasecmp(colorName, COLOR_SETTING_FOREGROUND,
		strlen(COLOR_SETTING_FOREGROUND)))
	{
		memcpy(getColor, &windowVariables->color.foreground, sizeof(color));
	}
	if (!strncasecmp(colorName, COLOR_SETTING_BACKGROUND,
		strlen(COLOR_SETTING_BACKGROUND)))
	{
		memcpy(getColor, &windowVariables->color.background, sizeof(color));
	}
	if (!strncasecmp(colorName, COLOR_SETTING_DESKTOP,
		strlen(COLOR_SETTING_DESKTOP)))
	{
		memcpy(getColor, &windowVariables->color.desktop, sizeof(color));
	}

	return (status = 0);
}


int kernelWindowSetColor(const char *colorName, color *setColor)
{
	// Given the color name, set it.

	int status = 0;

	if (!colorName || !setColor)
		return (status = ERR_NULLPARAMETER);

	// Change the color
	if (!strncasecmp(colorName, COLOR_SETTING_FOREGROUND,
		strlen(COLOR_SETTING_FOREGROUND)))
	{
		memcpy(&windowVariables->color.foreground, setColor, sizeof(color));
	}
	if (!strncasecmp(colorName, COLOR_SETTING_BACKGROUND,
		strlen(COLOR_SETTING_BACKGROUND)))
	{
		memcpy(&windowVariables->color.background, setColor, sizeof(color));
	}
	if (!strncasecmp(colorName, COLOR_SETTING_DESKTOP,
		strlen(COLOR_SETTING_DESKTOP)))
	{
		memcpy(&windowVariables->color.desktop, setColor, sizeof(color));
	}

	return (status = 0);
}


void kernelWindowResetColors(void)
{
	// This function will reset the colors of all the windows and their
	// components.  Useful for example when the user has changed the color
	// scheme

	kernelWindow *listWindow = NULL;
	kernelLinkedListItem *iter = NULL;
	kernelWindowComponent **array = NULL;
	int numComponents = 0;
	int count;

	// Make sure we've been initialized
	if (!initialized)
		return;

	// Iterate through the window list
	listWindow = kernelLinkedListIterStart(&windowList, &iter);
	while (listWindow)
	{
		if (listWindow->flags & WINFLAG_ROOTWINDOW)
		{
			memcpy((color *) &listWindow->background,
				&windowVariables->color.desktop, sizeof(color));
		}
		else
		{
			memcpy((color *) &listWindow->background,
				&windowVariables->color.background, sizeof(color));
		}

		// Loop through all the regular window components and set the colors
		numComponents =
			(listWindow->sysContainer->numComps(listWindow->sysContainer) +
			listWindow->mainContainer->numComps(listWindow->mainContainer));
		array = kernelMalloc(numComponents * sizeof(kernelWindowComponent *));
		if (!array)
			break;

		numComponents = 0;
		listWindow->sysContainer->flatten(listWindow->sysContainer, array,
			&numComponents, 0);
		listWindow->mainContainer->flatten(listWindow->mainContainer, array,
			&numComponents, 0);

		for (count = 0; count < numComponents; count ++)
		{
			if (!(array[count]->params.flags &
				WINDOW_COMPFLAG_CUSTOMFOREGROUND))
			{
				memcpy((color *) &array[count]->params.foreground,
					&windowVariables->color.foreground, sizeof(color));
			}

			if (!(array[count]->params.flags &
				WINDOW_COMPFLAG_CUSTOMBACKGROUND))
			{
				memcpy((color *) &array[count]->params.background,
					&windowVariables->color.background, sizeof(color));
			}
		}

		kernelFree(array);

		listWindow = kernelLinkedListIterNext(&windowList, &iter);
	}

	kernelWindowDrawAll();
}


void kernelWindowProcessEvent(windowEvent *event)
{
	// Some external thing, such as the mouse driver, wants us to add some
	// event into the event streams.

	// Make sure we've been initialized
	if (!initialized)
		return;

	// Check to make sure the window thread is still running
	if (!kernelMultitaskerProcessIsAlive(winThreadPid))
		spawnWindowThread();

	if (event->type & EVENT_MASK_MOUSE)
		// Write the mouse event into the mouse event stream for later
		// processing by the window thread
		kernelWindowEventStreamWrite(&mouseEvents, event);

	else if (event->type & EVENT_MASK_KEY)
		// Write the key event into the mouse event stream for later
		// processing by the window thread
		kernelWindowEventStreamWrite(&keyEvents, event);
}


int kernelWindowRegisterEventHandler(kernelWindowComponent *component,
	void (*function)(kernelWindowComponent *, windowEvent *))
{
	// This function is called to register a windowEvent callback handler for
	// a component.

	int status = 0;

	// Make sure we've been initialized
	if (!initialized)
		return (status = ERR_NOTINITIALIZED);

	// Check parameters
	if (!component || !function)
		return (status = ERR_NULLPARAMETER);

	component->eventHandler = function;

	return (status = 0);
}


int kernelWindowComponentEventGet(objectKey key, windowEvent *event)
{
	// This function is called to read an event from the component's
	// windowEventStream

	int status = 0;
	kernelWindow *listWindow = NULL;
	kernelLinkedListItem *iter = NULL;
	kernelWindow *window = NULL;
	kernelWindowComponent *component = NULL;

	// Make sure we've been initialized
	if (!initialized)
		return (status = ERR_NOTINITIALIZED);

	// Check parameters
	if (!key || !event)
		return (status = ERR_NULLPARAMETER);

	// First, determine whether the request is for a window or a component
	listWindow = kernelLinkedListIterStart(&windowList, &iter);
	while (listWindow)
	{
		if (listWindow == key)
		{
			window = listWindow;
			break;
		}

		listWindow = kernelLinkedListIterNext(&windowList, &iter);
	}

	if (window)
	{
		// The request is for a window windowEvent
		status = kernelWindowEventStreamRead(&window->events, event);
	}
	else
	{
		// The request must (we hope) be for a component windowEvent
		component = key;

		status = kernelWindowEventStreamRead(&component->events, event);
	}

	return (status);
}


int kernelWindowSetBackgroundColor(kernelWindow *window, color *background)
{
	// Set the colors for the window

	int status = 0;

	// Make sure we've been initialized
	if (!initialized)
		return (status = ERR_NOTINITIALIZED);

	// Check params
	if (!window)
		return (status = ERR_NULLPARAMETER);

	if (!background)
		background = &windowVariables->color.background;

	memcpy((void *) &window->background, background, sizeof(color));

	if ((window->flags & WINFLAG_VISIBLE) && window->draw)
		window->draw(window);

	return (status = 0);
}


int kernelWindowSetBackgroundImage(kernelWindow *window, image *imageCopy)
{
	// Set the window's background image

	int status = 0;

	// Make sure we've been initialized
	if (!initialized)
		return (status = ERR_NOTINITIALIZED);

	// Check params.  The image can be NULL.
	if (!window)
		return (status = ERR_NULLPARAMETER);

	// If there was a previous background image, deallocate its memory
	kernelImageFree((image *) &window->backgroundImage);

	if (imageCopy)
	{
		// Copy the image information into the window's background image
		status = kernelImageCopyToKernel(imageCopy, (image *)
			&window->backgroundImage);
		if (status < 0)
			return (status);
	}

	return (status = 0);
}


int kernelWindowScreenShot(image *saveImage)
{
	// This will grab the entire screen as a screen shot, and put the
	// data into the supplied image object

	int status = 0;

	// Make sure we've been initialized
	if (!initialized)
		return (status = ERR_NOTINITIALIZED);

	status = kernelGraphicGetImage(NULL, saveImage, 0, 0, screenWidth,
		screenHeight);

	return (status);
}


int kernelWindowSaveScreenShot(const char *name)
{
	// Save a screenshot in the current directory

	int status = 0;
	image saveImage;
	char *filename = NULL;
	kernelWindow *dialog = NULL;
	char *labelText = NULL;
	componentParameters params;

	// Make sure we've been initialized
	if (!initialized)
		return (status = ERR_NOTINITIALIZED);

	memset(&saveImage, 0, sizeof(image));

	// Try to grab the screenshot image
	status = kernelWindowScreenShot(&saveImage);
	if (status < 0)
	{
		kernelError(kernel_error, "Error getting screen shot image");
		return (status);
	}

	filename = kernelMalloc(MAX_PATH_NAME_LENGTH);
	if (!filename)
		return (status = ERR_MEMORY);

	if (!name)
	{
		kernelMultitaskerGetCurrentDirectory(filename, MAX_PATH_NAME_LENGTH);
		if (filename[strlen(filename) - 1] != '/')
			strcat(filename, "/");
		strcat(filename, "screenshot1.bmp");
	}
	else
	{
		strncpy(filename, name, MAX_PATH_NAME_LENGTH);
		filename[MAX_PATH_NAME_LENGTH - 1] = '\0';
	}

	dialog = kernelWindowNew(NULL, "Screen shot");
	if (dialog)
	{
		labelText = kernelMalloc(MAX_PATH_NAME_LENGTH + 40);
		if (labelText)
		{
			sprintf(labelText, "Saving screen shot as \"%s\"...", filename);

			memset(&params, 0, sizeof(componentParameters));
			params.gridWidth = 1;
			params.gridHeight = 1;
			params.padLeft = 5;
			params.padRight = 5;
			params.padTop = 5;
			params.padBottom = 5;
			params.orientationX = orient_center;
			params.orientationY = orient_middle;

			kernelWindowNewTextLabel(dialog, labelText, &params);
			kernelWindowSetVisible(dialog, 1);
			kernelFree(labelText);
		}
	}

	status = kernelImageSave(filename, IMAGEFORMAT_BMP, &saveImage);
	if (status < 0)
		kernelError(kernel_error, "Error %d saving image %s", status,
			filename);

	kernelWindowDestroy(dialog);
	kernelFree(filename);
	kernelImageFree(&saveImage);

	return (status);
}


int kernelWindowSetTextOutput(kernelWindowComponent *component)
{
	// This will set the text output stream for the given process to be
	// the supplied window component

	int status = 0;
	int processId = 0;
	kernelWindowTextArea *textArea = NULL;
	kernelTextInputStream *inputStream = NULL;
	kernelTextOutputStream *outputStream = NULL;

	// Make sure we've been initialized
	if (!initialized)
		return (status = ERR_NOTINITIALIZED);

	// Check parameters
	if (!component)
		return (status = ERR_NULLPARAMETER);

	processId = kernelMultitaskerGetCurrentProcessId();

	// Do different things depending on the type of the component
	if (component->type == textAreaComponentType)
	{
		// Switch the text area of the output stream to the supplied text
		// area component.

		textArea = component->data;

		inputStream = textArea->area->inputStream;
		outputStream = textArea->area->outputStream;

		kernelMultitaskerSetTextInput(processId, inputStream);
		kernelMultitaskerSetTextOutput(processId, outputStream);

		return (status = 0);
	}
	else
	{
		// Invalid component type
		kernelError(kernel_error, "Unable to switch text output; invalid "
			"window component type");
		return (status = ERR_INVALID);
	}
}


int kernelWindowLayout(kernelWindow *window)
{
	// Layout, or re-layout, the requested window.  This function can be used
	// when components are added to or removed from and already laid-out
	// window.

	int status = 0;

	// Make sure we've been initialized
	if (!initialized)
		return (status = ERR_NOTINITIALIZED);

	// Check parameters
	if (!window)
		return (status = ERR_NULLPARAMETER);

	status = kernelWindowSetVisible(window, 0);
	if (status < 0)
		return (status);

	status = layoutWindow(window);
	if (status < 0)
		return (status);

	status = autoSizeWindow(window);
	if (status < 0)
		return (status);

	status = kernelWindowSetVisible(window, 1);
	if (status < 0)
		return (status);

	return (status = 0);
}


void kernelWindowDebugLayout(kernelWindow *window)
{
	// Sets the 'debug layout' flag on the window so that layout grids get
	// drawn around the components

	// Make sure we've been initialized
	if (!initialized)
		return;

	// Check parameters
	if (!window)
		return;

	window->flags |= WINFLAG_DEBUGLAYOUT;

	if ((window->flags & WINFLAG_VISIBLE) && window->draw)
		window->draw(window);
}


int kernelWindowContextAdd(objectKey parent, windowMenuContents *contents)
{
	// This function allows the caller to add context menu items to the
	// supplied parent object (can be a window or a component).  The function
	// supplies the pointers to the new menu items in the caller's structure,
	// which can then be manipulated to some extent (enable/disable, destroy,
	// etc) using regular component functions.

	int status = 0;
	kernelWindow *parentWindow = NULL;
	kernelWindowComponent *parentComponent = NULL;
	kernelWindow *menu = NULL;
	componentParameters params;
	int count;

	// Make sure we've been initialized
	if (!initialized)
		return (status = ERR_NOTINITIALIZED);

	// Check params
	if (!parent || !contents)
	{
		kernelError(kernel_error, "NULL parameter");
		return (status = ERR_NULLPARAMETER);
	}

	// If the parent is a window, then we will add the content to a context
	// menu in the window's system container.
	if (((kernelWindow *) parent)->type == windowType)
	{
		parentWindow = parent;
		menu = parentWindow->contextMenu;
	}
	else
	{
		parentComponent = parent;
		parentWindow = parentComponent->window;
		menu = parentComponent->contextMenu;
	}

	// If there's no existing context menu, create one with the contents
	if (!menu)
	{
		memset(&params, 0, sizeof(componentParameters));

		if (parentComponent)
		{
			// Get any custom colors from the parent
			if (parentComponent->params.flags &
				WINDOW_COMPFLAG_CUSTOMFOREGROUND)
			{
				params.flags |= WINDOW_COMPFLAG_CUSTOMFOREGROUND;
				memcpy(&params.foreground,
					(void *) &parentComponent->params.foreground,
					sizeof(color));
			}

			if (parentComponent->params.flags &
				WINDOW_COMPFLAG_CUSTOMBACKGROUND)
			{
				params.flags |= WINDOW_COMPFLAG_CUSTOMBACKGROUND;
				memcpy(&params.background,
					(void *) &parentComponent->params.background,
					sizeof(color));
			}
		}

		menu = kernelWindowNewMenu(parentWindow, NULL, "contextMenu",
			contents, &params);
		if (!menu)
		{
			kernelError(kernel_error, "Couldn't create context menu");
			return (status = ERR_NOCREATE);
		}

		if (parentComponent)
			parentComponent->contextMenu = menu;
		else
			parentWindow->contextMenu = menu;
	}
	else
	{
		// Loop through the contents and add the individual items to the
		// existing context menu

		for (count = 0; count < contents->numItems; count ++)
		{
			contents->items[count].key = (objectKey)
				kernelWindowNewMenuItem(menu, contents->items[count].text,
					&params);
			if (!contents->items[count].key)
			{
				kernelError(kernel_error, "Couldn't add \"%s\" to context "
					"menu", contents->items[count].text);
				return (status = ERR_NOCREATE);
			}
		}
	}

	return (status = 0);
}


int kernelWindowContextSet(objectKey parent, kernelWindow *menu)
{
	// This function allows the caller to set the context menu directly using
	// the supplied parent object (can be a window or a component) and menu
	// window.

	int status = 0;
	kernelWindow *parentWindow = NULL;
	kernelWindowComponent *parentComponent = NULL;

	// Make sure we've been initialized
	if (!initialized)
		return (status = ERR_NOTINITIALIZED);

	// Check params
	if (!parent || !menu)
	{
		kernelError(kernel_error, "NULL parameter");
		return (status = ERR_NULLPARAMETER);
	}

	// If the parent is a window, then we will set the context menu of the
	// window's system container.
	if (((kernelWindow *) parent)->type == windowType)
	{
		parentWindow = parent;
		if (parentWindow->contextMenu)
			kernelWindowDestroy(parentWindow->contextMenu);
		parentWindow->contextMenu = menu;
	}
	else
	{
		parentComponent = parent;
		if (parentComponent->contextMenu)
			kernelWindowDestroy(parentComponent->contextMenu);
		parentComponent->contextMenu = menu;
	}

	return (status = 0);
}


int kernelWindowSwitchPointer(objectKey parent, const char *pointerName)
{
	// Sets the mouse pointer for the window or component object, by name.
	// If 'parent' is NULL, just set the mouse pointer without associating
	// it with any window or component.

	int status = 0;
	kernelWindow *window = NULL;
	kernelMousePointer *newPointer = NULL;

	// Make sure we've been initialized
	if (!initialized)
		return (status = ERR_NOTINITIALIZED);

	// Check params.  Parent can be NULL.
	if (!pointerName)
	{
		kernelError(kernel_error, "NULL parameter");
		return (status = ERR_NULLPARAMETER);
	}

	newPointer = kernelMouseGetPointer(pointerName);
	if (!newPointer)
		return (status = ERR_NOSUCHENTRY);

	if (parent)
		window = getWindow(parent);

	if (window)
		window->pointer = newPointer;

	status = kernelMouseSetPointer(newPointer);
	return (status);
}


void kernelWindowMoveConsoleTextArea(kernelWindow *oldWindow,
	kernelWindow *newWindow)
{
	// Moves the console text area component from the old window to the new
	// window

	kernelWindowTextArea *textArea = consoleTextArea->data;

	// Make sure we've been initialized
	if (!initialized)
		return;

	if (newWindow == oldWindow)
		return;

	// Remove it from the old window
	if (consoleTextArea->container && oldWindow->mainContainer->delete)
		oldWindow->mainContainer->delete(oldWindow->mainContainer,
			consoleTextArea);

	// Add it to the new window
	if (newWindow->mainContainer->add)
		newWindow->mainContainer->add(newWindow->mainContainer,
			consoleTextArea);

	consoleTextArea->window = newWindow;
	consoleTextArea->buffer = &newWindow->buffer;

	if (textArea->scrollBar)
	{
		textArea->scrollBar->window = newWindow;
		textArea->scrollBar->buffer = &newWindow->buffer;
	}
}


int kernelWindowToggleMenuBar(void)
{
	// If the currently-focused window (which could be the root window) has
	// a menu bar component, raise (or lower) it as if the first menu item has
	// been clicked.  This would typically be done in response to the user
	// pressing and releasing the ALT key.

	int status = 0;
	kernelWindowMenuBar *menuBar = NULL;
	int menuNumber = 0;
	kernelWindowContainer *container = NULL;
	windowEvent event;
	int count;

	// Make sure we've been initialized
	if (!initialized)
		return (status = ERR_NOTINITIALIZED);

	if (focusWindow && focusWindow->menuBar)
	{
		kernelDebug(debug_gui, "Window toggle current menu in '%s'",
			focusWindow->title);

		menuBar = (kernelWindowMenuBar *) focusWindow->menuBar->data;

		if (menuBar)
		{
			if (menuBar->numMenus)
			{
				if (menuBar->raisedMenu &&
					(menuBar->raisedMenu->flags & WINFLAG_VISIBLE))
				{
					kernelDebug(debug_gui, "Window lower current menu");
					for (count = 0; count < menuBar->numMenus; count ++)
					{
						if (menuBar->menu[count] == menuBar->raisedMenu)
						{
							menuNumber = count;
							break;
						}
					}
				}
				else
				{
					// The way menus and menu bars are currently implemented,
					// only menus with contents can be raised.  Try to find
					// the first one with menu items in it.
					kernelDebug(debug_gui, "Window raise first populated "
						"menu");

					for (count = 0; count < menuBar->numMenus; count ++)
					{
						container = menuBar->menu[count]->mainContainer->data;
						if (container->numComponents)
						{
							menuNumber = count;
							break;
						}
					}
				}

				// Send a fake mouse click to raise or lower the menu
				memset(&event, 0, sizeof(windowEvent));
				event.type = EVENT_MOUSE_LEFTDOWN;
				event.xPosition = (focusWindow->xCoord +
					focusWindow->menuBar->xCoord +
					menuBar->menuXCoord[menuNumber] +
					(menuBar->menuTitleWidth[menuNumber] / 2));
				event.yPosition = (focusWindow->yCoord +
					focusWindow->menuBar->yCoord +
					(focusWindow->menuBar->height / 2));

				kernelDebug(debug_gui, "Window send mouse event at (%d,%d)",
					event.xPosition, event.yPosition);

				kernelWindowProcessEvent(&event);
			}
			else
			{
				kernelDebugError("Window menuBar has no menus");
			}
		}
		else
		{
			kernelDebugError("NULL menuBar");
		}
	}
	else
	{
		kernelDebug(debug_gui, "No focus window or no menuBar component");
	}

	return (status = 0);
}


int kernelWindowRefresh(void)
{
	// This is a mechanism to tell the window system that something big has
	// changed, and that it should refresh everything.

	// This was implemented in order to facilitate instantaneous language
	// switching, but it can be expanded to cover more things (e.g. window
	// configuration changes like colors or screen resolution, things that
	// window shell manages, such as desktop configuration, icons, menus,
	// etc.)

	int status = 0;

	// Make sure we've been initialized
	if (!initialized)
		return (status = ERR_NOTINITIALIZED);

	kernelWindowShellRefresh();

	return (status = 0);
}

