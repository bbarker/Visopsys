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
//  kernelFilesystem.h
//

#if !defined(_KERNELFILESYSTEM_H)

#include "kernelDisk.h"
#include <sys/file.h>
#include <sys/progress.h>

// Definitions
#define FSNAME_EXT			"ext"
#define FSNAME_FAT			"fat"
#define FSNAME_ISO			"iso"
#define FSNAME_LINUXSWAP	"linux-swap"
#define FSNAME_NTFS			"ntfs"
#define FSNAME_UDF			"udf"
#define MAX_FILESYSTEMS		32
#define MAX_FS_NAME_LENGTH	64

typedef struct {
	uquad_t usedSectors;
	uquad_t freeSectors;
	unsigned blockSize;

} kernelFilesystemStats;

// This is the structure that is used to define a file system
// driver
typedef struct _kernelFilesystemDriver {
	char *driverTypeName;
	int (*driverDetect)(kernelDisk *);
	int (*driverFormat)(kernelDisk *, const char *, const char *, int,
		progress *);
	int (*driverClobber)(kernelDisk *);
	int (*driverCheck)(kernelDisk *, int, int, progress *);
	int (*driverDefragment)(kernelDisk *, progress *);
	int (*driverStat)(kernelDisk *, kernelFilesystemStats *);
	uquad_t (*driverGetFreeBytes)(kernelDisk *);
	int (*driverResizeConstraints)(kernelDisk *, uquad_t *, uquad_t *,
		progress *);
	int (*driverResize)(kernelDisk *, uquad_t, progress *);
	int (*driverMount)(kernelDisk *);
	int (*driverUnmount)(kernelDisk *);
	int (*driverNewEntry)(kernelFileEntry *);
	int (*driverInactiveEntry)(kernelFileEntry *);
	int (*driverResolveLink)(kernelFileEntry *);
	int (*driverReadFile)(kernelFileEntry *, unsigned, unsigned,
		unsigned char *);
	int (*driverWriteFile)(kernelFileEntry *, unsigned, unsigned,
		unsigned char *);
	int (*driverCreateFile)(kernelFileEntry *);
	int (*driverDeleteFile)(kernelFileEntry *);
	int (*driverFileMoved)(kernelFileEntry *);
	int (*driverReadDir)(kernelFileEntry *);
	int (*driverWriteDir)(kernelFileEntry *);
	int (*driverMakeDir)(kernelFileEntry *);
	int (*driverRemoveDir)(kernelFileEntry *);
	int (*driverTimestamp)(kernelFileEntry *);
	int (*driverSetBlocks)(kernelFileEntry *, unsigned);

} kernelFilesystemDriver;

// The default driver initializations
int kernelFilesystemExtInitialize(void);
int kernelFilesystemFatInitialize(void);
int kernelFilesystemIsoInitialize(void);
int kernelFilesystemLinuxSwapInitialize(void);
int kernelFilesystemNtfsInitialize(void);
int kernelFilesystemUdfInitialize(void);

// Functions exported by kernelFilesystem.c
int kernelFilesystemScan(const char *);
int kernelFilesystemFormat(const char *, const char *, const char *, int,
	progress *);
int kernelFilesystemClobber(const char *);
int kernelFilesystemCheck(const char *, int, int, progress *);
int kernelFilesystemDefragment(const char *, progress *);
int kernelFilesystemStat(const char *, kernelFilesystemStats *);
int kernelFilesystemResizeConstraints(const char *, uquad_t *, uquad_t *,
	progress *);
int kernelFilesystemResize(const char *, uquad_t, progress *);
int kernelFilesystemMount(const char *, const char *);
int kernelFilesystemUnmount(const char *);
int kernelFilesystemRemoved(const char *);
kernelDisk *kernelFilesystemGet(char *);
uquad_t kernelFilesystemGetFreeBytes(const char *);
unsigned kernelFilesystemGetBlockSize(const char *);

#define _KERNELFILESYSTEM_H
#endif

