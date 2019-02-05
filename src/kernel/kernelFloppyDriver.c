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
//  kernelFloppyDriver.c
//

// Driver for standard 3.5" floppy disks

#include "kernelDisk.h"
#include "kernelCpu.h"
#include "kernelDma.h"
#include "kernelError.h"
#include "kernelInterrupt.h"
#include "kernelLock.h"
#include "kernelMain.h"
#include "kernelMalloc.h"
#include "kernelMemory.h"
#include "kernelMultitasker.h"
#include "kernelPage.h"
#include "kernelParameters.h"
#include "kernelPic.h"
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <sys/processor.h>

// Error codes and messages
#define FLOPPY_ABNORMAL				0
#define FLOPPY_INVALIDCOMMAND		1
#define FLOPPY_EQUIPMENTCHECK		2
#define FLOPPY_ENDOFTRACK			3
#define FLOPPY_CRCERROR1			4
#define FLOPPY_DMAERROR				5
#define FLOPPY_INVALIDSECTOR		6
#define FLOPPY_WRITEPROTECT			7
#define FLOPPY_MISSINGADDRESSMARK	8
#define FLOPPY_CONTROLMARK			9
#define FLOPPY_CRCERROR2			10
#define FLOPPY_INVALIDTRACK			11
#define FLOPPY_BADTRACK				12
#define FLOPPY_BADADDRESSMARK		13
#define FLOPPY_TIMEOUT				14
#define FLOPPY_UNKNOWN				15

typedef volatile struct {
	int dmaChannel;			// DMA channel
	unsigned headLoad;		// Head load timer
	unsigned headUnload;	// Head unload timer
	unsigned stepRate;		// Step rate timer
	unsigned dataRate;		// Data rate
	unsigned gapLength;		// Gap length between sectors

} floppyDriveData;

static char *errorMessages[] = {
	"Abnormal termination - command did not complete",
	"Invalid command",
	"Equipment check - seek to invalid track",
	"The requested sector is past the end of the track",
	"ID byte or data - the CRC integrity check failed",
	"DMA transfer overrun or underrun",
	"No data - the requested sector was not found",
	"Write protect",
	"Missing address mark",
	"Sector control mark - data was not the expected type",
	"Data - the CRC integrity check failed",
	"Invalid or unexpected track",
	"Bad track",
	"Bad address mark",
	"Command timed out",
	"Unknown error"
};

static kernelPhysicalDisk disks[MAXFLOPPIES];
static int numberFloppies = 0;
static lock controllerLock;
static unsigned currentTrack = 0;
static int readStatusOnInterrupt = 0;
static int interruptReceived = 0;
static unsigned char statusRegister0;
static unsigned char statusRegister1;
static unsigned char statusRegister2;
static unsigned char statusRegister3;

// An area for doing floppy disk DMA transfers (physically aligned, etc)
static kernelIoMemory xferArea;


static void commandWrite(unsigned char cmd)
{
	// Waits until the floppy controller is ready for a new command (or part
	// thereof) in port 03F5h, and then writes it.

	uquad_t timeout = (kernelCpuGetMs() + 500);
	unsigned char data;

	while (kernelCpuGetMs() < timeout)
	{
		// Get the drive transfer mode from the port
		processorDelay();
		processorInPort8(0x03F4, data);

		// Check whether access is permitted
		if ((data & 0xC0) == 0x80)
			break;
	}

	processorOutPort8(0x03F5, cmd);
	processorDelay();

	return;
}


static unsigned char statusRead(void)
{
	// Waits until the floppy controller is ready for a read of port 03F5h,
	// and then reads it.

	uquad_t timeout = (kernelCpuGetMs() + 500);
	unsigned char data;

	while (kernelCpuGetMs() < timeout)
	{
		// Get the drive transfer mode from the port
		processorDelay();
		processorInPort8(0x03F4, data);

		// Check whether access is permitted
		if ((data >> 6) == 3)
			break;
	}

	processorInPort8(0x03F5, data);
	return (data);
}


static int waitOperationComplete(void)
{
	// This function just loops, reading the "interrupt received" byte.  When
	// the byte becomes 1, it resets the byte and returns.  If the wait times
	// out, the function returns negative.  Otherwise, it returns 0.

	int status = 0;
	uquad_t timeout = (kernelCpuGetMs() + MS_PER_SEC);

	while (!interruptReceived)
	{
		// Yield the rest of this timeslice.
		// kernelMultitaskerYield();

		if (kernelCpuGetMs() > timeout)
			break;
	}

	if (interruptReceived)
	{
		interruptReceived = 0;
		return (status = 0);
	}
	else
	{
		// No interrupt -- timed out.
		kernelError(kernel_error, "%s", errorMessages[FLOPPY_TIMEOUT]);
		return (status = ERR_IO);
	}
}


static int evaluateError(void)
{
	// This is an internal-only function that takes no parameters and returns
	// no value.  It evaluates the returned bytes in the statusRegister[X]
	// bytes and matches conditions to error codes and error messages

	int errorCode = 0;

	// Check for abnormal termination of command
	if ((statusRegister0 & 0xC0) == 0x40)
		errorCode = FLOPPY_ABNORMAL;

	// Check for invalid command
	else if ((statusRegister0 & 0xC0) == 0x80)
		errorCode = FLOPPY_INVALIDCOMMAND;

	// Check for Equipment check error
	if (statusRegister0 & 0x10)  // bit 4 (status register 0)
		errorCode = FLOPPY_EQUIPMENTCHECK;

	// Check for end-of-track
	if (statusRegister1 & 0x80)  // bit 7 (status register 1)
		errorCode = FLOPPY_ENDOFTRACK;

	// Bit 6 is unused in status register 1

	// Check for the first kind of data error
	if (statusRegister1 & 0x20)  // bit 5 (status register 1)
		errorCode = FLOPPY_CRCERROR1;

	// Check for overrun/underrun
	if (statusRegister1 & 0x10)  // bit 4 (status register 1)
		errorCode = FLOPPY_DMAERROR;

	// Bit 3 is unused in status register 1

	// Check for no data error
	if (statusRegister1 & 0x04)  // bit 2 (status register 1)
		errorCode = FLOPPY_INVALIDSECTOR;

	// Check for write protect error
	if (statusRegister1 & 0x02)  // bit 1 (status register 1)
		errorCode = FLOPPY_WRITEPROTECT;

	// Check for missing address mark
	if (statusRegister1 & 0x01)  // bit 0 (status register 1)
		errorCode = FLOPPY_MISSINGADDRESSMARK;

	// Bit 7 is unused in status register 2

	// Check for control mark error
	if (statusRegister2 & 0x40)  // bit 6 (status register 2)
		errorCode = FLOPPY_CONTROLMARK;

	// Check for the second kind of data error
	if (statusRegister2 & 0x20)  // bit 5 (status register 2)
		errorCode = FLOPPY_CRCERROR2;

	// Check for invalid track / wrong cylinder
	if (statusRegister2 & 0x10)  // bit 4 (status register 2)
		errorCode = FLOPPY_INVALIDTRACK;

	// Bit 3 is unused in status register 2
	// Bit 2 is unused in status register 2

	// Check for bad track
	if (statusRegister2 & 0x02)  // bit 1 (status register 2)
		errorCode = FLOPPY_BADTRACK;

	// Check for bad address mark
	if (statusRegister2 & 0x01)  // bit 0 (status register 2)
		errorCode = FLOPPY_BADADDRESSMARK;

	if (!errorCode)
		errorCode = FLOPPY_UNKNOWN;

	return (errorCode);
}


static void selectDrive(unsigned driveNum)
{
	// Takes the floppy number to select as its only parameter.  Selects the
	// drive on the controller

	unsigned char data = 0;

	// Select the drive on the controller

	// Get the current register value
	processorDelay();
	processorInPort8(0x03F2, data);

	// Make sure the DMA/Interrupt and reset-off bits are set
	data |= 0x0C;

	// Clear out the selection bits
	data &= 0xFC;

	// Set the selection bits
	data |= (unsigned char) driveNum;

	// Issue the command
	processorOutPort8(0x03F2, data);
	processorDelay();

	return;
}


static void specify(unsigned driveNum)
{
	// Sends some essential timing information to the floppy drive controller
	// about the specified drive.

	unsigned char command = 0;
	floppyDriveData *floppyData =
		(floppyDriveData *) disks[driveNum].driverData;

	// Construct the data rate byte
	command = floppyData->dataRate;
	processorOutPort8(0x03F7, command);
	processorDelay();

	// Construct the command byte
	command = 0x03;  // Specify command
	commandWrite(command);

	// Construct the step rate/head unload byte
	command = ((floppyData->stepRate << 4) | (floppyData->headUnload & 0x0F));
	commandWrite(command);

	// Construct the head load time byte.  Make sure that DMA mode is enabled.
	command = ((floppyData->headLoad << 1) & 0xFE);
	commandWrite(command);

	// There is no status information or interrupt after this command
	return;
}


static unsigned char driveStatus(int driveNum)
{
	// Read the "sense drive status" byte

	unsigned char command = 0;

	// Construct the command byte
	command = 0x04;  // Sense drive status command
	commandWrite(command);

	// Construct the drive/head select byte
	// Format [00000 (head 1 bit)(drive 2 bits)]
	command = (unsigned char)(driveNum & 3);
	commandWrite(command);

	return (statusRead());
}


static int setMotorState(int driveNum, int onOff)
{
	// Turns the floppy motor on or off

	unsigned char data = 0;
	unsigned char tmp;

	// Select the drive
	selectDrive(driveNum);

	// Read the port's current state
	processorDelay();
	processorInPort8(0x03F2, data);

	// Move the motor select bit to the correct location [7:4]
	tmp = ((unsigned char) 0x10 << driveNum);

	// Test whether the motor is on already
	if (onOff)
	{
		if (!(data & tmp))
		{
			// Turn on the 'motor on' bit
			data |= tmp;

			// Issue the command
			processorOutPort8(0x03F2, data);
			processorDelay();
		}
	}
	else
	{
		// Move the motor select bit to the correct location [7:4]
		tmp = 0x0F | ((unsigned char) 0xEF << driveNum);

		// Turn off the 'motor on' bit
		data &= tmp;

		// Issue the command
		processorOutPort8(0x03F2, data);
		processorDelay();
	}

	if (onOff)
		disks[driveNum].flags |= DISKFLAG_MOTORON;
	else
		disks[driveNum].flags &= ~DISKFLAG_MOTORON;

	return (0);
}


static int seek(unsigned driveNum, unsigned head, unsigned track, int wait)
{
	int status = 0;
	unsigned char command = 0;

	if (wait)
	{
		// Tell the interrupt-received function to issue the "sense interrupt
		// status" command after the operation
		readStatusOnInterrupt = 1;
		interruptReceived = 0;
	}

	// Construct the command byte
	command = 0x0F;  // Seek command
	commandWrite(command);

	// Construct the drive/head select byte
	// Format [00000 (head 1 bit)(drive 2 bits)]
	command = (unsigned char)(((head & 1) << 2) | (driveNum & 3));
	commandWrite(command);

	// Construct the track number byte
	command = (unsigned char) track;
	commandWrite(command);

	if (!wait)
		return (status = 0);

	status = waitOperationComplete();
	if ((status < 0) || ((statusRegister0 & 0xF8) != 0x20) ||
		(currentTrack != track))
	{
		return (status = ERR_IO);
	}

	return (status = 0);
}


static int readWriteSectors(unsigned driveNum, uquad_t logicalSector,
	uquad_t numSectors, void *buffer, int read)
{
	// Reads or writes data to/from the disk.  Both types of operation are
	// combined here since the functionality is nearly identical.  Returns 0
	// on success, negative otherwise.

	int status = 0;
	kernelPhysicalDisk *theDisk = NULL;
	floppyDriveData *floppyData = NULL;
	int errorCode = 0;
	unsigned head, track, sector;
	unsigned doSectors = 0;
	unsigned xferBytes = 0;
	unsigned char command = 0;
	int retry = 0;
	int count;

	// Get a pointer to the requested disk
	theDisk = &disks[driveNum];
	floppyData = theDisk->driverData;

	// Wait for a lock on the controller
	status = kernelLockGet(&controllerLock);
	if (status < 0)
		return (status);

	// Select the drive
	selectDrive(driveNum);

	// Check whether the disk is write-protected
	if (driveStatus(driveNum) & 0x40)
		theDisk->flags |= DISKFLAG_READONLY;

	// We will have to make sure the motor is turned on
	if (!(theDisk->flags & DISKFLAG_MOTORON))
	{
		// Turn the drive motor on
		setMotorState(driveNum, 1);

		// We don't have to wait for the disk to spin up on a read operation;
		// It will start reading when it's good and ready.  If it's a write
		// operation we have to wait for it.
		if (!read)
			// Wait half a second for the drive to spin up
			kernelMultitaskerWait(500);
	}

	// We don't want to cross a track boundary in one operation.  Some floppy
	// controllers can't do this.  Thus, if necessary we break up the operation
	// with this loop.

	while (numSectors > 0)
	{
		// Calculate the physical head, track and sector to use
		head = ((logicalSector % (theDisk->sectorsPerCylinder *
			theDisk->heads)) / theDisk->sectorsPerCylinder);
		track = (logicalSector / (theDisk->sectorsPerCylinder *
			theDisk->heads));
		sector = (logicalSector % theDisk->sectorsPerCylinder) + 1;

		// Make sure the head, track, and sector are within the legal range
		// of values
		if ((sector > theDisk->sectorsPerCylinder) ||
			(track >= theDisk->cylinders) || (head >= theDisk->heads))
		{
			kernelLockRelease(&controllerLock);
			return (status = ERR_BADADDRESS);
		}

		// Here's where we check for crossing track boundaries
		doSectors = numSectors;
		if (((head * theDisk->sectorsPerCylinder) + sector + (doSectors - 1)) >
			(theDisk->heads * theDisk->sectorsPerCylinder))
		doSectors = ((theDisk->heads * theDisk->sectorsPerCylinder) -
			((head * theDisk->sectorsPerCylinder) + (sector - 1)));

		// We need to do a seek for every read/write operation

		// Tell the interrupt-received function to issue the "sense interrupt
		// status" command after the operation
		readStatusOnInterrupt = 1;
		interruptReceived = 0;

		// Seek to the correct head and track
		seek(driveNum, head, track, 0 /* no wait */);

		// The drive should now be seeking.  While we wait for the seek to
		// complete, we can do some other things.

		// How many bytes will we transfer?
		xferBytes = (doSectors * theDisk->sectorSize);

		// If it's a write operation, copy xferBytes worth of user data
		// into the transfer area
		if (!read)
			memcpy(xferArea.virtual, buffer, xferBytes);

		// Set up the DMA controller for the transfer.
		if (read)
		{
			// Set the DMA channel for writing TO memory, demand mode
			status = kernelDmaOpenChannel(floppyData->dmaChannel,
				(void *) xferArea.physical, xferBytes, DMA_WRITEMODE);
		}
		else
		{
			// Set the DMA channel for reading FROM memory, demand mode
			status = kernelDmaOpenChannel(floppyData->dmaChannel,
				(void *) xferArea.physical, xferBytes, DMA_READMODE);
		}

		if (status < 0)
		{
			kernelError(kernel_error, "Unable to open DMA channel");
			kernelLockRelease(&controllerLock);
			return (status);
		}

		// Now wait for the seek to complete, check error conditions in the
		// status byte, and make sure that we are now at the correct track
		status = waitOperationComplete();
		if ((status < 0) || ((statusRegister0 & 0xF8) != 0x20) ||
			(currentTrack != track))
		{
			kernelDmaCloseChannel(floppyData->dmaChannel);
			kernelLockRelease(&controllerLock);
			kernelError(kernel_error, "Seek error: %s",
				errorMessages[evaluateError()]);
			return (status = ERR_IO);
		}

		// Now proceed with the read/write operation

		// Tell the interrupt-received function NOT to issue the "sense
		// interrupt status" command after the read/write operation
		readStatusOnInterrupt = 0;
		interruptReceived = 0;

		// Command byte
		// Drive/head select byte
		// Track number byte
		// Head number byte
		// Sector byte
		// Sector size code
		// End of track byte
		// Gap length byte
		// Custom sector size byte

		if (read)
			command = 0xE6;  // "Read normal data" command
		else
			command = 0xC5;  // "Write data" command

		commandWrite(command);

		// Construct the drive/head select byte
		// Format [00000 (head 1 bit)(drive 2 bits)]
		command = (unsigned char)(((head & 1) << 2) | (driveNum & 3));
		commandWrite(command);

		// Construct the track number byte
		command = (unsigned char) track;
		commandWrite(command);

		// Construct the head number byte
		command = (unsigned char) head;
		commandWrite(command);

		// Construct the sector byte
		command = (unsigned char) sector;
		commandWrite(command);

		// Construct the sector size code
		command = (unsigned char)(theDisk->sectorSize >> 8);
		commandWrite(command);

		// Construct the end of track byte
		command = (unsigned char) theDisk->sectorsPerCylinder;
		commandWrite(command);

		// Construct the gap length byte
		command = (unsigned char)
			((floppyDriveData *)(theDisk->driverData))->gapLength;
		commandWrite(command);

		// Construct the custom sector size byte
		command = (unsigned char) 0xFF;  // Always FFh
		commandWrite(command);

		status = waitOperationComplete();

		// Close the DMA channel
		kernelDmaCloseChannel(floppyData->dmaChannel);
		if (status < 0)
		{
			// The command timed out.  Save the error and return error.
			kernelLockRelease(&controllerLock);
			return (status);
		}

		// We have to read the seven status bytes from the controller.  Save
		// them in the designated memory locations

		statusRegister0 = statusRead();
		statusRegister1 = statusRead();
		statusRegister2 = statusRead();
		statusRegister3 = statusRead();

		// We don't care about status registers 4-6.
		for (count = 0; count < 3; count ++)
			statusRead();

		// Save the current track
		currentTrack = (unsigned ) statusRegister3;

		// Now we can examine the status.  If the top two bits of register 0
		// are clear, then the operation completed normally.
		if (statusRegister0 & 0xC0)
		{
			// We have an error.  Retry up to twice.
			if (retry < 2)
			{
				retry += 1;
				continue;
			}

			// We have an error.  We have to try to determine the cause and set
			// the error message.  We'll call a function which does all of this
			// for us.
			errorCode = evaluateError();
			break;
		}
		else
		{
			// If this was a read operation, copy xferBytes worth of data from
			// the transfer area to the user buffer
			if (read)
				memcpy(buffer, xferArea.virtual, xferBytes);
		}

		logicalSector += doSectors;
		numSectors -= doSectors;
		buffer += (doSectors * theDisk->sectorSize);
		retry = 0;

	} // Per-operation loop

	// Unlock the controller
	kernelLockRelease(&controllerLock);

	if (errorCode == FLOPPY_WRITEPROTECT)
	{
		return (status = ERR_NOWRITE);
	}
	else if (errorCode)
	{
		kernelError(kernel_error, "Read/write error: %s",
			errorMessages[errorCode]);
		return (status = ERR_IO);
	}
	else
	{
		return (status = 0);
	}
}


static void floppyInterrupt(void)
{
	// This is the floppy interrupt handler.  It will simply change a data
	// value to indicate that one has been received, and acknowldege the
	// interrupt to the PIC.  It's up to the other functions to do something
	// useful with the information.

	void *address = NULL;

	processorIsrEnter(address);
	kernelInterruptSetCurrent(INTERRUPT_NUM_FLOPPY);

	// Check whether to do the "sense interrupt status" command.
	if (readStatusOnInterrupt)
	{
		// Tell the diskette drive that the interrupt was serviced.  This
		// helps the drive stop doing the operation, and returns some status
		// operation, which we will save.

		commandWrite((unsigned char) 0x08);

		statusRegister0 = statusRead();
		currentTrack = (unsigned) statusRead();

		readStatusOnInterrupt = 0;
	}

	// Note that we got the interrupt.
	interruptReceived = 1;

	kernelPicEndOfInterrupt(INTERRUPT_NUM_FLOPPY);
	kernelInterruptClearCurrent();
	processorIsrExit(address);
}


static int driverSetMotorState(int driveNum, int onOff)
{
	// Turns the floppy motor on or off

	int status = 0;

	// Wait for a lock on the controller
	status = kernelLockGet(&controllerLock);
	if (status < 0)
		return (status);

	status = setMotorState(driveNum, onOff);

	// Unlock the controller
	kernelLockRelease(&controllerLock);

	return (status);
}


static int driverMediaChanged(int driveNum)
{
	// This function determines whether the media in the floppy drive has
	// changed.  Returns 1 if the disk is missing or has been changed, 0 if it
	// has not been changed, and negative on error.

	int status = 0;
	unsigned char data = 0;
	int changed = 0;
	static int broken = 0;

	if (driveNum >= MAXFLOPPIES)
		return (status = ERR_BOUNDS);

	// The changed bit is only valid if the motor is on
	if (!(disks[driveNum].flags & DISKFLAG_MOTORON))
		return (status = 0);

	// If we're running on a broken emulator that always says the media
	// changed, disable the check
	if (broken)
		return (status = 0);

	// Wait for a lock on the controller
	status = kernelLockGet(&controllerLock);
	if (status < 0)
		return (status);

	// Select the drive
	selectDrive(driveNum);

	// Now simply read port 03F7h.  Bit 7 is the only part that matters.
	processorDelay();
	processorInPort8(0x03F7, data);

	if (data & 0x80)
		changed = 1;

	if (changed)
	{
		// To reset the changed bit, we need to seek to a different head/track
		// than we were previously on
		seek(driveNum, 0 /* head */, (currentTrack? 0 : 1), 1 /* wait */);

		// Check whether it was cleared
		processorInPort8(0x03F7, data);

		if (data & 0x80)
			// Can't clear it
			broken = 1;
	}

	// Unlock the controller
	kernelLockRelease(&controllerLock);

	return (status = changed);
}


static int driverReadSectors(int driveNum, uquad_t logicalSector,
	uquad_t numSectors, void *buffer)
{
	if (driveNum >= MAXFLOPPIES)
		return (ERR_BOUNDS);

	// This function is a wrapper for the readWriteSectors function.
	return (readWriteSectors(driveNum, logicalSector, numSectors, buffer,
		1 /* read */));
}


static int driverWriteSectors(int driveNum, uquad_t logicalSector,
	uquad_t numSectors, const void *buffer)
{
	if (driveNum >= MAXFLOPPIES)
		return (ERR_BOUNDS);

	// This function is a wrapper for the readWriteSectors function.
	return (readWriteSectors(driveNum, logicalSector, numSectors,
		(void *) buffer, 0 /* write */));
}


static int driverDetect(void *parent, kernelDriver *driver)
{
	// This function is used to detect and initialize each device, as well as
	// registering each one with any higher-level interfaces.  Also does
	// general driver initialization.

	int status = 0;
	floppyDriveData *floppyData = NULL;
	kernelDevice *dev = NULL;
	int count;

	numberFloppies = 0;
	memset((void *) &controllerLock, 0, sizeof(lock));
	memset(&xferArea, 0, sizeof(kernelIoMemory));

	// Loop for each device reported by the BIOS
	for (count = 0; count < kernelOsLoaderInfo->floppyDisks; count ++)
	{
		memset((void *) &disks[numberFloppies], 0, sizeof(kernelPhysicalDisk));

		// The head, track and sector values we got from the loader
		disks[numberFloppies].heads = kernelOsLoaderInfo->fddInfo[count].heads;
		disks[numberFloppies].cylinders =
			kernelOsLoaderInfo->fddInfo[count].tracks;
		disks[numberFloppies].sectorsPerCylinder =
			kernelOsLoaderInfo->fddInfo[count].sectors;
		disks[numberFloppies].numSectors = (disks[numberFloppies].heads *
			disks[numberFloppies].cylinders *
			disks[numberFloppies].sectorsPerCylinder);

		// Some additional universal default values
		disks[numberFloppies].type = (DISKTYPE_PHYSICAL | DISKTYPE_REMOVABLE |
			DISKTYPE_FLOPPY);
		disks[numberFloppies].deviceNumber = count;
		disks[numberFloppies].sectorSize = 512;
		// Assume motor off for now

		// We do division operations with these values
		if (!disks[numberFloppies].sectorsPerCylinder ||
			!disks[numberFloppies].heads)
		{
			// We do division operations with these values
			kernelError(kernel_error, "NULL sectors or heads value");
			return (status = ERR_INVALID);
		}

		// Get memory for our private data
		floppyData = kernelMalloc(sizeof(floppyDriveData));
		if (!floppyData)
		{
			kernelError(kernel_error, "Can't get memory for floppy drive "
				"data");
			return (status = ERR_MEMORY);
		}

		// Generic, regardless of type
		floppyData->dmaChannel = 2;
		floppyData->headLoad = 0x02;
		floppyData->headUnload = 0x0F;
		floppyData->dataRate = 0;

		switch(kernelOsLoaderInfo->fddInfo[count].type)
		{
			case 1:
				// This is a 360 KB 5.25" Disk.  Yuck.
				disks[numberFloppies].description = "360 Kb 5.25\" floppy";
				floppyData->stepRate = 0x0D;
				floppyData->gapLength = 0x2A;
				break;

			case 2:
				// This is a 1.2 MB 5.25" Disk.  Yuck.
				disks[numberFloppies].description = "1.2 MB 5.25\" floppy";
				floppyData->stepRate = 0x0D;
				floppyData->gapLength = 0x2A;
				break;

			case 3:
				// This is a 720 KB 3.5" Disk.  Yuck.
				disks[numberFloppies].description = "720 Kb 3.5\" floppy";
				floppyData->stepRate = 0x0D;
				floppyData->gapLength = 0x1B;
				break;

			case 5:
			case 6:
				// This is a 2.88 MB 3.5" Disk.
				disks[numberFloppies].description = "2.88 MB 3.5\" floppy";
				floppyData->stepRate = 0x0A;
				floppyData->gapLength = 0x1B;
				break;

			case 16:
				// This is a removable ATAPI device (possibly USB).  Not an
				// old-fashioned floppy that we can use with this driver.
				kernelError(kernel_warn, "Floppy disk fd%d is not a standard "
					"floppy disk (ATAPI)", disks[numberFloppies].deviceNumber);
				kernelFree((void *) floppyData);
				continue;

			default:
				// Oh oh.  This is an unexpected value.  Make a warning and
				// fall through to 1.44 MB.
				kernelError(kernel_warn, "Floppy disk fd%d type %d is "
					"unknown.  Assuming 1.44 MB.",
					disks[numberFloppies].deviceNumber,
					kernelOsLoaderInfo->fddInfo[count].type);

			case 4:
				// This is a 1.44 MB 3.5" Disk.
				disks[numberFloppies].description = "1.44 MB 3.5\" floppy";
				floppyData->stepRate = 0x0A;
				floppyData->gapLength = 0x1B;
				break;
		}

		// Attach the drive data to the disk
		disks[numberFloppies].driverData = (void *) floppyData;

		disks[numberFloppies].driver = driver;

		numberFloppies += 1;
	}

	// Get memory for a disk transfer area.

	status = kernelMemoryGetIo(DISK_CACHE_ALIGN, DISK_CACHE_ALIGN,
		1 /* low memory */, "floppy cache", &xferArea);
	if (status < 0)
		goto out;

	// Clear the "interrupt received" byte
	interruptReceived = 0;
	readStatusOnInterrupt = 0;

	// Don't save any old handler for the dedicated floppy interrupt, but if
	// there is one, we want to know about it.
	if (kernelInterruptGetHandler(INTERRUPT_NUM_FLOPPY))
		kernelError(kernel_warn, "Not chaining unexpected existing handler "
			"for floppy int %d", INTERRUPT_NUM_FLOPPY);

	// Register our interrupt handler
	status = kernelInterruptHook(INTERRUPT_NUM_FLOPPY, &floppyInterrupt, NULL);
	if (status < 0)
		goto out;

	// Turn on the interrupt
	status = kernelPicMask(INTERRUPT_NUM_FLOPPY, 1);
	if (status < 0)
		goto out;

	// Loop again, for each device, to finalize the setup
	for (count = 0; count < numberFloppies; count ++)
	{
		// Select the drive on the controller
		selectDrive(disks[count].deviceNumber);

		// Send the controller info about the drive.
		specify(disks[count].deviceNumber);

		// Get a device
		dev = kernelMalloc(sizeof(kernelDevice));
		if (!dev)
			// Skip this one, we guess
			continue;

		dev->device.class = kernelDeviceGetClass(DEVICECLASS_DISK);
		dev->device.subClass =
			kernelDeviceGetClass(DEVICESUBCLASS_DISK_FLOPPY);
		dev->driver = driver;
		dev->data = (void *) &disks[count];

		// Register the floppy disk device
		status = kernelDiskRegisterDevice(dev);
		if (status < 0)
			kernelError(kernel_error, "Couldn't register the floppy disk");

		// Add the kernel device
		status = kernelDeviceAdd(parent, dev);
		if (status < 0)
		{
			kernelError(kernel_error, "Couldn't add the floppy device");
			kernelFree(dev);
		}
	}

	status = 0;

out:
	if (status < 0)
	{
		if (xferArea.virtual)
			kernelMemoryReleaseIo(&xferArea);

		if (floppyData)
			kernelFree((void *) floppyData);
	}

	return (status);
}


static kernelDiskOps floppyOps = {
	driverSetMotorState,
	NULL,	// driverSetLockState
	NULL,	// driverSetDoorState
	NULL,	// driverMediaPresent
	driverMediaChanged,
	driverReadSectors,
	driverWriteSectors,
	NULL	// driverFlush
};


/////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////
//
// Below here, the functions are exported for external use
//
/////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////

void kernelFloppyDriverRegister(kernelDriver *driver)
{
	// Device driver registration.

	driver->driverDetect = driverDetect;
	driver->ops = &floppyOps;

	return;
}

