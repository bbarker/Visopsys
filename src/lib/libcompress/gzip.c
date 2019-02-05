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
//  gzip.c
//

// This is common library code for the GZIP file format.

#include "libcompress.h"
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/api.h>
#include <sys/gzip.h>
#include <sys/loader.h>
#include <sys/stat.h>

#ifdef DEBUG
	#define DEBUG_OUTMAX	160
	int debugGzip = 0;
	static char debugOutput[DEBUG_OUTMAX];

	static void DEBUGMSG(const char *message, ...)
	{
		va_list list;

		if (debugGzip)
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
	// Read the next member header of a GZIP file, and return the relevant
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
		archiveInfoContentsFree(info);
		errno = status;
	}

	return (status);
}


/////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////
//
// Below here, the functions are exported for external use
//
/////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////

int gzipAddMember(FILE *inStream, FILE *outStream, const char *memberName,
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
	if (memberName)
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

	if (memberName)
	{
		// Strip any leading '/'s
		while (memberName[0] == '/')
			memberName += 1;

		// Output the NULL-terminated member name (original file name, perhaps)
		if (fwrite(memberName, 1, (strlen(memberName) + 1), outStream) <
			(strlen(memberName) + 1))
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

	// Classify the file.  We're interested in knowing whether this is a
	// binary or text file.
	if (loaderClassifyFile(inFileName, &class))
	{
		if (class.type & LOADERFILECLASS_TEXT)
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
		status = ERR_NOSUCHFILE;
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
		archiveInfoContentsFree(info);

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

			archiveInfoContentsFree(&info);
			break;
		}

		archiveInfoContentsFree(&info);
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

			status = archiveCopyFileData(inStream, outStream, info.totalSize,
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

		archiveInfoContentsFree(&info);

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

