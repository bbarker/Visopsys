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
//  imgedit.c
//

// This is a simple image editing program.

/* This is the text that appears when a user requests help about this program
<help>

 -- imgedit --

Simple image editor

Usage:
  imgedit [options] [file]

Options:
-s  : Save as same file name (don't prompt)

</help>
*/

#include <libintl.h>
#include <locale.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/api.h>
#include <sys/env.h>
#include <sys/image.h>
#include <sys/paths.h>
#include <sys/window.h>

#define BUTTONIMAGE_SIZE		24

#define _(string) gettext(string)

static image img;
static char *saveFileName = NULL;
static int saved = 0;
static objectKey window = NULL;
static windowPixelEditor *editor = NULL;
static objectKey scrollHoriz = NULL;
static objectKey scrollVert = NULL;
static image saveImage;
static objectKey saveButton = NULL;
static image zoomInImage;
static objectKey zoomInButton = NULL;
static image zoomOutImage;
static objectKey zoomOutButton = NULL;
static image colorImage;
static objectKey colorButton = NULL;
static image pickImage;
static objectKey pickButton = NULL;
static image drawImage;
static objectKey drawButton = NULL;
static image lineImage;
static objectKey lineButton = NULL;
static image rectImage;
static objectKey rectButton = NULL;
static image ovalImage;
static objectKey ovalButton = NULL;
static image thickImage;
static objectKey thickButton = NULL;
static image fillImage;
static objectKey fillButton = NULL;


__attribute__((format(printf, 1, 2)))
static void error(const char *format, ...)
{
	// Generic error message code

	va_list list;
	char output[MAXSTRINGLENGTH];

	va_start(list, format);
	vsnprintf(output, MAXSTRINGLENGTH, format, list);
	va_end(list);

	windowNewErrorDialog(window, _("Error"), output);
}


static int askDiscardChanges(void)
{
	int response = 0;

	response = windowNewChoiceDialog(window, _("Discard changes?"),
		_("File has been modified.  Discard changes?"),
		(char *[]){ _("Discard"), _("Cancel") }, 2, 1);

	if (!response)
		return (1);
	else
		return (0);
}


static int quit(void)
{
	if (!editor->changed || askDiscardChanges())
		return (1);
	else
		return (0);
}


static int saveFile(void)
{
	int status = 0;

	if (!saveFileName)
	{
		saveFileName = malloc(MAX_PATH_NAME_LENGTH);
		if (!saveFileName)
			return (status = ERR_MEMORY);
	}

	if (!saved)
	{
		// Prompt for a file name
		status = windowNewFileDialog(window, _("Enter filename"),
			_("Please enter the name of the file for saving:"), NULL,
			saveFileName, MAX_PATH_NAME_LENGTH, fileT,
			1 /* show thumbnails */);
		if (status < 0)
			return (status);

		if (status != 1)
			return (status = ERR_CANCELLED);
	}

	// At the moment, we only support bitmap format for saving
	status = imageSave(saveFileName, IMAGEFORMAT_BMP, &img);
	if (status < 0)
	{
		error(_("Error %d saving file"), status);
	}
	else
	{
		saved = 1;
		editor->changed = 0;
	}

	return (status);
}


static void createColorImage(int width, int height)
{
	// Create images for the color button

	graphicBuffer buffer;
	color greenColor;
	color tmpColor;

	memset((void *) &buffer, 0, sizeof(graphicBuffer));

	memset(&greenColor, 0, sizeof(color));
	greenColor.green = 0xFF;

	// Get a buffer to draw our button graphic
	buffer.width = width;
	buffer.height = height;
	buffer.data = malloc(graphicCalculateAreaBytes(width, height));
	if (!buffer.data)
		return;

	// Fill with transparency green color
	graphicClearArea(&buffer, &greenColor, 0, 0, width, height);

	memcpy(&tmpColor, &editor->drawing.background, sizeof(color));
	if (!memcmp(&tmpColor, &greenColor, sizeof(color)))
		tmpColor.green -= 1;

	graphicDrawRect(&buffer, &tmpColor, draw_normal,
		(width / 3), (height / 3), ((width * 2) / 3), ((height * 2) / 3),
		1 /* thickness */, 1 /* fill */);
	graphicDrawRect(&buffer, &COLOR_DARKGRAY, draw_normal,
		(width / 3), (height / 3), ((width * 2) / 3), ((height * 2) / 3),
		1 /* thickness */, 0 /* no fill */);

	memcpy(&tmpColor, &editor->drawing.foreground, sizeof(color));
	if (!memcmp(&tmpColor, &greenColor, sizeof(color)))
		tmpColor.green -= 1;

	graphicDrawRect(&buffer, &tmpColor, draw_normal, 0, 0,
		((width * 2) / 3), ((height * 2) / 3), 1 /* thickness */,
		1 /* fill */);
	graphicDrawRect(&buffer, &COLOR_DARKGRAY, draw_normal, 0, 0,
		((width * 2) / 3), ((height * 2) / 3), 1 /* thickness */,
		0 /* no fill */);

	if (colorImage.data)
		imageFree(&colorImage);

	graphicGetImage(&buffer, &colorImage, 0, 0, width, height);

	free(buffer.data);
}


static void createThickFillImages(int width, int height)
{
	// Create images for thickness/fill buttons

	graphicBuffer buffer;
	color greenColor;
	int thickness = min(height, editor->drawing.thickness);
	int diameter = ((min(width, height) * 2) / 3);
	int count;

	memset((void *) &buffer, 0, sizeof(graphicBuffer));

	memset(&greenColor, 0, sizeof(color));
	greenColor.green = 0xFF;

	// Get a buffer to draw our button graphics
	buffer.width = width;
	buffer.height = height;
	buffer.data = malloc(graphicCalculateAreaBytes(width, height));
	if (!buffer.data)
		return;

	// Do the 'thickness' image
	graphicClearArea(&buffer, &greenColor, 0, 0, width, height);
	for (count = ((height - thickness) / 2); thickness > 0;
		count ++, thickness --)
	{
		graphicDrawLine(&buffer, &COLOR_DARKGRAY, draw_normal, 0,
			count, (width - 1), count);
	}
	graphicGetImage(&buffer, &thickImage, 0, 0, width, height);

	// Do the 'fill' image
	graphicClearArea(&buffer, &greenColor, 0, 0, width, height);
	graphicDrawRect(&buffer, &COLOR_DARKGRAY, draw_normal, 0, 0,
		((width * 2) / 3), ((height * 2) / 3), 1 /* thickness */,
		editor->drawing.fill);
	graphicDrawOval(&buffer, &COLOR_DARKGRAY, draw_normal, (width / 3),
		(height / 3), diameter, diameter, 1 /* thickness */,
		editor->drawing.fill);
	graphicGetImage(&buffer, &fillImage, 0, 0, width, height);

	free(buffer.data);
}


static void enableButtons(void)
{
	windowComponentSetEnabled(zoomInButton,
		(editor->pixelSize < editor->maxPixelSize));
	windowComponentSetEnabled(zoomOutButton,
		(editor->pixelSize > editor->minPixelSize));
	windowComponentSetEnabled(pickButton, (editor->mode != pixedmode_pick));
	windowComponentSetEnabled(drawButton, ((editor->mode != pixedmode_draw) ||
		(editor->drawing.operation != draw_pixel)));
	windowComponentSetEnabled(lineButton, ((editor->mode != pixedmode_draw) ||
		(editor->drawing.operation != draw_line)));
	windowComponentSetEnabled(rectButton, ((editor->mode != pixedmode_draw) ||
		(editor->drawing.operation != draw_rect)));
	windowComponentSetEnabled(ovalButton, ((editor->mode != pixedmode_draw) ||
		(editor->drawing.operation != draw_oval)));
	windowComponentSetEnabled(thickButton, ((editor->mode != pixedmode_draw) ||
		(editor->drawing.operation == draw_rect) ||
		(editor->drawing.operation == draw_oval)));
	windowComponentSetEnabled(fillButton, ((editor->mode != pixedmode_draw) ||
		(editor->drawing.operation == draw_rect) ||
		(editor->drawing.operation == draw_oval)));
}


static void eventHandler(objectKey key, windowEvent *event)
{
	scrollBarState horiz;
	scrollBarState vert;

	// Check for editor events.

	if ((key == editor->canvas) && (event->type & EVENT_MOUSE_SCROLL))
	{
		windowComponentGetData(scrollVert, &vert, sizeof(scrollBarState));

		if (event->type == EVENT_MOUSE_SCROLLUP)
		{
			vert.positionPercent = ((vert.positionPercent > 5)?
				(vert.positionPercent - 5) : 0);
		}
		else if (event->type == EVENT_MOUSE_SCROLLDOWN)
		{
			vert.positionPercent = ((vert.positionPercent < 95)?
				(vert.positionPercent + 5) : 100);
		}

		windowComponentSetData(scrollVert, &vert, sizeof(scrollBarState),
			1 /* redraw */);

		if (vert.positionPercent != editor->vert.positionPercent)
			editor->scrollVert(editor, vert.positionPercent);
	}

	else if (key == editor->canvas)
	{
		editor->eventHandler(editor, event);

		if ((editor->mode == pixedmode_pick) &&
			(event->type & (EVENT_MOUSE_LEFTDOWN | EVENT_MOUSE_DRAG)))
		{
			createColorImage(BUTTONIMAGE_SIZE, BUTTONIMAGE_SIZE);
			windowComponentSetData(colorButton, &colorImage, sizeof(image),
				1 /* redraw */);
		}

		// Don't want to slow down free drawing with all our button-enabling,
		// etc., so bail here.
		return;
	}

	// Check for window events.
	else if (key == window)
	{
		// Check for the window being closed
		if (event->type == EVENT_WINDOW_CLOSE)
		{
			if (quit())
				windowGuiStop();
		}

		// If we get resized, pass the event on to the pixel editor widget
		else if (event->type == EVENT_WINDOW_RESIZE)
		{
			editor->resize(editor);

			windowComponentSetData(scrollHoriz, &editor->horiz,
				sizeof(scrollBarState), 1 /* redraw */);
			windowComponentSetData(scrollVert, &editor->vert,
				sizeof(scrollBarState), 1 /* redraw */);
		}
	}

	// Horizontal scroll bar
	else if (key == scrollHoriz)
	{
		windowComponentGetData(scrollHoriz, &horiz, sizeof(scrollBarState));
		if (horiz.positionPercent != editor->horiz.positionPercent)
			editor->scrollHoriz(editor, horiz.positionPercent);
	}

	// Vertical scroll bar
	else if (key == scrollVert)
	{
		windowComponentGetData(scrollVert, &vert, sizeof(scrollBarState));
		if (vert.positionPercent != editor->vert.positionPercent)
			editor->scrollVert(editor, vert.positionPercent);
	}

	// Save button
	else if (key == saveButton)
	{
		if (event->type & EVENT_MOUSE_LEFTUP)
			saveFile();
	}

	// Zoom buttons
	else if ((key == zoomInButton) || (key == zoomOutButton))
	{
		if (event->type & EVENT_MOUSE_LEFTUP)
		{
			if (key == zoomInButton)
				editor->zoom(editor, 1);
			else
				editor->zoom(editor, -1);

			windowComponentSetData(scrollHoriz, &editor->horiz,
				sizeof(scrollBarState), 1 /* redraw */);
			windowComponentSetData(scrollVert, &editor->vert,
				sizeof(scrollBarState), 1 /* redraw */);
		}
	}

	// Color chooser button
	else if (key == colorButton)
	{
		if (event->type & EVENT_MOUSE_LEFTUP)
		{
			windowNewColorDialog(window, &editor->drawing.foreground);
			createColorImage(BUTTONIMAGE_SIZE, BUTTONIMAGE_SIZE);
			windowComponentSetData(colorButton, &colorImage, sizeof(image),
				1 /* redraw */);
		}
	}

	// Color picker button
	else if (key == pickButton)
	{
		if (event->type & EVENT_MOUSE_LEFTUP)
			editor->mode = pixedmode_pick;
	}

	// Free drawing button
	else if (key == drawButton)
	{
		if (event->type & EVENT_MOUSE_LEFTUP)
		{
			editor->mode = pixedmode_draw;
			editor->drawing.operation = draw_pixel;
		}
	}

	// Line drawing button
	else if (key == lineButton)
	{
		if (event->type & EVENT_MOUSE_LEFTUP)
		{
			editor->mode = pixedmode_draw;
			editor->drawing.operation = draw_line;
		}
	}

	// Rectangle drawing button
	else if (key == rectButton)
	{
		if (event->type & EVENT_MOUSE_LEFTUP)
		{
			editor->mode = pixedmode_draw;
			editor->drawing.operation = draw_rect;
		}
	}

	// Oval drawing button
	else if (key == ovalButton)
	{
		if (event->type & EVENT_MOUSE_LEFTUP)
		{
			editor->mode = pixedmode_draw;
			editor->drawing.operation = draw_oval;
		}
	}

	// Thickness button
	else if (key == thickButton)
	{
		if (event->type & EVENT_MOUSE_LEFTUP)
		{
			windowNewNumberDialog(window, _("Thickness"),
				_("Enter line thickess"), 1, editor->img->height,
				editor->drawing.thickness, &editor->drawing.thickness);
			createThickFillImages(BUTTONIMAGE_SIZE, BUTTONIMAGE_SIZE);
			windowComponentSetData(thickButton, &thickImage, sizeof(image),
				1 /* redraw */);
		}
	}

	// Fill button
	else if (key == fillButton)
	{
		if (event->type & EVENT_MOUSE_LEFTUP)
		{
			editor->drawing.fill ^= 1;
			createThickFillImages(BUTTONIMAGE_SIZE, BUTTONIMAGE_SIZE);
			windowComponentSetData(fillButton, &fillImage, sizeof(image),
				1 /* redraw */);
		}
	}

	enableButtons();
}


static void createDrawImages(int width, int height)
{
	// Create images for drawing buttons

	graphicBuffer buffer;
	color greenColor;
	int diameter = min(width, height);

	memset((void *) &buffer, 0, sizeof(graphicBuffer));

	memset(&greenColor, 0, sizeof(color));
	greenColor.green = 0xFF;

	// Get a buffer to draw our button graphics
	buffer.width = width;
	buffer.height = height;
	buffer.data = malloc(graphicCalculateAreaBytes(width, height));
	if (!buffer.data)
		return;

	// Do the 'line' image
	graphicClearArea(&buffer, &greenColor, 0, 0, width, height);
	graphicDrawLine(&buffer, &COLOR_DARKGRAY, draw_normal, 0, (height - 1),
		(width - 1), 0);
	graphicGetImage(&buffer, &lineImage, 0, 0, width, height);

	// Do the 'rect' image
	graphicClearArea(&buffer, &greenColor, 0, 0, width, height);
	graphicDrawRect(&buffer, &COLOR_DARKGRAY, draw_normal, 0, 0, width, height,
		1 /* thickness */, 0 /* no fill */);
	graphicGetImage(&buffer, &rectImage, 0, 0, width, height);

	// Do the 'oval' image
	graphicClearArea(&buffer, &greenColor, 0, 0, width, height);
	graphicDrawOval(&buffer, &COLOR_DARKGRAY, draw_normal,
	((width - diameter) / 2), ((height - diameter) / 2),
	(diameter - 1), (diameter - 1), 1 /* thickness */, 0 /* no fill */);
	graphicGetImage(&buffer, &ovalImage, 0, 0, width, height);

	free(buffer.data);
}


static int constructWindow(void)
{
	int status = 0;
	objectKey buttonContainer = NULL;
	componentParameters params;

	// Create a new window
	window = windowNew(multitaskerGetCurrentProcessId(), _("Image Editor"));
	if (!window)
		return (status = ERR_NOCREATE);

	// Register an event handler to catch window close events
	windowRegisterEventHandler(window, &eventHandler);

	memset(&params, 0, sizeof(componentParameters));
	params.gridWidth = 1;
	params.gridHeight = 1;
	params.padTop = params.padLeft = 5;
	params.orientationX = orient_left;
	params.orientationY = orient_top;

	// Create the pixel editor widget.
	editor = windowNewPixelEditor(window,
		((graphicGetScreenHeight() * 2) / 3),
		((graphicGetScreenHeight() * 2) / 3), &img, &params);
	if (!editor)
	{
		status = ERR_NOCREATE;
		goto out;
	}

	windowRegisterEventHandler(editor->canvas, &eventHandler);

	// A horizontal scroll bar
	params.gridY += 1;
	params.padTop = 0;
	params.padBottom = 5;
	params.flags = WINDOW_COMPFLAG_FIXEDHEIGHT;
	scrollHoriz = windowNewScrollBar(window, scrollbar_horizontal, 0, 0,
		&params);
	if (!scrollHoriz)
	{
		status = ERR_NOCREATE;
		goto out;
	}

	windowComponentSetData(scrollHoriz, &editor->horiz,
		sizeof(scrollBarState), 1 /* redraw */);
	windowRegisterEventHandler(scrollHoriz, &eventHandler);

	// A vertical scroll bar
	params.gridY = 0;
	params.gridX += 1;
	params.padLeft = params.padBottom = 0;
	params.padTop = 5;
	params.flags = WINDOW_COMPFLAG_FIXEDWIDTH;
	scrollVert = windowNewScrollBar(window, scrollbar_vertical, 0, 0,
		&params);
	if (!scrollVert)
	{
		status = ERR_NOCREATE;
		goto out;
	}

	windowComponentSetData(scrollVert, &editor->vert, sizeof(scrollBarState),
		1 /* redraw */);
	windowRegisterEventHandler(scrollVert, &eventHandler);

	// Make a container for the buttons
	params.gridX += 1;
	params.padLeft = params.padRight = 5;
	params.gridHeight = 2;
	params.flags = (WINDOW_COMPFLAG_FIXEDWIDTH | WINDOW_COMPFLAG_FIXEDHEIGHT);
	buttonContainer = windowNewContainer(window, "buttonContainer", &params);
	if (!buttonContainer)
	{
		status = ERR_NOCREATE;
		goto out;
	}

	// Create a save button
	params.gridX = 0;
	params.padLeft = params.padRight = params.padTop = params.padBottom = 0;
	params.gridHeight = 1;
	params.flags = 0;
	imageLoad(PATH_SYSTEM_ICONS "/save.ico" , BUTTONIMAGE_SIZE,
		BUTTONIMAGE_SIZE, &saveImage);
	saveButton = windowNewButton(buttonContainer,
		(saveImage.data? NULL : _("Save")),
		(saveImage.data? &saveImage : NULL), &params);
	if (!saveButton)
	{
		status = ERR_NOCREATE;
		goto out;
	}

	windowRegisterEventHandler(saveButton, &eventHandler);

	// Create a 'zoom in' button
	params.gridY += 1;
	params.padTop = 5;
	imageLoad(PATH_SYSTEM_ICONS "/zoomin.ico", BUTTONIMAGE_SIZE,
		BUTTONIMAGE_SIZE, &zoomInImage);
	zoomInButton = windowNewButton(buttonContainer,
		(zoomInImage.data? NULL : "+"),
		(zoomInImage.data? &zoomInImage : NULL), &params);
	if (!zoomInButton)
	{
		status = ERR_NOCREATE;
		goto out;
	}

	windowRegisterEventHandler(zoomInButton, &eventHandler);

	// Create a 'zoom out' button
	params.gridY += 1;
	imageLoad(PATH_SYSTEM_ICONS "/zoomout.ico", BUTTONIMAGE_SIZE,
		BUTTONIMAGE_SIZE, &zoomOutImage);
	zoomOutButton = windowNewButton(buttonContainer,
		(zoomOutImage.data? NULL : "-"),
		(zoomOutImage.data? &zoomOutImage : NULL), &params);
	if (!zoomOutButton)
	{
		status = ERR_NOCREATE;
		goto out;
	}

	windowRegisterEventHandler(zoomOutButton, &eventHandler);

	// Create a color chooser button

	createColorImage(BUTTONIMAGE_SIZE, BUTTONIMAGE_SIZE);

	params.gridY += 1;
	colorButton = windowNewButton(buttonContainer,
		(colorImage.data? NULL : _("Color")),
		(colorImage.data? &colorImage : NULL), &params);
	if (!colorButton)
	{
		status = ERR_NOCREATE;
		goto out;
	}

	windowRegisterEventHandler(colorButton, &eventHandler);

	// Create a color picker button
	params.gridY += 1;
	imageLoad(PATH_SYSTEM_ICONS "/colrpick.ico", BUTTONIMAGE_SIZE,
		BUTTONIMAGE_SIZE, &pickImage);
	pickButton = windowNewButton(buttonContainer,
		(pickImage.data? NULL : _("Pick")),
		(pickImage.data? &pickImage : NULL), &params);
	if (!pickButton)
	{
		status = ERR_NOCREATE;
		goto out;
	}

	windowRegisterEventHandler(pickButton, &eventHandler);

	createDrawImages(BUTTONIMAGE_SIZE, BUTTONIMAGE_SIZE);

	// Create a free drawing button
	params.gridY += 1;
	imageLoad(PATH_SYSTEM_ICONS "/draw.ico", BUTTONIMAGE_SIZE,
		BUTTONIMAGE_SIZE, &drawImage);
	drawButton = windowNewButton(buttonContainer,
		(drawImage.data? NULL : _("Draw")),
		(drawImage.data? &drawImage : NULL), &params);
	if (!drawButton)
	{
		status = ERR_NOCREATE;
		goto out;
	}

	windowRegisterEventHandler(drawButton, &eventHandler);

	// Create a line drawing button
	params.gridY += 1;
	lineButton = windowNewButton(buttonContainer,
		(lineImage.data? NULL : _("Line")),
		(lineImage.data? &lineImage : NULL), &params);
	if (!lineButton)
	{
		status = ERR_NOCREATE;
		goto out;
	}

	windowRegisterEventHandler(lineButton, &eventHandler);

	// Create a rectangle drawing button
	params.gridY += 1;
	rectButton = windowNewButton(buttonContainer,
		(rectImage.data? NULL : _("Rect")),
		(rectImage.data? &rectImage : NULL), &params);
	if (!rectButton)
	{
		status = ERR_NOCREATE;
		goto out;
	}

	windowRegisterEventHandler(rectButton, &eventHandler);

	// Create an oval drawing button
	params.gridY += 1;
	ovalButton = windowNewButton(buttonContainer,
		(ovalImage.data? NULL : _("Oval")),
		(ovalImage.data? &ovalImage : NULL), &params);
	if (!ovalButton)
	{
		status = ERR_NOCREATE;
		goto out;
	}

	windowRegisterEventHandler(ovalButton, &eventHandler);

	params.gridY += 1;
	params.padTop = 10;
	windowNewDivider(buttonContainer, divider_horizontal, &params);

	createThickFillImages(BUTTONIMAGE_SIZE, BUTTONIMAGE_SIZE);

	// Create a thickness button
	params.gridY += 1;
	thickButton = windowNewButton(buttonContainer,
		(thickImage.data? NULL : _("Thickness")),
		(thickImage.data? &thickImage : NULL), &params);
	if (!thickButton)
	{
		status = ERR_NOCREATE;
		goto out;
	}

	windowRegisterEventHandler(thickButton, &eventHandler);

	// Create a fill button
	params.gridY += 1;
	params.padTop = 5;
	fillButton = windowNewButton(buttonContainer,
		(fillImage.data? NULL : _("Fill")),
		(fillImage.data? &fillImage : NULL), &params);
	if (!fillButton)
	{
		status = ERR_NOCREATE;
		goto out;
	}

	windowRegisterEventHandler(fillButton, &eventHandler);

	enableButtons();

	windowSetVisible(window, 1);

	status = 0;

out:
	if (status < 0)
		windowDestroy(window);

	return (status);
}


int main(int argc, char *argv[])
{
	int status = 0;
	char opt;
	char fileName[MAX_PATH_NAME_LENGTH];

	setlocale(LC_ALL, getenv(ENV_LANG));
	textdomain("imgedit");

	// Only work in graphics mode
	if (!graphicsAreEnabled())
	{
		fprintf(stderr, _("\nThe \"%s\" command only works in graphics "
			"mode\n"), (argc? argv[0] : ""));
		return (status = ERR_NOTINITIALIZED);
	}

	// Check options
	while (strchr("s?", (opt = getopt(argc, argv, "s"))))
	{
		switch (opt)
		{
			case 's':
				// Don't prompt for a filename to save as
				saveFileName = malloc(MAX_PATH_NAME_LENGTH);
				if (saveFileName)
					saved = 1;
				break;

			default:
				error(_("Unknown option '%c'"), optopt);
				return (status = ERR_INVALID);
		}
	}

	// If an image file was not specified, ask for it
	if ((argc < 2) || (optind >= argc))
	{
		status = windowNewFileDialog(NULL, _("Enter filename"),
			_("Please enter an image file to edit:"), NULL, fileName,
			MAX_PATH_NAME_LENGTH, fileT, 1 /* show thumbnails */);
		if (status != 1)
		{
			if (status)
				perror(argv[0]);
			goto out;
		}
	}
	else
	{
		strncpy(fileName, argv[argc - 1], MAX_PATH_NAME_LENGTH);
	}

	if (saveFileName)
		strncpy(saveFileName, fileName, MAX_PATH_NAME_LENGTH);

	// Get the image to edit
	status = imageLoad(fileName, 0, 0, &img);
	if (status < 0)
		return (status);

	// Make our window
	status = constructWindow();
	if (status < 0)
		goto out;

	// Run the GUI
	windowGuiRun();

	// ...and when we come back...
	windowDestroy(window);

	status = 0;

out:
	if (saveFileName)
		free(saveFileName);

	if (editor)
		editor->destroy(editor);

	imageFree(&saveImage);
	imageFree(&zoomInImage);
	imageFree(&zoomOutImage);
	imageFree(&colorImage);
	imageFree(&pickImage);
	imageFree(&drawImage);
	imageFree(&lineImage);
	imageFree(&rectImage);
	imageFree(&ovalImage);
	imageFree(&thickImage);
	imageFree(&fillImage);
	imageFree(&img);

	return (status);
}

