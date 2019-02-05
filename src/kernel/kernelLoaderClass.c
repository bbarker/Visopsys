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
//  kernelLoaderClass.c
//

// This file contains miscellaneous functions for various classes of files
// that don't have their own source files.

#include "kernelLoader.h"
#include <stdio.h>
#include <stdlib.h>
#include <sys/gzip.h>
#include <sys/keyboard.h>
#include <sys/msdos.h>
#include <sys/png.h>
#include <sys/tar.h>


static int textDetect(const char *fileName, void *dataPtr, unsigned size,
	loaderFileClass *class)
{
	// This function returns the percentage of characters that are plain text
	// if we think the supplied file data represents a text file.

	unsigned char *data = dataPtr;
	int textChars = 0;
	unsigned count;

	// Check params
	if (!fileName || !dataPtr || !size || !class)
		return (0);

	// Loop through the supplied data.  If it's at least 95% (this is an
	// arbitrary number) printable ascii, etc, say yes
	for (count = 0; count < size; count ++)
	{
		if ((data[count] == 0x0a) || (data[count] == 0x0d) ||
			(data[count] == 0x09) ||
			((data[count] >= 0x20) && (data[count] <= 0x7e)))
		{
			textChars += 1;
		}
	}

	if (((textChars * 100) / size) >= 95)
	{
		// We will call this a text file
		sprintf(class->name, "%s %s", FILECLASS_NAME_TEXT,
			FILECLASS_NAME_DATA);
		class->type = (LOADERFILECLASS_TEXT | LOADERFILECLASS_DATA);
		return (1);
	}
	else
	{
		// No
		return (0);
	}
}


static int binaryDetect(const char *fileName, void *dataPtr, unsigned size,
	loaderFileClass *class)
{
	// If it's not text, it's binary

	// Check params
	if (!fileName || !dataPtr || !class)
		return (0);

	if (!textDetect(fileName, dataPtr, size, class))
	{
		// We will call this a binary file
		sprintf(class->name, "%s %s", FILECLASS_NAME_BIN,
			FILECLASS_NAME_DATA);
		class->type = (LOADERFILECLASS_BIN | LOADERFILECLASS_DATA);
		return (1);
	}
	else
	{
		// No
		return (0);
	}
}


static int gifDetect(const char *fileName, void *dataPtr, unsigned size,
	loaderFileClass *class)
{
	// Must be binary, and have the signature 'GIF'

	#define GIF_MAGIC "GIF"

	// Check params
	if (!fileName || !dataPtr || !class)
		return (0);

	// Make sure there's enough data here for our detection
	if (size < sizeof(GIF_MAGIC))
		return (0);

	if (binaryDetect(fileName, dataPtr, size, class) &&
		!strncmp(dataPtr, GIF_MAGIC, min(size, 3)))
	{
		// We will call this a GIF image
		sprintf(class->name, "%s %s", FILECLASS_NAME_GIF,
			FILECLASS_NAME_IMAGE);
		class->type = (LOADERFILECLASS_BIN | LOADERFILECLASS_IMAGE);
		return (1);
	}
	else
	{
		// No
		return (0);
	}
}


static int pngDetect(const char *fileName, void *dataPtr, unsigned size,
	loaderFileClass *class)
{
	// Must be binary, and have the signature '0x89504E47 0x0D0A1A0A' at the
	// beginning

	unsigned *sig = dataPtr;

	// Check params
	if (!fileName || !dataPtr || !class)
		return (0);

	// Make sure there's enough data here for our detection
	if (size < (sizeof(PNG_MAGIC1) + sizeof(PNG_MAGIC2)))
		return (0);

	if (binaryDetect(fileName, dataPtr, size, class) &&
		(sig[0] == PNG_MAGIC1) && (sig[1] == PNG_MAGIC2))
	{
		// We will call this a PNG image
		sprintf(class->name, "%s %s", FILECLASS_NAME_PNG,
			FILECLASS_NAME_IMAGE);
		class->type = (LOADERFILECLASS_BIN | LOADERFILECLASS_IMAGE);
		return (1);
	}
	else
	{
		// No
		return (0);
	}
}


static int mp3Detect(const char *fileName, void *dataPtr, unsigned size,
	loaderFileClass *class)
{
	// Must be binary, and have the signature 'ID3'

	#define MP3_MAGIC "ID3"

	// Check params
	if (!fileName || !dataPtr || !class)
		return (0);

	// Make sure there's enough data here for our detection
	if (size < 3)
		return (0);

	if (binaryDetect(fileName, dataPtr, size, class) &&
		!strncmp(dataPtr, MP3_MAGIC, 3))
	{
		// We will call this an MP3 audio file
		sprintf(class->name, "%s %s", FILECLASS_NAME_MP3,
			FILECLASS_NAME_AUDIO);
		class->type = (LOADERFILECLASS_BIN | LOADERFILECLASS_AUDIO);
		return (1);
	}
	else
	{
		// No
		return (0);
	}
}


static int wavDetect(const char *fileName, void *dataPtr, unsigned size,
	loaderFileClass *class)
{
	// Must be binary, and have the signatures 'RIFF' and 'WAVE'

	#define WAV_MAGIC1 "RIFF" // followed by 4 bytes
	#define WAV_MAGIC2 "WAVE"

	// Check params
	if (!fileName || !dataPtr || !class)
		return (0);

	// Make sure there's enough data here for our detection
	if (size < 16)
		return (0);

	if (binaryDetect(fileName, dataPtr, size, class) &&
		!strncmp(dataPtr, WAV_MAGIC1, 4) &&
		!strncmp((dataPtr + 8), WAV_MAGIC2, 4))
	{
		// We will call this a WAV audio file
		sprintf(class->name, "%s %s", FILECLASS_NAME_WAV,
			FILECLASS_NAME_AUDIO);
		class->type = (LOADERFILECLASS_BIN | LOADERFILECLASS_AUDIO);
		return (1);
	}
	else
	{
		// No
		return (0);
	}
}


static int flvDetect(const char *fileName, void *dataPtr, unsigned size,
	loaderFileClass *class)
{
	// Must be binary, and have the signature 'FLV'

	#define FLV_MAGIC "FLV"

	// Check params
	if (!fileName || !dataPtr || !class)
		return (0);

	// Make sure there's enough data here for our detection
	if (size < 3)
		return (0);

	if (binaryDetect(fileName, dataPtr, size, class) &&
		!strncmp(dataPtr, FLV_MAGIC, 3))
	{
		// We will call this a FLV (Flash) video
		sprintf(class->name, "%s %s", FILECLASS_NAME_FLV,
			FILECLASS_NAME_VIDEO);
		class->type = (LOADERFILECLASS_BIN | LOADERFILECLASS_VIDEO);
		return (1);
	}
	else
	{
		// No
		return (0);
	}
}


static int aviDetect(const char *fileName, void *dataPtr, unsigned size,
	loaderFileClass *class)
{
	// Must be binary, and have the signatures 'RIFF' and 'AVI LIST'

	#define AVI_MAGIC1 "RIFF" // followed by 4 bytes
	#define AVI_MAGIC2 "AVI LIST"

	// Check params
	if (!fileName || !dataPtr || !class)
		return (0);

	// Make sure there's enough data here for our detection
	if (size < 16)
		return (0);

	if (binaryDetect(fileName, dataPtr, size, class) &&
		!strncmp(dataPtr, AVI_MAGIC1, 4) &&
		!strncmp((dataPtr + 8), AVI_MAGIC2, 8))
	{
		// We will call this a FLV (flash) video
		sprintf(class->name, "%s %s", FILECLASS_NAME_AVI,
			FILECLASS_NAME_VIDEO);
		class->type = (LOADERFILECLASS_BIN | LOADERFILECLASS_VIDEO);
		return (1);
	}
	else
	{
		// No
		return (0);
	}
}


static int mp4Detect(const char *fileName, void *dataPtr, unsigned size,
	loaderFileClass *class)
{
	// Must be binary, and have one of several possible signatures at offset 4

	#define MP4_MAGIC1 "ftypMSNV"	// MPEG-4 video file
	#define MP4_MAGIC2 "ftypisom"	// ISO Base Media file (MPEG-4) v1
	#define MP4_MAGIC3 "ftypmp42"	// MPEG-4 video|QuickTime file

	// Check params
	if (!fileName || !dataPtr || !class)
		return (0);

	// Make sure there's enough data here for our detection
	if (size < 12)
		return (0);

	if (binaryDetect(fileName, dataPtr, size, class) &&
		(!strncmp((dataPtr + 4), MP4_MAGIC1, 8) ||
			!strncmp((dataPtr + 4), MP4_MAGIC2, 8) ||
			!strncmp((dataPtr + 4), MP4_MAGIC3, 8)))
	{
		// We will call this an MP4 video
		sprintf(class->name, "%s %s", FILECLASS_NAME_MP4,
			FILECLASS_NAME_VIDEO);
		class->type = (LOADERFILECLASS_BIN | LOADERFILECLASS_VIDEO);
		return (1);
	}
	else
	{
		// No
		return (0);
	}
}


static int movDetect(const char *fileName, void *dataPtr, unsigned size,
	loaderFileClass *class)
{
	// Must be binary, and have the signature 'ftypqt  ' at offset 4

	#define MOV_MAGIC "ftypqt  "

	// Check params
	if (!fileName || !dataPtr || !class)
		return (0);

	// Make sure there's enough data here for our detection
	if (size < 12)
		return (0);

	if (binaryDetect(fileName, dataPtr, size, class) &&
		!strncmp((dataPtr + 4), MOV_MAGIC, 8))
	{
		// We will call this a QuickTime video
		sprintf(class->name, "%s %s", FILECLASS_NAME_MOV,
			FILECLASS_NAME_VIDEO);
		class->type = (LOADERFILECLASS_BIN | LOADERFILECLASS_VIDEO);
		return (1);
	}
	else
	{
		// No
		return (0);
	}
}


static int bootDetect(const char *fileName, void *dataPtr, unsigned size,
	loaderFileClass *class)
{
	// Must be binary, and have the boot signature MSDOS_BOOT_SIGNATURE in the
	// last 2 bytes

	unsigned short *sig = (dataPtr + 510);

	// Check params
	if (!fileName || !dataPtr || !class)
		return (0);

	// Make sure there's enough data here for our detection
	if (size < 512)
		return (0);

	if (binaryDetect(fileName, dataPtr, size, class) &&
		(*sig == MSDOS_BOOT_SIGNATURE))
	{
		// We will call this a boot sector
		sprintf(class->name, "%s %s", FILECLASS_NAME_BOOT,
			FILECLASS_NAME_EXEC);
		class->type = (LOADERFILECLASS_BIN | LOADERFILECLASS_EXEC |
			LOADERFILECLASS_BOOT);
		class->subType = LOADERFILESUBCLASS_STATIC;
		return (1);
	}
	else
	{
		// No
		return (0);
	}
}


static int keymapDetect(const char *fileName, void *dataPtr, unsigned size,
	loaderFileClass *class)
{
	// Must be binary, and have the magic number KEYMAP_MAGIC at the beginning

	// Check params
	if (!fileName || !dataPtr || !class)
		return (0);

	// Make sure there's enough data here for our detection
	if (size < sizeof(KEYMAP_MAGIC))
		return (0);

	if (binaryDetect(fileName, dataPtr, size, class) &&
		!strncmp(dataPtr, KEYMAP_MAGIC, min(size, sizeof(KEYMAP_MAGIC))))
	{
		// We will call this a keymap fie
		sprintf(class->name, "%s %s", FILECLASS_NAME_BIN,
			FILECLASS_NAME_KEYMAP);
		class->type = (LOADERFILECLASS_BIN | LOADERFILECLASS_KEYMAP);
		return (1);
	}
	else
	{
		// No
		return (0);
	}
}


static int pdfDetect(const char *fileName, void *dataPtr, unsigned size,
	loaderFileClass *class)
{
	// Must be binary, and have the magic number PDF_MAGIC at the beginning

	#define PDF_MAGIC "%PDF-"

	// Check params
	if (!fileName || !dataPtr || !class)
		return (0);

	// Make sure there's enough data here for our detection
	if (size < sizeof(PDF_MAGIC))
		return (0);

	if (!strncmp(dataPtr, PDF_MAGIC, min(size, 5)))
	{
		// We will call this a PDF document
		sprintf(class->name, "%s %s", FILECLASS_NAME_PDF, FILECLASS_NAME_DOC);
		class->type = (LOADERFILECLASS_BIN | LOADERFILECLASS_DOC);
		class->subType = LOADERFILESUBCLASS_PDF;
		return (1);
	}
	else
	{
		// No
		return (0);
	}
}


static int zipDetect(const char *fileName, void *dataPtr, unsigned size,
	loaderFileClass *class)
{
	// Must be binary, and have the signature '0x04034b50' at the beginning

	#define ZIP_MAGIC 0x04034B50

	unsigned *sig = dataPtr;

	// Check params
	if (!fileName || !dataPtr || !class)
		return (0);

	// Make sure there's enough data here for our detection
	if (size < sizeof(ZIP_MAGIC))
		return (0);

	if (binaryDetect(fileName, dataPtr, size, class) && (*sig == ZIP_MAGIC))
	{
		// We will call this a ZIP archive
		sprintf(class->name, "%s %s", FILECLASS_NAME_ZIP,
			FILECLASS_NAME_ARCHIVE);
		class->type = (LOADERFILECLASS_BIN | LOADERFILECLASS_ARCHIVE);
		class->subType = LOADERFILESUBCLASS_ZIP;
		return (1);
	}
	else
	{
		// No
		return (0);
	}
}


static int gzipDetect(const char *fileName, void *dataPtr, unsigned size,
	loaderFileClass *class)
{
	// Must have the signature '0x8B1F' at the beginning

	unsigned short *sig = dataPtr;

	// Check params
	if (!fileName || !dataPtr || !class)
		return (0);

	// Make sure there's enough data here for our detection
	if (size < sizeof(GZIP_MAGIC))
		return (0);

	if (*sig == GZIP_MAGIC)
	{
		// We will call this a GZIP archive
		sprintf(class->name, "%s %s", FILECLASS_NAME_GZIP,
			FILECLASS_NAME_ARCHIVE);
		class->type = (LOADERFILECLASS_BIN | LOADERFILECLASS_ARCHIVE);
		class->subType = LOADERFILESUBCLASS_GZIP;
		return (1);
	}
	else
	{
		// No
		return (0);
	}
}


static int arDetect(const char *fileName, void *dataPtr, unsigned size,
	loaderFileClass *class)
{
	// Must be binary, and have the signature '!<arch>' at the beginning

	#define AR_MAGIC "!<arch>\n"

	// Check params
	if (!fileName || !dataPtr || !class)
		return (0);

	// Make sure there's enough data here for our detection
	if (size < sizeof(AR_MAGIC))
		return (0);

	if (binaryDetect(fileName, dataPtr, size, class) &&
		!strncmp(dataPtr, AR_MAGIC, min(size, 8)))
	{
		// We will call this an AR archive.  .a (ar) format is really just an
		// archive file, but the typical usage is as a container for static
		// libraries, so we'll treat it that way.
		sprintf(class->name, "%s %s %s %s %s", FILECLASS_NAME_AR,
			FILECLASS_NAME_BIN, FILECLASS_NAME_STATIC, FILECLASS_NAME_LIB,
			FILECLASS_NAME_ARCHIVE);
		class->type = (LOADERFILECLASS_BIN | LOADERFILECLASS_LIB);
		class->subType = LOADERFILESUBCLASS_STATIC;
		return (1);
	}
	else
	{
		// No
		return (0);
	}
}


static int tarDetect(const char *fileName, void *dataPtr, unsigned size,
	loaderFileClass *class)
{
	// Must have the signature 'ustar' in a TAR header

	tarHeader *header = dataPtr;

	// Check params
	if (!fileName || !dataPtr || !class)
		return (0);

	// Make sure there's enough data here for our detection
	if (size < sizeof(tarHeader))
		return (0);

	if (!memcmp(header->magic, TAR_MAGIC, sizeof(TAR_MAGIC)) ||
		!memcmp(header->magic, TAR_OLDMAGIC, sizeof(TAR_OLDMAGIC)))
	{
		// We will call this a TAR archive
		sprintf(class->name, "%s %s", FILECLASS_NAME_TAR,
			FILECLASS_NAME_ARCHIVE);
		class->type = (LOADERFILECLASS_BIN | LOADERFILECLASS_ARCHIVE);
		class->subType = LOADERFILESUBCLASS_TAR;
		return (1);
	}
	else
	{
		// No
		return (0);
	}
}


static int pcfDetect(const char *fileName, void *dataPtr, unsigned size,
	loaderFileClass *class)
{
	// This function returns 1 and fills the fileClass structure if the data
	// points to an PCF file.

	#define PCF_MAGIC 0x70636601 // ("pcf" 0x01)

	// Check params
	if (!fileName || !dataPtr || !class)
		return (0);

	// Make sure there's enough data here for our detection
	if (size < sizeof(PCF_MAGIC))
		return (0);

	// See whether this file claims to be a PCF file.  Must have the signature
	// 'pcf1' at the beginning
	if (*((unsigned *) dataPtr) == PCF_MAGIC)
	{
		// We will call this a PCF font file
		sprintf(class->name, "%s %s", FILECLASS_NAME_PCF,
			FILECLASS_NAME_FONT);
		class->type = (LOADERFILECLASS_BIN | LOADERFILECLASS_FONT);
		class->subType = LOADERFILESUBCLASS_PCF;
		return (1);
	}
	else
	{
		// No
		return (0);
	}
}


static int messageDetect(const char *fileName, void *dataPtr, unsigned size,
	loaderFileClass *class)
{
	// This function returns 1 and fills the fileClass structure if the data
	// points to a GNU gettext messages file

	#define MO_MAGIC 0x950412DE

	// Check params
	if (!fileName || !dataPtr || !class)
		return (0);

	// Make sure there's enough data here for our detection
	if (size < sizeof(MO_MAGIC))
		return (0);

	// See whether this file claims to be an MO file.
	if (*((unsigned *) dataPtr) == MO_MAGIC)
	{
		// We will call this a message object file
		sprintf(class->name, "%s %s", FILECLASS_NAME_MESSAGE,
			FILECLASS_NAME_OBJ);
		class->type = (LOADERFILECLASS_BIN | LOADERFILECLASS_OBJ);
		class->subType = LOADERFILESUBCLASS_MESSAGE;
		return (1);
	}
	else
	{
		// No
		return (0);
	}
}


static int configDetect(const char *fileName, void *data, unsigned size,
	loaderFileClass *class)
{
	// Detect whether we think this is a Visopsys configuration file.

	// Config files are text
	char *dataPtr = data;
	int totalLines = 0;
	int configLines = 0;
	int haveEquals = 0;
	unsigned count;

	// This call will check params
	if (!textDetect(fileName, dataPtr, size, class))
		return (0);

	// Loop through the lines.  Each one should either start with a comment,
	// or a newline, or be of the form variable=value.
	dataPtr = data;
	for (count = 0; count < size; count ++)
	{
		totalLines += 1;

		if (dataPtr[count] == '\n')
		{
			configLines += 1;
			continue;
		}

		if (dataPtr[count] == '#')
		{
			// Go to the end of the line
			while ((count < size) && (dataPtr[count] != '\n') &&
				(dataPtr[count] != '\0'))
			{
				count ++;
			}

			configLines += 1;
			continue;
		}

		// It doesn't start with a comment or newline.  It should be of the
		// form variable=value
		haveEquals = 0;
		while (count < size)
		{
			if (dataPtr[count] == '=')
				haveEquals += 1;

			if ((dataPtr[count] == '\n') || (dataPtr[count] == '\0'))
			{
				if (haveEquals == 1)
					configLines += 1;
				break;
			}

			count ++;
		}
	}

	if (((configLines * 100) / totalLines) >= 95)
	{
		// We will call this a configuration file
		sprintf(class->name, "%s %s", FILECLASS_NAME_CONFIG,
			FILECLASS_NAME_DATA);
		class->type = (LOADERFILECLASS_TEXT | LOADERFILECLASS_DATA);
		class->subType = LOADERFILESUBCLASS_CONFIG;
		return (1);
	}
	else
	{
		// No
		return (0);
	}
}


static int htmlDetect(const char *fileName, void *dataPtr, unsigned size,
	loaderFileClass *class)
{
	// Detect whether we think this is an HTML document

	#define HTML_MAGIC1 "<html>"
	#define HTML_MAGIC2 "<!doctype html"

	// This call will check params
	if (!textDetect(fileName, dataPtr, size, class))
		return (0);

	// Make sure there's enough data here for our detection
	if (size < min(sizeof(HTML_MAGIC1), sizeof(HTML_MAGIC2)))
		return (0);

	if (!strncasecmp(dataPtr, HTML_MAGIC1, min(size, 6)) ||
		!strncasecmp(dataPtr, HTML_MAGIC2, min(size, 14)))
	{
		// We will call this an HTML document
		sprintf(class->name, "%s %s", FILECLASS_NAME_HTML,
			FILECLASS_NAME_DOC);
		class->type = (LOADERFILECLASS_TEXT | LOADERFILECLASS_DOC);
		class->subType = LOADERFILESUBCLASS_HTML;
		return (1);
	}
	else
	{
		// No
		return (0);
	}
}


// GIF images.
kernelFileClass gifFileClass = {
	FILECLASS_NAME_GIF,
	&gifDetect,
	{ }
};

// PNG images.
kernelFileClass pngFileClass = {
	FILECLASS_NAME_PNG,
	&pngDetect,
	{ }
};

// MP3 audio.
kernelFileClass mp3FileClass = {
	FILECLASS_NAME_MP3,
	&mp3Detect,
	{ }
};

// WAV audio.
kernelFileClass wavFileClass = {
	FILECLASS_NAME_WAV,
	&wavDetect,
	{ }
};

// FLV (Flash) videos.
kernelFileClass flvFileClass = {
	FILECLASS_NAME_FLV,
	&flvDetect,
	{ }
};

// AVI videos.
kernelFileClass aviFileClass = {
	FILECLASS_NAME_AVI,
	&aviDetect,
	{ }
};

// MP4 videos.
kernelFileClass mp4FileClass = {
	FILECLASS_NAME_MP4,
	&mp4Detect,
	{ }
};

// QuickTime videos.
kernelFileClass movFileClass = {
	FILECLASS_NAME_MOV,
	&movDetect,
	{ }
};

// Boot files.
kernelFileClass bootFileClass = {
	FILECLASS_NAME_BOOT,
	&bootDetect,
	{ }
};

// Keyboard key map files.
kernelFileClass keymapFileClass = {
	FILECLASS_NAME_KEYMAP,
	&keymapDetect,
	{ }
};

// PDF documents.
kernelFileClass pdfFileClass = {
	FILECLASS_NAME_PDF,
	&pdfDetect,
	{ }
};

// Zip archives.
kernelFileClass zipFileClass = {
	FILECLASS_NAME_ZIP,
	&zipDetect,
	{ }
};

// Gzip archives.
kernelFileClass gzipFileClass = {
	FILECLASS_NAME_GZIP,
	&gzipDetect,
	{ }
};

// Ar archives.
kernelFileClass arFileClass = {
	FILECLASS_NAME_AR,
	&arDetect,
	{ }
};

// Tar archives.
kernelFileClass tarFileClass = {
	FILECLASS_NAME_TAR,
	&tarDetect,
	{ }
};

// PCF font files.
kernelFileClass pcfFileClass = {
	FILECLASS_NAME_PCF,
	&pcfDetect,
	{ }
};

// GNU gettext message files
kernelFileClass messageFileClass = {
	FILECLASS_NAME_MESSAGE,
	&messageDetect,
	{ }
};

// Config files.
kernelFileClass configFileClass = {
	FILECLASS_NAME_CONFIG,
	&configDetect,
	{ }
};

// HTML files
kernelFileClass htmlFileClass = {
	FILECLASS_NAME_HTML,
	&htmlDetect,
	{ }
};

// Text files.
kernelFileClass textFileClass = {
	FILECLASS_NAME_TEXT,
	&textDetect,
	{ }
};

// Binary files.
kernelFileClass binaryFileClass = {
	FILECLASS_NAME_BIN,
	&binaryDetect,
	{ }
};


/////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////
//
//  Below here, the functions are exported for external use
//
/////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////

kernelFileClass *kernelFileClassGif(void)
{
	// The loader will call this function so that we can return a structure
	// for managing GIF images
	return (&gifFileClass);
}


kernelFileClass *kernelFileClassPng(void)
{
	// The loader will call this function so that we can return a structure
	// for managing PNG images
	return (&pngFileClass);
}


kernelFileClass *kernelFileClassMp3(void)
{
	// The loader will call this function so that we can return a structure
	// for managing MP3 audio files
	return (&mp3FileClass);
}


kernelFileClass *kernelFileClassWav(void)
{
	// The loader will call this function so that we can return a structure
	// for managing WAV audio files
	return (&wavFileClass);
}


kernelFileClass *kernelFileClassFlv(void)
{
	// The loader will call this function so that we can return a structure
	// for managing FLV (Flash) videos
	return (&flvFileClass);
}


kernelFileClass *kernelFileClassAvi(void)
{
	// The loader will call this function so that we can return a structure
	// for managing AVI videos
	return (&aviFileClass);
}


kernelFileClass *kernelFileClassMp4(void)
{
	// The loader will call this function so that we can return a structure
	// for managing MP4 videos
	return (&mp4FileClass);
}


kernelFileClass *kernelFileClassMov(void)
{
	// The loader will call this function so that we can return a structure
	// for managing MOV (QuickTime) videos
	return (&movFileClass);
}


kernelFileClass *kernelFileClassBoot(void)
{
	// The loader will call this function so that we can return a structure
	// for managing boot sector files
	return (&bootFileClass);
}


kernelFileClass *kernelFileClassKeymap(void)
{
	// The loader will call this function so that we can return a structure
	// for managing keyboard map files
	return (&keymapFileClass);
}


kernelFileClass *kernelFileClassPdf(void)
{
	// The loader will call this function so that we can return a structure
	// for managing PDF documents
	return (&pdfFileClass);
}


kernelFileClass *kernelFileClassZip(void)
{
	// The loader will call this function so that we can return a structure
	// for managing Zip archives
	return (&zipFileClass);
}


kernelFileClass *kernelFileClassGzip(void)
{
	// The loader will call this function so that we can return a structure
	// for managing Gzip archives
	return (&gzipFileClass);
}


kernelFileClass *kernelFileClassAr(void)
{
	// The loader will call this function so that we can return a structure
	// for managing Ar archives
	return (&arFileClass);
}


kernelFileClass *kernelFileClassTar(void)
{
	// The loader will call this function so that we can return a structure
	// for managing Tar archives
	return (&tarFileClass);
}


kernelFileClass *kernelFileClassPcf(void)
{
	// The loader will call this function so that we can return a structure
	// for managing PCF files
	return (&pcfFileClass);
}

kernelFileClass *kernelFileClassMessage(void)
{
	// The loader will call this function so that we can return a structure
	// for managing GNU gettext message files
	return (&messageFileClass);
}

kernelFileClass *kernelFileClassConfig(void)
{
	// The loader will call this function so that we can return a structure
	// for managing config files
	return (&configFileClass);
}


kernelFileClass *kernelFileClassHtml(void)
{
	// The loader will call this function so that we can return a structure
	// for managing HTML files
	return (&htmlFileClass);
}


kernelFileClass *kernelFileClassText(void)
{
	// The loader will call this function so that we can return a structure
	// for managing text files
	return (&textFileClass);
}


kernelFileClass *kernelFileClassBinary(void)
{
	// The loader will call this function so that we can return a structure
	// for managing binary files
	return (&binaryFileClass);
}

