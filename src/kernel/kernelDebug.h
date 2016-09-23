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
//  kernelDebug.h
//

// This header file defines things used to print debug messages from within
// the kernel

#if !defined(_KERNELDEBUG_H)

#if defined(DEBUG)

	#include "kernelError.h"
	#include <sys/debug.h>

	// Definitions
	#define MAX_DEBUGTEXT_LENGTH	1024
	#define MAX_DEBUG_CATEGORIES	16
	#define MAX_DEBUG_FILENAMES		16

	#define DEBUG_SHOWPROCESS		0x08
	#define DEBUG_SHOWFILE			0x04
	#define DEBUG_SHOWFUNCTION		0x02

	void kernelDebugInitialize(void);
	void kernelDebugFlags(int);
	void kernelDebugAddCategory(debug_category);
	void kernelDebugAddFile(const char *);
	void kernelDebugRemoveFile(const char *);
	void kernelDebugOutput(const char *, const char *, int, debug_category,
		const char *, ...) __attribute__((format(printf, 5, 6)));
	void kernelDebugHex(void *, unsigned);
	void kernelDebugHexDwords(void *, unsigned);
	void kernelDebugBinary(void *, unsigned);
	void kernelDebugStack(void *, unsigned, void *, long, unsigned);
	void kernelDebugDoStop(const char *, const char *, int);

	// Shortcuts
	#define DEBUG_ADDFILE kernelDebugAddFile(__FILE__)
	#define DEBUG_REMOVEFILE kernelDebugRemoveFile(__FILE__)

	// These macros should be used for actual debug calls
	#define kernelDebug(category, message, arg...) \
		kernelDebugOutput(__FILE__, __FUNCTION__, __LINE__, category, \
			message, ##arg)

	#define kernelDebugError(message, arg...) \
		kernelError(kernel_warn, message, ##arg)

	#define kernelDebugStop() \
		kernelDebugDoStop(__FILE__, __FUNCTION__, __LINE__)

#else // !defined(DEBUG)

	#define kernelDebugInitialize(...) do { } while (0)
	#define kernelDebugFlags(...) do { } while (0)
	#define kernelDebugAddCategory(...) do { } while (0)
	#define kernelDebugAddFile(...) do { } while (0)
	#define kernelDebugRemoveFile(...) do { } while (0)
	#define kernelDebug(...) do { } while (0)
	#define kernelDebugError(...) do { } while (0)
	#define kernelDebugHex(...) do { } while (0)
	#define kernelDebugHexDwords(...) do { } while (0)
	#define kernelDebugBinary(...) do { } while (0)
	#define kernelDebugStack(...) do { } while (0)
	#define kernelDebugStop() do { } while (0)

	#define DEBUG_ADDFILE do { } while (0)
	#define DEBUG_REMOVEFILE do { } while (0)

#endif // defined(DEBUG)

#define _KERNELDEBUG_H
#endif

