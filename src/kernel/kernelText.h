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
//  kernelText.h
//

#if !defined(_KERNELTEXT_H)

#include "kernelFont.h"
#include "kernelGraphic.h"
#include "kernelStream.h"
#include <sys/text.h>

// Forward declarations, where necessary
struct _kernelTextOutputStream;
// Can't include kernelWindow.h, it's circular.
struct _kernelWindowComponent;

// Per-process attributes for a text input stream
typedef struct {
	int echo;

} kernelTextInputStreamAttrs;

// A text input stream.  In single user operation there is only one, and it's
// where all keyboard input goes.
typedef volatile struct {
	stream s;
	int ownerPid;
	kernelTextInputStreamAttrs attrs;

} kernelTextInputStream;

// A data structure to represent a text area on the screen which gets drawn
// by the appropriate driver functions
typedef volatile struct {
	int xCoord;
	int yCoord;
	int columns;
	int rows;
	int bytesPerChar;
	int cursorColumn;
	int cursorRow;
	int cursorState;
	int maxBufferLines;
	int scrollBackLines;
	int scrolledBackLines;
	int hidden;
	color foreground;
	color background;
	unsigned char pcColor;
	kernelTextInputStream *inputStream;
	volatile struct _kernelTextOutputStream *outputStream;
	unsigned char *bufferData;
	unsigned char *visibleData;
	kernelFont *font;
	char *charSet;
	volatile struct _kernelWindowComponent *windowComponent;
	int noScroll;

} kernelTextArea;

// This structure contains pointers to all the appropriate functions
// to output text from a given text stream
typedef struct {
	void (*setCursor)(kernelTextArea *, int);
	int (*getCursorAddress)(kernelTextArea *);
	int (*setCursorAddress)(kernelTextArea *, int, int);
	int (*setForeground)(kernelTextArea *, color *);
	int (*setBackground)(kernelTextArea *, color *);
	int (*print)(kernelTextArea *, const char *, textAttrs *);
	int (*delete)(kernelTextArea *);
	int (*screenDraw)(kernelTextArea *);
	int (*screenClear)(kernelTextArea *);
	int (*screenSave)(kernelTextArea *, textScreen *);
	int (*screenRestore)(kernelTextArea *, textScreen *);

} kernelTextOutputDriver;

// This structure is used to refer to a stream made up of text.
typedef volatile struct _kernelTextOutputStream {
	kernelTextOutputDriver *outputDriver;
	kernelTextArea *textArea;

} kernelTextOutputStream;

// The default driver initializations
int kernelTextConsoleInitialize(void);
int kernelGraphicConsoleInitialize(void);

// Functions from kernelText.c

int kernelTextInitialize(int, int);
kernelTextArea *kernelTextAreaNew(int, int, int, int);
void kernelTextAreaDestroy(kernelTextArea *);
int kernelTextAreaResize(kernelTextArea *, int, int);
int kernelTextSwitchToGraphics(kernelTextArea *);
kernelTextInputStream *kernelTextGetConsoleInput(void);
kernelTextOutputStream *kernelTextGetConsoleOutput(void);
int kernelTextSetConsoleInput(kernelTextInputStream *);
int kernelTextSetConsoleOutput(kernelTextOutputStream *);
kernelTextInputStream *kernelTextGetCurrentInput(void);
int kernelTextSetCurrentInput(kernelTextInputStream *);
kernelTextOutputStream *kernelTextGetCurrentOutput(void);
int kernelTextSetCurrentOutput(kernelTextOutputStream *);
int kernelTextNewInputStream(kernelTextInputStream *);
int kernelTextNewOutputStream(kernelTextOutputStream *);
int kernelTextGetForeground(color *);
int kernelTextSetForeground(color *);
int kernelTextGetBackground(color *);
int kernelTextSetBackground(color *);
int kernelTextStreamPutc(kernelTextOutputStream *, int);
int kernelTextPutc(int);
int kernelTextStreamPrint(kernelTextOutputStream *, const char *);
int kernelTextPrint(const char *, ...) __attribute__((format(printf, 1, 2)));
int kernelTextStreamPrintAttrs(kernelTextOutputStream *, textAttrs *,
			 const char *);
int kernelTextPrintAttrs(textAttrs *, const char *, ...)
	__attribute__((format(printf, 2, 3)));
int kernelTextStreamPrintLine(kernelTextOutputStream *, const char *);
int kernelTextPrintLine(const char *, ...)
	__attribute__((format(printf, 1, 2)));
void kernelTextStreamNewline(kernelTextOutputStream *);
void kernelTextNewline(void);
void kernelTextStreamBackSpace(kernelTextOutputStream *);
void kernelTextBackSpace(void);
void kernelTextStreamTab(kernelTextOutputStream *);
void kernelTextTab(void);
void kernelTextStreamCursorUp(kernelTextOutputStream *);
void kernelTextCursorUp(void);
void kernelTextStreamCursorDown(kernelTextOutputStream *);
void kernelTextCursorDown(void);
void kernelTextStreamCursorLeft(kernelTextOutputStream *);
void kernelTextCursorLeft(void);
void kernelTextStreamCursorRight(kernelTextOutputStream *);
void kernelTextCursorRight(void);
int kernelTextStreamEnableScroll(kernelTextOutputStream *, int);
int kernelTextEnableScroll(int);
void kernelTextStreamScroll(kernelTextOutputStream *, int);
void kernelTextScroll(int);
int kernelTextStreamGetNumColumns(kernelTextOutputStream *);
int kernelTextGetNumColumns(void);
int kernelTextStreamGetNumRows(kernelTextOutputStream *);
int kernelTextGetNumRows(void);
int kernelTextStreamGetColumn(kernelTextOutputStream *);
int kernelTextGetColumn(void);
void kernelTextStreamSetColumn(kernelTextOutputStream *, int);
void kernelTextSetColumn(int);
int kernelTextStreamGetRow(kernelTextOutputStream *);
int kernelTextGetRow(void);
void kernelTextStreamSetRow(kernelTextOutputStream *,int);
void kernelTextSetRow(int);
void kernelTextStreamSetCursor(kernelTextOutputStream *, int);
void kernelTextSetCursor(int);
void kernelTextStreamScreenClear(kernelTextOutputStream *);
void kernelTextScreenClear(void);
int kernelTextScreenSave(textScreen *);
int kernelTextScreenRestore(textScreen *);

int kernelTextInputStreamCount(kernelTextInputStream *);
int kernelTextInputCount(void);
int kernelTextInputStreamGetc(kernelTextInputStream *, char *);
int kernelTextInputGetc(char *);
int kernelTextInputStreamPeek(kernelTextInputStream *, char *);
int kernelTextInputPeek(char *);
int kernelTextInputStreamReadN(kernelTextInputStream *, int, char *);
int kernelTextInputReadN(int, char *);
int kernelTextInputStreamReadAll(kernelTextInputStream *, char *);
int kernelTextInputReadAll(char *);
int kernelTextInputStreamAppend(kernelTextInputStream *, int);
int kernelTextInputAppend(int);
int kernelTextInputStreamAppendN(kernelTextInputStream *, int, char *);
int kernelTextInputAppendN(int, char *);
int kernelTextInputStreamRemove(kernelTextInputStream *);
int kernelTextInputRemove(void);
int kernelTextInputStreamRemoveN(kernelTextInputStream *, int);
int kernelTextInputRemoveN(int);
int kernelTextInputStreamRemoveAll(kernelTextInputStream *);
int kernelTextInputRemoveAll(void);
void kernelTextInputStreamSetEcho(kernelTextInputStream *, int);
void kernelTextInputSetEcho(int);

// Some useful macros for working with text areas
#define TEXTAREA_CURSORPOS(area) \
	((area->cursorRow * area->columns) + area->cursorColumn)
#define TEXTAREA_FIRSTSCROLLBACK(area) \
	(area->bufferData + ((area->maxBufferLines - \
		(area->rows + area->scrollBackLines)) * \
			(area->columns * area->bytesPerChar)))
#define TEXTAREA_LASTSCROLLBACK(area) \
	(area->bufferData + ((area->maxBufferLines - (area->rows + 1)) * \
		(area->columns * area->bytesPerChar)))
#define TEXTAREA_FIRSTVISIBLE(area) \
	(area->bufferData + ((area->maxBufferLines - area->rows) * \
		(area->columns * area->bytesPerChar)))
#define TEXTAREA_LASTVISIBLE(area) \
	(area->bufferData + ((area->maxBufferLines - 1) * \
		(area->columns * area->bytesPerChar)))

#define _KERNELTEXT_H
#endif

