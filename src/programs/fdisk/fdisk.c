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
//  fdisk.c
//

// This is a program for modifying partition tables and doing other disk
// management tasks.

/* This is the text that appears when a user requests help about this program
<help>

 -- fdisk --

Also known as the "Disk Manager", fdisk is a hard disk partitioning tool.
It can create, delete, format, resize, and move partitions and modify their
attributes.  It can copy entire hard disks from one to another.

Usage:
  fdisk [-T] [disk_name]

The fdisk program is interactive, and can be used in either text or graphics
mode.  It provides the same functionality in both modes; text mode operation
is menu-driven.

The disk can be automatically selected by specifying its name (as listed by
the 'disks' command) as the last argument.

Options:
-T  : Force text mode operation

</help>
*/

#include "fdisk.h"
#include "gpt.h"
#include "msdos.h"
#include <dlfcn.h>
#include <errno.h>
#include <libintl.h>
#include <locale.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/api.h>
#include <sys/ascii.h>
#include <sys/env.h>
#include <sys/fat.h>
#include <sys/paths.h>
#include <sys/vsh.h>

#define _(string) gettext(string)
#define gettext_noop(string) (string)

#define PERM				_("You must be a privileged user to use this " \
	"command.\n(Try logging in as user \"admin\")")
#define PARTTYPES			_("Supported Partition Types")
#define STARTMB_MESSAGE		_("Enter start in megabytes (%u-%u)")
#define SIZEMB_MESSAGE		_("Enter size in megabytes (%u-%u)")

static const char *programName = NULL;
static int processId = 0;
static int readOnly = 1;
static diskLabel *gptLabel = NULL;
static diskLabel *msdosLabel = NULL;
static int numberDisks = 0;
static disk *disks = NULL;
partitionTable *table = NULL;
static textScreen screen;
static char *tmpBackupName = NULL;
static char sliceListHeader[SLICESTRING_LENGTH + 1];
static listItemParameters *diskListParams = NULL;
static int (*ntfsFormat)(const char *, const char *, int, progress *);
static int (*ntfsGetResizeConstraints)(const char *, uquad_t *, uquad_t *,
	progress *) = NULL;
static int (*ntfsResize)(const char *, uquad_t, progress *) = NULL;
static ioThreadArgs readerArgs;
static ioThreadArgs writerArgs;
static int ioThreadsTerminate = 0;
static int ioThreadsFinished = 0;
static slice clipboardSlice;
static disk *clipboardDisk = NULL;
static int clipboardSliceValid = 0;

// GUI stuff
static int graphics = 0;
static objectKey window = NULL;
static objectKey fileMenu = NULL;
static objectKey diskMenu = NULL;
static objectKey partMenu = NULL;
static objectKey diskList = NULL;
static objectKey removableCheckbox = NULL;
static objectKey canvas = NULL;
static objectKey sliceList = NULL;
static objectKey writeButton = NULL;
static objectKey undoButton = NULL;
static objectKey defragButton = NULL;
static objectKey setActiveButton = NULL;
static objectKey deleteButton = NULL;
static objectKey deleteAllButton = NULL;
static objectKey formatButton = NULL;
static objectKey hideButton = NULL;
static objectKey infoButton = NULL;
static objectKey moveButton = NULL;
static objectKey createButton = NULL;
static objectKey resizeButton = NULL;
static int canvasWidth = 600;
static int canvasHeight = 60;

// Menus

#define FILEMENU_WRITE			0
#define FILEMENU_UNDO			1
#define FILEMENU_RESTOREBACKUP	2
#define FILEMENU_QUIT			3
static windowMenuContents fileMenuContents = {
	4,
	{
		{ gettext_noop("Write"), NULL },
		{ gettext_noop("Undo"), NULL },
		{ gettext_noop("Restore backup"), NULL },
		{ gettext_noop("Quit"), NULL }
	}
};

#define DISKMENU_COPYDISK		0
#define DISKMENU_PARTORDER		1
#define DISKMENU_SIMPLEMBR		2
#define DISKMENU_BOOTMENU		3
#define DISKMENU_DISKINFO		4
#define DISKMENU_ERASEDISK		5
#define DISKMENU_NEWLABEL		6
static windowMenuContents diskMenuContents = {
	7,
	{
		{ gettext_noop("Copy disk"), NULL },
		{ gettext_noop("Partition order"), NULL },
		{ gettext_noop("Write basic MBR"), NULL },
		{ gettext_noop("MBR boot menu"), NULL },
		{ gettext_noop("Disk info"), NULL },
		{ gettext_noop("Erase disk"), NULL },
		{ gettext_noop("New disk label"), NULL }
	}
};

#define PARTMENU_COPY			0
#define PARTMENU_PASTE			1
#define PARTMENU_SETACTIVE		2
#define PARTMENU_DELETE			3
#define PARTMENU_FORMAT			4
#define PARTMENU_DEFRAG			5
#define PARTMENU_RESIZE			6
#define PARTMENU_HIDE			7
#define PARTMENU_INFO			8
#define PARTMENU_LISTTYPES		9
#define PARTMENU_MOVE			10
#define PARTMENU_CREATE			11
#define PARTMENU_DELETEALL		12
#define PARTMENU_SETTYPE		13
#define PARTMENU_ERASE			14
static windowMenuContents partMenuContents = {
	15,
	{
		{ gettext_noop("Copy"), NULL },
		{ gettext_noop("Paste"), NULL },
		{ gettext_noop("Set active"), NULL },
		{ gettext_noop("Delete"), NULL },
		{ gettext_noop("Format"), NULL },
		{ gettext_noop("Defragment"), NULL },
		{ gettext_noop("Resize"), NULL },
		{ gettext_noop("Hide/Unhide"), NULL },
		{ gettext_noop("Info"), NULL },
		{ gettext_noop("List types"), NULL },
		{ gettext_noop("Move"), NULL },
		{ gettext_noop("Create"), NULL },
		{ gettext_noop("Delete all"), NULL },
		{ gettext_noop("Set type"), NULL },
		{ gettext_noop("Erase"), NULL }
	}
};


static int yesOrNo(char *question)
{
	char character;

	if (graphics)
		return (windowNewQueryDialog(window, _("Confirmation"), question));

	else
	{
		printf(_("\n%s (y/n): "), question);
		textInputSetEcho(0);

		while (1)
		{
			character = getchar();

			if ((character == 'y') || (character == 'Y'))
			{
				printf("%s", _("Yes\n"));
				textInputSetEcho(1);
				return (1);
			}
			else if ((character == 'n') || (character == 'N'))
			{
				printf("%s", _("No\n"));
				textInputSetEcho(1);
				return (0);
			}
		}
	}
}


static void quit(int status, int force)
{
	// Shut everything down

	if (!force && table->changesPending &&
		!yesOrNo(_("Quit without writing changes?")))
	{
		return;
	}

	if (graphics)
	{
		windowGuiStop();
		if (window)
			windowDestroy(window);
	}
	else
	{
		if (screen.data)
			textScreenRestore(&screen);
	}

	if (tmpBackupName)
	{
		fileDelete(tmpBackupName);
		tmpBackupName = NULL;
	}

	// Free any malloc'ed global memory

	if (screen.data)
		memoryRelease(screen.data);

	if (disks)
		free(disks);

	if (diskListParams)
		free(diskListParams);

	if (table)
		free(table);

	exit(errno = status);
}


static char readKey(const char *choices, int allowCursor)
{
	int stringLength = strlen(choices);
	char character;
	int count;

	textInputSetEcho(0);

	while (1)
	{
		character = getchar();

		if (allowCursor &&
			((character == (char) ASCII_CRSRUP) ||
			(character == (char) ASCII_CRSRDOWN)))
		{
			return (character);
		}

		for (count = 0; count < stringLength; count ++)
		{
			if (character == choices[count])
			{
				printf("%c\n", character);
				textInputSetEcho(1);
				return (character);
			}
		}
	}
}


static int readLine(const char *choices, char *buffer, int length)
{
	int choicesLength = strlen(choices);
	int count1, count2;

	for (count1 = 0; count1 < (length - 1); count1 ++)
	{
		textInputSetEcho(0);
		buffer[count1] = getchar();
		textInputSetEcho(1);

		if (buffer[count1] == 10) // Newline
		{
			buffer[count1] = '\0';
			break;
		}

		if (buffer[count1] == 8) // Backspace
		{
			if (count1 > 0)
			{
				textBackSpace();
				count1 -= 2;
			}
			else
				count1 -= 1;
			continue;
		}

		for (count2 = 0; count2 < choicesLength; count2 ++)
		{
			if (buffer[count1] == choices[count2])
			{
				printf("%c", buffer[count1]);
				break;
			}
		}

		if (buffer[count1] != choices[count2])
			count1--;
	}

	// Make sure there's a NULL at the end of buffer.
	buffer[length - 1] = '\0';

	printf("\n");

	return (0);
}


void pause(void)
{
	printf("%s", _("\nPress any key to continue. "));
	getchar();
	printf("\n");
}


void error(const char *format, ...)
{
	// Generic error message code for either text or graphics modes

	va_list list;
	char *output = NULL;

	output = malloc(MAXSTRINGLENGTH);
	if (!output)
		return;

	va_start(list, format);
	vsnprintf(output, MAXSTRINGLENGTH, format, list);
	va_end(list);

	if (graphics)
	{
		windowNewErrorDialog(window, _("Error"), output);
	}
	else
	{
		printf("\n\n%s\n", output);
		pause();
	}

	free(output);
}


void warning(const char *format, ...)
{
	// Generic error message code for either text or graphics modes

	va_list list;
	char *output = NULL;

	output = malloc(MAXSTRINGLENGTH);
	if (!output)
		return;

	va_start(list, format);
	vsnprintf(output, MAXSTRINGLENGTH, format, list);
	va_end(list);

	if (graphics)
		windowNewErrorDialog(window, _("Warning"), output);
	else
	{
		printf(_("\n\nWARNING: %s\n"), output);
		pause();
	}

	free(output);
}


static labelType detectLabel(disk *theDisk)
{
	labelType type = label_none;

	// Have to try GPT before MS-DOS
	if (gptLabel->detect(theDisk) == 1)
		type = label_gpt;

	// Now MS-DOS
	else if (msdosLabel->detect(theDisk) == 1)
		type = label_msdos;

	return (type);
}


static const char *labelType2String(labelType type)
{
	switch (type)
	{
		case label_msdos:
			return "MS-DOS";
		case label_gpt:
			return "GPT";
		default:
			return _("unknown");
	}
}


static uquad_t megabytesToSectors(disk *theDisk, unsigned megabytes)
{
	if (!megabytes)
		return (0);

	return (((uquad_t)(1048576 / theDisk->sectorSize) * (uquad_t) megabytes));
}


static unsigned sectorsToMegabytes(disk *theDisk, uquad_t sectors, int round)
{
	uquad_t sectsPerMb = 0;
	uquad_t megabytes = 0;

	if (!sectors)
		return (0);

	sectsPerMb = megabytesToSectors(theDisk, 1);
	megabytes = (sectors / sectsPerMb);

	if (!round)
	{
		// Round 'naturally'
		if ((sectors % sectsPerMb) >= (sectsPerMb / 2))
			megabytes += 1;
	}
	else if (round > 0)
	{
		// Round up
		if (sectors % sectsPerMb)
			megabytes += 1;
	}

	return ((unsigned) megabytes);
}


static int scanDisks(void)
{
	int status = 0;
	int tmpNumberDisks = 0;
	disk *tmpDiskInfo = NULL;
	int showRemovable = 1;
	int count;

	// Call the kernel to give us the number of available disks
	tmpNumberDisks = diskGetPhysicalCount();
	if (tmpNumberDisks <= 0)
		return (status = ERR_NOSUCHENTRY);

	tmpDiskInfo = malloc(tmpNumberDisks * sizeof(disk));
	if (!tmpDiskInfo)
		return (status = ERR_MEMORY);

	// Read disk info into our temporary structure
	status = diskGetAllPhysical(tmpDiskInfo, (tmpNumberDisks * sizeof(disk)));
	if (status < 0)
	{
		// Eek.  Problem getting disk info.
		free(tmpDiskInfo);
		return (status);
	}

	if (diskListParams)
		free(diskListParams);

	diskListParams = malloc(tmpNumberDisks * sizeof(listItemParameters));
	if (!diskListParams)
	{
		free(tmpDiskInfo);
		return (status = ERR_MEMORY);
	}

	// Show removable disks?
	if (graphics)
	{
		showRemovable = 0;
		if (removableCheckbox)
			windowComponentGetSelected(removableCheckbox, &showRemovable);
	}

	// Loop through these disks, figuring out which ones are hard disks
	// and putting them into the regular array
	numberDisks = 0;
	for (count = 0; count < tmpNumberDisks; count ++)
	{
		if ((tmpDiskInfo[count].type & DISKTYPE_HARDDISK) &&
			(!(tmpDiskInfo[count].type & DISKTYPE_REMOVABLE) || showRemovable))
		{
			memcpy(&disks[numberDisks], &tmpDiskInfo[count], sizeof(disk));

			snprintf(diskListParams[numberDisks].text, WINDOW_MAX_LABEL_LENGTH,
				_("Disk %d: [%s] %s, %u MB, %u bytes/sec"), numberDisks,
				disks[numberDisks].name,
				labelType2String(detectLabel(&disks[numberDisks])),
				sectorsToMegabytes(&disks[numberDisks],
					disks[numberDisks].numSectors, 1 /* round up */),
				disks[numberDisks].sectorSize);

			numberDisks += 1;
		}
	}

	free(tmpDiskInfo);

	if (numberDisks <= 0)
		return (status = ERR_NOSUCHENTRY);
	else
		return (status = 0);
}


static int isSliceReserved(partitionTable *t, int sliceNum)
{
	if (t->label->firstUsableSect)
	{
		if (t->slices[sliceNum].raw.startSector < t->label->firstUsableSect)
			return (1);
	}

	if (t->label->lastUsableSect < (t->disk->numSectors - 1))
	{
		if (t->slices[sliceNum].raw.startSector >= t->label->lastUsableSect)
			return (1);
	}

	return (0);
}


static int isSliceUsed(partitionTable *t, int sliceNum)
{
	if (t->label->flags & LABELFLAG_USETAGS)
	{
		if (t->slices[sliceNum].raw.tag)
			return (1);
		else
			return (0);
	}

	else if (t->label->flags & LABELFLAG_USEGUIDS)
	{
		if (memcmp(&t->slices[sliceNum].raw.typeGuid, &GUID_UNUSED,
			sizeof(guid)))
		{
			return (1);
		}
		else
		{
			return (0);
		}
	}

	else
	{
		error("%s", _("Can't determine whether slice is used"));
		return 1;
	}
}


static void insertSliceAt(partitionTable *t, int sliceNumber)
{
	// Just moves part of the slice list to accommodate an insertion.

	int count;

	for (count = t->numSlices; count > sliceNumber; count --)
		memcpy(&t->slices[count], &t->slices[count - 1], sizeof(slice));

	table->numSlices += 1;
}


static void removeSliceAt(partitionTable *t, int sliceNumber)
{
	// Just moves part of the slice list to accommodate a removal.

	int count;

	for (count = (sliceNumber + 1); count < t->numSlices; count ++)
		memcpy(&t->slices[count - 1], &t->slices[count], sizeof(slice));

	table->numSlices -= 1;
}


static void makeReservedSlice(partitionTable *t, int sliceNumber,
	uquad_t startSector, uquad_t numSectors)
{
	// Given a slice entry and location/size, make a reserved slice for it.

	slice *reservedSlice = &t->slices[sliceNumber];

	memset(reservedSlice, 0, sizeof(slice));

	reservedSlice->raw.startSector = startSector;
	reservedSlice->raw.numSectors = numSectors;
}


static void updateReservedSlices(partitionTable *t)
{
	// Make all the reserved slices reflect the actual reserved spaces on the
	// disk.

	int count;

	// First remove any existing reserved slices
	for (count = 0; count < t->numSlices; count ++)
	{
		if (isSliceReserved(t, count))
		{
			removeSliceAt(t, count);
			count -= 1;
		}
	}

	// Now insert reserved slices where appropriate

	if (t->label->firstUsableSect)
	{
		insertSliceAt(t, 0);
		makeReservedSlice(t, 0, 0, t->label->firstUsableSect);
	}

	if (t->label->lastUsableSect < (t->disk->numSectors - 1))
	{
		count = table->numSlices;
		insertSliceAt(t, count);
		makeReservedSlice(t, count, (t->label->lastUsableSect + 1),
			(t->disk->numSectors - (t->label->lastUsableSect + 1)));
	}
}


static void makeEmptySlice(partitionTable *t, int sliceNumber,
	uquad_t startSector, uquad_t numSectors)
{
	// Given a slice number and location/size, make a slice for it.

	slice *emptySlice = &t->slices[sliceNumber];

	memset(emptySlice, 0, sizeof(slice));

	emptySlice->raw.startSector = startSector;
	emptySlice->raw.numSectors = numSectors;
}


static void updateEmptySlices(partitionTable *t)
{
	// Make all the empty slices reflect the actual empty spaces on the disk.

	int count;

	// First remove any existing empty slices
	for (count = 0; count < t->numSlices; count ++)
	{
		if (!isSliceReserved(t, count) && !isSliceUsed(t, count))
		{
			removeSliceAt(t, count);
			count -= 1;
		}
	}

	// Now loop through the real slices and insert empty slices where
	// appropriate
	for (count = 0; count < t->numSlices; count ++)
	{
		// Is there empty space between this slice and the previous slice?
		if ((!count && (t->slices[count].raw.startSector > 0)) ||
			(count && (t->slices[count].raw.startSector >
				(t->slices[count - 1].raw.startSector +
					t->slices[count - 1].raw.numSectors))))
		{
			// If we're not at the first slice, and there's just a small
			// (< 1Mb) space before a logical slice, ignore it.
			if (count && (t->slices[count].raw.type == partition_logical) &&
				(sectorsToMegabytes(t->disk,
					(t->slices[count].raw.startSector -
						(t->slices[count - 1].raw.startSector +
							t->slices[count - 1].raw.numSectors)),
					0 /* round naturally */) < 1))
			{
				continue;
			}

			insertSliceAt(t, count);

			makeEmptySlice(t, count,
				(count? (t->slices[count - 1].raw.startSector +
					t->slices[count - 1].raw.numSectors) : 0),
				(count? (t->slices[count].raw.startSector -
					(t->slices[count - 1].raw.startSector +
						t->slices[count - 1].raw.numSectors)) :
					t->slices[count].raw.startSector));

			count += 1;
		}
	}

	// Is there empty space at the end of the disk?
	if (!t->numSlices || ((t->slices[t->numSlices - 1].raw.startSector +
		t->slices[t->numSlices - 1].raw.numSectors) < t->disk->numSectors))
	{
		makeEmptySlice(t, t->numSlices,
			(t->numSlices? (t->slices[t->numSlices - 1].raw.startSector +
				t->slices[t->numSlices - 1].raw.numSectors) : 0),
			(t->numSlices? (t->disk->numSectors -
				(t->slices[t->numSlices - 1].raw.startSector +
					t->slices[t->numSlices - 1].raw.numSectors)) :
				t->disk->numSectors));

		t->numSlices += 1;
	}
}


static void getFsInfo(partitionTable *t, int sliceNumber)
{
	slice *slc = &t->slices[sliceNumber];
	disk tmpDisk;

	strcpy(slc->fsType, _("unknown"));

	if (!slc->diskName[0])
		return;

	if (diskGet(slc->diskName, &tmpDisk) < 0)
		return;

	slc->opFlags = tmpDisk.opFlags;

	if (strcmp(tmpDisk.fsType, _("unknown")))
		strncpy(slc->fsType, tmpDisk.fsType, FSTYPE_MAX_NAMELENGTH);

	return;
}


static void makeSliceString(partitionTable *t, int sliceNumber)
{
	slice *slc = &t->slices[sliceNumber];
	int position = 0;

	memset(slc->string, ' ', MAX_DESCSTRING_LENGTH);
	slc->string[MAX_DESCSTRING_LENGTH - 1] = '\0';

	if (isSliceUsed(t, sliceNumber))
	{
		// Slice/disk name
		strcpy(slc->string, slc->showSliceName);
		slc->string[strlen(slc->string)] = ' ';
		position += SLICESTRING_DISKFIELD_WIDTH;

		// Partition type description
		if (!t->label->getSliceDesc ||
			(t->label->getSliceDesc(&slc->raw, (slc->string + position)) < 0))
		{
			strcpy((slc->string + position), _("unknown"));
		}

		slc->string[strlen(slc->string)] = ' ';
		position += SLICESTRING_LABELFIELD_WIDTH;

		// Filesystem type
		sprintf((slc->string + position), "%s", slc->fsType);
	}

	else if (isSliceReserved(t, sliceNumber))
	{
		position += SLICESTRING_DISKFIELD_WIDTH;
		strcpy((slc->string + position), _("Reserved space"));
		position += SLICESTRING_LABELFIELD_WIDTH;
	}

	else
	{
		position += SLICESTRING_DISKFIELD_WIDTH;
		strcpy((slc->string + position), _("Empty space"));
		position += SLICESTRING_LABELFIELD_WIDTH;
	}

	slc->string[strlen(slc->string)] = ' ';
	position += SLICESTRING_FSTYPEFIELD_WIDTH;

	sprintf((slc->string + position), "%u", sectorsToMegabytes(t->disk,
		slc->raw.startSector, 0 /* round naturally */));
	slc->string[strlen(slc->string)] = ' ';
	position += SLICESTRING_STARTFIELD_WIDTH;

	sprintf((slc->string + position), "%u", sectorsToMegabytes(t->disk,
		slc->raw.numSectors, 0 /* round naturally */));
	position += SLICESTRING_SIZEFIELD_WIDTH;

	if (isSliceUsed(t, sliceNumber))
	{
		slc->string[strlen(slc->string)] = ' ';
		if (!ISLOGICAL(slc))
			sprintf((slc->string + position), "%s", _("primary"));
		else
			sprintf((slc->string + position), "%s", _("logical"));

		if (slc->raw.flags & SLICEFLAG_BOOTABLE)
			strcat(slc->string, _("/active"));
		else
			strcat(slc->string, "       ");
	}
}


static void updateSliceList(partitionTable *t)
{
	int count;

	// Update the reserved slices
	updateReservedSlices(t);

	// Update the empty slices
	updateEmptySlices(t);

	// Update the slice strings
	for (count = 0; count < t->numSlices; count ++)
	{
		if (isSliceUsed(t, count))
		{
			#ifdef PARTLOGIC
				snprintf(t->slices[count].showSliceName, 6, "%d",
					(t->slices[count].raw.order + 1));
			#else
				snprintf(t->slices[count].showSliceName, 6, "%s%c",
					t->disk->name, ('a' + t->slices[count].raw.order));
			#endif
			getFsInfo(t, count);
		}

		makeSliceString(t, count);
	}
}


static int readPartitionTable(disk *theDisk, partitionTable *t)
{
	// Read the partition table from the physical disk

	int status = 0;
	labelType type = label_none;
	char *fileName = NULL;
	static char *tmpBackupFileName = NULL;
	file backupFile;
	fileStream tmpBackupFile;
	int count;

	// Clear stack data
	memset(&backupFile, 0, sizeof(file));
	memset(&tmpBackupFile, 0, sizeof(fileStream));

	// Clear any existing partition table data
	memset(t, 0, sizeof(partitionTable));

	t->disk = theDisk;
	for (count = 0; count < numberDisks; count ++)
	{
		if (&disks[count] == t->disk)
		{
			t->diskNumber = count;
			break;
		}
	}

	// Detect the disk label.
	type = detectLabel(t->disk);
	if (type == label_gpt)
		t->label = gptLabel;
	else if (type == label_msdos)
		t->label = msdosLabel;

	if (t->label)
	{
		status = t->label->readTable(t->disk, t->rawSlices, &t->numRawSlices);
		if (status < 0)
			warning(_("Error %d reading partition table, data may be "
				"incorrect.\nProceed with caution."), status);
	}
	else
	{
		warning("%s", _("Unknown disk label.  Writing changes will create an "
			"MS-DOS label."));
		t->label = msdosLabel;
	}

	// Any backup partition table saved?  Construct the file name

	fileName = malloc(MAX_PATH_NAME_LENGTH);
	if (!fileName)
		return (status = ERR_MEMORY);

	sprintf(fileName, BACKUP_MBR, t->disk->name);

	if (!fileFind(fileName, NULL))
		t->backupAvailable = 1;
	else
		t->backupAvailable = 0;

	free(fileName);

	if (!readOnly)
	{
		// We are not read-only, create a new temporary backup
		if (tmpBackupName)
		{
			fileDelete(tmpBackupName);
			tmpBackupName = NULL;
		}

		status = fileGetTemp(&backupFile);
		if (status < 0)
		{
			warning("%s", _("Can't create backup file"));
			return (status);
		}

		if (!tmpBackupFileName)
		{
			tmpBackupFileName = malloc(MAX_PATH_NAME_LENGTH);
			if (!tmpBackupFileName)
				return (status = ERR_MEMORY);
		}

		fileGetFullPath(&backupFile, tmpBackupFileName, MAX_PATH_NAME_LENGTH);
		fileClose(&backupFile);

		status = fileStreamOpen(tmpBackupFileName, OPENMODE_WRITE,
			&tmpBackupFile);
		if (status < 0)
		{
			warning(_("Can't open backup file %s"), backupFile.name);
			return (status);
		}

		status = fileStreamWrite(&tmpBackupFile, sizeof(int),
			(char *) &t->numRawSlices);
		if (status < 0)
			warning("%s", _("Error writing backup partition table file"));

		status = fileStreamWrite(&tmpBackupFile,
			(t->numRawSlices * sizeof(rawSlice)), (char *) t->rawSlices);
		if (status < 0)
			warning("%s", _("Error writing backup partition table file"));

		fileStreamClose(&tmpBackupFile);
		tmpBackupName = tmpBackupFileName;
	}

	return (status = 0);
}


static int writePartitionTable(partitionTable *t)
{
	// Write the partition table to the physical disk

	int status = 0;
	char *fileName = NULL;
	int count1, count2;

	memset(t->rawSlices, 0, (DISK_MAX_PARTITIONS * sizeof(rawSlice)));
	t->numRawSlices = 0;

	// Copy the 'raw' data from the data slices into the raw slice list.
	for (count1 = 0; count1 < DISK_MAX_PARTITIONS; count1 ++)
	{
		for (count2 = 0; count2 < t->numSlices; count2 ++)
		{
			if (isSliceUsed(t, count2) &&
				(t->slices[count2].raw.order == count1))
			{
				memcpy(&t->rawSlices[count1], &t->slices[count2].raw,
					sizeof(rawSlice));
				t->numRawSlices += 1;
				break;
			}
		}
	}

	status = t->label->writeTable(t->disk, t->rawSlices, t->numRawSlices);
	if (status < 0)
		return (status);

	// Make the backup file permanent
	if (tmpBackupName)
	{
		fileName = malloc(MAX_PATH_NAME_LENGTH);
		if (!fileName)
			return (status = ERR_MEMORY);

		// Construct the backup file name
		snprintf(fileName, MAX_PATH_NAME_LENGTH, BACKUP_MBR, t->disk->name);

		// Copy the temporary backup file to the backup
		fileMove(tmpBackupName, fileName);

		free(fileName);

		tmpBackupName = NULL;

		// We now have a proper backup
		t->backupAvailable = 1;
	}

	diskSync(t->disk->name);
	t->changesPending = 0;

	return (status = 0);
}


static void guidString(char *string, guid *g)
{
	sprintf(string, "%08x-%04x-%04x-%02x%02x-%02x%02x%02x%02x%02x%02x",
		g->timeLow, g->timeMid, g->timeHighVers, g->clockSeqRes,
		g->clockSeqLow, g->node[0], g->node[1], g->node[2], g->node[3],
		g->node[4], g->node[5]);
}


static void makeSliceList(partitionTable *t)
{
	// This function populates the list of slices using the 'raw' slices list
	// in the partitionTable structure.

	rawSlice *raw = NULL;
	int firstPartition = -1;
	unsigned firstSector = -1;
	int count1, count2;

	memset(t->slices, 0, (MAX_SLICES * sizeof(slice)));
	t->numSlices = 0;

	// Loop through all the raw partitions and put them in our list, sorted by
	// logical starting sector.
	for (count1 = 0; count1 < t->numRawSlices; count1 ++)
	{
		firstPartition = -1;
		firstSector = 0xFFFFFFFF;

		for (count2 = 0; count2 < t->numRawSlices; count2 ++)
		{
			raw = &t->rawSlices[count2];

			// If we have already processed this one, continue
			if (t->numSlices && (raw->startSector <=
				t->slices[t->numSlices - 1].raw.startSector))
			{
				continue;
			}

			if (raw->startSector < firstSector)
			{
				firstSector = raw->startSector;
				firstPartition = count2;
			}
		}

		if (firstPartition < 0)
			break;

		raw = &t->rawSlices[firstPartition];

		// Now add a slice for the current partition
		memcpy(&t->slices[t->numSlices].raw, raw, sizeof(rawSlice));

		snprintf(t->slices[t->numSlices].diskName, 6, "%s%c",
			t->disk->name, ('a' + t->slices[t->numSlices].raw.order));

		t->numSlices += 1;
	}

	updateSliceList(t);
}


static int selectDisk(disk *theDisk)
{
	int status = 0;
	char tmpChar[80];

	if (table->changesPending)
	{
		sprintf(tmpChar, _("Discard changes to disk %s?"), table->disk->name);
		if (!yesOrNo(tmpChar))
		{
			if (graphics)
				// Re-select the old disk in the list
				windowComponentSetSelected(diskList, table->diskNumber);
			return (status = 0);
		}

		table->changesPending = 0;
	}

	// Check for geometry.  The way things are currently implemented, we need
	// some of these values to be non-zero.
	if (!theDisk->cylinders || !theDisk->heads ||
		!theDisk->sectorsPerCylinder || !theDisk->sectorSize)
	{
		error(_("Disk \"%s\" is missing geometry information."),
			theDisk->name);
		return (status = ERR_NOTIMPLEMENTED);
	}

	status = readPartitionTable(theDisk, table);
	if (status < 0)
		return (status);

	if (graphics)
		windowComponentSetSelected(diskList, table->diskNumber);

	// Make the slice list
	makeSliceList(table);

	table->selectedSlice = 0;
	return (status = 0);
}


static int queryDisk(void)
{
	int status;
	char *diskStrings[DISK_MAXDEVICES];
	int count;

	for (count = 0; count < numberDisks; count ++)
		diskStrings[count] = diskListParams[count].text;

	status = vshCursorMenu(_("Please choose the disk on which to operate:"),
		diskStrings, numberDisks, 10 /* max rows */, table->diskNumber);
	if (status < 0)
		return (status);

	status = selectDisk(&disks[status]);
	if (!table->disk)
		status = ERR_INVALID;
	return (status);
}


static void drawDiagram(void)
{
	// Draw a picture of the disk layout on our 'canvas' component

	int needPixels = 0;
	int xCoord = 0;
	windowDrawParameters params;
	static color colors[DISK_MAX_PARTITIONS + 1] = {
		{ 0, 255, 255 }, // 0  = Yellow
		{ 255, 0, 0 },   // 1  = Blue
		{ 0, 255, 0 },   // 2  = Green
		{ 0, 0, 255 },   // 3  = Red
		{ 255, 0, 255 }, // 4  = Purple
		{ 0, 196, 255 }, // 5  = Orange
		// These standard shades can fill out the rest.
		COLOR_BLUE,
		COLOR_GREEN,
		COLOR_CYAN,
		COLOR_RED,
		COLOR_MAGENTA,
		COLOR_BROWN,
		COLOR_LIGHTBLUE,
		COLOR_LIGHTGREEN,
		COLOR_LIGHTCYAN,
		COLOR_LIGHTRED,
		// This one is for extended partitions
		{ 255, 196, 178 }
	};
	color *extendedColor = NULL;
	int count1, count2;

	// Clear our drawing parameters
	memset(&params, 0, sizeof(windowDrawParameters));

	// Some basic drawing values for slice rectangles
	params.operation = draw_rect;
	params.mode = draw_normal;
	params.xCoord1 = 0;
	params.yCoord1 = 0;
	params.width = canvasWidth;
	params.height = canvasHeight;
	params.thickness = 1;
	params.fill = 1;

	// Draw a white background
	params.foreground.red = 255;
	params.foreground.green = 255;
	params.foreground.blue = 255;
	windowComponentSetData(canvas, &params, 1, 1 /* redraw */);

	// Set the pixel widths of all the slices
	for (count1 = 0; count1 < table->numSlices; count1 ++)
	{
		table->slices[count1].pixelWidth = ((sectorsToMegabytes(table->disk,
				table->slices[count1].raw.numSectors, 1 /* round up */) *
			canvasWidth) / sectorsToMegabytes(table->disk,
				table->disk->numSectors, 1 /* round up */));
	}

	// Now, we want to make sure each slice has a width of at least MIN_WIDTH,
	// so that it is visible.  If we need to, we steal the pixels from any
	// adjacent slices.
	#define MIN_WIDTH 15
	for (count1 = 0; count1 < table->numSlices; count1 ++)
	{
		if (table->slices[count1].pixelWidth < MIN_WIDTH)
		{
			needPixels = (MIN_WIDTH - table->slices[count1].pixelWidth);

			while (needPixels)
			{
				for (count2 = 0; count2 < table->numSlices; count2 ++)
				{
					if ((count2 != count1) &&
						(table->slices[count2].pixelWidth > MIN_WIDTH))
					{
						table->slices[count1].pixelWidth += 1;
						table->slices[count2].pixelWidth -= 1;
						needPixels -= 1;
						if (!needPixels)
							break;
					}
				}
			}
		}
	}

	for (count1 = 0; count1 < table->numSlices; count1 ++)
	{
		table->slices[count1].pixelX = xCoord;

		params.mode = draw_normal;
		params.xCoord1 = table->slices[count1].pixelX;
		params.yCoord1 = 0;
		params.width = table->slices[count1].pixelWidth;
		params.height = canvasHeight;
		params.fill = 1;

		if (isSliceUsed(table, count1))
		{
			if (ISLOGICAL(&table->slices[count1]))
				extendedColor = &colors[DISK_MAX_PARTITIONS];
			else
				extendedColor = NULL;
		}

		if (extendedColor)
		{
			if (isSliceUsed(table, count1) ||
				((count1 < (table->numSlices - 1)) &&
				ISLOGICAL(&table->slices[count1 + 1])))
			{
				memcpy(&params.foreground, extendedColor, sizeof(color));
				windowComponentSetData(canvas, &params, 1, 1 /* redraw */);
			}
		}

		// If it is a used slice, we draw a filled rectangle on the canvas to
		// represent the slice.
		if (isSliceUsed(table, count1))
		{
			table->slices[count1].color =
				&colors[table->slices[count1].raw.order];
			memcpy(&params.foreground, table->slices[count1].color,
				 sizeof(color));
			if (ISLOGICAL(&table->slices[count1]))
			{
				params.xCoord1 += 3;
				params.yCoord1 += 3;
				params.width -= 6;
				params.height -= 6;
			}

			windowComponentSetData(canvas, &params, 1, 1 /* redraw */);
		}

		// If it is a reserved slice, we shade it gray
		else if (isSliceReserved(table, count1))
		{
			table->slices[count1].color = &COLOR_LIGHTGRAY;
			memcpy(&params.foreground, table->slices[count1].color,
				 sizeof(color));

			windowComponentSetData(canvas, &params, 1, 1 /* redraw */);
		}

		// If this is the selected slice, draw a border inside it
		if (count1 == table->selectedSlice)
		{
			params.mode = draw_xor;
			params.foreground.red = 200;
			params.foreground.green = 200;
			params.foreground.blue = 200;
			params.xCoord1 += 2;
			params.yCoord1 += 2;
			params.width -= 4;
			params.height -= 4;
			params.fill = 0;
			windowComponentSetData(canvas, &params, 1, 1 /* redraw */);
		}

		xCoord += table->slices[count1].pixelWidth;
	}
}


static int haveUsedSlices(void)
{
	int count;

	for (count = 0; count < table->numSlices; count ++)
	{
		if (isSliceUsed(table, count))
			return (1);
	}

	return (0);
}


static void printBanner(void)
{
	textScreenClear();
	printf(_("%s\nCopyright (C) 1998-2018 J. Andrew McLaughlin\n"),
		programName);
}


static void display(void)
{
	listItemParameters *sliceListParams = NULL;
	char lineString[SLICESTRING_LENGTH + 2];
	int slc = 0;
	int canDefrag = 0;
	int canHide = 0;
	int canActivate = 0;
	int canCreate = 0;
	textAttrs attrs;
	int count;

	if (graphics)
	{
		// Re-populate our slice list component
		sliceListParams = malloc(table->numSlices *
			sizeof(listItemParameters));
		if (!sliceListParams)
			return;
		for (count = 0; count < table->numSlices; count ++)
			strncpy(sliceListParams[count].text, table->slices[count].string,
				WINDOW_MAX_LABEL_LENGTH);
		windowComponentSetSelected(sliceList, 0);
		windowComponentSetData(sliceList, sliceListParams, table->numSlices,
			1 /* redraw */);
		free(sliceListParams);
		windowComponentSetSelected(sliceList, table->selectedSlice);

		drawDiagram();

		// Depending on which slice type is selected (i.e. partition vs. empty
		// space) we enable/disable button choices
		if (isSliceUsed(table, table->selectedSlice))
		{
			// It's a partition

			if (table->slices[table->selectedSlice].opFlags & FS_OP_DEFRAG)
				canDefrag = 1;

			if (table->label->canHide)
				canHide = table->label->
					canHide(&table->slices[table->selectedSlice]);

			if (table->label->flags & LABELFLAG_USEACTIVE)
				canActivate = 1;

			windowComponentSetEnabled(partMenuContents.items[PARTMENU_COPY]
				.key, 1);
			windowComponentSetEnabled(partMenuContents.items[PARTMENU_PASTE]
				.key, 0);
			windowComponentSetEnabled(setActiveButton, canActivate);
			windowComponentSetEnabled(partMenuContents
				.items[PARTMENU_SETACTIVE].key, canActivate);
			windowComponentSetEnabled(deleteButton, 1);
			windowComponentSetEnabled(partMenuContents.items[PARTMENU_DELETE]
				.key, 1);
			windowComponentSetEnabled(formatButton, 1);
			windowComponentSetEnabled(partMenuContents.items[PARTMENU_FORMAT]
				.key, 1);
			windowComponentSetEnabled(defragButton, canDefrag);
			windowComponentSetEnabled(partMenuContents.items[PARTMENU_DEFRAG]
				.key, canDefrag);
			windowComponentSetEnabled(resizeButton, 1);
			windowComponentSetEnabled(partMenuContents.items[PARTMENU_RESIZE]
				.key, 1);
			windowComponentSetEnabled(hideButton, canHide);
			windowComponentSetEnabled(partMenuContents.items[PARTMENU_HIDE]
				.key, canHide);
			windowComponentSetEnabled(moveButton, 1);
			windowComponentSetEnabled(partMenuContents.items[PARTMENU_MOVE]
				.key, 1);
			windowComponentSetEnabled(createButton, 0);
			windowComponentSetEnabled(partMenuContents.items[PARTMENU_CREATE]
				.key, 0);
			windowComponentSetEnabled(partMenuContents.items[PARTMENU_SETTYPE]
				.key, 1);
		}
		else
		{
			// It's empty or reserved space

			if (!isSliceReserved(table, table->selectedSlice) &&
				(table->label->canCreateSlice(table->slices, table->numSlices,
					table->selectedSlice) != partition_none))
			{
				canCreate = 1;
			}

			windowComponentSetEnabled(partMenuContents.items[PARTMENU_COPY]
				.key, 0);
			windowComponentSetEnabled(partMenuContents.items[PARTMENU_PASTE]
				.key, (canCreate && clipboardSliceValid));
			windowComponentSetEnabled(setActiveButton, 0);
			windowComponentSetEnabled(partMenuContents
				.items[PARTMENU_SETACTIVE].key, 0);
			windowComponentSetEnabled(deleteButton, 0);
			windowComponentSetEnabled(partMenuContents.items[PARTMENU_DELETE]
				.key, 0);
			windowComponentSetEnabled(formatButton, 0);
			windowComponentSetEnabled(partMenuContents.items[PARTMENU_FORMAT]
				.key, 0);
			windowComponentSetEnabled(defragButton, 0);
			windowComponentSetEnabled(partMenuContents.items[PARTMENU_DEFRAG]
				.key, 0);
			windowComponentSetEnabled(partMenuContents.items[PARTMENU_RESIZE]
				.key, 0);
			windowComponentSetEnabled(hideButton, 0);
			windowComponentSetEnabled(partMenuContents.items[PARTMENU_HIDE]
				.key, 0);
			windowComponentSetEnabled(moveButton, 0);
			windowComponentSetEnabled(partMenuContents.items[PARTMENU_MOVE]
				.key, 0);
			windowComponentSetEnabled(createButton, canCreate);
			windowComponentSetEnabled(partMenuContents.items[PARTMENU_CREATE]
				.key, canCreate);
			windowComponentSetEnabled(resizeButton, 0);
			windowComponentSetEnabled(partMenuContents.items[PARTMENU_SETTYPE]
				.key, 0);
		}

		// Other buttons and menu items enabled/disabled...
		windowComponentSetEnabled(deleteAllButton, haveUsedSlices());
		windowComponentSetEnabled(partMenuContents.items[PARTMENU_DELETEALL]
			.key, haveUsedSlices());
		windowComponentSetEnabled(fileMenuContents
			.items[FILEMENU_RESTOREBACKUP].key, table->backupAvailable);
		windowComponentSetEnabled(undoButton, table->changesPending);
		windowComponentSetEnabled(fileMenuContents.items[FILEMENU_UNDO].key,
			table->changesPending);
		windowComponentSetEnabled(writeButton, table->changesPending);
		windowComponentSetEnabled(fileMenuContents.items[FILEMENU_WRITE].key,
			table->changesPending);
		windowComponentSetEnabled(diskMenuContents.items[DISKMENU_PARTORDER]
			.key, haveUsedSlices());
	}
	else
	{
		printBanner();
		memset(&attrs, 0, sizeof(textAttrs));
		memset(lineString, 0, (SLICESTRING_LENGTH + 2));
		for (count = 0; count <= SLICESTRING_LENGTH; count ++)
			lineString[count] = 196;

		printf("\n%s\n\n  %s\n %s\n", diskListParams[table->diskNumber].text,
			sliceListHeader, lineString);

		// Print info about the slices
		for (slc = 0; slc < table->numSlices; slc ++)
		{
			printf(" ");

			if (slc == table->selectedSlice)
				attrs.flags = TEXT_ATTRS_REVERSE;
			else
				attrs.flags = 0;

			textPrintAttrs(&attrs, " ");
			textPrintAttrs(&attrs, table->slices[slc].string);
			for (count = strlen(table->slices[slc].string);
				count < SLICESTRING_LENGTH; count ++)
			textPrintAttrs(&attrs, " ");

			printf("\n");
		}

		printf(" %s\n", lineString);
	}
}


static void setActive(int sliceNumber)
{
	// Toggle the 'bootable' flag of the supplied slice number, and if
	// necessary clear the flag of any existing bootable slice.

	int count;

	// Loop through the slices
	for (count = 0; count < table->numSlices; count ++)
	{
		if (isSliceUsed(table, count))
		{
			if (count == sliceNumber)
			{
				if (table->slices[count].raw.flags & SLICEFLAG_BOOTABLE)
					// Clear the bootable flag
					table->slices[count].raw.flags &= ~SLICEFLAG_BOOTABLE;
				else
					// Set the bootable flag
					table->slices[count].raw.flags |= SLICEFLAG_BOOTABLE;
			}
			else
			{
				table->slices[count].raw.flags &= ~SLICEFLAG_BOOTABLE;
			}
		}
	}

	table->changesPending += 1;

	// Update the slice list
	updateSliceList(table);
}


static int typeListDialog(listItemParameters *typeListParams, int numberTypes,
	int select)
{
	int selection = 0;
	objectKey typesDialog = NULL;
	objectKey typesList = NULL;
	objectKey selectButton = NULL;
	objectKey cancelButton = NULL;
	componentParameters params;
	windowEvent event;

	// Create a new window, not a modal dialog
	typesDialog = windowNewDialog(window, PARTTYPES);
	if (!typesDialog)
	{
		error("%s", _("Can't create dialog window"));
		return (selection = ERR_NOCREATE);
	}

	memset(&params, 0, sizeof(componentParameters));
	params.gridWidth = 2;
	params.gridHeight = 1;
	params.padTop = 5;
	if (!select)
		params.padBottom = 5;
	params.padLeft = 5;
	params.padRight = 5;
	params.orientationX = orient_center;
	params.orientationY = orient_middle;

	// Make a list for our info
	typesList = windowNewList(typesDialog, windowlist_textonly, 10, 2, 0,
		typeListParams, numberTypes, &params);
	windowComponentFocus(typesList);

	if (select)
	{
		params.gridY += 1;
		params.gridWidth = 1;
		params.padBottom = 5;
		params.flags |=
			(WINDOW_COMPFLAG_FIXEDWIDTH | WINDOW_COMPFLAG_FIXEDHEIGHT);
		params.orientationX = orient_right;
		selectButton = windowNewButton(typesDialog, _("Select"), NULL,
			&params);

		params.gridX += 1;
		params.orientationX = orient_left;
		cancelButton = windowNewButton(typesDialog, _("Cancel"), NULL,
			&params);
	}

	windowCenterDialog(window, typesDialog);
	windowSetVisible(typesDialog, 1);

	while (1)
	{
		// Check for window close
		if ((windowComponentEventGet(typesDialog, &event) > 0) &&
			(event.type & EVENT_WINDOW_CLOSE))
		{
			break;
		}

		if (select)
		{
			// Check for selection
			if ((windowComponentEventGet(selectButton, &event) > 0) &&
				(event.type & EVENT_MOUSE_LEFTUP))
			{
				windowComponentGetSelected(typesList, &selection);
				break;
			}

			// Check for cancel
			if ((windowComponentEventGet(cancelButton, &event) > 0) &&
				(event.type & EVENT_MOUSE_LEFTUP))
			{
				selection = ERR_CANCELLED;
				break;
			}
		}

		multitaskerYield();
	}

	windowDestroy(typesDialog);

	return (selection);
}


static void listTypes(void)
{
	int numberTypes = 0;
	listItemParameters *typeListParams = NULL;
	int count;

	if (!table->label->getTypes ||
		((numberTypes = table->label->getTypes(&typeListParams)) < 0))
	{
		return;
	}

	if (graphics)
		typeListDialog(typeListParams, numberTypes, 0);

	else
	{
		printf("\n%s:\n", PARTTYPES);

		for (count = 0; count <= (numberTypes / 2); count ++)
		{
			printf("  %s", typeListParams[count].text);
			textSetColumn(30);
			if ((count + (numberTypes / 2)) < numberTypes)
				printf("  %s\n",
					typeListParams[count + (numberTypes / 2)].text);
		}

		pause();
	}

	if (typeListParams)
		free(typeListParams);

	return;
}


static int setType(int sliceNumber)
{
	int status = 0;
	int numberTypes = 0;
	listItemParameters *typeListParams = NULL;
	char **strings = NULL;
	int newTypeNum = 0;
	msdosPartType *types = NULL;
	int count;

	if (!table->label->getTypes || !table->label->setType)
	{
		status = ERR_NOTIMPLEMENTED;
		goto out;
	}

	if ((numberTypes = table->label->getTypes(&typeListParams)) < 0)
	{
		status = numberTypes;
		goto out;
	}

	if (graphics)
	{
		newTypeNum = typeListDialog(typeListParams, numberTypes, 1);
	}
	else
	{
		strings = malloc(numberTypes * sizeof(char *));
		if (!strings)
		{
			status = ERR_MEMORY;
			goto out;
		}

		for (count = 0; count < numberTypes; count ++)
			strings[count] = typeListParams[count].text;

		newTypeNum = vshCursorMenu(PARTTYPES, strings, numberTypes,
			45 /* max rows */, 0 /* selected */);

		free(strings);
	}

	free(typeListParams);

	if (newTypeNum < 0)
	{
		status = newTypeNum;
		goto out;
	}

	status = table->label->setType(&table->slices[sliceNumber], newTypeNum);
	if (status < 0)
		goto out;

	table->changesPending += 1;

	// Update the slice list
	updateSliceList(table);

	status = 0;

out:
	if (types)
		memoryRelease(types);

	return (status);
}


static int mountedCheckSlice(slice *entry)
{
	// If the slice is mounted, query whether to ignore, unmount, or cancel

	int status = 0;
	int choice = 0;
	char tmpChar[160];
	disk tmpDisk;
	char character;

	if (entry->diskName[0] && (diskGet(entry->diskName, &tmpDisk) >= 0))
	{
		if (!tmpDisk.mounted)
			// Not mounted
			return (status = 0);

		// Mounted.  Prompt.
		sprintf(tmpChar, _("The partition %s is mounted as %s.  It is "
			"STRONGLY\nrecommended that you unmount before continuing"),
			entry->diskName, tmpDisk.mountPoint);

		if (graphics)
		{
			choice = windowNewChoiceDialog(window, _("Partition Is Mounted"),
				tmpChar, (char *[]){ _("Ignore"), _("Unmount"),
				_("Cancel") }, 3, 1);
		}
		else
		{
			printf(_("\n%s (I)gnore/(U)nmount/(C)ancel?: "), tmpChar);
			textInputSetEcho(0);

			while (1)
			{
				character = getchar();

				if ((character == 'i') || (character == 'I'))
				{
					printf("%s", _("Ignore\n"));
					choice = 0;
					break;
				}
				else if ((character == 'u') || (character == 'U'))
				{
					printf("%s", _("Unmount\n"));
					choice = 1;
					break;
				}
				else if ((character == 'c') || (character == 'C'))
				{
					printf("%s", _("Cancel\n"));
					choice = 2;
					break;
				}
			}

			textInputSetEcho(1);
		}

		if (!choice)
			// Ignore
			return (status = 0);

		if ((choice < 0) || (choice == 2))
			// Cancelled
			return (status = ERR_CANCELLED);

		if (choice == 1)
		{
			// Try to unmount the filesystem
			status = filesystemUnmount(tmpDisk.mountPoint);
			if (status < 0)
				error(_("Unable to unmount %s"), tmpDisk.mountPoint);
			return (status);
		}
	}

	// The disk probably doesn't exist (yet).  So, it obviously can't be
	// mounted
	return (status = 0);
}


static int mountedCheckDisk(void)
{
	// Check for mounted filesystems anywhere on the disk

	int status = 0;
	int count;

	for (count = 0; count < table->numSlices; count ++)
	{
		if (isSliceUsed(table, count))
		{
			status = mountedCheckSlice(&table->slices[count]);
			if (status < 0)
				return (status);
		}
	}

	return (0);
}


static void doDelete(int sliceNumber)
{
	int order = table->slices[sliceNumber].raw.order;
	int count;

	removeSliceAt(table, sliceNumber);

	// Reduce the order numbers of all slices that occur after the deleted
	// slice.
	for (count = 0; count < table->numSlices; count ++)
	{
		if (isSliceUsed(table, count) &&
			(table->slices[count].raw.order > order))
		{
			table->slices[count].raw.order -= 1;
		}
	}

	// Update the slice list
	updateSliceList(table);
}


static void delete(int sliceNumber)
{
	if (mountedCheckSlice(&table->slices[sliceNumber]) < 0)
		return;

	if (table->slices[sliceNumber].raw.flags & SLICEFLAG_BOOTABLE)
		warning("%s", _("Deleting active partition.  You should set another "
			"partition active."));

	doDelete(sliceNumber);

	if (table->selectedSlice >= table->numSlices)
		table->selectedSlice = (table->numSlices - 1);

	table->changesPending += 1;
}


static sliceType queryPrimaryLogical(objectKey primLogRadio)
{
	sliceType retType = partition_none;
	int response = -1;

	if (graphics)
	{
		if (windowComponentGetSelected(primLogRadio, &response) < 0)
			return (retType = partition_none);
	}
	else
	{
		response = vshCursorMenu(_("Choose the partition type:"),
			(char *[]){ _("primary"), _("logical") }, 2, 0 /* no max rows */,
			0 /* selected */);
		if (response < 0)
			return (retType = partition_none);
	}

	switch (response)
	{
		case 0:
		default:
			retType = partition_primary;
			break;

		case 1:
			retType = partition_logical;
			break;
	}

	return (retType);
}


static int createSliceOrder(int sliceNumber, sliceType type)
{
	// Given a slice number which currently contains empty space, determine
	// the correct table order for a new slice which will reside there (and
	// re-order others, if appropriate).

	int order = 0;
	int count1, count2;

	// Determine the partition table order of this new slice.  First, just
	// find the first empty primary slot, which will be the order number if
	// the new slice is primary, and will also be the order number if the new
	// slice is logical but there are no existing logical slices already.
	for (count1 = 0; count1 < DISK_MAX_PARTITIONS; count1 ++)
	{
		for (count2 = 0; count2 < table->numSlices; count2 ++)
		{
			if (isSliceUsed(table, count2) &&
				!ISLOGICAL(&table->slices[count2]) &&
				(table->slices[count2].raw.order == order))
			{
				order += 1;
				break;
			}
		}
	}

	if (type == partition_primary)
	{
		// Any logical slices will have their order numbers increased.
		for (count1 = 0; count1 < table->numSlices; count1 ++)
		{
			if (ISLOGICAL(&table->slices[count1]))
				table->slices[count1].raw.order += 1;
		}
	}

	else if (type == partition_logical)
	{
		// Logical slices' order in the table should always correspond with
		// their on-disk order, so if any previous slice is logical, the new
		// slice will follow it in the order.
		if (sliceNumber && ISLOGICAL(&table->slices[sliceNumber - 1]))
			order = (table->slices[sliceNumber - 1].raw.order + 1);

		// Otherwise if any following slice is logical, the new slice will
		// precede it in the order.
		else if ((sliceNumber < (table->numSlices - 1)) &&
			ISLOGICAL(&table->slices[sliceNumber + 1]))
		{
			order = table->slices[sliceNumber + 1].raw.order;
		}

		// Any logical slices that follow this one will have their order
		// numbers increased.
		for (count1 = (sliceNumber + 1); count1 < table->numSlices; count1 ++)
		{
			if (ISLOGICAL(&table->slices[count1]))
				table->slices[count1].raw.order += 1;
		}
	}

	return (order);
}


static int doCreate(int sliceNumber, sliceType type, uquad_t startSector,
	uquad_t numSectors)
{
	// Does the non-interactive work of creating a partition

	slice newSlice;
	int count;

	memset(&newSlice, 0, sizeof(slice));

	newSlice.raw.order = createSliceOrder(sliceNumber, type);
	newSlice.raw.type = type;
	if (table->label->flags & LABELFLAG_USETAGS)
		newSlice.raw.tag = DEFAULT_TAG;
	if (table->label->flags & LABELFLAG_USEGUIDS)
		memcpy(&newSlice.raw.typeGuid, &DEFAULT_GUID, sizeof(guid));

	// Logical values
	newSlice.raw.startSector = startSector;
	newSlice.raw.numSectors = numSectors;

	// Copy it into the table
	memcpy(&table->slices[sliceNumber], &newSlice, sizeof(slice));

	// Update the slice list
	updateSliceList(table);

	// Find our new slice in the list
	sliceNumber = ERR_NOSUCHENTRY;
	for (count = 0; count < table->numSlices; count ++)
	{
		if (table->slices[count].raw.startSector == startSector)
		{
			sliceNumber = count;
			break;
		}
	}

	return (sliceNumber);
}


static void create(int sliceNumber)
{
	// This is the interactive partition creation function.

	int status = 0;
	unsigned minStartMb = 0;
	unsigned startMb = 0;
	unsigned maxSizeMb = 0;
	unsigned sizeMb = 0;
	sliceType type = partition_none;
	objectKey createDialog = NULL;
	objectKey primLogRadio = NULL;
	objectKey startField = NULL;
	objectKey startSlider = NULL;
	scrollBarState sliderState;
	objectKey sizeLabel = NULL;
	objectKey sizeField = NULL;
	objectKey sizeSlider = NULL;
	objectKey okButton = NULL;
	objectKey cancelButton = NULL;
	componentParameters params;
	windowEvent event;
	char startString[10];
	char sizeString[10];
	int startChanged = 0;
	uquad_t startSector = 0;
	uquad_t numSectors = 0;
	int newSliceNumber = ERR_NOSUCHENTRY;
	unsigned tmpMaxSizeMb;
	char tmpChar[160];

	startMb = minStartMb = sectorsToMegabytes(table->disk,
		table->slices[sliceNumber].raw.startSector, 1 /* round up */);
	sizeMb = maxSizeMb = sectorsToMegabytes(table->disk,
		table->slices[sliceNumber].raw.numSectors, -1 /* round down */);

	if (!maxSizeMb)
	{
		error("%s", _("Not enough space to create a partition here."));
		return;
	}

	while (1)
	{
		// See if we can create a slice here, and if so, what type?
		type = table->label->canCreateSlice(table->slices, table->numSlices,
			sliceNumber);
		if (type == partition_none)
		{
			error("%s", _("Not allowed to create any partition here."));
			return;
		}

		if (graphics)
		{
			createDialog = windowNewDialog(window, _("Create Partition"));

			memset(&params, 0, sizeof(componentParameters));
			params.gridWidth = 1;
			params.gridHeight = 1;
			params.padTop = 5;
			params.padLeft = 5;
			params.padRight = 5;
			params.orientationX = orient_right;
			params.orientationY = orient_middle;

			windowNewTextLabel(createDialog, _("Partition\ntype:"), &params);

			// A radio to select 'primary' or 'logical'
			params.gridX++;
			params.orientationX = orient_left;
			primLogRadio = windowNewRadioButton(createDialog, 2, 1,
				(char *[]){ _("Primary"), _("Logical") }, 2 , &params);
			if (type != partition_any)
			{
				if (type == partition_logical)
					windowComponentSetSelected(primLogRadio, 1);
				windowComponentSetEnabled(primLogRadio, 0);
			}

			// A label and field for the starting megabyte
			snprintf(tmpChar, 160, STARTMB_MESSAGE, minStartMb, (minStartMb +
				(maxSizeMb - 1)));
			params.gridX = 0;
			params.gridY++;
			params.gridWidth = 2;
			windowNewTextLabel(createDialog, tmpChar, &params);

			params.gridY++;
			startField = windowNewTextField(createDialog, 10, &params);
			sprintf(tmpChar, "%u", minStartMb);
			windowComponentSetData(startField, tmpChar, strlen(tmpChar),
				1 /* redraw */);

			// A slider to adjust the start value mouse-ly
			params.gridY++;
			startSlider = windowNewSlider(createDialog,
				scrollbar_horizontal, 0, 0, &params);
			sliderState.displayPercent = 20; // Size of slider 20%
			sliderState.positionPercent = 0; // minStartMb
			windowComponentSetData(startSlider, &sliderState,
				sizeof(scrollBarState), 1 /* redraw */);

			// A label and field for the size
			snprintf(tmpChar, 160, SIZEMB_MESSAGE, 1, maxSizeMb);
			params.gridY++;
			sizeLabel = windowNewTextLabel(createDialog, tmpChar, &params);

			params.gridY++;
			sizeField = windowNewTextField(createDialog, 10, &params);
			sprintf(tmpChar, "%u", maxSizeMb);
			windowComponentSetData(sizeField, tmpChar, strlen(tmpChar),
				1 /* redraw */);

			// A slider to adjust the size value mouse-ly
			params.gridY++;
			sizeSlider = windowNewSlider(createDialog, scrollbar_horizontal,
				0, 0, &params);
			sliderState.displayPercent = 20; // Size of slider 20%
			sliderState.positionPercent = 100; // maxSizeMb
			windowComponentSetData(sizeSlider, &sliderState,
				sizeof(scrollBarState), 1 /* redraw */);

			// Make 'OK' and 'cancel' buttons
			params.gridY++;
			params.gridWidth = 1;
			params.padBottom = 5;
			params.orientationX = orient_right;
			params.flags |= WINDOW_COMPFLAG_FIXEDWIDTH;
			okButton = windowNewButton(createDialog, _("OK"), NULL, &params);

			params.gridX++;
			params.orientationX = orient_left;
			cancelButton = windowNewButton(createDialog, _("Cancel"), NULL,
				&params);
			windowComponentFocus(cancelButton);

			// Make the window visible
			windowSetResizable(createDialog, 0);
			windowCenterDialog(window, createDialog);
			windowSetVisible(createDialog, 1);

			tmpMaxSizeMb = maxSizeMb;

			while (1)
			{
				// Check for start text field changes
				if ((windowComponentEventGet(startField, &event) > 0) &&
					(event.type == EVENT_KEY_DOWN))
				{
					if (event.key == keyEnter)
						// User hit enter.
						break;

					// See if we can apply a newly-typed number to the slider
					startString[0] = '\0';
					windowComponentGetData(startField, startString, 10);
					startMb = atoi(startString);
					if ((startMb >= minStartMb) &&
						(startMb < (minStartMb + maxSizeMb)))
					{
						sliderState.positionPercent =
							(((startMb - minStartMb) * 100) / maxSizeMb);
						windowComponentSetData(startSlider, &sliderState,
							sizeof(scrollBarState), 1 /* redraw */);
						startChanged = 1;
					}
				}

				// Check for start slider changes
				if (windowComponentEventGet(startSlider, &event) > 0)
				{
					if (event.type & (EVENT_MOUSE_LEFTDOWN | EVENT_MOUSE_DRAG |
						EVENT_KEY_DOWN))
					{
						windowComponentGetData(startSlider, &sliderState,
							sizeof(scrollBarState));
						startMb = min((minStartMb + (maxSizeMb - 1)),
							(minStartMb + ((sliderState.positionPercent *
								maxSizeMb) / 100)));
						sprintf(tmpChar, "%u", startMb);
						windowComponentSetData(startField, tmpChar,
							strlen(tmpChar), 1 /* redraw */);
						startChanged = 1;
					}
				}

				if (startChanged)
				{
					tmpMaxSizeMb = max(1, (maxSizeMb - (startMb -
						minStartMb)));

					if (sizeMb > tmpMaxSizeMb)
					{
						sizeMb = tmpMaxSizeMb;
						sprintf(tmpChar, "%u", sizeMb);
						windowComponentSetData(sizeField, tmpChar,
							strlen(tmpChar), 1 /* redraw */);
					}

					snprintf(tmpChar, 160, SIZEMB_MESSAGE, 1, tmpMaxSizeMb);
					windowComponentSetData(sizeLabel, tmpChar, strlen(tmpChar),
						1 /* redraw */);

					windowComponentGetData(sizeSlider, &sliderState,
						sizeof(scrollBarState));
					sliderState.positionPercent = ((sizeMb * 100) /
						tmpMaxSizeMb);
					windowComponentSetData(sizeSlider, &sliderState,
						sizeof(scrollBarState), 1 /* redraw */);

					startChanged = 0;
				}

				// Check for size text field changes
				if ((windowComponentEventGet(sizeField, &event) > 0) &&
					(event.type == EVENT_KEY_DOWN))
				{
					if (event.key == keyEnter)
						// User hit enter.
						break;

					// See if we can apply a newly-typed number to the slider
					sizeString[0] = '\0';
					windowComponentGetData(sizeField, sizeString, 10);
					sizeMb = atoi(sizeString);
					if (sizeMb <= tmpMaxSizeMb)
					{
						sliderState.positionPercent = ((sizeMb * 100) /
							tmpMaxSizeMb);
						windowComponentSetData(sizeSlider, &sliderState,
							sizeof(scrollBarState), 1 /* redraw */);
					}
				}

				// Check for size slider changes
				if (windowComponentEventGet(sizeSlider, &event) > 0)
				{
					if (event.type & (EVENT_MOUSE_LEFTDOWN | EVENT_MOUSE_DRAG |
						EVENT_KEY_DOWN))
					{
						windowComponentGetData(sizeSlider, &sliderState,
							sizeof(scrollBarState));
						sizeMb = max(1, ((tmpMaxSizeMb *
							sliderState.positionPercent) / 100));
						sprintf(tmpChar, "%u", sizeMb);
						windowComponentSetData(sizeField, tmpChar,
							strlen(tmpChar), 1 /* redraw */);
					}
				}

				// Check for the OK button
				if ((windowComponentEventGet(okButton, &event) > 0) &&
					(event.type == EVENT_MOUSE_LEFTUP))
				{
					break;
				}

				// Check for the Cancel button
				if ((windowComponentEventGet(cancelButton, &event) > 0) &&
					(event.type == EVENT_MOUSE_LEFTUP))
				{
					windowDestroy(createDialog);
					return;
				}

				// Check for window close events
				if ((windowComponentEventGet(createDialog, &event) > 0) &&
					(event.type == EVENT_WINDOW_CLOSE))
				{
					windowDestroy(createDialog);
					return;
				}

				// Done
				multitaskerYield();
			}

			type = queryPrimaryLogical(primLogRadio);
			if (type == partition_none)
				return;

			windowComponentGetData(startField, startString, 10);
			windowComponentGetData(sizeField, sizeString, 10);
			windowDestroy(createDialog);
		}

		else
		{
			if (type == partition_any)
			{
				// Does the user prefer primary or logical?
				type = queryPrimaryLogical(NULL);
				if (type == partition_none)
					return;
			}
			else
			{
				printf(_("\nCreating %s partition\n"),
					 ((type == partition_primary)? _("primary") :
						_("logical")));
			}

			printf("\n");
			printf(STARTMB_MESSAGE, minStartMb, (minStartMb +
				(maxSizeMb - 1)));
			printf("%s", _(", or 'Q' to quit:\n-> "));

			status = readLine("0123456789Qq", startString, 10);
			if (status < 0)
				continue;

			if ((startString[0] == 'Q') || (startString[0] == 'q'))
				return;

			// Adjust the max size based on the start
			startMb = atoi(startString);
			maxSizeMb -= (startMb - minStartMb);

			printf("\n");
			printf(SIZEMB_MESSAGE, 1, maxSizeMb);
			printf("%s", _(", or 'Q' to quit:\n-> "));

			status = readLine("0123456789CcMmQq", sizeString, 10);
			if (status < 0)
				return;

			if ((sizeString[0] == 'Q') || (sizeString[0] == 'q'))
				return;
		}

		startMb = atoi(startString);

		// Make sure the start is legit
		if ((startMb < minStartMb) || (startMb >= (minStartMb + maxSizeMb)))
		{
			error("%s", _("Invalid starting location"));
			continue;
		}

		sizeMb = atoi(sizeString);

		// Make sure the size is legit
		if ((sizeMb < 1) || (sizeMb > maxSizeMb))
		{
			error("%s", _("Invalid size"));
			continue;
		}

		break;
	}

	// Calculate the start and size as sector values

	if ((startMb == minStartMb) && ((table->label->type != label_msdos) ||
		(startMb > 1)))
	{
		startSector = table->slices[sliceNumber].raw.startSector;
	}
	else
	{
		startSector = megabytesToSectors(table->disk, startMb);
	}

	if (sizeMb >= maxSizeMb)
	{
		numSectors = (table->slices[sliceNumber].raw.numSectors -
			(startSector - table->slices[sliceNumber].raw.startSector));
	}
	else
	{
		numSectors = megabytesToSectors(table->disk, sizeMb);
	}

	// By convention, we don't write the first 'track' of the disk, or the
	// first sector of a logical partition (it's reserved area for needed
	// for any extended partition table)
	if (startSector < table->disk->sectorsPerCylinder)
	{
		numSectors -= (table->disk->sectorsPerCylinder - startSector);
		startSector = table->disk->sectorsPerCylinder;
	}
	if (type == partition_logical)
	{
		numSectors -= 1;
		startSector += 1;
	}

	// Do the slice creation
	newSliceNumber = doCreate(sliceNumber, type, startSector, numSectors);
	if (newSliceNumber < 0)
		// Failed
		return;

	// Set the type
	if (setType(newSliceNumber) < 0)
		// Cancelled.  Remove it again.
		doDelete(newSliceNumber);
	else
		// The setType() will increase the 'changes pending' if it succeeded,
		// so we don't do it here.
		table->selectedSlice = newSliceNumber;

	return;
}


static void format(int sliceNumber)
{
	// Prompt, and format a slice

	int status = 0;
	slice *formatSlice = &table->slices[sliceNumber];
	int numTypes = 0;
	char *fsTypes[16] = { NULL };
	char *fatTypes[] = { _("Default"), "FAT12", "FAT16", "FAT32" };
	const char *chooseString = _("Choose the filesystem type:");
	const char *fatString = _("Choose the FAT type:");
	int typeNum = 0;
	char typeName[16];
	char tmpChar[160];

	if (table->changesPending)
	{
		error("%s", _("A partition format cannot be undone, and it is "
			"required that\nyou write your other changes to disk before "
			"continuing."));
		return;
	}

	status = mountedCheckSlice(formatSlice);
	if (status < 0)
		return;

	// Populate the list of available types
	if (ntfsFormat)
		fsTypes[numTypes++] = "NTFS";
	fsTypes[numTypes++] = "FAT";
	fsTypes[numTypes++] = "EXT2";
	fsTypes[numTypes++] = "Linux-swap";
	fsTypes[numTypes++] = _("None");

	if (graphics)
	{
		sprintf(tmpChar, _("Format Partition %s"), formatSlice->showSliceName);
		typeNum = windowNewRadioDialog(window, tmpChar, chooseString, fsTypes,
			numTypes, 0);
	}
	else
	{
		typeNum = vshCursorMenu(chooseString, fsTypes, numTypes,
			0 /* no max rows */, 0 /* selected */);
	}

	if (typeNum < 0)
		return;

	strncpy(typeName, fsTypes[typeNum], 16);

	// If the filesystem type is FAT, additionally offer to let the user
	// choose the subtype
	if (!strncasecmp(typeName, "fat", 3))
	{
		if (graphics)
		{
			typeNum = windowNewRadioDialog(window, _("FAT Type"), fatString,
				fatTypes, 4, 0);
		}
		else
		{
			typeNum = vshCursorMenu(fatString, fatTypes, 4,
				0 /* no max rows */, 0 /* selected */);
		}

		if (typeNum < 0)
			return;

		strncpy(typeName, fatTypes[typeNum], 16);

		if (!strcasecmp(typeName, _("default")))
			strcpy(typeName, "FAT");
	}

	if (!strcasecmp(typeName, _("none")))
	{
		sprintf(tmpChar, _("Unformat partition %s?  (This change cannot be "
			"undone)"), formatSlice->showSliceName);

		if (!yesOrNo(tmpChar))
			return;

		status = filesystemClobber(formatSlice->diskName);
	}
	else
	{
		sprintf(tmpChar, _("Format partition %s as %s?\n(This change cannot "
			"be undone)"), formatSlice->showSliceName, typeName);

		if (!yesOrNo(tmpChar))
			return;

		// Do the format
		sprintf(tmpChar, PATH_PROGRAMS "/format %s -s -t %s %s",
			(graphics? "" : "-T"), typeName, formatSlice->diskName);

		status = system(tmpChar);
	}

	if (status < 0)
	{
		error("%s", _("Error during format"));
	}
	else
	{
		sprintf(tmpChar, "%s", _("Format complete"));
		if (graphics)
		{
			windowNewInfoDialog(window, _("Success"), tmpChar);
		}
		else
		{
			printf("%s\n", tmpChar);
			pause();
		}
	}

	// Make the slice list
	makeSliceList(table);

	return;
}


static void defragment(int sliceNumber)
{
	// Prompt, and defragment a slice

	int status = 0;
	slice *defragSlice = &table->slices[sliceNumber];
	progress prog;
	objectKey progressDialog = NULL;
	char tmpChar[160];

	if (table->changesPending)
	{
		error("%s", _("A partition defragmentation cannot be undone, and it "
			"is required\nthat you write your other changes to disk "
			"before continuing."));
		return;
	}

	sprintf(tmpChar, _("Defragment partition %s?\n(This change cannot be "
		"undone)"), defragSlice->showSliceName);

	if (!yesOrNo(tmpChar))
		return;

	status = mountedCheckSlice(defragSlice);
	if (status < 0)
		return;

	sprintf(tmpChar, "%s", _("Please use this feature with caution; it is "
		"not\nwell tested.  Continue?"));
	if (graphics)
	{
		if (!windowNewQueryDialog(window, _("New Feature"), tmpChar))
			return;
	}
	else
	{
		if (!yesOrNo(tmpChar))
			return;
	}

	// Do the defrag

	memset((void *) &prog, 0, sizeof(progress));
	if (graphics)
		progressDialog =
			windowNewProgressDialog(window, _("Defragmenting..."), &prog);
	else
		vshProgressBar(&prog);

	status = filesystemDefragment(defragSlice->diskName, &prog);

	if (graphics && progressDialog)
		windowProgressDialogDestroy(progressDialog);
	else
		vshProgressBarDestroy(&prog);

	if (status < 0)
	{
		error("%s", _("Error during defragmentation"));
	}
	else
	{
		sprintf(tmpChar, "%s", _("Defragmentation complete"));
		if (graphics)
			windowNewInfoDialog(window, _("Success"), tmpChar);
		else
		{
			printf("%s\n", tmpChar);
			pause();
		}
	}

	return;
}


static void hide(int sliceNumber)
{
	if (table->label->hide)
		table->label->hide(&table->slices[sliceNumber]);

	table->changesPending += 1;

	// Update the slice list
	updateSliceList(table);
}


void getChsValues(const disk *theDisk, rawSlice *raw, rawGeom *geom)
{
	uquad_t startSector = raw->startSector;
	uquad_t endSector = (startSector + (raw->numSectors - 1));

	memset(geom, 0, sizeof(rawGeom));

	// Calculate the starting CHS values
	geom->startCylinder = (startSector / CYLSECTS(theDisk));
	geom->startHead = ((startSector % CYLSECTS(theDisk)) /
		theDisk->sectorsPerCylinder);
	geom->startSector = (((startSector % CYLSECTS(theDisk)) %
		theDisk->sectorsPerCylinder) + 1);

	// Calculate the ending CHS values
	geom->endCylinder = (endSector / CYLSECTS(theDisk));
	geom->endHead = ((endSector % CYLSECTS(theDisk)) /
		theDisk->sectorsPerCylinder);
	geom->endSector = (((endSector % CYLSECTS(theDisk)) %
		theDisk->sectorsPerCylinder) + 1);
}


static void sliceInfo(int sliceNumber)
{
	// Show info about a slice

	slice *infoSlice = &table->slices[sliceNumber];
	char *buff = NULL;
	rawGeom geom;

	buff = malloc(1024);
	if (!buff)
		return;

	if (isSliceUsed(table, sliceNumber))
	{
		sprintf(buff, _("PARTITION %s INFO:\n\n"), infoSlice->showSliceName);

		if (table->label->flags & LABELFLAG_USEACTIVE)
			sprintf((buff + strlen(buff)), _("Active : %s\n"),
				(infoSlice->raw.flags & SLICEFLAG_BOOTABLE?
					_("yes") : _("no")));

		if (table->label->flags & LABELFLAG_USETAGS)
		{
			sprintf((buff + strlen(buff)), _("Type ID : %02x\n"),
				infoSlice->raw.tag);
		}
		else if (table->label->flags & LABELFLAG_USEGUIDS)
		{
			sprintf((buff + strlen(buff)), "%s", _("Type GUID : "));
			guidString((buff + strlen(buff)), &infoSlice->raw.typeGuid);
			sprintf((buff + strlen(buff)), "\n");
		}
	}
	else if (isSliceReserved(table, sliceNumber))
	{
		sprintf(buff, "%s", _("RESERVED SPACE INFO:\n\n"));
	}
	else
	{
		sprintf(buff, "%s", _("EMPTY SPACE INFO:\n\n"));
	}

	sprintf((buff + strlen(buff)), _("Starting sector: %llu\n"),
		infoSlice->raw.startSector);
	sprintf((buff + strlen(buff)), _("Number of sectors: %llu\n"),
		infoSlice->raw.numSectors);
	getChsValues(table->disk, &table->slices[sliceNumber].raw, &geom);
	sprintf((buff + strlen(buff)), _("Starting Cyl/Hd/Sect: %u/%u/%u\n"),
		geom.startCylinder, geom.startHead, geom.startSector);
	sprintf((buff + strlen(buff)), _("Ending Cyl/Hd/Sect: %u/%u/%u"),
		geom.endCylinder, geom.endHead, geom.endSector);

	if (graphics)
	{
		windowNewInfoDialog(window, _("Info"), buff);
	}
	else
	{
		printf("\n%s\n", buff);
		pause();
	}

	free(buff);
}


static void undo(void)
{
	// Undo changes
	if (table->changesPending)
	{
		// Re-scan from the original raw slices.
		makeSliceList(table);

		table->selectedSlice = 0;
		table->changesPending = 0;
	}
}


static void writeChanges(partitionTable *t, int confirm)
{
	int status = 0;

	if (t->changesPending)
	{
		if (confirm && !yesOrNo(_("Committing changes to disk.  Are you "
			"SURE?")))
		{
			return;
		}

		// Write out the partition table
		status = writePartitionTable(t);
		if (status < 0)
			error(_("Unable to write the partition table of %s."),
				t->disk->name);

		// Tell the kernel to re-examine the partition tables
		diskReadPartitions(t->disk->name);

		// Make the slice list.
		makeSliceList(t);
	}
}


static void formatTime(char *string, unsigned seconds)
{
	strcpy(string, _("Time remaining: "));

	if (seconds >= 7200)
		sprintf((string + strlen(string)), _("%d hours "), (seconds / 3600));
	else if (seconds > 3600)
		sprintf((string + strlen(string)), "%s", _("1 hour "));

	if (seconds >= 60)
		sprintf((string + strlen(string)), _("%d minutes"),
			((seconds % 3600) / 60));
	else
		sprintf((string + strlen(string)), "%s", _("less than 1 minute"));
}


static int doMove(int sliceNumber, uquad_t newStartSector)
{
	int status = 0;
	slice *moveSlice = &table->slices[sliceNumber];
	int moveLeft = 0;
	unsigned sectorsPerOp = 0;
	uquad_t srcSector = 0;
	uquad_t destSector = 0;
	uquad_t overlapSector = 0;
	unsigned sectorsToCopy = 0;
	unsigned char *buffer = NULL;
	int startSeconds = 0;
	int remainingSeconds = 0;
	progress prog;
	objectKey progressDialog = NULL;
	char tmpChar[160];
	int count;

	// Which direction?
	if (newStartSector < moveSlice->raw.startSector)
		moveLeft = 1;

	sectorsToCopy = moveSlice->raw.numSectors;
	sectorsPerOp = CYLSECTS(table->disk);

	if (moveLeft)
	{
		// Moving left, we copy from the beginning to the end

		if ((moveSlice->raw.startSector - newStartSector) < sectorsPerOp)
			sectorsPerOp = (moveSlice->raw.startSector - newStartSector);

		// Calculate initial source and destination sectors
		srcSector = moveSlice->raw.startSector;
		destSector = newStartSector;

		// Will the new location overlap the old location?
		if ((newStartSector + moveSlice->raw.numSectors) >
			moveSlice->raw.startSector)
		{
			overlapSector = moveSlice->raw.startSector;
		}
	}
	else
	{
		// Moving right, we copy from the end to the beginning

		if ((newStartSector - moveSlice->raw.startSector) < sectorsPerOp)
			sectorsPerOp = (newStartSector - moveSlice->raw.startSector);

		// Calculate initial source and destination sectors
		srcSector = (moveSlice->raw.startSector +
			(moveSlice->raw.numSectors - sectorsPerOp));
		destSector = (newStartSector +
			(moveSlice->raw.numSectors - sectorsPerOp));

		// Will the new location overlap the old location?
		if ((moveSlice->raw.startSector + moveSlice->raw.numSectors) >
			newStartSector)
		{
			overlapSector = (moveSlice->raw.startSector +
				(moveSlice->raw.numSectors - 1));
		}
	}

	// Get a memory buffer to copy data to/from
	buffer = malloc(sectorsPerOp * table->disk->sectorSize);
	if (!buffer)
	{
		error("%s", _("Unable to allocate memory"));
		return (status = ERR_MEMORY);
	}

	memset((void *) &prog, 0, sizeof(progress));
	prog.numTotal = sectorsToCopy;
	strcpy((char *) prog.statusMessage,
		_("Time remaining: ?? hours ?? minutes"));
	if (!overlapSector || !((overlapSector >= destSector) &&
		(overlapSector < (destSector + sectorsPerOp))))
	{
		prog.canCancel = 1;
	}

	sprintf(tmpChar, _("Moving %u MB"), sectorsToMegabytes(table->disk,
		moveSlice->raw.numSectors, 1 /* round up */));

	if (graphics)
	{
		progressDialog = windowNewProgressDialog(window, tmpChar, &prog);
	}
	else
	{
		printf("\n%s\n", tmpChar);
		vshProgressBar(&prog);
	}

	startSeconds = rtcUptimeSeconds();

	// Copy the data
	while (sectorsToCopy > 0)
	{
		if (sectorsToCopy < sectorsPerOp)
			sectorsPerOp = sectorsToCopy;

		// Read from source
		status = diskReadSectors(table->disk->name, srcSector, sectorsPerOp,
			buffer);
		if (status < 0)
		{
			error(_("Read error %d reading sectors %llu-%llu from disk %s"),
				status, srcSector, (srcSector + (sectorsPerOp - 1)),
				table->disk->name);
			goto out;
		}

		if (prog.cancel)
			goto out;

		// Write to destination
		status = diskWriteSectors(table->disk->name, destSector, sectorsPerOp,
			buffer);
		if (status < 0)
		{
			error(_("Write error %d writing sectors %llu-%llu to disk %s"),
				status, destSector, (destSector + (sectorsPerOp - 1)),
				table->disk->name);
			goto out;
		}

		if (prog.cancel)
			goto out;

		sectorsToCopy -= sectorsPerOp;

		if (moveLeft)
		{
			srcSector += sectorsPerOp;
			destSector += sectorsPerOp;
		}
		else
		{
			srcSector -= min(sectorsToCopy, sectorsPerOp);
			destSector -= min(sectorsToCopy, sectorsPerOp);
		}

		if (lockGet(&prog.progLock) >= 0)
		{
			prog.numFinished += sectorsPerOp;
			if (prog.numTotal >= 100)
			{
				prog.percentFinished = (prog.numFinished /
					(prog.numTotal / 100));
			}
			else
			{
				prog.percentFinished = ((prog.numFinished * 100) /
					prog.numTotal);
			}

			remainingSeconds = (((rtcUptimeSeconds() - startSeconds) *
				(sectorsToCopy / sectorsPerOp)) /
					(prog.numFinished / sectorsPerOp));

			formatTime((char *) prog.statusMessage, remainingSeconds);

			// Will the next operation overwrite any of the original data?
			if (overlapSector && (overlapSector >= destSector) &&
				(overlapSector < (destSector + sectorsPerOp)))
			{
				prog.canCancel = 0;
			}

			lockRelease(&prog.progLock);
		}
	}

	// Set the new slice data
	moveSlice->raw.startSector = newStartSector;

	table->changesPending += 1;

	// Write everything
	writeChanges(table, 0);

	// Find our moved slice in the list
	for (count = 0; count < table->numSlices; count ++)
	{
		if (table->slices[count].raw.startSector == newStartSector)
		{
			sliceNumber = count;
			break;
		}
	}

out:
	// Release memory
	free(buffer);

	if (graphics && progressDialog)
		windowProgressDialogDestroy(progressDialog);
	else
		vshProgressBarDestroy(&prog);

	return (sliceNumber);
}


static int move(int sliceNumber)
{
	int status = 0;
	slice *moveSlice = &table->slices[sliceNumber];
	uquad_t moveRangeSectors[] = { 0, 0 };
	int canMoveLeft = 0, canMoveRight = 0;
	unsigned currentStartMb = 0;
	unsigned moveRangeMb[] = { 0, 0 };
	char number[10];
	unsigned newStartMb = 0;
	uquad_t newStartSector = 0;
	char tmpChar[160];

	if (table->changesPending)
	{
		error("%s", _("A partition move cannot be undone, and must be "
			"committed\nto disk immediately.  You need to write your "
			"other changes\nto disk before continuing."));
		return (status = ERR_BUSY);
	}

	// If there are no empty spaces on either side, error
	if ((!sliceNumber || isSliceReserved(table, (sliceNumber - 1)) ||
			isSliceUsed(table, (sliceNumber - 1))) &&
		((sliceNumber == (table->numSlices - 1)) ||
			isSliceReserved(table, (sliceNumber + 1)) ||
			isSliceUsed(table, (sliceNumber + 1))))
	{
		error("%s", _("No empty space on either side!"));
		return (status = ERR_INVALID);
	}

	status = mountedCheckSlice(moveSlice);
	if (status < 0)
		return (status);

	// Figure out the ranges of sectors we can move in both directions

	moveRangeSectors[0] = moveRangeSectors[1] =
		table->slices[sliceNumber].raw.startSector;

	// Empty space to the left?
	if (sliceNumber &&
		!isSliceReserved(table, (sliceNumber - 1)) &&
		!isSliceUsed(table, (sliceNumber - 1)))
	{
		moveRangeSectors[0] = table->slices[sliceNumber - 1].raw.startSector;
		canMoveLeft = 1;
	}

	// Empty space to the right?
	if ((sliceNumber < (table->numSlices - 1)) &&
		!isSliceReserved(table, (sliceNumber + 1)) &&
		!isSliceUsed(table, (sliceNumber + 1)))
	{
		moveRangeSectors[1] += table->slices[sliceNumber + 1].raw.numSectors;
		canMoveRight = 1;
	}

	// Figure out the ranges of megabytes we can move in both directions

	moveRangeMb[0] = moveRangeMb[1] = currentStartMb =
		sectorsToMegabytes(table->disk,
			table->slices[sliceNumber].raw.startSector,
			0 /* round naturally */);

	if (canMoveLeft)
		moveRangeMb[0] = sectorsToMegabytes(table->disk, moveRangeSectors[0],
			1 /* round up */);
	if (canMoveRight)
		moveRangeMb[1] = sectorsToMegabytes(table->disk, moveRangeSectors[1],
			-1 /* round down */);

	while (1)
	{
		sprintf(tmpChar, _("Enter starting location in megabytes:\n(%u-%u)"),
			moveRangeMb[0], moveRangeMb[1]);

		if (graphics)
		{
			status = windowNewNumberDialog(window, _("Starting location"),
				tmpChar, moveRangeMb[0], moveRangeMb[1],
				min(max(currentStartMb, moveRangeMb[0]), moveRangeMb[1]),
				(int *) &newStartMb);
			if (status < 0)
				return (status);
		}
		else
		{
			printf(_("\n%s or 'Q' to quit\n-> "), tmpChar);

			status = readLine("0123456789Qq", number, 10);
			if (status < 0)
				continue;

			if ((number[0] == 'Q') || (number[0] == 'q'))
				return (status = 0);

			newStartMb = atoi(number);
		}

		// Make sure the starting location is legit
		if ((newStartMb < moveRangeMb[0]) ||
			(newStartMb > moveRangeMb[1]))
		{
			error("%s", _("Starting location is not valid"));
			continue;
		}

		break;
	}

	newStartSector = megabytesToSectors(table->disk, newStartMb);

	// By convention, we don't write the first 'track' of the disk, or the
	// first sector of a logical partition (it's reserved area for the
	// partition table and possibly other stuff)
	if (newStartSector < table->disk->sectorsPerCylinder)
		newStartSector = table->disk->sectorsPerCylinder;
	if (ISLOGICAL(moveSlice))
		newStartSector += 1;

	// If it's not moving, quit
	if ((newStartSector < moveRangeSectors[0]) ||
		(newStartSector > moveRangeSectors[1]) ||
		(newStartSector == table->slices[sliceNumber].raw.startSector))
	{
		return (status = 0);
	}

	sprintf(tmpChar, _("Moving partition from %uMb to %uMb.\n"
		"Continue?"), currentStartMb, newStartMb);
	if (graphics)
	{
		if (!windowNewQueryDialog(window, _("Moving"), tmpChar))
			return (status = 0);
	}
	else
	{
		if (!yesOrNo(tmpChar))
			return (status = 0);
	}

	status = doMove(sliceNumber, newStartSector);
	if (status >= 0)
		table->selectedSlice = status;

	return (status);
}


static void deleteAll(void)
{
	// This function completely nukes the slice entries

	// Check for any mounted partitions
	if (mountedCheckDisk() < 0)
		return;

	memset(table->slices, 0, (MAX_SLICES * sizeof(slice)));
	table->numSlices = 0;

	table->selectedSlice = 0;
	table->changesPending += 1;

	// Update the slice list.
	updateSliceList(table);
}


static void resizeSlice(slice *rszSlice, uquad_t newSectors)
{
	// Resize the slice

	rszSlice->raw.numSectors = newSectors;

	// Update the slice list
	updateSliceList(table);

	table->changesPending += 1;
}


static int doResize(int sliceNumber, uquad_t newSectors, int resizeFs)
{
	int status = 0;
	uquad_t oldSectors = 0;
	int didResize = 0;
	progress prog;
	objectKey progressDialog = NULL;

	oldSectors = table->slices[sliceNumber].raw.numSectors;

	if (newSectors > oldSectors)
	{
		resizeSlice(&table->slices[sliceNumber], newSectors);
		didResize = 1;
	}

	// Now, if we're resizing the filesystem...
	if (resizeFs)
	{
		// Write everything
		writeChanges(table, 0);

		// If disk caching is enabled on the disk, disable it whilst we do a
		// large operation like this.
		if (!(table->disk->flags & DISKFLAG_NOCACHE))
			diskSetFlags(table->disk->name, DISKFLAG_NOCACHE, 1);

		memset((void *) &prog, 0, sizeof(progress));
		if (graphics)
		{
			progressDialog = windowNewProgressDialog(window,
				_("Resizing Filesystem..."), &prog);
		}
		else
		{
			vshProgressBar(&prog);
		}

		if (!strcmp(table->slices[sliceNumber].fsType, "ntfs"))
		{
			// NTFS resizing is done by our libntfs library
			status = ntfsResize(table->slices[sliceNumber].diskName,
				newSectors, &prog);
		}
		else
		{
			// The kernel will do the resize
			status = filesystemResize(table->slices[sliceNumber].diskName,
				newSectors, &prog);
		}

		if (graphics && progressDialog)
			windowProgressDialogDestroy(progressDialog);
		else
			vshProgressBarDestroy(&prog);

		// If applicable, re-enable disk caching.
		if (!(table->disk->flags & DISKFLAG_NOCACHE))
			diskSetFlags(table->disk->name, DISKFLAG_NOCACHE, 0);

		// Update the slice list
		updateSliceList(table);

		if (status < 0)
		{
			if (didResize)
			{
				// Undo the slice resize
				resizeSlice(&table->slices[sliceNumber], oldSectors);
				writeChanges(table, 0);
			}

			if (status == ERR_CANCELLED)
				error("%s", _("Filesystem resize cancelled"));
			else
				error("%s", _("Error during filesystem resize"));

			return (status);
		}
	}

	if (!didResize)
	{
		resizeSlice(&table->slices[sliceNumber], newSectors);

		if (resizeFs)
			// We already resized the filesystem, so write everything
			writeChanges(table, 0);
	}

	return (status = 0);
}


static int resize(int sliceNumber)
{
	int status = 0;
	int resizeFs = 0;
	uquad_t minFsSectors = 0;
	uquad_t maxFsSectors = 0;
	int haveResizeConstraints = 0;
	unsigned currentSizeMb = 0;
	unsigned minSizeMb = 0;
	unsigned maxSizeMb = 0;
	objectKey resizeDialog = NULL;
	objectKey partCanvas = NULL;
	objectKey sizeField = NULL;
	objectKey sizeSlider = NULL;
	scrollBarState sliderState;
	objectKey okButton = NULL;
	objectKey cancelButton = NULL;
	char newSizeString[10];
	unsigned newSizeMb = 0;
	componentParameters params;
	windowDrawParameters drawParams;
	windowEvent event;
	progress prog;
	objectKey progressDialog = NULL;
	char tmpChar[256];

	// Determine whether or not we can resize the filesystem
	if ((table->slices[sliceNumber].opFlags & FS_OP_RESIZE) ||
		(!strcmp(table->slices[sliceNumber].fsType, "ntfs") && ntfsResize))
	{
		// We can resize this filesystem.
		resizeFs = 1;

		char *optionStrings[] =
		{
			_("Filesystem and partition (recommended)"),
			_("Partition only")
		};
		int selected = -1;

		// We can resize the filesystem, but does the user want to?
		strcpy(tmpChar, _("Please select the type of resize operation:"));
		if (graphics)
		{
			selected = windowNewRadioDialog(window, _("Resize Type"),
				tmpChar, optionStrings, 2, 0);
		}
		else
		{
			selected = vshCursorMenu(tmpChar, optionStrings, 2,
				0 /* no max rows */, 0 /* selected */);
		}

		switch (selected)
		{
			case 0:
				break;
			case 1:
				resizeFs = 0;
				break;
			default:
				// Cancelled
				return (status = 0);
		}

		if (resizeFs)
		{
			if (table->changesPending)
			{
				error("%s", _("A filesystem resize cannot be undone, and must "
					"be committed\nto disk immediately.  You need to "
					"write your other changes\nto disk before "
					"continuing."));
				return (status = ERR_BUSY);
			}

			if ((table->slices[sliceNumber].opFlags & FS_OP_RESIZECONST) ||
				(!strcmp(table->slices[sliceNumber].fsType, "ntfs") &&
					ntfsGetResizeConstraints))
			{
				strcpy(tmpChar, _("Collecting filesystem resizing "
					"constraints..."));
				memset((void *) &prog, 0, sizeof(progress));
				if (graphics)
				{
					progressDialog = windowNewProgressDialog(window, tmpChar,
						&prog);
				}
				else
				{
					printf("\n%s\n\n", tmpChar);
					vshProgressBar(&prog);
				}

				if (table->slices[sliceNumber].opFlags & FS_OP_RESIZECONST)
				{
					status = filesystemResizeConstraints(
						table->slices[sliceNumber].diskName, &minFsSectors,
						&maxFsSectors, &prog);
				}
				else if (!strcmp(table->slices[sliceNumber].fsType, "ntfs"))
				{
					status = ntfsGetResizeConstraints(
						table->slices[sliceNumber].diskName, &minFsSectors,
						&maxFsSectors, &prog);
				}

				if (graphics && progressDialog)
					windowProgressDialogDestroy(progressDialog);
				else
					vshProgressBarDestroy(&prog);

				if (status < 0)
				{
					snprintf(tmpChar, sizeof(tmpChar), "%s",
						_("Error reading filesystem information.  However, "
						"it is\npossible to resize the partition anyway "
						"and discard all\nof the data it contains.  "
						"Continue?"));
					if (graphics)
					{
						if (!windowNewQueryDialog(window,
							_("Can't Resize Filesystem"), tmpChar))
						{
							return (status = 0);
						}
					}
					else
					{
						if (!yesOrNo(tmpChar))
							return (status = 0);
					}

					resizeFs = 0;
				}
				else
				{
					haveResizeConstraints = 1;
				}
			}
		}
	}
	else
	{
		// We can't resize this filesystem, but we will offer to resize
		// the slice anyway.
		snprintf(tmpChar, sizeof(tmpChar), "%s",
			_("Resizing the filesystem on this partition is "
			"not supported.\nHowever, it is possible to resize the "
			"partition anyway and\ndiscard all of the data it contains.  "
			"Continue?"));
		if (graphics)
		{
			if (!windowNewQueryDialog(window, _("Can't Resize Filesystem"),
				tmpChar))
			{
				return (status = 0);
			}
		}
		else
		{
			if (!yesOrNo(tmpChar))
				return (status = 0);
		}
	}

	status = mountedCheckSlice(&table->slices[sliceNumber]);
	if (status < 0)
		return (status);

	// Calculate the current, minimum, and maximum permissable sizes.

	currentSizeMb = sectorsToMegabytes(table->disk,
		table->slices[sliceNumber].raw.numSectors, -1 /* round down */);

	minSizeMb = 1;
	if (haveResizeConstraints)
	{
		minSizeMb = sectorsToMegabytes(table->disk, minFsSectors,
			1 /* round up */);
	}

	if ((sliceNumber < (table->numSlices - 1)) &&
		!isSliceReserved(table, (sliceNumber + 1)) &&
		!isSliceUsed(table, (sliceNumber + 1)))
	{
		maxSizeMb = sectorsToMegabytes(table->disk,
			(table->slices[sliceNumber].raw.numSectors +
				table->slices[sliceNumber + 1].raw.numSectors),
			-1 /* round down */);
	}
	else
	{
		maxSizeMb = currentSizeMb;
	}

	if (haveResizeConstraints)
	{
		maxSizeMb = min(maxSizeMb, sectorsToMegabytes(table->disk,
			maxFsSectors, -1 /* round down */));
	}

	while (1)
	{
		if (graphics)
		{
			resizeDialog = windowNewDialog(window, _("Resize Partition"));

			memset(&params, 0, sizeof(componentParameters));
			params.gridWidth = 2;
			params.gridHeight = 1;
			params.padTop = 10;
			params.padLeft = 5;
			params.padRight = 5;
			params.orientationX = orient_center;
			params.orientationY = orient_middle;

			if (haveResizeConstraints)
			{
				params.flags |= WINDOW_COMPFLAG_HASBORDER;
				partCanvas = windowNewCanvas(resizeDialog, (canvasWidth / 2),
					canvasHeight, &params);
			}

			// A label and field for the new size
			sprintf(tmpChar, _("Current size: %uMb\n"), currentSizeMb);
			sprintf((tmpChar + strlen(tmpChar)), SIZEMB_MESSAGE, minSizeMb,
				maxSizeMb);
			params.gridY++;
			params.padTop = 5;
			params.orientationX = orient_left;
			params.flags &= ~WINDOW_COMPFLAG_HASBORDER;
			windowNewTextLabel(resizeDialog, tmpChar, &params);

			// Make a text field for the new size value
			params.gridY++;
			sizeField = windowNewTextField(resizeDialog, 10, &params);
			// Put the current partition size into the field
			sprintf(tmpChar, "%u", currentSizeMb);
			windowComponentSetData(sizeField, tmpChar, strlen(tmpChar),
				1 /* redraw */);

			// Add a slider to adjust the size value mouse-ly
			params.gridY++;
			sizeSlider = windowNewSlider(resizeDialog, scrollbar_horizontal,
				0, 0, &params);
			sliderState.displayPercent = 20; // Size of slider 20%
			sliderState.positionPercent = (((currentSizeMb - minSizeMb) *
				100) / (maxSizeMb - minSizeMb));
			windowComponentSetData(sizeSlider, &sliderState,
				sizeof(scrollBarState), 1 /* redraw */);

			// Make 'OK' and 'cancel' buttons
			params.gridY++;
			params.gridWidth = 1;
			params.padBottom = 5;
			params.orientationX = orient_right;
			params.flags |= WINDOW_COMPFLAG_FIXEDWIDTH;
			okButton = windowNewButton(resizeDialog, _("OK"), NULL, &params);

			params.gridX = 1;
			params.orientationX = orient_left;
			cancelButton =
			windowNewButton(resizeDialog, _("Cancel"), NULL, &params);
			windowComponentFocus(cancelButton);

			// Make the window visible
			windowSetResizable(resizeDialog, 0);
			windowCenterDialog(window, resizeDialog);
			windowSetVisible(resizeDialog, 1);

			if (haveResizeConstraints)
			{
				// Set up our drawing parameters for the canvas
				memset(&drawParams, 0, sizeof(windowDrawParameters));
				drawParams.operation = draw_rect;
				drawParams.mode = draw_normal;
				drawParams.width = windowComponentGetWidth(partCanvas);
				drawParams.height = canvasHeight;
				drawParams.thickness = 1;
				drawParams.fill = 1;

				// Draw a background
				memcpy(&drawParams.foreground,
					table->slices[sliceNumber].color, sizeof(color));
				windowComponentSetData(partCanvas, &drawParams, 1,
					1 /* redraw */);

				// Draw a shaded bit representing the used portion
				drawParams.foreground.red =
					((drawParams.foreground.red * 2) / 3);
				drawParams.foreground.green =
					((drawParams.foreground.green * 2) / 3);
				drawParams.foreground.blue =
					((drawParams.foreground.blue * 2) / 3);
				drawParams.width = ((minFsSectors * drawParams.width) /
					table->slices[sliceNumber].raw.numSectors);
				windowComponentSetData(partCanvas, &drawParams, 1,
					1 /* redraw */);
			}

			while (1)
			{
				// Check for size text field changes
				if ((windowComponentEventGet(sizeField, &event) > 0) &&
					(event.type == EVENT_KEY_DOWN))
				{
					if (event.key == keyEnter)
						// User hit enter.
						break;

					// See if we can apply a newly-typed number to the slider
					newSizeString[0] = '\0';
					windowComponentGetData(sizeField, newSizeString,
						sizeof(newSizeString));
					newSizeMb = atoi(newSizeString);
					if ((newSizeMb >= minSizeMb) && (newSizeMb <= maxSizeMb))
					{
						sliderState.positionPercent = (((newSizeMb -
							minSizeMb) * 100) / (maxSizeMb - minSizeMb));
						windowComponentSetData(sizeSlider, &sliderState,
							sizeof(scrollBarState), 1 /* redraw */);
					}
				}

				// Check for size slider changes
				if (windowComponentEventGet(sizeSlider, &event) > 0)
				{
					if (event.type & (EVENT_MOUSE_LEFTDOWN | EVENT_MOUSE_DRAG |
						EVENT_KEY_DOWN))
					{
						windowComponentGetData(sizeSlider, &sliderState,
							sizeof(scrollBarState));
						sprintf(tmpChar, "%u", (((sliderState.positionPercent *
							(maxSizeMb - minSizeMb)) / 100) + minSizeMb));
						windowComponentSetData(sizeField, tmpChar,
							strlen(tmpChar), 1 /* redraw */);
					}
				}

				// Check for the OK button
				if ((windowComponentEventGet(okButton, &event) > 0) &&
					(event.type == EVENT_MOUSE_LEFTUP))
				{
					break;
				}

				// Check for the Cancel button
				if ((windowComponentEventGet(cancelButton, &event) > 0) &&
					(event.type == EVENT_MOUSE_LEFTUP))
				{
					windowDestroy(resizeDialog);
					return (status = 0);
				}

				// Check for window close events
				if ((windowComponentEventGet(resizeDialog, &event) > 0) &&
					(event.type == EVENT_WINDOW_CLOSE))
				{
					windowDestroy(resizeDialog);
					return (status = 0);
				}

				// Done
				multitaskerYield();
			}

			windowComponentGetData(sizeField, newSizeString,
				sizeof(newSizeString));
			windowDestroy(resizeDialog);
		}

		else
		{
			printf(_("Current size: %uMb"), currentSizeMb);
			printf("\n");
			printf(SIZEMB_MESSAGE, minSizeMb, maxSizeMb);
			printf("%s", _(", or 'Q' to quit:\n-> "));

			status = readLine("0123456789CcMmQq", newSizeString,
				sizeof(newSizeString));
			if (status < 0)
				continue;

			if ((newSizeString[0] == 'Q') || (newSizeString[0] == 'q'))
				return (status = 0);
		}

		newSizeMb = atoi(newSizeString);
		if ((newSizeMb < minSizeMb) || (newSizeMb > maxSizeMb))
		{
			error("%s", _("Invalid size"));
			continue;
		}

		break;
	}

	// Before we go, warn about backups and such.
	snprintf(tmpChar, sizeof(tmpChar),
		_("Resizing partition from %u to %u megabytes.\n"
		"Please use this feature with caution, and only after\n"
		"making a backup of all important data.  Continue?"),
		currentSizeMb, newSizeMb);

	if (graphics)
	{
		if (!windowNewQueryDialog(window, _("Resizing"), tmpChar))
			return (status = 0);
	}
	else
	{
		if (!yesOrNo(tmpChar))
			return (status = 0);
	}

	status = doResize(sliceNumber, megabytesToSectors(table->disk, newSizeMb),
		resizeFs);
	if (status < 0)
		return (status);

	if (resizeFs)
	{
		snprintf(tmpChar, sizeof(tmpChar), "%s",
			_("Filesystem resize complete"));
		if (graphics)
		{
			windowNewInfoDialog(window, _("Success"), tmpChar);
		}
		else
		{
			printf("\n%s\n", tmpChar);
			pause();
		}
	}

	// Return success
	return (status = 0);
}


static void copyIoThread(int argc, char *argv[])
{
	// This thread polls for empty/full buffers and reads/writes them when
	// they're ready

	int status = 0;
	int reader = 0;
	ioThreadArgs *args = NULL;
	uquad_t currentSector = 0;
	uquad_t doSectors = 0;
	unsigned sectorsPerOp = 0;
	int currentBuffer = 0;
	int ignoreErrors = 0;
	uquad_t retrySector = 0;
	unsigned errorSectors = 0;
	int startSeconds = rtcUptimeSeconds();
	int remainingSeconds = 0;

	// Are we a reader thread or a writer thread?
	if (argc < 2)
	{
		error("IO thread argument count (%d) error", argc);
		status = ERR_ARGUMENTCOUNT;
		goto terminate;
	}

	if (!strcmp(argv[1], "reader"))
	{
		reader = 1;
	}
	else if (!strcmp(argv[1], "writer"))
	{
		reader = 0;
	}
	else
	{
		error("Invalid IO thread argument \"%s\"", argv[0]);
		status = ERR_INVALID;
		goto terminate;
	}

	if (reader)
		args = &readerArgs;
	else
		args = &writerArgs;

	currentSector = args->startSector;
	doSectors = args->numSectors;

	// Calculate the sectors per operation for the disk
	sectorsPerOp = (args->buffer->bufferSize / args->theDisk->sectorSize);

	while (doSectors && !ioThreadsTerminate)
	{
		if ((reader && args->buffer->buffer[currentBuffer].full) ||
			(!reader && !args->buffer->buffer[currentBuffer].full))
		{
			// For good behaviour
			multitaskerYield();
			continue;
		}

		// For the last op, reset the 'sectorsPerOp' values
		if (sectorsPerOp > doSectors)
			sectorsPerOp = doSectors;

		if (reader)
		{
			status = diskReadSectors(args->theDisk->name, currentSector,
				sectorsPerOp, args->buffer->buffer[currentBuffer].data);
		}
		else
		{
			status = diskWriteSectors(args->theDisk->name, currentSector,
				sectorsPerOp, args->buffer->buffer[currentBuffer].data);
		}

		if (status < 0)
		{
			// There was an error reading or writing.

			if (!ignoreErrors)
			{
				if (reader)
				{
					error(_("Error %d reading %u sectors at %llu from disk "
						"%s"), status, sectorsPerOp, currentSector,
						args->theDisk->name);

					if (yesOrNo(_("Ignore read errors, and continue trying "
						"to copy?")))
					{
						ignoreErrors = 1;
					}
				}
				else
				{
					error(_("Error %d writing %u sectors at %llu to disk %s"),
						status, sectorsPerOp, currentSector,
						args->theDisk->name);

					if (yesOrNo(_("Ignore write errors, and continue trying "
						"to copy?")))
					{
						ignoreErrors = 1;
					}
				}

				if (!ignoreErrors)
					goto terminate;
			}

			// We'll retry reading/writing these sectors one at a time
			for (retrySector = currentSector;
				retrySector < (currentSector + sectorsPerOp); retrySector ++)
			{
				if (reader)
				{
					status = diskReadSectors(args->theDisk->name, retrySector,
						1, (args->buffer->buffer[currentBuffer].data +
							((retrySector - currentSector) *
								args->theDisk->sectorSize)));
				}
				else
				{
					status = diskWriteSectors(args->theDisk->name, retrySector,
						1, (args->buffer->buffer[currentBuffer].data +
							((retrySector - currentSector) *
								args->theDisk->sectorSize)));
				}

				if (status < 0)
					errorSectors += 1;
			}
		}

		if (reader)
			args->buffer->buffer[currentBuffer].full = 1;
		else
			args->buffer->buffer[currentBuffer].full = 0;

		currentSector += sectorsPerOp;
		doSectors -= sectorsPerOp;

		if (!reader && args->prog && (lockGet(&args->prog->progLock) >= 0))
		{
			args->prog->numFinished = (currentSector - args->startSector);
			if (args->numSectors >= 100)
			{
				args->prog->percentFinished =
					(args->prog->numFinished / (args->numSectors / 100));
			}
			else
			{
				args->prog->percentFinished =
					((args->prog->numFinished * 100) / args->numSectors);
			}

			remainingSeconds = (((rtcUptimeSeconds() - startSeconds) *
				(doSectors / sectorsPerOp)) /
					(args->prog->numFinished / sectorsPerOp));

			formatTime((char *) args->prog->statusMessage, remainingSeconds);

			lockRelease(&args->prog->progLock);
		}

		if (!currentBuffer)
			currentBuffer = 1;
		else
			currentBuffer = 0;
	}

	if (errorSectors)
	{
		warning(_("Errors %s %u sectors of disk %s.  Disk may be failing."),
			(reader? _("reading") : _("writing")), errorSectors,
			args->theDisk->name);
	}

	// Indicate that we're finished
	ioThreadsFinished += 1;

	if (!ioThreadsTerminate)
	{
		// Wait for both to be finished
		while (ioThreadsFinished < 2)
			multitaskerYield();
	}

	status = 0;

terminate:
	multitaskerTerminate(status);
}


static int copyData(disk *srcDisk, unsigned srcSector, disk *destDisk,
	unsigned destSector, unsigned numSectors)
{
	// Generic function for raw disk I/O

	int status = 0;
	ioBuffer buffer;
	int readerPID = 0;
	int writerPID = 0;
	char tmpChar[160];
	progress prog;
	objectKey progressDialog = NULL;
	objectKey cancelDialog = NULL;

	// Set up the memory buffer to copy data to/from
	memset(&buffer, 0, sizeof(ioBuffer));
	buffer.bufferSize = 1048576;

	// This loop will allow us to try successively smaller memory buffer
	// allocations, so that we can start by trying to allocate a large amount
	// of memory, but not failing unless we're totally out of memory
	while (!buffer.buffer[0].data || !buffer.buffer[1].data)
	{
		buffer.buffer[0].data = memoryGet(buffer.bufferSize,
			"disk copy buffer");
		buffer.buffer[1].data = memoryGet(buffer.bufferSize,
			"disk copy buffer");

		if (!buffer.buffer[0].data || !buffer.buffer[1].data)
		{
			if (buffer.buffer[0].data)
			{
				memoryRelease(buffer.buffer[0].data);
				buffer.buffer[0].data = NULL;
			}
			if (buffer.buffer[1].data)
			{
				memoryRelease(buffer.buffer[1].data);
				buffer.buffer[1].data = NULL;
			}

			buffer.bufferSize /= 2;
			if (buffer.bufferSize < 65535)
			{
				error("%s", _("Unable to allocate memory buffer!"));
				return (status = ERR_MEMORY);
			}
		}
	}

	sprintf(tmpChar, _("Copying %u MB..."), sectorsToMegabytes(srcDisk,
		numSectors, 1 /* round up */));

	memset((void *) &prog, 0, sizeof(progress));
	prog.numTotal = numSectors;
	strcpy((char *) prog.statusMessage,
		_("Time remaining: ?? hours ?? minutes"));
	prog.canCancel = 1;

	if (graphics)
	{
		progressDialog = windowNewProgressDialog(window, tmpChar, &prog);
	}
	else
	{
		printf(_("\n%s (press 'Q' to cancel)\n"), tmpChar);
		vshProgressBar(&prog);
	}

	// If disk caching is enabled on the disks, disable it whilst we do a large
	// operation like this.
	if (!(srcDisk->flags & DISKFLAG_NOCACHE))
		diskSetFlags(srcDisk->name, DISKFLAG_NOCACHE, 1);
	if (!(destDisk->flags & DISKFLAG_NOCACHE))
		diskSetFlags(destDisk->name, DISKFLAG_NOCACHE, 1);

	// Set up and start our IO threads

	memset(&readerArgs, 0, sizeof(ioThreadArgs));
	readerArgs.theDisk = srcDisk;
	readerArgs.startSector = srcSector;
	readerArgs.numSectors = numSectors;
	readerArgs.buffer = &buffer;

	memset(&writerArgs, 0, sizeof(ioThreadArgs));
	writerArgs.theDisk = destDisk;
	writerArgs.startSector = destSector;
	writerArgs.numSectors = numSectors;
	writerArgs.buffer = &buffer;
	writerArgs.prog = &prog;

	ioThreadsTerminate = 0;
	ioThreadsFinished = 0;

	readerPID = multitaskerSpawn(&copyIoThread, "i/o reader thread", 1,
		(void *[]){ "reader" });
	if (readerPID < 0)
	{
		status = readerPID;
		goto out;
	}

	writerPID = multitaskerSpawn(&copyIoThread, "i/o writer thread", 1,
		(void *[]){ "writer" });
	if (writerPID < 0)
	{
		status = writerPID;
		goto out;
	}

	while (1)
	{
		if (ioThreadsFinished == 2)
			break;

		// Now we wait for the IO threads to terminate themselves
		if (!multitaskerProcessIsAlive(readerPID) ||
			!multitaskerProcessIsAlive(writerPID))
		{
			prog.cancel = 1;
		}

		if (prog.cancel)
			// This can be set above, or else by the progress dialog when the
			// user presses the cancel button
			break;

		multitaskerYield();
	}

	if (prog.cancel)
	{
		sprintf(tmpChar, "%s", _("Terminating processes..."));
		if (graphics)
		{
			cancelDialog = windowNewBannerDialog(progressDialog, _("Cancel"),
				tmpChar);
		}
		else
		{
			printf("\n%s\n", tmpChar);
		}

		ioThreadsTerminate = 1;
		multitaskerYield();
		if (multitaskerProcessIsAlive(readerPID))
			multitaskerBlock(readerPID);
		if (multitaskerProcessIsAlive(writerPID))
			multitaskerBlock(writerPID);

		if (cancelDialog)
			windowDestroy(cancelDialog);

		status = ERR_CANCELLED;
	}
	else
		status = 0;

out:
	// Release copy buffer data
	memoryRelease(buffer.buffer[0].data);
	memoryRelease(buffer.buffer[1].data);

	// Flush data.
	diskSync(destDisk->name);

	// If applicable, re-enable disk caching.
	if (!(srcDisk->flags & DISKFLAG_NOCACHE))
		diskSetFlags(srcDisk->name, DISKFLAG_NOCACHE, 0);
	if (!(destDisk->flags & DISKFLAG_NOCACHE))
		diskSetFlags(destDisk->name, DISKFLAG_NOCACHE, 0);

	if (graphics && progressDialog)
		windowProgressDialogDestroy(progressDialog);
	else
		vshProgressBarDestroy(&prog);

	return (status);
}


static void clearDiskLabel(disk *theDisk, diskLabel *label)
{
	partitionTable t;
	int count;

	memset(&t, 0, sizeof(partitionTable));

	// Find it
	for (count = 0; count < numberDisks; count ++)
	{
		if (&disks[count] == theDisk)
		{
			t.disk = theDisk;
			t.diskNumber = count;
			t.label = label;
			t.changesPending = 1;
			writeChanges(&t, 0);
			break;
		}
	}
}


static int setFatGeometry(partitionTable *t, int sliceNumber)
{
	// Given a slice, make sure the FAT disk geometry fields are correct.

	int status = 0;
	slice *slc = &t->slices[sliceNumber];
	unsigned char *bootSector = NULL;
	fatBPB *bpb = NULL;

	bootSector = malloc(t->disk->sectorSize);
	if (!bootSector)
		return (status = ERR_MEMORY);
	bpb = (fatBPB *) bootSector;

	// Read the boot sector
	status = diskReadSectors(t->disk->name, slc->raw.startSector, 1,
		bootSector);
	if (status < 0)
	{
		free(bootSector);
		return (status);
	}

	// Set the values
	bpb->sectsPerTrack = t->disk->sectorsPerCylinder;
	bpb->numHeads = t->disk->heads;

	if (!strcmp(slc->fsType, "fat32"))
		bpb->fat32.biosDriveNum = (0x80 + t->disk->deviceNumber);
	else
		bpb->fat.biosDriveNum = (0x80 + t->disk->deviceNumber);

	// Write back the boot sector
	status = diskWriteSectors(t->disk->name, slc->raw.startSector, 1,
		bootSector);

	free(bootSector);
	return (status);
}


static disk *chooseDiskDialog(void)
{
	// Graphical way of prompting for disk selection

	int status = 0;
	disk *retDisk = NULL;
	int selected = 0;
	objectKey chooseWindow = NULL;
	componentParameters params;
	objectKey dList = NULL;
	objectKey okButton = NULL;
	objectKey cancelButton = NULL;
	windowEvent event;

	chooseWindow = windowNew(processId, _("Choose Disk"));

	memset(&params, 0, sizeof(componentParameters));
	params.gridWidth = 2;
	params.gridHeight = 1;
	params.padTop = 5;
	params.padLeft = 5;
	params.padRight = 5;
	params.orientationX = orient_center;
	params.orientationY = orient_middle;

	// Make a window list with all the disk choices
	dList = windowNewList(chooseWindow, windowlist_textonly, numberDisks, 1, 0,
		diskListParams, numberDisks, &params);
	windowComponentFocus(dList);

	// Make 'OK' and 'cancel' buttons
	params.gridY = 1;
	params.gridWidth = 1;
	params.padBottom = 5;
	params.orientationX = orient_right;
	params.flags |= WINDOW_COMPFLAG_FIXEDWIDTH;
	okButton = windowNewButton(chooseWindow, _("OK"), NULL, &params);

	params.gridX = 1;
	params.orientationX = orient_left;
	cancelButton = windowNewButton(chooseWindow, _("Cancel"), NULL, &params);

	// Make the window visible
	windowRemoveMinimizeButton(chooseWindow);
	windowRemoveCloseButton(chooseWindow);
	windowSetResizable(chooseWindow, 0);
	windowSetVisible(chooseWindow, 1);

	while (1)
	{
		// Check for our OK button
		status = windowComponentEventGet(okButton, &event);
		if ((status > 0) && (event.type == EVENT_MOUSE_LEFTUP))
		{
			windowComponentGetSelected(dList, &selected);
			retDisk = &disks[selected];
			break;
		}

		// Check for our Cancel button
		status = windowComponentEventGet(cancelButton, &event);
		if ((status > 0) && (event.type == EVENT_MOUSE_LEFTUP))
			break;

		// Done
		multitaskerYield();
	}

	windowDestroy(chooseWindow);

	return (retDisk);
}


static void copyDisk(void)
{
	int status = 0;
	char *diskStrings[DISK_MAXDEVICES];
	int diskNumber = 0;
	disk *srcDisk = NULL;
	disk *destDisk = NULL;
	unsigned lastUsedSector = 0;
	char tmpChar[160];
	int count;

	if (numberDisks < 2)
	{
		error("%s", _("No other disks to copy to"));
		return;
	}

	srcDisk = table->disk;

	// If there's only one other disk, select it automatically
	if (numberDisks == 2)
	{
		for (count = 0; count < numberDisks; count ++)
		{
			if (&disks[count] != srcDisk)
			{
				destDisk = &disks[count];
				break;
			}
		}
	}

	// Else make the user choose one
	else
	{
		while (1)
		{
			if (graphics)
			{
				destDisk = chooseDiskDialog();
				if (!destDisk)
					return;
			}
			else
			{
				for (count = 0; count < numberDisks; count ++)
					diskStrings[count] = diskListParams[count].text;

				diskNumber =
					vshCursorMenu(_("Please choose the disk to copy to:"),
						diskStrings, numberDisks, 10 /* max rows */,
						0 /* selected */);

				if (diskNumber < 0)
					return;

				destDisk = &disks[diskNumber];
			}

			if (destDisk == srcDisk)
			{
				error("%s", _("Not much point in copying a disk to itself!"));
				continue;
			}

			break;
		}
	}

	// We have a source disk and a destination disk.
	sprintf(tmpChar, _("Copy disk %s to disk %s.\nWARNING: THIS WILL DESTROY "
		"ALL DATA ON DISK %s.\nARE YOU SURE YOU WANT TO DO "
		"THIS?"), srcDisk->name, destDisk->name, destDisk->name);
	if (!yesOrNo(tmpChar))
		return;

	// We will copy everything up to the end of the last slice (not much
	// point in copying a bunch of unused space, even though it's potentially
	// conceivable that someone, somewhere might want to do that).  Find out
	// the logical sector number of the end of the last slice.
	for (count = 0; count < table->numSlices; count ++)
	{
		if (isSliceUsed(table, count) &&
			((table->slices[count].raw.startSector +
			table->slices[count].raw.numSectors - 1) > lastUsedSector))
		{
			lastUsedSector = (table->slices[count].raw.startSector +
				table->slices[count].raw.numSectors - 1);
		}
	}

	if (!lastUsedSector)
	{
		if (!yesOrNo(_("No partitions on the disk.  Do you want to copy the "
			"whole\ndisk anyway?")))
		{
			return;
		}

		lastUsedSector = (srcDisk->numSectors - 1);
	}

	// Make sure that the destination disk can hold the data
	if (lastUsedSector >= destDisk->numSectors)
	{
		sprintf(tmpChar, _("Disk %s is smaller than the amount of data on "
			"disk %s.\nIf you wish, you can continue and copy the "
			"data that will\nfit.  Don't do this unless you're "
			"sure you know what you're\ndoing.  CONTINUE?"),
		destDisk->name, srcDisk->name);
		if (!yesOrNo(tmpChar))
			return;
		printf("\n");

		lastUsedSector = (destDisk->numSectors - 1);
	}

	// Go
	status = copyData(srcDisk, 0, destDisk, 0, (lastUsedSector + 1));

	// If it was cancelled, clear the disk label
	if (status == ERR_CANCELLED)
		clearDiskLabel(destDisk, table->label);

	status = selectDisk(destDisk);
	if (status < 0)
		return;

	// Now, if any slices are ouside (or partially outside) the bounds of the
	// destination disk, delete (or truncate)
	for (count = (table->numSlices - 1); count >= 0; count --)
	{
		// Starts past the end of the disk?
		if (table->slices[count].raw.startSector >= destDisk->numSectors)
		{
			table->numSlices -= 1;
			table->changesPending += 1;
		}

		// Ends past the end of the disk?
		else if ((table->slices[count].raw.startSector +
			table->slices[count].raw.numSectors) >= destDisk->numSectors)
		{
			table->slices[count].raw.numSectors = (destDisk->numSectors -
				table->slices[count].raw.startSector);
			table->changesPending += 1;
		}
	}

	// Write out the partition table
	writeChanges(table, 0);

	// Make sure the disk geometries of any FAT slices are correct for
	// the new disk.
	for (count = 0; count < table->numSlices; count ++)
	{
		if (isSliceUsed(table, count) &&
			!strncmp(table->slices[count].fsType, "fat", 3))
		{
			setFatGeometry(table, count);
		}
	}

	return;
}


static void copyPartition(int sliceNumber)
{
	// 'Copy' a slice to our slice 'clipboard'
	memcpy(&clipboardSlice, &table->slices[sliceNumber], sizeof(slice));
	clipboardDisk = table->disk;
	clipboardSliceValid = 1;
}


static int pastePartition(int sliceNumber)
{
	// 'Paste' a slice from our partition 'clipboard' to the supplied
	// empty space slice.  This is really where a slice copying operation
	// takes place.

	int status = 0;
	slice *emptySlice = NULL;
	sliceType newType = partition_none;
	uquad_t startSector = 0;
	uquad_t endSector = 0;
	int newSliceNumber = 0;
	char tmpChar[160];

	if (!clipboardSliceValid)
	{
		error("%s", _("No partition copied to the clipboard"));
		return (status = ERR_NODATA);
	}

	if (isSliceReserved(table, sliceNumber) ||
		isSliceUsed(table, sliceNumber))
	{
		// Not empty space
		return (status = ERR_INVALID);
	}

	emptySlice = &table->slices[sliceNumber];

	// See if we can create a slice here, and if so, what type?
	newType = table->label->canCreateSlice(table->slices, table->numSlices,
		sliceNumber);
	if (newType == partition_none)
		return (status = ERR_NOCREATE);

	if (newType == partition_any)
		newType = clipboardSlice.raw.type;

	// Round up to a megabyte boundary.
	startSector = megabytesToSectors(table->disk,
		sectorsToMegabytes(table->disk, emptySlice->raw.startSector,
			1 /* round up */));

	// By convention, we don't write the first 'track' of the disk, or the
	// first sector of a logical partition (it's reserved area for the
	// partition table and possibly other stuff)
	if (startSector < table->disk->sectorsPerCylinder)
		startSector = table->disk->sectorsPerCylinder;
	if (newType == partition_logical)
		startSector += 1;

	// Make sure the new slice doesn't occur before any minimum limit defined
	// by the label-specific code
	if (startSector < table->label->firstUsableSect)
		startSector = table->label->firstUsableSect;

	// Calculate the ending sector
	endSector = (startSector + (clipboardSlice.raw.numSectors - 1));

	// Make sure the new slice doesn't extend beyond any maximum limit defined
	// by the label-specific code
	if (endSector > table->label->lastUsableSect)
		endSector = table->label->lastUsableSect;

	// Now, make sure there's something left
	if (endSector <= startSector)
		return (status = ERR_NOFREE);

	// Check whether the empty slice is big enough
	if ((emptySlice->raw.numSectors -
			(startSector - emptySlice->raw.startSector)) <
		clipboardSlice.raw.numSectors)
	{
		error(_("Partition %s is too big (%llu sectors) to fit in the\n"
			"selected empty space (%llu sectors)"),
			clipboardSlice.showSliceName, clipboardSlice.raw.numSectors,
			(emptySlice->raw.numSectors -
				(startSector - emptySlice->raw.startSector)));
		return (status = ERR_NOFREE);
	}

	if (table->changesPending)
	{
		error("%s", _("A partition paste cannot be undone, and it is "
			"required that you\nwrite your other changes to disk before "
			"continuing."));
		return (status = ERR_BUSY);
	}

	// Everything seems OK.  Confirm.
	sprintf(tmpChar, _("Paste partition %s to selected empty space on disk "
		"%s?"), clipboardSlice.showSliceName, table->disk->name);
	if (!yesOrNo(tmpChar))
		return (status = 0);

	status = copyData(clipboardDisk, clipboardSlice.raw.startSector,
		table->disk, startSector, clipboardSlice.raw.numSectors);
	if (status < 0)
		return (status);

	newSliceNumber = doCreate(sliceNumber, newType, startSector,
		((endSector - startSector) + 1));
	if (newSliceNumber < 0)
		return (status = newSliceNumber);

	// Clone any tags, flags, GUIDs, etc. from the clipboard slice.  This
	// won't produce all the the desired results if the two disks don't share
	// the same label type, in which case the user will end up with some
	// things set to defaults.

	if (table->label->flags & LABELFLAG_USETAGS)
	{
		if (clipboardSlice.raw.tag)
			table->slices[newSliceNumber].raw.tag = clipboardSlice.raw.tag;
		else
			table->slices[newSliceNumber].raw.tag = DEFAULT_TAG;
	}

	if (table->label->flags & LABELFLAG_USEGUIDS)
	{
		if (memcmp(&clipboardSlice.raw.typeGuid, &GUID_UNUSED, sizeof(guid)))
			memcpy(&table->slices[newSliceNumber].raw.typeGuid,
				&clipboardSlice.raw.typeGuid, sizeof(guid));
		else
			memcpy(&table->slices[newSliceNumber].raw.typeGuid, &DEFAULT_GUID,
				sizeof(guid));
	}

	table->slices[newSliceNumber].raw.flags = clipboardSlice.raw.flags;
	table->slices[newSliceNumber].raw.attributes =
		clipboardSlice.raw.attributes;

	// If it's a FAT filesystem, make sure the disk geometry stuff in it
	// is correct for the new disk.
	if (!strncmp(clipboardSlice.fsType, "fat", 3))
		setFatGeometry(table, newSliceNumber);

	table->selectedSlice = newSliceNumber;
	table->changesPending += 1;

	// Update the slice list
	updateSliceList(table);

	return (status = 0);
}


static void swapSlices(partitionTable *t, int first, int second)
{
	// Given 2 slices, swap them.  This is primarily for the change partition
	// order function, below

	slice *firstSlice = &t->slices[first];
	slice *secondSlice = &t->slices[second];
	int tmpOrder = 0;
	slice tmpSlice;

	// Swap the partition data
	memcpy(&tmpSlice, secondSlice, sizeof(slice));
	memcpy(secondSlice, firstSlice, sizeof(slice));
	memcpy(firstSlice, &tmpSlice, sizeof(slice));

	tmpOrder = secondSlice->raw.order;
	secondSlice->raw.order = firstSlice->raw.order;
	firstSlice->raw.order = tmpOrder;

	#ifdef PARTLOGIC
		sprintf(firstSlice->showSliceName, "%d", (firstSlice->raw.order + 1));
		sprintf(secondSlice->showSliceName, "%d",
			(secondSlice->raw.order + 1));
	#else
		sprintf(firstSlice->showSliceName, "%s%c", t->disk->name,
			('a' + firstSlice->raw.order));
		sprintf(secondSlice->showSliceName, "%s%c", t->disk->name,
			('a' + secondSlice->raw.order));
	#endif

	makeSliceString(t, first);
	makeSliceString(t, second);
}


static void changePartitionOrder(void)
{
	// This allows the user to change the ordering of primary partitions

	partitionTable tableCopy;
	listItemParameters orderListParams[DISK_MAX_PARTITIONS];
	objectKey orderDialog = NULL;
	objectKey orderList = NULL;
	objectKey upButton = NULL;
	objectKey downButton = NULL;
	objectKey okButton = NULL;
	objectKey cancelButton = NULL;
	int selected = 0;
	char lineString[SLICESTRING_LENGTH + 2];
	componentParameters params;
	windowEvent event;
	textAttrs attrs;
	int count1, count2;

	// Make a copy of the partition table
	memcpy(&tableCopy, table, sizeof(partitionTable));

	// Clear out the slice list in the table copy
	memset(&tableCopy.slices, 0, (MAX_SLICES * sizeof(slice)));
	tableCopy.numSlices = 0;

	// Add back primary slices into our table copy, in order
	for (count1 = 0; count1 < DISK_MAX_PARTITIONS; count1 ++)
	{
		for (count2 = 0; count2 < table->numSlices; count2 ++)
		{
			if (isSliceUsed(table, count2) &&
				(table->slices[count2].raw.order == count1) &&
					!ISLOGICAL(&table->slices[count2]))
			{
				memcpy(&tableCopy.slices[tableCopy.numSlices],
					&table->slices[count2], sizeof(slice));
				strncpy(orderListParams[tableCopy.numSlices++].text,
					table->slices[count2].string, WINDOW_MAX_LABEL_LENGTH);
				break;
			}
		}
	}

	if (tableCopy.numSlices < 2)
	{
		error("%s", _("Must be more than one primary partition to reorder!"));
		return;
	}

	if (graphics)
	{
		orderDialog = windowNewDialog(window, _("Partition Order"));

		memset(&params, 0, sizeof(componentParameters));
		params.gridWidth = 2;
		params.gridHeight = 2;
		params.padTop = 10;
		params.padLeft = 5;
		params.padRight = 5;
		params.orientationX = orient_center;
		params.orientationY = orient_middle;
		params.font = fontGet(FONT_FAMILY_LIBMONO, FONT_STYLEFLAG_FIXED, 10,
			NULL);

		// Make a window list with all the disk choices
		orderList = windowNewList(orderDialog, windowlist_textonly,
			DISK_MAX_PRIMARY_PARTITIONS, 1, 0,
			orderListParams, tableCopy.numSlices, &params);
		windowComponentFocus(orderList);

		// Make 'up' and 'down' buttons
		params.gridX = 2;
		params.gridHeight = 1;
		params.gridWidth = 1;
		params.font = NULL;
		upButton = windowNewButton(orderDialog, _("Up"), NULL, &params);

		params.gridY = 1;
		params.padTop = 5;
		downButton = windowNewButton(orderDialog, _("Down"), NULL, &params);

		// Make 'OK' and 'cancel' buttons
		params.gridX = 0;
		params.gridY = 2;
		params.padBottom = 5;
		params.orientationX = orient_right;
		params.flags |= WINDOW_COMPFLAG_FIXEDWIDTH;
		okButton = windowNewButton(orderDialog, _("OK"), NULL, &params);

		params.gridX = 1;
		params.orientationX = orient_left;
		cancelButton = windowNewButton(orderDialog, _("Cancel"), NULL,
			&params);

		// Make the window visible
		windowRemoveMinimizeButton(orderDialog);
		windowSetResizable(orderDialog, 0);
		windowSetVisible(orderDialog, 1);

		while (1)
		{
			windowComponentGetSelected(orderList, &selected);

			if ((windowComponentEventGet(upButton, &event) > 0) &&
				(event.type == EVENT_MOUSE_LEFTUP) && (selected > 0))
			{
				// 'Up' button
				swapSlices(&tableCopy, selected, (selected - 1));
				strncpy(orderListParams[selected].text,
					tableCopy.slices[selected].string,
					WINDOW_MAX_LABEL_LENGTH);
				strncpy(orderListParams[selected - 1].text,
					tableCopy.slices[selected - 1].string,
					WINDOW_MAX_LABEL_LENGTH);
				windowComponentSetData(orderList, orderListParams,
					tableCopy.numSlices, 1 /* redraw */);
				windowComponentSetSelected(orderList, (selected - 1));
			}

			if ((windowComponentEventGet(downButton, &event) > 0) &&
				(event.type == EVENT_MOUSE_LEFTUP) &&
				(selected < (tableCopy.numSlices - 1)))
			{
				// 'Down' button
				swapSlices(&tableCopy, selected, (selected + 1));
				strncpy(orderListParams[selected].text,
					tableCopy.slices[selected].string,
					WINDOW_MAX_LABEL_LENGTH);
				strncpy(orderListParams[selected + 1].text,
					tableCopy.slices[selected + 1].string,
					WINDOW_MAX_LABEL_LENGTH);
				windowComponentSetData(orderList, orderListParams,
					tableCopy.numSlices, 1 /* redraw */);
				windowComponentSetSelected(orderList, (selected + 1));
			}

			// Check for our OK button
			if ((windowComponentEventGet(okButton, &event) > 0) &&
				(event.type == EVENT_MOUSE_LEFTUP))
			{
				windowDestroy(orderDialog);
				goto commit;
			}

			// Check for our Cancel button or window close
			if (((windowComponentEventGet(orderDialog, &event) > 0) &&
				(event.type == EVENT_WINDOW_CLOSE)) ||
				((windowComponentEventGet(cancelButton, &event) > 0) &&
				(event.type == EVENT_MOUSE_LEFTUP)))
			{
				windowDestroy(orderDialog);
				return;
			}

			// Done
			multitaskerYield();
		}
	}
	else
	{
		memset(&attrs, 0, sizeof(textAttrs));
		textSetCursor(0);
		textInputSetEcho(0);

		memset(lineString, 196, (SLICESTRING_LENGTH + 1));
		lineString[SLICESTRING_LENGTH + 1] = '\0';

		while (1)
		{
			printBanner();
			printf(_("\nChange Partition Order\n\n %s\n"), lineString);

			// Print the partition strings
			for (count1 = 0; count1 < tableCopy.numSlices; count1 ++)
			{
				printf(" ");

				if (count1 == selected)
					attrs.flags = TEXT_ATTRS_REVERSE;
				else
					attrs.flags = 0;

				textPrintAttrs(&attrs, " ");
				textPrintAttrs(&attrs, orderListParams[count1].text);
				for (count2 = strlen(orderListParams[count1].text);
					count2 < SLICESTRING_LENGTH; count2 ++)
				{
					textPrintAttrs(&attrs, " ");
				}

				printf("\n");
			}

			printf(_(" %s\n\n  [Cursor up/down to select, '-' move up, '+' "
				"move down,\n   Enter to accept, 'Q' to quit]"), lineString);

			switch (getchar())
			{
				case (char) ASCII_ENTER:
					textSetCursor(1);
					textInputSetEcho(1);
					goto commit;

				case (char) ASCII_CRSRUP:
					// Cursor up.
					if (selected > 0)
						selected -= 1;
					continue;

				case (char) ASCII_CRSRDOWN:
					// Cursor down.
					if (selected < (tableCopy.numSlices - 1))
						selected += 1;
					continue;

				case '-':
					if (selected > 0)
					{
						// Move up
						swapSlices(&tableCopy, selected, (selected - 1));
						strncpy(orderListParams[selected].text,
							tableCopy.slices[selected].string,
							WINDOW_MAX_LABEL_LENGTH);
						strncpy(orderListParams[selected - 1].text,
							tableCopy.slices[selected - 1].string,
							WINDOW_MAX_LABEL_LENGTH);
						selected -= 1;

					}
					continue;

				case '+':
					if (selected < (tableCopy.numSlices - 1))
					{
						// Move down
						swapSlices(&tableCopy, selected, (selected + 1));
						strncpy(orderListParams[selected].text,
							tableCopy.slices[selected].string,
							WINDOW_MAX_LABEL_LENGTH);
						strncpy(orderListParams[selected + 1].text,
							tableCopy.slices[selected + 1].string,
							WINDOW_MAX_LABEL_LENGTH);
						selected += 1;
					}
					continue;

				case 'q':
				case 'Q':
					textSetCursor(1);
					textInputSetEcho(1);
					return;

				default:
					continue;
			}
		}
	}

commit:
	// If we fall through to here, we are making the changes.  Copy the slices
	// from our table copy back into the partition table.
	for (count1 = 0; count1 < tableCopy.numSlices; count1 ++)
	{
		for (count2 = 0; count2 < table->numSlices; count2 ++)
		{
			if (tableCopy.slices[count1].raw.startSector ==
				table->slices[count2].raw.startSector)
			{
				memcpy(&table->slices[count2], &tableCopy.slices[count1],
					sizeof(slice));
				break;
			}
		}
	}

	updateSliceList(table);
	table->changesPending += 1;
	return;
}


static int writeSimpleMbr(void)
{
	// Put simple MBR code into the main partition table.

	int status = 0;
	fileStream mbrFile;
	msdosMbr *mbr = NULL;

	if (table->changesPending)
	{
		error("%s", _("This operation cannot be undone, and it is required "
			"that\nyou write your other changes to disk before "
			"continuing."));
		return (status = ERR_BUSY);
	}

	if (!yesOrNo(_("After you write changes, the \"active\" partition will\n"
		"always boot automatically.  Proceed?")))
	{
		return (status = 0);
	}

	// Open the MBR file
	memset(&mbrFile, 0, sizeof(fileStream));
	status = fileStreamOpen(SIMPLE_MBR_FILE, OPENMODE_READ, &mbrFile);
	if (status < 0)
	{
		error(_("Can't locate simple MBR file %s"), SIMPLE_MBR_FILE);
		return (status);
	}

	// Get memory to hold the MBR sector
	mbr = malloc(table->disk->sectorSize);
	if (!mbr)
	{
		error("%s", _("Error getting memory"));
		return (status = ERR_MEMORY);
	}

	// Read the current MBR sector.
	status = diskReadSectors(table->disk->name, 0, 1, mbr);
	if (status < 0)
	{
		error("%s", _("Couldn't read MBR sector"));
		return (status);
	}

	// Read the first 440 bytes into the sector data
	status = fileStreamRead(&mbrFile, MSDOS_BOOT_CODE_SIZE,
		(char *) mbr->bootcode);
	if (status < 0)
	{
		error(_("Can't read simple MBR file %s"), SIMPLE_MBR_FILE);
		return (status);
	}

	// Make sure it's got the boot sector signature
	mbr->bootSig = MSDOS_BOOT_SIGNATURE;

	// Write back the MBR sector.
	status = diskWriteSectors(table->disk->name, 0, 1, mbr);
	if (status < 0)
	{
		error("%s", _("Couldn't write MBR sector"));
		return (status);
	}

	return (status = 0);
}


static int mbrBootMenu(void)
{
	// Call the 'bootmenu' program to install a boot menu

	int status = 0;
	char command[80];

	if (table->changesPending)
	{
		error("%s", _("This operation cannot be undone, and it is required "
			"that\nyou write your other changes to disk before "
			"continuing."));
		return (status = ERR_BUSY);
	}

	sprintf(command, PATH_PROGRAMS "/bootmenu %s", table->disk->name);

	status = system(command);
	if (status < 0)
		error(_("Error %d running bootmenu command"), status);

	// Need to re-read the partition table
	return (selectDisk(table->disk));
}


static void restoreBackup(void)
{
	// Restore the backed-up partition table from a file

	int status = 0;
	char *fileName = NULL;
	fileStream backupFile;

	if (!yesOrNo(_("Restore old partition table from backup?")))
		return;

	// Clear stack data
	memset(&backupFile, 0, sizeof(fileStream));

	// Construct the file name
	fileName = malloc(MAX_PATH_NAME_LENGTH);
	if (!fileName)
		return;
	sprintf(fileName, BACKUP_MBR, table->disk->name);

	// Read a backup copy of the partition tables
	status = fileStreamOpen(fileName, OPENMODE_READ, &backupFile);

	free(fileName);

	if (status < 0)
	{
		error("%s", _("Error opening backup partition table file"));
		return;
	}

	// Clear the raw slices in the partition table.
	memset(table->rawSlices, 0, (DISK_MAX_PARTITIONS * sizeof(rawSlice)));
	table->numRawSlices = 0;

	// Get the number of raw slices
	status = fileStreamRead(&backupFile, sizeof(int),
		(char *) &table->numRawSlices);
	if (status < 0)
	{
		error("%s", _("Error reading backup partition table file"));
		fileStreamClose(&backupFile);
		return;
	}

	status = fileStreamRead(&backupFile, (table->numRawSlices *
		sizeof(rawSlice)), (char *) table->rawSlices);

	fileStreamClose(&backupFile);

	if (status < 0)
	{
		error("%s", _("Error reading backup partition table file"));
		return;
	}

	// Generate the slice list from our raw slices
	makeSliceList(table);

	// Don't write it.  The user has to do that explicitly.
	table->changesPending += 1;

	return;
}


static int chooseSecurityLevel(void)
{
	int securityLevel = 0;
	const char *chooseString =
		_("Erasing clears the data securely by overwriting successive\n"
		"passes of random data.  More passes is more secure but\n"
		"takes longer.  Choose the security level:");
	char *eraseLevels[] = { _("basic (clear only)"), _("secure"),
		_("more secure"), _("most secure") };

	if (graphics)
	{
		securityLevel = windowNewRadioDialog(window, _("Erase security level"),
			chooseString, eraseLevels, 4, 0);
	}
	else
	{
		securityLevel = vshCursorMenu(chooseString, eraseLevels, 4,
			0 /* no max rows */, 0 /* selected */);
	}

	if (securityLevel >= 0)
		securityLevel = ((securityLevel * 2) + 1);

	return (securityLevel);
}


static int eraseData(disk *theDisk, unsigned startSector, unsigned numSectors,
	int securityLevel)
{
	// Securely erase data sectors.

	int status = 0;
	unsigned remainingSectors = numSectors;
	unsigned doSectors = 0;
	objectKey progressDialog = NULL;
	unsigned startSeconds = rtcUptimeSeconds();;
	unsigned remainingSeconds = 0;
	progress prog;

	memset((void *) &prog, 0, sizeof(progress));
	prog.numTotal = numSectors;
	strcpy((char *) prog.statusMessage,
		_("Time remaining: ?? hours ?? minutes"));
	prog.canCancel = 1;

	if (graphics)
	{
		progressDialog =
			windowNewProgressDialog(window, _("Erasing data..."), &prog);
	}
	else
	{
		printf("%s", _("\nErasing data... (press 'Q' to cancel)\n"));
		vshProgressBar(&prog);
	}

	while (remainingSectors)
	{
		doSectors = min(remainingSectors, CYLSECTS(theDisk));

		status = diskEraseSectors(theDisk->name, startSector, doSectors,
			securityLevel);
		if (status < 0)
			break;

		if (prog.cancel)
		{
			status = ERR_CANCELLED;
			break;
		}

		remainingSectors -= doSectors;
		startSector += doSectors;

		if (lockGet(&prog.progLock) >= 0)
		{
			prog.numFinished = (numSectors - remainingSectors);
			if (numSectors >= 100)
			{
				prog.percentFinished = (prog.numFinished /
					(numSectors / 100));
			}
			else
			{
				prog.percentFinished = ((prog.numFinished * 100) / numSectors);
			}

			remainingSeconds = (((rtcUptimeSeconds() - startSeconds) *
				(remainingSectors / doSectors)) /
					(prog.numFinished / doSectors));

			formatTime((char *) prog.statusMessage, remainingSeconds);

			lockRelease(&prog.progLock);
		}
	}

	if (graphics && progressDialog)
		windowProgressDialogDestroy(progressDialog);
	else
		vshProgressBarDestroy(&prog);

	return (status);
}


static void diskInfo(void)
{
	// Show info about a disk

	char *buff = NULL;
	rawGeom geom;
	char tmp[80];
	int count;

	buff = malloc(1024);
	if (!buff)
		return;

	sprintf(buff, _("DISK %s INFO:\n\n"), table->disk->name);

	sprintf((buff + strlen(buff)), _("Label type: %s\n"),
		labelType2String(table->label->type));
	sprintf((buff + strlen(buff)), _("Total sectors: %llu\n"),
		table->disk->numSectors);
	sprintf((buff + strlen(buff)), _("%u MB, %u bytes/sec\n"),
		sectorsToMegabytes(table->disk, table->disk->numSectors,
			0 /* round naturally */), table->disk->sectorSize);
	sprintf((buff + strlen(buff)), _("Cyls/Hds/Sects: %u/%u/%u\n"),
		table->disk->cylinders, table->disk->heads,
		table->disk->sectorsPerCylinder);
	sprintf((buff + strlen(buff)), _("First usable sector: %llu\n"),
		table->label->firstUsableSect);
	sprintf((buff + strlen(buff)), _("Last usable sector: %llu\n"),
		table->label->lastUsableSect);

	for (count = 0; count < table->numSlices; count ++)
	{
		if (isSliceUsed(table, count))
		{
			sprintf((buff + strlen(buff)), _("\nPartition %d:\n"),
				table->slices[count].raw.order);

			tmp[0] = '\0';

			if (table->slices[count].raw.type == partition_primary)
				strcat((tmp + strlen(tmp)), _("Primary"));
			else if (table->slices[count].raw.type == partition_logical)
				strcat((tmp + strlen(tmp)), _("Logical"));

			if (table->label->flags & LABELFLAG_USETAGS)
			{
				sprintf((tmp + strlen(tmp)), _(", tag=%02x"),
					table->slices[count].raw.tag);
			}
			else if (table->label->flags & LABELFLAG_USEGUIDS)
			{
				strcat((tmp + strlen(tmp)), _(", GUID="));
				guidString((tmp + strlen(tmp)),
					&table->slices[count].raw.typeGuid);
			}

			if ((table->label->flags & LABELFLAG_USEACTIVE) &&
				(table->slices[count].raw.flags & SLICEFLAG_BOOTABLE))
			{
				strcat((tmp + strlen(tmp)), _(", active/bootable "));
			}

			if (tmp[0])
				sprintf((buff + strlen(buff)), "  %s\n", tmp);

			getChsValues(table->disk, &table->slices[count].raw, &geom);
			sprintf((buff + strlen(buff)),
				_("  Starting Cyl/Hd/Sect: %u/%u/%u, ending: %u/%u/%u\n"
				"  Logical start: %llu, end: %llu, size: %llu"),
				geom.startCylinder, geom.startHead, geom.startSector,
				geom.endCylinder, geom.endHead, geom.endSector,
				table->slices[count].raw.startSector,
				(table->slices[count].raw.startSector +
					table->slices[count].raw.numSectors - 1),
				table->slices[count].raw.numSectors);
		}
	}

	if (graphics)
	{
		windowNewInfoDialog(window, _("Info"), buff);
	}
	else
	{
		printf("\n%s\n", buff);
		pause();
	}

	free(buff);
}


static void erase(int wholeDisk)
{
	// Securely erase a slice or disk.  If it's a slice, the slice can be a
	// partition or empty space.

	int status = 0;
	const char *chooseString = _("Erase the partition or the whole disk?:");
	char *eraseLevels[] = { _("partition"), _("whole disk") };
	slice *slc = NULL;
	int securityLevel = 0;
	char tmpChar[80];

	if (wholeDisk < 0)
	{
		if (graphics)
		{
			wholeDisk = windowNewRadioDialog(window, _("Erase partition or "
				"disk?"), chooseString, eraseLevels, 2, 0);
		}
		else
		{
			wholeDisk = vshCursorMenu(chooseString, eraseLevels, 2,
				 0 /* no max rows */, 0 /* selected */);
		}

		if (wholeDisk < 0)
			return;
	}

	// Check for mounted filesystems
	if (wholeDisk)
	{
		status = mountedCheckDisk();
		if (status < 0)
			return;
	}
	else
	{
		if (table->changesPending)
		{
			error("%s", _("A partition erase cannot be undone, and it is "
				"required that you\nwrite your other changes to disk "
				"before continuing."));
			return;
		}

		slc = &table->slices[table->selectedSlice];

		if (isSliceUsed(table, table->selectedSlice))
		{
			status = mountedCheckSlice(slc);
			if (status < 0)
				return;
		}
	}

	// Get the security level
	securityLevel = chooseSecurityLevel();
	if (securityLevel < 0)
		return;

	if (wholeDisk)
	{
		sprintf(tmpChar, _("Erase whole disk %s"), table->disk->name);
	}
	else
	{
		if (isSliceUsed(table, table->selectedSlice))
			sprintf(tmpChar, _("Erase partition %s"), slc->showSliceName);
		else if (isSliceReserved(table, table->selectedSlice))
			strcpy(tmpChar, _("Erase this reserved space"));
		else
			strcpy(tmpChar, _("Erase this empty space"));
	}

	strcat(tmpChar, _("?\n(This change cannot be undone)"));
	if (!yesOrNo(tmpChar))
		return;

	// Erase the data
	if (wholeDisk)
	{
		status = eraseData(table->disk, 0, table->disk->numSectors,
			securityLevel);
	}
	else
	{
		status = eraseData(table->disk, slc->raw.startSector,
			slc->raw.numSectors, securityLevel);
	}

	// Tell the kernel to re-examine the partition tables
	diskReadPartitions(table->disk->name);

	if (status < 0)
	{
		if (status != ERR_CANCELLED)
			error(_("Error %d erasing %s"), status,
				(wholeDisk? _("disk") : _("partition")));
	}
	else
	{
		if (graphics)
		{
			windowNewInfoDialog(window, _("Success"), _("Erase complete"));
		}
		else
		{
			printf("%s", _("Erase complete\n"));
			pause();
		}
	}

	table->changesPending = 0;

	selectDisk(table->disk);

	return;
}


static void newLabel(void)
{
	// Write a new, empty disk label (partition table) of the desired type

	const char *labelString = _("Create a new disk label? (deletes all "
		"partitions):");
	char *typeStrings[] = { _("MS-DOS"), _("GPT") };
	int typeChoice = 0;
	diskLabel *label = NULL;

	if (graphics)
	{
		typeChoice = windowNewRadioDialog(window, _("New disk label"),
			labelString, typeStrings, 2, 0);
	}
	else
	{
		typeChoice = vshCursorMenu(labelString, typeStrings, 2,
			0 /* no max rows */, 0 /* selected */);
	}

	if (typeChoice < 0)
		return;

	switch (typeChoice)
	{
		case 0:
		default:
			label = msdosLabel;
			break;

		case 1:
			label = gptLabel;
			break;
	}

	if (label->create)
		label->create(table->disk);

	// Tell the kernel to re-examine the partition tables
	diskReadPartitions(table->disk->name);

	scanDisks();

	if (graphics)
		windowComponentSetData(diskList, diskListParams, numberDisks,
			1 /* redraw */);

	table->changesPending = 0;

	selectDisk(table->disk);

	return;
}


static void makeSliceListHeader(void)
{
	// The header that goes above the slice list.  Name string in graphics
	// and text modes

	const char *string = NULL;
	int count;

	for (count = 0; count < SLICESTRING_LENGTH; count ++)
		sliceListHeader[count] = ' ';
	count = 0;
	#ifdef PARTLOGIC
		strncpy(sliceListHeader, "#", 1);
	#else
	strncpy(sliceListHeader, _("Disk"), 4);
	#endif
	count += SLICESTRING_DISKFIELD_WIDTH;
	string = _("Partition");
	strncpy((sliceListHeader + count), string, strlen(string));
	count += SLICESTRING_LABELFIELD_WIDTH;
	string = _("Filesystem");
	strncpy((sliceListHeader + count), string, strlen(string));
	count += SLICESTRING_FSTYPEFIELD_WIDTH;
	string = _("Start (MB)");
	strncpy((sliceListHeader + count), string, strlen(string));
	count += SLICESTRING_STARTFIELD_WIDTH;
	string = _("Size (MB)");
	strncpy((sliceListHeader + count), string, strlen(string));
	count += SLICESTRING_SIZEFIELD_WIDTH;
	string = _("Attributes");
	strncpy((sliceListHeader + count), string, strlen(string));
	return;
}


static void eventHandler(objectKey key, windowEvent *event)
{
	int selected = -1;
	int redisplay = 0;
	int count;

	if (key == window)
	{
		if (event->type == EVENT_WINDOW_CLOSE)
		{
			// Window being closed by a GUI event.
			quit(0, 0);
		}

		else if (event->type == EVENT_WINDOW_RESIZE)
		{
			// Window resize.  Get the canvas sizes
			canvasWidth = windowComponentGetWidth(canvas);
			canvasHeight = windowComponentGetHeight(canvas);
			redisplay = 1;
		}
	}

	// Check for menu events

	// Check for 'file' menu events
	else if (key == fileMenuContents.items[FILEMENU_WRITE].key)
	{
		if (event->type & EVENT_SELECTION)
		{
			writeChanges(table, 1);
			redisplay = 1;
		}
	}

	else if (key == fileMenuContents.items[FILEMENU_UNDO].key)
	{
		if (event->type & EVENT_SELECTION)
		{
			undo();
			redisplay = 1;
		}
	}

	else if (key == fileMenuContents.items[FILEMENU_RESTOREBACKUP].key)
	{
		if (event->type & EVENT_SELECTION)
		{
			restoreBackup();
			redisplay = 1;
		}
	}

	else if (key == fileMenuContents.items[FILEMENU_QUIT].key)
	{
		if (event->type & EVENT_SELECTION)
			quit(0, 0);
	}

	// Check for 'disk' menu events

	else if (key == diskMenuContents.items[DISKMENU_COPYDISK].key)
	{
		if (event->type & EVENT_SELECTION)
		{
			copyDisk();
			redisplay = 1;
		}
	}

	else if (key == diskMenuContents.items[DISKMENU_PARTORDER].key)
	{
		if (event->type & EVENT_SELECTION)
		{
			changePartitionOrder();
			redisplay = 1;
		}
	}

	else if (key == diskMenuContents.items[DISKMENU_SIMPLEMBR].key)
	{
		if (event->type & EVENT_SELECTION)
		{
			writeSimpleMbr();
			redisplay = 1;
		}
	}

	else if (key == diskMenuContents.items[DISKMENU_BOOTMENU].key)
	{
		if (event->type & EVENT_SELECTION)
		{
			mbrBootMenu();
			redisplay = 1;
		}
	}

	else if (key == diskMenuContents.items[DISKMENU_DISKINFO].key)
	{
		if (event->type & EVENT_SELECTION)
			diskInfo();
	}

	else if (key == diskMenuContents.items[DISKMENU_ERASEDISK].key)
	{
		if (event->type & EVENT_SELECTION)
		{
			erase(1);
			redisplay = 1;
		}
	}

	else if (key == diskMenuContents.items[DISKMENU_NEWLABEL].key)
	{
		if (event->type & EVENT_SELECTION)
		{
			newLabel();
			redisplay = 1;
		}
	}

	// Check for 'partition' menu events

	else if (key == partMenuContents.items[PARTMENU_COPY].key)
	{
		if (event->type & EVENT_SELECTION)
			copyPartition(table->selectedSlice);
	}

	else if (key == partMenuContents.items[PARTMENU_PASTE].key)
	{
		if (event->type & EVENT_SELECTION)
		{
			pastePartition(table->selectedSlice);
			redisplay = 1;
		}
	}

	else if (key == partMenuContents.items[PARTMENU_SETACTIVE].key)
	{
		if (event->type & EVENT_SELECTION)
		{
			setActive(table->selectedSlice);
			redisplay = 1;
		}
	}

	else if (key == partMenuContents.items[PARTMENU_DELETE].key)
	{
		if (event->type & EVENT_SELECTION)
		{
			delete(table->selectedSlice);
			redisplay = 1;
		}
	}

	else if (key == partMenuContents.items[PARTMENU_FORMAT].key)
	{
		if (event->type & EVENT_SELECTION)
		{
			format(table->selectedSlice);
			redisplay = 1;
		}
	}

	else if (key == partMenuContents.items[PARTMENU_DEFRAG].key)
	{
		if (event->type & EVENT_SELECTION)
		{
			defragment(table->selectedSlice);
			redisplay = 1;
		}
	}

	else if (key == partMenuContents.items[PARTMENU_RESIZE].key)
	{
		if (event->type & EVENT_SELECTION)
		{
			resize(table->selectedSlice);
			redisplay = 1;
		}
	}

	else if (key == partMenuContents.items[PARTMENU_HIDE].key)
	{
		if (event->type & EVENT_SELECTION)
		{
			hide(table->selectedSlice);
			redisplay = 1;
		}
	}

	else if (key == partMenuContents.items[PARTMENU_INFO].key)
	{
		if (event->type & EVENT_SELECTION)
			sliceInfo(table->selectedSlice);
	}

	else if (key == partMenuContents.items[PARTMENU_LISTTYPES].key)
	{
		if (event->type & EVENT_SELECTION)
			listTypes();
	}

	else if (key == partMenuContents.items[PARTMENU_MOVE].key)
	{
		if (event->type & EVENT_SELECTION)
		{
			move(table->selectedSlice);
			redisplay = 1;
		}
	}

	else if (key == partMenuContents.items[PARTMENU_CREATE].key)
	{
		if (event->type & EVENT_SELECTION)
		{
			create(table->selectedSlice);
			redisplay = 1;
		}
	}

	else if (key == partMenuContents.items[PARTMENU_DELETEALL].key)
	{
		if (event->type & EVENT_SELECTION)
		{
			deleteAll();
			redisplay = 1;
		}
	}

	else if (key == partMenuContents.items[PARTMENU_SETTYPE].key)
	{
		if (event->type & EVENT_SELECTION)
		{
			setType(table->selectedSlice);
			redisplay = 1;
		}
	}

	else if (key == partMenuContents.items[PARTMENU_ERASE].key)
	{
		if (event->type & EVENT_SELECTION)
		{
			erase(0);
			redisplay = 1;
		}
	}

	// Check for changes to our disk list
	else if (key == diskList)
	{
		if ((event->type & EVENT_MOUSE_DOWN) ||
			(event->type & EVENT_KEY_DOWN))
		{
			windowComponentGetSelected(diskList, &selected);
			if ((selected >= 0) && (selected != table->diskNumber))
			{
				if (selectDisk(&disks[selected]) < 0)
					windowComponentSetSelected(diskList, table->diskNumber);

				redisplay = 1;
			}
		}
	}

	// Check for changes to our 'show removable' checkbox
	else if (key == removableCheckbox)
	{
		if (event->type & EVENT_SELECTION)
		{
			scanDisks();
			windowComponentSetData(diskList, diskListParams, numberDisks,
				1 /* redraw */);
			selectDisk(&disks[0]);
			windowComponentSetSelected(diskList, 0);
			redisplay = 1;
		}
	}

	// Check for clicks or cursor keys on our canvas diagram
	else if (key == canvas)
	{
		if (event->type & EVENT_MOUSE_DOWN)
		{
			for (count = 0; count < table->numSlices; count ++)
			{
				if ((event->xPosition > table->slices[count].pixelX) &&
					(event->xPosition < (table->slices[count].pixelX +
						table->slices[count].pixelWidth)))
				{
					selected = count;
					break;
				}
			}

			if (selected >= 0)
			{
				table->selectedSlice = selected;
				redisplay = 1;
			}
		}

		else if (event->type & EVENT_KEY_DOWN)
		{
			// Respond to cursor left or right
			switch (event->key)
			{
				case keyLeftArrow:
					// LEFT cursor key
					if (table->selectedSlice)
					{
						table->selectedSlice -= 1;
						redisplay = 1;
					}
					break;

				case keyRightArrow:
					// RIGHT cursor key
					if (table->selectedSlice < (table->numSlices - 1))
					{
						table->selectedSlice += 1;
						redisplay = 1;
					}
					break;

				default:
					break;
			}
		}
	}

	// Check for changes to our slice list
	else if (key == sliceList)
	{
		if ((event->type & EVENT_MOUSE_DOWN) ||
			(event->type & EVENT_KEY_DOWN))
		{
			windowComponentGetSelected(sliceList, &selected);
			if (selected >= 0)
			{
				table->selectedSlice = selected;
				redisplay = 1;
			}
		}
	}

	// Check for button clicks

	else if (key == writeButton)
	{
		if (event->type == EVENT_MOUSE_LEFTUP)
		{
			writeChanges(table, 1);
			redisplay = 1;
		}
	}

	else if (key == undoButton)
	{
		if (event->type == EVENT_MOUSE_LEFTUP)
		{
			undo();
			redisplay = 1;
		}
	}

	else if (key == setActiveButton)
	{
		if (event->type == EVENT_MOUSE_LEFTUP)
		{
			setActive(table->selectedSlice);
			redisplay = 1;
		}
	}

	else if (key == deleteButton)
	{
		if (event->type == EVENT_MOUSE_LEFTUP)
		{
			delete(table->selectedSlice);
			redisplay = 1;
		}
	}

	else if (key == formatButton)
	{
		if (event->type == EVENT_MOUSE_LEFTUP)
		{
			format(table->selectedSlice);
			redisplay = 1;
		}
	}

	else if (key == defragButton)
	{
		if (event->type == EVENT_MOUSE_LEFTUP)
		{
			defragment(table->selectedSlice);
			redisplay = 1;
		}
	}

	else if (key == hideButton)
	{
		if (event->type == EVENT_MOUSE_LEFTUP)
		{
			hide(table->selectedSlice);
			redisplay = 1;
		}
	}

	else if (key == infoButton)
	{
		if (event->type == EVENT_MOUSE_LEFTUP)
			sliceInfo(table->selectedSlice);
	}

	else if (key == moveButton)
	{
		if (event->type == EVENT_MOUSE_LEFTUP)
		{
			move(table->selectedSlice);
			redisplay = 1;
		}
	}

	else if (key == createButton)
	{
		if (event->type == EVENT_MOUSE_LEFTUP)
		{
			create(table->selectedSlice);
			redisplay = 1;
		}
	}

	else if (key == deleteAllButton)
	{
		if (event->type == EVENT_MOUSE_LEFTUP)
		{
			deleteAll();
			redisplay = 1;
		}
	}

	else if (key == resizeButton)
	{
		if (event->type == EVENT_MOUSE_LEFTUP)
		{
			resize(table->selectedSlice);
			redisplay = 1;
		}
	}

	if (redisplay)
		display();
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


static void handleMenuEvents(windowMenuContents *contents)
{
	int count;

	for (count = 0; count < contents->numItems; count ++)
		windowRegisterEventHandler(contents->items[count].key, &eventHandler);
}


static void constructWindow(void)
{
	// If we are in graphics mode, make a window rather than operating on the
	// command line.

	componentParameters params;
	image iconImage;
	objectKey container = NULL;
	static char *iconName = PATH_SYSTEM_ICONS "/diskman.ico";
	int widest = 0;
	int count;

	// Create a new window, with small, arbitrary size and location
	window = windowNew(processId, programName);
	if (!window)
		return;

	memset(&params, 0, sizeof(componentParameters));

	// Create the top menu bar
	objectKey menuBar = windowNewMenuBar(window, &params);

	// Create the top 'file' menu
	initMenuContents(&fileMenuContents);
	fileMenu = windowNewMenu(window, menuBar, _("File"), &fileMenuContents,
		&params);
	handleMenuEvents(&fileMenuContents);

	// Create the top 'disk' menu
	initMenuContents(&diskMenuContents);
	diskMenu = windowNewMenu(window, menuBar, _("Disk"), &diskMenuContents,
		&params);
	handleMenuEvents(&diskMenuContents);

	// Create the top 'partition' menu
	initMenuContents(&partMenuContents);
	partMenu = windowNewMenu(window, menuBar, _("Partition"),
		&partMenuContents, &params);
	handleMenuEvents(&partMenuContents);

	params.gridWidth = 1;
	params.gridHeight = 1;
	params.padTop = 5;
	params.orientationX = orient_left;
	params.orientationY = orient_middle;

	// Create a container for the disk icon image, the disk list, and the
	// 'show removable' checkbox
	container = windowNewContainer(window, "titleContainer", &params);

	params.padBottom = 5;
	params.padLeft = 5;
	params.padRight = 5;

	if (container)
	{
		// Try to load an icon image to go at the top of the window
		if ((fileFind(iconName, NULL) >= 0) &&
			(imageLoad(iconName, 64, 64, &iconImage) >= 0))
		{
			// Create an image component from it, and add it to the container
			iconImage.transColor.green = 255;
			params.flags |= (WINDOW_COMPFLAG_FIXEDWIDTH |
				WINDOW_COMPFLAG_FIXEDHEIGHT);
			windowNewImage(container, &iconImage, draw_alphablend, &params);
			imageFree(&iconImage);
		}

		// Make a list for the disks
		params.gridX++;
		params.flags &= ~(WINDOW_COMPFLAG_FIXEDWIDTH |
			WINDOW_COMPFLAG_FIXEDHEIGHT);
		diskList = windowNewList(container, windowlist_textonly, numberDisks,
			1, 0, diskListParams, numberDisks, &params);
		windowRegisterEventHandler(diskList, &eventHandler);
		windowContextSet(diskList, diskMenu);

		// Make a checkbox for showing removable disks
		params.gridY++;
		params.padTop = 0;
		params.padBottom = 0;
		removableCheckbox = windowNewCheckbox(container,
			_("Show removable disks"), &params);
		windowRegisterEventHandler(removableCheckbox, &eventHandler);
	}

	if (windowComponentGetWidth(container) > widest)
		widest = windowComponentGetWidth(container);

	// Get a canvas for drawing the visual representation
	params.gridX = 0;
	params.gridY++;
	params.padTop = 5;
	params.padBottom = 5;
	params.flags |= WINDOW_COMPFLAG_CANFOCUS;
	canvasWidth = ((graphicGetScreenWidth() * 2) / 3);
	canvas = windowNewCanvas(window, canvasWidth, canvasHeight, &params);
	windowRegisterEventHandler(canvas, &eventHandler);
	windowContextSet(canvas, partMenu);

	if (windowComponentGetWidth(canvas) > widest)
		widest = windowComponentGetWidth(canvas);

	// Put a header label over the slice list
	params.gridY++;
	params.flags &= ~WINDOW_COMPFLAG_CANFOCUS;
	params.flags |= WINDOW_COMPFLAG_FIXEDHEIGHT;
	params.padBottom = 0;
	params.font = fontGet(FONT_FAMILY_LIBMONO, FONT_STYLEFLAG_FIXED, 10, NULL);
	windowNewTextLabel(window, sliceListHeader, &params);

	// Make a list for the slices
	params.gridY++;
	params.flags &= ~WINDOW_COMPFLAG_FIXEDHEIGHT;
	listItemParameters tmpListParams;
	for (count = 0; count < WINDOW_MAX_LABEL_LENGTH; count ++)
		tmpListParams.text[count] = ' ';
	tmpListParams.text[WINDOW_MAX_LABEL_LENGTH - 1] = '\0';
	sliceList = windowNewList(window, windowlist_textonly, 8, 1, 0,
		&tmpListParams, 1, &params);
	windowRegisterEventHandler(sliceList, &eventHandler);
	windowContextSet(sliceList, partMenu);

	if (windowComponentGetWidth(sliceList) > widest)
		widest = windowComponentGetWidth(sliceList);

	// A container for the buttons
	params.gridY++;
	params.padBottom = 5;
	params.orientationX = orient_center;
	params.flags |= WINDOW_COMPFLAG_FIXEDHEIGHT;
	container = windowNewContainer(window, "buttonContainer", &params);
	if (container)
	{
		params.gridY = 0;
		params.orientationX = orient_left;
		params.padBottom = 0;
		params.font = fontGet(FONT_FAMILY_ARIAL, FONT_STYLEFLAG_BOLD, 10,
			NULL);
		createButton = windowNewButton(container, _("Create"), NULL, &params);
		windowRegisterEventHandler(createButton, &eventHandler);

		params.gridX++;
		setActiveButton = windowNewButton(container, _("Set active"), NULL,
			&params);
		windowRegisterEventHandler(setActiveButton, &eventHandler);

		params.gridX++;
		moveButton = windowNewButton(container, _("Move"), NULL, &params);
		windowRegisterEventHandler(moveButton, &eventHandler);

		params.gridX++;
		defragButton = windowNewButton(container, _("Defragment"), NULL,
			&params);
		windowRegisterEventHandler(defragButton, &eventHandler);

		params.gridX++;
		formatButton = windowNewButton(container, _("Format"), NULL, &params);
		windowRegisterEventHandler(formatButton, &eventHandler);

		params.gridX++;
		deleteAllButton = windowNewButton(container, _("Delete all"), NULL,
			&params);
		windowRegisterEventHandler(deleteAllButton, &eventHandler);

		params.gridX = 0;
		params.gridY++;
		params.padTop = 0;
		deleteButton = windowNewButton(container, _("Delete"), NULL, &params);
		windowRegisterEventHandler(deleteButton, &eventHandler);

		params.gridX++;
		hideButton = windowNewButton(container, _("Hide/unhide"), NULL,
			&params);
		windowRegisterEventHandler(hideButton, &eventHandler);

		params.gridX++;
		infoButton = windowNewButton(container, _("Info"), NULL, &params);
		windowRegisterEventHandler(infoButton, &eventHandler);

		params.gridX++;
		resizeButton = windowNewButton(container, _("Resize"), NULL, &params);
		windowRegisterEventHandler(resizeButton, &eventHandler);

		params.gridX++;
		undoButton = windowNewButton(container, _("Undo"), NULL, &params);
		windowRegisterEventHandler(undoButton, &eventHandler);

		params.gridX++;
		writeButton = windowNewButton(container, _("Write changes"), NULL,
			&params);
		windowRegisterEventHandler(writeButton, &eventHandler);
	}

	if (windowComponentGetWidth(container) > widest)
		widest = windowComponentGetWidth(container);

	// Adjust the canvas width so that it's at least the width of the widest
	// major component
	if (widest > canvasWidth)
	{
		canvasWidth = widest;
		windowComponentSetWidth(canvas, canvasWidth);
	}

	// Register an event handler to catch window close events
	windowRegisterEventHandler(window, &eventHandler);

	// Go
	windowSetVisible(window, 1);

	return;
}


static int textMenu(void)
{
	int status = 0;
	char optionString[80];
	int isPartition, canDefrag, canHide, canActivate, canCreate;
	int topRow, bottomRow;

	// This is the main menu bit
	while (1)
	{
		// Print out the partitions
		display();

		isPartition = 0;
		canDefrag = 0;
		canHide = 0;
		canActivate = 0;
		canCreate = 0;

		if (isSliceUsed(table, table->selectedSlice))
		{
			isPartition = 1;

			if (table->slices[table->selectedSlice].opFlags & FS_OP_DEFRAG)
				canDefrag = 1;

			if (table->label->canHide)
				canHide = table->label
					->canHide(&table->slices[table->selectedSlice]);

			if (table->label->flags & LABELFLAG_USEACTIVE)
				canActivate = 1;
		}
		else if (!isSliceReserved(table, table->selectedSlice))
		{
			if (table->label->canCreateSlice(table->slices, table->numSlices,
				table->selectedSlice) != partition_none)
			{
				canCreate = 1;
			}
		}

		// Print out the menu choices.  First column.
		printf("\n");
		topRow = textGetRow();
		printf("%s%s%s%s%s%s%s%s%s%s%s%s%s%s",
			(canActivate? _("[A] Set active\n") : ""),
			(haveUsedSlices()? _("[B] Partition order\n") : ""),
			_("[C] Copy disk\n"),
			(isPartition? _("[D] Delete\n") : ""),
			(isPartition? _("[E] Copy partition\n") : ""),
			(isPartition? _("[F] Format\n") : ""),
			(canDefrag? _("[G] Defragment\n") : ""),
			(canHide? _("[H] Hide/Unhide\n") : ""),
			_("[I] Info\n"),
			_("[J] New disk label\n"),
			_("[K] Disk info\n"),
			_("[L] List types\n"),
			(isPartition? _("[M] Move\n") : ""),
			(canCreate? _("[N] Create new\n") : ""));
		bottomRow = textGetRow();

		// Second column
		textSetRow(topRow);
		#define COL 24
		textSetColumn(COL);
		printf("%s", haveUsedSlices()? _("[O] Delete all\n") : "");
		textSetColumn(COL);
		printf("%s", (canCreate && clipboardSliceValid)?
			 _("[P] Paste partition\n") : "");
		textSetColumn(COL);
		printf("%s", _("[Q] Quit\n"));
		textSetColumn(COL);
		printf("%s", table->backupAvailable? _("[R] Restore backup\n") : "");
		textSetColumn(COL);
		printf("%s", _("[S] Select disk\n"));
		textSetColumn(COL);
		printf("%s", isPartition? _("[T] Set type\n") : "");
		textSetColumn(COL);
		printf("%s", table->changesPending? _("[U] Undo\n") : "");
		textSetColumn(COL);
		printf("%s", _("[V] Erase\n"));
		textSetColumn(COL);
		printf("%s", table->changesPending? _("[W] Write changes\n") : "");
		textSetColumn(COL);
		printf("%s", _("[X] Write basic MBR\n"));
		textSetColumn(COL);
		printf("%s", _("[Y] MBR boot menu\n"));
		textSetColumn(COL);
		printf("%s", isPartition? _("[Z] Resize\n") : "");
		if (bottomRow > textGetRow())
			textSetRow(bottomRow);
		textSetColumn(0);

		if (table->changesPending)
			printf(_("  -== %d changes pending ==-\n"), table->changesPending);
		printf("-> ");

		// Construct the string of allowable options, corresponding to what is
		// shown above.
		sprintf(optionString, "%s%sCc%s%s%s%s%sIiJjKkLl%s%s%s%sQq%sSs%s%sVv%s"
			"XxYyZz",
			(canActivate? "Aa" : ""),
			(haveUsedSlices()? "Bb" : ""),
			(isPartition? "Dd" : ""),
			(isPartition? "Ee" : ""),
			(isPartition? "Ff" : ""),
			(canDefrag? "Gg" : ""),
			(canHide? "Hh" : ""),
			(isPartition? "Mm" : ""),
			(canCreate? "Nn" : ""),
			(haveUsedSlices()? "Oo" : ""),
			((canCreate && clipboardSliceValid)? "Pp" : ""),
			(table->backupAvailable? "Rr" : ""),
			(isPartition? "Tt" : ""),
			(table->changesPending? "Uu" : ""),
			(table->changesPending? "Ww" : ""));

		switch (readKey(optionString, 1))
		{
			case (char) ASCII_CRSRUP:
				// Cursor up.
				if (table->selectedSlice > 0)
					table->selectedSlice -= 1;
				continue;

			case (char) ASCII_CRSRDOWN:
				// Cursor down.
				if (table->selectedSlice < (table->numSlices - 1))
					table->selectedSlice += 1;
				continue;

			case 'a':
			case 'A':
				setActive(table->selectedSlice);
				continue;

			case 'b':
			case 'B':
				changePartitionOrder();
				continue;

			case 'c':
			case 'C':
				copyDisk();
				continue;

			case 'd':
			case 'D':
				delete(table->selectedSlice);
				continue;

			case 'e':
			case 'E':
				copyPartition(table->selectedSlice);
				continue;

			case 'f':
			case 'F':
				format(table->selectedSlice);
				continue;

			case 'g':
			case 'G':
				defragment(table->selectedSlice);
				continue;

			case 'h':
			case 'H':
				hide(table->selectedSlice);
				continue;

			case 'i':
			case 'I':
				sliceInfo(table->selectedSlice);
				continue;

			case 'j':
			case 'J':
				newLabel();
				continue;

			case 'k':
			case 'K':
				diskInfo();
				continue;

			case 'l':
			case 'L':
				listTypes();
				continue;

			case 'm':
			case 'M':
				move(table->selectedSlice);
				continue;

			case 'n':
			case 'N':
				create(table->selectedSlice);
				continue;

			case 'o':
			case 'O':
				deleteAll();
				continue;

			case 'p':
			case 'P':
				pastePartition(table->selectedSlice);
				continue;

			case 'q':
			case 'Q':
				return (status = 0);

			case 'r':
			case 'R':
				restoreBackup();
				continue;

			case 's':
			case 'S':
				status = queryDisk();
				if (status < 0)
				{
					error("%s", _("No disk selected.  Quitting."));
					quit(ERR_CANCELLED, 1);
				}
				continue;

			case 't':
			case 'T':
				setType(table->selectedSlice);
				continue;

			case 'u':
			case 'U':
				undo();
				continue;

			case 'v':
			case 'V':
				erase(-1);
				continue;

			case 'w':
			case 'W':
				writeChanges(table, 1);
				continue;

			case 'x':
			case 'X':
				writeSimpleMbr();
				continue;

			case 'y':
			case 'Y':
				mbrBootMenu();
				continue;

			case 'z':
			case 'Z':
				resize(table->selectedSlice);
				continue;

			default:
				continue;
		}
    }
}


int main(int argc, char *argv[])
{
	int status = 0;
	char opt;
	void *handle = NULL;
	int count;

	memset(&screen, 0, sizeof(textScreen));

	// Are graphics enabled?
	graphics = graphicsAreEnabled();

	setlocale(LC_ALL, getenv(ENV_LANG));
	textdomain("fdisk");

	// Run as Partition Logic?
	#ifdef PARTLOGIC
		programName = "Partition Logic";
	#else
		programName = _("Disk Manager");
	#endif

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
				quit(ERR_INVALID, 1);
		}
	}

	processId = multitaskerGetCurrentProcessId();

	// Check privilege level
	if (multitaskerGetProcessPrivilege(processId))
	{
		if (graphics)
			error("%s", PERM);
		else
			printf("\n%s\n\n", PERM);
		quit(ERR_PERMISSION, 1);
	}

	// Get memory for various things

	disks = malloc(DISK_MAXDEVICES * sizeof(disk));
	table = malloc(sizeof(partitionTable));
	if (!disks || !table)
		quit(ERR_MEMORY, 1);

	// Find out whether our temp or backup directories are on a read-only
	// filesystem
	if (!fileGetDisk(PATH_TEMP, disks) && !disks->readOnly)
		if (!fileGetDisk(PATH_SYSTEM_BOOT, disks) && !disks->readOnly)
			readOnly = 0;

	// Get the disk label structures for all the types we support
	gptLabel = getLabelGpt();
	msdosLabel = getLabelMsdos();

	// Gather the disk info
	status = scanDisks();
	if (status < 0)
	{
		if (status == ERR_NOSUCHENTRY)
			error("%s", _("No hard disks registered"));
		else
			error("%s", _("Problem getting hard disk info"));

		quit(status, 1);
	}

	// See whether the NTFS resizing library is available
	handle = dlopen("libntfs.so", 0);
	if (handle)
	{
		ntfsFormat = dlsym(handle, "ntfsFormat");
		ntfsGetResizeConstraints = dlsym(handle, "ntfsGetResizeConstraints");
		ntfsResize = dlsym(handle, "ntfsResize");
	}

	makeSliceListHeader();

	if (graphics)
	{
		constructWindow();
	}
	else
	{
		textScreenSave(&screen);
		printBanner();
	}

	// The user can specify the disk name as an argument.  Try to see
	// whether they did so.
	if (argc > 1)
	{
		for (count = 0; count < numberDisks; count ++)
		{
			if (!strcmp(disks[count].name, argv[argc - 1]))
			{
				selectDisk(&disks[count]);
				break;
			}
		}
	}

	if (!table->disk)
	{
		// If we're in text mode, the user must first select a disk
		if (!graphics && (numberDisks > 1))
		{
			status = queryDisk();
			if (status < 0)
			{
				printf("%s", _("\n\nNo disk selected.  Quitting.\n\n"));
				quit(status, 1);
			}
		}
		else
		{
			status = selectDisk(&disks[0]);
			if (status < 0)
				quit(status, 1);
		}
	}

	if (graphics)
	{
		display();
		windowGuiRun();
		status = 0;
	}
	else
	{
		status = textMenu();
		textScreenRestore(&screen);
	}

	quit(status, 1);
	// Keep the compiler happy
	return (status);
}

