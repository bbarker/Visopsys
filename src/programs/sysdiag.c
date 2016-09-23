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
//  sysdiag.c
//

// Performs system diagnostics

/* This is the text that appears when a user requests help about this program
<help>

 -- sysdiag --

Perform system diagnostics

Usage:
  sysdiag

The sysdiag program is interactive, and can be used to perform diagnostic
functions on hardware such as the RAM memory or hard disks.

</help>
*/

#include <libintl.h>
#include <locale.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/api.h>
#include <sys/env.h>
#include <sys/memory.h>
#include <sys/paths.h>
#include <sys/progress.h>
#include <sys/text.h>
#include <sys/vsh.h>

#define _(string) gettext(string)

#define PROGNAME		_("System Diagnostics")
#define DISK_TEST		_("Disk test")
#define MEMORY_TEST		_("Memory test")
#define READ_ONLY		_("Read-only")
#define READ_WRITE		_("Read-write")
#define TEST			_("Test")
#define QUIT			_("Quit")
#define PERM			_("You must be a privileged user to use this "	\
						"command.\n(Try logging in as user \"admin\")")
#define READWRITE		\
	_("Do you want to do a read-only test, or a read-write test?\n" \
	"A read-only test is faster and guaranteed to be data-safe,\n" \
	"however it is less thorough.  A read-write test takes\n" \
	"longer and is more thorough, but can potentially cause\n" \
	"some data loss if your disk is failing.")
#define DISKTEST		_("Performing %s test on disk %s")
#define MEMORYTEST		_("Performing memory test of %uMB - %uMB in use")
#define TESTCANCELLED	_("Test cancelled")
#define TESTCOMPLETED	_("Test completed")
#define TESTERROR		_("Error performing test")
#define COUNTERRORS		_("%d errors")

typedef enum {
	disk_read_error, disk_write_error, memory_error
} errorType;

typedef struct {
	errorType type;
	uquad_t location;

} testError;

static int processId = 0;
static int privilege = 0;
static textScreen screen;
static disk *diskInfo = NULL;
static int numberDisks = 0;

// GUI stuff
static int graphics = 0;
static objectKey window = NULL;
static objectKey testTypeRadio = NULL;
static objectKey diskList = NULL;
static objectKey readWriteRadio = NULL;
static objectKey readWriteLabel = NULL;
static objectKey testButton = NULL;
static objectKey quitButton = NULL;


__attribute__((noreturn))
static void quit(int status)
{
	// Shut everything down

	if (graphics)
	{
		windowGuiStop();
		if (window)
			windowDestroy(window);
	}
	else
	{
		if (screen.data)
			textScreenRestore(&screen);
	}

	// Free any malloc'ed global memory

	exit(status);
}


static char pause(int q)
{
	printf(_("\nPress any key to continue%s"),
		(q? _(", or 'q' to quit") : "."));
	char c = getchar();
	printf("\n");
	return (c);
}


static void error(const char *format, ...)
{
	// Generic error message code for either text or graphics modes

	va_list list;
	char *output = NULL;

	output = malloc(MAXSTRINGLENGTH);
	if (!output)
		return;

	va_start(list, format);
	vsnprintf(output, MAXSTRINGLENGTH, format, list);
	va_end(list);

	if (graphics)
	{
		windowNewErrorDialog(window, _("Error"), output);
	}
	else
	{
		printf("\n\n%s\n", output);
		pause(0);
	}

	free(output);
}


static void printBanner(void)
{
	textScreenClear();
	printf(_("%s\nCopyright (C) 1998-2016 J. Andrew McLaughlin\n"), PROGNAME);
}


static int getDiskInfo(void)
{
	int status = 0;
	int physicalDisks = 0;
	int count;

	diskInfo = NULL;
	numberDisks = 0;

	// Call the kernel to give us the number of available disks
	physicalDisks = diskGetPhysicalCount();
	if (physicalDisks <= 0)
		return (status = ERR_NOSUCHENTRY);

	diskInfo = malloc(physicalDisks * sizeof(disk));
	if (!diskInfo)
		return (status = ERR_MEMORY);

	// Read disk info into our temporary structure
	status = diskGetAllPhysical(diskInfo, (physicalDisks * sizeof(disk)));
	if (status < 0)
	{
		// Eek.  Problem getting disk info.
		free(diskInfo);
		return (status);
	}

	for (count = 0; count < physicalDisks; count ++)
	{
		if (diskInfo[count].type & (DISKTYPE_FLOPPY | DISKTYPE_HARDDISK))
			memcpy(&diskInfo[numberDisks++], &diskInfo[count], sizeof(disk));
	}

	return (status = 0);
}


static int chooseDisk(void)
{
	// This is where the user chooses the disk to test

	int diskNumber = -1;
	char *diskStrings[DISK_MAXDEVICES];
	int count;

	if (numberDisks <= 0)
		return (ERR_NOSUCHENTRY);

	for (count = 0; count < numberDisks; count ++)
		diskStrings[count] = diskInfo[count].name;

	diskNumber = vshCursorMenu(_("Please choose the disk to test:"),
		diskStrings, numberDisks, 10 /* max rows */, 0 /* selected */);

	return (diskNumber);
}


static void showResults(int status, testError *testErrors, int numErrors)
{
	int choice = 0;
	char *choiceStrings[] = {
	_("View results"),
	_("Quit")
	};
	objectKey resultsWindow = NULL;
	objectKey textArea = NULL;
	componentParameters params;
	windowEvent event;
	char tmp[80];
	int count;

	if ((status == ERR_CANCELLED) || (status >= 0))
	{
		tmp[0] = '\0';

		if (status == ERR_CANCELLED)
			sprintf((tmp + strlen(tmp)), "%s\n", TESTCANCELLED);
		else
			sprintf((tmp + strlen(tmp)), "%s\n", TESTCOMPLETED);

		if (!graphics)
			strcat(tmp, "  ");

		sprintf((tmp + strlen(tmp)), COUNTERRORS, numErrors);

		if (graphics)
		{
			if (numErrors)
			{
				choice = windowNewChoiceDialog(window,
					((status == ERR_CANCELLED)? TESTCANCELLED : TESTCOMPLETED),
					tmp, choiceStrings, 2, 0);
			}
			else
			{
				windowNewInfoDialog(window, ((status == ERR_CANCELLED)?
					TESTCANCELLED : TESTCOMPLETED), tmp);
				return;
			}
		}
		else
		{
			printf("\n%s\n\n", tmp);
			if (numErrors)
			{
				choice = vshCursorMenu(_("Do you want to view the results?\n"),
					choiceStrings, 2, 0 /* no max rows */, 0 /* selected */);
			}
			else
			{
				return;
			}
		}

		if (!choice)
		{
			if (graphics)
			{
				// Create a new window
				resultsWindow = windowNew(processId, _("Test results"));

				// Put a text area in the window
				memset(&params, 0, sizeof(componentParameters));
				params.gridWidth = 1;
				params.gridHeight = 1;
				params.padLeft = 1;
				params.padRight = 1;
				params.padTop = 1;
				params.padBottom = 1;
				params.orientationX = orient_center;
				params.orientationY = orient_middle;
				params.font = fontGet(FONT_FAMILY_LIBMONO,
					FONT_STYLEFLAG_FIXED, 10, NULL);

				textArea = windowNewTextArea(resultsWindow, 60, 15, 200,
					&params);
				// Use the text area for the following output
				windowSetTextOutput(textArea);
				windowCenterDialog(window, resultsWindow);
				windowSetVisible(resultsWindow, 1);
			}
			else
			{
				printf("\n");
			}

			for (count = 0; count < numErrors; count ++)
			{
				switch (testErrors[count].type)
				{
					case disk_read_error:
						printf(_("Disk read error at sector %llu\n"),
							testErrors[count].location);
						break;
					case disk_write_error:
						printf(_("Disk write error at sector %llu\n"),
							testErrors[count].location);
						break;
					case memory_error:
						printf(_("Memory error at %llu\n"),
							testErrors[count].location);
						break;
				}

				if (!graphics && count && !(count % (textGetNumRows() - 3)))
				{
					// Pause after approximately a screenful
					char c = pause(1);
					if ((c == 'q') || (c == 'Q'))
						break;
				}
			}

			if (graphics)
			{
				while (1)
				{
					// Check for window close events
					status = windowComponentEventGet(resultsWindow, &event);
					if ((status < 0) ||
						((status > 0) && (event.type == EVENT_WINDOW_CLOSE)))
					{
						break;
					}

					// Done
					multitaskerYield();
				}

				windowDestroy(resultsWindow);
			}
			else
			{
				printf("\n");
			}
		}
	}
	else
	{
		if (graphics)
			windowNewInfoDialog(window, TESTCOMPLETED, TESTERROR);
		else
			printf("\n%s\n\n", TESTERROR);
	}

	return;
}


static int diskError(testError **testErrors, uquad_t *numErrors,
	errorType type, uquad_t location)
{
	*testErrors = realloc(*testErrors, ((*numErrors + 1) * sizeof(testError)));
	if (!(*testErrors))
		return (ERR_MEMORY);

	(*testErrors)[*numErrors].type = type;
	(*testErrors)[*numErrors].location = location;

	*numErrors += 1;
	return (0);
}


static int doDiskTest(disk *theDisk, testError **testErrors,
	uquad_t *numErrors, int write)
{
	int status = 0;
	int sectorsPerOp = 256;
	unsigned bufferSize = (theDisk->sectorSize * sectorsPerOp);
	unsigned char *dataBuffer = NULL;
	unsigned char *patternBuffer = NULL;
	unsigned char *compareBuffer = NULL;
	progress prog;
	objectKey progressDialog = NULL;
	char tmp[80];
	uquad_t count1, count2;

	dataBuffer = malloc(bufferSize);
	patternBuffer = malloc(bufferSize);
	compareBuffer = malloc(bufferSize);
	if (!dataBuffer || !patternBuffer || !compareBuffer)
	{
		status = ERR_MEMORY;
		goto out;
	}

	// Fill the pattern buffer with semi-random data
	randomBytes(patternBuffer, bufferSize);

	if (!graphics)
	{
		printf("\n");
		printf(DISKTEST, (write? _("read-write") : _("read-only")),
			theDisk->name);
		printf("%s", _("\n[ Press 'Q' to cancel ]\n"));
	}

	memset((void *) &prog, 0, sizeof(progress));
	prog.numTotal = theDisk->numSectors;
	prog.canCancel = 1;

	if (graphics)
	{
		sprintf(tmp, DISKTEST, (write? _("read-write") : _("read-only")),
			theDisk->name);
		progressDialog = windowNewProgressDialog(window, tmp, &prog);
	}
	else
	{
		vshProgressBar(&prog);
		textInputSetEcho(0);
	}

	for (count1 = 0; count1 < theDisk->numSectors; count1 += sectorsPerOp)
	{
		status =
			diskReadSectors(theDisk->name, count1, sectorsPerOp, dataBuffer);

		if (status < 0)
		{
			// We were unable to read some sector in that batch.  Test each
			// sector individually to see which one(s) are the problem.
			for (count2 = count1; count2 < (count1 + sectorsPerOp); count2 ++)
			{
				status = diskReadSectors(theDisk->name, count2, 1, dataBuffer);

				if (status < 0)
				{
					status = diskError(testErrors, numErrors, disk_read_error,
						count2);
					if (status < 0)
						goto out;
				}
			}
		}
		else if (write)
		{
			// If it's a write test, write our pattern data to the sector
			status = diskWriteSectors(theDisk->name, count1, sectorsPerOp,
				patternBuffer);

			if (status < 0)
			{
				// We were unable to write some sector in that batch.  Test
				// each sector individually to see which one(s) are the
				// problem, but write back the *real* data instead of the
				// pattern, because we're not continuing on to the next steps
				// for these sectors.
				for (count2 = count1; count2 < (count1 + sectorsPerOp);
					count2 ++)
				{
					status = diskWriteSectors(theDisk->name, count2, 1,
						(dataBuffer + ((count2 - count1) *
							theDisk->sectorSize)));

					if (status < 0)
					{
						status = diskError(testErrors, numErrors,
							disk_write_error, count2);
						if (status < 0)
							goto out;
					}
				}
			}
			else
			{
				// Read it back
				status = diskReadSectors(theDisk->name, count1, sectorsPerOp,
					compareBuffer);
				if (status < 0)
				{
					// Don't quit here if this call fails, as we need to
					// attempt to write the real data back.
					diskError(testErrors, numErrors, disk_read_error, count1);
				}
				else
				{
					// Compare the data we read to make sure it's the data from
					// the pattern.
					if (memcmp(patternBuffer, compareBuffer, bufferSize))
					{
						// Eeek, the write actually failed.  Don't quit here if
						// this call fails, as we need to attempt to write the
						// real data back.
						diskError(testErrors, numErrors, disk_write_error,
							count1);
					}
				}

				// Write the real data back to the disk
				status = diskWriteSectors(theDisk->name, count1, sectorsPerOp,
					dataBuffer);
				if (status < 0)
				{
					status = diskError(testErrors, numErrors, disk_write_error,
						count1);
					if (status < 0)
						goto out;
				}
			}
		}

		if (lockGet(&prog.progLock) >= 0)
		{
			if (prog.cancel)
			{
				status = ERR_CANCELLED;
				goto out;
			}

			prog.numFinished = count1;
			prog.percentFinished = ((prog.numFinished * 100) / prog.numTotal);
			snprintf((char *) prog.statusMessage, PROGRESS_MAX_MESSAGELEN,
				_("Testing disk sectors %llu/%llu"),
				(uquad_t) prog.numFinished, (uquad_t) prog.numTotal);
			lockRelease(&prog.progLock);
		}
	}

	status = 0;

out:

	if (graphics && progressDialog)
	{
		windowProgressDialogDestroy(progressDialog);
	}
	else
	{
		vshProgressBarDestroy(&prog);
		textInputSetEcho(1);
	}

	if (compareBuffer)
		free(compareBuffer);
	if (patternBuffer)
		free(patternBuffer);
	if (dataBuffer)
		free(dataBuffer);

	return (status);
}


static int diskTest(int diskNumber, int writeTest)
{
	int status = 0;
	testError *testErrors = NULL;
	uquad_t numErrors = 0;

	// For the time being, turn off disk caching for this disk
	status = diskSetFlags(diskInfo[diskNumber].name, DISKFLAG_NOCACHE, 1);
	if (status < 0)
		return (status);

	// Run the test
	status = doDiskTest(&diskInfo[diskNumber], &testErrors, &numErrors,
		writeTest);

	showResults(status, testErrors, numErrors);

	// Clear the 'no-cache' flag for this disk
	diskSetFlags(diskInfo[diskNumber].name, DISKFLAG_NOCACHE, 0);

	return (status);
}


static void countMemory(uquad_t *total, uquad_t *free)
{
	memoryStats stats;

	// Get overall memory statistics
	memset(&stats, 0, sizeof(memoryStats));
	if (memoryGetStats(&stats, 0) < 0)
		return;

	*total = stats.totalMemory;
	*free = (stats.totalMemory - stats.usedMemory);

	return;
}


static int doMemoryTest(testError **testErrors, uquad_t *numErrors)
{
	int status = 0;
	uquad_t totalMemory = 0;
	uquad_t totalFree = 0;
	volatile unsigned char **patternBuffers = NULL;
	unsigned numBlocks = 0;
	volatile unsigned char **blockPointers = NULL;
	progress prog;
	objectKey progressDialog = NULL;
	unsigned blockSize = 0;
	volatile unsigned char *page = NULL;
	char tmp[80];
	unsigned count1, count2;
	#define MB 1048576

	// Allocate memory for the pattern buffers
	patternBuffers = malloc((MB / MEMORY_PAGE_SIZE) * sizeof(unsigned char *));
	if (!patternBuffers)
	{
		status = ERR_MEMORY;
		goto out;
	}

	for (count1 = 0; count1 < (MB / MEMORY_PAGE_SIZE); count1 ++)
	{
		patternBuffers[count1] = malloc(MEMORY_PAGE_SIZE);
		if (!patternBuffers[count1])
		{
			status = ERR_MEMORY;
			goto out;
		}
	}

	// Provisionally, how much free memory is there?
	countMemory(&totalMemory, &totalFree);

	// We will allocate blocks to test by megabyte.  How many pointers will
	// that be?
	numBlocks = ((totalFree + (MB - 1)) / MB);

	// Allocate some memory for the pointers
	blockPointers = malloc(numBlocks * sizeof(unsigned char *));
	if (!blockPointers)
	{
		status = ERR_MEMORY;
		goto out;
	}

	// Re-calculate the free memory and number of blocks
	countMemory(&totalMemory, &totalFree);
	numBlocks = ((totalFree + (MB - 1)) / MB);

	if (!graphics)
	{
		printf("\n");
		printf(MEMORYTEST, numBlocks, ((totalMemory - totalFree) / MB));
		printf("%s", _("\n[ Press 'Q' to cancel ]\n"));
	}

	memset((void *) &prog, 0, sizeof(progress));
	prog.numTotal = numBlocks;
	prog.canCancel = 1;

	if (graphics)
	{
		sprintf(tmp, MEMORYTEST, numBlocks, ((totalMemory - totalFree) / MB));
		progressDialog = windowNewProgressDialog(window, tmp, &prog);
	}
	else
	{
		vshProgressBar(&prog);
		textInputSetEcho(0);
	}

	for (count1 = 0; count1 < numBlocks; count1 ++)
	{
		countMemory(&totalMemory, &totalFree);
		totalFree &= ~(MEMORY_PAGE_SIZE - 1);
		blockSize = min(MB, totalFree);

		if (blockSize)
		{
			blockPointers[count1] = memoryGet(blockSize, "memory testing");
			if (!blockPointers[count1])
				break;
		}
		else
		{
			break;
		}

		// Fill the pattern buffers with semi-random data, in a separate loop
		// so that we will hopefully foil memory caching (I have no idea if
		// this will work)
		for (count2 = 0; count2 < (blockSize / MEMORY_PAGE_SIZE); count2 ++)
		{
			randomBytes((unsigned char *)patternBuffers[count2],
				MEMORY_PAGE_SIZE);
		}

		// Test writing the pattern to each page in a separate loop
		for (count2 = 0; count2 < (blockSize / MEMORY_PAGE_SIZE); count2 ++)
		{
			page = (blockPointers[count1] + (count2 * MEMORY_PAGE_SIZE));
			memcpy((void *) page, (void *) patternBuffers[count2],
				MEMORY_PAGE_SIZE);
		}

		// Now compare them, again in a separate loop
		for (count2 = 0; count2 < (blockSize / MEMORY_PAGE_SIZE); count2 ++)
		{
			page = (blockPointers[count1] + (count2 * MEMORY_PAGE_SIZE));
			if (memcmp((void *) page, (void *) patternBuffers[count2],
				MEMORY_PAGE_SIZE))
			{
				// Didn't match

				*testErrors = realloc(*testErrors, ((*numErrors + 1) *
					sizeof(testError)));
				if (!(*testErrors))
				{
					status = ERR_MEMORY;
					goto out;
				}

				(*testErrors)[*numErrors].type = memory_error;
				(*testErrors)[*numErrors].location =
					(unsigned) pageGetPhysical(processId, (void *) page);

				*numErrors += 1;
			}
		}

		if (lockGet(&prog.progLock) >= 0)
		{
			if (prog.cancel)
			{
				status = ERR_CANCELLED;
				goto out;
			}

			prog.numFinished = count1;
			prog.percentFinished = ((prog.numFinished * 100) / prog.numTotal);
			snprintf((char *) prog.statusMessage, PROGRESS_MAX_MESSAGELEN,
				_("Testing memory MB %llu/%llu"), prog.numFinished,
				prog.numTotal);
			lockRelease(&prog.progLock);
		}
	}

	status = 0;

out:
	if (graphics && progressDialog)
	{
		windowProgressDialogDestroy(progressDialog);
	}
	else
	{
		vshProgressBarDestroy(&prog);
		textInputSetEcho(1);
	}

	if (blockPointers)
	{
		// Free all of the blocks
		for (count1 = 0; count1 < numBlocks; count1 ++)
		{
			if (blockPointers[count1])
				memoryRelease((void *) blockPointers[count1]);
		}

		free(blockPointers);
	}

	if (patternBuffers)
	{
		// Free all of the buffers
		for (count1 = 0; count1 < (MB / MEMORY_PAGE_SIZE); count1 ++)
		{
			if (patternBuffers[count1])
				free((void *) patternBuffers[count1]);
		}

		free(patternBuffers);
	}

	return (status);
}


static int memoryTest(void)
{
	int status = 0;
	testError *testErrors = NULL;
	uquad_t numErrors = 0;

	// Run the test
	status = doMemoryTest(&testErrors, &numErrors);

	showResults(status, testErrors, numErrors);

	return (status);
}


static void eventHandler(objectKey key, windowEvent *event)
{
	int selected1 = -1;
	int selected2 = -1;
	int selected3 = -1;

	// Check for window events.
	if (key == window)
	{
		// Check for the window being closed
		if (event->type == EVENT_WINDOW_CLOSE)
			quit(0);
	}

	else if (key == testTypeRadio)
	{
		windowComponentGetSelected(testTypeRadio, &selected1);
		switch (selected1)
		{
			case 0:
			default:
				windowComponentSetEnabled(diskList, 1);
				windowComponentSetEnabled(readWriteRadio, 1);
				windowComponentSetEnabled(readWriteLabel, 1);
				break;

			case 1:
				windowComponentSetEnabled(diskList, 0);
				windowComponentSetEnabled(readWriteRadio, 0);
				windowComponentSetEnabled(readWriteLabel, 0);
				break;
		}
	}

	else if ((key == testButton) && (event->type == EVENT_MOUSE_LEFTUP))
	{
		windowComponentSetEnabled(testButton, 0);
		windowComponentSetEnabled(quitButton, 0);

		windowComponentGetSelected(testTypeRadio, &selected1);
		windowComponentGetSelected(diskList, &selected2);
		windowComponentGetSelected(readWriteRadio, &selected3);

		if (selected1 == 0)
			diskTest(selected2, selected3);
		else if (selected1 == 1)
			memoryTest();

		windowComponentSetEnabled(testButton, 1);
		windowComponentSetEnabled(quitButton, 1);
	}

	else if ((key == quitButton) && (event->type == EVENT_MOUSE_LEFTUP))
		quit(0);
}


static void constructWindow(void)
{
	// If we are in graphics mode, make a window rather than operating on the
	// command line.

	listItemParameters diskListParams[DISK_MAXDEVICES];
	objectKey container = NULL;
	image iconImage;
	componentParameters params;
	int count;

	memset(diskListParams, 0, (numberDisks * sizeof(listItemParameters)));
	for (count = 0; count < numberDisks; count ++)
	{
		uquad_t diskSize =
			(diskInfo[count].numSectors * diskInfo[count].sectorSize);
		double gigabytes = ((double) diskSize / (1024 * 1024 * 1024));
		double megabytes = ((double) diskSize / (1024 * 1024));

		snprintf(diskListParams[count].text, WINDOW_MAX_LABEL_LENGTH,
			"%s - %s : %1g %s", diskInfo[count].name, diskInfo[count].model,
			((gigabytes >= 1.0)? gigabytes : megabytes),
			((gigabytes >= 1.0)? "GB" : "MB"));
	}

	// Create a new window
	window = windowNew(processId, PROGNAME);
	if (!window)
		return;

	memset(&params, 0, sizeof(componentParameters));
	params.gridWidth = 1;
	params.gridHeight = 1;
	params.orientationX = orient_left;
	params.orientationY = orient_top;

	// Make a container for the left hand side components
	container = windowNewContainer(window, "leftContainer", &params);

	// Try to load an icon image to go at the top of the window
	params.padTop = 5;
	params.padLeft = 5;
	params.padRight = 5;
	if (imageLoad(PATH_SYSTEM_ICONS "/sysdiag.ico", 0, 0, &iconImage) >= 0)
	{
		// Create an image component from it, and add it to the container
		iconImage.transColor.green = 255;
		params.flags |= (WINDOW_COMPFLAG_FIXEDWIDTH |
			WINDOW_COMPFLAG_FIXEDHEIGHT);
		windowNewImage(container, &iconImage, draw_alphablend, &params);
		imageFree(&iconImage);
	}

	// Create the test type radio button
	params.gridY += 1;
	params.flags &= ~(WINDOW_COMPFLAG_FIXEDWIDTH |
		WINDOW_COMPFLAG_FIXEDHEIGHT);
	testTypeRadio = windowNewRadioButton(container, 2, 1, (char *[])
			{ DISK_TEST, MEMORY_TEST }, 2, &params);
	windowRegisterEventHandler(testTypeRadio, &eventHandler);

	// A little divider between the containers
	params.gridX += 1;
	params.gridY = 0;
	params.orientationX = orient_center;
	params.flags |= WINDOW_COMPFLAG_FIXEDWIDTH;
	windowNewDivider(window, divider_vertical, &params);

	// Make a container for the right hand side components
	params.gridX += 1;
	params.padTop = 0;
	params.padLeft = 0;
	params.padRight = 0;
	params.flags &= ~WINDOW_COMPFLAG_FIXEDWIDTH;
	container = windowNewContainer(window, "rightContainer", &params);

	// Make a window list with all the disk choices
	params.gridX = 0;
	params.gridY = 0;
	params.gridWidth = 2;
	params.padTop = 5;
	params.padLeft = 5;
	params.padRight = 5;
	diskList = windowNewList(container, windowlist_textonly, 4, 1, 0,
		diskListParams, numberDisks, &params);

	// A radio button for choosing read-only or read-write tests
	params.gridY += 1;
	params.gridWidth = 1;
	readWriteRadio = windowNewRadioButton(container, 2, 1, (char *[])
		{ READ_ONLY, READ_WRITE }, 2, &params);

	params.gridX += 1;
	params.font = fontGet(FONT_FAMILY_LIBMONO, (FONT_STYLEFLAG_BOLD |
		FONT_STYLEFLAG_FIXED), 10, NULL);
	readWriteLabel = windowNewTextLabel(container, READWRITE, &params);

	// Make a container for the bottom buttons
	params.gridX = 0;
	params.gridY = 1;
	params.gridWidth = 3;
	params.padTop = 0;
	params.padLeft = 0;
	params.padRight = 0;
	params.orientationX = orient_center;
	params.font = NULL;
	params.flags |= (WINDOW_COMPFLAG_FIXEDWIDTH | WINDOW_COMPFLAG_FIXEDHEIGHT);
	container = windowNewContainer(window, "buttonContainer", &params);

	// Create the Test button
	params.gridY = 0;
	params.gridWidth = 1;
	params.padTop = 5;
	params.padBottom = 5;
	params.padLeft = 5;
	params.padRight = 5;
	params.orientationX = orient_right;
	testButton = windowNewButton(container, TEST, NULL, &params);
	windowRegisterEventHandler(testButton, &eventHandler);
	windowComponentFocus(testButton);

	// Create the Quit button
	params.gridX++;
	params.orientationX = orient_left;
	quitButton = windowNewButton(container, QUIT, NULL, &params);
	windowRegisterEventHandler(quitButton, &eventHandler);

	// Register an event handler to catch window close events
	windowRegisterEventHandler(window, &eventHandler);

	windowSetVisible(window, 1);

	return;
}


__attribute__((noreturn))
int main(void)
{
	int status = 0;
	int testType = 0;
	char *testStrings[] = {
		_("Disk test"),
		_("Memory test")
	};
	int diskNumber = 0;
	char *readWriteStrings[] = {
		_("Read-only"),
		_("Read-write")
	};
	int writeTest = 0;

	// Are graphics enabled?
	graphics = graphicsAreEnabled();

	setlocale(LC_ALL, getenv(ENV_LANG));
	textdomain("sysdiag");

	// We need our process ID and privilege to create the windows
	processId = multitaskerGetCurrentProcessId();
	privilege = multitaskerGetProcessPrivilege(processId);

	// Check privilege level
	if (privilege)
	{
		if (graphics)
			error(PERM);
		else
			printf("\n%s\n\n", PERM);

		quit(ERR_PERMISSION);
	}

	// Get all the available disks for disk testing
	status = getDiskInfo();
	if (status < 0)
		quit(status);

	if (graphics)
	{
		constructWindow();

		// Run the GUI
		windowGuiRun();

		windowDestroy(window);
	}
	else
	{
		textScreenSave(&screen);
		printBanner();

		// Disk test, or memory test?
		testType = vshCursorMenu(_("Do you want to do a disk test, or a "
			"memory test?\n"), testStrings, 2, 0 /* no max rows */,
			0 /* selected */);
		if (testType < 0)
			quit(ERR_CANCELLED);

		if (!testType)
		{
			// Disk testing.

			// Get the user to choose a disk
			diskNumber = chooseDisk();
			if (diskNumber < 0)
				quit(ERR_CANCELLED);

			// Read-only test, or read-write test?
			writeTest = vshCursorMenu(READWRITE, readWriteStrings, 2,
				0 /* no max rows */, 0 /* selected */);
			if (writeTest < 0)
				quit(ERR_CANCELLED);

			status = diskTest(diskNumber, writeTest);
		}
		else
		{
			// Memory testing
			status = memoryTest();
		}

		pause(0);
	}

	quit(status);
}

