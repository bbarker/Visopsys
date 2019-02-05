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
//  file2disk.c
//

// Davide Airaghi
// dump a regular file into a disk


/* This is the text that appears when a user requests help about this program
<help>

 -- file2disk --

Dump a disk into a regular file


Usage:
  file2disk <file> <disk> <sectors_per_block>
  
This command will dump the content of a gieven regular file into a given disk into reading blocks of given size (in sectors) 

</help>
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
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
  printf("%s  <file> <disk> <sectors_per_block> \n", name);
  return;
}


int main(int argc, char *argv[])
{
  int status = 0;
  unsigned num;
  unsigned left;
  unsigned each;
  unsigned cur;
  struct stat fileStat;
  void * buffer = NULL;
  int processId;
  disk diskData;
  FILE * f;
  char srcFileName[MAX_PATH_NAME_LENGTH];      
  
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

  vshMakeAbsolutePath(argv[1], srcFileName);

  status = diskGet(argv[2], &diskData);

  if (status < 0) {
     printf("Unable to get info about sectors of %s\n",argv[2]);
     return ERR_NODATA;
  }

  status = stat(srcFileName,&fileStat);

  if (status < 0) {
     printf("Unable to get info about %s\n",srcFileName);
     return ERR_NODATA;
  }

  if (fileStat.st_size != diskData.numSectors * diskData.sectorSize)  {
     printf("Incorrect file %s size, the file cannot contain the real %s image\n",srcFileName,argv[2]);  
     return ERR_NODATA;
  }

  f = fopen(srcFileName,"r");
  
  if (f == NULL) {
    printf("Unable to open %s for reading\n",srcFileName);
    return ERR_IO;
  }
  
  left = diskData.numSectors;
  
  each = num;

  if (left < each) {
    each = left;
  }

  printf("%u * %u = %u\n",each,diskData.sectorSize, each * diskData.sectorSize);

  buffer = (void *)malloc(each * diskData.sectorSize);
  
  if (buffer == NULL) {
    printf("Unable to get buffer memory\n");
    fclose(f);
    return ERR_MEMORY;
  }

  cur = 0;

  while (left > 0) {
       
    printf("\nDumping sector %u/%u\n",cur,diskData.numSectors);
  
    printf("Read\n");  

    if (fread(buffer, each * diskData.sectorSize, 1, f) <= 0) { /* <------- exception: page fault with each = 512, 1000, 1024, ! */
	printf("Read IN 0 \n");  
	fclose(f);
	printf("Read IN 1 \n");  
	free(buffer);
	printf("Read IN 2 \n");  
	printf("Error while reading file %s\n",srcFileName);
	return ERR_IO;
    }
  
    printf("Write\n");
    if (diskWriteSectors(argv[2],cur,each,buffer) != 0) {
	printf("Write IN 0 \n");  
	fclose(f);
	printf("Write IN 1 \n");  
	free(buffer);
	printf("Write IN 2\n");  
	printf("Error while writing disk %s\n",argv[2]);
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

  status = 0;

  // Return 
  return status;
}
