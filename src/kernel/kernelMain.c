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
//  kernelMain.c
//

#include "kernelMain.h"
#include "kernelCpu.h"
#include "kernelEnvironment.h"
#include "kernelError.h"
#include "kernelGraphic.h"
#include "kernelInitialize.h"
#include "kernelLoader.h"
#include "kernelMalloc.h"
#include "kernelMisc.h"
#include "kernelMultitasker.h"
#include "kernelParameters.h"
#include "kernelText.h"
#include "kernelVariableList.h"
#include <string.h>
#include <time.h>
#include <sys/kernconf.h>
#include <sys/osloader.h>
#include <sys/processor.h>

// This is the global 'errno' error status variable for the kernel
int errno = 0;

// This is a variable that is checked by the standard library before calling
// any kernel API functions.  This helps to prevent any API functions from
// being called from within the kernel (which is bad).  For example, it is
// permissable to use sprintf() inside the kernel, but not printf().  This
// should help to catch mistakes.
int visopsys_in_kernel = 1;

// A copy of the OS loader info structure
static loaderInfoStruct osLoaderInfo;
loaderInfoStruct *kernelOsLoaderInfo = &osLoaderInfo;

// General kernel configuration variables
static variableList variables;
variableList *kernelVariables = &variables;


/////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////
//
// Below here, the functions are exported for external use
//
/////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////

void kernelMain(unsigned kernelMemory, void *kernelStack,
	unsigned kernelStackSize, loaderInfoStruct *info)
{
	// This is the kernel entry point -- and main function --
	// which starts the entire show and, of course, never returns.

	int status = 0;
	int pid = -1;
	const char *value = NULL;

	// Copy the OS loader info structure into kernel memory
	memcpy(kernelOsLoaderInfo, info, sizeof(loaderInfoStruct));

	// Call the kernel initialization function
	status = kernelInitialize(kernelMemory, kernelStack, kernelStackSize);
	if (status < 0)
	{
		// Kernel initialization failed.  Crap.  We don't exactly know
		// what failed.  That makes it a little bit risky to call the
		// error function, but we'll do it anyway.

		kernelError(kernel_error, "Initialization failed.  Press any key "
			"(or the \"reset\" button) to reboot.");

		// Do a loop, manually polling the keyboard input buffer
		// looking for the key press to reboot.
		while (!kernelTextInputCount())
			kernelMultitaskerYield();

		kernelTextPrint("Rebooting...");
		kernelCpuSpinMs(MS_PER_SEC); // Wait 1 second
		processorReboot();
	}

	if (kernelVariables)
	{
		// Find out which initial program to launch
		value = kernelVariableListGet(kernelVariables,
			KERNELVAR_START_PROGRAM);
		if (value)
		{
			// If the start program is our standard login program, use a custom
			// function to launch a login process
			if (strncmp(value, DEFAULT_KERNEL_STARTPROGRAM, 128))
			{
				// Try to load the login process
				pid = kernelLoaderLoadProgram(value, PRIVILEGE_SUPERVISOR);
				if (pid < 0)
				{
					// Don't fail, but make a warning message
					kernelError(kernel_warn, "Couldn't load start program "
						"\"%s\"", value);
				}
				else
				{
					// Attach the start program to the console text streams
					kernelMultitaskerDuplicateIo(KERNELPROCID, pid, 1); // Clear

					// Execute the start program.  Don't block.
					kernelLoaderExecProgram(pid, 0);
				}
			}
		}
	}

	// If the kernel config file wasn't found, or the start program wasn't
	// specified, or loading the start program failed, assume we're going to
	// use the standard default login program
	if (pid < 0)
		kernelConsoleLogin();

	while (1)
	{
		// Finally, we will change the kernel state to 'sleeping'.  This is
		// done because there's nothing that needs to be actively done by
		// the kernel process itself; it just needs to remain resident in
		// memory.  Changing to a 'sleeping' state means that it won't get
		// invoked again by the scheduler.
		status = kernelMultitaskerSetProcessState(KERNELPROCID, proc_sleeping);
		if (status < 0)
			kernelError(kernel_error, "The kernel process could not go to sleep.");

		// Yield the rest of this time slice back to the scheduler
		kernelMultitaskerYield();

		// We should never get here.  But we put it inside a while loop anyway.
		kernelError(kernel_error, "The kernel was unexpectedly woken up");
	}
}

