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
//  atou.c
//

// Davide Airaghi
// This is the "atou" function, convert an array of chars into an unsigned

#include <stdlib.h>
#include <string.h>
#include <errno.h>


unsigned atou(const char *theString)
{

  unsigned result = 0;
  int length = 0;
  int count;

  if (theString == NULL)
    {
      errno = ERR_NULLPARAMETER;
      return (0);
    }

  if ((theString[0] < 48) || (theString[0] > 57))
    // Not a number
    return (errno = ERR_INVALID);

  // Get the length of the string
  length = strlen(theString);

  // Do a loop to iteratively add to the value of 'result'.
  for (count = 0; count < length; count ++)
    {
      // Check to make sure input is numeric ascii
      if ((theString[count] < 48) || (theString[count] > 57))
	// Return whatever we have so far
	return (result);

      result *= 10;
      result += (((unsigned) theString[count]) - 48);
    }
  
  return (result);
}
