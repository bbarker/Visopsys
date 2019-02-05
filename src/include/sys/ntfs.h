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
//  ntfs.h
//

// This file contains definitions and structures for using and manipulating
// the Microsoft(R) NTFS filesystem in Visopsys.

#if !defined(_NTFS_H)

#include <stdint.h>
#include <sys/disk.h>
#include <sys/progress.h>

#define NTFS_MAGIC_FILERECORD			0x454C4946 // 'FILE'

#define NTFS_ATTR_STANDARDINFO			0x10
#define NTFS_ATTR_ATTRLIST				0x20
#define NTFS_ATTR_FILENAME				0x30
#define NTFS_ATTR_VOLUMEVERSION			0x40
#define NTFS_ATTR_OBJECTID				0x40
#define NTFS_ATTR_SECURITYDESC			0x50
#define NTFS_ATTR_VOLUMENAME			0x60
#define NTFS_ATTR_VOLUMEINFO			0x70
#define NTFS_ATTR_DATA					0x80
#define NTFS_ATTR_INDEXROOT				0x90
#define NTFS_ATTR_INDEXALLOC			0xA0
#define NTFS_ATTR_BITMAP				0xB0
#define NTFS_ATTR_SYMBOLICLINK			0xC0
#define NTFS_ATTR_REPARSEPOINT			0xC0
#define NTFS_ATTR_EAINFO				0xD0
#define NTFS_ATTR_EA					0xE0
#define NTFS_ATTR_PROPERTYSET			0xF0
#define NTFS_ATTR_LOGGEDUTILSTR			0x100
#define NTFS_ATTR_TERMINATE				0xFFFFFFFF

typedef struct {
	uint32_t magic;						// 00 - 03  Magic number 'FILE'
	uint16_t updateSeqOffset;			// 04 - 05  Update sequence array offset
	uint16_t updateSeqLength;			// 06 - 08  Update sequence array length
	uint8_t unused[8];					// 09 - 0F  Unused
	uint16_t seqNumber;					// 10 - 11  Sequence number
	uint16_t refCount;					// 12 - 13  Reference count
	uint16_t attrSeqOffset;				// 14 - 15  Attributes sequence offset
	uint16_t flags;						// 16 - 17  Flags
	uint32_t recordRealLength;			// 18 - 1B  Real file record size
	uint32_t recordAllocLength;			// 1C - 1F  Allocated file record size
	uint64_t baseFileRecord;			// 20 - 27  Base record file reference
	uint16_t maxAttrId;					// 28 - 29  Max attribute identifier + 1
	uint16_t updateSeq;					// 2A - 2B  Update sequence number
	uint8_t updateSeqArray[];			// 2C -     Update sequence array

} __attribute__((packed)) ntfsFileRecord;

typedef struct {
	uint32_t type;						// 00 - 03  Attribute type
	uint32_t length;					// 04 - 07  Length
	uint8_t nonResident;				// 08 - 08  Non-resident flag
	uint8_t nameLength;					// 09 - 09  Name length
	uint16_t nameOffset;				// 0A - 0B  Name Offset
	uint16_t flags;						// 0C - 0C  Flags
	uint16_t attributeId;				// 0E - 0F  Attribute ID
	union {
		struct {
			uint32_t attributeLength;	// 10 - 13  Attribute length
			uint8_t attributeOffset;	// 14 - 15  Attribute offset
			uint8_t indexedFlag;		// 16 - 16  Indexed flag
			uint8_t unused;				// 17 - 17  Unused
			uint8_t attribute[];		// 18 -     Attribute - Starts with name
		} yes;							//          if named attribute.
		struct {
			uint64_t startVcn;			// 10 - 17  Starting VCN
			uint64_t lastVcn;			// 18 - 1F  Ending VCN
			uint16_t dataRunsOffset;	// 20 - 21  Data runs offset
			uint16_t compUnitSize;		// 22 - 23  Compression unit size
			uint8_t unused[4];			// 24 - 27  Unused
			uint64_t allocLength;		// 28 - 2F  Attribute allocated length
			uint64_t realLength;		// 30 - 37  Attribute real length
			uint64_t streamLength;		// 38 - 3F  Initialized data stream len
			uint8_t dataRuns[];			// 40 -     Data runs - Starts with name
		} no;							//          if named attribute.
	} res;

} __attribute__((packed)) ntfsAttributeHeader;

typedef struct {
	uint64_t parentDirRef;				// 00 - 07  File reference of parent dir
	uint64_t cTime;						// 08 - 0F  File creation
	uint64_t aTime;						// 10 - 17  File altered
	uint64_t mTime;						// 18 - 1F  MFT record changed
	uint64_t rTime;						// 20 - 27  File read (accessed) time
	uint64_t allocLength;				// 28 - 2F  File allocated length
	uint64_t realLength;				// 30 - 37  File real length
	uint32_t flags;						// 38 - 3B  Flags (eg. dir, comp, hidden)
	uint32_t EaFlags;					// 3C - 3F  Used by EAs and reparse
	uint8_t filenameLength;				// 40 - 40  Filename length in characters
	uint8_t filenameNamespace;			// 41 - 41  Filename namespace
	uint16_t filename[];				// 42 -     Filename in Unicode (not NULL
										//          terminated)
} __attribute__((packed)) ntfsFilenameAttribute;

typedef struct {
	uint32_t magic;						// 00 - 03  Magic number 'INDX'
	uint16_t updateSeqOffset;			// 04 - 05  Update sequence array offset
	uint16_t updateSeqLength;			// 06 - 08  Update sequence array length
	uint8_t unused1[8];					// 09 - 0F  Unused
	uint64_t indexBufferVcn;			// 10 - 17  VCN of the index buffer
	uint16_t entriesStartOffset;		// 18 - 19  Entries starting offset - 0x18
	uint8_t unused2[2];					// 1A - 1B  Unused
	uint32_t entriesEndOffset;			// 1C - 1F  Entries ending offset - 0x18
	uint32_t bufferEndOffset;			// 20 - 23  Buffer ending offset - 0x18
	uint32_t rootNode;					// 24 - 27  1 if not leaf node
	uint16_t updateSeq;					// 28 - 29  Update sequence number
	uint8_t updateSeqArray[];			// 2A -     Update sequence array

} __attribute__((packed)) ntfsIndexBuffer;

typedef struct {
	uint64_t fileReference;				// 00 - 07  File reference
	uint16_t entryLength;				// 08 - 09  Index entry length
	uint16_t streamLength;				// 0A - 0B  Length of the stream
	uint8_t flags;						// 0C - 0C  Flags
	uint8_t unused[3];					// 0D - 0F  Unused
	uint8_t stream[];					// 10 -     Data

} __attribute__((packed)) ntfsIndexEntry;

typedef struct {
	uint8_t jmpBoot[3];					// 00  - 02   Jmp to boot code
	char oemName[8];					// 03  - 0A   OEM Name
	uint16_t bytesPerSect;				// 0B  - 0C   Bytes per sector
	uint8_t sectsPerClust;				// 0D  - 0D   Sectors per cluster
	uint8_t unused1[7];					// 0E  - 14   Unused
	uint8_t media;						// 15  - 15   Media descriptor byte
	uint8_t unused2[2];					// 16  - 17   Unused
	uint16_t sectsPerTrack;				// 18  - 19   Sectors per track
	uint16_t numHeads;					// 1A  - 1B   Number of heads
	uint8_t unused3[8];					// 1C  - 23   Unused
	uint32_t biosDriveNum;				// 24  - 27   BIOS drive number
	uint64_t sectsPerVolume;			// 28  - 2F   Sectors per volume
	uint64_t mftStart;					// 30  - 37   LCN of VCN 0 of the $MFT
	uint64_t mftMirrStart;				// 38  - 3F   LCN of VCN 0 of the $MFTMirr
	uint32_t clustersPerMftRec;			// 40  - 43   Clusters per MFT Record
	uint32_t clustersPerIndexRec;		// 44  - 47   Clusters per Index Record
	uint64_t volSerial;					// 48  - 4F   Volume serial number
	uint8_t unused4[13];				// 50  - 5C   Unused
	uint8_t bootCode[417];				// 5D  - 1FD  Boot loader code
	uint16_t magic;						// 1FE - 1FF  Magic number
	uint8_t moreCode[];					// 200 -      More code of some sort

} __attribute__((packed)) ntfsBootFile;

// Functions in libntfs
int ntfsFormat(const char *, const char *, int, progress *);
int ntfsGetResizeConstraints(const char *, uquad_t *, uquad_t *, progress *);
int ntfsResize(const char *, uquad_t, progress *);

#define _NTFS_H
#endif

