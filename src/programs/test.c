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
//  test.c
//

// This is a test driver program.

#include <ctype.h>
#include <dlfcn.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/api.h>
#include <sys/paths.h>
#include <sys/processor.h>
#include <sys/vsh.h>

// Tests should save/restore the text screen if they (deliberately) spew
// output or errors.
static textScreen screen;

#define MAXFAILMSG 80
static char failMsg[MAXFAILMSG];
#define FAILMSG(message, arg...) \
	snprintf(failMsg, MAXFAILMSG, message, ##arg)


static int format_strings(void)
{
	// Tests the C library's handling of printf/scanf -style format strings

	int status = 0;
	char format[80];
	char buff[80];
	unsigned long long val[2];
	char charVal[2];
	char stringVal[8];
	int typeCount, widthCount, count;

	struct {
		const char *spec;
		int bits;
		int sign;
	} types[] = {
		{ "d", 32, 1 },
		{ "lld", 64, 1 },
		{ "u", 32, 0 },
		{ "llu", 64, 0 },
		{ "o", 32, 1 },
		{ "llo", 64, 1 },
		{ "p", 32, 0 },
		{ "x", 32, 0 },
		{ "X", 32, 0 },
		{ NULL, 0, 0, }
	};

	for (typeCount = 0; types[typeCount].spec; typeCount ++)
	{
		for (widthCount = 0; widthCount < 4; widthCount ++)
		{
			strcpy(format, "foo %");

			if (widthCount == 3)
				strcat(format, "-");

			if (widthCount == 2)
				strcat(format, "0");

			if (widthCount > 0)
			{
				if (types[typeCount].bits == 32)
					strcat(format, "8");
				else if (types[typeCount].bits == 64)
					strcat(format, "16");
			}

			strcat(format, types[typeCount].spec);
			strcat(format, " bar");

			for (count = 0; count < 100; count ++)
			{
				val[0] = (unsigned long long) randomUnformatted();
				if (types[typeCount].bits == 64)
				{
					val[0] <<= 32;
					val[0] |= (unsigned long long) randomUnformatted();
				}

				if (types[typeCount].bits == 32)
					status = snprintf(buff, 80, format, (unsigned) val[0]);
				else
					status = snprintf(buff, 80, format, val[0]);

				if (status < 0)
				{
					FAILMSG("Error expanding \"%s\" format", format);
					goto out;
				}

				val[1] = 0;
				if (types[typeCount].bits == 32)
					status = sscanf(buff, format, (unsigned *) &val[1]);
				else if (types[typeCount].bits == 64)
					status = sscanf(buff, format, &val[1]);

				if (status < 0)
				{
					FAILMSG("Error code %d while reading \"%s\" input",
						status, format);
					goto out;
				}

				if (status != 1)
				{
					FAILMSG("Couldn't read \"%s\" input", format);
					status = ERR_INVALID;
					goto out;
				}

				if (val[1] != val[0])
				{
					if (types[typeCount].sign)
					{
						FAILMSG("\"%s\" output %lld does not match input "
							"%lld", format, val[1], val[0]);
					}
					else
					{
						FAILMSG("\"%s\" output %llu does not match input "
							"%llu", format, val[1], val[0]);
					}
					status = ERR_INVALID;
					goto out;
				}
			}
		}
	}

	for (count = 0; count < 100; count ++)
	{
		// Test character output first, then input, by reading back the output
		charVal[0] = randomFormatted(' ', '~');
		strcpy(format, "%c");

		status = snprintf(buff, 80, format, charVal[0]);
		if (status < 0)
		{
			FAILMSG("Error expanding char format");
			goto out;
		}

		status = sscanf(buff, format, &charVal[1]);
		if (status < 0)
		{
			FAILMSG("Error formatting char input");
			goto out;
		}
		if (status != 1)
		{
			FAILMSG("Error formatting char input");
			status = ERR_INVALID;
			goto out;
		}
		if (charVal[0] != charVal[1])
		{
			FAILMSG("Char output '%c' does not match input '%c'", charVal[1],
				charVal[0]);
			status = ERR_INVALID;
			goto out;
		}
	}

	// Test string output first, then input, by reading back the output
	strcpy(format, "%s");

	status = snprintf(buff, 80, format, "FOOBAR!");
	if (status < 0)
	{
		FAILMSG("Error expanding string format");
		goto out;
	}

	status = sscanf(buff, format, stringVal);
	if (status < 0)
	{
		FAILMSG("Error formatting string input");
		goto out;
	}
	if (status != 1)
	{
		FAILMSG("Error formatting string input");
		status = ERR_INVALID;
		goto out;
	}
	if (strcmp(stringVal, "FOOBAR!"))
	{
		FAILMSG("String output %s does not match input %s",
			stringVal, "FOOBAR!");
		status = ERR_INVALID;
		goto out;
	}

	status = 0;

out:
	return (status);
}


static int crashThread(void)
{
	// This deliberately causes a divide-by-zero exception.

	int a = 1;
	int b = 0;

	if (a / b)
		multitaskerYield();

	while (1);

	exit(0);
}


static int exceptions(void)
{
	// Tests the kernel's exception handing.

	int status = 0;
	int procId = 0;
	int count = 0;

	// Save the current text screen
	status = textScreenSave(&screen);
	if (status < 0)
		goto out;

	for (count = 0; count < 10; count ++)
	{
		procId = multitaskerSpawn(&crashThread, "crashy thread", 0, NULL);
		if (procId < 0)
		{
			FAILMSG("Couldn't spawn exception-causing process");
			status = procId;
			goto out;
		}

		// Let it run
		multitaskerYield();
		multitaskerYield();

		if (graphicsAreEnabled())
		{
			// Try to get rid of any exception dialogs we caused
			multitaskerKillByName("error dialog thread", 0);
		}

		// Now it should be dead
		if (multitaskerProcessIsAlive(procId))
		{
			FAILMSG("Kernel did not kill exception-causing process");
			status = ERR_INVALID;
			goto out;
		}
	}

	status = 0;

out:
	// Restore the text screen
	textScreenRestore(&screen);

	return (status);
}


static int text_output(void)
{
	// Does a bunch of text-output testing.

	int status = 0;
	int columns = 0;
	int rows = 0;
	int screenFulls = 0;
	int bufferSize = 0;
	char *buffer = NULL;
	int length = 0;
	int count;

	// Save the current text screen
	status = textScreenSave(&screen);
	if (status < 0)
		goto out;

	columns = textGetNumColumns();
	if (columns < 0)
	{
		status = columns;
		goto out;
	}

	rows = textGetNumRows();
	if (rows < 0)
	{
		status = rows;
		goto out;
	}

	// How many screenfulls of data?
	screenFulls = 5;

	// Allocate a buffer
	bufferSize = (columns * rows * screenFulls);
	buffer = malloc(bufferSize);
	if (!buffer)
	{
		FAILMSG("Error getting memory");
		status = ERR_MEMORY;
		goto out;
	}

	// Fill it with random printable data
	for (count = 0; count < bufferSize; count ++)
	{
		buffer[count] = (char) randomFormatted(32, 126);

		// We don't want '%' format characters 'cause that could cause
		// unpredictable results
		if (buffer[count] == '%')
		{
			count -= 1;
			continue;
		}
	}

	// Randomly sprinkle newlines and tabs
	for (count = 0; count < (screenFulls * 10); count ++)
	{
		buffer[randomFormatted(0, (bufferSize - 1))] = '\n';
		buffer[randomFormatted(0, (bufferSize - 1))] = '\t';
	}

	// Print the buffer
	for (count = 0; count < bufferSize; count ++)
	{
		if (buffer[count] == (char) 9)
		{
			// Tab character
			status = textTab();
			if (status < 0)
				goto out;
		}
		else if (buffer[count] == (char) 10)
		{
			// Newline character
			textNewline();
		}
		else
		{
			status = textPutc(buffer[count]);
			if (status < 0)
				goto out;
		}
	}

	// Stick in a bunch of NULLs
	for (count = 0; count < (screenFulls * 30); count ++)
		buffer[randomFormatted(0, (bufferSize - 1))] = '\0';

	buffer[bufferSize - 1] = '\0';

	// Print the buffer again as lines
	for (count = 0; count < bufferSize; )
	{
		length = strlen(buffer + count);
		if (length < 0)
		{
			// Maybe we made a string that's too long
			buffer[count + MAXSTRINGLENGTH - 1] = '\0';

			length = strlen(buffer + count);
			if (length < 0)
			{
				status = length;
				goto out;
			}
		}

		status = textPrintLine(buffer + count);
		if (status < 0)
			goto out;

		if (length)
			count += length;
		else
			count += 1;
	}

	sleep(3);
	status = 0;

out:
	if (buffer)
		free(buffer);

	// Restore the text screen
	textScreenRestore(&screen);

	return (status);
}


static int text_colors(void)
{
	// Tests the setting/printing of text colors.

	int status = 0;
	int columns = 0;
	color foreground;
	char *buffer = NULL;
	color tmpColor;
	textAttrs attrs;
	int count1, count2;

	color allColors[16] = {
		COLOR_BLACK, COLOR_BLUE, COLOR_GREEN, COLOR_CYAN, COLOR_RED,
		COLOR_MAGENTA, COLOR_BROWN, COLOR_LIGHTGRAY, COLOR_DARKGRAY,
		COLOR_LIGHTBLUE, COLOR_LIGHTGREEN, COLOR_LIGHTCYAN, COLOR_LIGHTRED,
		COLOR_LIGHTMAGENTA, COLOR_YELLOW, COLOR_WHITE
	};

	// Save the current text screen
	status = textScreenSave(&screen);
	if (status < 0)
		goto out;

	columns = textGetNumColumns();
	if (columns < 0)
	{
		status = columns;
		goto out;
	}

	// Save the current foreground color
	status = textGetForeground(&foreground);
	if (status < 0)
		goto out;

	buffer = malloc(columns);
	if (!buffer)
	{
		FAILMSG("Error getting memory");
		status = ERR_MEMORY;
		goto out;
	}

	for (count1 = 0; count1 < (columns - 1); count1 ++)
		buffer[count1] = '#';

	buffer[columns - 1] = '\0';

	textNewline();

	for (count1 = 0; count1 < 16; count1 ++)
	{
		status = textSetForeground(&allColors[count1]);
		if (status < 0)
		{
			FAILMSG("Failed to set the foreground color");
			goto out;
		}

		status = textGetForeground(&tmpColor);
		if (status < 0)
		{
			FAILMSG("Failed to get the foreground color");
			goto out;
		}

		if (memcmp(&allColors[count1], &tmpColor, sizeof(color)))
		{
			FAILMSG("Foreground color not set correctly");
			status = ERR_INVALID;
			goto out;
		}

		textPrintLine(buffer);
	}

	for (count1 = 0; count1 < 16; count1 ++)
	{
		for (count2 = 0; count2 < 16; count2 ++)
		{
			memset(&attrs, 0, sizeof(textAttrs));
			attrs.flags = (TEXT_ATTRS_FOREGROUND | TEXT_ATTRS_BACKGROUND);
			memcpy(&attrs.foreground, &allColors[count1], sizeof(color));
			memcpy(&attrs.background, &allColors[count2], sizeof(color));
			textPrintAttrs(&attrs, buffer);
			textPrintLine("");
		}
	}

	textNewline();

	sleep(3);
	status = 0;

out:
	if (buffer)
		free(buffer);

	// Restore old foreground color
	textSetForeground(&foreground);

	// Restore the text screen
	textScreenRestore(&screen);

	return (status);
}


static int xtra_chars(void)
{
	// Just print out the 'extended ASCII' (actually ISO-8859-15) characters
	// so we can see whether or not they're appearing properly in our fonts

	int status = 0;
	int graphics = graphicsAreEnabled();
	objectKey window = NULL;
	componentParameters params;
	char lineBuffer[80];
	int count;

	// Save the current text screen
	status = textScreenSave(&screen);
	if (status < 0)
		goto out;

	if (graphics)
	{
		// Create a new window
		window = windowNew(multitaskerGetCurrentProcessId(), "Xtra chars "
			"test window");
		if (!window)
		{
			FAILMSG("Error getting window");
			status = ERR_NOTINITIALIZED;
			goto out;
		}

		memset(&params, 0, sizeof(componentParameters));
		params.gridWidth = 1;
		params.gridHeight = 1;
		params.padLeft = 5;
		params.padRight = 5;
		params.padTop = 5;
		params.padBottom = 5;
		params.orientationX = orient_center;
		params.orientationY = orient_middle;
		params.font = fontGet(FONT_FAMILY_XTERM, FONT_STYLEFLAG_NORMAL, 10,
			NULL);
		if (!params.font)
		{
			FAILMSG("Error %d getting font", status);
			goto out;
		}
	}

	printf("\n");

	lineBuffer[0] = '\0';
	for (count = 160; count <= 255; count ++)
	{
		sprintf((lineBuffer + strlen(lineBuffer)), "%d='%c' ", count, count);
		if (!((count + 1) % 8))
		{
			printf("%s\n", lineBuffer);

			if (window)
			{
				windowNewTextLabel(window, lineBuffer, &params);
				params.gridY += 1;
			}

			lineBuffer[0] = '\0';
		}
	}

	printf("\n");

	if (window)
	{
		status = windowSetVisible(window, 1);
		if (status < 0)
		{
			FAILMSG("Error %d setting window visible", status);
			goto out;
		}
	}

	sleep(3);
	status = 0;

out:
	// Restore the text screen
	textScreenRestore(&screen);

	if (window)
		windowDestroy(window);

	return (status);
}


static int port_io(void)
{
	// Test IO ports & related stuff!  Davide Airaghi

	int pid = 0;
	int setClear = 0;
	unsigned portN = 0;
	int res = 0;
	unsigned char ch;
	int count;

	pid = multitaskerGetCurrentProcessId();
	if (pid < 0)
	{
		FAILMSG("Error %d getting PID", res);
		goto fail;
	}

	for (count = 0; count < 65535; count ++)
	{
		portN = randomFormatted(0, 65535);
		setClear = ((rand() % 2) == 0);

		res = multitaskerSetIoPerm(pid, portN, setClear);
		if (res < 0)
		{
			FAILMSG("Error %d setting perms on port %d", res, portN);
			goto fail;
		}

		if (setClear)
			// Read from the port
			processorInPort8(portN, ch);

		// Clear permissions again
		res = multitaskerSetIoPerm(pid, portN, 0);
		if (res < 0)
		{
			FAILMSG("Error %d clearing perms on port %d", res, portN);
			goto fail;
		}
	}

	return (0);

fail:
	return (-1);
}


static int disk_reads(disk *theDisk)
{
	int status = 0;
	unsigned startSector = 0;
	unsigned numSectors = 0;
	unsigned char *buffer = NULL;
	int count;

	printf("\nTest reads from disk %s, numSectors %llu ", theDisk->name,
		theDisk->numSectors);

	for (count = 0; count < 1024; count ++)
	{
		numSectors = randomFormatted(1, min(theDisk->numSectors, 512));
		startSector = randomFormatted(0, (theDisk->numSectors - numSectors -
			1));

		buffer = malloc(numSectors * theDisk->sectorSize);
		if (!buffer)
		{
			FAILMSG("Error getting %u bytes disk %s buffer memory",
				(numSectors * theDisk->sectorSize), theDisk->name);
			return (status = ERR_MEMORY);
		}

		status = diskReadSectors(theDisk->name, startSector, numSectors,
			buffer);

		free(buffer);

		if (status < 0)
		{
			FAILMSG("Error %d reading %u sectors at %u on %s", status,
				numSectors, startSector, theDisk->name);
			return (status);
		}
	}

	return (status = 0);
}


static int disk_io(void)
{
	// Test disk IO reads

	int status = 0;
	char diskName[DISK_MAX_NAMELENGTH];
	disk theDisk;
	int count = 0;

	// Save the current text screen
	status = textScreenSave(&screen);
	if (status < 0)
		goto fail;

	// Get the logical boot disk name
	status = diskGetBoot(diskName);
	if (status < 0)
	{
		FAILMSG("Error %d getting disk name", status);
		goto fail;
	}

	for (count = 0; count < (DISK_MAX_PARTITIONS + 1); count ++)
	{
		// Take off any partition letter, so that we have the name of the
		// whole physical disk.
		if (isalpha(diskName[strlen(diskName) - 1]))
			diskName[strlen(diskName) - 1] = '\0';

		if (count)
		{
			// Add a partition letter
			sprintf((diskName + strlen(diskName)), "%c", ('a' + count - 1));
		}

		// Get the disk info
		status = diskGet(diskName, &theDisk);
		if (status < 0)
		{
			// No such disk.  Fine.
			break;
		}

		// Do random reads
		status = disk_reads(&theDisk);
		if (status < 0)
			goto fail;
	}

	status = 0;

fail:
	// Restore the text screen
	textScreenRestore(&screen);

	return (status);
}


static int file_recurse(const char *dirPath, unsigned startTime)
{
	int status = 0;
	file theFile;
	int numFiles = 0;
	int fileNum = 0;
	char relPath[MAX_PATH_NAME_LENGTH];
	unsigned op = 0;
	char newPath[MAX_PATH_NAME_LENGTH];
	int count;

	// Initialize the file structure
	memset(&theFile, 0, sizeof(file));

	// Loop through the contents of the directory
	while (rtcUptimeSeconds() < (startTime + 30))
	{
		numFiles = fileCount(dirPath);
		if (numFiles <= 0)
		{
			FAILMSG("Error %d getting directory %s file count", numFiles,
				dirPath);
			return (numFiles);
		}

		if (numFiles <= 2)
			return (status = 0);

		fileNum = randomFormatted(2, (numFiles - 1));
		for (count = 0; count <= fileNum; count ++)
		{
			if (!count)
			{
				// Get the first item in the directory
				status = fileFirst(dirPath, &theFile);
				if (status < 0)
				{
					FAILMSG("Error %d finding first file in %s", status,
						dirPath);
					return (status);
				}
			}
			else
			{
				status = fileNext(dirPath, &theFile);
				if (status < 0)
				{
					FAILMSG("Error %d finding next file after %s in %s",
						status, theFile.name, dirPath);
					return (status);
				}
			}
		}

		// Construct the relative pathname for this item
		sprintf(relPath, "%s/%s", dirPath, theFile.name);

		// And a new one in case we want to move/rename it
		strncpy(newPath, relPath, MAX_PATH_NAME_LENGTH);
		while (fileFind(newPath, NULL) >= 0)
		{
			snprintf(newPath, MAX_PATH_NAME_LENGTH, "%s/%c-%s", dirPath,
				randomFormatted(65, 90), theFile.name);
		}

		if (theFile.type == dirT)
		{
			// Randomly decide what type of operation to do to this diretory
			op = randomFormatted(0, 3);

			switch (op)
			{
				case 0:
				{
					if (numFiles < 4)
					{
						// Recursively copy it
						printf("Recursively copy %s to %s\n", relPath,
							newPath);
						status = fileCopyRecursive(relPath, newPath);
						if (status < 0)
						{
							FAILMSG("Error %d copying directory %s", status,
								relPath);
							return (status);
						}
					}

					break;
				}

				case 1:
				{
					if (numFiles > 4)
					{
						// Recursively delete it
						printf("Recursively delete %s\n", relPath);
						status = fileDeleteRecursive(relPath);
						if (status < 0)
						{
							FAILMSG("Error %d deleting directory %s", status,
								relPath);
							return (status);
						}
					}

					break;
				}

				case 2:
				{
					// Make a new directory
					printf("Create %s\n", newPath);
					status = fileMakeDir(newPath);
					if (status < 0)
					{
						FAILMSG("Error %d creating directory %s", status,
							newPath);
						return (status);
					}

					break;
				}

				case 3:
				{
					// Recursively process it the normal way
					status = file_recurse(relPath, startTime);
					if (status < 0)
						return (status);

					// Remove the directory.
					printf("Remove %s\n", relPath);
					status = fileDeleteRecursive(relPath);
					if (status < 0)
					{
						FAILMSG("Error %d removing directory %s", status,
							relPath);
						return (status);
					}

					break;
				}

				default:
				{
					FAILMSG("Unknown op %d for file %s", op, relPath);
					return (status = ERR_BUG);
				}
			}
		}
		else
		{
			// Randomly decide what type of operation to do to this file
			op = randomFormatted(0, 6);

			switch (op)
			{
				case 0:
				{
					// Just find the file
					status = fileFind(relPath, NULL);
					if (status < 0)
					{
						FAILMSG("Error %d finding file %s", status, relPath);
						return (status);
					}

					break;
				}

				case 1:
				{
					// Read and write the file using block IO
					printf("Read/write %s (block)\n", relPath);
					status = fileOpen(relPath, OPENMODE_READWRITE, &theFile);
					if (status < 0)
					{
						FAILMSG("Error %d opening file %s", status, relPath);
						return (status);
					}

					unsigned char *buffer = malloc(theFile.blocks *
						theFile.blockSize);
					if (!buffer)
					{
						FAILMSG("Couldn't get %u bytes memory for file %s",
							(theFile.blocks * theFile.blockSize), relPath);
						return (status = ERR_MEMORY);
					}

					status = fileRead(&theFile, 0, theFile.blocks, buffer);
					if (status < 0)
					{
						FAILMSG("Error %d reading file %s", status, relPath);
						free(buffer);
						return (status);
					}

					status = fileWrite(&theFile, 0, theFile.blocks, buffer);
					if (status < 0)
					{
						FAILMSG("Error %d writing file %s", status, relPath);
						free(buffer);
						return (status);
					}

					status = fileWrite(&theFile, theFile.blocks, 1, buffer);

					free(buffer);

					if (status < 0)
					{
						FAILMSG("Error %d rewriting file %s", status,
							relPath);
						return (status);
					}

					status = fileClose(&theFile);
					if (status < 0)
					{
						FAILMSG("Error %d closing file %s", status, relPath);
						return (status);
					}

					break;
				}

				case 2:
				{
					// Delete the file
					printf("Delete %s\n", relPath);
					status = fileDelete(relPath);
					if (status < 0)
					{
						FAILMSG("Error %d deleting file %s", status, relPath);
						return (status);
					}

					break;
				}

				case 3:
				{
					// Delete the file securely
					printf("Securely delete %s\n", relPath);
					status = fileDeleteSecure(relPath, 9);
					if (status < 0)
					{
						FAILMSG("Error %d securely deleting file %s", status,
							relPath);
						return (status);
					}

					break;
				}

				case 4:
				{
					// Copy the file
					printf("Copy %s to %s\n", relPath, newPath);
					status = fileCopy(relPath, newPath);
					if (status < 0)
					{
						FAILMSG("Error %d copying file %s to %s", status,
							relPath, newPath);
						return (status);
					}

					break;
				}

				case 5:
				{
					// Move the file
					printf("Move %s to %s\n", relPath, newPath);
					status = fileMove(relPath, newPath);
					if (status < 0)
					{
						FAILMSG("Error %d moving file %s to %s", status,
							relPath, newPath);
						return (status);
					}

					break;
				}

				case 6:
				{
					printf("Timestamp file %s\n", relPath);
					status = fileTimestamp(relPath);
					if (status < 0)
					{
						FAILMSG("Error %d timestamping file %s", status,
							relPath);
						return (status);
					}

					break;
				}

				default:
				{
					FAILMSG("Unknown op %d for file %s", op, relPath);
					return (status = ERR_BUG);
				}
			}
		}
	}

	// Timed out.
	return (status = 0);
}


static int file_ops(void)
{
	// Test filesystem operations

	int status = 0;
	unsigned startTime = 0;
	int count;

	char *useFiles[] = { PATH_PROGRAMS, PATH_SYSTEM, "/visopsys", NULL };
	#define DIRNAME "./test_tmp"

	// Save the current text screen
	status = textScreenSave(&screen);
	if (status < 0)
		goto fail;

	// If the test directory exists, delete it
	if (fileFind(DIRNAME, NULL) >= 0)
	{
		printf("Recursively delete %s\n", DIRNAME);
		status = fileDeleteRecursive(DIRNAME);
		if (status < 0)
		{
			FAILMSG("Error %d recursively deleting %s", status, DIRNAME);
			goto fail;
		}
	}

	status = fileMakeDir(DIRNAME);
	if (status < 0)
	{
		FAILMSG("Error %d creating test directory", status);
		goto fail;
	}

	startTime = rtcUptimeSeconds();
	while (rtcUptimeSeconds() < (startTime + 10))
	{
		for (count = 0; useFiles[count]; count ++)
		{
			char tmpName[MAX_PATH_NAME_LENGTH];
			sprintf(tmpName, "%s%s", DIRNAME, useFiles[count]);

			printf("Recursively copy %s to %s\n", useFiles[count], tmpName);
			status = fileCopyRecursive(useFiles[count], tmpName);
			if (status < 0)
			{
				FAILMSG("Error %d recursively copying files from %s", status,
					useFiles[count]);
				goto fail;
			}
		}

		// Now, recurse the directory, doing random file operations on the
		// contents.
		status = file_recurse(DIRNAME, startTime);
		if (status < 0)
			break;
	}

fail:
	if (fileFind(DIRNAME, NULL) >= 0)
	{
		printf("Recursively delete %s\n", DIRNAME);
		fileDeleteRecursive(DIRNAME);
	}

	// Restore the text screen
	textScreenRestore(&screen);

	return (status);
}


static int divide64(void)
{
	// Test 64-bit division

	struct {
		long long dividend;
		long long divisor;
		long long result;
		long long remainder;
	} array[] = {
		{ 0x99LL, 0x11LL, 0x9LL, 0x0LL },
		{ 0x99999999LL, 0x11111111LL, 0x9LL, 0x0LL },
		{ 0x999999999LL, 0x11111111LL, 0x90LL, 0x9LL },
		{ 0x999999999LL, 0x111111111LL, 0x9LL, 0x0LL },
		{ 0x4321432143214321LL, 0x1234123412341234LL, 0x3LL,
				0xC850C850C850C85LL },
		{ 0xF00F00F00F00LL, 0xABCABCABCLL, 0x165BLL, 0x72C72D62CLL },
		{ 0xF00F00F00F00LL, 0xF00F00F00LL, 0x1000LL, 0xF00LL },
		{ 0, 0, 0, 0 }
	};

	int status = 0;
	long long res = 0;
	long long rem = 0;
	int count;

	status = textScreenSave(&screen);
	if (status < 0)
	{
		FAILMSG("Error %d saving screen", status);
		goto out;
	}

	for (count = 0; array[count].dividend; count ++)
	{
		res = (array[count].dividend / array[count].divisor);
		rem = (array[count].dividend % array[count].divisor);

		if ((res != array[count].result) || (rem != array[count].remainder))
		{
			FAILMSG("%llx / %llx != %llx r %llx (%llx r %llx)",
				array[count].dividend, array[count].divisor,
				array[count].result, array[count].remainder, res, rem);
			status = ERR_INVALID;
			goto out;
		}
	}

	status = 0;

out:
	// Restore the text screen
	textScreenRestore(&screen);

	return (status);
}


static int sines(void)
{
	// Test the sin() and sinf() functions against some hard-coded values and
	// some random values.

	int status = 0;
	float rad = 0;
	float fres = 0;
	double dres = 0;
	int count;

	struct {
		float rad;
		float res;

	} farray[] = {
		{ -8.0, -0.989358246 },
		{ -7.0, -0.656986594 },
		{ -6.0, 0.279815882 },
		{ -5.0, 0.958932817 },
		{ -4.5, 0.977531254 },
		{ -4.0, 0.756802499 },
		{ -3.5, 0.350783199 },
		{ -3.0, -0.141119972 },
		{ -2.5, -0.598472118 },
		{ -2.0, -0.909297466 },
		{ -1.5, -0.997495055 },
		{ -1.0, -0.841470957 },
		{ -0.5, -0.479425519 },
		{ 0.5, 0.479425519 },
		{ 1.0, 0.84147095 },
		{ 1.5, 0.997495055 },
		{ 2.0, 0.909297466 },
		{ 2.5, 0.598472118 },
		{ 3.0, 0.141119972 },
		{ 3.5, -0.350783199 },
		{ 4.0, -0.756802499 },
		{ 4.5, -0.977531254 },
		{ 5.0, -0.958932817 },
		{ 6.0, -0.279815882 },
		{ 7.0, 0.656986594 },
		{ 8.0, 0.989358246 },
		{ 0.0, 0.0 }
	};

	struct {
		double rad;
		double res;

	} darray[] = {
		{ -8.0, -0.9893582466233817867 },
		{ -7.0, -0.6569865987187892825 },
		{ -6.0, 0.2794154980429556230 },
		{ -5.0, 0.9589242746625863393 },
		{ -4.5, 0.9775301176650759142 },
		{ -4.0, 0.7568024953079276465 },
		{ -3.5, 0.3507832276896200023 },
		{ -3.0, -0.1411200080598672135 },
		{ -2.5, -0.5984721441039563265 },
		{ -2.0, -0.9092974268256817094 },
		{ -1.5, -0.9974949866040545557 },
		{ -1.0, -0.8414709848078965049 },
		{ -0.5, -0.4794255386042030054 },
		{ 0.5, 0.4794255386042030054 },
		{ 1.0, 0.8414709848078965049 },
		{ 1.5, 0.9974949866040545557 },
		{ 2.0, 0.9092974268256817094 },
		{ 2.5, 0.5984721441039563265 },
		{ 3.0, 0.1411200080598672135 },
		{ 3.5, -0.3507832276896200023 },
		{ 4.0, -0.7568024953079276465 },
		{ 4.5, -0.9775301176650759142 },
		{ 5.0, -0.9589242746625863393 },
		{ 6.0, -0.2794154980429556230 },
		{ 7.0, 0.6569865987187892825 },
		{ 8.0, 0.9893582466233817867 },
		{ 0.0, 0.0 }
	};

	status = textScreenSave(&screen);
	if (status < 0)
	{
		FAILMSG("Error %d saving screen", status);
		goto out;
	}

	// Test some fixed values from the array above using floats
	for (count = 0; farray[count].res; count ++)
	{
		fres = sinf(farray[count].rad);
		if (fres != farray[count].res)
		{
			FAILMSG("Sine of float %f is incorrect (%f != %f)",
				farray[count].rad, fres, farray[count].res);
			status = ERR_INVALID;
			goto out;
		}
	}

	// Do a similar list using doubles
	for (count = 0; darray[count].res; count ++)
	{
		dres = sin(darray[count].rad);
		if (dres != darray[count].res)
		{
			FAILMSG("Sine of double %f is incorrect (%f != %f)",
				darray[count].rad, dres, darray[count].res);
			status = ERR_INVALID;
			goto out;
		}
	}

	// Test some additional random values to make sure they're in the correct
	// range.
	for (count = 0; count < 2000; count ++)
	{
		rad = (float) randomFormatted(5, 100);
		if (count % 2)
			rad *= -1.0;

		fres = sinf(rad);
		if ((fres < -1) || (fres == 0) || (fres > 1))
		{
			FAILMSG("Sine of %f is incorrect (%f)", rad, fres);
			status = ERR_INVALID;
			goto out;
		}
	}

	status = 0;

out:
	// Restore the text screen
	textScreenRestore(&screen);

	return (status);
}


static int cosines(void)
{
	// Test the cos() and cosf() functions against some hard-coded values and
	// some random values.

	int status = 0;
	float rad = 0;
	float fres = 0;
	double dres = 0;
	int count;

	struct {
		float rad;
		float res;

	} farray[] = {
		{ -8.0, -0.145499974 },
		{ -7.0, 0.753902254 },
		{ -6.0, 0.958777964 },
		{ -5.0, 0.283625454 },
		{ -4.5, -0.210800409 },
		{ -4.0, -0.653643906 },
		{ -3.5, -0.936456680 },
		{ -3.0, -0.989992439 },
		{ -2.5, -0.801143587 },
		{ -2.0, -0.416146845 },
		{ -1.5, 0.070737205 },
		{ -1.0, 0.540302277 },
		{ -0.5, 0.877582609 },
		{ 0.0, 1.000000000 },
		{ 0.5, 0.877582609 },
		{ 1.0, 0.540302277 },
		{ 1.5, 0.070737205 },
		{ 2.0, -0.416146845 },
		{ 2.5, -0.801143587 },
		{ 3.0, -0.989992439 },
		{ 3.5, -0.936456680 },
		{ 4.0, -0.653643906 },
		{ 4.5, -0.210800409 },
		{ 5.0, 0.283625454 },
		{ 6.0, 0.958777964 },
		{ 7.0, 0.753902254 },
		{ 8.0, -0.145499974 },
		{ 0.0, 0.0 }
	};

	struct {
		double rad;
		double res;

	} darray[] = {
		{ -8.0, -0.1455000338086137870 },
		{ -7.0, 0.7539022543433044892 },
		{ -6.0, 0.9601702874545081640 },
		{ -5.0, 0.2836621854666496179 },
		{ -4.5, -0.2107957994306340058 },
		{ -4.0, -0.6536436208636079431 },
		{ -3.5, -0.9364566872907962297 },
		{ -3.0, -0.9899924966004455255 },
		{ -2.5, -0.8011436155469335842 },
		{ -2.0, -0.4161468365471424069 },
		{ -1.5, 0.0707372016677029064 },
		{ -1.0, 0.5403023058681396534 },
		{ -0.5, 0.8775825618903727587 },
		{ 0.0, 1.0000000000000000000 },
		{ 0.5, 0.8775825618903727587 },
		{ 1.0, 0.5403023058681396534 },
		{ 1.5, 0.0707372016677029064 },
		{ 2.0, -0.4161468365471424069 },
		{ 2.5, -0.8011436155469335842 },
		{ 3.0, -0.9899924966004455255 },
		{ 3.5, -0.9364566872907962297 },
		{ 4.0, -0.6536436208636079431 },
		{ 4.5, -0.2107957994306340058 },
		{ 5.0, 0.2836621854666496179 },
		{ 6.0, 0.9601702874545081640 },
		{ 7.0, 0.7539022543433044892 },
		{ 8.0, -0.1455000338086137870 },
		{ 0.0, 0.0 }
	};

	status = textScreenSave(&screen);
	if (status < 0)
	{
		FAILMSG("Error %d saving screen", status);
		goto out;
	}

	// Test some fixed values from the array above using floats
	for (count = 0; farray[count].res; count ++)
	{
		fres = cosf(farray[count].rad);
		if (fres != farray[count].res)
		{
			FAILMSG("Cosine of float %f is incorrect (%f != %f)",
				farray[count].rad, fres, farray[count].res);
			status = ERR_INVALID;
			goto out;
		}
	}

	// Do a similar list using doubles
	for (count = 0; darray[count].res; count ++)
	{
		dres = cos(darray[count].rad);
		if (dres != darray[count].res)
		{
			FAILMSG("Cosine of double %f is incorrect (%f != %f)",
				darray[count].rad, dres, darray[count].res);
			status = ERR_INVALID;
			goto out;
		}
	}

	// Test some additional random values to make sure they're in the correct
	// range.
	for (count = 0; count < 2000; count ++)
	{
		rad = (float) randomFormatted(5, 100);
		if (count % 2)
			rad *= -1.0;

		fres = cosf(rad);
		if ((fres < -1) || (fres == 0) || (fres > 1))
		{
			FAILMSG("Cosine of %f is incorrect (%f)", rad, fres);
			status = ERR_INVALID;
			goto out;
		}
	}

	status = 0;

out:
	// Restore the text screen
	textScreenRestore(&screen);

	return (status);
}


static int floats(void)
{
	// Do calculations with floats (tests the kernel's FPU exception handling
	// and state saving).  Adapted from an early version of our JPEG
	// processing code.

	int coefficients[64];
	int u = 0;
	int v = 0;
	int x = 0;
	int y = 0;
	float tempValue = 0;
	float temp[64];
	int count;

	memset(coefficients, 0, (64 * sizeof(int)));
	memset(temp, 0, (64 * sizeof(float)));

	for (count = 0; count < 64; count ++)
		coefficients[count] = rand();

	for (count = 0; count < 1000; count ++)
	{
		for (x = 0; x < 8; x++)
		{
			for (y = 0; y < 8; y++)
			{
				for (u = 0; u < 8; u++)
				{
					for (v = 0; v < 8; v++)
					{
						tempValue = coefficients[(u * 8) + v] *
						cosf((2 * x + 1) * u * (float) M_PI / 16.0) *
						cosf((2 * y + 1) * v * (float) M_PI / 16.0);

						if (!u)
							tempValue *= (float) M_SQRT1_2;
						if (!v)
							tempValue *= (float) M_SQRT1_2;

						temp[(x * 8) + y] += tempValue;
					}
				}
			}
		}

		for (y = 0; y < 8; y++)
		{
			for (x = 0; x < 8; x++)
			{
				coefficients[(y * 8) + x] = (int)(temp[(y * 8) + x] / 4.0 +
					0.5);
				coefficients[(y * 8) + x] += 128;
			}
		}
	}

	// If we get this far without crashing, we're golden
	return (0);
}


static int libdl(void)
{
	int status = 0;
	char *libName = NULL;
	void *libHandle = NULL;
	char *symbolName = NULL;
	int (*fn)(const char *format, ...) = NULL;

	status = textScreenSave(&screen);
	if (status < 0)
	{
		FAILMSG("Error %d saving screen", status);
		goto out;
	}

	libName = "libc.so";
	libHandle = dlopen(libName, RTLD_NOW);
	if (!libHandle)
	{
		FAILMSG("Error getting library %s", libName);
		status = ERR_NODATA;
		goto out;
	}

	symbolName = "printf";
	fn = dlsym(libHandle, symbolName);
	if (!fn)
	{
		FAILMSG("Error getting library symbol %s", symbolName);
		status = ERR_NODATA;
		goto out;
	}

	fn("If you can read this, it works\n", libName, symbolName, fn);

	sleep(3);
	status = 0;

out:
	// Restore the text screen
	textScreenRestore(&screen);

	return (status);
}


static int randoms(void)
{
	int status = 0;
	unsigned numRandoms = 1000000;
	int val = 0;
	int evens = 0;
	int odds = 0;
	unsigned difference = 0;
	unsigned count;

	status = textScreenSave(&screen);
	if (status < 0)
	{
		FAILMSG("Error %d saving screen", status);
		goto out;
	}

	for (count = 0; count < numRandoms; count ++)
	{
		val = rand();

		if (val & 1)
			odds += 1;
		else
			evens += 1;
	}

	difference = abs(evens - odds);
	if (difference > (numRandoms / 10))
	{
		FAILMSG("Imbalance in evens (%d) and odds (%d) > 10%%", evens, odds);
		status = ERR_BADDATA;
		goto out;
	}

	status = 0;

out:
	// Restore the text screen
	textScreenRestore(&screen);

	return (status);
}


static int gui(void)
{
	int status = 0;
	int numListItems = 250;
	listItemParameters *listItemParams = NULL;
	objectKey window = NULL;
	componentParameters params;
	objectKey fileMenu = NULL;
	objectKey listList = NULL;
	objectKey buttonContainer = NULL;
	objectKey abcdButton = NULL;
	objectKey efghButton = NULL;
	objectKey ijklButton = NULL;
	int count1, count2, count3;

	#define FILEMENU_SAVE 0
	#define FILEMENU_QUIT 1
	static windowMenuContents fileMenuContents =
	{
		2,
		{
			{ "Save", NULL },
			{ "Quit", NULL }
		}
	};

	// Save the current text screen
	status = textScreenSave(&screen);
	if (status < 0)
	{
		FAILMSG("Error %d saving screen", status);
		goto out;
	}

	// Create a new window
	window = windowNew(multitaskerGetCurrentProcessId(), "GUI test window");
	if (!window)
	{
		FAILMSG("Error getting window");
		status = ERR_NOTINITIALIZED;
		goto out;
	}

	memset(&params, 0, sizeof(componentParameters));

	// Create the top 'file' menu
	objectKey menuBar = windowNewMenuBar(window, &params);
	if (!menuBar)
	{
		FAILMSG("Error getting menu bar");
		status = ERR_NOTINITIALIZED;
		goto out;
	}

	fileMenu = windowNewMenu(window, menuBar, "File", &fileMenuContents,
		&params);
	if (!fileMenu)
	{
		FAILMSG("Error getting menu");
		status = ERR_NOTINITIALIZED;
		goto out;
	}

	params.gridWidth = 1;
	params.gridHeight = 1;
	params.padLeft = 5;
	params.padTop = 5;
	params.padBottom = 5;
	params.orientationX = orient_center;
	params.orientationY = orient_middle;

	params.font = fontGet(FONT_FAMILY_XTERM, FONT_STYLEFLAG_NORMAL, 10, NULL);
	if (!params.font)
	{
		FAILMSG("Error %d getting font", status);
		goto out;
	}

	listItemParams = malloc(numListItems * sizeof(listItemParameters));
	if (!listItemParams)
	{
		FAILMSG("Error getting list parameters memory");
		status = ERR_MEMORY;
		goto out;
	}

	for (count1 = 0; count1 < numListItems; count1 ++)
	{
		for (count2 = 0; count2 < WINDOW_MAX_LABEL_LENGTH; count2 ++)
			listItemParams[count1].text[count2] = '#';

		listItemParams[count1].text[WINDOW_MAX_LABEL_LENGTH - 1] = '\0';
	}

	listList = windowNewList(window, windowlist_textonly, min(10,
		numListItems), 1, 0, listItemParams, numListItems, &params);
	if (!listList)
	{
		FAILMSG("Error getting list component");
		status = ERR_NOTINITIALIZED;
		goto out;
	}

	// Make a container component for the buttons
	params.gridX += 1;
	params.padRight = 5;
	params.orientationX = orient_left;
	params.orientationY = orient_top;
	params.flags |= WINDOW_COMPFLAG_FIXEDHEIGHT;
	params.font = NULL;
	buttonContainer = windowNewContainer(window, "buttonContainer", &params);
	if (!buttonContainer)
	{
		FAILMSG("Error getting button container");
		status = ERR_NOTINITIALIZED;
		goto out;
	}

	params.gridX = 0;
	params.gridY = 0;
	params.padLeft = 0;
	params.padRight = 0;
	params.padTop = 0;
	params.padBottom = 0;
	params.flags &= ~WINDOW_COMPFLAG_FIXEDHEIGHT;
	abcdButton = windowNewButton(buttonContainer, "ABCD", NULL, &params);
	if (!abcdButton)
	{
		FAILMSG("Error getting button");
		status = ERR_NOTINITIALIZED;
		goto out;
	}

	params.gridY += 1;
	params.padTop = 5;
	efghButton = windowNewButton(buttonContainer, "EFGH", NULL, &params);
	if (!efghButton)
	{
		FAILMSG("Error getting button");
		status = ERR_NOTINITIALIZED;
		goto out;
	}

	params.gridY += 1;
	ijklButton = windowNewButton(buttonContainer, "IJKL", NULL, &params);
	if (!ijklButton)
	{
		FAILMSG("Error getting button");
		status = ERR_NOTINITIALIZED;
		goto out;
	}

	status = windowSetVisible(window, 1);
	if (status < 0)
	{
		FAILMSG("Error %d setting window visible", status);
		goto out;
	}

	// Run a GUI thread just for fun
	status = windowGuiThread();
	if (status < 0)
	{
		FAILMSG("Error %d starting GUI thread", status);
		goto out;
	}

	// Do a bunch of random selections of the list
	for (count1 = 0; count1 < 100; count1 ++)
	{
		// Fill up our parameters with random printable text
		for (count2 = 0; count2 < numListItems; count2 ++)
		{
			int numChars = randomFormatted(1, (WINDOW_MAX_LABEL_LENGTH - 1));
			for (count3 = 0; count3 < numChars; count3 ++)
			{
				listItemParams[count2].text[count3] = (char)
					randomFormatted(32, 126);
			}

			listItemParams[count2].text[numChars] = '\0';
		}

		// Set the list data
		status = windowComponentSetData(listList, listItemParams,
			numListItems, 1 /* redraw */);
		if (status < 0)
		{
			FAILMSG("Error %d setting list component data", status);
			goto out;
		}

		for (count2 = 0; count2 < 10; count2 ++)
		{
			// Random selection, including illegal values
			int rnd = (randomFormatted(0, (numListItems * 3)) - numListItems);

			status = windowComponentSetSelected(listList, rnd);

			// See if the value we cooked up was supposed to be acceptable
			if ((rnd < -1) || (rnd >= numListItems))
			{
				// Illegal value, it should have failed.
				if (status >= 0)
				{
					FAILMSG("Selection value %d should fail", rnd);
					status = ERR_INVALID;
					goto out;
				}
			}
			else if (status < 0)
			{
				// Legal value, should have succeeded
				goto out;
			}
		}
	}

	windowGuiStop();

	status = windowDestroy(window);
	window = NULL;
	if (status < 0)
	{
		FAILMSG("Error %d destroying window", status);
		goto out;
	}

	status = 0;

out:
	if (listItemParams)
		free(listItemParams);

	if (window)
		windowDestroy(window);

	// Restore the text screen
	textScreenRestore(&screen);

	return (status);
}


static int icons(void)
{
	int status = 0;
	int numIcons = 0;
	file iconFile;
	char *fileName = NULL;
	image iconImage;
	objectKey window = NULL;
	componentParameters params;
	int count;

	memset(&iconFile, 0, sizeof(file));
	memset(&iconImage, 0, sizeof(image));
	memset(&params, 0, sizeof(componentParameters));

	// Save the current text screen
	status = textScreenSave(&screen);
	if (status < 0)
	{
		FAILMSG("Error %d saving screen", status);
		goto out;
	}

	fileName = malloc(MAX_PATH_NAME_LENGTH);
	if (!fileName)
	{
		FAILMSG("Error getting file name memory");
		status = ERR_MEMORY;
		goto out;
	}

	// Get the number of icons
	numIcons = fileCount(PATH_SYSTEM_ICONS);
	if (numIcons < 0)
	{
		FAILMSG("Error getting icon count");
		status = numIcons;
		goto out;
	}

	// Create a new window
	window = windowNew(multitaskerGetCurrentProcessId(), "Icon test window");
	if (!window)
	{
		FAILMSG("Error getting window");
		status = ERR_NOTINITIALIZED;
		goto out;
	}

	params.gridWidth = 1;
	params.gridHeight = 1;
	params.padLeft = 2;
	params.padRight = 2;
	params.padTop = 2;
	params.padBottom = 2;
	params.orientationX = orient_center;
	params.orientationY = orient_middle;
	params.flags |= WINDOW_COMPFLAG_CUSTOMBACKGROUND;
	params.background.red = 255;
	params.background.green = 255;
	params.background.blue = 255;

	for (count = 0; count < numIcons; count ++)
	{
		if (count)
			status = fileNext(PATH_SYSTEM_ICONS, &iconFile);
		else
			status = fileFirst(PATH_SYSTEM_ICONS, &iconFile);

		if (status < 0)
		{
			FAILMSG("Error getting next icon");
			goto out;
		}

		if (iconFile.type != fileT)
			continue;

		if (params.gridX >= 10)
		{
			params.gridX = 0;
			params.gridY += 1;
		}

		sprintf(fileName, "%s/%s", PATH_SYSTEM_ICONS, iconFile.name);

		// Try to load the image
		status = imageLoad(fileName, 0, 0, &iconImage);
		if (status < 0)
		{
			FAILMSG("Error loading icon image %s", fileName);
			goto out;
		}

		if (!windowNewIcon(window, &iconImage, iconFile.name, &params))
		{
			FAILMSG("Error creating icon component for %s", iconFile.name);
			status = ERR_NOCREATE;
			imageFree(&iconImage);
			goto out;
		}

		imageFree(&iconImage);

		params.gridX += 1;
	}

	status = windowSetBackgroundColor(window, &params.background);
	if (status < 0)
	{
		FAILMSG("Error %d setting window background color", status);
		goto out;
	}

	windowDebugLayout(window);

	status = windowSetVisible(window, 1);
	if (status < 0)
	{
		FAILMSG("Error %d showing window", status);
		goto out;
	}

	sleep(3);

	status = windowDestroy(window);
	window = NULL;
	if (status < 0)
	{
		FAILMSG("Error %d destroying window", status);
		goto out;
	}

	status = 0;

out:
	if (fileName)
		free(fileName);

	if (window)
		windowDestroy(window);

	// Restore the text screen
	textScreenRestore(&screen);

	return (status);
}


// This table describes all of the functions to run
struct {
	int (*function)(void);
	const char *name;
	int run;
	int graphics;

} functions[] = {
	// function			name				run graphics
	{ format_strings,	"format strings",	0,  0 },
	{ exceptions,		"exceptions",		0,  0 },
	{ text_output,		"text output",		0,  0 },
	{ text_colors,		"text colors",		0,  0 },
	{ xtra_chars,		"xtra chars",		0,  0 },
	{ port_io,			"port io",			0,  0 },
	{ disk_io,			"disk io",			0,  0 },
	{ file_ops,			"file ops",			0,  0 },
	{ divide64,			"divide64",			0,  0 },
	{ sines,			"sines",			0,  0 },
	{ cosines,			"cosines",			0,  0 },
	{ floats,			"floats",			0,  0 },
	{ libdl,			"libdl",			0,  0 },
	{ randoms,			"randoms",			0,  0 },
	{ gui,				"gui",				0,  1 },
	{ icons,			"icons",			0,  1 },
	{ NULL, NULL, 0, 0 }
};


static void begin(const char *name)
{
	printf("Testing %s... ", name);
	failMsg[0] = '\0';
}


static void pass(void)
{
	printf("passed\n");
}


static void fail(void)
{
	printf("failed");
	if (failMsg[0])
		printf("   [ %s ]", failMsg);
	printf("\n");
}


static int run(void)
{
	int errors = 0;
	int count;

	for (count = 0; functions[count].function ; count ++)
	{
		if (functions[count].run)
		{
			begin(functions[count].name);

			if (functions[count].function() >= 0)
			{
				pass();
			}
			else
			{
				fail();
				errors += 1;
			}
		}
	}

	return (errors);
}


static void usage(char *name)
{
	printf("usage:\n%s [-a] [-l] [test1] [test2] [...]\n", name);
	return;
}


int main(int argc, char *argv[])
{
	int graphics = graphicsAreEnabled();
	char opt;
	int testCount = 0;
	int errors = 0;
	int count1, count2;

	if (argc <= 1)
	{
		usage(argv[0]);
		return (-1);
	}

	// Check options
	while (strchr("al?", (opt = getopt(argc, argv, "al"))))
	{
		switch (opt)
		{
			case 'a':
			{
				// Run all tests
				for (count1 = 0; functions[count1].function ; count1 ++)
				{
					if (graphics || !functions[count1].graphics)
					{
						functions[count1].run = 1;
						testCount += 1;
					}
				}

				break;
			}

			case 'l':
			{
				// List all tests
				printf("\nTests:\n");
				for (count1 = 0; functions[count1].function ; count1 ++)
				{
					printf("  \"%s\"%s\n", functions[count1].name,
						(functions[count1].graphics? " (graphics)" : ""));
				}

				return (0);
			}

			default:
			{
				fprintf(stderr, "Unknown option '%c'\n", optopt);
				usage(argv[0]);
				return (-1);
			}
		}
	}

	// Any other arguments should indicate specific tests to run.
	for (count1 = optind; count1 < argc; count1 ++)
	{
		for (count2 = 0; functions[count2].function ; count2 ++)
		{
			if (!strcasecmp(argv[count1], functions[count2].name))
			{
				if (!graphics && functions[count2].graphics)
				{
					fprintf(stderr, "Can't run %s without graphics\n",
						functions[count2].name);
				}
				else
				{
					functions[count2].run = 1;
					testCount += 1;
					break;
				}
			}
		}
	}

	if (!testCount)
	{
		fprintf(stderr, "No (valid) tests specified.\n");
		usage(argv[0]);
		return (-1);
	}

	printf("\n");
	errors = run();

	if (errors)
	{
		printf("\n%d TESTS FAILED\n", errors);
		return (-1);
	}
	else
	{
		printf("\nALL TESTS PASSED\n");
		return (0);
	}
}

