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
//  compress.h
//

// This file contains public definitions and structures used by the
// compression/decompression library.

#if !defined(_COMPRESS_H)

#include <stdio.h>
#include <sys/deflate.h>
#include <sys/progress.h>

// For doing distance-length hashes
#define DEFLATE_HASH_NODES		(DEFLATE_MAX_INBUFFERSIZE * 2)
#define DEFLATE_HASH_BUCKETS	256

typedef struct {
	const unsigned char *data;
	unsigned short bufferedBytes;
	unsigned short byte;

} byteBufferIn;

typedef struct {
	unsigned char *data;
	unsigned byte;

} byteBufferOut;

typedef struct {
	const unsigned char *data;
	unsigned char bit;
	unsigned short byte;

} bitBufferIn;

typedef struct {
	unsigned char *data;
	unsigned char bit;
	unsigned short byte;

} bitBufferOut;

typedef struct {
	unsigned short codes[DEFLATE_MAX_INBUFFERSIZE + 1 /* EOB */];
	unsigned short numCodes;

} processedInput;

typedef struct _hashNode {
	int generation;
	const unsigned char *data;
	struct _hashNode *prev;
	struct _hashNode *next;

} hashNode;

typedef struct {
	hashNode *sub[DEFLATE_HASH_BUCKETS];

} hashBucket;

typedef struct {
	int generation;
	unsigned byte;
	hashNode nodeMemory[DEFLATE_MAX_OUTBUFFERSIZE * 2];
	hashNode *freeNodes;
	int numFreeNodes;
	hashBucket buckets[DEFLATE_HASH_BUCKETS];

} hashTable;

typedef struct {
	unsigned short weight;
	unsigned short value;

} huffmanLeaf;

typedef struct {
	unsigned short weight;
	char leftIsLeaf;
	char rightIsLeaf;
	void *left;
	void *right;

} huffmanNode;

typedef struct {
	huffmanNode nodeMem[DEFLATE_LITLEN_CODES];
	unsigned short numNodes;
	huffmanLeaf leafMem[DEFLATE_LITLEN_CODES];
	unsigned short numLeaves;
	huffmanNode *rootNode;

} huffmanTree;

typedef struct {
	unsigned char len;
	unsigned short num;
	unsigned short code;

} huffmanCode;

typedef struct {
	unsigned short numCodes;
	unsigned short first;
	unsigned short startCode;

} huffmanCodeLen;

typedef struct {
	unsigned char leastBits;
	unsigned char mostBits;
	unsigned short numCodes;
	huffmanCode codes[DEFLATE_LITLEN_CODES];
	huffmanCodeLen len[DEFLATE_CODELEN_CODES];
	huffmanCode *ordered[DEFLATE_LITLEN_CODES];

} huffmanTable;

// State structure passed to compressDeflate() and decompressDeflate().
// Incorporates all of the working memory needed for DEFLATE.  This is a
// BIG structure.  Don't try to allocate this on the stack!
typedef struct {
	// Buffers and counts set up by the caller (updated by the DEFLATE code)
	const unsigned char *inBuffer;
	unsigned inBytes;	// inBuffer remaining data
	unsigned inByte;	// inBuffer current (initially 0)
	unsigned char *outBuffer;
	unsigned outBytes;	// outBuffer remaining space
	unsigned outByte;	// outBuffer current (initially 0)

	// The running checksum and the final block flag, set by the DEFLATE code
	unsigned crc32Sum;
	unsigned short final;

	// Used internally by the DEFLATE code

	// Compression only
	byteBufferIn byteIn;
	bitBufferOut bitOut;
	processedInput processed;
	huffmanTable staticTable;
	hashTable hash;
	huffmanTree litLenTree;
	huffmanTree distTree;
	huffmanTree codeLenTree;

	// Decompression only
	bitBufferIn bitIn;
	byteBufferOut byteOut;

	// Compression and decompression
	huffmanTable litLenTable;
	huffmanTable distTable;
	huffmanTable codeLenTable;

} deflateState;

typedef struct {
	char *name;
	char *comment;
	unsigned mode;
	unsigned modTime;
	unsigned startOffset;
	unsigned totalSize;
	unsigned dataOffset;
	unsigned compressedDataSize;
	unsigned decompressedDataSize;

} archiveMemberInfo;

// Functions exported by libcompress

// Can be used both in user space, and by the kernel
int deflateCompress(deflateState *);
int deflateDecompress(deflateState *);

// Can be used in user space only
int deflateCompressFileData(deflateState *, FILE *, FILE *, progress *);
int deflateDecompressFileData(deflateState *, FILE *, FILE *, progress *);
int gzipAddMember(FILE *, FILE *, const char *, const char *, unsigned, int,
	progress *);
int gzipCompressFile(const char *, const char *, const char *, int,
	progress *);
int gzipMemberInfo(FILE *, archiveMemberInfo *, progress *);
int gzipExtractNextMember(FILE *, int, const char *, progress *);
int gzipExtractMember(const char *, const char *, int, const char *,
	progress *);
int gzipExtract(const char *, progress *);
int gzipDeleteMember(const char *, const char *, int, progress *);
int tarAddMember(const char *, const char *, progress *);
int tarMemberInfo(FILE *, archiveMemberInfo *, progress *);
int tarExtractNextMember(FILE *, progress *);
int tarExtractMember(const char *, const char *, int, progress *);
int tarExtract(const char *, progress *);
int tarDeleteMember(const char *, const char *, int, progress *);
int archiveAddMember(const char *, const char *, int, const char *,
	progress *);
int archiveAddRecursive(const char *, const char *, int, const char *,
	progress *);
int archiveInfo(const char *, archiveMemberInfo **, progress *);
void archiveInfoContentsFree(archiveMemberInfo *);
void archiveInfoFree(archiveMemberInfo *, int);
int archiveExtractMember(const char *, const char *, int, const char *,
	progress *);
int archiveExtract(const char *, progress *);
int archiveDeleteMember(const char *, const char *, int, progress *);

#define _COMPRESS_H
#endif

