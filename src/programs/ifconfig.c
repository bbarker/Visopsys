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
//  ifconfig.c
//

// Displays information about the system's network devices

/* This is the text that appears when a user requests help about this program
<help>

 -- ifconfig --

Network device control.

Usage:
  ifconfig [-T] [-e] [-d] [device_name]

This command will show information about the system's network devices, and
allow a privileged user to perform various network administration tasks.

In text mode:

  The -d option will will disable networking, de-configuring network devices.

  The -e option will enable networking, causing network devices to be
  configured.

In graphics mode, the program is interactive and the user can view network
device status and perform tasks visually.

Options:
-d  : Disable networking (text mode).
-e  : Enable networking (text mode).
-T  : Force text mode operation

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
#include <sys/kernconf.h>
#include <sys/paths.h>

#define _(string) gettext(string)

#define WINDOW_TITLE		_("Network Devices")
#define ENABLE				_("Enable")
#define DISABLE				_("Disable")
#define ENABLED_STARTUP		_("Enabled at startup")
#define HOST_NAME			_("Host name")
#define DOMAIN_NAME			_("Domain name")
#define DEVICES				_("Devices")
#define OK					_("OK")
#define CANCEL				_("Cancel")
#define NO_DEVICES			_("No supported network devices.")
#define DEVSTRMAXVALUE		32

typedef struct {
	char *label;
	char value[DEVSTRMAXVALUE];

} devStringItem;

typedef struct {
	char name[NETWORK_DEVICE_MAX_NAMELENGTH];
	devStringItem linkEncap;
	devStringItem hwAddr;
	devStringItem inetAddr;
	devStringItem mask;
	devStringItem bcast;
	devStringItem gateway;
	devStringItem dns;
	devStringItem rxPackets;
	devStringItem rxErrors;
	devStringItem rxDropped;
	devStringItem rxOverruns;
	devStringItem txPackets;
	devStringItem txErrors;
	devStringItem txDropped;
	devStringItem txOverruns;
	devStringItem linkStat;
	devStringItem running;
	devStringItem collisions;
	devStringItem txQueueLen;
	devStringItem interrupt;

} devStrings;

static int graphics = 0;
static int numDevices = 0;
static int enabled = 0;
static int readOnly = 1;
static objectKey window = NULL;
static objectKey enabledLabel = NULL;
static objectKey enableButton = NULL;
static objectKey enableCheckbox = NULL;
static objectKey hostLabel = NULL;
static objectKey domainLabel = NULL;
static objectKey hostField = NULL;
static objectKey domainField = NULL;
static objectKey devicesLabel = NULL;
static objectKey deviceList = NULL;
static listItemParameters *deviceListParams = NULL;
static objectKey deviceEnableButton = NULL;
static objectKey deviceStringLabel = NULL;
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


static devStrings *getDevStrings(networkDevice *dev)
{
	devStrings *str = NULL;
	char *link = NULL;

	str = calloc(1, sizeof(devStrings));
	if (!str)
		return (str);

	switch (dev->linkProtocol)
	{
		case NETWORK_LINKPROTOCOL_LOOP:
			link = _("Local Loopback");
			break;
		case NETWORK_LINKPROTOCOL_ETHERNET:
			link = _("Ethernet");
			break;
		default:
			link = _("Unknown");
			break;
	}

	strncpy(str->name, dev->name, NETWORK_DEVICE_MAX_NAMELENGTH);

	str->linkEncap.label = _("Link encap");
	snprintf(str->linkEncap.value, DEVSTRMAXVALUE, "%s", link);

	str->hwAddr.label = _("HWaddr");
	snprintf(str->hwAddr.value, DEVSTRMAXVALUE,
		"%02x:%02x:%02x:%02x:%02x:%02x",
		dev->hardwareAddress.byte[0], dev->hardwareAddress.byte[1],
		dev->hardwareAddress.byte[2], dev->hardwareAddress.byte[3],
		dev->hardwareAddress.byte[4], dev->hardwareAddress.byte[5]);

	str->inetAddr.label = _("inet addr");
	snprintf(str->inetAddr.value, DEVSTRMAXVALUE, "%d.%d.%d.%d",
		dev->hostAddress.byte[0], dev->hostAddress.byte[1],
		dev->hostAddress.byte[2], dev->hostAddress.byte[3]);

	str->mask.label = _("Mask");
	snprintf(str->mask.value, DEVSTRMAXVALUE, "%d.%d.%d.%d",
		dev->netMask.byte[0], dev->netMask.byte[1],
		dev->netMask.byte[2], dev->netMask.byte[3]);

	str->bcast.label = _("Bcast");
	snprintf(str->bcast.value, DEVSTRMAXVALUE, "%d.%d.%d.%d",
		dev->broadcastAddress.byte[0], dev->broadcastAddress.byte[1],
		dev->broadcastAddress.byte[2], dev->broadcastAddress.byte[3]);

	str->gateway.label = _("Gateway");
	snprintf(str->gateway.value, DEVSTRMAXVALUE, "%d.%d.%d.%d",
		dev->gatewayAddress.byte[0], dev->gatewayAddress.byte[1],
		dev->gatewayAddress.byte[2], dev->gatewayAddress.byte[3]);

	str->dns.label = _("DNS");
	snprintf(str->dns.value, DEVSTRMAXVALUE, "%d.%d.%d.%d",
		dev->dnsAddress.byte[0], dev->dnsAddress.byte[1],
		dev->dnsAddress.byte[2], dev->dnsAddress.byte[3]);

	str->rxPackets.label = _("RX packets");
	snprintf(str->rxPackets.value, DEVSTRMAXVALUE, "%u", dev->recvPackets);

	str->rxErrors.label = _("errors");
	snprintf(str->rxErrors.value, DEVSTRMAXVALUE, "%u", dev->recvErrors);

	str->rxDropped.label = _("dropped");
	snprintf(str->rxDropped.value, DEVSTRMAXVALUE, "%u", dev->recvDropped);

	str->rxOverruns.label = _("overruns");
	snprintf(str->rxOverruns.value, DEVSTRMAXVALUE, "%u", dev->recvOverruns);

	str->txPackets.label = _("TX packets");
	snprintf(str->txPackets.value, DEVSTRMAXVALUE, "%u", dev->transPackets);

	str->txErrors.label = _("errors");
	snprintf(str->txErrors.value, DEVSTRMAXVALUE, "%u", dev->transErrors);

	str->txDropped.label = _("dropped");
	snprintf(str->txDropped.value, DEVSTRMAXVALUE, "%u", dev->transDropped);

	str->txOverruns.label = _("overruns");
	snprintf(str->txOverruns.value, DEVSTRMAXVALUE, "%u", dev->transOverruns);

	str->linkStat.label = _("link status");
	snprintf(str->linkStat.value, DEVSTRMAXVALUE, "%s",
		((dev->flags & NETWORK_DEVICEFLAG_LINK)? _("LINK") : _("NOLINK")));

	str->running.label = _("running");
	snprintf(str->running.value, DEVSTRMAXVALUE, "%s",
		((dev->flags & NETWORK_DEVICEFLAG_RUNNING)? _("UP") : _("DOWN")));

	str->collisions.label = _("collisions");
	snprintf(str->collisions.value, DEVSTRMAXVALUE, "%u", dev->collisions);

	str->txQueueLen.label = _("txqueuelen");
	snprintf(str->txQueueLen.value, DEVSTRMAXVALUE, "%u", dev->transQueueLen);

	str->interrupt.label = _("Interrupt");
	snprintf(str->interrupt.value, DEVSTRMAXVALUE, "%d", dev->interruptNum);

	return (str);
}


static int getDevString(char *name, char *buffer)
{
	int status = 0;
	networkDevice dev;
	devStrings *str = NULL;

	status = networkDeviceGet(name, &dev);
	if (status < 0)
	{
		error(_("Can't get info for device %s"), name);
		return (status);
	}

	str = getDevStrings(&dev);
	if (!str)
		return (status = ERR_MEMORY);

	sprintf(buffer, "%s   %s:%s  %s %s\n"
		"       %s:%s  %s:%s  %s:%s\n"
		"       %s:%s %s:%s %s:%s %s:%s\n"
		"       %s:%s %s:%s %s:%s %s:%s\n"
		"       %s, %s %s:%s %s:%s %s:%s",
		str->name, str->linkEncap.label, str->linkEncap.value,
		str->hwAddr.label, str->hwAddr.value,
		str->inetAddr.label, str->inetAddr.value,
		str->bcast.label, str->bcast.value,
		str->mask.label, str->mask.value,
		str->rxPackets.label, str->rxPackets.value,
		str->rxErrors.label, str->rxErrors.value,
		str->rxDropped.label, str->rxDropped.value,
		str->rxOverruns.label, str->rxOverruns.value,
		str->txPackets.label, str->txPackets.value,
		str->txErrors.label, str->txErrors.value,
		str->txDropped.label, str->txDropped.value,
		str->txOverruns.label, str->txOverruns.value,
		str->linkStat.value, str->running.value,
		str->collisions.label, str->collisions.value,
		str->txQueueLen.label, str->txQueueLen.value,
		str->interrupt.label, str->interrupt.value);

	free(str);

	return (status = 0);
}


static int printDevices(char *devName)
{
	int status = 0;
	char name[NETWORK_DEVICE_MAX_NAMELENGTH];
	char buffer[MAXSTRINGLENGTH];
	int count;

	// Did the user specify a list of device names?
	if (devName)
	{
		// Get the device information
		status = getDevString(devName, buffer);
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
				status = getDevString(name, buffer);
				if (status < 0)
					return (status);

				printf("%s\n\n", buffer);
			}
		}
		else
		{
			printf("%s\n\n", NO_DEVICES);
		}
	}

	return (status = 0);
}


static void updateSelectedDevice(void)
{
	int selected = 0;
	networkDevice dev;
	char *buffer = NULL;

	if (windowComponentGetSelected(deviceList, &selected) < 0)
		return;

	// Get the device information
	if (networkDeviceGet(deviceListParams[selected].text, &dev) < 0)
		return;

	// Update the device enable/disable button
	if (dev.flags & NETWORK_DEVICEFLAG_RUNNING)
	{
		windowComponentSetData(deviceEnableButton, DISABLE, strlen(DISABLE),
			1 /* redraw */);
	}
	else
	{
		windowComponentSetData(deviceEnableButton, ENABLE, strlen(ENABLE),
			1 /* redraw */);
	}

	windowComponentSetEnabled(deviceEnableButton, enabled);

	buffer = malloc(MAXSTRINGLENGTH);
	if (!buffer)
		return;

	// Get the device string
	if (getDevString(deviceListParams[selected].text, buffer) < 0)
	{
		free(buffer);
		return;
	}

	windowComponentSetData(deviceStringLabel, buffer, strlen(buffer),
		1 /* redraw */);

	free(buffer);
}


static void updateEnabled(void)
{
	// Update the networking/device enabled widgets

	char tmp[128];

	snprintf(tmp, 128, _("Networking is %s"), (enabled? _("enabled") :
		_("disabled")));
	windowComponentSetData(enabledLabel, tmp, strlen(tmp), 1 /* redraw */);
	windowComponentSetData(enableButton, (enabled? DISABLE : ENABLE), 8,
		1 /* redraw */);

	// Update the device bits as well.
	updateSelectedDevice();
}


static void updateHostName(void)
{
	char hostName[NETWORK_MAX_HOSTNAMELENGTH];
	char domainName[NETWORK_MAX_DOMAINNAMELENGTH];
	const char *value = NULL;
	variableList kernelConf;

	if (enabled)
	{
		if (networkGetHostName(hostName, NETWORK_MAX_HOSTNAMELENGTH) >= 0)
		{
			windowComponentSetData(hostField, hostName,
				NETWORK_MAX_HOSTNAMELENGTH, 1 /* redraw */);
		}

		if (networkGetDomainName(domainName,
			NETWORK_MAX_DOMAINNAMELENGTH) >= 0)
		{
			windowComponentSetData(domainField, domainName,
				NETWORK_MAX_DOMAINNAMELENGTH, 1 /* redraw */);
		}
	}
	else
	{
		if (configRead(KERNEL_DEFAULT_CONFIG, &kernelConf) >= 0)
		{
			value = variableListGet(&kernelConf, KERNELVAR_NET_HOSTNAME);
			if (value)
			{
				windowComponentSetData(hostField, (void *) value,
					NETWORK_MAX_HOSTNAMELENGTH, 1 /* redraw */);
			}

			value = variableListGet(&kernelConf, KERNELVAR_NET_DOMAINNAME);
			if (value)
			{
				windowComponentSetData(domainField, (void *) value,
					NETWORK_MAX_DOMAINNAMELENGTH, 1 /* redraw */);
			}

			variableListDestroy(&kernelConf);
		}
	}
}


static void refreshWindow(void)
{
	// We got a 'window refresh' event (probably because of a language
	// switch), so we need to update things

	// Re-get the language setting
	setlocale(LC_ALL, getenv(ENV_LANG));
	textdomain("ifconfig");

	// Re-get the character set
	if (getenv(ENV_CHARSET))
		windowSetCharSet(window, getenv(ENV_CHARSET));

	// Refresh the 'networking enabled' label, button, the 'enable/disable
	// device' button, and device string label
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

	// Refresh the 'devices' label
	windowComponentSetData(devicesLabel, DEVICES, strlen(DEVICES),
		1 /* redraw */);

	// Refresh the 'ok' button
	windowComponentSetData(okButton, OK, strlen(OK), 1 /* redraw */);

	// Refresh the 'cancel' button
	windowComponentSetData(cancelButton, CANCEL, strlen(CANCEL),
		1 /* redraw */);

	// Refresh the window title
	windowSetTitle(window, WINDOW_TITLE);
}


static void toggleDeviceEnable(void)
{
	int selected = 0;
	networkDevice dev;
	int disable = 0;
	objectKey enableDialog = NULL;

	if (windowComponentGetSelected(deviceList, &selected) < 0)
		return;

	// Get the device information
	if (networkDeviceGet(deviceListParams[selected].text, &dev) < 0)
		return;

	if (dev.flags & NETWORK_DEVICEFLAG_RUNNING)
		disable = 1;

	enableDialog = windowNewBannerDialog(window, (disable?
		_("Disabling device") : _("Enabling device")),
		_("One moment please..."));

	if (disable)
		networkDeviceDisable(deviceListParams[selected].text);
	else
		networkDeviceEnable(deviceListParams[selected].text);

	windowDestroy(enableDialog);
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
		// The user wants to enable or disable networking.  Make a little
		// dialog while we're doing this because enabling can take a few
		// seconds
		enableDialog = windowNewBannerDialog(window, (enabled?
			_("Shutting down networking") : _("Initializing networking")),
			_("One moment please..."));

		if (enabled)
			networkDisable();
		else
			networkEnable();

		windowDestroy(enableDialog);

		enabled = networkEnabled();
		updateEnabled();
		updateHostName();
	}

	// Check for the user changing the selected device
	else if ((key == deviceList) && ((event->type & EVENT_MOUSE_DOWN) ||
		(event->type & EVENT_KEY_DOWN)))
	{
		updateSelectedDevice();
	}

	// Check for the user enabling/disabling a device
	else if ((key == deviceEnableButton) &&
		(event->type == EVENT_MOUSE_LEFTUP))
	{
		toggleDeviceEnable();
		updateSelectedDevice();
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
		if (!readOnly && configRead(KERNEL_DEFAULT_CONFIG, &kernelConf) >= 0)
		{
			variableListSet(&kernelConf, KERNELVAR_NETWORK, (selected?
				"yes" : "no"));
			variableListSet(&kernelConf, KERNELVAR_NET_HOSTNAME, hostName);
			variableListSet(&kernelConf, KERNELVAR_NET_DOMAINNAME,
				domainName);
			configWrite(KERNEL_DEFAULT_CONFIG, &kernelConf);
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
}


static int constructWindow(char *devName)
{
	int status = 0;
	componentParameters params;
	int containersGridY = 0;
	objectKey container = NULL;
	char name[NETWORK_DEVICE_MAX_NAMELENGTH];
	networkDevice dev;
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
	container = windowNewContainer(window, "enable", &params);

	// Make a label showing the status of networking
	params.padTop = params.padRight = 0;
	params.flags |= WINDOW_COMPFLAG_FIXEDWIDTH;
	enabledLabel = windowNewTextLabel(container, _("Networking is disabled"),
		&params);

	// Make a button for enabling/disabling networking
	params.gridX += 1;
	enableButton = windowNewButton(container, DISABLE, NULL, &params);
	windowRegisterEventHandler(enableButton, &eventHandler);

	// Make a checkbox so the user can choose to always enable/disable
	params.gridX += 1;
	enableCheckbox = windowNewCheckbox(container, ENABLED_STARTUP, &params);

	// Try to find out whether networking is enabled at startup
	if (configGet(KERNEL_DEFAULT_CONFIG, KERNELVAR_NETWORK, tmp, 8) >= 0)
	{
		if (!strncmp(tmp, "yes", 8))
			windowComponentSetSelected(enableCheckbox, 1);
		else
			windowComponentSetSelected(enableCheckbox, 0);
	}

	// If the boot disk is read-only, user can't change it
	if (readOnly)
		windowComponentSetEnabled(enableCheckbox, 0);

	// We used to call updateEnabled() here, but that also tries to update
	// other things we haven't created yet.

	// A container for the host and domain name stuff
	params.gridX = 0;
	params.gridY = ++containersGridY;
	params.padTop = params.padRight = 5;
	params.flags &= ~WINDOW_COMPFLAG_FIXEDWIDTH;
	container = windowNewContainer(window, "hostname", &params);

	params.gridY = 0;
	params.padTop = params.padRight = 0;
	hostLabel = windowNewTextLabel(container, HOST_NAME, &params);

	params.gridX += 1;
	domainLabel = windowNewTextLabel(container, DOMAIN_NAME, &params);

	params.gridX = 0;
	params.gridY += 1;
	hostField = windowNewTextField(container, 16, &params);
	windowRegisterEventHandler(hostField, &eventHandler);

	params.gridX += 1;
	domainField = windowNewTextField(container, 16, &params);
	windowRegisterEventHandler(domainField, &eventHandler);

	updateHostName();

	// A container for the device stuff
	params.gridX = 0;
	params.gridY = ++containersGridY;
	params.padTop = params.padRight = 5;
	container = windowNewContainer(window, "devices", &params);

	params.gridY = 0;
	params.padTop = params.padRight = 0;
	devicesLabel = windowNewTextLabel(container, DEVICES, &params);

	// A list for the network devices

	deviceListParams = calloc(numDevices, sizeof(listItemParameters));
	if (!deviceListParams)
		return (status = ERR_MEMORY);

	if (devName)
	{
		// Get the device information
		status = networkDeviceGet(devName, &dev);
		if (status < 0)
		{
			error(_("Can't get info for device %s"), devName);
			return (status);
		}

		strncpy(deviceListParams[0].text, devName, WINDOW_MAX_LABEL_LENGTH);
	}
	else
	{
		for (count = 0; count < numDevices; count ++)
		{
			sprintf(name, "net%d", count);

			// Get the device information
			if (networkDeviceGet(name, &dev) >= 0)
			{
				strncpy(deviceListParams[count].text, dev.name,
					WINDOW_MAX_LABEL_LENGTH);
			}
		}
	}

	params.gridY += 1;
	deviceList = windowNewList(container, windowlist_textonly, numDevices,
		1 /* columns */, 0 /* select multiple */, deviceListParams,
		numDevices, &params);
	windowRegisterEventHandler(deviceList, &eventHandler);

	// An enable/disable button
	params.gridX += 1;
	params.orientationY = orient_top;
	params.flags |= WINDOW_COMPFLAG_FIXEDWIDTH;
	deviceEnableButton = windowNewButton(container, DISABLE, NULL, &params);
	windowRegisterEventHandler(deviceEnableButton, &eventHandler);

	// Did the user specify a device name?
	if (devName)
	{
		for (count = 0; count < numDevices; count ++)
		{
			if (!strcmp(devName, deviceListParams[count].text))
			{
				windowComponentSetSelected(deviceList, count);
				break;
			}
		}
	}

	// A label for the selected device

	params.gridX = 0;
	params.gridY += 1;
	params.gridWidth = 2;
	params.padTop = 15;
	params.orientationX = orient_center;
	params.orientationY = orient_middle;
	params.flags &= ~WINDOW_COMPFLAG_FIXEDWIDTH;
	deviceStringLabel = windowNewTextLabel(container, NO_DEVICES, &params);

	// Also calls updateSelectedDevice()
	updateEnabled();

	// A container for the buttons
	params.gridY = ++containersGridY;
	params.gridWidth = 1;
	params.padRight = params.padTop = params.padBottom = 5;
	params.orientationX = orient_center;
	params.flags |= WINDOW_COMPFLAG_FIXEDWIDTH;
	container = windowNewContainer(window, "buttons", &params);

	// Create an 'OK' button
	params.gridY = 0;
	params.padLeft = 0;
	params.padRight = 3;
	params.padTop = params.padBottom = 0;
	params.orientationX = orient_right;
	okButton = windowNewButton(container, OK, NULL, &params);
	windowRegisterEventHandler(okButton, &eventHandler);
	windowComponentFocus(okButton);

	// Create a 'Cancel' button
	params.gridX += 1;
	params.padLeft = 3;
	params.padRight = 0;
	params.orientationX = orient_left;
	cancelButton = windowNewButton(container, CANCEL, NULL, &params);
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
	int enable = 0, disable = 0;
	char *devName = NULL;
	disk sysDisk;

	setlocale(LC_ALL, getenv(ENV_LANG));
	textdomain("ifconfig");

	// Are graphics enabled?
	graphics = graphicsAreEnabled();

	// Check options
	while (strchr("deT?", (opt = getopt(argc, argv, "deT"))))
	{
		switch (opt)
		{
			case 'd':
				// Disable networking
				disable = 1;
				break;

			case 'e':
				// Enable networking
				enable = 1;
				break;

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
		devName = argv[argc - 1];

	// Find out whether we are currently running on a read-only filesystem
	memset(&sysDisk, 0, sizeof(disk));
	if (!fileGetDisk(KERNEL_DEFAULT_CONFIG, &sysDisk))
		readOnly = sysDisk.readOnly;

	enabled = networkEnabled();

	if (graphics)
	{
		status = constructWindow(devName);
		if (status >= 0)
			windowGuiRun();
	}
	else
	{
		if (devName)
		{
			if (enable)
				networkDeviceEnable(devName);
			else if (disable)
				networkDeviceDisable(devName);
		}
		else
		{
			if (disable && enabled)
				networkDisable();
			else if (enable && !enabled)
				networkEnable();
		}

		status = printDevices(devName);
	}

	if (deviceListParams)
		free(deviceListParams);

	return (status);
}

