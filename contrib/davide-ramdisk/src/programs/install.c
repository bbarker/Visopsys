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
//  install.c
//

// This is a program for installing the system on a target disk (filesystem).

/* This is the text that appears when a user requests help about this program
<help>

 -- install --

This program will install a copy of Visopsys on another disk.

Usage:
  install [-T] [disk_name]

The 'install' program is interactive, but a logical disk parameter can
(optionally) be specified on the command line.  If no disk is specified,
then the user will be prompted to choose from a menu.  Use the 'disks'
command to list the available disks.

Options:
-T  : Force text mode operation

</help>
*/

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/api.h>
#include <sys/vsh.h>
#include <sys/cdefs.h>

#define MOUNTPOINT               "/tmp_install"
#define BASICINSTALL             "/system/install-files.basic"
#define FULLINSTALL              "/system/install-files.full"

typedef enum { install_basic, install_full } install_type;

static int processId = 0;
static char rootDisk[DISK_MAX_NAMELENGTH];
static int numberDisks = 0;
static disk diskInfo[DISK_MAXDEVICES];
static char *diskName = NULL;
static char *titleString = "Visopsys Installer\nCopyright (C) 1998-2006 "
                           "J. Andrew McLaughlin";
static char *chooseVolumeString = "Please choose the volume on which to "
  "install:";
static char *setPasswordString = "Please choose a password for the 'admin' "
                                 "account";
static char *partitionString = "Partition disks...";
static char *cancelString = "Installation cancelled.";
static install_type installType;
static unsigned bytesToCopy = 0;
static unsigned bytesCopied = 0;
static progress prog;
static int doFormat = 1;
static int chooseFsType = 0;
static char formatFsType[16];
static textScreen screen;

// GUI stuff
static int graphics = 0;
static objectKey window = NULL;
static objectKey installTypeRadio = NULL;
static objectKey formatCheckbox = NULL;
static objectKey fsTypeCheckbox = NULL;
static objectKey statusLabel = NULL;
static objectKey progressBar = NULL;
static objectKey installButton = NULL;
static objectKey quitButton = NULL;


static void pause(void)
{
  printf("\nPress any key to continue. ");
  getchar();
  printf("\n");
}


static void error(const char *format, ...)
{
  // Generic error message code for either text or graphics modes
  
  va_list list;
  char output[MAXSTRINGLENGTH];
  
  va_start(list, format);
  _expandFormatString(output, MAXSTRINGLENGTH, format, list);
  va_end(list);

  if (graphics)
    windowNewErrorDialog(window, "Error", output);
  else
    printf("\n\nERROR: %s\n\n", output);
}


static void quit(int status, const char *message, ...)
{
  // Shut everything down

  va_list list;
  char output[MAXSTRINGLENGTH];
  
  if (message != NULL)
    {
      va_start(list, message);
      _expandFormatString(output, MAXSTRINGLENGTH, message, list);
      va_end(list);
    }

  if (graphics)
    windowGuiStop();
  else
    textScreenRestore(&screen);

  if (message != NULL)
    {
      if (status < 0)
	error("%s  Quitting.", output);
      else
	{
	  if (graphics)
	    windowNewInfoDialog(window, "Complete", output);
	  else
	    printf("\n%s\n", output);
	}
    }

  if (graphics && window)
    windowDestroy(window);

  errno = status;

  if (screen.data)
    memoryRelease(screen.data);

  exit(status);
}


static void makeDiskList(void)
{
  // Make a list of disks on which we can install

  int status = 0;
  // Call the kernel to give us the number of available disks
  int tmpNumberDisks = diskGetCount();
  disk tmpDiskInfo[DISK_MAXDEVICES];
  int count;

  numberDisks = 0;
  bzero(diskInfo, (DISK_MAXDEVICES * sizeof(disk)));
  bzero(tmpDiskInfo, (DISK_MAXDEVICES * sizeof(disk)));

  status = diskGetAll(tmpDiskInfo, (DISK_MAXDEVICES * sizeof(disk)));
  if (status < 0)
    // Eek.  Problem getting disk info
    quit(status, "Unable to get disk information.");

  // Loop through the list we got.  Copy any valid disks (disks to which
  // we might be able to install) into the main array
  for (count = 0; count < tmpNumberDisks; count ++)
    {
      // Make sure it's not the root disk; that would be pointless and possibly
      // dangerous
      if (!strcmp(rootDisk, tmpDiskInfo[count].name))
	continue;

      // Skip CD-ROMS
      if (tmpDiskInfo[count].flags & DISKFLAG_CDROM)
	continue;

      // Otherwise, we will put this in the list
      memcpy(&(diskInfo[numberDisks]), &(tmpDiskInfo[count]), sizeof(disk));
      numberDisks += 1;
    }
}


static void eventHandler(objectKey key, windowEvent *event)
{
  // Check for the window being closed, or pressing of the 'Quit' button.
  if (((key == window) && (event->type == EVENT_WINDOW_CLOSE)) ||
      ((key == quitButton) && (event->type == EVENT_MOUSE_LEFTUP)))
    quit(0, NULL);

  else if ((key == formatCheckbox) && (event->type & EVENT_SELECTION))
    {
      windowComponentGetSelected(formatCheckbox, &doFormat);
      if (!doFormat)
	windowComponentSetSelected(fsTypeCheckbox, 0);
      windowComponentSetEnabled(fsTypeCheckbox, doFormat);
    }

  else if ((key == fsTypeCheckbox) && (event->type & EVENT_SELECTION))
    windowComponentGetSelected(fsTypeCheckbox, &chooseFsType);

  // Check for the 'Install' button
  else if ((key == installButton) && (event->type == EVENT_MOUSE_LEFTUP))
    // Stop the GUI here and the installation will commence
    windowGuiStop();
}


static void constructWindow(void)
{
  // If we are in graphics mode, make a window rather than operating on the
  // command line.

  componentParameters params;
  objectKey textLabel = NULL;
  char tmp[40];

  // Create a new window, with small, arbitrary size and location
  window = windowNew(processId, "Install");
  if (window == NULL)
    quit(ERR_NOCREATE, "Can't create window!");

  bzero(&params, sizeof(componentParameters));
  params.gridWidth = 2;
  params.gridHeight = 1;
  params.padTop = 5;
  params.padLeft = 5;
  params.padRight = 5;
  params.orientationX = orient_left;
  params.orientationY = orient_middle;
  textLabel = windowNewTextLabel(window, titleString, &params);
  
  params.gridY++;
  sprintf(tmp, "[ Installing on disk %s ]", diskName);
  windowNewTextLabel(window, tmp, &params);

  params.gridY++;
  installTypeRadio = windowNewRadioButton(window, 2, 1, (char *[])
      { "Basic install", "Full install" }, 2 , &params);
  windowComponentSetEnabled(installTypeRadio, 0);

  params.gridY++;
  sprintf(tmp, "Format %s (erases all data!)", diskName);
  formatCheckbox = windowNewCheckbox(window, tmp, &params);
  windowComponentSetSelected(formatCheckbox, 1);
  windowComponentSetEnabled(formatCheckbox, 0);
  windowRegisterEventHandler(formatCheckbox, &eventHandler);

  params.gridY++;
  fsTypeCheckbox =
    windowNewCheckbox(window, "Choose filesystem type", &params);
  windowComponentSetEnabled(fsTypeCheckbox, 0);
  windowRegisterEventHandler(fsTypeCheckbox, &eventHandler);

  params.gridY++;
  statusLabel = windowNewTextLabel(window, "", &params);
  windowComponentSetWidth(statusLabel, windowComponentGetWidth(textLabel));

  params.gridY++;
  progressBar = windowNewProgressBar(window, &params);

  params.gridY++;
  params.gridWidth = 1;
  params.padBottom = 5;
  params.orientationX = orient_right;
  params.flags |= WINDOW_COMPFLAG_FIXEDWIDTH;
  installButton = windowNewButton(window, "Install", NULL, &params);
  windowRegisterEventHandler(installButton, &eventHandler);
  windowComponentSetEnabled(installButton, 0);

  params.gridX++;
  params.orientationX = orient_left;
  quitButton = windowNewButton(window, "Quit", NULL, &params);
  windowRegisterEventHandler(quitButton, &eventHandler);
  windowComponentSetEnabled(quitButton, 0);

  // Register an event handler to catch window close events
  windowRegisterEventHandler(window, &eventHandler);

  // Go
  windowSetVisible(window, 1);
}


static void printBanner(void)
{
  // Print a message
  textScreenClear();
  printf("\n%s\n\n", titleString);
}


static int yesOrNo(char *question)
{
  char character;

  if (graphics)
    return (windowNewQueryDialog(window, "Confirmation", question));

  else
    {
      printf("\n%s (y/n): ", question);
      textInputSetEcho(0);

      while(1)
	{
	  character = getchar();

	  if ((character == 'y') || (character == 'Y'))
	    {
	      printf("Yes\n");
	      textInputSetEcho(1);
	      return (1);
	    }
	  else if ((character == 'n') || (character == 'N'))
	    {
	      printf("No\n");
	      textInputSetEcho(1);
	      return (0);
	    }
	}
    }
}


static int chooseDisk(void)
{
  // This is where the user chooses the disk on which to install

  int status = 0;
  int diskNumber = -1;
  objectKey chooseWindow = NULL;
  componentParameters params;
  objectKey diskList = NULL;
  objectKey okButton = NULL;
  objectKey partButton = NULL;
  objectKey cancelButton = NULL;
  listItemParameters diskListParams[DISK_MAXDEVICES];
  char *diskStrings[DISK_MAXDEVICES];
  windowEvent event;
  int count;

  // We jump back to this position if the user repartitions the disks
 start:

  bzero(&params, sizeof(componentParameters));
  params.gridX = 0;
  params.gridY = 0;
  params.gridWidth = 3;
  params.gridHeight = 1;
  params.padTop = 5;
  params.padLeft = 5;
  params.padRight = 5;
  params.orientationX = orient_center;
  params.orientationY = orient_middle;

  bzero(diskListParams, (numberDisks * sizeof(listItemParameters)));
  for (count = 0; count < numberDisks; count ++)
    snprintf(diskListParams[count].text, WINDOW_MAX_LABEL_LENGTH, "%s  [ %s ]",
	     diskInfo[count].name, diskInfo[count].partType.description);

  if (graphics)
    {
      chooseWindow = windowNew(processId, "Choose Installation Disk");
      windowNewTextLabel(chooseWindow, chooseVolumeString, &params);

      // Make a window list with all the disk choices
      params.gridY = 1;
      diskList = windowNewList(chooseWindow, windowlist_textonly, 5, 1, 0,
			       diskListParams, numberDisks, &params);

      // Make 'OK', 'partition', and 'cancel' buttons
      params.gridY = 2;
      params.gridWidth = 1;
      params.padBottom = 5;
      params.padRight = 0;
      params.orientationX = orient_right;
      okButton = windowNewButton(chooseWindow, "OK", NULL, &params);

      params.gridX = 1;
      params.padRight = 5;
      params.orientationX = orient_center;
      partButton = windowNewButton(chooseWindow, partitionString, NULL,
				   &params);

      params.gridX = 2;
      params.padLeft = 0;
      params.orientationX = orient_left;
      cancelButton = windowNewButton(chooseWindow, "Cancel", NULL, &params);

      // Make the window visible
      windowRemoveMinimizeButton(chooseWindow);
      windowRemoveCloseButton(chooseWindow);
      windowSetResizable(chooseWindow, 0);
      windowSetVisible(chooseWindow, 1);

      while(1)
	{
	  // Check for our OK button
	  status = windowComponentEventGet(okButton, &event);
	  if ((status < 0) || ((status > 0) &&
	      (event.type == EVENT_MOUSE_LEFTUP)))
	    {
	      windowComponentGetSelected(diskList, &diskNumber);
	      break;
	    }

	  // Check for our 'partition' button
	  status = windowComponentEventGet(partButton, &event);
	  if ((status < 0) || ((status > 0) &&
	      (event.type == EVENT_MOUSE_LEFTUP)))
	    {
	      // The user wants to repartition the disks.  Get rid of this
	      // window, run the disk manager, and start again
	      windowDestroy(chooseWindow);
	      chooseWindow = NULL;

	      // Privilege zero, no args, block
	      loaderLoadAndExec("/programs/fdisk", 0, 1);

	      // Remake our disk list
	      makeDiskList();

	      // Start again.
	      goto start;
	    }

	  // Check for our Cancel button
	  status = windowComponentEventGet(cancelButton, &event);
	  if ((status < 0) || ((status > 0) &&
	      (event.type == EVENT_MOUSE_LEFTUP)))
	    break;

	  // Done
	  multitaskerYield();
	}

      windowDestroy(chooseWindow);
      chooseWindow = NULL;
    }

  else
    {
      for (count = 0; count < numberDisks; count ++)
	diskStrings[count] = diskListParams[count].text;
      diskStrings[numberDisks] = partitionString;
      diskNumber =
	vshCursorMenu(chooseVolumeString, diskStrings, (numberDisks + 1), 0);
      if (diskNumber == numberDisks)
	{
	  // The user wants to repartition the disks.  Run the disk
	  // manager, and start again

	  // Privilege zero, no args, block
	  loaderLoadAndExec("/programs/fdisk", 0, 1);

	  // Remake our disk list
	  makeDiskList();

	  // Start again.
	  printBanner();
	  goto start;
	}
    }

  return (diskNumber);
}


static unsigned getInstallSize(const char *installFileName)
{
  // Given the name of an install file, calculate the number of bytes
  // of disk space the installation will require.

  #define BUFFSIZE 160

  int status = 0;
  fileStream installFile;
  file theFile;
  char buffer[BUFFSIZE];
  unsigned bytes = 0;

  // Clear stack data
  bzero(&installFile, sizeof(fileStream));
  bzero(&theFile, sizeof(file));
  bzero(buffer, BUFFSIZE);

  // See if the install file exists
  status = fileFind(installFileName, &theFile);
  if (status < 0)
    // Doesn't exist
    return (bytes = 0);

  // Open the install file
  status = fileStreamOpen(installFileName, OPENMODE_READ, &installFile);
  if (status < 0)
    // Can't open the install file.
    return (bytes = 0);

  // Read it line by line
  while (1)
    {
      status = fileStreamReadLine(&installFile, BUFFSIZE, buffer);
      if (status < 0)
	{
	  error("Error reading from install file \"%s\"", installFileName);
	  fileStreamClose(&installFile);
	  return (bytes = 0);
	}

      else if ((buffer[0] == '\n') || (buffer[0] == '#'))
	// Ignore blank lines and comments
	continue;

      else if (status == 0)
	{
	  // End of file
	  fileStreamClose(&installFile);
	  break;
	}

      else
	{
	  // If there's a newline at the end of the line, remove it
	  if (buffer[strlen(buffer) - 1] == '\n')
	    buffer[strlen(buffer) - 1] = '\0';

	  // Use the line of data as the name of a file.  We try to find the
	  // file and add its size to the number of bytes
	  status = fileFind(buffer, &theFile);
	  if (status < 0)
	    {
	      error("Can't open source file \"%s\"", buffer);
	      continue;
	    }

	  bytes += theFile.size;
	}
    }

  // Add 1K for a little buffer space
  return (bytes + 1024);
}


static int askFsType(void)
{
  // Query the user for the FAT filesystem type

  int status = 0;
  int selectedType = 0;
  char *fsTypes[] = { "Default", "FAT12", "FAT16", "FAT32" };

  if (graphics)
    selectedType = windowNewRadioDialog(window, "Choose Filesystem Type",
					"Supported types:", fsTypes, 4, 0);
  else
    selectedType = vshCursorMenu("Choose the filesystem type:", fsTypes, 4, 0);

  if (selectedType < 0)
    return (status = selectedType);

  if (!strcasecmp(fsTypes[selectedType], "Default"))
    strcpy(formatFsType, "fat");
  else
    strcpy(formatFsType, fsTypes[selectedType]);

  return (status = 0);
}


static void updateStatus(const char *message)
{
  // Updates progress messages.
  
  int statusLength = 0;

  if (lockGet(&(prog.lock)) >= 0)
    {
      if (strlen(prog.statusMessage) &&
	  (prog.statusMessage[strlen(prog.statusMessage) - 1] != '\n'))
	strcat(prog.statusMessage, message);
      else
	strcpy(prog.statusMessage, message);

      statusLength = strlen(prog.statusMessage);
      if (statusLength >= PROGRESS_MAX_MESSAGELEN)
	{
	  statusLength = (PROGRESS_MAX_MESSAGELEN - 1);
	  prog.statusMessage[statusLength] = '\0';
	}
      if (prog.statusMessage[statusLength - 1] == '\n')
	statusLength -= 1;

      if (graphics)
	windowComponentSetData(statusLabel, prog.statusMessage, statusLength);

      lockRelease(&(prog.lock));
    }
}


static int mountedCheck(disk *theDisk)
{
  // If the disk is mounted, ask if we can unmount it

  int status = 0;
  char tmpChar[160];

  if (!(theDisk->mounted))
    return (status = 0);

  sprintf(tmpChar, "The disk is mounted as %s.  It must be unmounted\n"
	  "before continuing.  Unmount?",
	  theDisk->mountPoint);

  if (!yesOrNo(tmpChar))
    return (status = ERR_CANCELLED);

  // Try to unmount the filesystem
  status = filesystemUnmount(theDisk->mountPoint);
  if (status < 0)
    {
      error("Unable to unmount %s", theDisk->mountPoint);
      return (status);
    }
  
  return (status = 0);
}


static int copyBootSector(disk *theDisk)
{
  // Overlay the boot sector from the root disk onto the boot sector of
  // the target disk

  int status = 0;
  char bootSectFilename[MAX_PATH_NAME_LENGTH];
  char command[MAX_PATH_NAME_LENGTH];
  file bootSectFile;

  updateStatus("Copying boot sector...  ");

  // Make sure we know the filesystem type
  status = diskGetFilesystemType(theDisk->name, theDisk->fsType,
				 FSTYPE_MAX_NAMELENGTH);
  if (status < 0)
    {
      error("Unable to determine the filesystem type on disk \"%s\"",
	    theDisk->name);
      return (status);
    }

  // Determine which boot sector we should be using
  if (strncmp(theDisk->fsType, "fat", 3))
    {
      error("Can't install a boot sector for filesystem type \"%s\"",
	    theDisk->fsType);
      return (status = ERR_INVALID);
    }

  strcpy(bootSectFilename, "/system/boot/bootsect.fat");
  if (!strcmp(theDisk->fsType, "fat32"))
    strcat(bootSectFilename, "32");

  // Find the boot sector
  status = fileFind(bootSectFilename, &bootSectFile);
  if (status < 0)
    {
      error("Unable to find the boot sector file \"%s\"", bootSectFilename);
      return (status);
    }

  // Use our companion program to do the work
  sprintf(command, "/programs/copy-boot %s %s", bootSectFilename,
	  theDisk->name);
  status = system(command);

  diskSync();

  if (status < 0)
    {
      error("Error %d copying boot sector \"%s\" to disk %s", status,
	    bootSectFilename, theDisk->name);
      return (status);
    }

  updateStatus("Done\n");

  return (status = 0);
}


static int copyFiles(const char *installFileName)
{
  #define BUFFSIZE 160

  int status = 0;
  fileStream installFile;
  file theFile;
  int percent = 0;
  char buffer[BUFFSIZE];
  char tmpFileName[128];
  file tmpFile;

  // Clear stack data
  bzero(&installFile, sizeof(fileStream));
  bzero(&theFile, sizeof(file));
  bzero(buffer, BUFFSIZE);
  bzero(tmpFileName, 128);
  bzero(&tmpFile, sizeof(file));

  // Open the install file
  status = fileStreamOpen(installFileName, OPENMODE_READ, &installFile);
  if (status < 0)
    {
      error("Can't open install file \"%s\"",  installFileName);
      return (status);
    }

  sprintf(buffer, "Copying %s files...  ",
	  ((installFileName == BASICINSTALL)? "basic" : "extra"));
  updateStatus(buffer);

  // Read it line by line
  while (1)
    {
      status = fileStreamReadLine(&installFile, BUFFSIZE, buffer);
      if (status < 0)
	{
	  fileStreamClose(&installFile);
	  error("Error reading from install file \"%s\"", installFileName);
	  goto done;
	}

      else if ((buffer[0] == '\n') || (buffer[0] == '#'))
	// Ignore blank lines and comments
	continue;

      else if (status == 0)
	{
	  // End of file
	  fileStreamClose(&installFile);
	  break;
	}

      else
	{
	  // Use the line of data as the name of a file.  We try to find the
	  // file and add its size to the number of bytes
	  status = fileFind(buffer, &theFile);
	  if (status < 0)
	    {
	      // Later we should do something here to make a message listing
	      // the names of any missing files
	      error("Missing file \"%s\"", buffer);
	      continue;
	    }

	  strcpy(tmpFileName, MOUNTPOINT);
	  strcat(tmpFileName, buffer);

	  if (theFile.type == dirT)
	    {
	      // It's a directory, create it in the desination
	      if (fileFind(tmpFileName, &tmpFile) < 0)
		status = fileMakeDir(tmpFileName);
	    }

	  else
	    // It's a file.  Copy it to the destination.
	    status = fileCopy(buffer, tmpFileName);

	  if (status < 0)
	    goto done;

	  bytesCopied += theFile.size;

	  percent = ((bytesCopied * 100) / bytesToCopy);

	  // Sync periodially
	  if (!(percent % 10))
	    diskSync();

	  if (graphics)
	    windowComponentSetData(progressBar, (void *) percent, 1);
	  else if (lockGet(&(prog.lock)) >= 0)
	    {
	      prog.percentFinished = percent;
	      lockRelease(&(prog.lock));
	    }
	}
    }

  status = 0;

 done:
  diskSync();

  updateStatus("Done\n");

  return (status);
}


static void setAdminPassword(void)
{
  // Show a 'set password' dialog box for the admin user

  int status = 0;
  objectKey dialogWindow = NULL;
  componentParameters params;
  objectKey label = NULL;
  objectKey passwordField1 = NULL;
  objectKey passwordField2 = NULL;
  objectKey noMatchLabel = NULL;
  objectKey okButton = NULL;
  objectKey cancelButton = NULL;
  windowEvent event;
  char confirmPassword[17];
  char newPassword[17];

  if (graphics)
    {
      // Create the dialog
      dialogWindow = windowNewDialog(window, "Set Administrator Password");

      bzero(&params, sizeof(componentParameters));
      params.gridWidth = 2;
      params.gridHeight = 1;
      params.padLeft = 5;
      params.padRight = 5;
      params.padTop = 5;
      params.orientationX = orient_center;
      params.orientationY = orient_middle;
      label = windowNewTextLabel(dialogWindow, setPasswordString, &params);
	  
      params.gridY = 1;
      params.gridWidth = 1;
      params.padRight = 0;
      params.orientationX = orient_right;
      label = windowNewTextLabel(dialogWindow, "New password:", &params);

      params.gridX = 1;
      params.flags |= WINDOW_COMPFLAG_HASBORDER;
      params.padRight = 5;
      params.orientationX = orient_left;
      passwordField1 = windowNewPasswordField(dialogWindow, 17, &params);
      
      params.gridX = 0;
      params.gridY = 2;
      params.padRight = 0;
      params.orientationX = orient_right;
      params.flags &= ~WINDOW_COMPFLAG_HASBORDER;
      label = windowNewTextLabel(dialogWindow, "Confirm password:", &params);

      params.gridX = 1;
      params.orientationX = orient_left;
      params.padRight = 5;
      params.flags |= WINDOW_COMPFLAG_HASBORDER;
      passwordField2 = windowNewPasswordField(dialogWindow, 17, &params);
	  
      params.gridX = 0;
      params.gridY = 3;
      params.gridWidth = 2;
      params.orientationX = orient_center;
      params.flags &= ~WINDOW_COMPFLAG_HASBORDER;
      noMatchLabel = windowNewTextLabel(dialogWindow, "Passwords do not "
					"match", &params);
      windowComponentSetVisible(noMatchLabel, 0);
      
      // Create the OK button
      params.gridY = 4;
      params.gridWidth = 1;
      params.padBottom = 5;
      params.padRight = 0;
      params.orientationX = orient_right;
      okButton = windowNewButton(dialogWindow, "OK", NULL, &params);
  
      // Create the Cancel button
      params.gridX = 1;
      params.padRight = 5;
      params.orientationX = orient_left;
      cancelButton = windowNewButton(dialogWindow, "Cancel", NULL, &params);

      windowCenterDialog(window, dialogWindow);
      windowSetVisible(dialogWindow, 1);

    graphicsRestart:
      while(1)
	{
	  // Check for window close events
	  status = windowComponentEventGet(dialogWindow, &event);
	  if ((status < 0) || ((status > 0) &&
			       (event.type == EVENT_WINDOW_CLOSE)))
	    {
	      error("No password set.  It will be blank.");
	      windowDestroy(dialogWindow);
	      return;
	    }
	  
	  // Check for the OK button
	  status = windowComponentEventGet(okButton, &event);
	  if ((status > 0) && (event.type == EVENT_MOUSE_LEFTUP))
	    break;
	  
	  // Check for the Cancel button
	  status = windowComponentEventGet(cancelButton, &event);
	  if ((status < 0) || ((status > 0) &&
	      (event.type == EVENT_MOUSE_LEFTUP)))
	    {
	      error("No password set.  It will be blank.");
	      windowDestroy(dialogWindow);
	      return;
	    }
	  
	  if ((windowComponentEventGet(passwordField1, &event) &&
	       (event.type == EVENT_KEY_DOWN)) ||
	      (windowComponentEventGet(passwordField2, &event) &&
	       (event.type == EVENT_KEY_DOWN)))
	    {
	      if (event.key == (unsigned char) 10)
		break;
	      else
		{
		  windowComponentGetData(passwordField1, newPassword, 16);
		  windowComponentGetData(passwordField2, confirmPassword, 16);
		  if (strncmp(newPassword, confirmPassword, 16))
		    {
		      windowComponentSetVisible(noMatchLabel, 1);
		      windowComponentSetEnabled(okButton, 0);
		    }
		  else
		    {
		      windowComponentSetVisible(noMatchLabel, 0);
		      windowComponentSetEnabled(okButton, 1);
		    }
		}
	    }
	  
	  // Done
	  multitaskerYield();
	}
      
      windowComponentGetData(passwordField1, newPassword, 16);
      windowComponentGetData(passwordField2, confirmPassword, 16);
    }
  else
    {
    textRestart:
      printf("\n%s\n", setPasswordString);

      // Turn keyboard echo off
      textInputSetEcho(0);
  
      vshPasswordPrompt("New password: ", newPassword);
      vshPasswordPrompt("Confirm password: ", confirmPassword);
    }

  // Make sure the new password and confirm passwords match
  if (strncmp(newPassword, confirmPassword, 16))
    {
      error("Passwords do not match");
      if (graphics)
	{
	  windowComponentSetData(passwordField1, "", 0);
	  windowComponentSetData(passwordField2, "", 0);
	  goto graphicsRestart;
	}
      else
	goto textRestart;
    }

  if (graphics)
    windowDestroy(dialogWindow);
  else
    printf("\n");

  // We have a password.  Copy the blank password file for the new system
  // password file
  status = fileCopy(MOUNTPOINT "/system/password.blank",
		    MOUNTPOINT "/system/password");
  if (status < 0)
    {
      error("Unable to create the password file");
      return;
    }

  status = userFileSetPassword(MOUNTPOINT "/system/password", "admin", "",
			       newPassword);
  if (status < 0)
    {
      error("Unable to set the \"admin\" password");
      return;
    }
  
  return;
}


static void changeStartProgram(void)
{
  // Change the target installation's start program to the login program

  variableList kernelConf;

  if (configurationReader(MOUNTPOINT "/system/config/kernel.conf",
			  &kernelConf) < 0)
    return;

  variableListSet(&kernelConf, "start.program", "/programs/login");
  configurationWriter(MOUNTPOINT "/system/config/kernel.conf", &kernelConf);
  variableListDestroy(&kernelConf);
}


int main(int argc, char *argv[])
{
  int status = 0;
  int diskNumber = -1;
  char tmpChar[80];
  unsigned diskSize = 0;
  unsigned basicInstallSize = 0xFFFFFFFF;
  unsigned fullInstallSize = 0xFFFFFFFF;
  const char *message = NULL;
  objectKey progressDialog = NULL;
  int selected = 0;
  int count;

  bzero((void *) &prog, sizeof(progress));

  processId = multitaskerGetCurrentProcessId();

  // Are graphics enabled?
  graphics = graphicsAreEnabled();

  if (getopt(argc, argv, "T") == 'T')
    // Force text mode
    graphics = 0;

  // Check privilege level
  if (multitaskerGetProcessPrivilege(processId) != 0)
    quit(ERR_PERMISSION, "You must be a privileged user to use this command."
	 "\n(Try logging in as user \"admin\").");

  // Get the root disk
  status = diskGetBoot(rootDisk);
  if (status < 0)
    // Couldn't get the root disk name
    quit(status, "Can't determine the root disk.");

  makeDiskList();

  if (!graphics)
    {
      textScreenSave(&screen);
      printBanner();
    }

  // The user can specify the disk name as an argument.  Try to see
  // whether they did so.
  if (argc > 1)
    {
      for (count = 0; count < numberDisks; count ++)
	if (!strcmp(diskInfo[count].name, argv[argc - 1]))
	  {
	    diskNumber = count;
	    break;
	  }
    }

  if (diskNumber < 0)
    // The user has not specified a disk number.  We need to display the
    // list of available disks and prompt them.
    diskNumber = chooseDisk();

  if (diskNumber < 0)
    quit(diskNumber, NULL);

  diskName = diskInfo[diskNumber].name;

  if (graphics)
    constructWindow();

  // Make sure the disk isn't mounted
  status = mountedCheck(&diskInfo[diskNumber]);
  if (status < 0)
    quit(0, cancelString);

  // Calculate the number of bytes that will be consumed by the various
  // types of install
  basicInstallSize = getInstallSize(BASICINSTALL);
  fullInstallSize = getInstallSize(FULLINSTALL);

  // How much space is available on the raw disk?
  diskSize =
    (diskInfo[diskNumber].numSectors * diskInfo[diskNumber].sectorSize);

  // Make sure there's at least room for a basic install
  if (diskSize < basicInstallSize)
    quit((status = ERR_NOFREE), "Disk %s is too small (%dK) to install "
	 "Visopsys\n(%dK required)", diskInfo[diskNumber].name,
	 (diskSize / 1024), (basicInstallSize / 1024));

  // Show basic/full install choices based on whether there's enough space
  // to do both
  if (fullInstallSize && ((basicInstallSize + fullInstallSize) < diskSize))
    {
      if (graphics)
	{
	  windowComponentSetSelected(installTypeRadio, 1);
	  windowComponentSetEnabled(installTypeRadio, 1);
	  windowComponentSetEnabled(formatCheckbox, 1);
	  windowComponentSetEnabled(fsTypeCheckbox, 1);
	}
    }

  // Wait for the user to select any options and commence the installation
  if (graphics)
    {
      // We're ready to go, enable the buttons
      windowComponentSetEnabled(installButton, 1);
      windowComponentSetEnabled(quitButton, 1);
      // Focus the 'install' button by default
      windowComponentFocus(installButton);

      windowGuiRun();

      // Disable some components which are no longer usable
      windowComponentSetEnabled(installButton, 0);
      windowComponentSetEnabled(quitButton, 0);
      windowComponentSetEnabled(installTypeRadio, 0);
      windowComponentSetEnabled(formatCheckbox, 0);
      windowComponentSetEnabled(fsTypeCheckbox, 0);
    }

  // Find out what type of installation to do
  installType = install_basic;
  if (graphics)
    {
      windowComponentGetSelected(installTypeRadio, &selected);
      if (selected == 1)
	installType = install_full;
    }
  else if (fullInstallSize &&
	   ((basicInstallSize + fullInstallSize) < diskSize))
    {
      status = vshCursorMenu("Please choose the install type:",
			     (char *[]) { "Basic", "Full" }, 2, 1);
      if (status < 0)
	{
	  textScreenRestore(&screen);
	  return (status);
	}

      if (status == 1)
	installType = install_full;
    }

  bytesToCopy = basicInstallSize;
  if (installType == install_full)
    bytesToCopy += fullInstallSize;

  sprintf(tmpChar, "Installing on disk %s.  Are you SURE?", diskName);
  if (!yesOrNo(tmpChar))
    quit(0, cancelString);

  // Default filesystem formatting type is optimal/default FAT.
  strcpy(formatFsType, "fat");

  // In text mode, ask whether to format
  if (!graphics)
    {
      sprintf(tmpChar, "Format disk %s? (erases all data!)", diskName);
      doFormat = yesOrNo(tmpChar);
    }

  if (doFormat)
    {
      if (!graphics || chooseFsType)
	if (askFsType() < 0)
	  quit(0, cancelString);

      updateStatus("Formatting... ");

      if (graphics)
	progressDialog = windowNewProgressDialog(NULL, "Formatting...", &prog);
      else
	{
	  printf("\nFormatting...\n");
	  vshProgressBar(&prog);
	}

      status = filesystemFormat(diskName, formatFsType, "Visopsys", 0, &prog);

      if (graphics)
	windowProgressDialogDestroy(progressDialog);
      else
	vshProgressBarDestroy(&prog);

      if (status < 0)
	quit(status, "Errors during format.");

      // Rescan the disk info so we get the new filesystem type, etc.
      status = diskGet(diskName, &diskInfo[diskNumber]);
      if (status < 0)
	quit(status, "Error rescanning disk after format.");

      updateStatus("Done\n");
      bzero((void *) &prog, sizeof(progress));
    }

  // Copy the boot sector to the destination disk
  status = copyBootSector(&diskInfo[diskNumber]);
  if (status < 0)
    quit(status, "Couldn't copy the boot sector.");

  // Mount the target filesystem
  updateStatus("Mounting target disk...  ");
  //,NULL added by Davide Airaghi
  status = filesystemMount(diskName, MOUNTPOINT,NULL);
  if (status < 0)
    quit(status, "Unable to mount the target disk.");
  updateStatus("Done\n");

  // Rescan the disk info so we get the available free space, etc.
  status = diskGet(diskName, &diskInfo[diskNumber]);
  if (status < 0)
    quit(status, "Error rescanning disk after mount.");

  // Try to make sure the filesystem contains enough free space
  if (diskInfo[diskNumber].freeBytes < bytesToCopy)
    {
      if (doFormat)
	{
	  // We formatted, so we're pretty sure we won't succeed here.
	  if (filesystemUnmount(MOUNTPOINT) < 0)
	    error("Unable to unmount the target disk.");
	  quit((status = ERR_NOFREE), "The filesystem on disk %s is too small "
	       "(%dK) for\nthe selected Visopsys installation (%dK required).",
	       diskName, (diskInfo[diskNumber].freeBytes / 1024),
	       (bytesToCopy / 1024));
	}
      else
	{
	  sprintf(tmpChar, "There MAY not be enough free space on disk %s "
		  "(%dK) for the\nselected Visopsys installation (%dK "
		  "required).  Continue?", diskName,
		  (diskInfo[diskNumber].freeBytes / 1024),
		  (bytesToCopy / 1024));
	  if (!yesOrNo(tmpChar))
	    {
	      if (filesystemUnmount(MOUNTPOINT) < 0)
		error("Unable to unmount the target disk.");
	      quit(0, cancelString);
	    }
	}
    }

  if (!graphics)
    {
      bzero((void *) &prog, sizeof(progress));
      printf("\nInstalling...\n");
      vshProgressBar(&prog);
    }

  // Copy the files

  status = copyFiles(BASICINSTALL);
  if ((status >= 0) && (installType == install_full))
    status = copyFiles(FULLINSTALL);

  if (!graphics)
    vshProgressBarDestroy(&prog);

  if (status >= 0)
    {
      // Set the start program of the target installation to be the login
      // program
      changeStartProgram();

      // Prompt the user to set the admin password
      setAdminPassword();
    }

  // Unmount the target filesystem
  updateStatus("Unmounting target disk...  ");
  if (filesystemUnmount(MOUNTPOINT) < 0)
    error("Unable to unmount the target disk.");
  updateStatus("Done\n");

  if (status < 0)
    {
      // Couldn't copy the files
      message = "Unable to copy files.";
      if (graphics)
	quit(status, message);
      else
	error(message);
    }
  else
    {
      message = "Installation successful.";
      if (graphics)
	quit(status, message);
      else
	printf("\n%s\n", message);
    }

  // Only text mode should fall through to here
  pause();
  quit(status, NULL);

  // Make the compiler happy
  return (status);
}
