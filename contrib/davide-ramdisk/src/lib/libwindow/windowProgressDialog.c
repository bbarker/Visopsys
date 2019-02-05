// 
//  Visopsys
//  Copyright (C) 1998-2006 J. Andrew McLaughlin
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
//  Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
//
//  windowProgressDialog.c
//

// This contains functions for user programs to operate GUI components.

#include <string.h>
#include <errno.h>
#include <sys/window.h>
#include <sys/api.h>

static volatile image waitImage;
static objectKey dialogWindow = NULL;
static objectKey progressBar = NULL;
static objectKey statusLabel = NULL;
static objectKey cancelButton = NULL;
static progress *prog = NULL;
static int threadPid = 0;


static void progressThread(void)
{
  // This thread monitors the supplied progress structure for changes and
  // updates the dialog window until the progress percentage equals 100 or
  // until the (interruptible) operation is interrupted.

  int status = 0;
  windowEvent event;
  progress lastProg;

  // Copy the supplied progress structure so we'll notice changes
  memcpy((void *) &lastProg, (void *) prog, sizeof(progress));

  windowComponentSetData(progressBar, (void *) prog->percentFinished, 1);
  // added (char*) (void*) by Davide Airaghi
  windowComponentSetData(statusLabel, (void*)prog->statusMessage,
			 strlen((char*)prog->statusMessage));
  windowComponentSetEnabled(cancelButton, prog->canCancel);

  while (1)
    {
      if (lockGet(&(prog->lock)) >= 0)
	{
	  // Did the status change?
	  if (memcmp((void *) &lastProg, (void *) prog, sizeof(progress)))
	    {
	      // Look for progress percentage changes
	      if (prog->percentFinished != lastProg.percentFinished)
		windowComponentSetData(progressBar,
				       (void *) prog->percentFinished, 1);

	      // Look for status message changes
	      // added (char*) by Davide Airaghi
	      if (strncmp((char*)prog->statusMessage, (char*)lastProg.statusMessage,
			  PROGRESS_MAX_MESSAGELEN))
		windowComponentSetData(statusLabel, (void*)prog->statusMessage,
				       strlen((char*)prog->statusMessage));

	      // Look for 'can cancel' flag changes
	      if (prog->canCancel != lastProg.canCancel)
		windowComponentSetEnabled(cancelButton, prog->canCancel);

	      // If the 'percent finished' is 100, quit
	      if (prog->percentFinished >= 100)
		break;

	      // Look for 'need confirmation' flag changes
	      if (prog->needConfirm)
		{
		  status = windowNewQueryDialog(dialogWindow, "Confirmation",
						(char *) prog->confirmMessage);
		  prog->needConfirm = 0;
		  if (status == 1)
		    prog->confirm = 1;
		  else
		    {
		      prog->confirm = -1;
		      break;
		    }
		}

	      // Look for 'error' flag changes
	      if (prog->error)
		{
		 // added (char*) by Davide Airaghi
		  windowNewErrorDialog(dialogWindow, "Error",
				       (char*)prog->statusMessage);
		  prog->confirmError = 1;
		  break;
		}

	      // Copy the status
	      memcpy((void *) &lastProg, (void *) prog, sizeof(progress));
	    }

	  lockRelease(&(prog->lock));
	}

      // Check for our Cancel button
      status = windowComponentEventGet(cancelButton, &event);
      if ((status < 0) || ((status > 0) && (event.type == EVENT_MOUSE_LEFTUP)))
	{
	  prog->cancel = 1;
	  windowComponentSetEnabled(cancelButton, 0);
	  break;
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


_X_ objectKey windowNewProgressDialog(objectKey parentWindow, const char *title, progress *tmpProg)
{
  // Desc: Create a 'progress' dialog box, with the parent window 'parentWindow', and the given titlebar text and progress structure.  The dialog creates a thread which monitors the progress structure for changes, and updates the progress bar and status message appropriately.  If the operation is interruptible, it will show a 'CANCEL' button.  If 'parentWindow' is NULL, the dialog box is actually created as an independent window that looks the same as a dialog.  This is a non-blocking call that returns immediately (but the dialog box itself is 'modal').  A call to this function should eventually be followed by a call to windowProgressDialogDestroy() in order to destroy and deallocate the window.

  int status = 0;
  objectKey imageComp = NULL;
  componentParameters params;
    
  // Check params.  It's okay for parentWindow to be NULL.
  if ((title == NULL) || (tmpProg == NULL))
    return (dialogWindow = NULL);

  // Create the dialog.  Arbitrary size and coordinates
  if (parentWindow)
    dialogWindow = windowNewDialog(parentWindow, title);
  else
    dialogWindow = windowNew(multitaskerGetCurrentProcessId(), title);
  if (dialogWindow == NULL)
    return (dialogWindow);

  bzero(&params, sizeof(componentParameters));
  params.gridWidth = 1;
  params.gridHeight = 1;
  params.padLeft = 5;
  params.padTop = 5;
  params.orientationX = orient_right;
  params.orientationY = orient_middle;
  params.flags = WINDOW_COMPFLAG_FIXEDWIDTH;

  if (waitImage.data == NULL)
    status = imageLoad(WAITIMAGE_NAME, 0, 0, (image *) &waitImage);

  if (status == 0)
    {
      waitImage.translucentColor.red = 0;
      waitImage.translucentColor.green = 255;
      waitImage.translucentColor.blue = 0;
      imageComp = windowNewImage(dialogWindow, (image *) &waitImage,
				 draw_translucent, &params);
    }

  // Create the progress bar
  params.gridX = 1;
  params.padRight = 5;
  params.orientationX = orient_center;
  params.flags = 0;
  progressBar = windowNewProgressBar(dialogWindow, &params);
  if (progressBar == NULL)
    {
      windowDestroy(dialogWindow);
      return (dialogWindow = NULL);
    }

  // Create the status label
  params.gridY = 1;
  statusLabel =
    windowNewTextLabel(dialogWindow, "                                       "
		       "                                         ", &params);
  if (statusLabel == NULL)
    {
      windowDestroy(dialogWindow);
      return (dialogWindow = NULL);
    }

  // Create the Cancel button
  params.gridY = 2;
  params.padBottom = 5;
  params.flags = WINDOW_COMPFLAG_FIXEDWIDTH;
  cancelButton = windowNewButton(dialogWindow, "Cancel", NULL, &params);
  if (cancelButton == NULL)
    {
      windowDestroy(dialogWindow);
      return (dialogWindow = NULL);
    }

  // Disable it until we know the operation is cancel-able.
  windowComponentSetEnabled(cancelButton, 0);

  windowRemoveCloseButton(dialogWindow);
  if (parentWindow)
    windowCenterDialog(parentWindow, dialogWindow);
  windowSetVisible(dialogWindow, 1);

  prog = tmpProg;

  // Spawn our thread to monitor the progress
  threadPid = multitaskerSpawn(progressThread, "progress thread", 0, NULL);
  if (threadPid < 0)
    {
      windowDestroy(dialogWindow);
      return (dialogWindow = NULL);
    }

  return (dialogWindow);
}


_X_ int windowProgressDialogDestroy(objectKey window)
{
  // Desc: Given the objectKey for a progress dialog 'window' previously returned by windowNewProgressDialog(), destroy and deallocate the window.

  int status = 0;

  if (window == NULL)
    return (status = ERR_NULLPARAMETER);

  if (window != dialogWindow)
    return (status = ERR_INVALID);

  windowComponentSetData(progressBar, (void *) 100, 1);
   // added (char*) (void*) by Davide Airaghi
  windowComponentSetData(statusLabel, (void*)prog->statusMessage,
			 strlen((char*)prog->statusMessage));

  if (multitaskerProcessIsAlive(threadPid))
    // Kill our thread
    status = multitaskerKillProcess(threadPid, 1);

  // Destroy the window
  windowDestroy(dialogWindow);

  dialogWindow = NULL;
  progressBar = NULL;
  statusLabel = NULL;
  cancelButton = NULL;
  prog = NULL;
  threadPid = 0;

  return (status);
}
