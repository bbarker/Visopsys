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
//  kernelVariableList.c
//

// This file contains the C functions belonging to the kernel's implementation
// of variable lists.  Variable lists will be used to store environment
// variables, as well as the contents of configuration files, for example

#include "kernelVariableList.h"
#include "kernelDebug.h"
#include "kernelMemory.h"
#include "kernelLock.h"
#include "kernelError.h"
#include <string.h>


static int expandList(variableList *list)
{
	// Takes the variable list and expands it.

	int status = 0;
	unsigned *oldVariables, *oldValues, *oldData;
	void *memory = NULL;
	unsigned *newVariables, *newValues, *newData;

	kernelDebug(debug_misc, "VariableList expand list");

	// Remember where the old data is
	oldVariables = list->memory;
	oldValues = (list->memory + (list->maxVariables * sizeof(unsigned)));
	oldData = (list->memory + (list->maxVariables * 2 * sizeof(unsigned)));

	// Double the list size
	list->maxVariables *= 2;
	list->maxData *= 2;
	list->memorySize =
		((list->maxVariables * sizeof(unsigned) * 2) + list->maxData);

	// Get new memory
	memory = kernelMemoryGet(list->memorySize, "variable list");
	if (!memory)
		return (status = ERR_MEMORY);

	memset(memory, 0, list->memorySize);

	// Figure out where the new data will go
	newVariables = (unsigned *) memory;
	newValues = (memory + (list->maxVariables * sizeof(unsigned)));
	newData = (memory + (list->maxVariables * 2 * sizeof(unsigned)));

	// Copy the data
	memcpy(newVariables, oldVariables, (list->numVariables *
		sizeof(unsigned)));
	memcpy(newValues, oldValues, (list->numVariables * sizeof(unsigned)));
	memcpy(newData, oldData, list->usedData);

	kernelMemoryRelease(list->memory);
	list->memory = memory;

	return (status = 0);
}


static int findVariable(variableList *list, const char *variable)
{
	// This will attempt to locate a variable in the supplied list.
	// On success, it returns the slot number of the variable.  Otherwise
	// it returns negative.

	int slot = ERR_NOSUCHENTRY;
	unsigned *variables = list->memory;
	char *data = (list->memory + (list->maxVariables * 2 * sizeof(unsigned)));
	int count;

	kernelDebug(debug_misc, "VariableList find variable %s", variable);

	// Search through the list of variables in the supplied list for the one
	// requested by the caller
	for (count = 0; count < list->numVariables; count ++)
	{
		if (!strcmp((data + variables[count]), variable))
		{
			slot = count;
			break;
		}
	}

	if (slot >= 0)
		kernelDebug(debug_misc, "VariableList return slot=%d", slot);
	else
		kernelDebug(debug_misc, "VariableList not found");

	return (slot);
}


static int unsetVariable(variableList *list, const char *variable)
{
	// Unset a variable's value from the supplied list.  This involves shifting
	// the entire contents of the list data starting from where the variable is
	// found.

	int status = 0;
	int slot = 0;
	unsigned *variables = list->memory;
	unsigned *values = (list->memory + (list->maxVariables * sizeof(unsigned)));
	char *data = (list->memory + (list->maxVariables * 2 * sizeof(unsigned)));
	int subtract = 0;
	int count;

	kernelDebug(debug_misc, "VariableList unset %s", variable);

	// Search the list of variables for the requested one.
	slot = findVariable(list, variable);
	if (slot < 0)
		// Not found
		return (status = ERR_NOSUCHENTRY);

	// Found it.  The amount of data to subtract from the data is equal to the
	// sum of the lengths of the variable name and its value (plus one for
	// each NULL character)
	subtract = ((strlen(data + variables[slot]) + 1) +
		(strlen(data + values[slot]) + 1));

	// Any more data after this?
	if (list->numVariables > 1)
	{
		// Starting from where the variable name starts, shift the whole
		// contents of the data forward by 'subtract' bytes
		memcpy((void *)(data + variables[slot]),
			(void *)((data + variables[slot]) + subtract),
			((list->usedData - variables[slot]) - subtract));

		// Now remove the 'variable' and 'value' offsets, and shift all
		// subsequent offsets in the lists forward by one, adjusting each
		// by the number of bytes we subtracted from the data
		for (count = slot; count < (list->numVariables - 1); count ++)
		{
			variables[count] = (variables[count + 1] - subtract);
			values[count] = (values[count + 1] - subtract);
		}
	}

	// We now have one fewer variables
	list->numVariables -= 1;

	// Adjust the number of bytes used
	list->usedData -= subtract;

	kernelDebug(debug_misc, "VariableList finished unsetting");

	// Return success
	return (status = 0);
}


static int setVariable(variableList *list, const char *variable,
	const char *value)
{
	// Does the work of setting a variable

	int status = 0;
	unsigned *variables, *values;
	char *data;

	kernelDebug(debug_misc, "VariableList set %s", variable);

	// Check to see whether the variable currently has a value
	if (findVariable(list, variable) >= 0)
	{
		// The variable already has a value.  We need to unset it first.
		status = unsetVariable(list, variable);
		if (status < 0)
			// We couldn't unset it.
			return (status);
	}

	// Make sure we're not exceeding the maximum number of variables, and
	// make sure we'll have enough room to store the variable name and value
	while ((list->numVariables >= list->maxVariables) ||
		((list->usedData + (strlen(variable) + strlen(value) + 2)) >
			list->maxData))
	{
		status = expandList(list);
		if (status < 0)
			return (status);
	}

	// Okay, we're setting the variable

	variables = list->memory;
	values = (list->memory + (list->maxVariables * sizeof(unsigned)));
	data = (list->memory + (list->maxVariables * 2 * sizeof(unsigned)));

	// The new variable goes at the end of the used data
	variables[list->numVariables] = list->usedData;

	// Copy the variable name
	strcpy((data + variables[list->numVariables]), variable);
	list->usedData += (strlen(variable) + 1);

	// The variable's value will come after the variable name
	values[list->numVariables] = list->usedData;

	// Copy the variable value
	strcpy((data + values[list->numVariables]), value);
	list->usedData += (strlen(value) + 1);

	// We now have one more variable
	list->numVariables++;

	kernelDebug(debug_misc, "VariableList finished setting");

	// Return success
	return (status = 0);
}


/////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////
//
//  Below here, the functions are exported for external use
//
/////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////

int kernelVariableListCreate(variableList *list)
{
	// This function will create a new variableList structure

	int status = 0;

	kernelDebug(debug_misc, "VariableList create list");

	// Check params
	if (!list)
	{
		kernelError(kernel_error, "NULL parameter");
		return (status = ERR_NULLPARAMETER);
	}

	// Initialize the number of variables and max variables and max data
	memset(list, 0, sizeof(variableList));
	list->maxVariables = VARIABLE_INITIAL_NUMBER;
	list->maxData = VARIABLE_INITIAL_DATASIZE;
	list->memorySize = VARIABLE_INITIAL_MEMORY;

	// The memory will be the size of pointers for both the variable names and
	// the values, plus additional memory for raw data
	list->memory = kernelMemoryGet(list->memorySize, "variable list");
	if (!list->memory)
		return (status = ERR_MEMORY);

	// Return success
	return (status = 0);
}


int kernelVariableListDestroy(variableList *list)
{
	// Deallocates a variable list.

	int status = 0;

	kernelDebug(debug_misc, "VariableList destroy list");

	// Check params
	if (!list)
	{
		kernelError(kernel_error, "NULL parameter");
		return (status = ERR_NULLPARAMETER);
	}

	if (list->memory)
		status = kernelMemoryRelease(list->memory);

	memset(list, 0, sizeof(variableList));

	return (status);
}


const char *kernelVariableListGetVariable(variableList *list, int slot)
{
	// Get the numbered variable from the list

	unsigned *variables = NULL;
	char *data = NULL;

	kernelDebug(debug_misc, "VariableList get variable %d", slot);

	// Check params
	if (!list)
	{
		kernelError(kernel_error, "NULL parameter");
		return (data = NULL);
	}

	if (slot >= list->numVariables)
	{
		kernelError(kernel_error, "No such variable");
		return (data = NULL);
	}

	// Lock the list while we're working with it
	if (kernelLockGet(&list->listLock) < 0)
		return (data = NULL);

	variables = list->memory;
	data = (list->memory + (list->maxVariables * 2 * sizeof(unsigned)));

	kernelLockRelease(&list->listLock);

	kernelDebug(debug_misc, "VariableList return variable %s",
		(data + variables[slot]));

	return (data + variables[slot]);
}


const char *kernelVariableListGet(variableList *list, const char *variable)
{
	// Get a variable's value from the list

	int slot = 0;
	unsigned *values = NULL;
	char *data = NULL;

	// Check params
	if (!list || !variable)
	{
		kernelError(kernel_error, "NULL parameter");
		return (data = NULL);
	}

	kernelDebug(debug_misc, "VariableList get %s", variable);

	// Lock the list while we're working with it
	if (kernelLockGet(&list->listLock) < 0)
		return (data = NULL);

	slot = findVariable(list, variable);
	if (slot < 0)
	{
		// No such variable
		kernelLockRelease(&list->listLock);
		return (data = NULL);
	}

	values = (list->memory + (list->maxVariables * sizeof(unsigned)));
	data = (list->memory + (list->maxVariables * 2 * sizeof(unsigned)));

	kernelLockRelease(&list->listLock);

	kernelDebug(debug_misc, "VariableList return value %s",
		(data + values[slot]));

	return (data + values[slot]);
}


int kernelVariableListSet(variableList *list, const char *variable,
	const char *value)
{
	// A wrapper function for setVariable

	int status = 0;

	// Check params
	if (!list || !variable || !value)
	{
		kernelError(kernel_error, "NULL parameter");
		return (status = ERR_NULLPARAMETER);
	}

	kernelDebug(debug_misc, "VariableList set %s=%s", variable, value);

	// Lock the list while we're working with it
	status = kernelLockGet(&list->listLock);
	if (status < 0)
		return (status = ERR_NOLOCK);

	status = setVariable(list, variable, value);

	kernelLockRelease(&list->listLock);

	return (status);
}


int kernelVariableListUnset(variableList *list, const char *variable)
{
	// A wrapper function for unsetVariable

	int status = 0;

	// Check params
	if (!list || !variable)
	{
		kernelError(kernel_error, "NULL parameter");
		return (status = ERR_NULLPARAMETER);
  	}

	kernelDebug(debug_misc, "VariableList unset %s", variable);

	// Lock the list while we're working with it
	status = kernelLockGet(&list->listLock);
	if (status < 0)
		return (status = ERR_NOLOCK);

	status = unsetVariable(list, variable);

	kernelLockRelease(&list->listLock);

	return (status);
}


int kernelVariableListClear(variableList *list)
{
	// Removes all the variables from the list.

	int status = 0;

	// Check params
	if (!list)
	{
		kernelError(kernel_error, "NULL parameter");
		return (status = ERR_NULLPARAMETER);
  	}

	kernelDebug(debug_misc, "VariableList clear list");

	// Lock the list while we're working with it
	status = kernelLockGet(&list->listLock);
	if (status < 0)
		return (status = ERR_NOLOCK);

	list->numVariables = 0;
	list->usedData = 0;

	kernelLockRelease(&list->listLock);

	return (status = 0);
}

