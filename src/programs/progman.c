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
//  progman.c
//

// This is a program for managing programs and processes.

/* This is the text that appears when a user requests help about this program
<help>

 -- progman --

Also known as the "Program Manager", progman is used to manage processes.

Usage:
  progman

(Only available in graphics mode)

The progman program is interactive, and shows a constantly-updated list of
running processes and threads, including statistics such as CPU and memory
usage.  It is a graphical utility combining the same functionalities as the
'ps', 'mem', 'kill', and 'renice' command-line programs.

</help>
*/

#include <errno.h>
#include <libintl.h>
#include <locale.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/api.h>
#include <sys/env.h>
#include <sys/paths.h>
#include <sys/vsh.h>

#define _(string) gettext(string)

#define WINDOW_TITLE			_("Program Manager")
#define USEDBLOCKS_STRING		_("Memory blocks: ")
#define USEDMEM_STRING			_("Used memory: ")
#define FREEMEM_STRING			_("Free memory: ")
#define DISKPERF_STRING			_("Disk performance:")
#define READPERF_STRING			_("Read: ")
#define WRITEPERF_STRING		_("Write: ")
#define IORATE_STRING			_("K/sec")
#define SHOW_MAX_PROCESSES		100
#define PROCESS_STRING_LENGTH	64

static int processId = 0;
static int privilege = 0;
static listItemParameters *processListParams = NULL;
static process *processes = NULL;
static int numProcesses = 0;
static objectKey window = NULL;
static objectKey memoryBlocksLabel = NULL;
static objectKey memoryUsedLabel = NULL;
static objectKey memoryFreeLabel = NULL;
static objectKey diskPerfLabel = NULL;
static objectKey diskReadPerfLabel = NULL;
static objectKey diskWritePerfLabel = NULL;
static objectKey processList = NULL;
static objectKey showThreadsCheckbox = NULL;
static objectKey runProgramButton = NULL;
static objectKey setPriorityButton = NULL;
static objectKey killProcessButton = NULL;
static int showThreads = 1;
static int stop = 0;


__attribute__((format(printf, 1, 2)))
static void error(const char *format, ...)
{
	// Generic error message code for either text or graphics modes

	va_list list;
	char output[MAXSTRINGLENGTH];

	va_start(list, format);
	vsnprintf(output, MAXSTRINGLENGTH, format, list);
	va_end(list);

	windowNewErrorDialog(window, _("Error"), output);
}


static void sortChildren(process *tmpProcessArray, int tmpNumProcesses)
{
	// (Recursively) sort any children of the last process in the process list
	// from the temporary list into our regular list.

	process *parent = NULL;
	int count;

	if (!numProcesses)
		// No parent to sort children for.
		return;

	parent = &processes[numProcesses - 1];

	for (count = 0; count < tmpNumProcesses; count ++)
	{
		if ((tmpProcessArray[count].name[0] == '\0') ||
			(tmpProcessArray[count].type != proc_thread) ||
			(tmpProcessArray[count].parentProcessId != parent->processId))
		{
			continue;
		}

		// Copy this thread into the regular array
		memcpy(&processes[numProcesses++], &tmpProcessArray[count],
			sizeof(process));
		tmpProcessArray[count].name[0] = '\0';

		// Now sort any children behind it.
		sortChildren(tmpProcessArray, tmpNumProcesses);
	}
}


static int getUpdate(void)
{
	// Get the list of processes from the kernel

	int status = 0;
	memoryStats memStats;
	char labelChar[32];
	// Memory stats
	unsigned totalFree = 0;
	int percentUsed = 0;
	// Disk stats
	diskStats dskStats;
	unsigned readPerf = 0;
	unsigned writePerf = 0;
	// Process info
	char *bufferPointer = NULL;
	process *tmpProcessArray = NULL;
	process *tmpProcess = NULL;
	int tmpNumProcesses = 0;
	int count;

	// Try to get the memory stats
	memset(&memStats, 0, sizeof(memoryStats));
	status = memoryGetStats(&memStats, 0);
	if (status >= 0)
	{
		// Switch raw bytes numbers to kilobytes.  This will also prevent
		// overflow when we calculate percentage, below.
		memStats.totalMemory >>= 10;
		memStats.usedMemory >>= 10;

		totalFree = (memStats.totalMemory - memStats.usedMemory);
		percentUsed = ((memStats.usedMemory * 100) / memStats.totalMemory);

		// Assign the strings to our labels
		if (memoryBlocksLabel)
		{
			sprintf(labelChar, "%s%d", USEDBLOCKS_STRING, memStats.usedBlocks);
			windowComponentSetData(memoryBlocksLabel, labelChar,
				strlen(labelChar), 1 /* redraw */);
		}
		if (memoryUsedLabel)
		{
			sprintf(labelChar, "%s%u Kb - %d%%", USEDMEM_STRING,
				memStats.usedMemory, percentUsed);
			windowComponentSetData(memoryUsedLabel, labelChar,
				strlen(labelChar), 1 /* redraw */);
		}
		if (memoryFreeLabel)
		{
			sprintf(labelChar, "%s%u Kb - %d%%", FREEMEM_STRING, totalFree,
				(100 - percentUsed));
			windowComponentSetData(memoryFreeLabel, labelChar,
				strlen(labelChar), 1 /* redraw */);
		}
	}

	status = diskGetStats(NULL, &dskStats);
	if (status >= 0)
	{
		// Convert ms times to seconds
		dskStats.readTimeMs /= 1000;
		dskStats.writeTimeMs /= 1000;

		if (!dskStats.readTimeMs)
			dskStats.readTimeMs = 1;

		if (!dskStats.writeTimeMs)
			dskStats.writeTimeMs = 1;

		readPerf = (dskStats.readKbytes / dskStats.readTimeMs);
		writePerf = (dskStats.writeKbytes / dskStats.writeTimeMs);

		if (diskReadPerfLabel)
		{
			sprintf(labelChar, "%s%u%s", READPERF_STRING, readPerf,
				IORATE_STRING);
			windowComponentSetData(diskReadPerfLabel, labelChar,
				strlen(labelChar), 1 /* redraw */);
		}

		if (diskWritePerfLabel)
		{
			sprintf(labelChar, "%s%u%s", WRITEPERF_STRING, writePerf,
				IORATE_STRING);
			windowComponentSetData(diskWritePerfLabel, labelChar,
				strlen(labelChar), 1 /* redraw */);
		}
	}

	tmpProcessArray = malloc(SHOW_MAX_PROCESSES * sizeof(process));
	if (!tmpProcessArray)
	{
		error("%s", _("Can't get temporary memory for processes"));
		return (status = ERR_MEMORY);
	}

	tmpNumProcesses = multitaskerGetProcesses(tmpProcessArray,
		(SHOW_MAX_PROCESSES * sizeof(process)));
	if (tmpNumProcesses < 0)
	{
		free(tmpProcessArray);
		return (tmpNumProcesses);
	}

	numProcesses = 0;

	// Sort the processes from our temporary array into our regular array
	// so that we are skipping threads, if applicable, and so that all children
	// follow their parents

	for (count = 0; count < tmpNumProcesses; count ++)
	{
		if ((tmpProcessArray[count].name[0] == '\0') ||
			(tmpProcessArray[count].type == proc_thread))
		{
			continue;
		}

		// Copy this process into the regular array
		memcpy(&processes[numProcesses++], &tmpProcessArray[count],
			sizeof(process));
		tmpProcessArray[count].name[0] = '\0';

		if (showThreads)
			// Now sort any children, grandchildren, etc., behind it.
			sortChildren(tmpProcessArray, tmpNumProcesses);
	}

	free(tmpProcessArray);

	for (count = 0; count < numProcesses; count ++)
	{
		memset(processListParams[count].text, ' ', WINDOW_MAX_LABEL_LENGTH);
		bufferPointer = processListParams[count].text;
		tmpProcess = &processes[count];

		sprintf(bufferPointer, "%s%s",
			((tmpProcess->type == proc_thread)? " - " : ""), tmpProcess->name);
		bufferPointer[strlen(bufferPointer)] = ' ';
		sprintf((bufferPointer + 26), "%d", tmpProcess->processId);
		bufferPointer[strlen(bufferPointer)] = ' ';
		sprintf((bufferPointer + 30), "%d", tmpProcess->parentProcessId);
		bufferPointer[strlen(bufferPointer)] = ' ';
		sprintf((bufferPointer + 35), "%d", tmpProcess->userId);
		bufferPointer[strlen(bufferPointer)] = ' ';
		sprintf((bufferPointer + 39), "%d", tmpProcess->priority);
		bufferPointer[strlen(bufferPointer)] = ' ';
		sprintf((bufferPointer + 43), "%d", tmpProcess->privilege);
		bufferPointer[strlen(bufferPointer)] = ' ';
		sprintf((bufferPointer + 48), "%d", tmpProcess->cpuPercent);
		bufferPointer[strlen(bufferPointer)] = ' ';

		// Get the state
		switch(tmpProcess->state)
		{
			case proc_running:
				strcpy((bufferPointer + 53), _("running "));
				break;
			case proc_ready:
			case proc_ioready:
				strcpy((bufferPointer + 53), _("ready "));
				break;
			case proc_waiting:
				strcpy((bufferPointer + 53), _("waiting "));
				break;
			case proc_sleeping:
				strcpy((bufferPointer + 53), _("sleeping "));
				break;
			case proc_stopped:
				strcpy((bufferPointer + 53), _("stopped "));
				break;
			case proc_finished:
				strcpy((bufferPointer + 53), _("finished "));
				break;
			case proc_zombie:
				strcpy((bufferPointer + 53), _("zombie "));
				break;
			default:
				strcpy((bufferPointer + 53), _("unknown "));
				break;
		}
	}

	return (status = 0);
}


static void runProgram(void)
{
	// Prompts the user for a program to run.

	int status = 0;
	char commandLine[MAX_PATH_NAME_LENGTH];
	char command[MAX_PATH_NAME_LENGTH];
	char fullCommand[MAX_PATH_NAME_LENGTH];
	int argc = 0;
	char *argv[64];
	int count;

	status = windowNewFileDialog(NULL, _("Enter command"),
		_("Please enter a command to run:"), PATH_PROGRAMS, commandLine,
		MAX_PATH_NAME_LENGTH, fileT, 0 /* no thumbnails */);
	if (status != 1)
		goto out;

	// Turn the command line into a program and args
	status = vshParseCommand(commandLine, command, &argc, argv);
	if (status < 0)
		goto out;
	if (command[0] == '\0')
	{
		status = ERR_NOSUCHFILE;
		goto out;
	}

	// Got an executable command.  Execute it.

	strcpy(fullCommand, command);
	strcat(fullCommand, " ");
	for (count = 1; count < argc; count ++)
	{
		strcat(fullCommand, argv[count]);
		strcat(fullCommand, " ");
	}

	status = loaderLoadAndExec(fullCommand, privilege, 0);

out:
	if (status < 0)
		error("%s", _("Unable to execute program"));
	multitaskerTerminate(status);
}


static int getNumberDialog(const char *title, const char *prompt)
{
	// Creates a dialog that prompts for a number value

	int status = 0;
	char buffer[11];

	status = windowNewPromptDialog(window, title, prompt, 1, 10, buffer);
	if (status < 0)
		return (status);

	if (buffer[0] == '\0')
		return (status = ERR_NODATA);

	// Try to turn it into a number
	buffer[10] = '\0';
	status = atoi(buffer);
	return (status);
}


static int setPriority(int whichProcess)
{
	// Set a new priority on a process

	int status = 0;
	int newPriority = 0;
	process *changeProcess = NULL;

	// Get the process to change
	changeProcess = &processes[whichProcess];

	newPriority = getNumberDialog(_("Set priority"),
		_("Please enter the desired priority"));
	if (newPriority < 0)
		return (newPriority);

	status = multitaskerSetProcessPriority(changeProcess->processId,
		newPriority);

	// Refresh our list of processes
	getUpdate();
	windowComponentSetData(processList, processListParams, numProcesses,
		1 /* redraw */);

	return (status);
}


static int killProcess(int whichProcess)
{
	// Tells the kernel to kill the requested process

	int status = 0;
	process *theProcess = NULL;

	// Get the process to kill
	theProcess = &processes[whichProcess];

	status = multitaskerKillProcess(theProcess->processId, 0);

	// Refresh our list of processes
	getUpdate();
	windowComponentSetData(processList, processListParams, numProcesses,
		1 /* redraw */);

	return (status);
}


static void refreshWindow(void)
{
	// We got a 'window refresh' event (probably because of a language
	// switch), so we need to update things

	// Re-get the language setting
	setlocale(LC_ALL, getenv(ENV_LANG));
	textdomain("progman");

	// Re-get the character set
	if (getenv(ENV_CHARSET))
		windowSetCharSet(window, getenv(ENV_CHARSET));

	// Refresh the 'disk performance' label
	windowComponentSetData(diskPerfLabel, DISKPERF_STRING,
		strlen(DISKPERF_STRING), 1 /* redraw */);

	// Refresh the window title
	windowSetTitle(window, WINDOW_TITLE);
}


static void eventHandler(objectKey key, windowEvent *event)
{
	int processNumber = 0;

	// Check for window events.
	if (key == window)
	{
		// Check for window refresh
		if (event->type == EVENT_WINDOW_REFRESH)
			refreshWindow();

		// Check for the window being closed
		else if (event->type == EVENT_WINDOW_CLOSE)
		{
			stop = 1;
			windowGuiStop();
			windowDestroy(window);
		}
	}

	else if ((key == showThreadsCheckbox) && (event->type & EVENT_SELECTION))
	{
		windowComponentGetSelected(showThreadsCheckbox, &showThreads);
		getUpdate();
		windowComponentSetData(processList, processListParams, numProcesses,
			1 /* redraw */);
	}

	else
	{
		windowComponentGetSelected(processList, &processNumber);
		if (processNumber < 0)
			return;

		if ((key == runProgramButton) && (event->type == EVENT_MOUSE_LEFTUP))
		{
			if (multitaskerSpawn(&runProgram, "run program", 0, NULL) < 0)
				error("%s", _("Unable to launch file dialog"));
		}

		else if ((key == setPriorityButton) &&
			(event->type == EVENT_MOUSE_LEFTUP))
		{
			setPriority(processNumber);
		}

		else if ((key == killProcessButton) &&
			(event->type == EVENT_MOUSE_LEFTUP))
		{
			killProcess(processNumber);
		}
	}
}


static void constructWindow(void)
{
	// If we are in graphics mode, make a window rather than operating on the
	// command line.

	componentParameters params;
	int containersGridY = 0;
	objectKey container = NULL;
	char tmp[80];

	// Create a new window
	window = windowNew(processId, WINDOW_TITLE);
	if (!window)
		return;

	memset(&params, 0, sizeof(componentParameters));
	params.gridWidth = 1;
	params.gridHeight = 1;
	params.padLeft = 5;
	params.padRight = 5;
	params.padTop = 5;
	params.orientationX = orient_left;
	params.orientationY = orient_top;
	params.flags = WINDOW_COMPFLAG_FIXEDHEIGHT;
	params.font = fontGet(FONT_FAMILY_ARIAL, FONT_STYLEFLAG_BOLD, 10, NULL);

	// A container for the memory and disk statistics
	container = windowNewContainer(window, "stats", &params);

	params.padLeft = params.padRight = params.padTop = 0;
	params.flags &= ~WINDOW_COMPFLAG_FIXEDHEIGHT;
	sprintf(tmp, "%sXXX", USEDBLOCKS_STRING);
	memoryBlocksLabel = windowNewTextLabel(container, tmp, &params);

	params.gridY += 1;
	params.padTop = 5;
	sprintf(tmp, "%sXXXXXXX Kb - XX%%", USEDMEM_STRING);
	memoryUsedLabel = windowNewTextLabel(container, tmp, &params);

	params.gridY += 1;
	sprintf(tmp, "%sXXXXXXX Kb - XX%%", FREEMEM_STRING);
	memoryFreeLabel = windowNewTextLabel(container, FREEMEM_STRING, &params);

	params.gridX += 1;
	params.gridY = 0;
	params.padLeft = 20;
	params.padTop = 0;
	diskPerfLabel = windowNewTextLabel(container, DISKPERF_STRING, &params);

	params.gridY += 1;
	params.padTop = 5;
	sprintf(tmp, "%sXXXX%s", READPERF_STRING, IORATE_STRING);
	diskReadPerfLabel = windowNewTextLabel(container, tmp, &params);

	params.gridY += 1;
	sprintf(tmp, "%sXXXX%s", WRITEPERF_STRING, IORATE_STRING);
	diskWritePerfLabel = windowNewTextLabel(container, tmp, &params);

	// Create the label of column headers for the list below
	params.gridX = 0;
	params.gridY = ++containersGridY;
	params.padLeft = params.padTop = params.padRight = 5;
	params.font = fontGet(FONT_FAMILY_LIBMONO, FONT_STYLEFLAG_FIXED, 8, NULL);
	windowNewTextLabel(window, _("Process                   "
		"PID PPID UID Pri Priv CPU% STATE   "), &params);

	// A container for the process list
	params.gridY = ++containersGridY;
	params.padRight = 0;
	params.padBottom = 5;
	container = windowNewContainer(window, "processes", &params);

	// Create the list of processes
	params.gridY = 0;
	params.padLeft = params.padTop = params.padBottom = 0;
	processList = windowNewList(container, windowlist_textonly, 20, 1, 0,
		processListParams, numProcesses, &params);
	windowComponentFocus(processList);

	// Create a 'show sub-processes' checkbox
	params.gridY += 1;
	params.padTop = 5;
	params.font = NULL;
	showThreadsCheckbox = windowNewCheckbox(container,
		_("Show all sub-processes"), &params);
	windowComponentSetSelected(showThreadsCheckbox, 1);
	windowRegisterEventHandler(showThreadsCheckbox, &eventHandler);

	// Make a container for the right hand side components
	params.gridX += 1;
	params.gridY = containersGridY;
	params.padLeft = params.padRight = params.padBottom = 5;
	params.flags |= (WINDOW_COMPFLAG_FIXEDWIDTH |
		WINDOW_COMPFLAG_FIXEDHEIGHT);
	container = windowNewContainer(window, "buttons", &params);

	// Create a 'run program' button
	params.gridX = 0;
	params.gridY = 0;
	params.padLeft = params.padRight = params.padTop = params.padBottom = 0;
	params.flags &= ~WINDOW_COMPFLAG_FIXEDWIDTH;
	runProgramButton = windowNewButton(container, _("Run program"), NULL,
		&params);
	windowRegisterEventHandler(runProgramButton, &eventHandler);

	// Create a 'set priority' button
	params.gridY += 1;
	params.padTop = 5;
	setPriorityButton = windowNewButton(container, _("Set priority"), NULL,
		&params);
	windowRegisterEventHandler(setPriorityButton, &eventHandler);

	// Create a 'kill process' button
	params.gridY += 1;
	killProcessButton = windowNewButton(container, _("Kill process"), NULL,
		&params);
	windowRegisterEventHandler(killProcessButton, &eventHandler);

	// Register an event handler to catch window close events
	windowRegisterEventHandler(window, &eventHandler);

	windowSetVisible(window, 1);
}


int main(int argc, char *argv[])
{
	int status = 0;
	int guiThreadPid = 0;

	setlocale(LC_ALL, getenv(ENV_LANG));
	textdomain("progman");

	// Only work in graphics mode
	if (!graphicsAreEnabled())
	{
		fprintf(stderr, _("\nThe \"%s\" command only works in graphics "
			"mode\n"), (argc? argv[0] : ""));
		return (status = ERR_NOTINITIALIZED);
	}

	processId = multitaskerGetCurrentProcessId();
	privilege = multitaskerGetProcessPrivilege(processId);

	// Get a buffer for process structures
	processes = malloc(SHOW_MAX_PROCESSES * sizeof(process));
	// Get an array of list parameters structures for process strings
	processListParams = malloc(SHOW_MAX_PROCESSES * sizeof(listItemParameters));

	if (!processes || !processListParams)
	{
		if (processes)
			free(processes);
		if (processListParams)
			free(processListParams);
		error("%s", _("Error getting memory"));
		return (status = ERR_MEMORY);
	}

	// Get the list of process strings
	status = getUpdate();
	if (status < 0)
	{
		free(processes);
		free(processListParams);
		errno = status;
		if (argc)
			perror(argv[0]);
		return (status);
	}

	// Make our window
	constructWindow();

	// Run the GUI
	guiThreadPid = windowGuiThread();

	while (!stop && multitaskerProcessIsAlive(guiThreadPid))
	{
		if (getUpdate() < 0)
			break;
		windowComponentSetData(processList, processListParams, numProcesses,
			1 /* redraw */);
		sleep(1);
	}

	free(processes);
	free(processListParams);
	return (status = 0);
}

