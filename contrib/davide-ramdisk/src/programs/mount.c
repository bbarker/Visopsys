//
//  Visopsys
//  Copyright (C) 1998-2006 J. Andrew McLaughlin
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
//  59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
//
//  mount.c
//

// This is the UNIX-style command for mounting filesystems

/* This is the text that appears when a user requests help about this program
<help>

 -- mount --

Mount a filesystem.

Usage:
  mount <disk> <mount_point> [comma_separated_options]

This command will mount (make usable) the filesystem on the specified logical
disk.  Available logical disks can be listed using the 'disks' command.
The second parameter is a location where the contents of the filesystem
should be mounted.

Example:
  mount cd0 /cdrom

This will mount the first CD-ROM device, and make its contents accessible
in the /cdrom subdirectory.

Note that the mount point parameter should specify a name that does *not*
exist.  This is the opposite of the UNIX mount command behaviour.  The
example above will fail if there is already a file or directory called /cdrom.

</help>
*/


// modified by Davide Airaghi to add "options" handling

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <sys/api.h>
#include <sys/vsh.h>


static void usage(char *name)
{
  printf("usage:\n");
  printf("%s <disk> <mount point> [comma_separated_options]\n", name);
  return;
}


int main(int argc, char *argv[])
{
  // Attempts to mount the named filesystem to the named mount point

  int status = 0;
  char *diskName = NULL;
  char filesystem[MAX_PATH_NAME_LENGTH];
  char *options = NULL; 
  if (argc < 3 || argc > 4)
    {
      usage(argv[0]);
      return (status = ERR_ARGUMENTCOUNT);
    }

  diskName = argv[1];
  
  vshMakeAbsolutePath(argv[2], filesystem);

  if (argc == 4) {
    options = argv[3];
  }

  status = filesystemMount(diskName, filesystem, options);
  if (status < 0)
    {
      errno = status;
      perror(argv[0]);
      return (status = errno);
    }
 
  // Finished
  return (status = 0);
}
