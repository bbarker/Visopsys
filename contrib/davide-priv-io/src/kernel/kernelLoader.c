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
//  kernelLoader.c
//
	
// This file contains the functions belonging to the kernel's executable
// program loader.

#include "kernelLoader.h"
#include "kernelFile.h"
#include "kernelMemory.h"
#include "kernelMalloc.h"
#include "kernelMultitasker.h"
// next one added by Davide Airaghi
#include "kernelParameters.h"
#include "kernelMisc.h"
#include "kernelPage.h"
#include "kernelError.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

// This is the static list of file class registration functions.  If you
// add any to this, remember to update the LOADER_NUM_FILECLASSES value
// in the header file.
static kernelFileClass *(*classRegFns[LOADER_NUM_FILECLASSES]) (void) = {
  kernelFileClassConfig,
  kernelFileClassText,
  kernelFileClassBmp,
  kernelFileClassIco,
  kernelFileClassJpg,
  kernelFileClassBoot,
  kernelFileClassElf,
  kernelFileClassBinary
};
kernelFileClass emptyFileClass = { FILECLASS_NAME_EMPTY, NULL, { } };
static kernelFileClass *fileClassList[LOADER_NUM_FILECLASSES];
static int numFileClasses = 0;
static kernelDynamicLibrary *libraryList = NULL;


static void parseCommand(char *commandLine, int *argc, char *argv[])
{
  // Attempts to take a raw 'commandLine' string and parse it into a command
  // name and arguments.

  int count;

  *argc = 0;

  // Loop through the command string

  for (count = 0; *commandLine != '\0'; count ++)
    {
      // Remove leading whitespace
      while ((*commandLine == ' ') && (*commandLine != '\0'))
	commandLine += 1;

      if (*commandLine == '\0')
	break;

      // If the argument starts with a double-quote, we will discard
      // that character and accept characters (including whitespace)
      // until we hit another double-quote (or the end)
      if (*commandLine != '\"')
	{
	  argv[*argc] = commandLine;

	  // Accept characters until we hit some whitespace (or the end of
	  // the arguments)
	  while ((*commandLine != ' ') && (*commandLine != '\0'))
	    commandLine += 1;
	}
      else
	{
	  // Discard the "
	  commandLine += 1;
	  
	  argv[*argc] = commandLine;

	  // Accept characters  until we hit another double-quote (or the
	  // end of the arguments)
	  while ((*commandLine != '\"') && (*commandLine != '\0'))
	    commandLine += 1;
	}

      *argc += 1;

      if (*commandLine == '\0')
	break;
      *commandLine++ = '\0';
    }

  return;
}


static void *load(const char *filename, file *theFile, int kernel)
{
  // This function merely loads the named file into memory (kernel memory
  // if 'kernel' is non-NULL, otherwise user memory) and returns a pointer
  // to the memory.  The caller must deallocate the memory when finished
  // with the data

  int status = 0;
  void *fileData = NULL;
  
  // Make sure the filename and theFile isn't NULL
  if ((filename == NULL) || (theFile == NULL))
    {
      kernelError(kernel_error, "NULL filename or file structure");
      return (fileData = NULL);
    }

  // Initialize the file structure we're going to use
  kernelMemClear((void *) theFile, sizeof(file));

  // Now, we need to ask the filesystem driver to find the appropriate
  // file, and return a little information about it
  status = kernelFileFind(filename, theFile);
  if (status < 0)
    {
      // Don't make an official error.  Print a message instead.
      kernelError(kernel_error, "The file '%s' could not be found.",
		  filename);
      return (fileData = NULL);
    }

  // If we get here, that means the file was found.  Make sure the size
  // of the program is greater than zero
  if (theFile->size == 0)
    {
      kernelError(kernel_error, "File to load is empty (size is zero)");
      return (fileData = NULL);
    }

  // Get some memory into which we can load the program
  if (kernel)
    fileData = kernelMalloc(theFile->blocks * theFile->blockSize);
  else
    fileData = kernelMemoryGet((theFile->blocks * theFile->blockSize),
			       "file data");
  if (fileData == NULL)
    return (fileData);

  // We got the memory.  Now we can load the program into memory
  status = kernelFileOpen(filename, OPENMODE_READ, theFile);
  if (status < 0)
    {
      // Release the memory we allocated for the program
      if (kernel)
	kernelFree(fileData);
      else
	kernelMemoryRelease(fileData);
      return (fileData = NULL);
    }

  status = kernelFileRead(theFile, 0, theFile->blocks, fileData);
  if (status < 0)
    {
      // Release the memory we allocated for the program
      if (kernel)
	kernelFree(fileData);
      else
	kernelMemoryRelease(fileData);
      return (fileData = NULL);
    }

  return (fileData);
}


static void populateFileClassList(void)
{
  // Populate our list of file classes

  int count;
  
  for (count = 0; count < LOADER_NUM_FILECLASSES; count ++)
    fileClassList[numFileClasses++] = classRegFns[count]();
}

  
/////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////
//
//  Below here, the functions are exported for external use
//
/////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////


void *kernelLoaderLoad(const char *filename, file *theFile)
{
  // This function merely loads the named file into memory and returns
  // a pointer to the memory.  The caller must deallocate the memory when
  // finished with the data

  // Make sure the filename and theFile isn't NULL
  if ((filename == NULL) || (theFile == NULL))
    {
      kernelError(kernel_error, "NULL filename or file structure");
      return (NULL);
    }

  return (load(filename, theFile, 0 /* not kernel */));
}


kernelFileClass *kernelLoaderGetFileClass(const char *className)
{
  // Given a file class name, try to find it in our list.  This is internal
  // for kernel use only.

  int count;

  // Has our list of file classes been initialized?
  if (!numFileClasses)
    populateFileClassList();

  // Find the named file class
  for (count = 0; count < numFileClasses; count ++)
    {
      if (!strcmp(fileClassList[count]->className, className))
	return (fileClassList[count]);
    }
  
  // Not found
  return (NULL);
}


kernelFileClass *kernelLoaderClassify(const char *fileName, void *fileData,
				      int size, loaderFileClass *class)
{
  // Given some file data, try to determine whether it is one of our known
  // file classes.

  int count;

  // Check params.  fileData and size can be NULL.
  if ((fileName == NULL) || (class == NULL))
    return (NULL);

  // Has our list of file classes been initialized?
  if (!numFileClasses)
    populateFileClassList();
  
  // Empty file?
  if ((fileData == NULL) || !size)
    {
      strcpy(class->className, FILECLASS_NAME_EMPTY);
      class->flags = LOADERFILECLASS_EMPTY;
      return (&emptyFileClass);
    }

  // Determine the file's class
  for (count = 0; count < numFileClasses; count ++)
    if (fileClassList[count]->detect(fileName, fileData, size, class))
      return (fileClassList[count]);

  // Not found
  return (NULL);
}


kernelFileClass *kernelLoaderClassifyFile(const char *fileName,
					  loaderFileClass *loaderClass)
{
  // This is a wrapper for the function above, and just temporarily loads
  // the first sector of the file in order to classify it.

  int status = 0;
  file theFile;
  int readBlocks = 0;
  void *fileData = NULL;
  kernelFileClass *class = NULL;
  #define PREVIEW_READBLOCKS 4

  // Check params
  if ((fileName == NULL) || (loaderClass == NULL))
    return (class = NULL);

  // Initialize the file structure we're going to use
  kernelMemClear(&theFile, sizeof(file));

  status = kernelFileOpen(fileName, OPENMODE_READ, &theFile);
  if (status < 0)
    return (class = NULL);

  readBlocks = min(PREVIEW_READBLOCKS, theFile.blocks);

  if (readBlocks)
    {
      fileData = kernelMalloc(readBlocks * theFile.blockSize);
      if (fileData == NULL)
	{
	  kernelFileClose(&theFile);
	  return (class = NULL);
	}

      status = kernelFileRead(&theFile, 0, readBlocks, fileData);
      if (status < 0)
	{
	  kernelFree(fileData);
	  kernelFileClose(&theFile);
	  return (class = NULL);
	}
    }

  class =
    kernelLoaderClassify(fileName, fileData,
			 min(theFile.size, (readBlocks * theFile.blockSize)),
			 loaderClass);

  if (fileData)
    kernelFree(fileData);
  kernelFileClose(&theFile);
  return (class);
}


loaderSymbolTable *kernelLoaderGetSymbols(const char *fileName, int dynamic)
{
  // Given a file name, get symbols.

  loaderSymbolTable *symTable = NULL;
  void *loadAddress = NULL;
  file theFile;
  kernelFileClass *fileClassDriver = NULL;
  loaderFileClass class;

  // Check params
  if (fileName == NULL)
    {
      kernelError(kernel_error, "File name is NULL");
      return (symTable = NULL);
    }

  // Load the file data into memory
  loadAddress =
    (unsigned char *) load(fileName, &theFile, 1 /* kernel memory */);
  if (loadAddress == NULL)
    return (symTable = NULL);

  // Try to determine what kind of executable format we're dealing with.
  fileClassDriver =
    kernelLoaderClassify(fileName, loadAddress, theFile.size, &class);
  if (fileClassDriver == NULL)
    {
      kernelFree(loadAddress);
      return (symTable = NULL);
    }

  if (fileClassDriver->executable.getSymbols)
    // Get the symbols
    symTable = fileClassDriver->executable
      .getSymbols(loadAddress, dynamic, 0 /* not kernel */);

  kernelFree(loadAddress);
  return (symTable);
}


int kernelLoaderLoadProgram(const char *command, int privilege)
{
  // This takes the name of an executable to load and creates a process
  // image based on the contents of the file.  The program is not started
  // by this function.

  int status = 0;
  file theFile;
  void *loadAddress = NULL;
  kernelFileClass *fileClassDriver = NULL;
  loaderFileClass class;
  char procName[MAX_NAME_LENGTH];
  char tmp[MAX_PATH_NAME_LENGTH];
  int newProcId = 0;
  processImage execImage;
  // added by Davide Airaghi
  int require_ring0 = 0;
  
  if (privilege & PRIVILEGE_KERNEL) {
    require_ring0 = 1;
    privilege &= ~ PRIVILEGE_KERNEL;
  }
  
  // Check params
  if (command == NULL)
    {
      kernelError(kernel_error, "Command line to load is NULL");
      return (status = ERR_NULLPARAMETER);
    }

  kernelMemClear(&execImage, sizeof(processImage));

  // Set up argc and argv
  strncpy(execImage.commandLine, command, MAXSTRINGLENGTH);
  parseCommand(execImage.commandLine, &(execImage.argc), execImage.argv);

  // Load the program code/data into memory
  loadAddress =
    (unsigned char *) load(execImage.argv[0], &theFile, 0 /* not kernel */);
  if (loadAddress == NULL)
    return (status = ERR_INVALID);

  // Try to determine what kind of executable format we're dealing with.
  fileClassDriver =
    kernelLoaderClassify(execImage.argv[0], loadAddress, theFile.size, &class);
  if (fileClassDriver == NULL)
    {
      kernelMemoryRelease(loadAddress);
      return (status = ERR_INVALID);
    }

  // Make sure it's an executable
  if (!(class.flags & LOADERFILECLASS_EXEC))
    {
      kernelError(kernel_error, "File \"%s\" is not an executable program",
		  command);
      kernelMemoryRelease(loadAddress);
      return (status = ERR_PERMISSION);
    }

  // We may need to do some fixup or relocations
  if (fileClassDriver->executable.layoutExecutable)
    {
      status = fileClassDriver->executable
	.layoutExecutable(loadAddress, &execImage);
      if (status < 0)
	{
	  kernelMemoryRelease(loadAddress);
	  return (status);
	}
    }

  // Just get the program name without the path in order to set the process
  // name
  status = kernelFileSeparateLast(execImage.argv[0], tmp, procName);
  if (status < 0)
    strncpy(procName, command, MAX_NAME_LENGTH);

  // Set up and run the user program as a process in the multitasker
  // modified by Davide Airaghi
  if (!require_ring0)
    newProcId = kernelMultitaskerCreateProcess(procName, privilege, &execImage);
  else
    newProcId = kernelMultitaskerCreateProcess(procName, privilege | PRIVILEGE_KERNEL, &execImage);  
  if (newProcId < 0)
    {
      // Release the memory we allocated for the program
      kernelMemoryRelease(loadAddress);
      kernelMemoryRelease(execImage.code);
      return (newProcId);
    }

  if (class.flags & LOADERFILECLASS_DYNAMIC)
    {
      // It's a dynamically-linked program, so we need to link in the required
      // libraries
      if (fileClassDriver->executable.link)
	{
	  status = fileClassDriver->executable
	    .link(newProcId, loadAddress, &execImage);
	  if (status < 0)
	    {
	      kernelMemoryRelease(loadAddress);
	      kernelMemoryRelease(execImage.code);
	      return (status);
	    }
	}
    }
  // Unmap the new process' image memory from this process' address space.
  status = kernelPageUnmap(kernelCurrentProcess->processId, execImage.code,
			   execImage.imageSize);
  if (status < 0)
    kernelError(kernel_warn, "Unable to unmap new process memory from current "
		"process");

  // Get rid of the old file memory
  kernelMemoryRelease(loadAddress);

  // All set.  Return the process id.
  return (newProcId);
}


int kernelLoaderLoadLibrary(const char *libraryName)
{
  // This takes the name of a library to load and creates a shared library
  // in the kernel.

  int status = 0;
  file theFile;
  void *loadAddress = NULL;
  kernelFileClass *fileClassDriver = NULL;
  loaderFileClass class;
  processImage libImage;
  kernelDynamicLibrary *library = NULL;
  char tmp[MAX_PATH_NAME_LENGTH];

  // Check params
  if (libraryName == NULL)
    {
      kernelError(kernel_error, "Library name to load is NULL");
      return (status = ERR_NULLPARAMETER);
    }

  kernelMemClear(&libImage, sizeof(processImage));

  // Load the program code/data into memory
  loadAddress = (unsigned char *) load(libraryName, &theFile, 1 /* kernel */);
  if (loadAddress == NULL)
    return (status = ERR_INVALID);

  // Try to determine what kind of executable format we're dealing with.
  fileClassDriver =
    kernelLoaderClassify(libraryName, loadAddress, theFile.size, &class);
  if (fileClassDriver == NULL)
    {
      kernelFree(loadAddress);
      return (status = ERR_INVALID);
    }

  // Make sure it's a dynamic library
  if (!(class.flags & LOADERFILECLASS_DYNAMIC) ||
      !(class.flags & LOADERFILECLASS_LIB))
    {
      kernelError(kernel_error, "File \"%s\" is not a shared library",
		  libraryName);
      kernelFree(loadAddress);
      return (status = ERR_PERMISSION);
    }

  // Get memory for our dynamic library
  library = kernelMalloc(sizeof(kernelDynamicLibrary));
  if (library == NULL)
    {
      kernelFree(loadAddress);
      return (status = ERR_MEMORY);
    }

  // Just get the library name without the path, and set it as the default
  // library name.  The file class driver can reset it to something else
  // if desired.
  status = kernelFileSeparateLast(libraryName, tmp, library->name);
  if (status < 0)
    strncpy(library->name, libraryName, MAX_NAME_LENGTH);

  if (fileClassDriver->executable.layoutLibrary)
    {
      // Do our library layout
      status = fileClassDriver->executable.layoutLibrary(loadAddress, library);
      if (status < 0)
	{
	  kernelFree(loadAddress);
	  kernelFree(library);
	  return (status);
	}
    }

  // Add it to our list of libraries
  library->next = libraryList;
  libraryList = library;

  // Get rid of the old memory
  kernelFree(loadAddress);

  return (status = 0);
}


kernelDynamicLibrary *kernelLoaderGetLibrary(const char *libraryName)
{
  // Searches through our list of loaded dynamic libraries for the requested
  // one, and returns it if found.  The name can be either a full pathname,
  // or just a short one such as 'libc.so'.  If not found, calls the
  // kernelLoaderLoadLibrary() function to try and load it, before searching
  // the list again.

  kernelDynamicLibrary *library = libraryList;
  char shortName[MAX_NAME_LENGTH];
  char tmp[MAX_PATH_NAME_LENGTH];
  int count;

  // Check params
  if (libraryName == NULL)
    {
      kernelError(kernel_error, "Library name is NULL");
      return (library = NULL);
    }

  // If the library name is fully-qualified, get the short version without
  // the path.
  if (kernelFileSeparateLast(libraryName, tmp, shortName) < 0)
    strncpy(shortName, libraryName, MAX_NAME_LENGTH);

  for (count = 0; count < 2; count ++)
    {
      while (library)
	{
	  if (!strncmp(shortName, library->name, MAX_NAME_LENGTH))
	    return (library);
	  else
	    library = library->next;
	}

      // If we fall through, it wasn't found.  Try to load it.
      sprintf(tmp, "/system/libraries/%s", shortName);
      if (kernelLoaderLoadLibrary(tmp) < 0)
	return (library = NULL);

      // Loop again.
      library = libraryList;
    }

  // If we fall through, we don't have the library.
  return (library = NULL);
}


int kernelLoaderExecProgram(int processId, int block)
{
  // This is a convenience function for executing a program loaded by
  // the kernelLoaderLoadProgram function.  The calling function could
  // easily accomplish this stuff by talking to the multitasker.  If
  // blocking is requested, the exit code of the program is returned to
  // the caller.
  
  int status = 0;
  
  // Start user program's process
  status = kernelMultitaskerSetProcessState(processId, proc_ready);
  if (status < 0)
    return (status);

  // Now, if we are supposed to block on this program, we should make
  // the appropriate call to the multitasker
  if (block)
    {
      status = kernelMultitaskerBlock(processId);

      // ...Now we're waiting for the program to terminate...

      // Return the exit code from the program
      return (status);
    }
  else
    // Return successfully
    return (status = 0);
}


int kernelLoaderLoadAndExec(const char *command, int privilege, int block)
{
  // This is a convenience function that just calls the
  // kernelLoaderLoadProgram and kernelLoaderExecProgram functions for the
  // caller.

  int processId = 0;
  int status = 0;

  processId = kernelLoaderLoadProgram(command, privilege);

  if (processId < 0)
    return (processId);

  status = kernelLoaderExecProgram(processId, block);

  // All set
  return (status);
}
