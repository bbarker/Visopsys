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
//  kernelUsbTouchscreenDriver.c
//

// Driver for standard USB touchscreens

#include "kernelUsbTouchscreenDriver.h"
#include "kernelBus.h"
#include "kernelDebug.h"
#include "kernelDriver.h"
#include "kernelError.h"
#include "kernelGraphic.h"
#include "kernelMalloc.h"
#include "kernelTouch.h"
#include "kernelUsbDriver.h"
#include "kernelVariableList.h"
#include <stdio.h>
#include <string.h>


#ifdef DEBUG
static void debugHidDesc(usbHidDesc *hidDesc)
{
	kernelDebug(debug_usb, "USB touchscreen debug HID descriptor:\n"
		"  descLength=%d\n"
		"  descType=%x\n"
		"  hidVersion=%d.%d\n"
		"  countryCode=%d\n"
		"  numDescriptors=%d\n"
		"  repDescType=%d\n"
		"  repDescLength=%d", hidDesc->descLength, hidDesc->descType,
		((hidDesc->hidVersion & 0xFF00) >> 8),
		(hidDesc->hidVersion & 0xFF), hidDesc->countryCode,
		hidDesc->numDescriptors, hidDesc->repDescType,
		hidDesc->repDescLength);
}

typedef struct {
	unsigned char id;
	const char *name;

} usageName;

static usageName usageNamesGenericDesktop[] = {
	{ 0x00, "undefined" },
	{ 0x01, "pointer" },
	{ 0x02, "mouse" },
	{ 0x04, "joystick" },
	{ 0x05, "game pad" },
	{ 0x06, "keyboard" },
	{ 0x07, "keypad" },
	{ 0x08, "multi-access controller" },
	{ 0x09, "tablet pc system controls" },
	{ 0x30, "x" },
	{ 0x31, "y" },
	{ 0x32, "z" },
	{ 0x33, "rx" },
	{ 0x34, "ry" },
	{ 0x35, "rz" },
	{ 0x36, "slider" },
	{ 0x37, "dia" },
	{ 0x38, "whee" },
	{ 0x39, "hat switch" },
	{ 0x3A, "counted buffer" },
	{ 0x3B, "byte count" },
	{ 0x3C, "motion wakeup" },
	{ 0x3D, "start" },
	{ 0x3E, "select" },
	{ 0x40, "vx" },
	{ 0x41, "vy" },
	{ 0x42, "vz" },
	{ 0x43, "vbrx" },
	{ 0x44, "vbry" },
	{ 0x45, "vbrz" },
	{ 0x46, "vno" },
	{ 0x47, "feature notification" },
	// Skipped a bunch more
	{ 0x00, NULL }
};

static usageName usageNamesDigitizer[] = {
	{ 0x00, "undefined" },
	{ 0x01, "digitizer" },
	{ 0x02, "pen" },
	{ 0x03, "light pen" },
	{ 0x04, "touch screen" },
	{ 0x05, "touch pad" },
	{ 0x06, "white board" },
	{ 0x07, "coordinate measuring machine" },
	{ 0x08, "3d digitizer" },
	{ 0x09, "stereo plotter" },
	{ 0x0A, "articulated arm" },
	{ 0x0B, "armature" },
	{ 0x0C, "multiple point digitizer" },
	{ 0x0D, "free space wand" },
	{ 0x0E, "configuration" },
	{ 0x20, "stylus" },
	{ 0x21, "puck" },
	{ 0x22, "finger" },
	{ 0x23, "device settings" },
	{ 0x30, "tip pressure" },
	{ 0x31, "barrel pressure" },
	{ 0x32, "in range" },
	{ 0x33, "touch" },
	{ 0x34, "untouch" },
	{ 0x35, "tap" },
	{ 0x36, "quality" },
	{ 0x37, "data valid" },
	{ 0x38, "transducer index" },
	{ 0x39, "tablet function keys" },
	{ 0x3A, "program change keys" },
	{ 0x3B, "battery strength" },
	{ 0x3C, "invert" },
	{ 0x3D, "x tilt" },
	{ 0x3E, "y tilt" },
	{ 0x3F, "azimuth" },
	{ 0x40, "altitude" },
	{ 0x41, "twist" },
	{ 0x42, "tip switch" },
	{ 0x43, "secondary tip switch" },
	{ 0x44, "barrel switch" },
	{ 0x45, "eraser" },
	{ 0x46, "tablet pick" },
	{ 0x47, "confidence" },
	{ 0x48, "width" },
	{ 0x49, "height" },
	{ 0x51, "contact id" },
	{ 0x52, "device mode" },
	{ 0x53, "device identifier" },
	{ 0x54, "contact count" },
	{ 0x55, "contact count maximum" },
	{ 0x00, NULL }
};

static const char *getUsageName(unsigned usagePage, unsigned id)
{
	// Return the text name of a usage, by page and ID code

	usageName *usageNames = NULL;
	int count;

	if (usagePage == USB_HID_USAGEPAGE_GENDESK)
		usageNames = usageNamesGenericDesktop;
	else if (usagePage == USB_HID_USAGEPAGE_DIGITIZER)
		usageNames = usageNamesDigitizer;
	else
		return ("unknown");

	for (count = 0; usageNames[count].name; count ++)
		if (usageNames[count].id == id)
			return (usageNames[count].name);

	return ("unknown");
}

static const char *getCollectionName(unsigned char id)
{
	switch (id)
	{
		case 0x00:
		return "physical (group of axes)";
		case 0x01:
		return "application (mouse, keyboard)";
		case 0x02:
		return "logical (interrelated data)";
		case 0x03:
		return "report";
		case 0x04:
		return "named array";
		case 0x05:
		return "usage switch";
		case 0x06:
		return "usage modifier";
		default:
		return "unknown";
	 }
}
#else
	#define debugHidDesc(hidDesc) do { } while (0)
	#define getUsageName(usageNames, id) ""
	#define getCollectionName(id) ""
#endif // DEBUG

#define itemSize(byte) ((byte) & 0x03)
#define itemType(byte) (((byte) & 0x0C) >> 2)
#define itemTag(byte) ((byte) >> 4)


static int getHidDescriptor(usbDevice *usbDev, int interNum,
	kernelBusTarget *busTarget, usbHidDesc *hidDesc)
{
	usbTransaction usbTrans;

	kernelDebug(debug_usb, "USB touchscreen get HID descriptor for target "
		"0x%08x, interface %d", busTarget->id, interNum);

	// Set up the USB transaction to send the 'get descriptor' command
	memset((void *) &usbTrans, 0, sizeof(usbTrans));
	usbTrans.type = usbxfer_control;
	usbTrans.address = usbDev->address;
	usbTrans.control.requestType = USB_DEVREQTYPE_INTERFACE;
	usbTrans.control.request = USB_GET_DESCRIPTOR;
	usbTrans.control.value = (USB_DESCTYPE_HID << 8);
	usbTrans.control.index = interNum;
	usbTrans.length = sizeof(usbHidDesc);
	usbTrans.buffer = hidDesc;
	usbTrans.pid = USB_PID_IN;
	usbTrans.timeout = USB_STD_TIMEOUT_MS;

	// Write the command
	return (kernelBusWrite(busTarget, sizeof(usbTransaction),
		(void *) &usbTrans));
}


static int getReportDescriptor(usbDevice *usbDev, int interNum,
	kernelBusTarget *busTarget, unsigned char *reportDesc,
	unsigned repDescLength)
{
	usbTransaction usbTrans;

	kernelDebug(debug_usb, "USB touchscreen get report descriptor for target "
		"%d, interface %d", busTarget->id, interNum);

	// Set up the USB transaction to send the 'get descriptor' command
	memset((void *) &usbTrans, 0, sizeof(usbTrans));
	usbTrans.type = usbxfer_control;
	usbTrans.address = usbDev->address;
	usbTrans.control.requestType = USB_DEVREQTYPE_INTERFACE;
	usbTrans.control.request = USB_GET_DESCRIPTOR;
	usbTrans.control.value = (USB_DESCTYPE_HIDREPORT << 8);
	usbTrans.control.index = interNum;
	usbTrans.length = repDescLength;
	usbTrans.buffer = reportDesc;
	usbTrans.pid = USB_PID_IN;
	usbTrans.timeout = USB_STD_TIMEOUT_MS;

	// Write the command
	return (kernelBusWrite(busTarget, sizeof(usbTransaction),
		(void *) &usbTrans));
}


static void saveReport(touchDevice *touchDev, genericReportDesc *report)
{
	if ((touchDev->numReports >= MAX_TOUCH_REPORTS) || !report->touch.set ||
		!report->x.set || !report->x.maximum ||
		!report->y.set || !report->y.maximum)
	{
		return;
	}

	// Since we're accepting 'mouse-like' button reports as touch reports,
	// we will insist that the minimum values for x/y are not negative.
	// Mice report relative (+/-) changes, whereas touch reports are always
	// absolute and positive.
	if (((report->x.bitLength == 8) && ((char) report->x.minimum < 0)) ||
		((report->x.bitLength == 16) && ((short) report->x.minimum < 0)) ||
		((report->x.bitLength == 32) && ((int) report->x.minimum < 0)) ||
		((report->y.bitLength == 8) && ((char) report->y.minimum < 0)) ||
		((report->y.bitLength == 16) && ((short) report->y.minimum < 0)) ||
		((report->y.bitLength == 32) && ((int) report->y.minimum < 0)))
	{
		kernelDebug(debug_usb, "USB touchscreen excluding report with "
			"negative minimum x/y");
		return;
	}

	// Save it
	memcpy(&touchDev->reports[touchDev->numReports++], report,
		sizeof(genericReportDesc));
}


static int parseReportDescriptor(unsigned char *reportDesc, unsigned len,
	touchDevice *touchDev)
{
	int status = 0;
	unsigned dataLen = 0;
	unsigned char type = 0;
	unsigned char tag = 0;
	int data = 0;
	unsigned bitOffset = 0;
	genericReportDesc report;
	reportFieldDesc *field = NULL;
	void *stack[16];
	int stackItems = 0;
	int count;

#ifdef DEBUG
	const char *typeName = NULL;
	const char *tagName = NULL;
#endif

	struct {
		int usagePage;
		int reportId;
		int reportSize;
		int reportCount;
		int logicalMinimum;
		int logicalMaximum;

	} globals;

	struct {
		int usage[256];
		unsigned char numUsages, nextUsage;
		unsigned usageMinimum;

	} locals;

	kernelDebug(debug_usb, "USB touchscreen parse report descriptor");

	memset(&report, 0, sizeof(genericReportDesc));
	memset(&globals, 0, sizeof(globals));
	memset(&locals, 0, sizeof(locals));

	kernelDebug(debug_usb, "USB touchscreen HID item type/tag/size");

	while (len)
	{
		dataLen = itemSize(*reportDesc);
		type = itemType(*reportDesc);
		tag = itemTag(*reportDesc);

#ifdef DEBUG
		typeName = ""; tagName = "";
		switch (type)
		{
			case USB_HID_ITEMTYPE_MAIN:
				typeName = "main";
				switch (tag)
				{
					case USB_HID_ITEMTAG_INPUT:
						tagName = "input";
						break;
					case USB_HID_ITEMTAG_OUTPUT:
						tagName = "output";
						break;
					case USB_HID_ITEMTAG_COLL:
						tagName = "collection";
						break;
					case USB_HID_ITEMTAG_FEATURE:
						tagName = "feature";
						break;
					case USB_HID_ITEMTAG_ENDCOLL:
						tagName = "endcollection";
						break;
					default:
						kernelDebugError("Unknown item tag %02x for type '%s'",
							tag, typeName);
						return (status = ERR_NOTIMPLEMENTED);
				}
				break;

			case USB_HID_ITEMTYPE_GLOBAL:
				typeName = "global";
				switch (tag)
				{
					case USB_HID_ITEMTAG_USAGEPG:
						tagName = "usage page";
						break;
					case USB_HID_ITEMTAG_LOGIMIN:
						tagName = "logical minimum";
						break;
					case USB_HID_ITEMTAG_LOGIMAX:
						tagName = "logical maximum";
						break;
					case USB_HID_ITEMTAG_PHYSMIN:
						tagName = "physical minimum";
						break;
					case USB_HID_ITEMTAG_PHYSMAX:
						tagName = "physical maximum";
						break;
					case USB_HID_ITEMTAG_UNITEXP:
						tagName = "unit exponent";
						break;
					case USB_HID_ITEMTAG_UNIT:
						tagName = "unit";
						break;
					case USB_HID_ITEMTAG_REPSIZE:
						tagName = "report size";
						break;
					case USB_HID_ITEMTAG_REPID:
						tagName = "report id";
						break;
					case USB_HID_ITEMTAG_REPCNT:
						tagName = "report count";
						break;
					case USB_HID_ITEMTAG_PUSH:
						tagName = "push";
						break;
					case USB_HID_ITEMTAG_POP:
						tagName = "pop";
						break;
					default:
						kernelDebugError("Unknown item tag %02x for type '%s'",
							tag, typeName);
						return (status = ERR_NOTIMPLEMENTED);
				}
				break;

			case USB_HID_ITEMTYPE_LOCAL:
				typeName = "local";
				switch (tag)
				{
					case USB_HID_ITEMTAG_USAGE:
						tagName = "usage";
						break;
					case USB_HID_ITEMTAG_USGMIN:
						tagName = "usage minimum";
						break;
					case USB_HID_ITEMTAG_USGMAX:
						tagName = "usage maximum";
						break;
					case USB_HID_ITEMTAG_DESGIDX:
						tagName = "designator index";
						break;
					case USB_HID_ITEMTAG_DESGMIN:
						tagName = "designator minimum";
						break;
					case USB_HID_ITEMTAG_DESGMAX:
						tagName = "designator maximum";
						break;
					case USB_HID_ITEMTAG_STRIDX:
						tagName = "string index";
						break;
					case USB_HID_ITEMTAG_STRMIN:
						tagName = "string minimum";
						break;
					case USB_HID_ITEMTAG_STRMAX:
						tagName = "string maximum";
						break;
					case USB_HID_ITEMTAG_DELIMTR:
						tagName = "delimiter";
						break;
					default:
						kernelDebugError("Unknown item tag %02x for type '%s'",
							tag, typeName);
						return (status = ERR_NOTIMPLEMENTED);
				}
				break;

			case USB_HID_ITEMTYPE_RES:
				typeName = "reserved";
				switch (tag)
				{
					case USB_HID_ITEMTAG_LONG:
						tagName = "long";
						break;
					default:
						kernelDebugError("Unknown item tag %02x for type '%s'",
							tag, typeName);
						return (status = ERR_NOTIMPLEMENTED);
				}
				break;

			default:
				kernelDebugError("Unknown item type %02x", type);
				return (status = ERR_NOTIMPLEMENTED);
		}
#endif

		// Long item?
		if ((type == USB_HID_ITEMTYPE_RES) && (tag == USB_HID_ITEMTAG_LONG))
		{
			// dataSize is in the next byte
			reportDesc++;
			len--;
			dataLen = *(reportDesc);
		}
		else if (dataLen == 3)
		{
			// dataLen == 3 really means 4
			dataLen = 4;
		}

		// Move to the data
		reportDesc++;
		len--;

		// Read the data
		if (dataLen == 1)
			data = (*((unsigned char *) reportDesc) & 0xFF);
		else if (dataLen == 2)
			data = (*((unsigned short *) reportDesc) & 0xFFFF);
		else if (dataLen == 4)
			data = *((unsigned *) reportDesc);
		else
			data = 0;

		kernelDebug(debug_usb, "USB touchscreen item %02x/%02x/%x [%s] %s =%d",
			type, tag, dataLen, typeName, tagName, data);

		// Interpret the data
		if (type == USB_HID_ITEMTYPE_MAIN)
		{
			if (tag == USB_HID_ITEMTAG_COLL)
			{
				kernelDebug(debug_usb, "USB touchscreen collection '%s' %s",
					getUsageName((unsigned) globals.usagePage,
						(unsigned) locals.usage[locals.nextUsage]),
					getCollectionName((unsigned) data));

				// Collection tags 'consume' one usage
				locals.nextUsage += 1;
			}

			else if (tag == USB_HID_ITEMTAG_INPUT)
			{
				for (count = 0; count < globals.reportCount; count ++)
				{
					field = NULL;

					if (globals.usagePage == USB_HID_USAGEPAGE_GENDESK)
					{
						if (locals.usage[locals.nextUsage] == 0x30) // "x"
						{
							field = &report.x;

							kernelDebug(debug_usb, "USB touchscreen reportId "
								"%d, minX=%d maxX=%d", globals.reportId,
								globals.logicalMinimum,
								globals.logicalMaximum);
						}

						else if (locals.usage[locals.nextUsage] == 0x31) // "y"
						{
							field = &report.y;

							kernelDebug(debug_usb, "USB touchscreen reportId "
								"%d, minY=%d maxY=%d", globals.reportId,
								globals.logicalMinimum,
								globals.logicalMaximum);
						}

						else if (locals.usage[locals.nextUsage] == 0x32) // "z"
						{
							field = &report.z;

							kernelDebug(debug_usb, "USB touchscreen reportId "
								"%d, minZ=%d maxZ=%d", globals.reportId,
								globals.logicalMinimum,
								globals.logicalMaximum);
						}
					}

					else if (globals.usagePage == USB_HID_USAGEPAGE_BUTTON)
					{
						// Allow a 'mouse-like' touch represented as a button
						if (locals.usage[locals.nextUsage] == 0x01) // "button 1"
						{
							field = &report.touch;

							kernelDebug(debug_usb, "USB touchscreen reportId "
								"%d, touch set", globals.reportId);
						}
					}

					else if (globals.usagePage == USB_HID_USAGEPAGE_DIGITIZER)
					{
						if (locals.usage[locals.nextUsage] == 0x42) // "tip switch"
						{
							field = &report.touch;

							kernelDebug(debug_usb, "USB touchscreen reportId "
								"%d, touch set", globals.reportId);
						}
					}

					if (field && !field->set)
					{
						field->set = 1;
						field->byteOffset =	(bitOffset / 8);
						field->bitPosition = (bitOffset % 8);
						field->bitLength = (unsigned) globals.reportSize;
						field->minimum = globals.logicalMinimum;
						field->maximum = globals.logicalMaximum;
					}

					locals.nextUsage += 1;
					bitOffset += (unsigned) globals.reportSize;
				}
			}

			// A 'main' tag clears all local variables
			memset(&locals, 0, sizeof(locals));
		}

		else if (type == USB_HID_ITEMTYPE_GLOBAL)
		{
			if (tag == USB_HID_ITEMTAG_USAGE)
				globals.usagePage = data;

			else if (tag == USB_HID_ITEMTAG_REPID)
			{
				// We are processing a new report.  Do we have an old one to
				// save?

				if (report.reportId)
					saveReport(touchDev, &report);

				globals.reportId = data;
				memset(&report, 0, sizeof(genericReportDesc));
				report.reportId = (unsigned) globals.reportId;
				bitOffset = 0;
			}

			else if (tag == USB_HID_ITEMTAG_REPSIZE)
				globals.reportSize = data;

			else if (tag == USB_HID_ITEMTAG_REPCNT)
				globals.reportCount = data;

			else if (tag == USB_HID_ITEMTAG_LOGIMIN)
				globals.logicalMinimum = data;

			else if (tag == USB_HID_ITEMTAG_LOGIMAX)
				globals.logicalMaximum = data;

			else if (tag == USB_HID_ITEMTAG_PUSH)
			{
				if (stackItems < 16)
				{
					stack[stackItems] = kernelMalloc(sizeof(globals));
					if (stack[stackItems])
					{
						memcpy(stack[stackItems], &globals, sizeof(globals));
						stackItems += 1;
					}
				}
			}

			else if (tag == USB_HID_ITEMTAG_POP)
			{
				if (stackItems)
				{
					stackItems -= 1;
					memcpy(&globals, stack[stackItems], sizeof(globals));
					kernelFree(stack[stackItems]);
				}
			}
		}

		else if (type == USB_HID_ITEMTYPE_LOCAL)
		{
			if (tag == USB_HID_ITEMTAG_USAGE)
				locals.usage[locals.numUsages++] = data;

			else if (tag == USB_HID_ITEMTAG_USGMIN)
				// We're about to be told about an array of sequential usages
				locals.usageMinimum = data;

			else if (tag == USB_HID_ITEMTAG_USGMAX)
			{
				// We now know the range of an array of sequential usages
				for (count = locals.usageMinimum; count <= data; count ++)
					locals.usage[locals.numUsages++] = count;
			}
		}

		// Move to the next item
		if ((type == USB_HID_ITEMTYPE_MAIN) &&
			(tag == USB_HID_ITEMTAG_INPUT) && !dataLen)
		{
			// Input item with dataLen 0 really means 4
			reportDesc += 4;
		}
		else
		{
			reportDesc += dataLen;
			len -= dataLen;
		}
	}

	// Do we have to save the last report we were processing?
	saveReport(touchDev, &report);

	kernelDebug(debug_usb, "USB touchscreen %d reports", touchDev->numReports);

	if (!touchDev->numReports)
	{
		// Not a supported device
		kernelDebugError("HID device has no supported touchscreen reports");
		return (status = ERR_NOTIMPLEMENTED);
	}

#ifdef DEBUG
	for (count = 0; count < touchDev->numReports; count ++)
	{
		kernelDebug(debug_usb, "USB touchscreen report %d:",
			touchDev->reports[count].reportId);

		if (touchDev->reports[count].x.set)
			kernelDebug(debug_usb, "USB touchscreen x: byte %d, bit %d, "
				"len=%d", touchDev->reports[count].x.byteOffset,
				touchDev->reports[count].x.bitPosition,
				touchDev->reports[count].x.bitLength);
		else
			kernelDebug(debug_usb, "USB touchscreen x: (not set)");

		if (touchDev->reports[count].y.set)
			kernelDebug(debug_usb, "USB touchscreen y: byte %d, bit %d, "
				"len=%d", touchDev->reports[count].y.byteOffset,
				touchDev->reports[count].y.bitPosition,
				touchDev->reports[count].y.bitLength);
		else
			kernelDebug(debug_usb, "USB touchscreen y: (not set)");

		if (touchDev->reports[count].z.set)
			kernelDebug(debug_usb, "USB touchscreen z: byte %d, bit %d, "
				"len=%d", touchDev->reports[count].z.byteOffset,
				touchDev->reports[count].z.bitPosition,
				touchDev->reports[count].z.bitLength);
		else
			kernelDebug(debug_usb, "USB touchscreen z: (not set)");

		if (touchDev->reports[count].touch.set)
			kernelDebug(debug_usb, "USB touchscreen touch: byte %d, bit %d, "
				"len=%d", touchDev->reports[count].touch.byteOffset,
				touchDev->reports[count].touch.bitPosition,
				touchDev->reports[count].touch.bitLength);
		else
			kernelDebug(debug_usb, "USB touchscreen touch: (not set)");
	}
#endif

	return (status = 0);
}


static void interrupt(usbDevice *usbDev, int interface, void *buffer,
	unsigned length __attribute__((unused)))
{
	touchDevice *touchDev = usbDev->interface[interface].data;
	genericReportDesc *reportDesc = NULL;
	int haveReportId = 0;
	kernelTouchReport report;
	int count;

	kernelDebug(debug_usb, "USB touchscreen interrupt %u bytes", length);

	if (touchDev->numReports && touchDev->reports[0].reportId)
		haveReportId = 1;

	for (count = 0; count < touchDev->numReports; count ++)
	{
		if (!haveReportId || (touchDev->reports[count].reportId ==
			((unsigned char *) buffer)[0]))
		{
			reportDesc = &touchDev->reports[count];
			memset(&report, 0, sizeof(kernelTouchReport));

			if (reportDesc->x.set)
			{
				//kernelDebug(debug_usb, "USB touchscreen x offset=%d pos=%d "
				//	"len=%d ", report->x.byteOffset, report->x.bitPosition,
				//	report->x.bitLength);

				report.x = *((int *)(buffer + haveReportId +
					reportDesc->x.byteOffset));
				report.x >>= reportDesc->x.bitPosition;
				report.x &= ((unsigned) -1 >> (32 - reportDesc->x.bitLength));
				report.maxX = reportDesc->x.maximum;
			}

			if (reportDesc->y.set)
			{
				//kernelDebug(debug_usb, "USB touchscreen y offset=%d pos=%d "
				//	"len=%d ", report->y.byteOffset, report->y.bitPosition,
				//	report->y.bitLength);

				report.y = *((int *)(buffer + haveReportId +
					reportDesc->y.byteOffset));
				report.y >>= reportDesc->y.bitPosition;
				report.y &= ((unsigned) -1 >> (32 - reportDesc->y.bitLength));
				report.maxY = reportDesc->y.maximum;
			}

			if (reportDesc->z.set)
			{
				//kernelDebug(debug_usb, "USB touchscreen z offset=%d pos=%d "
				//	"len=%d ", report->z.byteOffset, report->z.bitPosition,
				//	report->z.bitLength);

				report.z = *((int *)(buffer + haveReportId +
					reportDesc->z.byteOffset));
				report.z >>= reportDesc->z.bitPosition;
				report.z &= ((unsigned) -1 >> (32 - reportDesc->z.bitLength));
				report.maxZ = reportDesc->z.maximum;
			}

			if (reportDesc->touch.set)
			{
				//kernelDebug(debug_usb, "USB touchscreen touch offset=%d "
				//	"pos=%d len=%d ", report->touch.byteOffset,
				//	report->touch.bitPosition, report->touch.bitLength);

				report.flags = *((int *)(buffer + haveReportId +
					reportDesc->touch.byteOffset));
				report.flags >>= reportDesc->touch.bitPosition;
				report.flags &= ((unsigned) -1 >> (32 -
					reportDesc->touch.bitLength));
			}

			kernelDebug(debug_usb, "USB touchscreen report %d, x=%d, y=%d, "
				"z=%d, touch=%u", reportDesc->reportId, report.x, report.y,
				report.z, report.flags);

			if (kernelGraphicsAreEnabled() &&
				((report.x != touchDev->prevReport.x) ||
					(report.y != touchDev->prevReport.y) ||
					(report.flags != touchDev->prevReport.flags)))
			{
				kernelTouchInput(&report);
			}

			// Save this report
			memcpy(&touchDev->prevReport, &report, sizeof(kernelTouchReport));

			break;
		}
	}
}


static int detectTarget(void *parent, int target, void *driver)
{
	int status = 0;
	touchDevice *touchDev = NULL;
	kernelBusTarget *busTarget = NULL;
	int controller __attribute__((unused));
	int address __attribute__((unused));
	int interNum = 0;
	usbInterface *interface = NULL;
	usbEndpoint *endpoint = NULL;
	usbEndpoint *intrInEndp = NULL;
	usbHidDesc hidDesc;
	unsigned char *reportDesc = NULL;
	int supported = 0;
	char value[32];
	int count;

	// Get memory for a touchscreen device structure
	touchDev = kernelMalloc(sizeof(touchDevice));
	if (!touchDev)
		return (status = ERR_MEMORY);

	// Get the bus target
	busTarget = kernelBusGetTarget(bus_usb, target);
	if (!busTarget)
	{
		status = ERR_NOSUCHENTRY;
		goto out;
	}

	// Get the USB device
	touchDev->usbDev = kernelUsbGetDevice(target);
	if (!touchDev->usbDev)
	{
		status = ERR_NOSUCHENTRY;
		goto out;
	}

	// Get the interface number
	usbMakeContAddrIntr(target, controller, address, interNum);

	interface = (usbInterface *) &touchDev->usbDev->interface[interNum];

	kernelDebug(debug_usb, "USB touchscreen HID device has %d interfaces",
		touchDev->usbDev->numInterfaces);

	kernelDebug(debug_usb, "USB touchscreen checking interface %d", interNum);

	// Check that the interface class is 0x03
	if (interface->classCode != 0x03)
		// Not a human interface interface
		goto out;

	kernelDebug(debug_usb, "USB touchscreen class=0x%02x subclass=0x%02x "
		"protocol=0x%02x", interface->classCode, interface->subClassCode,
		interface->protocol);

	// Look for an interrupt-in endpoint
	for (count = 0; count < interface->numEndpoints; count ++)
	{
		endpoint = (usbEndpoint *) &interface->endpoint[count];

		if (((endpoint->attributes & USB_ENDP_ATTR_MASK) ==
			USB_ENDP_ATTR_INTERRUPT) && (endpoint->number & 0x80))
		{
			intrInEndp = endpoint;
			kernelDebug(debug_usb, "USB touchscreen got interrupt endpoint "
				"%02x for interface %d", intrInEndp->number, count);
			break;
		}
	}

	// We *must* have an interrupt in endpoint.
	if (!intrInEndp)
	{
		kernelDebug(debug_usb, "USB touchscreen device 0x%08x has no "
			"interrupt endpoint", target);
		goto out;
	}

	// Set the device configuration
	status = kernelUsbSetDeviceConfig(touchDev->usbDev);
	if (status < 0)
		goto out;

	// Try to get the HID descriptor
	status = getHidDescriptor(touchDev->usbDev, interNum, busTarget, &hidDesc);
	if (status < 0)
		goto out;

	debugHidDesc(&hidDesc);

	reportDesc = kernelMalloc(hidDesc.repDescLength);
	if (!reportDesc)
	{
		status = ERR_MEMORY;
		goto out;
	}

	status = getReportDescriptor(touchDev->usbDev, interNum, busTarget,
		reportDesc, hidDesc.repDescLength);
	if (status < 0)
		goto out;

	status = parseReportDescriptor(reportDesc, hidDesc.repDescLength,
		touchDev);
	if (status < 0)
	{
		// Not an error - just not supported
		status = 0;
		goto out;
	}

	// We have a supported interface
	kernelDebug(debug_usb, "USB touchscreen found touchscreen interface %d, "
		"%d reports", interNum, touchDev->numReports);

	interface->data = touchDev;
	supported = 1;

	// Schedule the regular interrupt.
	kernelUsbScheduleInterrupt(touchDev->usbDev, interNum, intrInEndp->number,
		intrInEndp->interval, intrInEndp->maxPacketSize, &interrupt);

	// Tell USB that we're claiming this device.
	kernelBusDeviceClaim(busTarget, driver);

	// Set up the kernel device
	touchDev->dev.device.class = kernelDeviceGetClass(DEVICECLASS_TOUCHSCR);
	touchDev->dev.device.subClass =
		kernelDeviceGetClass(DEVICESUBCLASS_TOUCHSCR_USB);
	kernelUsbSetDeviceAttrs(touchDev->usbDev, interNum, &touchDev->dev);
	snprintf(value, sizeof(value), "%d", touchDev->numReports);
	kernelVariableListSet(&touchDev->dev.device.attrs, "touch.reports", value);
	touchDev->dev.driver = driver;

	// Add the kernel device
	status = kernelDeviceAdd(parent, &touchDev->dev);

	// Tell the touch functions
	kernelTouchDetected();

out:
	if (reportDesc)
		kernelFree(reportDesc);

	if (busTarget)
		kernelFree(busTarget);

	if ((status < 0) || !supported)
	{
		if (touchDev)
			kernelFree(touchDev);
	}
	else
	{
		kernelDebug(debug_usb, "USB touchscreen detected device");
	}

	return (status);
}


static int driverDetect(void *parent __attribute__((unused)),
	kernelDriver *driver)
{
	// Try to detect USB touchscreens.

	int status = 0;
	kernelBusTarget *busTargets = NULL;
	int numBusTargets = 0;
	int deviceCount = 0;
	usbDevice usbDev;

	kernelDebug(debug_usb, "USB touchscreen search for devices");

	// Search the USB bus(es) for devices
	numBusTargets = kernelBusGetTargets(bus_usb, &busTargets);
	if (numBusTargets > 0)
	{
		// Search the bus targets for USB touchscreen devices
		for (deviceCount = 0; deviceCount < numBusTargets; deviceCount ++)
		{
			// Try to get the USB information about the target
			status = kernelBusGetTargetInfo(&busTargets[deviceCount],
				(void *) &usbDev);
			if (status < 0)
				continue;

			// If the USB class is 0x03, then we *may* have a touchscreen
			// device
			if (usbDev.classCode != 0x03)
				continue;

			// Already claimed?
			if (busTargets[deviceCount].claimed)
				continue;

			kernelDebug(debug_usb, "USB touchscreen found possible device");

			detectTarget(usbDev.controller->dev, busTargets[deviceCount].id,
				driver);
		}

		kernelFree(busTargets);
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

void kernelUsbTouchscreenDriverRegister(kernelDriver *driver)
{
	// Device driver registration.

	driver->driverDetect = driverDetect;

	return;
}

