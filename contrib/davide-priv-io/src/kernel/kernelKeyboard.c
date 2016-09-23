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
//  kernelKeyboard.c
//
//  German key mappings provided by Jonas Zaddach <jonaszaddach@gmx.de>
//  Italian key mappings provided by Davide Airaghi <davide.airaghi@gmail.com>
	
// This is the master code that wraps around the keyboard driver
// functionality

#include "kernelKeyboard.h"
#include "kernelInterrupt.h"
#include "kernelPic.h"
#include "kernelProcessorX86.h"
#include "kernelWindow.h"
#include "kernelError.h"
#include <string.h>

static kernelDevice *systemKeyboard = NULL;
static kernelKeyboard *keyboardDevice = NULL;
static kernelKeyboardOps *ops = NULL;
static stream *consoleStream = NULL;
static int graphics = 0;

static kernelKeyMap EN_US = {
  "English (US)",
  // Regular map
  { 27,'1','2','3','4','5','6','7','8','9','0','-','=',8,9,'q',    // 00-0F
    'w','e','r','t','y','u','i','o','p','[',']',10,0,'a','s','d',  // 10-1F
    'f','g','h','j','k','l',';',39,'`',0,'\\','z','x','c','v','b', // 20-2F
    'n','m',',','.','/',0,'*',0,' ',0,0,0,0,0,0,0,                 // 30-3F
    0,0,0,0,0,0,13,17,11,'-',18,'5',19,'+',0,20,                   // 40-4F
    12,0,127,0,0,0                                                 // 50-55
  },
  // Shift map
  { 27,'!','@','#','$','%','^','&','*','(',')','_','+',8,9,'Q',    // 00-0F
    'W','E','R','T','Y','U','I','O','P','{','}',10,0,'A','S','D',  // 10-1F 
    'F','G','H','J','K','L',':','"','~',0,'|','Z','X','C','V','B', // 20-2F
    'N','M','<','>','?',0,'*',0,' ',0,0,0,0,0,0,0,        	   // 30-3F
    0,0,0,0,0,0,'7','8','9','-','4','5','6','+','1','2',	   // 40-4F
    '3','0','.',0,0,0                				   // 50-55
  },
  // Control map.  Default is regular map value.
  { 27, '1','2','3','4','5','6','7','8','9','0','-','=',8,9,17,    // 00-0F
    23,5,18,20,25,21,9,15,16,'[',']',10,0,1,19,4,		   // 10-1F 
    6,7,8,10,11,12,';','"','`',0,0,26,24,3,22,2,    		   // 20-2F
    14,13,',','.','/',0,'*',0,' ',0,0,0,0,0,0,0,		   // 30-3F
    0,0,0,0,0,0,13,17,11,'-',18,'5',19,'+','1',20,		   // 40-4F
    12,'0',127,0,0,0                				   // 50-55
  },
  // Alt-Gr map.  Same as the regular map for this keyboard.
  { 27,'1','2','3','4','5','6','7','8','9','0','-','=',8,9,'q',    // 00-0F
    'w','e','r','t','y','u','i','o','p','[',']',10,0,'a','s','d',  // 10-1F
    'f','g','h','j','k','l',';',39,'`',0,'\\','z','x','c','v','b', // 20-2F
    'n','m',',','.','/',0,'*',0,' ',0,0,0,0,0,0,0,                 // 30-3F
    0,0,0,0,0,0,13,17,11,'-',18,'5',19,'+',0,20,                   // 40-4F
    12,0,127,0,0,0                                                 // 50-55
  }
};

static kernelKeyMap EN_UK = {
  "English (UK)",
  // Regular map
  { 27,'1','2','3','4','5','6','7','8','9','0','-','=',8,9,'q',    // 00-0F
    'w','e','r','t','y','u','i','o','p','[',']',10,0,'a','s','d',  // 10-1F
    'f','g','h','j','k','l',';',39,'`',0,'#','z','x','c','v','b',  // 20-2F
    'n','m',',','.','/',0,'*',0,' ',0,0,0,0,0,0,0,                 // 30-3F
    0,0,0,0,0,0,13,17,11,'-',18,'5',19,'+','1',20,                 // 40-4F
    12,'0',127,0,0,'\\'                                            // 50-55
  },
  // Shift map
  { 27,'!','"',156,'$','%','^','&','*','(',')','_','+',8,9,'Q',    // 00-0F
    'W','E','R','T','Y','U','I','O','P','{','}',10,0,'A','S','D',  // 10-1F 
    'F','G','H','J','K','L',':','@',170,0,'~','Z','X','C','V','B', // 20-2F
    'N','M','<','>','?',0,'*',0,' ',0,0,0,0,0,0,0,        	   // 30-3F
    0,0,0,0,0,0,'7','8','9','-','4','5','6','+','1','2',	   // 40-4F
    '3','0','.',0,0,'|'                				   // 50-55
  },
  // Control map.  Default is regular map value.
  { 27, '1','2','3','4','5','6','7','8','9','0','-','=',8,9,17,	   // 00-0F
    23,5,18,20,25,21,9,15,16,'[',']',10,0,1,19,4,		   // 10-1F 
    6,7,8,10,11,12,';', '@','`',0,0,26,24,3,22,2,    		   // 20-2F
    14,13,',','.','/',0,'*',0,' ',0,0,0,0,0,0,0,		   // 30-3F
    0,0,0,0,0,0,13,17,11,'-',18,'5',19,'+','1',20,		   // 40-4F
    12,'0',127,0,0,0                				   // 50-55
  },
  // Alt-Gr map.  Same as the regular map for this keyboard.
  { 27,'1','2','3','4','5','6','7','8','9','0','-','=',8,9,'q',    // 00-0F
    'w','e','r','t','y','u','i','o','p','[',']',10,0,'a','s','d',  // 10-1F
    'f','g','h','j','k','l',';',39,'`',0,'#','z','x','c','v','b',  // 20-2F
    'n','m',',','.','/',0,'*',0,' ',0,0,0,0,0,0,0,                 // 30-3F
    0,0,0,0,0,0,13,17,11,'-',18,'5',19,'+','1',20,                 // 40-4F
    12,'0',127,0,0,'\\'                                            // 50-55
  }
};

static kernelKeyMap DE_DE = {
  "Deutsch (DE)",
  // Regular map
  { 27,'1','2','3','4','5','6','7','8','9','0',225,'\'',8,9,'q',   // 00-0F
    'w','e','r','t','z','u','i','o','p',129,'+',10,0,'a','s','d',  // 10-1F
    'f','g','h','j','k','l',148,132,'^',0,'#','y','x','c','v','b', // 20-2F
    'n','m',',','.','-',0,'*',0,' ',0,0,0,0,0,0,0,                 // 30-3F
    0,0,0,0,0,0,13,17,11,'-',18,'5',19,'+',0,20,                   // 40-4F
    12,0,127,0,0,'<'                                               // 50-55
  },
  // Shift map
  { 27,'!','"',245,'$','%','&','/','(',')','=','?','`',8,9,'Q',    // 00-0F
    'W','E','R','T','Z','U','I','O','P',154,'*',10,0,'A','S','D',  // 10-1F 
    'F','G','H','J','K','L',153,142,248,0,'\'','Y','X','C','V','B',// 20-2F
    'N','M',';',':','_',0,'*',0,' ',0,0,0,0,0,0,0,                 // 30-3F
    0,0,0,0,0,0,'7','8','9','-','4','5','6','+','1','2',           // 40-4F
    '3','0','.',0,0,'>'                                            // 50-55
  },
  // Control map.  Default is regular map value.
  { 27, '1','2','3','4','5','6','7','8','9','0',225,'\'',8,9,17,   // 00-0F
    23,5,18,20,25,21,9,15,16,'[',']',10,0,1,19,4,                  // 10-1F 
    6,7,8,10,11,12,148,132,'^',0,0,26,24,3,22,2,                   // 20-2F
    14,13,',','.','-',0,'*',0,' ',0,0,0,0,0,0,0,                   // 30-3F
    0,0,0,0,0,0,13,17,11,'-',18,'5',19,'+','1',20,                 // 40-4F
    12,'0',127,0,0,'<'                                             // 50-55
  },
  // Alt-Gr map.  Default is regular map value.
  { 27,'1',253,252,172,171,'6','{','[',']','}','\\','\'',8,9,'@',  // 00-0F
    'w','e','r','t','z','u','i','o','p',129,'~',10,0,145,225,208,  // 10-1F
    'f','g','h','j','k','l',148,132,'^',0,'#',174,175,135,'v','b', // 20-2F
    'n',230,',','.','-',0,'*',0,' ',0,0,0,0,0,0,0,                 // 30-3F
    0,0,0,0,0,0,13,17,11,'-',18,'5',19,'+',0,20,                   // 40-4F
    12,0,127,0,0,'|'                                               // 50-55
  }
};

static kernelKeyMap IT_IT = {
  "Italian (IT)",
  // Regular map
  { 27,'1','2','3','4','5','6','7','8','9','0','\'',141,8,9,'q',   // 00-0F
    'w','e','r','t','y','u','i','o','p',138,'+',10,0,'a','s','d',  // 10-1F
    'f','g','h','j','k','l',149,133,'\\',0,151,'z','x','c','v','b',// 20-2F
    'n','m',',','.','-',0,'*',0,' ',0,0,0,0,0,0,0,                 // 30-3F
    0,0,0,0,0,0,13,17,11,'-',18,'5',19,'+',0,20,                   // 40-4F
    12,0,127,0,0,'<'                                               // 50-55
  },
  // Shift map
  { 27,'!','"',156,'$','%','&','/','(',')','=','?','^',8,9,'Q',    // 00-0F
    'W','E','R','T','Y','U','I','O','P',130,'*',10,0,'A','S','D',  // 10-1F 
    'F','G','H','J','K','L',135,248,'|',0,245,'Z','X','C','V','B', // 20-2F
    'N','M',';',':','_',0,'*',0,' ',0,0,0,0,0,0,0,        	   // 30-3F
    0,0,0,0,0,0,'7','8','9','-','4','5','6','+','1','2',	   // 40-4F
    '3','0','.',0,0,'>'               				   // 50-55
  },
  // Control map.  Default is regular map value.
  { 27,'1','2','3','4','5','6','7','8','9','0','\'',141,8,9,'q',   // 00-0F
    'w','e','r','t','y','u','i','o','p',138,'+',10,0,'a','s','d',  // 10-1F
    'f','g','h','j','k','l',149,133,'\\',0,151,'z','x','c','v','b',// 20-2F
    'n','m',',','.','-',0,'*',0,' ',0,0,0,0,0,0,0,                 // 30-3F
    0,0,0,0,0,0,13,17,11,'-',18,'5',19,'+',0,20,                   // 40-4F
    12,0,127,0,0,'<'                                               // 50-55
  },
  // Alt-Gr map.  Default is regular map value.
  { 27,'1','2','3','4','5','6','{','[',']','}','`','~',8,9,'q',    // 00-0F
    'w','e','r','t','y','u','i','o','p','[',']',10,0,'a','s','d',  // 10-1F
    'f','g','h','j','k','l','@','#','\\',0,151,'z','x','c','v','b',// 20-2F
    'n','m',',','.','-',0,'*',0,' ',0,0,0,0,0,0,0,                 // 30-3F
    0,0,0,0,0,0,13,17,11,'-',18,'5',19,'+',0,20,                   // 40-4F
    12,0,127,0,0,'<'                                               // 50-55
  },
};

static kernelKeyMap *allMaps[] = {
  &EN_US, &EN_UK, &DE_DE, &IT_IT, NULL
};


static void keyboardInterrupt(void)
{
  // This is the keyboard interrupt handler.  It calls the keyboard driver
  // to actually read data from the device.

  void *address = NULL;

  kernelProcessorIsrEnter(address);
  kernelProcessingInterrupt = 1;

  // Ok, now we can call the routine.
  if (ops->driverReadData)
    ops->driverReadData();

  kernelPicEndOfInterrupt(INTERRUPT_NUM_KEYBOARD);
  kernelProcessingInterrupt = 0;
  kernelProcessorIsrExit(address);
}


/////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////
//
//  Below here, the functions are exported for external use
//
/////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////


int kernelKeyboardInitialize(kernelDevice *dev)
{
  // This function initializes the keyboard code, and sets the default
  // keyboard mapping

  int status = 0;

  if (dev == NULL)
    {
      kernelError(kernel_error, "The keyboard device is NULL");
      return (status = ERR_NOTINITIALIZED);
    }

  systemKeyboard = dev;

  if ((systemKeyboard->data == NULL) || (systemKeyboard->driver == NULL) ||
      (systemKeyboard->driver->ops == NULL))
    {
      kernelError(kernel_error, "The keyboard, driver or ops are NULL");
      return (status = ERR_NULLPARAMETER);
    }

  keyboardDevice = (kernelKeyboard *) systemKeyboard->data;
  ops = systemKeyboard->driver->ops;

  // We use US English as default, because, well, Americans would be so
  // confused if it wasn't.  Everyone else understands the concept of
  // setting it.
  keyboardDevice->keyMap = &EN_US;

  // Register our interrupt handler
  status = kernelInterruptHook(INTERRUPT_NUM_KEYBOARD, &keyboardInterrupt);
  if (status < 0)
    return (status);

  // Turn on the interrupt
  kernelPicMask(INTERRUPT_NUM_KEYBOARD, 1);

  return (status = 0);
}


int kernelKeyboardGetMaps(char *buffer, unsigned size)
{
  // Get the names of the different available keyboard mappings
  
  int status = 0;
  int buffCount = 0;
  unsigned bytes = 0;
  int names = 1;
  int count;

  if (systemKeyboard == NULL)
    return (status = ERR_NOTINITIALIZED);

  // Check params
  if (buffer == NULL)
    return (status = ERR_NULLPARAMETER);

  // First, copy the name of the current map
  bytes = strlen(keyboardDevice->keyMap->name) + 1;
  strncpy(buffer, keyboardDevice->keyMap->name, size);
  buffer += bytes;
  buffCount += bytes;

  // Loop through our allMaps structure, copying the rest of the names into
  // the buffer
  for (count = 0; allMaps[count] != NULL; count ++)
    {
      bytes = strlen(allMaps[count]->name) + 1;

      if ((allMaps[count] != keyboardDevice->keyMap) &&
	  ((buffCount + bytes) < size))
	{
	  strcpy(buffer, allMaps[count]->name);
	  buffer += bytes;
	  buffCount += bytes;
	  names += 1;
	}
    }

  // Return the number of names
  return (names);
}


int kernelKeyboardSetMap(const char *name)
{
  // Set the current keyboard mapping by name.
  
  int status = 0;
  int count;

  if (systemKeyboard == NULL)
    return (status = ERR_NOTINITIALIZED);

  // Check params
  if (name == NULL)
    return (status = ERR_NULLPARAMETER);

  // Loop through our allMaps structure, looking for the one whose name
  // matches the supplied one
  for (count = 0; allMaps[count] != NULL; count ++)
    {
      if (!strcmp(allMaps[count]->name, name))
	{
	  // Found it.  Set the mapping.
	  keyboardDevice->keyMap = allMaps[count];
	  return (status = 0);
	}
    }

  // If we fall through to here, it wasn't found
  kernelError(kernel_error, "No such keyboard map \"%s\"", name);
  return (status = ERR_INVALID);
}


int kernelKeyboardSetStream(stream *newStream)
{
  // Set the current stream used by the keyboard driver
  
  int status = 0;

  if (systemKeyboard == NULL)
    return (status = ERR_NOTINITIALIZED);

  // Are graphics enabled?
  graphics = kernelGraphicsAreEnabled();

  // Save the address of the kernelStream we were passed to use for
  // keyboard data
  consoleStream = newStream;

  return (status = 0);
}


int kernelKeyboardInput(int ascii, int eventType)
{
  // This gets called by the keyboard driver to tell us that a key has been
  // pressed.
  windowEvent event;

  if (graphics)
    {
      // Fill out our event
      event.type = eventType;
      event.xPosition = 0;
      event.yPosition = 0;
      event.key = ascii;

      // Notify the window manager of the event
      kernelWindowProcessEvent(&event);
    }
  else
    {
      if (consoleStream && (eventType & EVENT_KEY_DOWN))
	consoleStream->append(consoleStream, (char) ascii);
    }

  return (0);
}
