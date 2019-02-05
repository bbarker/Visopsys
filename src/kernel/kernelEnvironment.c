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
//  kernelEnvironment.c
//

// This file contains convenience functions for creating/accessing a
// process' list of environment variables (for example, the PATH variable).
//  It's just a standard variableList.

#include "kernelEnvironment.h"
#include "kernelError.h"
#include "kernelFile.h"
#include "kernelParameters.h"
#include "kernelPage.h"
#include "kernelVariableList.h"
#include "kernelMemory.h"
#include "kernelMisc.h"
#include "kernelMultitasker.h"
#include <stdio.h>
#include <sys/paths.h>
#include <sys/user.h>


/////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////
//
//  Below here, the functions are exported for external use
//
/////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////


int kernelEnvironmentCreate(int processId, variableList *env,
	variableList *copy)
{
	// This function will create a new environment structure for a process.

	int status = 0;
	const char *variable = NULL;
	const char *value = NULL;
	int count;

	// Check params
	if (!env)
	{
		kernelError(kernel_error, "NULL parameter");
		return (status = ERR_NULLPARAMETER);
	}

	// It's OK for the 'copy' pointer to be NULL, but if it is not, it is
	// assumed that it is in the current process' address space.

	status = kernelVariableListCreate(env);
	if (status < 0)
		// Eek.  Couldn't get environment space.
		return (status);

	if (processId == KERNELPROCID)
		return (status = 0);

	// Are we supposed to inherit the environment from another process?
	if (copy)
	{
		for (count = 0; count < copy->numVariables; count ++)
		{
			variable = kernelVariableListGetVariable(copy, count);
			if (variable)
			{
				value = kernelVariableListGet(copy, variable);
				if (value)
					kernelVariableListSet(env, variable, value);
			}
		}
	}

	// Change the memory ownership.
	status = kernelMemoryChangeOwner(kernelMultitaskerGetCurrentProcessId(),
		processId, 1 /* remap */, env->memory, &env->memory);
	if (status < 0)
	{
		// Eek.  Couldn't chown the memory.
		kernelVariableListDestroy(env);
		return (status);
	}

	// Return success
	return (status = 0);
}


int kernelEnvironmentLoad(const char *userName)
{
	// Given a user name, load variables from the system's environment.conf
	// file into the current process' environment space, then try to load more
	// from the user's home directory, if applicable.

	int status = ERR_NOSUCHFILE;
	char fileName[MAX_PATH_NAME_LENGTH];
	variableList envList;
	const char *variable = NULL;
	int count;

	// Try to load environment variables from the system configuration
	// directory
	strcpy(fileName, PATH_SYSTEM_CONFIG "/environment.conf");
	if (kernelConfigRead(fileName, &envList) >= 0)
	{
		for (count = 0; count < envList.numVariables; count ++)
		{
			variable = kernelVariableListGetVariable(&envList, count);
			kernelEnvironmentSet(variable,
				kernelVariableListGet(&envList, variable));
		}

		kernelVariableListDestroy(&envList);
		status = 0;
	}

	if (strncmp(userName, USER_ADMIN, USER_MAX_NAMELENGTH))
	{
		// Try to load more environment variables from the user's home
		// directory
		sprintf(fileName, PATH_USERS_CONFIG "/environment.conf", userName);
		if (kernelFileLookup(fileName) &&
			(kernelConfigRead(fileName, &envList) >= 0))
		{
			for (count = 0; count < envList.numVariables; count ++)
			{
				variable = kernelVariableListGetVariable(&envList, count);
				kernelEnvironmentSet(variable,
					kernelVariableListGet(&envList, variable));
			}

			kernelVariableListDestroy(&envList);
			status = 0;
		}
	}

	return (status);
}


int kernelEnvironmentGet(const char *variable, char *buffer,
	unsigned buffSize)
{
	// Get a variable's value from the current process' environment space.

	const char *value = NULL;

	// Check params
	if (!buffer)
	{
		kernelError(kernel_error, "NULL parameter");
		return (ERR_NULLPARAMETER);
	}

	if (kernelCurrentProcess)
		value = kernelVariableListGet(kernelCurrentProcess->environment,
			variable);

	if (value)
	{
		strncpy(buffer, value, buffSize);
		return (0);
	}
	else
	{
		buffer[0] = '\0';
		return (ERR_NOSUCHENTRY);
	}
}


int kernelEnvironmentSet(const char *variable, const char *value)
{
	// Set a variable's value in the current process' environment space.

	if (!kernelCurrentProcess)
		return (ERR_NOSUCHPROCESS);

	return (kernelVariableListSet(kernelCurrentProcess->environment,
		variable, value));
}


int kernelEnvironmentUnset(const char *variable)
{
	// Unset a variable's value from the current process' environment space.

	if (!kernelCurrentProcess)
		return (ERR_NOSUCHPROCESS);

	return (kernelVariableListUnset(kernelCurrentProcess->environment,
		variable));
}


int kernelEnvironmentClear(void)
{
	// Clear the current process' entire environment space.

	if (!kernelCurrentProcess)
		return (ERR_NOSUCHPROCESS);

	return (kernelVariableListClear(kernelCurrentProcess->environment));
}


void kernelEnvironmentDump(void)
{
	variableList *list = NULL;
	const char *variable = NULL;
	int count;

	if (!kernelCurrentProcess)
		return;

	list = kernelCurrentProcess->environment;

	if (!list)
		return;

	for (count = 0; count < list->numVariables; count ++)
	{
		variable = kernelVariableListGetVariable(list, count);
		if (variable)
			kernelTextPrintLine("%s=%s", variable,
				kernelVariableListGet(list, variable));
	}

	return;
}

