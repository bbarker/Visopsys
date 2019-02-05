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
//  vshProgressBar.c
//

// This is a library function for displaying a progress bar.

#include <stdio.h>
#include <errno.h>
#include <sys/api.h>
#include <sys/vsh.h>

#define TEXT_PROGRESSBAR_LENGTH  20

static progress *prog = NULL;
static int textProgressBarRow = 0;
static int threadPid = 0;


static void makeTextProgressBar(void)
{
  // Make an initial text mode progress bar.

  char row[TEXT_PROGRESSBAR_LENGTH + 3];
  int count;

  // Cursor down the number of rows we need, then back up
  printf("\n\n\n\n\n");
  for (count = 0; count < 5; count ++)
    textCursorUp();

  // Make the top row
  row[0] = 218;
  for (count = 1; count <= TEXT_PROGRESSBAR_LENGTH; count ++)
    row[count] = 196;
  row[count++] = 191;
  row[TEXT_PROGRESSBAR_LENGTH + 2] = '\0';
  printf("\n%s\n", row);

  // Middle row
  row[0] = 179;
  for (count = 1; count <= TEXT_PROGRESSBAR_LENGTH; count ++)
    row[count] = ' ';
  row[count++] = 179;
  row[TEXT_PROGRESSBAR_LENGTH + 2] = '\0';
  printf("%s\n", row);

  // Bottom row
  row[0] = 192;
  for (count = 1; count <= TEXT_PROGRESSBAR_LENGTH; count ++)
    row[count] = 196;
  row[count++] = 217;
  row[TEXT_PROGRESSBAR_LENGTH + 2] = '\0';
  printf("%s\n\n\n\n", row);

  // Remember the starting row
  textProgressBarRow = (textGetRow() - 5);
}


static void setPercent(int percent)
{
  // Set the value of the text mode progress bar.

  int tempColumn = textGetColumn();
  int tempRow = textGetRow();
  int progressChars = 0;
  int column = 0;
  char row[TEXT_PROGRESSBAR_LENGTH + 1];
  int count;

  progressChars = ((percent * TEXT_PROGRESSBAR_LENGTH) / 100);

  textSetColumn(1);
  textSetRow(textProgressBarRow);

  for (count = 0; count < progressChars; count ++)
    row[count] = 177;
  row[count] = '\0';
  printf("%s\n", row);

  column = (TEXT_PROGRESSBAR_LENGTH / 2);
  if (percent < 10)
    column += 1;
  else if (percent >= 100)
    column -= 1;
  textSetColumn(column);
  printf("%d%%", percent);

  // Back to where we were
  textSetColumn(tempColumn);
  textSetRow(tempRow);
}


static void setMessage(volatile char *message, int confirm)
{
  // Set the line of text that can appear below the progress bar.

  int tempColumn = textGetColumn();
  int tempRow = textGetRow();
  char nullBuffer[PROGRESS_MAX_MESSAGELEN];
  char character = '\0';

  memset(nullBuffer, ' ', (PROGRESS_MAX_MESSAGELEN - 1));
  nullBuffer[PROGRESS_MAX_MESSAGELEN - 1] = '\0';

  textSetRow(textProgressBarRow + 2);
  textSetColumn(0);
  printf(nullBuffer);

  textSetRow(textProgressBarRow + 2);
  textSetColumn(0);
  printf((char *) message);

  if (confirm)
    {
      printf(" (y/n): ");

      textInputSetEcho(0);
      while(1)
	{
	  character = getchar();
      
	  if ((character == 'y') || (character == 'Y'))
	    {
	      printf("Yes");
	      prog->confirm = 1;
	      break;
	    }
	  else if ((character == 'n') || (character == 'N'))
	    {
	      printf("No");
	      prog->confirm = -1;
	      break;
	    }
	}

      textInputSetEcho(1);
      prog->needConfirm = 0;
    }

  // Back to where we were
  textSetColumn(tempColumn);
  textSetRow(tempRow);
}


static void progressThread(void)
{
  // This thread monitors the supplied progress structure for changes and
  // updates the progress bar until the progress percentage equals 100 or
  // until the (interruptible) operation is interrupted.

  progress lastProg;
  char character = '\0';

  memcpy((void *) &lastProg, (void *) prog, sizeof(progress));

  while (1)
    {
      // Try to get a lock on the progress structure
      if (lockGet(&(prog->lock)) >= 0)
	{
	  if (prog->canCancel && textInputCount())
	    {
	      textInputGetc(&character);
	      if ((character == 'Q') || (character == 'q'))
		prog->cancel = 1;
	    }

	  // Did the status change?
	  if (memcmp((void *) &lastProg, (void *) prog, sizeof(progress)))
	    {
	      // Look for progress percentage changes
	      if (prog->percentFinished != lastProg.percentFinished)
		setPercent(prog->percentFinished);

	      // Look for status message changes
	      // added by (void*) by Davide Airaghi to make gcc happy
	      if (strncmp((void*)prog->statusMessage, (void*)lastProg.statusMessage,
			  PROGRESS_MAX_MESSAGELEN))
		setMessage(prog->statusMessage, 0);

	      // Look for 'need confirmation' flag changes
	      if (prog->needConfirm)
		setMessage(prog->confirmMessage, 1);

	      // Look for 'error' flag changes
	      if (prog->error)
		{
		  prog->confirmError = 1;
		  break;
		}

	      // Look for 'cancel' flag changes
	      if (prog->cancel)
		break;

	      // If the 'percent finished' is 100, quit
	      if (prog->percentFinished >= 100)
		break;

	      // Copy the status
	      memcpy((void *) &lastProg, (void *) prog, sizeof(progress));
	    }

	  lockRelease(&(prog->lock));
	}

      // Done
      multitaskerYield();
    }

  lockRelease(&(prog->lock));

  // Exit.
  multitaskerTerminate(0);
}


/////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////
//
// Below here, the functions are exported for external use
//
/////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////


_X_ int vshProgressBar(progress *tmpProg)
{
  // Desc: Given a progress structure 'tmpProg', make a text progress bar that monitors the structure and updates itself (in a non-blocking way).  After the operation has completed, vshProgressBarDestroy() should be called to shut down the thread.

  int status = 0;

  if (tmpProg == NULL)
    return (status = ERR_NULLPARAMETER);

  makeTextProgressBar();

  prog = tmpProg;

  // Spawn our thread to monitor the progress
  threadPid = multitaskerSpawn(progressThread, "progress thread", 0, NULL);
  if (threadPid < 0)
    return (threadPid);

  return (status = 0);
}


_X_ int vshProgressBarDestroy(progress *tmpProg)
{
  // Desc: Given a progress structure 'tmpProg', indicate 100%, shut down and deallocate anything associated with a previous call to vshProgressBar().

  int status = 0;

  if (tmpProg == NULL)
    return (status = ERR_NULLPARAMETER);

  if (tmpProg != prog)
    return (status = ERR_INVALID);

  setPercent(100);
  setMessage(prog->statusMessage, 0);

  if (multitaskerProcessIsAlive(threadPid))
    // Kill our thread
    status = multitaskerKillProcess(threadPid, 1);

  prog = NULL;
  textProgressBarRow = 0;
  threadPid = 0;

  return (status);
}
