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


int kernelNetworkPacketStreamNew(kernelNetworkPacketStream *newStream)
{
	// This function initializes the new network packet stream.  Returns 0 on
	// success, negative otherwise.

	int status = 0;

	// Check params
	if (!newStream)
	{
		kernelError(kernel_error, "NULL parameter");
		return (status = ERR_NULLPARAMETER);
	}

	// Get a new stream
	status = kernelStreamNew(newStream, (NETWORK_PACKETS_PER_STREAM *
		(sizeof(kernelNetworkPacket) / sizeof(unsigned))), itemsize_dword);
	if (status < 0)
		return (status);

	// Clear the stream
	newStream->clear(newStream);

	// Yahoo, all set.
	return (status = 0);
}


int kernelNetworkPacketStreamRead(kernelNetworkPacketStream *theStream,
	kernelNetworkPacket *packet)
{
	// This function will read a packet from the packet stream into the
	// supplied kernelNetworkPacket structure

	// Check params
	if (!theStream || !packet)
	{
		kernelError(kernel_error, "NULL parameter");
		return (ERR_NULLPARAMETER);
	}

	// Read the requisite number of dwords from the stream
	return (theStream->popN(theStream, (sizeof(kernelNetworkPacket) /
		sizeof(unsigned)), packet));
}


int kernelNetworkPacketStreamWrite(kernelNetworkPacketStream *theStream,
	kernelNetworkPacket *packet)
{
	// This function will write the data from the supplied packet into the
	// network packet stream

	// Check arguments
	if (!theStream || !packet)
	{
		kernelError(kernel_error, "NULL parameter");
		return (ERR_NULLPARAMETER);
	}

	// Append the requisite number of unsigneds to the stream
	return (theStream->appendN(theStream, (sizeof(kernelNetworkPacket) /
		sizeof(unsigned)), packet));
}

