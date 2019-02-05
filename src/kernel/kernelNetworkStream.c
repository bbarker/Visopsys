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
//  kernelNetworkStream.c
//

// This file contains the kernel's facilities for reading and writing
// network packets using a 'streams' abstraction.

#include "kernelNetworkStream.h"
#include "kernelMisc.h"
#include "kernelError.h"
#include <string.h>


/////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////
//
//  Below here, the functions are exported for external use
//
/////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////


int kernelNetworkPacketStreamNew(kernelNetworkPacketStream *theStream)
{
	// This function initializes the new network packet stream.  Returns 0 on
	// success, negative otherwise.

	int status = 0;

	// Check params
	if (!theStream)
	{
		kernelError(kernel_error, "NULL parameter");
		return (status = ERR_NULLPARAMETER);
	}

	// Get a new stream
	status = kernelStreamNew(theStream, NETWORK_PACKETS_PER_STREAM,
		itemsize_pointer);
	if (status < 0)
		return (status);

	// Clear the stream
	theStream->clear(theStream);

	return (status = 0);
}


int kernelNetworkPacketStreamDestroy(kernelNetworkPacketStream *theStream)
{
	return (kernelStreamDestroy(theStream));
}


int kernelNetworkPacketStreamRead(kernelNetworkPacketStream *theStream,
	kernelNetworkPacket **packet)
{
	// This function will read a packet pointer from the packet stream into
	// the supplied packet pointer.  Does not release the packet; that's up to
	// the caller to do when finished with it.

	int status = 0;

	// Check params
	if (!theStream || !packet)
	{
		kernelError(kernel_error, "NULL parameter");
		return (status = ERR_NULLPARAMETER);
	}

	if (theStream->count < 1)
		return (status = ERR_NODATA);

	// Read the pointer from the stream
	status = theStream->pop(theStream, packet);
	if (status < 0)
		kernelError(kernel_error, "Couldn't read packet stream");

	return (status);
}


int kernelNetworkPacketStreamWrite(kernelNetworkPacketStream *theStream,
	kernelNetworkPacket *packet)
{
	// This function will write the pointer to the supplied packet into the
	// network packet stream, and add a reference count to it.

	int status = 0;
	kernelNetworkPacket *lostPacket = NULL;

	// Check params
	if (!theStream || !packet)
	{
		kernelError(kernel_error, "NULL parameter");
		return (status = ERR_NULLPARAMETER);
	}

	// If the stream is full, release the one at the head
	if (theStream->count >= NETWORK_PACKETS_PER_STREAM)
	{
		kernelError(kernel_error, "Packet stream is full");
		if (theStream->pop(theStream, &lostPacket) >= 0)
			kernelNetworkPacketRelease(lostPacket);
	}

	// Write the pointer to the stream
	status = theStream->append(theStream, packet);
	if (status < 0)
		return (status);

	// Add a reference count
	kernelNetworkPacketHold(packet);

	return (status = 0);
}

