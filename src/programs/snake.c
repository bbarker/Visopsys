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
//  snake.c
//

// This is the 'Snake' game

/* This is the text that appears when a user requests help about this program
<help>

 -- snake --

A 'Snake' game.

Usage:
  snake

</help>
*/

#include <libintl.h>
#include <locale.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/api.h>
#include <sys/ascii.h>
#include <sys/env.h>
#include <sys/paths.h>

#define _(string) gettext(string)

#define WINDOW_TITLE		_("Snake")
#define CHANGE_DIRECTION	_("Use cursor keys to change direction")
#define SCREENWIDTH			20
#define SCREENHEIGHT		9
#define SPEED				200
#define TREAT_MULTIPLE		5
#define TREAT_TIMER			20
#define TREAT_BASESCORE		30
#define NUM_IMAGES			16
#define SNAKE_DIR			PATH_PROGRAMS "/snake.dir/"

typedef enum { empty, snake, food, treat } objectType;
typedef enum { north = 0, south = 1, east = 2, west = 3 } direction;

typedef struct {
	int x;
	int y;
	direction dir;

} coord;

typedef enum {
	image_body_horiz = 0, image_body_vert = 1, image_corner_ne = 2,
	image_corner_nw = 3, image_corner_se = 4, image_corner_sw = 5,
	image_head_e = 6, image_head_n = 7, image_head_s = 8, image_head_w = 9,
	image_tail_e = 10, image_tail_n = 11, image_tail_s = 12, image_tail_w = 13,
	image_food = 14, image_treat = 15

} imageEnum;

static int graphics = 0;
static int screenWidth = SCREENWIDTH;
static int screenHeight = SCREENHEIGHT;
static int speed = SPEED;
static int score = 0;
static int run = 0;
static objectType *grid = NULL;
static coord *snakeArray = NULL;
static int snakeLength = 5;
static int snakeDirection = west;
static coord treatCoord;
static int treatTimer = 0;

// For graphics mode
static char *imageNames[NUM_IMAGES] = {
	SNAKE_DIR "body-horiz.bmp", SNAKE_DIR "body-vert.bmp",
	SNAKE_DIR "corner-ne.bmp", SNAKE_DIR "corner-nw.bmp",
	SNAKE_DIR "corner-se.bmp", SNAKE_DIR "corner-sw.bmp",
	SNAKE_DIR "head-e.bmp", SNAKE_DIR "head-n.bmp",
	SNAKE_DIR "head-s.bmp", SNAKE_DIR "head-w.bmp",
	SNAKE_DIR "tail-e.bmp", SNAKE_DIR "tail-n.bmp",
	SNAKE_DIR "tail-s.bmp", SNAKE_DIR "tail-w.bmp",
	SNAKE_DIR "food.bmp", SNAKE_DIR "treat.bmp"
};
static image images[NUM_IMAGES];
static unsigned imageWidth = 0;
static unsigned imageHeight = 0;
static objectKey window = NULL;
static objectKey scoreLabel = NULL;
static objectKey treatImage = NULL;
static objectKey treatLabel = NULL;
static objectKey canvas = NULL;
static objectKey changeDirLabel = NULL;


static inline objectType getGrid(coord *c)
{
	return (grid[(c->y * screenWidth) + c->x]);
}


static inline void setGrid(coord *c, objectType o)
{
	grid[(c->y * screenWidth) + c->x] = o;
}


static void getRandomEmpty(coord *c)
{
	unsigned random = 0;

	while (1)
	{
		// Pick a random empty space
		random = randomFormatted(0, ((screenWidth * screenHeight) - 1));

		c->x = (random % screenWidth);
		c->y = (random / screenWidth);

		if (getGrid(c) == empty)
			break;
	}
}


static void clearImage(coord *c)
{
	windowDrawParameters drawParams;

	memset(&drawParams, 0, sizeof(windowDrawParameters));
	drawParams.operation = draw_rect;
	drawParams.mode = draw_normal;
	drawParams.xCoord1 = (c->x * imageWidth);
	drawParams.yCoord1 = (c->y * imageHeight);
	drawParams.width = imageWidth;
	drawParams.height = imageHeight;
	drawParams.thickness = 1;
	drawParams.fill = 1;
	drawParams.foreground.red = 255;
	drawParams.foreground.green = 255;
	drawParams.foreground.blue = 255;
	windowComponentSetData(canvas, &drawParams, 1, 1 /* redraw */);
}


static void putImage(coord *c, imageEnum ie)
{
	windowDrawParameters drawParams;

	memset(&drawParams, 0, sizeof(windowDrawParameters));
	drawParams.operation = draw_image;
	drawParams.mode = draw_translucent;
	drawParams.xCoord1 = (c->x * imageWidth);
	drawParams.yCoord1 = (c->y * imageHeight);
	drawParams.data = &images[ie];
	windowComponentSetData(canvas, &drawParams, 1, 1 /* redraw */);
}


static void makeFood(void)
{
	coord c;

	// Pick a random empty space and put food in it.
	getRandomEmpty(&c);

	setGrid(&c, food);

	if (graphics)
		putImage(&c, image_food);
	else
	{
		textSetColumn(c.x + 1);
		textSetRow(c.y + 2);
		textPutc('o');
	}
}


static void makeTreat(void)
{
	// Pick a random empty space and put a treat in it.

	char tmpChar[8];

	getRandomEmpty(&treatCoord);

	setGrid(&treatCoord, treat);

	if (graphics)
		putImage(&treatCoord, image_treat);
	else
	{
		textSetColumn(treatCoord.x + 1);
		textSetRow(treatCoord.y + 2);
		textPutc('*');
	}

	treatTimer = TREAT_TIMER;

	sprintf(tmpChar, "%02d", treatTimer);
	if (graphics)
	{
		windowComponentSetVisible(treatImage, 1);
		windowComponentSetData(treatLabel, tmpChar, strlen(tmpChar),
			1 /* redraw */);
		windowComponentSetVisible(treatLabel, 1);
	}
	else
	{
		textSetColumn(screenWidth - 2);
		textSetRow(0);
		printf("* %s", tmpChar);
	}
}


static void updateTreat(void)
{
	char tmpChar[8];

	if (treatTimer > 0)
		treatTimer -= 1;

	if (!graphics)
	{
		textSetColumn(screenWidth - 2);
		textSetRow(0);
	}

	if (treatTimer > 0)
	{
		sprintf(tmpChar, "%02d", treatTimer);
		if (graphics)
		{
			windowComponentSetData(treatLabel, tmpChar, strlen(tmpChar),
				1 /* redraw */);
		}
		else
		{
			printf("* %s", tmpChar);
		}
	}
	else
	{
		if (graphics)
		{
			windowComponentSetVisible(treatImage, 0);
			windowComponentSetVisible(treatLabel, 0);
			windowComponentSetData(treatLabel, "    ", strlen(tmpChar),
				1 /* redraw */);
		}
		else
			printf("    ");
	}
}


static void setup(void)
{
	// Fill up the initial grid and snake coordinates

	int x = ((screenWidth - snakeLength) / 2);
	int y = (screenHeight / 2);
	int count;

	// Empty the grid
	for (count = 0; count < (screenWidth * screenHeight); count ++)
	grid[count] = empty;

	// Fill in the initial screen and snake coordinate arrays
	for (count = 0; count < snakeLength; count ++)
	{
		snakeArray[count].x = x;
		snakeArray[count].y = y;
		snakeArray[count].dir = west;
		grid[(y * screenWidth) + x] = snake;
		x += 1;
	}

	if (graphics)
	{
		windowComponentFocus(canvas);
		putImage(&snakeArray[0], image_head_w);
		for (count = 1; count < (snakeLength - 1); count ++)
			putImage(&snakeArray[count], image_body_horiz);
		putImage(&snakeArray[snakeLength - 1], image_tail_w);
	}
	else
	{
		for (count = 0; count < snakeLength; count ++)
		{
			textSetColumn(snakeArray[count].x + 1);
			textSetRow(snakeArray[count].y + 2);
			textPutc(205);
		}
	}

	return;
}


static int moveSnake(void)
{
	objectType object = empty;
	char c;
	int count;

	// Move the snake array along to make room for the new head.
	for (count = snakeLength; count > 0; count --)
	{
		snakeArray[count].x = snakeArray[count - 1].x;
		snakeArray[count].y = snakeArray[count - 1].y;
		snakeArray[count].dir = snakeArray[count - 1].dir;
	}

	snakeArray[0].dir = snakeArray[1].dir;

	if (!graphics)
	{
		// See if the direction should change
		if (textInputCount())
		{
			textInputGetc(&c);

			switch (c)
			{
				case ASCII_CRSRUP:
					// Cursor up
					if (snakeDirection != south)
						snakeDirection = north;
					break;

				case ASCII_CRSRDOWN:
					// Cursor down
					if (snakeDirection != north)
						snakeDirection = south;
					break;

				case ASCII_CRSRLEFT:
					// Cursor left
					if (snakeDirection != east)
						snakeDirection = west;
					break;

				case ASCII_CRSRRIGHT:
					// Cursor right
					if (snakeDirection != west)
						snakeDirection = east;
					break;

				default:
					break;
			}
		}
	}
	snakeArray[0].dir = snakeDirection;

	// In what direction is the snake moving?
	switch (snakeArray[0].dir)
	{
		case north:
			snakeArray[0].x = snakeArray[1].x;
			snakeArray[0].y = (snakeArray[1].y - 1);
			if (snakeArray[0].y < 0)
				snakeArray[0].y = (screenHeight - 1);
			break;

		case south:
			snakeArray[0].x = snakeArray[1].x;
			snakeArray[0].y = (snakeArray[1].y + 1);
			if (snakeArray[0].y >= screenHeight)
				snakeArray[0].y = 0;
			break;

		case west:
			snakeArray[0].x = (snakeArray[1].x - 1);
			snakeArray[0].y = snakeArray[1].y;
			if (snakeArray[0].x < 0)
				snakeArray[0].x = (screenWidth - 1);
			break;

		case east:
		default:
			snakeArray[0].x = (snakeArray[1].x + 1);
			snakeArray[0].y = snakeArray[1].y;
			if (snakeArray[0].x >= screenWidth)
				snakeArray[0].x = 0;
			break;
	}

	// What was in the grid space the snake is moving to?...
	object = getGrid(&snakeArray[0]);
	switch (object)
	{
		case empty:
		case treat:
			// The snake just moves.  Fill in the grid for the snake head
			setGrid(&snakeArray[0], snake);
			// Remove the last part of the snake from the grid.
			setGrid(&snakeArray[snakeLength], empty);
			// Erase the tail of the snake
			if (graphics)
			{
				clearImage(&snakeArray[snakeLength]);
				clearImage(&snakeArray[snakeLength - 1]);
				switch (snakeArray[snakeLength - 2].dir)
				{
					case north:
						putImage(&snakeArray[snakeLength - 1], image_tail_n);
						break;

					case south:
						putImage(&snakeArray[snakeLength - 1], image_tail_s);
						break;

					case east:
						putImage(&snakeArray[snakeLength - 1], image_tail_e);
						break;

					case west:
						putImage(&snakeArray[snakeLength - 1], image_tail_w);
						break;
				}
				clearImage(&snakeArray[0]);
			}
			else
			{
				textSetColumn(snakeArray[snakeLength].x + 1);
				textSetRow(snakeArray[snakeLength].y + 2);
				textPutc(' ');
			}
			if (object == treat)
			{
				score += (TREAT_BASESCORE + treatTimer);
				treatTimer = 0;
				updateTreat();
			}
			break;

		case food:
			// The snake ate some food.  Grow the length and make more food.
			setGrid(&snakeArray[0], snake);
			if (graphics)
				clearImage(&snakeArray[0]);
			snakeLength += 1;
			if (snakeLength >= (screenWidth * screenHeight))
				// The snake fills the grid.  Win.
				return (1);
			// Make another food.
			makeFood();
			if (!(snakeLength % TREAT_MULTIPLE))
				// Every TREAT_MULTIPLE foods, make a treat
				makeTreat();
			score += 4;
			break;

		default:
			// Dead
			return (-1);
	}

	// Draw the new head of the snake
	if (!graphics)
	{
		textSetColumn(snakeArray[0].x + 1);
		textSetRow(snakeArray[0].y + 2);
	}

	switch(snakeArray[0].dir)
	{
		case north:
			if (graphics)
				putImage(&snakeArray[0], image_head_n);
			else
				textPutc(186);
			break;

		case south:
			if (graphics)
				putImage(&snakeArray[0], image_head_s);
			else
				textPutc(186);
			break;

		case east:
			if (graphics)
				putImage(&snakeArray[0], image_head_e);
			else
				textPutc(205);
			break;

		case west:
		default:
			if (graphics)
				putImage(&snakeArray[0], image_head_w);
			else
				textPutc(205);
			break;
	}

	if (snakeArray[0].dir != snakeArray[1].dir)
	{
		if (graphics)
			clearImage(&snakeArray[1]);
		else
		{
			textSetColumn(snakeArray[1].x + 1);
			textSetRow(snakeArray[1].y + 2);
		}

		if (snakeArray[0].dir == north)
		{
			if (snakeArray[1].dir == east)
			{
				if (graphics)
					putImage(&snakeArray[1], image_corner_ne);
				else
					textPutc(188);
			}
			else if (snakeArray[1].dir == west)
			{
				if (graphics)
					putImage(&snakeArray[1], image_corner_nw);
				else
					textPutc(200);
			}
		}
		else if (snakeArray[0].dir == south)
		{
			if (snakeArray[1].dir == east)
			{
				if (graphics)
					putImage(&snakeArray[1], image_corner_se);
				else
					textPutc(187);
			}
			else if (snakeArray[1].dir == west)
			{
				if (graphics)
					putImage(&snakeArray[1], image_corner_sw);
				else
					textPutc(201);
			}
		}
		else if (snakeArray[0].dir == east)
		{
			if (snakeArray[1].dir == north)
			{
				if (graphics)
					putImage(&snakeArray[1], image_corner_sw);
				else
					textPutc(201);
			}
			else if (snakeArray[1].dir == south)
			{
				if (graphics)
					putImage(&snakeArray[1], image_corner_nw);
				else
					textPutc(200);
			}
		}
		else if (snakeArray[0].dir == west)
		{
			if (snakeArray[1].dir == north)
			{
				if (graphics)
					putImage(&snakeArray[1], image_corner_se);
				else
					textPutc(187);
			}
			else if (snakeArray[1].dir == south)
			{
				if (graphics)
					putImage(&snakeArray[1], image_corner_ne);
				else
					textPutc(188);
			}
		}
	}
	else
	{
		if (graphics)
		{
			clearImage(&snakeArray[1]);
			switch (snakeArray[1].dir)
			{
				case north:
				case south:
					putImage(&snakeArray[1], image_body_vert);
					break;

				case east:
				case west:
					putImage(&snakeArray[1], image_body_horiz);
					break;
			}
		}
	}

	return (0);
}


static void drawScreen(void)
{
	int count;

	textScreenClear();
	textSetRow(1);

	// Draw a border

	// Top row
	textPutc(218);
	for (count = 0; count < screenWidth; count ++)
	{
		textSetColumn(count + 1);
		textPutc(196);
	}
	textSetColumn(screenWidth + 1);
	textPutc(191);

	// Middle rows
	for (count = 0; count < screenHeight; count ++)
	{
		textSetRow(count + 2);
		textSetColumn(0);
		textPutc(179);
		textSetColumn(screenWidth + 1);
		textPutc(179);
	}

	// Bottom row
	textSetColumn(0);
	textSetRow(screenHeight + 2);
	textPutc(192);
	for (count = 0; count < screenWidth; count ++)
	{
		textSetColumn(count + 1);
		textPutc(196);
	}
	textSetColumn(screenWidth + 1);
	textPutc(217);
}


static void refreshWindow(void)
{
	// We got a 'window refresh' event (probably because of a language switch),
	// so we need to update things

	// Re-get the language setting
	setlocale(LC_ALL, getenv(ENV_LANG));
	textdomain("snake");

	// Re-get the character set
	if (getenv(ENV_CHARSET))
		windowSetCharSet(window, getenv(ENV_CHARSET));

	// Refresh the 'change direction' label
	windowComponentSetData(changeDirLabel, CHANGE_DIRECTION,
		strlen(CHANGE_DIRECTION), 1 /* redraw */);

	// Refresh the window title
	windowSetTitle(window, WINDOW_TITLE);
}


static void eventHandler(objectKey key, windowEvent *event)
{
	// Check for window events.
	if (key == window)
	{
		// Check for window refresh
		if (event->type == EVENT_WINDOW_REFRESH)
			refreshWindow();

		// Check for the window being closed
		else if (event->type == EVENT_WINDOW_CLOSE)
		{
			run = 0;
			windowGuiStop();
		}
	}

	else if ((key == canvas) && (event->type == EVENT_KEY_DOWN))
	{
		switch (event->key)
		{
			case keyUpArrow:
				// Cursor up
				if (snakeArray[0].dir != south)
					snakeDirection = north;
				break;

			case keyDownArrow:
				// Cursor down
				if (snakeArray[0].dir != north)
					snakeDirection = south;
				break;

			case keyLeftArrow:
				// Cursor left
				if (snakeArray[0].dir != east)
					snakeDirection = west;
				break;

			case keyRightArrow:
				// Cursor right
				if (snakeArray[0].dir != west)
					snakeDirection = east;
				break;

			default:
				break;
		}
	}
}


static int constructWindow(void)
{
	int status = 0;
	componentParameters params;
	windowDrawParameters drawParams;
	int count;

	// Try to load all the images
	for (count = 0; count < NUM_IMAGES; count ++)
	{
		status = imageLoad(imageNames[count], 0, 0, &images[count]);
		if (status < 0)
			return (status);

		images[count].transColor.green = 255;

		if (images[count].width > imageWidth)
			imageWidth = images[count].width;
		if (images[count].height > imageHeight)
			imageHeight = images[count].height;
	}

	// Create a new window
	window = windowNew(multitaskerGetCurrentProcessId(), WINDOW_TITLE);
	if (!window)
		return (status = ERR_NOTINITIALIZED);

	memset(&params, 0, sizeof(componentParameters));
	params.gridWidth = 1;
	params.gridHeight = 1;
	params.padLeft = 5;
	params.padRight = 5;
	params.padTop = 5;
	params.padBottom = 5;
	params.orientationX = orient_left;
	params.orientationY = orient_middle;
	params.flags |= WINDOW_COMPFLAG_FIXEDWIDTH;
	scoreLabel = windowNewTextLabel(window, "0000", &params);

	params.gridX += 1;
	params.orientationX = orient_right;
	params.flags &= ~WINDOW_COMPFLAG_FIXEDWIDTH;
	treatImage =
	windowNewImage(window, &images[image_treat], draw_translucent, &params);
	windowComponentSetVisible(treatImage, 0);

	params.gridX += 1;
	params.orientationX = orient_right;
	params.flags |= WINDOW_COMPFLAG_FIXEDWIDTH;
	treatLabel = windowNewTextLabel(window, "00", &params);
	windowComponentSetVisible(treatLabel, 0);

	params.gridX = 0;
	params.gridY += 1;
	params.gridWidth = 3;
	params.orientationX = orient_center;
	params.flags &= ~WINDOW_COMPFLAG_FIXEDWIDTH;
	params.flags |= (WINDOW_COMPFLAG_CUSTOMBACKGROUND |
		WINDOW_COMPFLAG_HASBORDER | WINDOW_COMPFLAG_CANFOCUS);
	params.background.red = 255;
	params.background.green = 255;
	params.background.blue = 255;
	canvas = windowNewCanvas(window, (screenWidth * imageWidth),
		(screenHeight * imageHeight), &params);
	windowRegisterEventHandler(canvas, &eventHandler);

	params.gridY += 1;
	params.orientationX = orient_left;
	params.flags &= ~(WINDOW_COMPFLAG_CUSTOMBACKGROUND |
		WINDOW_COMPFLAG_HASBORDER | WINDOW_COMPFLAG_CANFOCUS);
	changeDirLabel = windowNewTextLabel(window, CHANGE_DIRECTION, &params);

	// Register an event handler to catch window close events
	windowRegisterEventHandler(window, &eventHandler);

	windowSetResizable(window, 0);
	windowSetVisible(window, 1);

	// Clear the background of the canvas
	memset(&drawParams, 0, sizeof(windowDrawParameters));
	drawParams.operation = draw_rect;
	drawParams.mode = draw_normal;
	drawParams.width = (screenWidth * imageWidth);
	drawParams.height = (screenHeight * imageHeight);
	drawParams.thickness = 1;
	drawParams.fill = 1;
	drawParams.foreground.red = 255;
	drawParams.foreground.green = 255;
	drawParams.foreground.blue = 255;
	windowComponentSetData(canvas, &drawParams, 1, 1 /* redraw */);

	return (status = 0);
}


static int play(void)
{
	int status = 0;
	char tmpChar[8];

	score = 0;
	treatTimer = 0;

	setup();

	// Make the first food
	makeFood();

	run = 1;

	while (run)
	{
		// Sleep
		multitaskerWait(speed);

		if (treatTimer)
		{
			updateTreat();
			if (!treatTimer)
			{
				// The player didn't get the treat in time.
				setGrid(&treatCoord, empty);
				if (graphics)
					clearImage(&treatCoord);
				else
				{
					textSetColumn(treatCoord.x + 1);
					textSetRow(treatCoord.y + 2);
					textPutc(' ');
				}
			}
		}

		status = moveSnake();
		if (status)
			return (status);

		sprintf(tmpChar, "%04d", score);
		if (graphics)
		{
			windowComponentSetData(scoreLabel, tmpChar, strlen(tmpChar),
				1 /* redraw */);
		}
		else
		{
			textSetColumn(0);
			textSetRow(0);
			printf("%s", tmpChar);
		}
	}

	return (status = 0);
}


int main(int argc, char *argv[])
{
	int status = 0;
	char opt;
	textScreen screen;
	char tmpChar[80];
	int count;

	setlocale(LC_ALL, getenv(ENV_LANG));
	textdomain("snake");

	// Are graphics enabled?
	graphics = graphicsAreEnabled();

	// Check options
	while (strchr("T?", (opt = getopt(argc, argv, "T"))))
	{
		switch (opt)
		{
			case 'T':
				// Force text mode
				graphics = 0;
				break;

			default:
				fprintf(stderr, _("Unknown option '%c'\n"), optopt);
				return (status = ERR_INVALID);
		}
	}

	if (graphics)
	{
		constructWindow();
		windowGuiThread();
	}
	else
	{
		textScreenSave(&screen);
		textSetCursor(0);
		drawScreen();
	}

	grid = malloc(screenWidth * screenHeight * sizeof(objectType));
	snakeArray = malloc(((screenWidth * screenHeight) + 1) * sizeof(coord));
	if (!grid || !snakeArray)
		return (ERR_MEMORY);

	status = play();

	sprintf(tmpChar, _("%s\nScore %d.\n"),
		((status > 0)? _("You WIN!") : _("Dead.")), score);

	if (graphics)
	{
		if (status)
			windowNewInfoDialog(window, _("Game over"), tmpChar);
		windowDestroy(window);
	}
	else
	{
		textSetCursor(1);
		textScreenRestore(&screen);
		if (status)
			printf("%s\n", tmpChar);
	}

	if (screen.data)
		memoryRelease(screen.data);

	free(grid);
	free(snakeArray);
	for (count = 0; count < NUM_IMAGES; count ++)
		imageFree(&images[count]);

	return (status = 0);
}

