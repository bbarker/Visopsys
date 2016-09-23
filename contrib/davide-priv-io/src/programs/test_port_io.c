//
//  Visopsys
//  Copyright (C) 1998-2005 J. Andrew McLaughlin
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
//  test_port_io.c
//
//  Program to test IO ports & related stuff! Davide Airaghi

#include <stdio.h>
#include <sys/api.h>
#include <sys/vsh.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>

#define InPort8(port, data)  __asm__ __volatile__ ("inb %%dx, %%al" :  "=a" (data) : "d" (port))    

int main(int argc,char **argv)
{

  unsigned int portN = 0;
  unsigned char ch;
  int pId;
  int res = 0;
  
  pId = multitaskerGetCurrentProcessId();  
  
  // test_port_io <portnum> : try to execute inb !
  if (argc == 2) {
    portN = atoi(argv[1]);
    printf("Press a key to read port #%d ",portN);
    (void) getchar();
    printf("Reading port!\n");
    InPort8(portN, ch);
    return 0;
  }

  if (argc == 4) {
    pId = atoi(argv[3]);
    portN = atoi(argv[2]);
    if (argv[1][0]=='1')
	res = multitaskerAllowIO(pId,portN);
    if (argv[1][0]=='0')
	res = multitaskerNotAllowIO(pId,portN);	
    printf("ERRCODE: %d\n",res);	
    return 0;	
  }
  
  printf("Usage:\n");
  printf("test_port_io <portnum> : read from the port\n");
  printf("test_port_io 1 <portnum> <pId> : Allow <pId> to do IO port on <port>\n");
  printf("test_port_io 0 <portnum> <pId> : Don't Allow <pId> to do IO port on <port>\n");  
  
  return 0;
}
