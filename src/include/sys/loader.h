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
//  loader.h
//

// This file contains definitions and structures for using the Visopsys
// kernel loader

#if !defined(_LOADER_H)

// Loader File class types
#define LOADERFILECLASS_NONE		0x00000000
#define LOADERFILECLASS_BIN			0x00000001
#define LOADERFILECLASS_TEXT		0x00000002
#define LOADERFILECLASS_EXEC		0x00000004
#define LOADERFILECLASS_OBJ			0x00000008
#define LOADERFILECLASS_LIB			0x00000010
#define LOADERFILECLASS_CORE		0x00000020
#define LOADERFILECLASS_IMAGE		0x00000040
#define LOADERFILECLASS_DATA		0x00000080
#define LOADERFILECLASS_DOC			0x00000100
#define LOADERFILECLASS_ARCHIVE		0x00000200
#define LOADERFILECLASS_FONT		0x00000400
#define LOADERFILECLASS_BOOT		0x00000800
#define LOADERFILECLASS_KEYMAP		0x00001000

// Loader File subclass types
#define LOADERFILESUBCLASS_NONE		0x00000000
#define LOADERFILESUBCLASS_STATIC	0x00000001
#define LOADERFILESUBCLASS_DYNAMIC	0x00000002
#define LOADERFILESUBCLASS_ICO		0x00000004
#define LOADERFILESUBCLASS_PDF		0x00000008
#define LOADERFILESUBCLASS_ZIP		0x00000010
#define LOADERFILESUBCLASS_GZIP		0x00000020
#define LOADERFILESUBCLASS_TAR		0x00000040
#define LOADERFILESUBCLASS_PCF		0x00000080
#define LOADERFILESUBCLASS_TTF		0x00000100
#define LOADERFILESUBCLASS_VBF		0x00000200
#define LOADERFILESUBCLASS_CONFIG	0x00000400
#define LOADERFILESUBCLASS_HTML		0x00000800
#define LOADERFILESUBCLASS_MESSAGE	0x00001000

// Symbol bindings for loader symbol structure
#define LOADERSYMBOLBIND_LOCAL		0
#define LOADERSYMBOLBIND_GLOBAL		1
#define LOADERSYMBOLBIND_WEAK		2

// Symbol types for the loader symbol structure
#define LOADERSYMBOLTYPE_NONE		0
#define LOADERSYMBOLTYPE_OBJECT		1
#define LOADERSYMBOLTYPE_FUNC		2
#define LOADERSYMBOLTYPE_SECTION	3
#define LOADERSYMBOLTYPE_FILE		4

// This structure describes the classification of the file.
typedef struct {
	char className[64];
	int class;
	int subClass;

} loaderFileClass;

typedef struct {
	char *name;
	int defined;
	void *value;
	unsigned size;
	int binding;
	int type;

} loaderSymbol;

typedef struct {
	int numSymbols;
	int tableSize;
	loaderSymbol symbols[];

} loaderSymbolTable;

#define _LOADER_H
#endif

