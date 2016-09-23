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
//  kernelDriverManagement.c
//

#include "kernelDriver.h"
#include "kernelText.h"
#include "kernelFilesystem.h"
#include "kernelError.h"
#include <string.h>

// Arrays of the kernel's built-in (non-device) drivers.  In no particular
// order, except that the initializations are done in sequence

static void *consoleDriverInits[] = {
	kernelTextConsoleInitialize,
	kernelGraphicConsoleInitialize,
	(void *) -1
};

static void *filesystemDriverInits[] = {
	kernelFilesystemExtInitialize,
	kernelFilesystemFatInitialize,
	kernelFilesystemIsoInitialize,
	kernelFilesystemLinuxSwapInitialize,
	kernelFilesystemNtfsInitialize,
	kernelFilesystemUdfInitialize,
	(void *) -1
};

// A structure to hold the kernel's built-in console drivers
static struct {
	kernelTextOutputDriver *textConsoleDriver;
	kernelTextOutputDriver *graphicConsoleDriver;

} consoleDrivers = {
	NULL, // Text-mode console driver
	NULL  // Graphic-mode console driver
};

// A structure to hold all the kernel's built-in filesystem drivers.
static struct {
	kernelFilesystemDriver *extDriver;
	kernelFilesystemDriver *fatDriver;
	kernelFilesystemDriver *isoDriver;
	kernelFilesystemDriver *linuxSwapDriver;
	kernelFilesystemDriver *ntfsDriver;
	kernelFilesystemDriver *udfDriver;

} filesystemDrivers = {
	NULL, // EXT filesystem driver
	NULL, // FAT filesystem driver
	NULL, // ISO filesystem driver
	NULL, // Linux swap filesystem driver
	NULL, // NTFS filesystem driver
	NULL  // UDF filesystem driver
};


static int driversInitialize(void *initArray[])
{
	// This function calls the driver initialize() functions of the supplied

	int status = 0;
	int errors = 0;
	int (*driverInit)(void) = NULL;
	int count;

	// Loop through all of the initialization functions we have
	for (count = 0; ; count ++)
	{
		driverInit = initArray[count];

		if (driverInit == (void *) -1)
			break;

		if (!driverInit)
			continue;

		// Call the initialization.  The driver should then call
		// kernelSoftwareDriverRegister() when it has finished initializing
		status = driverInit();
		if (status < 0)
			errors++;
	}

	if (errors)
		return (status = ERR_NOTINITIALIZED);
	else
		return (status = 0);
}


/////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////
//
//  Below here, the functions are exported for external use
//
/////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////

int kernelConsoleDriversInitialize(void)
{
	// This function is called during startup so we can call the initialize()
	// functions of the console drivers
	return (driversInitialize(consoleDriverInits));
}


int kernelFilesystemDriversInitialize(void)
{
	// This function is called during startup so we can call the initialize()
	// functions of the filesystem drivers
	return (driversInitialize(filesystemDriverInits));
}


int kernelSoftwareDriverRegister(kernelSoftwareDriverType type, void *driver)
{
	// This function is called by the software drivers during their
	// initialize() call, so that we can add them to the table of known
	// drivers.

	int status = 0;

	// Check params
	if (!driver)
	{
		kernelError(kernel_error, "NULL parameter");
		return (status = ERR_NULLPARAMETER);
	}

	switch (type)
	{
		case extDriver:
			filesystemDrivers.extDriver = driver;
			break;
		case fatDriver:
			filesystemDrivers.fatDriver = driver;
			break;
		case isoDriver:
			filesystemDrivers.isoDriver = driver;
			break;
		case linuxSwapDriver:
			filesystemDrivers.linuxSwapDriver = driver;
			break;
		case ntfsDriver:
			filesystemDrivers.ntfsDriver = driver;
			break;
		case udfDriver:
			filesystemDrivers.udfDriver = driver;
			break;
		case textConsoleDriver:
			consoleDrivers.textConsoleDriver = driver;
			break;
		case graphicConsoleDriver:
			consoleDrivers.graphicConsoleDriver = driver;
			break;
		default:
			kernelError(kernel_error, "Unknown driver type %d", type);
			return (status = ERR_NOSUCHENTRY);
	}

	return (status = 0);
}


void *kernelSoftwareDriverGet(kernelSoftwareDriverType type)
{
	switch (type)
	{
		case extDriver:
			// Return the EXT filesystem driver
			return (filesystemDrivers.extDriver);
		case fatDriver:
			// Return the FAT filesystem driver
			return (filesystemDrivers.fatDriver);
		case isoDriver:
			// Return the ISO filesystem driver
			return (filesystemDrivers.isoDriver);
		case linuxSwapDriver:
			// Return the Linux swap filesystem driver
			return (filesystemDrivers.linuxSwapDriver);
		case ntfsDriver:
			// Return the NTFS filesystem driver
			return (filesystemDrivers.ntfsDriver);
		case udfDriver:
			// Return the UDF filesystem driver
			return (filesystemDrivers.udfDriver);
		case textConsoleDriver:
			// Return the text mode console driver
			return (consoleDrivers.textConsoleDriver);
		case graphicConsoleDriver:
			// Return the graphic mode console driver
			return (consoleDrivers.graphicConsoleDriver);
		default:
			return (NULL);
	}
}

