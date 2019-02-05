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
//  windowFileList.c
//

// This contains functions for user programs to operate GUI components.

#include <errno.h>
#include <libintl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/api.h>
#include <sys/file.h>
#include <sys/image.h>
#include <sys/loader.h>
#include <sys/paths.h>
#include <sys/window.h>

#define _(string) gettext(string)
#define FILEBROWSE_CONFIG			PATH_SYSTEM_CONFIG "/filebrowse.conf"
#define DEFAULT_FOLDERICON_VAR		"icon.folder"
#define DEFAULT_FOLDERICON_FILE		PATH_SYSTEM_ICONS "/folder.ico"
#define DEFAULT_FILEICON_VAR		"icon.file"
#define DEFAULT_FILEICON_FILE		PATH_SYSTEM_ICONS "/file.ico"
#define DEFAULT_IMAGEICON_VAR		"icon.image"
#define DEFAULT_IMAGEICON_FILE		PATH_SYSTEM_ICONS "/image.ico"
#define DEFAULT_AUDIOICON_VAR		"icon.audio"
#define DEFAULT_AUDIOICON_FILE		PATH_SYSTEM_ICONS "/audio.ico"
#define DEFAULT_VIDEOICON_VAR		"icon.video"
#define DEFAULT_VIDEOICON_FILE		PATH_SYSTEM_ICONS "/video.ico"
#define DEFAULT_EXECICON_VAR		"icon.executable"
#define DEFAULT_EXECICON_FILE		PATH_SYSTEM_ICONS "/execable.ico"
#define DEFAULT_MESSAGEICON_VAR		"icon.message"
#define DEFAULT_MESSAGEICON_FILE	PATH_SYSTEM_ICONS "/messages.ico"
#define DEFAULT_OBJICON_VAR			"icon.object"
#define DEFAULT_OBJICON_FILE		PATH_SYSTEM_ICONS "/object.ico"
#define DEFAULT_BOOTSECTICON_VAR	"icon.bootsect"
#define DEFAULT_BOOTSECTICON_FILE	PATH_SYSTEM_ICONS "/bootsect.ico"
#define DEFAULT_KEYMAPICON_VAR		"icon.keymap"
#define DEFAULT_KEYMAPICON_FILE		PATH_SYSTEM_ICONS "/kmapfile.ico"
#define DEFAULT_PDFICON_VAR			"icon.pdf"
#define DEFAULT_PDFICON_FILE		PATH_SYSTEM_ICONS "/pdf.ico"
#define DEFAULT_ARCHIVEICON_VAR		"icon.archive"
#define DEFAULT_ARCHIVEICON_FILE	PATH_SYSTEM_ICONS "/archive.ico"
#define DEFAULT_FONTICON_VAR		"icon.font"
#define DEFAULT_FONTICON_FILE		PATH_SYSTEM_ICONS "/font.ico"
#define DEFAULT_CONFIGICON_VAR		"icon.config"
#define DEFAULT_CONFIGICON_FILE		PATH_SYSTEM_ICONS "/config.ico"
#define DEFAULT_HTMLICON_VAR		"icon.html"
#define DEFAULT_HTMLICON_FILE		PATH_SYSTEM_ICONS "/html.ico"
#define DEFAULT_TEXTICON_VAR		"icon.text"
#define DEFAULT_TEXTICON_FILE		PATH_SYSTEM_ICONS "/text.ico"
#define DEFAULT_BINICON_VAR			"icon.binary"
#define DEFAULT_BINICON_FILE		PATH_SYSTEM_ICONS "/binary.ico"

#define STANDARD_ICON_SIZE			64

#define FOLDER_ICON ((typeIcon) { \
	LOADERFILECLASS_NONE, LOADERFILESUBCLASS_NONE, DEFAULT_FOLDERICON_VAR, \
		DEFAULT_FOLDERICON_FILE, folderImageIndex } )
#define TEXT_ICON ((typeIcon) { \
	LOADERFILECLASS_TEXT, LOADERFILESUBCLASS_NONE, DEFAULT_TEXTICON_VAR, \
		DEFAULT_TEXTICON_FILE, textImageIndex } )
#define BIN_ICON ((typeIcon) { \
	LOADERFILECLASS_BIN, LOADERFILESUBCLASS_NONE, DEFAULT_BINICON_VAR, \
		DEFAULT_BINICON_FILE, binImageIndex } )
#define FILE_ICON ((typeIcon) { \
	0xFFFFFFFF, 0xFFFFFFFF, DEFAULT_FILEICON_VAR, DEFAULT_FILEICON_FILE, \
		fileImageIndex } )

typedef enum {
	folderImageIndex = 0,
	fileImageIndex,
	imageImageIndex,
	audioImageIndex,
	videoImageIndex,
	bootImageIndex,
	keymapImageIndex,
	pdfImageIndex,
	archImageIndex,
	fontImageIndex,
	execImageIndex,
	messageImageIndex,
	objImageIndex,
	configImageIndex,
	htmlImageIndex,
	textImageIndex,
	binImageIndex,
	maxImageIndex

} imageIndex;

typedef struct {
	int fileClass;
	int fileSubClass;
	const char *imageVariable;
	const char *imageFile;
	imageIndex index;

} typeIcon;

typedef struct {
	file file;
	char fullName[MAX_PATH_NAME_LENGTH];
	listItemParameters iconParams;
	loaderFileClass class;
	typeIcon *icon;

} fileEntry;

extern int libwindow_initialized;
extern void libwindowInitialize(void);

static typeIcon iconList[] = {
	// These get traversed in order; the first matching file class flags get
	// the icon.  So, for example, if you want to make an icon for a type
	// of binary file, put it *before* the icon for plain binaries.
	{ LOADERFILECLASS_IMAGE, LOADERFILESUBCLASS_NONE, DEFAULT_IMAGEICON_VAR,
		DEFAULT_IMAGEICON_FILE, imageImageIndex },
	{ LOADERFILECLASS_AUDIO, LOADERFILESUBCLASS_NONE, DEFAULT_AUDIOICON_VAR,
		DEFAULT_AUDIOICON_FILE, audioImageIndex },
	{ LOADERFILECLASS_VIDEO, LOADERFILESUBCLASS_NONE, DEFAULT_VIDEOICON_VAR,
		DEFAULT_VIDEOICON_FILE, videoImageIndex },
	{ LOADERFILECLASS_BOOT, LOADERFILESUBCLASS_NONE, DEFAULT_BOOTSECTICON_VAR,
		DEFAULT_BOOTSECTICON_FILE, bootImageIndex },
	{ LOADERFILECLASS_KEYMAP, LOADERFILESUBCLASS_NONE, DEFAULT_KEYMAPICON_VAR,
		DEFAULT_KEYMAPICON_FILE, keymapImageIndex },
	{ LOADERFILECLASS_DOC, LOADERFILESUBCLASS_PDF, DEFAULT_PDFICON_VAR,
		DEFAULT_PDFICON_FILE, pdfImageIndex },
	{ LOADERFILECLASS_ARCHIVE, LOADERFILESUBCLASS_NONE, DEFAULT_ARCHIVEICON_VAR,
		DEFAULT_ARCHIVEICON_FILE, archImageIndex },
	{ LOADERFILECLASS_FONT, LOADERFILESUBCLASS_NONE, DEFAULT_FONTICON_VAR,
		DEFAULT_FONTICON_FILE, fontImageIndex },
	{ LOADERFILECLASS_EXEC, LOADERFILESUBCLASS_NONE, DEFAULT_EXECICON_VAR,
		DEFAULT_EXECICON_FILE, execImageIndex },
	{ LOADERFILECLASS_OBJ, LOADERFILESUBCLASS_MESSAGE, DEFAULT_MESSAGEICON_VAR,
		DEFAULT_MESSAGEICON_FILE, messageImageIndex },
	{ (LOADERFILECLASS_OBJ | LOADERFILECLASS_LIB), LOADERFILESUBCLASS_NONE,
		DEFAULT_OBJICON_VAR, DEFAULT_OBJICON_FILE, objImageIndex },
	{ LOADERFILECLASS_DATA, LOADERFILESUBCLASS_CONFIG, DEFAULT_CONFIGICON_VAR,
		DEFAULT_CONFIGICON_FILE, configImageIndex },
	{ LOADERFILECLASS_DOC, LOADERFILESUBCLASS_HTML, DEFAULT_HTMLICON_VAR,
		DEFAULT_HTMLICON_FILE, htmlImageIndex },
	TEXT_ICON,
	BIN_ICON,
	// This one goes last, because the flags match every file class.
	FILE_ICON,
	{ NULL, NULL, NULL, NULL, NULL }
};

typedef struct {
	variableList config;
	image images[maxImageIndex];
	image *customImages;
	int numCustomImages;
	typeIcon folderIcon;
	typeIcon textIcon;
	typeIcon binIcon;
	typeIcon fileIcon;

} fileListData;


__attribute__((format(printf, 1, 2)))
static void error(const char *format, ...)
{
	// Generic error message code

	va_list list;
	char *output = NULL;

	output = malloc(MAXSTRINGLENGTH);
	if (!output)
		return;

	va_start(list, format);
	vsnprintf(output, MAXSTRINGLENGTH, format, list);
	va_end(list);

	windowNewErrorDialog(NULL, _("Error"), output);
	free(output);
}


static fileListData *setup(void)
{
	int status = 0;
	fileListData *data = NULL;

	// Get memory for our file list structure
	data = calloc(1, sizeof(fileListData));
	if (!data)
		return (data);

	// Try to read our config file
	status = configRead(FILEBROWSE_CONFIG, &data->config);
	if (status < 0)
	{
		error(_("Can't locate configuration file %s"), FILEBROWSE_CONFIG);
		status = ERR_NODATA;
		goto out;
	}

	data->folderIcon = FOLDER_ICON;
	data->textIcon = TEXT_ICON;
	data->binIcon = BIN_ICON;
	data->fileIcon = FILE_ICON;

	status = 0;

out:
	if (status < 0)
	{
		free(data);
		data = NULL;
		errno = status;
	}

	return (data);
}


static int loadIcon(fileListData *data, const char *variableName,
	const char *defaultIcon, image *theImage)
{
	// Try to load the requested icon, first based on the configuration file
	// variable name, then by the default filename.

	int status = 0;
	const char *value = NULL;

	// First try the variable
	value = variableListGet(&data->config, variableName);
	if (value)
		defaultIcon = value;

	// Try to load the image
	status = fileFind(defaultIcon, NULL);
	if (status < 0)
		return (status);

	return (imageLoad(defaultIcon, STANDARD_ICON_SIZE, STANDARD_ICON_SIZE,
		theImage));
}


static void getFileIcon(fileListData *data, fileEntry *entry)
{
	// Choose the appropriate icon for the class of file, and load it if
	// necessary.

	int count;

	entry->icon = &data->fileIcon;

	// Try to find an exact match.  If there isn't one, this should default
	// to the 'file' type at the end of the list.
	for (count = 0; count < maxImageIndex ; count ++)
	{
		if (((iconList[count].fileClass == LOADERFILECLASS_NONE) ||
				(entry->class.type & iconList[count].fileClass)) &&
			((iconList[count].fileSubClass == LOADERFILESUBCLASS_NONE) ||
				(entry->class.subType & iconList[count].fileSubClass)))
		{
			entry->icon = &iconList[count];
			break;
		}
	}

	// Do we need to load the image data?
	while (!data->images[entry->icon->index].data)
	{
		if (loadIcon(data, entry->icon->imageVariable, entry->icon->imageFile,
			&data->images[entry->icon->index]) < 0)
		{
			if (entry->icon == &data->fileIcon)
				// Even the 'file' icon image failed.
				return;

			if ((entry->icon != &data->binIcon) &&
				(entry->class.type & LOADERFILECLASS_BIN))
			{
				// It's a binary file - try the binary icon image
				entry->icon = &data->binIcon;
			}
			else if ((entry->icon != &data->textIcon) &&
				(entry->class.type & LOADERFILECLASS_TEXT))
			{
				// It's a text file - try the text icon image
				entry->icon = &data->textIcon;
			}
			else
			{
				// Default to the file icon image
				entry->icon = &data->fileIcon;
			}
		}
		else
		{
			break;
		}
	}

	memcpy(&entry->iconParams.iconImage, &data->images[entry->icon->index],
		sizeof(image));
}


static int classifyEntry(fileListData *data, fileEntry *entry)
{
	// Given a file entry with its 'file' field filled, classify the file, set
	// up the icon image, etc.

	int status = 0;

	strncpy(entry->iconParams.text, entry->file.name,
		WINDOW_MAX_LABEL_LENGTH);

	switch (entry->file.type)
	{
		case dirT:
		{
			if (!strcmp(entry->file.name, ".."))
				strcpy(entry->iconParams.text, _("(up)"));

			entry->icon = &data->folderIcon;
			if (!data->images[entry->icon->index].data)
			{
				status = loadIcon(data, entry->icon->imageVariable,
					entry->icon->imageFile,
					&data->images[entry->icon->index]);
				if (status < 0)
					return (status);
			}

			memcpy(&entry->iconParams.iconImage,
				&data->images[entry->icon->index], sizeof(image));

			break;
		}

		case fileT:
		{
			// Get the file class information
			loaderClassifyFile(entry->fullName, &entry->class);

			// Get the icon for the file
			getFileIcon(data, entry);

			break;
		}

		case linkT:
		{
			if (!strcmp(entry->file.name, ".."))
			{
				strcpy(entry->iconParams.text, _("(up)"));

				entry->icon = &data->folderIcon;
				if (!data->images[entry->icon->index].data)
				{
					status = loadIcon(data, entry->icon->imageVariable,
						entry->icon->imageFile,
						&data->images[entry->icon->index]);
					if (status < 0)
						return (status);
				}

				memcpy(&entry->iconParams.iconImage,
					&data->images[entry->icon->index], sizeof(image));
			}
			else
			{
				// Get the target's file class information
				loaderClassifyFile(entry->fullName, &entry->class);

				// Get the icon for the file
				getFileIcon(data, entry);
			}

			break;
		}

		default:
		{
			break;
		}
	}

	return (status = 0);
}


static int changeDirectory(windowFileList *fileList, const char *rawPath)
{
	// Given a directory path, allocate memory, and read all of the required
	// information into memory

	int status = 0;
	char path[MAX_PATH_LENGTH];
	char tmpFileName[MAX_PATH_NAME_LENGTH];
	int totalFiles = 0;
	fileEntry *tmpFileEntries = NULL;
	int tmpNumFileEntries = 0;
	file tmpFile;
	int count;

	fileFixupPath(rawPath, path);

	// Get the count of files so we can preallocate memory, etc.
	totalFiles = fileCount(path);
	if (totalFiles < 0)
	{
		error(_("Can't get directory \"%s\" file count"), path);
		return (totalFiles);
	}

	// Read the file information for all the files
	if (totalFiles)
	{
		// Get memory for the new entries
		tmpFileEntries = malloc(totalFiles * sizeof(fileEntry));
		if (!tmpFileEntries)
		{
			error("%s", _("Memory allocation error"));
			return (status = ERR_MEMORY);
		}

		for (count = 0; count < totalFiles; count ++)
		{
			if (!count)
				status = fileFirst(path, &tmpFile);
			else
				status = fileNext(path, &tmpFile);

			if (status < 0)
			{
				error(_("Error reading files in \"%s\""), path);
				free(tmpFileEntries);
				return (status);
			}

			if (strcmp(tmpFile.name, "."))
			{
				memcpy(&tmpFileEntries[tmpNumFileEntries].file, &tmpFile,
					sizeof(file));

				sprintf(tmpFileName, "%s/%s", path, tmpFile.name);
				fileFixupPath(tmpFileName,
					tmpFileEntries[tmpNumFileEntries].fullName);

				if (!classifyEntry(fileList->data,
					&tmpFileEntries[tmpNumFileEntries]))
				{
					tmpNumFileEntries += 1;
				}
			}
		}
	}

	// Commit
	strncpy(fileList->cwd, path, MAX_PATH_LENGTH);
	if (fileList->fileEntries)
		free(fileList->fileEntries);
	fileList->fileEntries = tmpFileEntries;
	fileList->numFileEntries = tmpNumFileEntries;

	return (status = 0);
}


static listItemParameters *allocateIconParameters(windowFileList *fileList)
{
	listItemParameters *newIconParams = NULL;
	int count;

	if (fileList->numFileEntries)
	{
		newIconParams = malloc(fileList->numFileEntries *
			sizeof(listItemParameters));
		if (!newIconParams)
		{
			error("%s", _("Memory allocation error creating icon "
				"parameters"));
			return (newIconParams);
		}

		// Fill in an array of list item parameters structures for our file
		// entries.  It will get passed to the window list creation/set data
		// function in a moment
		for (count = 0; count < fileList->numFileEntries; count ++)
		{
			memcpy(&newIconParams[count], (listItemParameters *)
				&(((fileEntry *) fileList->fileEntries)[count].iconParams),
				sizeof(listItemParameters));
		}
	}

	return (newIconParams);
}


static void freeCustomImages(fileListData *data)
{
	int count;

	if (data)
	{
		if (data->customImages)
		{
			for (count = 0; count < data->numCustomImages; count ++)
				imageFree(&data->customImages[count]);

			free(data->customImages);
			data->customImages = NULL;
		}

		data->numCustomImages = 0;
	}
}


static int getCustomIcon(fileListData *data, fileEntry *entry)
{
	// Try to load a custom icon for certain types of files

	int status = 0;
	int newCustom = 0;
	image *newCustomImages = NULL;
	image *newImage = NULL;
	image tmpImage;
	image tmpImage2;

	memset(&tmpImage, 0, sizeof(image));
	memset(&tmpImage2, 0, sizeof(image));

	if (entry->class.type & LOADERFILECLASS_IMAGE)
	{
		// Try to load the image
		status = imageLoad(entry->fullName, 0, 0, &tmpImage);
		if (status < 0)
			return (status);

		// If it's bigger than our standard icon size, try to resize it so it
		// fits in both dimensions, preserving the aspect ratio.
		if ((tmpImage.width > STANDARD_ICON_SIZE) ||
			(tmpImage.height > STANDARD_ICON_SIZE))
		{
			if (tmpImage.width >= tmpImage.height)
			{
				status = imageResize(&tmpImage, STANDARD_ICON_SIZE,
					((tmpImage.height * 100) / ((tmpImage.width * 100) /
						STANDARD_ICON_SIZE)));
			}
			else
			{
				status = imageResize(&tmpImage, ((tmpImage.width * 100) /
					((tmpImage.height * 100) / STANDARD_ICON_SIZE)),
					STANDARD_ICON_SIZE);
			}

			if (status < 0)
				return (status);
		}

		// If it's smaller than our standard icon size, paste it into a larger
		// image, so it's centered.
		if ((tmpImage.width < STANDARD_ICON_SIZE) ||
			(tmpImage.height < STANDARD_ICON_SIZE))
		{
			status = imageNew(&tmpImage2, STANDARD_ICON_SIZE,
				STANDARD_ICON_SIZE);
			if (status >= 0)
			{
				tmpImage2.transColor = (color){ 0, 0xFF, 0 };

				status = imageFill(&tmpImage2, &tmpImage2.transColor);
				if (status >= 0)
				{
					status = imagePaste(&tmpImage, &tmpImage2,
						((tmpImage2.width - tmpImage.width) / 2),
						((tmpImage2.height - tmpImage.height) / 2));
					if (status >= 0)
					{
						imageFree(&tmpImage);
						imageCopy(&tmpImage2, &tmpImage);
					}
				}

				imageFree(&tmpImage2);
			}
		}

		newCustom = 1;
	}

	if (!newCustom)
		return (status = ERR_NOTINITIALIZED);

	// Get memory for a new custom image list
	newCustomImages = calloc((data->numCustomImages + 1), sizeof(image));
	if (!newCustomImages)
	{
		imageFree(&tmpImage);
		return (status = ERR_MEMORY);
	}

	// Copy the old list
	memcpy(newCustomImages, data->customImages, (data->numCustomImages *
		sizeof(image)));

	newImage = &newCustomImages[data->numCustomImages];
	memcpy(newImage, &tmpImage, sizeof(image));

	if (data->customImages)
		free(data->customImages);

	data->customImages = newCustomImages;
	data->numCustomImages += 1;

	memcpy(&entry->iconParams.iconImage, newImage, sizeof(image));

	return (status = 0);
}


static void iconThread(int argc, void *argv[])
{
	int status = 0;
	windowFileList *fileList = NULL;
	int updateAfter = 0;
	fileEntry *entry = NULL;
	listItemParameters *iconParams = NULL;
	int count;

	// Convert the argument string to a pointer
	if (argc == 2)
	{
		fileList = (windowFileList *)
			((unsigned long) xtoll((char *) argv[1]));
	}

	if (!fileList)
	{
		status = ERR_NULLPARAMETER;
		goto terminate;
	}

	freeCustomImages(fileList->data);

	updateAfter = max(5, (fileList->numFileEntries / 10));

	for (count = 0; count < fileList->numFileEntries; count ++)
	{
		entry = &((fileEntry *) fileList->fileEntries)[count];

		if (entry->file.type == fileT)
		{
			status = getCustomIcon(fileList->data, entry);
			if (status >= 0)
			{
				if ((count && !(count % updateAfter)) || (count >=
					(fileList->numFileEntries - 1)))
				{
					iconParams = allocateIconParameters(fileList);
					if (iconParams)
					{
						windowComponentSetData(fileList->key, iconParams,
							fileList->numFileEntries, 1 /* redraw */);
						free(iconParams);
					}
				}
			}
		}
	}

	status = 0;

terminate:
	multitaskerTerminate(status);
}


static void killIconThread(windowFileList *fileList)
{
	// If an icon thread was running, try to kill it
	if (fileList->iconThreadPid > 0)
	{
		if (multitaskerProcessIsAlive(fileList->iconThreadPid))
		{
			multitaskerKillProcess(fileList->iconThreadPid, 1 /* force */);

			while (multitaskerProcessIsAlive(fileList->iconThreadPid))
				multitaskerYield();
		}

		fileList->iconThreadPid = 0;
	}
}


static void launchIconThread(windowFileList *fileList)
{
	char ptrString[(sizeof(void *) * 2) + 3];

	// If an existing icon thread was running, try to kill it
	killIconThread(fileList);

	// Launch a new icon thread to do any custom icons, if applicable

	lltoux((unsigned long) fileList, ptrString);

	fileList->iconThreadPid = multitaskerSpawn(&iconThread, "icon thread", 1,
		(void *[]){ ptrString });

	// Give the thread a chance to get going before we return
	multitaskerYield();
}


static int changeDirWithLock(windowFileList *fileList, const char *newDir)
{
	// Rescan the directory information and rebuild the file list, with
	// locking so that our GUI thread and main thread don't trash one another

	int status = 0;
	listItemParameters *iconParams = NULL;

	// Lock before killing any icon thread
	status = lockGet(&fileList->lock);
	if (status < 0)
		return (status);

	// If an existing icon thread was running, try to kill it
	killIconThread(fileList);

	windowSwitchPointer(fileList->key, MOUSE_POINTER_BUSY);

	status = changeDirectory(fileList, newDir);
	if (status < 0)
	{
		windowSwitchPointer(fileList->key, MOUSE_POINTER_DEFAULT);
		lockRelease(&fileList->lock);
		return (status);
	}

	iconParams = allocateIconParameters(fileList);
	if (!iconParams)
	{
		windowSwitchPointer(fileList->key, MOUSE_POINTER_DEFAULT);
		lockRelease(&fileList->lock);
		return (status = ERR_MEMORY);
	}

	// Clear the list
	windowComponentSetData(fileList->key, NULL, 0, 0 /* no redraw */);
	windowComponentSetData(fileList->key, iconParams,
		fileList->numFileEntries, 1 /* redraw */);
	windowComponentSetSelected(fileList->key, 0);

	windowSwitchPointer(fileList->key, MOUSE_POINTER_DEFAULT);

	free(iconParams);

	// Unlock before starting a new icon thread
	lockRelease(&fileList->lock);

	// Start an icon thread
	launchIconThread(fileList);

	return (status = 0);
}


static int update(windowFileList *fileList)
{
	// Update the supplied file list from the current directory.
	return (changeDirWithLock(fileList, fileList->cwd));
}


static int eventHandler(windowFileList *fileList, windowEvent *event)
{
	int status = 0;
	int selected = -1;
	fileEntry *fileEntries = (fileEntry *) fileList->fileEntries;
	fileEntry saveEntry;

	// Get the selected item
	windowComponentGetSelected(fileList->key, &selected);
	if (selected < 0)
		return (status = selected);

	// Check for events in our icon list.  We consider the icon 'clicked'
	// if it is a mouse click selection, or an ENTER key selection
	if ((event->type & EVENT_SELECTION) &&
		((event->type & EVENT_MOUSE_LEFTUP) ||
		((event->type & EVENT_KEY_DOWN) && (event->key == keyEnter))))
	{
		memcpy(&saveEntry, &fileEntries[selected], sizeof(fileEntry));

		if ((fileEntries[selected].file.type == linkT) &&
			!strcmp((char *) fileEntries[selected].file.name, ".."))
		{
			saveEntry.file.type = dirT;
		}

		if ((saveEntry.file.type == dirT) && (fileList->browseFlags &
			WINFILEBROWSE_CAN_CD))
		{
			// Change to the directory, get the list of icon parameters, and
			// update our window list.
			status = changeDirWithLock(fileList, saveEntry.fullName);
			if (status < 0)
			{
				error(_("Can't change to directory %s"), saveEntry.file.name);
				return (status);
			}
		}

		if (fileList->selectionCallback)
		{
			fileList->selectionCallback(fileList, (file *) &saveEntry.file,
				(char *) saveEntry.fullName, (loaderFileClass *)
				&saveEntry.class);
		}
	}

	else if ((event->type & EVENT_KEY_DOWN) && (event->key == keyDel))
	{
		if ((fileList->browseFlags & WINFILEBROWSE_CAN_DEL) &&
			strcmp((char *) fileEntries[selected].file.name, ".."))
		{
			windowSwitchPointer(fileList->key, MOUSE_POINTER_BUSY);

			status = fileDeleteRecursive((char *)
				fileEntries[selected].fullName);

			windowSwitchPointer(fileList->key, MOUSE_POINTER_DEFAULT);

			if (status < 0)
			{
				error(_("Error deleting file %s"),
					fileEntries[selected].file.name);
			}

			status = update(fileList);
			if (status < 0)
				return (status);

			if (selected >= fileList->numFileEntries)
				selected = (fileList->numFileEntries - 1);

			windowComponentSetSelected(fileList->key, selected);
		}
	}

	return (status = 0);
}


static int destroy(windowFileList *fileList)
{
	// Detroy and deallocate the file list.

	fileListData *data = (fileListData *) fileList->data;
	int count;

	// If an icon thread was running, try to kill it
	killIconThread(fileList);

	if (fileList->fileEntries)
		free(fileList->fileEntries);

	if (data)
	{
		freeCustomImages(data);

		for (count = 0; count < maxImageIndex; count ++)
		{
			if (data->images[count].data)
				imageFree(&data->images[count]);
		}

		variableListDestroy(&data->config);
	}

	free(fileList);

	return (0);
}


/////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////
//
// Below here, the functions are exported for external use
//
/////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////

_X_ windowFileList *windowNewFileList(objectKey parent, windowListType type, int rows, int columns, const char *directory, int flags, void (*callback)(windowFileList *, file *, char *, loaderFileClass *), componentParameters *params)
{
	// Desc: Create a new file list widget with the parent window 'parent', the window list type 'type' (windowlist_textonly or windowlist_icononly is currently supported), of height 'rows' and width 'columns', the name of the starting location 'directory', flags (such as WINFILEBROWSE_CAN_CD or WINFILEBROWSE_CAN_DEL -- see sys/window.h), a function 'callback' for when the status changes, and component parameters 'params'.

	int status = 0;
	windowFileList *fileList = NULL;
	fileListData *data = NULL;
	listItemParameters *iconParams = NULL;

	if (!libwindow_initialized)
		libwindowInitialize();

	// Check params.  Callback can be NULL.
	if (!parent || !directory || !params)
	{
		errno = ERR_NULLPARAMETER;
		return (fileList = NULL);
	}

	// Allocate memory for our file list
	fileList = calloc(1, sizeof(windowFileList));
	if (!fileList)
	{
		errno = ERR_MEMORY;
		return (fileList);
	}

	// Allocate private data
	data = setup();
	if (!data)
	{
		free(fileList);
		errno = ERR_MEMORY;
		return (NULL);
	}

	fileList->data = data;

	// Scan the directory
	status = changeDirectory(fileList, directory);
	if (status < 0)
	{
		destroy(fileList);
		errno = status;
		return (fileList = NULL);
	}

	// Get our array of icon parameters
	iconParams = allocateIconParameters(fileList);

	// Create a window list to hold the icons
	fileList->key = windowNewList(parent, type, rows, columns,
		0 /* no select multiple */, iconParams, fileList->numFileEntries,
		params);

	if (iconParams)
		free(iconParams);

	fileList->browseFlags = flags;

	fileList->selectionCallback = callback;

	fileList->update = &update;
	fileList->eventHandler = &eventHandler;
	fileList->destroy = &destroy;

	// Start an icon thread
	launchIconThread(fileList);

	return (fileList);
}

