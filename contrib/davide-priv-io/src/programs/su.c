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
//  su.c
//

// This is similar to unix "su" command , Davide Airaghi
//

#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <sys/process.h>
#include <sys/errors.h>
#include <sys/api.h>

#define LOGIN_SHELL "/programs/vsh"
#define SHELLNAME   "vsh"
#define AUTHFAILED  "Authentication failed"
#define LOGINNAME   "Please enter your login name:"
#define LOGINPASS   "Please enter your password:"
#define READONLY    "You are running the system from a read-only device.\n" \
                    "You will not be able to alter settings, or generally\n" \
                    "change anything."
#define MAX_LOGIN_LENGTH 64

// The following are only used if we are running a graphics mode login window.
static int readOnly = 1;


static char login[MAX_LOGIN_LENGTH];
static char password[MAX_LOGIN_LENGTH];


static void printPrompt(void)
{
  // Print the login: prompt
  printf("%s", "login: ");
  return;
}


static void processChar(char *buffer, unsigned char bufferChar, int echo)
{
  int currentCharacter = 0;
  char *tooLong = NULL;
  static char *loginTooLong = "That login name is too long.";
  static char *passwordTooLong = "That password is too long.";

  if (buffer == login)
    tooLong = loginTooLong;
  else if (buffer == password)
    tooLong = passwordTooLong;

  currentCharacter = strlen(buffer);

  // Make sure our buffer isn't full
  if (currentCharacter >= (MAX_LOGIN_LENGTH - 1))
    {
      buffer[0] = '\0';
      printf("\n");
      printf("%s\n", tooLong);
      printPrompt();
      return;
    }
  
  if (bufferChar == (unsigned char) 8)
    {
      if (currentCharacter > 0)
	{
	  buffer[currentCharacter - 1] = '\0';
	  textBackSpace();
	}
    }

  else if (bufferChar == (unsigned char) 10)
    printf("\n");
  
  else
    {
      // Add the current character to the login buffer
      buffer[currentCharacter] = bufferChar;
      buffer[currentCharacter + 1] = '\0';

      if (echo)
	textPutc(bufferChar);
      else
	textPutc((int) '*');
    }

  return;
}




static void getLogin(void)
{
  char bufferCharacter = '\0';

  // Clear the login name and password buffers
  login[0] = '\0';
  password[0] = '\0';
      

      // Turn keyboard echo off
      textInputSetEcho(0);
  
      printf("\n");
      printPrompt();

      // This loop grabs characters
      while(1)
	{
	  bufferCharacter = getchar();
	  processChar(login, bufferCharacter, 1);
	  
	  if (bufferCharacter == (unsigned char) 10)
	    {
	      if (strcmp(login, ""))
		// Now we interpret the login
		break;
		  
	      else
		{
		  // The user hit 'enter' without typing anything.
		  // Make a new prompt
		    printPrompt();
		  continue;
		}
	    }
	}
	      
      printf("password: ");
	  
      // This loop grabs characters
      while(1)
	{
	  bufferCharacter = getchar();
	  processChar(password, bufferCharacter, 0);
	  
	  if (bufferCharacter == (unsigned char) 10)
	    break;
	}
  
      // Turn keyboard echo back on
      textInputSetEcho(1);
    
}


int main(int argc, char *argv[])
{
  int status = 0;
  // char opt = '\0';
  int skipLogin = 0;
  int myPid = 0;
  int shellPid = 0;
  disk sysDisk;
  process cur, parent;

  // make the compiler happy
  if ((argc || argv[0][0]=='0') && (0)) {
    // getLogin();
  }

  // Find out whether we are currently running on a read-only filesystem
  if (!fileGetDisk("/", &sysDisk))
    readOnly = sysDisk.readOnly;

  myPid = multitaskerGetCurrentProcessId();
  status = multitaskerGetProcess(myPid, &cur);
  if (status < 0) {
    // printf("C %d \n",status);
    return -1;
  }

  status = multitaskerGetProcess(cur.parentProcessId,&parent);
  
  if (status < 0) {
    // printf("P %d \n",status);
    return -1;
  }
    
  //printf("\nParent %s\n",parent.processName);
  //return  0;
  if (strcmp(parent.processName,SHELLNAME)!=0) {
    printf("This program che only be run from %s\n",SHELLNAME);
    return -1;
  }
  
  // Outer loop, from which we never exit
  while(1)
    {
      // Inner loop, which goes until we authenticate successfully
      while (1)
	{

	  skipLogin = 0;
	  
	  getLogin();
	  
	  // We have a login name to process.  Authenticate the user and
	  // log them into the system
	  status = userLogin(login, password);
	  if (status < 0)
	    {
		printf("\n*** " AUTHFAILED " ***\n\n");
	      continue;
	    }

	  break;
	}

      // Set the login name as an environment variable
      environmentSet("USER", login);
      
	
	  // Load a shell process
	  shellPid = loaderLoadProgram(LOGIN_SHELL, userGetPrivilege(login));
	  if (shellPid < 0)
	    {
	      printf("Couldn't load login shell %s!", LOGIN_SHELL);
	      continue;
	    }

	  // Set the PID to the window manager thread
	  userSetPid(login, shellPid);
	  
	  printf("\nWelcome %s\n%s", login,
		 (readOnly? "\n" READONLY "\n" : ""));

	  // Run the text shell and block on it
	  loaderExecProgram(shellPid, 1 /* block */);

	  // If we return to here, the login session is over.
	

      // Log the user out of the system
      //userLogout(login);
      break;
      
    }
    
    printf("SU: Returning to previous shell.\n");
    
    return 0;
}
