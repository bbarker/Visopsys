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
//  kernelMisc.c
//

// A file for miscellaneous things

#include "kernelMisc.h"
#include "kernelDebug.h"
#include "kernelError.h"
#include "kernelFile.h"
#include "kernelFileStream.h"
#include "kernelLoader.h"
#include "kernelLog.h"
#include "kernelMalloc.h"
#include "kernelNetwork.h"
#include "kernelParameters.h"
#include "kernelRandom.h"
#include "kernelRtc.h"
#include "kernelText.h"
#include <ctype.h>
#include <stdio.h>
#include <sys/processor.h>

static unsigned long  crcTable[256] = {
	0x00000000, 0x77073096, 0xEE0E612C, 0x990951BA, 0x076DC419, 0x706AF48F,
	0xE963A535, 0x9E6495A3, 0x0EDB8832, 0x79DCB8A4, 0xE0D5E91E, 0x97D2D988,
	0x09B64C2B, 0x7EB17CBD, 0xE7B82D07, 0x90BF1D91, 0x1DB71064, 0x6AB020F2,
	0xF3B97148, 0x84BE41DE, 0x1ADAD47D, 0x6DDDE4EB, 0xF4D4B551, 0x83D385C7,
	0x136C9856, 0x646BA8C0, 0xFD62F97A, 0x8A65C9EC, 0x14015C4F, 0x63066CD9,
	0xFA0F3D63, 0x8D080DF5, 0x3B6E20C8, 0x4C69105E, 0xD56041E4, 0xA2677172,
	0x3C03E4D1, 0x4B04D447, 0xD20D85FD, 0xA50AB56B, 0x35B5A8FA, 0x42B2986C,
	0xDBBBC9D6, 0xACBCF940, 0x32D86CE3, 0x45DF5C75, 0xDCD60DCF, 0xABD13D59,
	0x26D930AC, 0x51DE003A, 0xC8D75180, 0xBFD06116, 0x21B4F4B5, 0x56B3C423,
	0xCFBA9599, 0xB8BDA50F, 0x2802B89E, 0x5F058808, 0xC60CD9B2, 0xB10BE924,
	0x2F6F7C87, 0x58684C11, 0xC1611DAB, 0xB6662D3D, 0x76DC4190, 0x01DB7106,
	0x98D220BC, 0xEFD5102A, 0x71B18589, 0x06B6B51F, 0x9FBFE4A5, 0xE8B8D433,
	0x7807C9A2, 0x0F00F934, 0x9609A88E, 0xE10E9818, 0x7F6A0DBB, 0x086D3D2D,
	0x91646C97, 0xE6635C01, 0x6B6B51F4, 0x1C6C6162, 0x856530D8, 0xF262004E,
	0x6C0695ED, 0x1B01A57B, 0x8208F4C1, 0xF50FC457, 0x65B0D9C6, 0x12B7E950,
	0x8BBEB8EA, 0xFCB9887C, 0x62DD1DDF, 0x15DA2D49, 0x8CD37CF3, 0xFBD44C65,
	0x4DB26158, 0x3AB551CE, 0xA3BC0074, 0xD4BB30E2, 0x4ADFA541, 0x3DD895D7,
	0xA4D1C46D, 0xD3D6F4FB, 0x4369E96A, 0x346ED9FC, 0xAD678846, 0xDA60B8D0,
	0x44042D73, 0x33031DE5, 0xAA0A4C5F, 0xDD0D7CC9, 0x5005713C, 0x270241AA,
	0xBE0B1010, 0xC90C2086, 0x5768B525, 0x206F85B3, 0xB966D409, 0xCE61E49F,
	0x5EDEF90E, 0x29D9C998, 0xB0D09822, 0xC7D7A8B4, 0x59B33D17, 0x2EB40D81,
	0xB7BD5C3B, 0xC0BA6CAD, 0xEDB88320, 0x9ABFB3B6, 0x03B6E20C, 0x74B1D29A,
	0xEAD54739, 0x9DD277AF, 0x04DB2615, 0x73DC1683, 0xE3630B12, 0x94643B84,
	0x0D6D6A3E, 0x7A6A5AA8, 0xE40ECF0B, 0x9309FF9D, 0x0A00AE27, 0x7D079EB1,
	0xF00F9344, 0x8708A3D2, 0x1E01F268, 0x6906C2FE, 0xF762575D, 0x806567CB,
	0x196C3671, 0x6E6B06E7, 0xFED41B76, 0x89D32BE0, 0x10DA7A5A, 0x67DD4ACC,
	0xF9B9DF6F, 0x8EBEEFF9, 0x17B7BE43, 0x60B08ED5, 0xD6D6A3E8, 0xA1D1937E,
	0x38D8C2C4, 0x4FDFF252, 0xD1BB67F1, 0xA6BC5767, 0x3FB506DD, 0x48B2364B,
	0xD80D2BDA, 0xAF0A1B4C, 0x36034AF6, 0x41047A60, 0xDF60EFC3, 0xA867DF55,
	0x316E8EEF, 0x4669BE79, 0xCB61B38C, 0xBC66831A, 0x256FD2A0, 0x5268E236,
	0xCC0C7795, 0xBB0B4703, 0x220216B9, 0x5505262F, 0xC5BA3BBE, 0xB2BD0B28,
	0x2BB45A92, 0x5CB36A04, 0xC2D7FFA7, 0xB5D0CF31, 0x2CD99E8B, 0x5BDEAE1D,
	0x9B64C2B0, 0xEC63F226, 0x756AA39C, 0x026D930A, 0x9C0906A9, 0xEB0E363F,
	0x72076785, 0x05005713, 0x95BF4A82, 0xE2B87A14, 0x7BB12BAE, 0x0CB61B38,
	0x92D28E9B, 0xE5D5BE0D, 0x7CDCEFB7, 0x0BDBDF21, 0x86D3D2D4, 0xF1D4E242,
	0x68DDB3F8, 0x1FDA836E, 0x81BE16CD, 0xF6B9265B, 0x6FB077E1, 0x18B74777,
	0x88085AE6, 0xFF0F6A70, 0x66063BCA, 0x11010B5C, 0x8F659EFF, 0xF862AE69,
	0x616BFFD3, 0x166CCF45, 0xA00AE278, 0xD70DD2EE, 0x4E048354, 0x3903B3C2,
	0xA7672661, 0xD06016F7, 0x4969474D, 0x3E6E77DB, 0xAED16A4A, 0xD9D65ADC,
	0x40DF0B66, 0x37D83BF0, 0xA9BCAE53, 0xDEBB9EC5, 0x47B2CF7F, 0x30B5FFE9,
	0xBDBDF21C, 0xCABAC28A, 0x53B39330, 0x24B4A3A6, 0xBAD03605, 0xCDD70693,
	0x54DE5729, 0x23D967BF, 0xB3667A2E, 0xC4614AB8, 0x5D681B02, 0x2A6F2B94,
	0xB40BBE37, 0xC30C8EA1, 0x5A05DF1B, 0x2D02EF8D
};

char *kernelVersion[] = {
	"Visopsys",
	_KVERSION_
};


static void walkStack(kernelProcess *traceProcess, void *stackMemory,
	unsigned stackSize, long memoryOffset, void **framePointer, char *buffer,
	int len)
{
	void *oldFramePointer = 0;
	void *stackBase = (stackMemory + stackSize - sizeof(void *));
	void *returnAddress = NULL;
	const char *symbolName = NULL;

#define STILLWALKING(sm, sb, fp, ofp) ((fp >= sm) && (fp > ofp) && (fp <= sb))

	while (STILLWALKING(stackMemory, stackBase, *framePointer, oldFramePointer))
	{
		// The return address of each frame is sizeof(void *) bytes past the
		// frame pointer.
		returnAddress = (void *)
			*((unsigned long *)(*framePointer + memoryOffset + sizeof(void *)));

		// Walk to the next frame
		oldFramePointer = *framePointer;
		*framePointer = (void *)
		*((unsigned long *)(*framePointer + memoryOffset));

		if (returnAddress &&
			STILLWALKING(stackMemory, stackBase, *framePointer,
				oldFramePointer))
		{
			symbolName = kernelLookupClosestSymbol(traceProcess, returnAddress);
			if (symbolName)
				snprintf((buffer + strlen(buffer)), (len - strlen(buffer)),
					"  %p  %s\n", returnAddress, symbolName);
			else
				snprintf((buffer + strlen(buffer)), (len - strlen(buffer)),
					"  %p\n", returnAddress);
		}
	}
}


static inline void eraseLine(void)
{
	int cursorPos = 0;

	// Erase the current line that the cursor is sitting on
	for (cursorPos = kernelTextGetColumn(); cursorPos > 0; cursorPos--)
		kernelTextBackSpace();

	return;
}


/////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////
//
//  Below here, the functions are exported for external use
//
/////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////

void kernelGetVersion(char *buffer, int bufferSize)
{
	// This function creates and returns a pointer to the kernel's version
	// string.

	// Construct the version string
	snprintf(buffer, bufferSize, "%s v%s", kernelVersion[0], kernelVersion[1]);

	return;
}


int kernelSystemInfo(struct utsname *uname)
{
	// This function gathers some info about the system and puts it into
	// a 'utsname' structure, just like the one returned by 'uname' in Unix.

	int status = 0;
	kernelDevice *cpuDevice = NULL;

	// Check params
	if (!uname)
		return (status = ERR_NULLPARAMETER);

	strncpy(uname->sysname, kernelVersion[0], UTSNAME_MAX_SYSNAME_LENGTH);
	kernelNetworkGetHostName(uname->nodename, NETWORK_MAX_HOSTNAMELENGTH);
	strncpy(uname->release, kernelVersion[1], UTSNAME_MAX_RELEASE_LENGTH);
	strncpy(uname->version, __DATE__" "__TIME__, UTSNAME_MAX_VERSION_LENGTH);
	if ((kernelDeviceFindType(kernelDeviceGetClass(DEVICECLASS_CPU), NULL,
		&cpuDevice, 1) > 0) && cpuDevice->device.subClass)
	{
		strncpy(uname->machine, cpuDevice->device.subClass->name,
			UTSNAME_MAX_MACHINE_LENGTH);
	}
	kernelNetworkGetDomainName(uname->domainname, NETWORK_MAX_DOMAINNAMELENGTH);

	return (status = 0);
}


const char *kernelLookupClosestSymbol(kernelProcess *lookupProcess,
	void *address)
{
	loaderSymbolTable *symTable = NULL;
	int count = 0;

	if (address >= (void *) KERNEL_VIRTUAL_ADDRESS)
		// Try to get the symbol from the kernel's process
		symTable = kernelMultitaskerGetSymbols(KERNELPROCID);
	else
		symTable = lookupProcess->symbols;

	if (!symTable)
		return (NULL);

	for (count = 0; count < symTable->numSymbols; count ++)
	{
		if ((symTable->symbols[count].type == LOADERSYMBOLTYPE_FUNC) &&
			(address >= symTable->symbols[count].value) &&
			((count >= (symTable->numSymbols - 1)) ||
				(address < symTable->symbols[count + 1].value)))
		{
			return (symTable->symbols[count].name);
		}
	}

	// Not found
	return (NULL);
}


int kernelStackTrace(kernelProcess *traceProcess, char *buffer, int len)
{
	// Will try to do a stack trace of the return addresses between for each
	// stack frame between the current stack pointer and stack base.  The
	// stack memory in question must already be mapped in the current address
	// space.

	int status = 0;
	void *instPointer = 0;
	void *framePointer = NULL;
	unsigned stackPhysical = NULL;
	void *stackVirtual = NULL;
	long memoryOffset = 0;
	const char *symbolName = NULL;

	// Check params
	if (!buffer)
	{
		kernelError(kernel_error, "NULL parameter");
		return (status = ERR_NULLPARAMETER);
	}

	if (!kernelCurrentProcess)
	{
		kernelError(kernel_error, "Current process is NULL.  Multitasking not "
			"yet initialized?");
		return (status = ERR_INVALID);
	}

	if (!traceProcess)
		traceProcess = kernelCurrentProcess;

	// Permission check.  A privileged process can trace any other process,
	// but a non-privileged process can only trace processes owned by the
	// same user
	if ((kernelCurrentProcess->privilege != PRIVILEGE_SUPERVISOR) &&
		(kernelCurrentProcess->userId != traceProcess->userId))
	{
		kernelError(kernel_error, "Current process does not have supervisor "
			"privilege and user does not own the process");
		return (status = ERR_PERMISSION);
	}

	snprintf(buffer, len, "--> stack trace process \"%s\":\n",
		traceProcess->name);

	// The initial frame pointer will be different depending on whether or not
	// we are live-tracing the current process.  If so, then we need the
	// current frame pointer.  Otherwise we get the saved frame pointer from
	// the process structure.
	if (traceProcess == kernelCurrentProcess)
	{
		// Live-tracing the current process
		processorGetInstructionPointer(instPointer);
		processorGetFramePointer(framePointer);
	}
	else
	{
		instPointer = (void *) traceProcess->taskStateSegment.EIP;
		framePointer = (void *) traceProcess->taskStateSegment.EBP;

		// If we're tracing some other process, we need to map its stack into
		// our address space.
		if (traceProcess != kernelCurrentProcess)
		{
			stackPhysical = kernelPageGetPhysical(traceProcess->processId,
				traceProcess->userStack);
			if (!stackPhysical)
				return (status = ERR_BADADDRESS);

			status = kernelPageMapToFree(kernelCurrentProcess->processId,
				stackPhysical, &stackVirtual, (traceProcess->userStackSize +
					traceProcess->superStackSize));
			if (status < 0)
				return (status);

			// Calculate the difference between the process' stack addresses
			// and the memory we mapped.
			if (stackVirtual > traceProcess->userStack)
				memoryOffset = (stackVirtual - traceProcess->userStack);
			else
				memoryOffset = -(traceProcess->userStack - stackVirtual);
		}
	}

	// First try and figure out the current function
	symbolName = kernelLookupClosestSymbol(traceProcess, instPointer);
	if (symbolName)
		snprintf((buffer + strlen(buffer)), (len - strlen(buffer)),
			"  %p  %s\n", instPointer, symbolName);

	// If there is a separate, privileged stack, show that first.
	if (traceProcess->superStack &&
		(framePointer >= traceProcess->superStack) &&
		(framePointer <
			(traceProcess->superStack + traceProcess->superStackSize)))
	{
		snprintf((buffer + strlen(buffer)), (len - strlen(buffer)),
			" supervisor stack:\n");
		walkStack(traceProcess, traceProcess->superStack,
			traceProcess->superStackSize, memoryOffset, &framePointer,
			(buffer + strlen(buffer)), (len - strlen(buffer)));
	}

	if ((framePointer >= traceProcess->userStack) &&
		(framePointer < (traceProcess->userStack +
			traceProcess->userStackSize)))
	{
		// Now do the normal, 'user' stack
		snprintf((buffer + strlen(buffer)), (len - strlen(buffer)),
			" user stack:\n");
		walkStack(traceProcess, traceProcess->userStack,
			traceProcess->userStackSize, memoryOffset, &framePointer,
			(buffer + strlen(buffer)), (len - strlen(buffer)));
	}

	snprintf((buffer + strlen(buffer)), (len - strlen(buffer)), "<--\n");

	if (stackVirtual)
		kernelPageUnmap(kernelCurrentProcess->processId, stackVirtual,
			(traceProcess->userStackSize + traceProcess->superStackSize));

	return (status = 0);
}


void kernelConsoleLogin(void)
{
	// This routine will launch a login process on the console.  This should
	// normally be called by the kernel's kernelMain() routine, above, but
	// also possibly by the keyboard driver when some hot-key is pressed.

	static int loginPid = 0;
	processState tmp;

	// Try to make sure we don't start multiple logins at once
	if (loginPid)
	{
		// Try to kill the old one, but don't mind the success or failure of
		// the operation
		if (kernelMultitaskerGetProcessState(loginPid, &tmp) >= 0)
			kernelMultitaskerKillProcess(loginPid, 1);
	}

	// Try to load the login process
	loginPid = kernelLoaderLoadProgram(DEFAULT_KERNEL_STARTPROGRAM,
		PRIVILEGE_SUPERVISOR);
	if (loginPid < 0)
	{
		// Don't fail, but make a warning message
		kernelError(kernel_warn, "Couldn't start a login process");
		return;
	}

	// Attach the login process to the console text streams
	kernelMultitaskerDuplicateIO(KERNELPROCID, loginPid, 1); // clear

	// Execute the login process.  Don't block.
	kernelLoaderExecProgram(loginPid, 0);

	// Done
	return;
}


int kernelConfigRead(const char *fileName, variableList *list)
{
	// Read a config file into the supplied variable list structure.

	int status = 0;
	fileStream *configFile = NULL;
	char lineBuffer[256];
	int hasContent = 0;
	char *variable = NULL;
	char *value = NULL;
	int count;

	// Check params
	if (!fileName || !list)
	{
		kernelError(kernel_error, "NULL parameter");
		return (status = ERR_NULLPARAMETER);
	}

	configFile = kernelMalloc(sizeof(fileStream));
	if (!configFile)
		return (status = ERR_MEMORY);

	status = kernelFileStreamOpen(fileName, OPENMODE_READ, configFile);
	if (status < 0)
	{
		kernelError(kernel_warn, "Unable to read the configuration file \"%s\"",
			fileName);
		kernelFree(configFile);
		return (status);
	}

	// Create the list
	status = kernelVariableListCreate(list);
	if (status < 0)
	{
		kernelError(kernel_warn, "Unable to create a variable list for "
			"configuration file \"%s\"", fileName);
		kernelFileStreamClose(configFile);
		kernelFree(configFile);
		return (status);
	}

	// Read line by line
	while (1)
	{
		status = kernelFileStreamReadLine(configFile, 256, lineBuffer);
		if (status < 0)
			// End of file?
			break;

		// See if there's anything but whitespace, or comment here
		hasContent = 0;
		for (count = 0; count < status; count ++)
		{
			if (isspace(lineBuffer[count]))
				continue;
			else
			{
				if (lineBuffer[count] != '#')
					hasContent = 1;
				break;
			}
		}

		if (!hasContent)
			continue;

		variable = lineBuffer;
		for (count = 0; (lineBuffer[count] != '=') && (count < 255); count ++);
			lineBuffer[count] = '\0';

		if (strlen(variable) < 255)
			// Everything after the '=' is the value
			value = (lineBuffer + count + 1);

		// Store it
		kernelVariableListSet(list, variable, value);
	}

	kernelFree(configFile);
	kernelFileStreamClose(configFile);

	return (status = 0);
}


int kernelConfigWrite(const char *fileName, variableList *list)
{
	// Writes a variable list out to a config file, with a little bit of
	// extra sophistication so that if the file already exists, comments and
	// blank lines are (hopefully) preserved.

	int status = 0;
	char tmpName[MAX_PATH_NAME_LENGTH];
	fileStream *oldFileStream = NULL;
	fileStream *newFileStream = NULL;
	char lineBuffer[256];
	int hasContent = 0;
	const char *variable = NULL;
	const char *value = NULL;
	int count1, count2;

	// Check params
	if (!fileName || !list)
	{
		kernelError(kernel_error, "NULL parameter");
		return (status = ERR_NULLPARAMETER);
	}

	// Is there already an old version of the config file?
	if (!kernelFileFind(fileName, NULL))
	{
		// Yup.  Open it for reading, and make our temporary filename different
		// so that we don't overwrite anything until we've been successful.

		oldFileStream = kernelMalloc(sizeof(fileStream));
		if (!oldFileStream)
			return (status = ERR_MEMORY);

		status = kernelFileStreamOpen(fileName, OPENMODE_READ, oldFileStream);
		if (status < 0)
		{
			kernelFree(oldFileStream);
			return (status);
		}

		sprintf(tmpName, "%s.TMP", fileName);
	}
	else
		strcpy(tmpName, fileName);

	newFileStream = kernelMalloc(sizeof(fileStream));
	if (!newFileStream)
	{
		if (oldFileStream)
		{
			kernelFileStreamClose(oldFileStream);
			kernelFree(oldFileStream);
		}

		return (status = ERR_MEMORY);
	}

	// Create the new config file for writing
	status = kernelFileStreamOpen(tmpName, (OPENMODE_CREATE | OPENMODE_WRITE |
		OPENMODE_TRUNCATE), newFileStream);
	if (status < 0)
	{
		if (oldFileStream)
		{
			kernelFileStreamClose(oldFileStream);
			kernelFree(oldFileStream);
		}

		return (status);
	}

	// Write line by line for each variable
	for (count1 = 0; count1 < list->numVariables; count1 ++)
	{
		// If we successfully opened an old file, first try to keep stuff in
		// sync with the line numbers
		if (oldFileStream)
		{
			strcpy(lineBuffer, "#");
			while (lineBuffer[0] == '#')
			{
				status =
					kernelFileStreamReadLine(oldFileStream, 256, lineBuffer);
				if (status < 0)
					break;

				// See if there's anything but whitespace, or comment here
				hasContent = 0;
				for (count2 = 0; count2 < status; count2 ++)
				{
					if (isspace(lineBuffer[count2]))
						continue;
					else
					{
						if (lineBuffer[count2] != '#')
							hasContent = 1;
						break;
					}
				}

				if (!hasContent)
				{
					// Just write it back out verbatim
					status =
						kernelFileStreamWriteLine(newFileStream, lineBuffer);
					if (status < 0)
					{
						kernelFileStreamClose(oldFileStream);
						kernelFree(oldFileStream);
						kernelFileStreamClose(newFileStream);
						kernelFree(newFileStream);
						return (status);
					}
				}
			}
		}

		variable = kernelVariableListGetVariable(list, count1);
		value = kernelVariableListGet(list, variable);

		sprintf(lineBuffer, "%s=%s", variable, value);

		status = kernelFileStreamWriteLine(newFileStream, lineBuffer);
		if (status < 0)
		{
			kernelFileStreamClose(oldFileStream);
			kernelFree(oldFileStream);
			kernelFileStreamClose(newFileStream);
			kernelFree(newFileStream);
			return (status);
		}
	}

	// Close things up
	if (oldFileStream)
	{
		kernelFileStreamClose(oldFileStream);
		kernelFree(oldFileStream);
	}

	status = kernelFileStreamClose(newFileStream);

	kernelFree(newFileStream);

	if (status < 0)
		return (status);

	if (oldFileStream)
	{
		// Move the temporary file to the destination
		status = kernelFileMove(tmpName, fileName);
		if (status < 0)
			return (status);
	}

	return (status = 0);
}


int kernelConfigGet(const char *fileName, const char *variable, char *buffer,
	unsigned buffSize)
{
	// This is a convenience function giving the ability to quickly get a
	// single variable value from a config file.  Uses the kernelConfigRead
	// function, above.

	int status = 0;
	variableList list;
	const char *value = NULL;

	// Check params
	if (!fileName || !variable || !buffer || !buffSize)
		return (status = ERR_NULLPARAMETER);

	// Try to read in the config file
	status = kernelConfigRead(fileName, &list);
	if (status < 0)
		return (status);

	// Try to get the value
	value = kernelVariableListGet(&list, variable);

	if (value)
	{
		strncpy(buffer, value, buffSize);
		status = 0;
	}
	else
	{
		buffer[0] = '\0';
		status = ERR_NOSUCHENTRY;
	}

	// Deallocate the list.
	kernelVariableListDestroy(&list);

	return (status);
}


int kernelConfigSet(const char *fileName, const char *variable,
	const char *value)
{
	// This is a convenience function giving the ability to quickly set a single
	// variable value in a config file.  Uses the kernelConfigRead and
	// kernelConfigWrite functions, above.

	int status = 0;
	variableList list;

	// Check params
	if (!fileName || !variable || !value)
		return (status = ERR_NULLPARAMETER);

	// Try to read in the config file
	status = kernelConfigRead(fileName, &list);
	if (status < 0)
		return (status);

	// Try to set the value
	status = kernelVariableListSet(&list, variable, value);
	if (status < 0)
		goto out;

	// Try to write the config file
	status = kernelConfigWrite(fileName, &list);

out:
	// Deallocate the list.
	kernelVariableListDestroy(&list);
	return (status);
}


int kernelConfigUnset(const char *fileName, const char *variable)
{
	// This is a convenience function giving the ability to quickly unset a
	// single variable value in a config file.  Uses the kernelConfigRead and
	// kernelConfigWrite functions, above.

	int status = 0;
	variableList list;

	// Check params
	if (!fileName || !variable)
		return (status = ERR_NULLPARAMETER);

	// Try to read in the config file
	status = kernelConfigRead(fileName, &list);
	if (status < 0)
		return (status);

	// Try to unset the value
	status = kernelVariableListUnset(&list, variable);
	if (status < 0)
		goto out;

	// Try to write the config file
	status = kernelConfigWrite(fileName, &list);

out:
	// Deallocate the list.
	kernelVariableListDestroy(&list);
	return (status);
}


int kernelReadSymbols(void)
{
	// This will attempt to read the symbol table from the kernel executable,
	// and attach it to the kernel process.

	int status = 0;
	loaderSymbolTable *kernelSymbols = NULL;

	// See if there is a kernel file.
	status = kernelFileFind(KERNEL_FILE, NULL);
	if (status < 0)
	{
		kernelLog("No kernel file \"%s\"", KERNEL_FILE);
		return (status);
	}

	// Make a log message
	kernelLog("Reading kernel symbols from \"%s\"", KERNEL_FILE);

	kernelSymbols = kernelLoaderGetSymbols(KERNEL_FILE);
	if (!kernelSymbols)
	{
		kernelDebugError("Couldn't load kernel symbols");
		return (status = ERR_NODATA);
	}

	status = kernelMultitaskerSetSymbols(KERNELPROCID, kernelSymbols);
	if (status < 0)
	{
		kernelError(kernel_warn, "Couldn't set kernel symbols");
		return (status);
	}

	return (status = 0);
}


time_t kernelUnixTime(void)
{
	// Unix time is seconds since 00:00:00 January 1, 1970

	time_t returnTime = 0;
	struct tm timeStruct;

	// Get the date and time according to the kernel
	if (kernelRtcDateTime(&timeStruct) < 0)
		return (returnTime = -1);

	return (mktime(&timeStruct));
}


int kernelGuidGenerate(guid *g)
{
	// Generates our best approximation of a GUID, which is not to spec but
	// so what, really?  Will generate GUIDs unique enough for us.

	int status = 0;
	unsigned long long longTime = 0;
	static unsigned clockSeq = 0;
	static lock globalLock;
	static int initialized = 0;

	// Check params
	if (!g)
	{
		kernelError(kernel_error, "NULL parameter");
		return (status = ERR_NULLPARAMETER);
	}

	if (!initialized)
	{
		clockSeq = kernelRandomUnformatted();
		memset((void *) &globalLock, 0, sizeof(lock));
		initialized = 1;
	}

	// Get the lock
	status = kernelLockGet(&globalLock);
	if (status < 0)
		return (status);

	// Increment the clock
	clockSeq += 1;

	// Get the time as a 60-bit value representing a count of 100-nanosecond
	// intervals since 00:00:00.00, 15 October 1582 (the date of Gregorian
	// reform to the Christian calendar).
	//
	// Umm.  Overkill on the time thing?  Maybe?  Nanoseconds since 1582?
	// Why are we wasting the number of nanoseconds in the 400 years before
	// most of us ever touched computers?

	longTime = ((kernelUnixTime() * 10000000) + 0x01b21dd213814000ULL);

	g->timeLow = (unsigned)(longTime & 0x00000000FFFFFFFF);
	g->timeMid = (unsigned)((longTime >> 32) & 0x0000FFFF);
	g->timeHighVers = (((unsigned)(longTime >> 48) & 0x0FFF) | 0x1000);
	g->clockSeqRes = (((clockSeq >> 16) & 0x3FFF) | 0x8000);
	g->clockSeqLow = (clockSeq & 0xFFFF);
	// Random node ID
	*((unsigned *) g->node) = kernelRandomUnformatted();
	*((unsigned short *)(g->node + 4)) = (kernelRandomUnformatted() >> 16);

	kernelLockRelease(&globalLock);

	return (status = 0);
}


unsigned kernelCrc32(void *buff, unsigned len, unsigned *lastCrc)
{
	// Generates a CRC32.

	register char *p = buff;
	register unsigned crc = 0;

	if (lastCrc)
		crc = *lastCrc;

	crc ^= ~0U;

	while (len-- > 0)
		crc = (unsigned)(crcTable[(crc ^ *p++) & 0xFFL] ^ (unsigned)(crc >> 8));

	if (lastCrc)
		*lastCrc = crc;

	return (crc ^ ~0U);
}


void kernelPause(int seconds)
{
	// Print a message, and wait for a key press, or else the specified number
	// of seconds

	char buffer[32];
	int currentSeconds = 0;
	textAttrs attrs;

	memset(&attrs, 0, sizeof(textAttrs));
	attrs.flags = TEXT_ATTRS_REVERSE;

	kernelTextInputSetEcho(0);

	if (seconds)
 	{
		for (currentSeconds = kernelRtcReadSeconds(); seconds > 0; seconds--)
		{
			snprintf(buffer, 32, " --- Pausing for %d seconds ---", seconds);
			kernelTextPrintAttrs(&attrs, "%s", buffer);

			while (kernelRtcReadSeconds() == currentSeconds)
				kernelMultitaskerYield();

			currentSeconds = kernelRtcReadSeconds();
			eraseLine();
		}
	}
	else
	{
		kernelTextPrintAttrs(&attrs, " --- Press any key to continue ---");

		// Do a loop, manually polling the keyboard input buffer, looking for
		// the key press.
		while (!kernelTextInputCount())
			kernelMultitaskerYield();

		kernelTextInputRemoveAll();

		eraseLine();
	}

	kernelTextInputSetEcho(1);

	return;
}

