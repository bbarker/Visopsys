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
//  signal.c
//

// This is the standard "signal" function, as found in standard C libraries

#include <signal.h>
#include <stdlib.h>
#include <errno.h>
#include <sys/api.h>

static int processId = 0;
static int stop = 0;
static sighandler_t *signalHandlers = NULL;
static int signalThreadPid = 0;


static void signalThread(void)
{
	// This thread listens for signals and calls the appropriate signal handler

	int sig = 0;

	while (!stop && signalHandlers)
	{
		sig = multitaskerSignalRead(processId);

		if (sig > 0)
		{
			// If the signal is SIG_IGN, do nothing
			if ((signalHandlers[sig] == SIG_DFL) ||
				(signalHandlers[sig] == SIG_IGN))
				continue;

			// Otherwise, call the handler
			signalHandlers[sig](sig);
		}

		multitaskerYield();
	}

	free(signalHandlers);
	multitaskerTerminate(0);
}


sighandler_t signal(int sig, sighandler_t handler)
{
	// From the GNU man page:
	// The signal() system call installs a new signal handler for the signal
	// with number sig.  The signal handler is set to handler which may
	// be a user specified function, or either SIG_IGN or SIG_DFL.
	// <snip>
	// The signal() function returns the previous value of the signal handler,
	// or SIG_ERR on error.

	sighandler_t oldHandler = SIG_DFL;
	int count;

	if (visopsys_in_kernel)
	{
		errno = ERR_BUG;
		return (SIG_ERR);
	}

	// This is extra (non-spec).  We need this for the moment because we need
	// to be able to terminate the signal thread
	if (!sig && (handler == SIG_DFL))
	{
		stop = 1;
		return (SIG_ERR);
	}

	// Check params.  The signal number must be (0 < sig < 32)
	if ((sig <= 0) || (sig >= SIGNALS_MAX))
	{
		errno = ERR_RANGE;
		return (SIG_ERR);
	}

	// Can't accept handler SIG_ERR
	if (handler == SIG_ERR)
	{
		errno = ERR_INVALID;
		return (SIG_ERR);
	}

	// If we yet have no memory for signal handlers, allocate it now
	if (!signalHandlers)
	{
		signalHandlers = malloc(SIGNALS_MAX * sizeof(sighandler_t));
		if (!signalHandlers)
		{
			errno = ERR_MEMORY;
			return (SIG_ERR);
		}

		// Clear them all
		for (count = 0; count < SIGNALS_MAX; count ++)
			signalHandlers[count] = SIG_DFL;
	}

	processId = multitaskerGetCurrentProcessId();
	if (processId < 0)
		return (SIG_ERR);

	// Set/clear the signal mask in the kernel
	if (multitaskerSignalSet(processId, sig, (handler != SIG_DFL)) < 0)
		return (SIG_ERR);

	// Make sure the signal thread is running
	if (!signalThreadPid || !multitaskerProcessIsAlive(signalThreadPid))
	{
		stop = 0;
		signalThreadPid =
			multitaskerSpawn(&signalThread, "signal thread", 0, NULL);
		if (signalThreadPid < 0)
			return (SIG_ERR);
	}

	// Set the indicated signal
	oldHandler = signalHandlers[sig];
	signalHandlers[sig] = handler;

	return (oldHandler);
}

