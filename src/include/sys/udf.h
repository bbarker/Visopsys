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
//  udf.h
//

// This file contains definitions and structures for using and manipulating
// UDF filesystems in Visopsys.

#if !defined(_UDF_H)

#include <sys/types.h>

// Definitions

#define UDF_ANCHOR_VOLDESC_SECTOR			256
#define UDF_STANDARD_IDENTIFIER_BEA			"BEA01"
#define UDF_STANDARD_IDENTIFIER_BOOT		"BOOT2"
#define UDF_STANDARD_IDENTIFIER_VOLSEQ2		"NSR02"
#define UDF_STANDARD_IDENTIFIER_VOLSEQ3		"NSR03"
#define UDF_STANDARD_IDENTIFIER_TEA			"TEA01"

// Tag types
#define UDF_TAGID_PRIMARYVOLDESC			1
#define UDF_TAGID_ANCHORVOLDESC				2
#define UDF_TAGID_VOLDESCPOINTER			3
#define UDF_TAGID_IMPLUSEVOLDESC			4
#define UDF_TAGID_PARTDESC					5
#define UDF_TAGID_LOGICALVOLDESC			6
#define UDF_TAGID_UNALLOCSPACEDESC			7
#define UDF_TAGID_TERMDESC					8
#define UDF_TAGID_LOGICALVOLINTEGDESC		9
#define UDF_TAGID_FILESETDESC				256
#define UDF_TAGID_FILEIDDESC				257
#define UDF_TAGID_FILEENTRYDESC				261

// Structures

typedef struct {
	unsigned char type;
	char info[63];

} __attribute__((packed)) udfCharSpec;

typedef struct {
	unsigned short typeTimezone;
	unsigned short year;
	unsigned char month;
	unsigned char day;
	unsigned char hour;
	unsigned char minute;
	unsigned char second;
	unsigned char centiSecond;
	unsigned char microSeconds100;
	unsigned char microSeconds;

} __attribute__((packed)) udfTimestamp;

typedef struct {
	unsigned char flags;
	char identifier[23];
	char suffix[8];

} __attribute__((packed)) udfEntityId;

typedef struct {
	unsigned byteLength;
	unsigned location;

} __attribute__((packed)) udfExtAllocDesc;

typedef struct {
	unsigned byteLength;
	unsigned location;

} __attribute__((packed)) udfShortAllocDesc;

typedef struct {
	unsigned byteLength;
	unsigned location;
	unsigned short locationHi;
	unsigned char implUse[6];

} __attribute__((packed)) udfLongAllocDesc;

typedef struct {
	unsigned blockNum;
	unsigned short partRefNum;

} __attribute__((packed)) udfLogicalBlock;

typedef struct {
	unsigned char type;
	char identifier[5];
	unsigned char version;
	unsigned char pad[2041];

} __attribute__((packed)) udfBeaDesc;

typedef udfBeaDesc udfTeaDesc;

typedef struct {
	unsigned char type;
	char identifier[5];
	unsigned char version;
	unsigned char res1;
	udfEntityId archType;
	udfEntityId bootId;
	unsigned bootExtLogical;
	unsigned bootExtLength;
	uquad_t loadAddress;
	uquad_t startAddress;
	udfTimestamp createTime;
	unsigned short flags;
	char res2[32];
	unsigned char boot[1906];

} __attribute__((packed)) udfBootDesc;

typedef struct {
	unsigned char type;
	char identifier[5];
	unsigned char version;
	unsigned char res;
	unsigned char pad[2040];

} __attribute__((packed)) udfVolSeqDesc;

typedef struct {
	unsigned short tagId;
	unsigned short descVersion;
	unsigned char tagChecksum;
	unsigned char res;
	unsigned short tagSerial;
	unsigned short descCrc;
	unsigned short descCrcLen;
	unsigned tagLocation;

} __attribute__((packed)) udfDescTag;

typedef struct {
	udfDescTag tag;
	udfExtAllocDesc primVolDescExt;
	udfExtAllocDesc resVolDescExt;
	unsigned char res[480];

} __attribute__((packed)) udfAnchorVolDesc;

typedef struct {
	udfDescTag tag;
	unsigned volDescSeqNum;
	unsigned primaryVolDescNum;
	char identifier[32];
	unsigned short volSeqNum;
	unsigned short maxVolSeqNum;
	unsigned short interLevel;
	unsigned short maxInterLevel;
	unsigned charSetList;
	unsigned maxCharSetList;
	char volSetId[128];
	udfCharSpec descCharSet;
	udfCharSpec explCharSet;
	udfExtAllocDesc volAbstract;
	udfExtAllocDesc volCopyright;
	udfEntityId appId;
	udfTimestamp recordTime;
	udfEntityId implId;
	char implUse[64];
	unsigned predVolDescSeqLocation;
	unsigned short flags;
	unsigned char res[22];

} __attribute__((packed)) udfPrimaryVolDesc;

typedef struct {
	udfDescTag tag;
	unsigned volDescSeqNum;
	unsigned short flags;
	unsigned short number;
	udfEntityId contents;
	unsigned char contentsUse[128];
	unsigned accessType;
	unsigned startLocation;
	unsigned length;
	udfEntityId implId;
	unsigned char implUse[128];
	unsigned char res[156];

} __attribute__((packed)) udfPartitionDesc;

typedef struct {
	udfDescTag tag;
	unsigned volDescSeqNum;
	udfCharSpec descCharSet;
	char identifier[128];
	unsigned blockSize;
	udfEntityId domainId;
	udfLongAllocDesc volContentsUse;
	unsigned mapTableLen;
	unsigned numPartMaps;
	udfEntityId implId;
	unsigned char implUse[128];
	udfExtAllocDesc integSeqExt;
	unsigned char partMap[6];

} __attribute__((packed)) udfLogicalVolDesc;

typedef struct {
	udfDescTag tag;
	udfTimestamp recordTime;
	unsigned short interLevel;
	unsigned short maxInterLevel;
	unsigned charSetList;
	unsigned maxCharSetList;
	unsigned fileSetNum;
	unsigned fileSetDescNum;
	udfCharSpec logicalVolIdCharSet;
	char logicalVolId[128];
	udfCharSpec fileSetCharSet;
	char fileSetId[32];
	char copyrightFileId[32];
	char abstractFileId[32];
	udfLongAllocDesc rootDirIcb;
	udfEntityId domainId;
	udfLongAllocDesc nextExt;
	unsigned char res[48];

} __attribute__((packed)) udfFileSetDesc;

typedef struct {
	unsigned priorRecDirEntries;
	unsigned short strategy;
	unsigned short strategyParam;
	unsigned short maxEntries;
	unsigned char res;
	unsigned char fileType;
	udfLogicalBlock parentIcb;
	unsigned short flags;

} __attribute__((packed)) udfIcbTag;

typedef struct {
	udfDescTag tag;
	udfIcbTag icbTag;
	unsigned uid;
	unsigned gid;
	unsigned perms;
	unsigned short linkCount;
	unsigned char recordFormat;
	unsigned char recordDisplayAttrs;
	unsigned recordLength;
	uquad_t length;
	uquad_t blocks;
	udfTimestamp accessTime;
	udfTimestamp modifiedTime;
	udfTimestamp attrTime;
	unsigned checkpoint;
	udfLongAllocDesc extdAttrIcb;
	udfEntityId implId;
	uquad_t uniqueId;
	unsigned extdAttrsLength;
	unsigned allocDescsLength;
	// The rest of the stuff is variable length
	unsigned char extdAttrs[];
	// ...

} __attribute__((packed)) udfFileEntry;

typedef struct {
	udfDescTag tag;
	unsigned short version;
	unsigned char charx;
	unsigned char idLength;
	udfLongAllocDesc icb;
	unsigned short implUseLength;
	// The rest of the stuff is variable length
	udfEntityId implUse[];
	// ...

} __attribute__((packed)) udfFileIdDesc;

#define _UDF_H
#endif

