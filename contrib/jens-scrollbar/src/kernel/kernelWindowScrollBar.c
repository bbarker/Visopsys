//
//  Visopsys
//  Copyright (C) 1998-2013 J. Andrew McLaughlin
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
//  kernelWindowScrollBar.c
//

// This code is for managing kernelWindowScrollBar objects.

#include "kernelWindow.h"     // Our prototypes are here
#include "kernelMalloc.h"
#include "kernelMisc.h"
#include <stdlib.h>

extern kernelWindowVariables *windowVariables;


static void calcSliderSizePos(kernelWindowScrollBar *scrollBar, int width,
			      int height)
{
  if (scrollBar->type == scrollbar_horizontal)
    {
      scrollBar->sliderWidth =
	(((width - (windowVariables->border.thickness * 2)) *
	  scrollBar->state.displayPercent) / 100);
      scrollBar->sliderHeight =
	(height - (windowVariables->border.thickness * 2));

      // Need a minimum width
      scrollBar->sliderWidth =
	max(scrollBar->sliderWidth, (windowVariables->border.thickness * 3));

      scrollBar->sliderX =
	(((width - (windowVariables->border.thickness * 2) -
	   scrollBar->sliderWidth) * scrollBar->state.positionPercent) / 100);
      scrollBar->sliderY = 0;
    }
  else if (scrollBar->type == scrollbar_vertical)
    {
      scrollBar->sliderWidth =
	(width - (windowVariables->border.thickness * 2));
      scrollBar->sliderHeight =
	(((height - (windowVariables->border.thickness * 2)) *
	  scrollBar->state.displayPercent) / 100);

      // Need a minimum height
      scrollBar->sliderHeight =
	max(scrollBar->sliderHeight, (windowVariables->border.thickness * 3));

      scrollBar->sliderX = 0;
      scrollBar->sliderY =
	((((height - (windowVariables->border.thickness * 2)) -
	   scrollBar->sliderHeight) * scrollBar->state.positionPercent) / 100);
    }
}


static void calcSliderPosPercent(kernelWindowScrollBar *scrollBar, int width,
				 int height)
{
  int extraSpace = 0;

  if (scrollBar->type == scrollbar_horizontal)
    {
      extraSpace = ((width - (windowVariables->border.thickness * 2)) -
		    scrollBar->sliderWidth);
      if (extraSpace > 0)
	scrollBar->state.positionPercent =
	  ((scrollBar->sliderX * 100) / extraSpace);
      else
	scrollBar->state.positionPercent = 0;
    }
  else if (scrollBar->type == scrollbar_vertical)
    {
      extraSpace = ((height - (windowVariables->border.thickness * 2)) -
		    scrollBar->sliderHeight);
      if (extraSpace > 0)
	scrollBar->state.positionPercent =
	  ((scrollBar->sliderY * 100) / extraSpace);
      else
	scrollBar->state.positionPercent = 0;
    }
}


static int draw(kernelWindowComponent *component)
{
  // Draw the scroll bar component

  kernelWindowScrollBar *scrollBar = component->data;
  int sliderDrawX = (component->xCoord + windowVariables->border.thickness +
		     scrollBar->sliderX);
  int sliderDrawY = (component->yCoord + windowVariables->border.thickness +
		     scrollBar->sliderY);
  
  // Clear the background
  kernelGraphicDrawRect(component->buffer,
			(color *) &(component->params.background),
			draw_normal, component->xCoord, component->yCoord,
			component->width, component->height, 1, 1);

  // Draw the outer border
  kernelGraphicDrawGradientBorder(component->buffer,
				  component->xCoord, component->yCoord,
				  component->width, component->height,
				  windowVariables->border.thickness, (color *)
				  &(component->params.background),
				  windowVariables->border.shadingIncrement,
				  draw_reverse, border_all);

  // Draw the slider.  First the shading
  kernelGraphicConvexShade(component->buffer,
			   (color *) &(component->params.foreground),
			   sliderDrawX, sliderDrawY, scrollBar->sliderWidth,
			   scrollBar->sliderHeight,
			   ((scrollBar->type == scrollbar_horizontal)?
			    shade_fromtop : shade_fromleft));

  // Then its border
  kernelGraphicDrawGradientBorder(component->buffer, sliderDrawX, sliderDrawY,
				  scrollBar->sliderWidth,
				  scrollBar->sliderHeight,
				  windowVariables->border.thickness, (color *)
				  &(component->params.background),
				  windowVariables->border.shadingIncrement,
				  draw_normal, border_all);
  return (0);
}


static int resize(kernelWindowComponent *component, int width, int height)
{
  calcSliderSizePos(component->data, width, height);
  return (0);
}


static int getData(kernelWindowComponent *component, void *buffer, int size)
{
  // Gets the state of the scroll bar

  kernelWindowScrollBar *scrollBar = component->data;

  kernelMemCopy((void *) &(scrollBar->state), buffer,
		max(size, (int) sizeof(scrollBarState)));

  return (0);
}


static int setData(kernelWindowComponent *component, void *buffer, int size)
{
  // Sets the state of the scroll bar

  kernelWindowScrollBar *scrollBar = component->data;

  kernelMemCopy(buffer, (void *) &(scrollBar->state),
		max(size, (int) sizeof(scrollBarState)));

  calcSliderSizePos(scrollBar, component->width, component->height);

  draw(component);

  component->window
    ->update(component->window, component->xCoord, component->yCoord,
	     component->width, component->height);

  return (0);
}


static int mouseEvent(kernelWindowComponent *component, windowEvent *event)
{
  kernelWindowScrollBar *scrollBar = component->data;
  int eventX = 0;
  int eventY = 0;
  static int dragging = 0;
  static int dragX;
  static int dragY;

  // Get X and Y coordinates relative to the component
  eventX = (event->xPosition - component->window->xCoord - component->xCoord);
  eventY = (event->yPosition - component->window->yCoord - component->yCoord);

// Is the mouse event moving
if (scrollBar->type == scrollbar_horizontal){
  if (eventX != scrollBar->sliderX)
    {
      if (event->type == EVENT_MOUSE_DRAG)
	{
	  if (dragging)
	    {
	      // The scroll bar is still moving.  Set the new position
		scrollBar->sliderX += (eventX - dragX);
	    }
	  else
	    // The scroll bar has started moving
	    dragging = 1;

	  // Save the current dragging Y and Y coordinate 
	  dragX = eventX;
	  dragY = eventY;
	}
      else
	// Not dragging.  Do nothing.
	dragging = 0;
    }
}

if (scrollBar->type == scrollbar_vertical){
  if (eventY != scrollBar->sliderY)
    {
      if (event->type == EVENT_MOUSE_DRAG)
	{
	  if (dragging)
	    {
	      // The scroll bar is still moving.  Set the new position
	      scrollBar->sliderY += (eventY - dragY);
	    }
	  else
	    // The scroll bar has started moving
	    dragging = 1;

	  // Save the current dragging Y and Y coordinate 
	  dragX = eventX;
	  dragY = eventY;
	}
      else
	// Not dragging.  Do nothing.
	dragging = 0;
    }
}

  else if (scrollBar->type == scrollbar_horizontal)
    {
      // Is it in the space on either side of the slider?
      
      if ((event->type == EVENT_MOUSE_LEFTDOWN) &&
	  (eventX > 0) && (eventX < scrollBar->sliderX))
	// It's to the left of the slider
	scrollBar->sliderX -= scrollBar->sliderWidth;
      
      else if ((event->type == EVENT_MOUSE_LEFTDOWN) &&
	       (eventX >= (scrollBar->sliderX + scrollBar->sliderWidth)) &&
	       (eventX < (component->width -
			  windowVariables->border.thickness)))
	// It's to the right of the slider
	scrollBar->sliderX += scrollBar->sliderWidth;
      
      else
	// Do nothing.
	return (0);
    }

  else if (scrollBar->type == scrollbar_vertical)
    {
      // Is it in the space above or below the slider?
      
      if ((event->type == EVENT_MOUSE_LEFTDOWN) &&
	  (eventY > 0) && (eventY < scrollBar->sliderY))
	// It's above the slider
	scrollBar->sliderY -= scrollBar->sliderHeight;
      
      else if ((event->type == EVENT_MOUSE_LEFTDOWN) &&
	       (eventY >= (scrollBar->sliderY + scrollBar->sliderHeight)) &&
	       (eventY <
		(component->height - windowVariables->border.thickness)))
	// It's below the slider
	scrollBar->sliderY += scrollBar->sliderHeight;

      else
	// Do nothing.
	return (0);
    }

  // Make sure the slider stays within the bounds

  if (scrollBar->type == scrollbar_horizontal)
    {
      if (scrollBar->sliderX < 0)
	scrollBar->sliderX = 0;

      if ((scrollBar->sliderX + scrollBar->sliderWidth) >=
	  (component->width - (windowVariables->border.thickness * 2)))
	scrollBar->sliderX =
	  (component->width - (windowVariables->border.thickness * 2) -
	   scrollBar->sliderWidth);
    }

  else if (scrollBar->type == scrollbar_vertical)
    {
      if (scrollBar->sliderY < 0)
	scrollBar->sliderY = 0;

      if ((scrollBar->sliderY + scrollBar->sliderHeight) >=
	  (component->height - (windowVariables->border.thickness * 2)))
	scrollBar->sliderY =
	  (component->height - (windowVariables->border.thickness * 2) -
	   scrollBar->sliderHeight);
    }

  // Recalculate the position percentage
  calcSliderPosPercent(scrollBar, component->width, component->height);

  draw(component);
  
  component->window
    ->update(component->window, component->xCoord, component->yCoord,
	     component->width, component->height);

  return (0);
}


static int destroy(kernelWindowComponent *component)
{
  // Release all our memory
  if (component->data)
    {
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


kernelWindowComponent *kernelWindowNewScrollBar(objectKey parent,
			scrollBarType type, int width, int height,
			componentParameters *params)
{
  // Formats a kernelWindowComponent as a kernelWindowScrollBar

  kernelWindowComponent *component = NULL;
  kernelWindowScrollBar *scrollBar = NULL;

  // Check parameters.
  if ((parent == NULL) || (params == NULL))
    return (component = NULL);

  // Get the basic component structure
  component = kernelWindowComponentNew(parent, params);
  if (component == NULL)
    return (component);

  // If default colors were requested, override the standard foreground color
  // with the one we prefer (blueish)
  if (!(component->params.flags & WINDOW_COMPFLAG_CUSTOMFOREGROUND))
    {
      component->params.foreground.red = 0x84;
      component->params.foreground.green = 0x84;
      component->params.foreground.blue = 0xF8;
      component->params.flags |= WINDOW_COMPFLAG_CUSTOMFOREGROUND;
    }

  component->type = scrollBarComponentType;
  component->flags |= WINFLAG_RESIZABLE;

  scrollBar = kernelMalloc(sizeof(kernelWindowScrollBar));
  if (scrollBar == NULL)
    {
      kernelFree((void *) component);
      return (component = NULL);
    }

  if ((type == scrollbar_vertical) && !width)
    width = 20;
  if ((type == scrollbar_horizontal) && !height)
    height = 20;

  component->width = width;
  component->height = height;

  scrollBar->type = type;
  scrollBar->state.displayPercent = 100;
  scrollBar->state.positionPercent = 0;
  calcSliderSizePos(scrollBar, component->width, component->height);

  component->minWidth = component->width;
  component->minHeight = component->height;
  component->data = (void *) scrollBar;

  // The functions
  component->draw = &draw;
  component->resize = &resize;
  component->getData = &getData;
  component->setData = &setData;
  component->mouseEvent = &mouseEvent;
  component->destroy = &destroy;

  return (component);
}
