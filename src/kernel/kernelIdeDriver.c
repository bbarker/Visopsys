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
//  kernelIdeDriver.c
//

// Driver for standard PATA/ATAPI (IDE) disks

#include "kernelIdeDriver.h"
#include "kernelAtaDriver.h"
#include "kernelBus.h"
#include "kernelCpu.h"
#include "kernelDebug.h"
#include "kernelError.h"
#include "kernelInterrupt.h"
#include "kernelLog.h"
#include "kernelMalloc.h"
#include "kernelMemory.h"
#include "kernelMultitasker.h"
#include "kernelParameters.h"
#include "kernelPciDriver.h"
#include "kernelPic.h"
#include "kernelScsiDriver.h"
#include "kernelVariableList.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/processor.h>

// Some little macro shortcuts
#define CHANNEL(ctrlNum, chanNum) (controllers[ctrlNum].channel[chanNum])
#define DISK_CTRL(diskNum) (controllers[(diskNum & 0xF0) >> 4])
#define DISK_CHAN(diskNum) \
	(DISK_CTRL(diskNum).channel[(diskNum & 0xF) / 2])
#define DISK(diskNum) (DISK_CHAN(diskNum).disk[diskNum & 0x1])
#define DISKISMULTI(diskNum) (DISK(diskNum).featureFlags & ATA_FEATURE_MULTI)
#define DISKISDMA(diskNum) (DISK_CTRL(diskNum).busMaster && \
	(DISK(diskNum).featureFlags & ATA_FEATURE_DMA))
#define DISKISSMART(diskNum) \
	(DISK(diskNum).featureFlags & ATA_FEATURE_SMART)
#define DISKISRCACHE(diskNum) \
	(DISK(diskNum).featureFlags & ATA_FEATURE_RCACHE)
#define DISKISWCACHE(diskNum) \
	(DISK(diskNum).featureFlags & ATA_FEATURE_WCACHE)
#define DISKISMEDSTAT(diskNum) \
	(DISK(diskNum).featureFlags & ATA_FEATURE_MEDSTAT)
#define DISKIS48(diskNum) \
	(DISK(diskNum).featureFlags & ATA_FEATURE_48BIT)
#define BMPORT_CMD(ctrlNum, chanNum) \
	(controllers[ctrlNum].busMasterIo + (chanNum * 8))
#define BMPORT_STATUS(ctrlNum, chanNum) (BMPORT_CMD(ctrlNum, chanNum) + 2)
#define BMPORT_PRDADDR(ctrlNum, chanNum) (BMPORT_CMD(ctrlNum, chanNum) + 4)
#define DISK_BMPORT_CMD(diskNum) \
	(DISK_CTRL(diskNum).busMasterIo + (((diskNum & 0xF) / 2) * 8))
#define DISK_BMPORT_STATUS(diskNum) (DISK_BMPORT_CMD(diskNum) + 2)
#define DISK_BMPORT_PRDADDR(diskNum) (DISK_BMPORT_CMD(diskNum) + 4)

// List of default IDE ports, per device number
static idePorts defaultPorts[] = {
	{ 0x01F0, 0x01F1, 0x01F2, 0x01F3, 0x01F4, 0x01F5, 0x01F6, 0x01F7, 0x03F6 },
	{ 0x0170, 0x0171, 0x0172, 0x0173, 0x0174, 0x0175, 0x0176, 0x0177, 0x0376 },
	{ 0x00F0, 0x00F1, 0x00F2, 0x00F3, 0x00F4, 0x00F5, 0x00F6, 0x00F7, 0x02F6 },
	{ 0x0070, 0x0071, 0x0072, 0x0073, 0x0074, 0x0075, 0x0076, 0x0077, 0x0276 }
};

// Error messages
static char *errorMessages[] = {
	"Address mark not found",
	"Cylinder 0 not found",
	"Command aborted - invalid command",
	"Media change requested",
	"ID or target sector not found",
	"Media changed",
	"Uncorrectable data error",
	"Bad sector detected",
	"Unknown error",
	"Command timed out"
};

static ideController *controllers = NULL;
static int numControllers = 0;

// Saved old interrupt handlers
static void **oldIntHandlers = NULL;
static int numOldHandlers = 0;


static int pollStatus(int diskNum, unsigned char mask, int onOff)
{
	// Returns when the requested status bits are on or off, or else the
	// timeout is reached

	unsigned timeout = 100;
	unsigned char data = 0;
	unsigned count;

	if (DISK(diskNum).physical.type & DISKTYPE_IDECDROM)
		// CD-ROMs can be pokey here, but eventually come around.
		timeout = 2000;

	for (count = 0; count < timeout; count ++)
	{
		// Get the contents of the status register for the channel of the
		// selected disk.
		processorInPort8(DISK_CHAN(diskNum).ports.altComStat, data);

		if ((data & 0x7F) == 0x7F)
		{
			kernelDebug(debug_io, "IDE controller says 7F");
			return (-1);
		}

		if ((onOff && ((data & mask) == mask)) ||
			(!onOff && !(data & mask)))
		{
			return (0);
		}

		kernelCpuSpinMs(1);
	}

	// Timed out.
	kernelDebug(debug_io, "IDE timeout waiting for disk %02x port %08x=%04x",
		diskNum, DISK_CHAN(diskNum).ports.altComStat, data);
	return (-1);
}


static inline void _expectInterrupt(int diskNum, const char *function
	__attribute__((unused)), int line __attribute__((unused)))
{
	kernelDebug(debug_io, "IDE disk %02x %s:%d expect interrupt", diskNum,
				function, line);

	if (kernelCurrentProcess)
		DISK_CHAN(diskNum).expectInterrupt = kernelCurrentProcess->processId;
	else
		DISK_CHAN(diskNum).expectInterrupt = KERNELPROCID;
}


#define expectInterrupt(diskNum) \
	_expectInterrupt(diskNum, __FUNCTION__, __LINE__)


static inline void _ackInterrupt(int diskNum, const char *function
	__attribute__((unused)), int line __attribute__((unused)))
{
	if (DISK_CHAN(diskNum).gotInterrupt)
	{
		DISK_CHAN(diskNum).gotInterrupt = 0;
		kernelDebug(debug_io, "IDE disk %02x %s:%d ack interrupt %d #%d",
			diskNum, function, line, DISK_CHAN(diskNum).interrupt,
			DISK_CHAN(diskNum).acks);
		DISK_CHAN(diskNum).acks += 1;
		kernelPicEndOfInterrupt(DISK_CHAN(diskNum).interrupt);
	}
}


#define ackInterrupt(diskNum) _ackInterrupt(diskNum, __FUNCTION__, __LINE__)


static int select(int diskNum)
{
	// Selects the disk on the controller.  Returns 0 on success, negative
	// otherwise

	int status = 0;
	unsigned char data = 0;

	kernelDebug(debug_io, "IDE select disk %02x", diskNum);

	// Make sure the disk number is legal.
	if ((diskNum & 0xF) > 3)
		return (status = ERR_INVALID);

	// Wait for the controller to be ready and data request not asserted
	status = pollStatus(diskNum, (ATA_STAT_BSY | ATA_STAT_DRQ), 0);
	if (status < 0)
	{
		kernelDebugError("Disk %02x controller not ready", diskNum);
		return (status);
	}

	// Set the disk select bit in the drive/head register.  This will help to
	// introduce some delay between disk selection and any actual commands.
	// Disk number is LSBit.  Move disk number to bit 4.  NO LBA.
	data = ((diskNum & 0x1) << 4);
	processorOutPort8(DISK_CHAN(diskNum).ports.device, data);

	// Wait for the controller to be ready
	status = pollStatus(diskNum, ATA_STAT_BSY, 0);
	if (status < 0)
	{
		kernelDebugError("Disk %02x controller not ready", diskNum);
		return (status);
	}

	return (status = 0);
}


static void lbaSetup(int diskNum, uquad_t logicalSector, uquad_t numSectors)
{
	// This routine is strictly internal, and is used to set up the disk
	// controller registers with an LBA disk address in the drive/head, cylinder
	// low, cylinder high, and start sector registers.  It doesn't return
	// anything.

	unsigned char cmd = 0;

	// Set the LBA registers

	if (DISKIS48(diskNum))
	{
		// If numSectors is 65536, we need to change it to zero.
		if (numSectors == 65536)
			numSectors = 0;

		// Send a value of 0 to the error/precomp register
		processorOutPort8(DISK_CHAN(diskNum).ports.featErr, 0);

		// With 48-bit addressing, we write the top bytes to the same registers
		// as we will later write the bottom 3 bytes.

		// Send the high byte of the sector count
		cmd = ((numSectors >> 8) & 0xFF);
		processorOutPort8(DISK_CHAN(diskNum).ports.sectorCount, cmd);

		// Bits 24-31 of the address
		cmd = ((logicalSector >> 24) & 0xFF);
		processorOutPort8(DISK_CHAN(diskNum).ports.lbaLow, cmd);
		// Bits 32-39 of the address
		processorOutPort8(DISK_CHAN(diskNum).ports.lbaMid, 0);
		// Bits 40-47 of the address
		processorOutPort8(DISK_CHAN(diskNum).ports.lbaHigh, 0);
	}
	else
	{
		// If numSectors is 256, we need to change it to zero.
		if (numSectors == 256)
			numSectors = 0;
	}

	// Send a value of 0 to the error/precomp register
	processorOutPort8(DISK_CHAN(diskNum).ports.featErr, 0);

	// Send the low byte of the sector count
	cmd = (numSectors & 0xFF);
	processorOutPort8(DISK_CHAN(diskNum).ports.sectorCount, cmd);

	// Bits 0-7 of the address
	cmd = (logicalSector & 0xFF);
	processorOutPort8(DISK_CHAN(diskNum).ports.lbaLow, cmd);
	// Bits 8-15 of the address
	cmd = ((logicalSector >> 8) & 0xFF);
	processorOutPort8(DISK_CHAN(diskNum).ports.lbaMid, cmd);
	// Bits 16-23 of the address
	cmd = ((logicalSector >> 16) & 0xFF);
	processorOutPort8(DISK_CHAN(diskNum).ports.lbaHigh, cmd);

	// LBA and device
	cmd = (0x40 | ((diskNum & 0x1) << 4));
	if (!DISKIS48(diskNum))
		// Bits 24-27 of the address
		cmd |= ((logicalSector >> 24) & 0xF);
	processorOutPort8(DISK_CHAN(diskNum).ports.device, cmd);

	return;
}


static int evaluateError(int diskNum)
{
	// This routine will check the error status on the disk controller
	// of the selected disk.  It evaluates the returned byte and matches
	// conditions to error codes and error messages

	int errorCode = 0;
	unsigned char data = 0;

	processorInPort8(DISK_CHAN(diskNum).ports.featErr, data);

	if (data & ATA_ERR_AMNF)
		errorCode = IDE_ADDRESSMARK;
	else if (data & ATA_ERR_TKNONF)
		errorCode = IDE_CYLINDER0;
	else if (data & ATA_ERR_ABRT)
		errorCode = IDE_INVALIDCOMMAND;
	else if (data & ATA_ERR_MCR)
		errorCode = IDE_MEDIAREQ;
	else if (data & ATA_ERR_IDNF)
		errorCode = IDE_SECTNOTFOUND;
	else if (data & ATA_ERR_MC)
		errorCode = IDE_MEDIACHANGED;
	else if (data & ATA_ERR_UNC)
		errorCode = IDE_BADDATA;
	else if (data & 0x80)
		errorCode = IDE_BADSECTOR;
	else
		errorCode = IDE_UNKNOWN;

	return (errorCode);
}


static int waitOperationComplete(int diskNum, int yield, int dataWait,
	int ack, unsigned timeout)
{
	// This routine reads the "interrupt received" byte, waiting for the last
	// command to complete.  Every time the command has not completed, the
	// driver returns the remainder of the process' timeslice to the
	// multitasker.  When the interrupt byte becomes 1, it resets the byte and
	// checks the status of the selected disk controller

	int status = 0;
	uquad_t endTime = 0;
	unsigned char statReg = 0;
	unsigned char data = 0;

	kernelDebug(debug_io, "IDE disk %02x wait (%s) for interrupt %d "
		"dataWait=%d ack=%d", diskNum, (yield? "yield" : "poll"),
		DISK_CHAN(diskNum).interrupt, dataWait, ack);

	if (!timeout)
		timeout = 1000;

	endTime = (kernelCpuGetMs() + timeout);

	while (1)
	{
		if (yield && !DISK_CHAN(diskNum).gotInterrupt)
		{
			// Go into a waiting state.  The caller should previously have
			// called expectInterrupt(), which will tell the interrupt handler
			// that our process ID is waiting.  It will change our state to
			// 'IO ready' which will give us high priority for a wakeup
			kernelMultitaskerWait(timeout);
		}

		if (DISK_CHAN(diskNum).gotInterrupt)
		{
			if (kernelCpuGetMs() > endTime)
				kernelDebugError("Got interrupt but timed out");

			statReg = DISK_CHAN(diskNum).intStatus;
			break;
		}
		else
		{
			// Read the disk status register and short-circuit any error
			// conditions
			processorInPort8(DISK_CHAN(diskNum).ports.altComStat,
				statReg);
			if (statReg & ATA_STAT_ERR)
			{
				kernelDebugError("Disk %02x error waiting for interrupt",
					 diskNum);
				break;
			}

			if (kernelCpuGetMs() > endTime)
				// No interrupt -- timed out
				break;
		}
	}

	// Did the status indicate an error? (regardless of whether or not we got
	// the interrupt)
	if (statReg & ATA_STAT_ERR)
	{
		// Let the caller read and report the error condition if desired.
		status = ERR_IO;
		goto out;
	}

	if (!DISK_CHAN(diskNum).gotInterrupt)
	{
		// Just a timeout
		kernelDebugError("Disk %02x no interrupt received - timeout",
			 diskNum);
		status = ERR_IO;
		ack = 0;
		goto out;
	}

	// 'Officially' read the status register to clear it
	processorInPort8(DISK_CHAN(diskNum).ports.comStat, data);

	// Wait for controller not busy
	if (pollStatus(diskNum, ATA_STAT_BSY, 0) < 0)
	{
		// This can happen when an ATAPI device is spinning up
		status = ERR_BUSY;
		goto out;
	}

	if (dataWait)
	{
		// Wait for data ready
		if (pollStatus(diskNum, ATA_STAT_DRQ, 1) < 0)
		{
			kernelDebugError("IDE Disk %02x data not ready after command",
				 diskNum);
			status = ERR_NODATA;
			goto out;
		}
	}

	status = 0;

out:
	if (ack)
		ackInterrupt(diskNum);

	return (status);
}


static int writeCommandFile(int diskNum, unsigned char featErr,
	unsigned short sectorCount, unsigned short lbaLow, unsigned short lbaMid,
	unsigned short lbaHigh,	unsigned char comStat)
{
	// Simply write out the whole 'command file' (the registers for issuing an
	// ATA (non-packet) command)

	int status = 0;
	unsigned char data = 0;

	// Select the disk.  Probably unnecessary because the disk *should* be
	// selected already.
	status = select(diskNum);
	if (status < 0)
		return (status);

	if (DISKIS48(diskNum))
	{
		kernelDebug(debug_io, "IDE disk %02x write command file 48-bit",
			diskNum);

		// With 48-bit addressing, we write the top bytes to the same registers
		// as we will later write the bottom bytes.

		data = ((featErr >> 8) & 0xFF);
		processorOutPort8(DISK_CHAN(diskNum).ports.featErr, data);

		data = ((sectorCount >> 8) & 0xFF);
		processorOutPort8(DISK_CHAN(diskNum).ports.sectorCount, data);

		data = ((lbaLow >> 8) & 0xFF);
		processorOutPort8(DISK_CHAN(diskNum).ports.lbaLow, data);

		data = ((lbaMid >> 8) & 0xFF);
		processorOutPort8(DISK_CHAN(diskNum).ports.lbaMid, data);

		data = ((lbaHigh >> 8) & 0xFF);
		processorOutPort8(DISK_CHAN(diskNum).ports.lbaHigh, data);
	}
	else
	{
		kernelDebug(debug_io, "IDE disk %02x write command file 28-bit",
			diskNum);
	}

	data = (featErr & 0xFF);
	processorOutPort8(DISK_CHAN(diskNum).ports.featErr, data);

	data = (sectorCount & 0xFF);
	processorOutPort8(DISK_CHAN(diskNum).ports.sectorCount, data);

	data = (lbaLow & 0xFF);
	processorOutPort8(DISK_CHAN(diskNum).ports.lbaLow, data);

	data = (lbaMid & 0xFF);
	processorOutPort8(DISK_CHAN(diskNum).ports.lbaMid, data);

	data = (lbaHigh & 0xFF);
	processorOutPort8(DISK_CHAN(diskNum).ports.lbaHigh, data);

	processorOutPort8(DISK_CHAN(diskNum).ports.comStat, comStat);
	return (status = 0);
}


static inline void readPort16(int portNum, void *buffer, unsigned words)
{
	unsigned count;

	for (count = 0; count < words; count ++)
	{
		processorInPort16(portNum, *((unsigned short *) buffer));
		buffer += 2;
	}
}


static inline void writePort16(int portNum, void *buffer, unsigned words)
{
	unsigned count;

	for (count = 0; count < words; count ++)
	{
		processorOutPort16(portNum, *((unsigned short *) buffer));
		buffer += 2;
	}
}


static int sendAtapiPacket(int diskNum, unsigned byteCount,
	unsigned char *packet)
{
	int status = 0;
	unsigned char data = 0;

	kernelDebug(debug_io, "IDE disk %02x sending ATAPI packet %02x %s",
		diskNum, packet[0], atapiCommand2String(packet[0]));

	// Wait for the controller to be ready, and data request not active
	status = pollStatus(diskNum, (ATA_STAT_BSY | ATA_STAT_DRQ), 0);
	if (status < 0)
		return (status);

	expectInterrupt(diskNum);

	processorOutPort8(DISK_CHAN(diskNum).ports.featErr, 0);
	data = (unsigned char)(byteCount & 0x000000FF);
	processorOutPort8(DISK_CHAN(diskNum).ports.lbaMid, data);
	data = (unsigned char)((byteCount & 0x0000FF00) >> 8);
	processorOutPort8(DISK_CHAN(diskNum).ports.lbaHigh, data);

	// Send the "ATAPI packet" command
	processorOutPort8(DISK_CHAN(diskNum).ports.comStat, ATA_ATAPIPACKET);

	// Wait for the data request bit
	status = pollStatus(diskNum, ATA_STAT_DRQ, 1);
	if (status < 0)
		return (status);

	// (Possible) interrupt says "I'm ready for the command"
	ackInterrupt(diskNum);

	expectInterrupt(diskNum);

	// Send the 12 bytes of packet data.
	writePort16(DISK_CHAN(diskNum).ports.data, packet, 6);

	// Interrupt says data received
	status = waitOperationComplete(diskNum, 0 /* no yield */,
		0 /* no data wait */, 1 /* ack */, 5000 /* timeout */);

	// The disk may interrupt again if/when it's got data for us
	expectInterrupt(diskNum);

	kernelDebug(debug_io, "IDE disk %02x sent ATAPI packet", diskNum);

	return (status);
}


static int atapiRequestSense(int diskNum, atapiSenseData *senseData)
{
	int status = 0;

	kernelDebug(debug_io, "IDE disk %02x request sense", diskNum);

	status = sendAtapiPacket(diskNum, sizeof(atapiSenseData),
		((unsigned char[]){ ATAPI_REQUESTSENSE, 0, 0, 0,
			sizeof(atapiSenseData), 0, 0, 0, 0, 0, 0, 0 } ));
	if (status < 0)
		return (status);

	kernelDebug(debug_io, "IDE disk %02x wait for data req", diskNum);

	// Wait for the data request bit
	status = pollStatus(diskNum, ATA_STAT_DRQ, 1);
	if (status < 0)
		return (status);

	kernelDebug(debug_io, "IDE disk %02x read sense data", diskNum);

	expectInterrupt(diskNum);

	// Read in the sense data
	readPort16(DISK_CHAN(diskNum).ports.data, senseData,
		(sizeof(atapiSenseData) / 2));

	// Interrupt at the end says data is finished
	waitOperationComplete(diskNum, 0 /* no yield */, 0 /* no data wait */,
		1 /* ack */, 0 /* default timeout */);

	kernelDebug(debug_io, "IDE disk %02x sense key=%02x", diskNum,
		senseData->senseKey);
	kernelDebug(debug_io, "IDE disk %02x addl sense=%02x", diskNum,
		senseData->addlSenseCode);

	return (status = 0);
}


static int atapiStartStop(int diskNum, int start)
{
	// Start or stop an ATAPI device

	int status = 0;
	uquad_t timeout = (kernelCpuGetMs() + 10000);
	unsigned short dataWord = 0;
	atapiSenseData senseData;

	if (start)
	{
		// If we know the disk door is open, try to close it
		if (DISK(diskNum).physical.flags & DISKFLAG_DOOROPEN)
		{
			kernelDebug(debug_io, "IDE disk %02x close ATAPI device", diskNum);
			sendAtapiPacket(diskNum, 0, ATAPI_PACKET_CLOSE);
		}

		// Well, okay, assume this.
		DISK(diskNum).physical.flags &= ~DISKFLAG_DOOROPEN;

		// Try for several seconds to start the device.  If there is no media,
		// or if the media has just been inserted, this command can return
		// various error codes.
		do {
			kernelDebug(debug_io, "IDE disk %02x start ATAPI device", diskNum);
			status = sendAtapiPacket(diskNum, 0, ATAPI_PACKET_START);
			if (status < 0)
			{
				dataWord = evaluateError(diskNum);

				// 'invalid command' seems to indicate pretty strongly that we
				// shouldn't keep retrying.
				if (dataWord == IDE_INVALIDCOMMAND)
					break;

				// Request sense data
				if (atapiRequestSense(diskNum, &senseData) < 0)
				{
					break;
				}

				// Check sense responses

				if (senseData.senseKey == SCSI_SENSE_NOSENSE)
				{
					// No error reported, try again
					kernelMultitaskerWait(5);
					continue;
				}
				else if (senseData.senseKey == SCSI_SENSE_RECOVEREDERROR)
				{
					// Recovered error.  Hmm, some error happened, but the
					// device thinks it handled it.  We shouldn't get this,
					// in other words.
					kernelMultitaskerWait(5);
					continue;
				}
				else if ((senseData.senseKey == SCSI_SENSE_NOTREADY) &&
					(senseData.addlSenseCode == 0x04))
				{
					// The drive may be in the process of becoming ready
					kernelMultitaskerWait(5);
					continue;
				}
				else if ((senseData.senseKey == SCSI_SENSE_UNITATTENTION) &&
					(senseData.addlSenseCode == 0x29))
				{
					// This happens after a reset
					kernelMultitaskerWait(5);
					continue;
				}
				else
				{
					// Assume we shouldn't retry
					break;
				}
			}
			else
			{
				break;
			}

		} while (kernelCpuGetMs() < timeout);

		// Start successful?
		if (status < 0)
		{
			kernelError(kernel_error, "%s", errorMessages[dataWord]);
			return (status);
		}

		kernelDebug(debug_io, "IDE disk %02x ATAPI read capacity", diskNum);
		status = sendAtapiPacket(diskNum, 8, ATAPI_PACKET_READCAPACITY);
		if (status < 0)
		{
			kernelError(kernel_error, "%s",
				errorMessages[evaluateError(diskNum)]);
			return (status);
		}

		pollStatus(diskNum, ATA_STAT_DRQ, 1);

		// (Possible) interrupt at the beginning says data is ready
		ackInterrupt(diskNum);

		expectInterrupt(diskNum);

		// Read the number of sectors
		processorInPort16(DISK_CHAN(diskNum).ports.data, dataWord);
		DISK(diskNum).physical.numSectors =
			(((unsigned)(dataWord & 0x00FF)) << 24);
		DISK(diskNum).physical.numSectors |=
			(((unsigned)(dataWord & 0xFF00)) << 8);
		processorInPort16(DISK_CHAN(diskNum).ports.data, dataWord);
		DISK(diskNum).physical.numSectors |=
			(((unsigned)(dataWord & 0x00FF)) << 8);
		DISK(diskNum).physical.numSectors |=
			(((unsigned)(dataWord & 0xFF00)) >> 8);

		// Read the sector size
		processorInPort16(DISK_CHAN(diskNum).ports.data, dataWord);
		DISK(diskNum).physical.sectorSize =
			(((unsigned)(dataWord & 0x00FF)) << 24);
		DISK(diskNum).physical.sectorSize |=
			(((unsigned)(dataWord & 0xFF00)) << 8);
		processorInPort16(DISK_CHAN(diskNum).ports.data, dataWord);
		DISK(diskNum).physical.sectorSize |=
			(((unsigned)(dataWord & 0x00FF)) << 8);
		DISK(diskNum).physical.sectorSize |=
			(((unsigned)(dataWord & 0xFF00)) >> 8);

		// Interrupt at the end says data is finished
		waitOperationComplete(diskNum, 0 /* no yield */, 0 /* no data wait */,
			1 /* ack */, 0 /* default timeout */);

		// If there's no disk, the number of sectors will be illegal.	Set
		// to the maximum value and quit
		if (!DISK(diskNum).physical.numSectors ||
			(DISK(diskNum).physical.numSectors == 0xFFFFFFFF))
		{
			DISK(diskNum).physical.numSectors = 0xFFFFFFFF;
			DISK(diskNum).physical.sectorSize = ATAPI_SECTORSIZE;
			kernelError(kernel_error, "No media in drive %s",
				DISK(diskNum).physical.name);
			return (status = ERR_NOMEDIA);
		}

		DISK(diskNum).physical.logical[0].numSectors =
			DISK(diskNum).physical.numSectors;

		// Read the TOC (Table Of Contents)
		kernelDebug(debug_io, "IDE disk %02x ATAPI read TOC", diskNum);
		status = sendAtapiPacket(diskNum, 12, ATAPI_PACKET_READTOC);
		if (status < 0)
		{
			kernelError(kernel_error, "%s",
				errorMessages[evaluateError(diskNum)]);
			return (status);
		}

		pollStatus(diskNum, ATA_STAT_DRQ, 1);

		// (Possible) interrupt at the beginning says data is ready
		ackInterrupt(diskNum);

		expectInterrupt(diskNum);

		// Ignore the first four words
		processorInPort16(DISK_CHAN(diskNum).ports.data, dataWord);
		processorInPort16(DISK_CHAN(diskNum).ports.data, dataWord);
		processorInPort16(DISK_CHAN(diskNum).ports.data, dataWord);
		processorInPort16(DISK_CHAN(diskNum).ports.data, dataWord);

		// Read the LBA address of the start of the last track
		processorInPort16(DISK_CHAN(diskNum).ports.data, dataWord);
		DISK(diskNum).physical.lastSession =
			(((unsigned)(dataWord & 0x00FF)) << 24);
		DISK(diskNum).physical.lastSession |=
			(((unsigned)(dataWord & 0xFF00)) << 8);
		processorInPort16(DISK_CHAN(diskNum).ports.data, dataWord);
		DISK(diskNum).physical.lastSession |=
			(((unsigned)(dataWord & 0x00FF)) << 8);
		DISK(diskNum).physical.lastSession |=
			(((unsigned)(dataWord & 0xFF00)) >> 8);

		DISK(diskNum).physical.flags |= DISKFLAG_MOTORON;

		// Interrupt at the end says data is finished
		waitOperationComplete(diskNum, 0 /* no yield */, 0 /* no data wait */,
			1 /* ack */, 0 /* default timeout */);
	}
	else
	{
		kernelDebug(debug_io, "IDE disk %02x stop ATAPI device", diskNum);
		status = sendAtapiPacket(diskNum, 0, ATAPI_PACKET_STOP);
		DISK(diskNum).physical.flags &= ~DISKFLAG_MOTORON;
	}

	return (status);
}


static void dmaSetCommand(int diskNum, unsigned char data, int set)
{
	// Set or clear bits in the DMA command register for the appropriate
	// channel

	unsigned char cmd = 0;

	// Get the command reg
	processorInPort8(DISK_BMPORT_CMD(diskNum), cmd);

	if (set)
		cmd |= data;
	else
		cmd &= ~data;

	// Write the command reg.
	processorOutPort8(DISK_BMPORT_CMD(diskNum), cmd);

	return;
}


static inline void dmaStartStop(int diskNum, int start)
{
	// Start or stop DMA for the appropriate channel
	dmaSetCommand(diskNum, 1, start);
	return;
}


static inline void dmaReadWrite(int diskNum, int read)
{
	// Set the DMA read/write bit for the appropriate channel
	dmaSetCommand(diskNum, 8, read);
	return;
}


static unsigned char dmaGetStatus(int diskNum)
{
	// Gets the DMA status register

	unsigned char stat = 0;

	// Get the status, and write back the lower 3 bits to clear them.
	processorInPort8(DISK_BMPORT_STATUS(diskNum), stat);

	return (stat);
}


static void dmaClearStatus(int diskNum)
{
	// Clears the DMA status register.

	unsigned char stat = dmaGetStatus(diskNum);
	processorOutPort8(DISK_BMPORT_STATUS(diskNum), (stat | 0x7));
}


static int dmaSetup(int diskNum, void *address, unsigned bytes, int read,
	unsigned *doneBytes)
{
	// Do DMA transfer setup.

	int status = 0;
	unsigned maxBytes = 0;
	unsigned physicalAddress = NULL;
	unsigned doBytes = 0;
	int numPrds = 0;
	idePrd *prds = NULL;
	int count;

	// How many bytes can we do per DMA operation?
	maxBytes = min((DISK(diskNum).physical.multiSectors * 512), 0x10000);

	// Get the buffer physical address
	physicalAddress =
		kernelPageGetPhysical((((unsigned) address < KERNEL_VIRTUAL_ADDRESS)?
			 kernelCurrentProcess->processId : KERNELPROCID), address);
	if (!physicalAddress)
	{
		kernelError(kernel_error, "Couldn't get buffer physical address for "
			"%p", address);
		return (status = ERR_INVALID);
	}

	// Address must be dword-aligned
	if (physicalAddress % 4)
	{
		kernelError(kernel_error, "Physical address 0x%08x of virtual address "
			"%p not dword-aligned", physicalAddress, address);
		return (status = ERR_ALIGN);
	}

	kernelDebug(debug_io, "IDE disk %02x do DMA setup for %u bytes to "
		"address 0x%08x", diskNum, bytes, physicalAddress);

	// Set up all the PRDs
	prds = DISK_CHAN(diskNum).prds.virtual;

	for (count = 0; bytes > 0; count ++)
	{
		if (numPrds >= DISK_CHAN(diskNum).prdEntries)
			// We've reached the limit of what we can do in one DMA setup
			break;

		doBytes = min(bytes, maxBytes);

		// No individual transfer (as represented by 1 PRD) should cross a
		// 64K boundary -- some DMA chips won't do that.
		if ((((unsigned) physicalAddress & 0xFFFF) + doBytes) > 0x10000)
		{
			kernelDebug(debug_io, "IDE physical buffer crosses a 64K "
				"boundary");
			doBytes = (0x10000 - ((unsigned) physicalAddress & 0xFFFF));
		}

		// If the number of bytes is exactly 64K, break it up into 2 transfers
		// in case the controller gets confused by a count of zero.
		if (doBytes == 0x10000)
			doBytes = 0x8000;

		// Each byte count must be dword-multiple
		if (doBytes % 4)
		{
			kernelError(kernel_error, "Byte count not dword-multiple");
			return (status = ERR_ALIGN);
		}

		// Set up the address and count in the channel's PRD
		prds[count].physicalAddress = physicalAddress;
		prds[count].count = doBytes;
		prds[count].EOT = 0;

		kernelDebug(debug_io, "IDE disk %02x set up PRD for address 0x%08x, "
			"bytes %u", diskNum, prds[count].physicalAddress, doBytes);

		physicalAddress += doBytes;
		bytes -= doBytes;
		*doneBytes += doBytes;
		numPrds += 1;
	}

	// Mark the last entry in the PRD table.
	prds[numPrds - 1].EOT = 0x8000;

	// Try to wait for the controller to be ready
	status = pollStatus(diskNum, ATA_STAT_BSY, 0);
	if (status < 0)
		return (status);

	// Set the PRD table address
	processorOutPort32(DISK_BMPORT_PRDADDR(diskNum),
		 DISK_CHAN(diskNum).prds.physical);

	// Set DMA read/write bit
	dmaReadWrite(diskNum, read);

	// Clear DMA status
	dmaClearStatus(diskNum);

	return (status = 0);
}


static int dmaCheckStatus(int diskNum)
{
	// Get the DMA status (for example after an operation) and clear it.

	int status = 0;
	unsigned char stat = 0;

	// Try to wait for the controller to be ready
	pollStatus(diskNum, ATA_STAT_BSY, 0);

	stat = dmaGetStatus(diskNum);

	dmaClearStatus(diskNum);

	if (stat & 0x01)
	{
		kernelError(kernel_error, "DMA transfer is still active");
		return (status = ERR_BUSY);
	}
	if (stat & 0x02)
	{
		kernelError(kernel_error, "DMA error");
		return (status = ERR_IO);
	}
	/*
	// This is supposed to be set, but it often isn't on my Fujitsu laptop
	if (!(stat & 0x04))
	{
		kernelError(kernel_error, "No DMA interrupt");
		return (status = ERR_NODATA);
	}
	*/

	return (status = 0);
}


static int reset(int diskNum)
{
	// Does a software reset of the requested disk controller.

	int status = 0;
	int master = (diskNum & ~1);
	unsigned char data[4];

	#if DEBUG
	int channel = ((diskNum & 0xF) >> 1);
	#endif

	kernelDebug(debug_io, "IDE reset channel %d (disk %02x)", channel,
		diskNum);

	// We need to set bit 2 for at least 4.8 microseconds.
	processorOutPort8(DISK_CHAN(master).ports.altComStat, 0x04);

	// Delay 10ms
	kernelCpuSpinMs(10);

	// Clear bit 2 again
	processorOutPort8(DISK_CHAN(master).ports.altComStat, 0);

	// Delay 10ms
	kernelCpuSpinMs(10);

	// Wait for controller ready
	status = pollStatus(master, ATA_STAT_BSY, 0);
	if (status < 0)
	{
		kernelDebugError("Channel %d controller not ready after reset",
			channel);
		return (status);
	}

	// Read the error register
	processorInPort8(DISK_CHAN(master).ports.featErr, data[0]);

	// If bit 7 is set, no slave
	status = 0;
	if (data[0] & 0x80)
	{
		kernelDebug(debug_io, "IDE channel %d no slave", channel);
		if (diskNum & 1)
			status = ERR_NOSUCHENTRY;
	}

	// Read the 'signature'
	processorInPort8(DISK_CHAN(master).ports.sectorCount, data[0]);
	processorInPort8(DISK_CHAN(master).ports.lbaLow, data[1]);
	processorInPort8(DISK_CHAN(master).ports.lbaMid, data[2]);
	processorInPort8(DISK_CHAN(master).ports.lbaHigh, data[3]);

	kernelDebug(debug_io, "IDE channel %d reset signature %02x, %02x, "
		"%02x, %02x", channel, data[0], data[1], data[2], data[3]);

	if ((data[2] == 0x14) && (data[3] == 0xEB))
	{
		kernelDebug(debug_io, "IDE channel %d (disk %02x) reset indicates "
			"packet device", channel, diskNum);
		DISK(diskNum).packetMaster = 1;
	}
	else if ((data[0] == 0x01) && (data[1] == 0x01))
	{
		kernelDebug(debug_io, "IDE channel %d (disk %02x) reset indicates "
			"non-packet device", channel, diskNum);
		DISK(diskNum).packetMaster = 0;
	}
	else
	{
		kernelDebug(debug_io, "IDE channel %d (disk %02x) reset has unknown "
			"signature %02x, %02x, %02x, %02x", channel, diskNum,
			data[0], data[1], data[2], data[3]);
		status = ERR_INVALID;
	}

	kernelDebug(debug_io, "IDE channel %d reset finished", channel);
	return (status);
}


static int identify(int diskNum, ataIdentifyData *identData)
{
	// Issues the ATA "identify device" command.  If that fails, it tries the
	// ATAPI "identify packet device" command.

	int status = 0;
	int error = 0;
	unsigned char data[4];

	kernelDebug(debug_io, "IDE identify disk %02x", diskNum);

	memset(identData, 0, sizeof(ataIdentifyData));

	// Wait for controller ready
	status = pollStatus(diskNum, ATA_STAT_BSY, 0);
	if (status < 0)
		return (status);

	// Skip this if we already know it's ATAPI
	if (!(DISK(diskNum).physical.type & DISKTYPE_IDECDROM) &&
		!DISK(diskNum).packetMaster)
	{
		expectInterrupt(diskNum);

		// Send the "identify device" command
		status = writeCommandFile(diskNum, 0, 0, 0, 0, 0, ATA_IDENTIFY);
		if (status < 0)
			return (status);

		// Delay 10ms
		kernelCpuSpinMs(10);

		// Wait for the controller to finish the operation
		status = waitOperationComplete(diskNum, 0 /* no yield */,
			1 /* data wait */, 0 /* no ack */, 0 /* default timeout */);

		if (status >= 0)
		{
			// Transfer one sector's worth of data from the controller.
			kernelDebug(debug_io, "IDE disk %02x identify succeeded",
				diskNum);
			readPort16(DISK_CHAN(diskNum).ports.data, identData,
				(sizeof(ataIdentifyData) / 2));
			kernelDebug(debug_io, "IDE disk %02x read identify data",
				diskNum);
			ackInterrupt(diskNum);
			return (status = 0);
		}
		else
		{
			error = evaluateError(diskNum);
			if (error != IDE_INVALIDCOMMAND)
			{
				// We don't know what this is
				kernelDebugError("%s", errorMessages[error]);
				ackInterrupt(diskNum);
				return (status);
			}

			// Possibly ATAPI?
			kernelDebug(debug_io, "IDE disk %02x identify failed", diskNum);

			// Read the registers looking for an ATAPI signature
			processorInPort8(DISK_CHAN(diskNum).ports.sectorCount, data[0]);
			processorInPort8(DISK_CHAN(diskNum).ports.lbaLow, data[1]);
			processorInPort8(DISK_CHAN(diskNum).ports.lbaMid, data[2]);
			processorInPort8(DISK_CHAN(diskNum).ports.lbaHigh, data[3]);

			// Check for the ATAPI signature
			if ((data[2] != 0x14) || (data[3] != 0xEB))
			{
				// We don't know what this is
				kernelDebug(debug_io, "IDE disk %02x signature %02x %02x "
					"%02x %02x", diskNum, data[0], data[1], data[2], data[3]);
				return (status = ERR_NOTIMPLEMENTED);
			}

			ackInterrupt(diskNum);

			// Reset the disk before we try again.
			status = reset(diskNum);
			if (status < 0)
				return (status);
		}
	}

	// This is an ATAPI device

	expectInterrupt(diskNum);

	// Send the "identify packet device" command
	kernelDebug(debug_io, "IDE disk %02x try 'id packet dev'", diskNum);
	status = writeCommandFile(diskNum, 0, 0, 0, 0, 0, ATA_ATAPIIDENTIFY);
	if (status < 0)
		return (status);

	// Delay 10ms
	kernelCpuSpinMs(10);

	status = waitOperationComplete(diskNum, 0 /* no yield */,
		1 /* data wait */, 0 /* no ack */, 0 /* default timeout */);
	if (status < 0)
	{
		ackInterrupt(diskNum);
		error = evaluateError(diskNum);
		if (error != IDE_INVALIDCOMMAND)
			// We don't know what this is
			kernelDebugError("%s", errorMessages[error]);

		return (status);
	}

	// Transfer one sector's worth of data from the controller.
	readPort16(DISK_CHAN(diskNum).ports.data, identData,
		(sizeof(ataIdentifyData) / 2));
	kernelDebug(debug_io, "IDE disk %02x read identify data", diskNum);

	ackInterrupt(diskNum);

	return (status);
}


static int readWriteAtapi(int diskNum, uquad_t logicalSector,
	uquad_t numSectors, void *buffer, int read __attribute__((unused)))
{
	int status = 0;
	unsigned atapiNumBytes = 0;
	unsigned char data8 = 0;

	kernelDebug(debug_io, "IDE ATAPI %s %llu at %llu",
		(read? "read" : "write"), numSectors, logicalSector);

	// If it's not started, we start it
	if (!(DISK(diskNum).physical.flags & DISKFLAG_MOTORON))
	{
		// We haven't done the full initial motor on, read TOC, etc.
		kernelDebug(debug_io, "IDE disk %02x starting up", diskNum);
		status = atapiStartStop(diskNum, 1);
		if (status < 0)
			return (status);
	}
	else
	{
		// Just kickstart the device
		kernelDebug(debug_io, "IDE disk %02x kickstart ATAPI device", diskNum);
		status = sendAtapiPacket(diskNum, 0, ATAPI_PACKET_START);
		if (status < 0)
		{
			// Oops, didn't work -- try a full startup
			status = atapiStartStop(diskNum, 1);
			if (status < 0)
				return (status);
		}
	}

	atapiNumBytes = (numSectors * DISK(diskNum).physical.sectorSize);

	status = sendAtapiPacket(diskNum, 0xFFFF, ((unsigned char[])
		{ ATAPI_READ12, 0,
			(unsigned char)((logicalSector >> 24) & 0xFF),
			(unsigned char)((logicalSector >> 16) & 0xFF),
			(unsigned char)((logicalSector >> 8) & 0xFF),
			(unsigned char)(logicalSector & 0xFF),
			(unsigned char)((numSectors >> 24) & 0xFF),
			(unsigned char)((numSectors >> 16) & 0xFF),
			(unsigned char)((numSectors >> 8) & 0xFF),
			(unsigned char)(numSectors & 0xFF),
			0, 0 } ));
	if (status < 0)
		return (status);

	pollStatus(diskNum, ATA_STAT_DRQ, 1);

	// (Possible) interrupt at the beginning says data is ready
	ackInterrupt(diskNum);

	while (atapiNumBytes)
	{
		// Wait for the controller to assert data request
		while (pollStatus(diskNum, ATA_STAT_DRQ, 1))
		{
			// Check for an error...
			processorInPort8(DISK_CHAN(diskNum).ports.altComStat, data8);
			if (data8 & ATA_STAT_ERR)
			{
				kernelError(kernel_error, "%s",
					errorMessages[evaluateError(diskNum)]);
				return (status = ERR_NODATA);
			}
		}

		// How many words to read?
		unsigned bytes = 0;
		processorInPort8(DISK_CHAN(diskNum).ports.lbaMid, data8);
		bytes = data8;
		processorInPort8(DISK_CHAN(diskNum).ports.lbaHigh, data8);
		bytes |= (data8 << 8);

		unsigned words = (bytes >> 1);

		expectInterrupt(diskNum);

		// Transfer the number of words from the disk.
		readPort16(DISK_CHAN(diskNum).ports.data, buffer, words);

		buffer += (words << 1);
		atapiNumBytes -= (words << 1);

		// Just in case it's an odd number
		if (bytes % 2)
		{
			processorInPort8(DISK_CHAN(diskNum).ports.data, data8);
				((unsigned char *) buffer)[0] = data8;
			buffer += 1;
			atapiNumBytes -= 1;
		}

		// Interrupt at the end says data is finished
		waitOperationComplete(diskNum, 0 /* no yield */,
			0 /* no data wait */, 1 /* ack */, 0 /* default timeout */);
	}

	return (status = 0);
}


static int readWriteDma(int diskNum, uquad_t logicalSector, uquad_t numSectors,
	void *buffer, int read)
{
	int status = 0;
	unsigned char command = 0;
	unsigned sectorsPerCommand = 0;
	unsigned dmaBytes = 0;
	int dmaStatus = 0;

	// Figure out which command we're going to be sending to the controller
	if (DISKIS48(diskNum))
	{
		if (read)
			command = ATA_READDMA_EXT;
		else
			command = ATA_WRITEDMA_EXT;
	}
	else
	{
		if (read)
			command = ATA_READDMA;
		else
			command = ATA_WRITEDMA;
	}

	// Figure out the number of sectors per command
	sectorsPerCommand = numSectors;
	if (DISKIS48(diskNum))
	{
		if (sectorsPerCommand > 65536)
			sectorsPerCommand = 65536;
	}
	else if (sectorsPerCommand > 256)
		sectorsPerCommand = 256;

	// This outer loop is done once for each *command* we send.	Actual
	// data transfers, DMA transfers, etc. may occur more than once per command
	// and are handled by the inner loop.	The number of times we send a
	// command depends upon the maximum number of sectors we can specify per
	// command.

	while (numSectors > 0)
	{
		sectorsPerCommand = min(sectorsPerCommand, numSectors);

		// Set up the DMA transfer
		kernelDebug(debug_io, "IDE setting up DMA transfer");
		status = dmaSetup(diskNum, buffer, (sectorsPerCommand * 512), read,
			&dmaBytes);
		if (status < 0)
			return (status);

		if (dmaBytes < (sectorsPerCommand * 512))
		{
			sectorsPerCommand = (dmaBytes / 512);
			kernelDebug(debug_io, "IDE DMA reduces sectors to %u",
				sectorsPerCommand);
		}

		kernelDebug(debug_io, "IDE %d sectors per command",
			sectorsPerCommand);

		// Wait for the controller to be ready
		status = pollStatus(diskNum, ATA_STAT_BSY, 0);
		if (status < 0)
		{
			kernelError(kernel_error, "%s", errorMessages[IDE_TIMEOUT]);
			return (status);
		}

		// We always use LBA.  Break up the sector count and LBA value and
		// deposit them into the appropriate controller registers.
		lbaSetup(diskNum, logicalSector, sectorsPerCommand);

		expectInterrupt(diskNum);

		// Issue the command
		kernelDebug(debug_io, "IDE sending command for %d sectors",
			sectorsPerCommand);
		processorOutPort8(DISK_CHAN(diskNum).ports.comStat, command);

		// Start DMA.
		dmaStartStop(diskNum, 1);

		// Wait for the controller to finish the operation
		status = waitOperationComplete(diskNum, 1 /* yield */,
			0 /* no data wait */, 0 /* no ack */, 0 /* default timeout */);

		// Stop DMA.
		dmaStartStop(diskNum, 0);

		if (status >= 0)
			dmaStatus = dmaCheckStatus(diskNum);

		ackInterrupt(diskNum);

		if ((status < 0) || (dmaStatus < 0))
		{
			kernelError(kernel_error, "Disk %02x, %s %u at %llu: %s",
				diskNum, (read? "read" : "write"),
				sectorsPerCommand, logicalSector,
				((status < 0)? errorMessages[evaluateError(diskNum)] :
					 "DMA error"));
			if (status >= 0)
				status = dmaStatus;
			break;
		}

		buffer += (sectorsPerCommand * 512);
		numSectors -= sectorsPerCommand;
		logicalSector += sectorsPerCommand;
	}

	return (status);
}


static int readWritePio(int diskNum, uquad_t logicalSector, uquad_t numSectors,
	void *buffer, int read)
{
	int status = 0;
	unsigned char command = 0;
	unsigned sectorsPerCommand = 0;
	unsigned sectorsPerInt = 0;
	unsigned ints = 0;
	unsigned count;

	// Figure out which command we're going to be sending to the controller
	if (DISKISMULTI(diskNum))
	{
		if (DISKIS48(diskNum))
		{
			if (read)
				command = ATA_READMULTI_EXT;
			else
				command = ATA_WRITEMULTI_EXT;
		}
		else
		{
			if (read)
				command = ATA_READMULTI;
			else
				command = ATA_WRITEMULTI;
		}
	}
	else
	{
		if (DISKIS48(diskNum))
		{
			if (read)
				command = ATA_READSECTS_EXT;
			else
				command = ATA_WRITESECTS_EXT;
		}
		else
		{
			if (read)
				command = ATA_READSECTS;
			else
				command = ATA_WRITESECTS;
		}
	}

	// Figure out the number of sectors per command
	sectorsPerCommand = numSectors;
	if (DISKIS48(diskNum))
	{
		if (sectorsPerCommand > 65536)
			sectorsPerCommand = 65536;
	}
	else if (sectorsPerCommand > 256)
		sectorsPerCommand = 256;

	// This outer loop is done once for each *command* we send.  Actual
	// data transfers, DMA transfers, etc. may occur more than once per command
	// and are handled by the inner loop.  The number of times we send a
	// command depends upon the maximum number of sectors we can specify per
	// command.

	while (numSectors > 0)
	{
		sectorsPerCommand = min(sectorsPerCommand, numSectors);

		// Calculate the number of data cycles (interrupts) for this command
		if (DISKISMULTI(diskNum))
			sectorsPerInt = min(sectorsPerCommand,
				(unsigned) DISK(diskNum).physical.multiSectors);
		else
			sectorsPerInt = 1;

		ints = ((sectorsPerCommand + (sectorsPerInt - 1)) / sectorsPerInt);

		kernelDebug(debug_io, "IDE %d sectors per command, %d per interrupt, "
			"%d interrupts", sectorsPerCommand, sectorsPerInt, ints);

		// Wait for the controller to be ready
		status = pollStatus(diskNum, ATA_STAT_BSY, 0);
		if (status < 0)
		{
			kernelError(kernel_error, "%s", errorMessages[IDE_TIMEOUT]);
			return (status);
		}

		// We always use LBA.	Break up the sector count and LBA value and
		// deposit them into the appropriate controller registers.
		lbaSetup(diskNum, logicalSector, sectorsPerCommand);

		expectInterrupt(diskNum);

		// Issue the command
		kernelDebug(debug_io, "IDE sending command for %d sectors",
			sectorsPerCommand);
		processorOutPort8(DISK_CHAN(diskNum).ports.comStat, command);

		// The inner loop is used to service each interrupt.  The disk will
		// interrupt once for each sector (or multiple of sectors if read/write
		// multiple is enabled) and we will read each word of data from the
		// port.

		for (count = 0; count < ints; count ++)
		{
			sectorsPerInt = min(sectorsPerInt, numSectors);
			kernelDebug(debug_io, "IDE cycle %d for %d sectors", count,
				sectorsPerInt);

			if (!read)
			{
				// Wait for DRQ
				while (pollStatus(diskNum, ATA_STAT_DRQ, 1));

				kernelDebug(debug_io, "IDE transfer out %d sectors",
					sectorsPerInt);
				writePort16(DISK_CHAN(diskNum).ports.data, buffer,
					(sectorsPerInt * 256));
			}

			// Wait for the controller to finish the operation
			status = waitOperationComplete(diskNum, 1 /* yield */,
				read /* data wait on read */, 0 /* no ack */,
				0 /* default timeout */);
			if (status < 0)
				break;

			if (read)
			{
				kernelDebug(debug_io, "IDE transfer in %d sectors",
					sectorsPerInt);
				readPort16(DISK_CHAN(diskNum).ports.data, buffer,
					(sectorsPerInt * 256));
			}

			// 'expect' before 'ack' in case the next interrupt comes really
			// quickly.
			if (count < (ints - 1))
				expectInterrupt(diskNum);

			ackInterrupt(diskNum);

			buffer += (sectorsPerInt * 512);
			numSectors -= sectorsPerInt;
			logicalSector += sectorsPerInt;
		}

		if (status < 0)
		{
			kernelError(kernel_error, "Disk %s, %s %llu at %llu: %s",
				DISK(diskNum).physical.name, (read? "read" : "write"),
				numSectors, logicalSector,
				errorMessages[evaluateError(diskNum)]);
			return (status);
		}
	}

	// Return success
	return (status = 0);
}


static int readWriteSectors(int diskNum, uquad_t logicalSector,
	uquad_t numSectors, void *buffer, int read)
{
	// This routine reads or writes sectors to/from the disk.  Returns 0 on
	// success, negative otherwise.

	int status = 0;

	kernelDebug(debug_io, "IDE disk %02x %s %llu at %llu", diskNum,
		(read? "read" : "write"), numSectors, logicalSector);

	if (!DISK(diskNum).physical.name[0])
	{
		kernelError(kernel_error, "No such disk %02x", diskNum);
		return (status = ERR_NOSUCHENTRY);
	}

	// Make sure we don't try to read/write an address we can't access
	if (!DISKIS48(diskNum) && ((logicalSector + numSectors - 1) > 0x0FFFFFFF))
	{
		kernelError(kernel_error, "Can't access sectors %llu->%llu on disk "
			"%02x with 28-bit addressing", logicalSector,
			(logicalSector + numSectors - 1), diskNum);
		return (status = ERR_BOUNDS);
	}

	// Wait for a lock on the controller
	status = kernelLockGet(&DISK_CHAN(diskNum).lock);
	if (status < 0)
		return (status);

	// Select the disk
	status = select(diskNum);
	if (status < 0)
		goto out;

	// If it's an ATAPI device
	if (DISK(diskNum).physical.type & DISKTYPE_IDECDROM)
	{
		status = readWriteAtapi(diskNum, logicalSector, numSectors, buffer,
			read);
	}

	// Or a DMA ATA device
	else if (DISKISDMA(diskNum))
	{
		status = readWriteDma(diskNum, logicalSector, numSectors, buffer,
			read);
	}

	// Default: A PIO ATA device
	else
	{
		status = readWritePio(diskNum, logicalSector, numSectors, buffer,
			read);
	}

out:
	if (!status)
		// We are finished.  The data should be transferred.
		kernelDebug(debug_io, "IDE transfer successful");

	// Unlock the controller
	kernelLockRelease(&DISK_CHAN(diskNum).lock);

	return (status);
}


static int atapiReset(int diskNum)
{
	int status = 0;
	unsigned char data = 0;

	kernelDebug(debug_io, "IDE disk %02x ATAPI reset", diskNum);

	// Wait for controller ready
	status = pollStatus(diskNum, ATA_STAT_BSY, 0);
	if (status < 0)
		return (status);

	expectInterrupt(diskNum);

	// Enable "revert to power on defaults"
	status = writeCommandFile(diskNum, 0xCC, 0, 0, 0, 0, ATA_SETFEATURES);
	if (status < 0)
		return (status);

	status = waitOperationComplete(diskNum, 1 /* yield */,
		0 /* no data wait */, 1 /* ack */, 0 /* default timeout */);
	if (status < 0)
		return (status);

	// Do ATAPI reset
	processorOutPort8(DISK_CHAN(diskNum).ports.comStat, ATA_ATAPIRESET);

	// Delay 5ms
	kernelCpuSpinMs(5);

	// Wait for controller ready
	status = pollStatus(diskNum, ATA_STAT_BSY, 0);
	if (status < 0)
		return (status);

	// Read the status register
	processorInPort8(DISK_CHAN(diskNum).ports.comStat, data);
	if (data & ATA_STAT_ERR)
	{
		kernelError(kernel_error, "%s", errorMessages[evaluateError(diskNum)]);
		return (status = ERR_NOTINITIALIZED);
	}

	kernelDebug(debug_io, "IDE disk %02x ATAPI reset finished", diskNum);
	return (status = 0);
}


static int atapiSetLockState(int diskNum, int locked)
{
	// Lock or unlock an ATAPI device

	int status = 0;

	kernelDebug(debug_io, "IDE disk %02x %slock ATAPI device", diskNum,
		(locked? "" : "un"));

	if (locked)
		status = sendAtapiPacket(diskNum, 0, ATAPI_PACKET_LOCK);
	else
		status = sendAtapiPacket(diskNum, 0, ATAPI_PACKET_UNLOCK);

	if (status < 0)
		return (status);

	if (locked)
		DISK(diskNum).physical.flags |= DISKFLAG_DOORLOCKED;
	else
		DISK(diskNum).physical.flags &= ~DISKFLAG_DOORLOCKED;

	return (status);
}


static int atapiSetDoorState(int diskNum, int open)
{
	// Open or close the door of an ATAPI device

	int status = 0;

	kernelDebug(debug_io, "IDE disk %02x %s ATAPI device", diskNum,
		(open? "open" : "close"));

	if (open)
	{
		// Stop it, then eject.
		atapiStartStop(diskNum, 0);
		status = sendAtapiPacket(diskNum, 0, ATAPI_PACKET_EJECT);
	}
	else
		status = sendAtapiPacket(diskNum, 0, ATAPI_PACKET_CLOSE);

	if (status < 0)
		return (status);

	if (open)
		DISK(diskNum).physical.flags |= DISKFLAG_DOOROPEN;
	else
		DISK(diskNum).physical.flags &= ~DISKFLAG_DOOROPEN;

	return (status);
}


static void pciIdeInterrupt(void)
{
	// This is the PCI IDE interrupt handler.  It will be called whenever the
	// disk controller issues its service interrupt, and will simply change a
	// data value to indicate that one has been received.  It's up to the other
	// routines to do something useful with the information.

	void *address = NULL;
	int interruptNum = 0;
	unsigned char status = 0;
	int serviced = 0;
	int count1, count2;

	processorIsrEnter(address);

	// Which interrupt number is active?
	interruptNum = kernelPicGetActive();
	if (interruptNum < 0)
		goto out;

	kernelInterruptSetCurrent(interruptNum);

	kernelDebug(debug_io, "IDE PCI interrupt");

	// Loop through the controllers to find the one that uses this interrupt
	for (count1 = 0; (count1 < numControllers) && !serviced ; count1 ++)
	{
		if (controllers[count1].pciInterrupt == interruptNum)
		{
			// Figure out which channel(s) are asserting an interrupt.
			for (count2 = 0; count2 < 2; count2 ++)
			{
				processorInPort8(BMPORT_STATUS(count1, count2), status);
				if (status & 0x04)
				{
					CHANNEL(count1, count2).gotInterrupt = 1;
					if (CHANNEL(count1, count2).expectInterrupt)
					{
						kernelDebug(debug_io, "IDE controller %d channel %d "
							"PCI interrupt %d #%d", count1, count2,
							interruptNum, CHANNEL(count1, count2).ints);

						// Wake up the process that's expecting the interrupt
						kernelMultitaskerSetProcessState(CHANNEL(count1,
							count2).expectInterrupt, proc_ioready);
						CHANNEL(count1, count2).expectInterrupt = 0;
					}
					else
					{
						kernelDebugError("Controller %d channel %d unexpected "
							"PCI interrupt %d #%d", count1, count2,
							interruptNum, CHANNEL(count1, count2).ints);
						ackInterrupt((count1 << 4) | (count2 << 1));
					}

					CHANNEL(count1, count2).ints += 1;

					// Read the alternate status register
					processorInPort8(CHANNEL(count1, count2).ports.altComStat,
						CHANNEL(count1, count2).intStatus);

					// Write back (clear) the status register's interrupt bit
					processorOutPort8(BMPORT_STATUS(count1, count2),
						(status & 0x04));

					serviced = 1;
				}
			}
		}
	}

	kernelInterruptClearCurrent();

	if (!serviced && oldIntHandlers[interruptNum])
		// We didn't service this interrupt, and we're sharing this PCI
		// interrupt with another device whose handler we saved.  Call it.
		processorIsrCall(oldIntHandlers[interruptNum]);

out:
	processorIsrExit(address);
}


static void primaryIdeInterrupt(void)
{
	// This is the IDE interrupt handler for the primary channel.  It will
	// be called whenever the disk controller issues its service interrupt,
	// and will simply change a data value to indicate that one has been
	// received.  It's up to the other routines to do something useful with
	// the information.

	void *address = NULL;
	int interruptNum = 0;
	int count;

	processorIsrEnter(address);

	// Which interrupt number is active?
	interruptNum = kernelPicGetActive();
	if (interruptNum < 0)
		goto out;

	kernelInterruptSetCurrent(interruptNum);

	kernelDebug(debug_io, "IDE primary interrupt");

	// Loop through the controllers to find the one that uses this interrupt
	for (count = 0; count < numControllers; count ++)
	{
		if (CHANNEL(count, 0).interrupt == interruptNum)
		{
			CHANNEL(count, 0).gotInterrupt = 1;
			if (CHANNEL(count, 0).expectInterrupt)
			{
				kernelDebug(debug_io, "IDE controller %d primary interrupt "
					"%d #%d", count, interruptNum, CHANNEL(count, 0).ints);
				// Wake up the process that's expecting the interrupt
				kernelMultitaskerSetProcessState(CHANNEL(count, 0)
					.expectInterrupt, proc_ioready);
				CHANNEL(count, 0).expectInterrupt = 0;
			}
			else
			{
				kernelDebugError("IDE controller %d unexpected primary "
					"interrupt %d #%d", count, interruptNum,
					CHANNEL(count, 0).ints);
				ackInterrupt(count << 4);
			}

			CHANNEL(count, 0).ints += 1;

			// Read the alternate status register
			processorInPort8(CHANNEL(count, 0).ports.altComStat,
				CHANNEL(count, 0).intStatus);

			break;
		}
	}

out:
	kernelInterruptClearCurrent();
	processorIsrExit(address);
}


static void secondaryIdeInterrupt(void)
{
	// This is the IDE interrupt handler for the secondary channel.  It will
	// be called whenever the disk controller issues its service interrupt,
	// and will simply change a data value to indicate that one has been
	// received.  It's up to the other routines to do something useful with
	// the information.

	void *address = NULL;
	int interruptNum = 0;
	int count;

	processorIsrEnter(address);

	// Which interrupt number is active?
	interruptNum = kernelPicGetActive();
	if (interruptNum < 0)
		goto out;

	kernelInterruptSetCurrent(interruptNum);

	kernelDebug(debug_io, "IDE secondary interrupt");

	// Loop through the controllers to find the one that uses this interrupt
	for (count = 0; count < numControllers; count ++)
	{
		if (CHANNEL(count, 1).interrupt == interruptNum)
		{
			CHANNEL(count, 1).gotInterrupt = 1;
			if (CHANNEL(count, 1).expectInterrupt)
			{
				kernelDebug(debug_io, "IDE controller %d secondary interrupt "
					"%d #%d", count, interruptNum, CHANNEL(count, 1).ints);
				// Wake up the process that's expecting the interrupt
				kernelMultitaskerSetProcessState(CHANNEL(count, 1)
					.expectInterrupt, proc_ioready);
				CHANNEL(count, 1).expectInterrupt = 0;
			}
			else
			{
				kernelDebugError("IDE controller %d unexpected secondary "
					"interrupt %d #%d", count, interruptNum,
					CHANNEL(count, 1).ints);
				ackInterrupt((count << 4) | 2);
			}

			CHANNEL(count, 1).ints += 1;

			// Read the alternate status register
			processorInPort8(CHANNEL(count, 1).ports.altComStat,
				CHANNEL(count, 1).intStatus);

			break;
		}
	}

out:
	kernelInterruptClearCurrent();
	processorIsrExit(address);
}


static int setTransferMode(int diskNum, ataDmaMode *mode,
	ataIdentifyData *identData)
{
	// Try to set the transfer mode (e.g. DMA, UDMA).

	int status = 0;

	kernelDebug(debug_io, "IDE disk %02x set transfer mode %s (%02x)",
		diskNum, mode->name, mode->val);

	// Wait for controller ready
	status = pollStatus(diskNum, ATA_STAT_BSY, 0);
	if (status < 0)
		return (status);

	expectInterrupt(diskNum);

	status = writeCommandFile(diskNum, 0x03, mode->val, 0, 0, 0,
		ATA_SETFEATURES);
	if (status < 0)
		return (status);

	// Wait for the command to complete
	status = waitOperationComplete(diskNum, 1 /* yield */,
		0 /* no data wait */, 1 /* ack */, 0 /* default timeout */);
	if (status < 0)
		return (status);

	// Now we do an "identify device" to find out if we were successful
	status = identify(diskNum, identData);
	if (status < 0)
		// Couldn't verify.
		return (status);

	// Verify that the requested mode has been set
	if (identData->word[mode->identWord] & mode->enabledMask)
	{
		kernelDebug(debug_io, "IDE disk %02x successfully set transfer mode "
			"%s", diskNum, mode->name);
		return (status = 0);
	}
	else
	{
		kernelDebugError("Failed to set transfer mode %s for disk %02x",
			mode->name, diskNum);
		return (status = ERR_INVALID);
	}
}


static int setMultiMode(int diskNum, unsigned short multiSectors)
{
	// Set multiple mode

	int status = 0;
	ataIdentifyData identData;

	kernelDebug(debug_io, "IDE set multiple mode (%d) for disk %02x",
		multiSectors, diskNum);

	// Wait for the controller to be ready
	status = pollStatus(diskNum, ATA_STAT_BSY, 0);
	if (status < 0)
	{
		kernelError(kernel_error, "%s", errorMessages[IDE_TIMEOUT]);
		goto out;
	}

	expectInterrupt(diskNum);

	status = writeCommandFile(diskNum, 0, multiSectors, 0, 0, 0,
		ATA_SETMULTIMODE);
	if (status < 0)
		goto out;

	// Wait for the controller to finish the operation
	status = waitOperationComplete(diskNum, 1 /* yield */,
		0 /* no data wait */, 1 /* ack */, 0 /* default timeout */);
	if (status < 0)
		goto out;

	// Now we do an "identify device" to find out if we were successful
	status = identify(diskNum, &identData);
	if (status < 0)
		// Couldn't verify.
		goto out;

out:
	// Determine whether multimode is enabled or not

	if (identData.field.multiSector & 0x0100)
	{
		if ((identData.field.multiSector & 0xFF) == multiSectors)
			kernelDebug(debug_io, "IDE set multiple mode succeeded (%d) for "
				"disk %02x", (identData.field.multiSector & 0xFF), diskNum);
		else
			kernelDebugError("Failed to set multiple mode for disk %02x to %d "
				"(now %d)", diskNum, multiSectors,
				(identData.field.multiSector & 0xFF));
		DISK(diskNum).featureFlags |= ATA_FEATURE_MULTI;
		DISK(diskNum).physical.multiSectors =
			(identData.field.multiSector & 0xFF);
		status = 0;
	}
	else
	{
		kernelDebugError("Failed to set multiple mode for disk %02x", diskNum);
		DISK(diskNum).featureFlags &= ~ATA_FEATURE_MULTI;
		DISK(diskNum).physical.multiSectors = 1;
		status = ERR_INVALID;
	}

	return (status);
}


static int enableFeature(int diskNum, ataFeature *feature,
	ataIdentifyData *identData)
{
	// Try to enable a general feature.

	int status = 0;

	kernelDebug(debug_io, "IDE disk %02x enable feature %s", diskNum,
		feature->name);

	// Wait for controller ready
	status = pollStatus(diskNum, ATA_STAT_BSY, 0);
	if (status < 0)
	{
		kernelError(kernel_error, "%s", errorMessages[IDE_TIMEOUT]);
		return (status);
	}

	expectInterrupt(diskNum);

	// Send the "set features" command
	status = writeCommandFile(diskNum, feature->featureCode, 0, 0, 0, 0,
		ATA_SETFEATURES);
	if (status < 0)
		return (status);

	// Wait for the command to complete
	status = waitOperationComplete(diskNum, 1 /* yield */,
		0 /* no data wait */, 1 /* ack */, 0 /* default timeout */);
	if (status < 0)
		return (status);

	// Can we verify that we were successful?
	if (feature->enabledWord)
	{
		// Now we do an "identify device" to find out if we were successful
		status = identify(diskNum, identData);
		if (status < 0)
			// Couldn't verify.
			return (status);

		// Verify that the requested mode has been set
		if (identData->word[feature->enabledWord] & feature->enabledMask)
		{
			kernelDebug(debug_io, "IDE disk %02x successfully set feature %s",
				diskNum, feature->name);
			return (status = 0);
		}
		else
		{
			kernelDebugError("Failed to set feature %s for disk %02x",
				 feature->name, diskNum);
			return (status = ERR_INVALID);
		}
	}

	return (status = 0);
}


static int testDma(int diskNum)
{
	// This is called once for each hard disk for which we've initially
	// selected DMA operation.  It does a simple single-sector read.  If that
	// succeeds then we continue to use DMA.  Otherwise, we clear the DMA
	// feature flags.

	int status = 0;
	unsigned testSecs = 0;
	unsigned char *buffer = NULL;

	#define DMATESTSECS 32

	kernelDebug(debug_io, "IDE disk %02x test DMA", diskNum);

	testSecs = max((DISK(diskNum).physical.multiSectors + 1), DMATESTSECS);

	buffer = kernelMalloc(testSecs * DISK(diskNum).physical.sectorSize);
	if (!buffer)
		return (status = ERR_MEMORY);

	status = readWriteDma(diskNum, 0, testSecs, buffer, 1);

	kernelFree(buffer);

	if (status < 0)
		kernelLog("IDE: Disk %d DMA test failed", diskNum);

	return (status);
}


static int driverSetLockState(int diskNum, int lockState)
{
	// This will lock or unlock the CD-ROM door

	int status = 0;

	if (!DISK(diskNum).physical.name[0])
	{
		kernelError(kernel_error, "No such disk %02x", diskNum);
		return (status = ERR_NOSUCHENTRY);
	}

	// Wait for a lock on the controller
	status = kernelLockGet(&DISK_CHAN(diskNum).lock);
	if (status < 0)
		return (status);

	// Select the disk
	status = select(diskNum);
	if (status < 0)
		goto out;

	status = atapiSetLockState(diskNum, lockState);

out:
	// Unlock the controller
	kernelLockRelease(&DISK_CHAN(diskNum).lock);

	return (status);
}


static int driverSetDoorState(int diskNum, int open)
{
	// This will open or close the CD-ROM door

	int status = 0;

	if (!DISK(diskNum).physical.name[0])
	{
		kernelError(kernel_error, "No such disk %02x", diskNum);
		return (status = ERR_NOSUCHENTRY);
	}

	if (open && (DISK(diskNum).physical.flags & DISKFLAG_DOORLOCKED))
	{
		// Don't try to open the door if it is locked
		kernelError(kernel_error, "Disk door is locked");
		return (status = ERR_PERMISSION);
	}

	// Wait for a lock on the controller
	status = kernelLockGet(&DISK_CHAN(diskNum).lock);
	if (status < 0)
		return (status);

	// Select the disk
	status = select(diskNum);
	if (status < 0)
		goto out;

	status = atapiSetDoorState(diskNum, open);

out:
	// Unlock the controller
	kernelLockRelease(&DISK_CHAN(diskNum).lock);

	return (status);
}


static int driverMediaPresent(int diskNum)
{
	int present = 0;

	kernelDebug(debug_io, "IDE check media present");

	// If it's not removable, say media is present
	if (!(DISK(diskNum).physical.type & DISKTYPE_REMOVABLE))
		return (present = 1);

	// Wait for a lock on the controller
	if (kernelLockGet(&DISK_CHAN(diskNum).lock) < 0)
		return (present = 0);

	// Select the disk
	if (select(diskNum) < 0)
		goto out;

	kernelDebug(debug_io, "IDE does %ssupport media status",
		(DISKISMEDSTAT(diskNum)? "" : "not "));

	// If it's an ATAPI device
	if (DISK(diskNum).physical.type & DISKTYPE_IDECDROM)
	{
		// If it's not started, we start it
		if (!(DISK(diskNum).physical.flags & DISKFLAG_MOTORON))
		{
			// We haven't done the full initial motor on, read TOC, etc.
			kernelDebug(debug_io, "IDE disk %02x starting up", diskNum);
			if (atapiStartStop(diskNum, 1) >= 0)
				present = 1;
		}
		else
		{
			// Just kickstart the device
			kernelDebug(debug_io, "IDE disk %02x kickstart ATAPI device", diskNum);
			if (sendAtapiPacket(diskNum, 0, ATAPI_PACKET_START) >= 0)
			{
				present = 1;
			}
			else
			{
				// Oops, didn't work -- try a full startup
				if (atapiStartStop(diskNum, 1) >= 0)
					present = 1;
			}
		}
	}

	kernelDebug(debug_io, "IDE media %spresent", (present? "" : "not "));

out:
	kernelLockRelease(&DISK_CHAN(diskNum).lock);
	return (present);
}


static int driverReadSectors(int diskNum, uquad_t logicalSector,
	uquad_t numSectors, void *buffer)
{
	// This routine is a wrapper for the readWriteSectors routine.
	return (readWriteSectors(diskNum, logicalSector, numSectors, buffer,
		1));	// Read operation
}


static int driverWriteSectors(int diskNum, uquad_t logicalSector,
	uquad_t numSectors, const void *buffer)
{
	// This routine is a wrapper for the readWriteSectors routine.
	return (readWriteSectors(diskNum, logicalSector, numSectors,
		(void *) buffer, 0));	// Write operation
}


static int driverFlush(int diskNum)
{
	// If write caching is enabled for this disk, flush the cache

	int status = 0;
	unsigned char command = 0;

	if (!DISK(diskNum).physical.name[0])
	{
		kernelError(kernel_error, "No such disk %02x", diskNum);
		return (status = ERR_NOSUCHENTRY);
	}

	// If write caching is not enabled, just return
	if (!DISKISWCACHE(diskNum))
		return (status = 0);

	// Wait for a lock on the controller
	status = kernelLockGet(&DISK_CHAN(diskNum).lock);
	if (status < 0)
		return (status);

	// Select the disk and wait for it to be ready
	if (select(diskNum) < 0)
	{
		kernelError(kernel_error, "%s", errorMessages[IDE_TIMEOUT]);
		goto out;
	}

	// Figure out which command we're going to be sending to the controller
	if (DISKIS48(diskNum))
		command = ATA_FLUSHCACHE_EXT;
	else
		command = ATA_FLUSHCACHE;

	expectInterrupt(diskNum);

	// Issue the command
	kernelDebug(debug_io, "IDE sending 'flush' command");
	processorOutPort8(DISK_CHAN(diskNum).ports.comStat, command);

	// Wait for the controller to finish the operation
	status = waitOperationComplete(diskNum, 1 /* yield */,
		0 /* no data wait */, 1 /* ack */, 0 /* default timeout */);
	if (status < 0)
		goto out;

	status = 0;

out:
	// Unlock the controller
	kernelLockRelease(&DISK_CHAN(diskNum).lock);

	return (status);
}


static int detectPciControllers(kernelDevice *controllerDevices[],
	kernelDriver *driver)
{
	// Try to detect IDE controllers on the PCI bus

	int status = 0;
	kernelBusTarget *pciTargets = NULL;
	int numPciTargets = 0;
	int deviceCount = 0;
	pciDeviceInfo pciDevInfo;
	unsigned prdMemorySize = 0;
	int count;

	// See if there are any IDE controllers on the PCI bus.  This obviously
	// depends upon PCI hardware detection occurring before IDE detection.

	kernelDebug(debug_io, "IDE detect PCI controllers");

	// Search the PCI bus(es) for devices
	numPciTargets = kernelBusGetTargets(bus_pci, &pciTargets);
	if (numPciTargets <= 0)
		return (numPciTargets);

	// Search the PCI bus targets for IDE controllers
	for (deviceCount = 0; deviceCount < numPciTargets; deviceCount ++)
	{
		// If it's not an IDE or SATA controller, skip it
		if (!pciTargets[deviceCount].class ||
			(pciTargets[deviceCount].class->class != DEVICECLASS_DISKCTRL) ||
			!pciTargets[deviceCount].subClass ||
			((pciTargets[deviceCount].subClass->class !=
				DEVICESUBCLASS_DISKCTRL_IDE) &&
			(pciTargets[deviceCount].subClass->class !=
				DEVICESUBCLASS_DISKCTRL_SATA)))
		{
			continue;
		}

		// Get the PCI device header
		status = kernelBusGetTargetInfo(&pciTargets[deviceCount], &pciDevInfo);
		if (status < 0)
			continue;

		// Make sure it's a non-bridge header
		if (pciDevInfo.device.headerType != PCI_HEADERTYPE_NORMAL)
		{
			kernelDebug(debug_io, "IDE PCI headertype not 'normal' (%d)",
				pciDevInfo.device.headerType);
			continue;
		}

		kernelDebug(debug_io, "IDE PCI Found");

		if ((pciTargets[deviceCount].subClass->class ==
			DEVICESUBCLASS_DISKCTRL_SATA) && pciTargets[deviceCount].claimed)
		{
			// This appears to be an AHCI controller that has been claimed by
			// the AHCI driver (i.e. operating in native SATA mode)
			kernelDebug(debug_io, "IDE PCI controller has already been "
				"claimed, perhaps by AHCI");
			continue;
		}
		else if (pciTargets[deviceCount].subClass->class ==
			DEVICESUBCLASS_DISKCTRL_IDE)
		{
			// Make sure it's a bus-mastering controller
			if (!(pciDevInfo.device.progIF & 0x80))
			{
				kernelDebug(debug_io, "IDE PCI not a bus-mastering IDE.  "
					"ProgIF=%02x", pciDevInfo.device.progIF);
				continue;
			}
		}

		// Try to enable bus mastering
		if (!(pciDevInfo.device.commandReg & PCI_COMMAND_MASTERENABLE))
		{
			kernelBusSetMaster(&pciTargets[deviceCount], 1);

			// Re-read target info
			status = kernelBusGetTargetInfo(&pciTargets[deviceCount],
				&pciDevInfo);
			if (status < 0)
				continue;

			if (!(pciDevInfo.device.commandReg & PCI_COMMAND_MASTERENABLE))
			{
				kernelDebugError("Couldn't enable bus mastering");
				continue;
			}
			else
			{
				kernelDebug(debug_usb, "IDE PCI bus mastering enabled");
			}
		}
		else
		{
			kernelDebug(debug_io, "IDE PCI bus mastering already enabled");
		}

		// Try to enable the device's I/O decoder
		if (!(pciDevInfo.device.commandReg & PCI_COMMAND_IOENABLE))
		{
			kernelBusDeviceEnable(&pciTargets[deviceCount],
				PCI_COMMAND_IOENABLE);

			// Re-read target info
			status = kernelBusGetTargetInfo(&pciTargets[deviceCount],
				&pciDevInfo);
			if (status < 0)
				continue;

			if (!(pciDevInfo.device.commandReg & PCI_COMMAND_IOENABLE))
			{
				kernelDebugError("Couldn't enable device I/O");
				continue;
			}
			else
			{
				kernelDebug(debug_usb, "IDE PCI device I/O enabled");
			}
		}
		else
		{
			kernelDebug(debug_io, "IDE PCI device I/O already enabled");
		}

		// (Re)allocate memory for the controllers
		controllers = kernelRealloc((void *) controllers,
			((numControllers + 1) *	sizeof(ideController)));
		if (!controllers)
			return (status = ERR_MEMORY);

		// Print the registers
		kernelDebug(debug_io, "IDE PCI interrupt line=%d",
			pciDevInfo.device.nonBridge.interruptLine);

		kernelDebug(debug_io, "IDE PCI primary command regs=%08x",
			pciDevInfo.device.nonBridge.baseAddress[0]);
		kernelDebug(debug_io, "IDE PCI primary control reg=%08x",
			pciDevInfo.device.nonBridge.baseAddress[1]);
		kernelDebug(debug_io, "IDE PCI secondary command regs=%08x",
			pciDevInfo.device.nonBridge.baseAddress[2]);
		kernelDebug(debug_io, "IDE PCI secondary control reg=%08x",
			pciDevInfo.device.nonBridge.baseAddress[3]);
		kernelDebug(debug_io, "IDE PCI busmaster control reg=%08x",
			pciDevInfo.device.nonBridge.baseAddress[4]);

		// Get the interrupt number
		if (pciDevInfo.device.nonBridge.interruptLine != 0xFF)
		{
			kernelDebug(debug_io, "IDE PCI using PCI interrupt=%d",
				pciDevInfo.device.nonBridge.interruptLine);

			// Use the interrupt number specified here.
			CHANNEL(numControllers, 0).interrupt =
				CHANNEL(numControllers, 1).interrupt =
					controllers[numControllers].pciInterrupt =
						pciDevInfo.device.nonBridge.interruptLine;
		}
		else
		{
			kernelDebug(debug_io, "IDE PCI unknown interrupt=%d",
				pciDevInfo.device.nonBridge.interruptLine);
		}

		// Get the PCI IDE channel port addresses
		for (count = 0; count < 2; count ++)
		{
			unsigned portAddr =
				(pciDevInfo.device.nonBridge.baseAddress[count * 2] &
					0xFFFFFFFE);

			if (portAddr)
			{
				CHANNEL(numControllers, count).ports.data = portAddr++;
				CHANNEL(numControllers, count).ports.featErr = portAddr++;
				CHANNEL(numControllers, count).ports.sectorCount = portAddr++;
				CHANNEL(numControllers, count).ports.lbaLow = portAddr++;
				CHANNEL(numControllers, count).ports.lbaMid = portAddr++;
				CHANNEL(numControllers, count).ports.lbaHigh = portAddr++;
				CHANNEL(numControllers, count).ports.device = portAddr++;
				CHANNEL(numControllers, count).ports.comStat = portAddr++;
				CHANNEL(numControllers, count).ports.altComStat =
					((pciDevInfo.device.nonBridge.baseAddress[(count * 2) + 1] &
					0xFFFFFFFE) + 2);
				kernelDebug(debug_io, "IDE PCI I/O ports %04x-%04x & %04x",
					CHANNEL(numControllers, count).ports.data,
					(CHANNEL(numControllers, count).ports.data + 7),
					CHANNEL(numControllers, count).ports.altComStat);
			}
			else
			{
				kernelDebug(debug_io, "IDE PCI channel %d unknown I/O port "
					"addresses", count);
			}
		}

		// Get the bus master IO address
		controllers[numControllers].busMasterIo =
			(pciDevInfo.device.nonBridge.baseAddress[4] & 0xFFFFFFFE);
		if (!controllers[numControllers].busMasterIo)
		{
			kernelDebugError("Unknown bus master I/O address");
			continue;
		}

		// We found a bus mastering controller.
		controllers[numControllers].busMaster = 1;

		kernelDebug(debug_io, "IDE PCI bus master I/O address=%04x",
			controllers[numControllers].busMasterIo);

		// Determine whether each channel is operating in 'native PCI' or
		// 'compatibility' mode.

		// If native mode, then all is well.

		// If compatibility mode, and the mode is programmable, and the
		// controller has a PCI interrupt, and the channel has I/O addresses
		// already assigned, then we will change it to native mode.

		kernelDebug(debug_io, "IDE PCI progIF=%02x", pciDevInfo.device.progIF);
		for (count = 0; count < 2; count ++)
		{
			if (!(pciDevInfo.device.progIF & (1 << (count * 2))))
			{
				kernelDebug(debug_io, "IDE PCI channel %d is operating in "
					"compatibility mode", count);

				if ((pciDevInfo.device.progIF & (2 << (count * 2))) &&
					controllers[numControllers].pciInterrupt &&
					CHANNEL(numControllers, count).ports.data)
				{
					pciDevInfo.device.progIF |= (1 << (count * 2));

					kernelBusWriteRegister(&pciTargets[deviceCount],
						PCI_CONFREG_PROGIF_8, 8, pciDevInfo.device.progIF);

					// Re-read target info
					status = kernelBusGetTargetInfo(&pciTargets[deviceCount],
						&pciDevInfo);
					if (status < 0)
						continue;

					kernelDebug(debug_io, "IDE PCI progIF now=%02x",
						pciDevInfo.device.progIF);
				}

				if (pciDevInfo.device.progIF & (1 << (count * 2)))
					kernelDebug(debug_io, "IDE PCI channel %d switched to "
						"native PCI mode", count);
				else
					// This channel will stay in compatibility mode
					CHANNEL(numControllers, count).compatibility = 1;
			}
		}

		// Get memory for physical region descriptors and transfer areas

		for (count = 0; count < 2; count ++)
		{
			// 65K max per PRD, max 65K 512-byte sectors per command equals
			// maximum 512 PRDs.

			CHANNEL(numControllers, count).prdEntries = 512;

			prdMemorySize = (CHANNEL(numControllers, count).prdEntries *
				sizeof(idePrd));

			status = kernelMemoryGetIo(prdMemorySize, DISK_CACHE_ALIGN,
				(kernelIoMemory *) &CHANNEL(numControllers, count).prds);
		}

		if (status < 0)
			continue;

		// Success.
		kernelLog("IDE: %sPCI controller enabled",
			(controllers[numControllers].busMaster?
				 "Bus mastering " : ""));

		// Create a device for it in the kernel.

		controllerDevices[numControllers] = kernelMalloc(sizeof(kernelDevice));
		if (controllerDevices[numControllers])
		{
			controllerDevices[numControllers]->device.class =
				kernelDeviceGetClass(DEVICECLASS_DISKCTRL);
			controllerDevices[numControllers]->device.subClass =
				kernelDeviceGetClass(DEVICESUBCLASS_DISKCTRL_IDE);

			// Initialize the variable list for attributes of the controller
			kernelVariableListCreate(
				&controllerDevices[numControllers]->device.attrs);

			// Claim the controller device in the list of PCI targets.
			kernelBusDeviceClaim(&pciTargets[deviceCount], driver);

			// Add the kernel device
			kernelDeviceAdd(pciTargets[deviceCount].bus->dev,
				controllerDevices[numControllers]);

			numControllers += 1;
		}
	}

	kernelFree(pciTargets);
	return (status = 0);
}


static int driverDetect(void *parent __attribute__((unused)),
	kernelDriver *driver)
{
	// This routine is used to detect and initialize each device, as well as
	// registering each one with any higher-level interfaces.  Also does
	// general driver initialization.

	int status = 0;
	kernelDevice *controllerDevices[IDE_MAX_CONTROLLERS];
	int controllerCount = 0;
	void *oldHandler = NULL;
	int diskCount = 0;
	int diskNum = 0;
	int selectStatus = 0, resetStatus = 0, identifyStatus = 0;
	ataIdentifyData identData;
	uquad_t tmpNumSectors = 0;
	int numberHardDisks = 0;
	int numberIdeDisks = 0;
	kernelDevice *devices = NULL;
	ataDmaMode *dmaModes = kernelAtaGetDmaModes();
	ataFeature *features = kernelAtaGetFeatures();
	int deviceCount = 0;
	char value[80];
	int count;

	kernelLog("IDE: Searching for controllers");

	// Reset controller count
	numControllers = 0;

	// See whether we have PCI controller(s)
	status = detectPciControllers(controllerDevices, driver);
	if (status < 0)
		kernelDebugError("IDE PCI controller detection error");

	if (numControllers <= 0)
	{
		kernelDebug(debug_io, "IDE no controllers detected.");
		return (status = 0);
	}

	kernelDebug(debug_io, "IDE %d controllers detected", numControllers);

	for (controllerCount = 0; controllerCount < numControllers;
		controllerCount ++)
	{
		// Make sure we have I/O port addresses and interrupts set up for
		// each channel.
		for (count = 0; count < 2; count ++)
		{
			// Interrupts first
			if (!controllers[controllerCount].busMaster ||
				!controllers[controllerCount].pciInterrupt ||
				CHANNEL(controllerCount, count).compatibility)
			{
				if (!count)
					CHANNEL(controllerCount, count).interrupt =
						INTERRUPT_NUM_PRIMARYIDE;
				else
					CHANNEL(controllerCount, count).interrupt =
						INTERRUPT_NUM_SECONDARYIDE;

				kernelDebug(debug_io, "IDE controller %d channel %d using "
					"legacy interrupt=%d", controllerCount, count,
					CHANNEL(controllerCount, count).interrupt);

				sprintf(value, "%d", CHANNEL(controllerCount, count).interrupt);

				if (!count)
					kernelVariableListSet(
						&controllerDevices[controllerCount]->device.attrs,
						"controller.interrupt.primary", value);
				else
					kernelVariableListSet(
						&controllerDevices[controllerCount]->device.attrs,
						"controller.interrupt.secondary", value);
			}
			else
			{
				sprintf(value, "%d", controllers[controllerCount].pciInterrupt);
				kernelVariableListSet(
					&controllerDevices[controllerCount]->device.attrs,
					"controller.interrupt", value);
			}

			// Now ports
			if (!controllers[controllerCount].busMaster ||
				!CHANNEL(controllerCount, count).ports.data ||
				CHANNEL(controllerCount, count).compatibility)
			{
				memcpy((void *) &CHANNEL(controllerCount, count).ports,
					&defaultPorts[count], sizeof(idePorts));

				kernelDebug(debug_io, "IDE controller %d channel %d using "
					"legacy I/O ports %04x-%04x & %04x", controllerCount,
					count, CHANNEL(controllerCount, count).ports.data,
					(CHANNEL(controllerCount, count).ports.data + 7),
					CHANNEL(controllerCount, count).ports.altComStat);
			}
			else
			{
				kernelDebug(debug_io, "IDE controller %d channel %d using "
					"PCI I/O ports %04x-%04x & %04x", controllerCount,
					count, CHANNEL(controllerCount, count).ports.data,
					(CHANNEL(controllerCount, count).ports.data + 7),
					CHANNEL(controllerCount, count).ports.altComStat);
			}
		}

		// Register interrupt handlers and turn on the interrupts

		// We turn on the PCI interrupt if the controller is a bus master,
		// has a PCI interrupt assigned, and at least one of the channels is
		// not operating in compatibility mode.
		if (controllers[controllerCount].busMaster &&
			controllers[controllerCount].pciInterrupt &&
			(!CHANNEL(controllerCount, 0).compatibility ||
				!CHANNEL(controllerCount, 1).compatibility))
		{
			// Turn on the PCI interrupt.  Save any old handler for this
			// interrupt number

			if (numOldHandlers <= controllers[controllerCount].pciInterrupt)
			{
				numOldHandlers =
					(controllers[controllerCount].pciInterrupt + 1);

				oldIntHandlers = kernelRealloc(oldIntHandlers,
					(numOldHandlers * sizeof(void *)));
				if (!oldIntHandlers)
				{
					status = ERR_MEMORY;
					goto out;
				}
			}

			if (!oldIntHandlers[controllers[controllerCount].pciInterrupt] &&
				(kernelInterruptGetHandler(controllers[controllerCount]
					.pciInterrupt) != pciIdeInterrupt))
			{
				oldIntHandlers[controllers[controllerCount].pciInterrupt] =
					kernelInterruptGetHandler(controllers[controllerCount]
						.pciInterrupt);
			}

			status =
				kernelInterruptHook(controllers[controllerCount].pciInterrupt,
					&pciIdeInterrupt, NULL);
			if (status < 0)
				continue;

			kernelDebug(debug_io, "IDE turn on interrupt %d",
				controllers[controllerCount].pciInterrupt);

			// Just in case there's an outstanding interrupt
			expectInterrupt(controllerCount << 4);

			status = kernelPicMask(controllers[controllerCount].pciInterrupt, 1);
			if (status < 0)
				continue;

			// Ack any outstanding interrupt
			ackInterrupt(controllerCount << 4);
		}

		// We turn on legacy interrupts for channels if the controller is not
		// a bus master, or has no PCI interrupt assignment, or some channel
		// is operating in compatibility mode.
		if (!controllers[controllerCount].busMaster ||
			!controllers[controllerCount].pciInterrupt ||
			CHANNEL(controllerCount, 0).compatibility ||
			CHANNEL(controllerCount, 1).compatibility)
		{
			// Turn on legacy interrupt(s)
			for (count = 0; count < 2; count ++)
			{
				// Don't save any old handler for the old dedicated IDE
				// interrupts, but if there is one, we want to know about it.
				oldHandler = kernelInterruptGetHandler(
					CHANNEL(controllerCount, count).interrupt);
				if (oldHandler)
				{
					if ((!count && (oldHandler == &primaryIdeInterrupt)) ||
						(count && (oldHandler == &secondaryIdeInterrupt)))
					{
						kernelDebug(debug_io, "IDE %s interrupt %d already "
							"hooked", (count? "secondary" : "primary"),
							CHANNEL(controllerCount, count).interrupt);
					}
					else
					{
						kernelError(kernel_warn, "Not chaining unexpected "
							"existing handler for %s channel int %d",
							(count? "secondary" : "primary"),
							CHANNEL(controllerCount, count).interrupt);
					}
				}

				if ((!count && (oldHandler != &primaryIdeInterrupt)) ||
					(count && (oldHandler != &secondaryIdeInterrupt)))
				{
					if (!count)
						status = kernelInterruptHook(
							CHANNEL(controllerCount, count).interrupt,
							&primaryIdeInterrupt, NULL);
					else
						status = kernelInterruptHook(
							CHANNEL(controllerCount, count).interrupt,
							&secondaryIdeInterrupt, NULL);

					if (status < 0)
						continue;

					kernelDebug(debug_io, "IDE turn on interrupt %d",
						CHANNEL(controllerCount, count).interrupt);

					// Just in case there's an outstanding interrupt
					expectInterrupt(controllerCount << 4);

					status = kernelPicMask(
						CHANNEL(controllerCount, count).interrupt, 1);
					if (status < 0)
						continue;

					// Ack any outstanding interrupt
					ackInterrupt(controllerCount << 4);
				}
			}
		}

		kernelDebug(debug_io, "IDE detect disks on controller %d",
			controllerCount);

		// Loop through the controller's disk(s) if any.
		for (diskCount = 0; diskCount < IDE_MAX_DISKS; diskCount ++)
		{
			diskNum = ((controllerCount << 4) | diskCount);

			kernelDebug(debug_io, "IDE try to detect disk %d:%d",
				controllerCount, diskCount);

			DISK(diskNum).physical.description = "Unknown IDE disk";
			DISK(diskNum).physical.deviceNumber = diskNum;
			DISK(diskNum).physical.driver = driver;

			// Wait for a lock on the controller
			status = kernelLockGet(&DISK_CHAN(diskNum).lock);
			if (status < 0)
				continue;

			if (!(diskNum & 1))
				// Do a reset without checking the status.  Some controllers
				// need a select before a reset, some the other way around.
				// These should cause the code below to satisfy both.
				reset(diskNum);

			// Now do a select, reset, and identify.

			// Some controllers can interrupt on a select(), if there's no
			// disk.
			expectInterrupt(diskNum);

			selectStatus = select(diskNum);

			// Wait for any potential interrupt to arrive
			kernelCpuSpinMs(5);

			if (DISK_CHAN(diskNum).gotInterrupt)
			{
				kernelDebug(debug_io, "IDE selection caused interrupt, status "
					"%02x", DISK_CHAN(diskNum).intStatus);
				ackInterrupt(diskNum);
			}

			if (selectStatus < 0)
			{
				kernelDebug(debug_io, "IDE selection failed");
			}
			else
			{
				resetStatus = reset(diskNum);

				if (resetStatus >= 0)
					identifyStatus = identify(diskNum, &identData);
				else
					kernelDebug(debug_io, "IDE reset failed");
			}

			if ((selectStatus < 0) || (resetStatus < 0) ||
				(identifyStatus < 0))
			{
				kernelDebug(debug_io, "IDE can't identify disk %d:%d",
				controllerCount, diskCount);

				// If this disk number represents a master disk on a channel
				// (diskCount is even) then there is automatically no slave
				if (!(diskNum % 2))
				{
					kernelDebug(debug_io, "IDE no master -- skipping slave");
					diskCount += 1;
				}
				else
				{
					// Try to reset the master on the channel
					reset(diskNum & ~1);
				}

				goto detectNextDisk;
			}

			// Is it regular ATA?
			if (!(identData.field.generalConfig & 0x8000))
			{
				// This is an ATA hard disk device
				kernelLog("IDE: Disk %d:%d is an IDE hard disk",
					controllerCount, diskCount);

				DISK(diskNum).physical.description = "IDE/ATA hard disk";
				DISK(diskNum).physical.type =
					(DISKTYPE_PHYSICAL | DISKTYPE_FIXED | DISKTYPE_IDEDISK);
				DISK(diskNum).physical.flags = DISKFLAG_MOTORON;

				// Get the geometry

				// Get the mandatory number of sectors field from the 32-bit
				// location
				DISK(diskNum).physical.numSectors =
					identData.field.totalSectors;

				// If the 64-bit location contains something larger, use that
				// instead
				tmpNumSectors = identData.field.maxLba48;
				if (tmpNumSectors && (tmpNumSectors < 0x0000FFFFFFFFFFFFULL))
					DISK(diskNum).physical.numSectors = tmpNumSectors;

				// Try to get the number of cylinders, heads, and sectors per
				// cylinder from the 'identify device' info
				DISK(diskNum).physical.cylinders = identData.field.cylinders;
				DISK(diskNum).physical.heads = identData.field.heads;
				DISK(diskNum).physical.sectorsPerCylinder =
					identData.field.sectsPerCyl;
				// Default sector size is 512.  Don't know how to figure it out
				// short of trusting the BIOS values.
				DISK(diskNum).physical.sectorSize = 512;

				// The values above don't have to be set.  If they're not,
				// we'll conjure some.
				if (!DISK(diskNum).physical.heads ||
					!DISK(diskNum).physical.sectorsPerCylinder)
				{
					DISK(diskNum).physical.heads = 255;
					DISK(diskNum).physical.sectorsPerCylinder = 63;
				}

				// Make sure C*H*S is the same as the number of sectors, and if
				// not, adjust the cylinder number accordingly.
				if ((DISK(diskNum).physical.cylinders *
					DISK(diskNum).physical.heads *
					DISK(diskNum).physical.sectorsPerCylinder) !=
					DISK(diskNum).physical.numSectors)
				{
					kernelDebug(debug_io, "IDE disk %d:%d number of "
						"cylinders calculation is manual.  Was %u",
						controllerCount, diskCount,
						DISK(diskNum).physical.cylinders);

					DISK(diskNum).physical.cylinders =
						(DISK(diskNum).physical.numSectors /
						(DISK(diskNum).physical.heads *
						DISK(diskNum).physical.sectorsPerCylinder));

					kernelDebug(debug_io, "IDE disk %d:%d number of "
						"cylinders calculation is manual.  Now %u",
						controllerCount, diskCount,
						DISK(diskNum).physical.cylinders);
				}

				kernelDebug(debug_io, "IDE disk %d:%d cylinders=%u heads=%u "
					"sectors=%u", controllerCount, diskCount,
					DISK(diskNum).physical.cylinders,
					DISK(diskNum).physical.heads,
					DISK(diskNum).physical.sectorsPerCylinder);

				numberHardDisks += 1;
			}

			// Is it ATAPI?
			else if ((identData.field.generalConfig & 0xC000) == 0x8000)
			{
				// This is an ATAPI device (such as a CD-ROM)
				kernelLog("IDE: Disk %d:%d is an IDE CD/DVD", controllerCount,
					diskCount);

				DISK(diskNum).physical.description = "IDE/ATAPI CD/DVD";
				DISK(diskNum).physical.type = DISKTYPE_PHYSICAL;
				// Removable?
				if (identData.field.generalConfig & 0x0080)
					DISK(diskNum).physical.type |= DISKTYPE_REMOVABLE;
				else
					DISK(diskNum).physical.type |= DISKTYPE_FIXED;

				// Device type: Bits 12-8 of generalConfig should indicate 0x05
				// for CDROM, but we will just warn if it isn't for now
				DISK(diskNum).physical.type |= DISKTYPE_IDECDROM;
				if (((identData.field.generalConfig & 0x1F00) >> 8) != 0x05)
					kernelError(kernel_warn, "ATAPI device type may not be "
						"supported");

				if (identData.field.generalConfig & 0x0003)
					kernelError(kernel_warn, "ATAPI packet size not 12");

				atapiReset(diskNum);

				// Return some information we know from our device info command
				DISK(diskNum).physical.cylinders = identData.field.cylinders;
				DISK(diskNum).physical.heads = identData.field.heads;
				DISK(diskNum).physical.sectorsPerCylinder =
					identData.field.sectsPerCyl;
				DISK(diskNum).physical.numSectors = 0xFFFFFFFF;
				DISK(diskNum).physical.sectorSize = ATAPI_SECTORSIZE;
			}

			// Get the model string
			for (count = 0; count < 20; count ++)
			{
				((unsigned short *) DISK(diskNum).physical.model)[count] =
					processorSwap16(identData.field.modelNum[count]);
			}
			for (count = (DISK_MAX_MODELLENGTH - 1);
				((count >= 0) && (DISK(diskNum).physical.model[count] == ' '));
				count --)
			{
				DISK(diskNum).physical.model[count] = '\0';
			}

			kernelLog("IDE: Disk %d:%d model \"%s\"", controllerCount,
				diskCount, DISK(diskNum).physical.model);

			// Increase the overall count of IDE disks
			numberIdeDisks += 1;

		detectNextDisk:
			kernelLockRelease(&DISK_CHAN(diskNum).lock);
		}
	}

	// If there aren't any disks, exit here.
	if (!numberIdeDisks)
		return (status = 0);

	// Allocate memory for the disk device(s)
	devices = kernelMalloc(numberIdeDisks * sizeof(kernelDevice));
	if (!devices)
	{
		status = ERR_MEMORY;
		goto out;
	}

	for (controllerCount = 0; controllerCount < numControllers;
		 controllerCount ++)
	{
		for (diskCount = 0; diskCount < IDE_MAX_DISKS; diskCount ++)
		{
			diskNum = ((controllerCount << 4) | diskCount);

			// Wait for a lock on the controller
			status = kernelLockGet(&DISK_CHAN(diskNum).lock);
			if (status < 0)
				continue;

			if (DISK(diskNum).physical.numSectors)
			{
				// Select the disk
				if (select(diskNum) < 0)
				{
					// Eek?
					kernelError(kernel_warn, "Unable to select disk %d:%d",
						controllerCount, diskCount);
					goto initNextDisk;
				}

				// Get the identify data again
				if (identify(diskNum, &identData) < 0)
				{
					// Eek?
					kernelError(kernel_warn, "Unable to identify disk %d:%d",
						controllerCount, diskCount);
					goto initNextDisk;
				}

				// Log the ATA/ATAPI standard level
				if (!identData.field.majorVersion ||
					(identData.field.majorVersion == 0xFFFF))
				{
					kernelLog("IDE: Disk %d:%d no ATA/ATAPI version reported",
						controllerCount, diskCount);
				}
				else
				{
					for (count = 14; count >= 3; count --)
					{
						if ((identData.field.majorVersion >> count) & 1)
						{
							kernelLog("IDE: Disk %d:%d supports ATA/ATAPI %d",
								controllerCount, diskCount, count);
							break;
						}
					}
				}

				// Now do some general feature detection (common to hard disks
				// and CD-ROMs

				// Record the current multi-sector transfer mode, if any
				DISK(diskNum).physical.multiSectors = 1;
				if ((identData.field.multiSector & 0x01FF) > 0x101)
				{
					DISK(diskNum).featureFlags |= ATA_FEATURE_MULTI;
					DISK(diskNum).physical.multiSectors =
						(identData.field.multiSector & 0xFF);
				}
				if ((identData.field.maxMulti & 0xFF) > 1)
				{
					kernelDebug(debug_io, "IDE disk %d:%d supports %d sector "
						"multi-transfers (currently %d%s)",
						controllerCount, diskCount,
						(identData.field.maxMulti & 0xFF),
						DISK(diskNum).physical.multiSectors,
						((DISK(diskNum).featureFlags & ATA_FEATURE_MULTI)?
							"" : " - invalid"));

					// If the disk is not set to use its maximum multi-transfer
					// setting, try to set it now.
					if ((identData.field.maxMulti & 0xFF) >
						DISK(diskNum).physical.multiSectors)
					{
						if (!((identData.field.multiSector & 0x01FF) > 0x100) ||
							((identData.field.multiSector & 0xFF) <
								(identData.field.maxMulti & 0xFF)))
						{
							setMultiMode(diskNum,
								(identData.field.maxMulti & 0xFF));
						}
					}
				}

				kernelDebug(debug_io, "IDE disk %d:%d is %s multi-mode (%d)",
					controllerCount, diskCount,
					(DISKISMULTI(diskNum)? "in" : "not"),
					DISK(diskNum).physical.multiSectors);

				// See whether the disk supports various DMA transfer modes.
				//
				// word 49:	bit 8 indicates DMA supported
				// word 53:	bit 2 indicates word 88 is valid
				// word 88:	bits 0-6 indicate supported modes
				//			bits 8-14 indicate selected mode
				// word 93:	bit 13 indicates 80-pin cable for UDMA3+
				//
				if (DISK_CTRL(diskNum).busMaster &&
					(identData.field.capabilities1 & 0x0100))
				{
					for (count = 0; dmaModes[count].name; count ++)
					{
						if ((dmaModes[count].identWord == 88) &&
							!(identData.field.validFields & 0x0004))
						{
							// Values are invalid
							continue;
						}

						if (identData.word[dmaModes[count].identWord] &
							dmaModes[count].suppMask)
						{
							kernelDebug(debug_io, "IDE disk %d:%d supports %s",
								controllerCount, diskCount,
								dmaModes[count].name);

							if (!(identData.word[dmaModes[count].identWord] &
								dmaModes[count].enabledMask))
							{
								// Don't attempt to use modes UDMA3 and up if
								// there's not an 80-pin connector
								if (!(identData.field.hardResetResult & 0x2000) &&
									(dmaModes[count].identWord == 88) &&
									(dmaModes[count].suppMask > 0x04))
								{
									kernelDebug(debug_io, "IDE skip mode, no "
										"80-pin cable detected");
									continue;
								}

								// If this is not a CD-ROM, and the mode is not
								// enabled, try to enable it.
								if (!(DISK(diskNum).physical.type &
									DISKTYPE_IDECDROM))
								{
									if (setTransferMode(diskNum,
										&dmaModes[count], &identData) < 0)
									{
										continue;
									}

									// Test DMA operation
									if (testDma(diskNum) < 0)
										continue;
								}
							}
							else
							{
								kernelDebug(debug_io, "IDE disk %d:%d mode "
									"already enabled", controllerCount,
									diskCount);
							}

							DISK(diskNum).featureFlags |=
								dmaModes[count].featureFlag;
							DISK(diskNum).dmaMode = dmaModes[count].name;

							// Set the 'DMA capable' bit for this disk in the
							// channel status register
							processorInPort8(DISK_BMPORT_STATUS(diskNum),
								status);
							status |= (0x20 << (diskNum % 2));
							processorOutPort8(DISK_BMPORT_STATUS(diskNum),
								status);
							break;
						}
					}
				}

				kernelLog("IDE: Disk %d:%d in %s mode %s", controllerCount,
					diskCount, (DISKISDMA(diskNum)? "DMA" : "PIO"),
					(DISKISDMA(diskNum)? DISK(diskNum).dmaMode : ""));

				// Misc features
				for (count = 0; features[count].name; count ++)
				{
					if (identData.word[features[count].identWord] &
						features[count].suppMask)
					{
						// Supported.
						kernelDebug(debug_io, "IDE disk %d:%d supports %s",
							controllerCount, diskCount,
							features[count].name);

						// Do we have to enable it?
						if (features[count].featureCode &&
							!(identData.word[features[count].enabledWord] &
								features[count].enabledMask))
						{
							if (enableFeature(diskNum, &features[count],
								&identData) < 0)
							{
								continue;
							}
						}
						else
							kernelDebug(debug_io, "IDE disk %d:%d feature "
								"already enabled", controllerCount, diskCount);

						DISK(diskNum).featureFlags |=
							features[count].featureFlag;
					}
				}

				devices[deviceCount].device.class =
					kernelDeviceGetClass(DEVICECLASS_DISK);
				devices[deviceCount].device.subClass =
					kernelDeviceGetClass(DEVICESUBCLASS_DISK_IDE);
				devices[deviceCount].driver = driver;
				devices[deviceCount].data = (void *) &DISK(diskNum).physical;

				// Register the disk
				status = kernelDiskRegisterDevice(&devices[deviceCount]);
				if (status < 0)
					continue;

				// Add the kernel device
				status = kernelDeviceAdd(controllerDevices[controllerCount],
					&devices[deviceCount]);
				if (status < 0)
					continue;

				kernelDebug(debug_io, "IDE disk %s successfully detected",
					DISK(diskNum).physical.name);

				// Initialize the variable list for attributes of the disk.
				status = kernelVariableListCreate(&devices[deviceCount]
					.device.attrs);
				if (status >= 0)
				{
					kernelVariableListSet(&devices[deviceCount].device.attrs,
						DEVICEATTRNAME_MODEL,
						(char *) DISK(diskNum).physical.model);

					if (DISKISMULTI(diskNum))
					{
						sprintf(value, "%d",
							DISK(diskNum).physical.multiSectors);
						kernelVariableListSet(&devices[deviceCount]
							.device.attrs, "disk.multisectors", value);
					}

					value[0] = '\0';
					if (DISKISDMA(diskNum))
						strcat(value, DISK(diskNum).dmaMode);
					else
						strcat(value, "PIO");

					if (DISKISSMART(diskNum))
						strcat(value, ",SMART");

					if (DISKISRCACHE(diskNum))
						strcat(value, ",rcache");

					if (DISKISMEDSTAT(diskNum))
						strcat(value, ",medstat");

					if (DISKISWCACHE(diskNum))
						strcat(value, ",wcache");

					if (DISKIS48(diskNum))
						strcat(value, ",48-bit");

					kernelVariableListSet(&devices[deviceCount].device.attrs,
						"disk.features", value);
				}

				deviceCount += 1;

			initNextDisk:
				kernelLockRelease(&DISK_CHAN(diskNum).lock);
			}
		}
	}

	status = 0;

out:
	kernelDebug(debug_io, "IDE detection complete");
	return (status);
}


static kernelDiskOps ideOps = {
	NULL,	// driverSetMotorState
	driverSetLockState,
	driverSetDoorState,
	driverMediaPresent,
	NULL,	// driverMediaChanged
	driverReadSectors,
	driverWriteSectors,
	driverFlush
};


/////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////
//
// Below here, the functions are exported for external use
//
/////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////

void kernelIdeDriverRegister(kernelDriver *driver)
{
	// Device driver registration.

	driver->driverDetect = driverDetect;
	driver->ops = &ideOps;

	return;
}

