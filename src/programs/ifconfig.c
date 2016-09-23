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
//  ifconfig.c
//

// Displays information about the system's network devices

/* This is the text that appears when a user requests help about this program
<help>

 -- ifconfig --

Network device control.

Usage:
  ifconfig [-T] [device_name]

This command will show information about the system's network devices.

Options:
-T              : Force text mode operation

</help>
*/

#include <errno.h>
#include <libintl.h>
#include <locale.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/api.h>
#include <sys/env.h>
#include <sys/paths.h>

#define _(string) gettext(string)

#define WINDOW_TITLE		_("Network Devices")
#define ENABLED_STARTUP		_("Enabled at startup")
#define HOST_NAME			_("Host name")
#define DOMAIN_NAME			_("Domain name")
#define OK					_("OK")
#define CANCEL				_("Cancel")
#define NO_DEVICES			_("No supported network devices.")
#define KERNELCONF			PATH_SYSTEM_CONFIG "/kernel.conf"

static int graphics = 0;
static int numDevices = 0;
static int networkEnabled = 0;
static int readOnly = 1;
static objectKey window = NULL;
static objectKey enabledLabel = NULL;
static objectKey enableButton = NULL;
static objectKey enableCheckbox = NULL;
static objectKey hostLabel = NULL;
static objectKey domainLabel = NULL;
static objectKey hostField = NULL;
static objectKey domainField = NULL;
static objectKey deviceLabel[NETWORK_MAX_ADAPTERS];
static objectKey okButton = NULL;
static objectKey cancelButton = NULL;


__attribute__((format(printf, 1, 2)))
static void error(const char *format, ...)
{
	// Generic error message code for either text or graphics modes

	va_list list;
	char output[MAXSTRINGLENGTH];

	va_start(list, format);
	vsnprintf(output, MAXSTRINGLENGTH, format, list);
	va_end(list);

	if (graphics)
		windowNewErrorDialog(NULL, _("Error"), output);
	else
		printf("\n%s\n", output);
}


static int devString(char *name, char *buffer)
{
	int status = 0;
	networkDevice dev;
	char *link = NULL;

	status = networkDeviceGet(name, &dev);
	if (status < 0)
	{
		error(_("Can't get info for device %s"), name);
		return (status);
	}

	switch (dev.linkProtocol)
	{
		case NETWORK_LINKPROTOCOL_ETHERNET:
			link = _("Ethernet");
			break;
		default:
			link = _("Unknown");
			break;
	}

	sprintf(buffer,
		_("%s   Link encap:%s  HWaddr %02x:%02x:%02x:%02x:%02x:%02x\n"
		"       inet addr:%d.%d.%d.%d  Bcast:%d.%d.%d.%d  Mask:%d.%d.%d."
		"%d\n"
		"       RX packets:%u errors:%u dropped:%u overruns:%u\n"
		"       TX packets:%u errors:%u dropped:%u overruns:%u\n"
		"       %s, %s collisions:%u txqueuelen:%u Interrupt:%d"),
		dev.name, link,
		dev.hardwareAddress.bytes[0], dev.hardwareAddress.bytes[1],
		dev.hardwareAddress.bytes[2], dev.hardwareAddress.bytes[3],
		dev.hardwareAddress.bytes[4], dev.hardwareAddress.bytes[5],
		dev.hostAddress.bytes[0], dev.hostAddress.bytes[1],
		dev.hostAddress.bytes[2], dev.hostAddress.bytes[3],
		dev.broadcastAddress.bytes[0], dev.broadcastAddress.bytes[1],
		dev.broadcastAddress.bytes[2], dev.broadcastAddress.bytes[3],
		dev.netMask.bytes[0], dev.netMask.bytes[1],
		dev.netMask.bytes[2], dev.netMask.bytes[3],
		dev.recvPackets, dev.recvErrors, dev.recvDropped, dev.recvOverruns,
		dev.transPackets, dev.transErrors, dev.transDropped,
		dev.transOverruns,
		((dev.flags & NETWORK_ADAPTERFLAG_LINK)? _("LINK") : _("NOLINK")),
		((dev.flags & NETWORK_ADAPTERFLAG_RUNNING)? _("UP") : _("DOWN")),
		dev.collisions, dev.transQueueLen, dev.interruptNum);

	return (status = 0);
}


static int printDevices(char *arg)
{
	int status = 0;
	char name[NETWORK_ADAPTER_MAX_NAMELENGTH];
	char buffer[MAXSTRINGLENGTH];
	int count;

	// Did the user specify a list of device names?
	if (arg)
	{
		// Get the device information
		status = devString(arg, buffer);
		if (status < 0)
			return (status);

		printf("%s\n\n", buffer);
	}
	else
	{
		if (numDevices)
		{
			// Show all of them
			for (count = 0; count < numDevices; count ++)
			{
				sprintf(name, "net%d", count);

				// Get the device information
				status = devString(name, buffer);
				if (status < 0)
					return (status);

				printf("%s\n\n", buffer);
			}
		}
		else
			printf("%s\n\n", NO_DEVICES);
	}

	return (status = 0);
}


static void updateEnabled(void)
{
	// Update the networking enabled widgets

	char name[NETWORK_ADAPTER_MAX_NAMELENGTH];
	char *buffer = NULL;
	char tmp[128];
	int count;

	snprintf(tmp, 128, _("Networking is %s"),
		(networkEnabled? _("enabled") : _("disabled")));
	windowComponentSetData(enabledLabel, tmp, strlen(tmp), 1 /* redraw */);
	windowComponentSetData(enableButton,
		(networkEnabled? _("Disable") : _("Enable")), 8, 1 /* redraw */);

	// Update the device strings as well.
	buffer = malloc(MAXSTRINGLENGTH);
	if (buffer)
	{
		for (count = 0; count < numDevices; count ++)
		{
			if (deviceLabel[count])
			{
				sprintf(name, "net%d", count);

				if (devString(name, buffer) < 0)
					continue;

				windowComponentSetData(deviceLabel[count], buffer,
					MAXSTRINGLENGTH, 1 /* redraw */);
			}
		}

		free(buffer);
	}
}


static void updateHostName(void)
{
	char hostName[NETWORK_MAX_HOSTNAMELENGTH];
	char domainName[NETWORK_MAX_DOMAINNAMELENGTH];
	const char *value = NULL;
	variableList kernelConf;

	if (networkEnabled)
	{
		if (networkGetHostName(hostName, NETWORK_MAX_HOSTNAMELENGTH) >= 0)
			windowComponentSetData(hostField, hostName,
				NETWORK_MAX_HOSTNAMELENGTH, 1 /* redraw */);

		if (networkGetDomainName(domainName,
			NETWORK_MAX_DOMAINNAMELENGTH) >= 0)
		{
			windowComponentSetData(domainField, domainName,
				NETWORK_MAX_DOMAINNAMELENGTH, 1 /* redraw */);
		}
	}
	else
	{
		if (configRead(KERNELCONF, &kernelConf) >= 0)
		{
			value = variableListGet(&kernelConf, "network.hostname");
			if (value)
				windowComponentSetData(hostField, (void *) value,
					NETWORK_MAX_HOSTNAMELENGTH, 1 /* redraw */);

			value = variableListGet(&kernelConf, "network.domainname");
			if (value)
				windowComponentSetData(domainField, (void *) value,
					NETWORK_MAX_DOMAINNAMELENGTH, 1 /* redraw */);

			variableListDestroy(&kernelConf);
		}
	}

	return;
}


static void refreshWindow(void)
{
	// We got a 'window refresh' event (probably because of a language switch),
	// so we need to update things

	// Re-get the language setting
	setlocale(LC_ALL, getenv(ENV_LANG));
	textdomain("ifconfig");

	// Re-get the character set
	if (getenv(ENV_CHARSET))
		windowSetCharSet(window, getenv(ENV_CHARSET));

	// Refresh the 'networking enabled' label, button, and device strings
	updateEnabled();

	// Refresh the 'enabled at startup' checkbox
	windowComponentSetData(enableCheckbox, ENABLED_STARTUP,
		strlen(ENABLED_STARTUP), 1 /* redraw */);

	// Refresh the 'host name' label
	windowComponentSetData(hostLabel, HOST_NAME, strlen(HOST_NAME),
		1 /* redraw */);

	// Refresh the 'domain name' label
	windowComponentSetData(domainLabel, DOMAIN_NAME, strlen(DOMAIN_NAME),
		1 /* redraw */);

	// Refresh the 'ok' button
	windowComponentSetData(okButton, OK, strlen(OK), 1 /* redraw */);

	// Refresh the 'cancel' button
	windowComponentSetData(cancelButton, CANCEL, strlen(CANCEL),
		1 /* redraw */);

	// Refresh the window title
	windowSetTitle(window, WINDOW_TITLE);
}


static void eventHandler(objectKey key, windowEvent *event)
{
	objectKey enableDialog = NULL;
	int selected = 0;
	variableList kernelConf;
	char hostName[NETWORK_MAX_HOSTNAMELENGTH];
	char domainName[NETWORK_MAX_DOMAINNAMELENGTH];

	// Check for window events.
	if (key == window)
	{
		// Check for window refresh
		if (event->type == EVENT_WINDOW_REFRESH)
			refreshWindow();

		// Check for the window being closed
		else if (event->type == EVENT_WINDOW_CLOSE)
		{
			windowGuiStop();
			windowDestroy(window);
		}
	}

	// Check for the user clicking the enable/disable networking button
	else if ((key == enableButton) && (event->type == EVENT_MOUSE_LEFTUP))
	{
		// The user wants to enable or disable networking button.  Make a
		// little dialog while we're doing this because enabling can take a few
		// seconds
		enableDialog = windowNewBannerDialog(window,
			(networkEnabled? _("Shutting down networking") :
				_("Initializing networking")), _("One moment please..."));

		if (networkEnabled)
			networkShutdown();
		else
			networkInitialize();

		windowDestroy(enableDialog);

		networkEnabled = networkInitialized();
		updateEnabled();
		updateHostName();
	}

	// Check for the user clicking the 'OK' button
	else if ((key == okButton) && (event->type == EVENT_MOUSE_LEFTUP))
	{
		windowComponentGetSelected(enableCheckbox, &selected);
		windowComponentGetData(hostField, hostName,
			NETWORK_MAX_HOSTNAMELENGTH);
		windowComponentGetData(domainField, domainName,
			NETWORK_MAX_DOMAINNAMELENGTH);

		// Set new values in the kernel
		networkSetHostName(hostName, NETWORK_MAX_HOSTNAMELENGTH);
		networkSetDomainName(domainName, NETWORK_MAX_DOMAINNAMELENGTH);

		// Try to read and change the kernel config
		if (!readOnly && configRead(KERNELCONF, &kernelConf) >= 0)
		{
			variableListSet(&kernelConf, "network", (selected? "yes" : "no"));
			variableListSet(&kernelConf, "network.hostname", hostName);
			variableListSet(&kernelConf, "network.domainname", domainName);
			configWrite(KERNELCONF, &kernelConf);
			variableListDestroy(&kernelConf);
		}

		windowGuiStop();
		windowDestroy(window);
	}

	// Check for the user clicking the 'cancel' button
	else if ((key == cancelButton) && (event->type == EVENT_MOUSE_LEFTUP))
	{
		windowGuiStop();
		windowDestroy(window);
	}

	return;
}


static int constructWindow(char *arg)
{
	int status = 0;
	componentParameters params;
	objectKey container = NULL;
	char name[NETWORK_ADAPTER_MAX_NAMELENGTH];
	char *buffer = NULL;
	char tmp[8];
	int count;

	// Create a new window
	window = windowNew(multitaskerGetCurrentProcessId(), WINDOW_TITLE);
	if (!window)
		return (status = ERR_NOTINITIALIZED);

	memset(&params, 0, sizeof(componentParameters));
	params.gridWidth = 1;
	params.gridHeight = 1;
	params.padLeft = 5;
	params.padRight = 5;
	params.padTop = 5;
	params.orientationX = orient_left;
	params.orientationY = orient_middle;

	// A container for the 'enable networking' stuff
	params.gridWidth = 2;
	container = windowNewContainer(window, "enable", &params);

	// Make a label showing the status of networking
	params.gridWidth = 1;
	params.padTop = 0;
	params.flags |= WINDOW_COMPFLAG_FIXEDWIDTH;
	enabledLabel = windowNewTextLabel(container, _("Networking is disabled"),
		&params);

	// Make a button for enabling/disabling networking
	params.gridX = 1;
	enableButton = windowNewButton(container, _("Enable"), NULL, &params);
	windowRegisterEventHandler(enableButton, &eventHandler);

	// Make a checkbox so the user can choose to always enable/disable
	params.gridX = 2;
	enableCheckbox = windowNewCheckbox(container, ENABLED_STARTUP,
		&params);
	params.gridY += 1;

	// Try to find out whether networking is enabled
	if (configGet(KERNELCONF, "network", tmp, 8) >= 0)
	{
		if (!strncmp(tmp, "yes", 8))
			windowComponentSetSelected(enableCheckbox, 1);
		else
			windowComponentSetSelected(enableCheckbox, 0);
	}

	if (readOnly)
		windowComponentSetEnabled(enableCheckbox, 0);

	updateEnabled();

	// A container for the host and domain name stuff
	params.gridX = 0;
	params.gridWidth = 2;
	params.padTop = 5;
	params.flags &= ~WINDOW_COMPFLAG_FIXEDWIDTH;
	container = windowNewContainer(window, "hostname", &params);

	params.gridWidth = 1;
	hostLabel = windowNewTextLabel(container, HOST_NAME, &params);

	params.gridX = 1;
	params.padTop = 0;
	domainLabel = windowNewTextLabel(container, DOMAIN_NAME, &params);
	params.gridY += 1;

	params.gridX = 0;
	params.padBottom = 5;
	hostField = windowNewTextField(container, 16, &params);
	windowRegisterEventHandler(hostField, &eventHandler);

	params.gridX = 1;
	domainField = windowNewTextField(container, 16, &params);
	windowRegisterEventHandler(domainField, &eventHandler);
	params.gridY += 1;

	updateHostName();

	params.gridX = 0;
	params.gridY += 1;
	params.gridWidth = 2;
	params.padTop = 5;
	params.padBottom = 0;
	params.orientationX = orient_center;
	params.flags &= ~WINDOW_COMPFLAG_FIXEDWIDTH;

	buffer = malloc(MAXSTRINGLENGTH);
	if (!buffer)
		return (status = ERR_MEMORY);

	// Did the user specify a device name?
	if (arg)
	{
		// Get the device information
		status = devString(arg, buffer);
		if (status < 0)
		{
			free(buffer);
			return (status);
		}

		windowNewTextLabel(window, buffer, &params);
		params.gridY += 1;
	}
	else
	{
		if (numDevices)
		{
			// Show all of them
			for (count = 0; count < numDevices; count ++)
			{
				sprintf(name, "net%d", count);

				// Get the device information
				status = devString(name, buffer);
				if (status < 0)
				{
					free(buffer);
					return (status);
				}

				deviceLabel[count] = windowNewTextLabel(window, buffer,
					&params);
				params.gridY += 1;
			}
		}
		else
		{
			windowNewTextLabel(window, NO_DEVICES, &params);
			params.gridY += 1;
		}
	}

	free(buffer);

	// Create an 'OK' button
	params.gridWidth = 1;
	params.padBottom = 5;
	params.orientationX = orient_right;
	params.flags |= WINDOW_COMPFLAG_FIXEDWIDTH;
	okButton = windowNewButton(window, OK, NULL, &params);
	windowRegisterEventHandler(okButton, &eventHandler);
	windowComponentFocus(okButton);

	// Create a 'Cancel' button
	params.gridX = 1;
	params.orientationX = orient_left;
	cancelButton = windowNewButton(window, CANCEL, NULL, &params);
	windowRegisterEventHandler(cancelButton, &eventHandler);

	// Register an event handler to catch window close events
	windowRegisterEventHandler(window, &eventHandler);

	windowSetVisible(window, 1);

	return (status = 0);
}


int main(int argc, char *argv[])
{
	int status = 0;
	char opt;
	char *arg = NULL;
	disk sysDisk;

	setlocale(LC_ALL, getenv(ENV_LANG));
	textdomain("ifconfig");

	// Are graphics enabled?
	graphics = graphicsAreEnabled();

	// Check options
	while (strchr("T?", (opt = getopt(argc, argv, "T"))))
	{
		switch (opt)
		{
			case 'T':
				// Force text mode
				graphics = 0;
				break;

			default:
				error(_("Unknown option '%c'"), optopt);
				return (status = ERR_INVALID);
		}
	}

	numDevices = networkDeviceGetCount();
	if (numDevices < 0)
	{
		error("%s", _("Can't get the count of network devices"));
		return (numDevices);
	}

	// Is the last argument a non-option?
	if ((argc > 1) && (argv[argc - 1][0] != '-'))
		arg = argv[argc - 1];

	// Find out whether we are currently running on a read-only filesystem
	memset(&sysDisk, 0, sizeof(disk));
	if (!fileGetDisk(KERNELCONF, &sysDisk))
		readOnly = sysDisk.readOnly;

	networkEnabled = networkInitialized();

	if (graphics)
	{
		status = constructWindow(arg);
		if (status >= 0)
			windowGuiRun();
	}
	else
	{
		status = printDevices(arg);
	}

	return (status);
}

