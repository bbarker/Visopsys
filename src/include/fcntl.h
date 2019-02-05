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
//  fcntl.h
//
//  The information gathered here and some of the descriptions are from
//  http://www.opennc.org/onlinepubs/7908799/xsh/fcntl.h.html
//  Copyright (C) 1997 The Open Group
//

// This header defines the following requests and arguments for use by the
// functions fcntl() and open().

#if !defined(_FCNTL_H)

// Values for cmd used by fcntl() (the following values are unique):
#define F_DUPFD     0x01  // Duplicate file descriptor
#define F_GETFD     0x02  // Get file descriptor flags
#define F_SETFD     0x04  // Set file descriptor flags.
#define F_GETFL     0x08  // Get file status flags and file access modes.
#define F_SETFL     0x10  // Set file status flags.
#define F_GETLK     0x11  // Get record locking information.
#define F_SETLK     0x12  // Set record locking information.
#define F_SETLKW    0x14  // Set record locking information; wait if blocked.

// File descriptor flags used for fcntl():
#define FD_CLOEXEC  0x01  // Close the file descriptor upon execution of an
                          // exec family function.

// Values for l_type used for record locking with fcntl() (the following
// values are unique):
#define F_RDLCK     0x01  // Shared or read lock.
#define F_UNLCK     0x02  // Unlock.
#define F_WRLCK     0x04  // Exclusive or write lock.

// The values used for l_whence, SEEK_SET, SEEK_CUR and SEEK_END are
// defined as described in <unistd.h>.

// The following three sets of values for oflag are bitwise distinct:

#define O_CREAT     0x0001  // Create file if it does not exist.
#define O_EXCL      0x0002  // Exclusive use flag.
#define O_NOCTTY    0x0004  // Do not assign controlling terminal.
#define O_TRUNC     0x0008  // Truncate flag.
#define O_DIRECTORY 0x0010  // Fail if not a directory

// File status flags used for open() and fcntl():
#define O_APPEND    0x0020  // Set append mode.
#define O_DSYNC     0x0040  // Write according to synchronised I/O data
                            // integrity completion.
#define O_NONBLOCK  0x0080  // Non-blocking mode.
#define O_RSYNC     0x0100  // Synchronised read I/O operations.
#define O_SYNC      0x0200  // Write according to synchronised I/O file
                            // integrity completion.

// File access modes used for open() and fcntl():
#define O_RDONLY    0x0400  // Open for reading only.
#define O_RDWR      0x0800  // Open for reading and writing.
#define O_WRONLY    0x1000  // Open for writing only.
#define O_ACCMODE   0x1C00  // Mask for file access modes.

// The off_t, pid_t, and mode_t types are defined as described in
// <sys/types.h>.
#include <sys/types.h>

// The structure flock describes a file lock. It includes the following
// members:
typedef struct {
  short l_type;   // type of lock; F_RDLCK, F_WRLCK, F_UNLCK
  short l_whence; // flag for starting offset
  off_t l_start;  // relative offset in bytes
  off_t l_len;    // size; if 0 then until EOF
  pid_t l_pid;    // process ID of the process holding the lock; returned
                  // with F_GETLK
} flock;

// The following functions are supported
int open(const char *, int);

#define _FCNTL_H
#endif

