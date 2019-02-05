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
//  kernelWindowEventStream.c
//

// This file contains the kernel's facilities for reading and writing
// window events using a 'streams' abstraction.

#include "kernelWindowEventStream.h"
#include "kernelDebug.h"
#include "kernelError.h"
#include "kernelStream.h"


/////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////
//
//  Below here, the functions are exported for external use
//
/////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////

int kernelWindowEventStreamNew(windowEventStream *newStream)
{
	// This function initializes the new window event stream structure.
	// Returns 0 on success, negative otherwise.

	int status = 0;

	// Check params
	if (!newStream)
	{
		kernelError(kernel_error, "NULL parameter");
		return (status = ERR_NULLPARAMETER);
	}

	// We need to get a new stream and attach it to the window event stream
	// structure
	status = kernelStreamNew(newStream, (WINDOW_MAX_EVENTS *
		WINDOW_EVENT_DWORDS), itemsize_dword);
	if (status < 0)
	{
		kernelError(kernel_error, "Unable to create the window event stream");
		return (status);
	}

	return (status = 0);
}


int kernelWindowEventStreamPeek(windowEventStream *theStream)
{
	// This function will peek at the next window event from the window event
	// stream, and return the type, if any.

	int type = 0;

	// Check params
	if (!theStream)
	{
		kernelError(kernel_error, "NULL parameter");
		return (ERR_NULLPARAMETER);
	}

	if (theStream->count >= WINDOW_EVENT_DWORDS)
	{
		if (theStream->peek(theStream, &type))
			return (type = 0);
	}

	// Return the type, or NULL
	return (type);
}


int kernelWindowEventStreamRead(windowEventStream *theStream,
	windowEvent *event)
{
	// This function will read a window event from the window event stream
	// into the supplied windowEvent structure

	int status = 0;

	// Check params
	if (!theStream || !event)
	{
		kernelError(kernel_error, "NULL parameter");
		return (status = ERR_NULLPARAMETER);
	}

	if (theStream->count < WINDOW_EVENT_DWORDS)
		return (status = 0);

	// Read the requisite number of dwords from the stream
	if (theStream->popN(theStream, WINDOW_EVENT_DWORDS, event) <
		(int) WINDOW_EVENT_DWORDS)
	{
		kernelDebugError("Error reading complete window event from stream");
		return (status = ERR_NODATA);
	}

	return (status = 1);
}


int kernelWindowEventStreamWrite(windowEventStream *theStream,
	windowEvent *event)
{
	// This function will write the data from the supplied windowEvent
	// structure to the window event stream

	int status = 0;

	// Check params
	if (!theStream || !event)
	{
		kernelError(kernel_error, "NULL parameter");
		return (status = ERR_NULLPARAMETER);
	}

	// Append the requisite number of unsigneds to the stream
	if (theStream->appendN(theStream, WINDOW_EVENT_DWORDS, event) < 0)
	{
		kernelDebugError("Error writing complete window event to stream");
		return (status = ERR_NODATA);
	}

	return (status = 0);
}

