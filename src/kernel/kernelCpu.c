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
//  kernelCpu.c
//

#include "kernelCpu.h"
#include "kernelDebug.h"
#include "kernelDevice.h"
#include "kernelDriver.h"
#include "kernelLog.h"
#include "kernelMalloc.h"
#include "kernelSysTimer.h"
#include "kernelVariableList.h"
#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <sys/processor.h>

static struct {
	char *string;
	char *vendor;

} cpuVendorIds[] = {
	{ "AMDisbetter!", "AMD" },
	{ "AuthenticAMD", "AMD" },
	{ "CentaurHauls", "IDT/Centaur/VIA" },
	{ "CyrixInstead", "Cyrix" },
	{ "GenuineIntel", "Intel" },
	{ "GenuineTMx86", "Transmeta" },
	{ "TransmetaCPU", "Transmeta" },
	{ "Geode by NSC", "National Semiconductor" },
	{ "NexGenDriven", "NexGen" },
	{ "RiseRiseRise", "Rise" },
	{ "SiS SiS SiS ", "SiS" },
	{ "UMC UMC UMC ", "United Microelectronics" },
	{ "VIA VIA VIA ", "Via" },
	{ NULL, NULL }
};

static uquad_t timestampFreq = 0;


static int driverDetectCpu(void *parent, kernelDriver *driver)
{
	int status = 0;
	unsigned cpuIdLimit = 0;
	char vendorString[13];
	unsigned rega = 0, regb = 0, regc = 0, regd = 0;
	kernelDevice *dev = NULL;
	char variable[80];
	char value[80];
	int longMode = 0;
	int whitespace = 1;
	unsigned count1, count2;

	// Allocate memory for the device
	dev = kernelMalloc(sizeof(kernelDevice));
	if (!dev)
		return (status = ERR_NOCREATE);

	// Initialize the variable list for attributes of the CPU
	status = kernelVariableListCreate(&dev->device.attrs);
	if (status < 0)
	{
		kernelFree(dev);
		return (status);
	}

	// Try to identify the CPU

	// The initial call gives us the vendor string and tells us how many other
	// functions are supported

	processorId(0, rega, regb, regc, regd);

	cpuIdLimit = (rega & 0x7FFFFFFF);
	((unsigned *) vendorString)[0] = regb;
	((unsigned *) vendorString)[1] = regd;
	((unsigned *) vendorString)[2] = regc;
	vendorString[12] = '\0';

	// Try to identify the chip vendor by name
	kernelVariableListSet(&dev->device.attrs, DEVICEATTRNAME_VENDOR,
		"unknown");
	for (count1 = 0; cpuVendorIds[count1].string; count1 ++)
	{
		if (!strncmp(vendorString, cpuVendorIds[count1].string, 12))
		{
			kernelVariableListSet(&dev->device.attrs, DEVICEATTRNAME_VENDOR,
				cpuVendorIds[count1].vendor);
			break;
		}
	}
	kernelVariableListSet(&dev->device.attrs, "vendor.string", vendorString);

	// Do additional supported functions

	// If supported, the second call gives us a bunch of binary flags telling
	// us about the capabilities of the chip
	if (cpuIdLimit >= 1)
	{
		processorId(1, rega, regb, regc, regd);

		// CPU type
		sprintf(variable, "%s.%s", "cpu", "type");
		sprintf(value, "%02x", ((rega & 0xF000) >> 12));
		kernelVariableListSet(&dev->device.attrs, variable, value);

		// CPU family
		sprintf(variable, "%s.%s", "cpu", "family");
		sprintf(value, "%02x", ((rega & 0xF00) >> 8));
		kernelVariableListSet(&dev->device.attrs, variable, value);

		// CPU model
		sprintf(variable, "%s.%s", "cpu", "model");
		sprintf(value, "%02x", ((rega & 0xF0) >> 4));
		kernelVariableListSet(&dev->device.attrs, variable, value);

		// CPU revision
		sprintf(variable, "%s.%s", "cpu", "rev");
		sprintf(value, "%02x", (rega & 0xF));
		kernelVariableListSet(&dev->device.attrs, variable, value);

		// CPU features
		sprintf(variable, "%s.%s", "cpu", "features");
		sprintf(value, "%08x", regd);
		kernelVariableListSet(&dev->device.attrs, variable, value);
	}

	// See if there's extended CPUID info
	processorId(0x80000000, cpuIdLimit, regb, regc, regd);

	// If supported, get extended processor info and feature bits
	if (cpuIdLimit >= 0x80000001)
	{
		processorId(0x80000001, rega, regb, regc, regd);

		// Is it an x86-64 processor?
		longMode = ((regd >> 29) & 1);
	}

	if (cpuIdLimit >= 0x80000004)
	{
		// Get the product string
		value[0] = '\0';
		for (count1 = 0x80000002; count1 <= 0x80000004; count1 ++)
		{
			processorId(count1, rega, regb, regc, regd);
			strncat(value, (char *) &rega, 4);
			strncat(value, (char *) &regb, 4);
			strncat(value, (char *) &regc, 4);
			strncat(value, (char *) &regd, 4);
		}

		// Get rid of any extraneous white space
		for (count1 = 0; count1 < 48; count1 ++)
		{
			if (isspace(value[count1]))
			{
				if (whitespace || (count1 && isspace(value[count1 - 1])))
				{
					for (count2 = count1; count2 < 47; count2 ++)
						value[count2] = value[count2 + 1];
					value[47] = '\0';
					count1 -= 1;
				}
			}
			else
			{
				whitespace = 0;
			}
		}

		kernelVariableListSet(&dev->device.attrs, DEVICEATTRNAME_MODEL,
			value);
	}

	// Complete the kernel device depending on what we detected

	dev->device.class = kernelDeviceGetClass(DEVICECLASS_CPU);

	if (longMode)
		dev->device.subClass = kernelDeviceGetClass(DEVICESUBCLASS_CPU_X86_64);
	else
		dev->device.subClass = kernelDeviceGetClass(DEVICESUBCLASS_CPU_X86);

	dev->driver = driver;

	// Add the kernel device
	status = kernelDeviceAdd(parent, dev);
	if (status < 0)
	{
		kernelVariableListDestroy(&dev->device.attrs);
		kernelFree(dev);
		return (status);
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

void kernelCpuDriverRegister(kernelDriver *driver)
{
	// Device driver registration.
	driver->driverDetect = driverDetectCpu;
	return;
}


uquad_t kernelCpuTimestampFreq(void)
{
	// Try to determine the rate at which the processor's timestamp counter
	// changes

	int interrupts = 0;
	unsigned hi1, lo1, hi2, lo2;
	uquad_t timestamp1 = 0;
	uquad_t timestamp2 = 0;

	// Only do this once; it takes a second, and the answer never changes
	if (timestampFreq)
		return (timestampFreq);

	kernelLog("Measuring CPU timestamp frequency");

	// Disable interrupts
	processorSuspendInts(interrupts);

	// Set up the PIT to do a single countdown
	kernelSysTimerSetupTimer(0 /* timer */, 0 /* mode */, 0 /* startCount */);

	// Now get the processor's timestamp
	processorTimestamp(hi1, lo1);
	timestamp1 = (((uquad_t) hi1 << 32) | lo1);

	kernelDebug(debug_device, "CPU starting timestamp is %llx", timestamp1);

	// Wait until the PIT counter outputs high
	while (!kernelSysTimerGetOutput(0));

	// Get the new timestamp
	processorTimestamp(hi2, lo2);
	timestamp2 = (((uquad_t) hi2 << 32) | lo2);

	// Restore the PIT to default mode
	kernelSysTimerSetupTimer(0 /* timer */, 3 /* mode */, 0 /* startCount */);

	// Restore interrupts
	processorRestoreInts(interrupts);

	kernelDebug(debug_device, "CPU ending timestamp is %llx", timestamp2);

	// Multiply by 18.206
	timestampFreq = ((timestamp2 - timestamp1) * 18);
	timestampFreq += (((timestamp2 - timestamp1) * 206) / 1000);

	kernelLog("CPU timestamp frequency is %llu MHz", (timestampFreq /
		US_PER_SEC));

	return (timestampFreq);
}


uquad_t kernelCpuTimestamp(void)
{
	// Convenience function to return a the CPU timestamp as a quad value.

	unsigned hi, lo;
	uquad_t timestamp = 0;

	processorTimestamp(hi, lo);
	timestamp = (((uquad_t) hi << 32) | lo);

	return (timestamp);
}


uquad_t kernelCpuGetMs(void)
{
	// Returns a value representing the current CPU timestamp in milliseconds.

	// Make sure the timestamp frequency has been determined
	if (!timestampFreq)
		kernelCpuTimestampFreq();

	return (kernelCpuTimestamp() / (timestampFreq / US_PER_MS));
}


void kernelCpuSpinMs(unsigned millisecs)
{
	// This will use the CPU timestamp counter to spin for (at least) the
	// specified number of milliseconds.

	uquad_t endtime = (kernelCpuGetMs() + millisecs);

	while (kernelCpuGetMs() < endtime);
}

