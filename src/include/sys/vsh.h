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
//  vsh.h
//

// These are functions written for vsh which can be used by other programs

#if !defined(_VSH_H)

#include <time.h>
#include <sys/progress.h>

#ifndef _X_
#define _X_
#endif

// Functions
void vshCompleteFilename(char *);
int vshCursorMenu(const char *, char *[], int, int, int);
int vshDeleteFile(const char *);
int vshDumpFile(const char *);
int vshFileList(const char *);
void vshMakeAbsolutePath(const char *, char *);
int vshMoveFile(const char *, const char *);
int vshParseCommand(char *, char *, int *, char *[]);
void vshPasswordPrompt(const char *, char *);
void vshPrintDate(char *, struct tm *);
void vshPrintTime(char *, struct tm *);
int vshProgressBar(progress *);
int vshProgressBarDestroy(progress *);
int vshSearchPath(const char *, char *);

#define _VSH_H
#endif

