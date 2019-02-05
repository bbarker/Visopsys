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
//  kernelError.h
//

// This header file defines things used to communicate errors within
// the kernel, and functions for communicating these errors to the user.

#if !defined(_KERNELERROR_H)

#include <sys/errors.h>

// Definitions
#define MAX_ERRORTEXT_LENGTH	1024
#define ERRORDIALOG_THREADNAME	"error dialog thread"

void kernelErrorOutput(const char *, const char *, int, kernelErrorKind,
	const char *, ...) __attribute__((format(printf, 5, 6)));
void kernelErrorDialog(const char *, const char *, const char *);

// This macro should be used to invoke all kernel errors
#define kernelError(kind, message, arg...)	\
	kernelErrorOutput(__FILE__, __FUNCTION__, __LINE__, kind, message, ##arg)

#define _KERNELERROR_H
#endif

