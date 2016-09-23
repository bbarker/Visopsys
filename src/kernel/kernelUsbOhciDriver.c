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
//  kernelUsbOhciDriver.c
//

#include "kernelUsbDriver.h"
#include "kernelUsbOhciDriver.h"
#include "kernelCpu.h"
#include "kernelDebug.h"
#include "kernelError.h"
#include "kernelLog.h"
#include "kernelMalloc.h"
#include "kernelMemory.h"
#include "kernelPage.h"
#include "kernelParameters.h"
#include "kernelPciDriver.h"
#include "kernelVariableList.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int reset(usbController *);

#ifdef DEBUG
static inline void debugOpRegs(ohciData *ohci)
{
	kernelDebug(debug_usb, "OHCI operational registers:\n"
		"  hcRevision=0x%08x\n"
		"  hcControl=0x%08x\n"
		"  hcCommandStatus=0x%08x\n"
		"  hcInterruptStatus=0x%08x\n"
		"  hcInterruptEnable=0x%08x\n"
		"  hcInterruptDisable=0x%08x\n"
		"  hcHcca=0x%08x\n"
		"  hcPeriodCurrentEd=0x%08x\n"
		"  hcControlHeadEd=0x%08x\n"
		"  hcControlCurrentEd=0x%08x\n"
		"  hcBulkHeadEd=0x%08x\n"
		"  hcBulkCurrentEd=0x%08x\n"
		"  hcDoneHead=0x%08x\n"
		"  hcFmInterval=0x%08x\n"
		"  hcFmRemaining=0x%08x\n"
		"  hcFmNumber=0x%08x\n"
		"  hcPeriodicStart=0x%08x\n"
		"  hcLsThreshold=0x%08x\n"
		"  hcRhDescriptorA=0x%08x\n"
		"  hcRhDescriptorB=0x%08x\n"
		"  hcRhStatus=0x%08x",
		ohci->opRegs->hcRevision, ohci->opRegs->hcControl,
		ohci->opRegs->hcCommandStatus, ohci->opRegs->hcInterruptStatus,
		ohci->opRegs->hcInterruptEnable, ohci->opRegs->hcInterruptDisable,
		ohci->opRegs->hcHcca, ohci->opRegs->hcPeriodicCurrentEd,
		ohci->opRegs->hcControlHeadEd, ohci->opRegs->hcControlCurrentEd,
		ohci->opRegs->hcBulkHeadEd, ohci->opRegs->hcBulkCurrentEd,
		ohci->opRegs->hcDoneHead, ohci->opRegs->hcFmInterval,
		ohci->opRegs->hcFmRemaining, ohci->opRegs->hcFmNumber,
		ohci->opRegs->hcPeriodicStart, ohci->opRegs->hcLsThreshold,
		ohci->opRegs->hcRhDescriptorA, ohci->opRegs->hcRhDescriptorB,
		ohci->opRegs->hcRhStatus);
}

static inline void debugRootHub(ohciData *ohci)
{
	unsigned descA = ohci->opRegs->hcRhDescriptorA;
	unsigned descB = ohci->opRegs->hcRhDescriptorB;

	kernelDebug(debug_usb, "OHCI root hub registers:\n"
		"  hcRhDescriptorA=0x%08x\n"
		"    powerOn2PowerGood=%d\n"
		"    noOverCurrentProtect=%d\n"
		"    overCurrentProtMode=%d\n"
		"    noPowerSwitching=%d\n"
		"    powerSwitchingMode=%d\n"
		"    numDownstreamPorts=%d\n"
		"  hcRhDescriptorB=0x%08x\n"
		"    portPowerCtrlMask=0x%04x\n"
		"    deviceRemovable=0x%04x", descA,
		((descA & OHCI_ROOTDESCA_POTPGT) >> 24),
		((descA & OHCI_ROOTDESCA_NOCP) >> 15),
		((descA & OHCI_ROOTDESCA_OCPM) >> 11),
		((descA & OHCI_ROOTDESCA_NPS) >> 9),
		((descA & OHCI_ROOTDESCA_PSM) >> 8),
		(descA & OHCI_ROOTDESCA_NDP), descB,
		((descB & OHCI_ROOTDESCB_PPCM) >> 16),
		(descB & OHCI_ROOTDESCB_DR));
}

static inline void debugPortStatus(ohciData *ohci, int portNum)
{
	unsigned status = ohci->opRegs->hcRhPortStatus[portNum];

	kernelDebug(debug_usb, "OHCI port %d status: 0x%08x\n"
		"  resetChange=%d overCurrentChange=%d suspendedChange=%d\n"
		"  enabledChange=%d connectedChange=%d lowSpeed=%d\n"
		"  power=%d reset=%d overCurrent=%d suspended=%d\n"
		"  enabled=%d connected=%d", portNum, status,
		((status & OHCI_PORTSTAT_PRSC) >> 20),
		((status & OHCI_PORTSTAT_OCIC) >> 19),
		((status & OHCI_PORTSTAT_PSSC) >> 18),
		((status & OHCI_PORTSTAT_PESC) >> 17),
		((status & OHCI_PORTSTAT_CSC) >> 16),
		((status & OHCI_PORTSTAT_LSDA) >> 9),
		((status & OHCI_PORTSTAT_PPS) >> 8),
		((status & OHCI_PORTSTAT_PRS) >> 4),
		((status & OHCI_PORTSTAT_POCI) >> 3),
		((status & OHCI_PORTSTAT_PSS) >> 2),
		((status & OHCI_PORTSTAT_PES) >> 1),
		(status & OHCI_PORTSTAT_CCS));
}

static inline const char *debugOpState2String(ohciData *ohci)
{
	switch (ohci->opRegs->hcControl & OHCI_HCCTRL_HCFS)
	{
		default:
		case OHCI_HCCTRL_HCFS_RESET:
			return ("USBRESET");
		case OHCI_HCCTRL_HCFS_RESUME:
			return ("USBRESUME");
		case OHCI_HCCTRL_HCFS_OPERATE:
			return ("USBOPERATIONAL");
		case OHCI_HCCTRL_HCFS_SUSPEND:
			return ("USBSUSPEND");
	}
}

static inline void debugEndpointDesc(ohciEndpDesc *endpDesc)
{
	kernelDebug(debug_usb, "OHCI endpoint descriptor 0x%08x:\n"
		"  flags=0x%08x\n"
		"    maxPacketSize=%d\n"
		"    format=%d (%s)\n"
		"    skip=%d\n"
		"    speed=%d (%s)\n"
		"    direction=%d\n"
		"    endpoint=0x%02x\n"
		"    address=%d\n"
		"  tdQueueTail=0x%08x\n"
		"  tdQueueHead=0x%08x\n"
		"  nextEd=0x%08x",
		kernelPageGetPhysical(KERNELPROCID, (void *) endpDesc),
		endpDesc->flags,
		((endpDesc->flags & OHCI_EDFLAGS_MAXPACKET) >> 16),
		((endpDesc->flags & OHCI_EDFLAGS_FORMAT) >> 15),
		((endpDesc->flags & OHCI_EDFLAGS_FORMAT)? "isoc" : "normal"),
		((endpDesc->flags & OHCI_EDFLAGS_SKIP) >> 14),
		((endpDesc->flags & OHCI_EDFLAGS_SPEED) >> 13),
		((endpDesc->flags & OHCI_EDFLAGS_SPEED)? "low" : "full"),
		((endpDesc->flags & OHCI_EDFLAGS_DIRECTION) >> 11),
		((endpDesc->flags & OHCI_EDFLAGS_ENDPOINT) >> 7),
		(endpDesc->flags & OHCI_EDFLAGS_ADDRESS), endpDesc->tailPhysical,
		endpDesc->headPhysical, endpDesc->nextPhysical);
}

static inline void debugTransDesc(ohciTransDesc *transDesc)
{
	kernelDebug(debug_usb, "OHCI transfer descriptor 0x%08x:\n"
		"  flags=0x%08x\n"
		"    condCode=%d\n"
		"    errCount=%d\n"
		"    dataToggle=%d\n"
		"    delayInt=%d\n"
		"    dirPid=%d\n"
		"    rounding=%d\n"
		"  currBuffPtr=0x%08x\n"
		"  nextPhysical=0x%08x\n"
		"  bufferEnd=0x%08x", transDesc->physical, transDesc->flags,
		((transDesc->flags & OHCI_TDFLAGS_CONDCODE) >> 28),
		((transDesc->flags & OHCI_TDFLAGS_ERRCOUNT) >> 26),
		((transDesc->flags & OHCI_TDFLAGS_DATATOGGLE) >> 24),
		((transDesc->flags & OHCI_TDFLAGS_DELAYINT) >> 21),
		((transDesc->flags & OHCI_TDFLAGS_DIRPID) >> 19),
		((transDesc->flags & OHCI_TDFLAGS_ROUNDING) >> 18),
		transDesc->currBuffPtr, transDesc->nextPhysical, transDesc->bufferEnd);
}
#else
	#define debugOpRegs(ohci) do { } while (0)
	#define debugRootHub(ohci) do { } while (0)
	#define debugPortStatus(ohci, portNum) do { } while (0)
	#define debugOpState2String(ohci) ""
	#define debugEndpointDesc(endpDesc) do { } while (0)
	#define debugTransDesc(transDesc) do { } while (0)
#endif // DEBUG


static int removeFromDoneQueue(usbController *controller,
	ohciTransDesc *transDescs, int numDescs)
{
	// If any part of a transaction is at the head of the done queue, this
	// will remove it.

	int status = 0;
	ohciData *ohci = controller->data;
	int count;

	kernelDebug(debug_usb, "OHCI remove transaction from done queue");

	if (!(ohci->opRegs->hcInterruptStatus & OHCI_HCINT_WDH))
	{
		kernelDebugError("Done queue is not valid (value=0x%08x)",
			ohci->hcca->doneHead);
		return (status = ERR_NODATA);
	}

	// Lock the controller.
	status = kernelLockGet(&controller->lock);
	if (status < 0)
	{
		kernelError(kernel_error, "Can't get controller lock");
		return (status);
	}

	for (count = (numDescs - 1); count >= 0; count --)
	{
		if ((ohci->hcca->doneHead & ~1) == transDescs[count].physical)
		{
			kernelDebug(debug_usb, "OHCI remove 0x%08x from done queue",
				transDescs[count].physical);
			ohci->hcca->doneHead = transDescs[count].nextPhysical;
		}
	}

	// Did we empty the done queue?
	if (ohci->hcca->doneHead)
	{
		kernelDebug(debug_usb, "OHCI done queue is not empty");
	}
	else
	{
		kernelDebug(debug_usb, "OHCI done queue is now empty");

		// Clear the 'writeback done head' interrupt bit (tell the controller
		// it can give us a new done queue)
		ohci->opRegs->hcInterruptStatus = OHCI_HCINT_WDH;
	}

	kernelLockRelease(&controller->lock);
	return (status = 0);
}


static int linkTransaction(usbController *controller, ohciEndpDesc *endpDesc,
	ohciTransDesc *transDescs)
{
	// Attach the TDs to the queue of the ED

	int status = 0;
	ohciData *ohci = controller->data;
	unsigned hcFmNumber = ohci->opRegs->hcFmNumber;
	ohciTransDesc *lastDesc = NULL;

	kernelDebug(debug_usb, "OHCI link transaction to ED");

	// Make sure the controller isn't processing this ED
	endpDesc->flags |= OHCI_EDFLAGS_SKIP;

	// Wait for the frame number to change
	while (ohci->opRegs->hcFmNumber == hcFmNumber);

	// Lock the controller.
	status = kernelLockGet(&controller->lock);
	if (status < 0)
	{
		kernelError(kernel_error, "Can't get controller lock");
		return (status);
	}

	if (endpDesc->head)
	{
		kernelDebug(debug_usb, "OHCI linking to last descriptor");

		lastDesc = endpDesc->head;

		while (lastDesc->next)
			lastDesc = lastDesc->next;

		lastDesc->next = transDescs;
		lastDesc->nextPhysical = transDescs[0].physical;
	}
	else
	{
		kernelDebug(debug_usb, "OHCI linking at the head");

		endpDesc->head = transDescs;
		endpDesc->headPhysical = ((endpDesc->headPhysical & 0xF) |
			transDescs[0].physical);
	}

	kernelLockRelease(&controller->lock);

	// The controller can now process this ED
	endpDesc->flags &= ~OHCI_EDFLAGS_SKIP;

	return (status = 0);
}


static int unlinkTransaction(usbController *controller,
	ohciEndpDesc *endpDesc, ohciTransDesc *transDescs, int numDescs)
{
	// Remove the TDs from the (virtual) queue of the ED.  The controller
	// removes the physical pointers.

	int status = 0;
	ohciData *ohci = controller->data;
	unsigned hcFmNumber = ohci->opRegs->hcFmNumber;
	ohciTransDesc *tmpDesc = NULL;

	kernelDebug(debug_usb, "OHCI unlink TDs from ED");

	// Make sure the controller isn't processing this ED
	endpDesc->flags |= OHCI_EDFLAGS_SKIP;

	// Wait for the frame number to change
	while (ohci->opRegs->hcFmNumber == hcFmNumber);

	// Lock the controller.
	status = kernelLockGet(&controller->lock);
	if (status < 0)
	{
		kernelError(kernel_error, "Can't get controller lock");
		return (status);
	}

	if (endpDesc->head == &transDescs[0])
	{
		kernelDebug(debug_usb, "OHCI unlinking from the head");
		endpDesc->head = transDescs[numDescs - 1].next;
	}
	else
	{
		kernelDebug(debug_usb, "OHCI unlinking from another TD");

		tmpDesc = endpDesc->head;
		while (tmpDesc)
		{
			if (tmpDesc->next == &transDescs[0])
			{
				tmpDesc->next = transDescs[numDescs - 1].next;
				break;
			}

			tmpDesc = tmpDesc->next;
		}

		if (!tmpDesc)
			kernelDebugError("Couldn't find transaction for unlink");
	}

	kernelLockRelease(&controller->lock);

	// The controller can now process this ED
	endpDesc->flags &= ~OHCI_EDFLAGS_SKIP;

	return (status = 0);
}


static ohciEndpDesc *findEndpDesc(ohciData *ohci, usbDevice *usbDev,
	int endpoint)
{
	// Search for an ED for a particular device+endpoint

	ohciEndpDesc *endpDesc = NULL;
	kernelLinkedList *usedList = &ohci->usedEndpDescs;
	kernelLinkedListItem *iter = NULL;

	kernelDebug(debug_usb, "OHCI find ED for usbDev %p, endpoint 0x%02x",
		usbDev, endpoint);

	// Try searching for an existing ED
	if (usedList->numItems)
	{
		endpDesc = kernelLinkedListIterStart(usedList, &iter);

		while (endpDesc)
		{
			if (endpDesc->usbDev)
			{
				kernelDebug(debug_usb, "OHCI examine ED for device %p "
					"endpoint 0x%02x", endpDesc->usbDev, endpDesc->endpoint);

				if ((endpDesc->usbDev == usbDev) &&
					(endpDesc->endpoint == endpoint))
				{
					break;
				}
			}

			endpDesc = kernelLinkedListIterNext(usedList, &iter);
		}

		// Found it?
		if (endpDesc)
			kernelDebug(debug_usb, "OHCI found ED");
		else
			kernelDebug(debug_usb, "OHCI ED not found");
	}
	else
		kernelDebug(debug_usb, "OHCI no items in ED list");

	return (endpDesc);
}


static int releaseEndpDesc(ohciData *ohci, ohciEndpDesc *endpDesc)
{
	// Remove the ED from the list of 'used' ones, and add it back into the
	// list of 'free' ones.

	int status = 0;
	kernelLinkedList *usedList = &ohci->usedEndpDescs;
	kernelLinkedList *freeList = &ohci->freeEndpDescs;

	// Remove it from the used list
	if (kernelLinkedListRemove(usedList, (void *) endpDesc) >= 0)
	{
		// Add it to the free list
		if (kernelLinkedListAdd(freeList, (void *) endpDesc) < 0)
			kernelError(kernel_warn, "Couldn't add item to ED free list");
	}
	else
	{
		kernelError(kernel_warn, "Couldn't remove item from ED used list");
	}

	return (status = 0);
}


static int allocEndpDescs(kernelLinkedList *freeList)
{
	// Allocate a page worth of physical memory for ohciEndpDesc data
	// structures, and add them to the supplied kernelLinkedList.

	int status = 0;
	kernelIoMemory ioMem;
	ohciEndpDesc *endpDescs = NULL;
	int numEndpDescs = 0;
	int count;

	kernelDebug(debug_usb, "OHCI adding EDs to free list");

	// Request an aligned page of I/O memory (we need to be sure of 16-byte
	// alignment for each ED)
	status = kernelMemoryGetIo(MEMORY_PAGE_SIZE, MEMORY_PAGE_SIZE, &ioMem);
	if (status < 0)
		goto err_out;

	endpDescs = ioMem.virtual;

	// How many EDs per memory page?
	numEndpDescs = (MEMORY_PAGE_SIZE / sizeof(ohciEndpDesc));

	// Loop through all of them, and add them to the supplied free list
	for (count = 0; count < numEndpDescs; count ++)
	{
		status = kernelLinkedListAdd(freeList, (void *) &endpDescs[count]);
		if (status < 0)
		{
			kernelError(kernel_error, "Couldn't add new EDs to free list");
			goto err_out;
		}
	}

	kernelDebug(debug_usb, "OHCI added %d queue heads", numEndpDescs);
	return (status = 0);

err_out:

	if (endpDescs)
		kernelMemoryReleaseIo(&ioMem);

	return (status);
}


static ohciEndpDesc *allocEndpDesc(ohciData *ohci, usbDevice *usbDev,
	int endpoint)
{
	// Allocate an ED.  Each device endpoint has at most one ED (which may be
	// linked into either the synchronous or asynchronous queues, depending
	// on the endpoint type).  We also use EDs as generic heads for queues,
	// so it's OK for usbDev and endpoint to be NULL.

	kernelLinkedList *usedList = &ohci->usedEndpDescs;
	kernelLinkedList *freeList = &ohci->freeEndpDescs;
	ohciEndpDesc *endpDesc = NULL;
	kernelLinkedListItem *iter = NULL;

	kernelDebug(debug_usb, "OHCI alloc ED");

	// Anything in the free list?
	if (!freeList->numItems)
	{
		// Super, the free list is empty.  We need to allocate everything.
		if (allocEndpDescs(freeList) < 0)
		{
			kernelError(kernel_error, "Couldn't allocate new EDs");
			goto err_out;
		}
	}

	// Grab the first one from the free list
	endpDesc = kernelLinkedListIterStart(freeList, &iter);
	if (!endpDesc)
	{
		kernelError(kernel_error, "Couldn't get a new ED");
		goto err_out;
	}

	// Remove it from the free list
	if (kernelLinkedListRemove(freeList, (void *) endpDesc) < 0)
	{
		kernelError(kernel_error, "Couldn't remove ED from free list");
		goto err_out;
	}

	// Initialize it
	memset((void *) endpDesc, 0, sizeof(ohciEndpDesc));
	endpDesc->flags = OHCI_EDFLAGS_SKIP;
	endpDesc->usbDev = (usbDevice *) usbDev;
	endpDesc->endpoint = endpoint;

	if (usbDev)
	{
		if (usbDev->speed == usbspeed_low)
			endpDesc->flags |= OHCI_EDFLAGS_SPEED;
	}

	// Add it to the used list
	if (kernelLinkedListAdd(usedList, (void *) endpDesc) < 0)
	{
		kernelError(kernel_error, "Couldn't add ED to used list");
		goto err_out;
	}

	// Return success
	return (endpDesc);

err_out:
	if (endpDesc)
		releaseEndpDesc(ohci, endpDesc);

	return (endpDesc = NULL);
}


static int linkEndpDescToQueue(usbController *controller,
	ohciEndpDesc *queueEndpDesc, ohciEndpDesc *linkEndpDesc)
{
	int status = 0;
	ohciData *ohci = controller->data;
	unsigned hcFmNumber = ohci->opRegs->hcFmNumber;

	kernelDebug(debug_usb, "OHCI link ED to queue");

	// Lock the controller.
	status = kernelLockGet(&controller->lock);
	if (status < 0)
	{
		kernelError(kernel_error, "Can't get controller lock");
		return (status);
	}

	// Disable processing of the queue whilst we change the pointers
	if (queueEndpDesc == ohci->queueEndpDescs[OHCI_ED_CONTROL])
		// Disable control processing
		ohci->opRegs->hcControl &= ~OHCI_HCCTRL_CLE;
	else if (queueEndpDesc == ohci->queueEndpDescs[OHCI_ED_BULK])
		// Disable bulk processing
		ohci->opRegs->hcControl &= ~OHCI_HCCTRL_BLE;
	else
		// Disable interrupt processing
		ohci->opRegs->hcControl &= ~OHCI_HCCTRL_PLE;

	// Wait for the frame number to change
	while (ohci->opRegs->hcFmNumber == hcFmNumber);

	linkEndpDesc->next = queueEndpDesc->next;
	linkEndpDesc->nextPhysical = queueEndpDesc->nextPhysical;
	queueEndpDesc->next = linkEndpDesc;
	queueEndpDesc->nextPhysical = kernelPageGetPhysical(KERNELPROCID,
		(void *) linkEndpDesc);

	// Re-enable queue processing
	ohci->opRegs->hcControl |= (OHCI_HCCTRL_BLE | OHCI_HCCTRL_CLE |
		OHCI_HCCTRL_PLE);

	kernelLockRelease(&controller->lock);
	return (status = 0);
}


static void updateEndpDescFlags(ohciEndpDesc *endpDesc)
{
	int maxPacketSize = 0;
	usbEndpoint *endpoint = NULL;

	endpoint = kernelUsbGetEndpoint(endpDesc->usbDev, endpDesc->endpoint);
	if (!endpoint)
	{
		kernelError(kernel_error, "Endpoint 0x%02x not found",
			endpDesc->endpoint);
		return;
	}

	// Set the maximum endpoint packet size
	maxPacketSize = endpoint->maxPacketSize;

	// If we haven't yet got the descriptors, etc., use 8 as the maximum size
	if (!maxPacketSize)
	{
		kernelDebug(debug_usb, "OHCI using default maximum endpoint transfer "
			"size 8 for endpoint 0x%02x", endpDesc->endpoint);
		maxPacketSize = 8;
	}

	endpDesc->flags &= ~(OHCI_EDFLAGS_MAXPACKET | OHCI_EDFLAGS_ENDPOINT |
		OHCI_EDFLAGS_ADDRESS);

	endpDesc->flags |= (((maxPacketSize << 16) & OHCI_EDFLAGS_MAXPACKET) |
		((endpDesc->endpoint << 7) & OHCI_EDFLAGS_ENDPOINT) |
		(endpDesc->usbDev->address & OHCI_EDFLAGS_ADDRESS));

	kernelDebug(debug_usb, "OHCI endpoint 0x%02x, maxPacketSize=%d",
		endpDesc->endpoint, maxPacketSize);
}


static ohciTransDesc *allocTransDescs(int numDescs)
{
	// Allocate an array of OHCI transfer descriptors, 16-byte-aligned.

	ohciTransDesc *transDescs = NULL;
	unsigned memSize = 0;
	kernelIoMemory ioMem;
	int count;

	kernelDebug(debug_usb, "OHCI allocate %d TDs", numDescs);

	memSize = (numDescs * sizeof(ohciTransDesc));

	if (kernelMemoryGetIo(memSize, 16 /* alignment */, &ioMem) < 0)
	{
		kernelError(kernel_error, "Unable to get TD memory");
		return (transDescs = NULL);
	}

	transDescs = ioMem.virtual;

	// Connect the descriptors and set their physical addresses
	for (count = 0; count < numDescs; count ++)
	{
		transDescs[count].physical = (ioMem.physical +
			(count * sizeof(ohciTransDesc)));

		if (count)
		{
			transDescs[count - 1].nextPhysical = transDescs[count].physical;
			transDescs[count - 1].next = &transDescs[count];
		}
	}

	return (transDescs);
}


static int setTransDescBuffer(ohciTransDesc *transDesc)
{
	// Allocate a data buffer for a TD.  This is only used for cases in which
	// the caller doesn't supply its own data buffer, such as the setup stage
	// of control transfers, or for interrupt registrations.

	int status = 0;
	unsigned buffPhysical = NULL;

	if (!transDesc->buffer)
	{
		// Get the memory from kernelMalloc(), so that the caller can easily
		// kernelFree() it when finished.
		kernelDebug(debug_usb, "OHCI allocate TD buffer of %u",
			transDesc->buffSize);
		transDesc->buffer = kernelMalloc(transDesc->buffSize);
	}

	if (!transDesc->buffer)
		return (status = ERR_MEMORY);

	// Get the physical address of this memory
	buffPhysical = kernelPageGetPhysical(KERNELPROCID, transDesc->buffer);
	if (!buffPhysical)
	{
		kernelError(kernel_error, "Couldn't get physical address of "
			"transaction buffer");
		return (status = ERR_BADADDRESS);
	}

	// Now set up the buffer pointers in the TD
	transDesc->currBuffPtr = buffPhysical;
	transDesc->bufferEnd = (buffPhysical + (transDesc->buffSize - 1));

	// Return success
	return (status = 0);
}


static int waitTransactionComplete(ohciData *ohci, ohciTransDesc *lastDesc,
	unsigned timeout)
{
	// Loop until the last descriptor of a transaction appers in the HCCA
	// done queue.

	uquad_t currTime = kernelCpuGetMs();
	uquad_t endTime = (currTime + timeout);

	kernelDebug(debug_usb, "OHCI wait for transaction complete");

	while (currTime <= endTime)
	{
		if (ohci->opRegs->hcInterruptStatus & OHCI_HCINT_WDH)
		{
			if ((ohci->hcca->doneHead & ~1) == lastDesc->physical)
			{
				kernelDebug(debug_usb, "OHCI transaction complete");
				return (0);
			}
		}

		currTime = kernelCpuGetMs();
	}

	kernelError(kernel_error, "Transaction timed out");
	return (ERR_TIMEOUT);
}


static int deallocTransDescs(ohciTransDesc *transDescs, int numDescs)
{
	// De-allocate an array of OHCI transfer descriptors

	kernelIoMemory ioMem;

	ioMem.size = (numDescs * sizeof(ohciTransDesc));
	ioMem.physical = transDescs[0].physical;
	ioMem.virtual = (void *) transDescs;

	return (kernelMemoryReleaseIo(&ioMem));
}


static ohciEndpDesc *findIntEndpDesc(ohciData *ohci, int interval)
{
	// Figure out which interrupt queue head to use, given an interval which
	// is a maximum frequency -- so we locate the first one which is less than
	// or equal to the specified interval.

	int queues[6] = { 32, 16, 8, 4, 2, 1 };
	int count;

	for (count = 0; count < 6; count ++)
		if (queues[count] <= interval)
			return (ohci->queueEndpDescs[count]);

	// Should never fall through
	return (NULL);
}


static void unregisterInterrupt(ohciData *ohci, ohciIntrReg *intrReg)
{
	// Remove an interrupt registration and deallocate resources

	kernelLinkedListRemove(&ohci->intrRegs, intrReg);

	// Don't need to deallocate the ED

	if (intrReg->transDesc)
	{
		if (intrReg->transDesc->buffer)
			kernelFree(intrReg->transDesc->buffer);

		deallocTransDescs(intrReg->transDesc, 1);
	}

	kernelFree(intrReg);
}


static int unlinkEndpDescFromQueue(usbController *controller,
	ohciEndpDesc *queueEndpDesc, ohciEndpDesc *linkedEndpDesc)
{
	// Unlink the ED from the queue

	int status = 0;
	ohciData *ohci = controller->data;
	unsigned hcFmNumber = ohci->opRegs->hcFmNumber;
	ohciEndpDesc *tmpEndpDesc = queueEndpDesc;

	// Lock the controller.
	status = kernelLockGet(&controller->lock);
	if (status < 0)
	{
		kernelError(kernel_error, "Can't get controller lock");
		return (status);
	}

	// Disable processing of the queue whilst we change the pointers
	if (queueEndpDesc == ohci->queueEndpDescs[OHCI_ED_CONTROL])
		// Disable control processing
		ohci->opRegs->hcControl &= ~OHCI_HCCTRL_CLE;
	else if (queueEndpDesc == ohci->queueEndpDescs[OHCI_ED_BULK])
		// Disable bulk processing
		ohci->opRegs->hcControl &= ~OHCI_HCCTRL_BLE;
	else
		// Disable interrupt processing
		ohci->opRegs->hcControl &= ~OHCI_HCCTRL_PLE;

	// Wait for the frame number to change
	while (ohci->opRegs->hcFmNumber == hcFmNumber);

	while (tmpEndpDesc)
	{
		if (tmpEndpDesc->next == linkedEndpDesc)
		{
			kernelDebug(debug_usb, "OHCI unlink ED 0x%08x",
				tmpEndpDesc->nextPhysical);

			tmpEndpDesc->next = linkedEndpDesc->next;
			tmpEndpDesc->nextPhysical = linkedEndpDesc->nextPhysical;
			break;
		}

		tmpEndpDesc = tmpEndpDesc->next;
	}

	// Re-enable queue processing
	ohci->opRegs->hcControl |= (OHCI_HCCTRL_BLE | OHCI_HCCTRL_CLE |
		OHCI_HCCTRL_PLE);

	kernelLockRelease(&controller->lock);
	return (status = 0);
}


static int unlinkEndpDescFromAll(usbController *controller,
	ohciEndpDesc *linkedEndpDesc)
{
	// Unlink the ED from any queue to which it's linked

	int status = 0;
	ohciData *ohci = controller->data;
	int count;

	kernelDebug(debug_usb, "OHCI unlink ED from all queues");

	for (count = 0; count < OHCI_NUM_QUEUEDESCS; count ++)
	{
		status = unlinkEndpDescFromQueue(controller,
			ohci->queueEndpDescs[count], linkedEndpDesc);
		if (status < 0)
			break;
	}

	return (status);
}


static void portReset(ohciData *ohci, int portNum)
{
	int count;

	// All of the bits in the port status register are write-to-set or
	// write-to-clear.  Writing zeros to any field has no effect.

	kernelDebug(debug_usb, "OHCI port %d reset", portNum);

	// Set the reset bit
	ohci->opRegs->hcRhPortStatus[portNum] = OHCI_PORTSTAT_PRS;

	if (!(ohci->opRegs->hcRhPortStatus[portNum] & OHCI_PORTSTAT_PRS))
	{
		kernelError(kernel_error, "Couldn't set port reset bit");
		return;
	}

	// Wait up to 50ms for the reset to clear
	for (count = 0; count < 50; count ++)
	{
		if (ohci->opRegs->hcRhPortStatus[portNum] & OHCI_PORTSTAT_PRSC)
		{
			kernelDebug(debug_usb, "OHCI port %d reset took %dms", portNum,
				count);
			break;
		}

		kernelCpuSpinMs(1);
	}

	if (!(ohci->opRegs->hcRhPortStatus[portNum] & OHCI_PORTSTAT_PRSC))
	{
		kernelError(kernel_error, "Port reset did not complete");
		return;
	}

	// Clear the reset change bit
	ohci->opRegs->hcRhPortStatus[portNum] = OHCI_PORTSTAT_PRSC;

	// The port should also show enabled
	if (!(ohci->opRegs->hcRhPortStatus[portNum] & OHCI_PORTSTAT_PES))
	{
		kernelError(kernel_error, "Port did not enable");
		return;
	}

	// Delay another 10ms
	kernelDebug(debug_usb, "OHCI port %d delay after reset", portNum);
	kernelCpuSpinMs(10);
}


static void doDetectDevices(usbHub *hub, int hotplug)
{
	// Detect devices connected to the root hub

	usbController *controller = hub->controller;
	ohciData *ohci = controller->data;
	usbDevSpeed speed = usbspeed_unknown;
	int count;

	//kernelDebug(debug_usb, "OHCI detect devices");

	for (count = 0; count < ohci->numPorts; count ++)
	{
		if (ohci->opRegs->hcRhPortStatus[count] & OHCI_PORTSTAT_CSC)
		{
			kernelDebug(debug_usb, "OHCI port %d connection changed", count);

			debugPortStatus(ohci, count);

			if (ohci->opRegs->hcRhPortStatus[count] & OHCI_PORTSTAT_CCS)
			{
				kernelDebug(debug_usb, "OHCI port %d connected", count);

				// Something connected, so wait 100ms
				kernelDebug(debug_usb, "OHCI port %d delay after port status "
					"change", count);
				kernelCpuSpinMs(100);

				// Reset and enable the port
				portReset(ohci, count);

				// Default speed is full, unless the low speed bit is set
				speed = usbspeed_full;
				if (ohci->opRegs->hcRhPortStatus[count] & OHCI_PORTSTAT_LSDA)
					speed = usbspeed_low;

				if (kernelUsbDevConnect(controller, hub, count, speed,
					hotplug) < 0)
				{
					kernelError(kernel_error, "Error enumerating new device");
				}
			}
			else
			{
				// Tell the USB functions that the device disconnected.  This
				// will call us back to tell us about all affected devices -
				// there might be lots if this was a hub
				kernelUsbDevDisconnect(controller, hub, count);

				kernelDebug(debug_usb, "OHCI port %d is disconnected",
					count);
			}

			// Clear the connection status change bit
			ohci->opRegs->hcRhPortStatus[count] = OHCI_PORTSTAT_CSC;
		}
	}
}


static int takeOwnership(ohciData *ohci)
{
	// Take ownership of the controller

	int status = 0;
	int intsRouted = 0;
	int count;

	kernelDebug(debug_usb, "OHCI take ownership");

	// If interrupts are routed, then SMM is in control.  Otherwise, maybe
	// a BIOS driver is in control.
	intsRouted = (ohci->opRegs->hcControl & OHCI_HCCTRL_IR);

	kernelDebug(debug_usb, "OHCI interrupt routing bit is %sset",
		(intsRouted? "" : "not "));

	if (intsRouted)
	{
		// An SMM driver has control

		// Set the ownership request bit.
		ohci->opRegs->hcCommandStatus |= OHCI_HCCMDSTAT_OCR;

		// Wait for the interrupt routing bit to clear
		for (count = 0; count < 200; count ++)
		{
			if (!(ohci->opRegs->hcControl & OHCI_HCCTRL_IR))
			{
				kernelDebug(debug_usb, "OHCI ownership change took %dms",
					count);
				break;
			}

			kernelCpuSpinMs(1);
		}

		if (ohci->opRegs->hcControl & OHCI_HCCTRL_IR)
		{
			kernelDebugError("SMM driver did not release ownership");
			return (status = ERR_TIMEOUT);
		}
	}
	else
	{
		if ((ohci->opRegs->hcControl & OHCI_HCCTRL_HCFS) ==
			OHCI_HCCTRL_HCFS_RESET)
		{
			// If the state is 'reset', then no driver has control, and
			// we make sure that the minimum reset time has elapsed.

			// Delay 50ms (minimum is 10ms for reset, but 50 is recommended
			// for downstream signaling)
			kernelDebug(debug_usb, "OHCI delay for reset");
			kernelCpuSpinMs(50);
		}
		else
		{
			// If the state is not 'reset', then a BIOS driver has control.

			// If the state is already 'operational', then do nothing.
			// Otherwise, we need to send the 'resume' signal
			if ((ohci->opRegs->hcControl & OHCI_HCCTRL_HCFS) !=
				OHCI_HCCTRL_HCFS_OPERATE)
			{
				ohci->opRegs->hcControl =
					((ohci->opRegs->hcControl & ~OHCI_HCCTRL_HCFS) |
						OHCI_HCCTRL_HCFS_RESUME);

				// Delay 20ms (minimum for resume)
				kernelDebug(debug_usb, "OHCI delay for resume");
				kernelCpuSpinMs(20);
			}
		}
	}

	kernelDebug(debug_usb, "OHCI functional state is %s",
		debugOpState2String(ohci));

	kernelDebug(debug_usb, "OHCI driver has ownership");
	return (status = 0);
}


static void powerOnPorts(ohciData *ohci)
{
	// Power on all the ports, if possible.

	int count;

	kernelDebug(debug_usb, "OHCI power on all ports");

	for (count = 0; count < ohci->numPorts; count ++)
	{
		if (ohci->opRegs->hcRhPortStatus[count] & OHCI_PORTSTAT_PPS)
		{
			kernelDebug(debug_usb, "OHCI port %d already powered", count);
			continue;
		}

		kernelDebug(debug_usb, "OHCI powering on port %d", count);
		ohci->opRegs->hcRhPortStatus[count] = OHCI_PORTSTAT_PPS;
	}

	// Delay for "power on to power good" ms
	kernelDebug(debug_usb, "Delay for 'power on to power good'");
	kernelCpuSpinMs((ohci->opRegs->hcRhDescriptorA &
		OHCI_ROOTDESCA_POTPGT) >> 24);
}


static int setup(usbController *controller)
{
	// Set up data structures for the controller

	int status = 0;
	ohciData *ohci = controller->data;
	kernelIoMemory ioMem;
	ohciEndpDesc *intQueueEndpDesc = NULL;
	int count;

	// Allocate our 'queue' of EDs.  These aren't for specific devices, and
	// we don't queue TDs on them directly.
	for (count = 0; count < OHCI_NUM_QUEUEDESCS; count ++)
	{
		ohci->queueEndpDescs[count] = allocEndpDesc(ohci, NULL, 0);
		if (!ohci->queueEndpDescs[count])
			return (status = ERR_MEMORY);
	}

	// Link the periodic queue EDs together
	for (count = OHCI_ED_INT32; count < OHCI_ED_INT1; count ++)
	{
		ohci->queueEndpDescs[count]->next =
			ohci->queueEndpDescs[count + 1];
		ohci->queueEndpDescs[count]->nextPhysical =
			kernelPageGetPhysical(KERNELPROCID, (void *)
				ohci->queueEndpDescs[count]->next);
	}

	// Allocate memory for the Host Controller Communications Area (HCCA)
	status = kernelMemoryGetIo(sizeof(ohciHcca), 256 /* alignment */, &ioMem);
	if (status < 0)
		return (status);

	// Record the virtual address of the HCCA
	ohci->hcca = ioMem.virtual;

	// Fill the periodic schedule with the appropriate EDs
	for (count = 0; count < OHCI_NUM_FRAMES; count ++)
	{
		if (!(count % 32))
			intQueueEndpDesc = ohci->queueEndpDescs[OHCI_ED_INT32];
		else if (!(count % 16))
			intQueueEndpDesc = ohci->queueEndpDescs[OHCI_ED_INT16];
		else if (!(count % 8))
			intQueueEndpDesc = ohci->queueEndpDescs[OHCI_ED_INT8];
		else if (!(count % 4))
			intQueueEndpDesc = ohci->queueEndpDescs[OHCI_ED_INT4];
		else if (!(count % 2))
			intQueueEndpDesc = ohci->queueEndpDescs[OHCI_ED_INT2];
		else
			// By default, use the 'int 1' queue head which gets run every
			// frame
			intQueueEndpDesc = ohci->queueEndpDescs[OHCI_ED_INT1];

		ohci->hcca->intTable[count] =
			kernelPageGetPhysical(KERNELPROCID, (void *) intQueueEndpDesc);
	}

	// Reset the controller
	status = reset(controller);
	if (status < 0)
		return (status);

	// Set the physical HCCA pointer in the controller
	ohci->opRegs->hcHcca = ioMem.physical;

	// Set the physical control queue head ED in the controller
	ohci->opRegs->hcControlHeadEd =
		kernelPageGetPhysical(KERNELPROCID,
			(void *) ohci->queueEndpDescs[OHCI_ED_CONTROL]);

	// Set the physical bulk queue head ED in the controller
	ohci->opRegs->hcBulkHeadEd =
		kernelPageGetPhysical(KERNELPROCID,
			(void *) ohci->queueEndpDescs[OHCI_ED_BULK]);

	// Enable interrupts
	ohci->opRegs->hcInterruptEnable = (OHCI_HCINT_MIE | OHCI_HCINT_UE |
		OHCI_HCINT_WDH | OHCI_HCINT_SO);

	// Set up the frame interval register, if the values aren't already set.
	if (!(ohci->opRegs->hcFmInterval & OHCI_HCFMINT_FI))
		ohci->opRegs->hcFmInterval |= OHCI_DEFAULT_FRAMEINT;
	if (!(ohci->opRegs->hcFmInterval & OHCI_HCFMINT_FSMPS))
		ohci->opRegs->hcFmInterval |= (OHCI_DEFAULT_MAXPKTSZ << 16);

	// Set up the periodic schedule time.  90% of the frame interval.
	ohci->opRegs->hcPeriodicStart = (((ohci->opRegs->hcFmInterval &
		OHCI_HCFMINT_FI) * 9) / 10);

	// Tell the controller that ports are always powered on
	ohci->opRegs->hcRhDescriptorA &= ~OHCI_ROOTDESCA_PSM;
	ohci->opRegs->hcRhDescriptorA |= OHCI_ROOTDESCA_NPS;

	// Power them on globally
	ohci->opRegs->hcRhStatus = OHCI_RHSTAT_LPSC;

	// Set the controller to the operational state
	ohci->opRegs->hcControl =
		((ohci->opRegs->hcControl & ~OHCI_HCCTRL_HCFS) |
			OHCI_HCCTRL_HCFS_OPERATE);

	// Wait a short time for the controller to indicate it's operational
	for (count = 0; count < 10; count ++)
	{
		if ((ohci->opRegs->hcControl & OHCI_HCCTRL_HCFS) ==
			OHCI_HCCTRL_HCFS_OPERATE)
		{
			kernelDebug(debug_usb, "OHCI controller operational after %dms",
				count);
			break;
		}

		kernelCpuSpinMs(1);
	}

	kernelDebug(debug_usb, "OHCI functional state is %s",
		debugOpState2String(ohci));

	if ((ohci->opRegs->hcControl & OHCI_HCCTRL_HCFS) !=
		OHCI_HCCTRL_HCFS_OPERATE)
	{
		kernelError(kernel_error, "Controller did not move to the operational "
			"state");
		return (status = ERR_NOTINITIALIZED);
	}

	// Turn on the control, bulk, and periodic schedules
	ohci->opRegs->hcControl |= (OHCI_HCCTRL_BLE | OHCI_HCCTRL_CLE |
		OHCI_HCCTRL_PLE);

	// Delay an extra 10ms for resume recovery time, in case we signaled
	// resume during the ownership transfer operation.
	kernelCpuSpinMs(10);

	// Turn on ports power, if necessary
	powerOnPorts(ohci);

	kernelDebug(debug_usb, "OHCI finished setup");
	return (status = 0);
}


/////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////
//
//  Standard USB controller functions
//
/////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////

static int reset(usbController *controller)
{
	// Do a software reset of the controller

	int status = 0;
	ohciData *ohci = NULL;
	unsigned hcFmInterval = 0;
	int count;

	kernelDebug(debug_usb, "OHCI reset");

	// Check params
	if (!controller)
	{
		kernelError(kernel_error, "NULL parameter");
		return (status = ERR_NULLPARAMETER);
	}

	ohci = controller->data;

	// Record the value in this register
	hcFmInterval = ohci->opRegs->hcFmInterval;

	// Initiate the reset.  This register is write-to-set.
	ohci->opRegs->hcCommandStatus = OHCI_HCCMDSTAT_HCR;

	// Wait for the reset to clear.  Maximum of 10ms
	for (count = 0; count < 10; count ++)
	{
		if (!(ohci->opRegs->hcCommandStatus & OHCI_HCCMDSTAT_HCR))
		{
			kernelDebug(debug_usb, "OHCI reset took %dms", count);
			break;
		}

		kernelCpuSpinMs(1);
	}

	if (ohci->opRegs->hcCommandStatus & OHCI_HCCMDSTAT_HCR)
	{
		kernelError(kernel_error, "Controller reset timed out");
		return (status = ERR_TIMEOUT);
	}

	// Restore the hcFmInterval register
	ohci->opRegs->hcFmInterval = hcFmInterval;

	kernelDebug(debug_usb, "OHCI reset successful");
	return (status = 0);
}


static int interrupt(usbController *controller)
{
	// This function gets called when the controller issues an interrupt.

	int status = 0;
	ohciData *ohci = NULL;
	ohciIntrReg *intrReg = NULL;
	kernelLinkedListItem *iter = NULL;
	unsigned bytes = 0;
	volatile unsigned char *dataToggle = NULL;

	// Check params
	if (!controller)
	{
		kernelError(kernel_error, "NULL parameter");
		return (status = ERR_NULLPARAMETER);
	}

	ohci = controller->data;

	// See whether this controller interrupted
	if (!(ohci->opRegs->hcInterruptStatus & ohci->opRegs->hcInterruptEnable))
		return (status = ERR_NODATA);

	//kernelDebug(debug_usb, "OHCI interrupt");

	if (ohci->opRegs->hcInterruptStatus & OHCI_HCINT_UE)
	{
		kernelError(kernel_error, "USB unrecoverable error, controller %d",
			controller->num);

		debugOpRegs(ohci);
	}

	if (ohci->opRegs->hcInterruptStatus & OHCI_HCINT_WDH)
	{
		// We need to check whether the done queue head points to one
		// of our interrupt registrations
		intrReg = kernelLinkedListIterStart(&ohci->intrRegs, &iter);
		while (intrReg)
		{
			if ((ohci->hcca->doneHead & ~1) != intrReg->transDesc->physical)
			{
				intrReg = kernelLinkedListIterNext(&ohci->intrRegs, &iter);
				continue;
			}

			kernelDebug(debug_usb, "OHCI device %d, endpoint 0x%02x "
				"interrupted", intrReg->usbDev->address, intrReg->endpoint);

			// Remove it from the done queue
			removeFromDoneQueue(controller, intrReg->transDesc, 1);

			// Unlink from the ED
			unlinkTransaction(controller, intrReg->endpDesc,
				intrReg->transDesc, 1);

			// Was there any error?
			if (intrReg->transDesc->flags & OHCI_TDFLAGS_CONDCODE)
			{
				// Remove the interrupt registration (can't deallocate in an
				// interrupt handler, though)
				kernelError(kernel_error, "USB interrupt device error - not "
					"rescheduling");
				kernelLinkedListRemove(&ohci->intrRegs, intrReg);
			}
			else
			{
				// If there's data and a callback function, do the callback
				if (intrReg->callback)
				{
					// Calculate the number of bytes transferred
					bytes = intrReg->maxLen;
					if (intrReg->transDesc->currBuffPtr)
					{
						bytes -= ((intrReg->transDesc->bufferEnd -
							intrReg->transDesc->currBuffPtr) + 1);
					}

					kernelDebug(debug_usb, "OHCI %u bytes", bytes);

					if (bytes)
					{
						intrReg->callback(intrReg->usbDev, intrReg->interface,
							intrReg->transDesc->buffer, bytes);
					}
				}

				dataToggle = kernelUsbGetEndpointDataToggle(intrReg->usbDev,
					intrReg->endpoint);

				if (dataToggle)
				{
					*dataToggle ^= 1;

					// Reset the TD
					intrReg->transDesc->flags |= OHCI_TDFLAGS_CONDCODE;
					intrReg->transDesc->flags &= ~OHCI_TDFLAGS_DATATOGGLE;
					intrReg->transDesc->flags |= (((2 | *dataToggle) << 24) &
						OHCI_TDFLAGS_DATATOGGLE);

					intrReg->transDesc->currBuffPtr = intrReg->bufferPhysical;

					// Re-link to the ED
					linkTransaction(controller, intrReg->endpDesc,
						intrReg->transDesc);
				}
			}

			// Re-start the iteration
			intrReg = kernelLinkedListIterStart(&ohci->intrRegs, &iter);
		}
	}

	if (ohci->opRegs->hcInterruptStatus & OHCI_HCINT_SO)
	{
		kernelError(kernel_error, "USB scheduling overrun error, controller "
			"%d", controller->num);

		debugOpRegs(ohci);
	}

	// Clear interrupt status bits, but not the 'write back done queue' one
	ohci->opRegs->hcInterruptStatus =
		(ohci->opRegs->hcInterruptStatus & ~OHCI_HCINT_WDH);

	return (status = 0);
}


static int queue(usbController *controller, usbDevice *usbDev,
	usbTransaction *trans, int numTrans)
{
	// This function contains the intelligence necessary to initiate a
	// transaction (all phases)

	int status = 0;
	ohciData *ohci = NULL;
	unsigned timeout = 0;
	ohciEndpDesc *endpDesc = NULL;
	ohciEndpDesc *queueEndpDesc = NULL;
	int packetSize = 0;
	int numDataDescs = 0;
	int numDescs = 0;
	void *buffPtr = NULL;
	unsigned bytesToTransfer = 0;
	unsigned doBytes = 0;
	ohciTransDesc *transDescs = NULL;
	volatile unsigned char *dataToggle = NULL;
	ohciTransDesc *setupDesc = NULL;
	ohciTransDesc *dataDescs = NULL;
	ohciTransDesc *statusDesc = NULL;
	int transCount, descCount;

	// Check params
	if (!controller || !usbDev || !trans)
	{
		kernelError(kernel_error, "NULL parameter");
		return (status = ERR_NULLPARAMETER);
	}

	kernelDebug(debug_usb, "OHCI queue %d transactions for device %d",
		numTrans, usbDev->address);

	ohci = controller->data;

	for (transCount = 0; transCount < numTrans; transCount ++)
	{
		timeout = trans[transCount].timeout;
		if (!timeout)
			timeout = USB_STD_TIMEOUT_MS;

		// Try to find an existing ED for this transaction's endpoint
		endpDesc = findEndpDesc(ohci, usbDev, trans[transCount].endpoint);

		// Found it?
		if (endpDesc)
		{
			kernelDebug(debug_usb, "OHCI found existing ED");
		}
		else
		{
			// We don't yet have an ED for this endpoint.  Try to allocate one.
			endpDesc = allocEndpDesc(ohci, usbDev, trans[transCount].endpoint);
			if (!endpDesc)
			{
				status = ERR_NOSUCHENTRY;
				goto out;
			}

			// Add the ED to the appropriate queue
			if (trans[transCount].type == usbxfer_control)
				// Control queue
				queueEndpDesc = ohci->queueEndpDescs[OHCI_ED_CONTROL];
			else
				// Bulk queue
				queueEndpDesc = ohci->queueEndpDescs[OHCI_ED_BULK];

			status = linkEndpDescToQueue(controller, queueEndpDesc, endpDesc);
			if (status < 0)
				goto out;
		}

		// Make sure the flags are up to date.
		updateEndpDescFlags(endpDesc);

		// We can get the maximum packet size from the ED flags (it will have
		// been updated with the current device info upon retrieval, above).
		packetSize = ((endpDesc->flags & OHCI_EDFLAGS_MAXPACKET) >> 16);

		// Figure out how many TDs we're going to need for this transaction
		numDataDescs = 0;
		numDescs = 0;

		// Setup/status descriptors?
		if (trans[transCount].type == usbxfer_control)
			// At least one each for setup and status
			numDescs += 2;

		// Data descriptors?
		if (trans[transCount].length)
		{
			buffPtr = trans[transCount].buffer;
			bytesToTransfer = trans[transCount].length;

			while (bytesToTransfer)
			{
				doBytes = min(bytesToTransfer, OHCI_MAX_TD_BUFFERSIZE);

				// Don't let packets cross TD boundaries
				if ((doBytes < bytesToTransfer) && (doBytes % packetSize))
					doBytes -= (doBytes % packetSize);

				numDataDescs += 1;
				bytesToTransfer -= doBytes;
				buffPtr += doBytes;
			}

			kernelDebug(debug_usb, "OHCI data payload of %u requires %d "
				"descriptors", trans[transCount].length, numDataDescs);

			numDescs += numDataDescs;
		}

		kernelDebug(debug_usb, "OHCI transaction requires %d descriptors",
			numDescs);

		// Allocate the TDs we need for this transaction
		transDescs = allocTransDescs(numDescs);
		if (!transDescs)
		{
			kernelError(kernel_error, "Couldn't get transfer descriptors for "
				"transaction");
			status = ERR_NOFREE;
			goto out;
		}

		// Get the data toggle for the endpoint
		dataToggle = kernelUsbGetEndpointDataToggle(usbDev,
			trans[transCount].endpoint);
		if (!dataToggle)
		{
			kernelError(kernel_error, "No data toggle for endpoint 0x%02x",
				trans[transCount].endpoint);
			status = ERR_NOSUCHFUNCTION;
			goto out;
		}

		setupDesc = NULL;
		if (trans[transCount].type == usbxfer_control)
		{
			// Begin setting up the device request

			// Get the TD for the setup phase
			setupDesc = &transDescs[0];

			setupDesc->buffer = NULL; // Allocate it for us
			setupDesc->buffSize = sizeof(usbDeviceRequest);

			status = setTransDescBuffer(setupDesc);
			if (status < 0)
				goto out;

			status = kernelUsbSetupDeviceRequest(&trans[transCount],
				setupDesc->buffer);
			if (status < 0)
				goto out;

			// Data toggle is always 0 for the setup transfer
			*dataToggle = 0;

			// Set up the rest of the TD
			setupDesc->flags = (OHCI_TDFLAGS_CONDCODE |
				((2 << 24) & OHCI_TDFLAGS_DATATOGGLE) | OHCI_TDFLAGS_DELAYINT |
				OHCI_TDFLAGS_ROUNDING);

			// Data toggle
			*dataToggle ^= 1;
		}

		// If there is a data phase, set up the TD(s) for the data phase
		if (trans[transCount].length)
		{
			buffPtr = trans[transCount].buffer;
			bytesToTransfer = trans[transCount].length;

			dataDescs = &transDescs[0];
			if (setupDesc)
				dataDescs = &transDescs[1];

			for (descCount = 0; descCount < numDataDescs; descCount ++)
			{
				doBytes = min(bytesToTransfer, OHCI_MAX_TD_BUFFERSIZE);

				// Don't let packets cross TD boundaries
				if ((doBytes < bytesToTransfer) && (doBytes % packetSize))
					doBytes -= (doBytes % packetSize);

				kernelDebug(debug_usb, "OHCI bytesToTransfer=%u, doBytes=%u",
					bytesToTransfer, doBytes);

				// Set the TD's buffer pointer to the relevent portion of
				// the transaction buffer.
				dataDescs[descCount].buffer = buffPtr;
				dataDescs[descCount].buffSize = doBytes;

				status = setTransDescBuffer(&dataDescs[descCount]);
				if (status < 0)
					goto out;

				// Set up the rest of the TD
				dataDescs[descCount].flags = (OHCI_TDFLAGS_CONDCODE |
					(((2 | *dataToggle) << 24) & OHCI_TDFLAGS_DATATOGGLE) |
					OHCI_TDFLAGS_DELAYINT | OHCI_TDFLAGS_ROUNDING);

				if (trans[transCount].pid == USB_PID_IN)
				{
					dataDescs[descCount].flags |=
						((2 << 19) & OHCI_TDFLAGS_DIRPID);
				}
				else
				{
					dataDescs[descCount].flags |=
						((1 << 19) & OHCI_TDFLAGS_DIRPID);
				}

				// If the TD generated an odd number of packets, toggle the
				// data toggle.
				if (((doBytes + (packetSize - 1)) / packetSize) % 2)
					*dataToggle ^= 1;

				buffPtr += doBytes;
				bytesToTransfer -= doBytes;
			}
		}

		if (trans[transCount].type == usbxfer_control)
		{
			// Setup the TD for the status phase

			statusDesc = &transDescs[numDescs - 1];

			// Data toggle is always 1 for the status transfer
			*dataToggle = 1;

			// Set up the rest of the TD
			statusDesc->flags = (OHCI_TDFLAGS_CONDCODE |
				OHCI_TDFLAGS_DATATOGGLE | OHCI_TDFLAGS_DELAYINT |
				OHCI_TDFLAGS_ROUNDING);

			if (trans[transCount].pid == USB_PID_IN)
				statusDesc->flags |= ((1 << 19) & OHCI_TDFLAGS_DIRPID);
			else
				statusDesc->flags |= ((2 << 19) & OHCI_TDFLAGS_DIRPID);
		}

		// Get the controller to write the transaction to the done queue
		// when finished, with a delay limit of 1 frame.
		transDescs[numDescs - 1].flags &= ~OHCI_TDFLAGS_DELAYINT;
		transDescs[numDescs - 1].flags |= ((1 << 21) & OHCI_TDFLAGS_DELAYINT);

		// Link the transaction to the queue head
		status = linkTransaction(controller, endpDesc, transDescs);
		if (status < 0)
			break;

		// Tell the controller that we put something into the schedule
		if (trans[transCount].type == usbxfer_control)
			ohci->opRegs->hcCommandStatus = OHCI_HCCMDSTAT_CLF;
		else
			ohci->opRegs->hcCommandStatus = OHCI_HCCMDSTAT_BLF;

		// Wait for the transaction to complete
		status = waitTransactionComplete(ohci, &transDescs[numDescs - 1],
			timeout);

		if (setupDesc)
		{
			if (setupDesc->buffer)
				kernelFree(setupDesc->buffer);
			setupDesc = NULL;
		}

		// Remove it from the done queue
		removeFromDoneQueue(controller, transDescs, numDescs);

		// Unlink the transaction from the queue head
		unlinkTransaction(controller, endpDesc, transDescs, numDescs);

		if (status < 0)
			break;

		// Check for errors
		for (descCount = 0; descCount < numDescs; descCount ++)
		{
			if (transDescs[descCount].flags & OHCI_TDFLAGS_CONDCODE)
			{
				status = ERR_IO;
				break;
			}
		}

		if (transDescs)
		{
			deallocTransDescs(transDescs, numDescs);
			transDescs = NULL;
		}

		if (status < 0)
			break;

		// This is a bit crude
		trans[transCount].bytes = trans[transCount].length;
	}

	status = 0;

out:
	if (setupDesc && setupDesc->buffer)
		kernelFree(setupDesc->buffer);

	if (transDescs)
		deallocTransDescs(transDescs, numDescs);

	return (status);
}


static int schedInterrupt(usbController *controller, usbDevice *usbDev,
	int interface, unsigned char endpoint, int interval, unsigned maxLen,
	void (*callback)(usbDevice *, int, void *, unsigned))
{
	// This function is used to schedule an interrupt.

	int status = 0;
	ohciData *ohci = NULL;
	ohciIntrReg *intrReg = NULL;
	ohciEndpDesc *queueEndpDesc = NULL;
	volatile unsigned char *dataToggle = NULL;

	// Check params
	if (!controller || !usbDev || !callback)
	{
		kernelError(kernel_error, "NULL parameter");
		return (status = ERR_NULLPARAMETER);
	}

	kernelDebug(debug_usb, "OHCI schedule interrupt for device %d endpoint "
		"0x%02x interval %d len %u", usbDev->address, endpoint, interval,
		maxLen);

	ohci = controller->data;

	// Get memory to hold info about the interrupt
	intrReg = kernelMalloc(sizeof(ohciIntrReg));
	if (!intrReg)
	{
		status = ERR_MEMORY;
		goto out;
	}

	intrReg->usbDev = usbDev;
	intrReg->interface = interface;
	intrReg->endpoint = endpoint;
	intrReg->maxLen = maxLen;
	intrReg->interval = interval;
	intrReg->callback = callback;

	// Try to find an existing ED for this endpoint
	intrReg->endpDesc = findEndpDesc(ohci, usbDev, endpoint);

	// Found it?
	if (intrReg->endpDesc)
	{
		kernelDebug(debug_usb, "OHCI found existing ED");
	}
	else
	{
		// We don't yet have an ED for this endpoint.  Try to allocate one.
		intrReg->endpDesc = allocEndpDesc(ohci, usbDev, endpoint);
		if (!intrReg->endpDesc)
		{
			status = ERR_NOSUCHENTRY;
			goto out;
		}

		// Add the ED to the appropriate queue
		queueEndpDesc = findIntEndpDesc(ohci, interval);
		if (!queueEndpDesc)
		{
			status = ERR_NOSUCHENTRY;
			goto out;
		}

		status = linkEndpDescToQueue(controller, queueEndpDesc,
			intrReg->endpDesc);
		if (status < 0)
			goto out;
	}

	// Make sure the flags are up to date.
	updateEndpDescFlags(intrReg->endpDesc);

	// Get the data toggle for the endpoint
	dataToggle = kernelUsbGetEndpointDataToggle(usbDev, endpoint);
	if (!dataToggle)
	{
		kernelError(kernel_error, "No data toggle for endpoint 0x%02x",
			endpoint);
		status = ERR_NOSUCHFUNCTION;
		goto out;
	}

	// Get a TD for it
	intrReg->transDesc = allocTransDescs(1);
	if (!intrReg->transDesc)
	{
		kernelError(kernel_error, "Couldn't get transfer descriptor for "
			"interrupt");
		status = ERR_NOFREE;
		goto out;
	}

	intrReg->transDesc->buffer = NULL; // Allocate it for us
	intrReg->transDesc->buffSize = maxLen;

	status = setTransDescBuffer(intrReg->transDesc);
	if (status < 0)
		goto out;

	intrReg->bufferPhysical = kernelPageGetPhysical(KERNELPROCID,
		intrReg->transDesc->buffer);

	// Set up the rest of the TD
	intrReg->transDesc->flags = (OHCI_TDFLAGS_CONDCODE |
		(((2 | *dataToggle) << 24) & OHCI_TDFLAGS_DATATOGGLE) |
		((1 << 21) & OHCI_TDFLAGS_DELAYINT) | OHCI_TDFLAGS_ROUNDING);

	if (endpoint & 0x80)
		intrReg->transDesc->flags |=
			((2 << 19) & OHCI_TDFLAGS_DIRPID); // in
	else
		intrReg->transDesc->flags |=
			((1 << 19) & OHCI_TDFLAGS_DIRPID); // out

	// Link the TD to the queue head
	status = linkTransaction(controller, intrReg->endpDesc,
		intrReg->transDesc);
	if (status < 0)
		goto out;

	// Add the interrupt registration to the controller's list.
	status = kernelLinkedListAdd(&ohci->intrRegs, intrReg);
	if (status < 0)
		goto out;

	kernelDebug(debug_usb, "OHCI succcessfully scheduled interrupt");

out:
	if ((status < 0) && intrReg)
		unregisterInterrupt(ohci, intrReg);

	return (status);
}


static int deviceRemoved(usbController *controller, usbDevice *usbDev)
{
	int status = 0;
	ohciData *ohci = NULL;
	ohciIntrReg *intrReg = NULL;
	kernelLinkedListItem *iter = NULL;
	kernelLinkedList *usedList = NULL;
	ohciEndpDesc *endpDesc = NULL;

	// Check params
	if (!controller || !usbDev)
	{
		kernelError(kernel_error, "NULL parameter");
		return (status = ERR_NULLPARAMETER);
	}

	kernelDebug(debug_usb, "OHCI device %d removed", usbDev->address);

	ohci = controller->data;

	// Remove any interrupt registrations for the device
	intrReg = kernelLinkedListIterStart(&ohci->intrRegs, &iter);
	while (intrReg)
	{
		if (intrReg->usbDev != usbDev)
		{
			intrReg = kernelLinkedListIterNext(&ohci->intrRegs, &iter);
			continue;
		}

		unregisterInterrupt(ohci, intrReg);

		// Restart the iteration
		intrReg = kernelLinkedListIterStart(&ohci->intrRegs, &iter);
	}

	// Find, unlink, and deallocate any EDs that we have for this device

	usedList = &ohci->usedEndpDescs;

	endpDesc = kernelLinkedListIterStart(usedList, &iter);
	while (endpDesc)
	{
		if (endpDesc->usbDev != usbDev)
		{
			endpDesc = kernelLinkedListIterNext(usedList, &iter);
			continue;
		}

		// Found one.  Remove it from any/all queues, and release it.
		unlinkEndpDescFromAll(controller, endpDesc);
		releaseEndpDesc(ohci, endpDesc);

		// Restart the iteration
		endpDesc = kernelLinkedListIterStart(usedList, &iter);
	}

	return (status = 0);
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
	// This function gets called once at startup to detect 'cold-plugged'
	// devices.

	kernelDebug(debug_usb, "OHCI initial device detection, hotplug=%d",
		hotplug);

	// Check params
	if (!hub)
	{
		kernelError(kernel_error, "NULL parameter");
		return;
	}

	debugRootHub(hub->controller->data);

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
//  Below here, the functions are exported for external use
//
/////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////

kernelDevice *kernelUsbOhciDetect(kernelBusTarget *busTarget,
	kernelDriver *driver)
{
	// This routine is used to detect OHCI USB controllers, as well as
	// registering it with the higher-level interfaces.

	int status = 0;
	pciDeviceInfo pciDevInfo;
	usbController *controller = NULL;
	ohciData *ohci = NULL;
	unsigned physMemSpace;
	unsigned memSpaceSize;
	kernelDevice *dev = NULL;
	char value[32];

	// Get the PCI device header
	status = kernelBusGetTargetInfo(busTarget, &pciDevInfo);
	if (status < 0)
		goto err_out;

	// Don't care about the 'multi-function' bit in the header type
	if (pciDevInfo.device.headerType & PCI_HEADERTYPE_MULTIFUNC)
		pciDevInfo.device.headerType &= ~PCI_HEADERTYPE_MULTIFUNC;

	// Make sure it's a non-bridge header
	if (pciDevInfo.device.headerType != PCI_HEADERTYPE_NORMAL)
	{
		kernelDebug(debug_usb, "OHCI headertype not 'normal' (%d)",
			pciDevInfo.device.headerType);
		goto err_out;
	}

	// Make sure it's an OHCI controller (programming interface is 0x10 in
	// the PCI header)
	if (pciDevInfo.device.progIF != OHCI_PCI_PROGIF)
		goto err_out;

	// After this point, we believe we have a supported device.

	// Allocate memory for the controller
	controller = kernelMalloc(sizeof(usbController));
	if (!controller)
		goto err_out;

	// Set the controller type
	controller->type = usb_ohci;

	// Get the interrupt number.
	controller->interruptNum = pciDevInfo.device.nonBridge.interruptLine;

	// Allocate our private driver data
	controller->data = kernelMalloc(sizeof(ohciData));
	if (!controller->data)
		goto err_out;

	ohci = controller->data;

	// Get the memory range address
	physMemSpace = (pciDevInfo.device.nonBridge.baseAddress[0] & 0xFFFFF000);

	kernelDebug(debug_usb, "OHCI physMemSpace=0x%08x", physMemSpace);

	// Determine the memory space size.  Write all 1s to the register.
	kernelBusWriteRegister(busTarget, PCI_CONFREG_BASEADDRESS0_32, 32,
		0xFFFFFFFF);

	memSpaceSize = (~(kernelBusReadRegister(busTarget,
		PCI_CONFREG_BASEADDRESS0_32, 32) & ~0xF) + 1);

	kernelDebug(debug_usb, "OHCI memSpaceSize=0x%08x", memSpaceSize);

	// Restore the register we clobbered.
	kernelBusWriteRegister(busTarget, PCI_CONFREG_BASEADDRESS0_32, 32,
		pciDevInfo.device.nonBridge.baseAddress[0]);

	// Map the physical memory address of the controller's registers into
	// our virtual address space.

	// Map the physical memory space pointed to by the decoder.
	status = kernelPageMapToFree(KERNELPROCID, physMemSpace,
		(void **) &ohci->opRegs, memSpaceSize);
	if (status < 0)
	{
		kernelDebugError("Error mapping memory");
		goto err_out;
	}

	// Make it non-cacheable, since this memory represents memory-mapped
	// hardware registers.
	status = kernelPageSetAttrs(KERNELPROCID, 1 /* set */,
		PAGEFLAG_CACHEDISABLE, (void *) ohci->opRegs, memSpaceSize);
	if (status < 0)
	{
		kernelDebugError("Error setting page attrs");
		goto err_out;
	}

	// Enable memory mapping access
	if (!(pciDevInfo.device.commandReg & PCI_COMMAND_MEMORYENABLE))
	{
		kernelBusDeviceEnable(busTarget, PCI_COMMAND_MEMORYENABLE);

		// Re-read target info
		kernelBusGetTargetInfo(busTarget, &pciDevInfo);

		if (!(pciDevInfo.device.commandReg & PCI_COMMAND_MEMORYENABLE))
		{
			kernelDebugError("Couldn't enable memory access");
			goto err_out;
		}

		kernelDebug(debug_usb, "OHCI memory access enabled in PCI");
	}
	else
	{
		kernelDebug(debug_usb, "OHCI memory access already enabled");
	}

	// The USB version number.
	controller->usbVersion = ohci->opRegs->hcRevision;

	kernelLog("USB: OHCI controller USB %d.%d interrupt %d",
		((controller->usbVersion & 0xF0) >> 4),
		(controller->usbVersion & 0xF), controller->interruptNum);

	ohci->numPorts = (ohci->opRegs->hcRhDescriptorA & OHCI_ROOTDESCA_NDP);
	kernelDebug(debug_usb, "OHCI number of ports=%d", ohci->numPorts);

	// Take ownership of the controller.
	status = takeOwnership(ohci);
	if (status < 0)
		goto err_out;

	// Set up the registers and data structures, and make it operational.
	status = setup(controller);
	if (status < 0)
		goto err_out;

	debugOpRegs(ohci);

	controller->hub.controller = controller;
	controller->hub.detectDevices = &detectDevices;
	controller->hub.threadCall = &threadCall;

	// Set controller function calls
	controller->reset = &reset;
	controller->interrupt = &interrupt;
	controller->queue = &queue;
	controller->schedInterrupt = &schedInterrupt;
	controller->deviceRemoved = &deviceRemoved;

	// Allocate memory for the kernel device
	dev = kernelMalloc(sizeof(kernelDevice));
	if (!dev)
		goto err_out;

	// Create the USB kernel device
	dev->device.class = kernelDeviceGetClass(DEVICECLASS_BUS);
	dev->device.subClass = kernelDeviceGetClass(DEVICESUBCLASS_BUS_USB);
	dev->driver = driver;
	dev->data = (void *) controller;

	// Initialize the variable list for attributes of the controller
	status = kernelVariableListCreate(&dev->device.attrs);
	if (status >= 0)
	{
		kernelVariableListSet(&dev->device.attrs, "controller.type", "OHCI");
		snprintf(value, 32, "%d", ohci->numPorts);
		kernelVariableListSet(&dev->device.attrs, "controller.numPorts",
			value);
	}

	// Claim the controller device in the list of PCI targets.
	kernelBusDeviceClaim(busTarget, driver);

	// Add the kernel device
	status = kernelDeviceAdd(busTarget->bus->dev, dev);
	if (status < 0)
		goto err_out;

	return (dev);

err_out:
	if (dev)
		kernelFree(dev);
	if (controller)
		kernelFree((void *) controller);

  return (dev = NULL);
}

