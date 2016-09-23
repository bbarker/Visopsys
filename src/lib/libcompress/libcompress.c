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
//  libcompress.c
//

// This is the main entry point for our library of compression functions

#include "libcompress.h"
#include <errno.h>
#include <libgen.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/api.h>
#include <sys/compress.h>
#include <sys/deflate.h>
#include <sys/gzip.h>
#include <sys/loader.h>
#include <sys/progress.h>
#include <sys/stat.h>
#include <sys/tar.h>

#ifdef DEBUG
	#define DEBUG_OUTMAX	160
	int debugLibCompress = 0;
	static char debugOutput[DEBUG_OUTMAX];

	static void DEBUGMSG(const char *message, ...)
	{
		va_list list;

		if (debugLibCompress)
		{
			va_start(list, message);
			vsnprintf(debugOutput, DEBUG_OUTMAX, message, list);
			printf("%s", debugOutput);
		}
	}
#else
	#define DEBUGMSG(message, arg...) do { } while (0)
#endif


static int gzipReadMemberHeader(FILE *inStream, archiveMemberInfo *info)
{
	// Read the next member header of a GZIP file, and return the relevent
	// info from it.

	int status = 0;
	gzipMember member;
	gzipExtraField extraField;
	int memberNameLen = 0;
	int commentLen = 0;
	unsigned short crc16 = 0;

	DEBUGMSG("Read GZIP member header\n");

	memset(&member, 0, sizeof(gzipMember));
	memset(&extraField, 0, sizeof(gzipExtraField));

	if (fread((void *) &member, sizeof(gzipMember), 1, inStream) < 1)
		// Finished, we guess.  No more members.
		return (status = 0);

	if (member.sig != GZIP_MAGIC)
	{
		fprintf(stderr, "Not a valid GZIP file\n");
		return (status = ERR_BADDATA);
	}

	DEBUGMSG("Sig %04x cm=%d flg=%02x mtime=%08x xfl=%d os=%d\n",
		member.sig, member.compMethod, member.flags, member.modTime,
		member.extraFlags, member.opSys);

	// Save the modification time
	info->modTime = member.modTime;

	if (member.flags & GZIP_FLG_FEXTRA)
	{
		// Read "extra field" header
		if (fread((void *) &extraField, sizeof(gzipExtraField), 1,
			inStream) < 1)
		{
			fprintf(stderr, "Error reading extra field header\n");
			status = ERR_NODATA;
			goto out;
		}

		// Skip it for now
		status = fseek(inStream, extraField.len, SEEK_CUR);
		if (status)
		{
			fprintf(stderr, "Error seeking extra field header\n");
			status = ERR_NODATA;
			goto out;
		}
	}

	if (member.flags & GZIP_FLG_FNAME)
	{
		// Read the NULL-terminated "file name" field
		memberNameLen = 0;
		while (!memberNameLen || (info->name[memberNameLen - 1] != '\0'))
		{
			info->name = realloc(info->name, (memberNameLen + 1));
			if (!info->name)
			{
				fprintf(stderr, "Error allocating memory\n");
				status = ERR_MEMORY;
				goto out;
			}

			if (fread((info->name + memberNameLen), 1, 1, inStream) < 1)
			{
				fprintf(stderr, "Error reading member name\n");
				status = ERR_NODATA;
				goto out;
			}

			memberNameLen += 1;
		}

		DEBUGMSG("Member file name: %s\n", info->name);
	}

	if (member.flags & GZIP_FLG_FCOMMENT)
	{
		// Read the NULL-terminated "comment" field.  GNU gzip doesn't seem
		// to create these.
		commentLen = 0;
		while (!commentLen || (info->comment[commentLen - 1] != '\0'))
		{
			info->comment = realloc(info->comment, (commentLen + 1));
			if (!info->comment)
			{
				fprintf(stderr, "Error allocating memory\n");
				status = ERR_MEMORY;
				goto out;
			}

			if (fread((info->comment + commentLen), 1, 1, inStream) < 1)
			{
				fprintf(stderr, "Error reading member comment\n");
				status = ERR_NODATA;
				goto out;
			}

			commentLen += 1;
		}

		DEBUGMSG("Comment: %s\n", info->comment);
	}

	if (member.flags & GZIP_FLG_FHCRC)
	{
		// Read CRC16
		if (fread(&crc16, sizeof(unsigned short), 1, inStream) < 1)
		{
			fprintf(stderr, "Error reading member CRC16\n");
			status = ERR_NODATA;
			goto out;
		}

		DEBUGMSG("CRC16: %04x\n", crc16);
	}

	// Return success
	status = 1;

out:
	if (status < 0)
	{
		if (info->comment)
		{
			free(info->comment);
			info->comment = NULL;
		}

		if (info->name)
		{
			free(info->name);
			info->name = NULL;
		}

		errno = status;
	}

	return (status);
}


static int tarReadMemberHeader(FILE *inStream, archiveMemberInfo *info)
{
	// Read the next member header of a TAR file, and return the relevent
	// info from it.

	int status = 0;
	tarHeader header;
	int prefixLen = 0;
	int nameLen = 0;
	int count;

	memset(&header, 0, sizeof(tarHeader));

	DEBUGMSG("Read TAR member header\n");

	// Record where the member starts
	info->startOffset = ftell(inStream);

	status = fread((void *) &header, sizeof(tarHeader), 1, inStream);
	if (status < 1)
		return (status = errno);

	// Look out for an empty block - indicates end of archive (actually 2 of
	// them
	for (count = 0; count < 512; count ++)
	{
		if (((char *) &header)[count])
			break;
	}

	if (count >= 512)
	{
		// Finished, we guess.  No more members.
		DEBUGMSG("End of TAR archive\n");
		return (status = 0);
	}

	if (memcmp(header.magic, TAR_MAGIC, sizeof(TAR_MAGIC)) &&
		memcmp(header.magic, TAR_OLDMAGIC, sizeof(TAR_OLDMAGIC)))
	{
		fprintf(stderr, "Not a valid TAR entry\n");
		return (status = ERR_BADDATA);
	}

	// Get the file name

	// The 'prefix' and 'name' are NULL-terminated -- unless they're full,
	// in which case they're not.  Bah!

	if (header.prefix[0])
	{
		if (header.prefix[TAR_MAX_PREFIX - 1])
			prefixLen = TAR_MAX_PREFIX;
		else
			prefixLen = strlen(header.prefix);
	}

	if (header.name[TAR_MAX_NAMELEN - 1])
		nameLen = TAR_MAX_NAMELEN;
	else
		nameLen = strlen(header.name);

	DEBUGMSG("Member name length: %d\n", nameLen);

	info->name = malloc(nameLen + prefixLen + 1);
	if (!info->name)
	{
		fprintf(stderr, "Error allocating memory\n");
		status = ERR_MEMORY;
		goto out;
	}

	if (header.prefix[0])
	{
		strncpy(info->name, header.prefix, prefixLen);
		info->name[prefixLen] = '\0';
		strncat(info->name, header.name, nameLen);
	}
	else
	{
		strncpy(info->name, header.name, nameLen);
	}

	DEBUGMSG("Member file name: %s\n", info->name);

	// Directory, link?
	switch (header.typeFlag)
	{
		case TAR_TYPEFLAG_DIR:
			info->mode = (unsigned) dirT;
			break;

		case TAR_TYPEFLAG_SYMLINK:
			info->mode = (unsigned) linkT;
			break;

		default:
			info->mode = (unsigned) fileT;
			break;
	}

	// Get the modification time
	info->modTime = strtoul(header.modTime, NULL, 8);

	info->dataOffset = ftell(inStream);

	DEBUGMSG("Member data offset: %u\n", info->dataOffset);

	info->compressedDataSize = info->decompressedDataSize =
		strtoul(header.size, NULL, 8);

	DEBUGMSG("Member data size: %u\n", info->compressedDataSize);

	// Total size is aligned to a 512-byte coundary
	info->totalSize = (sizeof(tarHeader) + info->compressedDataSize);
	if (info->compressedDataSize % 512)
		info->totalSize += (512 - (info->compressedDataSize % 512));

	DEBUGMSG("Member total size: %u\n", info->totalSize);

	// Return success
	status = 1;

out:
	if (status < 0)
	{
		if (info->name)
		{
			free(info->name);
			info->name = NULL;
		}

		errno = status;
	}

	return (status);
}


static int deflateCompressFileData(deflateState *deflate, FILE *inStream,
	FILE *outStream, progress *prog)
{
	int status = 0;
	unsigned totalBytes = inStream->f.size;
	unsigned maxInBytes = 0;
	unsigned maxOutBytes = 0;
	unsigned doneBytes = 0;

	maxInBytes = min(totalBytes, COMPRESS_MAX_BUFFERSIZE);
	maxInBytes = max(maxInBytes, 1); // Makes dealing with empty files easier

	// Worst case scenario, DEFLATE expands to 5 extra bytes per 32K block,
	// but give it a bit of extra working space in any case.
	maxOutBytes = (maxInBytes +
		max((((maxInBytes + (DEFLATE_MAX_INBUFFERSIZE - 1)) /
			DEFLATE_MAX_INBUFFERSIZE) * 5),
		(maxInBytes / 10)));
	maxOutBytes = max(maxOutBytes, 5);

	memset(deflate, 0, sizeof(deflateState));
	deflate->inBuffer = calloc(maxInBytes, 1);
	deflate->outBuffer = calloc(maxOutBytes, 1);

	if (!deflate->inBuffer || !deflate->outBuffer)
	{
		fprintf(stderr, "Memory error\n");
		return (status = ERR_MEMORY);
	}

	if (prog)
	{
		memset((void *) prog, 0, sizeof(progress));
		prog->numTotal = totalBytes;
	}

	do
	{
		maxInBytes = min((totalBytes - doneBytes), maxInBytes);

		if (doneBytes < totalBytes)
		{
			DEBUGMSG("Reading %u bytes\n", maxInBytes);
			if (prog && (lockGet(&prog->progLock) >= 0))
			{
				sprintf((char *) prog->statusMessage, "Reading %u bytes",
					maxInBytes);
				lockRelease(&prog->progLock);
			}

			if (fread((void *)(deflate->inBuffer + deflate->inByte), 1,
				maxInBytes, inStream) < maxInBytes)
			{
				fprintf(stderr, "Error reading %s\n", inStream->f.name);
				status = ERR_IO;
				break;
			}
		}

		deflate->inBytes = maxInBytes;
		deflate->outBytes = maxOutBytes;
		deflate->outByte = 0;

		DEBUGMSG("Compressing %u bytes\n", deflate->inBytes);
		if (prog && (lockGet(&prog->progLock) >= 0))
		{
			sprintf((char *) prog->statusMessage, "Compressing %u bytes",
				deflate->inBytes);
			lockRelease(&prog->progLock);
		}

		status = deflateCompress(deflate);
		if (status < 0)
		{
			fprintf(stderr, "Error compressing %s\n", inStream->f.name);
			break;
		}

		DEBUGMSG("Writing %u bytes\n", deflate->outByte);
		if (prog && (lockGet(&prog->progLock) >= 0))
		{
			sprintf((char *) prog->statusMessage, "Writing %u bytes",
				deflate->outByte);
			lockRelease(&prog->progLock);
		}

		if (fwrite((void *) deflate->outBuffer, 1, deflate->outByte,
			outStream) < deflate->outByte)
		{
			fprintf(stderr, "Error writing %s\n", outStream->f.name);
			status = ERR_IO;
			break;
		}

		doneBytes += maxInBytes;

		if (prog && (lockGet(&prog->progLock) >= 0))
		{
			prog->numFinished = doneBytes;
			if (totalBytes)
				prog->percentFinished = ((doneBytes * 100) / totalBytes);
			else
				prog->percentFinished = 100;
			lockRelease(&prog->progLock);
		}

		if (!deflate->final)
		{
			// This is not mandatory for the DEFLATE compression code, but for
			// maximum compression, we should keep the last
			// DEFLATE_MAX_DISTANCE (32K) bytes at the top of the input buffer
			// for more matches.
			memmove((void *) deflate->inBuffer,
				(deflate->inBuffer + (deflate->inByte - DEFLATE_MAX_DISTANCE)),
				DEFLATE_MAX_DISTANCE);
			deflate->inByte = DEFLATE_MAX_DISTANCE;
			maxInBytes -= DEFLATE_MAX_DISTANCE;

			// If the previous round produced an incomplete output byte,
			// preserve it for the next round in byte 0, and clear the rest.
			// Otherwise, just clear.
			if (deflate->bitOut.bit)
			{
				deflate->outBuffer[0] = deflate->outBuffer[deflate->outByte];
				memset((deflate->outBuffer + 1), 0, deflate->outByte);
			}
			else
			{
				memset(deflate->outBuffer, 0, deflate->outByte);
			}
		}

	} while (!deflate->final);

	if (deflate->outBuffer)
		free(deflate->outBuffer);

	if (deflate->inBuffer)
		free((void *) deflate->inBuffer);

	return (status);
}


static int deflateDecompressFileData(deflateState *deflate, FILE *inStream,
	FILE *outStream, progress *prog)
{
	int status = 0;
	unsigned totalBytes = inStream->f.size;
	unsigned maxInBytes = 0;
	unsigned maxOutBytes = 0;
	unsigned doneBytes = 0;
	unsigned skipOutBytes = 0;

	maxInBytes = min(totalBytes, COMPRESS_MAX_BUFFERSIZE);
	maxOutBytes = COMPRESS_MAX_BUFFERSIZE;

	memset(deflate, 0, sizeof(deflateState));
	deflate->inBuffer = calloc(maxInBytes, 1);
	deflate->outBuffer = calloc(maxOutBytes, 1);

	if (!deflate->inBuffer || !deflate->outBuffer)
	{
		fprintf(stderr, "Memory error\n");
		return (status = ERR_MEMORY);
	}

	if (prog)
	{
		memset((void *) prog, 0, sizeof(progress));
		prog->numTotal = totalBytes;
	}

	while (doneBytes < totalBytes)
	{
		maxInBytes = (min((totalBytes - doneBytes), COMPRESS_MAX_BUFFERSIZE) -
			deflate->inBytes);

		DEBUGMSG("Reading %u bytes\n", maxInBytes);
		if (prog && (lockGet(&prog->progLock) >= 0))
		{
			sprintf((char *) prog->statusMessage, "Reading %u bytes",
				maxInBytes);
			lockRelease(&prog->progLock);
		}

		maxInBytes = (fread((void *)(deflate->inBuffer + deflate->inBytes), 1,
			maxInBytes, inStream) + deflate->inBytes);

		if (!maxInBytes)
		{
			fprintf(stderr, "Error reading %s\n", inStream->f.name);
			status = ERR_IO;
			break;
		}

		deflate->inBytes = maxInBytes;
		deflate->inByte = 0;
		deflate->outBytes = maxOutBytes;
		deflate->outByte = skipOutBytes;

		DEBUGMSG("Decompressing %u bytes\n", deflate->inBytes);
		if (prog && (lockGet(&prog->progLock) >= 0))
		{
			sprintf((char *) prog->statusMessage, "Decompressing %u bytes",
				deflate->inBytes);
			lockRelease(&prog->progLock);
		}

		status = deflateDecompress(deflate);
		if (status < 0)
		{
			fprintf(stderr, "Error decompressing %s\n", inStream->f.name);
			break;
		}

		if (outStream)
		{
			DEBUGMSG("Writing %u bytes\n", (deflate->outByte - skipOutBytes));
			if (prog && (lockGet(&prog->progLock) >= 0))
			{
				sprintf((char *) prog->statusMessage, "Writing %u bytes",
					(deflate->outByte - skipOutBytes));
				lockRelease(&prog->progLock);
			}

			if (fwrite((void *)(deflate->outBuffer + skipOutBytes), 1,
				(deflate->outByte - skipOutBytes), outStream) <
				(deflate->outByte - skipOutBytes))
			{
				fprintf(stderr, "Error writing %s\n", outStream->f.name);
				status = ERR_IO;
				break;
			}
		}

		doneBytes += (maxInBytes - deflate->inBytes);

		if (prog && (lockGet(&prog->progLock) >= 0))
		{
			prog->numFinished = doneBytes;
			prog->percentFinished = ((doneBytes * 100) / totalBytes);
			lockRelease(&prog->progLock);
		}

		if (deflate->final)
			break;

		// If there are unprocessed bytes remaining in the input buffer, we
		// need to copy them to the top before we start the next loop.
		if (deflate->inBytes)
		{
			memcpy((void *) deflate->inBuffer, (deflate->inBuffer +
				deflate->inByte), deflate->inBytes);
		}

		// We must keep the last DEFLATE_MAX_DISTANCE (32K) bytes at the top
		// of the output buffer.
		memmove(deflate->outBuffer,
			(deflate->outBuffer + (deflate->outByte - DEFLATE_MAX_DISTANCE)),
			DEFLATE_MAX_DISTANCE);

		if (!skipOutBytes)
		{
			skipOutBytes = DEFLATE_MAX_DISTANCE;
			maxOutBytes -= skipOutBytes;
		}

		// Clear the rest of the output buffer
		memset((deflate->outBuffer + skipOutBytes), 0,
			(deflate->outByte - skipOutBytes));
	}

	// Seek backwards to the start of any un-processed input bytes
	if (deflate->inBytes)
	{
		DEBUGMSG("Rewinding %u bytes\n", deflate->inBytes);
		fseek(inStream, -((long) deflate->inBytes), SEEK_CUR);
	}

	if (deflate->outBuffer)
		free(deflate->outBuffer);

	if (deflate->inBuffer)
		free((void *) deflate->inBuffer);

	return (status);
}


static int copyFileData(FILE *inStream, FILE *outStream, unsigned totalBytes,
	progress *prog)
{
	int status = 0;
	unsigned maxBytes = 0;
	unsigned char *buffer = NULL;
	unsigned doneBytes = 0;

	if (prog)
	{
		memset((void *) prog, 0, sizeof(progress));
		prog->numTotal = totalBytes;
		strcpy((char *) prog->statusMessage, "Copying data");
	}

	maxBytes = min(totalBytes, COMPRESS_MAX_BUFFERSIZE);

	buffer = calloc(maxBytes, 1);
	if (!buffer)
	{
		fprintf(stderr, "Memory error\n");
		return (status = ERR_MEMORY);
	}

	while (doneBytes < totalBytes)
	{
		maxBytes = min(maxBytes, (totalBytes - doneBytes));

		DEBUGMSG("Reading %u bytes\n", maxBytes);

		if (fread(buffer, 1, maxBytes, inStream) < maxBytes)
		{
			fprintf(stderr, "Error reading\n");
			status = ERR_IO;
			break;
		}

		DEBUGMSG("Writing %u bytes\n", maxBytes);
		if (fwrite(buffer, 1, maxBytes, outStream) < maxBytes)
		{
			fprintf(stderr, "Error writing\n");
			status = ERR_IO;
			break;
		}

		doneBytes += maxBytes;

		if (prog && (lockGet(&prog->progLock) >= 0))
		{
			prog->numFinished = doneBytes;
			prog->percentFinished = ((doneBytes * 100) / totalBytes);
			lockRelease(&prog->progLock);
		}
	}

	free(buffer);

	return (status);
}


static int makeDirRecursive(char *path)
{
	int status = 0;
	file theDir;
	char *parent = NULL;

	if (fileFind(path, &theDir) >= 0)
		return (status = 0);

	parent = dirname(path);
	if (!parent)
		return (status = ERR_NOSUCHENTRY);

	status = makeDirRecursive(parent);

	free(parent);

	if (status < 0)
		return (status);

	return (status = fileMakeDir(path));
}


/////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////
//
// Below here, the functions are exported for external use
//
/////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////

int gzipAddMember(FILE *inStream, FILE *outStream, const char *name,
	const char *comment, unsigned modTime, int textFile, progress *prog)
{
	int status = 0;
	gzipMember member;
	deflateState *deflate = NULL;

	if (visopsys_in_kernel)
	{
		status = ERR_BUG;
		goto out;
	}

	// Check params.  It's OK for name, comment, and prog to be NULL.
	if (!inStream || !outStream)
	{
		fprintf(stderr, "NULL parameter\n");
		status = ERR_NULLPARAMETER;
		goto out;
	}

	memset(&member, 0, sizeof(gzipMember));

	// Output GZIP header information

	member.sig = GZIP_MAGIC;
	member.compMethod = GZIP_COMP_DEFLATE;
	if (name)
		member.flags |= GZIP_FLG_FNAME;
	if (comment)
		member.flags |= GZIP_FLG_FCOMMENT;
	if (textFile)
		member.flags |= GZIP_FLG_FTEXT;
	member.modTime = modTime;
	member.opSys = GZIP_OS_UNIX; // Closest thing available

	// Output the member header
	if (fwrite(&member, sizeof(gzipMember), 1, outStream) < 1)
	{
		fprintf(stderr, "Error writing\n");
		status = ERR_IO;
		goto out;
	}

	if (name)
	{
		// Output the NULL-terminated member name (original file name, perhaps)
		if (fwrite(name, 1, (strlen(name) + 1), outStream) <
			(strlen(name) + 1))
		{
			fprintf(stderr, "Error writing\n");
			status = ERR_IO;
			goto out;
		}
	}

	if (comment)
	{
		// Output the NULL-terminated comment
		if (fwrite(comment, 1, (strlen(comment) + 1), outStream) <
			(strlen(comment) + 1))
		{
			fprintf(stderr, "Error writing\n");
			status = ERR_IO;
			goto out;
		}
	}

	deflate = calloc(1, sizeof(deflateState));
	if (!deflate)
	{
		fprintf(stderr, "Memory error\n");
		status = ERR_MEMORY;
		goto out;
	}

	// Compress the data
	status = deflateCompressFileData(deflate, inStream, outStream, prog);
	if (status < 0)
		goto out;

	// Output CRC32 and decompressed size

	DEBUGMSG("CRC32: %08x\n", deflate->crc32Sum);
	if (fwrite(&deflate->crc32Sum, 4, 1, outStream) < 1)
	{
		fprintf(stderr, "Error writing\n");
		status = ERR_IO;
		goto out;
	}

	DEBUGMSG("Decompressed size: %u\n", inStream->f.size);
	if (fwrite(&inStream->f.size, 4, 1, outStream) < 1)
	{
		fprintf(stderr, "Error writing\n");
		status = ERR_IO;
		goto out;
	}

	if (prog && (lockGet(&prog->progLock) >= 0))
	{
		fflush(outStream);
		sprintf((char *) prog->statusMessage, "Compressed size %u",
			outStream->f.size);
		lockRelease(&prog->progLock);
	}

	// Return success
	status = 0;

out:
	if (deflate)
		free(deflate);

	if (status < 0)
		errno = status;

	return (status);
}


int gzipCompressFile(const char *inFileName, const char *outFileName,
	const char *comment, int append, progress *prog)
{
	// Compress a file using the GZIP file format, and the DEFLATE compression
	// algorithm.

	int status = 0;
	struct stat st;
	loaderFileClass class;
	int textFile = 0;
	FILE *inStream = NULL;
	char constOutFileName[MAX_NAME_LENGTH];
	FILE *outStream = NULL;

	if (visopsys_in_kernel)
	{
		status = ERR_BUG;
		goto out;
	}

	// Check params.  It's OK for outFileName and prog to be NULL.
	if (!inFileName)
	{
		fprintf(stderr, "NULL parameter\n");
		status = ERR_NULLPARAMETER;
		goto out;
	}

	memset(&st, 0, sizeof(struct stat));
	memset(&class, 0, sizeof(loaderFileClass));
	memset(constOutFileName, 0, sizeof(constOutFileName));

	DEBUGMSG("GZIP compress %s\n", inFileName);

	// Stat() the file.  We're interested in the modification time.
	status = stat(inFileName, &st);
	if (status < 0)
	{
		fprintf(stderr, "Couldn't stat() %s\n", inFileName);
		status = errno;
		goto out;
	}

	// Classify the file.  We're interested in knowing whether this is a binary
	// or text file.
	if (loaderClassifyFile(inFileName, &class))
	{
		if (class.class & LOADERFILECLASS_TEXT)
			textFile = 1;
	}

	// Open the input stream
	inStream = fopen(inFileName, "r");
	if (!inStream)
	{
		fprintf(stderr, "Couldn't open %s\n", inFileName);
		status = ERR_NOSUCHFILE;
		goto out;
	}

	if (!outFileName)
	{
		// Create the output file name
		snprintf(constOutFileName, MAX_NAME_LENGTH, "%s.gz", inFileName);
		outFileName = constOutFileName;
	}

	// Open output stream
	if (append)
		outStream = fopen(outFileName, "a");
	else
		outStream = fopen(outFileName, "w");

	if (!outStream)
	{
		fprintf(stderr, "Couldn't open %s\n", outFileName);
		goto out;
	}

	// Add the member
	status = gzipAddMember(inStream, outStream, inFileName, comment,
		st.st_mtime, textFile, prog);

out:
	if (outStream)
		fclose(outStream);

	if (inStream)
		fclose(inStream);

	if ((status < 0) && outStream)
		// Delete the incomplete output file
		fileDelete(outFileName);

	if (status < 0)
		errno = status;

	return (status);
}


int gzipMemberInfo(FILE *inStream, archiveMemberInfo *info, progress *prog)
{
	// Decompress the current member of a GZIP file, but don't write out the
	// uncompressed data.  Just collect information.  Unfortunately the GZIP
	// member header doesn't give us the information we need without
	// decompressing.

	int status = 0;
	deflateState *deflate = NULL;
	unsigned crc32Sum = 0;

	if (visopsys_in_kernel)
		return (status = ERR_BUG);

	// Check params.  It's OK for prog to be NULL.
	if (!inStream || !info)
	{
		fprintf(stderr, "NULL parameter\n");
		return (status = ERR_NULLPARAMETER);
	}

	memset(info, 0, sizeof(archiveMemberInfo));

	DEBUGMSG("GZIP get member info\n");

	// Record where the member starts
	info->startOffset = ftell(inStream);

	status = gzipReadMemberHeader(inStream, info);
	if (status <= 0)
		goto out;

	// Record where the data starts
	info->dataOffset = ftell(inStream);

	deflate = calloc(1, sizeof(deflateState));
	if (!deflate)
	{
		fprintf(stderr, "Memory error\n");
		status = ERR_MEMORY;
		goto out;
	}

	status = deflateDecompressFileData(deflate, inStream, NULL, prog);
	if (status < 0)
		goto out;

	// Record the data size
	info->compressedDataSize = (ftell(inStream) - info->dataOffset);

	if (fread(&crc32Sum, sizeof(unsigned), 1, inStream) < 1)
	{
		fprintf(stderr, "Error reading CRC32\n");
		status = ERR_NODATA;
		goto out;
	}

	// Read decompressed size
	if (fread(&info->decompressedDataSize, sizeof(unsigned), 1, inStream) < 1)
	{
		fprintf(stderr, "Error reading decompressed size\n");
		status = ERR_NODATA;
		goto out;
	}

	// Record the total member size
	info->totalSize = (ftell(inStream) - info->startOffset);

	// Return success
	status = 1;

out:
	if (deflate)
		free(deflate);

	if (status <= 0)
	{
		if (info->name)
			free(info->name);

		if (info->comment)
			free(info->comment);

		memset(info, 0, sizeof(archiveMemberInfo));
	}

	return (status);
}


int gzipExtractNextMember(FILE *inStream, int memberNum,
	const char *outFileName, progress *prog)
{
	// Decompress and extract the current member of a GZIP file.  This implies
	// the DEFLATE compression algorithm.

	int status = 0;
	archiveMemberInfo info;
	char constOutFileName[MAX_NAME_LENGTH];
	FILE *outStream = NULL;
	deflateState *deflate = NULL;
	unsigned fileCrc32Sum = 0;
	unsigned decompressedSize = 0;

	// Check params.  It's OK for outFileName and prog to be NULL.
	if (!inStream)
	{
		fprintf(stderr, "NULL parameter\n");
		status = ERR_NULLPARAMETER;
		goto out;
	}

	memset(&info, 0, sizeof(archiveMemberInfo));

	DEBUGMSG("GZIP extract next member\n");

	status = gzipReadMemberHeader(inStream, &info);
	if (status < 0)
		goto out;

	if (!status)
		// Finished, we guess
		goto out;

	// Create the output file name
	if (!outFileName)
	{
		if (info.name)
		{
			// We read a name from the file
			strcpy(constOutFileName, info.name);
			free(info.name);
			info.name = NULL;
		}
		else
		{
			// We have to make something up
			sprintf(constOutFileName, "gzip-out.%d", memberNum);
		}

		outFileName = constOutFileName;
	}

	// We don't currently use the comment for anything in this function
	if (info.comment)
	{
		free(info.comment);
		info.comment = NULL;
	}

	deflate = calloc(1, sizeof(deflateState));
	if (!deflate)
	{
		fprintf(stderr, "Memory error\n");
		status = ERR_MEMORY;
		goto out;
	}

	// Open output stream
	outStream = fopen(outFileName, "w");
	if (!outStream)
	{
		fprintf(stderr, "Couldn't open %s\n", outFileName);
		status = ERR_NOCREATE;
		goto out;
	}

	// Decompress the data
	status = deflateDecompressFileData(deflate, inStream, outStream, prog);

	if (prog && (lockGet(&prog->progLock) >= 0))
	{
		fflush(outStream);
		sprintf((char *) prog->statusMessage, "Decompressed size %u",
			outStream->f.size);
		lockRelease(&prog->progLock);
	}

	fclose(outStream);

	if (status < 0)
	{
		// Delete the incomplete output file
		fileDelete(outFileName);
		goto out;
	}

	// Read CRC32
	DEBUGMSG("Reading CRC\n");
	if (fread(&fileCrc32Sum, sizeof(unsigned), 1, inStream) < 1)
	{
		fprintf(stderr, "Error reading CRC\n");
		status = ERR_NODATA;
		goto out;
	}

	DEBUGMSG("Data CRC32: %08x\n", deflate->crc32Sum);
	DEBUGMSG("File CRC32: %08x\n", fileCrc32Sum);

	// Read decompressed size
	DEBUGMSG("Reading decompressed size\n");
	if (fread(&decompressedSize, sizeof(unsigned), 1, inStream) < 1)
	{
		fprintf(stderr, "Error reading decompressed size\n");
		status = ERR_NODATA;
		goto out;
	}

	DEBUGMSG("Decompressed size: %u\n", decompressedSize);

	// Check that the checksums match
	if (deflate->crc32Sum != fileCrc32Sum)
		fprintf(stderr, "%s CRC32 checksum mismatch (expected %08x, got "
			"%08x)\n", outFileName, fileCrc32Sum, deflate->crc32Sum);

	free(deflate);
	deflate = NULL;

	// Return success
	status = 1;

out:
	if (deflate)
		free(deflate);

	if (info.comment)
		free(info.comment);

	if (info.name)
		free(info.name);

	if (status < 0)
		errno = status;

	return (status);
}


int gzipExtractMember(const char *inFileName, const char *memberName,
	int memberIndex, const char *outFileName, progress *prog)
{
	// Extract a member from a GZIP file, either using the member name or the
	// index of the member -- a member name need not be unique, or it may not
	// be known

	int status = 0;
	FILE *inStream = NULL;
	archiveMemberInfo info;
	int memberCount;

	if (visopsys_in_kernel)
	{
		status = ERR_BUG;
		goto out;
	}

	// Check params.  It's OK for memberName and outFileName and prog to be
	// NULL.
	if (!inFileName)
	{
		fprintf(stderr, "NULL parameter\n");
		status = ERR_NULLPARAMETER;
		goto out;
	}

	if (memberName)
		DEBUGMSG("GZIP extract %s from %s\n", memberName, inFileName);
	else
		DEBUGMSG("GZIP extract member %d from %s\n", memberIndex, inFileName);

	inStream = fopen(inFileName, "r");
	if (!inStream)
	{
		fprintf(stderr, "Couldn't open %s\n", inFileName);
		status = ERR_NOSUCHFILE;
		goto out;
	}

	for (memberCount = 0; ; memberCount ++)
	{
		status = gzipMemberInfo(inStream, &info, NULL);
		if (!status)
		{
			// No more entries
			fprintf(stderr, "Member not found\n");
			status = ERR_NOSUCHENTRY;
			break;
		}
		else if (status < 0)
		{
			fprintf(stderr, "Couldn't get member info\n");
			break;
		}

		if ((memberName && !strcmp(memberName, info.name)) ||
			(!memberName && (memberCount == memberIndex)))
		{
			// This is the one we're extracting
			DEBUGMSG("Found member to extract, offset %u\n", info.startOffset);

			fseek(inStream, info.startOffset, SEEK_SET);

			status = gzipExtractNextMember(inStream, memberIndex, outFileName,
				prog);
			break;
		}
	}

out:
	if (inStream)
		fclose(inStream);

	if (status < 0)
		errno = status;

	return (status);
}


int gzipExtract(const char *inFileName, progress *prog)
{
	// Decompress and extract a GZIP file.  This implies the DEFLATE
	// compression algorithm.

	int status = 0;
	FILE *inStream = NULL;
	int memberCount;

	if (visopsys_in_kernel)
	{
		status = ERR_BUG;
		goto out;
	}

	// Check params.  It's OK for prog to be NULL.
	if (!inFileName)
	{
		fprintf(stderr, "NULL parameter\n");
		status = ERR_NULLPARAMETER;
		goto out;
	}

	DEBUGMSG("GZIP extract %s\n", inFileName);

	inStream = fopen(inFileName, "r");
	if (!inStream)
	{
		fprintf(stderr, "Couldn't open %s\n", inFileName);
		status = ERR_NOSUCHFILE;
		goto out;
	}

	for (memberCount = 0; ; memberCount ++)
	{
		status = gzipExtractNextMember(inStream, memberCount, NULL, prog);
		if (status <= 0)
			goto out;
	}

	// Return success
	status = 0;

out:
	if (inStream)
		fclose(inStream);

	if (status < 0)
		errno = status;

	return (status);
}


int gzipDeleteMember(const char *inFileName, const char *memberName,
	int memberIndex, progress *prog)
{
	// Delete a member from a GZIP file, either using the member name or the
	// index of the member -- a member name need not be unique, or it may not
	// be known

	int status = 0;
	FILE *inStream = NULL;
	char outFileName[MAX_PATH_NAME_LENGTH];
	FILE *outStream = NULL;
	archiveMemberInfo info;
	unsigned outputSize = 0;
	int deleted = 0;
	int memberCount;

	if (visopsys_in_kernel)
	{
		status = ERR_BUG;
		goto out;
	}

	// Check params.  It's OK for memberName and prog to be NULL.
	if (!inFileName)
	{
		fprintf(stderr, "NULL parameter\n");
		status = ERR_NULLPARAMETER;
		goto out;
	}

	if (memberName)
		DEBUGMSG("GZIP delete %s from %s\n", memberName, inFileName);
	else
		DEBUGMSG("GZIP delete member %d from %s\n", memberIndex, inFileName);

	inStream = fopen(inFileName, "r");
	if (!inStream)
	{
		fprintf(stderr, "Couldn't open %s\n", inFileName);
		status = ERR_NOSUCHFILE;
		goto out;
	}

	if (prog)
	{
		memset((void *) prog, 0, sizeof(progress));
		prog->numTotal = inStream->f.size;
	}

	// Open a temporary file for output
	sprintf(outFileName, "%s.tmp", inFileName);
	outStream = fopen(outFileName, "w");
	if (!outStream)
	{
		fprintf(stderr, "Couldn't open %s\n", outFileName);
		status = ERR_NOSUCHFILE;
		goto out;
	}

	DEBUGMSG("Using temporary file %s\n", outFileName);

	for (memberCount = 0; ; memberCount ++)
	{
		status = gzipMemberInfo(inStream, &info, NULL);
		if (!status)
		{
			// Finished
			break;
		}
		else if (status < 0)
		{
			fprintf(stderr, "Couldn't get member info\n");
			goto out;
		}

		if (deleted || (memberName && strcmp(memberName, info.name)) ||
			(!memberName && (memberCount != memberIndex)))
		{
			// We're not deleting this one.  Write it out to the temporary
			// file.

			DEBUGMSG("Re-write member from offset %u\n", info.startOffset);

			fseek(inStream, info.startOffset, SEEK_SET);

			status = copyFileData(inStream, outStream, info.totalSize,
				NULL /* progress */);
			if (status < 0)
				goto out;

			outputSize += info.totalSize;
		}
		else
		{
			// This is the one we're deleting
			DEBUGMSG("Found member to delete, offset %u\n", info.startOffset);
			deleted = 1;
		}

		if (prog)
		{
			prog->numFinished = ftell(inStream);
			prog->percentFinished = ((prog->numFinished * 100) /
				prog->numTotal);
			lockRelease(&prog->progLock);
		}
	}

	fclose(outStream);
	outStream = NULL;
	fclose(inStream);
	inStream = NULL;

	if (!deleted)
	{
		fprintf(stderr, "Member not found\n");
		status = ERR_NOSUCHENTRY;
		goto out;
	}

	// Replace the input file with the temporary file
	status = fileMove(outFileName, inFileName);

	// Did we just create an empty file?
	if (!outputSize)
		fileDelete(inFileName);

out:
	if (outStream)
		fclose(outStream);

	if (inStream)
		fclose(inStream);

	if (status < 0)
		fileDelete(outFileName);

	if (status < 0)
		errno = status;

	return (status);
}


int tarMemberInfo(FILE *inStream, archiveMemberInfo *info,
	progress *prog __attribute__((unused)))
{
	// Fill in the info structure from data pointed to by the current file
	// pointer.

	int status = 0;

	if (visopsys_in_kernel)
	{
		status = ERR_BUG;
		goto out;
	}

	// Check params.  It's OK for prog to be NULL.
	if (!inStream || !info)
	{
		fprintf(stderr, "NULL parameter\n");
		return (status = ERR_NULLPARAMETER);
	}

	memset(info, 0, sizeof(archiveMemberInfo));

	DEBUGMSG("TAR get member info\n");

	status = tarReadMemberHeader(inStream, info);
	if (status <= 0)
		goto out;

	// Seek to the end of the member
	status = fseek(inStream, (info->totalSize - sizeof(tarHeader)), SEEK_CUR);
	if (status < 0)
		return (status);

	// Return success
	status = 1;

out:
	if (status <= 0)
	{
		if (info->name)
			free(info->name);

		memset(info, 0, sizeof(archiveMemberInfo));

		if (status < 0)
			errno = status;
	}

	return (status);
}


int tarExtractNextMember(FILE *inStream, progress *prog)
{
	// Extract the current member of a TAR file.

	int status = 0;
	archiveMemberInfo info;
	char *destDir = NULL;
	FILE *outStream = NULL;

	if (visopsys_in_kernel)
	{
		status = ERR_BUG;
		goto out;
	}

	// Check params.  It's OK for prog to be NULL.
	if (!inStream)
	{
		fprintf(stderr, "NULL parameter\n");
		status = ERR_NULLPARAMETER;
		goto out;
	}

	memset(&info, 0, sizeof(archiveMemberInfo));

	DEBUGMSG("TAR extract member\n");

	status = tarReadMemberHeader(inStream, &info);
	if (status < 0)
		goto out;

	if (!status)
		// Finished, we guess
		goto out;

	// Create output parent directory, if necessary
	destDir = dirname(info.name);
	if (destDir && strcmp(destDir, "."))
	{
		DEBUGMSG("TAR make parent directory %s\n", destDir);
		status = makeDirRecursive(destDir);
		if (status < 0)
			goto out;
	}

	if ((fileType) info.mode == dirT)
	{
		DEBUGMSG("TAR make directory %s\n", info.name);
		status = makeDirRecursive(info.name);
		if (status < 0)
			goto out;
	}
	else if ((fileType) info.mode == linkT)
	{
		// Ignore this for the time being
		DEBUGMSG("TAR ignoring link %s\n", info.name);
	}
	else
	{
		// Open output stream
		DEBUGMSG("TAR create file %s\n", info.name);
		outStream = fopen(info.name, "w");
		if (!outStream)
		{
			fprintf(stderr, "Couldn't open %s\n", info.name);
			status = ERR_NOCREATE;
			goto out;
		}

		if (info.compressedDataSize)
		{
			// Copy the data
			DEBUGMSG("TAR write %u bytes\n", info.compressedDataSize);
			status = copyFileData(inStream, outStream, info.compressedDataSize,
				prog);
		}

		fclose(outStream);

		if (status < 0)
		{
			// Delete the incomplete output file
			fileDelete(info.name);
			goto out;
		}
	}

	// Return success
	status = 1;

out:
	if (destDir)
		free(destDir);

	if (info.name)
		free(info.name);

	if (status < 0)
		errno = status;

	// Seek to the end of the member
	fseek(inStream, (info.startOffset + info.totalSize), SEEK_SET);

	return (status);
}


int tarExtractMember(const char *inFileName, const char *memberName,
	int memberIndex, progress *prog)
{
	// Extract a member from a TAR file, either using the member name or the
	// index of the member -- a member name need not be unique, or it may not
	// be known

	int status = 0;
	FILE *inStream = NULL;
	archiveMemberInfo info;
	int memberCount;

	if (visopsys_in_kernel)
	{
		status = ERR_BUG;
		goto out;
	}

	// Check params.  It's OK for memberName and prog to be NULL.
	if (!inFileName)
	{
		fprintf(stderr, "NULL parameter\n");
		status = ERR_NULLPARAMETER;
		goto out;
	}

	if (memberName)
		DEBUGMSG("TAR extract %s from %s\n", memberName, inFileName);
	else
		DEBUGMSG("TAR extract member %d from %s\n", memberIndex, inFileName);

	inStream = fopen(inFileName, "r");
	if (!inStream)
	{
		fprintf(stderr, "Couldn't open %s\n", inFileName);
		status = ERR_NOSUCHFILE;
		goto out;
	}

	for (memberCount = 0; ; memberCount ++)
	{
		status = tarMemberInfo(inStream, &info, NULL);
		if (!status)
		{
			// No more entries
			fprintf(stderr, "Member not found\n");
			status = ERR_NOSUCHENTRY;
			break;
		}
		else if (status < 0)
		{
			fprintf(stderr, "Couldn't get member info\n");
			break;
		}

		if ((memberName && !strcmp(memberName, info.name)) ||
			(!memberName && (memberCount == memberIndex)))
		{
			// This is the one we're extracting
			DEBUGMSG("Found member to extract, offset %u\n", info.startOffset);

			fseek(inStream, info.startOffset, SEEK_SET);

			status = tarExtractNextMember(inStream, prog);
			break;
		}
	}

out:
	if (inStream)
		fclose(inStream);

	if (status < 0)
		errno = status;

	return (status);
}


int tarExtract(const char *inFileName, progress *prog)
{
	// Extract a TAR file.

	int status = 0;
	FILE *inStream = NULL;

	if (visopsys_in_kernel)
	{
		status = ERR_BUG;
		goto out;
	}

	// Check params.  It's OK for prog to be NULL.
	if (!inFileName)
	{
		fprintf(stderr, "NULL parameter\n");
		status = ERR_NULLPARAMETER;
		goto out;
	}

	DEBUGMSG("TAR extract %s\n", inFileName);

	inStream = fopen(inFileName, "r");
	if (!inStream)
	{
		fprintf(stderr, "Couldn't open %s\n", inFileName);
		status = ERR_NOSUCHFILE;
		goto out;
	}

	for (status = 1; status > 0; )
		status = tarExtractNextMember(inStream, prog);

	if (status < 0)
		goto out;

	// Return success
	status = 0;

out:
	if (inStream)
		fclose(inStream);

	if (status < 0)
		errno = status;

	return (status);
}


int archiveAddMember(const char *inFileName, const char *outFileName,
	const char *comment, progress *prog)
{
	// Determine the archive type, and if supported, add the new member to it.
	// If the archive doesn't exist, create it (we choose the type).

	int status = 0;
	file theFile;
	loaderFileClass class;

	if (visopsys_in_kernel)
	{
		status = ERR_BUG;
		goto out;
	}

	// Check params.  It's OK for outFileName, comment, and prog to be NULL.
	if (!inFileName)
	{
		fprintf(stderr, "NULL parameter\n");
		status = ERR_NULLPARAMETER;
		goto out;
	}

	memset(&class, 0, sizeof(loaderFileClass));

	DEBUGMSG("Add %s to archive %s\n", inFileName, outFileName);

	// Does the archive already exist?
	if (outFileName && (fileFind(outFileName, &theFile) >= 0))
	{
		// What kind of file have we got?  Ask the runtime loader to classify
		// it.
		if (!loaderClassifyFile(outFileName, &class))
		{
			fprintf(stderr, "Unable to determine file type of %s\n",
				outFileName);
			status = ERR_INVALID;
			goto out;
		}
	}
	else
	{
		// We will choose GZIP.
		class.class = LOADERFILECLASS_ARCHIVE;
		class.subClass = LOADERFILESUBCLASS_GZIP;
	}

	if (class.class & LOADERFILECLASS_ARCHIVE)
	{
		if (class.subClass & LOADERFILESUBCLASS_GZIP)
		{
			status = gzipCompressFile(inFileName, outFileName, comment,
				1 /* append */, prog);
		}
		else if (class.subClass & LOADERFILESUBCLASS_ZIP)
		{
			fprintf(stderr, "%s archives are not yet supported\n",
				class.className);
			status = ERR_NOTIMPLEMENTED;
		}
		else
		{
			fprintf(stderr, "%s (%s) is not a supported archive file type\n",
				inFileName, class.className);
			status = ERR_NOTIMPLEMENTED;
		}
	}
	else
	{
		fprintf(stderr, "%s (%s) is not a recognized archive file type\n",
			inFileName, class.className);
		status = ERR_INVALID;
	}

out:
	if (status < 0)
		errno = status;

	return (status);
}


int archiveInfo(const char *inFileName, archiveMemberInfo **info,
	progress *prog)
{
	// Determine the archive type, and return info about the members.

	int status = 0;
	loaderFileClass class;
	FILE *inStream = NULL;
	int memberCount = 0;

	if (visopsys_in_kernel)
	{
		status = ERR_BUG;
		goto out;
	}

	// Check params.  It's OK for prog to be NULL.
	if (!inFileName || !info)
	{
		fprintf(stderr, "NULL parameter\n");
		status = ERR_NULLPARAMETER;
		goto out;
	}

	memset(&class, 0, sizeof(loaderFileClass));

	DEBUGMSG("Archive info for %s\n", inFileName);

	// What kind of file have we got?  Ask the runtime loader to classify it.
	if (!loaderClassifyFile(inFileName, &class))
	{
		fprintf(stderr, "Unable to determine file type of %s\n", inFileName);
		status = ERR_INVALID;
		goto out;
	}

	if (class.class & LOADERFILECLASS_ARCHIVE)
	{
		if ((class.subClass & LOADERFILESUBCLASS_GZIP) ||
			(class.subClass & LOADERFILESUBCLASS_TAR))
		{
			inStream = fopen(inFileName, "r");
			if (!inStream)
			{
				fprintf(stderr, "Couldn't open %s\n", inFileName);
				status = ERR_NOSUCHFILE;
				goto out;
			}

			do
			{
				*info = realloc(*info, ((memberCount + 1) *
					sizeof(archiveMemberInfo)));
				if (!*info)
				{
					status = ERR_MEMORY;
					goto out;
				}

				if (class.subClass & LOADERFILESUBCLASS_GZIP)
				{
					status = gzipMemberInfo(inStream, &((*info)[memberCount]),
						prog);
				}
				else if (class.subClass & LOADERFILESUBCLASS_TAR)
				{
					status = tarMemberInfo(inStream, &((*info)[memberCount]),
						prog);
				}

				if (status > 0)
					memberCount += 1;

			} while (status > 0);
		}
		else if (class.subClass & LOADERFILESUBCLASS_ZIP)
		{
			fprintf(stderr, "%s archives are not yet supported\n",
				class.className);
			status = ERR_NOTIMPLEMENTED;
		}
		else
		{
			fprintf(stderr, "%s (%s) is not a supported archive file type\n",
				inFileName, class.className);
			status = ERR_NOTIMPLEMENTED;
		}
	}
	else
	{
		fprintf(stderr, "%s (%s) is not a recognized archive file type\n",
			inFileName, class.className);
		status = ERR_INVALID;
	}

	status = memberCount;

out:
	if (status < 0)
	{
		if (*info)
		{
			archiveInfoFree(*info, memberCount);
			*info = NULL;
		}

		errno = status;
	}

	return (status);
}


void archiveInfoFree(archiveMemberInfo *info, int memberCount)
{
	if (info)
	{
		while (memberCount)
		{
			if (info[memberCount - 1].name)
				free(info[memberCount - 1].name);

			if (info[memberCount - 1].comment)
				free(info[memberCount - 1].comment);

			memberCount -= 1;
		}

		free(info);
	}
}


int archiveExtractMember(const char *inFileName, const char *memberName,
	int memberIndex, const char *outFileName, progress *prog)
{
	// Determine the archive type, and if supported, extract the member from
	// it, either using the member name or the index of the member -- a member
	// name may not be unique, or it may not be known.

	int status = 0;
	loaderFileClass class;

	if (visopsys_in_kernel)
	{
		status = ERR_BUG;
		goto out;
	}

	// Check params.  It's OK for memberName and outFileName and prog to be
	// NULL.
	if (!inFileName)
	{
		fprintf(stderr, "NULL parameter\n");
		status = ERR_NULLPARAMETER;
		goto out;
	}

	memset(&class, 0, sizeof(loaderFileClass));

	if (memberName)
		DEBUGMSG("Extract %s from archive %s\n", memberName, inFileName);
	else
		DEBUGMSG("Extract member %d from archive %s\n", memberIndex,
			inFileName);

	// What kind of file have we got?  Ask the runtime loader to classify it.
	if (!loaderClassifyFile(inFileName, &class))
	{
		fprintf(stderr, "Unable to determine file type of %s\n", inFileName);
		status = ERR_INVALID;
		goto out;
	}

	if (class.class & LOADERFILECLASS_ARCHIVE)
	{
		if (class.subClass & LOADERFILESUBCLASS_GZIP)
		{
			status = gzipExtractMember(inFileName, memberName, memberIndex,
				outFileName, prog);
		}
		else if (class.subClass & LOADERFILESUBCLASS_TAR)
		{
			status = tarExtractMember(inFileName, memberName, memberIndex,
				prog);
		}
		else if (class.subClass & LOADERFILESUBCLASS_ZIP)
		{
			fprintf(stderr, "%s archives are not yet supported\n",
				class.className);
			status = ERR_NOTIMPLEMENTED;
		}
		else
		{
			fprintf(stderr, "%s (%s) is not a supported archive file type\n",
				inFileName, class.className);
			status = ERR_NOTIMPLEMENTED;
		}
	}
	else
	{
		fprintf(stderr, "%s (%s) is not a recognized archive file type\n",
			inFileName, class.className);
		status = ERR_INVALID;
	}

out:
	if (status < 0)
		errno = status;

	return (status);
}


int archiveExtract(const char *inFileName, progress *prog)
{
	// Determine the archive type, and extract/decompress the members.

	int status = 0;
	loaderFileClass class;

	if (visopsys_in_kernel)
	{
		status = ERR_BUG;
		goto out;
	}

	// Check params.  It's OK for outFileName and prog to be NULL.
	if (!inFileName)
	{
		fprintf(stderr, "NULL parameter\n");
		status = ERR_NULLPARAMETER;
		goto out;
	}

	memset(&class, 0, sizeof(loaderFileClass));

	DEBUGMSG("Extract archive %s\n", inFileName);

	// What kind of file have we got?  Ask the runtime loader to classify it.
	if (!loaderClassifyFile(inFileName, &class))
	{
		fprintf(stderr, "Unable to determine file type of %s\n", inFileName);
		status = ERR_INVALID;
		goto out;
	}

	if (class.class & LOADERFILECLASS_ARCHIVE)
	{
		if (class.subClass & LOADERFILESUBCLASS_GZIP)
		{
			status = gzipExtract(inFileName, prog);
		}
		else if (class.subClass & LOADERFILESUBCLASS_TAR)
		{
			status = tarExtract(inFileName, prog);
		}
		else if (class.subClass & LOADERFILESUBCLASS_ZIP)
		{
			fprintf(stderr, "%s archives are not yet supported\n",
				class.className);
			status = ERR_NOTIMPLEMENTED;
		}
		else
		{
			fprintf(stderr, "%s (%s) is not a supported archive file type\n",
				inFileName, class.className);
			status = ERR_NOTIMPLEMENTED;
		}
	}
	else
	{
		fprintf(stderr, "%s (%s) is not a recognized archive file type\n",
			inFileName, class.className);
		status = ERR_INVALID;
	}

out:
	if (status < 0)
		errno = status;

	return (status);
}


int archiveDeleteMember(const char *inFileName, const char *memberName,
	int memberIndex, progress *prog)
{
	// Determine the archive type, and if supported, delete the member from
	// it, either using the member name or the index of the member -- a member
	// name may not be unique, or it may not be known.

	int status = 0;
	loaderFileClass class;

	if (visopsys_in_kernel)
	{
		status = ERR_BUG;
		goto out;
	}

	// Check params.  It's OK for memberName and prog to be NULL.
	if (!inFileName)
	{
		fprintf(stderr, "NULL parameter\n");
		status = ERR_NULLPARAMETER;
		goto out;
	}

	memset(&class, 0, sizeof(loaderFileClass));

	if (memberName)
		DEBUGMSG("Delete %s from archive %s\n", memberName, inFileName);
	else
		DEBUGMSG("Delete member %d from archive %s\n", memberIndex,
			inFileName);

	// What kind of file have we got?  Ask the runtime loader to classify it.
	if (!loaderClassifyFile(inFileName, &class))
	{
		fprintf(stderr, "Unable to determine file type of %s\n", inFileName);
		status = ERR_INVALID;
		goto out;
	}

	if (class.class & LOADERFILECLASS_ARCHIVE)
	{
		if (class.subClass & LOADERFILESUBCLASS_GZIP)
		{
			status = gzipDeleteMember(inFileName, memberName, memberIndex,
				prog);
		}
		else if (class.subClass & LOADERFILESUBCLASS_ZIP)
		{
			fprintf(stderr, "%s archives are not yet supported\n",
				class.className);
			status = ERR_NOTIMPLEMENTED;
		}
		else
		{
			fprintf(stderr, "%s (%s) is not a supported archive file type\n",
				inFileName, class.className);
			status = ERR_NOTIMPLEMENTED;
		}
	}
	else
	{
		fprintf(stderr, "%s (%s) is not a recognized archive file type\n",
			inFileName, class.className);
		status = ERR_INVALID;
	}

out:
	if (status < 0)
		errno = status;

	return (status);
}

