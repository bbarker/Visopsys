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
//  kernelUsbEhciDriver.c
//

#include "kernelUsbEhciDriver.h"
#include "kernelCpu.h"
#include "kernelDebug.h"
#include "kernelError.h"
#include "kernelLog.h"
#include "kernelMalloc.h"
#include "kernelMemory.h"
#include "kernelMultitasker.h"
#include "kernelPage.h"
#include "kernelParameters.h"
#include "kernelPciDriver.h"
#include "kernelVariableList.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int reset(usbController *);

#ifdef DEBUG
static inline void debugCapRegs(usbEhciData *ehci)
{
	kernelDebug(debug_usb, "EHCI capability registers:\n"
		"  capslen=0x%02x\n"
		"  hciver=0x%04x\n"
		"  hcsparams=0x%08x\n"
		"  hccparams=0x%08x\n"
		"  hcsp_portroute=0x%llx (%svalid)",
		ehci->capRegs->capslen, ehci->capRegs->hciver,
		ehci->capRegs->hcsparams, ehci->capRegs->hccparams,
		ehci->capRegs->hcsp_portroute,
		((ehci->capRegs->hcsparams & EHCI_HCSP_PORTRTERULES)? "" : "in"));
}

static inline void debugHcsParams(usbEhciData *ehci)
{
	kernelDebug(debug_usb, "EHCI HCSParams register:\n"
		"  debug port=%d\n"
		"  port indicators=%d\n"
		"  num companion controllers=%d\n"
		"  ports per companion=%d\n"
		"  port routing rules=%d\n"
		"  port power control=%d\n"
		"  num ports=%d",
		((ehci->capRegs->hcsparams & EHCI_HCSP_DEBUGPORT) >> 20),
		((ehci->capRegs->hcsparams & EHCI_HCSP_PORTINICATORS) >> 16),
		((ehci->capRegs->hcsparams & EHCI_HCSP_NUMCOMPANIONS) >> 12),
		((ehci->capRegs->hcsparams & EHCI_HCSP_PORTSPERCOMP) >> 8),
		((ehci->capRegs->hcsparams & EHCI_HCSP_PORTRTERULES) >> 7),
		((ehci->capRegs->hcsparams & EHCI_HCSP_PORTPOWERCTRL) >> 4),
		(ehci->capRegs->hcsparams & EHCI_HCSP_NUMPORTS));
}

static inline void debugHccParams(usbEhciData *ehci)
{
	kernelDebug(debug_usb, "EHCI HCCParams register:\n"
		"  extended caps ptr=0x%02x\n"
		"  isoc schedule threshold=0x%x\n"
		"  async schedule park=%d\n"
		"  programmable frame list=%d\n"
		"  64-bit addressing=%d",
		((ehci->capRegs->hccparams & EHCI_HCCP_EXTCAPPTR) >> 8),
		((ehci->capRegs->hccparams & EHCI_HCCP_ISOCSCHDTHRES) >> 4),
		((ehci->capRegs->hccparams & EHCI_HCCP_ASYNCSCHDPARK) >> 2),
		((ehci->capRegs->hccparams & EHCI_HCCP_PROGFRAMELIST) >> 1),
		(ehci->capRegs->hccparams & EHCI_HCCP_ADDR64));
}

static void debugOpRegs(usbEhciData *ehci)
{
	int numPorts = (ehci->capRegs->hcsparams & EHCI_HCSP_NUMPORTS);
	char portsStatCtl[1024];
	int count;

	// Read the port status/control registers
	portsStatCtl[0] = '\0';
	for (count = 0; count < numPorts; count ++)
	{
		sprintf((portsStatCtl + strlen(portsStatCtl)), "\n  portsc%d=0x%08x",
			(count + 1), ehci->opRegs->portsc[count]);
	}

	kernelDebug(debug_usb, "EHCI operational registers:\n"
		"  cmd=0x%08x\n"
		"  stat=0x%08x\n"
		"  intr=0x%08x\n"
		"  frindex=0x%08x\n"
		"  ctrldsseg=0x%08x\n"
		"  perlstbase=0x%08x\n"
		"  asynclstaddr=0x%08x\n"
		"  configflag=0x%08x%s",
		ehci->opRegs->cmd, ehci->opRegs->stat, ehci->opRegs->intr,
		ehci->opRegs->frindex, ehci->opRegs->ctrldsseg,
		ehci->opRegs->perlstbase, ehci->opRegs->asynclstaddr,
		ehci->opRegs->configflag, portsStatCtl);
}

static inline void debugPortStatus(usbController *controller, int portNum)
{
	usbEhciData *ehci = controller->data;

	kernelDebug(debug_usb, "EHCI controller %d, port %d: 0x%08x",
		controller->num, portNum, ehci->opRegs->portsc[portNum]);
}

static inline void debugQtd(ehciQtd *qtd)
{
	kernelDebug(debug_usb, "EHCI qTD (0x%08x):\n"
		"  nextQtd=0x%08x\n"
		"  altNextQtd=0x%08x\n"
		"  token=0x%08x\n"
		"    dataToggle=%d\n"
		"    totalBytes=%d\n"
		"    interruptOnComplete=%d\n"
		"    currentPage=%d\n"
		"    errorCounter=%d\n"
		"    pidCode=%d\n"
		"    status=0x%02x\n"
		"  buffer0=0x%08x\n"
		"  buffer1=0x%08x\n"
		"  buffer2=0x%08x\n"
		"  buffer3=0x%08x\n"
		"  buffer4=0x%08x",
		kernelPageGetPhysical((((unsigned) qtd < KERNEL_VIRTUAL_ADDRESS)?
			kernelCurrentProcess->processId :
				KERNELPROCID), (void *) qtd),
		qtd->nextQtd, qtd->altNextQtd, qtd->token,
		((qtd->token & EHCI_QTDTOKEN_DATATOGG) >> 31),
		((qtd->token & EHCI_QTDTOKEN_TOTBYTES) >> 16),
		((qtd->token & EHCI_QTDTOKEN_IOC) >> 15),
		((qtd->token & EHCI_QTDTOKEN_CURRPAGE) >> 12),
		((qtd->token & EHCI_QTDTOKEN_ERRCOUNT) >> 10),
		((qtd->token & EHCI_QTDTOKEN_PID) >> 8),
		(qtd->token & EHCI_QTDTOKEN_STATMASK),
		qtd->buffPage[0], qtd->buffPage[1], qtd->buffPage[2],
		qtd->buffPage[3], qtd->buffPage[4]);
}

static inline void debugQueueHead(ehciQueueHead *queueHead)
{
	kernelDebug(debug_usb, "EHCI queue head (0x%08x):\n"
		"  horizLink=0x%08x\n"
		"  endpointChars=0x%08x\n"
		"    nakCountReload=%d\n"
		"    controlEndpoint=%d\n"
		"    maxPacketLen=%d\n"
		"    reclListHead=%d\n"
		"    dataToggleCntl=%d\n"
		"    endpointSpeed=%d\n"
		"    endpointNum=0x%02x\n"
		"    inactivateOnNext=%d\n"
		"    deviceAddress=%d\n"
		"  endpointCaps=0x%08x\n"
		"    hiBandMult=%d\n"
		"    portNumber=%d\n"
		"    hubAddress=%d\n"
		"    splitCompMask=0x%02x\n"
		"    intSchedMask=0x%02x\n"
		"  currentQtd=0x%08x",
		kernelPageGetPhysical((((unsigned) queueHead <
			KERNEL_VIRTUAL_ADDRESS)?
				kernelCurrentProcess->processId :
					KERNELPROCID), (void *) queueHead),
		queueHead->horizLink, queueHead->endpointChars,
		((queueHead->endpointChars & EHCI_QHDW1_NAKCNTRELOAD) >> 28),
		((queueHead->endpointChars & EHCI_QHDW1_CTRLENDPOINT) >> 27),
		((queueHead->endpointChars & EHCI_QHDW1_MAXPACKETLEN) >> 16),
		((queueHead->endpointChars & EHCI_QHDW1_RECLLISTHEAD) >> 15),
		((queueHead->endpointChars & EHCI_QHDW1_DATATOGGCTRL) >> 14),
		((queueHead->endpointChars & EHCI_QHDW1_ENDPTSPEED) >> 12),
		((queueHead->endpointChars & EHCI_QHDW1_ENDPOINT) >> 8),
		((queueHead->endpointChars & EHCI_QHDW1_INACTONNEXT) >> 7),
		(queueHead->endpointChars & EHCI_QHDW1_DEVADDRESS),
		queueHead->endpointCaps,
		((queueHead->endpointCaps & EHCI_QHDW2_HISPEEDMULT) >> 30),
		((queueHead->endpointCaps & EHCI_QHDW2_PORTNUMBER) >> 23),
		((queueHead->endpointCaps & EHCI_QHDW2_HUBADDRESS) >> 16),
		((queueHead->endpointCaps & EHCI_QHDW2_SPLTCOMPMASK) >> 8),
		(queueHead->endpointCaps & EHCI_QHDW2_INTSCHEDMASK),
		queueHead->currentQtd);
	debugQtd(&queueHead->overlay);
}

static void debugTransError(ehciQtd *qtd)
{
	char *errorText = NULL;
	char *transString = NULL;

	errorText = kernelMalloc(MAXSTRINGLENGTH);
	if (errorText)
	{
		switch (qtd->token & EHCI_QTDTOKEN_PID)
		{
			case EHCI_QTDTOKEN_PID_SETUP:
				transString = "SETUP";
				break;
			case EHCI_QTDTOKEN_PID_IN:
				transString = "IN";
				break;
			case EHCI_QTDTOKEN_PID_OUT:
				transString = "OUT";
				break;
		}
		sprintf(errorText, "Trans desc %s: ", transString);

		if (!(qtd->token & EHCI_QTDTOKEN_ERROR))
			strcat(errorText, "no error, ");

		if (qtd->token & EHCI_QTDTOKEN_ERRHALT)
			strcat(errorText, "halted, ");
		if (qtd->token & EHCI_QTDTOKEN_ERRDATBUF)
			strcat(errorText, "data buffer error, ");
		if (qtd->token & EHCI_QTDTOKEN_ERRBABBLE)
			strcat(errorText, "babble, ");
		if (qtd->token & EHCI_QTDTOKEN_ERRXACT)
			strcat(errorText, "transaction error, ");
		if (qtd->token & EHCI_QTDTOKEN_ERRMISSMF)
			strcat(errorText, "missed micro-frame, ");

		if (qtd->token & EHCI_QTDTOKEN_ACTIVE)
			strcat(errorText, "TD is still active");
		else
			strcat(errorText, "finished");

		kernelDebugError("%s", errorText);
		kernelFree(errorText);
	}
}
#else
	#define debugCapRegs(ehci) do { } while (0)
	#define debugHcsParams(ehci) do { } while (0)
	#define debugHccParams(ehci) do { } while (0)
	#define debugOpRegs(ehci) do { } while (0)
	#define debugPortStatus(controller, num) do { } while (0)
	#define debugQtd(qtd) do { } while (0)
	#define debugQueueHead(queueHead) do { } while (0)
	#define debugTransError(qtd) do { } while (0)
#endif // DEBUG


static int releaseQueueHead(usbEhciData *ehci,
	ehciQueueHeadItem *queueHeadItem)
{
	// Remove the queue head from the list of 'used' queue heads, and add it
	// back into the list of 'free' queue heads.

	int status = 0;
	kernelLinkedList *freeList =
		(kernelLinkedList *) &ehci->freeQueueHeadItems;

	// Add it to the free list
	if (kernelLinkedListAdd(freeList, queueHeadItem) < 0)
		kernelError(kernel_warn, "Couldn't add item to queue head free list");

	return (status = 0);
}


static int allocQueueHeads(kernelLinkedList *freeList)
{
	// Allocate a page worth of physical memory for ehciQueueHead data
	// structures, allocate an equal number of ehciQueueHeadItem
	// structures to point at them, link them together, and add them to the
	// supplied kernelLinkedList.

	int status = 0;
	kernelIoMemory ioMem;
	ehciQueueHead *queueHeads = NULL;
	int numQueueHeads = 0;
	ehciQueueHeadItem *queueHeadItems = NULL;
	unsigned physicalAddr = 0;
	int count;

	kernelDebug(debug_usb, "EHCI adding queue heads to free list");

	// Request an aligned page of I/O memory (we need to be sure of 32-byte
	// alignment for each queue head)
	status = kernelMemoryGetIo(MEMORY_PAGE_SIZE, MEMORY_PAGE_SIZE, &ioMem);
	if (status < 0)
		goto err_out;

	queueHeads = ioMem.virtual;

	// How many queue heads per memory page?  The ehciQueueHead data structure
	// will be padded so that they start on 32-byte boundaries, as required by
	// the hardware
	numQueueHeads = (MEMORY_PAGE_SIZE / sizeof(ehciQueueHead));

	// Get memory for ehciQueueHeadItem structures
	queueHeadItems = kernelMalloc(numQueueHeads * sizeof(ehciQueueHeadItem));
	if (!queueHeadItems)
	{
		status = ERR_MEMORY;
		goto err_out;
	}

	// Now loop through each list item, link it to a queue head, and add it
	// to the free list
	physicalAddr = ioMem.physical;
	for (count = 0; count < numQueueHeads; count ++)
	{
		queueHeadItems[count].queueHead = &queueHeads[count];
		queueHeadItems[count].physical = physicalAddr;
		physicalAddr += sizeof(ehciQueueHead);

		status = kernelLinkedListAdd(freeList, &queueHeadItems[count]);
		if (status < 0)
		{
			kernelError(kernel_error, "Couldn't add new queue heads to free "
				"list");
			goto err_out;
		}
	}

	kernelDebug(debug_usb, "EHCI added %d queue heads", numQueueHeads);
	return (status = 0);

err_out:

	if (queueHeadItems)
		kernelFree(queueHeadItems);
	if (queueHeads)
		kernelMemoryReleaseIo(&ioMem);

	return (status);
}


static int setQueueHeadEndpointState(usbDevice *usbDev,
	unsigned char endpointNum, ehciQueueHead *queueHead)
{
	// Given a usbDevice structure and an endpoint number, set all the relevent
	// "static endpoint state" fields in the queue head.

	int status = 0;
	usbEndpoint *endpoint = NULL;
	int maxPacketLen = 0;
	usbDevice *parentHub = NULL;
	int hubPort = 0;

	kernelDebug(debug_usb, "EHCI set queue head state for %s speed device %u, "
		"endpoint 0x%02x", usbDevSpeed2String(usbDev->speed), usbDev->address,
		endpointNum);

	// Max NAK retries, we guess
	queueHead->endpointChars = EHCI_QHDW1_NAKCNTRELOAD;

	// If this is not a high speed device, and we're talking to the control
	// endpoint, set this to 1
	if ((usbDev->speed != usbspeed_high) && !endpointNum)
		queueHead->endpointChars |= EHCI_QHDW1_CTRLENDPOINT;

	endpoint = kernelUsbGetEndpoint(usbDev, endpointNum);
	if (!endpoint)
	{
		kernelError(kernel_error, "Endpoint 0x%02x not found", endpointNum);
		return (status = ERR_NOSUCHENTRY);
	}

	// Set the maximum endpoint packet length
	maxPacketLen = endpoint->maxPacketSize;

	// If we haven't yet got the descriptors, etc., use 8 as the maximum size
	if (!maxPacketLen)
	{
		kernelDebug(debug_usb, "EHCI using default maximum endpoint transfer "
			"size 8 for endpoint 0x%02x", endpointNum);
		maxPacketLen = 8;
	}

	// Set the maximum endpoint packet length
	queueHead->endpointChars |= ((maxPacketLen << 16) &
		EHCI_QHDW1_MAXPACKETLEN);

	// Tell the controller to get the data toggle from the qTDs
	queueHead->endpointChars |= EHCI_QHDW1_DATATOGGCTRL;

	// Mark the speed of the device
	switch (usbDev->speed)
	{
		case usbspeed_high:
		default:
			queueHead->endpointChars |= EHCI_QHDW1_ENDPTSPDHIGH;
			break;
		case usbspeed_full:
			queueHead->endpointChars |= EHCI_QHDW1_ENDPTSPDFULL;
			break;
		case usbspeed_low:
			queueHead->endpointChars |= EHCI_QHDW1_ENDPTSPDLOW;
			break;
	}

	// The endpoint number
	queueHead->endpointChars |= (((unsigned) endpointNum << 8) &
		EHCI_QHDW1_ENDPOINT);

	// The device address
	queueHead->endpointChars |= (usbDev->address & EHCI_QHDW1_DEVADDRESS);

	// Assume minimum speed multiplier for now
	queueHead->endpointCaps = EHCI_QHDW2_HISPEEDMULT1;

	if (usbDev->speed != usbspeed_high)
	{
		// Port number, hub address, and split completion mask only set for
		// a full- or low-speed device

		// Find the parent high speed hub, if applicable.  Root hubs don't
		// have constituent USB devices.
		parentHub = usbDev->hub->usbDev;
		hubPort = usbDev->hubPort;
		while (parentHub)
		{
			if (parentHub->speed == usbspeed_high)
			{
				queueHead->endpointCaps |= (((hubPort + 1) << 23) &
					EHCI_QHDW2_PORTNUMBER);

				queueHead->endpointCaps |= ((parentHub->address << 16) &
					EHCI_QHDW2_HUBADDRESS);

				kernelDebug(debug_usb, "EHCI using hub address %d, "
					"port %d", parentHub->address, hubPort);

				break;
			}

			hubPort = parentHub->hubPort;
			parentHub = parentHub->hub->usbDev;
		}
	}

	return (status = 0);
}


static ehciQueueHeadItem *allocQueueHead(usbController *controller,
	usbDevice *usbDev, unsigned char endpoint)
{
	// Allocate the queue head for the device endpoint.
	//
	// Each device endpoint has at most one queue head (which may be linked
	// into either the synchronous or asynchronous queue, depending on the
	// endpoint type).
	//
	// It's OK for the usbDevice parameter to be NULL; the asynchronous list
	// will have a single, unused queue head to mark the start of the list.

	usbEhciData *ehci = controller->data;
	kernelLinkedList *freeList =
		(kernelLinkedList *) &ehci->freeQueueHeadItems;
	ehciQueueHeadItem *queueHeadItem = NULL;
	kernelLinkedListItem *iter = NULL;

	kernelDebug(debug_usb, "EHCI alloc queue head for controller %d, "
		"usbDev %p, endpoint 0x%02x", controller->num, usbDev, endpoint);

	// Anything in the free list?
	if (!freeList->numItems)
	{
		// Super, the free list is empty.  We need to allocate everything.
		if (allocQueueHeads(freeList) < 0)
		{
			kernelError(kernel_error, "Couldn't allocate new queue heads");
			goto err_out;
		}
	}

	// Grab the first item in the free list
	queueHeadItem = kernelLinkedListIterStart(freeList, &iter);
	if (!queueHeadItem)
	{
		kernelError(kernel_error, "Couldn't get a list item for a new queue "
			"head");
		goto err_out;
	}

	// Remove it from the free list
	if (kernelLinkedListRemove(freeList, queueHeadItem) < 0)
	{
		kernelError(kernel_error, "Couldn't remove item from queue head free "
			"list");
		goto err_out;
	}

	// Initialize the queue head item
	queueHeadItem->usbDev = (void *) usbDev;
	queueHeadItem->endpoint = endpoint;
	queueHeadItem->firstQtdItem = NULL;

	// Initialize the queue head
	memset((void *) queueHeadItem->queueHead, 0, sizeof(ehciQueueHead));
	queueHeadItem->queueHead->horizLink =
		queueHeadItem->queueHead->currentQtd =
			queueHeadItem->queueHead->overlay.nextQtd =
				queueHeadItem->queueHead->overlay.altNextQtd = EHCI_LINK_TERM;

	kernelDebug(debug_usb, "EHCI added queue head for usbDev %p, endpoint "
		"0x%02x", queueHeadItem->usbDev, queueHeadItem->endpoint);

	if (usbDev)
	{
		// Set the "static endpoint state" in the queue head
		if (setQueueHeadEndpointState(usbDev, endpoint,
				queueHeadItem->queueHead) < 0)
			goto err_out;
	}

	// Return success
	return (queueHeadItem);

err_out:
	if (queueHeadItem)
		releaseQueueHead(ehci, queueHeadItem);

	return (queueHeadItem = NULL);
}


static int setup(usbController *controller)
{
	// Allocate things, and set up any global controller registers prior to
	// changing the controller to the 'running' state.

	// Note that in the case of a host system error, we use this function to
	// re-initialize things, but we don't have to reallocate the memory.

	int status = 0;
	usbEhciData *ehci = controller->data;
	kernelIoMemory ioMem;
	int count1, count2;

	kernelDebug(debug_usb, "EHCI set up controller %d", controller->num);

	if (!ehci->asyncHeads)
	{
		// Allocate memory for the NULL queue head that will be the head of the
		// 'reclaim' (asynchronous) queue
		ehci->asyncHeads = allocQueueHead(controller, NULL /* no dev */,
			0 /* no endpoint */);
		if (!ehci->asyncHeads)
			return (status = ERR_NOTINITIALIZED);
	}

	// Make it point to itself, set the 'H' bit, and make sure the qTD
	// pointers don't point to anything.
	ehci->asyncHeads->queueHead->horizLink =
		(ehci->asyncHeads->physical | EHCI_LINKTYP_QH);
	ehci->asyncHeads->queueHead->endpointChars = EHCI_QHDW1_RECLLISTHEAD;
	ehci->asyncHeads->queueHead->currentQtd =
		ehci->asyncHeads->queueHead->overlay.nextQtd =
			ehci->asyncHeads->queueHead->overlay.altNextQtd = EHCI_LINK_TERM;

	// Put the address into the asychronous list address register
	ehci->opRegs->asynclstaddr = ehci->asyncHeads->physical;

	// After the reset, the default value of the USBCMD register is 0x00080000
	// (no async schedule park) or 0x00080B00 (async schedule park).  If any of
	// the default values aren't acceptable for us, change them here.

	// Hmm, VMware doesn't seem to set the defaults.  Set the interrupt
	// threshold control
	ehci->opRegs->cmd &= ~EHCI_CMD_INTTHRESCTL;
	ehci->opRegs->cmd |= (0x08 << 16);

	// The FRINDEX register will be 0 (start of periodic frame list).
	ehci->opRegs->frindex = 0;

	// The CTRLDSSEGMENT will be 0, which means we're using a 32-bit address
	// space.
	ehci->opRegs->ctrldsseg = 0;

	// If the size of the periodic queue frame list is programmable, make sure
	// it's set to the default (1024 = 0)
	if (ehci->capRegs->hccparams & EHCI_HCCP_PROGFRAMELIST)
		ehci->opRegs->cmd &= ~EHCI_CMD_FRAMELISTSIZE;

	if (!ehci->periodicList)
	{
		// Allocate memory for the periodic queue frame list, and assign the
		// physical address to the PERIODICLISTBASE register
		status = kernelMemoryGetIo(EHCI_FRAMELIST_MEMSIZE, MEMORY_PAGE_SIZE,
			&ioMem);
		if (status < 0)
		{
			kernelError(kernel_error, "Couldn't get periodic frame list "
				"memory");
			return (status);
		}

		ehci->periodicList = ioMem.virtual;
		ehci->opRegs->perlstbase = ioMem.physical;
	}
	else
	{
		ehci->opRegs->perlstbase = (unsigned)
			kernelPageGetPhysical(KERNELPROCID, ehci->periodicList);
	}

	if (!ehci->intQueue[0])
	{
		// Attach queue heads for interrupts, and link them from highest
		// interval to lowest
		for (count1 = 0; count1 < EHCI_NUM_INTQUEUEHEADS; count1 ++)
		{
			ehci->intQueue[count1] = allocQueueHead(controller,
				NULL /* no dev */, 0 /* no endpoint */);
			if (!ehci->intQueue[count1])
			{
				kernelError(kernel_error, "Couldn't get interrupt queue "
					"heads");
				return (status = ERR_MEMORY);
			}

			// Link
			if (count1)
			{
				ehci->intQueue[count1 - 1]->queueHead->horizLink =
					(ehci->intQueue[count1]->physical | EHCI_LINKTYP_QH);
			}
		}

		// Set each periodic list pointer
		for (count1 = 0; count1 < EHCI_NUM_FRAMES; count1 ++)
		{
			// Default is 256 (for 0 and other multiples of 0x100)
			ehci->periodicList[count1] =
				(ehci->intQueue[0]->physical | EHCI_LINKTYP_QH);

			for (count2 = 0; count2 < 8; count2 ++)
			{
				if ((count1 >> count2) & 1)
				{
					ehci->periodicList[count1] =
						(ehci->intQueue[EHCI_NUM_INTQUEUEHEADS - (count2 + 1)]
							->physical | EHCI_LINKTYP_QH);
					break;
				}
			}
		}
	}

	// Enable the interrupts we're interested in, in the USBINTR register; Host
	// system error, port change, error interrupt, and USB (data) interrupt.
	ehci->opRegs->intr = (EHCI_INTR_HOSTSYSERROR | EHCI_INTR_USBERRORINT |
		EHCI_INTR_USBINTERRUPT);

	// Set the 'configured' flag in the CONFIGFLAG register
	ehci->opRegs->configflag |= 1;

	return (status = 0);
}


static int startStop(usbEhciData *ehci, int start)
{
	// Start or stop the EHCI controller

	int status = 0;
	int count;

	kernelDebug(debug_usb, "EHCI st%s controller", (start? "art" : "op"));

	if (start)
	{
		if (ehci->opRegs->stat & EHCI_STAT_HCHALTED)
		{
			// Set the run/stop bit
			ehci->opRegs->cmd |= EHCI_CMD_RUNSTOP;

			// Wait for not halted
			for (count = 0; count < 200; count ++)
			{
				if (!(ehci->opRegs->stat & EHCI_STAT_HCHALTED))
				{
					kernelDebug(debug_usb, "EHCI starting controller took "
						"%dms", count);
					break;
				}

				kernelCpuSpinMs(1);
			}

			// Started?
			if (!(ehci->opRegs->stat & EHCI_STAT_HCHALTED))
			{
				// Started, but some controllers need a small delay here,
				// before they're fully up and running.  3ms seems to be
				// enough, but we'll give it a little bit longer .
				kernelCpuSpinMs(10);
			}
			else
			{
				kernelError(kernel_error, "Couldn't clear controller halted "
					"bit");
				status = ERR_TIMEOUT;
			}
		}
	}
	else // stop
	{
		if (!(ehci->opRegs->stat & EHCI_STAT_HCHALTED))
		{
			// Clear the run/stop bit
			ehci->opRegs->cmd &= ~EHCI_CMD_RUNSTOP;

			// Wait for halted
			for (count = 0; count < 20; count ++)
			{
				if (ehci->opRegs->stat & EHCI_STAT_HCHALTED)
				{
					kernelDebug(debug_usb, "EHCI stopping controller took "
						"%dms", count);
					break;
				}

				kernelCpuSpinMs(1);
			}

			// Stopped?
			if (!(ehci->opRegs->stat & EHCI_STAT_HCHALTED))
			{
				kernelError(kernel_error, "Couldn't set controller halted "
					"bit");
				status = ERR_TIMEOUT;
			}
		}
	}

	kernelDebug(debug_usb, "EHCI controller %sst%sed", (status? "not " : ""),
		(start? "art" : "opp"));

	return (status);
}


static inline void setPortStatusBits(usbEhciData *ehci, int portNum,
	unsigned bits)
{
	// Set the requested read-write status bits, without affecting any of
	// the read-only or read-write-clear bits

	ehci->opRegs->portsc[portNum] = ((ehci->opRegs->portsc[portNum] &
		~(EHCI_PORTSC_ROMASK | EHCI_PORTSC_RWCMASK)) | bits);
}


static inline void clearPortStatusBits(usbEhciData *ehci, int portNum,
	unsigned bits)
{
	// Clear the requested read-write status bits, without affecting any of
	// the read-only or read-write-clear bits

	ehci->opRegs->portsc[portNum] = (ehci->opRegs->portsc[portNum] &
		~(EHCI_PORTSC_ROMASK | EHCI_PORTSC_RWCMASK | bits));
}


static int portPower(usbEhciData *ehci, int portNum, int on)
{
	// If port power control is available, this function will turn it on or
	// off

	int status = 0;
	int count;

	kernelDebug(debug_usb, "EHCI %sable port power", (on? "en" : "dis"));

	if (on)
	{
		// Turn on the port power bit
		setPortStatusBits(ehci, portNum, EHCI_PORTSC_PORTPOWER);

		// Wait for it to read as set
		for (count = 0; count < 20; count ++)
		{
			if (ehci->opRegs->portsc[portNum] & EHCI_PORTSC_PORTPOWER)
			{
				kernelDebug(debug_usb, "EHCI turning on port power took %dms",
					count);
				break;
			}

			kernelCpuSpinMs(1);
		}

		// Set?
		if (!(ehci->opRegs->portsc[portNum] & EHCI_PORTSC_PORTPOWER))
		{
			kernelError(kernel_warn, "Couldn't set port power bit");
			return (status = ERR_TIMEOUT);
		}
	}
	else // off
	{
		// Don't think we'll ever use this
	}

	return (status = 0);
}


static int startStopSched(usbEhciData *ehci, unsigned statBit, unsigned cmdBit,
	int start)
{
	// Start or stop the processing of a schedule

	int status = 0;
	const char *schedName = NULL;
	int count;

	switch (statBit)
	{
		case EHCI_STAT_ASYNCSCHED:
			schedName = "asynchronous";
			break;
		case EHCI_STAT_PERIODICSCHED:
			schedName = "periodic";
			break;
	}

	kernelDebug(debug_usb, "EHCI st%s %s processing", (start? "art" : "op"),
		schedName);

	if (start)
	{
		if (!(ehci->opRegs->stat & statBit))
		{
			// Start schedule processing
			ehci->opRegs->cmd |= cmdBit;

			// Wait for it to be started
			for (count = 0; count < 20; count ++)
			{
				if (ehci->opRegs->stat & statBit)
				{
					kernelDebug(debug_usb, "EHCI starting %s schedule took "
						"%dms", schedName, count);
					break;
				}

				kernelCpuSpinMs(1);
			}

			// Started?
			if (!(ehci->opRegs->stat & statBit))
			{
				kernelError(kernel_error, "Couldn't enable %s schedule",
					schedName);
				return (status = ERR_TIMEOUT);
			}
		}
	}
	else
	{
		if (ehci->opRegs->stat & statBit)
		{
			// Stop schedule processing
			ehci->opRegs->cmd &= ~cmdBit;

			// Wait for it to be stopped
			for (count = 0; count < 20; count ++)
			{
				if (!(ehci->opRegs->stat & statBit))
				{
					kernelDebug(debug_usb, "EHCI stopping %s schedule took "
						"%dms", schedName, count);
					break;
				}

				kernelCpuSpinMs(1);
			}

			// Stopped?
			if (ehci->opRegs->stat & statBit)
			{
				kernelError(kernel_error, "Couldn't disable %s schedule",
					schedName);
				return (status = ERR_TIMEOUT);
			}
		}
	}

	kernelDebug(debug_usb, "EHCI %s processing st%s", schedName,
		(start? "arted" : "opped"));
	return (status = 0);
}


static void hostSystemError(usbController *controller)
{
	int status = 0;
	usbEhciData *ehci = controller->data;
	ehciQueueHeadItem *queueHeadItem = NULL;
	ehciQtdItem *qtdItem = NULL;
	int count;

	// Reset the controller
	status = reset(controller);
	if (status < 0)
		return;

	// Set up the controller's data structures, etc.
	status = setup(controller);
	if (status < 0)
		return;

	// Start the controller
	status = startStop(ehci, 1);
	if (status < 0)
		return;

	// Power on all the ports, if applicable
	if (ehci->capRegs->hcsparams & EHCI_HCSP_PORTPOWERCTRL)
	{
		for (count = 0; count < ehci->numPorts; count ++)
			portPower(ehci, count, 1);

		// Wait 20ms for power to stabilize on all ports (per EHCI spec)
		kernelCpuSpinMs(20);
	}

	// Remove all transactions from the queue heads and mark them as failed.
	queueHeadItem = ehci->devHeads;
	while (queueHeadItem)
	{
		queueHeadItem->queueHead->currentQtd = EHCI_LINK_TERM;
		memset((void *) &queueHeadItem->queueHead->overlay, 0,
			sizeof(ehciQtd));

		qtdItem = queueHeadItem->firstQtdItem;
		while (qtdItem)
		{
			qtdItem->qtd->token |= EHCI_QTDTOKEN_ERRXACT;
			qtdItem->qtd->token &= ~EHCI_QTDTOKEN_ACTIVE;
			qtdItem = qtdItem->nextQtdItem;
		}

		queueHeadItem = queueHeadItem->devNext;
	}

	// Restart the asynchronous schedule
	ehci->opRegs->asynclstaddr = ehci->asyncHeads->physical;
	startStopSched(ehci, EHCI_STAT_ASYNCSCHED, EHCI_CMD_ASYNCSCHEDENBL, 1);

	// Restart the periodic schedule
	startStopSched(ehci, EHCI_STAT_PERIODICSCHED, EHCI_CMD_PERSCHEDENBL, 1);

	return;
}


static int setupQtdToken(ehciQtd *qtd, volatile unsigned char *dataToggle,
	unsigned totalBytes, int interrupt, unsigned char pid)
{
	// Do the nuts-n-bolts setup for a qTD transfer descriptor

	int status = 0;

	qtd->token = 0;

	if (dataToggle)
		// Set the data toggle
		qtd->token |= (*dataToggle << 31);

	// Set the number of bytes to transfer
	qtd->token |= ((totalBytes << 16) & EHCI_QTDTOKEN_TOTBYTES);

	// Interrupt on complete?
	qtd->token |= ((interrupt << 15) & EHCI_QTDTOKEN_IOC);

	// Current page is 0

	// Set the error down-counter to 3
	qtd->token |= EHCI_QTDTOKEN_ERRCOUNT;

	switch (pid)
	{
		case USB_PID_OUT:
			qtd->token |= EHCI_QTDTOKEN_PID_OUT;
			break;
		case USB_PID_IN:
			qtd->token |= EHCI_QTDTOKEN_PID_IN;
			break;
		case USB_PID_SETUP:
			qtd->token |= EHCI_QTDTOKEN_PID_SETUP;
			break;
		default:
			kernelError(kernel_error, "Invalid PID %u", pid);
			return (status = ERR_INVALID);
	}

	// Mark it active
	qtd->token |= EHCI_QTDTOKEN_ACTIVE;

	// Return success
	return (status = 0);
}


static void setStatusBits(usbEhciData *ehci, unsigned bits)
{
	// Set the requested status bits, without affecting the read-only and
	// read-write-clear bits (can also be used to clear read-write-clear bits)

	ehci->opRegs->stat = ((ehci->opRegs->stat &
		~(EHCI_STAT_ROMASK | EHCI_STAT_RWCMASK)) | bits);
}


static ehciQueueHeadItem *findQueueHead(usbController *controller,
	usbDevice *usbDev, unsigned char endpoint)
{
	// Search the controller's list of used queue heads for one that belongs
	// to the requested device and endpoint.

	usbEhciData *ehci = controller->data;
	ehciQueueHeadItem *queueHeadItem = NULL;

	kernelDebug(debug_usb, "EHCI find queue head for controller %d, usbDev "
		"%p, endpoint 0x%02x", controller->num, usbDev, endpoint);

	// Try searching for an existing queue head
	queueHeadItem = ehci->devHeads;
	while (queueHeadItem)
	{
		kernelDebug(debug_usb, "EHCI examine queue head for device %p "
			"endpoint 0x%02x", queueHeadItem->usbDev, queueHeadItem->endpoint);

		if ((queueHeadItem->usbDev == usbDev) &&
			(queueHeadItem->endpoint == endpoint))
		{
			break;
		}

		queueHeadItem = queueHeadItem->devNext;
	}

	// Found it?
	if (queueHeadItem)
		kernelDebug(debug_usb, "EHCI found queue head");
	else
		kernelDebug(debug_usb, "EHCI queue head not found");

	return (queueHeadItem);
}


static ehciQueueHeadItem *allocAsyncQueueHead(usbController *controller,
	usbDevice *usbDev, unsigned char endpoint)
{
	// Allocate a queue head for the asynchronous queue, insert it, and make
	// sure the controller is processing them.

	usbEhciData *ehci = controller->data;
	ehciQueueHeadItem *queueHeadItem = NULL;

	queueHeadItem = allocQueueHead(controller, usbDev, endpoint);
	if (!queueHeadItem)
	{
		kernelError(kernel_error, "Couldn't allocate asynchronous queue head");
		goto err_out;
	}

	kernelDebug(debug_usb, "EHCI inserting queue head into device head list");

	queueHeadItem->devNext = ehci->devHeads;
	ehci->devHeads = queueHeadItem;

	kernelDebug(debug_usb, "EHCI inserting queue head into asynchronous "
		"schedule");

	// Insert it into the asynchronous queue
	queueHeadItem->listNext = ehci->asyncHeads->listNext;
	queueHeadItem->queueHead->horizLink =
		ehci->asyncHeads->queueHead->horizLink;
	ehci->asyncHeads->listNext = queueHeadItem;
	ehci->asyncHeads->queueHead->horizLink = (queueHeadItem->physical |
		EHCI_LINKTYP_QH);

	// If the asynchronous schedule is not running, start it now
	if (!(ehci->opRegs->stat & EHCI_STAT_ASYNCSCHED))
	{
		// Seems like sometimes this register gets corrupted by something after
		// our initial setup - starting the controller?
		ehci->opRegs->asynclstaddr = ehci->asyncHeads->physical;

		if (startStopSched(ehci, EHCI_STAT_ASYNCSCHED,
			EHCI_CMD_ASYNCSCHEDENBL, 1) < 0)
		{
			goto err_out;
		}
	}

	// Return success
	return (queueHeadItem);

err_out:
	if (queueHeadItem)
		releaseQueueHead(ehci, queueHeadItem);

	return (queueHeadItem = NULL);
}


static int allocQtds(kernelLinkedList *freeList)
{
	// Allocate a page worth of physical memory for ehciQtd data structures,
	// allocate an equal number of ehciQtdItem structures to point at them,
	// and add them to the supplied kernelLinkedList.

	int status = 0;
	kernelIoMemory ioMem;
	ehciQtd *qtds = NULL;
	int numQtds = 0;
	ehciQtdItem *qtdItems = NULL;
	unsigned physicalAddr = 0;
	int count;

	// Request an aligned page of I/O memory (we need to be sure of 32-byte
	// alignment for each qTD)
	status = kernelMemoryGetIo(MEMORY_PAGE_SIZE, MEMORY_PAGE_SIZE, &ioMem);
	if (status < 0)
		goto err_out;

	qtds = ioMem.virtual;

	// How many queue heads per memory page?  The ehciQueueHead data structure
	// will be padded so that they start on 32-byte boundaries, as required by
	// the hardware
	numQtds = (MEMORY_PAGE_SIZE / sizeof(ehciQtd));

	// Get memory for ehciQtdItem structures
	qtdItems = kernelMalloc(numQtds * sizeof(ehciQtdItem));
	if (!qtdItems)
	{
		status = ERR_MEMORY;
		goto err_out;
	}

	// Now loop through each list item, link it to a qTD, and add it to the
	// free list
	physicalAddr = ioMem.physical;
	for (count = 0; count < numQtds; count ++)
	{
		qtdItems[count].qtd = &qtds[count];
		qtdItems[count].physical = physicalAddr;
		physicalAddr += sizeof(ehciQtd);

		status = kernelLinkedListAdd(freeList, &qtdItems[count]);
		if (status < 0)
			goto err_out;
	}

	// Return success
	return (status = 0);

err_out:

	if (qtdItems)
		kernelFree(qtdItems);
	if (qtds)
		kernelMemoryReleaseIo(&ioMem);

	return (status);
}


static void releaseQtds(usbEhciData *ehci, ehciQtdItem **qtdItems, int numQtds)
{
	// Release qTDs back to the free pool after use

	kernelLinkedList *freeList = (kernelLinkedList *) &ehci->freeQtdItems;
	int count;

	for (count = 0; count < numQtds; count ++)
	{
		if (qtdItems[count])
		{
			// If a buffer was allocated for the qTD, free it
			if (qtdItems[count]->buffer)
				kernelFree(qtdItems[count]->buffer);

			// Try to add it back to the free list
			kernelLinkedListAdd(freeList, qtdItems[count]);
		}
	}

	kernelFree(qtdItems);
	return;
}


static ehciQtdItem **getQtds(usbEhciData *ehci, int numQtds)
{
	// Allocate the requested number of Queue Element Transfer Descriptors,
	// and chain them together.

	ehciQtdItem **qtdItems = NULL;
	kernelLinkedList *freeList = (kernelLinkedList *) &ehci->freeQtdItems;
	kernelLinkedListItem *iter = NULL;
	int count;

	kernelDebug(debug_usb, "EHCI get %d qTDs", numQtds);

	// Get memory for our list of ehciQtdItem pointers
	qtdItems = kernelMalloc(numQtds * sizeof(ehciQtdItem *));
	if (!qtdItems)
		goto err_out;

	for (count = 0; count < numQtds; count ++)
	{
		// Anything in the free list?
		if (!freeList->numItems)
		{
			// Super, the free list is empty.  We need to allocate everything.
			if (allocQtds(freeList) < 0)
			{
				kernelError(kernel_error, "Couldn't allocate new qTDs");
				goto err_out;
			}
		}

		// Grab the first one from the free list
		qtdItems[count] = kernelLinkedListIterStart(freeList, &iter);
		if (!qtdItems[count])
		{
			kernelError(kernel_error, "Couldn't get a list item for a new qTD");
			goto err_out;
		}

		// Remove it from the free list
		if (kernelLinkedListRemove(freeList, qtdItems[count]) < 0)
		{
			kernelError(kernel_error, "Couldn't remove item from qTD free list");
			goto err_out;
		}

		// Initialize the qTD item
		qtdItems[count]->buffer = NULL;
		qtdItems[count]->nextQtdItem = NULL;

		// Clear/terminate the 'next' pointers, and any old data
		memset((void *) qtdItems[count]->qtd, 0, sizeof(ehciQtd));
		qtdItems[count]->qtd->nextQtd = EHCI_LINK_TERM;
		qtdItems[count]->qtd->altNextQtd = EHCI_LINK_TERM;

		// Chain it to the previous one, if applicable
		if (count)
		{
			qtdItems[count - 1]->nextQtdItem = qtdItems[count];
			qtdItems[count - 1]->qtd->nextQtd = qtdItems[count]->physical;
		}
	}

	// Return success
	return (qtdItems);

err_out:

	if (qtdItems)
		releaseQtds(ehci, qtdItems, numQtds);

	return (qtdItems = NULL);
}


static int setQtdBufferPages(ehciQtd *qtd, unsigned buffPhysical,
	unsigned buffSize)
{
	// Given a physical buffer address and size, apportion the buffer page
	// fields in the qTD, so that they don't cross physical memory page
	// boundaries

	int status = 0;
	unsigned bytes = 0;
	int count;

	for (count = 0; ((count < EHCI_MAX_QTD_BUFFERS) && (buffSize > 0));
		count ++)
	{
		bytes = min(buffSize, (EHCI_MAX_QTD_BUFFERSIZE -
			(buffPhysical % EHCI_MAX_QTD_BUFFERSIZE)));

		kernelDebug(debug_usb, "EHCI qTD buffer page %d=0x%08x size=%u", count,
			buffPhysical, bytes);

		qtd->buffPage[count] = buffPhysical;

		buffPhysical += bytes;
		buffSize -= bytes;
	}

	if (buffSize)
	{
		// The size and/or page alignment of the buffer didn't fit into the qTD
		kernelError(kernel_error, "Buffer does not fit in a single qTD");
		return (status = ERR_BOUNDS);
	}

	// Return success
	return (status = 0);
}


static int allocQtdBuffer(ehciQtdItem *qtdItem, unsigned buffSize)
{
	// Allocate a data buffer for a qTD.  This is only used for cases in which
	// the caller doesn't supply its own data buffer, such as the setup stage
	// of control transfers, or for interrupt registrations.

	int status = 0;
	unsigned buffPhysical = NULL;

	kernelDebug(debug_usb, "EHCI allocate qTD buffer of %u", buffSize);

	// Get the memory from kernelMalloc(), so that the caller can easily
	// kernelFree() it when finished.
	qtdItem->buffer = kernelMalloc(buffSize);
	if (!qtdItem->buffer)
	{
		kernelDebugError("Can't alloc trans desc buffer size %u", buffSize);
		return (status = ERR_MEMORY);
	}

	// Get the physical address of this memory
	buffPhysical = kernelPageGetPhysical(KERNELPROCID, qtdItem->buffer);
	if (!buffPhysical)
	{
		kernelDebugError("Can't get buffer physical address");
		kernelFree(qtdItem->buffer);
		qtdItem->buffer = NULL;
		return (status = ERR_BADADDRESS);
	}

	// Now set up the buffer pointers in the qTD
	status = setQtdBufferPages(qtdItem->qtd, buffPhysical, buffSize);
	if (status < 0)
		return (status);

	// Return success
	return (status = 0);
}


static int queueTransaction(ehciTransQueue *transQueue)
{
	// The ehciTransQueue structure contains pointers to a queue head, and
	// an array of qTDs that should be linked to it.  If any existing qTDs are
	// linked, walk the chain and attach the new ones at the end.  Otherwise,
	// attach them directly to the queue head.

	int status = 0;
	ehciQtdItem *qtdItem = NULL;

	kernelDebug(debug_usb, "EHCI add transaction to queue");

	if (transQueue->queueHeadItem->firstQtdItem)
	{
		// There's already something in the queue.  Walk it to find the end.

		kernelDebug(debug_usb, "EHCI link to existing qTDs");

		qtdItem = transQueue->queueHeadItem->firstQtdItem;
		while (qtdItem->nextQtdItem)
			qtdItem = qtdItem->nextQtdItem;

		qtdItem->nextQtdItem = transQueue->qtdItems[0];
		qtdItem->qtd->nextQtd = transQueue->qtdItems[0]->physical;

		// Make sure the 'next' pointer of the queue head points to something
		// valid (if not, point to our first qTD)
		if (transQueue->queueHeadItem->queueHead->overlay.nextQtd &
			EHCI_LINK_TERM)
		{
			transQueue->queueHeadItem->queueHead->overlay.nextQtd =
				transQueue->qtdItems[0]->physical;
		}
	}
	else
	{
		// There's nothing in the queue.  Link to the queue head.

		kernelDebug(debug_usb, "EHCI link directly to queue head");

		transQueue->queueHeadItem->firstQtdItem = transQueue->qtdItems[0];
		transQueue->queueHeadItem->queueHead->overlay.nextQtd =
			transQueue->qtdItems[0]->physical;
	}

	// Return success
	return (status = 0);
}


static int runTransaction(ehciTransQueue *transQueue, unsigned timeout)
{
	int status = 0;
	uquad_t currTime = kernelCpuGetMs();
	uquad_t endTime = (currTime + timeout);
	int active = 0;
	unsigned firstActive __attribute__((unused)) = 0;
	int error = 0;
	int count;

	kernelDebug(debug_usb, "EHCI run transaction with %d qTDs",
		transQueue->numQtds);

	// Wait while some qTD is active, or until we detect an error
	while (currTime <= endTime)
	{
		active = 0;
		firstActive = 0;

		for (count = 0; count < transQueue->numQtds; count ++)
		{
			if (transQueue->qtdItems[count]->qtd->token &
				EHCI_QTDTOKEN_ACTIVE)
			{
				active = 1;
				firstActive = count;
				break;
			}
			else if (transQueue->qtdItems[count]->qtd->token &
				EHCI_QTDTOKEN_ERROR)
			{
				kernelDebugError("Transaction error on qTD %d", count);
				debugTransError(transQueue->qtdItems[count]->qtd);
				error = 1;
				break;
			}
		}

		// If no more active, or errors, we're finished.
		if (!active || error)
		{
			if (error)
			{
				status = ERR_IO;
			}
			else
			{
				kernelDebug(debug_usb, "EHCI transaction completed "
					"successfully");
				status = 0;
			}

			break;
		}

		// Yielding here really hits performance.  Perhaps need to experiment
		// with interrupt-driven system.
		// kernelMultitaskerYield();

		currTime = kernelCpuGetMs();
	}

	// Were any bytes left un-transferred?
	for (count = 0; count < transQueue->numQtds; count ++)
		transQueue->bytesRemaining +=
			((transQueue->qtdItems[count]->qtd->token &
				EHCI_QTDTOKEN_TOTBYTES) >> 16);

	if (currTime > endTime)
	{
		kernelDebugError("Software timeout on TD %d", firstActive);
		status = ERR_TIMEOUT;
	}

	return (status);
}


static int dequeueTransaction(ehciTransQueue *transQueue)
{
	// The ehciTransQueue structure contains pointers to a queue head, and
	// an array of qTDs that should be unlinked from it.  Determine whether
	// they are linked directly from the queue head, or else somewhere else in
	// a chain of transactions.

	int status = 0;
	ehciQtdItem *qtdItem = NULL;

	kernelDebug(debug_usb, "EHCI remove transaction from queue head");

	if (!transQueue->queueHeadItem)
		return (status = ERR_NOTINITIALIZED);

	if (transQueue->queueHeadItem->firstQtdItem == transQueue->qtdItems[0])
	{
		// We're linked directly from the queue head.  Replace the 'next'
		// qTD pointers in the queue head with the 'next' pointers from our
		// last qTD (which might be NULL/terminating)

		kernelDebug(debug_usb, "EHCI unlink directly from queue head");

		transQueue->queueHeadItem->firstQtdItem =
			transQueue->qtdItems[transQueue->numQtds - 1]->nextQtdItem;
		transQueue->queueHeadItem->queueHead->overlay.nextQtd =
			transQueue->qtdItems[transQueue->numQtds - 1]->qtd->nextQtd;
	}
	else
	{
		// There's something else in the queue.  Walk it to find the qTD that
		// links to our first one, and replace its 'next' qTD pointers with
		// the 'next' pointers from our last qTD (which might be
		// NULL/terminating)

		kernelDebug(debug_usb, "EHCI unlink from chained qTDs");

		qtdItem = transQueue->queueHeadItem->firstQtdItem;
		while (qtdItem && (qtdItem->nextQtdItem != transQueue->qtdItems[0]))
			qtdItem = qtdItem->nextQtdItem;

		if (!qtdItem)
		{
			// Not found!
			kernelError(kernel_error, "Transaction to de-queue was not found");
			return (status = ERR_NOSUCHENTRY);
		}

		qtdItem->nextQtdItem =
			transQueue->qtdItems[transQueue->numQtds - 1]->nextQtdItem;
		qtdItem->qtd->nextQtd =
			transQueue->qtdItems[transQueue->numQtds - 1]->qtd->nextQtd;
	}

	// Return success
	return (status = 0);
}


static ehciQueueHeadItem *allocIntrQueueHead(usbController *controller,
	usbDevice *usbDev, unsigned char endpoint)
{
	// Allocate a queue head for the periodic queue, insert it, and make
	// sure the controller is processing them.

	usbEhciData *ehci = controller->data;
	ehciQueueHeadItem *queueHeadItem = NULL;

	queueHeadItem = allocQueueHead(controller, usbDev, endpoint);
	if (!queueHeadItem)
	{
		kernelError(kernel_error, "Couldn't allocate interrupt queue head");
		goto err_out;
	}

	kernelDebug(debug_usb, "EHCI inserting queue head into device head list");

	queueHeadItem->devNext = ehci->devHeads;
	ehci->devHeads = queueHeadItem;

	// If the periodic schedule is not running, start it now
	if (!(ehci->opRegs->stat & EHCI_STAT_PERIODICSCHED))
	{
		if (startStopSched(ehci, EHCI_STAT_PERIODICSCHED,
			EHCI_CMD_PERSCHEDENBL, 1) < 0)
		goto err_out;
	}

	// Return success
	return (queueHeadItem);

err_out:
	if (queueHeadItem)
		releaseQueueHead(ehci, queueHeadItem);

	return (queueHeadItem = NULL);
}


static int unlinkSyncQueueHead(usbEhciData *ehci,
	ehciQueueHeadItem *unlinkQueueHeadItem)
{
	// Search the controller's list of synchronous queue heads for any that
	// link to the one supplied, and unlink them.

	int status = 0;
	ehciQueueHeadItem *queueHeadItem = NULL;
	int count;

	kernelDebug(debug_usb, "EHCI unlink sync queue head");

	// Try searching for a queue head that links to the one we're removing
	for (count = 0; count < EHCI_NUM_INTQUEUEHEADS; count ++)
	{
		queueHeadItem = ehci->intQueue[count];

		while (queueHeadItem)
		{
			if (queueHeadItem->listNext == unlinkQueueHeadItem)
			{
				// This one links to the one we're removing
				kernelDebug(debug_usb, "EHCI found linking queue head");

				// Replace the links with whatever the one we're removing
				// points to
				queueHeadItem->listNext = unlinkQueueHeadItem->listNext;
				queueHeadItem->queueHead->horizLink =
					unlinkQueueHeadItem->queueHead->horizLink;

				// Finished
				return (status = 0);
			}

			queueHeadItem = queueHeadItem->listNext;
		}
	}

	// Not found
	return (status = ERR_NOSUCHENTRY);
}


static void unregisterInterrupt(usbEhciData *ehci, ehciIntrReg *intrReg)
{
	// Remove an interrupt registration from the controller's list

	int status = 0;

	kernelLinkedListRemove(&ehci->intrRegs, intrReg);

	if (intrReg->transQueue.qtdItems)
	{
		// De-queue the qTDs from the queue head
		dequeueTransaction(&intrReg->transQueue);

		// Release the qTDs
		releaseQtds(ehci, intrReg->transQueue.qtdItems,
			intrReg->transQueue.numQtds);
	}

	// Remove the queue head from the periodic schedule.

	// Unlink it from any others that point to it.
	status = unlinkSyncQueueHead(ehci, intrReg->transQueue.queueHeadItem);
	if (status < 0)
		return;

	// Release the queue head
	status = releaseQueueHead(ehci, intrReg->transQueue.queueHeadItem);
	if (status < 0)
		return;

	kernelDebug(debug_usb, "EHCI interrupt registration for device %p "
		"classCode=0x%02x removed", intrReg->usbDev,
		intrReg->usbDev->classCode);

	// Free the memory
	kernelFree(intrReg);

	return;
}


static int unlinkAsyncQueueHead(usbEhciData *ehci,
	ehciQueueHeadItem *unlinkQueueHeadItem)
{
	// Search the controller's list of asynchronous queue heads for any that
	// link to the one supplied, and unlink them.

	int status = 0;
	ehciQueueHeadItem *queueHeadItem = NULL;

	kernelDebug(debug_usb, "EHCI unlink async queue head");

	// Try searching for a queue head that links to the one we're removing
	queueHeadItem = ehci->asyncHeads;
	while (queueHeadItem)
	{
		if (queueHeadItem->listNext == unlinkQueueHeadItem)
		{
			// This one links to the one we're removing
			kernelDebug(debug_usb, "EHCI found linking queue head");

			// Replace the links with whatever the one we're removing points to
			queueHeadItem->listNext = unlinkQueueHeadItem->listNext;
			queueHeadItem->queueHead->horizLink =
				unlinkQueueHeadItem->queueHead->horizLink;

			// Finished
			return (status = 0);
		}

		queueHeadItem = queueHeadItem->listNext;
	}

	// Not found
	return (status = ERR_NOSUCHENTRY);
}


static int removeAsyncQueueHead(usbEhciData *ehci,
	ehciQueueHeadItem *queueHeadItem)
{
	// Remove the queue head from the asynchronous queue, and release it.

	int status = 0;
	int count;

	// Unlink the queue head from the queue
	status = unlinkAsyncQueueHead(ehci, queueHeadItem);
	if (status < 0)
		return (status);

	// Now we set the 'interrupt on async advance doorbell' but in the command
	// register
	ehci->opRegs->cmd |= EHCI_CMD_INTASYNCADVRST;

	// Wait for the controller to set the 'interrupt on async advance' bit
	// in the status register
	kernelDebug(debug_usb, "EHCI wait for async advance");
	for (count = 0; count < 20; count ++)
	{
		if (ehci->opRegs->stat & EHCI_STAT_ASYNCADVANCE)
		{
			kernelDebug(debug_usb, "EHCI async advance took %dms", count);
			break;
		}

		kernelCpuSpinMs(1);
	}

	// Did the controller respond?
	if (!(ehci->opRegs->stat & EHCI_STAT_ASYNCADVANCE))
	{
		kernelError(kernel_error, "Controller did not set async advance bit");
		return (status = ERR_TIMEOUT);
	}

	// Clear it
	setStatusBits(ehci, EHCI_STAT_ASYNCADVANCE);

	status = releaseQueueHead(ehci, queueHeadItem);
	if (status < 0)
		return (status);

	return (status = 0);
}


static void removeDevQueueHead(usbEhciData *ehci,
	ehciQueueHeadItem *queueHeadItem)
{
	// Remove the queue head from the list of 'dev' queue heads

	ehciQueueHeadItem *devQueueHeadItem = NULL;

	if (queueHeadItem == ehci->devHeads)
	{
		ehci->devHeads = queueHeadItem->devNext;
	}
	else
	{
		devQueueHeadItem = ehci->devHeads;
		while (devQueueHeadItem)
		{
			if (devQueueHeadItem->devNext == queueHeadItem)
			{
				devQueueHeadItem->devNext = queueHeadItem->devNext;
				break;
			}
		}
	}
}


static int portReset(usbEhciData *ehci, int portNum)
{
	// Reset the port, with the appropriate delays, etc.

	int status = 0;
	int count;

	kernelDebug(debug_usb, "EHCI port reset");

	// Clear the port 'enabled' bit
	clearPortStatusBits(ehci, portNum, EHCI_PORTSC_PORTENABLED);

	// Wait for it to read as clear
	for (count = 0; count < 20; count ++)
	{
		if (!(ehci->opRegs->portsc[portNum] & EHCI_PORTSC_PORTENABLED))
		{
			kernelDebug(debug_usb, "EHCI disabling port took %dms", count);
			break;
		}

		kernelCpuSpinMs(1);
	}

	// Clear?
	if (ehci->opRegs->portsc[portNum] & EHCI_PORTSC_PORTENABLED)
	{
		kernelError(kernel_warn, "Couldn't clear port enabled bit");
		status = ERR_TIMEOUT;
		goto out;
	}

	// Set the port 'reset' bit
	setPortStatusBits(ehci, portNum, EHCI_PORTSC_PORTRESET);

	// Wait for it to read as set
	for (count = 0; count < 20; count ++)
	{
		if (ehci->opRegs->portsc[portNum] & EHCI_PORTSC_PORTRESET)
		{
			kernelDebug(debug_usb, "EHCI setting reset bit took %dms", count);
			break;
		}

		kernelCpuSpinMs(1);
	}

	// Set?
	if (!(ehci->opRegs->portsc[portNum] & EHCI_PORTSC_PORTRESET))
	{
		kernelError(kernel_warn, "Couldn't set port reset bit");
		status = ERR_TIMEOUT;
		goto out;
	}

	// Delay 50ms
	kernelDebug(debug_usb, "EHCI delay for port reset");
	kernelCpuSpinMs(50);

	// Clear the port 'reset' bit
	clearPortStatusBits(ehci, portNum, EHCI_PORTSC_PORTRESET);

	// Wait for it to read as clear
	for (count = 0; count < 200; count ++)
	{
		if (!(ehci->opRegs->portsc[portNum] & EHCI_PORTSC_PORTRESET))
		{
			kernelDebug(debug_usb, "EHCI clearing reset bit took %dms", count);
			break;
		}

		kernelCpuSpinMs(1);
	}

	// Clear?
	if (ehci->opRegs->portsc[portNum] & EHCI_PORTSC_PORTRESET)
	{
		kernelError(kernel_warn, "Couldn't clear port reset bit");
		status = ERR_TIMEOUT;
		goto out;
	}

	// Delay another 20ms
	kernelDebug(debug_usb, "EHCI delay after port reset");
	kernelCpuSpinMs(20);

	// Return success
	status = 0;

out:
	kernelDebug(debug_usb, "EHCI port reset %s",
		(status? "failed" : "success"));

	return (status);
}


static int portConnected(usbController *controller, int portNum, int hotPlug)
{
	// This function is called whenever we notice that a port has indicated
	// a new connection.

	int status = 0;
	usbEhciData *ehci = controller->data;
	usbDevSpeed speed = usbspeed_unknown;

	kernelDebug(debug_usb, "EHCI controller %d, port %d connected",
		controller->num, portNum);

	debugPortStatus(controller, portNum);

	if (ehci->opRegs->portsc[portNum] & EHCI_PORTSC_CONNCHANGE)
		// Acknowledge connection status change
		setPortStatusBits(ehci, portNum, EHCI_PORTSC_CONNCHANGE);

	debugPortStatus(controller, portNum);

	// Check the line status bits to see whether this is a low-speed device
	if (((ehci->opRegs->portsc[portNum] & EHCI_PORTSC_LINESTATUS) ==
			EHCI_PORTSC_LINESTAT_LS) &&
		(ehci->capRegs->hcsparams & EHCI_HCSP_NUMCOMPANIONS))
	{
		speed = usbspeed_low;

		// Release ownership of the port.
		kernelDebug(debug_usb, "EHCI low-speed connection.  Releasing port "
			"ownership");
		setPortStatusBits(ehci, portNum, EHCI_PORTSC_PORTOWNER);
		debugPortStatus(controller, portNum);
	}
	else
	{
		// Reset the port
		status = portReset(ehci, portNum);
		if (status < 0)
			return (status);

		debugPortStatus(controller, portNum);

		// Is the port showing as enabled?
		if (ehci->opRegs->portsc[portNum] & EHCI_PORTSC_PORTENABLED)
		{
			if (ehci->opRegs->portsc[portNum] & EHCI_PORTSC_PORTENBLCHG)
				// Acknowledge enabled status change
				setPortStatusBits(ehci, portNum, EHCI_PORTSC_PORTENBLCHG);

			speed = usbspeed_high;
		}
		else
		{
			// Release ownership?
			if (ehci->capRegs->hcsparams & EHCI_HCSP_NUMCOMPANIONS)
			{
				kernelDebug(debug_usb, "EHCI full-speed connection.  "
					"Releasing port ownership");
				setPortStatusBits(ehci, portNum, EHCI_PORTSC_PORTOWNER);
				debugPortStatus(controller, portNum);
				return (status = 0);
			}

			speed = usbspeed_full;
		}

		kernelDebug(debug_usb, "EHCI connection speed: %s",
			usbDevSpeed2String(speed));

		status = kernelUsbDevConnect(controller, &controller->hub, portNum,
			speed, hotPlug);
		if (status < 0)
		{
			kernelError(kernel_error, "Error enumerating new USB device");
			return (status);
		}
	}

	debugPortStatus(controller, portNum);

	return (status = 0);
}


static void portDisconnected(usbController *controller, int portNum)
{
	usbEhciData *ehci = controller->data;

	kernelDebug(debug_usb, "EHCI controller %d, port %d disconnected",
		controller->num, portNum);

	debugPortStatus(controller, portNum);

	if (ehci->opRegs->portsc[portNum] & EHCI_PORTSC_PORTENBLCHG)
		// Acknowledge enabled status change
		setPortStatusBits(ehci, portNum, EHCI_PORTSC_PORTENBLCHG);

	if (ehci->opRegs->portsc[portNum] & EHCI_PORTSC_CONNCHANGE)
		// Acknowledge connection status change
		setPortStatusBits(ehci, portNum, EHCI_PORTSC_CONNCHANGE);

	debugPortStatus(controller, portNum);

	// Tell the USB functions that the device disconnected.  This will call us
	// back to tell us about all affected devices - there might be lots if
	// this was a hub
	kernelUsbDevDisconnect(controller, &controller->hub, portNum);

	return;
}


static void doDetectDevices(usbHub *hub, int hotplug)
{
	// This function gets called to check for device connections (either cold-
	// plugged ones at boot time, or hot-plugged ones during operations.

	usbController *controller = hub->controller;
	usbEhciData *ehci = controller->data;
	int count;

	// Check to see whether any of the ports are showing a connection change
	for (count = 0; count < ehci->numPorts; count ++)
	{
		if (ehci->opRegs->portsc[count] & EHCI_PORTSC_CONNCHANGE)
		{
			kernelDebug(debug_usb, "EHCI port %d connection changed", count);

			if (ehci->opRegs->portsc[count] & EHCI_PORTSC_CONNECTED)
				portConnected(controller, count, hotplug);
			else
				portDisconnected(controller, count);
		}
	}

	return;
}


static int handoff(usbEhciData *ehci, kernelBusTarget *busTarget,
	pciDeviceInfo *pciDevInfo)
{
	// If the controller supports the extended capability for legacy handoff
	// synchronization between the BIOS and the OS, do that here.

	int status = 0;
	unsigned eecp = 0;
	ehciExtendedCaps *extCap = NULL;
	ehciLegacySupport *legSupp = NULL;
	int count;

	kernelDebug(debug_usb, "EHCI try BIOS-to-OS handoff");

	eecp = ((ehci->capRegs->hccparams & EHCI_HCCP_EXTCAPPTR) >> 8);
	if (eecp)
	{
		kernelDebug(debug_usb, "EHCI has extended capabilities");

		extCap = (ehciExtendedCaps *)((void *) pciDevInfo->header + eecp);
		while (1)
		{
			kernelDebug(debug_usb, "EHCI extended capability %d", extCap->id);
			if (extCap->id == EHCI_EXTCAP_HANDOFFSYNC)
			{
				kernelDebug(debug_usb, "EHCI legacy support implemented");

				legSupp = (ehciLegacySupport *) extCap;

				// Does the BIOS claim ownership of the controller?
				if (legSupp->legSuppCap & EHCI_LEGSUPCAP_BIOSOWND)
					kernelDebug(debug_usb, "EHCI BIOS claims ownership, "
						"contStat=0x%08x", legSupp->legSuppContStat);
				else
					kernelDebug(debug_usb, "EHCI BIOS does not claim ownership");

				// Attempt to take over ownership; write the 'OS-owned' flag,
				// and wait for the BIOS to release ownership, if applicable
				for (count = 0; count < 200; count ++)
				{
					legSupp->legSuppCap |= EHCI_LEGSUPCAP_OSOWNED;
					kernelBusWriteRegister(busTarget,
						((eecp +
							offsetof(ehciLegacySupport, legSuppCap)) >> 2),
						32, legSupp->legSuppCap);

					// Re-read target info
					kernelBusGetTargetInfo(busTarget, pciDevInfo);

					if ((legSupp->legSuppCap & EHCI_LEGSUPCAP_OSOWNED) &&
						!(legSupp->legSuppCap & EHCI_LEGSUPCAP_BIOSOWND))
					{
						kernelDebug(debug_usb, "EHCI OS ownership took %dms",
							count);
						break;
					}

					kernelDebug(debug_usb, "EHCI legSuppCap=0x%08x",
						legSupp->legSuppCap);

					kernelCpuSpinMs(1);
				}

				// Do we have ownership?
				if (!(legSupp->legSuppCap & EHCI_LEGSUPCAP_OSOWNED) ||
					(legSupp->legSuppCap & EHCI_LEGSUPCAP_BIOSOWND))
				{
					kernelError(kernel_error, "BIOS did not release ownership");
				}

				// Make sure any SMIs are acknowledged and disabled
				legSupp->legSuppContStat = EHCI_LEGSUPCONT_SMIRWC;
				kernelBusWriteRegister(busTarget,
					((eecp +
						offsetof(ehciLegacySupport, legSuppContStat)) >> 2),
					32, legSupp->legSuppContStat);

				// Re-read target info
				kernelBusGetTargetInfo(busTarget, pciDevInfo);

				kernelDebug(debug_usb, "EHCI contStat now=0x%08x",
					legSupp->legSuppContStat);
			}

			if (extCap->next)
			{
				eecp = extCap->next;
				extCap = (ehciExtendedCaps *)
					((void *) pciDevInfo->header + eecp);
			}
			else
				break;
		}
	}
	else
		kernelDebug(debug_usb, "EHCI has no extended capabilities");

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
	// Do complete USB (controller and bus) reset

	int status = 0;
	usbEhciData *ehci = NULL;
	int count;

	// Check params
	if (!controller)
	{
		kernelError(kernel_error, "NULL parameter");
		return (status = ERR_NULLPARAMETER);
	}

	ehci = controller->data;

	// Make sure the controller is stopped
	status = startStop(ehci, 0);
	if (status < 0)
		return (status);

	kernelDebug(debug_usb, "EHCI reset controller");

	// Set host controller reset
	ehci->opRegs->cmd |= EHCI_CMD_HCRESET;

	// Wait until the host controller clears it
	for (count = 0; count < 2000; count ++)
	{
		if (!(ehci->opRegs->cmd & EHCI_CMD_HCRESET))
		{
			kernelDebug(debug_usb, "EHCI resetting controller took %dms",
				count);
			break;
		}

		kernelCpuSpinMs(1);
	}

	// Clear?
	if (ehci->opRegs->cmd & EHCI_CMD_HCRESET)
	{
		kernelError(kernel_error, "Controller did not clear reset bit");
		status = ERR_TIMEOUT;
	}
	else
	{
		// Clear the lock
		memset((void *) &controller->lock, 0, sizeof(lock));
		status = 0;
	}

	kernelDebug(debug_usb, "EHCI controller reset %s",
		(status? "failed" : "successful"));

	return (status);
}


static int interrupt(usbController *controller)
{
	// This function gets called when the controller issues an interrupt.

	int status = 0;
	usbEhciData *ehci = controller->data;
	ehciIntrReg *intrReg = NULL;
	kernelLinkedListItem *iter = NULL;
	ehciQueueHeadItem *queueHeadItem = NULL;
	ehciQtdItem *qtdItem = NULL;
	unsigned bytes = 0;
	unsigned char dataToggle = 0;

	// See whether the status register indicates any of the interrupts we
	// enabled
	if (!(ehci->opRegs->stat & ehci->opRegs->intr))
	{
		//kernelDebug(debug_usb, "EHCI no interrupt from controller %d",
		//	controller->num);
		return (status = ERR_NODATA);
	}

	if (ehci->opRegs->stat & EHCI_STAT_HOSTSYSERROR)
	{
		kernelError(kernel_error, "USB host system error, controller %d",
			controller->num);

		debugOpRegs(ehci);

		// Try to get the controller running again
		hostSystemError(controller);
	}

	if (ehci->opRegs->stat & EHCI_STAT_USBERRORINT)
	{
		kernelDebug(debug_usb, "EHCI error interrupt, controller %d",
			controller->num);

		debugOpRegs(ehci);
	}

	if (ehci->opRegs->stat & EHCI_STAT_USBINTERRUPT)
	{
		//kernelDebug(debug_usb, "EHCI data interrupt, controller %d",
		//	controller->num);

		// Loop through the registered interrupts for ones that are no longer
		// active.
		intrReg = kernelLinkedListIterStart(&ehci->intrRegs, &iter);
		while (intrReg)
		{
			//kernelDebug(debug_usb, "EHCI check interrupt QTD for device %d, "
			//	"endpoint 0x%02x", intrReg->usbDev->address,
			//	intrReg->endpoint);

			queueHeadItem = intrReg->transQueue.queueHeadItem;
			qtdItem = intrReg->transQueue.qtdItems[0];

			// If the QTD is no longer active, there might be some data there
			// for us.
			if (!(qtdItem->qtd->token & EHCI_QTDTOKEN_ACTIVE))
			{
				//kernelDebug(debug_usb, "EHCI interrupt QTD processed for "
				//	"device %d, endpoint 0x%02x", intrReg->usbDev->address,
				//	intrReg->endpoint);

				// Temporarily 'disconnect' the qTD
				queueHeadItem->queueHead->overlay.nextQtd = EHCI_LINK_TERM;

				// If the interrupt caused an error, don't do the callback
				// and don't re-schedule it.
				if (qtdItem->qtd->token & EHCI_QTDTOKEN_ERROR)
				{
					kernelDebugError("Interrupt QTD token error 0x%02x",
						(qtdItem->qtd->token & EHCI_QTDTOKEN_ERROR));
					goto intr_error;
				}

				bytes = (intrReg->maxLen -
					((qtdItem->qtd->token & EHCI_QTDTOKEN_TOTBYTES) >> 16));

				// If there's data and a callback function, do the callback.
				if (bytes && intrReg->callback)
					intrReg->callback(intrReg->usbDev, intrReg->interface,
						qtdItem->buffer, bytes);

				// Get the data toggle
				dataToggle = ((qtdItem->qtd->token &
					EHCI_QTDTOKEN_DATATOGG) >> 31);

				// Reset the qTD
				status = setupQtdToken(qtdItem->qtd, &dataToggle,
					intrReg->maxLen, 1, USB_PID_IN);
				if (status < 0)
					goto intr_error;

				// Reset the buffer pointer
				qtdItem->qtd->buffPage[0] = intrReg->bufferPhysical;

				// Reconnect the qTD to the queue head
				queueHeadItem->queueHead->overlay.nextQtd = qtdItem->physical;
			}

			intrReg = kernelLinkedListIterNext(&ehci->intrRegs, &iter);
			continue;

		intr_error:
			kernelDebugError("Interrupt error - not re-scheduling");
			intrReg = kernelLinkedListIterNext(&ehci->intrRegs, &iter);
			continue;
		}

		kernelDebug(debug_usb, "EHCI data interrupt serviced");
	}

	// Clear the relevent interrupt bits
	setStatusBits(ehci, (ehci->opRegs->stat & ehci->opRegs->intr));

	return (status = 0);
}


static int queue(usbController *controller, usbDevice *usbDev,
	usbTransaction *trans, int numTrans)
{
	// This function contains the intelligence necessary to initiate a
	// transaction (all phases)

	int status = 0;
	ehciTransQueue *transQueues = NULL;
	int packetSize = 0;
	void *buffPtr = NULL;
	unsigned bytesToTransfer = 0;
	unsigned bufferPhysical = 0;
	unsigned doBytes = 0;
	volatile unsigned char *dataToggle = NULL;
	ehciQtdItem *setupQtdItem = NULL;
	usbDeviceRequest *req = NULL;
	ehciQtdItem **dataQtdItems = NULL;
	ehciQtdItem *statusQtdItem = NULL;
	unsigned timeout = 0;
	int transCount, qtdCount;

	kernelDebug(debug_usb, "EHCI queue %d transaction%s", numTrans,
		((numTrans > 1)? "s" : ""));

	// Check params
	if (!controller || !usbDev || !trans)
	{
		kernelError(kernel_error, "NULL parameter");
		return (status = ERR_NULLPARAMETER);
	}

	// Get memory for pointers to the transaction queues
	transQueues = kernelMalloc(numTrans * sizeof(ehciTransQueue));
	if (!transQueues)
		return (status = ERR_MEMORY);

	// Loop to set up each transaction
	for (transCount = 0; transCount < numTrans; transCount ++)
	{
		// Lock the controller
		status = kernelLockGet(&controller->lock);
		if (status < 0)
		{
			kernelError(kernel_error, "Can't get controller lock");
			goto out;
		}

		// Try to find an existing queue head for this transaction's endpoint
		transQueues[transCount].queueHeadItem =
			findQueueHead(controller, usbDev, trans[transCount].endpoint);

		// Found it?
		if (transQueues[transCount].queueHeadItem)
		{
			kernelDebug(debug_usb, "EHCI found existing queue head");

			// Update the "static endpoint state" in the queue head (in case
			// anything has changed, such as the device address)
			status = setQueueHeadEndpointState(usbDev,
				trans[transCount].endpoint,
				transQueues[transCount].queueHeadItem->queueHead);
			if (status < 0)
				goto out;
		}
		else
		{
			// We don't yet have a queue head for this endpoint.  Try to
			// allocate one
			transQueues[transCount].queueHeadItem =
				allocAsyncQueueHead(controller, usbDev,
					trans[transCount].endpoint);
			if (!transQueues[transCount].queueHeadItem)
			{
				kernelError(kernel_error, "Couldn't allocate endpoint queue "
					"head");
				status = ERR_NOSUCHENTRY;
				goto out;
			}
		}

		// We can get the maximum packet size for this endpoint from the queue
		// head (it will have been updated with the current device info upon
		// retrieval, above).
		packetSize = ((transQueues[transCount].queueHeadItem->queueHead
			->endpointChars & EHCI_QHDW1_MAXPACKETLEN) >> 16);

		// Figure out how many transfer descriptors we're going to need for
		// this transaction
		transQueues[transCount].numDataQtds = 0;
		transQueues[transCount].numQtds = 0;

		// Setup/status descriptors?
		if (trans[transCount].type == usbxfer_control)
			// At least one each for setup and status
			transQueues[transCount].numQtds += 2;

		// Data descriptors?
		if (trans[transCount].length)
		{
			buffPtr = trans[transCount].buffer;
			bytesToTransfer = trans[transCount].length;

			while (bytesToTransfer)
			{
				bufferPhysical = (unsigned)
					kernelPageGetPhysical((((unsigned) buffPtr <
						KERNEL_VIRTUAL_ADDRESS)?
							kernelCurrentProcess->processId :
								KERNELPROCID), buffPtr);

				doBytes = min(bytesToTransfer,
					(EHCI_MAX_QTD_DATA -
						(bufferPhysical % EHCI_MAX_QTD_BUFFERSIZE)));

				// Don't let packets cross qTD boundaries
				if ((doBytes < bytesToTransfer) && (doBytes % packetSize))
					doBytes -= (doBytes % packetSize);

				transQueues[transCount].numDataQtds += 1;
				bytesToTransfer -= doBytes;
				buffPtr += doBytes;
			}

			kernelDebug(debug_usb, "EHCI data payload of %u requires %d "
				"descriptors", trans[transCount].length,
				transQueues[transCount].numDataQtds);

			transQueues[transCount].numQtds +=
				transQueues[transCount].numDataQtds;
		}

		kernelDebug(debug_usb, "EHCI transaction requires %d descriptors",
			transQueues[transCount].numQtds);

		// Allocate the qTDs we need for this transaction
		transQueues[transCount].qtdItems = getQtds(controller->data,
			transQueues[transCount].numQtds);
		if (!transQueues[transCount].qtdItems)
		{
			kernelError(kernel_error, "Couldn't get qTDs for transaction");
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

		setupQtdItem = NULL;
		if (trans[transCount].type == usbxfer_control)
		{
			// Begin setting up the device request

			// Get the qTD for the setup phase
			setupQtdItem = transQueues[transCount].qtdItems[0];

			// Get a buffer for the device request memory
			status = allocQtdBuffer(setupQtdItem, sizeof(usbDeviceRequest));
			if (status < 0)
				goto out;

			req = setupQtdItem->buffer;

			status = kernelUsbSetupDeviceRequest(&trans[transCount], req);
			if (status < 0)
				goto out;

			// Data toggle is always 0 for the setup transfer
			*dataToggle = 0;

			// Setup the qTD for the setup phase
			status = setupQtdToken(setupQtdItem->qtd, dataToggle,
				sizeof(usbDeviceRequest), 0, USB_PID_SETUP);
			if (status < 0)
				goto out;

			// Data toggle
			*dataToggle ^= 1;
		}

		// If there is a data phase, set up the transfer descriptor(s) for the
		// data phase
		if (trans[transCount].length)
		{
			buffPtr = trans[transCount].buffer;
			bytesToTransfer = trans[transCount].length;

			dataQtdItems = &transQueues[transCount].qtdItems[0];
			if (setupQtdItem)
				dataQtdItems = &transQueues[transCount].qtdItems[1];

			for (qtdCount = 0; qtdCount < transQueues[transCount].numDataQtds;
				qtdCount ++)
			{
				bufferPhysical = (unsigned)
					kernelPageGetPhysical((((unsigned) buffPtr <
						KERNEL_VIRTUAL_ADDRESS)?
							kernelCurrentProcess->processId :
								KERNELPROCID), buffPtr);
				if (!bufferPhysical)
				{
					kernelDebugError("Can't get physical address for buffer "
						"fragment at %p", buffPtr);
					status = ERR_MEMORY;
					goto out;
				}

				doBytes = min(bytesToTransfer,
					(EHCI_MAX_QTD_DATA -
						(bufferPhysical % EHCI_MAX_QTD_BUFFERSIZE)));

				// Don't let packets cross qTD boundaries
				if ((doBytes < bytesToTransfer) && (doBytes % packetSize))
					doBytes -= (doBytes % packetSize);

				kernelDebug(debug_usb, "EHCI bytesToTransfer=%u, doBytes=%u",
					bytesToTransfer, doBytes);

				// Set the qTD's buffer pointers to the relevent portions of
				// the transaction buffer.
				status = setQtdBufferPages(dataQtdItems[qtdCount]->qtd,
					bufferPhysical, doBytes);
				if (status < 0)
					goto out;

				// Set up the data qTD
				status = setupQtdToken(dataQtdItems[qtdCount]->qtd, dataToggle,
					doBytes, 0, trans[transCount].pid);
				if (status < 0)
					goto out;

				// If the qTD generated an odd number of packets, toggle the
				// data toggle.
				if (((doBytes + (packetSize - 1)) / packetSize) % 2)
					*dataToggle ^= 1;

				buffPtr += doBytes;
				bytesToTransfer -= doBytes;
			}
		}

		if (trans[transCount].type == usbxfer_control)
		{
			// Setup the transfer descriptor for the status phase

			statusQtdItem = transQueues[transCount]
				.qtdItems[transQueues[transCount].numQtds - 1];

			// Data toggle is always 1 for the status transfer
			*dataToggle = 1;

			// Setup the status packet
			status = setupQtdToken(statusQtdItem->qtd, dataToggle, 0, 0,
				((trans[transCount].pid == USB_PID_OUT)?
					USB_PID_IN : USB_PID_OUT));
			if (status < 0)
				goto out;
		}

		// Link the qTDs into the queue via the queue head
		status = queueTransaction(&transQueues[transCount]);
		if (status < 0)
			goto out;

		// Release the controller lock to process the transaction
		kernelLockRelease(&controller->lock);

		timeout = trans[transCount].timeout;
		if (!timeout)
			timeout = USB_STD_TIMEOUT_MS;

		// Process the transaction
		status = runTransaction(&transQueues[transCount], timeout);

		// Record the actual number of bytes transferred
		trans[transCount].bytes =
			(trans[transCount].length- transQueues[transCount].bytesRemaining);

		if (status < 0)
			goto out;
	}

out:
	// If the call to runTransaction() returned an error, the controller lock
	// won't be currently held.
	if (kernelLockVerify(&controller->lock) <= 0)
	{
		if (kernelLockGet(&controller->lock) < 0)
			kernelError(kernel_error, "Can't get controller lock");
	}

	if (kernelLockVerify(&controller->lock) > 0)
	{
		for (transCount = 0; transCount < numTrans; transCount ++)
		{
			if (transQueues[transCount].qtdItems)
			{
				// De-queue the qTDs from the queue head
				dequeueTransaction(&transQueues[transCount]);

				// Release the qTDs
				releaseQtds(controller->data, transQueues[transCount].qtdItems,
					transQueues[transCount].numQtds);
			}
		}

		kernelFree(transQueues);
		kernelLockRelease(&controller->lock);
	}
	else
	{
		kernelError(kernel_error, "Don't have controller lock");
	}

	return (status);
}


static int schedInterrupt(usbController *controller, usbDevice *usbDev,
	int interface, unsigned char endpoint, int interval, unsigned maxLen,
	void (*callback)(usbDevice *, int, void *, unsigned))
{
	// This function is used to schedule an interrupt.

	int status = 0;
	usbEhciData *ehci = NULL;
	ehciIntrReg *intrReg = NULL;
	unsigned char cMask = 0xFE;
	unsigned char sMask = 0x01;
	ehciQueueHeadItem *intQueue = NULL;
	int count;

	// Check params
	if (!controller || !usbDev || !callback)
	{
		kernelError(kernel_error, "NULL parameter");
		status = ERR_NULLPARAMETER;
		goto out;
	}

	kernelDebug(debug_usb, "EHCI schedule interrupt for address %d endpoint "
		"0x%02x len %u", usbDev->address, endpoint, maxLen);

	// Lock the controller
	status = kernelLockGet(&controller->lock);
	if (status < 0)
	{
		kernelError(kernel_error, "Can't get controller lock");
		return (status);
	}

	ehci = controller->data;

	// Get memory to hold info about the interrupt
	intrReg = kernelMalloc(sizeof(ehciIntrReg));
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

	// Get a queue head for the interrupt endpoint.
	intrReg->transQueue.queueHeadItem = allocIntrQueueHead(controller, usbDev,
		endpoint);
	if (!intrReg->transQueue.queueHeadItem)
	{
		kernelError(kernel_error, "Couldn't retrieve endpoint queue head");
		status = ERR_BUG;
		goto out;
	}

	// Get a qTD for it.
	intrReg->transQueue.numQtds = intrReg->transQueue.numDataQtds = 1;
	intrReg->transQueue.qtdItems = getQtds(ehci, 1);
	if (!intrReg->transQueue.qtdItems)
	{
		kernelError(kernel_error, "Couldn't get qTD for interrupt");
		status = ERR_BUG;
		goto out;
	}

	// Get the buffer for the qTD
	status = allocQtdBuffer(intrReg->transQueue.qtdItems[0], maxLen);
	if (status < 0)
		goto out;

	intrReg->bufferPhysical = (unsigned) kernelPageGetPhysical(KERNELPROCID,
		intrReg->transQueue.qtdItems[0]->buffer);
	if (!intrReg->bufferPhysical)
	{
		status = ERR_MEMORY;
		goto out;
	}

	// Set up the qTD
	status = setupQtdToken(intrReg->transQueue.qtdItems[0]->qtd, NULL,
		intrReg->maxLen, 1 /* interrupt */, USB_PID_IN);
	if (status < 0)
		goto out;

	// Enqueue the qTD onto the queue head
	status = queueTransaction(&intrReg->transQueue);
	if (status < 0)
		goto out;

	// Add the interrupt registration to the controller's list.
	status = kernelLinkedListAdd(&ehci->intrRegs, intrReg);
	if (status < 0)
		goto out;

	if (usbDev->speed != usbspeed_high)
	{
		// For now, set all bits in the split completion mask, and leave the
		// interrupt schedule mask empty (its value will depend on the
		// interval)
		intrReg->transQueue.queueHeadItem->queueHead->endpointCaps |=
			((cMask << 8) & EHCI_QHDW2_SPLTCOMPMASK);
	}

	// Interpret the interval value.  Expressed in frames or microframes
	// depending on the device operating speed (i.e., either 1 millisecond or
	// 125 us units).  For full- or low-speed interrupt endpoints, the value of
	// this field may be from 1 to 255.  For high-speed interrupt endpoints,
	// interval is used as the exponent for a 2^(interval - 1) value;
	// e.g., an interval of 4 means a period of 8 (2^(4 - 1)).  This value
	// must be from 1 to 16.
	if (usbDev->speed == usbspeed_high)
	{
		// Get the interval in microframes
		intrReg->interval = (1 << (interval - 1));

		// Set the interrupt mask in the queue head
		if (intrReg->interval < 8)
		{
			for (count = 1; count < 8; count ++)
				if (!(count % (intrReg->interval & 0x7)))
					sMask |= (1 << count);

			intrReg->interval = 1;
		}
		else
		{
			sMask = 0x01;
			intrReg->interval >>= 3;
		}
	}

	kernelDebug(debug_usb, "EHCI interrupt interval at %d frames, "
		"s-mask=0x%02x", intrReg->interval, sMask);

	intrReg->transQueue.queueHeadItem->queueHead->endpointCaps |=
		(sMask & EHCI_QHDW2_INTSCHEDMASK);

	// Insert it into the periodic schedule

	// See which interrupt queue head we should link to

	if (!interval)
		interval = 1;

	for (count = 0; ((count < 9) && ((1 << count) < (int) interval));
		count ++)
	{
		// empty
	}

	kernelDebug(debug_usb, "EHCI linking to interrupt queue head in slot %d "
		"(interval %d)", (EHCI_NUM_INTQUEUEHEADS - (count + 1)), (1 << count));

	intQueue = ehci->intQueue[EHCI_NUM_INTQUEUEHEADS - (count + 1)];

	intrReg->transQueue.queueHeadItem->listNext = intQueue->listNext;
	intrReg->transQueue.queueHeadItem->queueHead->horizLink =
		intQueue->queueHead->horizLink;
	intQueue->listNext = intrReg->transQueue.queueHeadItem;
	intQueue->queueHead->horizLink =
		(intrReg->transQueue.queueHeadItem->physical | EHCI_LINKTYP_QH);

	status = 0;

out:
	if (status < 0)
	{
		if (intrReg)
			unregisterInterrupt(ehci, intrReg);
	}

	kernelLockRelease(&controller->lock);
	return (status);
}


static int deviceRemoved(usbController *controller, usbDevice *usbDev)
{
	int status = 0;
	usbEhciData *ehci = NULL;
	ehciQueueHeadItem *queueHeadItem = NULL;
	kernelLinkedListItem *iter = NULL;
	ehciIntrReg *intrReg = NULL;
	int count;

	// Check params
	if (!controller || !usbDev)
	{
		kernelError(kernel_error, "NULL parameter");
		return (status = ERR_NULLPARAMETER);
	}

	kernelDebug(debug_usb, "EHCI device %d removed", usbDev->address);

	// Lock the controller
	status = kernelLockGet(&controller->lock);
	if (status < 0)
	{
		kernelError(kernel_error, "Can't get controller lock");
		return (status);
	}

	ehci = controller->data;

	// Remove any interrupt registrations for the device
	intrReg = kernelLinkedListIterStart(&ehci->intrRegs, &iter);
	while (intrReg)
	{
		if (intrReg->usbDev != usbDev)
		{
			intrReg = kernelLinkedListIterNext(&ehci->intrRegs, &iter);
			continue;
		}

		unregisterInterrupt(ehci, intrReg);
		intrReg = kernelLinkedListIterStart(&ehci->intrRegs, &iter);
	}

	for (count = 0; count < usbDev->numEndpoints; count ++)
	{
		queueHeadItem =	findQueueHead(controller, usbDev,
			usbDev->endpoint[count]->number);
		if (queueHeadItem)
		{
			switch (usbDev->endpoint[count]->attributes & USB_ENDP_ATTR_MASK)
			{
				case USB_ENDP_ATTR_CONTROL:
				case USB_ENDP_ATTR_BULK:
				{
					// Remove any queue heads belonging to this device's
					// endpoints from the asynchronous queue
					removeAsyncQueueHead(ehci, queueHeadItem);
					break;
				}

				default:
					break;
			}

			// Remove the queue head from the list of 'dev' queue heads
			removeDevQueueHead(ehci, queueHeadItem);
		}
	}

	kernelLockRelease(&controller->lock);
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

	kernelDebug(debug_usb, "EHCI initial device detection, hotplug=%d",
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

	usbController *controller = NULL;
	usbEhciData *ehci = NULL;

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

	controller = hub->controller;
	if (!controller)
	{
		kernelError(kernel_error, "Hub controller is NULL");
		return;
	}

	ehci = controller->data;

	if (ehci->opRegs->stat & EHCI_STAT_PORTCHANGE)
	{
		doDetectDevices(hub, 1 /* hotplug */);

		// Clear the port change bit
		setStatusBits(ehci, EHCI_STAT_PORTCHANGE);
	}
}


/////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////
//
//  Below here, the functions are exported for external use
//
/////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////

kernelDevice *kernelUsbEhciDetect(kernelBusTarget *busTarget,
	kernelDriver *driver)
{
	// This routine is used to detect and initialize a potential EHCI USB
	// device, as well as registering it with the higher-level interfaces.

	int status = 0;
	pciDeviceInfo pciDevInfo;
	usbController *controller = NULL;
	usbEhciData *ehci = NULL;
	unsigned physMemSpace;
	unsigned memSpaceSize;
	kernelDevice *dev = NULL;
	char value[32];
	int count;

	// Get the PCI device header
	status = kernelBusGetTargetInfo(busTarget, &pciDevInfo);
	if (status < 0)
		goto err_out;

	// Make sure it's a non-bridge header
	if ((pciDevInfo.device.headerType & ~PCI_HEADERTYPE_MULTIFUNC) !=
		PCI_HEADERTYPE_NORMAL)
	{
		kernelDebugError("PCI headertype not 'normal' (0x%02x)",
			(pciDevInfo.device.headerType & ~PCI_HEADERTYPE_MULTIFUNC));
		goto err_out;
	}

	// Make sure it's an EHCI controller (programming interface is 0x20 in
	// the PCI header)
	if (pciDevInfo.device.progIF != EHCI_PCI_PROGIF)
		goto err_out;

	// After this point, we believe we have a supported device.

	kernelDebug(debug_usb, "EHCI controller found");

	// Try to enable bus mastering
	if (!(pciDevInfo.device.commandReg & PCI_COMMAND_MASTERENABLE))
	{
		kernelBusSetMaster(busTarget, 1);

		// Re-read target info
		status = kernelBusGetTargetInfo(busTarget, &pciDevInfo);
		if (status < 0)
			goto err_out;

		if (!(pciDevInfo.device.commandReg & PCI_COMMAND_MASTERENABLE))
			kernelDebugError("Couldn't enable bus mastering");
		else
			kernelDebug(debug_usb, "EHCI bus mastering enabled in PCI");
	}
	else
	{
		kernelDebug(debug_usb, "EHCI bus mastering already enabled");
	}

	// Make sure the BAR refers to a memory decoder
	if (pciDevInfo.device.nonBridge.baseAddress[0] & 0x1)
	{
		kernelDebugError("ABAR is not a memory decoder");
		goto err_out;
	}

	// Allocate memory for the controller
	controller = kernelMalloc(sizeof(usbController));
	if (!controller)
		goto err_out;

	// Set the controller type
	controller->type = usb_ehci;

	// Get the USB version number
	controller->usbVersion = kernelBusReadRegister(busTarget, 0x60, 8);

	// Get the interrupt number.
	controller->interruptNum = pciDevInfo.device.nonBridge.interruptLine;

	kernelLog("USB: EHCI controller USB %d.%d interrupt %d",
		((controller->usbVersion & 0xF0) >> 4),
		(controller->usbVersion & 0xF), controller->interruptNum);

	// Allocate memory for the EHCI data
	controller->data = kernelMalloc(sizeof(usbEhciData));
	if (!controller->data)
		goto err_out;

	ehci = controller->data;

	// Get the memory range address
	physMemSpace = (pciDevInfo.device.nonBridge.baseAddress[0] & 0xFFFFFFF0);

	if (pciDevInfo.device.nonBridge.baseAddress[0] & 0x6)
	{
		kernelError(kernel_error, "Register memory must be mappable in "
			"32-bit address space");
		goto err_out;
	}

	// Determine the memory space size.  Write all 1s to the register.
	kernelBusWriteRegister(busTarget, PCI_CONFREG_BASEADDRESS0_32, 32,
		0xFFFFFFFF);

	memSpaceSize = (~(kernelBusReadRegister(busTarget,
		PCI_CONFREG_BASEADDRESS0_32, 32) & ~0xF) + 1);

	// Restore the register we clobbered.
	kernelBusWriteRegister(busTarget, PCI_CONFREG_BASEADDRESS0_32, 32,
		pciDevInfo.device.nonBridge.baseAddress[0]);

	// Map the physical memory address of the controller's registers into
	// our virtual address space.

	// Map the physical memory space pointed to by the decoder.
	status = kernelPageMapToFree(KERNELPROCID, physMemSpace,
		(void **) &ehci->capRegs, memSpaceSize);
	if (status < 0)
	{
		kernelDebugError("Error mapping memory");
		goto err_out;
	}

	// Make it non-cacheable, since this memory represents memory-mapped
	// hardware registers.
	status = kernelPageSetAttrs(KERNELPROCID, 1 /* set */,
		PAGEFLAG_CACHEDISABLE, (void *) ehci->capRegs, memSpaceSize);
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

		kernelDebug(debug_usb, "EHCI memory access enabled in PCI");
	}
	else
	{
		kernelDebug(debug_usb, "EHCI memory access already enabled");
	}

	debugCapRegs(ehci);
	debugHcsParams(ehci);
	debugHccParams(ehci);

	ehci->opRegs = ((void *) ehci->capRegs + ehci->capRegs->capslen);

	ehci->numPorts = (ehci->capRegs->hcsparams & EHCI_HCSP_NUMPORTS);
	kernelDebug(debug_usb, "EHCI number of ports=%d", ehci->numPorts);

	ehci->debugPort =
		((ehci->capRegs->hcsparams & EHCI_HCSP_DEBUGPORT) >> 20);
	kernelDebug(debug_usb, "EHCI debug port=%d", ehci->debugPort);

	// If the extended capabilities registers are implemented, perform an
	// orderly ownership transfer from the BIOS.
	status = handoff(ehci, busTarget, &pciDevInfo);
	if (status < 0)
		goto err_out;

	// Reset the controller
	status = reset(controller);
	if (status < 0)
		goto err_out;

	// Set up the controller's data structures, etc.
	status = setup(controller);
	if (status < 0)
		goto err_out;

	// Start the controller
	status = startStop(ehci, 1);
	if (status < 0)
		goto err_out;

	// Power on all the ports, if applicable
	if (ehci->capRegs->hcsparams & EHCI_HCSP_PORTPOWERCTRL)
	{
		for (count = 0; count < ehci->numPorts; count ++)
		{
			status = portPower(ehci, count, 1);
			if (status < 0)
				goto err_out;
		}

		// Wait 20ms for power to stabilize on all ports (per EHCI spec)
		kernelCpuSpinMs(20);
	}

	debugOpRegs(ehci);

	// Set controller function calls
	controller->reset = &reset;
	controller->interrupt = &interrupt;
	controller->queue = &queue;
	controller->schedInterrupt = &schedInterrupt;
	controller->deviceRemoved = &deviceRemoved;

	// The controller's root hub
	controller->hub.controller = controller;

	// Set hub function calls
	controller->hub.detectDevices = &detectDevices;
	controller->hub.threadCall = &threadCall;

	// Allocate memory for the kernel device
	dev = kernelMalloc(sizeof(kernelDevice));
	if (!dev)
		goto err_out;

	dev->device.class = kernelDeviceGetClass(DEVICECLASS_BUS);
	dev->device.subClass = kernelDeviceGetClass(DEVICESUBCLASS_BUS_USB);
	dev->driver = driver;
	dev->data = (void *) controller;

	// Initialize the variable list for attributes of the controller
	status = kernelVariableListCreate(&dev->device.attrs);
	if (status >= 0)
	{
		kernelVariableListSet(&dev->device.attrs, "controller.type", "EHCI");
		snprintf(value, 32, "%d", ehci->numPorts);
		kernelVariableListSet(&dev->device.attrs, "controller.numPorts",
			value);
		if (ehci->debugPort)
		{
			snprintf(value, 32, "%d", ehci->debugPort);
			kernelVariableListSet(&dev->device.attrs, "controller.debugPort",
				value);
		}
	}

	// Claim the controller device in the list of PCI targets.
	kernelBusDeviceClaim(busTarget, driver);

	// Add the kernel device
	status = kernelDeviceAdd(busTarget->bus->dev, dev);
	if (status < 0)
		goto err_out;
	else
		return (dev);

err_out:

	if (dev)
		kernelFree(dev);
	if (controller)
	{
		if (controller->data)
			kernelFree(controller->data);
		kernelFree((void *) controller);
	}

	return (dev = NULL);
}

