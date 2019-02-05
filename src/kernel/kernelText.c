//
//  Visopsys
//  Copyright (C) 1998-2018 J. Andrew McLaughlin
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
//  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
//
//  kernelText.c
//

#include "kernelText.h"
#include "kernelCharset.h"
#include "kernelKeyboard.h"
#include "kernelParameters.h"
#include "kernelPage.h"
#include "kernelMultitasker.h"
#include "kernelMalloc.h"
#include "kernelWindow.h"
#include "kernelError.h"
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// There is only ONE kernelTextInputStream for console input
static kernelTextInputStream originalConsoleInput;
static kernelTextInputStream *consoleInput = &originalConsoleInput;

// There is only ONE kernelTextOutputStream for console output as well.
static kernelTextOutputStream originalConsoleOutput;
static kernelTextOutputStream *consoleOutput = &originalConsoleOutput;

// ...But the 'current' input and output streams can be anything
static kernelTextInputStream *currentInput = NULL;
static kernelTextOutputStream *currentOutput = NULL;

static kernelTextArea consoleArea = {
	0,								// xCoord
	0,								// yCoord;
	80,								// columns
	50,								// rows
	2,								// bytes per char
	0,								// cursor column
	0,								// cursor row
	1,								// cursor state
	0,								// max buffer lines
	0,								// scrollback lines
	0,								// scrolled back lines
	0,								// hidden
	TEXT_DEFAULT_FOREGROUND,		// foreground
	TEXT_DEFAULT_BACKGROUND,		// background
	0,								// pcColor
	NULL,							// inputStream
	NULL,							// outputStream
	NULL,							// buffer data
	(unsigned char *) 0x000B8000,	// Text screen address (visible data)
	NULL,							// font
	CHARSET_NAME_DEFAULT,			// charSet
	NULL,							// window component
	0								// no-scroll flag
};

// So nobody can use us until we're ready
static int initialized = 0;


static int currentInputIntercept(stream *theStream, unsigned char byte)
{
	// This function allows us to intercept special-case characters coming
	// into the console input stream

	int status = 0;

	// We always use currentInput and currentOutput.  This keeps the compiler
	// happy
	if (!theStream)
		return (status = ERR_NULLPARAMETER);

	// Check for a few special scenarios

	// Check for CTRL-C
	if (byte == 3)
	{
		// Show that something happened
		kernelTextStreamPrintLine(currentOutput, "^C");

		// Send the interrupt signal to the process that owns the input stream
		kernelMultitaskerSignal(currentInput->ownerPid, SIGINT);

		return (status = 0);
	}

	// Check for PAGE UP
	else if (byte == 11)
	{
		kernelTextStreamScroll(currentOutput, -1);
		return (status = 0);
	}

	// Check for PAGE DOWN
	else if (byte == 12)
	{
		kernelTextStreamScroll(currentOutput, 1);
		return (status = 0);
	}

	else if (currentInput->attrs.echo)
	{
		// Check for BACKSPACE
		if (byte == 8)
			kernelTextStreamBackSpace(currentOutput);

		// Check for TAB
		else if (byte == 9)
			kernelTextStreamTab(currentOutput);

		// Check for ENTER
		else if (byte == 10)
			kernelTextStreamNewline(currentOutput);

		else if (byte >= 32)
			// Echo the character
			kernelTextStreamPutc(currentOutput, byte);
	}

	// The keyboard driver tries to append everything to the original text
	// console stream.  If the current console input is different, we need to
	// put it into that stream instead.  We just ignore the stream told to
	// us by our caller.

	// Call the original stream append function
	status = currentInput->s.intercept(&currentInput->s, byte);

	return (status);
}


static inline void updateComponent(kernelTextArea *area)
{
	kernelWindowComponent *component =
		(kernelWindowComponent *) area->windowComponent;

	if (component && component->update)
		component->update(area->windowComponent);
}


/////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////
//
// Below here, the functions are exported for external use
//
/////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////

int kernelTextInitialize(int columns, int rows)
{
	// Initialize the console input and output streams

	int status = 0;

	// Check params
	if ((columns <= 0) || (rows <= 0))
		return (status = ERR_INVALID);

	// Initialize text mode output

	// Set the initial rows and columns
	consoleArea.columns = columns;
	consoleArea.rows = rows;

	// Get some buffer space
	consoleArea.bufferData = (unsigned char *) kernelMalloc((rows +
		TEXT_DEFAULT_SCROLLBACKLINES) * columns * consoleArea.bytesPerChar);
	if (!consoleArea.bufferData)
		return (status = ERR_MEMORY);

	consoleArea.maxBufferLines = (rows + TEXT_DEFAULT_SCROLLBACKLINES);

	// Take the physical text screen address and turn it into a virtual
	// address in the kernel's address space.
	status = kernelPageMapToFree(KERNELPROCID,
		(unsigned) consoleArea.visibleData, (void *) &consoleArea.visibleData,
		(columns * rows * consoleArea.bytesPerChar));
	// Make sure we got a proper new virtual address
	if (status < 0)
		return (status);

	// Copy the current screen into the buffer
	memcpy((consoleArea.bufferData + ((consoleArea.maxBufferLines - rows) *
		columns * consoleArea.bytesPerChar)), consoleArea.visibleData,
		(columns * rows * consoleArea.bytesPerChar));

	// Initialize the console drivers
	kernelConsoleDriversInitialize();

	// We assign the text mode driver to be the output driver for now.
	consoleOutput->textArea = &consoleArea;
	consoleOutput->outputDriver = kernelSoftwareDriverGet(textConsoleDriver);

	// Set the foreground/background colors

	if (consoleOutput->outputDriver->setForeground)
	{
		consoleOutput->outputDriver->setForeground(consoleOutput->textArea,
			&TEXT_DEFAULT_FOREGROUND);
	}

	if (consoleOutput->outputDriver->setBackground)
	{
		consoleOutput->outputDriver->setBackground(consoleOutput->textArea,
			&TEXT_DEFAULT_BACKGROUND);
	}

	consoleArea.outputStream = consoleOutput;

	// Set up our console input stream
	status = kernelStreamNew(&consoleInput->s, TEXT_STREAMSIZE,
		itemsize_byte);
	if (status < 0)
		return (status);

	// We want to be able to intercept things as they're put into the console
	// input stream as they're placed there, so we can catch keyboard
	// interrupts and such.  Remember the original append function though
	consoleInput->s.intercept = consoleInput->s.append;
	consoleInput->s.append = (int (*)(stream *, ...)) &currentInputIntercept;
	consoleInput->attrs.echo = 1;

	consoleArea.inputStream = consoleInput;

	// Finally, set the current input and output streams to point to the
	// console ones we've just created
	currentInput = consoleInput;
	currentOutput = consoleOutput;

	// Make note that we've been initialized
	initialized = 1;

	// Return success
	return (status = 0);
}


kernelTextArea *kernelTextAreaNew(int columns, int rows, int bytesPerChar,
	int bufferLines)
{
	// Do the allocations and whatnot for a kernelTextArea.  Doesn't set any
	// colors, and makes some other assumptions that may need to be
	// overridden.

	kernelTextArea *area = NULL;

	// Check params
	if ((columns <= 0) || (rows <= 0) || (bytesPerChar <= 0))
		return (area = NULL);

	area = kernelMalloc(sizeof(kernelTextArea));
	if (!area)
		return (area = NULL);

	// All values not listed are NULL
	area->columns = columns;
	area->rows = rows;
	area->bytesPerChar = bytesPerChar;
	area->cursorState = 1;
	area->maxBufferLines = (rows + bufferLines);

	// An input stream
	area->inputStream = kernelMalloc(sizeof(kernelTextInputStream));
	if (!area->inputStream || kernelTextNewInputStream(area->inputStream))
	{
		kernelTextAreaDestroy(area);
		return (area = NULL);
	}

	// An output stream
	area->outputStream = kernelMalloc(sizeof(kernelTextOutputStream));
	if (!area->outputStream || kernelTextNewOutputStream(area->outputStream))
	{
		kernelTextAreaDestroy(area);
		return (area = NULL);
	}

	// Assign the area to the output stream
	((kernelTextOutputStream *) area->outputStream)->textArea = area;

	// The big buffer
	area->bufferData = kernelMalloc(columns * area->maxBufferLines *
		bytesPerChar);
	if (!area->bufferData)
	{
		kernelTextAreaDestroy(area);
		return (area = NULL);
	}

	// The buffer for the visible part
	area->visibleData = kernelMalloc(columns * rows * bytesPerChar);
	if (!area->visibleData)
	{
		kernelTextAreaDestroy(area);
		return (area = NULL);
	}

	// Default character set
	area->charSet = CHARSET_NAME_DEFAULT;

	return (area);
}


void kernelTextAreaDestroy(kernelTextArea *area)
{
	// Release the allocations and whatnot for a kernelTextArea.

	kernelTextInputStream *inputStream = NULL;
	kernelTextOutputStream *outputStream = NULL;

	// Check params
	if (!area)
		return;

	inputStream = (kernelTextInputStream *) area->inputStream;
	outputStream = (kernelTextOutputStream *) area->outputStream;

	if (inputStream && (inputStream != &originalConsoleInput))
	{
		kernelStreamDestroy(&inputStream->s);
		kernelFree((void *) area->inputStream);
	}

	if (outputStream && (outputStream != &originalConsoleOutput))
		kernelFree((void *) area->outputStream);

	if (area->bufferData)
		kernelFree(area->bufferData);

	if (area->visibleData)
		kernelFree(area->visibleData);

	memset((void *) area, 0, sizeof(kernelTextArea));
	kernelFree((void *) area);
}


int kernelTextAreaResize(kernelTextArea *area, int columns, int rows)
{
	// Given an existing text area, resize it.

	int newBufferLines = 0;
	unsigned char *newVisibleData = NULL;
	unsigned char *newBufferData = NULL;
	int copyColumns = 0, diffRows = 0, diffVisibleRows = 0;
	int rowCount;

	// Check params
	if (!area || (columns <= 0) || (rows <= 0))
		return (ERR_INVALID);

	diffRows = (rows - area->rows);

	// Adjust this by the difference between the number of rows
	newBufferLines = (area->maxBufferLines + diffRows);

	// Get a new main buffer, and a new buffer for the visible part
	newBufferData = kernelMalloc(columns * newBufferLines *
		area->bytesPerChar);
	newVisibleData = kernelMalloc(columns * rows * area->bytesPerChar);
	if (!newBufferData || !newVisibleData)
		return (ERR_MEMORY);

	copyColumns = min(area->columns, columns);

	// Copy the rows from the buffer.
	if (diffRows >= 0)
	{
		diffVisibleRows = min(diffRows, area->scrollBackLines);
		for (rowCount = 0; rowCount < area->maxBufferLines; rowCount ++)
		{
			strncpy((char *)(newBufferData + ((diffVisibleRows + rowCount) *
				columns)), (char *)(area->bufferData + (rowCount *
				area->columns)), copyColumns);
		}
		area->cursorRow += diffVisibleRows;
		area->scrollBackLines -= diffVisibleRows;
	}
	else
	{
		diffVisibleRows = min(-diffRows, area->scrollBackLines);
		for (rowCount = 0; rowCount < newBufferLines; rowCount ++)
		{
			strncpy((char *)(newBufferData + (rowCount * columns)),
				(char *)(area->bufferData + ((diffVisibleRows + rowCount) *
				area->columns)), copyColumns);
		}
		if (area->cursorRow >= (area->rows - 1))
		{
			area->scrollBackLines += min(-diffRows, ((newBufferLines - rows) -
				area->scrollBackLines));
		}
	}

	updateComponent(area);

	// Free the old buffers and assign the new ones
	kernelFree(area->bufferData);
	area->bufferData = newBufferData;
	kernelFree(area->visibleData);
	area->visibleData = newVisibleData;

	// Adjust the cursor position if it falls outside the new boundaries
	if (area->cursorColumn >= columns)
		area->cursorColumn = (columns - 1);
	if (area->cursorRow >= rows)
		area->cursorRow = (rows - 1);

	area->columns = columns;
	area->rows = rows;
	area->maxBufferLines = newBufferLines;

	// Update the visible bit.  Not sure this is really necessary since in
	// most cases the screenDraw() function will be called next.
	memcpy(area->visibleData, TEXTAREA_FIRSTVISIBLE(area),
		(columns * rows * area->bytesPerChar));

	return (0);
}


int kernelTextSwitchToGraphics(kernelTextArea *area)
{
	// If the kernel is operating in a graphics mode, it will call this
	// function after graphics and window functions have been initialized.
	// This will update the contents of the supplied text area with the
	// previous contents of the text screen to the supplied text area, if any,
	// and associate that text area with the console output stream.

	// Don't do anything unless we've been initialized
	if (!initialized)
		return (ERR_NOTINITIALIZED);

	// For now, don't allow area to be NULL
	if (!area)
		return (ERR_NULLPARAMETER);

	// Assign the text area to the console output stream
	consoleOutput->textArea = area;
	consoleOutput->outputDriver =
		kernelSoftwareDriverGet(graphicConsoleDriver);

	// Done
	return (0);
}


kernelTextInputStream *kernelTextGetConsoleInput(void)
{
	// Returns a pointer to the console input stream

	// Don't do anything unless we've been initialized
	if (!initialized)
		return (NULL);

	return (consoleInput);
}


int kernelTextSetConsoleInput(kernelTextInputStream *newInput)
{
	// Sets the console input to be something else.  We copy the data from the
	// supplied stream to the static one upstairs

	// Don't do anything unless we've been initialized
	if (!initialized)
		return (ERR_NOTINITIALIZED);

	// If the input stream is NULL, use our default area
	if (!newInput)
		consoleInput = consoleArea.inputStream;
	else
		consoleInput = newInput;

	return (0);
}


kernelTextOutputStream *kernelTextGetConsoleOutput(void)
{
	// Returns a pointer to the console output stream

	// Don't do anything unless we've been initialized
	if (!initialized)
		return (NULL);

	return (consoleOutput);
}


int kernelTextSetConsoleOutput(kernelTextOutputStream *newOutput)
{
	// Sets the console output to be something else.  We copy the data from
	// the supplied stream to the static one upstairs

	// Don't do anything unless we've been initialized
	if (!initialized)
		return (ERR_NOTINITIALIZED);

	// If the output stream is NULL, use our default area
	if (!newOutput)
		consoleOutput = consoleArea.outputStream;
	else
		consoleOutput = newOutput;

	return (0);
}


kernelTextInputStream *kernelTextGetCurrentInput(void)
{
	// Returns a pointer to the current input stream

	// Don't do anything unless we've been initialized
	if (!initialized)
		return (NULL);

	return (currentInput);
}


int kernelTextSetCurrentInput(kernelTextInputStream *newInput)
{
	// Sets the current input to be something else.  We copy the data from the
	// supplied stream to the static one upstairs

	int status = 0;

	// Don't do anything unless we've been initialized
	if (!initialized)
		return (status = ERR_NOTINITIALIZED);

	// The input stream is allowed to be NULL.  This can happen if the current
	// current input stream is going away
	if (!newInput)
		newInput = consoleInput;

	currentInput = newInput;

	// Tell the keyboard driver to append all new input to this stream
	status = kernelKeyboardSetStream(&currentInput->s);

	return (status);
}


kernelTextOutputStream *kernelTextGetCurrentOutput(void)
{
	// Returns a pointer to the current output stream

	// Don't do anything unless we've been initialized
	if (!initialized)
		return (NULL);

	return (currentOutput);
}


int kernelTextSetCurrentOutput(kernelTextOutputStream *newOutput)
{
	// Sets the current output to be something else.  We copy the data from
	// the supplied stream to the static one upstairs

	// Don't do anything unless we've been initialized
	if (!initialized)
		return (ERR_NOTINITIALIZED);

	// The output stream is allowed to be NULL.  This can happen if the
	// current output stream is going away
	if (!newOutput)
		newOutput = consoleOutput;

	currentOutput = newOutput;

	return (0);
}


int kernelTextNewInputStream(kernelTextInputStream *newStream)
{
	// Create a new kernelTextInputStream.

	int status = 0;

	// Don't do anything unless we've been initialized
	if (!initialized)
		return (status = ERR_NOTINITIALIZED);

	// Check params
	if (!newStream)
		return (status = ERR_NULLPARAMETER);

	status = kernelStreamNew(&newStream->s, TEXT_STREAMSIZE, itemsize_byte);
	if (status < 0)
		return (status);

	// We want to be able to intercept things as they're put into the input
	// stream, so we can catch keyboard interrupts and such.
	newStream->s.intercept = newStream->s.append;
	newStream->s.append = (int (*)(stream *, ...)) &currentInputIntercept;
	newStream->attrs.echo = 1;

	return (status = 0);
}


int kernelTextNewOutputStream(kernelTextOutputStream *newStream)
{
	// Create a new kernelTextOutputStream.

	// Don't do anything unless we've been initialized
	if (!initialized)
		return (ERR_NOTINITIALIZED);

	// Check params
	if (!newStream)
		return (ERR_NULLPARAMETER);

	newStream->outputDriver = kernelSoftwareDriverGet(graphicConsoleDriver);
	newStream->textArea = NULL;

	return (0);
}


int kernelTextGetForeground(color *foreground)
{
	kernelTextOutputStream *outputStream = NULL;

	// Don't do anything unless we've been initialized
	if (!initialized)
		return (ERR_NOTINITIALIZED);

	// Check params
	if (!foreground)
		return (ERR_NULLPARAMETER);

	// Get the text output stream for the current process
	outputStream = kernelMultitaskerGetTextOutput();
	if (!outputStream)
		return (ERR_INVALID);

	memcpy(foreground, (color *) &outputStream->textArea->foreground,
		sizeof(color));

	return (0);
}


int kernelTextSetForeground(color *foreground)
{
	// Sets the foreground color of the screen output.

	int status = 0;
	kernelTextOutputStream *outputStream = NULL;

	// Don't do anything unless we've been initialized
	if (!initialized)
		return (status = ERR_NOTINITIALIZED);

	// Check params
	if (!foreground)
		return (status = ERR_NULLPARAMETER);

	// Get the text output stream for the current process
	outputStream = kernelMultitaskerGetTextOutput();
	if (!outputStream)
		return (status = ERR_INVALID);

	if (outputStream->outputDriver->setForeground)
	{
		// Set it in the text output driver
		status = outputStream->outputDriver->setForeground(
			outputStream->textArea, foreground);
	}

	memcpy((color *) &outputStream->textArea->foreground, foreground,
		sizeof(color));

	return (status);
}


int kernelTextGetBackground(color *background)
{
	kernelTextOutputStream *outputStream = NULL;

	// Don't do anything unless we've been initialized
	if (!initialized)
		return (ERR_NOTINITIALIZED);

	// Check params
	if (!background)
		return (ERR_NULLPARAMETER);

	// Get the text output stream for the current process
	outputStream = kernelMultitaskerGetTextOutput();
	if (!outputStream)
		return (ERR_INVALID);

	memcpy(background, (color *) &outputStream->textArea->background,
		sizeof(color));

	return (0);
}


int kernelTextSetBackground(color *background)
{
	// Sets the background color of the screen output.

	int status = 0;
	kernelTextOutputStream *outputStream = NULL;

	// Don't do anything unless we've been initialized
	if (!initialized)
		return (status = ERR_NOTINITIALIZED);

	// Check params
	if (!background)
		return (status = ERR_NULLPARAMETER);

	// Get the text output stream for the current process
	outputStream = kernelMultitaskerGetTextOutput();
	if (!outputStream)
		return (status = ERR_INVALID);

	// Set it in the text output driver
	if (outputStream->outputDriver->setBackground)
	{
		status = outputStream->outputDriver->setBackground(
			outputStream->textArea, background);
	}

	memcpy((color *) &outputStream->textArea->background, background,
		sizeof(color));

	return (status);
}


int kernelTextStreamPutc(kernelTextOutputStream *outputStream, int ascii)
{
	int status = 0;
	char theChar[2];

	// Don't do anything unless we've been initialized
	if (!initialized)
		return (status = ERR_NOTINITIALIZED);

	// Check params
	if (!outputStream)
		return (status = ERR_NULLPARAMETER);

	if (!outputStream->outputDriver || !outputStream->outputDriver->print)
		return (status = ERR_NOSUCHFUNCTION);

	theChar[0] = ascii;
	theChar[1] = '\0';

	// Call the text stream output driver function with the character we were
	// passed
	return (status = outputStream->outputDriver->print(outputStream->textArea,
		theChar, NULL));
}


int kernelTextPutc(int ascii)
{
	// Determines the current target of character output, then makes calls to
	// output the character.  Returns 0 if successful, negative otherwise.

	kernelTextOutputStream *outputStream = NULL;

	// Get the text output stream for the current process
	outputStream = kernelMultitaskerGetTextOutput();

	return (kernelTextStreamPutc(outputStream, ascii));
}


int kernelTextStreamPrint(kernelTextOutputStream *outputStream,
	const char *output)
{
	int status = 0;

	// Don't do anything unless we've been initialized
	if (!initialized)
		return (status = ERR_NOTINITIALIZED);

	// Check params
	if (!outputStream || !output)
		return (status = ERR_NULLPARAMETER);

	if (!outputStream->outputDriver || !outputStream->outputDriver->print)
		return (status = ERR_NOSUCHFUNCTION);

	// We will call the text stream output driver function with the
	// characters we were passed
	return (status = outputStream->outputDriver->print(outputStream->textArea,
		output, NULL));
}


int kernelTextPrint(const char *format, ...)
{
	// Determines the current target of character output, then makes calls to
	// output the text (without a newline).  Returns 0 if successful, negative
	// otherwise.

	va_list list;
	char output[MAXSTRINGLENGTH];
	kernelTextOutputStream *outputStream = NULL;

	// Check params
	if (!format)
		return (ERR_NULLPARAMETER);

	// Initialize the argument list
	va_start(list, format);

	// Expand the format string into an output string
	vsnprintf(output, MAXSTRINGLENGTH, format, list);

	va_end(list);

	// Get the text output stream for the current process
	outputStream = kernelMultitaskerGetTextOutput();

	return (kernelTextStreamPrint(outputStream, output));
}


int kernelTextStreamPrintAttrs(kernelTextOutputStream *outputStream,
	textAttrs *attrs, const char *output)
{
	int status = 0;

	// Don't do anything unless we've been initialized
	if (!initialized)
		return (status = ERR_NOTINITIALIZED);

	// Check params.  It's pointless for 'attrs' to be NULL, but it's allowed
	if (!outputStream || !output)
		return (status = ERR_NULLPARAMETER);

	if (!outputStream->outputDriver || !outputStream->outputDriver->print)
		return (status = ERR_NOSUCHFUNCTION);

	// We will call the text stream output driver function with the characters
	// we were passed
	return (status = outputStream->outputDriver->print(outputStream->textArea,
		output, attrs));
}


int kernelTextPrintAttrs(textAttrs *attrs, const char *format, ...)
{
	// Determines the current target of character output, then makes calls to
	// output the text (without a newline).  Returns 0 if successful, negative
	// otherwise.

	va_list list;
	char output[MAXSTRINGLENGTH];
	kernelTextOutputStream *outputStream = NULL;

	// Check params
	if (!attrs || !format)
		return (ERR_NULLPARAMETER);

	// Initialize the argument list
	va_start(list, format);

	if (attrs->flags & TEXT_ATTRS_NOFORMAT)
		// Just copy the format string to the output string
		strncpy(output, format, MAXSTRINGLENGTH);
	else
		// Expand the format string into an output string
		vsnprintf(output, MAXSTRINGLENGTH, format, list);

	va_end(list);

	// Get the text output stream for the current process
	outputStream = kernelMultitaskerGetTextOutput();

	return (kernelTextStreamPrintAttrs(outputStream, attrs, output));
}


int kernelTextStreamPrintLine(kernelTextOutputStream *outputStream,
	const char *output)
{
	// Output text with a newline

	int status = 0;

	// Don't do anything unless we've been initialized
	if (!initialized)
		return (status = ERR_NOTINITIALIZED);

	// Check params
	if (!outputStream || !output)
		return (status = ERR_NULLPARAMETER);

	if (!outputStream->outputDriver || !outputStream->outputDriver->print)
		return (status = ERR_NOSUCHFUNCTION);

	// We will call the text stream output driver function with the characters
	// we were passed
	status = outputStream->outputDriver->print(outputStream->textArea, output,
		NULL);

	// Print the newline too
	outputStream->outputDriver->print(outputStream->textArea, "\n", NULL);

	return (status);
}


int kernelTextPrintLine(const char *format, ...)
{
	// Determines the current target of character output, then makes calls to
	// output the text with a newline.

	va_list list;
	char output[MAXSTRINGLENGTH];
	kernelTextOutputStream *outputStream = NULL;

	// Check params
	if (!format)
		return (ERR_NULLPARAMETER);

	// Initialize the argument list
	va_start(list, format);

	// Expand the format string into an output string
	vsnprintf(output, MAXSTRINGLENGTH, format, list);

	va_end(list);

	// Get the text output stream for the current process
	outputStream = kernelMultitaskerGetTextOutput();

	return (kernelTextStreamPrintLine(outputStream, output));
}


void kernelTextStreamNewline(kernelTextOutputStream *outputStream)
{
	// Outputs a newline

	// Don't do anything unless we've been initialized
	if (!initialized)
		return;

	// Check params
	if (!outputStream)
		return;

	if (!outputStream->outputDriver || !outputStream->outputDriver->print)
		return;

	// Call the text stream output driver function to print the newline
	outputStream->outputDriver->print(outputStream->textArea, "\n", NULL);
}


void kernelTextNewline(void)
{
	// Outputs a newline to the current output stream

	kernelTextOutputStream *outputStream = NULL;

	// Get the text output stream for the current process
	outputStream = kernelMultitaskerGetTextOutput();

	kernelTextStreamNewline(outputStream);
}


void kernelTextStreamBackSpace(kernelTextOutputStream *outputStream)
{
	// Outputs a backspace (or delete)

	int cursorColumn = 0;
	int cursorRow = 0;

	// Don't do anything unless we've been initialized
	if (!initialized)
		return;

	// Check params
	if (!outputStream)
		return;

	if (!outputStream->textArea || !outputStream->outputDriver)
		return;

	// We will call the text stream output driver functions to make the
	// backspace appear.  Move the cursor back one position
	cursorRow = outputStream->textArea->cursorRow;
	cursorColumn = outputStream->textArea->cursorColumn;

	if (!cursorRow && !cursorColumn)
		// Already top left
		return;

	cursorColumn--;
	if (cursorColumn < 0)
	{
		cursorRow--;
		cursorColumn = (outputStream->textArea->columns - 1);
	}

	if (outputStream->outputDriver->setCursorAddress)
	{
		outputStream->outputDriver->setCursorAddress(outputStream->textArea,
			cursorRow, cursorColumn);
	}

	if (outputStream->outputDriver->delete)
		outputStream->outputDriver->delete(outputStream->textArea);
}


void kernelTextBackSpace(void)
{
	// Outputs a backspace (or delete) to the current output stream

	kernelTextOutputStream *outputStream = NULL;

	// Get the text output stream for the current process
	outputStream = kernelMultitaskerGetTextOutput();

	kernelTextStreamBackSpace(outputStream);
}


void kernelTextStreamTab(kernelTextOutputStream *outputStream)
{
	// Outputs a hoizontal tab

	int tabChars = 0;
	char spaces[TEXT_DEFAULT_TAB + 1];
	int count;

	// Don't do anything unless we've been initialized
	if (!initialized)
		return;

	// Check params
	if (!outputStream)
		return;

	if (!outputStream->outputDriver || !outputStream->outputDriver->print)
		return;

	// Figure out how many characters the tab should be
	tabChars = TEXT_DEFAULT_TAB;
	if (outputStream->outputDriver->getCursorAddress)
	{
		tabChars -= (outputStream->outputDriver->getCursorAddress(
			outputStream->textArea) % TEXT_DEFAULT_TAB);
	}

	if (!tabChars)
		tabChars = TEXT_DEFAULT_TAB;

	// Fill up the spaces buffer with the appropriate number of spaces
	for (count = 0; count < tabChars; count ++)
		spaces[count] = ' ';
	spaces[count] = NULL;

	// Call the text stream output driver to print the spaces
	outputStream->outputDriver->print(outputStream->textArea, spaces, NULL);
}


void kernelTextTab(void)
{
	// Outputs a hoizontal tab to the current output stream

	kernelTextOutputStream *outputStream = NULL;

	// Get the text output stream for the current process
	outputStream = kernelMultitaskerGetTextOutput();

	kernelTextStreamTab(outputStream);
}


void kernelTextStreamCursorUp(kernelTextOutputStream *outputStream)
{
	// Executes a 'cursor up' operation

	// Don't do anything unless we've been initialized
	if (!initialized)
		return;

	// Check params
	if (!outputStream)
		return;

	if (!outputStream->textArea || !outputStream->outputDriver ||
		!outputStream->outputDriver->setCursorAddress)
	{
		return;
	}

	// We will call the text stream output driver functions to make the cursor
	// move up one row
	if (outputStream->textArea->cursorRow > 0)
	{
		outputStream->outputDriver->setCursorAddress(outputStream->textArea,
			(outputStream->textArea->cursorRow - 1),
			outputStream->textArea->cursorColumn);
	}
}


void kernelTextCursorUp(void)
{
	// Executes a 'cursor up' operation on the current output stream

	kernelTextOutputStream *outputStream = NULL;

	// Get the text output stream for the current process
	outputStream = kernelMultitaskerGetTextOutput();

	kernelTextStreamCursorUp(outputStream);
}


void kernelTextStreamCursorDown(kernelTextOutputStream *outputStream)
{
	// Executes a 'cursor down' operation

	// Don't do anything unless we've been initialized
	if (!initialized)
		return;

	// Check params
	if (!outputStream)
		return;

	if (!outputStream->textArea || !outputStream->outputDriver ||
		!outputStream->outputDriver->setCursorAddress)
	{
		return;
	}

	// We will call the text stream output driver functions to make the
	// cursor move down one row
	if (outputStream->textArea->cursorRow <
		(outputStream->textArea->rows - 1))
	{
		outputStream->outputDriver->setCursorAddress(outputStream->textArea,
			(outputStream->textArea->cursorRow + 1),
			outputStream->textArea->cursorColumn);
	}
}


void kernelTextCursorDown(void)
{
	// Executes a 'cursor down' operation on the current output stream

	kernelTextOutputStream *outputStream = NULL;

	// Get the text output stream for the current process
	outputStream = kernelMultitaskerGetTextOutput();

	kernelTextStreamCursorDown(outputStream);
}


void kernelTextStreamCursorLeft(kernelTextOutputStream *outputStream)
{
	// Executes a 'cursor left' operation

	int cursorColumn = 0;
	int cursorRow = 0;

	// Don't do anything unless we've been initialized
	if (!initialized)
		return;

	// Check params
	if (!outputStream)
		return;

	if (!outputStream->textArea || !outputStream->outputDriver)
		return;

	// We will call the text stream output driver functions to make the
	// backspace appear.  Move the cursor back one position
	cursorRow = outputStream->textArea->cursorRow;
	cursorColumn = outputStream->textArea->cursorColumn;

	if (!cursorRow && !cursorColumn)
		// Already top left
		return;

	cursorColumn--;
	if (cursorColumn < 0)
	{
		cursorRow--;
		cursorColumn = (outputStream->textArea->columns - 1);
	}

	if (outputStream->outputDriver->setCursorAddress)
	{
		outputStream->outputDriver->setCursorAddress(outputStream->textArea,
			cursorRow, cursorColumn);
	}
}


void kernelTextCursorLeft(void)
{
	// Executes a 'cursor left' operation on the current output stream

	kernelTextOutputStream *outputStream = NULL;

	// Get the text output stream for the current process
	outputStream = kernelMultitaskerGetTextOutput();

	kernelTextStreamCursorLeft(outputStream);
}


void kernelTextStreamCursorRight(kernelTextOutputStream *outputStream)
{
	// Executes a 'cursor right' operation

	int cursorColumn = 0;
	int cursorRow = 0;

	// Don't do anything unless we've been initialized
	if (!initialized)
		return;

	// Check params
	if (!outputStream)
		return;

	if (!outputStream->textArea || !outputStream->outputDriver)
		return;

	// We will call the text stream output driver functions to make the
	// backspace appear.  Move the cursor back one position
	cursorRow = outputStream->textArea->cursorRow;
	cursorColumn = outputStream->textArea->cursorColumn;

	if ((cursorRow == (outputStream->textArea->rows - 1)) &&
		(cursorColumn == (outputStream->textArea->columns - 1)))
	{
		// Already bottom right
		return;
	}

	cursorColumn++;
	if (cursorColumn == outputStream->textArea->columns)
	{
		cursorRow++;
		cursorColumn = 0;
	}

	if (outputStream->outputDriver->setCursorAddress)
	{
		outputStream->outputDriver->setCursorAddress(outputStream->textArea,
			cursorRow, cursorColumn);
	}
}


void kernelTextCursorRight(void)
{
	// Executes a 'cursor right' operation on the current output stream

	kernelTextOutputStream *outputStream = NULL;

	// Get the text output stream for the current process
	outputStream = kernelMultitaskerGetTextOutput();

	kernelTextStreamCursorRight(outputStream);
}


int kernelTextStreamEnableScroll(kernelTextOutputStream *outputStream,
	int enable)
{
	// Enable or disable screen scrolling for the supplied text output stream

	// Don't do anything unless we've been initialized
	if (!initialized)
		return (ERR_NOTINITIALIZED);

	// Check params
	if (!outputStream)
		return (ERR_NULLPARAMETER);

	if (enable)
		outputStream->textArea->noScroll = 0;
	else
		outputStream->textArea->noScroll = 1;

	return (0);
}


int kernelTextEnableScroll(int enable)
{
	// Enable or disable screen scrolling for the current text output stream

	kernelTextOutputStream *outputStream = NULL;

	// Get the text output stream for the current process
	outputStream = kernelMultitaskerGetTextOutput();

	return (kernelTextStreamEnableScroll(outputStream, enable));
}


void kernelTextStreamScroll(kernelTextOutputStream *outputStream, int upDown)
{
	// Scroll the text area up (-upDown) or down (+upDown)

	// Don't do anything unless we've been initialized
	if (!initialized)
		return;

	// Check params
	if (!outputStream)
		return;

	if (!outputStream->textArea || !outputStream->outputDriver)
		return;

	if ((upDown < 0) && (outputStream->textArea->scrolledBackLines <
		outputStream->textArea->scrollBackLines))
	{
		// Scroll up by upDown screenfuls
		outputStream->textArea->scrolledBackLines +=
			min((outputStream->textArea->rows * -upDown),
				(outputStream->textArea->scrollBackLines -
		 			outputStream->textArea->scrolledBackLines));
	}
	else if ((upDown > 0) && outputStream->textArea->scrolledBackLines)
	{
		// Scroll down by upDown screenfuls
		outputStream->textArea->scrolledBackLines -=
			min((outputStream->textArea->rows * upDown),
				outputStream->textArea->scrolledBackLines);
	}

	updateComponent(outputStream->textArea);

	// We will call the text stream output driver functions to scroll the
	// screen to the specified area
	if (outputStream->outputDriver->screenDraw)
		outputStream->outputDriver->screenDraw(outputStream->textArea);
}


void kernelTextScroll(int upDown)
{
	// Scroll the text area of the current output stream up (-updown) or down
	// (+upDown)

	kernelTextOutputStream *outputStream = NULL;

	// Get the text output stream for the current process
	outputStream = kernelMultitaskerGetTextOutput();

	kernelTextStreamScroll(outputStream, upDown);
}


int kernelTextStreamGetNumColumns(kernelTextOutputStream *outputStream)
{
	// Returns the number of columns

	// Don't do anything unless we've been initialized
	if (!initialized)
		return (ERR_NOTINITIALIZED);

	// Check params
	if (!outputStream)
		return (ERR_NULLPARAMETER);

	return (outputStream->textArea->columns);
}


int kernelTextGetNumColumns(void)
{
	// Returns the number of columns of the current output stream

	kernelTextOutputStream *outputStream = NULL;

	// Get the text output stream for the current process
	outputStream = kernelMultitaskerGetTextOutput();

	return (kernelTextStreamGetNumColumns(outputStream));
}


int kernelTextStreamGetNumRows(kernelTextOutputStream *outputStream)
{
	// Returns the number of rows

	// Don't do anything unless we've been initialized
	if (!initialized)
		return (ERR_NOTINITIALIZED);

	// Check params
	if (!outputStream)
		return (ERR_NULLPARAMETER);

	return (outputStream->textArea->rows);
}


int kernelTextGetNumRows(void)
{
	// Returns the number of rows of the current output stream

	kernelTextOutputStream *outputStream = NULL;

	// Get the text output stream for the current process
	outputStream = kernelMultitaskerGetTextOutput();

	return (kernelTextStreamGetNumRows(outputStream));
}


int kernelTextStreamGetColumn(kernelTextOutputStream *outputStream)
{
	// Returns the (zero-based) cursor column

	// Don't do anything unless we've been initialized
	if (!initialized)
		return (0);

	// Check params
	if (!outputStream || !outputStream->textArea)
		return (0);

	return (outputStream->textArea->cursorColumn);
}


int kernelTextGetColumn(void)
{
	// Returns the (zero-based) cursor column of the current output stream

	kernelTextOutputStream *outputStream = NULL;

	// Get the text output stream for the current process
	outputStream = kernelMultitaskerGetTextOutput();

	return (kernelTextStreamGetColumn(outputStream));
}


void kernelTextStreamSetColumn(kernelTextOutputStream *outputStream,
	int newColumn)
{
	// Sets the (zero-based) cursor column

	// Don't do anything unless we've been initialized
	if (!initialized)
		return;

	// Check params
	if (!outputStream || (newColumn < 0))
		return;

	if (!outputStream->outputDriver ||
		!outputStream->outputDriver->setCursorAddress)
	{
		return;
	}

	outputStream->outputDriver->setCursorAddress(outputStream->textArea,
		outputStream->textArea->cursorRow, newColumn);
}


void kernelTextSetColumn(int newColumn)
{
	// Sets the (zero-based) cursor column of the current output stream,
	// leaving it in the same row as before

	kernelTextOutputStream *outputStream = NULL;

	// Check params
	if (newColumn < 0)
		return;

	// Get the text output stream for the current process
	outputStream = kernelMultitaskerGetTextOutput();

	kernelTextStreamSetColumn(outputStream, newColumn);
}


int kernelTextStreamGetRow(kernelTextOutputStream *outputStream)
{
	// Returns the (zero-based) cursor row

	// Don't do anything unless we've been initialized
	if (!initialized)
		return (0);

	// Check params
	if (!outputStream || !outputStream->textArea)
		return (0);

	return (outputStream->textArea->cursorRow);
}


int kernelTextGetRow(void)
{
	// Returns the (zero-based) cursor row of the current output stream

	kernelTextOutputStream *outputStream = NULL;

	// Get the text output stream for the current process
	outputStream = kernelMultitaskerGetTextOutput();

	return (kernelTextStreamGetRow(outputStream));
}


void kernelTextStreamSetRow(kernelTextOutputStream *outputStream, int newRow)
{
	// Sets the (zero-based) cursor row

	// Don't do anything unless we've been initialized
	if (!initialized)
		return;

	// Check params
	if (!outputStream || (newRow < 0))
		return;

	if (!outputStream->outputDriver ||
		!outputStream->outputDriver->setCursorAddress)
	{
		return;
	}

	outputStream->outputDriver->setCursorAddress(outputStream->textArea,
		newRow, outputStream->textArea->cursorColumn);
}


void kernelTextSetRow(int newRow)
{
	// Sets the (zero-based) cursor row of the current output stream, leaving
	// it in the same column as before

	kernelTextOutputStream *outputStream = NULL;

	// Check params
	if (newRow < 0)
		return;

	// Get the text output stream for the current process
	outputStream = kernelMultitaskerGetTextOutput();

	kernelTextStreamSetRow(outputStream, newRow);
}


void kernelTextStreamSetCursor(kernelTextOutputStream *outputStream, int on)
{
	// Turns the appearance of the cursor on or off

	// Don't do anything unless we've been initialized
	if (!initialized)
		return;

	// Check params
	if (!outputStream || !outputStream->outputDriver ||
		!outputStream->outputDriver->setCursor)
	{
		return;
	}

	// Call the text stream output driver function to clear the screen
	outputStream->outputDriver->setCursor(outputStream->textArea, on);
}


void kernelTextSetCursor(int on)
{
	// Turns the appearance of the cursor on or off in the current output
	// stream

	kernelTextOutputStream *outputStream = NULL;

	// Get the text output stream for the current process
	outputStream = kernelMultitaskerGetTextOutput();

	kernelTextStreamSetCursor(outputStream, on);
}


void kernelTextStreamScreenClear(kernelTextOutputStream *outputStream)
{
	// Clears the 'screen' of the output stream's text area

	// Don't do anything unless we've been initialized
	if (!initialized)
		return;

	// Check params
	if (!outputStream || !outputStream->outputDriver ||
		!outputStream->outputDriver->screenClear)
	{
		return;
	}

	// Call the text stream output driver function to clear the screen
	outputStream->outputDriver->screenClear(outputStream->textArea);
}


void kernelTextScreenClear(void)
{
	// Clears the 'screen' of the current output stream's text area

	kernelTextOutputStream *outputStream = NULL;

	// Get the text output stream for the current process
	outputStream = kernelMultitaskerGetTextOutput();

	kernelTextStreamScreenClear(outputStream);
}


int kernelTextScreenSave(textScreen *screen)
{
	// Saves the contents of the 'screen' of the current output stream

	kernelTextOutputStream *outputStream = NULL;

	// Don't do anything unless we've been initialized
	if (!initialized)
		return (ERR_NOTINITIALIZED);

	// Check params
	if (!screen)
		return (ERR_NULLPARAMETER);

	// Get the text output stream for the current process
	outputStream = kernelMultitaskerGetTextOutput();
	if (!outputStream)
		return (ERR_NOSUCHENTRY);

	if (!outputStream->textArea || !outputStream->outputDriver ||
		!outputStream->outputDriver->screenSave)
	{
		return (ERR_NOSUCHFUNCTION);
	}

	return (outputStream->outputDriver->screenSave((kernelTextArea *)
		outputStream->textArea, screen));
}


int kernelTextScreenRestore(textScreen *screen)
{
	// Restores the saved contents to the 'screen' of the current output
	// stream

	kernelTextOutputStream *outputStream = NULL;

	// Don't do anything unless we've been initialized
	if (!initialized)
		return (ERR_NOTINITIALIZED);

	// Check params
	if (!screen)
		return (ERR_NULLPARAMETER);

	// Get the text output stream for the current process
	outputStream = kernelMultitaskerGetTextOutput();
	if (!outputStream)
		return (ERR_NOSUCHENTRY);

	if (!outputStream->textArea || !outputStream->outputDriver ||
		!outputStream->outputDriver->screenRestore)
	{
		return (ERR_NOSUCHFUNCTION);
	}

	return (outputStream->outputDriver->screenRestore((kernelTextArea *)
		outputStream->textArea, screen));
}


int kernelTextInputStreamCount(kernelTextInputStream *inputStream)
{
	// Returns the number of characters that are currently waiting in the
	// input stream.

	int numberChars = 0;

	// Don't do anything unless we've been initialized
	if (!initialized)
		return (ERR_NOTINITIALIZED);

	if (!inputStream)
	{
		inputStream = kernelMultitaskerGetTextInput();
		if (!inputStream)
			return (ERR_NOSUCHENTRY);
	}

	// Get the number of characters in the stream
	numberChars = inputStream->s.count;

	// Return the value from the call
	return (numberChars);
}


int kernelTextInputCount(void)
{
	return (kernelTextInputStreamCount(NULL));
}


int kernelTextInputStreamGetc(kernelTextInputStream *inputStream,
	char *returnChar)
{
	// Returns a single character from the keyboard buffer.

	int status = 0;

	// Don't do anything unless we've been initialized
	if (!initialized)
		return (status = ERR_NOTINITIALIZED);

	// Check params
	if (!returnChar)
		return (status = ERR_NULLPARAMETER);

	if (!inputStream)
	{
		inputStream = kernelMultitaskerGetTextInput();
		if (!inputStream)
			return (status = ERR_NOSUCHENTRY);
	}

	// Wait for something to be there.
	while (!inputStream->s.count)
		kernelMultitaskerYield();

	// Call the 'pop' function for this stream
	status = inputStream->s.pop(&inputStream->s, returnChar);

	// Return the status from the call
	return (status);
}


int kernelTextInputGetc(char *returnChar)
{
	return (kernelTextInputStreamGetc(NULL, returnChar));
}


int kernelTextInputStreamPeek(kernelTextInputStream *inputStream,
	char *returnChar)
{
	// Returns a single character from the keyboard buffer.

	int status = 0;

	// Don't do anything unless we've been initialized
	if (!initialized)
		return (status = ERR_NOTINITIALIZED);

	// Check params
	if (!returnChar)
		return (status = ERR_NULLPARAMETER);

	if (!inputStream)
	{
		inputStream = kernelMultitaskerGetTextInput();
		if (!inputStream)
			return (status = ERR_NOSUCHENTRY);
	}

	// Call the 'peek' function for this stream
	status = inputStream->s.peek(&inputStream->s, returnChar);

	// Return the status from the call
	return (status);
}


int kernelTextInputPeek(char *returnChar)
{
	return (kernelTextInputStreamPeek(NULL, returnChar));
}


int kernelTextInputStreamReadN(kernelTextInputStream *inputStream,
	int numberRequested, char *returnChars)
{
	// Gets the requested number of characters from the keyboard buffer,
	// and puts them in the string supplied.

	int status = 0;

	// Don't do anything unless we've been initialized
	if (!initialized)
		return (status = ERR_NOTINITIALIZED);

	// Check params
	if (!returnChars || (numberRequested <= 0))
		return (status = ERR_NULLPARAMETER);

	if (!inputStream)
	{
		inputStream = kernelMultitaskerGetTextInput();
		if (!inputStream)
			return (status = ERR_NOSUCHENTRY);
	}

	// Call the 'popN' function for this stream
	status = inputStream->s.popN(&inputStream->s, numberRequested,
		returnChars);

	// Return the status from the call
	return (status);
}


int kernelTextInputReadN(int numberRequested, char *returnChars)
{
	return (kernelTextInputStreamReadN(NULL, numberRequested, returnChars));
}


int kernelTextInputStreamReadAll(kernelTextInputStream *inputStream,
	char *returnChars)
{
	// Takes a pointer to an initialized character array, and fills it with
	// all of the characters present in the buffer.

	int status = 0;

	// Don't do anything unless we've been initialized
	if (!initialized)
		return (status = ERR_NOTINITIALIZED);

	// Check params
	if (!returnChars)
		return (status = ERR_NULLPARAMETER);

	if (!inputStream)
	{
		inputStream = kernelMultitaskerGetTextInput();
		if (!inputStream)
			return (status = ERR_NOSUCHENTRY);
	}

	// Get all of the characters in the stream.  Call the 'popN' function for
	// this stream
	status = inputStream->s.popN(&inputStream->s, inputStream->s.count,
		returnChars);

	// Return the status from the call
	return (status);
}


int kernelTextInputReadAll(char *returnChars)
{
	return (kernelTextInputStreamReadAll(NULL, returnChars));
}


int kernelTextInputStreamAppend(kernelTextInputStream *inputStream, int ascii)
{
	// Adds a single character to the text input stream.

	int status = 0;

	// Don't do anything unless we've been initialized
	if (!initialized)
		return (status = ERR_NOTINITIALIZED);

	if (!inputStream)
	{
		inputStream = kernelMultitaskerGetTextInput();
		if (!inputStream)
			return (status = ERR_NOSUCHENTRY);
	}

	// Call the 'append' function for this stream
	status = inputStream->s.append(&inputStream->s, (unsigned char) ascii);

	// Return the status from the call
	return (status);
}


int kernelTextInputAppend(int ascii)
{
	return (kernelTextInputStreamAppend(NULL, ascii));
}


int kernelTextInputStreamAppendN(kernelTextInputStream *inputStream,
	int numberRequested, char *addCharacters)
{
	// Adds the requested number of characters to the text input stream.

	int status = 0;

	// Don't do anything unless we've been initialized
	if (!initialized)
		return (status = ERR_NOTINITIALIZED);

	// Check params
	if (!addCharacters || (numberRequested <= 0))
		return (status = ERR_NULLPARAMETER);

	if (!inputStream)
	{
		inputStream = kernelMultitaskerGetTextInput();
		if (!inputStream)
			return (status = ERR_NOSUCHENTRY);
	}

	// Call the 'appendN' function for this stream
	status = inputStream->s.appendN(&inputStream->s, numberRequested,
		addCharacters);

	// Return the status from the call
	return (status);
}


int kernelTextInputAppendN(int numberRequested, char *addCharacters)
{
	return (kernelTextInputStreamAppendN(NULL, numberRequested,
		addCharacters));
}


int kernelTextInputStreamRemove(kernelTextInputStream *inputStream)
{
	// Removes a single character from the keyboard buffer.

	int status = 0;
	char junk = NULL;

	// Don't do anything unless we've been initialized
	if (!initialized)
		return (status = ERR_NOTINITIALIZED);

	if (!inputStream)
	{
		inputStream = kernelMultitaskerGetTextInput();
		if (!inputStream)
			return (status = ERR_NOSUCHENTRY);
	}

	// Call the 'pop' function for this stream, and discard the char we get
	// back.
	status = inputStream->s.pop(&inputStream->s, &junk);

	// Return the status from the call
	return (status);
}


int kernelTextInputRemove(void)
{
	return (kernelTextInputStreamRemove(NULL));
}


int kernelTextInputStreamRemoveN(kernelTextInputStream *inputStream,
	int numberRequested)
{
	// Removes the requested number of characters from the keyboard buffer.

	int status = 0;
	char junk[TEXT_STREAMSIZE];

	// Don't do anything unless we've been initialized
	if (!initialized)
		return (status = ERR_NOTINITIALIZED);

	// Check params
	if (numberRequested <= 0)
		return (status = ERR_RANGE);

	if (!inputStream)
	{
		inputStream = kernelMultitaskerGetTextInput();
		if (!inputStream)
			return (status = ERR_NOSUCHENTRY);
	}

	// Call the 'popN' function for this stream, and discard the chars we get
	// back.
	status = inputStream->s.popN(&inputStream->s, numberRequested, junk);

	// Return the status from the call
	return (status);
}


int kernelTextInputRemoveN(int numberRequested)
{
	return (kernelTextInputStreamRemoveN(NULL, numberRequested));
}


int kernelTextInputStreamRemoveAll(kernelTextInputStream *inputStream)
{
	// Removes all data from the keyboard buffer.

	int status = 0;

	// Don't do anything unless we've been initialized
	if (!initialized)
		return (status = ERR_NOTINITIALIZED);

	if (!inputStream)
	{
		inputStream = kernelMultitaskerGetTextInput();
		if (!inputStream)
			return (status = ERR_NOSUCHENTRY);
	}

	// Call the 'clear' function for this stream
	status = inputStream->s.clear(&inputStream->s);

	// Return the status from the call
	return (status);
}


int kernelTextInputRemoveAll(void)
{
	return (kernelTextInputStreamRemoveAll(NULL));
}


void kernelTextInputStreamSetEcho(kernelTextInputStream *inputStream,
	int onOff)
{
	// Turn input echoing on or off

	// Don't do anything unless we've been initialized
	if (!initialized)
		return;

	if (!inputStream)
	{
		inputStream = kernelMultitaskerGetTextInput();
		if (!inputStream)
			return;
	}

	inputStream->attrs.echo = onOff;
}


void kernelTextInputSetEcho(int onOff)
{
	kernelTextInputStreamSetEcho(NULL, onOff);
}

