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
//  libcompress.c
//

// This is the main entry point for our library of compression functions

#include "libcompress.h"
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/api.h>
#include <sys/loader.h>

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


/////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////
//
// Below here, the functions are exported for external use
//
/////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////

int archiveAddMember(const char *inFileName, const char *outFileName,
	int type, const char *comment, progress *prog)
{
	// Determine the archive type, and if supported, add the new member to it.
	// If the archive doesn't exist, create it (we choose the type unless
	// specified).

	int status = 0;
	file f;
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

	memset(&f, 0, sizeof(file));
	memset(&class, 0, sizeof(loaderFileClass));

	DEBUGMSG("Add %s to archive %s\n", inFileName, outFileName);

	// Does the archive already exist and have content?
	if (outFileName && (fileFind(outFileName, &f) >= 0) && f.size)
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
		class.type = LOADERFILECLASS_ARCHIVE;

		if (type)
			class.subType = type;
		else
			// We will choose GZIP.
			class.subType = LOADERFILESUBCLASS_GZIP;
	}

	if (class.type & LOADERFILECLASS_ARCHIVE)
	{
		if (class.subType & LOADERFILESUBCLASS_GZIP)
		{
			status = gzipCompressFile(inFileName, outFileName, comment,
				1 /* append */, prog);
		}
		else if (class.subType & LOADERFILESUBCLASS_TAR)
		{
			status = tarAddMember(inFileName, outFileName, prog);
		}
		else if (class.subType & LOADERFILESUBCLASS_ZIP)
		{
			fprintf(stderr, "%s archives are not yet supported\n",
				class.name);
			status = ERR_NOTIMPLEMENTED;
		}
		else
		{
			fprintf(stderr, "%s (%s) is not a supported archive file type\n",
				inFileName, class.name);
			status = ERR_NOTIMPLEMENTED;
		}
	}
	else
	{
		fprintf(stderr, "%s (%s) is not a recognized archive file type\n",
			inFileName, class.name);
		status = ERR_INVALID;
	}

out:
	if (status < 0)
		errno = status;

	return (status);
}


int archiveAddRecursive(const char *inFileName, const char *outFileName,
	int type, const char *comment, progress *prog)
{
	// Call archiveAddMember() - recursively if inFileName names a directory

	int status = 0;
	file f;
	char *newFileName = NULL;

	// Check params.  It's OK for outFileName, comment, and prog to be NULL.
	if (!inFileName)
	{
		fprintf(stderr, "NULL parameter\n");
		status = ERR_NULLPARAMETER;
		goto out;
	}

	// Initialize the file structure
	memset(&f, 0, sizeof(file));

	// Locate the file
	status = fileFind(inFileName, &f);
	if (status < 0)
	{
		fprintf(stderr, "No such file %s\n", inFileName);
		goto out;
	}

	if (prog)
		sprintf((char *) prog->statusMessage, "Adding %s", inFileName);

	// Add the member we were passed
	status = archiveAddMember(inFileName, outFileName, type, comment, prog);
	if (status < 0)
		goto out;

	// If the member is a directory, recurse for each of its members
	if (f.type == dirT)
	{
		// Initialize the file structure
		memset(&f, 0, sizeof(file));

		// Get the first item in the directory
		status = fileFirst(inFileName, &f);
		if (status < 0)
			goto out;

		// Loop through the contents of the directory
		while (1)
		{
			if (strcmp(f.name, ".") && strcmp(f.name, ".."))
			{
				newFileName = malloc(strlen(inFileName) + strlen(f.name) + 2);

				if (newFileName)
				{
					// Construct the relative pathname for this member
					sprintf(newFileName, "%s%s%s", inFileName,
						((strlen(inFileName) &&
							(inFileName[strlen(inFileName) - 1] == '/'))?
						"" : "/"), f.name);

					status = archiveAddRecursive(newFileName, outFileName,
						type, comment, prog);

					free(newFileName);

					if (status < 0)
						goto out;
				}
			}

			// Move to the next item
			status = fileNext(inFileName, &f);
			if (status < 0)
				break;
		}
	}

	status = 0;

out:
	if (status < 0)
		errno = status;

	return (status);
}


int archiveCopyFileData(FILE *inStream, FILE *outStream, unsigned totalBytes,
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

	if (class.type & LOADERFILECLASS_ARCHIVE)
	{
		if ((class.subType & LOADERFILESUBCLASS_GZIP) ||
			(class.subType & LOADERFILESUBCLASS_TAR))
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

				if (class.subType & LOADERFILESUBCLASS_GZIP)
				{
					status = gzipMemberInfo(inStream, &((*info)[memberCount]),
						prog);
				}
				else if (class.subType & LOADERFILESUBCLASS_TAR)
				{
					status = tarMemberInfo(inStream, &((*info)[memberCount]),
						prog);
				}

				if (status > 0)
					memberCount += 1;

			} while (status > 0);
		}
		else if (class.subType & LOADERFILESUBCLASS_ZIP)
		{
			fprintf(stderr, "%s archives are not yet supported\n",
				class.name);
			status = ERR_NOTIMPLEMENTED;
		}
		else
		{
			fprintf(stderr, "%s (%s) is not a supported archive file type\n",
				inFileName, class.name);
			status = ERR_NOTIMPLEMENTED;
		}
	}
	else
	{
		fprintf(stderr, "%s (%s) is not a recognized archive file type\n",
			inFileName, class.name);
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


void archiveInfoContentsFree(archiveMemberInfo *info)
{
	if (info->name)
		free(info->name);

	if (info->comment)
		free(info->comment);

	memset(info, 0, sizeof(archiveMemberInfo));
}


void archiveInfoFree(archiveMemberInfo *info, int memberCount)
{
	if (info)
	{
		while (memberCount)
		{
			archiveInfoContentsFree(&info[memberCount - 1]);
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
	{
		DEBUGMSG("Extract %s from archive %s\n", memberName, inFileName);
	}
	else
	{
		DEBUGMSG("Extract member %d from archive %s\n", memberIndex,
			inFileName);
	}

	// What kind of file have we got?  Ask the runtime loader to classify it.
	if (!loaderClassifyFile(inFileName, &class))
	{
		fprintf(stderr, "Unable to determine file type of %s\n", inFileName);
		status = ERR_INVALID;
		goto out;
	}

	if (class.type & LOADERFILECLASS_ARCHIVE)
	{
		if (class.subType & LOADERFILESUBCLASS_GZIP)
		{
			status = gzipExtractMember(inFileName, memberName, memberIndex,
				outFileName, prog);
		}
		else if (class.subType & LOADERFILESUBCLASS_TAR)
		{
			status = tarExtractMember(inFileName, memberName, memberIndex,
				prog);
		}
		else if (class.subType & LOADERFILESUBCLASS_ZIP)
		{
			fprintf(stderr, "%s archives are not yet supported\n",
				class.name);
			status = ERR_NOTIMPLEMENTED;
		}
		else
		{
			fprintf(stderr, "%s (%s) is not a supported archive file type\n",
				inFileName, class.name);
			status = ERR_NOTIMPLEMENTED;
		}
	}
	else
	{
		fprintf(stderr, "%s (%s) is not a recognized archive file type\n",
			inFileName, class.name);
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

	if (class.type & LOADERFILECLASS_ARCHIVE)
	{
		if (class.subType & LOADERFILESUBCLASS_GZIP)
		{
			status = gzipExtract(inFileName, prog);
		}
		else if (class.subType & LOADERFILESUBCLASS_TAR)
		{
			status = tarExtract(inFileName, prog);
		}
		else if (class.subType & LOADERFILESUBCLASS_ZIP)
		{
			fprintf(stderr, "%s archives are not yet supported\n",
				class.name);
			status = ERR_NOTIMPLEMENTED;
		}
		else
		{
			fprintf(stderr, "%s (%s) is not a supported archive file type\n",
				inFileName, class.name);
			status = ERR_NOTIMPLEMENTED;
		}
	}
	else
	{
		fprintf(stderr, "%s (%s) is not a recognized archive file type\n",
			inFileName, class.name);
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
	{
		DEBUGMSG("Delete %s from archive %s\n", memberName, inFileName);
	}
	else
	{
		DEBUGMSG("Delete member %d from archive %s\n", memberIndex,
			inFileName);
	}

	// What kind of file have we got?  Ask the runtime loader to classify it.
	if (!loaderClassifyFile(inFileName, &class))
	{
		fprintf(stderr, "Unable to determine file type of %s\n", inFileName);
		status = ERR_INVALID;
		goto out;
	}

	if (class.type & LOADERFILECLASS_ARCHIVE)
	{
		if (class.subType & LOADERFILESUBCLASS_GZIP)
		{
			status = gzipDeleteMember(inFileName, memberName, memberIndex,
				prog);
		}
		else if (class.subType & LOADERFILESUBCLASS_TAR)
		{
			status = tarDeleteMember(inFileName, memberName, memberIndex,
				prog);
		}
		else if (class.subType & LOADERFILESUBCLASS_ZIP)
		{
			fprintf(stderr, "%s archives are not yet supported\n",
				class.name);
			status = ERR_NOTIMPLEMENTED;
		}
		else
		{
			fprintf(stderr, "%s (%s) is not a supported archive file type\n",
				inFileName, class.name);
			status = ERR_NOTIMPLEMENTED;
		}
	}
	else
	{
		fprintf(stderr, "%s (%s) is not a recognized archive file type\n",
			inFileName, class.name);
		status = ERR_INVALID;
	}

out:
	if (status < 0)
		errno = status;

	return (status);
}

