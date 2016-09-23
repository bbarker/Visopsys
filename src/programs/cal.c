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
//  cal.c
//

// This is a Calendar program, the original version of which was submitted
// by Bauer Vladislav <bauer@ccfit.nsu.ru>

/* This is the text that appears when a user requests help about this program
<help>

 -- cal --

Display the days of the current calendar month.

Usage:
  cal [-T]

In graphics mode, the program is interactive and allows the user to change
the month and year to display.

Options:
-T              : Force text mode operation

</help>
*/

#include <libintl.h>
#include <locale.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/api.h>
#include <sys/env.h>

#define _(string) gettext(string)
#define gettext_noop(string) (string)

#define WINDOW_TITLE	_("Calendar")

static const char *weekDay[] = {
	gettext_noop("Mo"),
	gettext_noop("Tu"),
	gettext_noop("We"),
	gettext_noop("Th"),
	gettext_noop("Fr"),
	gettext_noop("Sa"),
	gettext_noop("Su")
};

static const char *monthName[] = {
	gettext_noop("January"),
	gettext_noop("February"),
	gettext_noop("March"),
	gettext_noop("April"),
	gettext_noop("May"),
	gettext_noop("June"),
	gettext_noop("July"),
	gettext_noop("August"),
	gettext_noop("September"),
	gettext_noop("October"),
	gettext_noop("November"),
	gettext_noop("December")
};

static int monthDays[12] = {
	31 /* Jan */, 00 /* Feb */, 31 /* Mar */, 30 /* Apr */, 31 /* May */,
	30 /* Jun */, 31 /* Jul */, 31 /* Aug */, 30 /* Sep */, 31 /* Oct */,
	30 /* Nov */, 31 /* Dec */
};

static int graphics = 0;
static int date = 0;
static int month = 0;
static int year = 0;
static int dayOfWeek = 0;

// For graphics mode
static objectKey window = NULL;
static objectKey plusMonthButton  = NULL;
static objectKey minusMonthButton = NULL;
static objectKey plusYearButton  = NULL;
static objectKey minusYearButton = NULL;
static objectKey monthLabel = NULL;
static objectKey yearLabel = NULL;
static objectKey calList = NULL;
static listItemParameters *calListParams = NULL;


static int leapYear(int y)
{
	// There is a leap year in every year divisible by 4 except for years which
	// are both divisible by 100 and not by 400.  Got it?
	if (!(y % 4) && ((y % 100) || !(y % 400)))
		return (1);
	else
		return (0);
}


static int getDays(int m, int y)
{
	if (m == 1)
	{
		if (leapYear(y))
			return (29);
		else
			return (28);
	}

	return (monthDays[m]);
}


static void textCalendar(void)
{
	int days = getDays((month - 1), year);
	int firstDay  = rtcDayOfWeek(1, month, year);
	int spaceSkip = (10 - (strlen(_(monthName[month - 1])) + 5) / 2);
	int count;

	for (count = 0; count < spaceSkip; count++)
		printf(" ");

	printf("%s %i", _(monthName[month - 1]), year);

	printf("\n");
	for (count = 0; count < 7; count++)
		printf("%s ", _(weekDay[count]));

	printf("\n");
	for (count = 0; count < firstDay; count++)
		printf("   ");

	for (count = 1; count <= days; count++)
	{
		dayOfWeek = rtcDayOfWeek(count, month, year);
		printf("%2i ", count);

		if (dayOfWeek == 6)
			printf("\n");
	}

	if (dayOfWeek != 6)
		printf("\n");

	return;
}


static void initCalListParams(void)
{
	int days = getDays((month - 1), year);
	int firstDay  = rtcDayOfWeek(1, month, year);
	int count;

	for (count = 0; count < 49; count++)
		sprintf(calListParams[count].text, "  ");

	for (count = 0; count < 7; count++)
		sprintf(calListParams[count].text, "%s", _(weekDay[count]));

	for (count = 1; count <= days; count++)
		sprintf(calListParams[count + 6 + firstDay].text, "%2i", count);
}


static void getUpdate(void)
{
	char yearString[5];

	initCalListParams();

	itoa(year, yearString);
	windowComponentSetData(calList, calListParams, 49, 1 /* redraw */);
	windowComponentSetData(monthLabel, _(monthName[month - 1]), 10,
		1 /* redraw */);
	windowComponentSetData(yearLabel, yearString, 4, 1 /* redraw */);
}


static void refreshWindow(void)
{
	// We got a 'window refresh' event (probably because of a language switch),
	// so we need to update things

	// Re-get the language setting
	setlocale(LC_ALL, getenv(ENV_LANG));
	textdomain("cal");

	// Re-get the character set
	if (getenv(ENV_CHARSET))
		windowSetCharSet(window, getenv(ENV_CHARSET));

	// Refresh the window title
	windowSetTitle(window, WINDOW_TITLE);

	// Refresh the contents
	getUpdate();
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
			windowGuiStop();
	}

	else if (event->type == EVENT_MOUSE_LEFTUP)
	{
		if (key == minusMonthButton)
			month = ((month > 1)? (month - 1) : 12);

		else if (key == plusMonthButton)
			month = ((month < 12)? (month + 1) : 1);

		else if (key == minusYearButton)
			year = ((year >= 1900)? (year - 1) : 1900);

		else if (key == plusYearButton)
			year = ((year <= 3000)? (year + 1) : 3000);

		if ((key == minusMonthButton) || (key == plusMonthButton) ||
			(key == minusYearButton) || (key == plusYearButton))
		{
			getUpdate();
		}
	}

	return;
}


static void constructWindow(void)
{
	componentParameters params;
	struct tm theTime;

	window = windowNew(multitaskerGetCurrentProcessId(), WINDOW_TITLE);
	if (!window)
		exit(ERR_NOTINITIALIZED);

	memset(&params, 0, sizeof(componentParameters));
	params.gridWidth = 1;
	params.gridHeight = 1;
	params.padRight = 1;
	params.padLeft = 1;
	params.padTop = 5;
	params.padBottom = 5;
	params.orientationX = orient_center;
	params.orientationY = orient_middle;

	minusMonthButton = windowNewButton(window, "<", NULL, &params);
	windowRegisterEventHandler(minusMonthButton, &eventHandler);

	params.gridX += 1;
	plusMonthButton = windowNewButton(window, ">", NULL, &params);
	windowRegisterEventHandler(plusMonthButton, &eventHandler);

	params.gridX += 1;
	monthLabel = windowNewTextLabel(window, "", &params);
	windowComponentSetWidth(monthLabel, 80);

	params.gridX += 1;
	yearLabel = windowNewTextLabel(window, "", &params);

	params.gridX += 1;
	minusYearButton = windowNewButton(window, "<", NULL, &params);
	windowRegisterEventHandler(minusYearButton, &eventHandler);

	params.gridX += 1;
	plusYearButton = windowNewButton(window, ">", NULL, &params);
	windowRegisterEventHandler(plusYearButton, &eventHandler);

	params.gridX = 0;
	params.gridY = 1;
	params.gridWidth = 6;
	params.flags = WINDOW_COMPFLAG_FIXEDWIDTH;
	initCalListParams();
	calList = windowNewList(window, windowlist_textonly, 7, 7, 0,
		calListParams, 49, &params);
	getUpdate();

	memset(&theTime, 0, sizeof(struct tm));
	rtcDateTime(&theTime);
	windowComponentSetSelected(calList, rtcDayOfWeek(1, month, year) + 6 +
		theTime.tm_mday);
	windowComponentFocus(calList);
	windowRegisterEventHandler(window, &eventHandler);

	// Make the window visible
	windowSetResizable(window, 0);
	windowSetVisible(window, 1);

	return;
}


static void graphCalendar(void)
{
	calListParams = malloc(49 * sizeof(listItemParameters));
	if (!calListParams)
		exit(ERR_MEMORY);

	constructWindow();
	windowGuiRun();
	windowDestroy(window);
	free(calListParams);

	return;
}


int main(int argc, char *argv[])
{
	int status = 0;
	char opt;

	setlocale(LC_ALL, getenv(ENV_LANG));
	textdomain("cal");

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

	date  = rtcReadDayOfMonth();
	month = rtcReadMonth();
	year  = rtcReadYear();

	if (graphics)
		graphCalendar();
	else
		textCalendar();

	return (status);
}

