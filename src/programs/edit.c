//
//  Visopsys
//  Copyright (C) 1998-2016 J. Andrew McLaughlin
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
//  edit.c
//

// This is a simple text editor

/* This is the text that appears when a user requests help about this program
<help>

 -- edit --

Simple, interactive text editor.

Usage:
  edit [-T] [file]

(Only available in graphics mode)

Options:
-T              : Force text mode operation

</help>
*/

#include <libintl.h>
#include <locale.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/api.h>
#include <sys/env.h>
#include <sys/font.h>
#include <sys/paths.h>
#include <sys/text.h>
#include <sys/vsh.h>

#define _(string) gettext(string)
#define gettext_noop(string) (string)

#define WINDOW_TITLE		_("Edit")
#define FILE_MENU			_("File")
#define UNTITLED_FILENAME	_("Untitled")
#define DISCARDQUESTION		_("File has been modified.  Discard changes?")
#define FILENAMEQUESTION	_("Please enter the name of the file to edit:")

typedef struct {
	unsigned filePos;
	int length;
	int screenStartRow;
	int screenEndRow;
	int screenLength;
	int screenRows;

} screenLineInfo;

static int processId = 0;
static int screenColumns = 0;
static int screenRows = 0;
static char *tempFileName = NULL;
static char *editFileName = NULL;
static fileStream editFileStream;
static unsigned fileSize = 0;
static char *buffer = NULL;
static unsigned bufferSize = 0;
static screenLineInfo *screenLines = NULL;
static int numScreenLines = 0;
static unsigned firstLineFilePos = 0;
static unsigned lastLineFilePos = 0;
static unsigned cursorLineFilePos = 0;
static int cursorColumn = 0;
static unsigned line = 0;
static unsigned screenLine = 0;
static unsigned numLines = 0;
static int readOnly = 0;
static int modified = 0;
static int stop = 0;

// GUI stuff
static int graphics = 0;
static objectKey window = NULL;
static objectKey fileMenu = NULL;
static objectKey font = NULL;
static objectKey textArea = NULL;
static objectKey statusLabel = NULL;

#define FILEMENU_OPEN 0
#define FILEMENU_SAVE 1
#define FILEMENU_QUIT 2
windowMenuContents fileMenuContents = {
	3,
	{
		{ gettext_noop("Open"), NULL },
		{ gettext_noop("Save"), NULL },
		{ gettext_noop("Quit"), NULL }
	}
};


__attribute__((format(printf, 1, 2)))
static void error(const char *format, ...)
{
	// Generic error message code for either text or graphics modes

	va_list list;
	char output[MAXSTRINGLENGTH];

	va_start(list, format);
	vsnprintf(output, MAXSTRINGLENGTH, format, list);
	va_end(list);

	if (graphics)
	{
		windowNewErrorDialog(window, _("Error"), output);
	}
	else
	{
	}
}


static void updateStatus(void)
{
	// Update the status line

	int column = textGetColumn();
	int row = textGetRow();
	char statusMessage[MAXSTRINGLENGTH];
	textAttrs attrs;

	memset(&attrs, 0, sizeof(textAttrs));
	attrs.flags = TEXT_ATTRS_REVERSE;

	if (!strncmp(editFileName, UNTITLED_FILENAME, MAX_PATH_NAME_LENGTH))
		sprintf(statusMessage, "%s%s  %u/%u", UNTITLED_FILENAME,
			(modified? _(" (modified)") : ""), line, numLines);
	else
		sprintf(statusMessage, "%s%s  %u/%u", editFileStream.f.name,
			(modified? _(" (modified)") : ""), line, numLines);

	if (graphics)
		windowComponentSetData(statusLabel, statusMessage,
			strlen(statusMessage), 1 /* redraw */);

	else
	{
		// Extend to the end of the line
		while (textGetColumn() < (screenColumns - 1))
			strcat(statusMessage, " ");

		// Put the cursor on the last line
		textSetColumn(0);
		textSetRow(screenRows);

		textPrintAttrs(&attrs, statusMessage);

		// Put the cursor back where it belongs
		textSetColumn(column);
		textSetRow(row);
	}
}


static void countLines(void)
{
	// Sets the 'numLines' variable to the number of lines in the file

	unsigned count;

	numLines = 0;
	for (count = 0; count < fileSize; count ++)
	{
		if (buffer[count] == '\n')
			numLines += 1;
	}

	if (!numLines)
		numLines = 1;
}


static void printLine(int lineNum)
{
	// Given a screen line number, print it on the screen at the current cursor
	// position and update its length fields in the array

	char character;
	int maxScreenLength = 0;
	int count1, count2;

	screenLines[lineNum].length = 0;
	screenLines[lineNum].screenLength = 0;

	maxScreenLength =
		((screenRows - screenLines[lineNum].screenStartRow) * screenColumns);

	for (count1 = 0; (screenLines[lineNum].screenLength < maxScreenLength);
		count1 ++)
	{
		character = buffer[screenLines[lineNum].filePos + count1];

		// Look out for tab characters
		if (character == (char) 9)
		{
			textTab();

			screenLines[lineNum].screenLength +=
			(TEXT_DEFAULT_TAB - (screenLines[lineNum].screenLength %
				TEXT_DEFAULT_TAB));
		}
		else
		{
			if (character == (char) 10)
			{
				for (count2 = 0; count2 < (screenColumns -
					(screenLines[lineNum].screenLength %
						screenColumns)); count2 ++)
				{
					textPutc(' ');
				}

				screenLines[lineNum].screenLength += 1;
				break;
			}
			else
			{
				textPutc(character);
				screenLines[lineNum].screenLength += 1;
			}
		}

		screenLines[lineNum].length += 1;
	}

	screenLines[lineNum].screenEndRow = (textGetRow() - 1);

	screenLines[lineNum].screenRows =
		((screenLines[lineNum].screenEndRow -
			screenLines[lineNum].screenStartRow) + 1);
}


static void showScreen(void)
{
	// Show the screen at the current file position

	textScreenClear();
	memset(screenLines, 0, (screenRows * sizeof(screenLineInfo)));

	screenLines[0].filePos = firstLineFilePos;

	for (numScreenLines = 0; numScreenLines < screenRows; )
	{
		lastLineFilePos = screenLines[numScreenLines].filePos;

		screenLines[numScreenLines].screenStartRow = textGetRow();

		printLine(numScreenLines);

		if (screenLines[numScreenLines].screenEndRow >= (screenRows - 1))
			break;

		if (numScreenLines < (screenRows - 1))
		{
			screenLines[numScreenLines + 1].filePos =
				(screenLines[numScreenLines].filePos +
					screenLines[numScreenLines].length + 1);
		}

		if (screenLines[numScreenLines].filePos >= fileSize)
			break;

		numScreenLines += 1;
	}

	updateStatus();
}


static void setCursorColumn(int column)
{
	int screenColumn = 0;
	int count;

	if (column > screenLines[screenLine].length)
		column = screenLines[screenLine].length;

	for (count = 0; count < column; count ++)
	{
		if (buffer[screenLines[screenLine].filePos + count] == '\t')
		{
			screenColumn += (TEXT_DEFAULT_TAB - (screenColumn %
				TEXT_DEFAULT_TAB));
		}
		else
		{
			screenColumn += 1;
		}
	}

	textSetRow(screenLines[screenLine].screenStartRow +
		(screenColumn / screenColumns));
	textSetColumn(screenColumn % screenColumns);

	cursorColumn = column;
}


static int doLoadFile(const char *fileName)
{
	int status = 0;
	disk theDisk;
	file tmpFile;
	int openFlags = OPENMODE_READWRITE;

	// Initialize the file structure
	memset(&tmpFile, 0, sizeof(file));
	memset(&editFileStream, 0, sizeof(fileStream));

	if (buffer)
		free(buffer);

	// Find out whether the file is on a read-only filesystem
	if (!fileGetDisk(fileName, &theDisk))
		readOnly = theDisk.readOnly;

	// Call the "find file" routine to see if we can get the file.
	status = fileFind(fileName, &tmpFile);

	if (status >= 0)
	{
		if (readOnly)
			openFlags = OPENMODE_READ;
	}

	if ((status < 0) || !tmpFile.size)
	{
		// The file either doesn't exist or is zero-length.

		// If the file doesn't exist, and the filesystem is read-only, quit
		// here
		if ((status < 0) && readOnly)
			return (status = ERR_NOWRITE);

		if (status < 0)
			// The file doesn't exist; try to create one
			openFlags |= OPENMODE_CREATE;

		status = fileStreamOpen(fileName, openFlags, &editFileStream);
		if (status < 0)
			return (status);

		// Use a default initial buffer size of one file block
		bufferSize = editFileStream.f.blockSize;
		buffer = malloc(bufferSize);
		if (!buffer)
			return (status = ERR_MEMORY);
	}
	else
	{
		// The file exists and has data in it

		status = fileStreamOpen(fileName, openFlags, &editFileStream);
		if (status < 0)
			return (status);

		// Allocate a buffer to store the file contents in
		bufferSize = (editFileStream.f.blocks * editFileStream.f.blockSize);
		buffer = malloc(bufferSize);
		if (!buffer)
			return (status = ERR_MEMORY);

		status = fileStreamRead(&editFileStream, editFileStream.f.size,
			buffer);
		if (status < 0)
			return (status);
	}

	strncpy(editFileName, fileName, MAX_PATH_NAME_LENGTH);
	return (status = 0);
}


static int askFileName(char *fileName)
{
	int status = 0;
	char pwd[MAX_PATH_NAME_LENGTH];

	multitaskerGetCurrentDirectory(pwd, MAX_PATH_NAME_LENGTH);

	if (graphics)
	{
		// Prompt for a file name
		status = windowNewFileDialog(window, _("Enter filename"),
			FILENAMEQUESTION, pwd, fileName, MAX_PATH_NAME_LENGTH, fileT,
			0 /* no thumbnails */);
		return (status);
	}
	else
		return (status = 0);
}


static int loadFile(const char *fileName)
{
	int status = 0;
	disk rootDisk;

	// Did the user specify a file name?
	if (fileName)
	{
		// Yes.  Do the load.
		status = doLoadFile(fileName);
		if (status < 0)
			return (status);
	}
	else
	{
		// No.  Try to open a new temporary file to use as an 'untitled' file
		// that we will prompt for a file name later when it gets saved.

		if (!fileGetDisk("/", &rootDisk) && !rootDisk.readOnly &&
			(fileStreamGetTemp(&editFileStream) >= 0))
		{
			// Use a default initial buffer size of one file block.
			bufferSize = editFileStream.f.blockSize;
			buffer = malloc(bufferSize);
			if (!buffer)
				return (status = ERR_MEMORY);

			strncpy(editFileName, UNTITLED_FILENAME, MAX_PATH_NAME_LENGTH);

			// Try to remember the name of the temp file, so we can delete it
			// later if it doesn't get saved.

			tempFileName = malloc(MAX_PATH_NAME_LENGTH);
			if (!tempFileName)
				return (status = ERR_MEMORY);

			if (fileGetFullPath(&editFileStream.f, tempFileName,
				MAX_PATH_NAME_LENGTH) < 0)
			{
				free(tempFileName);
				tempFileName = NULL;
			}
		}
		else
		{
			// Couldn't open a temporary file.  We might be running from a
			// read-only filesystem, for example.  Prompt for some file to
			// open, otherwise there's no point really.
			status = askFileName(editFileName);
			if (status != 1)
			{
				if (status < 0)
					return (status);
				else
					return (status = ERR_CANCELLED);
			}

			fileName = editFileName;

			// Do the load.
			status = doLoadFile(fileName);
			if (status < 0)
				return (status);
		}
	}

	fileSize = editFileStream.f.size;
	firstLineFilePos = 0;
	lastLineFilePos = 0;
	cursorLineFilePos = 0;
	cursorColumn = 0;
	line = 0;
	screenLine = 0;
	numLines = 0;
	modified = 0;

	countLines();
	showScreen();
	setCursorColumn(0);

	if (graphics)
	{
		if (readOnly)
			windowComponentSetEnabled(
				fileMenuContents.items[FILEMENU_SAVE].key, 0);
		windowComponentFocus(textArea);
	}

	return (status = 0);
}


static int saveFile(void)
{
	int status = 0;
	fileStream tmpFileStream;

	if (!strncmp(editFileName, UNTITLED_FILENAME, MAX_PATH_NAME_LENGTH))
	{
		if (graphics)
		{
			// Prompt for a file name
			status = askFileName(editFileName);
			if (status != 1)
			{
				if (status < 0)
					return (status);
				else
					return (status = ERR_CANCELLED);
			}
		}

		// Open the file (truncate if necessary)
		status = fileStreamOpen(editFileName,
			(OPENMODE_CREATE | OPENMODE_TRUNCATE | OPENMODE_READWRITE),
			&tmpFileStream);
		if (status < 0)
			return (status);

		// Close the temp file and swap the info.
		fileStreamClose(&editFileStream);
		if (tempFileName)
		{
			fileDelete(tempFileName);
			free(tempFileName);
			tempFileName = NULL;
		}

		memcpy(&editFileStream, &tmpFileStream, sizeof(fileStream));
	}

	status = fileStreamSeek(&editFileStream, 0);
	if (status < 0)
		return (status);

	status = fileStreamWrite(&editFileStream, fileSize, buffer);
	if (status < 0)
		return (status);

	status = fileStreamFlush(&editFileStream);
	if (status < 0)
		return (status);

	modified = 0;
	updateStatus();

	if (graphics)
		windowComponentFocus(textArea);

	return (status = 0);
}


static unsigned previousLineStart(unsigned filePos)
{
	unsigned lineStart = 0;

	if (!filePos)
		return (lineStart = 0);

	lineStart = (filePos - 1);

	// Watch for the start of the buffer
	if (!lineStart)
		return (lineStart);

	// Lines that end with a newline (most)
	if (buffer[lineStart] == '\n')
		lineStart -= 1;

	// Watch for the start of the buffer
	if (!lineStart)
		return (lineStart);

	for ( ; buffer[lineStart] != '\n'; lineStart --)
	{
		// Watch for the start of the buffer
		if (!lineStart)
			return (lineStart);
	}

	lineStart += 1;

	return (lineStart);
}


static unsigned nextLineStart(unsigned filePos)
{
	unsigned lineStart = 0;

	if (filePos >= fileSize)
		return (lineStart = (fileSize - 1));

	lineStart = filePos;

	// Determine where the current line ends
	while (lineStart < (fileSize - 1))
	{
		if (buffer[lineStart] == '\n')
		{
			lineStart += 1;
			break;
		}
		else
			lineStart += 1;
	}

	return (lineStart);
}


static void cursorUp(void)
{
	if (!line)
		return;

	cursorLineFilePos = previousLineStart(cursorLineFilePos);

	// Do we need to scroll the screen up?
	if (cursorLineFilePos < firstLineFilePos)
	{
		firstLineFilePos = cursorLineFilePos;
		showScreen();
	}
	else
		textSetRow(screenLines[--screenLine].screenStartRow);

	setCursorColumn(cursorColumn);

	line -= 1;
	return;
}


static void cursorDown(void)
{
	if (line >= numLines)
		return;

	cursorLineFilePos = nextLineStart(cursorLineFilePos);

	if (cursorLineFilePos > lastLineFilePos)
	{
		// Do we need to scroll the screen down?
		firstLineFilePos = nextLineStart(firstLineFilePos);
		showScreen();
	}
	else
		textSetRow(screenLines[++screenLine].screenStartRow);

	setCursorColumn(cursorColumn);

	line += 1;
	return;
}


static void cursorLeft(void)
{
	if (cursorColumn)
		setCursorColumn(cursorColumn - 1);
	else
	{
		cursorUp();
		setCursorColumn(screenLines[screenLine].length);
	}

	return;
}


static void cursorRight(void)
{
	if (cursorColumn < screenLines[screenLine].length)
		setCursorColumn(cursorColumn + 1);
	else
	{
		cursorDown();
		setCursorColumn(0);
	}

	return;
}


static int expandBuffer(unsigned length)
{
	// Expand the buffer by at least 'length' characters

	int status = 0;
	unsigned tmpBufferSize = 0;
	char *tmpBuffer = NULL;

	// Allocate more buffer, rounded up to the nearest block size of the file
	tmpBufferSize = (bufferSize + (((length / editFileStream.f.blockSize) +
		((length % editFileStream.f.blockSize)? 1 : 0)) *
			editFileStream.f.blockSize));

	tmpBuffer = realloc(buffer, tmpBufferSize);
	if (!tmpBuffer)
		return (status = ERR_MEMORY);

	buffer = tmpBuffer;
	bufferSize = tmpBufferSize;

	return (status = 0);
}


static void shiftBuffer(unsigned filePos, int shiftBy)
{
	// Shift the contents of the buffer at 'filePos' by 'shiftBy' characters
	// (positive or negative)

	unsigned shiftChars = 0;
	unsigned count;

	shiftChars = (fileSize - filePos);

	if (shiftChars)
	{
		if (shiftBy > 0)
		{
			for (count = 1; count <= shiftChars; count ++)
				buffer[fileSize + (shiftBy - count)] =
					buffer[filePos + (shiftChars - count)];
		}
		else if (shiftBy < 0)
		{
			filePos -= 1;
			shiftBy *= -1;
			for (count = 0; count < shiftChars; count ++)
				buffer[filePos + count] = buffer[filePos + shiftBy + count];
		}
	}
}


static int insertChars(char *string, unsigned length)
{
	// Insert characters at the current position.

	int status = 0;
	int oldRows = 0;
	int count;

	// Do we need a bigger buffer?
	if ((fileSize + length) >= bufferSize)
	{
		status = expandBuffer(length);
		if (status < 0)
			return (status);
	}

	if ((screenLines[screenLine].filePos + cursorColumn) < (fileSize - 1))
		// Shift data that occurs later in the buffer.
		shiftBuffer((screenLines[screenLine].filePos + cursorColumn), length);

	// Copy the data
	strncpy((buffer + screenLines[screenLine].filePos + cursorColumn), string,
		length);

	// We need to adjust the recorded file positions of all lines that follow
	// on the screen
	for (count = (screenLine + 1); count < numScreenLines; count ++)
		screenLines[count].filePos += length;

	fileSize += length;
	modified = 1;

	textSetRow(screenLines[screenLine].screenStartRow);
	textSetColumn(0);
	oldRows = screenLines[screenLine].screenRows;
	printLine(screenLine);

	// If the line now occupies more screen lines, redraw the lines below it.
	if (screenLines[screenLine].screenRows != oldRows)
	{
		for (count = (screenLine + 1); count < numScreenLines; count ++)
		{
			screenLines[count].screenStartRow = textGetRow();
			printLine(count);
		}
	}

	return (status = 0);
}


static void deleteChars(unsigned length)
{
	// Delete characters at the current position.

	int oldRows = 0;
	int count;

	if ((screenLines[screenLine].filePos + cursorColumn) < (fileSize - 1))
	{
		// Shift data that occurs later in the buffer.
		shiftBuffer((screenLines[screenLine].filePos + cursorColumn + 1),
			-length);
	}

	// Clear data
	memset((buffer + (fileSize - length)), 0, length);

	// We need to adjust the recorded file positions of all lines that follow
	// on the screen
	for (count = (screenLine + 1); count < numScreenLines; count ++)
		screenLines[count].filePos -= length;

	fileSize -= length;
	modified = 1;

	textSetRow(screenLines[screenLine].screenStartRow);
	textSetColumn(0);
	oldRows = screenLines[screenLine].screenRows;
	printLine(screenLine);

	// If the line now occupies fewer screen lines, redraw the lines below it.
	if (screenLines[screenLine].screenRows != oldRows)
	{
		for (count = (screenLine + 1); count < numScreenLines; count ++)
		{
			screenLines[count].screenStartRow = textGetRow();
			printLine(count);
		}
	}

	return;
}


static int edit(void)
{
	// This routine is the base from which we do all the editing.

	int status = 0;
	char character = '\0';
	int oldRow = 0;
	int endLine = 0;

	while (!stop)
	{
		if (!textInputCount())
		{
			multitaskerYield();
			continue;
		}
		textInputGetc(&character);

		switch (character)
		{
			case (char) ASCII_CRSRUP:
				// UP cursor key
				cursorUp();
				break;

			case (char) ASCII_CRSRDOWN:
				// DOWN cursor key
				cursorDown();
				break;

			case (char) ASCII_CRSRLEFT:
				// LEFT cursor key
				cursorLeft();
				break;

			case (char) ASCII_CRSRRIGHT:
				// RIGHT cursor key
				cursorRight();
				break;

			case (char) ASCII_BACKSPACE:
				// BACKSPACE key
				oldRow = screenLines[screenLine].screenStartRow;
				if (oldRow || cursorColumn)
				{
					cursorLeft();
					deleteChars(1);
					// If we were at the beginning of a line...
					if (screenLines[screenLine].screenStartRow != oldRow)
					{
						numLines -= 1;
						showScreen();
					}
					setCursorColumn(cursorColumn);
				}
				break;

			case (char) ASCII_DEL:
				// DEL key
				endLine = (cursorColumn >= screenLines[screenLine].length);
				deleteChars(1);
				// If we were at the end of a line...
				if (endLine)
				{
				numLines -= 1;
				showScreen();
				}
				setCursorColumn(cursorColumn);
				break;

			case (char) ASCII_ENTER:
				// ENTER key
				status = insertChars(&character, 1);
				if (status < 0)
				break;
				numLines += 1;
				showScreen();
				setCursorColumn(0);
				cursorDown();
				break;

			default:
				// Typing anything else.  Is it printable?
				status = insertChars(&character, 1);
				if (status < 0)
					break;
				setCursorColumn(cursorColumn + 1);
				break;
		}

		updateStatus();
	}

	status = fileStreamClose(&editFileStream);

	if (tempFileName)
	{
		fileDelete(tempFileName);
		free(tempFileName);
		tempFileName = NULL;
	}

	return (status);
}


static int askDiscardChanges(void)
{
	int response = 0;

	if (graphics)
	{
		response = windowNewChoiceDialog(window, _("Discard changes?"),
			DISCARDQUESTION, (char *[]){ _("Discard"), _("Cancel") }, 2, 1);

		if (!response)
			return (1);
		else
			return (0);
	}
	else
	{
		return (0);
	}
}


static void openFileThread(void)
{
	int status = 0;
	char *fileName = NULL;

	fileName = malloc(MAX_PATH_NAME_LENGTH);
	if (!fileName)
		goto out;

	status = askFileName(fileName);
	if (status != 1)
	{
		if (status >= 0)
			status = ERR_CANCELLED;
		goto out;
	}

	status = loadFile(fileName);
	if (status < 0)
	{
		if (status == ERR_NOWRITE)
			error("%s", _("Couldn't create file in a read-only filesystem"));
		else if (status != ERR_CANCELLED)
			error(_("Error %d loading file"), status);
	}

	 out:
	if (fileName)
		free(fileName);

	multitaskerTerminate(status);
}


static int calcCursorColumn(int screenColumn)
{
	// Given an on-screen column, calculate the real character column i.e. as
	// we would pass to setCursorColumn(), above.

	int column = 0;
	int count;

	for (count = 0; column < screenColumn; count ++)
	{
		if (buffer[screenLines[screenLine].filePos + count] == '\t')
			column += (TEXT_DEFAULT_TAB - (column % TEXT_DEFAULT_TAB));
		else
			column += 1;
	}

	return (count);
}


static void quit(void)
{
	if (!modified || askDiscardChanges())
		stop = 1;
}


static void initMenuContents(windowMenuContents *contents)
{
	int count;

	for (count = 0; count < contents->numItems; count ++)
	{
		strncpy(contents->items[count].text, _(contents->items[count].text),
			WINDOW_MAX_LABEL_LENGTH);
		contents->items[count].text[WINDOW_MAX_LABEL_LENGTH - 1] = '\0';
	}
}


static void refreshMenuContents(void)
{
	int count;

	initMenuContents(&fileMenuContents);

	for (count = 0; count < fileMenuContents.numItems; count ++)
		windowComponentSetData(fileMenuContents.items[count].key,
			fileMenuContents.items[count].text,
			strlen(fileMenuContents.items[count].text),
			(count == (fileMenuContents.numItems - 1)));
}


static void refreshWindow(void)
{
	// We got a 'window refresh' event (probably because of a language switch),
	// so we need to update things

	// Re-get the language setting
	setlocale(LC_ALL, getenv(ENV_LANG));
	textdomain("edit");

	// Re-get the character set
	if (getenv(ENV_CHARSET))
		windowSetCharSet(window, getenv(ENV_CHARSET));

	// Refresh all the menu contents
	refreshMenuContents();

	// Refresh the 'file' menu
	windowSetTitle(fileMenu, FILE_MENU);

	// Refresh the window title
	windowSetTitle(window, WINDOW_TITLE);
}


static void eventHandler(objectKey key, windowEvent *event)
{
	int status = 0;
	int oldScreenLine = 0;
	int newColumn = 0;
	int newRow = 0;
	int count;

	// Check for window events.
	if (key == window)
	{
		// Check for window refresh
		if (event->type == EVENT_WINDOW_REFRESH)
			refreshWindow();

		// Check for window resize
		else if (event->type == EVENT_WINDOW_RESIZE)
		{
			screenColumns = textGetNumColumns();
			screenRows = textGetNumRows();
			showScreen();
		}

		// Check for the window being closed
		else if (event->type == EVENT_WINDOW_CLOSE)
			quit();
	}

	// Look for file menu events

	else if (key == fileMenuContents.items[FILEMENU_OPEN].key)
	{
		if (event->type & EVENT_SELECTION)
		{
			if (!modified || askDiscardChanges())
			{
				if (multitaskerSpawn(&openFileThread, "open file", 0,
					NULL) < 0)
				{
					error("%s", _("Unable to launch file dialog"));
				}
			}
		}
	}

	else if (key == fileMenuContents.items[FILEMENU_SAVE].key)
	{
		if (event->type & EVENT_SELECTION)
		{
			status = saveFile();
			if (status < 0)
				error(_("Error %d saving file"), status);
		}
	}

	else if (key == fileMenuContents.items[FILEMENU_QUIT].key)
	{
		if (event->type & EVENT_SELECTION)
			quit();
	}

	// Look for cursor movements caused by clicking in the text area

	else if (key == textArea)
	{
	 	if (event->type & EVENT_CURSOR_MOVE)
		{
			// The user clicked to move the cursor, which is a pain.  We need
			// to try to figure out the new screen line.

			oldScreenLine = screenLine;
			newRow = textGetRow();

			for (count = 0; count < numScreenLines; count ++)
			{
				if ((newRow >= screenLines[count].screenStartRow) &&
					(newRow <= screenLines[count].screenEndRow))
				{
					screenLine = count;
					cursorLineFilePos = screenLines[count].filePos;
					line += (screenLine - oldScreenLine);
					newColumn = (((newRow - screenLines[count].screenStartRow) *
						screenColumns) + textGetColumn());
					setCursorColumn(calcCursorColumn(newColumn));
					updateStatus();
					break;
				}
			}
		}
	}
}


static void handleMenuEvents(windowMenuContents *contents)
{
	int count;

	for (count = 0; count < contents->numItems; count ++)
		windowRegisterEventHandler(contents->items[count].key, &eventHandler);
}


static void constructWindow(void)
{
	int rows = 25;
	componentParameters params;

	// Create a new window
	window = windowNew(processId, WINDOW_TITLE);

	memset(&params, 0, sizeof(componentParameters));

	// Create the top menu bar
	objectKey menuBar = windowNewMenuBar(window, &params);

	// Create the top 'file' menu
	initMenuContents(&fileMenuContents);
	fileMenu = windowNewMenu(window, menuBar, FILE_MENU, &fileMenuContents,
		&params);
	handleMenuEvents(&fileMenuContents);

	params.gridWidth = 1;
	params.gridHeight = 1;
	params.padLeft = 1;
	params.padRight = 1;
	params.padTop = 1;
	params.orientationX = orient_center;
	params.orientationY = orient_middle;

	// Set up the font for our main text area
	font = fontGet(FONT_FAMILY_LIBMONO, FONT_STYLEFLAG_FIXED, 10, NULL);
	if (!font)
		// We'll be using the system font we guess.  The system font can
		// comfortably show more rows
		rows = 40;

	// Put a text area in the window
	params.flags |=
		(WINDOW_COMPFLAG_STICKYFOCUS | WINDOW_COMPFLAG_CLICKABLECURSOR);
	params.font = font;
	textArea = windowNewTextArea(window, 80, rows, 0, &params);
	windowRegisterEventHandler(textArea, &eventHandler);
	windowComponentFocus(textArea);

	// Use the text area for all our input and output
	windowSetTextOutput(textArea);

	// Put a status label below the text area
	params.flags &= ~WINDOW_COMPFLAG_STICKYFOCUS;
	params.gridY += 1;
	params.padBottom = 1;
	params.font = fontGet(FONT_FAMILY_ARIAL, FONT_STYLEFLAG_BOLD, 10, NULL);
	statusLabel = windowNewTextLabel(window, "", &params);
	windowComponentSetWidth(statusLabel, windowComponentGetWidth(textArea));

	// Go live.
	windowSetVisible(window, 1);

	// Register an event handler to catch window close events
	windowRegisterEventHandler(window, &eventHandler);

	// Run the GUI as a thread
	windowGuiThread();
}


int main(int argc, char *argv[])
{
	int status = 0;
	char opt;
	char *fileName = NULL;
	textScreen screen;

	setlocale(LC_ALL, getenv(ENV_LANG));
	textdomain("edit");

	processId = multitaskerGetCurrentProcessId();

	// Are graphics enabled?
	graphics = graphicsAreEnabled();

	// For the moment, only operate in graphics mode
	if (!graphics)
	{
		printf(_("\nThe \"%s\" command only works in graphics mode\n"),
			(argc? argv[0] : ""));
		return (status = ERR_NOTINITIALIZED);
	}

	// Check options
	while (strchr("T?", (opt = getopt(argc, argv, "T"))))
	{
		switch (opt)
		{
			case 'T':
				// Force text mode
				graphics = 0;
				break;

			default:
				error(_("Unknown option '%c'"), optopt);
				return (status = ERR_INVALID);
		}
	}

	if (optind < argc)
		fileName = argv[optind];

	if (graphics)
		constructWindow();
	else
		// Save the current screen
		textScreenSave(&screen);

	// Get screen parameters
	screenColumns = textGetNumColumns();
	screenRows = textGetNumRows();
	if (!graphics)
		// Save one for the status line
		screenRows -= 1;

	// Clear it
	textScreenClear();
	textEnableScroll(0);

	screenLines = malloc(screenRows * sizeof(screenLineInfo));
	editFileName = malloc(MAX_PATH_NAME_LENGTH);
	if (!screenLines || !editFileName)
	{
		status = ERR_MEMORY;
		goto out;
	}

	status = loadFile(fileName);
	if (status < 0)
	{
		if (status == ERR_NOWRITE)
			error("%s", _("Couldn't create file in a read-only filesystem"));
		else if (status != ERR_CANCELLED)
			error(_("Error %d loading file"), status);
		goto out;
	}

	// Go
	status = edit();

out:
	textEnableScroll(1);

	if (graphics)
	{
		// Stop our GUI thread
		windowGuiStop();

		// Destroy the window
		windowDestroy(window);
	}
	else
	{
		textScreenRestore(&screen);

		if (screen.data)
			memoryRelease(screen.data);
	}

	if (screenLines)
		free(screenLines);
	if (editFileName)
		free (editFileName);
	if (buffer)
		free(buffer);

	// Return success
	return (status);
}

