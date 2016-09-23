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
//  write.c
//

// This is the standard "write" function, as found in standard C libraries

#include <unistd.h>
#include <stdio.h>
#include <errno.h>
#include <sys/api.h>

size_t write(int fd, const void *buf, size_t count)
{
	// Write count bytes to the stream

	int status = 0;

	if (visopsys_in_kernel)
		return (errno = ERR_BUG);

	if ((fd == (int) stdout) || (fd == (int) stderr))
		status = textPrint(buf);
	else
		status = fileStreamWrite((fileStream *) fd, count, (void *) buf);

	if (status < 0)
		return (errno = status);

	return (count);
}

