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
//  kernelWindowButton.c
//

// This code is for managing kernelWindowButton objects.

#include "kernelWindow.h"	// Our prototypes are here
#include "kernelError.h"
#include "kernelFont.h"
#include "kernelImage.h"
#include "kernelMalloc.h"
#include "kernelMemory.h"
#include "kernelMisc.h"
#include <stdlib.h>
#include <string.h>

extern kernelWindowVariables *windowVariables;


static void setText(kernelWindowComponent *component, const char *label,
	int length)
{
	kernelWindowButton *button = (kernelWindowButton *) component->data;
	kernelFont *labelFont = (kernelFont *) component->params.font;
	int borderThickness = windowVariables->border.thickness;

	strncpy((char *) button->label, label,
		min(length, WINDOW_MAX_LABEL_LENGTH));
	button->label[length] = '\0';

	int tmp = ((borderThickness * 2) + 6);
	if (labelFont)
		tmp += kernelFontGetPrintedWidth(labelFont,
			(char *) component->charSet, (char *) button->label);

	if (tmp > component->width)
		component->width = tmp;

	tmp = ((borderThickness * 2) + 6);
	if (labelFont)
		tmp += labelFont->glyphHeight;

	if (tmp > component->height)
		component->height = tmp;
}


static int setImage(kernelWindowComponent *component, image *img)
{
	int status = 0;
	kernelWindowButton *button = (kernelWindowButton *) component->data;

	status = kernelImageCopyToKernel(img, (image *) &button->buttonImage);
	if (status < 0)
		return (status);

	// Button images use pure green as the transparency color
	button->buttonImage.transColor.blue = 0;
	button->buttonImage.transColor.green = 255;
	button->buttonImage.transColor.red = 0;

	int tmp = ((windowVariables->border.thickness * 2) + 6 + img->width);
	if (tmp > component->width)
		component->width = tmp;

	tmp = ((windowVariables->border.thickness * 2) + 6 + img->height);
	if (tmp > component->height)
		component->height = tmp;

	return (status = 0);
}


static void drawFocus(kernelWindowComponent *component, int focus)
{
	color *drawColor = NULL;
	int borderThickness = windowVariables->border.thickness;

	if (component->flags & WINFLAG_CANFOCUS)
	{
		if (focus)
			drawColor = (color *) &component->params.foreground;
		else
			drawColor = (color *) &component->params.background;

		kernelGraphicDrawRect(component->buffer, drawColor, draw_normal,
			(component->xCoord + borderThickness),
			(component->yCoord + borderThickness),
			(component->width - (borderThickness * 2)),
			(component->height - (borderThickness * 2)), 1, 0);
	}

	return;
}


static int draw(kernelWindowComponent *component)
{
	// Draw the button component

	kernelWindowButton *button = (kernelWindowButton *) component->data;
	kernelFont *labelFont = (kernelFont *) component->params.font;

	// Draw the background of the button
	if (button->state)
		kernelGraphicConvexShade(component->buffer,
			(color *) &component->params.background, component->xCoord,
			component->yCoord, component->width, component->height,
			shade_frombottom);
	else
		kernelGraphicConvexShade(component->buffer,
			(color *) &component->params.background, component->xCoord,
			component->yCoord, component->width, component->height,
			shade_fromtop);

	// If there is a label on the button, draw it
	if (button->label[0])
	{
		kernelGraphicDrawText(component->buffer,
			(color *) &component->params.foreground,
			(color *) &component->params.background,
			labelFont, (char *) component->charSet, (char *) button->label,
			draw_translucent, (component->xCoord + ((component->width -
				kernelFontGetPrintedWidth(labelFont,
					(char *) component->charSet,
					(char *) button->label)) / 2)),
			(component->yCoord + ((component->height -
				labelFont->glyphHeight) / 2)));
	}

	// If there is an image on the button, draw it centered on the button
	if (button->buttonImage.data)
	{
		unsigned tmpX, tmpY, tmpXoff = 0, tmpYoff = 0;
		tmpX = component->xCoord +
			((component->width - button->buttonImage.width) / 2);
		tmpY = component->yCoord +
			((component->height - button->buttonImage.height) / 2);

		if (button->buttonImage.width > (unsigned) component->width)
			tmpXoff = -((button->buttonImage.width - component->width) / 2);
		if (button->buttonImage.height > (unsigned) component->height)
			tmpYoff = -((button->buttonImage.height - component->height) / 2);

		kernelGraphicDrawImage(component->buffer,
			(image *) &button->buttonImage, draw_alphablend, tmpX, tmpY,
			tmpXoff, tmpYoff, component->width, component->height);
	}

	drawFocus(component, (component->flags & WINFLAG_HASFOCUS));

	return (0);
}


static int focus(kernelWindowComponent *component, int yesNo)
{
	drawFocus(component, yesNo);
	component->window
		->update(component->window, component->xCoord, component->yCoord,
			component->width, component->height);
	return (0);
}


static int setData(kernelWindowComponent *component, void *data, int length)
{
	// Set the button text

	kernelWindowButton *button = (kernelWindowButton *) component->data;

	if (button->label[0])
		setText(component, data, length);
	else
		setImage(component, data);

	if (component->draw)
		component->draw(component);

	component->window
		->update(component->window, component->xCoord, component->yCoord,
			component->width, component->height);

	return (0);
}


static int mouseEvent(kernelWindowComponent *component, windowEvent *event)
{
	kernelWindowButton *button = (kernelWindowButton *) component->data;

	if ((event->type & EVENT_MOUSE_DOWN) ||
		(event->type & EVENT_MOUSE_UP) ||
		(event->type & EVENT_MOUSE_DRAG))
	{
		if ((event->type == EVENT_MOUSE_LEFTUP) ||
			(event->type == EVENT_MOUSE_DRAG))
		{
			button->state = 0;
		}
		else if (event->type == EVENT_MOUSE_LEFTDOWN)
		{
			button->state = 1;
		}

		if (component->draw)
			draw(component);

		component->window
			->update(component->window, component->xCoord, component->yCoord,
				component->width, component->height);
	}

	return (0);
}


static int keyEvent(kernelWindowComponent *component, windowEvent *event)
{
	int status = 0;
	kernelWindowButton *button = (kernelWindowButton *) component->data;

	// We're only looking for 'enter' key releases, which we turn into mouse
	// button presses.
	if ((event->type & EVENT_MASK_KEY) && (event->key == keyEnter))
	{
		// If the button is not pushed, ignore this
		if ((event->type == EVENT_KEY_UP) && !(button->state))
			return (status = 0);

		if (event->type == EVENT_KEY_DOWN)
			event->type = EVENT_MOUSE_LEFTDOWN;
		if (event->type == EVENT_KEY_UP)
			event->type = EVENT_MOUSE_LEFTUP;

		status = mouseEvent(component, event);
	}

	return (status);
}


static int destroy(kernelWindowComponent *component)
{
	kernelWindowButton *button = (kernelWindowButton *) component->data;

	// Release all our memory
	if (button)
	{
		// If we have an image, release the image data
		if (button->buttonImage.data)
		{
			kernelFree(button->buttonImage.data);
			button->buttonImage.data = NULL;
		}

		// The button itself.
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

kernelWindowComponent *kernelWindowNewButton(objectKey parent,
	const char *label, image *buttonImage, componentParameters *params)
{
	// Formats a kernelWindowComponent as a kernelWindowButton

	kernelWindowComponent *component = NULL;
	kernelWindowButton *button = NULL;

	// Check params.  It's okay for the image or label to be NULL
	if (!parent || !params)
	{
		kernelError(kernel_error, "NULL parameter");
		return (component = NULL);
	}

	// Get the basic component structure
	component = kernelWindowComponentNew(parent, params);
	if (!component)
		return (component);

	component->type = buttonComponentType;
	component->flags |= (WINFLAG_CANFOCUS | WINFLAG_RESIZABLEX);

	// Set the functions
	component->draw = &draw;
	component->focus = &focus;
	component->setData = &setData;
	component->mouseEvent = &mouseEvent;
	component->keyEvent = &keyEvent;
	component->destroy = &destroy;

	// If font is NULL, use the default
	if (!component->params.font)
		component->params.font = windowVariables->font.varWidth.medium.font;

	button = kernelMalloc(sizeof(kernelWindowButton));
	if (!button)
	{
		kernelWindowComponentDestroy(component);
		return (component = NULL);
	}

	component->data = (void *) button;

	// If the button has a label, copy it
	if (label)
		setText(component, label, strlen(label));

	// If the button has an image, copy it
	if (buttonImage && buttonImage->data)
	{
		if (setImage(component, buttonImage) < 0)
		{
			kernelWindowComponentDestroy(component);
			return (component = NULL);
		}
	}

	component->minWidth = component->width;
	component->minHeight = component->height;

	return (component);
}

