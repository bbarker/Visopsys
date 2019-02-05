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
//  msdos.c
//

// This code does operations specific to MS-DOS-labelled disks.

#include "fdisk.h"
#include "msdos.h"
#include <errno.h>
#include <libintl.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/api.h>
#include <sys/gpt.h>

#define _(string) gettext(string)

// Forward declaration
diskLabel msdosLabel;


static inline int checkSignature(unsigned char *sectorData)
{
	// Returns 1 if the buffer contains an MS-DOS signature.

	msdosMbr *mbr = (msdosMbr *) sectorData;

	if (mbr->bootSig == MSDOS_BOOT_SIGNATURE)
		// We'll say this has an MS-DOS signature.
		return (1);
	else
		// No signature.  Return 0.
		return (0);
}


static int doReadTable(const disk *theDisk, unsigned sector,
	unsigned extendedStart, rawSlice *slices, int *numSlices)
{
	// This function (recursively) reads partition table sectors and populates
	// the array of raw slices.

	int status = 0;
	unsigned char *sectorData = NULL;
	int maxEntries = DISK_MAX_PRIMARY_PARTITIONS;
	msdosTable *table = NULL;
	int count;

	sectorData = malloc(theDisk->sectorSize);
	if (!sectorData)
		return (status = ERR_MEMORY);

	// Read the first sector.
	status = diskReadSectors(theDisk->name, sector, 1, sectorData);
	if (status < 0)
	{
		error(_("Couldn't read partition table sector %u"), sector);
		free(sectorData);
		return (status);
	}

	if (!checkSignature(sectorData))
	{
		error(_("Table at %u has no signature"), sector);
		free(sectorData);
		return (status = ERR_INVALID);
	}

	// Set the pointer to the start of partition records in the table
	table = (msdosTable *)(sectorData + 0x01BE);

	// If this is not the first partition table, the maximum number of entries
	// is 2.
	if (sector)
		maxEntries = 2;

	// Loop through the partition entries and create slices for them.
	for (count = 0; count < maxEntries; count ++)
	{
		// If this is an entry for an extended partition, skip it for the
		// moment.
		if (MSDOSTAG_IS_EXTD(table->entries[count].tag))
			continue;

		// If the tag is NULL, and we are in the primary table, we have to
		// leave an empty slice.  Otherwise, we are finished.
		if (!table->entries[count].tag)
		{
			if (!sector)
			{
				memset(&slices[*numSlices], 0, sizeof(rawSlice));
				*numSlices += 1;
				continue;
			}
			else
			{
				break;
			}
		}

		// Assign the data fields in the appropriate slice

		slices[*numSlices].order = *numSlices;

		if (sector)
			slices[*numSlices].type = partition_logical;
		else
			slices[*numSlices].type = partition_primary;

		if (table->entries[count].driveActive >> 7)
			slices[*numSlices].flags = SLICEFLAG_BOOTABLE;

		// The MS-DOS partition tag.
		slices[*numSlices].tag = table->entries[count].tag;

		// The logical (LBA) start sector and number of sectors.
		slices[*numSlices].startSector = (table->entries[count].startLogical +
			sector);
		slices[*numSlices].numSectors = table->entries[count].sizeLogical;

		*numSlices += 1;
	}

	// Remove any 'trailing' empty slices
	while (*numSlices && !slices[*numSlices - 1].tag)
		*numSlices -= 1;

	// Loop through the entries one more time, looking for extended entries
	// (we skipped them, above).
	for (count = 0; count < maxEntries; count ++)
	{
		if (MSDOSTAG_IS_EXTD(table->entries[count].tag))
		{
			// This is an extended entry.  Recurse for it.
			if (sector)
			{
				status = doReadTable(theDisk,
					(table->entries[count].startLogical + extendedStart),
					extendedStart, slices, numSlices);
			}
			else
			{
				status = doReadTable(theDisk,
					table->entries[count].startLogical,
					table->entries[count].startLogical, slices, numSlices);
			}

			break;
		}
	}

	free(sectorData);
	return (status);
}


static void calcExtendedSize(rawSlice *extSlice, rawSlice *slices)
{
	// Given an array of slices beginning with a logical slice, calculate the
	// size of the extended partition to contain the logical slice and all
	// following logical slices.

	uquad_t start = extSlice->startSector;
	int count;

	extSlice->numSectors = 0;

	for (count = 0; count < DISK_MAX_PARTITIONS; count ++)
	{
		if (!slices[count].tag || (slices[count].type != partition_logical))
			break;
	}

	if (count)
	{
		extSlice->numSectors = ((slices[count - 1].startSector - start) +
			slices[count - 1].numSectors);
	}
}


static void formatTableEntry(const disk *theDisk, rawSlice *raw,
	msdosEntry *entry)
{
	rawGeom geom;

	memset(&geom, 0, sizeof(rawGeom));

	getChsValues(theDisk, raw, &geom);

	// Check whether our start or end cylinder values exceed the legal
	// maximum of 1023.  If so, set them to 1023.
	geom.startCylinder = min(geom.startCylinder, 1023);
	geom.endCylinder = min(geom.endCylinder, 1023);

	if (raw->flags & SLICEFLAG_BOOTABLE)
		entry->driveActive = 0x80;
	entry->startHead = (unsigned char) geom.startHead;
	entry->startCylSect = (unsigned char)(((geom.startCylinder & 0x300) >> 2) |
		(geom.startSector & 0x3F));
	entry->startCyl = (unsigned char)(geom.startCylinder & 0x0FF);
	entry->tag = raw->tag;
	entry->endHead = (unsigned char) geom.endHead;
	entry->endCylSect = (unsigned char)(((geom.endCylinder & 0x300) >> 2) |
		(geom.endSector & 0x3F));
	entry->endCyl = (unsigned char)(geom.endCylinder & 0x0FF);
	entry->startLogical = raw->startSector;
	entry->sizeLogical = raw->numSectors;
}


static int doWriteTable(const disk *theDisk, unsigned sector,
	unsigned extendedStart, rawSlice *slices)
{
	// This function (recursively) writes partition table sectors from the
	// array of slices.

	int status = 0;
	unsigned char *sectorData = NULL;
	msdosMbr *mbr = NULL;
	int maxEntries = MSDOS_TABLE_ENTRIES;
	int numEntries = 0;
	rawSlice tmpSlice;
	int count;

	sectorData = malloc(theDisk->sectorSize);
	if (!sectorData)
		return (status = ERR_MEMORY);

	// Read the specified sector.
	status = diskReadSectors(theDisk->name, sector, 1, sectorData);
	if (status < 0)
	{
		error(_("Couldn't read partition table sector %u"), sector);
		goto out;
	}

	mbr = (msdosMbr *) sectorData;

	// Clear the partition records in the table
	memset(&mbr->partTable, 0, sizeof(msdosTable));

	// If this is not the first partition table, the maximum number of entries
	// is 2.
	if (sector)
		maxEntries = 2;

	// Loop through the slices and create partition entries for them.
	for (count = 0; ((count < DISK_MAX_PARTITIONS) &&
		(numEntries < maxEntries)); count ++)
	{
		// Empty slot?
		if (!slices[count].tag)
		{
			// If we are in the primary table, continue.  Otherwise, we are
			// finished.
			if (!sector)
			{
				numEntries += 1;
				continue;
			}
			else
			{
				break;
			}
		}

		// Make a copy of the slice in case we need to change values
		memcpy(&tmpSlice, &slices[count], sizeof(rawSlice));

		// If the current slice is logical, and this is not the first entry
		// of an extended table, we need to create an extended entry for it
		// instead, and recurse.
		if ((tmpSlice.type == partition_logical) && (!sector || count))
		{
			// Create an extended entry to correspond to the logical entry
			// -- which we will to create in the next call.
			tmpSlice.tag = 0x0F;

			// The extended slice begins 1 sector before the logical slice
			tmpSlice.startSector -= 1;

			// Calculate the size to accommodate the logical slice and all
			// following logical slices
			calcExtendedSize(&tmpSlice, &slices[count]);

			// If this is an extended table, re-adjust the starting logical
			// starting sector value to reflect a position relative to the
			// start of the extended partition.
			if (sector)
				tmpSlice.startSector -= extendedStart;

			// Fill out the table entry
			formatTableEntry(theDisk, &tmpSlice,
				&mbr->partTable.entries[numEntries]);

			if (sector)
			{
				status = doWriteTable(theDisk,
					(mbr->partTable.entries[numEntries].startLogical +
					extendedStart), extendedStart, &slices[count]);
			}
			else
			{
				status = doWriteTable(theDisk,
					mbr->partTable.entries[numEntries].startLogical,
					mbr->partTable.entries[numEntries].startLogical,
					&slices[count]);
			}

			// If we are in the main table, skip to the end of all the logical
			// slices.  If we're not in the main table, we are finished.
			if (!sector)
			{
				while ((count < (DISK_MAX_PARTITIONS - 1)) &&
					(slices[count + 1].type == partition_logical))
				{
					count += 1;
				}
			}
			else
			{
				break;
			}
		}
		else
		{
			// If this is an extended table, re-adjust the starting logical
			// starting sector value to reflect a position relative to the
			// start of the extended partition.
			if (sector)
				tmpSlice.startSector -= sector;

			// Fill out the table entry
			formatTableEntry(theDisk, &tmpSlice,
				&mbr->partTable.entries[numEntries]);
		}

		numEntries += 1;
	}

	// Make sure it has a valid signature
	mbr->bootSig = MSDOS_BOOT_SIGNATURE;

	// Write back the sector.
	status = diskWriteSectors(theDisk->name, sector, 1, sectorData);
	if (status < 0)
		error(_("Couldn't write partition table sector %u"), sector);

out:
	free(sectorData);
	return (status);
}


/////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////
//
//  Standard disk label functions
//
/////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////

static int detect(const disk *theDisk)
{
	// Checks for the presense of an MS-DOS disk label.

	int status = 0;
	unsigned char *sectorData = NULL;

	sectorData = malloc(theDisk->sectorSize);
	if (!sectorData)
		return (status = ERR_MEMORY);

	// Read the first sector of the device
	status = diskReadSectors(theDisk->name, 0, 1, sectorData);
	if (status < 0)
	{
		free(sectorData);
		return (status);
	}

	// Is this a valid partition table?  Make sure the signature is at the end.
	status = checkSignature(sectorData);

	free(sectorData);

	if (status == 1)
		// Call this an MS-DOS label.
		return (status);
	else
		// Not an MS-DOS label
		return (status = 0);
}


static int create(const disk *theDisk)
{
	// Creates an MS-DOS disk label.

	int status = 0;
	msdosMbr *dosMbr = NULL;

	dosMbr = malloc(theDisk->sectorSize);
	if (!dosMbr)
		return (status = ERR_MEMORY);

	// Read the first sector of the device
	status = diskReadSectors(theDisk->name, 0, 1, dosMbr);
	if (status < 0)
	{
		free(dosMbr);
		return (status);
	}

	// Clear the partition table
	memset(&dosMbr->partTable, 0, sizeof(msdosTable));

	// Set the signature
	dosMbr->bootSig = MSDOS_BOOT_SIGNATURE;

	// Write back the sector.
	status = diskWriteSectors(theDisk->name, 0, 1, dosMbr);
	if (status < 0)
	{
		free(dosMbr);
		return (status);
	}

	// Clobber any GPT header sector by erasing the signature, since other
	// tools (parted) can complain and get confused
	status = diskReadSectors(theDisk->name, 1, 1, dosMbr);
	if (status >= 0)
	{
		if (!memcmp(((gptHeader *) dosMbr)->signature, GPT_SIG, 8))
		{
			memset(((gptHeader *) dosMbr)->signature, 0, 8);

			diskWriteSectors(theDisk->name, 1, 1, dosMbr);
		}
	}

	free(dosMbr);
	return (status = 0);
}


static int readTable(const disk *theDisk, rawSlice *slices, int *numSlices)
{
	// Recursively read the partition tables, starting with the MBR in sector
	// zero.

	// Record the first/last usable partition sectors
	msdosLabel.firstUsableSect = 1;
	msdosLabel.lastUsableSect = (theDisk->numSectors - 1);

	*numSlices = 0;
	return (doReadTable(theDisk, 0 /* sector */, 0 /* extendedStart */, slices,
		numSlices));
}


static int writeTable(const disk *theDisk, rawSlice *slices,
	int numSlices __attribute__((unused)))
{
	// Recursively write the partition tables, starting with the MBR in sector
	// zero.
	return (doWriteTable(theDisk, 0 /* sector */, 0 /* extendedStart */,
		slices));
}


static int getSliceDesc(rawSlice *slc, char *string)
{
	// Given a pointer to a raw slice, return a string description based on
	// the partition type.  For MS-DOS we call the kernel to ask for the tag
	// description, if known.

	int status = 0;
	msdosPartType type;

	status = diskGetMsdosPartType(slc->tag, &type);
	if (status < 0)
		return (status);

	strncpy(string, type.description, FSTYPE_MAX_NAMELENGTH);
	return (status = 0);
}


static sliceType canCreateSlice(slice *slices, int numSlices, int sliceNumber)
{
	// This will return a sliceType enumeration if, given a slice number
	// representing free space, a partition can be created there.  If not, it
	// returns error.  Otherwise it returns the enumeration representing the
	// type that can be created: primary, logical, or 'any'.

	sliceType returnType = partition_any;
	int numPrimary = 0;
	int numLogical = 0;
	int count;

	// Gather a bit of information
	for (count = 0; count < numSlices; count ++)
	{
		if (slices[count].raw.tag)
		{
			if (ISLOGICAL(&slices[count]))
				numLogical += 1;
			else
				numPrimary += 1;
		}
	}

	if (numLogical)
	{
		// There are existing logical partitions.

		// Logical partitions use up one primary slot for all of them.
		numPrimary += 1;

		// If this will be the first slice, and the second is not logical,
		// then allow only primary.
		if (!sliceNumber && !ISLOGICAL(&slices[sliceNumber + 1]))
			returnType = partition_primary;

		// If this will be the last slice, and the previous is not logical,
		// then only allow primary.
		else if ((sliceNumber == (numSlices - 1)) &&
			!ISLOGICAL(&slices[sliceNumber - 1]))
		{
			returnType = partition_primary;
		}

		// If this will be between two slices, and neither is logical, then
		// only allow primary.
		else if (((sliceNumber > 0) && !ISLOGICAL(&slices[sliceNumber - 1])) &&
			((sliceNumber < (numSlices - 1)) &&
			!ISLOGICAL(&slices[sliceNumber + 1])))
		{
			returnType = partition_primary;
		}

		// If this will be between two logical slices, then only allow logical.
		else if (((sliceNumber > 0) && ISLOGICAL(&slices[sliceNumber - 1])) &&
			((sliceNumber < (numSlices - 1)) &&
			ISLOGICAL(&slices[sliceNumber + 1])))
		{
			returnType = partition_logical;
		}
	}

	// If we don't have to do logical, check whether the main table is full
	if ((returnType != partition_logical) &&
		(numPrimary >= DISK_MAX_PRIMARY_PARTITIONS))
	{
		// If logical is possible, then logical it will be
		if (numLogical && (returnType == partition_any))
		{
			returnType = partition_logical;
		}
		else
		{
			// Can't do logical, and the main table is full of entries
			returnType = partition_none;
		}
	}

	// Can't create a logical partition from a single sector
	if (slices[sliceNumber].raw.numSectors < 2)
	{
		if ((returnType == partition_any) || (returnType == partition_primary))
			returnType = partition_primary;
		else
			returnType = partition_none;
	}

	return (returnType);
}


static int canHide(slice *slc)
{
	// This will return 1 if the given slice is hideable.  For MS-DOS that
	// means we return 1 if the tag is a hideable tag, or a hidden tag.

	if (MSDOSTAG_IS_HIDEABLE(slc->raw.tag) || MSDOSTAG_IS_HIDDEN(slc->raw.tag))
		return (1);
	else
		return (0);
}


static void hide(slice *slc)
{
	// This will hide or unhide the slice.

	if (MSDOSTAG_IS_HIDDEN(slc->raw.tag))
		slc->raw.tag -= 0x10;
	else if (MSDOSTAG_IS_HIDEABLE(slc->raw.tag))
		slc->raw.tag += 0x10;
}


static int getTypes(listItemParameters **typeListParams)
{
	// Get the list of supported partition types as an array of
	// listItemParameters structures

	msdosPartType *types = NULL;
	int numberTypes = 0;
	int count;

	// Get the list of types
	types = diskGetMsdosPartTypes();
	if (!types)
		return (numberTypes = ERR_NODATA);

	for (count = 0; types[count].tag; count ++)
		numberTypes += 1;

	// Make an array of list item parameters

	*typeListParams = malloc(numberTypes * sizeof(listItemParameters));
	if (!(*typeListParams))
	{
		numberTypes = ERR_MEMORY;
		goto out;
	}

	for (count = 0; count < numberTypes; count ++)
		snprintf((*typeListParams)[count].text, WINDOW_MAX_LABEL_LENGTH,
			"%02x  %s", types[count].tag, types[count].description);

out:
	memoryRelease(types);
	return (numberTypes);
}


static int setType(slice *slc, int typeNum)
{
	// Given a slice and the number of a type (returned in the list by the
	// function getTypes()), set the type.

	int status = 0;
	msdosPartType *types = NULL;

	// Get the list of types
	types = diskGetMsdosPartTypes();
	if (!types)
		return (status = ERR_NODATA);

	slc->raw.tag = types[typeNum].tag;

	memoryRelease(types);
	return (status = 0);
}


diskLabel msdosLabel = {
	// Data
	label_msdos,	// type
	(LABELFLAG_PRIMARYPARTS | // flags
	LABELFLAG_LOGICALPARTS |
	LABELFLAG_USETAGS |
	LABELFLAG_USEACTIVE),
	0,				// firstUsableSect (set dynamically)
	(uquad_t) -1,	// lastUsableSect (set dynamically)

	// Functions
	&detect,
	&create,
	&readTable,
	&writeTable,
	&getSliceDesc,
	&canCreateSlice,
	&canHide,
	&hide,
	&getTypes,
	&setType
};


diskLabel *getLabelMsdos(void)
{
	// Called at initialization, returns a pointer to the disk label structure.
	return (&msdosLabel);
}

