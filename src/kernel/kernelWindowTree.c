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
//  kernelWindowTree.c
//

// This code is for managing kernelWindowTree objects.

#include "kernelWindow.h"	// Our prototypes are here
#include "kernelDebug.h"
#include "kernelError.h"
#include "kernelFont.h"
#include "kernelMalloc.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define INDENT	11

extern kernelWindowVariables *windowVariables;


static int countItemsRecursive(windowTreeItem *item)
{
	// Recursively count the number of items in the tree

	int numItems = 0;

	if (!item)
		return (0);

	if (item->firstChild)
		numItems += countItemsRecursive(item->firstChild);

	if (item->next)
		numItems += countItemsRecursive(item->next);

	// +1 for self
	return (numItems + 1);
}


static void copyItemsRecursive(kernelWindowTree *tree, windowTreeItem *srcItem,
	windowTreeItem **link)
{
	// Given a hierarchy of items, copy them into the tree

	windowTreeItem *destItem = &tree->items[tree->numItems++];

	if (!srcItem)
		return;

	kernelDebug(debug_gui, "WindowTree copying \"%s\"", srcItem->text);

	memcpy(destItem, srcItem, sizeof(windowTreeItem));

	if (link)
		*link = destItem;

	if (srcItem->firstChild)
		copyItemsRecursive(tree, srcItem->firstChild, &destItem->firstChild);

	if (srcItem->next)
		copyItemsRecursive(tree, srcItem->next, &destItem->next);
}


static int createListItemsRecursive(kernelWindowComponent *component,
	windowTreeItem *item)
{
	int status = 0;
	kernelWindowTree *tree = component->data;
	listItemParameters itemParams;

	if (!item)
		return (0);

	kernelDebug(debug_gui, "WindowTree create list item for \"%s\"",
		item->text);

	memset(&itemParams, 0, sizeof(listItemParameters));
	memcpy(itemParams.text, item->text, WINDOW_MAX_LABEL_LENGTH);

	item->key = kernelWindowNewListItem(tree->container, windowlist_textonly,
		&itemParams, (componentParameters *) &component->params);
	if (!item->key)
		return (status = ERR_NOCREATE);

	if (item->firstChild)
	{
		status = createListItemsRecursive(component, item->firstChild);
		if (status < 0)
			return (status);
	}

	if (item->next)
	{
		status = createListItemsRecursive(component, item->next);
		if (status < 0)
			return (status);
	}

	return (status = 0);
}


static void layoutItemsRecursive(kernelWindowComponent *component,
	windowTreeItem *item, int level)
{
	kernelWindowTree *tree = component->data;
	kernelWindowComponent *itemComponent = NULL;
	int xCoord = 0, yCoord = 0;

	if (!item)
		return;

	itemComponent = item->key;

	// Calculate the X and Y coordinates for the list item
	xCoord = (tree->container->xCoord + (level * INDENT) + INDENT);
	yCoord = (tree->container->yCoord + (int)(itemComponent->height *
		(tree->expandedItems - tree->scrolledLines)));

	// Remember what row it's in
	itemComponent->params.gridY = tree->expandedItems;

	tree->expandedItems += 1;

	// Determine whether the item is currently visible, or scrolled out of
	// the container area
	if ((yCoord >= tree->container->yCoord) &&
		((yCoord + itemComponent->height) <=
			(tree->container->yCoord + tree->container->height)))
	{
		// This one is visible

		if ((xCoord != itemComponent->xCoord) ||
			(yCoord != itemComponent->yCoord))
		{
			kernelDebug(debug_gui, "WindowTree position \"%s\" at (%d, %d)",
				item->text, xCoord, yCoord);

			// Move it
			if (itemComponent->move)
				itemComponent->move(itemComponent, xCoord, yCoord);

			itemComponent->xCoord = xCoord;
			itemComponent->yCoord = yCoord;
		}

		// If the item goes off the right edge of the container, shorten it.
		snprintf(item->displayText, WINDOW_MAX_LABEL_LENGTH, "%s%s",
			(item->subItem? "- " : ""), item->text);
		if ((xCoord + kernelFontGetPrintedWidth((kernelFont *)
			component->params.font, (char *) component->charSet,
				item->displayText)) >=
			((tree->container->xCoord + tree->container->width) - 2))
		{
			while ((xCoord + kernelFontGetPrintedWidth((kernelFont *)
				component->params.font, (char *) component->charSet,
				item->displayText)) >=
				((tree->container->xCoord + tree->container->width) - 2))
			{
				item->displayText[strlen(item->displayText) - 1] = '\0';
			}
		}

		kernelWindowComponentSetData(itemComponent, item->displayText,
			strlen(item->displayText), 0 /* no render */);

		if (!(itemComponent->flags & WINFLAG_VISIBLE))
			kernelWindowComponentSetVisible(itemComponent, 1);

		tree->visibleItems += 1;
	}

	if (item->expanded)
	{
		if (item->firstChild)
			layoutItemsRecursive(component, item->firstChild, (level + 1));
	}

	if (item->next)
		layoutItemsRecursive(component, item->next, level);
}


static void layoutItems(kernelWindowComponent *component)
{
	kernelWindowTree *tree = component->data;
	kernelWindowContainer *container = tree->container->data;
	int count;

	// Set all item components to not visible
	for (count = 0; count < container->numComponents; count ++)
	{
		if (container->components[count]->flags & WINFLAG_VISIBLE)
			kernelWindowComponentSetVisible(container->components[count], 0);
	}

	tree->expandedItems = 0;
	tree->visibleItems = 0;
	layoutItemsRecursive(component, tree->items, 0 /* level */);

	kernelDebug(debug_gui, "WindowTree %d visible items", tree->visibleItems);
}


static int populateTree(kernelWindowComponent *component,
	windowTreeItem *rootItem)
{
	int status = 0;
	kernelWindowTree *tree = component->data;
	int numItems = 0;
	int count;

	numItems = countItemsRecursive(rootItem);

	kernelDebug(debug_gui, "WindowTree populate tree (%d items)", numItems);

	// Free old stuff
	if (tree->items)
	{
		for (count = 0; count < tree->numItems; count ++)
		{
			if (tree->items[count].key)
				kernelWindowComponentDestroy(tree->items[count].key);
		}

		kernelFree(tree->items);
		tree->items = NULL;
	}

	tree->numItems = 0;

	if (!rootItem)
		return (status = 0);

	// Get kernel memory for the items
	tree->items = kernelMalloc(numItems * sizeof(windowTreeItem));
	if (!tree->items)
		return (status = ERR_MEMORY);

	copyItemsRecursive(tree, rootItem, NULL /* no link */);

	status = createListItemsRecursive(component, tree->items);
	if (status < 0)
		return (status);

	layoutItems(component);

	return (status = 0);
}


static void setScrollBar(kernelWindowTree *tree)
{
	// Set the scroll bar display and position percentages

	scrollBarState state;

	kernelDebug(debug_gui, "WindowTree setScrollBar visibleItems=%d rows=%d "
		"scrolledLines=%d", tree->visibleItems, tree->rows,
		tree->scrolledLines);

	if (tree->expandedItems > tree->rows)
	{
		state.positionPercent = ((tree->scrolledLines * 100) /
			(tree->expandedItems - tree->rows));
		state.displayPercent = ((tree->rows * 100) / tree->expandedItems);
	}
	else
	{
		state.positionPercent = 0;
		state.displayPercent = 100;
	}

	if (tree->scrollBar && tree->scrollBar->setData)
		tree->scrollBar->setData(tree->scrollBar, &state,
			sizeof(scrollBarState));
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


static void expandCollapse(kernelWindowComponent *component, int item)
{
	kernelWindowTree *tree = component->data;

	if (tree->items[item].expanded)
		tree->items[item].expanded = 0;
	else
		tree->items[item].expanded = 1;

	layoutItems(component);

	// Should this (collapse) cause a scroll-down?  So that the component is
	// still filled with items?
	if ((tree->visibleItems < tree->rows) && tree->scrolledLines)
	{
		tree->scrolledLines = max(0, (tree->expandedItems - tree->rows));
		layoutItems(component);
	}

	setScrollBar(tree);

	if (component->draw)
		component->draw(component);

	component->window->update(component->window, component->xCoord,
		component->yCoord, component->width, component->height);
}


static void scroll(kernelWindowComponent *component, int lines)
{
	kernelWindowTree *tree = component->data;

	tree->scrolledLines += lines;
	layoutItems(component);
	setScrollBar(tree);

	if (component->draw)
		component->draw(component);

	component->window->update(component->window, component->xCoord,
		component->yCoord, component->width, component->height);
}


static int numComps(kernelWindowComponent *component)
{
	int numItems = 0;
	kernelWindowTree *tree = component->data;

	kernelDebug(debug_gui, "WindowTree numComps");

	if (tree->container->numComps)
		// Count our container's components
		numItems = tree->container->numComps(tree->container);

	// Add 2 for our scrollbars
	numItems += 2;

	kernelDebug(debug_gui, "WindowTree numItems=%d", numItems);

	return (numItems);
}


static int flatten(kernelWindowComponent *component,
	kernelWindowComponent **array, int *numItems, unsigned flags)
{
	int status = 0;
	kernelWindowTree *tree = component->data;

	kernelDebug(debug_gui, "WindowTree flatten");

	if (tree->container->flatten)
	{
		// Flatten our container
		status = tree->container->flatten(tree->container, array, numItems,
			flags);
	}

	if ((tree->scrollBar->flags & flags) == flags)
	{
		// Add our scrollbar
		array[*numItems] = tree->scrollBar;
		*numItems += 1;
	}

	return (status);
}


static kernelWindowComponent *activeComp(kernelWindowComponent *component)
{
	// Return the selected list item component, if applicable

	kernelWindowTree *tree = component->data;
	kernelWindowContainer *container = tree->container->data;

	kernelDebug(debug_gui, "WindowTree get active component");

	if (tree->selectedItem >= 0)
		return (container->components[tree->selectedItem]);
	else
		return (component);
}


static int setBuffer(kernelWindowComponent *component, graphicBuffer *buffer)
{
	// Set the graphics buffer for the component's subcomponents.

	int status = 0;
	kernelWindowTree *tree = component->data;

	kernelDebug(debug_gui, "WindowTree setBuffer");

	if (tree->container->setBuffer)
	{
		status = tree->container->setBuffer(tree->container, buffer);
		if (status < 0)
			return (status);
	}

	tree->container->buffer = buffer;

	if (tree->scrollBar->setBuffer)
	{
		status = tree->scrollBar->setBuffer(tree->scrollBar, buffer);
		if (status < 0)
			return (status);
	}

	tree->scrollBar->buffer = buffer;

	return (status = 0);
}


static int draw(kernelWindowComponent *component)
{
	// Draw the tree component

	kernelWindowTree *tree = component->data;
	kernelWindowContainer *container = tree->container->data;
	int width = 0, xCoord = 0, yCoord = 0;
	int count;

	kernelDebug(debug_gui, "WindowTree draw");

	// Draw the background of the list
	kernelGraphicDrawRect(component->buffer,
		(color *) &component->params.background, draw_normal,
		tree->container->xCoord, tree->container->yCoord,
		tree->container->width, tree->container->height, 1, 1);

	for (count = 0; count < container->numComponents; count ++)
	{
		if (container->components[count]->flags & WINFLAG_VISIBLE)
		{
			kernelDebug(debug_gui, "WindowTree item %d xCoord %d, yCoord %d",
				count, container->components[count]->xCoord,
				container->components[count]->yCoord);

			if (tree->items[count].firstChild)
			{
				// Draw an expansion box

				width = (INDENT - 2);
				xCoord = (container->components[count]->xCoord - (width + 1));
				yCoord = (container->components[count]->yCoord +
					((container->components[count]->height - width) / 2));

				kernelGraphicDrawRect(component->buffer,
					(color *) &component->params.foreground, draw_normal,
					xCoord, yCoord, width, width, 1, 0);

				kernelGraphicDrawLine(component->buffer,
					(color *) &component->params.foreground, draw_normal,
					(xCoord + 2), (yCoord + (width / 2)),
					(xCoord + (width - 3)), (yCoord + (width / 2)));

				if (!tree->items[count].expanded)
				{
					kernelGraphicDrawLine(component->buffer,
						(color *) &component->params.foreground, draw_normal,
						(xCoord + (width / 2)), (yCoord + 2),
						(xCoord + (width / 2)), (yCoord + (width - 3)));
				}
			}

			if (container->components[count]->draw)
				container->components[count]->
					draw(container->components[count]);
		}
	}

	// Draw our scroll bar too

	if (tree->scrollBar->draw)
		tree->scrollBar->draw(tree->scrollBar);

	if (component->params.flags & WINDOW_COMPFLAG_HASBORDER)
		component->drawBorder(component, 1);

	return (0);
}


static int move(kernelWindowComponent *component, int xCoord, int yCoord)
{
	kernelWindowTree *tree = component->data;
	int scrollBarX = 0;

	kernelDebug(debug_gui, "WindowTree move from (%d, %d) to (%d, %d)",
		component->xCoord, component->yCoord, xCoord, yCoord);

	// Move our container
	if (tree->container->move)
		tree->container->move(tree->container, xCoord, yCoord);

	tree->container->xCoord = xCoord;
	tree->container->yCoord = yCoord;

	// Move any scroll bars

	scrollBarX = (xCoord + tree->container->width);

	if (tree->scrollBar->move)
		tree->scrollBar->move(tree->scrollBar, scrollBarX, yCoord);

	tree->scrollBar->xCoord = scrollBarX;
	tree->scrollBar->yCoord = yCoord;

	return (0);
}


static int resize(kernelWindowComponent *component, int width, int height)
{
	int status = 0;
	kernelWindowTree *tree = component->data;
	int scrollBarX = 0;

	kernelDebug(debug_gui, "WindowTree resize from %dx%d to %dx%d",
		component->width, component->height, width, height);

	if (width != component->width)
	{
		// Resize the container
		tree->container->width = (width - tree->scrollBar->width);

		// Move/resize the scroll bar too

		scrollBarX = (component->xCoord + tree->container->width);

		if (tree->scrollBar->move)
			tree->scrollBar->move(tree->scrollBar, scrollBarX,
				component->yCoord);

		tree->scrollBar->xCoord = scrollBarX;
	}

	if (height != component->height)
	{
		// Resize the container
		tree->container->height = height;

		// Calculate a new number of rows we can display
		if (tree->numItems)
		{
			tree->rows = (height /
				((kernelWindowComponent *) tree->items[0].key)->height);

			kernelDebug(debug_gui, "WindowTree rows now %d", tree->rows);
		}

		// Move/resize scroll bars too

		if (tree->scrollBar->resize)
			tree->scrollBar->resize(tree->scrollBar, tree->scrollBar->width,
				tree->container->height);

		tree->scrollBar->height = tree->container->height;
	}

	if ((width != component->width) || (height != component->height))
	{
		layoutItems(component);

		// Should this cause a scroll-down?  So that the component is still
		// filled with items?
		if ((tree->visibleItems < tree->rows) && tree->scrolledLines)
		{
			tree->scrolledLines = max(0, (tree->expandedItems - tree->rows));
			layoutItems(component);
		}

		setScrollBar(tree);
	}

	return (status = 0);
}


static int setData(kernelWindowComponent *component, void *buffer,
	int size __attribute__((unused)))
{
	// Set new tree contents.

	int status = 0;
	kernelWindowTree *tree = component->data;

	status = populateTree(component, buffer);

	// Calculate a new number of rows we can display
	if (!tree->rows && tree->numItems)
	{
		tree->rows = (tree->container->height /
			((kernelWindowComponent *) tree->items[0].key)->height);
		kernelDebug(debug_gui, "WindowTree rows now %d", tree->rows);
	}

	setScrollBar(tree);

	// Nothing is selected now
	tree->selectedItem = -1;

	if (component->draw)
		component->draw(component);

	return (status);
}


static int getSelected(kernelWindowComponent *component, int *itemNumber)
{
	kernelWindowTree *tree = component->data;

	kernelDebug(debug_gui, "WindowTree get selected %d", tree->selectedItem);

	*itemNumber = tree->selectedItem;
	return (0);
}


static int setSelected(kernelWindowComponent *component, int item)
{
	// The selected list item has changed.

	int status = 0;
	kernelWindowTree *tree = component->data;
	kernelWindowContainer *container = tree->container->data;
	kernelWindowComponent *itemComponent = NULL;
	int oldItem = 0;

	kernelDebug(debug_gui, "WindowTree set selected %d", item);

	if ((item < -1) || (item >= container->numComponents))
	{
		kernelError(kernel_error, "Illegal component number %d", item);
		return (status = ERR_BOUNDS);
	}

	oldItem = tree->selectedItem;

	if ((oldItem != item) && (oldItem != -1))
	{
		// Deselect the old selected item
		itemComponent = container->components[oldItem];
		itemComponent->setSelected(itemComponent, 0);
	}

	tree->selectedItem = item;

	if ((oldItem != item) && (item != -1))
	{
		// Select the selected item
		itemComponent = container->components[item];
		itemComponent->setSelected(itemComponent, 1);
	}

	return (status = 0);
}


static int mouseEvent(kernelWindowComponent *component, windowEvent *event)
{
	int status = 0;
	kernelWindowTree *tree = component->data;
	kernelWindowContainer *container = tree->container->data;
	kernelWindowScrollBar *scrollBar = NULL;
	kernelWindowComponent *itemComponent = NULL;
	int scrolledLines = 0;
	screenArea *area = NULL;
	int count;

	kernelDebug(debug_gui, "WindowTree mouse event");

	// Is the event in our scroll bar?
	if (tree->scrollBar && isMouseInScrollBar(event, tree->scrollBar))
	{
		scrollBar = tree->scrollBar->data;

		// First, pass on the event to the scroll bar
		if (tree->scrollBar->mouseEvent)
			tree->scrollBar->mouseEvent(tree->scrollBar, event);

		scrolledLines = 0;
		if (tree->expandedItems > tree->rows)
		{
			scrolledLines = ((scrollBar->state.positionPercent *
				(tree->expandedItems - tree->rows)) / 100);
		}

		if (scrolledLines != tree->scrolledLines)
		{
			// Adjust the scroll value of the tree area based on the
			// positioning of the scroll bar.
			tree->scrolledLines = scrolledLines;

			layoutItems(component);

			if (component->draw)
				component->draw(component);

			component->window->update(component->window, component->xCoord,
				component->yCoord, component->width, component->height);
		}
	}
	else if (event->type & (EVENT_MOUSE_DOWN | EVENT_MOUSE_UP))
	{
		// Figure out which list item was clicked based on the coordinates
		// of the event
		kernelDebug(debug_gui, "WindowTree mouse click");

		for (count = 0; count < container->numComponents; count ++)
		{
			itemComponent = container->components[count];

			if (itemComponent->flags & WINFLAG_VISIBLE)
			{
				area = makeComponentScreenArea(itemComponent);

				// Was it a click inside the item itself?
				if (isPointInside(event->xPosition, event->yPosition, area))
				{
					// Don't bother passing the mouse event to the list item
					setSelected(component, count);

					// Make this also a 'selection' event
					event->type |= EVENT_SELECTION;

					break;
				}

				// Was it a click to expand or collapse the item?
				if ((event->type & EVENT_MOUSE_DOWN) &&
					(event->yPosition >= area->topY) &&
					(event->yPosition <= area->bottomY) &&
					(event->xPosition >= (area->leftX - INDENT)) &&
					(event->xPosition <= area->leftX))
				{
					expandCollapse(component, count);
					break;
				}
			}
		}
	}

	return (status);
}


static int keyEvent(kernelWindowComponent *component, windowEvent *event)
{
	// We allow the user to control the tree widget with key presses, such
	// as cursor movements.

	int status = 0;
	kernelWindowTree *tree = component->data;
	kernelWindowContainer *container = tree->container->data;
	kernelWindowComponent *itemComponent = NULL;
	int gridY = 0;
	int count;

	kernelDebug(debug_gui, "WindowTree key event");

	if (event->type == EVENT_KEY_DOWN)
	{
		if (!container->numComponents)
			return (status = 0);

		if (tree->selectedItem >= 0)
		{
			// Get the currently selected item
			itemComponent = container->components[tree->selectedItem];
			gridY = itemComponent->params.gridY;

			switch (event->key)
			{
				case keyUpArrow:
					// Cursor up
					if (gridY > 0)
						gridY -= 1;
					break;

				case keyDownArrow:
					// Cursor down
					if (gridY < (tree->expandedItems - 1))
						gridY += 1;
					break;

				case keySpaceBar:
					// Expand or collapse
					if (tree->items[tree->selectedItem].firstChild)
						expandCollapse(component, tree->selectedItem);
					break;

				case keyEnter:
					// ENTER.  We will make this also a 'selection' event.
					event->type |= EVENT_SELECTION;
					break;

				default:
					break;
			}

			if (gridY != itemComponent->params.gridY)
			{
				// Scroll up?
				if (gridY < tree->scrolledLines)
				{
					scroll(component, (gridY - tree->scrolledLines));
				}
				// Down?
				else if (gridY >= (tree->scrolledLines + tree->rows))
				{
					scroll(component, ((gridY + 1) -
						(tree->scrolledLines + tree->rows)));
				}

				// Find a visible item with these coordinates
				for (count = 0; count < container->numComponents; count ++)
				{
					if ((container->components[count]->flags &
							WINFLAG_VISIBLE) &&
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
	kernelWindowTree *tree = component->data;

	if (tree)
	{
		if (tree->items)
			kernelFree(tree->items);

		if (tree->container)
			kernelWindowComponentDestroy(tree->container);

		if (tree->scrollBar)
			kernelWindowComponentDestroy(tree->scrollBar);

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

kernelWindowComponent *kernelWindowNewTree(objectKey parent,
	windowTreeItem *rootItem, int width, int height,
	componentParameters *params)
{
	// Formats a kernelWindowComponent as a kernelWindowTree

	int status = 0;
	kernelWindowComponent *component = NULL;
	kernelWindowTree *tree = NULL;
	componentParameters subParams;

	// Check params
	if (!parent || !params)
	{
		kernelError(kernel_error, "NULL parameter");
		return (component = NULL);
	}

	kernelDebug(debug_gui, "WindowTree new tree");

	// Get the basic component structure
	component = kernelWindowComponentNew(parent, params);
	if (!component)
		return (component);

	component->type = treeComponentType;
	component->flags |= (WINFLAG_CANFOCUS | WINFLAG_RESIZABLE);

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

	// Get memory for the kernelWindowTree
	tree = kernelMalloc(sizeof(kernelWindowTree));
	if (!tree)
	{
		kernelWindowComponentDestroy(component);
		return (component = NULL);
	}

	// Set any defaults for the tree
	tree->selectedItem = -1;

	component->data = (void *) tree;

	// Get our container component
	memcpy(&subParams, params, sizeof(componentParameters));
	tree->container = kernelWindowNewContainer(parent, "windowtree container",
		&subParams);
	if (!tree->container)
	{
		kernelWindowComponentDestroy(component);
		return (component = NULL);
	}

	// Remove it from the parent container
	removeFromContainer(tree->container);

	// Set a default/minimum sizes
	width = max(width, (windowVariables->slider.width * 2));
	height = max(height, windowVariables->slider.width);
	tree->container->width = (width - windowVariables->slider.width);
	tree->container->height = height;

	// We need a scroll bar as well.

	// Standard parameters for a scroll bar
	subParams.flags &= ~(WINDOW_COMPFLAG_CUSTOMFOREGROUND |
		WINDOW_COMPFLAG_CUSTOMBACKGROUND);
	tree->scrollBar = kernelWindowNewScrollBar(parent, scrollbar_vertical,
		0, tree->container->height, &subParams);
	if (!tree->scrollBar)
	{
		kernelWindowComponentDestroy(component);
		return (component = NULL);
	}

	// Remove the scrollbar from the parent container
	removeFromContainer(tree->scrollBar);

	component->width = width;
	component->height = height;
	component->minWidth = component->width;
	component->minHeight = component->height;

	// Set the functions
	component->numComps = &numComps;
	component->flatten = &flatten;
	component->activeComp = &activeComp;
	component->setBuffer = &setBuffer;
	component->draw = &draw;
	component->move = &move;
	component->resize = &resize;
	component->setData = &setData;
	component->getSelected = &getSelected;
	component->setSelected = &setSelected;
	component->mouseEvent = &mouseEvent;
	component->keyEvent = &keyEvent;
	component->destroy = &destroy;

	status = populateTree(component, rootItem);
	if (status < 0)
	{
		kernelWindowComponentDestroy(component);
		return (component = NULL);
	}

	// Do layout
	resize(component, width, height);

	return (component);
}

