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
//  disk2file.c
//

// Davide Airaghi
// dump a disk into a regular file


/* This is the text that appears when a user requests help about this program
<help>

 -- disk2file --

Dump a disk into a regular file


Usage:
  disk2file <disk> <file> <sectors_per_block>
  
This command will dump the content of the given disk into a file reading blocks of given size (in sectors) 

</help>
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/api.h>
#include <sys/disk.h>
#include <sys/vsh.h>

#define PERM "You must be a privileged user to use this " \
             "command.\n(Try logging in as user \"admin\")" 

// with sectors of 512 bytes we read and write blocks up to 67108864 bytes ( 64MB )
#define MAX_SECTORS 8192*16

static void usage(char *name)
{
  printf("usage:\n");
  printf("%s <disk> <file> <sectors_per_block> \n", name);
  return;
}


int main(int argc, char *argv[])
{
  int status = 0;
  unsigned num;
  unsigned left;
  unsigned each;
  unsigned cur;
  void * buffer = NULL;
  int processId;
  disk diskData;
  FILE * f;
  char destFileName[MAX_PATH_NAME_LENGTH];
      
  // There need to be at least a source and destination file
  if (argc < 4 || argc > 4) 
    {
      usage(argv[0]);
      return (status = ERR_ARGUMENTCOUNT);
    }

  processId = multitaskerGetCurrentProcessId();                                                                                                                       

  // Check privilege level 
  if (multitaskerGetProcessPrivilege(processId) != 0) 
    {
	printf("\n%s\n\n", PERM);
	return (errno = ERR_PERMISSION);
    }

  num = atou(argv[3]);
  
  if (num == 0 || num > MAX_SECTORS) {
    printf("Invalid number of sectors per block, not in 1 - %d\n",MAX_SECTORS);
    return ERR_BOUNDS;
  }

  status = diskGet(argv[1], &diskData);
  
  if (status < 0) {
     printf("Unable to get info about sectors of %s\n",argv[1]);
     return ERR_NODATA;
  }

  vshMakeAbsolutePath(argv[2], destFileName);

  f = fopen(destFileName,"w");
  
  if (f == NULL) {
    printf("Unable to open %s for writing\n",destFileName);
    return ERR_IO;
  }

  left = diskData.numSectors;
  
  each = num;

  if (left < each) {
    each = left;
  }

  buffer = malloc(each * diskData.sectorSize);
  
  if (buffer == NULL) {
    printf("Unable to get buffer memory\n");
    fclose(f);
    return ERR_MEMORY;
  }
  
  cur = 0;

  while (left > 0) {
       
    printf("\nDumping sector %u/%u",cur,diskData.numSectors);
  
    if (diskReadSectors(argv[1],cur,each,buffer) != 0) {
	fclose(f);
	free(buffer);
	printf("Error while reading disk %s\n",argv[1]);
	return ERR_IO;
    }
    
    if (fwrite(buffer, each * diskData.sectorSize, 1, f) <= 0) {
	printf("Error while writing %s\n",destFileName);
	free(buffer);
	fclose(f);
	return ERR_IO;
    }
    
    left -= each;

    cur += each;
    
    if (each > left)
	each = left;
        
  }
  
  printf("\nDone\n");
  
  free(buffer);
  
  fclose(f);

  // Return 
  return status;
}
