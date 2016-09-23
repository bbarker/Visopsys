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
//  kernelDebug.c
//

#if defined(DEBUG)

#include "kernelDebug.h"
#include "kernelError.h"
#include "kernelInterrupt.h"
#include "kernelMultitasker.h"
#include "kernelPic.h"
#include "kernelText.h"
#include <stdio.h>
#include <stdlib.h>
#include <sys/cdefs.h>

static debug_category categories[MAX_DEBUG_CATEGORIES];
static int numDebugCategories = 0;
static const char *fileNames[MAX_DEBUG_FILENAMES];
static int numDebugFileNames = 0;
static int debugAll = 0;
static int showProcess = 0;
static int showFile = 0;
static int showFunction = 0;


static int isCategory(debug_category category)
{
	// Returns 1 if we're debugging a particular category

	int count;

	if (category == debug_all)
		return (1);

	for (count = 0; count < numDebugCategories; count ++)
		if (categories[count] == category)
			return (1);

	// Fell through -- not there.
	return (0);
}


static int isFileName(const char *fileName)
{
	// Returns 1 if we're debugging a particular file

	int count;

	for (count = 0; count < numDebugFileNames; count ++)
		if (fileNames[count] == fileName)
			return (1);

	// Fell through -- not there.
	return (0);
}


/////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////
//
//  Below here, the functions are exported for external use
//
/////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////


void kernelDebugInitialize(void)
{
	// Here is where we can enable any flags/categories/files for debugging.
	// It can be done inline, around specific sections of code, or here.
	//
	// Here are some examples:
	//
	// kernelDebugFlags(DEBUG_SHOWPROCESS |
	//					DEBUG_SHOWFILE |
	//					DEBUG_SHOWFUNCTION);
	//
	// kernelDebugAddCategory(debug_all);
	// kernelDebugAddCategory(debug_api);
	// kernelDebugAddCategory(debug_font);
	// kernelDebugAddCategory(debug_fs);
	// kernelDebugAddCategory(debug_gui);
	// kernelDebugAddCategory(debug_io);
	// kernelDebugAddCategory(debug_loader);
	// kernelDebugAddCategory(debug_memory);
	// kernelDebugAddCategory(debug_misc);
	// kernelDebugAddCategory(debug_multitasker);
	// kernelDebugAddCategory(debug_pci);
	// kernelDebugAddCategory(debug_power);
	// kernelDebugAddCategory(debug_scsi);
	// kernelDebugAddCategory(debug_usb);
	// kernelDebugAddCategory(debug_device);
	//
	// kernelDebugAddFile("kernelWindow.c");

	return;
}


void kernelDebugFlags(int flags)
{
	// Sets the amount of information that gets displayed with each
	// line of debugging information

	if (flags & DEBUG_SHOWPROCESS)
		showProcess = 1;
	if (flags & DEBUG_SHOWFILE)
		showFile = 1;
	if (flags & DEBUG_SHOWFUNCTION)
		showFunction = 1;

	return;
}


void kernelDebugAddCategory(debug_category category)
{
	// Used to turn on a category of debug messages

	if (category == debug_all)
		debugAll = 1;

	else if (!isCategory(category))
	{
		if (numDebugCategories >= MAX_DEBUG_CATEGORIES)
			kernelError(kernel_error, "Max debug categories (%d) already "
				"registered", MAX_DEBUG_CATEGORIES);
		else
			categories[numDebugCategories++] = category;
	}

	return;
}


void kernelDebugAddFile(const char *fileName)
{
	// Used to turn on debug messages for a source file

	if (!isFileName(fileName))
	{
		if (numDebugFileNames >= MAX_DEBUG_FILENAMES)
			kernelError(kernel_error, "Max debug file names (%d) already "
				"registered", MAX_DEBUG_FILENAMES);
		else
			fileNames[numDebugFileNames++] = fileName;
	}

	return;
}


void kernelDebugRemoveFile(const char *fileName)
{
	// Used to turn off debug messages for a source file

	int count;

	if (!isFileName(fileName))
		return;

	for (count = 0; count < numDebugFileNames; count ++)
	{
		if (fileNames[count] == fileName)
		{
			fileNames[count] = NULL;
			if (--numDebugFileNames && (count < numDebugFileNames))
				fileNames[count] = fileNames[numDebugFileNames];
		}
	}
}


void kernelDebugOutput(const char *fileName, const char *function, int line,
	debug_category category, const char *message, ...)
{
	// This routine takes a bunch of parameters and outputs the message,
	// depending on a couple of filtering parameters.

	va_list list;
	char debugText[MAX_DEBUGTEXT_LENGTH];
	kernelTextOutputStream *console = kernelTextGetConsoleOutput();
	int interrupt = 0;

	// See whether we should skip this message
	if (!debugAll && !isCategory(category) && !isFileName(fileName))
		return;

	if (strlen(message) > (MAX_DEBUGTEXT_LENGTH - 80))
		message = "<<<debug message too long>>>";

	strcpy(debugText, "DEBUG " );
	if (showProcess)
	{
		if (kernelProcessingInterrupt() &&
			((interrupt = kernelPicGetActive()) >= 0))
		{
			sprintf((debugText + strlen(debugText)), "interrupt %d:",
				interrupt);
		}
		else
		{
			if (kernelCurrentProcess)
				sprintf((debugText + strlen(debugText)), "%s:",
					kernelCurrentProcess->name);
			else
				strcat(debugText, "kernel:");
		}
	}

	if (showFile)
		sprintf((debugText + strlen(debugText)), "%s(%d):", fileName, line);
	if (showFunction)
		sprintf((debugText + strlen(debugText)), "%s:", function);

	kernelTextStreamPrint(console, debugText);

	// Initialize the argument list
	va_start(list, message);

	// Expand the message if there were any parameters
	_xpndfmt(debugText, MAX_DEBUGTEXT_LENGTH, message, list);

	va_end(list);

	kernelTextStreamPrintLine(console, debugText);

	return;
}


void kernelDebugHex(void *ptr, unsigned length)
{
	unsigned char *buff = ptr;
	char debugText[MAX_DEBUGTEXT_LENGTH];
	kernelTextOutputStream *console = kernelTextGetConsoleOutput();
	unsigned count1, count2;

	for (count1 = 0; count1 < ((length / 16) + ((length % 16)? 1 : 0));
		count1 ++)
	{
		strcpy(debugText, "DEBUG HEX " );

		for (count2 = 0; (count2 < 16) && (((count1 * 16) + count2) < length);
			count2 ++)
		{
			sprintf((debugText + strlen(debugText)),
				"%02x ", buff[(count1 * 16) + count2]);
		}

		kernelTextStreamPrintLine(console, debugText);
	}
}


void kernelDebugHexDwords(void *ptr, unsigned length)
{
	unsigned *buff = ptr;
	char debugText[MAX_DEBUGTEXT_LENGTH];
	kernelTextOutputStream *console = kernelTextGetConsoleOutput();
	unsigned count1, count2;

	for (count1 = 0; count1 < ((length / 4) + ((length % 4)? 1 : 0));
		count1 ++)
	{
		strcpy(debugText, "DEBUG HEX " );

		for (count2 = 0; (count2 < 4) && (((count1 * 4) + count2) < length);
			count2 ++)
		{
			sprintf((debugText + strlen(debugText)),
				"%08x ", buff[(count1 * 4) + count2]);
		}

		kernelTextStreamPrintLine(console, debugText);
	}
}


void kernelDebugBinary(void *ptr, unsigned length)
{
	unsigned char *buff = ptr;
	char tmp = 0;
	char debugText[MAX_DEBUGTEXT_LENGTH];
	kernelTextOutputStream *console = kernelTextGetConsoleOutput();
	unsigned count1, count2, count3;

	for (count1 = 0; count1 < ((length / 4) + ((length % 4)? 1 : 0));
		count1 ++)
	{
		strcpy(debugText, "DEBUG BINARY " );

		for (count2 = 0; (count2 < 4) && (((count1 * 4) + count2) < length);
			count2 ++)
		{
			tmp = buff[(count1 * 4) + count2];
			for (count3 = 0; count3 < 8; count3 ++)
			{
				strcat(debugText, ((tmp & 0x80)? "1" : "0"));
				tmp <<= 1;
			}

			strcat(debugText, " ");
		}

		kernelTextStreamPrintLine(console, debugText);
	}
}


void kernelDebugStack(void *stackMemory, unsigned stackSize, void *stackPtr,
	long memoryOffset, unsigned showMax)
{
	void *stackBase = (stackMemory + stackSize - sizeof(void *));
	char debugText[MAX_DEBUGTEXT_LENGTH];
	kernelTextOutputStream *console = kernelTextGetConsoleOutput();
	unsigned count;

	showMax = min(showMax, ((stackBase - stackPtr) / sizeof(void *)));
	if (!showMax)
		showMax = ((stackBase - stackPtr) / sizeof(void *));

	for (count = 0; count < showMax; count ++)
	{
		strcpy(debugText, "DEBUG STACK ");

		sprintf((debugText + strlen(debugText)), "%p: %08x",
			(stackPtr + (count * sizeof(void *))),
			*((unsigned *)(stackPtr + memoryOffset +
				(count * sizeof(void *)))));

		if (!count)
			strcat(debugText, " <- sp");

		kernelTextStreamPrintLine(console, debugText);
	}
}


void kernelDebugDoStop(const char *fileName, const char *function, int line)
{
	kernelDebugFlags(DEBUG_SHOWPROCESS | DEBUG_SHOWFILE | DEBUG_SHOWFUNCTION);
	kernelDebugOutput(fileName, function, line, debug_all, "STOP");
	while (1);
}

#endif  // defined(DEBUG)

