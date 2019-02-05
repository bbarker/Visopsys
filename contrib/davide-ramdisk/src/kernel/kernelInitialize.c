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
//  kernelInitialize.c
//

#include "kernelInitialize.h"
#include "kernelDebug.h"
#include "kernelDisk.h"
#include "kernelFile.h"
#include "kernelFilesystem.h"
#include "kernelImage.h"
#include "kernelMisc.h"
#include "kernelPage.h"
#include "kernelMemory.h"
#include "kernelText.h"
#include "kernelLog.h"
#include "kernelDescriptor.h"
#include "kernelInterrupt.h"
#include "kernelMalloc.h"
#include "kernelMultitasker.h"
#include "kernelParameters.h"
#include "kernelRandom.h"
#include "kernelKeyboard.h"
#include "kernelNetwork.h"
#include "kernelUsbDriver.h"
#include "kernelUser.h"
#include "kernelWindow.h"
#include "kernelError.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>


static void writeLoaderLog(unsigned char *screen, int chars, int bytesPerChar)
{
  // Write the saved screen contents to a 'loader log'

  char *buffer = NULL;
  fileStream tmpFile;
  int count;

  buffer = kernelMalloc(chars);
  if (buffer == NULL)
    return;

  for (count = 0; count < chars; count ++)
    buffer[count] = screen[count * bytesPerChar];

  if (kernelFileStreamOpen("/system/vloader.log",
			   (OPENMODE_WRITE | OPENMODE_CREATE |
			    OPENMODE_TRUNCATE), &tmpFile) < 0)
    {
      kernelFree(buffer);
      return;
    }

  kernelFileStreamWrite(&tmpFile, chars, buffer);
  kernelFileStreamClose(&tmpFile);
  kernelFree(buffer);
  return;
}


/////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////
//
// Below here, the functions are exported for external use
//
/////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////


int kernelInitialize(unsigned kernelMemory)
{
  // Does a bunch of calls involved in initializing the kernel.
  // kernelMain passes all of the kernel's arguments to this function
  // for processing.  Returns 0 if successful, negative on error.

  int status;
  textScreen screen;
  int graphics = 0;
  char welcomeMessage[512];
  static char rootDiskName[DISK_MAX_NAMELENGTH];
  kernelDisk *rootDisk = NULL;
  char value[128];
  char splashName[128];
  image splashImage;
  int networking = 0;
  int count;

  extern char *kernelVersion[];

  // The kernel config file.  We read it later on in this function
  extern variableList *kernelVariables;

  // The default colors
  extern color kernelDefaultForeground;
  extern color kernelDefaultBackground;
  extern color kernelDefaultDesktop;

  // Initialize the page manager
  status = kernelPageInitialize(kernelMemory);
  if (status < 0)
    return (status);

  // Initialize the memory manager
  status = kernelMemoryInitialize(kernelMemory);
  if (status < 0)
    return (status);

  // Initialize the descriptor tables (GDT and IDT)
  status = kernelDescriptorInitialize();
  if (status < 0)
    {
      kernelError(kernel_error, "Descriptor table initialization failed");
      return (status);
    }

  // Do device initialization
  status = kernelDeviceInitialize();
  if (status < 0)
    {
      kernelError(kernel_error, "Hardware initialization failed");
      return (status);
    }

  // Initialize the interrupt vector tables and default handlers.  Note
  // that interrupts are not enabled here; that is done during hardware
  // enumeration after the Programmable Interrupt Controller has been
  // set up.
  status = kernelInterruptInitialize();
  if (status < 0)
    {
      kernelError(kernel_error, "Interrupt vector initialization failed");
      return (status);
    }

  // Initialize the text screen output.  This needs to be done after paging
  // has been initialized so that our screen memory can be mapped to a
  // virtual memory address.
  status = kernelTextInitialize(80, 50);
  if (status < 0)
    return (status);

  // Initialize debugging output
  kernelDebugInitialize();

  // Save the current screen
  kernelTextScreenSave(&screen);

  // Do display device detection
  status = kernelDeviceDetectDisplay();
  if (status < 0)
    {
      kernelError(kernel_error, "Display initialization failed");
      return (status);
    }

  // Clear the screen
  kernelTextScreenClear();

  // Initialize kernel logging
  status = kernelLogInitialize();
  if (status < 0)
    {
      kernelTextPrintLine("Logging initialization failed");
      return (status);
    }

  // Disable console logging by default, since it fills up the screen
  // with unnecessary details.
  kernelLogSetToConsole(0);

  // Log a starting message
  sprintf(welcomeMessage, "%s %s\nCopyright (C) 1998-2006 J. Andrew "
	  "McLaughlin", kernelVersion[0], kernelVersion[1]);
  kernelLog(welcomeMessage);

  // Print the welcome message.
  kernelTextPrintLine("%s\nStarting, one moment please...", welcomeMessage);

  // Do general device detection
  status = kernelDeviceDetect();
  if (status < 0)
    {
      kernelError(kernel_error, "Hardware detection failed");
      return (status);
    }

  // Initialize the multitasker
  status = kernelMultitaskerInitialize();
  if (status < 0)
    {
      kernelError(kernel_error, "Multitasker initialization failed");
      return (status);
    }

  // Initialize USB bus functions
  status = kernelUsbInitialize();
  if (status < 0)
    {
      kernelError(kernel_error, "USB initialization failed");
      return (status);
    }

  // Initialize the kernel's random number generator.
  // has been initialized.
  status = kernelRandomInitialize();
  if (status < 0)
    {
      kernelError(kernel_error, "Random number initialization failed");
      return (status);
    }

  // Initialize the filesystem drivers
  status = kernelFilesystemDriversInitialize();
  if (status < 0)
    {
      kernelError(kernel_error, "Filesystem drivers initialization failed");
      return (status);
    }

  // Initialize the disk functions.  This must be done AFTER the hardware
  // has been enumerated, and AFTER the drivers have been installed.
  status = kernelDiskInitialize();
  if (status < 0)
    {
      kernelError(kernel_error, "Disk functions initialization failed");
      return (status);
    }

  // Get the name of the boot disk
  status = kernelDiskGetBoot(rootDiskName);
  kernelDebug(debug_misc, "Rootdisk name %s", rootDiskName);
  if (status < 0)
    {
      kernelError(kernel_error, "Unable to determine boot device");
      return (status);
    }

  // Initialize the file management
  status = kernelFileInitialize();
  if (status < 0)
    {
      kernelError(kernel_error, "Files functions initialization failed");
      return (status);
    }

  // Mount the root filesystem.
  // ,NULL added by Davide Airaghi
  status = kernelFilesystemMount(rootDiskName, "/",NULL);
  if (status < 0)
    {
      // If it's a CD, it might be some other than cd0.  Our boot loader can't
      // distinguish which one it is.  Try other possibilities.
      if (!strcmp(rootDiskName, "cd0"))
	{
	  for (count = 1; count < MAXHARDDISKS; count ++)
	    {
	      sprintf(rootDiskName, "cd%d", count);
	      if (kernelDiskGetByName(rootDiskName))
		{
		  // ,NULL added by Davide Airaghi
		  status = kernelFilesystemMount(rootDiskName, "/",NULL);
		  if (status == 0)
		    break;
		}
	    }
	}

      if (status < 0)
	{
	  kernelError(kernel_error, "Mounting root filesystem failed");
	  return (status = ERR_NOTINITIALIZED);
	}
    }
  kernelDebug(debug_misc, "Mounted rootdisk %s", rootDiskName);

  rootDisk = kernelDiskGetByName(rootDiskName);
  if (rootDisk == NULL)
    {
      kernelError(kernel_error, "Couldn't get root disk \"%s\"", rootDiskName);
      return (status = ERR_INVALID);
    }

  // Are we in a graphics mode?
  graphics = kernelGraphicsAreEnabled();

  // Read the kernel config file
  status = kernelConfigurationReader(DEFAULT_KERNEL_CONFIG, kernelVariables);
  if (status < 0)
    kernelVariables = NULL;

  if (kernelVariables != NULL)
    {
      // Get the keyboard mapping
      kernelVariableListGet(kernelVariables, "keyboard.map", value, 128);
      if (value[0] != '\0')
	kernelKeyboardSetMap(value);

      if (graphics)
	{
	  // Get the default color values, if they're set in this file
	  kernelVariableListGet(kernelVariables, "foreground.color.red",
				value, 128);
	  if (value[0] != '\0')
	    kernelDefaultForeground.red = atoi(value);
	  kernelVariableListGet(kernelVariables, "foreground.color.green",
				value, 128);
	  if (value[0] != '\0')
	    kernelDefaultForeground.green = atoi(value);
	  kernelVariableListGet(kernelVariables, "foreground.color.blue",
				value, 128);
	  if (value[0] != '\0')
	    kernelDefaultForeground.blue = atoi(value);


          kernelVariableListGet(kernelVariables, "background.color.red",
				value, 128);
          if (value[0] != '\0')
            kernelDefaultBackground.red = atoi(value);
          kernelVariableListGet(kernelVariables, "background.color.green",
                                value, 128);
          if (value[0] != '\0')
            kernelDefaultBackground.green = atoi(value);
          kernelVariableListGet(kernelVariables, "background.color.blue",
                                value, 128);
          if (value[0] != '\0')
            kernelDefaultBackground.blue = atoi(value);

          kernelVariableListGet(kernelVariables, "desktop.color.red",
				value, 128);
          if (value[0] != '\0')
            kernelDefaultDesktop.red = atoi(value);
          kernelVariableListGet(kernelVariables, "desktop.color.green",
                                value, 128);
          if (value[0] != '\0')
            kernelDefaultDesktop.green = atoi(value);
          kernelVariableListGet(kernelVariables, "desktop.color.blue",
                                value, 128);
          if (value[0] != '\0')
            kernelDefaultDesktop.blue = atoi(value);

	  // Get the name of the splash image (used only if we are in
	  // graphics mode)
	  kernelMemClear(splashName, 128);
	  kernelVariableListGet(kernelVariables, "splash.image", splashName,
				128);
	}

      kernelVariableListGet(kernelVariables, "network", value, 128);
      if (!strncmp(value, "yes", 128))
	networking = 1;
    }

  if (graphics)
    {
      // Clear the screen with our default background color
      kernelGraphicClearScreen(&kernelDefaultDesktop);

      if (splashName[0] != '\0')
	{
	  // Try to load the default splash image to use when starting/
	  // restarting
	  kernelMemClear(&splashImage, sizeof(image));
	  kernelImageLoad(splashName, 0, 0, &splashImage);
	  if (splashImage.data)
	    // Loaded successfully.  Put it in the middle of the screen.
	    kernelGraphicDrawImage(NULL, &splashImage, draw_normal, 
				   ((kernelGraphicGetScreenWidth() -
				     splashImage.width) / 2),
				   ((kernelGraphicGetScreenHeight() -
				     splashImage.height) / 2), 0, 0, 0, 0);
	}

      // Initialize mouse operations
      status = kernelMouseInitialize();
      if (status < 0)
	kernelError(kernel_warn, "Mouse initialization failed");
    }

  // If the filesystem is not read-only, open a kernel log file
  if (!(rootDisk->filesystem.readOnly))
    {
      status = kernelLogSetFile(DEFAULT_LOGFILE);
      if (status < 0)
	// Make a warning, but don't return error.  This is not fatal.
	kernelError(kernel_warn, "Unable to open the kernel log file");

      // Write the saved screen contents to a "boot log"
      writeLoaderLog(screen.data, (80 * 50),
		     kernelTextGetConsoleOutput()->textArea->bytesPerChar);
    }
  if (screen.data)
    kernelMemoryRelease(screen.data);

  // Read the kernel's symbols from the kernel symbols file, if possible
  kernelReadSymbols(KERNEL_SYMBOLS_FILE);

  // Initialize network functions?
  if (networking)
    {
      status = kernelNetworkInitialize();
      if (status < 0)
	{
	  kernelError(kernel_error, "Network initialization failed");
	  return (status = ERR_NOTINITIALIZED);
	}
    }

  // Initialize user functions
  status = kernelUserInitialize();
  if (status < 0)
    {
      kernelError(kernel_error, "User functions initialization failed");
      return (status = ERR_NOTINITIALIZED);
    }

  // Start the window management system.  Don't bother checking for an
  // error code.
  if (graphics)
    {
      status = kernelWindowInitialize();
      if (status < 0)
	// Make a warning, but don't return error.  This is not fatal.
	kernelError(kernel_warn, "Unable to start the window manager");

      // Clear the screen with our default background color
      kernelGraphicClearScreen(&kernelDefaultDesktop);
      if (splashImage.data)
	kernelMemoryRelease(splashImage.data);
    }
  else
    kernelTextPrint("\nGraphics are not enabled.  Operating in text mode.\n");

  // Done setup.  Return success.
  return (status = 0);
}
