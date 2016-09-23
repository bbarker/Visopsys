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
//  kernelWindowList.c
//

// This code is for managing kernelWindowList objects.  These are containers
// for kernelWindowListItem components.

#include "kernelWindow.h"	// Our prototypes are here
#include "kernelDebug.h"
#include "kernelError.h"
#include "kernelMalloc.h"
#include "kernelWindowEventStream.h"
#include <string.h>

extern kernelWindowVariables *windowVariables;


static void setVisibleItems(kernelWindowComponent *component)
{
	// Set the visible/not visible attributes of the components based on the
	// scroll state

	kernelWindowList *list = component->data;
	kernelWindowContainer *container = list->container->data;
	kernelWindowComponent *listItemComponent = NULL;
	int xCoord = 0, yCoord = 0;
	int count;

	kernelDebug(debug_gui, "WindowList set visible items");

	for (count = 0; count < container->numComponents; count ++)
	{
		listItemComponent = container->components[count];

		if ((listItemComponent->params.gridY >= list->firstVisibleRow) &&
			(listItemComponent->params.gridY <
				(list->rows + list->firstVisibleRow)))
		{
			xCoord = (list->container->xCoord +
				(listItemComponent->params.gridX * list->itemWidth));
			yCoord = (list->container->yCoord +
				((listItemComponent->params.gridY -
					list->firstVisibleRow) * list->itemHeight));

			if ((xCoord != listItemComponent->xCoord) ||
				(yCoord != listItemComponent->yCoord))
			{
				kernelDebug(debug_gui, "WindowList item %d oldX %d, oldY %d, "
					"newX %d, newY %d", count, listItemComponent->xCoord,
					listItemComponent->yCoord, xCoord, yCoord);

				if (listItemComponent->move)
					listItemComponent->move(listItemComponent, xCoord, yCoord);

				listItemComponent->xCoord = xCoord;
				listItemComponent->yCoord = yCoord;
			}

			if (!(listItemComponent->flags & WINFLAG_VISIBLE))
				kernelWindowComponentSetVisible(listItemComponent, 1);
		}
		else if (listItemComponent->flags & WINFLAG_VISIBLE)
			kernelWindowComponentSetVisible(listItemComponent, 0);
	}
}


static void drawVisibleItems(kernelWindowComponent *component)
{
	kernelWindowList *list = component->data;
	kernelWindowContainer *container = list->container->data;
	int count;

	//kernelDebug(debug_gui, "WindowList draw visible items");

	// Draw the background of the list
	kernelGraphicDrawRect(component->buffer,
		(color *) &component->params.background, draw_normal,
		list->container->xCoord, list->container->yCoord,
		list->container->width, list->container->height, 1, 1);

	for (count = 0; count < container->numComponents; count ++)
	{
		if ((container->components[count]->flags & WINFLAG_VISIBLE) &&
			(container->components[count]->draw))
		{
			//kernelDebug(debug_gui, "WindowList item %d xCoord %d, yCoord %d",
			//	count, container->components[count]->xCoord,
			//	container->components[count]->yCoord);
			container->components[count]->draw(container->components[count]);
		}
	}
}


static void setItemGrid(kernelWindowComponent *component)
{
	// Set the grid coordinates of the items

	kernelWindowList *list = component->data;
	kernelWindowContainer *container = list->container->data;
	int column = 0;
	int row = 0;
	int count;

	for (count = 0; count < container->numComponents; count ++)
	{
		container->components[count]->params.gridX = column;
		container->components[count]->params.gridY = row;

		column += 1;
		if (column >= list->columns)
		{
			row += 1;
			column = 0;
		}
	}
}


static inline int isMouseInScrollBar(windowEvent *event,
	kernelWindowComponent *component)
{
	// We use this to determine whether a mouse event is inside the scroll bar

	kernelWindowScrollBar *scrollBar = component->data;

	if (scrollBar->dragging ||
		(event->xPosition >= (component->window->xCoord + component->xCoord)))
	{
		return (1);
	}
	else
	{
		return (0);
	}
}


static void setScrollBar(kernelWindowList *list)
{
	// Set the scroll bar display and position percentages

	scrollBarState state;

	if (list->itemRows > list->rows)
	{
		state.positionPercent = ((list->firstVisibleRow * 100) /
			(list->itemRows - list->rows));
		state.displayPercent = ((list->rows * 100) / list->itemRows);
	}
	else
	{
		state.positionPercent = 0;
		state.displayPercent = 100;
	}

	if (list->scrollBar && list->scrollBar->setData)
		list->scrollBar->setData(list->scrollBar, &state,
			sizeof(scrollBarState));
}


static void setItemSizes(kernelWindowList *list)
{
	// Set the sizes of all the items, making them uniform.

	kernelWindowContainer *container = list->container->data;
	kernelWindowComponent *listItemComponent = NULL;
	int count;

	kernelDebug(debug_gui, "WindowList set item sizes");

	for (count = 0; count < container->numComponents; count ++)
	{
		listItemComponent = container->components[count];

		if ((listItemComponent->width != list->itemWidth) ||
			(listItemComponent->height != list->itemHeight))
		{
			kernelDebug(debug_gui, "WindowList item %d oldWidth %d, oldHeight "
				"%d, newWidth %d, newHeight %d", count,
				listItemComponent->width, listItemComponent->height,
				list->itemWidth, list->itemHeight);

			if (listItemComponent->resize)
				listItemComponent->resize(listItemComponent, list->itemWidth,
					list->itemHeight);

			listItemComponent->width = list->itemWidth;
			listItemComponent->height = list->itemHeight;
		}
	}
}


static void setRowsAndColumns(kernelWindowComponent *component)
{
	kernelWindowList *list = component->data;

	kernelDebug(debug_gui, "WindowList set rows and columns");

	if (list->multiColumn)
	{
		if (list->itemWidth)
			list->columns = (list->container->width / list->itemWidth);
	}
	else
	{
		// Re-set the item width to the container width
		list->itemWidth = list->container->width;

		// Set the sizes of our items
		setItemSizes(list);
	}

	if (list->itemHeight)
		list->rows = (list->container->height / list->itemHeight);

	kernelDebug(debug_gui, "WindowList rows %d, columns %d", list->rows,
		list->columns);
}


static void selectionScroll(kernelWindowComponent *component)
{
	// Looks at the currently selected item and scrolls

	kernelWindowList *list = component->data;
	kernelWindowContainer *container = list->container->data;
	kernelWindowComponent *listItemComponent = NULL;

	//kernelDebug(debug_gui, "WindowList check scroll");

	if (list->selectedItem == -1)
		return;

	listItemComponent = container->components[list->selectedItem];

	// Do we have to scroll the list?
	if ((listItemComponent->params.gridY < list->firstVisibleRow) ||
		(listItemComponent->params.gridY >=
	 		(list->firstVisibleRow + list->rows)))
	{
		if (listItemComponent->params.gridY < list->firstVisibleRow)
			list->firstVisibleRow = listItemComponent->params.gridY;

		else
			list->firstVisibleRow =
			((listItemComponent->params.gridY - list->rows) + 1);

		if (list->scrollBar)
			// Set the scroll bar display percent
			setScrollBar(list);

		setVisibleItems(component);
		drawVisibleItems(component);

		component->window
			->update(component->window, component->xCoord, component->yCoord,
				 component->width, component->height);
	}
}


static int setSelected(kernelWindowComponent *component, int item)
{
	// The selected list item has changed.

	int status = 0;
	kernelWindowList *list = component->data;
	kernelWindowContainer *container = list->container->data;
	kernelWindowComponent *listItemComponent = NULL;
	int oldItem = 0;

	kernelDebug(debug_gui, "WindowList set selected %d", item);

	if ((item < -1) || (item >= container->numComponents))
	{
		kernelError(kernel_error, "Illegal component number %d", item);
		return (status = ERR_BOUNDS);
	}

	oldItem = list->selectedItem;

	if ((oldItem != item) && (oldItem != -1))
	{
		// Deselect the old selected item
		listItemComponent = container->components[oldItem];
		listItemComponent->setSelected(listItemComponent, 0);
	}

	list->selectedItem = item;

	// See if we have to scroll
	selectionScroll(component);

	if ((oldItem != item) && (item != -1))
	{
		// Select the selected item
		listItemComponent = container->components[item];
		listItemComponent->setSelected(listItemComponent, 1);
	}

	return (status = 0);
}


static int numComps(kernelWindowComponent *component)
{
	int numItems = 0;
	kernelWindowList *list = component->data;

	if (list->container->numComps)
		// Count our container's components
		numItems = list->container->numComps(list->container);

	if (list->scrollBar)
		// Add 1 for our scrollbar
		numItems += 1;

	return (numItems);
}


static int flatten(kernelWindowComponent *component,
	kernelWindowComponent **array, int *numItems, unsigned flags)
{
	int status = 0;
	kernelWindowList *list = component->data;

	if (list->container->flatten)
		// Flatten our container
		status = list->container->flatten(list->container, array, numItems,
			flags);

	if (list->scrollBar && ((list->scrollBar->flags & flags) == flags))
	{
		// Add our scrollbar
		array[*numItems] = list->scrollBar;
		*numItems += 1;
	}

	return (status);
}


static int layout(kernelWindowComponent *component)
{
	// Loop through the list items in the container and put them into a grid

	kernelWindowList *list = component->data;
	kernelWindowContainer *container = list->container->data;
	int scrollBarX = 0;

	kernelDebug(debug_gui, "WindowList layout");

	// If we've not previously done layout, set the container and component
	// sizes
	if (!component->doneLayout)
	{
		list->container->width = (list->columns * list->itemWidth);
		list->container->height = (list->rows * list->itemHeight);
		component->width = list->container->width;
		component->height = list->container->height;

		if (list->scrollBar)
			component->width += list->scrollBar->width;

		kernelDebug(debug_gui, "WindowList width %d, height %d",
			component->width, component->height);
	}

	// Calculate the number of total rows needed to accommodate *all* the items
	// (not just visible ones)
	if (list->columns)
	{
		list->itemRows = (container->numComponents / list->columns);
		if (container->numComponents % list->columns)
			list->itemRows += 1;
	}

	setItemGrid(component);
	setVisibleItems(component);

	if (list->scrollBar)
	{
		// Set up the scroll bar size and location and adjust the list
		// component size to account for it

		scrollBarX = (component->xCoord + list->container->width);

		if ((list->scrollBar->xCoord != scrollBarX) ||
			(list->scrollBar->yCoord != component->yCoord))
		{
			if (list->scrollBar->move)
				list->scrollBar->move(list->scrollBar, scrollBarX,
					component->yCoord);
			list->scrollBar->xCoord = scrollBarX;
			list->scrollBar->yCoord = component->yCoord;
		}

		if (list->scrollBar->height != list->container->height)
		{
			if (list->scrollBar->resize)
				list->scrollBar->resize(list->scrollBar,
					list->scrollBar->width, list->container->height);
			list->scrollBar->height = list->container->height;
		}
	}

	component->doneLayout = 1;
	return (0);
}


static kernelWindowComponent *activeComp(kernelWindowComponent *component)
{
	// Return the selected list item component, if applicable

	kernelWindowList *list = component->data;
	kernelWindowContainer *container = list->container->data;

	kernelDebug(debug_gui, "WindowList get active component");

	if (list->selectedItem >= 0)
		return (container->components[list->selectedItem]);
	else
		return (component);
}


static kernelWindowComponent *eventComp(kernelWindowComponent *component,
	windowEvent *event)
{
	// Determine which (if any) of our list items received the event.

	// This function is implemented only so that we can return the list item
	// in the sole case of a right-click context menu invocation.  If it's
	// some other kind of event, we want the main windowList component to
	// receive it.

	kernelWindowList *list = component->data;
	kernelWindowContainer *container = list->container->data;
	kernelWindowComponent *listItemComponent = NULL;
	windowEvent tmpEvent;
	int count;

	kernelDebug(debug_gui, "WindowList get event component");

	// If this is anything other than a right-click, we want our windowList
	// component to receive it.
	if (!(event->type & EVENT_MOUSE_RIGHTDOWN))
	{
		kernelDebug(debug_gui, "WindowList return main component");
		return (component);
	}

	for (count = 0; count < container->numComponents; count ++)
	{
		listItemComponent = container->components[count];

		// If not visible or enabled, skip it
		if (!(listItemComponent->flags & WINFLAG_VISIBLE) ||
			!(listItemComponent->flags & WINFLAG_ENABLED))
		{
			continue;
		}

		// Are the coordinates inside this component?
		if (isPointInside(event->xPosition, event->yPosition,
			makeComponentScreenArea(listItemComponent)))
		{
			kernelDebug(debug_gui, "WindowList found right-clicked list item");

			// If the list item doesn't have its own context menu, return the
			// windowList component.
			if (!listItemComponent->contextMenu)
			{
				kernelDebug(debug_gui, "WindowList return main component");
				return (component);
			}

			setSelected(component, count);

			// Make a copy of this event, make it also a 'selection' event,
			// and put it into the windowList's event stream

			memcpy(&tmpEvent, event, sizeof(windowEvent));

			// Make this also a 'selection' event
			tmpEvent.type |= EVENT_SELECTION;

			// Adjust to the coordinates of the windowList component
			tmpEvent.xPosition -=
				(component->window->xCoord + component->xCoord);
			tmpEvent.yPosition -=
				(component->window->yCoord + component->yCoord);

			// Put this mouse event into windowList component's
			// windowEventStream
			kernelWindowEventStreamWrite(&component->events, &tmpEvent);

			return (listItemComponent);
		}
	}

	// Nothing found.  Return the windowList component itself.
	kernelDebug(debug_gui, "WindowList return main component");
	return (component);
}


static int setBuffer(kernelWindowComponent *component, graphicBuffer *buffer)
{
	// Set the graphics buffer for the component and its subcomponents.

	int status = 0;
	kernelWindowList *list = component->data;

	if (list->container->setBuffer)
	{
		// Do our container
		status = list->container->setBuffer(list->container, buffer);
		if (status < 0)
			return (status);
	}

	list->container->buffer = buffer;

	if (list->scrollBar && list->scrollBar->setBuffer)
	{
		// Also do our scrollbar
		status = list->scrollBar->setBuffer(list->scrollBar, buffer);
		if (status < 0)
			return (status);
		list->scrollBar->buffer = buffer;
	}

	return (status = 0);
}


static int draw(kernelWindowComponent *component)
{
	// Draw the component, which is really just a collection of other
	// components.

	int status = 0;
	kernelWindowList *list = component->data;

	//kernelDebug(debug_gui, "WindowList Draw width %d, height %d",
	//	component->width, component->height);

	drawVisibleItems(component);

	// Draw any scrollbars
	if (list->scrollBar && list->scrollBar->draw)
		list->scrollBar->draw(list->scrollBar);

	if ((component->params.flags & WINDOW_COMPFLAG_HASBORDER) ||
		(component->flags & WINFLAG_HASFOCUS))
	{
		component->drawBorder(component, 1);
	}

	return (status);
}


static int focus(kernelWindowComponent *component, int yesNo)
{
	kernelDebug(debug_gui, "WindowList %s focus", (yesNo? "got" : "lost"));

	component->drawBorder(component, yesNo);
	component->window->update(component->window, (component->xCoord - 2),
		(component->yCoord - 2), (component->width + 4),
		(component->height + 4));
	return (0);
}


static int getSelected(kernelWindowComponent *component, int *itemNumber)
{
	kernelWindowList *list = component->data;

	kernelDebug(debug_gui, "WindowList get selected %d", list->selectedItem);

	*itemNumber = list->selectedItem;
	return (0);
}


static void populateList(kernelWindowComponent *listComponent,
	listItemParameters *items, int numItems)
{
	// Sets up all the kernelWindowListItem subcomponents of the list

	kernelWindowList *list = listComponent->data;
	kernelWindowContainer *container = list->container->data;
	kernelWindowComponent *listItemComponent = NULL;
	componentParameters params;
	int count;

	kernelDebug(debug_gui, "WindowList populate list (%d items)", numItems);

	// If the list already has components, get rid of them
	while (container->numComponents)
		kernelWindowComponentDestroy(container
			->components[container->numComponents - 1]);

	// If the selected item is greater than the new number we have, make it the
	// last one
	if (list->selectedItem >= numItems)
		list->selectedItem = (numItems - 1);

	// Standard parameters for the list items
	memcpy(&params, (componentParameters *) &listComponent->params,
		sizeof(componentParameters));
	params.gridX = 0;
	params.gridY = 0;
	params.gridWidth = 1;
	params.gridHeight = 1;
	params.padTop = 0;
	params.padBottom = 0;
	params.padLeft = 0;
	params.padRight = 0;
	params.orientationX = orient_top;
	params.orientationY = orient_left;
	params.flags |= (WINDOW_COMPFLAG_FIXEDWIDTH | WINDOW_COMPFLAG_FIXEDHEIGHT);

	list->itemWidth = 0;
	list->itemHeight = 0;

	// Loop through the list item parameter list, creating kernelWindowListItem
	// components and adding them to this component
	for (count = 0; count < numItems; count ++)
	{
		kernelDebug(debug_gui, "WindowList create %s", items[count].text);
		listItemComponent = kernelWindowNewListItem(list->container,
			list->type, &items[count], &params);
		if (!listItemComponent)
			continue;

		// Make it not visible for now.
		kernelWindowComponentSetVisible(listItemComponent, 0);

		// The component should adopt and keep the color of the list component
		listItemComponent->params.flags |= WINDOW_COMPFLAG_CUSTOMBACKGROUND;

		if (listItemComponent->width > list->itemWidth)
			list->itemWidth = listItemComponent->width;

		if (listItemComponent->height > list->itemHeight)
			list->itemHeight = listItemComponent->height;

		if (listItemComponent->setSelected)
			listItemComponent->setSelected(listItemComponent,
				(count == list->selectedItem));
	}

	// Set the sizes of all the items
	setItemSizes(list);

	if (listComponent->doneLayout)
		// We're re-populating the list, so re-calculate the number of rows
		// and columns
		setRowsAndColumns(listComponent);

	// Do layout
	layout(listComponent);

	// Update the scroll bar position percent
	setScrollBar(list);

	return;
}


static int getData(kernelWindowComponent *component, void *buffer, int size)
{
	// Return the object keys of the list components

	kernelWindowList *list = component->data;
	kernelWindowContainer *container = list->container->data;
	kernelWindowComponent **components = buffer;
	int count;

	for (count = 0; ((count < container->numComponents) && (count < size));
		count ++)
	{
		components[count] = container->components[count];
	}

	return (0);
}


static int setData(kernelWindowComponent *component, void *buffer, int size)
{
	// Resets the subcomponents

	kernelDebug(debug_gui, "WindowList set data");

	// Re-populate the list
	populateList(component, buffer, size);

	// Re-draw the list
	if (component->draw)
		component->draw(component);

	component->window
		->update(component->window, component->xCoord, component->yCoord,
			component->width, component->height);

	return (0);
}


static int move(kernelWindowComponent *component, int xCoord, int yCoord)
{
	kernelWindowList *list = component->data;
	int scrollBarX = 0;

	kernelDebug(debug_gui, "WindowList move oldX %d, oldY %d, newX %d, newY "
		"%d (%s%d, %s%d)", component->xCoord, component->yCoord, xCoord,
		yCoord, ((xCoord >= component->xCoord)? "+" : ""),
		(xCoord - component->xCoord),
		((yCoord >= component->yCoord)? "+" : ""),
		(yCoord - component->yCoord));

	// Move our container
	if (list->container->move)
		list->container->move(list->container, xCoord, yCoord);

	list->container->xCoord = xCoord;
	list->container->yCoord = yCoord;

	// Move any scroll bars
	if (list->scrollBar)
	{
		scrollBarX = (xCoord + list->container->width);

		if ((list->scrollBar->xCoord != scrollBarX) ||
			(list->scrollBar->yCoord != yCoord))
		{
			if (list->scrollBar->move)
				list->scrollBar->move(list->scrollBar, scrollBarX, yCoord);

			list->scrollBar->xCoord = scrollBarX;
			list->scrollBar->yCoord = yCoord;
		}
	}

	return (0);
}


static int resize(kernelWindowComponent *component, int width, int height)
{
	kernelWindowList *list = component->data;

	kernelDebug(debug_gui, "WindowList resize oldWidth %d, oldHeight %d, "
		"width %d, height %d", component->width, component->height,
		width, height);

	list->container->width = width;
	list->container->height = height;

	if (list->scrollBar)
		list->container->width -= list->scrollBar->width;

	if (component->doneLayout &&
		((width != component->width) || (height != component->height)))
	{
		// Re-calculate the number of rows and columns
		setRowsAndColumns(component);
	}

	// Redo layout
	layout(component);

	// Update the scroll bar position percent
	setScrollBar(list);

	return (0);
}


static int mouseEvent(kernelWindowComponent *component, windowEvent *event)
{
	// When mouse events happen to list components, we pass them on to the
	// appropriate kernelWindowListItem component

	int status = 0;
	kernelWindowList *list = component->data;
	kernelWindowContainer *container = list->container->data;
	kernelWindowScrollBar *scrollBar = NULL;
	kernelWindowComponent *listItemComponent = NULL;
	int firstVisibleRow = 0;
	int count;

	kernelDebug(debug_gui, "WindowList mouse event");

	// Is the event in one of our scroll bars?
	if (list->scrollBar && isMouseInScrollBar(event, list->scrollBar))
	{
		if (list->scrollBar->mouseEvent)
		{
			// First, pass on the event to the scroll bar
			status = list->scrollBar->mouseEvent(list->scrollBar, event);
			if (status < 0)
				return (status);
		}

		scrollBar = list->scrollBar->data;

		// Now, adjust the visible subcomponents based on the 'position
		// percent' of the scroll bar
		if (container->numComponents > list->rows)
		{
			firstVisibleRow = (((list->itemRows - list->rows) *
				scrollBar->state.positionPercent) / 100);

			if (firstVisibleRow != list->firstVisibleRow)
			{
				list->firstVisibleRow = firstVisibleRow;

				setVisibleItems(component);
				drawVisibleItems(component);

				component->window->update(component->window, component->xCoord,
					component->yCoord, component->width, component->height);
			}
		}
	}
	else if (event->type & (EVENT_MOUSE_DOWN | EVENT_MOUSE_UP))
	{
		// Figure out which list item was clicked based on the coordinates
		// of the event
		for (count = 0; count < container->numComponents; count ++)
		{
			listItemComponent = container->components[count];

			if ((listItemComponent->flags & WINFLAG_VISIBLE) &&
				isPointInside(event->xPosition, event->yPosition,
					makeComponentScreenArea(listItemComponent)))
			{
				// Don't bother passing the mouse event to the list item
				setSelected(component, count);

				// Make this also a 'selection' event
				event->type |= EVENT_SELECTION;

				break;
			}
		}
	}

	return (status = 0);
}


static int keyEvent(kernelWindowComponent *component, windowEvent *event)
{
	// We allow the user to control the list widget with key presses, such
	// as cursor movements.

	int status = 0;
	kernelWindowList *list = component->data;
	kernelWindowContainer *container = list->container->data;
	kernelWindowComponent *listItemComponent = NULL;
	int gridX = 0, gridY = 0;
	int count;

	kernelDebug(debug_gui, "WindowList key event");

	if (event->type == EVENT_KEY_DOWN)
	{
		if (!container->numComponents)
			return (status = 0);

		if (list->selectedItem >= 0)
		{
			// Get the currently selected item
			listItemComponent = container->components[list->selectedItem];
			gridX = listItemComponent->params.gridX;
			gridY = listItemComponent->params.gridY;

			switch (event->key)
			{
				case keyUpArrow:
					// Cursor up
					if (gridY > 0)
						gridY -= 1;
					break;

				case keyDownArrow:
					// Cursor down
					if (gridY < (list->itemRows - 1))
						gridY += 1;
					break;

				case keyLeftArrow:
					// Cursor left
					if (gridX > 0)
						gridX -= 1;
					else if (gridY > 0)
					{
						gridX = (list->columns - 1);
						gridY -= 1;
					}
					break;

				case keyRightArrow:
					// Cursor right
					if (gridX < (list->columns - 1))
						gridX += 1;
					else if (gridY < (list->itemRows - 1))
					{
						gridX = 0;
						gridY += 1;
					}
					break;

				case keyEnter:
					// ENTER.  We will make this also a 'selection' event.
					event->type |= EVENT_SELECTION;
					break;

				default:
					break;
			}

			if ((gridX != listItemComponent->params.gridX) ||
				(gridY != listItemComponent->params.gridY))
			{
				// Find an item with these coordinates
				for (count = 0; count < container->numComponents; count ++)
				{
					if ((container->components[count]->params.gridX == gridX) &&
						(container->components[count]->params.gridY == gridY))
					{
						// Don't bother passing the key event to the list item
						setSelected(component, count);
						// Make this also a 'selection' event
						event->type |= EVENT_SELECTION;
						break;
					}
				}
			}
		}
		else
		{
			// No item was selected, so we just select the first item.
			setSelected(component, 0);
			// Make this also a 'selection' event
			event->type |= EVENT_SELECTION;
		}
	}

	return (status = 0);
}


static int destroy(kernelWindowComponent *component)
{
	kernelWindowList *list = component->data;

	kernelDebug(debug_gui, "WindowList destroy");

	// Release all our memory
	if (list)
	{
		if (list->container)
			kernelWindowComponentDestroy(list->container);

		if (list->scrollBar)
			kernelWindowComponentDestroy(list->scrollBar);

		kernelFree(component->data);
		component->data = NULL;
	}

	return (0);
}


/////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////
//
// Below here, the functions are exported for external use
//
/////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////

kernelWindowComponent *kernelWindowNewList(objectKey parent,
	windowListType type, int rows, int columns, int selectMultiple,
	listItemParameters *items, int numItems, componentParameters *params)
{
	// Formats a kernelWindowComponent as a kernelWindowList

	kernelWindowComponent *component = NULL;
	kernelWindowList *list = NULL;
	kernelWindowContainer *container = NULL;
	kernelWindowListItem *listItem = NULL;
	componentParameters subParams;

	// Check params
	if (!parent || !items || !params)
	{
		kernelError(kernel_error, "NULL parameter");
		return (component = NULL);
	}

	kernelDebug(debug_gui, "WindowList new list rows %d, columns %d, "
		"selectMultiple %d, numItems %d", rows, columns, selectMultiple,
		numItems);

	// Get the basic component structure
	component = kernelWindowComponentNew(parent, params);
	if (!component)
		return (component);

	component->type = listComponentType;
	component->flags |= (WINFLAG_CANFOCUS | WINFLAG_RESIZABLE);

	// Set the functions
	component->numComps = &numComps;
	component->flatten = &flatten;
	component->layout = &layout;
	component->activeComp = &activeComp;
	component->eventComp = &eventComp;
	component->setBuffer = &setBuffer;
	component->draw = &draw;
	component->focus = &focus;
	component->getSelected = &getSelected;
	component->setSelected = &setSelected;
	component->getData = &getData;
	component->setData = &setData;
	component->move = &move;
	component->resize = &resize;
	component->mouseEvent = &mouseEvent;
	component->keyEvent = &keyEvent;
	component->destroy = &destroy;

	// If default colors were requested, override the standard background color
	// with the one we prefer (white)
	if (!(component->params.flags & WINDOW_COMPFLAG_CUSTOMBACKGROUND))
	{
		memcpy((color *) &component->params.background, &COLOR_WHITE,
			sizeof(color));
		component->params.flags |= WINDOW_COMPFLAG_CUSTOMBACKGROUND;
	}

	// If font is NULL, use the default
	if (!component->params.font)
		component->params.font = windowVariables->font.varWidth.medium.font;

	// Get memory for this list component
	list = kernelMalloc(sizeof(kernelWindowList));
	if (!list)
	{
		kernelWindowComponentDestroy(component);
		return (component = NULL);
	}

	component->data = (void *) list;

	list->type = type;
	list->columns = columns;
	list->rows = rows;
	list->selectMultiple = selectMultiple;
	if (list->columns > 1)
		list->multiColumn = 1;

	// Get our container component
	list->container = kernelWindowNewContainer(parent, "windowlist container",
		params);
	if (!list->container)
	{
		kernelWindowComponentDestroy(component);
		return (component = NULL);
	}

	// Remove it from the parent container
	removeFromContainer(list->container);

	container = list->container->data;

	// Standard parameters for a scroll bar
	memcpy(&subParams, params, sizeof(componentParameters));
	subParams.flags &= ~(WINDOW_COMPFLAG_CUSTOMFOREGROUND |
		WINDOW_COMPFLAG_CUSTOMBACKGROUND);

	if (!(subParams.flags & WINDOW_COMPFLAG_NOSCROLLBARS))
	{
		// Get our scrollbar component
		list->scrollBar = kernelWindowNewScrollBar(parent, scrollbar_vertical,
			0, component->height, &subParams);
		if (!list->scrollBar)
		{
			kernelWindowComponentDestroy(component);
			return (component = NULL);
		}

		// Remove it from the parent container
		removeFromContainer(list->scrollBar);
	}

	// Fill up
	populateList(component, items, numItems);

	if (!numItems)
	{
		// Set some minimum sizes
		component->resize(component, 100, 50);
		component->width = 100;
		component->height = 50;
	}

	component->minWidth = component->width;
	component->minHeight = component->height;

	// Take care of any default selection
	if (selectMultiple)
	{
		list->selectedItem = -1;
	}
	else
	{
		// Multiple selections are not allowed, so we select the first one
		if (numItems)
		{
			listItem = container->components[0]->data;
			listItem->selected = 1;
			list->selectedItem = 0;
		}
	}

	return (component);
}

