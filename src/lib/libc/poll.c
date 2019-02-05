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
//  poll.c
//

// This is the standard "poll" function, as found in standard C libraries

#include <poll.h>
#include <errno.h>
#include <sys/api.h>


int poll(struct pollfd *fds, nfds_t nfds, int timeout)
{
	int status = 0;
	int numEvents = 0;
	uquad_t endTime = 0;
	fileDescType type = filedesc_unknown;
	void *data = NULL;
	nfds_t count;

	if (visopsys_in_kernel)
	{
		errno = ERR_BUG;
		return (numEvents = -1);
	}

	endTime = (cpuGetMs() + timeout);

	do
	{
		for (count = 0; count < nfds; count ++)
		{
			// Look up the file descriptor
			status = _fdget(fds[count].fd, &type, &data);
			if (status < 0)
			{
				errno = status;
				return (numEvents = -1);
			}

			switch (type)
			{
				case filedesc_textstream:
				{
					// Text data to read?
					if ((fds[count].events & POLLIN) &&
						(textInputStreamCount(multitaskerGetTextInput()) > 0))
					{
						fds[count].revents |= POLLIN;
						numEvents += 1;
					}

					break;
				}

				case filedesc_socket:
				{
					// Network data to read?
					if ((fds[count].events & POLLIN) &&
						(networkCount((objectKey *) data) > 0))
					{
						fds[count].revents |= POLLIN;
						numEvents += 1;
					}

					break;
				}

				default:
				{
					status = ERR_NOTIMPLEMENTED;
					break;
				}
			}

			if (status < 0)
			{
				errno = status;
				return (numEvents = -1);
			}
		}

	} while (!numEvents && (cpuGetMs() < endTime));

	return (numEvents);
}

