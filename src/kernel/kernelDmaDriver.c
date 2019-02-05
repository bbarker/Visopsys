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
//  kernelDmaDriver.c
//

// Driver for standard PC system DMA chip

#include "kernelDriver.h" // Contains my prototypes
#include "kernelDma.h"
#include "kernelMalloc.h"
#include "kernelError.h"
#include <string.h>
#include <sys/processor.h>

static struct {
	int statusReg;
	int commandReg;
	int requestReg;
	int maskReg;
	int modeReg;
	int clearReg;
	int tempReg;
	int disableReg;
	int clearMaskReg;
	int writeMaskReg;

} controllerPorts[] = {
	{ 0x08, 0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0D, 0x0E, 0x0F },
	{ 0xD0, 0xD0, 0xD2, 0xD4, 0xD6, 0xD8, 0xDA, 0xDA, 0xDC, 0xDE }
};

static struct {
	int baseCurrentAddrReg;
	int baseCurrentCountReg;
	int pageReg;

} channelPorts[] = {
	{ 0x00, 0x01, 0x87 },
	{ 0x02, 0x03, 0x83 },
	{ 0x04, 0x05, 0x81 },
	{ 0x06, 0x07, 0x82 },
	{ 0xC0, 0xC2, 0x8F },
	{ 0xC4, 0xC6, 0x8B },
	{ 0xC8, 0xCA, 0x89 },
	{ 0xCC, 0xCE, 0x8A }
};


static inline void enableController(int controller)
{
	// Enables the selected DMA controller.  Disabling is recommended before
	// setting other registers.

	// Bit 2 is cleared
	processorOutPort8(controllerPorts[controller].commandReg, 0x00);
	processorDelay();
	return;
}


static inline void disableController(int controller)
{
	// Disables the selected DMA controller, which is recommended before
	// setting other registers.

	// Bit 2 is set
	processorOutPort8(controllerPorts[controller].commandReg, 0x04);
	processorDelay();
	return;
}


static void writeWordPort(int port, int value)
{
	// Does sequential 2-write port outputs for a couple of the registers

	unsigned char data;

	// Set the controller register.  Start with the low byte.
	data = (unsigned char)(value & 0xFF);
	processorOutPort8(port, data);
	processorDelay();

	// Now the high byte
	data = (unsigned char)((value >> 8) & 0xFF);
	processorOutPort8(port, data);
	processorDelay();
}


static int driverOpenChannel(int channel, void *address, int count, int mode)
{
	// This function prepares the registers of the specified DMA channel for
	// a data transfer.   This function calls a series of other functions that
	// set individual registers.

	int status = 0;
	int controller = 0;
	int interrupts = 0;
	unsigned segment = 0;
	unsigned offset = 0 ;
	unsigned char data;

	if (channel > 7)
		return (status = ERR_NOSUCHENTRY);

	if (channel >= 4)
		controller = 1;

	// Convert the "address" argument we were passed into a base address
	// and page register
	segment = ((unsigned) address >> 16);
	offset = ((unsigned) address - (segment << 16));

	// Disable the controller while setting registers
	disableController(controller);

	// Clear interrupts while setting DMA controller registers
	processorSuspendInts(interrupts);

	// 1. Disable the channel.  Mask out all but the bottom two bits of the
	// channel number, then turn on the disable 'mask' bit
	data = (unsigned char)((channel & 0x03) | 0x04);
	processorOutPort8(controllerPorts[controller].maskReg, data);
	processorDelay();

	// 2. Set the channel and mode.  "or" the channel with the mode
	data = (unsigned char)((mode | channel) & 0xFF);
	processorOutPort8(controllerPorts[controller].modeReg, data);
	processorDelay();

	// 3. Do channel setup.

	// Reset the byte flip-flop before the following actions, as they each
	// require two consecutive port writes.  Value is unimportant.
	processorOutPort8(controllerPorts[controller].clearReg, 0x01);
	processorDelay();

	// Set the base and current address register
	writeWordPort(channelPorts[channel].baseCurrentAddrReg, offset);

	// Set the base and current count register, but subtract 1 first
	count--;
	writeWordPort(channelPorts[channel].baseCurrentCountReg, count);

	// Set the page register
	data = (unsigned char)(segment & 0xFF);
	processorOutPort8(channelPorts[channel].pageReg, data);
	processorDelay();

	// 4. Enable the channel.  Mask out all but the bottom two bits of the
	// channel number.
	data = (unsigned char)(channel & 0x03);
	processorOutPort8(controllerPorts[controller].maskReg, data);
	processorDelay();

	processorRestoreInts(interrupts);

	// Re-enable the appropriate controller
	enableController(controller);

	return (status = 0);
}


static int driverCloseChannel(int channel)
{
	// This function disables the selected DMA channel by setting the
	// appropriate mask bit.

	int status = 0;
	int controller = 0;
	int interrupts = 0;
	unsigned char data;

	if (channel >= 4)
		controller = 1;

	// Disable the controller while setting registers
	disableController(controller);

	// Clear interrupts while setting DMA controller registers
	processorSuspendInts(interrupts);

	// Mask out all but the bottom two bits of the channel number, as above,
	// then turn on the 'mask' bit
	data = (unsigned char)((channel & 0x03) | 0x04);
	processorOutPort8(controllerPorts[controller].maskReg, data);
	processorDelay();

	processorRestoreInts(interrupts);

	// Re-enable the appropriate controller
	enableController(controller);

	return (status = 0);
}


static int driverDetect(void *parent, kernelDriver *driver)
{
	// Normally, this function is used to detect and initialize each device,
	// as well as registering each one with any higher-level interfaces.
	// Since we can assume that there's a DMA controller, just initialize it.

	int status = 0;
	kernelDevice *dev = NULL;

	// Allocate memory for the device
	dev = kernelMalloc(sizeof(kernelDevice));
	if (!dev)
		return (status = 0);

	dev->device.class = kernelDeviceGetClass(DEVICECLASS_DMA);
	dev->driver = driver;

	// Initialize DMA operations
	status = kernelDmaInitialize(dev);
	if (status < 0)
	{
		kernelFree(dev);
		return (status);
	}

	// Add the kernel device
	return (status = kernelDeviceAdd(parent, dev));
}


static kernelDmaOps dmaOps = {
	driverOpenChannel,
	driverCloseChannel
};


/////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////
//
// Below here, the functions are exported for external use
//
/////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////

void kernelDmaDriverRegister(kernelDriver *driver)
{
	 // Device driver registration.

	driver->driverDetect = driverDetect;
	driver->ops = &dmaOps;

	return;
}

