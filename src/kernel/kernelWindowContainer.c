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
//  kernelWindowContainer.c
//

// This code is for managing kernelWindowContainer objects.  These are
// containers for all other types of components.

#include "kernelWindow.h"	// Our prototypes are here
#include "kernelDebug.h"
#include "kernelError.h"
#include "kernelMalloc.h"
#include <stdlib.h>
#include <string.h>


static void distributeGrid(int size, int coord, int cells, int *gridSize,
	int resizable, int *gridResizable)
{
	// Distributes a component's width or height across its columns or rows.

	int count;

	if (cells <= 0)
		return;

	// Calculate how much (if any) excess size this component requires
	// compared to the other elements in its grid column(s)/row(s)
	for (count = 0; count < cells; count ++)
		size -= gridSize[coord + count];

	if (size <= 0)
		// The existing space is fine
		return;

	if (resizable)
	{
		// Determine how many (if any) of the column(s)/row(s) already contain
		// resizable elements

		resizable = 0;
		for (count = 0; count < cells; count ++)
		{
			if (gridResizable[coord + count])
				resizable += 1;
		}
	}

	if (resizable)
	{
		// Resizable column(s)/row(s) were found, so spread the component's
		// excess size across those
		size /= resizable;
		for (count = 0; count < cells; count ++)
		{
			if (gridResizable[coord + count])
				gridSize[coord + count] += size;
		}
	}
	else
	{
		// No resizable cells, so just spread the component's excess size
		// across all cells evenly
		size /= cells;
		for (count = 0; count < cells; count ++)
			gridSize[coord + count] += size;
	}
}


static void calculateMinimumGrid(kernelWindowComponent *containerComponent,
	int maxX, int maxY, int *columnStartX, int *rowStartY, int *columnWidth,
	int *rowHeight, int *resizableX, int *resizableY, int bareMinimum)
{
	// This does the basic grid calculation, using only the minimum required
	// size, and setting the container component's minWidth and minHeight
	// variables

	kernelWindowContainer *container = containerComponent->data;
	kernelWindowComponent *component = NULL;
	int count;

	// Clear our arrays
	memset(columnStartX, 0, (maxX * sizeof(int)));
	memset(rowStartY, 0, (maxY * sizeof(int)));
	memset(columnWidth, 0, (maxX * sizeof(int)));
	memset(rowHeight, 0, (maxY * sizeof(int)));

	// Widths, first pass: find the width of each column based on the widest
	// component of each.  Skip any multi-column components that are
	// resizable; we'll do those in the second pass.
	for (count = 0; count < container->numComponents; count ++)
	{
		component = container->components[count];

		if ((component->params.gridWidth > 1) &&
			(component->flags & WINFLAG_RESIZABLEX) &&
			!(component->params.flags & WINDOW_COMPFLAG_FIXEDWIDTH))
		{
			continue;
		}

		distributeGrid(((bareMinimum? component->minWidth :
			component->width) + component->params.padLeft +
			component->params.padRight), component->params.gridX,
			component->params.gridWidth, columnWidth, 0 /* resizable */,
			NULL /* resizableX */);
	}

	// Widths, second pass: for any multi-column resizable components,
	// distribute the excess width across columns containing other resizable
	// components if possible
	for (count = 0; count < container->numComponents; count ++)
	{
		component = container->components[count];

		if ((component->params.gridWidth < 2) ||
			!(component->flags & WINFLAG_RESIZABLEX) ||
			(component->params.flags & WINDOW_COMPFLAG_FIXEDWIDTH))
		{
			continue;
		}

		distributeGrid(((bareMinimum? component->minWidth :
			component->width) + component->params.padLeft +
			component->params.padRight), component->params.gridX,
			component->params.gridWidth, columnWidth, 1 /* resizable */,
			resizableX);
	}

	// Heights, first pass: find the height of each row based on the tallest
	// component of each.  Skip any multi-row components that are resizable;
	// we'll do those in the second pass.
	for (count = 0; count < container->numComponents; count ++)
	{
		component = container->components[count];

		if ((component->params.gridHeight > 1) &&
			(component->flags & WINFLAG_RESIZABLEY) &&
			!(component->params.flags & WINDOW_COMPFLAG_FIXEDHEIGHT))
		{
			continue;
		}

		distributeGrid(((bareMinimum? component->minHeight :
			component->height) + component->params.padTop +
			component->params.padBottom), component->params.gridY,
			component->params.gridHeight, rowHeight, 0 /* resizable */,
			NULL /* resizableY */);
	}

	// Heights, second pass: for any multi-row resizable components,
	// distribute the excess height across columns containing other resizable
	// components if possible
	for (count = 0; count < container->numComponents; count ++)
	{
		component = container->components[count];

		if ((component->params.gridHeight < 2) ||
			!(component->flags & WINFLAG_RESIZABLEY) ||
			(component->params.flags & WINDOW_COMPFLAG_FIXEDHEIGHT))
		{
			continue;
		}

		distributeGrid(((bareMinimum? component->minHeight :
			component->height) + component->params.padTop +
			component->params.padBottom), component->params.gridY,
			component->params.gridHeight, rowHeight, 1 /* resizable */,
			resizableY);
	}

	// Set the starting X coordinates of the columns.
	columnStartX[0] = containerComponent->xCoord;
	for (count = 1; count < maxX; count ++)
	{
		columnStartX[count] = (columnStartX[count - 1] +
			columnWidth[count - 1]);
	}

	if (bareMinimum)
	{
		// Set the minimum width
		containerComponent->minWidth = ((columnStartX[maxX - 1] +
			columnWidth[maxX - 1]) - containerComponent->xCoord);
	}
	else
	{
		// Set the default width
		containerComponent->width = ((columnStartX[maxX - 1] +
			columnWidth[maxX - 1]) - containerComponent->xCoord);
	}

	// Set the starting Y coordinates of the rows.
	rowStartY[0] = containerComponent->yCoord;
	for (count = 1; count < maxY; count ++)
		rowStartY[count] = (rowStartY[count - 1] + rowHeight[count - 1]);

	if (bareMinimum)
	{
		// Set the minimum height
		containerComponent->minHeight = ((rowStartY[maxY - 1] +
			rowHeight[maxY - 1]) - containerComponent->yCoord);
	}
	else
	{
		// Set the default height
		containerComponent->height = ((rowStartY[maxY - 1] +
			rowHeight[maxY - 1]) - containerComponent->yCoord);
	}
}


static void calculateGrid(kernelWindowComponent *containerComponent,
	int maxX, int maxY, int *columnStartX, int *rowStartY, int *columnWidth,
	int *rowHeight, int width, int height)
{
	kernelWindowContainer *container = containerComponent->data;
	int *resizableX = NULL, *resizableY = NULL;
	int numResizableX = 0, numResizableY = 0;
	int deltaWidth = 0, deltaHeight = 0;
	kernelWindowComponent *component = NULL;
	int count;

	resizableX = kernelMalloc(maxX * sizeof(int));
	resizableY = kernelMalloc(maxY * sizeof(int));
	if (!resizableX || !resizableY)
		goto out;

	// Clear our arrays
	memset(resizableX, 0, (maxX * sizeof(int)));
	memset(resizableY, 0, (maxY * sizeof(int)));

	// Determine which columns and rows have components that can be resized,
	// not counting resizable components that span multiple columns or rows
	for (count = 0; count < container->numComponents; count ++)
	{
		component = container->components[count];

		if ((component->params.gridWidth < 2) &&
			(component->flags & WINFLAG_RESIZABLEX) &&
			!(component->params.flags & WINDOW_COMPFLAG_FIXEDWIDTH) &&
			!resizableX[component->params.gridX])
		{
			resizableX[component->params.gridX] = 1;
			numResizableX += 1;
		}

		if ((component->params.gridHeight < 2) &&
			(component->flags & WINFLAG_RESIZABLEY) &&
			!(component->params.flags & WINDOW_COMPFLAG_FIXEDHEIGHT) &&
			!resizableY[component->params.gridY])
		{
			resizableY[component->params.gridY] = 1;
			numResizableY += 1;
		}
	}

	// Calculate the minimum grid.  This call just gets us the minimum
	// container size value.
	calculateMinimumGrid(containerComponent, maxX, maxY, columnStartX,
		rowStartY, columnWidth, rowHeight, resizableX, resizableY,
		1 /* use minimum component sizes */);

	// Now the default grid, and default container size value.
	calculateMinimumGrid(containerComponent, maxX, maxY, columnStartX,
		rowStartY, columnWidth, rowHeight, resizableX, resizableY,
		0 /* use preferred sizes */);

	// If no width and height were specified, use defaults
	if (!width)
		width = containerComponent->width;
	if (!height)
		height = containerComponent->height;

	// Don't go below the minimums
	width = max(width, containerComponent->minWidth);
	height = max(height, containerComponent->minHeight);

	deltaWidth = (width - containerComponent->width);
	deltaHeight = (height - containerComponent->height);

	// Any change?
	if (!deltaWidth && !deltaHeight)
		goto out;

	// Distribute any width change over all the columns that have width and
	// are resizable.
	if (deltaWidth && numResizableX)
	{
		deltaWidth /= numResizableX;

		for (count = 0; count < maxX; count ++)
		{
			if (count)
			{
				columnStartX[count] = (columnStartX[count - 1] +
					columnWidth[count - 1]);
			}

			if (columnWidth[count] && resizableX[count])
				columnWidth[count] += deltaWidth;
		}
	}

	// Distribute any height change over all the rows that have height and are
	// resizable.
	if (deltaHeight && numResizableY)
	{
		deltaHeight /= numResizableY;

		for (count = 0; count < maxY; count ++)
		{
			if (count)
			{
				rowStartY[count] = (rowStartY[count - 1] +
					rowHeight[count - 1]);
			}

			if (rowHeight[count] && resizableY[count])
				rowHeight[count] += deltaHeight;
		}
	}

out:
	if (resizableY)
		kernelFree(resizableY);
	if (resizableX)
		kernelFree(resizableX);
}


static int layoutSized(kernelWindowComponent *containerComponent, int width,
	int height)
{
	// Calculate the grid layout, then loop through the subcomponents,
	// adjusting their dimensions and calling their resize() and move()
	// functions

	int status = 0;
	kernelWindowContainer *container = containerComponent->data;
	kernelWindowComponent *component = NULL;
	int maxX = 1, maxY = 1;
	int *columnStartX = NULL, *rowStartY = NULL;
	int *columnWidth = NULL, *rowHeight = NULL;
	int tmpWidth, tmpHeight;
	int tmpX, tmpY;
	int count1, count2;

	// Determine the maximum X/Y grid coordinates
	for (count1 = 0; count1 < container->numComponents; count1 ++)
	{
		component = container->components[count1];

		if ((component->params.gridX + component->params.gridWidth) > maxX)
			maxX = (component->params.gridX + component->params.gridWidth);

		if ((component->params.gridY + component->params.gridHeight) > maxY)
			maxY = (component->params.gridY + component->params.gridHeight);
	}

	columnStartX = kernelMalloc(maxX * sizeof(int));
	rowStartY = kernelMalloc(maxY * sizeof(int));
	columnWidth = kernelMalloc(maxX * sizeof(int));
	rowHeight = kernelMalloc(maxY * sizeof(int));
	if (!columnStartX || !rowStartY || !columnWidth || !rowHeight)
	{
		status = ERR_MEMORY;
		goto out;
	}

	kernelDebug(debug_gui, "WindowContainer '%s' container '%s' layout sized",
		containerComponent->window->title, container->name);

	kernelDebug(debug_gui, "WindowContainer old width=%d height=%d "
		"minWidth=%d minHeight=%d", containerComponent->width,
		containerComponent->height, containerComponent->minWidth,
		containerComponent->minHeight);

	// Calculate the grid with the extra/less space factored in
	calculateGrid(containerComponent, maxX, maxY, columnStartX, rowStartY,
		columnWidth, rowHeight, width, height);

	kernelDebug(debug_gui, "WindowContainer new width=%d height=%d "
		"minWidth=%d minHeight=%d", containerComponent->width,
		containerComponent->height, containerComponent->minWidth,
		containerComponent->minHeight);

	// Now that the grid has been recalculated, incorporating any change in
	// size, see whether we should resize/move any of the components.
	for (count1 = 0; count1 < container->numComponents; count1 ++)
	{
		component = container->components[count1];

		tmpWidth = 0;
		if ((component->flags & WINFLAG_RESIZABLEX) &&
			!(component->params.flags & WINDOW_COMPFLAG_FIXEDWIDTH))
		{
			for (count2 = 0; count2 < component->params.gridWidth; count2 ++)
				tmpWidth += columnWidth[component->params.gridX + count2];

			tmpWidth -= (component->params.padLeft +
				component->params.padRight);
		}
		else
		{
			tmpWidth = component->width;
		}

		if (tmpWidth < component->minWidth)
			tmpWidth = component->minWidth;

		tmpHeight = 0;
		if ((component->flags & WINFLAG_RESIZABLEY) &&
			!(component->params.flags & WINDOW_COMPFLAG_FIXEDHEIGHT))
		{
			for (count2 = 0; count2 < component->params.gridHeight; count2 ++)
				tmpHeight += rowHeight[component->params.gridY + count2];

			tmpHeight -= (component->params.padTop +
				component->params.padBottom);
		}
		else
		{
			tmpHeight = component->height;
		}

		if (tmpHeight < component->minHeight)
			tmpHeight = component->minHeight;

		// Should the component be resized?
		if ((tmpWidth != component->width) ||
			(tmpHeight != component->height))
		{
			if (component->resize)
				component->resize(component, tmpWidth, tmpHeight);

			component->width = tmpWidth;
			component->height = tmpHeight;
		}

		// Move it too, if applicable

		tmpX = columnStartX[component->params.gridX];
		tmpWidth = 0;

		for (count2 = 0; count2 < component->params.gridWidth; count2 ++)
			tmpWidth += columnWidth[component->params.gridX + count2];

		switch (component->params.orientationX)
		{
			case orient_left:
				tmpX += component->params.padLeft;
				break;

			case orient_center:
				tmpX += ((tmpWidth - component->width) / 2);
				break;

			case orient_right:
				tmpX += ((tmpWidth - component->width) -
					 component->params.padRight);
				break;
		}

		tmpY = rowStartY[component->params.gridY];
		tmpHeight = 0;

		for (count2 = 0; count2 < component->params.gridHeight; count2 ++)
			tmpHeight += rowHeight[component->params.gridY + count2];

		switch (component->params.orientationY)
		{
			case orient_top:
				tmpY += component->params.padTop;
				break;

			case orient_middle:
				tmpY += ((tmpHeight - component->height) / 2);
				break;

			case orient_bottom:
				tmpY += ((tmpHeight - component->height) -
					 component->params.padBottom);
				break;
		}

		if ((tmpX != component->xCoord) || (tmpY != component->yCoord))
		{
			if (component->move)
				component->move(component, tmpX, tmpY);

			component->xCoord = tmpX;
			component->yCoord = tmpY;
		}
	}

	status = 0;

 out:
	if (columnWidth)
		kernelFree(columnWidth);
	if (columnStartX)
		kernelFree(columnStartX);
	if (rowHeight)
		kernelFree(rowHeight);
	if (rowStartY)
		kernelFree(rowStartY);

	return (status);
}


static int add(kernelWindowComponent *containerComponent,
	kernelWindowComponent *component)
{
	// Add the supplied component to the container.

	int status = 0;
	kernelWindowContainer *container = containerComponent->data;
	int maxComponents = 0;
	kernelWindowComponent **components = NULL;
	int count;

	// Check params
	if (!containerComponent || !component)
	{
		kernelError(kernel_error, "NULL parameter");
		return (status = ERR_NULLPARAMETER);
	}

	if (containerComponent->type != containerComponentType)
	{
		kernelError(kernel_error, "Component is not a container");
		return (status = ERR_INVALID);
	}

	// Make sure there's room for more components
	if (container->numComponents >= container->maxComponents)
	{
		// Try to make more room
		maxComponents = (container->maxComponents * 2);
		components = kernelMalloc(maxComponents *
			sizeof(kernelWindowComponent *));
		if (!components)
		{
			kernelError(kernel_error, "Component container is full");
			return (status = ERR_MEMORY);
		}

		for (count = 0; count < container->numComponents; count ++)
			components[count] = container->components[count];

		container->maxComponents = maxComponents;
		kernelFree(container->components);
		container->components = components;
	}

	// Add it to the container
	container->components[container->numComponents++] = component;
	component->container = containerComponent;
	component->window = containerComponent->window;
	component->buffer = containerComponent->buffer;

	return (status);
}


static int delete(kernelWindowComponent *containerComponent,
	kernelWindowComponent *component)
{
	// Deletes a component from a container

	int status = 0;
	kernelWindowContainer *container = containerComponent->data;
	int count = 0;

	// Check params
	if (!containerComponent || !component)
	{
		kernelError(kernel_error, "NULL parameter");
		return (status = ERR_NULLPARAMETER);
	}

	if (containerComponent->type != containerComponentType)
	{
		kernelError(kernel_error, "Component is not a container");
		return (status = ERR_INVALID);
	}

	for (count = 0; count < container->numComponents; count ++)
	{
		if (container->components[count] == component)
		{
			// Replace the component with the last one, if applicable
			container->numComponents--;

			if ((container->numComponents > 0) &&
				(count < container->numComponents))
			{
				container->components[count] =
					container->components[container->numComponents];
			}

			component->container = NULL;
			return (status = 0);
		}
	}

	// If we fall through, it was not found
	kernelError(kernel_error, "No such component in container");
	return (status = ERR_NOSUCHENTRY);
}


static int numComps(kernelWindowComponent *component)
{
	// Count up the number of components in a container and any subcomponents.

	int numComponents = 0;
	kernelWindowContainer *container = component->data;
	kernelWindowComponent *item = NULL;
	int count;

	numComponents = container->numComponents;

	for (count = 0; count < container->numComponents; count ++)
	{
		item = container->components[count];

		if (item->numComps)
			numComponents += item->numComps(item);
	}

	return (numComponents);
}


static int flatten(kernelWindowComponent *component,
	kernelWindowComponent **array, int *numItems, unsigned flags)
{
	// Given a container, return a flattened array of components by adding
	// each one in the container, and calling each to add any subcomponents,
	// if applicable.

	kernelWindowContainer *container = component->data;
	kernelWindowComponent *item = NULL;
	int count;

	for (count = 0; count < container->numComponents; count ++)
	{
		item = container->components[count];

		if (((item->flags & flags) == flags) &&
			// Don't include pure container components
			((item->type != containerComponentType) ||
			(item->subType != genericComponentType)))
		{
			array[*numItems] = item;
			*numItems += 1;
		}

		// If this component is a container, recurse it
		if (item->flatten)
			item->flatten(item, array, numItems, flags);
	}

	return (0);
}


static int layout(kernelWindowComponent *containerComponent)
{
	// Do layout for the container.

	int status = 0;
	kernelWindowContainer *container = containerComponent->data;
	kernelWindowComponent *component = NULL;
	int count1, count2;

	// Check params
	if (!containerComponent)
	{
		kernelError(kernel_error, "NULL parameter");
		return (status = ERR_NULLPARAMETER);
	}

	if (containerComponent->type != containerComponentType)
	{
		kernelError(kernel_error, "Component is not a container");
		return (status = ERR_INVALID);
	}

	// For any components that are containers, have them do their layouts
	// first so we know their sizes.
	for (count1 = 0; count1 < container->numComponents; count1 ++)
	{
		component = container->components[count1];

		if (component->layout)
		{
			status = component->layout(component);
			if (status < 0)
				return (status);
		}
	}

	// Call our 'layoutSized' function to do the layout, but don't specify any
	// extra size since we want 'minimum' sizes for now
	status = layoutSized(containerComponent, 0, 0);
	if (status < 0)
		return (status);

	// Loop through the container's components, checking to see whether each
	// overlaps any others, and if so, decrement their levels
	for (count1 = 0; count1 < container->numComponents; count1 ++)
	{
		component = container->components[count1];

		for (count2 = 0; count2 < container->numComponents; count2 ++)
		{
			if (container->components[count2] != component)
			{
				if (doAreasIntersect(makeComponentScreenArea(component),
					makeComponentScreenArea(container->components[count2])))

				container->components[count2]->level++;
			}
		}
	}

	// Set the flag to indicate layout complete
	containerComponent->doneLayout = 1;

	return (status = 0);
}


static kernelWindowComponent *eventComp(kernelWindowComponent *component,
	windowEvent *event)
{
	// Determine which component should receive a window event at the supplied
	// coordinates.  First look for any component at the coordinates, and if
	// applicable, call that component to allow it to specify a particular
	// subcomponent to receive the event.

	kernelWindowContainer *container = component->data;
	kernelWindowComponent *item = NULL;
	int count;

	kernelDebug(debug_gui, "WindowContainer '%s' container '%s' get "
		"component", component->window->title, container->name);

	for (count = 0; count < container->numComponents; count ++)
	{
		item = container->components[count];

		// If not visible or enabled, skip it
		if (!(item->flags & WINFLAG_VISIBLE) ||
			!(item->flags & WINFLAG_ENABLED))
		{
			continue;
		}

		// Are the coordinates inside this component?
		if (isPointInside(event->xPosition, event->yPosition,
			makeComponentScreenArea(item)))
		{
			kernelDebug(debug_gui, "WindowContainer '%s' container '%s' "
				"found component", component->window->title, container->name);

			// The coordinates are inside this component.  Does it want to
			// specify a subcomponent?
			if (item->eventComp)
				return (item->eventComp(item, event));
			else
				return (item);
		}
	}

	// Nothing found.  Return the container itself.
	return (component);
}


static int setBuffer(kernelWindowComponent *containerComponent,
	graphicBuffer *buffer)
{
	// Set the graphics buffer for the container and all its subcomponents.

	int status = 0;
	kernelWindowContainer *container = containerComponent->data;
	kernelWindowComponent *component = NULL;
	int count;

	// Check params
	if (!containerComponent)
	{
		kernelError(kernel_error, "NULL parameter");
		return (status = ERR_NULLPARAMETER);
	}

	// Loop through the container's components, setting the buffers
	for (count = 0; count < container->numComponents; count ++)
	{
		component = container->components[count];

		if (component->setBuffer)
		{
			status = component->setBuffer(component, buffer);
			if (status < 0)
				return (status);
		}

		component->buffer = buffer;
	}

	return (status = 0);
}


static int draw(kernelWindowComponent *component)
{
	// Draw the component, which is really just a collection of other
	// components.

	int status = 0;

	if (component->params.flags & WINDOW_COMPFLAG_HASBORDER)
		component->drawBorder(component, 1);

	return (status);
}


static int move(kernelWindowComponent *component, int xCoord, int yCoord)
{
	// Loop through the subcomponents, adjusting their coordinates and calling
	// their move() functions

	int status = 0;
	kernelWindowContainer *container = component->data;
	kernelWindowComponent *itemComponent = NULL;
	int count;

	int differenceX = (xCoord - component->xCoord);
	int differenceY = (yCoord - component->yCoord);

	kernelDebug(debug_gui, "WindowContainer %s move components %s%d, %s%d",
		container->name, ((differenceX >= 0)? "+" : ""), differenceX,
		((differenceY >= 0)? "+" : ""), differenceY);

	if (differenceX || differenceY)
	{
		for (count = 0; count < container->numComponents; count ++)
		{
			itemComponent = container->components[count];

			if (itemComponent->move)
			{
				status = itemComponent->move(itemComponent,
					(itemComponent->xCoord + differenceX),
					(itemComponent->yCoord + differenceY));
				if (status < 0)
					return (status);
			}

			itemComponent->xCoord += differenceX;
			itemComponent->yCoord += differenceY;
		}
	}

	return (status = 0);
}


static int resize(kernelWindowComponent *component, int width, int height)
{
	// Calls our internal 'layoutSized' function
	return (layoutSized(component, width, height));
}


static int destroy(kernelWindowComponent *component)
{
	kernelWindowContainer *container = component->data;

	// Release all our memory
	if (container)
	{
		while (container->numComponents)
			kernelWindowComponentDestroy(container->components[0]);

		if (container->components)
			kernelFree(container->components);

		kernelFree(component->data);
		component->data = NULL;
	}

	return (0);
}


static void drawGrid(kernelWindowComponent *containerComponent)
{
	// This function draws grid boxes around all the grid cells containing
	// components (or parts thereof)

	kernelWindowContainer *container = containerComponent->data;
	kernelWindowComponent *component = NULL;
	int maxX = 1, maxY = 1;
	int *columnStartX = NULL, *rowStartY = NULL;
	int *columnWidth = NULL, *rowHeight = NULL;
	int gridWidth = 0, gridHeight = 0;
	int count1, count2;

	// Check params
	if (!containerComponent)
		return;

	if (containerComponent->type != containerComponentType)
	{
		kernelError(kernel_error, "Component is not a container");
		return;
	}

	// Determine the maximum X/Y grid coordinates
	for (count1 = 0; count1 < container->numComponents; count1 ++)
	{
		component = container->components[count1];

		if ((component->params.gridX + component->params.gridWidth) > maxX)
			maxX = (component->params.gridX + component->params.gridWidth);

		if ((component->params.gridY + component->params.gridHeight) > maxY)
			maxY = (component->params.gridY + component->params.gridHeight);
	}

	columnStartX = kernelMalloc(maxX * sizeof(int));
	rowStartY = kernelMalloc(maxY * sizeof(int));
	columnWidth = kernelMalloc(maxX * sizeof(int));
	rowHeight = kernelMalloc(maxY * sizeof(int));
	if (!columnStartX || !rowStartY || !columnWidth || !rowHeight)
		goto out;

	for (count1 = 0; count1 < container->numComponents; count1 ++)
	{
		if (container->components[count1]->type == containerComponentType)
			drawGrid(container->components[count1]);
	}

	// Calculate the grid
	calculateGrid(containerComponent, maxX, maxY, columnStartX, rowStartY,
		columnWidth, rowHeight, containerComponent->width,
		containerComponent->height);

	for (count1 = 0; count1 < container->numComponents; count1 ++)
	{
		component = container->components[count1];

		gridWidth = 0;
		for (count2 = 0; count2 < component->params.gridWidth; count2 ++)
			gridWidth += columnWidth[component->params.gridX + count2];

		gridHeight = 0;
		for (count2 = 0; count2 < component->params.gridHeight; count2 ++)
			gridHeight += rowHeight[component->params.gridY + count2];

		kernelGraphicDrawRect(containerComponent->buffer, &((color){0, 0, 0}),
			draw_normal, columnStartX[component->params.gridX],
			rowStartY[component->params.gridY], gridWidth, gridHeight, 1, 0);
	}

out:
	if (columnWidth)
		kernelFree(columnWidth);
	if (columnStartX)
		kernelFree(columnStartX);
	if (rowHeight)
		kernelFree(rowHeight);
	if (rowStartY)
		kernelFree(rowStartY);

	return;
}


/////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////
//
// Below here, the functions are exported for external use
//
/////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////

kernelWindowComponent *kernelWindowNewContainer(objectKey parent,
	const char *name, componentParameters *params)
{
	// Formats a kernelWindowComponent as a kernelWindowContainer

	kernelWindowComponent *component = NULL;
	kernelWindowContainer *container = NULL;

	// Check parameters.
	if (!parent || !name || !params)
	{
		kernelError(kernel_error, "NULL parameter");
		return (component = NULL);
	}

	// Get the basic component structure
	component = kernelWindowComponentNew(parent, params);
	if (!component)
		return (component);

	component->type = containerComponentType;
	component->flags |= WINFLAG_RESIZABLE;

	// Set the functions
	component->add = (int (*)(kernelWindowComponent *, objectKey)) &add;
	component->delete = &delete;
	component->numComps = &numComps;
	component->flatten = &flatten;
	component->layout = &layout;
	component->eventComp = &eventComp;
	component->setBuffer = &setBuffer;
	component->draw = &draw;
	component->move = &move;
	component->resize = &resize;
	component->destroy = &destroy;

	// Get memory for this container component
	container = kernelMalloc(sizeof(kernelWindowContainer));
	if (!container)
	{
		kernelWindowComponentDestroy(component);
		return (component = NULL);
	}

	component->data = (void *) container;

	// Now populate the component

	strncpy((char *) container->name, name, WINDOW_MAX_LABEL_LENGTH);

	// Arbitrary -- sufficient for many windows, will expand dynamically
	container->maxComponents = 64;

	container->components = kernelMalloc(container->maxComponents *
		sizeof(kernelWindowComponent *));
	if (!container->components)
	{
		kernelWindowComponentDestroy(component);
		return (component = NULL);
	}

	container->drawGrid = &drawGrid;

	return (component);
}

