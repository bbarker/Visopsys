//
//  Visopsys
//  Copyright (C) 1998-2018 J. Andrew McLaughlin
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
//  unistd.h
//

// This is the Visopsys version of the standard header file unistd.h

#if !defined(_UNISTD_H)

extern char *optarg;
extern int optind, opterr, optopt;

// For seeking using lseek()
#ifndef SEEK_SET
	#define SEEK_SET	1
	#define SEEK_CUR	2
	#define SEEK_END	3
#endif

// Contains the size_t and ssize_t definitions
#include <stddef.h>

// Contains the off_t definition
#include <sys/types.h>

int chdir(const char *);
int close(int);
int ftruncate(int, off_t);
char *getcwd(char *, size_t);
int getopt(int, char *const[], const char *);
off_t lseek(int, off_t, int);
ssize_t read(int, void *, size_t);
int rmdir(const char *);
unsigned sleep(unsigned);
void swab(const void *, void *, ssize_t);
int truncate(const char *, off_t);
int unlink(const char *);
ssize_t write(int, const void *, size_t);

#define _UNISTD_H
#endif

