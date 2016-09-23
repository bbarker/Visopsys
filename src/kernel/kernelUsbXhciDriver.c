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
//  kernelUsbXhciDriver.c
//

#include "kernelUsbXhciDriver.h"
#include "kernelUsbDriver.h"
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
#include "kernelText.h"
#include "kernelVariableList.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>


#ifdef DEBUG
static inline void debugCapRegs(xhciData *xhci)
{
	kernelDebug(debug_usb, "XHCI capability registers:\n"
		"  capslen=0x%02x\n"
		"  hciver=0x%04x\n"
		"  hcsparams1=0x%08x\n"
		"  hcsparams2=0x%08x\n"
		"  hcsparams3=0x%08x\n"
		"  hccparams=0x%08x\n"
		"  dboffset=0x%08x\n"
		"  runtimeoffset=0x%08x",
		(xhci->capRegs->capslenHciver & 0xFF),
		(xhci->capRegs->capslenHciver >> 16),
		xhci->capRegs->hcsparams1, xhci->capRegs->hcsparams2,
		xhci->capRegs->hcsparams3, xhci->capRegs->hccparams,
		xhci->capRegs->dboffset, xhci->capRegs->runtimeoffset);
}

static inline void debugOpRegs(xhciData *xhci)
{
	kernelDebug(debug_usb, "XHCI operational registers:\n"
		"  cmd=0x%08x\n"
		"  stat=0x%08x\n"
		"  pagesz=0x%04x (%u)\n"
		"  dncntrl=0x%08x\n"
		"  cmdrctrl=0x...............%1x\n"
		"  dcbaap=0x%08x%08x\n"
		"  config=0x%08x",
		xhci->opRegs->cmd, xhci->opRegs->stat, xhci->opRegs->pagesz,
		(xhci->opRegs->pagesz << 12), xhci->opRegs->dncntrl,
		xhci->opRegs->cmdrctrlLo, xhci->opRegs->dcbaapHi,
		xhci->opRegs->dcbaapLo, xhci->opRegs->config);
}

static inline void debugHcsParams1(xhciData *xhci)
{
	kernelDebug(debug_usb, "XHCI HCSParams1 register (0x%08x):\n"
		"  max ports=%d\n"
		"  max interrupters=%d\n"
		"  max device slots=%d", xhci->capRegs->hcsparams1,
		((xhci->capRegs->hcsparams1 & XHCI_HCSP1_MAXPORTS) >> 24),
		((xhci->capRegs->hcsparams1 & XHCI_HCSP1_MAXINTRPTRS) >> 8),
		(xhci->capRegs->hcsparams1 & XHCI_HCSP1_MAXDEVSLOTS));
}

static inline void debugHcsParams2(xhciData *xhci)
{
	kernelDebug(debug_usb, "XHCI HCSParams2 register (0x%08x):\n"
		"  max scratchpad buffers=%d\n"
		"  scratchpad restore=%d\n"
		"  event ring segment table max=%d\n"
		"  isochronous scheduling threshold=%d",
		xhci->capRegs->hcsparams2,
		(((xhci->capRegs->hcsparams2 & XHCI_HCSP2_MAXSCRPBUFFSHI) >> 16) |
			((xhci->capRegs->hcsparams2 & XHCI_HCSP2_MAXSCRPBUFFSLO) >> 27)),
		((xhci->capRegs->hcsparams2 & XHCI_HCSP2_SCRATCHPREST) >> 26),
		((xhci->capRegs->hcsparams2 & XHCI_HCSP2_ERSTMAX) >> 4),
		(xhci->capRegs->hcsparams2 & XHCI_HCSP2_ISOCSCHDTHRS));
}

static inline void debugHcsParams3(xhciData *xhci)
{
	kernelDebug(debug_usb, "XHCI HCSParams3 register (0x%08x):\n"
		"  u2 device exit latency=%d\n"
		"  u1 device exit latency=%d", xhci->capRegs->hcsparams3,
		((xhci->capRegs->hcsparams3 & XHCI_HCSP3_U2DEVLATENCY) >> 16),
		(xhci->capRegs->hcsparams3 & XHCI_HCSP3_U1DEVLATENCY));
}

static inline void debugHccParams(xhciData *xhci)
{
	kernelDebug(debug_usb, "XHCI HCCParams register(0x%08x):\n"
		"  extended caps ptr=0x%04x\n"
		"  max pri stream array size=%d\n"
		"  no sec sid support=%d\n"
		"  latency tolerance msg cap=%d\n"
		"  light hc reset cap=%d\n"
		"  port indicators=%d\n"
		"  port power control=%d\n"
		"  context size=%d\n"
		"  bandwidth neg cap=%d\n"
		"  64-bit addressing=%d", xhci->capRegs->hccparams,
		((xhci->capRegs->hccparams & XHCI_HCCP_EXTCAPPTR) >> 16),
		((xhci->capRegs->hccparams & XHCI_HCCP_MAXPRISTRARSZ) >> 12),
		((xhci->capRegs->hccparams & XHCI_HCCP_NOSECSIDSUP) >> 7),
		((xhci->capRegs->hccparams & XHCI_HCCP_LATTOLMESSCAP) >> 6),
		((xhci->capRegs->hccparams & XHCI_HCCP_LIGHTHCRESET) >> 5),
		((xhci->capRegs->hccparams & XHCI_HCCP_PORTIND) >> 4),
		((xhci->capRegs->hccparams & XHCI_HCCP_PORTPOWER) >> 3),
		((xhci->capRegs->hccparams & XHCI_HCCP_CONTEXTSIZE) >> 2),
		((xhci->capRegs->hccparams & XHCI_HCCP_BANDNEGCAP) >> 1),
		(xhci->capRegs->hccparams & XHCI_HCCP_64ADDRCAP));
}

static inline void debugCmdStatRegs(xhciData *xhci)
{
	kernelDebug(debug_usb, "XHCI command/status registers:\n"
		"  cmd=0x%08x\n"
		"  stat=0x%08x",
		xhci->opRegs->cmd, xhci->opRegs->stat);
}

static inline void debugRuntimeRegs(xhciData *xhci)
{
	int numIntrs = max(xhci->numIntrs, 1);
	char *intrRegs = NULL;
	int count;

	intrRegs = kernelMalloc(kernelTextGetNumColumns() * numIntrs * 2);
	if (intrRegs)
	{
		// Read the interrupter register sets
		for (count = 0; count < numIntrs; count ++)
			sprintf((intrRegs + strlen(intrRegs)),
				"\n  inter%d intrMan=0x%08x intrMod=0x%08x "
				"evtRngSegTabSz=0x%08x"
				"\n  inter%d evtRngSegBase=0x%08x%08x "
				"evtRngDeqPtr=0x%08x%08x", count,
				xhci->rtRegs->intrReg[count].intrMan,
				xhci->rtRegs->intrReg[count].intrMod,
				xhci->rtRegs->intrReg[count].evtRngSegTabSz, count,
				xhci->rtRegs->intrReg[count].evtRngSegBaseHi,
				xhci->rtRegs->intrReg[count].evtRngSegBaseLo,
				xhci->rtRegs->intrReg[count].evtRngDeqPtrHi,
				xhci->rtRegs->intrReg[count].evtRngDeqPtrLo);

		kernelDebug(debug_usb, "XHCI runtime registers:\n"
			"  mfindex=0x%08x%s", xhci->rtRegs->mfindex, intrRegs);

		kernelFree(intrRegs);
	}
}

static const char *debugTrbType2String(xhciTrb *trb)
{
	switch (trb->typeFlags & XHCI_TRBTYPE_MASK)
	{
		case XHCI_TRBTYPE_RESERVED:
			return "reserved";
		case XHCI_TRBTYPE_NORMAL:
			return "normal";
		case XHCI_TRBTYPE_SETUPSTG:
			return "setup stage";
		case XHCI_TRBTYPE_DATASTG:
			return "data stage";
		case XHCI_TRBTYPE_STATUSSTG:
			return "status stage";
		case XHCI_TRBTYPE_ISOCH:
			return "isochronous";
		case XHCI_TRBTYPE_LINK:
			return "link";
		case XHCI_TRBTYPE_EVENTDATA:
			return "event data";
		case XHCI_TRBTYPE_TRANSNOOP:
			return "transfer no-op";
		case XHCI_TRBTYPE_ENABLESLOT:
			return "enable slot";
		case XHCI_TRBTYPE_DISBLESLOT:
			return "disable slot";
		case XHCI_TRBTYPE_ADDRESSDEV:
			return "address device";
		case XHCI_TRBTYPE_CFGENDPT:
			return "configure endpoint";
		case XHCI_TRBTYPE_EVALCNTXT:
			return "evaluate context";
		case XHCI_TRBTYPE_RESETENDPT:
			return "reset endpoint";
		case XHCI_TRBTYPE_STOPENDPT:
			return "stop endpoint";
		case XHCI_TRBTYPE_SETTRDQ:
			return "set dequeue pointer";
		case XHCI_TRBTYPE_RESETDEV:
			return "reset device";
		case XHCI_TRBTYPE_FORCEEVNT:
			return "force event";
		case XHCI_TRBTYPE_NEGBNDWDTH:
			return "negotiate bandwidth";
		case XHCI_TRBTYPE_SETLATTVAL:
			return "set latency tolerance";
		case XHCI_TRBTYPE_GETPORTBW:
			return "get port bandwidth";
		case XHCI_TRBTYPE_FORCEHDR:
			return "force header";
		case XHCI_TRBTYPE_CMDNOOP:
			return "command no-op";
		case XHCI_TRBTYPE_TRANSFER:
			return "transfer event";
		case XHCI_TRBTYPE_CMDCOMP:
			return "command complete";
		case XHCI_TRBTYPE_PRTSTATCHG:
			return "port status change";
		case XHCI_TRBTYPE_BANDWREQ:
			return "bandwidth request";
		case XHCI_TRBTYPE_DOORBELL:
			return "doorbell";
		case XHCI_TRBTYPE_HOSTCONT:
			return "host controller event";
		case XHCI_TRBTYPE_DEVNOTIFY:
			return "device notification";
		case XHCI_TRBTYPE_MFIDXWRAP:
			return "mfindex wrap";
		default:
			return "unknown";
	}
}

static const char *debugXhciSpeed2String(xhciDevSpeed speed)
{
	switch (speed)
	{
		case xhcispeed_full:
			return "full";
		case xhcispeed_low:
			return "low";
		case xhcispeed_high:
			return "high";
		case xhcispeed_super:
			return "super";
		default:
			return "unknown";
	}
}

static inline void debugPortStatus(xhciData *xhci, int portNum)
{
	unsigned portsc = xhci->opRegs->portRegSet[portNum].portsc;

	kernelDebug(debug_usb, "XHCI port %d status: 0x%08x\n"
		"  changes=0x%02x (%s%s%s%s%s%s%s)\n"
		"  indicator=%d\n"
		"  speed=%d\n"
		"  power=%d\n"
		"  linkState=0x%01x\n"
		"  reset=%d\n"
		"  overCurrent=%d\n"
		"  enabled=%d\n"
		"  connected=%d", portNum, portsc,
		((portsc & XHCI_PORTSC_CHANGES) >> 17),
		((portsc & XHCI_PORTSC_CONFERR_CH)? "conferr," : ""),
		((portsc & XHCI_PORTSC_LINKSTAT_CH)? "linkstat," : ""),
		((portsc & XHCI_PORTSC_RESET_CH)? "reset," : ""),
		((portsc & XHCI_PORTSC_OVERCURR_CH)? "overcurr," : ""),
		((portsc & XHCI_PORTSC_WARMREST_CH)? "warmreset," : ""),
		((portsc & XHCI_PORTSC_ENABLED_CH)? "enable," : ""),
		((portsc & XHCI_PORTSC_CONNECT_CH)? "connect," : ""),
		((portsc & XHCI_PORTSC_PORTIND) >> 14),
		((portsc & XHCI_PORTSC_PORTSPEED) >> 10),
		((portsc & XHCI_PORTSC_PORTPOWER) >> 9),
		((portsc & XHCI_PORTSC_LINKSTATE) >> 5),
		((portsc & XHCI_PORTSC_PORTRESET) >> 4),
		((portsc & XHCI_PORTSC_OVERCURRENT) >> 3),
		((portsc & XHCI_PORTSC_PORTENABLED) >> 1),
		(portsc & XHCI_PORTSC_CONNECTED));
}

static inline void debugSlotCtxt(xhciSlotCtxt *ctxt)
{
	kernelDebug(debug_usb, "XHCI slot context:\n"
		"  contextEntries=%d\n"
		"  hub=%d\n"
		"  MTT=%d\n"
		"  speed=%d\n"
		"  routeString=0x%05x\n"
		"  numPorts=%d\n"
		"  portNum=%d\n"
		"  maxExitLatency=%d\n"
		"  interrupterTarget=%d\n"
		"  TTT=%d\n"
		"  ttPortNum=%d\n"
		"  ttHubSlotId=%d\n"
		"  slotState=%d\n"
		"  devAddr=%d",
		((ctxt->entFlagsSpeedRoute & XHCI_SLTCTXT_CTXTENTS) >> 27),
		((ctxt->entFlagsSpeedRoute & XHCI_SLTCTXT_HUB) >> 26),
		((ctxt->entFlagsSpeedRoute & XHCI_SLTCTXT_MTT) >> 25),
		((ctxt->entFlagsSpeedRoute & XHCI_SLTCTXT_SPEED) >> 20),
		(ctxt->entFlagsSpeedRoute & XHCI_SLTCTXT_ROUTESTRNG),
		((ctxt->numPortsPortLat & XHCI_SLTCTXT_NUMPORTS) >> 24),
		((ctxt->numPortsPortLat & XHCI_SLTCTXT_ROOTPRTNUM) >> 16),
		(ctxt->numPortsPortLat & XHCI_SLTCTXT_MAXEXITLAT),
		((ctxt->targetTT & XHCI_SLTCTXT_INTRTARGET) >> 22),
		((ctxt->targetTT & XHCI_SLTCTXT_TTT) >> 16),
		((ctxt->targetTT & XHCI_SLTCTXT_TTPORTNUM) >> 8),
		(ctxt->targetTT & XHCI_SLTCTXT_TTHUBSLOT),
		((ctxt->slotStateDevAddr & XHCI_SLTCTXT_SLOTSTATE) >> 27),
		(ctxt->slotStateDevAddr & XHCI_SLTCTXT_USBDEVADDR));
}

static inline void debugTrb(xhciTrb *trb)
{
	kernelDebug(debug_usb, "XHCI TRB:\n"
		"  paramLo=0x%08x\n"
		"  paramHi=0x%08x\n"
		"  status=0x%08x\n"
		"  typeFlags=0x%04x (type=%s, flags=0x%03x)\n"
		"  control=0x%04x", trb->paramLo, trb->paramHi,
		trb->status, trb->typeFlags, debugTrbType2String(trb),
		(trb->typeFlags & ~XHCI_TRBTYPE_MASK), trb->control);
}

static inline void debugEndpointCtxt(xhciEndpointCtxt *ctxt)
{
	kernelDebug(debug_usb, "XHCI endpoint context:\n"
		"  interval=%d\n"
		"  linearStreamArray=%d\n"
		"  maxPrimaryStreams=%d\n"
		"  multiplier=%d\n"
		"  endpointState=%d\n"
		"  maxPacketSize=%d\n"
		"  maxBurstSize=%d\n"
		"  hostInitiateDisable=%d\n"
		"  endpointType=%d\n"
		"  errorCount=%d\n"
		"  trDequeuePtr=%p\n"
		"  maxEsitPayload=%d\n"
		"  avgTrbLen=%d",
		((ctxt->intvlLsaMaxPstrMultEpState & XHCI_EPCTXT_INTERVAL) >> 16),
		((ctxt->intvlLsaMaxPstrMultEpState & XHCI_EPCTXT_LINSTRARRAY) >> 15),
		((ctxt->intvlLsaMaxPstrMultEpState & XHCI_EPCTXT_MAXPRIMSTR) >> 10),
		((ctxt->intvlLsaMaxPstrMultEpState & XHCI_EPCTXT_MULT) >> 8),
		(ctxt->intvlLsaMaxPstrMultEpState & XHCI_EPCTXT_EPSTATE),
		((ctxt->maxPSizeMaxBSizeEpTypeCerr & XHCI_EPCTXT_MAXPKTSIZE) >> 16),
		((ctxt->maxPSizeMaxBSizeEpTypeCerr & XHCI_EPCTXT_MAXBRSTSIZE) >> 8),
		((ctxt->maxPSizeMaxBSizeEpTypeCerr & XHCI_EPCTXT_HSTINITDSBL) >> 7),
		((ctxt->maxPSizeMaxBSizeEpTypeCerr & XHCI_EPCTXT_ENDPNTTYPE) >> 3),
		((ctxt->maxPSizeMaxBSizeEpTypeCerr & XHCI_EPCTXT_CERR) >> 1),
		(void *) ctxt->trDeqPtrLo,
		((ctxt->maxEpEsitAvTrbLen & XHCI_EPCTXT_MAXESITPAYL) >> 16),
		(ctxt->maxEpEsitAvTrbLen & XHCI_EPCTXT_AVGTRBLEN));
}

static const char *debugTrbCompletion2String(xhciTrb *trb)
{
	switch (trb->status & XHCI_TRBCOMP_MASK)
	{
		case XHCI_TRBCOMP_INVALID:
			return "invalid code";
		case XHCI_TRBCOMP_SUCCESS:
			return "success";
		case XHCI_TRBCOMP_DATABUFF:
			return "data buffer error";
		case XHCI_TRBCOMP_BABBLE:
			return "babble detected";
		case XHCI_TRBCOMP_TRANS:
			return "USB transaction error";
		case XHCI_TRBCOMP_TRB:
			return "TRB error";
		case XHCI_TRBCOMP_STALL:
			return "stall";
		case XHCI_TRBCOMP_RESOURCE:
			return "resource error";
		case XHCI_TRBCOMP_BANDWIDTH:
			return "bandwidth error";
		case XHCI_TRBCOMP_NOSLOTS:
			return "no slots available";
		case XHCI_TRBCOMP_INVALIDSTREAM:
			return "invalid stream type";
		case XHCI_TRBCOMP_SLOTNOTENAB:
			return "slot not enabled";
		case XHCI_TRBCOMP_ENDPTNOTENAB:
			return "endpoint not enabled";
		case XHCI_TRBCOMP_SHORTPACKET:
			return "short packet";
		case XHCI_TRBCOMP_RINGUNDERRUN:
			return "ring underrun";
		case XHCI_TRBCOMP_RINGOVERRUN:
			return "ring overrun";
		case XHCI_TRBCOMP_VFEVNTRINGFULL:
			return "VF event ring full";
		case XHCI_TRBCOMP_PARAMETER:
			return "parameter error";
		case XHCI_TRBCOMP_BANDWOVERRUN:
			return "bandwidth overrun";
		case XHCI_TRBCOMP_CONTEXTSTATE:
			return "context state error";
		case XHCI_TRBCOMP_NOPINGRESPONSE:
			return "no ping response";
		case XHCI_TRBCOMP_EVNTRINGFULL:
			return "event ring full";
		case XHCI_TRBCOMP_INCOMPATDEVICE:
			return "incompatible device";
		case XHCI_TRBCOMP_MISSEDSERVICE:
			return "missed service";
		case XHCI_TRBCOMP_CMDRINGSTOPPED:
			return "command ring stopped";
		case XHCI_TRBCOMP_COMMANDABORTED:
			return "command aborted";
		case XHCI_TRBCOMP_STOPPED:
			return "stopped";
		case XHCI_TRBCOMP_STOPPEDLENGTH:
			return "stopped - length invalid";
		case XHCI_TRBCOMP_MAXLATTOOLARGE:
			return "max exit latency";
		case XHCI_TRBCOMP_ISOCHBUFFOVER:
			return "isoch buffer overrun";
		case XHCI_TRBCOMP_EVENTLOST:
			return "event lost";
		case XHCI_TRBCOMP_UNDEFINED:
			return "undefined error";
		case XHCI_TRBCOMP_INVSTREAMID:
			return "invalid stream ID";
		case XHCI_TRBCOMP_SECBANDWIDTH:
			return "secondary bandwidth error";
		case XHCI_TRBCOMP_SPLITTRANS:
			return "split transaction error";
		default:
			return "(unknown)";
	}
}
#else
	#define debugCapRegs(xhci) do { } while (0)
	#define debugOpRegs(xhci) do { } while (0)
	#define debugHcsParams1(xhci) do { } while (0)
	#define debugHcsParams2(xhci) do { } while (0)
	#define debugHcsParams3(xhci) do { } while (0)
	#define debugHccParams(xhci) do { } while (0)
	#define debugCmdStatRegs(xhci) do { } while (0)
	#define debugRuntimeRegs(xhci) do { } while (0)
	#define debugTrbType2String(trb) ""
	#define debugXhciSpeed2String(speed) ""
	#define debugPortStatus(xhci, portNum) do { } while (0)
	#define debugSlotCtxt(ctxt) do { } while (0)
	#define debugTrb(trb) do { } while (0)
	#define debugEndpointCtxt(ctxt) do { } while (0)
	#define debugTrbCompletion2String(trb) ""
#endif // DEBUG


static int startStop(xhciData *xhci, int start)
{
	// Start or stop the XHCI controller

	int status = 0;
	int count;

	kernelDebug(debug_usb, "XHCI st%s controller", (start? "art" : "op"));

	if (start)
	{
		// Set the run/stop bit
		xhci->opRegs->cmd |= XHCI_CMD_RUNSTOP;

		// Wait for not halted
		for (count = 0; count < 20; count ++)
		{
			if (!(xhci->opRegs->stat & XHCI_STAT_HCHALTED))
			{
				kernelDebug(debug_usb, "XHCI starting took %dms", count);
				break;
			}

			kernelCpuSpinMs(1);
		}

		// Started?
		if (xhci->opRegs->stat & XHCI_STAT_HCHALTED)
		{
			kernelError(kernel_error, "Couldn't clear controller halted bit");
			status = ERR_TIMEOUT;
			goto out;
		}
	}
	else // stop
	{
		// Make sure the command ring is stopped
		if (xhci->opRegs->cmdrctrlLo & XHCI_CRCR_CMDRNGRUNNING)
		{
			kernelDebug(debug_usb, "XHCI stopping command ring");
			xhci->opRegs->cmdrctrlLo = XHCI_CRCR_COMMANDABORT;
			xhci->opRegs->cmdrctrlHi = 0;

			// Wait for stopped
			for (count = 0; count < 5000; count ++)
			{
				if (!(xhci->opRegs->cmdrctrlLo & XHCI_CRCR_CMDRNGRUNNING))
				{
					kernelDebug(debug_usb, "XHCI stopping command ring took "
						"%dms", count);
					break;
				}

				kernelCpuSpinMs(1);
			}

			// Stopped?
			if (xhci->opRegs->cmdrctrlLo & XHCI_CRCR_CMDRNGRUNNING)
				kernelError(kernel_warn, "Couldn't stop command ring");
		}

		// Clear the run/stop bit
		xhci->opRegs->cmd &= ~XHCI_CMD_RUNSTOP;

		// Wait for halted
		for (count = 0; count < 20; count ++)
		{
			if (xhci->opRegs->stat & XHCI_STAT_HCHALTED)
			{
				kernelDebug(debug_usb, "XHCI stopping controller took %dms",
					count);
				break;
			}

			kernelCpuSpinMs(1);
		}

		// Stopped?
		if (!(xhci->opRegs->stat & XHCI_STAT_HCHALTED))
		{
			kernelError(kernel_error, "Couldn't set controller halted bit");
			status = ERR_TIMEOUT;
			goto out;
		}
	}

out:
	kernelDebug(debug_usb, "XHCI controller %sst%sed", (status? "not " : ""),
		(start? "art" : "opp"));

	return (status);
}


static inline void clearStatusBits(xhciData *xhci, unsigned bits)
{
	// Clear the requested write-1-to-clear status bits, without affecting
	// the others

	xhci->opRegs->stat = ((xhci->opRegs->stat &
		~(XHCI_STAT_ROMASK | XHCI_STAT_RW1CMASK)) | bits);
}


static inline unsigned trbPhysical(xhciTrbRing *ring, xhciTrb *trb)
{
	return (ring->trbsPhysical +
		(unsigned)((void *) trb - (void *) &ring->trbs[0]));
}


static inline int ringNextTrb(xhciTrbRing *transRing)
{
	int nextTrb = (transRing->nextTrb + 1);

	if ((nextTrb >= transRing->numTrbs) ||
		((transRing->trbs[nextTrb].typeFlags & XHCI_TRBTYPE_MASK) ==
			XHCI_TRBTYPE_LINK))
	{
		nextTrb = 0;
	}

	return (nextTrb);
}


static int getEvent(xhciData *xhci, int intrNum, xhciTrb *destTrb, int consume)
{
	int status = 0;
	xhciIntrRegSet *regSet = NULL;
	xhciTrbRing *eventRing = NULL;
	xhciTrb *eventTrb = NULL;

	regSet = &xhci->rtRegs->intrReg[intrNum];
	eventRing = xhci->eventRings[intrNum];
	eventTrb = &eventRing->trbs[eventRing->nextTrb];

	if ((eventTrb->typeFlags & XHCI_TRBFLAG_CYCLE) == eventRing->cycleState)
	{
		kernelDebug(debug_usb, "XHCI next event TRB %d type=%d (%s) 0x%08x "
			"cyc=%d", eventRing->nextTrb,
			((eventTrb->typeFlags & XHCI_TRBTYPE_MASK) >> 10),
			debugTrbType2String(eventTrb), trbPhysical(eventRing, eventTrb),
			(eventTrb->typeFlags & XHCI_TRBFLAG_CYCLE));

		// Copy it
		memcpy((void *) destTrb, (void *) eventTrb, sizeof(xhciTrb));

		if (consume)
		{
			kernelDebug(debug_usb, "XHCI consume event TRB %d type=%d (%s) "
				"0x%08x cyc=%d", eventRing->nextTrb,
				((eventTrb->typeFlags & XHCI_TRBTYPE_MASK) >> 10),
				debugTrbType2String(eventTrb),
				trbPhysical(eventRing, eventTrb),
				(eventTrb->typeFlags & XHCI_TRBFLAG_CYCLE));

			// Move to the next TRB
			eventRing->nextTrb = ringNextTrb(eventRing);
			if (!eventRing->nextTrb)
				eventRing->cycleState ^= 1;

			// Update the controller's event ring dequeue TRB pointer to point
			// to the next one we expect to process, and clear the 'handler
			// busy' flag
			regSet->evtRngDeqPtrLo =
				(trbPhysical(eventRing, &eventRing->trbs[eventRing->nextTrb]) |
					XHCI_ERDP_HANDLERBUSY);
			regSet->evtRngDeqPtrHi = 0;
		}

		return (status = 0);
	}

	// No data
	return (status = ERR_NODATA);
}


static int command(xhciData *xhci, xhciTrb *cmdTrb)
{
	// Place a command in the command ring

	int status = 0;
	xhciTrb *nextTrb = NULL;
	xhciTrb eventTrb;
	int count;

	kernelDebug(debug_usb, "XHCI command %d (%s) position %d",
		((cmdTrb->typeFlags & XHCI_TRBTYPE_MASK) >> 10),
		debugTrbType2String(cmdTrb), xhci->commandRing->nextTrb);

	nextTrb = &xhci->commandRing->trbs[xhci->commandRing->nextTrb];

	kernelDebug(debug_usb, "XHCI use TRB with physical address=0x%08x",
		trbPhysical(xhci->commandRing, nextTrb));

	// Set the cycle bit
	if (xhci->commandRing->cycleState)
		cmdTrb->typeFlags |= XHCI_TRBFLAG_CYCLE;
	else
		cmdTrb->typeFlags &= ~XHCI_TRBFLAG_CYCLE;

	// Copy the command
	memcpy((void *) nextTrb, (void *) cmdTrb, sizeof(xhciTrb));

	// Ring the command doorbell
	xhci->dbRegs->doorbell[0] = 0;

	// Wait until the command has completed
	for (count = 0; count < USB_STD_TIMEOUT_MS; count ++)
	{
		memset((void *) &eventTrb, 0, sizeof(xhciTrb));

		if (!getEvent(xhci, 0, &eventTrb, 1) &&
			((eventTrb.typeFlags & XHCI_TRBTYPE_MASK) == XHCI_TRBTYPE_CMDCOMP))
		{
			kernelDebug(debug_usb, "XHCI got command completion event for "
				"TRB 0x%08x", (eventTrb.paramLo & ~0xFU));

			kernelDebug(debug_usb, "XHCI completion code %d",
				((eventTrb.status & XHCI_TRBCOMP_MASK) >> 24));

			if ((eventTrb.paramLo & ~0xFU) ==
				trbPhysical(xhci->commandRing, nextTrb))
			{
				break;
			}
		}

		kernelCpuSpinMs(1);
	}

	if (count >= USB_STD_TIMEOUT_MS)
	{
		kernelDebugError("No command event received");
		return (status = ERR_TIMEOUT);
	}

	// Copy the completion event TRB back to the command TRB
	memcpy((void *) cmdTrb, (void *) &eventTrb, sizeof(xhciTrb));

	// Advance the nextTrb 'enqueue pointer'
	xhci->commandRing->nextTrb = ringNextTrb(xhci->commandRing);
	if (!xhci->commandRing->nextTrb)
	{
		// Update the cycle bit of the link TRB
		if (xhci->commandRing->cycleState)
			xhci->commandRing->trbs[xhci->commandRing->numTrbs - 1]
				.typeFlags |= XHCI_TRBFLAG_CYCLE;
		else
			xhci->commandRing->trbs[xhci->commandRing->numTrbs - 1]
				.typeFlags &= ~XHCI_TRBFLAG_CYCLE;

		xhci->commandRing->cycleState ^= 1;
	}

	return (status = 0);
}


static xhciDevSpeed usbSpeed2XhciSpeed(usbDevSpeed usbSpeed)
{
	switch (usbSpeed)
	{
		case usbspeed_full:
			return xhcispeed_full;
		case usbspeed_low:
			return xhcispeed_low;
		case usbspeed_high:
			return xhcispeed_high;
		case usbspeed_super:
			return xhcispeed_super;
		default:
			return xhcispeed_unknown;
	}
}


static int getHighSpeedHubSlotPort(xhciData *xhci, usbDevice *usbDev,
	int *slotNum, int *portNum)
{
	// For low/full-speed devices attached to high speed hubs, we need to get
	// the slot number and port number of the upstream high speed hub, for use
	// in the device's slot context

	usbDevice *hubDev = NULL;
	usbDevice *parentHub = NULL;
	int hubPort = 0;
	kernelLinkedListItem *iter = NULL;
	xhciSlot *slot = NULL;

	// First, look upstream for the hub
	parentHub = usbDev->hub->usbDev;
	hubPort = usbDev->hubPort;

	while (parentHub)
	{
		if (parentHub->usbVersion >= 0x0200)
		{
			hubDev = parentHub;
			break;
		}

		hubPort = parentHub->hubPort;
		parentHub = parentHub->hub->usbDev;
	}

	if (!hubDev)
		// Not found - it's probably on a root port
		return (ERR_NOSUCHENTRY);

	// Found the hub, now look for its slot
	slot = kernelLinkedListIterStart(&xhci->slots, &iter);
	while (slot)
	{
		if (slot->usbDev == hubDev)
		{
			*slotNum = slot->num;
			*portNum = hubPort;
			return (0);
		}

		slot = kernelLinkedListIterNext(&xhci->slots, &iter);
	}

	return (ERR_NOSUCHENTRY);
}


static void deallocTrbRing(xhciTrbRing *trbRing)
{
	// Dellocate a TRB ring.

	kernelIoMemory ioMem;

	if (trbRing->trbs)
	{
		ioMem.size = (trbRing->numTrbs * sizeof(xhciTrb));
		ioMem.physical = trbRing->trbsPhysical;
		ioMem.virtual = (void *) trbRing->trbs;
		kernelMemoryReleaseIo(&ioMem);
	}

	kernelFree(trbRing);
}


static xhciTrbRing *allocTrbRing(int numTrbs, int circular)
{
	// Allocate and link TRBs into a TRB ring, used for events, transfers, and
	// commands.

	xhciTrbRing *trbRing = NULL;
	unsigned memSize = 0;
	kernelIoMemory ioMem;

	memset(&ioMem, 0, sizeof(kernelIoMemory));

	// Allocate memory for the trbRing structure
	trbRing = kernelMalloc(sizeof(xhciTrbRing));
	if (!trbRing)
	{
		kernelError(kernel_error, "Couldn't get memory for TRB ring");
		goto err_out;
	}

	trbRing->numTrbs = numTrbs;
	trbRing->cycleState = XHCI_TRBFLAG_CYCLE;

	// How much memory do we need for TRBs?
	memSize = (numTrbs * sizeof(xhciTrb));

	// Request the memory
	if (kernelMemoryGetIo(memSize, 64 /* alignment */, &ioMem) < 0)
	{
		kernelError(kernel_error, "Couldn't get memory for TRBs");
		goto err_out;
	}

	trbRing->trbs = ioMem.virtual;
	trbRing->trbsPhysical = ioMem.physical;

	if (circular)
	{
		// Use the last TRB as a 'link' back to the beginning of the ring
		trbRing->trbs[trbRing->numTrbs - 1].paramLo = trbRing->trbsPhysical;
		trbRing->trbs[trbRing->numTrbs - 1].typeFlags =
			(XHCI_TRBTYPE_LINK | XHCI_TRBFLAG_TOGGLECYCL);
	}

	// Return success
	return (trbRing);

err_out:
	if (trbRing)
		deallocTrbRing(trbRing);

	return (trbRing = NULL);
}


static int allocEndpoint(xhciSlot *slot, int endpoint, int endpointType,
	int interval, int maxPacketSize, int maxBurst)
{
	// Allocate a transfer ring and initialize the endpoint context.

	int status = 0;
	xhciEndpointCtxt *inputEndpointCtxt = NULL;
	unsigned avgTrbLen = 0;

	kernelDebug(debug_usb, "XHCI initialize endpoint 0x%02x", endpoint);

	// Allocate the transfer ring for the endpoint
	slot->transRings[endpoint & 0xF] =
		allocTrbRing(XHCI_TRANSRING_SIZE, 1 /* circular */);
	if (!slot->transRings[endpoint & 0xF])
		return (status = ERR_MEMORY);

	// Get a pointer to the endpoint input context
	if (endpoint)
		inputEndpointCtxt = &slot->inputCtxt->devCtxt
			.endpointCtxt[(((endpoint & 0xF) * 2) - 1) + (endpoint >> 7)];
	else
		inputEndpointCtxt = &slot->inputCtxt->devCtxt.endpointCtxt[0];

	// Initialize the input endpoint context

	inputEndpointCtxt->intvlLsaMaxPstrMultEpState =
		((interval << 16) & XHCI_EPCTXT_INTERVAL);

	inputEndpointCtxt->maxPSizeMaxBSizeEpTypeCerr =
		(((maxPacketSize << 16) & XHCI_EPCTXT_MAXPKTSIZE) |
			((maxBurst << 8) & XHCI_EPCTXT_MAXBRSTSIZE) |
			((endpointType << 3) & XHCI_EPCTXT_ENDPNTTYPE) |
			((3 << 1) & XHCI_EPCTXT_CERR) /* cerr */);

	inputEndpointCtxt->trDeqPtrLo =
		(slot->transRings[endpoint & 0xF]->trbsPhysical | XHCI_TRBFLAG_CYCLE);

	switch (endpointType)
	{
		case XHCI_EPTYPE_CONTROL:
			avgTrbLen = 0x8;
			break;
		case XHCI_EPTYPE_INTR_OUT:
		case XHCI_EPTYPE_INTR_IN:
			avgTrbLen = 0x400;
			break;
		case XHCI_EPTYPE_ISOCH_OUT:
		case XHCI_EPTYPE_ISOCH_IN:
		case XHCI_EPTYPE_BULK_OUT:
		case XHCI_EPTYPE_BULK_IN:
			avgTrbLen = 0xC00;
			break;
	}

	inputEndpointCtxt->maxEpEsitAvTrbLen = (avgTrbLen & XHCI_EPCTXT_AVGTRBLEN);

	return (status = 0);
}


static int deallocSlot(xhciData *xhci, xhciSlot *slot)
{
	// Deallocate a slot, also releasing it in the controller, if applicable

	int status = 0;
	xhciTrb cmdTrb;
	kernelIoMemory ioMem;
	int count;

	kernelDebug(debug_usb, "XHCI de-allocate device slot");

	// Remove it from the controller's list
	status = kernelLinkedListRemove(&xhci->slots, slot);
	if (status < 0)
		return (status);

	// Send a 'disable slot' command
	memset((void *) &cmdTrb, 0, sizeof(xhciTrb));
	cmdTrb.typeFlags = XHCI_TRBTYPE_DISBLESLOT;
	cmdTrb.control = (slot->num << 8);
	status = command(xhci, &cmdTrb);
	if (status < 0)
		return (status);

	if ((cmdTrb.status & XHCI_TRBCOMP_MASK) != XHCI_TRBCOMP_SUCCESS)
	{
		kernelError(kernel_error, "Command error %d disabling device slot",
			((cmdTrb.status & XHCI_TRBCOMP_MASK) >> 24));
		return (status = ERR_IO);
	}

	// Delete the reference to the device context from the device context
	// base address array
	xhci->devCtxtPhysPtrs[slot->num] = NULL;

	// Free memory

	if (slot->devCtxt)
	{
		ioMem.size = sizeof(xhciDevCtxt);
		ioMem.physical = slot->devCtxtPhysical;
		ioMem.virtual = (void *) slot->devCtxt;
		kernelMemoryReleaseIo(&ioMem);
	}

	for (count = 0; count < USB_MAX_ENDPOINTS; count ++)
		if (slot->transRings[count])
			deallocTrbRing(slot->transRings[count]);

	if (slot->inputCtxt)
	{
		ioMem.size = sizeof(xhciInputCtxt);
		ioMem.physical = slot->inputCtxtPhysical;
		ioMem.virtual = (void *) slot->inputCtxt;
		kernelMemoryReleaseIo(&ioMem);
	}

	status = kernelFree(slot);
	if (status < 0)
		return (status);

	return (status = 0);
}


static xhciSlot *allocSlot(xhciData *xhci, usbDevice *usbDev)
{
	// Ask the controller for a new device slot.  If we get one, allocate
	// data structures for it.

	xhciSlot *slot = NULL;
	xhciTrb cmdTrb;
	kernelIoMemory ioMem;
	int maxPacketSize0 = 0;
	int hubSlotNum = 0, hubPortNum = -1;

	kernelDebug(debug_usb, "XHCI allocate new device slot");

	// Send an 'enable slot' command to get a device slot
	memset((void *) &cmdTrb, 0, sizeof(xhciTrb));
	cmdTrb.typeFlags = XHCI_TRBTYPE_ENABLESLOT;
	if (command(xhci, &cmdTrb) < 0)
		return (slot = NULL);

	if ((cmdTrb.status & XHCI_TRBCOMP_MASK) != XHCI_TRBCOMP_SUCCESS)
	{
		kernelError(kernel_error, "Command error %d enabling device slot",
			((cmdTrb.status & XHCI_TRBCOMP_MASK) >> 24));
		return (slot = NULL);
	}

	slot = kernelMalloc(sizeof(xhciSlot));
	if (!slot)
		return (slot);

	// Record the device slot number and device
	memset(slot, 0, sizeof(xhciSlot));
	slot->num = (cmdTrb.control >> 8);
	slot->usbDev = usbDev;

	kernelDebug(debug_usb, "XHCI got device slot %d from controller",
		slot->num);

	// Allocate I/O memory for the input context
	if (kernelMemoryGetIo(sizeof(xhciInputCtxt),
		xhci->pageSize /* alignment on page boundary */, &ioMem) < 0)
	{
		goto err_out;
	}

	slot->inputCtxt = ioMem.virtual;
	slot->inputCtxtPhysical = ioMem.physical;

	// Set the A0 and A1 bits of the input control context
	slot->inputCtxt->inputCtrlCtxt.add = (BIT(0) | BIT(1));

	// Initialize the input slot context data structure
	slot->inputCtxt->devCtxt.slotCtxt.entFlagsSpeedRoute =
		(((1 << 27) & XHCI_SLTCTXT_CTXTENTS) /* context entries = 1 */ |
		((usbSpeed2XhciSpeed(usbDev->speed) << 20) & XHCI_SLTCTXT_SPEED) |
		(usbDev->routeString & XHCI_SLTCTXT_ROUTESTRNG));

	slot->inputCtxt->devCtxt.slotCtxt.numPortsPortLat =
		(((slot->usbDev->rootPort + 1) << 16) & XHCI_SLTCTXT_ROOTPRTNUM);

	if (usbDev->hub->usbDev && ((usbDev->speed == usbspeed_low) ||
		(usbDev->speed == usbspeed_full)))
	{
		// It's OK if this fails, which it will if there's no high speed hub
		// between here and the host controller
		getHighSpeedHubSlotPort(xhci, usbDev, &hubSlotNum, &hubPortNum);

		slot->inputCtxt->devCtxt.slotCtxt.targetTT =
			((((hubPortNum + 1) << 8) & XHCI_SLTCTXT_TTPORTNUM) |
			(hubSlotNum & XHCI_SLTCTXT_TTHUBSLOT));
	}

	// Super-speed, high-speed, and low-speed devices have fixed maximum
	// packet sizes.  Full-speed devices need to be queried, so start with
	// the minimum of 8.
	switch (usbDev->speed)
	{
		case usbspeed_super:
			maxPacketSize0 = 512;
			break;
		case usbspeed_high:
			maxPacketSize0 = 64;
			break;
		case usbspeed_low:
		default:
			maxPacketSize0 = 8;
			break;
	}

	// Allocate the control endpoint
	if (allocEndpoint(slot, 0, XHCI_EPTYPE_CONTROL, 0, maxPacketSize0, 0) < 0)
		goto err_out;

	// Allocate I/O memory for the device context
	if (kernelMemoryGetIo(sizeof(xhciDevCtxt),
		xhci->pageSize /* alignment on page boundary */, &ioMem) < 0)
	{
		goto err_out;
	}

	slot->devCtxt = ioMem.virtual;
	slot->devCtxtPhysical = ioMem.physical;

	// Record the physical address in the device context base address array
	xhci->devCtxtPhysPtrs[slot->num] = slot->devCtxtPhysical;

	// Add it to the list
	if (kernelLinkedListAdd(&xhci->slots, slot) < 0)
		goto err_out;

	return (slot);

err_out:
	if (slot)
		deallocSlot(xhci, slot);

	return (slot = NULL);
}


static int setDevAddress(xhciData *xhci, xhciSlot *slot, usbDevice *usbDev)
{
	int status = 0;
	xhciTrb cmdTrb;
	xhciEndpointCtxt *inputEndpointCtxt = NULL;

	// If usbDev is NULL, that tells us we're only doing this to enable the
	// control endpoint on the controller, but that we don't want to send
	// a USB_SET_ADDRESS to the device.

	// Send an 'address device' command
	memset((void *) &cmdTrb, 0, sizeof(xhciTrb));
	cmdTrb.paramLo = slot->inputCtxtPhysical;
	cmdTrb.typeFlags = XHCI_TRBTYPE_ADDRESSDEV;
	if (!usbDev)
		cmdTrb.typeFlags|= XHCI_TRBFLAG_BLKSETADDR;
	cmdTrb.control = (slot->num << 8);

	status = command(xhci, &cmdTrb);
	if (status < 0)
		return (status);

	if ((cmdTrb.status & XHCI_TRBCOMP_MASK) != XHCI_TRBCOMP_SUCCESS)
	{
		debugSlotCtxt(&slot->inputCtxt->devCtxt.slotCtxt);
		kernelError(kernel_error, "Command error %d addressing device",
			((cmdTrb.status & XHCI_TRBCOMP_MASK) >> 24));
		return (status = ERR_IO);
	}

	//debugSlotCtxt(&slot->devCtxt->slotCtxt);
	//debugEndpointCtxt(&slot->devCtxt->endpointCtxt[0]);

	if (usbDev)
	{
		// Set the address in the USB device
		usbDev->address = (slot->devCtxt->slotCtxt.slotStateDevAddr &
			XHCI_SLTCTXT_USBDEVADDR);

		kernelDebug(debug_usb, "XHCI device address is now %d",
			usbDev->address);

		// If it's a full-speed device, now is the right time to set the control
		// endpoint packet size
		if (usbDev->speed == usbspeed_full)
		{
			inputEndpointCtxt = &slot->inputCtxt->devCtxt.endpointCtxt[0];

			inputEndpointCtxt->maxPSizeMaxBSizeEpTypeCerr &=
				~XHCI_EPCTXT_MAXPKTSIZE;
			inputEndpointCtxt->maxPSizeMaxBSizeEpTypeCerr |=
				((usbDev->deviceDesc.maxPacketSize0 << 16) &
					XHCI_EPCTXT_MAXPKTSIZE);

			// Set the 'add' bit of the input control context
			slot->inputCtxt->inputCtrlCtxt.add = BIT(1);
			slot->inputCtxt->inputCtrlCtxt.drop = 0;

			// Send the 'evaluate context' command
			memset((void *) &cmdTrb, 0, sizeof(xhciTrb));
			cmdTrb.paramLo = slot->inputCtxtPhysical;
			cmdTrb.typeFlags = XHCI_TRBTYPE_EVALCNTXT;
			cmdTrb.control = (slot->num << 8);

			status = command(xhci, &cmdTrb);
			if (status < 0)
				return (status);

			if ((cmdTrb.status & XHCI_TRBCOMP_MASK) != XHCI_TRBCOMP_SUCCESS)
			{
				kernelDebugError("Command error %d evaluating device context",
					((cmdTrb.status & XHCI_TRBCOMP_MASK) >> 24));
				return (status = ERR_IO);
			}
		}
	}

	return (status = 0);
}


static xhciSlot *getDevSlot(xhciData *xhci, usbDevice *usbDev)
{
	// Return the a pointer to the slot structure belonging to a device.
	// First, search the list of existing ones.  If none is found, then
	// allocate and intitialize a new one.

	xhciSlot *slot = NULL;
	kernelLinkedListItem *iter = NULL;

	kernelDebug(debug_usb, "XHCI get device slot for device %d",
		usbDev->address);

	slot = kernelLinkedListIterStart(&xhci->slots, &iter);
	while (slot)
	{
		if (slot->usbDev == usbDev)
			return (slot);

		slot = kernelLinkedListIterNext(&xhci->slots, &iter);
	}

	// Not found.  Allocate a new one.
	slot = allocSlot(xhci, usbDev);
	if (!slot)
		return (slot);

	// Do a setDevAddress() for the controller's sake (to enable the control
	// endpoint) but don't address the device.
	if (setDevAddress(xhci, slot, NULL) < 0)
	{
		deallocSlot(xhci, slot);
		return (slot = NULL);
	}

	return (slot);
}


static xhciTrb *queueIntrDesc(xhciData *xhci, xhciSlot *slot, int endpoint,
	xhciTrb *srcTrb)
{
	// Enqueue the supplied interrupt on the transfer ring of the requested
	// endpoint.

	xhciTrbRing *transRing = NULL;
	xhciTrb *destTrb = NULL;

	transRing = slot->transRings[endpoint & 0xF];
	if (!transRing)
	{
		kernelError(kernel_error, "Endpoint 0x%02x has no transfer ring",
			endpoint);
		return (destTrb = NULL);
	}

	kernelDebug(debug_usb, "XHCI queue interrupt TRB, slot %d, endpoint "
		"0x%02x, position %d", slot->num, endpoint, transRing->nextTrb);

	destTrb = &transRing->trbs[transRing->nextTrb];

	kernelDebug(debug_usb, "XHCI use TRB with physical address=0x%08x",
		trbPhysical(transRing, destTrb));

	// Set the cycle bit
	if (transRing->cycleState)
		srcTrb->typeFlags |= XHCI_TRBFLAG_CYCLE;
	else
		srcTrb->typeFlags &= ~XHCI_TRBFLAG_CYCLE;

	// Copy the TRB
	memcpy((void *) destTrb, (void *) srcTrb, sizeof(xhciTrb));

	// Advance the nextTrb 'enqueue pointer'
	transRing->nextTrb = ringNextTrb(transRing);
	if (!transRing->nextTrb)
	{
		// Update the cycle bit of the link TRB
		if (transRing->cycleState)
			transRing->trbs[transRing->numTrbs - 1].typeFlags |=
				XHCI_TRBFLAG_CYCLE;
		else
			transRing->trbs[transRing->numTrbs - 1].typeFlags &=
				~XHCI_TRBFLAG_CYCLE;

		transRing->cycleState ^= 1;
	}

	// Ring the slot doorbell with the endpoint number
	kernelDebug(debug_usb, "XHCI ring endpoint 0x%02x doorbell", endpoint);
	if (endpoint)
		xhci->dbRegs->doorbell[slot->num] =
			(((endpoint & 0xF) * 2) + (endpoint >> 7));
	else
		xhci->dbRegs->doorbell[slot->num] = 1;

	return (destTrb);
}


static int transferEventInterrupt(xhciData *xhci, xhciTrb *eventTrb)
{
	int slotNum = 0;
	int endpoint = 0;
	kernelLinkedListItem *iter = NULL;
	xhciIntrReg *intrReg = NULL;
	xhciSlot *slot = NULL;
	unsigned bytes = 0;

	slotNum = (eventTrb->control >> 8);
	endpoint = (((eventTrb->control & 0x0001) << 7) |
		((eventTrb->control & 0x001F) >> 1));

	kernelDebug(debug_usb, "XHCI transfer event interrupt, slot %d, endpoint "
		"0x%02x", slotNum, endpoint);

	// Loop through this controller's interrupt registrations, to find out
	// whether one of them caused this interrupt.
	intrReg = kernelLinkedListIterStart(&xhci->intrRegs, &iter);
	while (intrReg)
	{
		kernelDebug(debug_usb, "XHCI examine interrupt reg for slot %d, "
			"endpoint 0x%02x", intrReg->slot->num, intrReg->endpoint);

		if ((intrReg->slot->num == slotNum) && (intrReg->endpoint == endpoint))
		{
			slot = getDevSlot(xhci, intrReg->usbDev);
			if (slot)
			{
				if ((eventTrb->paramLo & ~0xFU) ==
					trbPhysical(slot->transRings[intrReg->endpoint & 0xF],
						intrReg->queuedTrb))
				{
					bytes = (intrReg->dataLen - (eventTrb->status & 0xFFFFFF));

					kernelDebug(debug_usb, "XHCI found, device address %d, "
						"endpoint 0x%02x, %u bytes", intrReg->usbDev->address,
						intrReg->endpoint, bytes);

					if (intrReg->callback)
					{
						intrReg->callback(intrReg->usbDev, intrReg->interface,
							intrReg->buffer, bytes);
					}
					else
					{
						kernelDebug(debug_usb, "XHCI no callback");
					}

					// Re-queue the TRB
					intrReg->queuedTrb = queueIntrDesc(xhci, intrReg->slot,
						intrReg->endpoint, &intrReg->trb);

					break;
				}
			}
		}

		intrReg = kernelLinkedListIterNext(&xhci->intrRegs, &iter);
	}

	// If we did a callback, consume the event.  Otherwise, leave the event
	// in the ring for synchronous consumption.
	if (intrReg && intrReg->callback)
		return (1);
	else
		return (0);
}


static int portEventInterrupt(xhciData *xhci, xhciTrb *eventTrb)
{
	// Port status changed.

	int portNum = ((eventTrb->paramLo >> 24) - 1);

	kernelDebug(debug_usb, "XHCI port %d event interrupt, portsc=%08x",
		portNum, xhci->opRegs->portRegSet[portNum].portsc);

	xhci->portChangedBitmap |= BIT(portNum);

	return (1);
}


static int eventInterrupt(xhciData *xhci)
{
	// When an interrupt arrives that indicates an event has occurred, this
	// function is called to process it.

	int status = 0;
	xhciIntrRegSet *regSet = NULL;
	xhciTrb eventTrb;
	int consume = 0;
	int intrCount;

	kernelDebug(debug_usb, "XHCI process event interrupt");

	// Loop through the interrupters, to see which one(s) are interrupting
	for (intrCount = 0; intrCount < xhci->numIntrs; intrCount ++)
	{
		regSet = &xhci->rtRegs->intrReg[intrCount];

		if (!(regSet->intrMan & XHCI_IMAN_INTPENDING))
			continue;

		kernelDebug(debug_usb, "XHCI interrupter %d active", intrCount);

		// Clear the interrupt pending flag
		regSet->intrMan |= XHCI_IMAN_INTPENDING;

		memset((void *) &eventTrb, 0, sizeof(xhciTrb));

		while (!getEvent(xhci, intrCount, &eventTrb, 0))
		{
			consume = 0;

			switch (eventTrb.typeFlags & XHCI_TRBTYPE_MASK)
			{
				case XHCI_TRBTYPE_TRANSFER:
					consume = transferEventInterrupt(xhci, &eventTrb);
					if (!consume)
						kernelDebug(debug_usb, "XHCI ignore transfer event");
					break;

				case XHCI_TRBTYPE_CMDCOMP:
					kernelDebug(debug_usb, "XHCI ignore command completion "
						"event");
					break;

				case XHCI_TRBTYPE_PRTSTATCHG:
					consume = portEventInterrupt(xhci, &eventTrb);
					break;

				case XHCI_TRBTYPE_HOSTCONT:
					// Host controller event (an error, we presume)
					kernelDebug(debug_usb, "XHCI host controller event, "
						"status=0x%02x (%s)", (eventTrb.status >> 24),
						(((eventTrb.status & XHCI_TRBCOMP_MASK) ==
							XHCI_TRBCOMP_SUCCESS)? "success" : "error"));
					consume = 1;
					status = ERR_IO;
					break;

				default:
					kernelError(kernel_error, "Unsupported event type %d",
						((eventTrb.typeFlags & XHCI_TRBTYPE_MASK) >> 10));
					status = ERR_IO;
					break;
			}

			if (consume)
				getEvent(xhci, intrCount, &eventTrb, 1);
			else
				break;
		}
	}

	return (status);
}


static int configDevSlot(xhciData *xhci, xhciSlot *slot, usbDevice *usbDev)
{
	// 'configure' the supplied device slot

	int status = 0;
	usbEndpoint *endpoint = NULL;
	int ctxtIndex = 0;
	int endpointType = 0;
	int interval = 0;
	int contextEntries = 0;
	xhciTrb cmdTrb;
	int count;

	kernelDebug(debug_usb, "XHCI configure device slot %d", slot->num);

	slot->inputCtxt->inputCtrlCtxt.add = BIT(0);
	slot->inputCtxt->inputCtrlCtxt.drop = 0;

	// Loop through the endpoints (not including default endpoint 0) and set
	// up their endpoint contexts
	for (count = 0; count < usbDev->numEndpoints; count ++)
	{
		// Get the endpoint descriptor
		endpoint = usbDev->endpoint[count];

		if (!endpoint->number)
			continue;

		ctxtIndex = ((((endpoint->number & 0xF) * 2) - 1) +
			(endpoint->number >> 7));

		kernelDebug(debug_usb, "XHCI configure endpoint 0x%02x, ctxtIndex=%d",
			endpoint->number, ctxtIndex);

		// What kind of XHCI endpoint is it?
		switch (endpoint->attributes & USB_ENDP_ATTR_MASK)
		{
			case USB_ENDP_ATTR_CONTROL:
				endpointType = XHCI_EPTYPE_CONTROL;
				break;

			case USB_ENDP_ATTR_BULK:
				if (endpoint->number & 0x80)
					endpointType = XHCI_EPTYPE_BULK_IN;
				else
					endpointType = XHCI_EPTYPE_BULK_OUT;
				break;

			case USB_ENDP_ATTR_ISOCHRONOUS:
				if (endpoint->number & 0x80)
					endpointType = XHCI_EPTYPE_ISOCH_IN;
				else
					endpointType = XHCI_EPTYPE_ISOCH_OUT;
				break;

			case USB_ENDP_ATTR_INTERRUPT:
			{
				if (endpoint->number & 0x80)
					endpointType = XHCI_EPTYPE_INTR_IN;
				else
					endpointType = XHCI_EPTYPE_INTR_OUT;
			}
		}

		kernelDebug(debug_usb, "XHCI endpoint interval value %d",
			endpoint->interval);

		// Interpret the endpoint interval value.  Expressed in frames
		// or microframes depending on the device operating speed (i.e.,
		// either 1 millisecond or 125 us units).
		interval = 0;
		if (usbDev->speed < usbspeed_high)
		{
			// Interval is expressed in frames
			while ((1 << (interval + 1)) <= (endpoint->interval << 3))
				interval += 1;
		}
		else
		{
			// Interval is expressed in microframes as a 1-based
			// exponent
			interval = (endpoint->interval - 1);
		}

		if (interval)
			kernelDebug(debug_usb, "XHCI interrupt interval at 2^%d "
				"microframes", interval);

		// Allocate things needed for the endpoint.
		status = allocEndpoint(slot, endpoint->number, endpointType, interval,
			endpoint->maxPacketSize, endpoint->maxBurst);
		if (status < 0)
			return (status);

		// Set the 'add' bit of the input control context
		slot->inputCtxt->inputCtrlCtxt.add |= BIT(ctxtIndex + 1);

		kernelDebug(debug_usb, "XHCI BIT(%d) now 0x%08x", (ctxtIndex + 1),
			slot->inputCtxt->inputCtrlCtxt.add);

		contextEntries = (ctxtIndex + 1);
	}

	// Update the input slot context data structure
	slot->inputCtxt->devCtxt.slotCtxt.entFlagsSpeedRoute &= 0x07FFFFFF;
	slot->inputCtxt->devCtxt.slotCtxt.entFlagsSpeedRoute |=
		((contextEntries << 27) & XHCI_SLTCTXT_CTXTENTS);

	kernelDebug(debug_usb, "XHCI contextEntries=%d now 0x%08x", contextEntries,
		slot->inputCtxt->devCtxt.slotCtxt.entFlagsSpeedRoute);

	// Send the 'configure endpoint' command
	memset((void *) &cmdTrb, 0, sizeof(xhciTrb));
	cmdTrb.paramLo = slot->inputCtxtPhysical;
	cmdTrb.typeFlags = XHCI_TRBTYPE_CFGENDPT;
	cmdTrb.control = (slot->num << 8);

	status = command(xhci, &cmdTrb);
	if (status < 0)
		return (status);

	if ((cmdTrb.status & XHCI_TRBCOMP_MASK) != XHCI_TRBCOMP_SUCCESS)
	{
		kernelError(kernel_error, "Command error %d configuring device slot",
			((cmdTrb.status & XHCI_TRBCOMP_MASK) >> 24));
		return (status = ERR_IO);
	}

	return (status = 0);
}


static int transfer(usbController *controller, xhciSlot *slot, int endpoint,
	unsigned timeout, int numTrbs, xhciTrb *trbs)
{
	// Enqueue the supplied transaction on the transfer ring of the requested
	// endpoint.

	int status = 0;
	xhciData *xhci = controller->data;
	xhciTrbRing *transRing = NULL;
	xhciTrb *srcTrb = NULL;
	xhciTrb *destTrb = NULL;
	xhciTrb eventTrb;
	uquad_t currTime = 0;
	uquad_t endTime = 0;
	int trbCount;

	transRing = slot->transRings[endpoint & 0xF];
	if (!transRing)
	{
		kernelError(kernel_error, "Endpoint 0x%02x has no transfer ring",
			endpoint);
		return (status = ERR_NOTINITIALIZED);
	}

	kernelDebug(debug_usb, "XHCI queue transfer (%d TRBs) slot %d, "
		"endpoint 0x%02x, pos %d", numTrbs, slot->num, endpoint,
		transRing->nextTrb);

	for (trbCount = 0; trbCount < numTrbs; trbCount ++)
	{
		srcTrb = &trbs[trbCount];
		destTrb = &transRing->trbs[transRing->nextTrb];

		kernelDebug(debug_usb, "XHCI use TRB with physical address=0x%08x",
			trbPhysical(transRing, destTrb));

		// Copy the TRB
		memcpy((void *) destTrb, (void *) srcTrb, sizeof(xhciTrb));

		// Set the last TRB to interrupt
		if (trbCount == (numTrbs - 1))
			destTrb->typeFlags |= XHCI_TRBFLAG_INTONCOMP;

		// Set the cycle bit
		if (transRing->cycleState)
			destTrb->typeFlags |= XHCI_TRBFLAG_CYCLE;
		else
			destTrb->typeFlags &= ~XHCI_TRBFLAG_CYCLE;

		debugTrb(destTrb);

		// Advance the nextTrb 'enqueue pointer'
		transRing->nextTrb = ringNextTrb(transRing);
		if (!transRing->nextTrb)
		{
			// Update the cycle bit of the link TRB
			if (transRing->cycleState)
				transRing->trbs[transRing->numTrbs - 1].typeFlags |=
					XHCI_TRBFLAG_CYCLE;
			else
				transRing->trbs[transRing->numTrbs - 1].typeFlags &=
					~XHCI_TRBFLAG_CYCLE;

			transRing->cycleState ^= 1;
		}
	}

	// Ring the slot doorbell with the endpoint number
	kernelDebug(debug_usb, "XHCI ring endpoint 0x%02x doorbell", endpoint);
	if (endpoint)
		xhci->dbRegs->doorbell[slot->num] =
			(((endpoint & 0xF) * 2) + (endpoint >> 7));
	else
		xhci->dbRegs->doorbell[slot->num] = 1;

	// Unlock the controller while we wait
	kernelLockRelease(&controller->lock);

	// Wait until the transfer has completed
	kernelDebug(debug_usb, "XHCI wait for transaction complete");

	currTime = kernelCpuGetMs();
	endTime = (currTime + timeout);

	while (currTime <= endTime)
	{
		memset((void *) &eventTrb, 0, sizeof(xhciTrb));

		if (!getEvent(xhci, 0, &eventTrb, 1) &&
			((eventTrb.typeFlags & XHCI_TRBTYPE_MASK) ==
				XHCI_TRBTYPE_TRANSFER))
		{
			kernelDebug(debug_usb, "XHCI got transfer event for TRB 0x%08x",
				(eventTrb.paramLo & ~0xFU));

			kernelDebug(debug_usb, "XHCI completion code %d",
				((eventTrb.status & XHCI_TRBCOMP_MASK) >> 24));

			if ((eventTrb.status & XHCI_TRBCOMP_MASK) != XHCI_TRBCOMP_SUCCESS)
			{
				kernelDebugError("TRB error: %d (%s)",
					((eventTrb.status & XHCI_TRBCOMP_MASK) >> 24),
					debugTrbCompletion2String(&eventTrb));
			}

			if ((eventTrb.paramLo & ~0xFU) == trbPhysical(transRing, destTrb))
				break;
		}

		currTime = kernelCpuGetMs();
	}

	if (currTime > endTime)
	{
		kernelError(kernel_error, "No transfer event received");
		return (status = ERR_TIMEOUT);
	}

	kernelDebug(debug_usb, "XHCI transaction finished");

	// Copy the completion event TRB back to the last transfer TRB
	memcpy((void *) &trbs[numTrbs - 1], (void *) &eventTrb, sizeof(xhciTrb));

	return (status = 0);
}


static int controlBulkTransfer(usbController *controller, xhciSlot *slot,
	usbTransaction *trans, unsigned maxPacketSize, unsigned timeout)
{
	int status = 0;
	unsigned numTrbs = 0;
	unsigned numDataTrbs = 0;
	xhciTrb trbs[XHCI_TRANSRING_SIZE];
	xhciSetupTrb *setupTrb = NULL;
	unsigned bytesToTransfer = 0;
	unsigned buffPtr = 0;
	unsigned doBytes = 0;
	unsigned remainingPackets = 0;
	xhciTrb *dataTrbs = NULL;
	xhciTrb *statusTrb = NULL;
	unsigned trbCount;

	kernelDebug(debug_usb, "XHCI control/bulk transfer for endpoint "
		"0x%02x, maxPacketSize=%u", trans->endpoint, maxPacketSize);

	// Figure out how many TRBs we're going to need for this transfer

	if (trans->type == usbxfer_control)
		// 2 TRBs for the setup and status phases
		numTrbs += 2;

	// Data descriptors?
	if (trans->length)
	{
		numDataTrbs = ((trans->length + (XHCI_TRB_MAXBYTES - 1)) /
			XHCI_TRB_MAXBYTES);

		kernelDebug(debug_usb, "XHCI data payload of %u requires %d "
			"descriptors", trans->length, numDataTrbs);

		numTrbs += numDataTrbs;
	}

	kernelDebug(debug_usb, "XHCI transfer requires %d descriptors", numTrbs);

	if (numTrbs > XHCI_TRANSRING_SIZE)
	{
		kernelDebugError("Number of TRBs exceeds maximum allowed per transfer "
			"(%d)", XHCI_TRANSRING_SIZE);
		return (status = ERR_RANGE);
	}

	memset((void *) &trbs, 0, (numTrbs * sizeof(xhciTrb)));

	if (trans->type == usbxfer_control)
	{
		// Set up the device request.  The setup stage is a single-TRB TD, so
		// it is not chained to the data or status stages

		// Get the TRB for the setup stage
		setupTrb = (xhciSetupTrb *) &trbs[0];

		// The device request goes straight into the initial part of the
		// setup TRB as immediate data
		status = kernelUsbSetupDeviceRequest(trans, (usbDeviceRequest *)
			&setupTrb->request);
		if (status < 0)
			return (status);

		setupTrb->intTargetTransLen = sizeof(usbDeviceRequest); // 8!!!
		setupTrb->typeFlags = (XHCI_TRBTYPE_SETUPSTG | XHCI_TRBFLAG_IMMEDDATA);

		// Transfer type depends on data stage and direction
		if (trans->length)
		{
			if (trans->pid == USB_PID_IN)
				setupTrb->control = 3;
			else
				setupTrb->control = 2;
		}
	}

	// If there is a data stage, set up the TRB(s) for the data.  The data
	// stage is its own TD, distinct from the setup and status stages (in the
	// control transfer case), so they are all chained together, but the last
	// TRB is not chained to anything.
	if (trans->length)
	{
		buffPtr = (unsigned) kernelPageGetPhysical((((unsigned) trans->buffer <
			KERNEL_VIRTUAL_ADDRESS)? kernelCurrentProcess->processId :
				KERNELPROCID), trans->buffer);
		if (!buffPtr)
		{
			kernelDebugError("Can't get physical address for buffer at %p",
				trans->buffer);
			return (status = ERR_MEMORY);
		}

		bytesToTransfer = trans->length;

		dataTrbs = &trbs[0];
		if (setupTrb)
			dataTrbs = &trbs[1];

		for (trbCount = 0; trbCount < numDataTrbs; trbCount ++)
		{
			doBytes = min(bytesToTransfer, XHCI_TRB_MAXBYTES);
			remainingPackets = (((bytesToTransfer - doBytes) +
				(maxPacketSize - 1)) / maxPacketSize);

			kernelDebug(debug_usb, "XHCI doBytes=%u remainingPackets=%u",
				doBytes, remainingPackets);

			if (doBytes)
			{
				// Set the data TRB
				dataTrbs[trbCount].paramLo = buffPtr;
				dataTrbs[trbCount].status =
					((min(remainingPackets, 31) << 17) | doBytes);
			}

			// Control transfers use 'data stage' TRBs for the first data TRB,
			// and 'normal' TRBs for the rest.  Bulk transfers use 'normal'
			// for all
			if ((trans->type == usbxfer_control) && !trbCount)
			{
				dataTrbs[trbCount].typeFlags = XHCI_TRBTYPE_DATASTG;
				dataTrbs[trbCount].control =
					((trans->pid == USB_PID_IN)? 1 : 0);
			}
			else
				dataTrbs[trbCount].typeFlags = XHCI_TRBTYPE_NORMAL;

			// Chain all but the last TRB
			if (trbCount < (numDataTrbs - 1))
				dataTrbs[trbCount].typeFlags |= XHCI_TRBFLAG_CHAIN;

			buffPtr += doBytes;
			bytesToTransfer -= doBytes;
		}
	}

	if (trans->type == usbxfer_control)
	{
		// Set up the TRB for the status stage

		statusTrb = &trbs[numTrbs - 1];

		statusTrb->typeFlags = XHCI_TRBTYPE_STATUSSTG;

		// Direction flag depends on data stage and direction
		if (trans->length)
		{
			// If there's data, status direction is opposite
			if (trans->pid == USB_PID_OUT)
				statusTrb->control = 1; // in
		}
		else
			// No data, status direction is always in
			statusTrb->control = 1; // in
	}

	// Queue the TRBs in the endpoint's transfer ring
	status = transfer(controller, slot, trans->endpoint, timeout, numTrbs,
		trbs);
	if (status < 0)
		return (status);

	if ((trbs[numTrbs - 1].status & XHCI_TRBCOMP_MASK) != XHCI_TRBCOMP_SUCCESS)
	{
		// If it's bulk, we allow short packet
		if ((trans->type == usbxfer_bulk) &&
			(trbs[numTrbs - 1].status & XHCI_TRBCOMP_MASK) ==
				XHCI_TRBCOMP_SHORTPACKET)
		{
			kernelDebug(debug_usb, "XHCI short packet allowed");
		}
		else
		{
			kernelError(kernel_error, "Transfer failed, status=%d",
				((trbs[numTrbs - 1].status & XHCI_TRBCOMP_MASK) >> 24));
			return (status = ERR_IO);
		}
	}

	if (trans->length)
	{
		// Return the number of bytes transferred
		trans->bytes = (trans->length - (trbs[numTrbs - 1].status & 0xFFFFFF));
		kernelDebug(debug_usb, "XHCI transferred %u of %u requested bytes",
			trans->bytes, trans->length);
	}

	return (status = 0);
}


static int recordHubAttrs(xhciData *xhci, xhciSlot *slot, usbHubDesc *hubDesc)
{
	// If we have discovered that a device is a hub, we need to tell the
	// controller about that.

	int status = 0;
	xhciTrb cmdTrb;

	kernelDebug(debug_usb, "XHCI record hub attributes");

	slot->inputCtxt->inputCtrlCtxt.add = BIT(0);
	slot->inputCtxt->inputCtrlCtxt.drop = 0;

	// Set the 'hub' flag
	slot->inputCtxt->devCtxt.slotCtxt.entFlagsSpeedRoute |= XHCI_SLTCTXT_HUB;

	// Set the number of ports
	slot->inputCtxt->devCtxt.slotCtxt.numPortsPortLat |=
		((hubDesc->numPorts << 24) & XHCI_SLTCTXT_NUMPORTS);

	// Set the TT Think Time
	slot->inputCtxt->devCtxt.slotCtxt.targetTT |=
		(((hubDesc->hubChars & USB_HUBCHARS_TTT_V2) << 11) & XHCI_SLTCTXT_TTT);

	kernelDebug(debug_usb, "XHCI numPorts=%d", hubDesc->numPorts);

	// Send the 'configure endpoint' command
	memset((void *) &cmdTrb, 0, sizeof(xhciTrb));
	cmdTrb.paramLo = slot->inputCtxtPhysical;
	cmdTrb.typeFlags = XHCI_TRBTYPE_CFGENDPT;
	cmdTrb.control = (slot->num << 8);

	status = command(xhci, &cmdTrb);
	if (status < 0)
		return (status);

	if ((cmdTrb.status & XHCI_TRBCOMP_MASK) != XHCI_TRBCOMP_SUCCESS)
	{
		kernelError(kernel_error, "Command error %d configuring device slot",
			((cmdTrb.status & XHCI_TRBCOMP_MASK) >> 24));
		return (status = ERR_IO);
	}

	return (status = 0);
}


static int controlTransfer(usbController *controller, usbDevice *usbDev,
	usbTransaction *trans, unsigned timeout)
{
	int status = 0;
	xhciData *xhci = controller->data;
	xhciSlot *slot = NULL;
	int standard = 0;
	unsigned maxPacketSize = 0;

	kernelDebug(debug_usb, "XHCI control transfer to controller %d, device %d",
		controller->num, usbDev->address);

	slot = getDevSlot(xhci, usbDev);
	if (!slot)
	{
		kernelError(kernel_error, "Couldn't get device slot");
		return (status = ERR_NOSUCHENTRY);
	}

	// Is it a USB standard request?
	if ((trans->control.requestType & 0x70) == USB_DEVREQTYPE_STANDARD)
		standard = 1;

	// If this is a USB_SET_ADDRESS, we don't send it.  Instead, we tell the
	// controller to put the device into the addressed state.
	if (standard && (trans->control.request == USB_SET_ADDRESS))
	{
		kernelDebug(debug_usb, "XHCI skip sending USB_SET_ADDRESS");
		status = setDevAddress(xhci, slot, usbDev);

		// Finished
		return (status);
	}

	// If we are at the stage of configuring the device, we also need to tell
	// the controller about it.
	if (standard && (trans->control.request == USB_SET_CONFIGURATION))
	{
		status = configDevSlot(xhci, slot, usbDev);
		if (status < 0)
			return (status);

		// Carry on with the transfer
	}

	// Get the maximum packet size for the control endpoint
	maxPacketSize = usbDev->deviceDesc.maxPacketSize0;
	if (!maxPacketSize)
	{
		// If we haven't yet got the descriptors, etc., use 8 as the maximum
		// packet size
		kernelDebug(debug_usb, "XHCI using default maximum endpoint transfer "
			"size 8");
		maxPacketSize = 8;
	}

	status = controlBulkTransfer(controller, slot, trans, maxPacketSize,
		timeout);
	if (status < 0)
		return (status);

	// If this was a 'get hub descriptor' control transfer, we need to spy
	// on it to record a) the fact that it's a hub; b) the number of ports
	if (standard && (trans->control.request == USB_GET_DESCRIPTOR))
	{
		if (((trans->control.value >> 8) == USB_DESCTYPE_HUB) ||
			((trans->control.value >> 8) == USB_DESCTYPE_SSHUB))
		{
			recordHubAttrs(xhci, slot, (usbHubDesc *) trans->buffer);
		}
	}

	return (status = 0);
}


static int bulkTransfer(usbController *controller, usbDevice *usbDev,
	usbTransaction *trans, unsigned timeout)
{
	int status = 0;
	xhciData *xhci = controller->data;
	xhciSlot *slot = NULL;
	usbEndpoint *endpoint = NULL;
	unsigned maxPacketSize = 0;

	kernelDebug(debug_usb, "XHCI bulk transfer to controller %d, device %d, "
		"endpoint 0x%02x", controller->num, usbDev->address, trans->endpoint);

	slot = getDevSlot(xhci, usbDev);
	if (!slot)
	{
		kernelError(kernel_error, "Couldn't get device slot");
		return (status = ERR_NOSUCHENTRY);
	}

	// Get the endpoint descriptor
	endpoint = kernelUsbGetEndpoint(usbDev, trans->endpoint);
	if (!endpoint)
	{
		kernelError(kernel_error, "No such endpoint 0x%02x", trans->endpoint);
		return (status = ERR_NOSUCHFUNCTION);
	}

	// Get the maximum packet size for the endpoint
	maxPacketSize = endpoint->maxPacketSize;
	if (!maxPacketSize)
	{
		kernelError(kernel_error, "Device endpoint 0x%02x has a max packet"
			"size of 0", trans->endpoint);
		return (status = ERR_BADDATA);
	}

	status = controlBulkTransfer(controller, slot, trans, maxPacketSize,
		timeout);

	return (status);
}


static int interruptTransfer(xhciData *xhci, usbDevice *usbDev, int interface,
	int endpoint, unsigned dataLen,
	void (*callback)(usbDevice *, int, void *, unsigned))
{
	// This function is used to schedule an interrupt.

	int status = 0;
	xhciIntrReg *intrReg = NULL;
	kernelIoMemory ioMem;

	kernelDebug(debug_usb, "XHCI register interrupt transfer for endpoint "
		"0x%02x", endpoint);

	// Get memory to store information about the interrupt registration */
	intrReg = kernelMalloc(sizeof(xhciIntrReg));
	if (!intrReg)
		return (status = ERR_MEMORY);

	memset(intrReg, 0, sizeof(xhciIntrReg));
	intrReg->usbDev = usbDev;
	intrReg->interface = interface;
	intrReg->endpoint = endpoint;

	// Get the device slot
	intrReg->slot = getDevSlot(xhci, usbDev);
	if (!intrReg->slot)
	{
		status = ERR_NOSUCHENTRY;
		goto out;
	}

	// Get buffer memory
	status = kernelMemoryGetIo(dataLen, 0, &ioMem);
	if (status < 0)
		goto out;

	intrReg->buffer = ioMem.virtual;
	intrReg->dataLen = dataLen;

	// Set up the TRB
	intrReg->trb.paramLo = ioMem.physical;
	intrReg->trb.status = dataLen;
	intrReg->trb.typeFlags = (XHCI_TRBTYPE_NORMAL | XHCI_TRBFLAG_INTONCOMP);

	kernelDebug(debug_usb, "XHCI buffer=0x%08x len=%u flags=0x%04x",
		intrReg->trb.paramLo, intrReg->trb.status, intrReg->trb.typeFlags);

	intrReg->callback = callback;

	// Add the interrupt registration to the controller's list.
	status = kernelLinkedListAdd(&xhci->intrRegs, intrReg);
	if (status < 0)
		goto out;

	// Enqueue the TRB
	intrReg->queuedTrb = queueIntrDesc(xhci, intrReg->slot,
		intrReg->endpoint, &intrReg->trb);

	status = 0;

out:
	if (status < 0)
	{
		if (intrReg->buffer)
			kernelMemoryReleaseIo(&ioMem);

		kernelFree(intrReg);
		intrReg = NULL;
	}

	return (status);
}


static void unregisterInterrupt(xhciData *xhci, xhciIntrReg *intrReg)
{
	// Remove an interrupt registration from the controller's list

	kernelIoMemory ioMem;

	kernelDebug(debug_usb, "XHCI remove interrupt registration for device %d, "
		"endpoint 0x%02x", intrReg->usbDev->address, intrReg->endpoint);

	// Remove it from the list
	kernelLinkedListRemove(&xhci->intrRegs, intrReg);

	// Deallocate it
	if (intrReg->buffer)
	{
		ioMem.size = intrReg->dataLen;
		ioMem.physical = intrReg->trb.paramLo;
		ioMem.virtual = intrReg->buffer;
		kernelMemoryReleaseIo(&ioMem);
	}

	kernelFree(intrReg);
	return;
}


static int waitPortChangeEvent(xhciData *xhci, int anyPort, int portNum,
	unsigned timeout)
{
	// Wait for, and consume, an expected port status change event

	unsigned count;

	kernelDebug(debug_usb, "XHCI try to wait for port change events");

	for (count = 0; count < timeout; count ++)
	{
		if (anyPort)
		{
			if (xhci->portChangedBitmap)
			{
				kernelDebug(debug_usb, "XHCI got any port change event (%dms)",
					count);
				return (1);
			}
		}
		else
		{
			if (xhci->portChangedBitmap & BIT(portNum))
			{
				kernelDebug(debug_usb, "XHCI got port %d change event (%dms)",
					portNum, count);
				xhci->portChangedBitmap &= ~BIT(portNum);
				return (1);
			}
		}

		kernelCpuSpinMs(1);
	}

	// Timed out
	kernelDebugError("No port change event received");
	return (0);
}


static inline void setPortStatusBits(xhciData *xhci, int portNum,
	unsigned bits)
{
	// Set (or clear write-1-to-clear) the requested  port status bits, without
	// affecting the others

	xhci->opRegs->portRegSet[portNum].portsc =
		((xhci->opRegs->portRegSet[portNum].portsc &
			~(XHCI_PORTSC_ROMASK | XHCI_PORTSC_RW1CMASK)) | bits);
}


static int portReset(xhciData *xhci, int portNum)
{
	// Reset the port, with the appropriate delays, etc.

	int status = 0;
	int count;

	kernelDebug(debug_usb, "XHCI port reset");

	// Set the port 'reset' bit
	setPortStatusBits(xhci, portNum, XHCI_PORTSC_PORTRESET);

	// Give a little delay for the reset to take effect, before we start
	// looking for the 'clear'.
	kernelCpuSpinMs(1);

	// Wait for it to read as clear
	for (count = 0; count < 100; count ++)
	{
		// Clear all port status 'change' bits
		setPortStatusBits(xhci, portNum, XHCI_PORTSC_CHANGES);

		if (!(xhci->opRegs->portRegSet[portNum].portsc &
			XHCI_PORTSC_PORTRESET))
		{
			kernelDebug(debug_usb, "XHCI port reset took %dms", count);
			break;
		}

		kernelCpuSpinMs(1);
	}

	// Clear?
	if (xhci->opRegs->portRegSet[portNum].portsc & XHCI_PORTSC_PORTRESET)
	{
		kernelError(kernel_warn, "Port reset bit was not cleared");
		status = ERR_TIMEOUT;
		goto out;
	}

	// Try to wait for a 'port status change' event on the event ring.
	// Once we get this, we know that the port is enabled.
	if (!waitPortChangeEvent(xhci, 0, portNum, 200))
		kernelDebugError("No port change event");

	// Delay 10ms
	kernelCpuSpinMs(10);

	//debugPortStatus(xhci, portNum);

	// Return success
	status = 0;

out:
	kernelDebug(debug_usb, "XHCI port reset %s",
		(status? "failed" : "success"));
	return (status);
}


static int portConnected(usbController *controller, int portNum, int hotPlug)
{
	// This function is called whenever we notice that a port has indicated
	// a new connection.

	int status = 0;
	xhciData *xhci = controller->data;
	xhciDevSpeed xhciSpeed = xhcispeed_unknown;
	usbDevSpeed usbSpeed = usbspeed_unknown;
	int count;

	kernelDebug(debug_usb, "XHCI port %d connected", portNum);

	// Clear all port status 'change' bits
	setPortStatusBits(xhci, portNum, XHCI_PORTSC_CHANGES);

	// USB3 devices should automatically transition the port to the 'enabled'
	// state, but older devices will need to go through the port reset
	// procedure.
	for (count = 0; count < 100; count ++)
	{
		if (xhci->opRegs->portRegSet[portNum].portsc & XHCI_PORTSC_PORTENABLED)
		{
			kernelDebug(debug_usb, "XHCI port auto-enable took %dms", count);
			break;
		}

		kernelCpuSpinMs(1);
	}

	// Did the port become enabled?
	if (!(xhci->opRegs->portRegSet[portNum].portsc & XHCI_PORTSC_PORTENABLED))
	{
		kernelDebug(debug_usb, "XHCI port did not auto-enable");

		status = portReset(xhci, portNum);
		if (status < 0)
			return (status);

		// Clear all port status 'change' bits
		setPortStatusBits(xhci, portNum, XHCI_PORTSC_CHANGES);

		for (count = 0; count < 100; count ++)
		{
			if (xhci->opRegs->portRegSet[portNum].portsc &
				XHCI_PORTSC_PORTENABLED)
			{
				kernelDebug(debug_usb, "XHCI port enable took %dms", count);
				break;
			}

			kernelCpuSpinMs(1);
		}
	}

	// Did the port become enabled?
	if (!(xhci->opRegs->portRegSet[portNum].portsc & XHCI_PORTSC_PORTENABLED))
	{
		kernelDebugError("Port did not transition to the enabled state");
		return (status = ERR_IO);
	}

	// Determine the speed of the device
	xhciSpeed = (xhciDevSpeed)((xhci->opRegs->portRegSet[portNum].portsc &
		XHCI_PORTSC_PORTSPEED) >> 10);

	kernelDebug(debug_usb, "XHCI connection speed: %s",
		debugXhciSpeed2String(xhciSpeed));

	switch (xhciSpeed)
	{
		case xhcispeed_low:
			usbSpeed = usbspeed_low;
			break;
		case xhcispeed_full:
			usbSpeed = usbspeed_full;
			break;
		case xhcispeed_high:
			usbSpeed = usbspeed_high;
			break;
		case xhcispeed_super:
			usbSpeed = usbspeed_super;
			break;
		default:
			usbSpeed = usbspeed_unknown;
			break;
	}

	kernelDebug(debug_usb, "XHCI USB connection speed: %s",
		usbDevSpeed2String(usbSpeed));

	status = kernelUsbDevConnect(controller, &controller->hub, portNum,
		usbSpeed, hotPlug);
	if (status < 0)
		kernelError(kernel_error, "Error enumerating new USB device");

	return (status);
}


static void portDisconnected(usbController *controller, int portNum)
{
	// This function is called whenever we notice that a port has indicated
	// a device disconnection.

	kernelDebug(debug_usb, "XHCI port %d disconnected", portNum);

	// Tell the USB functions that the device disconnected.  This will call us
	// back to tell us about all affected devices - there might be lots if
	// this was a hub - so we can disable slots, etc., then.
	kernelUsbDevDisconnect(controller, &controller->hub, portNum);
}


static void detectPortChanges(usbController *controller, int portNum,
	int hotplug)
{
	xhciData *xhci = controller->data;

	kernelDebug(debug_usb, "XHCI check port %d", portNum);

	if (!controller->hub.doneColdDetect ||
		(xhci->opRegs->portRegSet[portNum].portsc & XHCI_PORTSC_CONNECT_CH))
	{
		if (xhci->opRegs->portRegSet[portNum].portsc & XHCI_PORTSC_CONNECTED)
		{
			// Do port connection setup
			portConnected(controller, portNum, hotplug);
		}
		else
		{
			// Do port connection tear-down
			portDisconnected(controller, portNum);
		}
	}

	// Clear all port status 'change' bits
	setPortStatusBits(xhci, portNum, XHCI_PORTSC_CHANGES);
}


static void doDetectDevices(usbController *controller, int hotplug)
{
	// This function gets called to check for device connections (either cold-
	// plugged ones at boot time, or hot-plugged ones during operations.

	xhciData *xhci = controller->data;
	int count;

	kernelDebug(debug_usb, "XHCI check non-USB3 ports");

	// Check to see whether any non-USB3 ports are showing a connection
	for (count = 0; count < xhci->numPorts; count ++)
	{
		if (xhci->portProtos[count] >= usbproto_usb3)
			continue;

		detectPortChanges(controller, count, hotplug);
	}

	// It can happen that USB3 protocol ports suddenly show connections after
	// we have attempted to reset a corresponding USB2 protocol port.

	kernelDebug(debug_usb, "XHCI check USB3 ports");

	// Now check any USB3 protocol ports
	for (count = 0; count < xhci->numPorts; count ++)
	{
		if (xhci->portProtos[count] < usbproto_usb3)
			continue;

		detectPortChanges(controller, count, hotplug);
	}

	xhci->portChangedBitmap = 0;

	return;
}


static int processExtCaps(xhciData *xhci)
{
	// If the controller has extended capabilities, such as legacy support that
	// requires a handover between the BIOS and the OS, we do that here.

	int status = 0;
	xhciExtendedCaps *extCap = NULL;
	xhciLegacySupport *legSupp = NULL;
	xhciSupportedProtocol *suppProto = NULL;
	int count;

	// Examine the extended capabilities
	extCap = ((void *) xhci->capRegs +
		((xhci->capRegs->hccparams & XHCI_HCCP_EXTCAPPTR) >> 14));

	while (1)
	{
		kernelDebug(debug_usb, "XHCI extended capability %d", extCap->id);

		// Is there legacy support?
		if (extCap->id == XHCI_EXTCAP_LEGACYSUPP)
		{
			kernelDebug(debug_usb, "XHCI legacy support implemented");

			legSupp = (xhciLegacySupport *) extCap;

			// Does the BIOS claim ownership of the controller?
			if (legSupp->legSuppCap & XHCI_LEGSUPCAP_BIOSOWND)
			{
				kernelDebug(debug_usb, "XHCI BIOS claims ownership, "
					"cap=0x%08x contStat=0x%08x", legSupp->legSuppCap,
					legSupp->legSuppContStat);

				// Attempt to take over ownership
				legSupp->legSuppCap |= XHCI_LEGSUPCAP_OSOWNED;

				// Wait for the BIOS to release ownership, if applicable
				for (count = 0; count < 200; count ++)
				{
					if ((legSupp->legSuppCap & XHCI_LEGSUPCAP_OSOWNED) &&
						!(legSupp->legSuppCap & XHCI_LEGSUPCAP_BIOSOWND))
					{
						kernelDebug(debug_usb, "XHCI OS ownership took %dms",
							count);
						break;
					}

					kernelCpuSpinMs(1);
				}

				// Do we have ownership?
				if (!(legSupp->legSuppCap & XHCI_LEGSUPCAP_OSOWNED) ||
					(legSupp->legSuppCap & XHCI_LEGSUPCAP_BIOSOWND))
				{
					kernelDebugError("BIOS did not release ownership");
				}
			}
			else
				kernelDebug(debug_usb, "XHCI BIOS does not claim ownership");

			// Make sure any SMIs are acknowledged and disabled
			legSupp->legSuppContStat = 0xE0000000;
			kernelDebug(debug_usb, "XHCI now cap=0x%08x, contStat=0x%08x",
				legSupp->legSuppCap, legSupp->legSuppContStat);
		}

		else if (extCap->id == XHCI_EXTCAP_SUPPPROTO)
		{
			char name[5];

			suppProto = (xhciSupportedProtocol *) extCap;

			strncpy(name, (char *) &suppProto->suppProtName, 4);
			name[4] = '\0';

			kernelDebug(debug_usb, "XHCI supported protocol \"%s\" %d.%d "
				"startPort=%d numPorts=%d",
				name, (suppProto->suppProtCap >> 24),
				((suppProto->suppProtCap >> 16) & 0xFF),
				((suppProto->suppProtPorts & 0xFF) - 1),
				((suppProto->suppProtPorts >> 8) & 0xFF));

			if (!strncmp(name, "USB ", 4))
			{
				for (count = ((suppProto->suppProtPorts & 0xFF) - 1);
					(count < (int)(((suppProto->suppProtPorts & 0xFF) - 1) +
						((suppProto->suppProtPorts >> 8) & 0xFF))); count ++)
				{
					if ((suppProto->suppProtCap >> 24) >= 2)
						xhci->portProtos[count] = usbproto_usb2;
					if ((suppProto->suppProtCap >> 24) >= 3)
						xhci->portProtos[count] = usbproto_usb3;

					kernelDebug(debug_usb, "XHCI port %d is protocol %d",
						count, xhci->portProtos[count]);
				}
			}
		}

		if (extCap->next)
			extCap = ((void *) extCap + (extCap->next << 2));
		else
			break;
	}

	return (status = 0);
}


static int allocScratchPadBuffers(xhciData *xhci, unsigned *scratchPadPhysical)
{
	int status = 0;
	int numScratchPads = (((xhci->capRegs->hcsparams2 &
			XHCI_HCSP2_MAXSCRPBUFFSHI) >> 16) |
		((xhci->capRegs->hcsparams2 & XHCI_HCSP2_MAXSCRPBUFFSLO) >> 27));
	kernelIoMemory ioMem;
	unsigned long long *scratchPadBufferArray = NULL;
	unsigned buffer = NULL;
	int count;

	*scratchPadPhysical = NULL;

	if (!numScratchPads)
	{
		kernelDebug(debug_usb, "XHCI no scratchpad buffers required");
		return (status = 0);
	}

	kernelDebug(debug_usb, "XHCI allocating %d scratchpad buffers of %u",
		numScratchPads, xhci->pageSize);

	// Allocate the array for pointers
	status = kernelMemoryGetIo((numScratchPads * sizeof(unsigned long long)),
		64 /* alignment */, &ioMem);
	if (status < 0)
		return (status);

	scratchPadBufferArray = ioMem.virtual;

	// Allocate each buffer.  We don't access these (they're purely for the
	// controller) so we don't need to allocate them as I/O memory.
	for (count = 0; count < numScratchPads; count ++)
	{
		buffer = kernelMemoryGetPhysical(xhci->pageSize,
			xhci->pageSize /* alignment */, "xhci scratchpad");
		if (!buffer)
		{
			status = ERR_MEMORY;
			goto out;
		}

		scratchPadBufferArray[count] = (unsigned long long) buffer;
	}

	*scratchPadPhysical = ioMem.physical;
	status = 0;

out:
	if ((status < 0) && scratchPadBufferArray)
	{
		for (count = 0; count < numScratchPads; count ++)
		{
			if (scratchPadBufferArray[count])
				kernelMemoryReleasePhysical((unsigned)
					scratchPadBufferArray[count]);
		}

		kernelMemoryReleaseIo(&ioMem);
	}

	return (status);
}


static int initInterrupter(xhciData *xhci)
{
	// Set up the numbered interrupter

	int status = 0;
	kernelIoMemory ioMem;
	xhciEventRingSegTable *segTable = NULL;
	void *segTablePhysical = NULL;

	memset(&ioMem, 0, sizeof(kernelIoMemory));

	kernelDebug(debug_usb, "XHCI initialize interrupter %d (max=%d)",
		xhci->numIntrs, ((xhci->capRegs->hcsparams1 &
			XHCI_HCSP1_MAXINTRPTRS) >> 8));

	// Expand the array for holding pointers to event rings
	xhci->eventRings = kernelRealloc(xhci->eventRings,
		((xhci->numIntrs + 1) * sizeof(xhciTrbRing *)));
	if (!xhci->eventRings)
	{
		status = ERR_MEMORY;
		goto err_out;
	}

	// Allocate a TRB ring for events
	xhci->eventRings[xhci->numIntrs] =
		allocTrbRing(XHCI_EVENTRING_SIZE, 0 /* not circular */);
	if (!xhci->eventRings[xhci->numIntrs])
	{
		status = ERR_MEMORY;
		goto err_out;
	}

	kernelDebug(debug_usb, "XHCI eventRings[%d]->trbsPhysical=0x%08x",
		xhci->numIntrs, xhci->eventRings[xhci->numIntrs]->trbsPhysical);

	// Get some aligned memory for the segment table
	status = kernelMemoryGetIo(sizeof(xhciEventRingSegTable),
		 64 /* alignment */, &ioMem);
	if (status < 0)
		goto err_out;

	segTable = ioMem.virtual;
	segTablePhysical = (void *) ioMem.physical;

	// Point the segment table to the TRB ring
	segTable->baseAddrLo = xhci->eventRings[xhci->numIntrs]->trbsPhysical;
	segTable->segSize = XHCI_EVENTRING_SIZE;

	// Update the interrupter's register set to point to the segment table
	xhci->rtRegs->intrReg[xhci->numIntrs].intrMod = 0x00000FA0; // 1ms
	xhci->rtRegs->intrReg[xhci->numIntrs].evtRngSegTabSz = 1;
	xhci->rtRegs->intrReg[xhci->numIntrs].evtRngDeqPtrLo =
		xhci->eventRings[xhci->numIntrs]->trbsPhysical;
	xhci->rtRegs->intrReg[xhci->numIntrs].evtRngDeqPtrHi = 0;
	xhci->rtRegs->intrReg[xhci->numIntrs].evtRngSegBaseLo =
		(unsigned) segTablePhysical;
	xhci->rtRegs->intrReg[xhci->numIntrs].evtRngSegBaseHi = 0;
	xhci->rtRegs->intrReg[xhci->numIntrs].intrMan = XHCI_IMAN_INTSENABLED;

	xhci->numIntrs += 1;

	//debugRuntimeRegs(xhci);

	return (status = 0);

err_out:
	if (ioMem.virtual)
		kernelMemoryReleaseIo(&ioMem);

	if (xhci->eventRings[xhci->numIntrs])
		deallocTrbRing(xhci->eventRings[xhci->numIntrs]);

	return (status);
}


static int setup(xhciData *xhci)
{
	// Allocate things, and set up any global controller registers prior to
	// changing the controller to the 'running' state

	int status = 0;
	unsigned devCtxtPhysPtrsMemSize = 0;
	kernelIoMemory ioMem;
	void *devCtxtPhysPtrsPhysical = NULL;
	unsigned scratchPadBufferArray = 0;

	memset(&ioMem, 0, sizeof(kernelIoMemory));

#if defined(DEBUG)
	// Check the sizes of some structures
	if (sizeof(xhciCtxt) != 32)
	{
		kernelDebugError("sizeof(xhciCtxt) is %u, not 32", sizeof(xhciCtxt));
		status = ERR_ALIGN;
		goto err_out;
	}
	if (sizeof(xhciSlotCtxt) != 32)
	{
		kernelDebugError("sizeof(xhciSlotCtxt) is %u, not 32",
			sizeof(xhciSlotCtxt));
		status = ERR_ALIGN;
		goto err_out;
	}
	if (sizeof(xhciEndpointCtxt) != 32)
	{
		kernelDebugError("sizeof(xhciEndpointCtxt) is %u, not 32",
			sizeof(xhciEndpointCtxt));
		status = ERR_ALIGN;
		goto err_out;
	}
	if (sizeof(xhciInputCtrlCtxt) != 32)
	{
		kernelDebugError("sizeof(xhciInputCtrlCtxt) is %u, not 32",
			sizeof(xhciInputCtrlCtxt));
		status = ERR_ALIGN;
		goto err_out;
	}
	if (sizeof(xhciInputCtxt) != 1056)
	{
		kernelDebugError("sizeof(xhciInputCtxt) is %u, not 1056",
			sizeof(xhciDevCtxt));
		status = ERR_ALIGN;
		goto err_out;
	}
	if (sizeof(xhciDevCtxt) != 1024)
	{
		kernelDebugError("sizeof(xhciDevCtxt) is %u, not 1024",
			sizeof(xhciDevCtxt));
		status = ERR_ALIGN;
		goto err_out;
	}
	if (sizeof(xhciTrb) != 16)
	{
		kernelDebugError("sizeof(xhciTrb) is %u, not 16", sizeof(xhciTrb));
		status = ERR_ALIGN;
		goto err_out;
	}
	if (sizeof(xhciPortRegSet) != 16)
	{
		kernelDebugError("sizeof(xhciPortRegSet) is %u, not 16",
			sizeof(xhciPortRegSet));
		status = ERR_ALIGN;
		goto err_out;
	}
	if (sizeof(xhciOpRegs) != 5120)
	{
		kernelDebugError("sizeof(xhciOpRegs) is %u, not 5120",
			sizeof(xhciOpRegs));
		status = ERR_ALIGN;
		goto err_out;
	}
	if (sizeof(xhciCapRegs) != 28)
	{
		kernelDebugError("sizeof(xhciCapRegs) is %u, not 28",
			sizeof(xhciCapRegs));
		status = ERR_ALIGN;
		goto err_out;
	}
	if (sizeof(xhciIntrRegSet) != 32)
	{
		kernelDebugError("sizeof(xhciIntrRegSet) is %u, not 32",
			sizeof(xhciIntrRegSet));
		status = ERR_ALIGN;
		goto err_out;
	}
	if (sizeof(xhciRuntimeRegs) != 32)

	{
		kernelDebugError("sizeof(xhciRuntimeRegs) is %u, not 32",
			sizeof(xhciRuntimeRegs));
		status = ERR_ALIGN;

		goto err_out;
	}

	if (sizeof(xhciDoorbellRegs) != 1024)
	{

		kernelDebugError("sizeof(xhciDoorbellRegs) is %u, not 1024",
			sizeof(xhciDoorbellRegs));

		status = ERR_ALIGN;
		goto err_out;

	}
	if (sizeof(xhciEventRingSegTable) != 16)
	{
		kernelDebugError("sizeof(xhciEventRingSegTable) is %u, not 16",
			sizeof(xhciEventRingSegTable));
		status = ERR_ALIGN;
		goto err_out;
	}
#endif

	// Program the max device slots enabled field in the config register to
	// enable the device slots that system software is going to use
	xhci->opRegs->config = xhci->numDevSlots;

	// Program the device context base address array pointer

	// How much memory is needed for the (64-bit) pointers to the device
	// contexts?
	devCtxtPhysPtrsMemSize = ((xhci->numDevSlots + 1) *
		sizeof(unsigned long long));

	// Request memory for an aligned array of pointers to device contexts
	status = kernelMemoryGetIo(devCtxtPhysPtrsMemSize, 64 /* alignment */,
		&ioMem);
	if (status < 0)
		goto err_out;

	xhci->devCtxtPhysPtrs = ioMem.virtual;
	devCtxtPhysPtrsPhysical = (void *) ioMem.physical;

	kernelDebug(debug_usb, "XHCI device context base array memory=%p",
		devCtxtPhysPtrsPhysical);

	// Allocate the scratchpad buffers requested by the controller
	status = allocScratchPadBuffers(xhci, &scratchPadBufferArray);
	if (status < 0)
		goto err_out;

	if (scratchPadBufferArray)
		xhci->devCtxtPhysPtrs[0] = scratchPadBufferArray;

	// Set the device context base address array pointer in the host
	// controller register
	xhci->opRegs->dcbaapLo = (unsigned) devCtxtPhysPtrsPhysical;
	xhci->opRegs->dcbaapHi = 0;

	// Allocate the command ring
	xhci->commandRing = allocTrbRing(XHCI_COMMANDRING_SIZE,
		1 /* circular */);
	if (!xhci->commandRing)
	{
		status = ERR_MEMORY;
		goto err_out;
	}

	kernelDebug(debug_usb, "XHCI commandRing->trbsPhysical=0x%08x",
		xhci->commandRing->trbsPhysical);

	// Define the command ring dequeue pointer by programming the command ring
	// control register with the 64-bit address of the first TRB in the command
	// ring
	xhci->opRegs->cmdrctrlLo =
		(xhci->commandRing->trbsPhysical | XHCI_CRCR_RINGCYCSTATE);
	xhci->opRegs->cmdrctrlHi = 0;

	// Initialize interrupts

	// Initialize each the 1st (primary) interrupter
	status = initInterrupter(xhci);
	if (status < 0)
		goto err_out;

	// Enable the interrupts we're interested in, in the command register;
	// interrupter, and host system error
	xhci->opRegs->cmd |= (XHCI_CMD_HOSTSYSERRENBL | XHCI_CMD_INTERUPTRENBL);

	// Return success
	return (status = 0);

err_out:
	if (xhci->commandRing)
		deallocTrbRing(xhci->commandRing);

	if (ioMem.virtual)
		kernelMemoryReleaseIo(&ioMem);

	return (status);
}


static int setPortPower(xhciData *xhci, int portNum, int on)
{
	int status = 0;
	int count;

	kernelDebug(debug_usb, "XHCI power %s port %d", (on? "on" : "off"),
		portNum);

	if (on && !(xhci->opRegs->portRegSet[portNum].portsc &
		XHCI_PORTSC_PORTPOWER))
	{
		// Set the power on bit and clear all port status 'change' bits
		setPortStatusBits(xhci, portNum,
			(XHCI_PORTSC_CHANGES | XHCI_PORTSC_PORTPOWER));

		// Wait for it to read as set
		for (count = 0; count < 20; count ++)
		{
			if (xhci->opRegs->portRegSet[portNum].portsc &
				XHCI_PORTSC_PORTPOWER)
			{
				kernelDebug(debug_usb, "XHCI powering up took %dms", count);
				break;
			}

			kernelCpuSpinMs(1);
		}

		if (!(xhci->opRegs->portRegSet[portNum].portsc &
			XHCI_PORTSC_PORTPOWER))
		{
			kernelError(kernel_error, "XHCI: unable to power on port %d",
				portNum);
			return (status = ERR_IO);
		}
	}
	else if (!on)
	{
		// Would we ever need this?
		kernelDebugError("Port power off not implemented");
		return (status = ERR_NOTIMPLEMENTED);
	}

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
	xhciData *xhci = NULL;
	int count;

	// Check params
	if (!controller)
	{
		kernelError(kernel_error, "NULL parameter");
		return (status = ERR_NULLPARAMETER);
	}

	xhci = controller->data;

	// Try to make sure the controller is stopped
	status = startStop(xhci, 0);
	if (status < 0)
		return (status);

	kernelDebug(debug_usb, "XHCI reset controller");

	// Set host controller reset
	xhci->opRegs->cmd |= XHCI_CMD_HCRESET;

	// Wait until the host controller clears it
	for (count = 0; count < 2000; count ++)
	{
		if (!(xhci->opRegs->cmd & XHCI_CMD_HCRESET))
		{
			kernelDebug(debug_usb, "XHCI resetting controller took %dms",
				count);
			break;
		}

		kernelCpuSpinMs(1);
	}

	// Clear?
	if (xhci->opRegs->cmd & XHCI_CMD_HCRESET)
	{
		kernelError(kernel_error, "Controller did not clear reset bit");
		status = ERR_TIMEOUT;
	}

	kernelDebug(debug_usb, "XHCI controller reset %s",
		(status? "failed" : "successful"));

	return (status);
}


static int interrupt(usbController *controller)
{
	// This function gets called when the controller issues an interrupt

	int gotInterrupt = 0;
	xhciData *xhci = controller->data;

	// See whether the status register indicates any of the interrupts we
	// enabled
	if (xhci->opRegs->stat & (XHCI_STAT_HOSTCTRLERR | XHCI_STAT_INTERRUPTMASK))
	{
		kernelDebug(debug_usb, "XHCI controller %d interrupt, status=0x%08x",
			controller->num, xhci->opRegs->stat);
	}

	if (xhci->opRegs->stat & XHCI_STAT_HOSTSYSERROR)
	{
		kernelError(kernel_error, "Host system error interrupt");
		debugOpRegs(xhci);

		// Clear the host system error bit
		clearStatusBits(xhci, XHCI_STAT_HOSTSYSERROR);

		gotInterrupt = 1;
	}

	else if (xhci->opRegs->stat & XHCI_STAT_EVENTINTR)
    {
		kernelDebug(debug_usb, "XHCI event interrupt");

		// Clear the event interrupt bit before processing the interrupters
		clearStatusBits(xhci, XHCI_STAT_EVENTINTR);

		eventInterrupt(xhci);

		gotInterrupt = 1;
	}

	else if (xhci->opRegs->stat & XHCI_STAT_HOSTCTRLERR)
    {
		kernelError(kernel_error, "Host controller error");
		debugOpRegs(xhci);
	}

	if (gotInterrupt)
		return (0);
	else
		return (ERR_NODATA);
}


static int queue(usbController *controller, usbDevice *usbDev,
	usbTransaction *trans, int numTrans)
{
	// This function contains the intelligence necessary to initiate a set of
	// transactions (all phases)

	int status = 0;
	unsigned timeout = 0;
	int transCount;

	kernelDebug(debug_usb, "XHCI queue %d transaction%s for controller %d, "
		"device %d", numTrans, ((numTrans > 1)? "s" : ""), controller->num,
		usbDev->address);

	if (!controller || !usbDev || !trans)
	{
		kernelError(kernel_error, "NULL parameter");
		return (status = ERR_NULLPARAMETER);
	}

	// Loop for each transaction
	for (transCount = 0; transCount < numTrans; transCount ++)
	{
		timeout = trans[transCount].timeout;
		if (!timeout)
			timeout = USB_STD_TIMEOUT_MS;

		// Lock the controller.  It's the responsibility of the functions
		// called below to unlock as appropriate whilst waiting for I/O
		status = kernelLockGet(&controller->lock);
		if (status < 0)
		{
			kernelError(kernel_error, "Can't get controller lock");
			break;
		}

		switch (trans[transCount].type)
		{
			case usbxfer_control:
				status = controlTransfer(controller, usbDev,
					&trans[transCount], timeout);
				break;

			case usbxfer_bulk:
				status = bulkTransfer(controller, usbDev,
					&trans[transCount], timeout);
				break;

			default:
				kernelError(kernel_error, "Illegal transaction type for "
					"queueing");
				status = ERR_INVALID;
				break;
		}

		if (status < 0)
			break;
	}

	// If the controller is still locked (due to errors or whatever else),
	// unlock it.
	if (kernelLockVerify(&controller->lock) > 0)
		kernelLockRelease(&controller->lock);

	return (status);
}


static int schedInterrupt(usbController *controller, usbDevice *usbDev,
	int interface, unsigned char endpoint,
	int interval __attribute__((unused)), unsigned maxLen,
	void (*callback)(usbDevice *, int, void *, unsigned))
{
	// This function is used to schedule an interrupt.

	int status = 0;
	xhciData *xhci = NULL;

	// Check params
	if (!controller || !usbDev || !callback)
	{
		kernelError(kernel_error, "NULL parameter");
		return (status = ERR_NULLPARAMETER);
	}

	kernelDebug(debug_usb, "XHCI schedule interrupt for address %d endpoint "
		"%02x len %u", usbDev->address, endpoint, maxLen);

	xhci = controller->data;

	// Lock the controller.
	status = kernelLockGet(&controller->lock);
	if (status < 0)
	{
		kernelError(kernel_error, "Can't get controller lock");
		return (status);
	}

	status = interruptTransfer(xhci, usbDev, interface, endpoint, maxLen,
		callback);

	kernelLockRelease(&controller->lock);
	return (status);
}


static int deviceRemoved(usbController *controller, usbDevice *usbDev)
{
	int status = 0;
	xhciData *xhci = controller->data;
	xhciSlot *slot = NULL;
	xhciIntrReg *intrReg = NULL;
	kernelLinkedListItem *iter = NULL;

	// Check params
	if (!controller || !usbDev)
	{
		kernelError(kernel_error, "NULL parameter");
		return (status = ERR_NULLPARAMETER);
	}

	kernelDebug(debug_usb, "XHCI device %d removed", usbDev->address);

	xhci = controller->data;

	// Get the device slot
	slot = getDevSlot(xhci, usbDev);
	if (!slot)
		return (status = ERR_NOSUCHENTRY);

	// Disable the slot
	status = deallocSlot(xhci, slot);
	if (status < 0)
		return (status);

	// Remove any interrupt registrations for the device
	intrReg = kernelLinkedListIterStart(&xhci->intrRegs, &iter);
	while (intrReg)
	{
		if (intrReg->usbDev != usbDev)
		{
			intrReg = kernelLinkedListIterNext(&xhci->intrRegs, &iter);
			continue;
		}

		unregisterInterrupt(xhci, intrReg);

		// Restart the iteration
		intrReg = kernelLinkedListIterStart(&xhci->intrRegs, &iter);
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

	usbController *controller = NULL;
	xhciData *xhci = NULL;
	xhciTrb cmdTrb;

	kernelDebug(debug_usb, "XHCI initial device detection, hotplug=%d",
		hotplug);

	// Check params
	if (!hub)
	{
		kernelError(kernel_error, "NULL parameter");
		return;
	}

	controller = hub->controller;
	if (!controller)
	{
		kernelError(kernel_error, "Hub controller is NULL");
		return;
	}

	xhci = controller->data;

	// Do a no-op command.  Helps the port change interrupt to arrive on
	// time, and demonstrates that the command ring and interrupter are
	// working properly.
	memset((void *) &cmdTrb, 0, sizeof(xhciTrb));
	cmdTrb.typeFlags = XHCI_TRBTYPE_CMDNOOP;
	if (command(xhci, &cmdTrb) < 0)
		kernelDebugError("No-op command failed");

	// Try to wait for a 'port status change' event on the event ring.  Once
	// we get this, we know that any connected ports should be showing their
	// connections
	if (!waitPortChangeEvent(xhci, 1, 0, 150))
		kernelDebugError("No port change event");

	doDetectDevices(controller, hotplug);

	hub->doneColdDetect = 1;
}


static void threadCall(usbHub *hub)
{
	// This function gets called periodically by the USB thread, to give us
	// an opportunity to detect connections/disconnections, or whatever else
	// we want.

	usbController *controller = NULL;
	xhciData *xhci = NULL;

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

	xhci = controller->data;

	if (xhci->portChangedBitmap)
		doDetectDevices(controller, 1 /* hotplug */);
}


/////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////
//
//  Below here, the functions are exported for external use
//
/////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////

kernelDevice *kernelUsbXhciDetect(kernelBusTarget *busTarget,
	kernelDriver *driver)
{
	// This function is used to detect and initialize a potential XHCI USB
	// controller, as well as registering it with the higher-level interfaces.

	int status = 0;
	pciDeviceInfo pciDevInfo;
	usbController *controller = NULL;
	xhciData *xhci = NULL;
	unsigned physMemSpace;
	unsigned physMemSpaceHi;
	unsigned memSpaceSize;
	unsigned hciver = 0;
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
		kernelDebugError("PCI headertype not 'normal' (%02x)",
			(pciDevInfo.device.headerType & ~PCI_HEADERTYPE_MULTIFUNC));
		goto err_out;
	}

	// Make sure it's an XHCI controller (programming interface is 0x30 in
	// the PCI header)
	if (pciDevInfo.device.progIF != XHCI_PCI_PROGIF)
		goto err_out;

	// After this point, we believe we have a supported device.

	kernelDebug(debug_usb, "XHCI controller found");

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
			kernelDebug(debug_usb, "XHCI bus mastering enabled in PCI");
	}
	else
	{
		kernelDebug(debug_usb, "XHCI bus mastering already enabled");
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
	controller->type = usb_xhci;

	// Get the USB version number
	controller->usbVersion = kernelBusReadRegister(busTarget, 0x60, 8);

	// Get the interrupt number.
	controller->interruptNum = pciDevInfo.device.nonBridge.interruptLine;

	kernelLog("USB: XHCI controller USB %d.%d interrupt %d",
		((controller->usbVersion & 0xF0) >> 4),
		(controller->usbVersion & 0xF), controller->interruptNum);

	// Allocate memory for the XHCI data
	controller->data = kernelMalloc(sizeof(xhciData));
	if (!controller->data)
		goto err_out;

	xhci = controller->data;

	// Get the memory range address
	physMemSpace = (pciDevInfo.device.nonBridge.baseAddress[0] & 0xFFFFFFF0);

	kernelDebug(debug_usb, "XHCI physMemSpace=0x%08x", physMemSpace);

	physMemSpaceHi = (pciDevInfo.device.nonBridge.baseAddress[1] & 0xFFFFFFF0);

	if (physMemSpaceHi)
	{
		kernelError(kernel_error, "Register memory must be mapped in 32-bit "
			"address space");
		status = ERR_NOTIMPLEMENTED;
		goto err_out;
	}

	// Determine the memory space size.  Write all 1s to the register.
	kernelBusWriteRegister(busTarget, PCI_CONFREG_BASEADDRESS0_32, 32,
		0xFFFFFFFF);

	memSpaceSize = (~(kernelBusReadRegister(busTarget,
		PCI_CONFREG_BASEADDRESS0_32, 32) & ~0xF) + 1);

	kernelDebug(debug_usb, "XHCI memSpaceSize=0x%08x", memSpaceSize);

	// Restore the register we clobbered.
	kernelBusWriteRegister(busTarget, PCI_CONFREG_BASEADDRESS0_32, 32,
		pciDevInfo.device.nonBridge.baseAddress[0]);

	// Map the physical memory address of the controller's registers into
	// our virtual address space.

	// Map the physical memory space pointed to by the decoder.
	status = kernelPageMapToFree(KERNELPROCID, physMemSpace,
		(void **) &xhci->capRegs, memSpaceSize);
	if (status < 0)
	{
		kernelDebugError("Error mapping memory");
		goto err_out;
	}

	// Make it non-cacheable, since this memory represents memory-mapped
	// hardware registers.
	status = kernelPageSetAttrs(KERNELPROCID, 1 /* set */,
		PAGEFLAG_CACHEDISABLE, (void *) xhci->capRegs, memSpaceSize);
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

		kernelDebug(debug_usb, "XHCI memory access enabled in PCI");
	}
	else
	{
		kernelDebug(debug_usb, "XHCI memory access already enabled");
	}

	// Warn if the controller is pre-release
	hciver = (xhci->capRegs->capslenHciver >> 16);
	if (hciver < 0x0100)
		kernelLog("USB: XHCI warning, version is older than 1.0 (%d.%d%d)",
			((hciver >> 8) & 0xFF), ((hciver >> 4) & 0xF), (hciver & 0xF));

	//debugCapRegs(xhci);
	//debugHcsParams1(xhci);
	//debugHcsParams2(xhci);
	//debugHcsParams3(xhci);
	//debugHccParams(xhci);

	xhci->numPorts = ((xhci->capRegs->hcsparams1 & XHCI_HCSP1_MAXPORTS) >> 24);
	kernelDebug(debug_usb, "XHCI number of ports=%d", xhci->numPorts);

	// Record the address of the operational registers
	xhci->opRegs = ((void *) xhci->capRegs +
		(xhci->capRegs->capslenHciver & 0xFF));

	//debugOpRegs(xhci);

	// Record the address of the doorbell registers
	xhci->dbRegs = ((void *) xhci->capRegs +
		(xhci->capRegs->dboffset & ~0x3UL));

	// Record the address of the runtime registers
	xhci->rtRegs = ((void *) xhci->capRegs +
		(xhci->capRegs->runtimeoffset & ~0x1FUL));

	// Record the maximum number of device slots
	xhci->numDevSlots = min(XHCI_MAX_DEV_SLOTS,
		(xhci->capRegs->hcsparams1 & XHCI_HCSP1_MAXDEVSLOTS));
	kernelDebug(debug_usb, "XHCI number of device slots=%d (max=%d)",
		xhci->numDevSlots,
		(xhci->capRegs->hcsparams1 & XHCI_HCSP1_MAXDEVSLOTS));

	// Calculate and record the controller's notion of a 'page size'
	xhci->pageSize = (xhci->opRegs->pagesz << 12);

	// Look out for 64-bit contexts - not yet supported
	if (xhci->capRegs->hccparams & XHCI_HCCP_CONTEXTSIZE)
	{
		kernelError(kernel_error, "Controller is using 64-bit contexts");
		status = ERR_NOTIMPLEMENTED;
		goto err_out;
	}

	// Does the controller have any extended capabilities?
	if (xhci->capRegs->hccparams & XHCI_HCCP_EXTCAPPTR)
	{
		kernelDebug(debug_usb, "XHCI controller has extended capabilities");

		// Process them
		status = processExtCaps(xhci);
		if (status < 0)
			goto err_out;
	}

	// Reset the controller
	status = reset(controller);
	if (status < 0)
		goto err_out;

	// Set up the controller's registers, data structures, etc.
	status = setup(xhci);
	if (status < 0)
		goto err_out;

	//debugOpRegs(xhci);

	// If port power is software-controlled, make sure they're all powered on
	if (xhci->capRegs->hccparams & XHCI_HCCP_PORTPOWER)
	{
		for (count = 0; count < xhci->numPorts; count ++)
			setPortPower(xhci, count, 1);

		// The spec says we need to wait 20ms for port power to stabilize
		// (only do it once though, after they've all been turned on)
		kernelCpuSpinMs(20);
	}

	// Start the controller
	status = startStop(xhci, 1);
	if (status < 0)
		goto err_out;

	// Allocate memory for the kernel device
	dev = kernelMalloc(sizeof(kernelDevice));
	if (!dev)
		goto err_out;

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

	// Set up the kernel device
	dev->device.class = kernelDeviceGetClass(DEVICECLASS_BUS);
	dev->device.subClass = kernelDeviceGetClass(DEVICESUBCLASS_BUS_USB);
	dev->driver = driver;
	dev->data = (void *) controller;

	// Initialize the variable list for attributes of the controller
	status = kernelVariableListCreate(&dev->device.attrs);
	if (status >= 0)
	{
		kernelVariableListSet(&dev->device.attrs, "controller.type", "XHCI");
		snprintf(value, 32, "%d", xhci->numPorts);
		kernelVariableListSet(&dev->device.attrs, "controller.numPorts",
			value);
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

