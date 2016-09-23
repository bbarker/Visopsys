//
//  Visopsys
//  Copyright (C) 1998-2016 J. Andrew McLaughlin
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
//  errors.h
//

// This file contains all of the standard error numbers returned by
// calls to the Visopsys kernel (and by applications programs, if so
// desired).

#if !defined(_ERRORS_H)

// Items concerning severity for kernel errors
typedef enum {
	kernel_panic, kernel_error,  kernel_warn

} kernelErrorKind;

// This is the generic error
#define ERR_ERROR			-1	// No additional error information

// These are the most basic, standard, catch-all error codes.  They're not
// very specific or informative.  They're similar in name to some of the UNIX
// error codes.
#define ERR_INVALID			-2	// Invalid idea, generally
#define ERR_PERMISSION		-3	// Permission denied
#define ERR_MEMORY			-4	// Memory allocation or freeing error
#define ERR_BUSY			-5	// The resource is in use
#define ERR_NOSUCHENTRY		-6	// Generic things that don't exist
#define ERR_BADADDRESS		-7	// Bad pointers
#define ERR_TIMEOUT			-8	// Something timed out

// These are a little bit more specific
#define ERR_NOTINITIALIZED	-9	// The resource hasn't been initialized
#define ERR_NOTIMPLEMENTED	-10	// Functionality that hasn't been implemented
#define ERR_NULLPARAMETER	-11	// NULL pointer passsed as a parameter
#define ERR_NODATA			-12	// There's no data on which to operate
#define ERR_BADDATA			-13	// The data being operated on is corrupt
#define ERR_ALIGN			-14	// Memory alignment errors
#define ERR_NOFREE			-15	// No free (whatever is being requested)
#define ERR_DEADLOCK		-16	// The action would result in a deadlock
#define ERR_PARADOX			-17	// The requested action is paradoxical
#define ERR_NOLOCK			-18	// The requested resource could not be locked
#define ERR_NOVIRTUAL		-19	// Virtual address space error
#define ERR_EXECUTE			-20	// Could not execute a command or program
#define ERR_NOTEMPTY		-21	// Attempt to remove something that has content
#define ERR_NOCREATE		-22	// Could not create an item
#define ERR_NODELETE		-23	// Could not delete an item
#define ERR_IO				-24	// Input/Output error
#define ERR_BOUNDS			-25	// Array bounds exceeded, etc
#define ERR_ARGUMENTCOUNT	-26	// Incorrect number of arguments to a function
#define ERR_ALREADY			-27	// The action has already been performed
#define ERR_DIVIDEBYZERO	-28	// You're not allowed to do this!
#define ERR_DOMAIN			-29	// Argument is out of the domain of math func
#define ERR_RANGE			-30	// Result is out of the range of the math func
#define ERR_CANCELLED		-31	// Operation was explicitly cancelled
#define ERR_KILLED			-32	// Process or operation was unexpectedly killed
#define ERR_NOMEDIA			-33	// A removable disk has no media present.

// Things to do with files
#define ERR_NOSUCHFILE		-34	// No such file
#define ERR_NOSUCHDIR		-35	// No such directory
#define ERR_NOTAFILE		-36	// The item is not a regular file
#define ERR_NOTADIR			-37	// The item is not a directory
#define ERR_NOWRITE			-38	// The item cannot be written

// Other things that don't exist
#define ERR_NOSUCHUSER		-39	// The used ID is unknown
#define ERR_NOSUCHPROCESS	-40	// The process in question does not exist
#define ERR_NOSUCHDRIVER	-41	// There is no driver to perform an action
#define ERR_NOSUCHFUNCTION	-42	// The requested function does not exist

// Oops, it's the kernel's fault...
#define ERR_BUG				-43	// An internal bug has been detected

#define _ERRORS_H
#endif

