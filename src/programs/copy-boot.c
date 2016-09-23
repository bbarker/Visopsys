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
//  copy-boot.c
//

// This is a program for writing Visopsys boot sectors.  It is meant to
// work both as part of a Visopsys installation and as a part of the build
// system, so, it's a little tricky like that and needs to be compiled for
// each.

/* This is the text that appears when a user requests help about this program
<help>

 -- copy-boot --

Write a Visopsys boot sector.

Usage:
  copy-boot <image> <disk>

The copy-boot command copies the named boot sector image to the named
physical disk.  Not useful to most users under normal circumstances.  It
is used, for example, by the installation program.  It could also be useful
in a system rescue situation.

Example:
  copy-boot /system/boot/bootsect.fat32 hd0

</help>
*/

#ifdef VISOPSYS
	#include <sys/api.h>
	#include <sys/env.h>
	#define _(string)	gettext(string)
	#define OSLOADER	"/vloader"
#else
	#define _GNU_SOURCE
	#define _(string)	string
	#define OSLOADER	"../build/vloader"
#endif

#include <errno.h>
#include <fcntl.h>
#include <libintl.h>
#include <locale.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/utsname.h>

#define FAT12_SIG		"FAT12   "
#define FAT16_SIG		"FAT16   "
#define FAT32_SIG		"FAT32   "

#ifdef DEBUG
	#define DEBUGMSG(message, arg...) printf(message, ##arg)
#else
	#define DEBUGMSG(message, arg...) do { } while (0)
#endif

// FAT structures.  We pad out the bits we don't care about.

typedef struct {
	unsigned char pad1[11];
	unsigned short bytesPerSector;
	unsigned char sectorsPerCluster;
	unsigned short reservedSectors;
	unsigned char numberOfFats;
	unsigned short rootDirEntries;
	unsigned char pad2[3];
	unsigned short fatSectors;
	unsigned char pad3[12];

} __attribute__((packed)) fatCommonBSHeader1;

typedef struct {
	unsigned char pad[18];
	char fsSignature[8];

} __attribute__((packed)) fatCommonBSHeader2;

typedef struct {
	fatCommonBSHeader1 common1;
	fatCommonBSHeader2 common2;

} __attribute__((packed)) fatBSHeader;

typedef struct {
	fatCommonBSHeader1 common1;
	unsigned fatSectors;
	unsigned char pad1[4];
	unsigned rootDirCluster;
	unsigned short fsInfoSector;
	unsigned short backupBootSector;
	unsigned char pad2[12];
	fatCommonBSHeader2 common2;

} __attribute__((packed)) fat32BSHeader;

typedef struct {
	unsigned char pad1[492];
	unsigned firstFreeCluster;
	unsigned char pad2[16];

} __attribute__((packed)) fat32FsInfo;


static void usage(char *name)
{
	printf(_("usage:\n%s <boot image> <output file|device>\n"), name);
	return;
}


static int readSector(const char *inputName, unsigned sector,
	int bytesPerSector, void *buffer)
{
	int status = 0;
	int fd = 0;

#ifdef VISOPSYS
	// Is the destination a Visopsys disk name?
	if (inputName[0] != '/')
	{
		status = diskReadSectors(inputName, sector, 1, buffer);
		if (status < 0)
		{
			printf(_("Error reading disk %s\n"), inputName);
			return (errno = status);
		}
	}
	else
#endif
	{
		fd = open(inputName, O_RDONLY);
		if (fd < 0)
		{
			printf(_("Can't open device %s\n"), inputName);
			return (errno = fd);
		}

		status = lseek(fd, (sector * bytesPerSector), SEEK_SET);
		if (status < 0)
		{
			printf(_("Can't seek device to sector %u\n"), sector);
			close(fd);
			return (errno = status);
		}

		status = read(fd, buffer, bytesPerSector);

		close(fd);

		if ((status < 0) || (status < bytesPerSector))
		{
			printf("%s", _("Can't read sector\n"));
			return (errno = status = EIO);
		}
	}

	return (status = 0);
}



static int writeSector(const char *outputName, unsigned sector,
	int bytesPerSector, void *buffer)
{
	int status = 0;
	int fd = 0;

#ifdef VISOPSYS
	// Is the destination a Visopsys disk name?
	if (outputName[0] != '/')
	{
		status = diskWriteSectors(outputName, sector, 1, buffer);
		if (status < 0)
		{
			printf(_("Error writing disk %s\n"), outputName);
			return (errno = status);
		}
	}
	else
#endif
	{
		fd = open(outputName, (O_WRONLY | O_SYNC));
		if (fd < 0)
		{
			printf(_("Can't open device %s\n"), outputName);
			return (errno = fd);
		}

		status = lseek(fd, (sector * bytesPerSector), SEEK_SET);
		if (status < 0)
		{
			printf(_("Can't seek device to sector %u\n"), sector);
			close(fd);
			return (errno = status);
		}

		status = write(fd, buffer, bytesPerSector);

		close(fd);

		if ((status < 0) || (status < bytesPerSector))
		{
			printf("%s", _("Can't write sector\n"));
			return (errno = status = EIO);
		}
	}

	return (status = 0);
}


static int readBootsect(const char *inputName, unsigned char *bootSect)
{
	int status = 0;

	DEBUGMSG(_("Read boot sector from %s\n"), inputName);

	status = readSector(inputName, 0, 512, bootSect);
	if (status < 0)
	{
		DEBUGMSG(_("Couldn't read boot sector from %s\n"), inputName);
		return (errno = status);
	}

	if ((bootSect[510] != 0x55) || (bootSect[511] != 0xAA))
	{
		DEBUGMSG(_("%s is not a valid boot sector\n"), inputName);
		errno = EINVAL;
		return (-1);
	}

	return (errno = status = 0);
}


static int writeBootsect(const char *outputName, unsigned char *bootSect)
{
	int status = 0;
	fat32BSHeader *fat32Header = (fat32BSHeader *) bootSect;

	DEBUGMSG(_("Write boot sector to %s\n"), outputName);

	status = writeSector(outputName, 0, 512, bootSect);
	if (status < 0)
	{
		DEBUGMSG(_("Couldn't write boot sector to %s\n"), outputName);
		return (errno = status);
	}

	// If we think this is a FAT32 boot sector, we need to write the backup
	// one as well.
	if (!strncmp(fat32Header->common2.fsSignature, FAT32_SIG, 8))
	{
		status = writeSector(outputName, fat32Header->backupBootSector, 512,
			bootSect);
		if (status < 0)
		{
			DEBUGMSG(_("Couldn't write backup boot sector to %s\n"),
				outputName);
			return (errno = status);
		}
	}

	return (errno = status = 0);
}


static int merge(unsigned char *oldBootsect, unsigned char *newBootsect)
{
	// Merge the old and new boot sectors.
	fatBSHeader *fatHeader = (fatBSHeader *) oldBootsect;
	fat32BSHeader *fat32Header = (fat32BSHeader *) oldBootsect;

	DEBUGMSG("%s", _("Merge boot sectors\n"));

	if (!strncmp(fatHeader->common2.fsSignature, FAT12_SIG, 8) ||
		!strncmp(fatHeader->common2.fsSignature, FAT16_SIG, 8))
	{
		memcpy((newBootsect + 3), (oldBootsect + 3),
			(sizeof(fatBSHeader) - 3));
	}
	else if (!strncmp(fat32Header->common2.fsSignature, FAT32_SIG, 8))
	{
		memcpy((newBootsect + 3), (oldBootsect + 3),
			(sizeof(fat32BSHeader) - 3));
	}
	else
	{
		printf("%s", _("File system type is not supported\n"));
		errno = EINVAL;
		return (-1);
	}

	return (errno = 0);
}


static unsigned findUnusedCluster(const char *outputName, char *signature,
	fatCommonBSHeader1 *bsHeader)
{
	// Given the device name and the starting sector of the FAT, load the FAT
	// and return the first unused cluster number

	int status = 0;
	unsigned char *buffer;
	// Guess, we guess.  The standard is 2 but it's often not correct, and
	// *will not* be correct for FAT32 (the root directory uses clusters)
	unsigned firstUnused = 2;
	unsigned count;

	DEBUGMSG("%s", _("Find first unused cluster\n"));

	buffer = malloc(bsHeader->bytesPerSector);
	if (!buffer)
	{
		DEBUGMSG(_("Can't alloc %u bytes to find an unused cluster\n"),
			bsHeader->bytesPerSector);
		goto out;
	}

	status = readSector(outputName, bsHeader->reservedSectors,
		bsHeader->bytesPerSector, buffer);
	if (status < 0)
	{
		DEBUGMSG("%s", _("Can't read FAT sector\n"));
		goto out;
	}

	if (!strncmp(signature, FAT12_SIG, 8))
	{
		unsigned short entry = 0;
		for (count = 2; count < (bsHeader->bytesPerSector / (unsigned) 3);
			count ++)
		{
			entry = *((unsigned short *)(buffer + (count + (count >> 1))));

			// 0 = mask, since the extra data is in the upper 4 bits
			// 1 = shift, since the extra data is in the lower 4 bits
			if (count % 2)
				entry = ((entry & 0xFFF0) >> 4);
			else
				entry = (entry & 0x0FFF);

			if (!entry)
			{
				firstUnused = count;
				break;
			}
		}
	}
	else if (!strncmp(signature, FAT16_SIG, 8))
	{
		unsigned short *fat = (unsigned short *) buffer;
		for (count = 2; count < (bsHeader->bytesPerSector /
			sizeof(unsigned short)); count ++)
		{
			if (!fat[count])
			{
				firstUnused = count;
				break;
			}
		}
	}
	else if (!strncmp(signature, FAT32_SIG, 8))
	{
		unsigned *fat = (unsigned *) buffer;
		for (count = 2; count < (bsHeader->bytesPerSector / sizeof(unsigned));
			count ++)
		{
			// Really only the bottom 28 bits of this value are relevant
			if (!(fat[count] & 0x0FFFFFFF))
			{
				firstUnused = count;
				break;
			}
		}
	}
	else
		printf(_("Unknown FAT type %s\n"), signature);

	 out:
	if (buffer)
		free(buffer);
	DEBUGMSG(_("First unused cluster %u\n"), firstUnused);
	return (firstUnused);
}


static int setOsLoaderParams(const char *outputName,
	unsigned char *newBootsect, const char *osLoader)
{
	// Our new, generic boot sector keeps the logical sector and length of the
	// OS loader in the 3rd and 2nd last dwords, respectively.  This allows the
	// boot sector code to be simpler and requires little-to-no understanding
	// of the filesystem, which is good.
	//
	// So, given a pointer to the OS loader program and the contents of the
	// new boot sector, calculate the starting logical sector the boot sector
	// will occupy (the first user-data sector) and its length in sectors, and
	// write them into the boot sector.

	int status = 0;
	struct stat statBuff;
	fatBSHeader *fatHeader = (fatBSHeader *) newBootsect;
	fat32BSHeader *fat32Header = (fat32BSHeader *) newBootsect;
	unsigned firstUnusedCluster = 0;
	unsigned fatSectors = 0;
	unsigned *firstUserSector = (unsigned *)(newBootsect + 502);
	unsigned *osLoaderSectors = (unsigned *)(newBootsect + 506);

	DEBUGMSG("%s", _("Set OS loader parameters\n"));

	if (!strncmp(fatHeader->common2.fsSignature, FAT12_SIG, 8) ||
	!strncmp(fatHeader->common2.fsSignature, FAT16_SIG, 8))
	{
		if (!strncmp(fatHeader->common2.fsSignature, FAT12_SIG, 8))
			DEBUGMSG("%s", _("Target filesystem is FAT12\n"));
		else if (!strncmp(fatHeader->common2.fsSignature, FAT16_SIG, 8))
			DEBUGMSG("%s", _("Target filesystem is FAT16\n"));
		DEBUGMSG(_("%u reserved\n"),
			(unsigned) fatHeader->common1.reservedSectors);
		DEBUGMSG(_("%u FATs of %u\n"),
			(unsigned) fatHeader->common1.numberOfFats,
			(unsigned) fatHeader->common1.fatSectors);
		DEBUGMSG(_("%u root dir sectors\n"),
			(((unsigned) fatHeader->common1.rootDirEntries * 32) /
				fatHeader->common1.bytesPerSector));
		DEBUGMSG(_("Sectors per cluster %u\n"),
			(unsigned) fatHeader->common1.sectorsPerCluster);

		firstUnusedCluster = findUnusedCluster(outputName,
			fatHeader->common2.fsSignature, &fatHeader->common1);

#ifndef VISOPSYS
		// For some reason the Linux driver gives us the second unused cluster
		firstUnusedCluster += 1;
#endif

		*firstUserSector =
			((unsigned) fatHeader->common1.reservedSectors +
			((unsigned) fatHeader->common1.numberOfFats *
				(unsigned) fatHeader->common1.fatSectors) +
			(((unsigned) fatHeader->common1.rootDirEntries * 32) /
				fatHeader->common1.bytesPerSector) +
			((firstUnusedCluster - 2) *
				(unsigned) fatHeader->common1.sectorsPerCluster));
	}
	else if (!strncmp(fat32Header->common2.fsSignature, FAT32_SIG, 8))
	{
		fatSectors = (unsigned) fat32Header->common1.fatSectors;
		if (fat32Header->fatSectors)
			fatSectors = (unsigned) fat32Header->fatSectors;

		DEBUGMSG("%s", _("Target filesystem is FAT32\n"));
		DEBUGMSG(_("%u reserved\n"),
			(unsigned) fat32Header->common1.reservedSectors);
		DEBUGMSG(_("%u FATs of %u\n"),
			(unsigned) fat32Header->common1.numberOfFats, fatSectors);
		DEBUGMSG(_("Root dir cluster %u\n"),
			(unsigned) fat32Header->rootDirCluster);
		DEBUGMSG(_("Sectors per cluster %u\n"),
			(unsigned) fat32Header->common1.sectorsPerCluster);

		// Read the FAT32 FSInfo sector

		firstUnusedCluster = findUnusedCluster(outputName,
			fat32Header->common2.fsSignature, &fat32Header->common1);

		*firstUserSector =
			((unsigned) fat32Header->common1.reservedSectors +
			((unsigned) fat32Header->common1.numberOfFats * fatSectors) +
			((firstUnusedCluster - 2) *
				(unsigned) fat32Header->common1.sectorsPerCluster));
	}

	DEBUGMSG(_("First user sector for OS loader is %u\n"), *firstUserSector);

	memset(&statBuff, 0, sizeof(struct stat));
	status = stat(osLoader, &statBuff);
	if (status < 0)
		return (status);

	*osLoaderSectors = (((unsigned) statBuff.st_size +
		(fatHeader->common1.bytesPerSector - 1)) /
		fatHeader->common1.bytesPerSector);

	DEBUGMSG(_("OS loader sectors are %u\n"), *osLoaderSectors);

	return (errno = status = 0);
}


static void addBootSignature(unsigned char *newBootsect)
{
	// We use a unique signature in each Visopsys boot sector, in order for
	// the kernel to determine which device it was *really* booted from
	// (information about this from the BIOS can be misleading or inadequate)

	time_t t = 0;

	DEBUGMSG("%s", _("Add boot sector signature\n"));
	time(&t);
	memcpy((newBootsect + 498), &t, 4);
}


int main(int argc, char *argv[])
{
	int status = 0;
	char *sourceName = NULL;
	char *destName = NULL;
	unsigned char oldBootsect[512];
	unsigned char newBootsect[512];

#ifdef VISOPSYS
	setlocale(LC_ALL, getenv(ENV_LANG));
	textdomain("copy-boot");
#endif

	// Check that we're compiled the way we expect
	if (sizeof(fatBSHeader) != 0x3E)
	{
		printf(_("fatBSHeader size is 0x%02x instead of 0x3E\n"),
			(unsigned char) sizeof(fatBSHeader));
		errno = EINVAL;
		return (-1);
	}
	if (sizeof(fat32BSHeader) != 0x5A)
	{
		printf(_("fat32BSHeader size is 0x%02x instead of 0x5A\n"),
			(unsigned char) sizeof(fat32BSHeader));
		errno = EINVAL;
		return (-1);
	}
	if (sizeof(fat32FsInfo) != 512)
	{
		printf(_("fat32FsInfo size is %d instead of 512\n"),
			(int) sizeof(fat32FsInfo));
		errno = EINVAL;
		return (-1);
	}

	if (argc != 3)
	{
		usage(argv[0]);
		errno = EINVAL;
		return (status = -1);
	}

	sourceName = argv[1];
	destName = argv[2];

	// Read the new boot sector from the source file
	status = readBootsect(sourceName, newBootsect);
	if (status < 0)
	{
		perror(argv[0]);
		return (status);
	}

	// Read the old boot sector from the target device
	status = readBootsect(destName, oldBootsect);
	if (status < 0)
	{
		perror(argv[0]);
		return (status);
	}

	status = merge(oldBootsect, newBootsect);
	if (status < 0)
	{
		perror(argv[0]);
		return (status);
	}

	status = setOsLoaderParams(destName, newBootsect, OSLOADER);
	if (status < 0)
	{
		perror(argv[0]);
		return (status);
	}

	addBootSignature(newBootsect);

	// Write the new boot sector
	status = writeBootsect(destName, newBootsect);
	if (status < 0)
	{
		perror(argv[0]);
		return (status);
	}

	// Return success
	return (errno = status = 0);
}

