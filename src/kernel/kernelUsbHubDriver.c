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
//  kernelUsbHubDriver.c
//

// Driver for USB hubs.

#include "kernelBus.h"
#include "kernelCpu.h"
#include "kernelDebug.h"
#include "kernelDevice.h"
#include "kernelDriver.h"
#include "kernelError.h"
#include "kernelLinkedList.h"
#include "kernelMalloc.h"
#include "kernelUsbDriver.h"
#include "kernelVariableList.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef DEBUG
static void debugHubDesc(volatile usbHubDesc *hubDesc)
{
	kernelDebug(debug_usb, "USB HUB descriptor:\n"
		"  descLength=%d\n"
		"  descType=%x\n"
		"  numPorts=%d\n"
		"  hubChars=%04x\n"
		"  pwrOn2PwrGood=%d\n"
		"  maxPower=%d", hubDesc->descLength, hubDesc->descType,
		hubDesc->numPorts, hubDesc->hubChars, hubDesc->pwrOn2PwrGood,
		hubDesc->maxPower);
}

static const char *portFeat2String(int featNum)
{
	switch (featNum)
	{
		case USB_HUBFEAT_PORTCONN:				// 0
			return "PORT_CONNECTION";
		case USB_HUBFEAT_PORTENABLE_V12:		// 1
			return "PORT_ENABLE";
		case USB_HUBFEAT_PORTSUSPEND_V12:		// 2
			return "PORT_SUSPEND";
		case USB_HUBFEAT_PORTOVERCURR:			// 3
			return "PORT_OVERCURR";
		case USB_HUBFEAT_PORTRESET:				// 4
			return "PORT_RESET";
		case USB_HUBFEAT_PORTLINKSTATE_V3:		// 5
			return "PORT_LINK_STATE";
		case USB_HUBFEAT_PORTPOWER:				// 8
			return "PORT_POWER";
		case USB_HUBFEAT_PORTLOWSPEED_V12:		// 9
			return "PORT_LOWSPEED";
		case USB_HUBFEAT_PORTCONN_CH:			// 16
			return "PORT_CONNECTION_CHANGE";
		case USB_HUBFEAT_PORTENABLE_CH_V12:		// 17
			return "PORT_ENABLE_CHANGE";
		case USB_HUBFEAT_PORTSUSPEND_CH_V12:	// 18
			return "PORT_SUSPEND_CHANGE";
		case USB_HUBFEAT_PORTOVERCURR_CH:		// 19
			return "PORT_OVERCURR_CHANGE";
		case USB_HUBFEAT_PORTRESET_CH:			// 20
			return "PORT_RESET_CHANGE";
		case USB_HUBFEAT_PORTU1TIMEOUT_V3:		// 23
			return "PORT_U1_TIMEOUT";
		case USB_HUBFEAT_PORTU2TIMEOUT_V3:		// 24
			return "PORT_U2_TIMEOUT";
		case USB_HUBFEAT_PORTLINKSTATE_CH_V3:	// 25
			return "PORT_LINK_STATE_CHANGE";
		case USB_HUBFEAT_PORTCONFERR_CH_V3:		// 26
			return "PORT_CONFIG_ERROR_CHANGE";
		case USB_HUBFEAT_PORTREMWAKEMASK_V3:	// 27
			return "PORT_REMOTE_WAKE_MASK";
		case USB_HUBFEAT_PORTBHRESET_V3:		// 28
			return "BH_PORT_RESET";
		case USB_HUBFEAT_PORTBHRESET_CH_V3:		// 29
			return "BH_PORT_RESET_CHANGE";
		case USB_HUBFEAT_PORTFRCELNKPMACC_V3:	// 30
			return "FORCE_LINKPM_ACCEPT";
		default:
			return "(UNKNOWN)";
	}
}
#else
	#define debugHubDesc(hubDesc) do { } while (0)
	#define portFeat2String(featNum) ""
#endif // DEBUG


static int getHubDescriptor(usbHub *hub)
{
	usbTransaction usbTrans;

	kernelDebug(debug_usb, "USB HUB get hub descriptor for target 0x%08x",
		hub->busTarget->id);

	// Set up the USB transaction to send the 'get descriptor' command
	memset((void *) &usbTrans, 0, sizeof(usbTrans));
	usbTrans.type = usbxfer_control;
	usbTrans.address = hub->usbDev->address;
	usbTrans.control.requestType = (USB_DEVREQTYPE_DEV2HOST |
		USB_DEVREQTYPE_CLASS);
	usbTrans.control.request = USB_GET_DESCRIPTOR;
	if (hub->usbDev->speed >= usbspeed_super)
		usbTrans.control.value = (USB_DESCTYPE_SSHUB << 8);
	else
		usbTrans.control.value = (USB_DESCTYPE_HUB << 8);
	usbTrans.length = sizeof(usbHubDesc);
	usbTrans.buffer = (void *) &hub->hubDesc;
	usbTrans.pid = USB_PID_IN;
	usbTrans.timeout = USB_STD_TIMEOUT_MS;

	// Write the command
	return (kernelBusWrite(hub->busTarget, sizeof(usbTransaction),
		(void *) &usbTrans));
}


static int setHubDepth(usbHub *hub)
{
	// Tells the hub its depth in the topology.  USB 3.0+

	usbTransaction usbTrans;

	kernelDebug(debug_usb, "USB HUB set hub depth for address %d",
		hub->usbDev->address);

	memset((void *) &usbTrans, 0, sizeof(usbTrans));
	usbTrans.type = usbxfer_control;
	usbTrans.address = hub->usbDev->address;
	usbTrans.control.requestType =
		(USB_DEVREQTYPE_HOST2DEV | USB_DEVREQTYPE_CLASS);
	usbTrans.control.request = USB_HUB_SET_HUB_DEPTH;
	usbTrans.control.value = hub->usbDev->hubDepth;
	usbTrans.pid = USB_PID_OUT;
	usbTrans.timeout = USB_STD_TIMEOUT_MS;

	// Write the command
	return (kernelBusWrite(hub->busTarget, sizeof(usbTransaction),
		(void *) &usbTrans));
}


static void interrupt(usbDevice *usbDev, int interface, void *buffer,
	unsigned length)
{
	// This is called when the hub wants to report a change, on the hub or
	// else on one of the ports.

	usbHub *hub = usbDev->interface[interface].data;

	kernelDebug(debug_usb, "USB HUB interrupt %u bytes", length);

	memcpy(hub->changeBitmap, buffer,
		min(hub->intrInEndp->maxPacketSize, length));

	return;
}


static int getPortStatus(usbHub *hub, int portNum,
	usbHubPortStatus *portStatus)
{
	// Fills in the usbPortStatus for the requested port with data returned
	// by the hub.

	int status = 0;
	usbTransaction usbTrans;

	kernelDebug(debug_usb, "USB HUB get port status for address %d port %d",
		hub->usbDev->address, portNum);

	memset((void *) &usbTrans, 0, sizeof(usbTrans));
	usbTrans.type = usbxfer_control;
	usbTrans.address = hub->usbDev->address;
	usbTrans.control.requestType = (USB_DEVREQTYPE_DEV2HOST |
		USB_DEVREQTYPE_CLASS | USB_DEVREQTYPE_OTHER);
	usbTrans.control.request = USB_GET_STATUS;
	usbTrans.control.index = (portNum + 1);
	usbTrans.length = sizeof(usbHubPortStatus);
	usbTrans.buffer = portStatus;
	usbTrans.pid = USB_PID_IN;
	usbTrans.timeout = USB_STD_TIMEOUT_MS;

	// Write the command
	status = kernelBusWrite(hub->busTarget, sizeof(usbTransaction),
		(void *) &usbTrans);

	if (status < 0)
	{
		kernelError(kernel_error, "Couldn't get port status");
	}
	else
	{
		//kernelDebug(debug_usb, "USB HUB port status=0x%04x, change=0x%04x",
		//	portStatus->status, portStatus->change);
	}

	return (status);
}


static int setPortFeature(usbHub *hub, int portNum, unsigned char feature)
{
	// Sends a 'set feature' request to the hub for the requested port.

	usbTransaction usbTrans;

	kernelDebug(debug_usb, "USB HUB set port feature %s for address %d "
		"port %d", portFeat2String(feature), hub->usbDev->address, portNum);

	memset((void *) &usbTrans, 0, sizeof(usbTrans));
	usbTrans.type = usbxfer_control;
	usbTrans.address = hub->usbDev->address;
	usbTrans.control.requestType = (USB_DEVREQTYPE_HOST2DEV |
		USB_DEVREQTYPE_CLASS | USB_DEVREQTYPE_OTHER);
	usbTrans.control.request = USB_SET_FEATURE;
	usbTrans.control.value = feature;
	usbTrans.control.index = (portNum + 1);
	usbTrans.pid = USB_PID_OUT;
	usbTrans.timeout = USB_STD_TIMEOUT_MS;

	// Write the command
	return (kernelBusWrite(hub->busTarget, sizeof(usbTransaction),
		(void *) &usbTrans));
}


static int clearPortFeature(usbHub *hub, int portNum, unsigned char feature)
{
	// Sends a 'clear feature' request to the hub for the requested port.

	usbTransaction usbTrans;

	kernelDebug(debug_usb, "USB HUB clear port feature %s for address %d "
		"port %d", portFeat2String(feature), hub->usbDev->address, portNum);

	memset((void *) &usbTrans, 0, sizeof(usbTrans));
	usbTrans.type = usbxfer_control;
	usbTrans.address = hub->usbDev->address;
	usbTrans.control.requestType = (USB_DEVREQTYPE_HOST2DEV |
		USB_DEVREQTYPE_CLASS | USB_DEVREQTYPE_OTHER);
	usbTrans.control.request = USB_CLEAR_FEATURE;
	usbTrans.control.value = feature;
	usbTrans.control.index = (portNum + 1);
	usbTrans.pid = USB_PID_OUT;
	usbTrans.timeout = USB_STD_TIMEOUT_MS;

	// Write the command
	return (kernelBusWrite(hub->busTarget, sizeof(usbTransaction),
		(void *) &usbTrans));
}


static void clearPortChangeBits(usbHub *hub, int portNum,
	usbHubPortStatus *portStatus)
{
	if (portStatus->change & USB_HUBPORTSTAT_CONN)
		clearPortFeature(hub, portNum, USB_HUBFEAT_PORTCONN_CH);

	if (hub->usbDev->usbVersion < 0x0300)
	{
		if (portStatus->change & USB_HUBPORTSTAT_ENABLE)
			clearPortFeature(hub, portNum, USB_HUBFEAT_PORTENABLE_CH_V12);

		if (portStatus->change & USB_HUBPORTSTAT_SUSPEND_V12)
			clearPortFeature(hub, portNum, USB_HUBFEAT_PORTSUSPEND_CH_V12);
	}

	if (portStatus->change & USB_HUBPORTSTAT_OVERCURR)
		clearPortFeature(hub, portNum, USB_HUBFEAT_PORTOVERCURR_CH);

	if (portStatus->change & USB_HUBPORTSTAT_RESET)
		clearPortFeature(hub, portNum, USB_HUBFEAT_PORTRESET_CH);

	if (hub->usbDev->usbVersion >= 0x0300)
	{
		if (portStatus->change & USB_HUBPORTCHANGE_BHRESET_V3)
			clearPortFeature(hub, portNum, USB_HUBFEAT_PORTBHRESET_CH_V3);

		if (portStatus->change & USB_HUBPORTCHANGE_LINKSTATE_V3)
			clearPortFeature(hub, portNum, USB_HUBFEAT_PORTLINKSTATE_CH_V3);

		if (portStatus->change & USB_HUBPORTCHANGE_CONFERROR_V3)
			clearPortFeature(hub, portNum, USB_HUBFEAT_PORTCONFERR_CH_V3);
	}

	getPortStatus(hub, portNum, portStatus);
}


static void doDetectDevices(usbHub *hub, int hotplug)
{
	// Detect devices connected to the hub

	usbHubPortStatus portStatus;
	usbDevSpeed speed = usbspeed_unknown;
	int retries = 0;
	int portCount, count;

	// Look for port status changes
	for (portCount = 0; portCount < hub->hubDesc.numPorts; portCount ++)
	{
		if (hub->doneColdDetect &&
			!((hub->changeBitmap[(portCount + 1) / 8] >>
				((portCount + 1) % 8)) & 0x01))
		{
			continue;
		}

		if (getPortStatus(hub, portCount, &portStatus) < 0)
			continue;

		if (!portStatus.change)
			continue;

		retries = 0;

	retry:

		if (portStatus.status & USB_HUBPORTSTAT_CONN)
		{
			// A device connected.
			kernelDebug(debug_usb, "USB HUB port %d, device connected",
				portCount);

			// Clear the hub port's change bits
			clearPortChangeBits(hub, portCount,	&portStatus);

			if (hub->usbDev->speed < usbspeed_super)
			{
				// Set the reset feature on the port.
				if (setPortFeature(hub, portCount, USB_HUBFEAT_PORTRESET) < 0)
					continue;

				kernelDebug(debug_usb, "USB HUB port %d, wait for port reset "
					"to clear", portCount);

				// Try to wait up to 500ms, until the hub clears the reset
				// bit
				for (count = 0; count < 500; count ++)
				{
					if (getPortStatus(hub, portCount, &portStatus) >= 0)
					{
						if (!(portStatus.status & USB_HUBPORTSTAT_RESET))
						{
							kernelDebug(debug_usb, "USB HUB port %d, reset "
								"took %dms", portCount, count);
							break;
						}
					}

					kernelCpuSpinMs(1);
				}

				// Clear the hub port's change bits
				clearPortChangeBits(hub, portCount,	&portStatus);

				if (portStatus.status & USB_HUBPORTSTAT_RESET)
				{
					kernelDebugError("Port %d reset did not clear",	portCount);
					continue;
				}
			}

			if (!(portStatus.status & USB_HUBPORTSTAT_ENABLE))
			{
				// Try to wait up to 500ms, until the hub sets the enabled
				// bit
				for (count = 0; count < 500; count ++)
				{
					if (getPortStatus(hub, portCount, &portStatus) >= 0)
					{
						if (portStatus.status & USB_HUBPORTSTAT_ENABLE)
						{
							kernelDebug(debug_usb, "USB HUB port %d, enable "
								"took %dms", portCount, count);
							break;
						}
					}

					kernelCpuSpinMs(1);
				}

				// Clear the hub port's change bits
				clearPortChangeBits(hub, portCount,	&portStatus);

				if (!(portStatus.status & USB_HUBPORTSTAT_ENABLE))
				{
					kernelDebugError("Port %d did not enable", portCount);

					if (retries++ < 3)
						goto retry;

					continue;
				}
			}

			if (hub->usbDev->usbVersion < 0x0300)
			{
				if (portStatus.status & USB_HUBPORTSTAT_LOWSPEED_V12)
				{
					speed = usbspeed_low;
				}

				else if ((hub->usbDev->usbVersion >= 0x0200) &&
					(portStatus.status & USB_HUBPORTSTAT_HIGHSPEED_V2))
				{
					speed = usbspeed_high;
				}

				else
				{
					speed = usbspeed_full;
				}
			}
			else
			{
				speed = usbspeed_super;
			}

			kernelDebug(debug_usb, "USB HUB port %d is connected, speed=%s",
				portCount, usbDevSpeed2String(speed));

			// Some devices/hubs need a short delay here, before we try to
			// start talking to the device
			kernelCpuSpinMs(10);

			if (kernelUsbDevConnect(hub->controller, hub, portCount, speed,
				hotplug) < 0)
			{
				kernelError(kernel_error, "Error enumerating new device");
			}
			else
			{
				kernelDebug(debug_usb, "USB HUB new device registered "
					"successfully");
			}
		}
		else
		{
			// A device disconnected.
			kernelDebug(debug_usb, "USB HUB port %d disconnected",
				portCount);

			// Clear the hub port's change bits
			clearPortChangeBits(hub, portCount,	&portStatus);

			// A device disconnected.
			kernelUsbDevDisconnect(hub->controller, hub, portCount);

			kernelDebug(debug_usb, "USB HUB port %d, device disconnected",
				portCount);
		}
	}

	// Clear the port change bitmap
	memset(hub->changeBitmap, 0, hub->intrInEndp->maxPacketSize);
}


/////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////
//
//  Standard USB hub functions
//
/////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////

static void detectDevices(usbHub *hub, int hotplug)
{
	// This function gets called once, when the hub is first detected.  This
	// can happen at system startup, or if a hub is hot-plugged.

	kernelDebug(debug_usb, "USB HUB initial device detection, hotplug=%d",
		hotplug);

	// Check params
	if (!hub)
	{
		kernelError(kernel_error, "NULL parameter");
		return;
	}

	doDetectDevices(hub, hotplug);

	hub->doneColdDetect = 1;
}


static void threadCall(usbHub *hub)
{
	// This function gets called periodically by the USB thread, to give us
	// an opportunity to detect connections/disconnections, or whatever else
	// we want.

	// Check params
	if (!hub)
	{
		kernelError(kernel_error, "NULL parameter");
		return;
	}

	// Only continue if we've already completed 'cold' device connection
	// detection.  Don't want to interfere with that.
	if (!hub->doneColdDetect)
		return;

	doDetectDevices(hub, 1 /* hotplug */);
}


/////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////
//
//  Standard device driver functions
//
/////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////

static int detectHub(usbDevice *usbDev, kernelDriver *driver, int hotplug)
{
	int status = 0;
	usbHub *hub = NULL;
	usbEndpoint *endpoint = NULL;
	char value[32];
	int count;

	kernelDebug(debug_usb, "USB HUB detect hub device %p", usbDev);

	// Get a hub structure
	hub = kernelMalloc(sizeof(usbHub));
	if (!hub)
		return (status = ERR_MEMORY);

	// Hubs only have one interface
	usbDev->interface[0].data = (void *) hub;

	hub->controller = usbDev->controller;
	hub->usbDev = usbDev;

	hub->busTarget = kernelBusGetTarget(bus_usb,
		usbMakeTargetCode(hub->controller->num, hub->usbDev->address, 0));
	if (!hub->busTarget)
	{
		status = ERR_NODATA;
		goto out;
	}

	// Record the interrupt-in endpoint
	for (count = 0; count < hub->usbDev->interface[0].numEndpoints; count ++)
	{
		endpoint = (usbEndpoint *) &hub->usbDev->interface[0].endpoint[count];

		if (((endpoint->attributes & USB_ENDP_ATTR_MASK) ==
			USB_ENDP_ATTR_INTERRUPT) && (endpoint->number & 0x80))
		{
			hub->intrInEndp = endpoint;
			kernelDebug(debug_usb, "USB HUB got interrupt endpoint %02x",
				hub->intrInEndp->number);
			break;
		}
	}

	// We *must* have an interrupt in endpoint.
	if (!hub->intrInEndp)
	{
		kernelError(kernel_error, "Hub device %p has no interrupt endpoint",
			hub->usbDev);
		status = ERR_NODATA;
		goto out;
	}

	if (!hub->intrInEndp->maxPacketSize)
	{
		kernelError(kernel_error, "Hub device %p max packet size is 0",
			hub->usbDev);
		status = ERR_INVALID;
		goto out;
	}

	// Set the device configuration
	status = kernelUsbSetDeviceConfig(usbDev);
	if (status < 0)
		return (status);

	if (!hub->usbDev->protocol && !hub->usbDev->interface[0].protocol)
	{
		kernelDebug(debug_usb, "USB HUB is operating at low/full speed");
	}
	else
	{
		kernelDebug(debug_usb, "USB HUB is operating at high speed");
		if ((hub->usbDev->protocol == 1) &&
			!hub->usbDev->interface[0].protocol)
		{
			kernelDebug(debug_usb, "USB HUB has a single TT");
		}
		else if ((hub->usbDev->protocol == 2) &&
			(hub->usbDev->interface[0].protocol == 1))
		{
			kernelDebug(debug_usb, "USB HUB has multiple TTs");
		}
	}

	// Try to get the hub descriptor
	status = getHubDescriptor(hub);
	if (status < 0)
		goto out;

	debugHubDesc(&hub->hubDesc);

	// Get memory for a port change bitmap
	hub->changeBitmap = kernelMalloc(hub->intrInEndp->maxPacketSize);
	if (!hub->changeBitmap)
	{
		status = ERR_MEMORY;
		goto out;
	}

	if (usbDev->speed >= usbspeed_super)
	{
		// Set the hub depth
		status = setHubDepth(hub);
		if (status < 0)
			goto out;
	}

	// Add our function pointers
	hub->detectDevices = &detectDevices;
	hub->threadCall = &threadCall;

	hub->dev.device.class = kernelDeviceGetClass(DEVICECLASS_HUB);
	hub->dev.device.subClass = kernelDeviceGetClass(DEVICESUBCLASS_HUB_USB);
	kernelUsbSetDeviceAttrs(hub->usbDev, 0, (kernelDevice *) &hub->dev);
	hub->dev.driver = driver;

	// Set attributes of the hub
	snprintf(value, 32, "%d", hub->hubDesc.numPorts);
	kernelVariableListSet((variableList *) &hub->dev.device.attrs,
		"hub.numPorts", value);

	// Tell USB that we're claiming this device.
	kernelBusDeviceClaim(hub->busTarget, driver);

	// Add the kernel device
	status = kernelDeviceAdd(hub->controller->dev, (kernelDevice *) &hub->dev);

	// Schedule the regular interrupt.
	kernelDebug(debug_usb, "USB HUB schedule interrupt, %d bytes, interval=%d",
		hub->intrInEndp->maxPacketSize, hub->intrInEndp->interval);
	kernelUsbScheduleInterrupt(hub->usbDev, 0, hub->intrInEndp->number,
		hub->intrInEndp->interval, hub->intrInEndp->maxPacketSize,
		&interrupt);

	// Set the power on for all ports
	kernelDebug(debug_usb, "USB HUB turn on ports power");
	for (count = 0; count < hub->hubDesc.numPorts; count ++)
		setPortFeature(hub, count, USB_HUBFEAT_PORTPOWER);

	// Use the "power on to power good" value to delay for the appropriate
	// number of milliseconds
	kernelCpuSpinMs(hub->hubDesc.pwrOn2PwrGood * 2);

	if (usbDev->usbVersion < 0x0300)
	{
		// Disable all the ports
		for (count = 0; count < hub->hubDesc.numPorts; count ++)
			clearPortFeature(hub, count, USB_HUBFEAT_PORTENABLE_V12);
	}

out:
	if (status < 0)
	{
		if (hub)
		{
			if (hub->busTarget)
				kernelFree(hub->busTarget);
			kernelFree((void *) hub);
		}
	}
	else
	{
		kernelDebug(debug_usb, "USB HUB detected USB hub device");
		kernelUsbAddHub(hub, hotplug);
	}

	return (status);
}


static int driverDetect(void *parent __attribute__((unused)),
	kernelDriver *driver)
{
	// This routine is used to detect and initialize each device, as well as
	// registering each one with any higher-level interfaces.

	int status = 0;
	kernelBusTarget *busTargets = NULL;
	int numBusTargets = 0;
	int deviceCount = 0;
	usbDevice *usbDev = NULL;
	int found = 0;
	usbDevice tmpDev;

	kernelDebug(debug_usb, "USB HUB detect hubs");

restart:

	if (busTargets)
		kernelFree(busTargets);

	// Search the USB bus(es) for devices
	numBusTargets = kernelBusGetTargets(bus_usb, &busTargets);
	if (numBusTargets <= 0)
		return (status = 0);

	// Search the bus targets for USB hub devices
	for (deviceCount = 0; deviceCount < numBusTargets; deviceCount ++)
	{
		// Try to get the USB information about the target
		status = kernelBusGetTargetInfo(&busTargets[deviceCount],
			(void *) &tmpDev);
		if (status < 0)
			continue;

		if ((tmpDev.classCode != 0x09) || (tmpDev.subClassCode != 0x00))
			continue;

		// Already claimed?
		if (busTargets[deviceCount].claimed)
			continue;

		found += 1;

		usbDev = kernelUsbGetDevice(busTargets[deviceCount].id);
		if (!usbDev)
			continue;

		status = detectHub(usbDev, driver, 0 /* no hotplug */);
		if (status < 0)
			continue;

		// If the hub was detected successfully, it will call kernelUsbAddHub()
		// which will in turn call our detectDevices() function.  This could
		// cause more hubs to be added to the pile, so start again.
		goto restart;
	}

	kernelDebug(debug_usb, "USB HUB finished detecting hubs (found %d)",
		found);

	kernelFree(busTargets);
	return (status = 0);
}


static int driverHotplug(void *parent __attribute__((unused)),
	int busType __attribute__((unused)), int target, int connected,
	kernelDriver *driver)
{
	// This routine is used to detect whether a newly-connected, hotplugged
	// device is supported by this driver during runtime, and if so to do the
	// appropriate device setup and registration.  Alternatively if the device
	// is disconnected a call to this function lets us know to stop trying
	// to communicate with it.

	int status = 0;
	usbDevice *usbDev = NULL;
	usbHub *hub = NULL;

	kernelDebug(debug_usb, "USB HUB hotplug %sconnection",
		(connected? "" : "dis"));

	usbDev = kernelUsbGetDevice(target);
	if (!usbDev)
	{
		kernelError(kernel_error, "No such USB device %d", target);
		return (status = ERR_NOSUCHENTRY);
	}

	if (connected)
	{
		status = detectHub(usbDev, driver, 1 /* hotplug */);
		if (status < 0)
			return (status);
	}
	else
	{
		// Hubs only have one interface
		hub = usbDev->interface[0].data;
		if (!hub)
		{
			kernelError(kernel_error, "No such hub device %d", target);
			return (status = ERR_NOSUCHENTRY);
		}

		// Found it.
		kernelDebug(debug_usb, "USB HUB hub device removed");

		// Remove it from the device tree
		kernelDeviceRemove((kernelDevice *) &hub->dev);

		// Free the device's attributes list
		kernelVariableListDestroy((variableList *) &hub->dev.device.attrs);

		// Free the memory.
		if (hub->busTarget)
			kernelFree(hub->busTarget);

		kernelFree((void *) hub);
	}

	return (status = 0);
}


/////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////
//
// Below here, the functions are exported for external use
//
/////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////

void kernelUsbHubDriverRegister(kernelDriver *driver)
{
	// Device driver registration.

	driver->driverDetect = driverDetect;
	driver->driverHotplug = driverHotplug;

	return;
}

