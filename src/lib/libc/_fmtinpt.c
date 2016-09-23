//
//  Visopsys
//  Copyright (C) 1998-2016 J. Andrew McLaughlin
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
//  Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
//
//  _fmtinpt.c
//

// This function does all the work of filling data values from input
// based on the format strings used by the scanf family of functions
// (and others, if desired)

#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <sys/cdefs.h>


int _fmtinpt(const char *input, const char *format, va_list list)
{
	int matchItems = 0;
	int inputLen = 0;
	int formatLen = 0;
	int inputCount = 0;
	int formatCount = 0;
	unsigned long long *argument = NULL;
	//int zeroPad = 0;
	//int leftJust = 0;
	//int fieldWidth = 0;
	int isLong = 0;
	int count;

	// How long are the input and format strings?
	inputLen = strlen(input);
	if (inputLen < 0)
	{
		errno = inputLen;
		return (matchItems = 0);
	}

	inputLen = min(inputLen, MAXSTRINGLENGTH);
	formatLen = strlen(format);
	if (formatLen < 0)
	{
		errno = formatLen;
		return (matchItems = 0);
	}

	formatLen = min(formatLen, MAXSTRINGLENGTH);

	// The argument list must already have been initialized using va_start

	// Loop through all of the characters in the format string
	for (formatCount = 0; formatCount < formatLen; )
	{
		// If we encounter any whitespace in the format string, ignore it and
		// ignore any whitespace in the input
		if ((formatCount < formatLen) && isspace(format[formatCount]))
		{
			// Skip format string whitespace
			while ((formatCount < formatLen) && isspace(format[formatCount]))
				formatCount += 1;
			// Skip input whitespace
			while ((inputCount < inputLen) && isspace(input[inputCount]))
				inputCount += 1;
			continue;
		}

		// If "%%" appears in the format, we expect to match one '%' in the
		// input
		if ((format[formatCount] == '%') && (format[formatCount + 1] == '%'))
		{
			if (input[inputCount] != '%')
			{
				errno = ERR_BADDATA;
				return (matchItems);
			}

			formatCount += 2;
			inputCount += 1;
			continue;
		}

		// For other non-conversion specifiers, we simply verify that they match
		// and then skip them
		if (format[formatCount] != '%')
		{
			if (format[formatCount] != input[inputCount])
			{
				errno = ERR_BADDATA;
				return (matchItems);
			}

			formatCount += 1;
			inputCount += 1;
			continue;
		}

		// Move to the next character
		formatCount += 1;

		// We have some format characters.  Get the corresponding argument.

		// Look for a zero digit, which indicates that any field width argument
		// is to be zero-padded
		if (format[formatCount] == '0')
		{
			//zeroPad = 1;
			formatCount  += 1;
		}
		//else
		//	zeroPad = 0;

		// Look for left-justification (applicable if there's a field-width
		// specifier to follow
		if (format[formatCount] == '-')
		{
			//leftJust = 1;
			formatCount += 1;
		}
		//else
		//	leftJust = 0;

		// Look for field length indicator
		if ((format[formatCount] >= '1') && (format[formatCount] <= '9'))
		{
			//fieldWidth = atoi(format + formatCount);
			while ((format[formatCount] >= '0') && (format[formatCount] <= '9'))
				formatCount++;
		}
		//else
		//	fieldWidth = 0;

		// If there's a 'll' qualifier for long values, make note of it
		if (format[formatCount] == 'l')
		{
			formatCount += 1;
			if (format[formatCount] == 'l')
			{
				isLong = 1;
				formatCount += 1;
			}
		}
		else
			isLong = 0;

		// We have some format characters.  Get the corresponding argument.
		argument = (unsigned long long *) va_arg(list, unsigned);

		// What is it?
		switch(format[formatCount])
		{
			case 'd':
			case 'i':
				// This is an integer.  Read the characters for the integer
				// from the input string
				if (isLong)
				{
					*argument = atoll(input + inputCount);
					inputCount += _ldigits(*argument, 10, 1);
				}
				else
				{
					*((int *) argument) = atoi(input + inputCount);
					inputCount += _digits(*argument, 10, 1);
				}
				break;

			case 'u':
				// This is an unsigned integer.  Put the characters for
				// the integer into the destination string
				if (isLong)
				{
					*argument = atoull(input + inputCount);
					inputCount += _ldigits(*argument, 10, 0);
				}
				else
				{
					*((unsigned *) argument) = atou(input + inputCount);
					inputCount += _digits(*argument, 10, 0);
				}
				break;

			case 'c':
				// A character.
				*((char *) argument) = input[inputCount++];
				break;

			case 's':
				// This is a string.  Copy until we meet a whitespace character.
				for (count = 0; ((inputCount < inputLen) &&
					!isspace(input[inputCount])); count ++)
				{
					((char *) argument)[count] = input[inputCount++];
				}
				((char *) argument)[count] = '\0';
				break;

			case 'p':
			case 'x':
			case 'X':
				if (format[formatCount] == 'p')
				{
					// Bit of special stuff for pointer args
					if (!strncmp((input + inputCount), "0x", 2))
						inputCount += 2;
				}
				if (isLong)
				{
					*argument = xtoll(input + inputCount);
					inputCount += _ldigits(*argument, 16, 1);
				}
				else
				{
					*((int *) argument) = xtoi(input + inputCount);
					inputCount += _digits(*argument, 16, 1);
				}
				break;

			default:
				// Umm, we don't know what this is.  Fail.
				errno = ERR_INVALID;
				return (matchItems);
				break;
		}

		matchItems += 1;
		formatCount += 1;
	}

	// Return the number of items we matched
	return (matchItems);
}

