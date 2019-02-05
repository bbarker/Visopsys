//
//  Visopsys
//  Copyright (C) 1998-2018 J. Andrew McLaughlin
//
//  This library is free software; you can redistribute it and/or modify it
//  under the terms of the GNU Lesser General Public License as published by
//  the Free Software Foundation; either version 2.1 of the License, or (at
//  your option) any later version.
//
//  This library is distributed in the hope that it will be useful, but
//  WITHOUT ANY WARRANTY; without even the implied warranty of
//  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU Lesser
//  General Public License for more details.
//
//  You should have received a copy of the GNU Lesser General Public License
//  along with this library; if not, write to the Free Software Foundation,
//  Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
//
//  strerror.c
//

// This "strerror" function is similar in function to the one found in
// other standard C libraries

#include <string.h>
#include <sys/errors.h>


static struct {
	int code;
	char *string;

} errorStringTable[] = {
	{ 0, "No error." },
	{ ERR_INVALID, "Invalid operation." },
	{ ERR_PERMISSION, "Permission denied." },
	{ ERR_MEMORY, "Memory allocation or freeing error." },
	{ ERR_BUSY, "The resource is busy." },
	{ ERR_NOSUCHENTRY, "Object does not exist." },
	{ ERR_BADADDRESS, "Invalid memory address." },
	{ ERR_TIMEOUT, "Operation timed out." },
	{ ERR_NOTINITIALIZED, "Resource has not been initialized." },
	{ ERR_NOTIMPLEMENTED, "Requested functionality not implemented." },
	{ ERR_NULLPARAMETER, "Required parameter was NULL." },
	{ ERR_NODATA, "No data supplied." },
	{ ERR_BADDATA, "Corrupt data encountered." },
	{ ERR_ALIGN, "Memory alignment error." },
	{ ERR_NOFREE, "No free resources." },
	{ ERR_DEADLOCK, "Deadlock situation avoided." },
	{ ERR_PARADOX, "Requested action is paradoxical." },
	{ ERR_NOLOCK, "Resource lock could not be obtained." },
	{ ERR_NOVIRTUAL, "Virtual memory error." },
	{ ERR_EXECUTE, "Command could not be executed." },
	{ ERR_NOTEMPTY, "Object is not empty." },
	{ ERR_NOCREATE, "Cannot create." },
	{ ERR_NODELETE, "Cannot delete." },
	{ ERR_IO, "Device input/output error." },
	{ ERR_BOUNDS, "Out of bounds error." },
	{ ERR_ARGUMENTCOUNT, "Incorrect number of parameters." },
	{ ERR_ALREADY, "Requested action is unnecessary." },
	{ ERR_DIVIDEBYZERO, "Divide by zero error." },
	{ ERR_DOMAIN, "Math operation is not in the domain." },
	{ ERR_RANGE, "Math operation is out of range." },
	{ ERR_CANCELLED, "Operation was cancelled." },
	{ ERR_KILLED, "Process killed." },
	{ ERR_NOMEDIA, "No media present." },
	{ ERR_NOSUCHFILE, "No such file." },
	{ ERR_NOSUCHDIR, "No such directory." },
	{ ERR_NOTAFILE, "Object is not a file." },
	{ ERR_NOTADIR, "Object is not a directory." },
	{ ERR_NOWRITE, "Cannot write data." },
	{ ERR_HOSTUNKNOWN, "Host lookup failed." },
	{ ERR_NOROUTETOHOST, "No route to host." },
	{ ERR_NOCONNECTION, "Couldn't connect." },
	{ ERR_NOSUCHUSER, "No such user." },
	{ ERR_NOSUCHPROCESS, "No such process." },
	{ ERR_NOSUCHDRIVER, "There is no driver for this operation." },
	{ ERR_NOSUCHFUNCTION, "Operation not supported." },
	{ ERR_BUG, "Internal error (bug)." },
	{ 0, NULL }
};


char *strerror(int error)
{
	// Returns the appropriate error message corresponding to the error
	// number that we were passed.

	int count;

	for (count = 0; errorStringTable[count].string; count ++)
	{
		if (errorStringTable[count].code == error)
			return (errorStringTable[count].string);
	}

	// Not found.  Don't change errno.
	return (NULL);
}

