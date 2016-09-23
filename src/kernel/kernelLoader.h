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
//  kernelLoader.h
//

// This is the header file to go with the kernel's loader

#if !defined(_KERNELLOADER_H)

#include "kernelFont.h"
#include <sys/loader.h>
#include <sys/file.h>
#include <sys/image.h>
#include <sys/process.h>

#define FILECLASS_NAME_EMPTY	"empty"
#define FILECLASS_NAME_TEXT		"text"
#define FILECLASS_NAME_BIN		"binary"
#define FILECLASS_NAME_STATIC	"static"
#define FILECLASS_NAME_DYNAMIC	"dynamic"
#define FILECLASS_NAME_EXEC		"executable"
#define FILECLASS_NAME_OBJ		"object"
#define FILECLASS_NAME_LIB		"library"
#define FILECLASS_NAME_CORE		"core"
#define FILECLASS_NAME_IMAGE	"image"
#define FILECLASS_NAME_DATA		"data"
#define FILECLASS_NAME_DOC		"document"
#define FILECLASS_NAME_ARCHIVE	"archive"
#define FILECLASS_NAME_FONT		"font"

#define FILECLASS_NAME_BMP		"bitmap"
#define FILECLASS_NAME_ICO		"icon"
#define FILECLASS_NAME_JPG		"JPEG"
#define FILECLASS_NAME_GIF		"GIF"
#define FILECLASS_NAME_PNG		"PNG"
#define FILECLASS_NAME_PPM		"PPM"
#define FILECLASS_NAME_BOOT		"boot"
#define FILECLASS_NAME_KEYMAP	"keymap"
#define FILECLASS_NAME_PDF		"PDF"
#define FILECLASS_NAME_ELF		"ELF"
#define FILECLASS_NAME_ZIP		"zip"
#define FILECLASS_NAME_GZIP		"gzip"
#define FILECLASS_NAME_AR		"ar"
#define FILECLASS_NAME_TAR		"tar"
#define FILECLASS_NAME_PCF		"PCF"
#define FILECLASS_NAME_TTF		"TTF"
#define FILECLASS_NAME_VBF		"VBF"
#define FILECLASS_NAME_MESSAGE	"message"
#define FILECLASS_NAME_CONFIG	"configuration"
#define FILECLASS_NAME_HTML		"HTML"
#define LOADER_NUM_FILECLASSES	22

// A generic structure to represent a relocation entry
typedef struct {
	void *offset;		// Virtual offset in image
	char *symbolName;	// Index into symbol table
	int info;			// Driver-specific
	unsigned addend;	// Not used (yet)

} kernelRelocation;

// A collection of kernelRelocation entries
typedef struct {
	int numRelocs;
	int tableSize;
	kernelRelocation relocations[];

} kernelRelocationTable;

// Forward declarations, where necessary
struct _kernelDynamicLibrary;

// This is a structure for a file class.  It contains a standard name for
// the file class and function pointers for managing that class of file.
typedef struct {
	char *className;
	int (*detect)(const char *, void *, unsigned, loaderFileClass *);
	union {
		struct {
			loaderSymbolTable * (*getSymbols)(void *, int);
			int (*layoutLibrary)(void *, struct _kernelDynamicLibrary *);
			int (*layoutExecutable)(void *, processImage *);
			int (*link)(int, void *, processImage *, loaderSymbolTable **);
			int (*hotLink)(struct _kernelDynamicLibrary *);
		} executable;
		struct {
			int (*load)(unsigned char *, int, int, int, image *);
			int (*save)(const char *, image *);
		} image;
		struct {
			int (*getInfo)(const char *, kernelFont *);
			int (*load)(unsigned char *, int, kernelFont *, int);
		} font;
	};

} kernelFileClass;

// The structure that describes a dynamic library ready for use by the loader
typedef struct _kernelDynamicLibrary {
	char name[MAX_NAME_LENGTH];
	void *code;
	void *codeVirtual;
	unsigned codePhysical;
	unsigned codeSize;
	void *data;
	void *dataVirtual;
	unsigned dataSize;
	unsigned imageSize;
	loaderSymbolTable *symbolTable;
	kernelRelocationTable *relocationTable;
	struct _kernelDynamicLibrary *next;
	kernelFileClass *classDriver;

} kernelDynamicLibrary;

// Functions exported by kernelLoader.c
void *kernelLoaderLoad(const char *, file *);
kernelFileClass *kernelLoaderGetFileClass(const char *);
kernelFileClass *kernelLoaderClassify(const char *, void *, unsigned,
	loaderFileClass *);
kernelFileClass *kernelLoaderClassifyFile(const char *, loaderFileClass *);
loaderSymbolTable *kernelLoaderGetSymbols(const char *);
loaderSymbol *kernelLoaderFindSymbol(const char *, loaderSymbolTable *);
int kernelLoaderCheckCommand(const char *);
int kernelLoaderLoadProgram(const char *, int);
int kernelLoaderLoadLibrary(const char *);
kernelDynamicLibrary *kernelLoaderGetLibrary(const char *);
kernelDynamicLibrary *kernelLoaderLinkLibrary(const char *);
void *kernelLoaderGetSymbol(const char *);
int kernelLoaderExecProgram(int, int);
int kernelLoaderLoadAndExec(const char *, int, int);

// These are format-specific file class functions
kernelFileClass *kernelFileClassBmp(void);
kernelFileClass *kernelFileClassIco(void);
kernelFileClass *kernelFileClassJpg(void);
kernelFileClass *kernelFileClassGif(void);
kernelFileClass *kernelFileClassPng(void);
kernelFileClass *kernelFileClassPpm(void);
kernelFileClass *kernelFileClassBoot(void);
kernelFileClass *kernelFileClassKeymap(void);
kernelFileClass *kernelFileClassPdf(void);
kernelFileClass *kernelFileClassZip(void);
kernelFileClass *kernelFileClassGzip(void);
kernelFileClass *kernelFileClassAr(void);
kernelFileClass *kernelFileClassTar(void);
kernelFileClass *kernelFileClassPcf(void);
kernelFileClass *kernelFileClassTtf(void);
kernelFileClass *kernelFileClassVbf(void);
kernelFileClass *kernelFileClassElf(void);
kernelFileClass *kernelFileClassMessage(void);
kernelFileClass *kernelFileClassConfig(void);
kernelFileClass *kernelFileClassHtml(void);
kernelFileClass *kernelFileClassText(void);
kernelFileClass *kernelFileClassBinary(void);

#define _KERNELLOADER_H
#endif

