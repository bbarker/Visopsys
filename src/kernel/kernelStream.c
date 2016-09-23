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
//  kernelStream.c
//

// This file contains all of the basic routines for dealing with generic
// data streams.  Data streams in Visopsys are implemented as circular
// buffers of variable size.

#include "kernelStream.h"
#include "kernelLock.h"
#include "kernelMalloc.h"
#include "kernelError.h"
#include <stdlib.h>
#include <string.h>


static int clear(stream *theStream)
{
	// Removes all data from the stream.  Returns 0 if successful,
	// negative otherwise.

	int status = 0;

	// Make sure the stream pointer isn't NULL
	if (!theStream)
		return (status = ERR_NULLPARAMETER);

	status = kernelLockGet(&theStream->lock);
	if (status < 0)
		return (status);

	// We clear the buffer and set first, next, and count to zero
	memset(theStream->buffer, 0, theStream->buffSize);

	theStream->first = 0;
	theStream->last = 0;
	theStream->count = 0;

	kernelLockRelease(&theStream->lock);

	// Return success
	return (status = 0);
}


static int appendByte(stream *theStream, unsigned char byte)
{
	// Appends a single byte to the end of the stream.  Returns 0 if
	// successful, negative otherwise.

	int status = 0;

	// Make sure the stream pointer isn't NULL
	if (!theStream)
		return (status = ERR_NULLPARAMETER);

	status = kernelLockGet(&theStream->lock);
	if (status < 0)
		return (status);

	// Add the character
	theStream->buffer[theStream->last++] = byte;

	// Increase the count
	theStream->count++;

	// Watch for buffer-wrap
	if (theStream->last >= theStream->size)
		theStream->last = 0;

	kernelLockRelease(&theStream->lock);

	// Return success
	return (status = 0);
}


static int appendDword(stream *theStream, unsigned dword)
{
	// Appends a single dword to the end of the stream.  Returns 0 if
	// successful, negative otherwise.

	int status = 0;

	// Make sure the stream pointer isn't NULL
	if (!theStream)
		return (status = ERR_NULLPARAMETER);

	status = kernelLockGet(&theStream->lock);
	if (status < 0)
		return (status);

	// Add the dword
	((unsigned *) theStream->buffer)[theStream->last++] = dword;

	// Increase the count
	theStream->count++;

	// Watch for buffer-wrap
	if (theStream->last >= theStream->size)
		theStream->last = 0;

	kernelLockRelease(&theStream->lock);

	// Return success
	return (status = 0);
}


static int appendBytes(stream *theStream, unsigned number,
	unsigned char *buffer)
{
	// Appends the requested number of bytes to the end of the stream.
	// Returns 0 on success, negative otherwise.

	int status = 0;
	unsigned added = 0;

	// Check parameters
	if (!theStream || !buffer)
		return (status = ERR_NULLPARAMETER);

	status = kernelLockGet(&theStream->lock);
	if (status < 0)
		return (status);

	// Do a loop to add the characters
	while (added < number)
	{
		// Add 1 character
		theStream->buffer[theStream->last++] = buffer[added++];

		// Watch for buffer-wrap
		if (theStream->last >= theStream->size)
			theStream->last = 0;
	}

	// Increase the count
	theStream->count = min((theStream->count + number), theStream->size);

	kernelLockRelease(&theStream->lock);

	// Return success
	return (status = 0);
}


static int appendDwords(stream *theStream, unsigned number, unsigned *buffer)
{
	// Appends the requested number of dwords to the end of the stream.
	// Returns 0 on success, negative otherwise.

	int status = 0;
	unsigned added = 0;

	// Check parameters
	if (!theStream || !buffer)
		return (status = ERR_NULLPARAMETER);

	status = kernelLockGet(&theStream->lock);
	if (status < 0)
		return (status);

	// Do a loop to add the dwords
	while (added < number)
	{
		// Add 1 dword
		((unsigned *) theStream->buffer)[theStream->last++] = buffer[added++];

		// Watch for buffer-wrap
		if (theStream->last >= theStream->size)
			theStream->last = 0;
	}

	// Increase the count
	theStream->count = min((theStream->count + number), theStream->size);

	kernelLockRelease(&theStream->lock);

	// Return success
	return (status = 0);
}


static int pushByte(stream *theStream, unsigned char byte)
{
	// Adds a single byte to the beginning of the stream.  Returns 0 on
	// success, negative otherwise.

	int status = 0;

	// Make sure the stream pointer isn't NULL
	if (!theStream)
		return (status = ERR_NULLPARAMETER);

	status = kernelLockGet(&theStream->lock);
	if (status < 0)
		return (status);

	// Move the head of the buffer backwards.  Watch out for backwards
	// wrap-around.
	if (theStream->first > 0)
		theStream->first--;
	else
		theStream->first = (theStream->size - 1);

	// Add the byte to the head of the buffer
	theStream->buffer[theStream->first] = byte;

	// Increase the count
	theStream->count++;

	kernelLockRelease(&theStream->lock);

	// Return success
	return (status = 0);
}


static int pushDword(stream *theStream, unsigned dword)
{
	// Adds a single dword to the beginning of the stream.  Returns 0 on
	// success, negative otherwise.

	int status = 0;

	// Make sure the stream pointer isn't NULL
	if (!theStream)
		return (status = ERR_NULLPARAMETER);

	status = kernelLockGet(&theStream->lock);
	if (status < 0)
		return (status);

	// Move the head of the buffer backwards.  Watch out for backwards
	// wrap-around
	if (theStream->first > 0)
		theStream->first--;
	else
		theStream->first = (theStream->size - 1);

	// Add the byte to the head of the buffer
	((unsigned *) theStream->buffer)[theStream->first] = dword;

	// Increase the count
	theStream->count++;

	kernelLockRelease(&theStream->lock);

	// Return success
	return (status = 0);
}


static int pushBytes(stream *theStream, unsigned number, unsigned char *buffer)
{
	// Adds the requested number of bytes to the beginning of the stream.
	// On success, it returns 0, negative otherwise

	int status = 0;
	int added = 0;

	// Check parameters
	if (!theStream || !buffer)
		return (status = ERR_NULLPARAMETER);

	// Make sure the number of bytes to push is greater than zero
	if (number <= 0)
		return (status = ERR_INVALID);

	status = kernelLockGet(&theStream->lock);
	if (status < 0)
		return (status);

	// Do a loop to add bytes
	while (number > 0)
	{
		// Move the head of the buffer backwards.  Watch out for backwards
		// wrap-around
		if (theStream->first > 0)
			theStream->first--;
		else
			theStream->first = (theStream->size - 1);

		// Add the byte to the head of the buffer
		theStream->buffer[theStream->first] = buffer[--number];

		added++;
	}

	// Increase the count
	theStream->count += added;

	kernelLockRelease(&theStream->lock);

	// Return success
	return (status = 0);
}


static int pushDwords(stream *theStream, unsigned number, unsigned *buffer)
{
	// Adds the requested number of dwords to the beginning of the stream.
	// On success, it returns 0, negative otherwise

	int status = 0;
	int added = 0;

	// Check parameters
	if (!theStream || !buffer)
		return (status = ERR_NULLPARAMETER);

	// Make sure the number of dwords to push is greater than zero
	if (number <= 0)
		return (status = ERR_INVALID);

	status = kernelLockGet(&theStream->lock);
	if (status < 0)
		return (status);

	// Do a loop to add dwords
	while (number > 0)
	{
		// Move the head of the buffer backwards.  Watch out for backwards
		// wrap-around
		if (theStream->first > 0)
			theStream->first--;
		else
			theStream->first = (theStream->size - 1);

		// Add the byte to the head of the buffer
		((unsigned *) theStream->buffer)[theStream->first] = buffer[--number];

		added++;
	}

	// Increase the count
	theStream->count += added;

	kernelLockRelease(&theStream->lock);

	// Return success
	return (status = 0);
}


static int popByte(stream *theStream, unsigned char *byte)
{
	// Removes a single byte from the beginning of the stream and returns
	// it to the caller.  On error, it returns a NULL byte.

	int status = 0;

	// Check parameters
	if (!theStream || !byte)
		return (status = ERR_NULLPARAMETER);

	// Make sure the buffer isn't empty
	if (!theStream->count)
		return (status = ERR_NODATA);

	status = kernelLockGet(&theStream->lock);
	if (status < 0)
		return (status);

	// Get the byte at the head of the buffer
	*byte = theStream->buffer[theStream->first];

	// Put a new NULL at the head of the buffer, and increment the head
	theStream->buffer[theStream->first++] = NULL;

	// Watch out for wrap-around
	if (theStream->first >= theStream->size)
		theStream->first = 0;

	// Decrease the count
	theStream->count--;

	kernelLockRelease(&theStream->lock);

	// Return success
	return (status = 0);
}


static int popDword(stream *theStream, unsigned *dword)
{
	// Removes a single dword from the beginning of the stream and returns
	// it to the caller.  On error, it returns a NULL byte.

	int status = 0;

	// Check parameters
	if (!theStream || !dword)
		return (status = ERR_NULLPARAMETER);

	// Make sure the buffer isn't empty
	if (!theStream->count)
		return (status = ERR_NODATA);

	status = kernelLockGet(&theStream->lock);
	if (status < 0)
		return (status);

	// Get the byte at the head of the buffer
	*dword = ((unsigned *) theStream->buffer)[theStream->first];

	// Put a new NULL at the head of the buffer and increment the head
	((unsigned *) theStream->buffer)[theStream->first++] = NULL;

	// Watch out for wrap-around
	if (theStream->first >= theStream->size)
		theStream->first = 0;

	// Decrease the count
	theStream->count--;

	kernelLockRelease(&theStream->lock);

	// Return success
	return (status = 0);
}


static int popBytes(stream *theStream, unsigned number, unsigned char *buffer)
{
	// Removes the requested number of bytes from the beginning of the stream
	// and returns them in the buffer provided.  On success, it returns the
	// number of characters it actually removed.  Returns negative on error.

	int status = 0;
	unsigned removed = 0;

	// Check parameters
	if (!theStream || !buffer)
		return (removed = ERR_NULLPARAMETER);

	// Make sure the buffer isn't empty
	if (!theStream->count)
		return (removed = 0);

	status = kernelLockGet(&theStream->lock);
	if (status < 0)
		return (status);

	// Do a loop to remove bytes and place them in the buffer
	while (removed < number)
	{
		// If the buffer is now empty, we stop here
		if (!theStream->count)
			break;

		// Get the byte at the head of the buffer
		buffer[removed++] = theStream->buffer[theStream->first];

		// Put a new NULL at this spot in the stream's buffer
		theStream->buffer[theStream->first++] = NULL;

		// Watch out for wrap-around
		if (theStream->first >= theStream->size)
			theStream->first = 0;

		// Decrease the count
		theStream->count--;
	}

	kernelLockRelease(&theStream->lock);

	// Return the number of bytes we copied
	return (removed);
}


static int popDwords(stream *theStream, unsigned number, unsigned *buffer)
{
	// Removes the requested number of dwords from the beginning of the stream
	// and returns them in the buffer provided.  On success, it returns the
	// number of dwords it actually removed.  Returns negative on error.

	int status = 0;
	unsigned removed = 0;

	// Check parameters
	if (!theStream || !buffer)
		return (removed = ERR_NULLPARAMETER);

	// Make sure the buffer isn't empty
	if (!theStream->count)
		return (removed = 0);

	status = kernelLockGet(&theStream->lock);
	if (status < 0)
		return (status);

	// Do a loop to remove dwords and place them in the buffer
	while (removed < number)
	{
		// If the buffer is now empty, we stop here
		if (!theStream->count)
			break;

		// Get the dword at the head of the buffer
		buffer[removed++] = ((unsigned *) theStream->buffer)[theStream->first];

		// Put a new NULL at this spot in the stream's buffer
		((unsigned *) theStream->buffer)[theStream->first++] = NULL;

		// Watch out for wrap-around
		if (theStream->first >= theStream->size)
			theStream->first = 0;

		// Decrease the count
		theStream->count--;
	}

	kernelLockRelease(&theStream->lock);

	// Return the number of bytes we copied
	return (removed);
}


/////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////
//
// Below here, the functions are exported for external use
//
/////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////

int kernelStreamNew(stream *theStream, unsigned size, streamItemSize itemSize)
{
	// Gets memory, initializes, clears out, and prepares the new stream

	int status = 0;

	// Check parameters
	if (!theStream)
		return (status = ERR_NULLPARAMETER);

	// Items should be greater than zero
	if (size <= 0)
		return (status = ERR_BOUNDS);

	// Clear out the stream structure
	memset((void *) theStream, 0, sizeof(stream));

	theStream->size = size;

	// What is the size, in bytes, of the requested stream?
	switch(itemSize)
	{
		case itemsize_byte:
			theStream->buffSize = (theStream->size * sizeof(char));
			break;
		case itemsize_dword:
			theStream->buffSize = (theStream->size * sizeof(unsigned));
			break;
		default:
			return (status = ERR_INVALID);
	}

	// Set up the stream's internal data.  All the other bits are zero
	theStream->buffer = kernelMalloc(theStream->buffSize);
	if (!theStream->buffer)
		return (status = ERR_MEMORY);

	// Set the appropriate manipulation functions for this stream.

	theStream->clear = &clear;
	theStream->intercept = NULL;

	switch(itemSize)
	{
		case itemsize_byte:
			// Copy the byte stream functions
			theStream->append = (int(*)(stream *, ...)) &appendByte;
			theStream->appendN = (int(*)(stream *, unsigned, ...)) &appendBytes;
			theStream->push = (int(*)(stream *, ...)) &pushByte;
			theStream->pushN = (int(*)(stream *, unsigned, ...)) &pushBytes;
			theStream->pop = (int(*)(stream *, ...)) &popByte;
			theStream->popN = (int(*)(stream *, unsigned, ...)) &popBytes;
			break;

		case itemsize_dword:
			// Copy the dword stream functions
			theStream->append = (int(*)(stream *, ...)) &appendDword;
			theStream->appendN = (int(*)(stream *, unsigned, ...)) &appendDwords;
			theStream->push = (int(*)(stream *, ...)) &pushDword;
			theStream->pushN = (int(*)(stream *, unsigned, ...)) &pushDwords;
			theStream->pop = (int(*)(stream *, ...)) &popDword;
			theStream->popN = (int(*)(stream *, unsigned, ...)) &popDwords;
			break;
	}

	return (status = 0);
}


int kernelStreamDestroy(stream *theStream)
{
	// Frees memory and clears out the stream.

	int status = 0;
	void *buffer = theStream->buffer;

	// Check parameters
	if (!theStream)
		return (status = ERR_NULLPARAMETER);

	// Clear it
	memset((void *) theStream, 0, sizeof(stream));

	// Free memory
	kernelFree(buffer);

	return (status = 0);
}

