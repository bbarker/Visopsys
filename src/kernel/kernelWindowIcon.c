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
//  kernelWindowIcon.c
//

// This code is for managing kernelWindowIcon objects.

#include "kernelWindow.h"	// Our prototypes are here
#include "kernelDebug.h"
#include "kernelError.h"
#include "kernelFont.h"
#include "kernelImage.h"
#include "kernelMalloc.h"
#include <stdlib.h>
#include <string.h>

#define MAX_LABEL_WIDTH		90
#define IMAGEX	\
	(component->xCoord + ((component->width - icon->iconImage.width) / 2))

extern kernelWindowVariables *windowVariables;


static void splitLabelAt(kernelWindowIcon *icon, int line, int splitAt,
	int preserve)
{
	int count;

	if (preserve)
	{
		// Shift data by 1 byte
		for (count = (WINDOW_MAX_LABEL_LENGTH - 1);
			count > ((icon->labelLine[line] - icon->labelData) + splitAt);
			count --)
		{
			icon->labelData[count] = icon->labelData[count - 1];
		}
	}

	// Move the following pointers
	for (count = icon->labelLines; count > (line + 1); count --)
	{
		if (count < WINDOW_MAX_LABEL_LINES)
		{
			icon->labelLine[count] = icon->labelLine[count - 1];
			if (preserve)
				icon->labelLine[count] += 1;
		}
	}

	// Set the new pointer
	icon->labelLine[line][splitAt] = '\0';
	if (icon->labelLines < WINDOW_MAX_LABEL_LINES)
	{
		icon->labelLine[line + 1] = (icon->labelLine[line] + splitAt + 1);
		icon->labelLines += 1;
	}
}


static void splitLabelsAtNewlines(kernelWindowIcon *icon)
{
	int labelCount, charCount;

	for (labelCount = 0; labelCount < icon->labelLines; labelCount ++)
	{
		for (charCount = 0;
			charCount < (int) strlen(icon->labelLine[labelCount]);
			charCount ++)
		{
			if (icon->labelLine[labelCount][charCount] == '\n')
			{
				splitLabelAt(icon, labelCount, charCount,
					0 /* no preserve */);
				break;
			}
		}
	}
}


static void splitLongLabel(kernelWindowIcon *icon, int line, kernelFont *font,
	char *charSet)
{
	int labelLen = 0;
	char tmp[WINDOW_MAX_LABEL_LENGTH + 1];
	int count;

	if (line < (WINDOW_MAX_LABEL_LINES - 1))
	{
		// Try to find a 'space' character, starting at the end of the string

		strcpy(tmp, icon->labelLine[line]);
		labelLen = strlen(tmp);

		for (count = (labelLen - 1); count > 0; count --)
		{
			if (tmp[count] == ' ')
			{
				tmp[count] = '\0';
				if (kernelFontGetPrintedWidth(font, charSet, tmp) <=
					MAX_LABEL_WIDTH)
				{
					splitLabelAt(icon, line, count, 0 /* no preserve */);
					return;
				}
			}
		}

		// Just split at the longest point

		strcpy(tmp, icon->labelLine[line]);
		labelLen = strlen(tmp);

		while (kernelFontGetPrintedWidth(font, charSet, tmp) >
			MAX_LABEL_WIDTH)
		{
			tmp[--labelLen] = '\0';
		}

		splitLabelAt(icon, line, labelLen, 1 /* preserve */);
	}
	else
	{
		// Truncate

		for (count = 0; kernelFontGetPrintedWidth(font, charSet,
			icon->labelLine[line]) > MAX_LABEL_WIDTH ; count ++)
		{
			strcpy((char *)(icon->labelData + (WINDOW_MAX_LABEL_LENGTH -
				(4 + count))), " ...");
		}
	}
}


static void splitLongLabels(kernelWindowIcon *icon, kernelFont *font,
	char *charSet)
{
	int count;

	for (count = 0; count < icon->labelLines; count ++)
	{
		if (kernelFontGetPrintedWidth(font, charSet, icon->labelLine[count]) >
			MAX_LABEL_WIDTH)
		{
			splitLongLabel(icon, count, font, charSet);
		}
	}
}


static void setLabel(kernelWindowIcon *icon, const char *label,
	kernelFont *font, char *charSet)
{
	// Given a string, try and fit it into our maximum number of label lines
	// with each having a maximum width.  For a long icon label, try to split
	// it up in a sensible and pleasing way.

	int labelLen = 0;
	int tmpWidth = 0;
	int count;

	labelLen = min(strlen(label), WINDOW_MAX_LABEL_LENGTH);

	// By default just copy the label into a single line.
	icon->labelLine[0] = (char *) icon->labelData;
	strncpy(icon->labelLine[0], label, labelLen);
	icon->labelLine[0][labelLen] = '\0';
	icon->labelLines = 1;

	// If there are any newlines, split the label
	splitLabelsAtNewlines(icon);

	// If the labels are too long, split them
	splitLongLabels(icon, font, charSet);

	// Set the label width
	icon->labelWidth = 0;
	for (count = 0; count < icon->labelLines; count ++)
	{
		tmpWidth = kernelFontGetPrintedWidth(font, charSet,
			icon->labelLine[count]);

		if (tmpWidth > icon->labelWidth)
			icon->labelWidth = tmpWidth;
	}
}


static int draw(kernelWindowComponent *component)
{
	// Draw the icon component

	kernelWindowIcon *icon = component->data;
	color *textColor = NULL;
	color *textBackground = NULL;
	kernelFont *font = (kernelFont *) component->params.font;
	int labelX = 0, labelY = 0;
	int count;

	kernelDebug(debug_gui, "WindowIcon draw");

	// Draw the icon image
	kernelGraphicDrawImage(component->buffer, (image *) &icon->iconImage,
		draw_alphablend, IMAGEX, component->yCoord, 0, 0, 0, 0);

	if (icon->selected)
	{
		textColor = (color *) &component->params.background;
		textBackground = (color *) &component->params.foreground;
	}
	else
	{
		textColor = (color *) &component->params.foreground;
		textBackground = (color *) &component->params.background;
	}

	for (count = 0; count < icon->labelLines; count ++)
	{
		if (font)
		{
			labelX = (component->xCoord + ((component->width -
				kernelFontGetPrintedWidth(font, (char *) component->charSet,
				(char *) icon->labelLine[count])) / 2) + 1);
			labelY = (component->yCoord + icon->iconImage.height + 4 +
				(font->glyphHeight * count));

			kernelGraphicDrawText(component->buffer, textBackground,
				textColor, font, (char *) component->charSet,
				icon->labelLine[count], draw_translucent, (labelX + 1),
				(labelY + 1));
			kernelGraphicDrawText(component->buffer, textColor,
				textBackground, font, (char *) component->charSet,
				icon->labelLine[count], draw_translucent, labelX, labelY);
		}
	}

	if (component->params.flags & WINDOW_COMPFLAG_HASBORDER)
		component->drawBorder(component, 1);

	return (0);
}


static int focus(kernelWindowComponent *component, int gotFocus)
{
	kernelWindowIcon *icon = component->data;

	kernelDebug(debug_gui, "WindowIcon %s focus", (gotFocus? "got" : "lost"));

	if (gotFocus)
	{
		kernelGraphicDrawImage(component->buffer, (image *)
			&icon->selectedImage, draw_alphablend, IMAGEX, component->yCoord,
			0, 0, 0, 0);
		component->window->update(component->window, component->xCoord,
			component->yCoord, component->width, component->height);
	}
	else if (component->window->drawClip)
	{
		component->window->drawClip(component->window, component->xCoord,
			component->yCoord, component->width, component->height);
	}

	return (0);
}


static int setData(kernelWindowComponent *component, void *label,
	int length __attribute__((unused)))
{
	// Set the icon label

	kernelWindowIcon *icon = component->data;

	kernelDebug(debug_gui, "WindowIcon set data");

	if (component->params.font)
	{
		setLabel(icon, label, (kernelFont *) component->params.font,
			(char *) component->charSet);
	}

	// Re-draw
	if (component->draw)
		draw(component);

	component->window->update(component->window, component->xCoord,
		component->yCoord, component->width, component->height);

	return (0);
}


static int mouseEvent(kernelWindowComponent *component, windowEvent *event)
{
	kernelWindowIcon *icon = component->data;
	static int dragging = 0;
	static windowEvent dragEvent;

	kernelDebug(debug_gui, "WindowIcon mouse event");

	// Is the icon being dragged around?
	if (dragging)
	{
		if (event->type == EVENT_MOUSE_DRAG)
		{
			// The icon is still moving

			// Erase the moving image
			kernelWindowRedrawArea((component->window->xCoord +
				component->xCoord), (component->window->yCoord +
				component->yCoord), component->width, component->height);

			// Set the new position
			component->xCoord += (event->xPosition - dragEvent.xPosition);
			component->yCoord += (event->yPosition - dragEvent.yPosition);

			// Draw the moving image.
			kernelGraphicDrawImage(NULL, (image *) &icon->selectedImage,
				draw_alphablend, (component->window->xCoord + IMAGEX),
				(component->window->yCoord + component->yCoord),
				0, 0, 0, 0);

			// Save a copy of the dragging event
			memcpy(&dragEvent, event, sizeof(windowEvent));
		}
		else
		{
			// The move is finished

			component->flags |= WINFLAG_VISIBLE;

			// Erase the moving image
			kernelWindowRedrawArea((component->window->xCoord +
				component->xCoord), (component->window->yCoord +
				component->yCoord), component->width, component->height);

			icon->selected = 0;

			// Re-render it at the new location
			if (component->draw)
				component->draw(component);

			// If we've moved the icon outside the parent container, expand
			// the container to contain it.

			if ((component->xCoord + component->width) >=
				(component->container->xCoord + component->container->width))
			{
				component->container->width = ((component->xCoord -
					component->container->xCoord) + component->width + 1);
			}

			if ((component->yCoord + component->height) >=
				(component->container->yCoord + component->container->height))
			{
				component->container->height = ((component->yCoord -
					component->container->yCoord) + component->height + 1);
			}

			component->window->update(component->window, component->xCoord,
				component->yCoord, component->width, component->height);

			// If the new location intersects any other components of the
			// window, we need to focus the icon
			kernelWindowComponentFocus(component);

			dragging = 0;
		}

		// Redraw the mouse
		kernelMouseDraw();
	}

	else if ((event->type == EVENT_MOUSE_DRAG) &&
		(component->params.flags & WINDOW_COMPFLAG_CANDRAG))
	{
		// The icon has started moving

		// Don't show it while it's moving
		component->flags &= ~WINFLAG_VISIBLE;

		if (component->window->drawClip)
			component->window->drawClip(component->window, component->xCoord,
				component->yCoord, component->width, component->height);

		// Draw the moving image.
		kernelGraphicDrawImage(NULL, (image *) &icon->selectedImage,
			draw_alphablend, (component->window->xCoord + IMAGEX),
			(component->window->yCoord + component->yCoord), 0, 0, 0, 0);

		// Save a copy of the dragging event
		memcpy(&dragEvent, event, sizeof(windowEvent));
		dragging = 1;
	}

	else if ((event->type == EVENT_MOUSE_LEFTDOWN) ||
		(event->type == EVENT_MOUSE_LEFTUP))
	{
		// Just a click

		if (event->type == EVENT_MOUSE_LEFTDOWN)
		{
			kernelDebug(debug_gui, "WindowIcon mouse click");

			kernelGraphicDrawImage(component->buffer,
				(image *) &icon->selectedImage, draw_alphablend, IMAGEX,
				component->yCoord, 0, 0, 0, 0);

			icon->selected = 1;
		}

		else if (event->type == EVENT_MOUSE_LEFTUP)
		{
			kernelDebug(debug_gui, "WindowIcon mouse unclick");

			icon->selected = 0;

			// Remove the focus from the icon.  This will cause it to be
			// redrawn in its default way.
			if (component->window->changeComponentFocus)
			{
				component->window->changeComponentFocus(component->window,
					NULL);
			}
		}

		component->window->update(component->window, IMAGEX,
			component->yCoord, icon->iconImage.width, icon->iconImage.height);
	}

	return (0);
}


static int keyEvent(kernelWindowComponent *component, windowEvent *event)
{
	int status = 0;

	kernelDebug(debug_gui, "WindowIcon key event");

	// We're only looking for 'enter' key releases, which we turn into mouse
	// button presses.
	if ((event->type & EVENT_MASK_KEY) && (event->key == keyEnter))
	{
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
	kernelWindowIcon *icon = component->data;

	// Release all our memory
	if (icon)
	{
		kernelImageFree((image *) &icon->iconImage);
		kernelImageFree((image *) &icon->selectedImage);

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

kernelWindowComponent *kernelWindowNewIcon(objectKey parent, image *origImage,
	const char *label, componentParameters *params)
{
	// Formats a kernelWindowComponent as a kernelWindowIcon

	kernelWindowComponent *component = NULL;
	kernelWindowIcon *icon = NULL;
	pixel *pix = NULL;
	unsigned count;

	// Check params.  Label can be NULL.
	if (!parent || !origImage || !params)
	{
		kernelError(kernel_error, "NULL parameter");
		return (component = NULL);
	}

	if (!origImage->data)
	{
		kernelError(kernel_error, "Image data is NULL");
		return (component = NULL);
	}

	// Get the basic component structure
	component = kernelWindowComponentNew(parent, params);
	if (!component)
		return (component);

	component->type = iconComponentType;

	// Set the functions
	component->draw = &draw;
	component->focus = &focus;
	component->setData = &setData;
	component->mouseEvent = &mouseEvent;
	component->keyEvent = &keyEvent;
	component->destroy = &destroy;

	// If default colors are requested, override the standard component colors
	// with the ones we prefer

	if (!(component->params.flags & WINDOW_COMPFLAG_CUSTOMFOREGROUND))
	{
		// Use default foreground color
		memcpy((void *) &component->params.foreground,
			&windowVariables->color.foreground, sizeof(color));
	}

	if (!(component->params.flags & WINDOW_COMPFLAG_CUSTOMBACKGROUND))
	{
		memcpy((color *) &component->params.background, &COLOR_WHITE,
			sizeof(color));
	}

	// Always use our font
	component->params.font = windowVariables->font.varWidth.small.font;

	// Copy all the relevant data into our memory
	icon = kernelMalloc(sizeof(kernelWindowIcon));
	if (!icon)
	{
		kernelWindowComponentDestroy(component);
		return (component = NULL);
	}

	component->data = (void *) icon;

	// Copy the image to kernel memory
	if (kernelImageCopyToKernel(origImage, (image *) &icon->iconImage) < 0)
	{
		kernelWindowComponentDestroy(component);
		return (component = NULL);
	}

	// Icons use pure green as the transparency color
	icon->iconImage.transColor.blue = 0;
	icon->iconImage.transColor.green = 255;
	icon->iconImage.transColor.red = 0;

	// When the icon is selected, we do a little effect that makes the image
	// appear yellowish.
	if (kernelImageCopyToKernel(origImage, (image *)
		&icon->selectedImage) < 0)
	{
		kernelWindowComponentDestroy(component);
		return (component = NULL);
	}

	// Icons use pure green as the transparency color
	icon->selectedImage.transColor.blue = 0;
	icon->selectedImage.transColor.green = 255;
	icon->selectedImage.transColor.red = 0;

	for (count = 0; count < icon->selectedImage.pixels; count ++)
	{
		pix = &((pixel *) icon->selectedImage.data)[count];

		if (!PIXELS_EQ(pix, &icon->selectedImage.transColor))
		{
			pix->red = ((pix->red + 255) / 2);
			pix->green = ((pix->green + 255) / 2);
			pix->blue /= 2;
		}
	}

	if (label && component->params.font)
	{
		setLabel(icon, label, (kernelFont *) component->params.font,
			(char *) component->charSet);
	}

	// Now populate the main component

	component->width = max(origImage->width,
		((unsigned)(icon->labelWidth + 3)));
	component->height = (origImage->height + 5);

	if (component->params.font)
	{
		component->height += (((kernelFont *)
			component->params.font)->glyphHeight * icon->labelLines);
	}

	component->minWidth = component->width;
	component->minHeight = component->height;

	return (component);
}

