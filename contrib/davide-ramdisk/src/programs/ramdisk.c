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
//  ramdisk.c
//

// Davide Airaghi
// create/destroy ramdisks  and show info


/* This is the text that appears when a user requests help about this program
<help>

 -- ramdisk --

Create/Destroy Ramdisks and show info


Usage:
  ramdisk <create | destroy | info> <num> <bytes> [unit]

This command will create or destroy fake-disk data related to 
ramdisk identified by ram<num>.
Data size is <bytes> bytes or, if a <unit> (M,K,G) is specified it's used:
<bytes> = 1 and <unit> = K ==> total size 1 KB = 1024 Bytes
<bytes> = 1 and <unit> = M ==> total size 1 MB = 1024*1024 Bytes
<bytes> = 1 and <unit> = G ==> total size 1 GB = 1024*1024*1024 Bytes

</help>
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/api.h>
#include <sys/disk.h>
#include <sys/ramdisk.h>

static void usage(char *name)
{
  printf("usage:\n");
  printf("%s <create | destroy | info> <num> <bytes> [unit]\n", name);
  return;
}


int main(int argc, char *argv[])
{
  int status = 0;
  int num;
  unsigned size;
  ramdisk diskinfo;
  
  // There need to be at least a source and destination file
  if (argc < 3 || argc > 5) 
    {
      usage(argv[0]);
      return (status = ERR_ARGUMENTCOUNT);
    }

  if (strlen(argv[2])>1) {
    usage(argv[0]);
    return (status=ERR_ARGUMENTCOUNT);
  }
  
  num = atoi(argv[2]);

  if (strcmp(argv[1],"create")==0) {
    
    if (argc < 4) {
      usage(argv[0]);
      return (status = ERR_ARGUMENTCOUNT);    
    }
    
    size = atou(argv[3]);
    
    if (argc == 5) {
	if (strlen(argv[4]) != 1) {
	    printf("Invalid <unit>, it can be: K or M or G\n");
	    usage(argv[0]);
	    return (status = ERR_ARGUMENTCOUNT);    
	}
	if (argv[4][0]=='K' || argv[4][0]=='M' || argv[4][0]=='G') {
	    if (argv[4][0]=='K') {
		size *= 1024;
	    }
	    if (argv[4][0]=='M') {
		size *= (1024*1024);		
	    }
	    if (argv[4][0]=='G') {
		size *= (1024*1024*1024);		
	    }
	}
	else {
	    printf("Invalid <unit>, it can be: K or M or G\n");
	    usage(argv[0]);
	    return (status = ERR_ARGUMENTCOUNT);    
	}	
	
    }

    if (size == 0) {
      usage(argv[0]);
      return (status = ERR_ARGUMENTCOUNT);    
    } 
    
    status = diskRamDiskCreate(num,size);
    
    if (status != 0) {
	printf("Unable to create RamDisk ram%d of size %u\n",num,size);
    }
    
  
  }
  else if (strcmp(argv[1],"destroy")==0) {
    
    status = diskRamDiskDestroy(num);
    
    if (status != 0)
	printf("Unable to destroy RamDisk ram%d\n",num);
  
  }
  else if (strcmp(argv[1],"info")==0) {
    
    status = diskRamDiskInfo(num,&diskinfo);
    
    if (status != 0)
	printf("Unable to get info about RamDisk ram%d\n",num);
    else {
	printf("\n");
	printf("RamDisk ram%d\n",num);
	printf("- Name: %s\n",diskinfo.name);
	printf("- Created: %s\n",(diskinfo.created ? "Yes" : "No"));
	printf("- Size: %u bytes\n",diskinfo.size);
	printf("- Sector Size: %u bytes\n",diskinfo.sectorSize);
	printf("- Read Only: %s\n",(diskinfo.readOnly ? "Yes" : "No"));
	printf("- Mounted: %s\n",(diskinfo.mounted ? "Yes" : "No"));
	printf("- Mount Point: %s\n",diskinfo.mountPoint);
	printf("- File System: %s\n",diskinfo.fsType);
	printf("\n");
    }
  
  } 
  else {
    usage(argv[0]);
    status = ERR_ARGUMENTCOUNT;
  }
  
  // Return 
  return status;
}
