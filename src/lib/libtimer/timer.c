//
//  Visopsys
//  Copyright (C) 1998-2016 J. Andrew McLaughlin
//
//  This library is free software; you can redistribute it and/or modify it
//  under the terms of the GNU Lesser General Public License as published by
//  the Free Software Foundation; either version 2.1 of the License, or (at
//  your option) any later version.
//
//  This library is distributed in the hope that it will be useful, but
//  WITHOUT ANY WARRANTY; without even the implied warranty of
//  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU Lesser
//  General Public License for more details.
//
//  You should have received a copy of the GNU Lesser General Public License
//  along with this library; if not, write to the Free Software Foundation,
//  Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
//
//  timer.c
//

// This is a library for timing functions (for performance analysis)

#include <sys/timer.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/api.h>
#include <sys/types.h>

#define NUM_ENTRIES	TIMER_MAX_FUNCTIONS

#define timestamp(hi, lo) do { \
	__asm__ __volatile__ ("rdtsc" : "=a" (lo), "=d" (hi)); \
} while (0)

static timerFunctionEntry entries[NUM_ENTRIES];
static int numEntries = 0;
static timerFunctionEntry *currentEntry = NULL;
static int recursing = 0;


static timerFunctionEntry *findEntry(const char *function)
{
	timerFunctionEntry *entry = NULL;
	int count;

	for (count = 0; count < numEntries; count ++)
	{
		if (!strcmp(entries[count].function, function))
		{
			entry = &entries[count];
			break;
		}
	}

	return (entry);
}


/////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////
//
// Below here, the functions are exported for external use
//
/////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////

void timerSetup(void)
{
	memset(entries, 0, sizeof(entries));
	numEntries = 0;
	currentEntry = NULL;
}


void timerEnter(const char *function)
{
	// Start timing the entry to a function

	timerFunctionEntry *entry = NULL;
	unsigned lo = 0, hi = 0;

	// Do we already have an entry for it?
	entry = findEntry(function);
	if (!entry)
	{
		// Need a new entry.  Any slots left?
		if (numEntries >= NUM_ENTRIES)
		{
			// Full
			errno = ERR_NOFREE;
			return;
		}

		entry = &entries[numEntries++];
		entry->function = function;
	}

	// Are we interrupting another function?
	if (currentEntry)
	{
		// Are we recursing?
		if (currentEntry == entry)
		{
			recursing += 1;
			return;
		}

		entry->interrupted = currentEntry;
	}

	currentEntry = entry;

	entry->calls += 1;

	// Get the current timestamp, and save it in the entry
	timestamp(hi, lo);
	entry->entered = ((((uquad_t) hi) << 32) | lo);
	entry->pausedTime = 0;

	return;
}


void timerExit(const char *function)
{
	// Finish timing at the exit of a function

	timerFunctionEntry *entry = NULL;
	unsigned lo = 0, hi = 0;
	uquad_t exited = 0;

	// Do we have an entry for this function?
	entry = findEntry(function);
	if (!entry)
	{
		// Maybe the user forgot to call timerEnter, or maybe the list of
		// entries was full.
		errno = ERR_NOSUCHENTRY;
		return;
	}

	// Are we exiting a recursion?
	if (recursing)
	{
		recursing -= 1;
		return;
	}

	// Get the current timestamp, subtract the last entry value from it, and
	// add it to the entry's total time
	timestamp(hi, lo);
	exited = ((((uquad_t) hi) << 32) | lo);
	entry->totalTime += ((exited - entry->entered) - entry->pausedTime);

	// Did we interrupt another function?
	if (entry->interrupted)
	{
		// Restore it as the current function entry, and note the time that was
		// spent in the function entry.
		currentEntry = entry->interrupted;
		currentEntry->pausedTime += (exited - entry->entered);
	}
	else
	{
		currentEntry = NULL;
	}
}


void timerGetSummary(timerFunctionEntry *summaryEntries, int numSummaryEntries)
{
	// Return a sorted summary of time spent in functions, with the most-active
	// functions listed first.

	uquad_t highest = 0;
	int count1, count2;

	for (count1 = 0; ((count1 < numSummaryEntries) && (count1 < numEntries));
		count1 ++)
	{
		highest = 0;
		for (count2 = 0; count2 < numEntries; count2 ++)
		{
			if ((entries[count2].totalTime > highest) &&
				(!count1 ||	(entries[count2].totalTime <
					summaryEntries[count1 - 1].totalTime)))
			{
				highest = entries[count2].totalTime;
				memcpy(&summaryEntries[count1], &entries[count2],
					sizeof(timerFunctionEntry));
			}
		}
	}
}


void timerPrintSummary(int maxEntries)
{
	// Print out a summary of time spent in functions

	timerFunctionEntry *summaryEntries = NULL;
	int numSummaryEntries = NUM_ENTRIES;
	unsigned maxNameLen = 0;
	uquad_t totalTime = 0;
	int count;

	// Can't use this in the kernel
	if (visopsys_in_kernel)
	{
		errno = ERR_BUG;
		return;
	}

	if (maxEntries)
		numSummaryEntries = min(maxEntries, NUM_ENTRIES);

	summaryEntries = calloc(numSummaryEntries, sizeof(timerFunctionEntry));
	if (!summaryEntries)
	{
		errno = ERR_MEMORY;
		return;
	}

	timerGetSummary(summaryEntries, numSummaryEntries);

	// First just loop through to get a couple of bits of data
	for (count = 0; count < numSummaryEntries; count ++)
	{
		if (summaryEntries[count].function)
		{
			if (strlen(summaryEntries[count].function) > maxNameLen)
				maxNameLen = strlen(summaryEntries[count].function);

			totalTime += summaryEntries[count].totalTime;
		}
	}

	// Now print
	printf(" --- FUNCTION TIMER SUMMARY ---\n");
	for (count = 0; count < numSummaryEntries; count ++)
	{
		if (summaryEntries[count].function)
		{
			printf("%s:", summaryEntries[count].function);
			textSetColumn(maxNameLen + 2);
			printf("calls: %d \ttime: %llu \t(%llu%%)\n",
				summaryEntries[count].calls,
				summaryEntries[count].totalTime,
				((summaryEntries[count].totalTime * 100) / totalTime));
		}
	}

	free(summaryEntries);

	return;
}

