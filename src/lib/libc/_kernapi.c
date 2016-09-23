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
//  _kernapi.c
//

// This contains code for calling the Visopsys kernel

#include <sys/api.h>

// This is generic method for invoking the kernel API
#define kernelCall(fnum, args, codeLo, codeHi)	\
	__asm__ __volatile__ ("pushl %3 \n\t"		\
		"pushl %2 \n\t"							\
		"lcall $0x003B,$0x00000000 \n\t"		\
		"movl %%eax, %0 \n\t"					\
		"movl %%edx, %1 \n\t"					\
		"addl $8, %%esp \n\t"					\
		: "=r" (codeLo), "=r" (codeHi)			\
		: "r" (fnum), "r" (args)				\
		: "%eax", "memory");
#define _U_ __attribute__((unused))


static quad_t _syscall(int fnum, void *args)
{
	// This function sets up the stack and arguments, invokes the kernel API,
	// cleans up the stack, and returns the return code.

	quad_t statusLo = 0;
	quad_t statusHi = 0;
	quad_t status = 0;

	if (!visopsys_in_kernel)
		// Call the kernel
		kernelCall(fnum, args, statusLo, statusHi);

	status = ((statusHi << 32) | statusLo);
	return (status);
}


// These functions are used to call specific kernel functions.  There will be
// one of these for every API function.


//
// Text input/output functions
//

_X_ objectKey textGetConsoleInput(void)
{
	// Proto: kernelTextInputStream *kernelTextGetConsoleInput(void);
	// Desc : Returns a reference to the console input stream.  This is where keyboard input goes by default.
	return ((objectKey)(long) _syscall(_fnum_textGetConsoleInput, NULL));
}

_X_ int textSetConsoleInput(objectKey newStream)
{
	// Proto: int kernelTextSetConsoleInput(kernelTextInputStream *);
	// Desc : Changes the console input stream.  GUI programs can use this function to redirect input to a text area or text field, for example.
	return (_syscall(_fnum_textSetConsoleInput, &newStream));
}

_X_ objectKey textGetConsoleOutput(void)
{
	// Proto: kernelTextOutputStream *kernelTextGetConsoleOutput(void);
	// Desc : Returns a reference to the console output stream.  This is where kernel logging output goes by default.
	return ((objectKey)(long) _syscall(_fnum_textGetConsoleOutput, NULL));
}

_X_ int textSetConsoleOutput(objectKey newStream)
{
	// Proto: int kernelTextSetConsoleOutput(kernelTextOutputStream *);
	// Desc : Changes the console output stream.  GUI programs can use this function to redirect output to a text area or text field, for example.
	return (_syscall(_fnum_textSetConsoleOutput, &newStream));
}

_X_ objectKey textGetCurrentInput(void)
{
	// Proto: kernelTextInputStream *kernelTextGetCurrentInput(void);
	// Desc : Returns a reference to the input stream of the current process.  This is where standard input (for example, from a getc() call) is received.
	return ((objectKey)(long) _syscall(_fnum_textGetCurrentInput, NULL));
}

_X_ int textSetCurrentInput(objectKey newStream)
{
	// Proto: int kernelTextSetCurrentInput(kernelTextInputStream *);
	// Desc : Changes the current input stream.  GUI programs can use this function to redirect input to a text area or text field, for example.
	return (_syscall(_fnum_textSetCurrentInput, &newStream));
}

_X_ objectKey textGetCurrentOutput(void)
{
	// Proto: kernelTextOutputStream *kernelTextGetCurrentOutput(void);
	// Desc : Returns a reference to the console output stream.
	return ((objectKey)(long) _syscall(_fnum_textGetCurrentOutput, NULL));
}

_X_ int textSetCurrentOutput(objectKey newStream)
{
	// Proto: int kernelTextSetCurrentOutput(kernelTextOutputStream *);
	// Desc : Changes the current output stream.  This is where standard output (for example, from a putc() call) goes.
	return (_syscall(_fnum_textSetCurrentOutput, &newStream));
}

_X_ int textGetForeground(color *foreground)
{
	// Proto: int kernelTextGetForeground(color *);
	// Desc : Return the current foreground color in the color structure 'foreground'.
	return (_syscall(_fnum_textGetForeground, &foreground));
}

_X_ int textSetForeground(color *foreground)
{
	// Proto: int kernelTextSetForeground(color *);
	// Desc : Set the current foreground color to the one represented in the color structure 'foreground'.  Some standard color values (as in PC text-mode values) can be found in <sys/color.h>.
	return (_syscall(_fnum_textSetForeground, &foreground));
}

_X_ int textGetBackground(color *background)
{
	// Proto: int kernelTextGetBackground(color *);
	// Desc : Return the current background color in the color structure 'background'.
	return (_syscall(_fnum_textGetBackground, &background));
}

_X_ int textSetBackground(color *background)
{
	// Proto: int kernelTextSetBackground(color *);
	// Desc : Set the current background color to the one represented in the color structure 'background'.  Some standard color values (as in PC text-mode values) can be found in <sys/color.h>.
	return (_syscall(_fnum_textSetBackground, &background));
}

_X_ int textPutc(int ascii)
{
	// Proto: int kernelTextPutc(int);
	// Desc : Print a single character
	return (_syscall(_fnum_textPutc, &ascii));
}

_X_ int textPrint(const char *str)
{
	// Proto: int kernelTextPrint(const char *);
	// Desc : Print a string
	return (_syscall(_fnum_textPrint, &str));
}

_X_ int textPrintAttrs(textAttrs *attrs, const char *str _U_)
{
	// Proto: int kernelTextPrintAttrs(textAttrs *, const char *, ...);
	// Desc : Print a string, with attributes.  See <sys/text.h> for the definition of the textAttrs structure.
	return (_syscall(_fnum_textPrintAttrs, &attrs));
}

_X_ int textPrintLine(const char *str)
{
	// Proto: int kernelTextPrintLine(const char *);
	// Desc : Print a string with a newline at the end
	return (_syscall(_fnum_textPrintLine, &str));
}

_X_ void textNewline(void)
{
	// Proto: void kernelTextNewline(void);
	// Desc : Print a newline
	_syscall(_fnum_textNewline, NULL);
}

_X_ int textBackSpace(void)
{
	// Proto: void kernelTextBackSpace(void);
	// Desc : Backspace the cursor, deleting any character there
	return (_syscall(_fnum_textBackSpace, NULL));
}

_X_ int textTab(void)
{
	// Proto: void kernelTextTab(void);
	// Desc : Print a tab
	return (_syscall(_fnum_textTab, NULL));
}

_X_ int textCursorUp(void)
{
	// Proto: void kernelTextCursorUp(void);
	// Desc : Move the cursor up one row.  Doesn't affect any characters there.
	return (_syscall(_fnum_textCursorUp, NULL));
}

_X_ int textCursorDown(void)
{
	// Proto: void kernelTextCursorDown(void);
	// Desc : Move the cursor down one row.  Doesn't affect any characters there.
	return (_syscall(_fnum_textCursorDown, NULL));
}

_X_ int textCursorLeft(void)
{
	// Proto: void kernelTextCursorLeft(void);
	// Desc : Move the cursor left one column.  Doesn't affect any characters there.
	return (_syscall(_fnum_ternelTextCursorLeft, NULL));
}

_X_ int textCursorRight(void)
{
	// Proto: void kernelTextCursorRight(void);
	// Desc : Move the cursor right one column.  Doesn't affect any characters there.
	return (_syscall(_fnum_textCursorRight, NULL));
}

_X_ int textEnableScroll(int enable)
{
	// Proto: int kernelTextEnableScroll(int);
	// Desc : Enable or disable screen scrolling for the current text output stream
	return (_syscall(_fnum_textEnableScroll, &enable));
}

_X_ void textScroll(int upDown)
{
	// Proto: void kernelTextScroll(int upDown)
	// Desc : Scroll the current text area up 'upDown' screenfulls, if negative, or down 'upDown' screenfulls, if positive.
	_syscall(_fnum_textScroll, &upDown);
}

_X_ int textGetNumColumns(void)
{
	// Proto: int kernelTextGetNumColumns(void);
	// Desc : Get the total number of columns in the text area.
	return (_syscall(_fnum_textGetNumColumns, NULL));
}

_X_ int textGetNumRows(void)
{
	// Proto: int kernelTextGetNumRows(void);
	// Desc : Get the total number of rows in the text area.
	return (_syscall(_fnum_textGetNumRows, NULL));
}

_X_ int textGetColumn(void)
{
	// Proto: int kernelTextGetColumn(void);
	// Desc : Get the number of the current column.  Zero-based.
	return (_syscall(_fnum_textGetColumn, NULL));
}

_X_ void textSetColumn(int c)
{
	// Proto: void kernelTextSetColumn(int);
	// Desc : Set the number of the current column.  Zero-based.  Doesn't affect any characters there.
	_syscall(_fnum_textSetColumn, &c);
}

_X_ int textGetRow(void)
{
	// Proto: int kernelTextGetRow(void);
	// Desc : Get the number of the current row.  Zero-based.
	return (_syscall(_fnum_textGetRow, NULL));
}

_X_ void textSetRow(int r)
{
	// Proto: void kernelTextSetRow(int);
	// Desc : Set the number of the current row.  Zero-based.  Doesn't affect any characters there.
	_syscall(_fnum_textSetRow, &r);
}

_X_ void textSetCursor(int on)
{
	// Proto: void kernelTextSetCursor(int);
	// Desc : Turn the cursor on (1) or off (0)
	_syscall(_fnum_textSetCursor, &on);
}

_X_ int textScreenClear(void)
{
	// Proto: void kernelTextScreenClear(void);
	// Desc : Erase all characters in the text area and set the row and column to (0, 0)
	return (_syscall(_fnum_textScreenClear, NULL));
}

_X_ int textScreenSave(textScreen *screen)
{
	// Proto: int kernelTextScreenSave(textScreen *);
	// Desc : Save the current screen in the supplied structure.  Use with the textScreenRestore function.
	return (_syscall(_fnum_textScreenSave, &screen));
}

_X_ int textScreenRestore(textScreen *screen)
{
	// Proto: int kernelTextScreenRestore(textScreen *);
	// Desc : Restore the screen previously saved in the structure with the textScreenSave function
	return (_syscall(_fnum_textScreenRestore, &screen));
}

_X_ int textInputStreamCount(objectKey strm)
{
	// Proto: int kernelTextInputStreamCount(kernelTextInputStream *);
	// Desc : Get the number of characters currently waiting in the specified input stream
	return (_syscall(_fnum_textInputStreamCount, &strm));
}

_X_ int textInputCount(void)
{
	// Proto: int kernelTextInputCount(void);
	// Desc : Get the number of characters currently waiting in the current input stream
	return (_syscall(_fnum_textInputCount, NULL));
}

_X_ int textInputStreamGetc(objectKey strm, char *cp _U_)
{
	// Proto: int kernelTextInputStreamGetc(kernelTextInputStream *, char *);
	// Desc : Get one character from the specified input stream (as an integer value).
	return (_syscall(_fnum_textInputStreamGetc, &strm));
}

_X_ int textInputGetc(char *cp)
{
	// Proto: char kernelTextInputGetc(void);
	// Desc : Get one character from the default input stream (as an integer value).
	return (_syscall(_fnum_textInputGetc, &cp));
}

_X_ int textInputStreamReadN(objectKey strm, int num _U_, char *buff _U_)
{
	// Proto: int kernelTextInputStreamReadN(kernelTextInputStream *, int, char *);
	// Desc : Read up to 'num' characters from the specified input stream into 'buff'
	return (_syscall(_fnum_textInputStreamReadN, &strm));
}

_X_ int textInputReadN(int num, char *buff _U_)
{
	// Proto: int kernelTextInputReadN(int, char *);
	// Desc : Read up to 'num' characters from the default input stream into 'buff'
	return (_syscall(_fnum_textInputReadN, &num));
}

_X_ int textInputStreamReadAll(objectKey strm, char *buff _U_)
{
	// Proto: int kernelTextInputStreamReadAll(kernelTextInputStream *, char *);
	// Desc : Read all of the characters from the specified input stream into 'buff'
	return (_syscall(_fnum_textInputStreamReadAll, &strm));
}

_X_ int textInputReadAll(char *buff)
{
	// Proto: int kernelTextInputReadAll(char *);
	// Desc : Read all of the characters from the default input stream into 'buff'
	return (_syscall(_fnum_textInputReadAll, &buff));
}

_X_ int textInputStreamAppend(objectKey strm, int ascii _U_)
{
	// Proto: int kernelTextInputStreamAppend(kernelTextInputStream *, int);
	// Desc : Append a character (as an integer value) to the end of the specified input stream.
	return (_syscall(_fnum_textInputStreamAppend, &strm));
}

_X_ int textInputAppend(int ascii)
{
	// Proto: int kernelTextInputAppend(int);
	// Desc : Append a character (as an integer value) to the end of the default input stream.
	return (_syscall(_fnum_textInputAppend, &ascii));
}

_X_ int textInputStreamAppendN(objectKey strm, int num _U_, char *str _U_)
{
	// Proto: int kernelTextInputStreamAppendN(kernelTextInputStream *, int, char *);
	// Desc : Append 'num' characters to the end of the specified input stream from 'str'
	return (_syscall(_fnum_textInputStreamAppendN, &strm));
}

_X_ int textInputAppendN(int num, char *str _U_)
{
	// Proto: int kernelTextInputAppendN(int, char *);
	// Desc : Append 'num' characters to the end of the default input stream from 'str'
	return (_syscall(_fnum_textInputAppendN, &num));
}

_X_ int textInputStreamRemove(objectKey strm)
{
	// Proto: int kernelTextInputStreamRemove(kernelTextInputStream *);
	// Desc : Remove one character from the start of the specified input stream.
	return (_syscall(_fnum_textInputStreamRemove, &strm));
}

_X_ int textInputRemove(void)
{
	// Proto: int kernelTextInputRemove(void);
	// Desc : Remove one character from the start of the default input stream.
	return (_syscall(_fnum_textInputRemove, NULL));
}

_X_ int textInputStreamRemoveN(objectKey strm, int num _U_)
{
	// Proto: int kernelTextInputStreamRemoveN(kernelTextInputStream *, int);
	// Desc : Remove 'num' characters from the start of the specified input stream.
	return (_syscall(_fnum_textInputStreamRemoveN, &strm));
}

_X_ int textInputRemoveN(int num)
{
	// Proto: int kernelTextInputRemoveN(int);
	// Desc : Remove 'num' characters from the start of the default input stream.
	return (_syscall(_fnum_textInputRemoveN, &num));
}

_X_ int textInputStreamRemoveAll(objectKey strm)
{
	// Proto: int kernelTextInputStreamRemoveAll(kernelTextInputStream *);
	// Desc : Empty the specified input stream.
	return (_syscall(_fnum_textInputStreamRemoveAll, &strm));
}

_X_ int textInputRemoveAll(void)
{
	// Proto: int kernelTextInputRemoveAll(void);
	// Desc : Empty the default input stream.
	return (_syscall(_fnum_textInputRemoveAll, NULL));
}

_X_ void textInputStreamSetEcho(objectKey strm, int onOff _U_)
{
	// Proto: void kernelTextInputStreamSetEcho(kernelTextInputStream *, int);
	// Desc : Set echo on (1) or off (0) for the specified input stream.  When on, any characters typed will be automatically printed to the text area.  When off, they won't.
	_syscall(_fnum_textInputStreamSetEcho, &strm);
}

_X_ void textInputSetEcho(int onOff)
{
	// Proto: void kernelTextInputSetEcho(int);
	// Desc : Set echo on (1) or off (0) for the default input stream.  When on, any characters typed will be automatically printed to the text area.  When off, they won't.
	_syscall(_fnum_textInputSetEcho, &onOff);
}


//
// Disk functions
//

_X_ int diskReadPartitions(const char *name)
{
	// Proto: int kernelDiskReadPartitions(const char *);
	// Desc : Tells the kernel to (re)read the partition table of disk 'name'.
	return (_syscall(_fnum_diskReadPartitions, &name));
}

_X_ int diskReadPartitionsAll(void)
{
	// Proto: int kernelDiskReadPartitionsAll(void);
	// Desc : Tells the kernel to (re)read all the disks' partition tables.
	return (_syscall(_fnum_diskReadPartitionsAll, NULL));
}

_X_ int diskSync(const char *name)
{
	// Proto: int kernelDiskSync(const char *);
	// Desc : Tells the kernel to synchronize the named disk, flushing any output.
	return (_syscall(_fnum_diskSync, &name));
}

_X_ int diskSyncAll(void)
{
	// Proto: int kernelDiskSyncAll(void);
	// Desc : Tells the kernel to synchronize all the disks, flushing any output.
	return (_syscall(_fnum_diskSyncAll, NULL));
}

_X_ int diskGetBoot(char *name)
{
	// Proto: int kernelDiskGetBoot(char *)
	// Desc : Get the disk name of the boot device.  Normally this will contain the root filesystem.
	return (_syscall(_fnum_diskGetBoot, &name));
}

_X_ int diskGetCount(void)
{
	// Proto: int kernelDiskGetCount(void);
	// Desc : Get the number of logical disk volumes recognized by the system
	return (_syscall(_fnum_diskGetCount, NULL));
}

_X_ int diskGetPhysicalCount(void)
{
	// Proto: int kernelDiskGetPhysicalCount(void);
	// Desc : Get the number of physical disk devices recognized by the system
	return (_syscall(_fnum_diskGetPhysicalCount, NULL));
}

_X_ int diskGet(const char *name, disk *userDisk _U_)
{
	// Proto: int kernelDiskGet(const char *, disk *);
	// Desc : Given a disk name string 'name', fill in the corresponding user space disk structure 'userDisk.
	return (_syscall(_fnum_diskGet, &name));
}

_X_ int diskGetAll(disk *userDiskArray, unsigned buffSize _U_)
{
	// Proto: int kernelDiskGetAll(disk *, unsigned);
	// Desc : Return user space disk structures in 'userDiskArray' for each logical disk, up to 'buffSize' bytes.
	return (_syscall(_fnum_diskGetAll, &userDiskArray));
}

_X_ int diskGetAllPhysical(disk *userDiskArray, unsigned buffSize _U_)
{
	// Proto: int kernelDiskGetAllPhysical(disk *, unsigned);
	// Desc : Return user space disk structures in 'userDiskArray' for each physical disk, up to 'buffSize' bytes.
	return (_syscall(_fnum_diskGetAllPhysical, &userDiskArray));
}

_X_ int diskGetFilesystemType(const char *name, char *buf _U_, unsigned bufSize _U_)
{
	// Proto: int kernelDiskGetFilesystemType(const char *, char *, unsigned);
	// Desc : This function attempts to explicitly detect the filesystem type on disk 'name', and copy up to 'bufSize' bytes of the filesystem type name into 'buf'.  Particularly useful for things like removable media where the correct info may not be automatically provided in the disk structure.
	return (_syscall(_fnum_diskGetFilesystemType, &name));
}

_X_ int diskGetMsdosPartType(int tag, msdosPartType *p _U_)
{
	// Proto: int kernelDiskGetMsdosPartType(int, msdosPartType *);
	// Desc : Gets the MS-DOS partition type description for the corresponding tag.  This function was added specifically for use by programs such as 'fdisk' to get descriptions of different MS-DOS types known to the kernel.
	return (_syscall(_fnum_diskGetMsdosPartType, &tag));
}

_X_ msdosPartType *diskGetMsdosPartTypes(void)
{
	// Proto: msdosPartType *kernelDiskGetMsdosPartTypes(void);
	// Desc : Like diskGetMsdosPartType(), but returns a pointer to a list of all known MS-DOS types.  The memory is allocated dynamically and should be deallocated with a call to memoryRelease()
	return ((msdosPartType *)(long) _syscall(_fnum_diskGetMsdosPartTypes,
		NULL));
}

_X_ int diskGetGptPartType(guid *g, gptPartType *p _U_)
{
	// Proto: int kernelDiskGetGptPartType(guid *, gptPartType *);
	// Desc : Gets the GPT partition type description for the corresponding GUID.  This function was added specifically for use by programs such as 'fdisk' to get descriptions of different GPT types known to the kernel.
	return (_syscall(_fnum_diskGetGptPartType, &g));
}

_X_ gptPartType *diskGetGptPartTypes(void)
{
	// Proto: gptPartType *kernelDiskGetGptPartTypes(void);
	// Desc : Like diskGetGptPartType(), but returns a pointer to a list of all known GPT types.  The memory is allocated dynamically and should be deallocated with a call to memoryRelease()
	return ((gptPartType *)(long) _syscall(_fnum_diskGetGptPartTypes, NULL));
}

_X_ int diskSetFlags(const char *name, unsigned flags _U_, int set _U_)
{
	// Proto: int kernelDiskSetFlags(const char *, unsigned, int);
	// Desc : Set or clear the (user-settable) disk flags bits in 'flags' of the disk 'name'.
	return (_syscall(_fnum_diskSetFlags, &name));
}

_X_ int diskSetLockState(const char *name, int state _U_)
{
	// Proto: int kernelDiskSetLockState(const char *diskName, int state);
	// Desc : Set the locked state of the disk 'name' to either unlocked (0) or locked (1)
	return (_syscall(_fnum_diskSetLockState, &name));
}

_X_ int diskSetDoorState(const char *name _U_, int state _U_)
{
	// Proto: int kernelDiskSetDoorState(const char *, int);
	// Desc : Open (1) or close (0) the disk 'name'.  May require a unlocking the door first, see diskSetLockState().
	return (_syscall(_fnum_diskSetDoorState, &name));
}

_X_ int diskMediaPresent(const char *diskName)
{
	// Proto: int kernelDiskMediaPresent(const char *diskName)
	// Desc : Returns 1 if the removable disk 'diskName' is known to have media present.
	return (_syscall(_fnum_diskMediaPresent, &diskName));
}

_X_ int diskReadSectors(const char *name, uquad_t sect _U_, uquad_t count _U_, void *buf _U_)
{
	// Proto: int kernelDiskReadSectors(const char *, unsigned, unsigned, void *)
	// Desc : Read 'count' sectors from disk 'name', starting at (zero-based) logical sector number 'sect'.  Put the data in memory area 'buf'.  This function requires supervisor privilege.
	return (_syscall(_fnum_diskReadSectors, &name));
}

_X_ int diskWriteSectors(const char *name, uquad_t sect _U_, uquad_t count _U_, const void *buf _U_)
{
	// Proto: int kernelDiskWriteSectors(const char *, unsigned, unsigned, const void *)
	// Desc : Write 'count' sectors to disk 'name', starting at (zero-based) logical sector number 'sect'.  Get the data from memory area 'buf'.  This function requires supervisor privilege.
	return (_syscall(_fnum_diskWriteSectors, &name));
}

_X_ int diskEraseSectors(const char *name, uquad_t sect _U_, uquad_t count _U_, int passes _U_)
{
	// Proto: int kernelDiskEraseSectors(const char *, unsigned, unsigned, int);
	// Desc : Synchronously and securely erases disk sectors.  It writes ('passes' - 1) successive passes of random data followed by a final pass of NULLs, to disk 'name' starting at (zero-based) logical sector number 'sect'.  This function requires supervisor privilege.
	return (_syscall(_fnum_diskEraseSectors, &name));
}

_X_ int diskGetStats(const char *name, diskStats *stats _U_)
{
	// Proto: int kernelDiskGetStats(const char *, diskStats *);
	// Desc: Return performance stats about the disk 'name' (if non-NULL,
	// otherwise about all the disks combined).
	return (_syscall(_fnum_diskGetStats, &name));
}

_X_ int diskRamDiskCreate(unsigned size, char *name _U_)
{
	// Proto: int kernelDiskRamDiskCreate(unsigned, char *);
	// Desc : Given a size in bytes, and a pointer to a buffer 'name', create a RAM disk.  If 'name' is non-NULL, place the name of the new disk in the buffer.
	return (_syscall(_fnum_diskRamDiskCreate, &size));
}

_X_ int diskRamDiskDestroy(const char *name)
{
	// Proto: int kernelDiskRamDiskDestroy(const char *);
	// Desc : Given the name of an existing RAM disk 'name', destroy and deallocate it.
	return (_syscall(_fnum_diskRamDiskDestroy, &name));
}


//
// Filesystem functions
//

_X_ int filesystemScan(const char *name)
{
	// Proto: int kernelFilesystemScan(const char *);
	// Desc : Ask the kernel to re-scan the filesystem type on the logical volume 'name'.
	return (_syscall(_fnum_filesystemScan, &name));
}

_X_ int filesystemFormat(const char *name, const char *type _U_, const char *label _U_, int longFormat _U_, progress *prog _U_)
{
	// Proto: int kernelFilesystemFormat(const char *, const char *, const char *, int, progress *);
	// Desc : Format the logical volume 'name', with a string 'type' representing the preferred filesystem type (for example, "fat", "fat16", "fat32, etc).  Label it with 'label'.  'longFormat' will do a sector-by-sector format, if supported, and progress can optionally be monitored by passing a non-NULL progress structure pointer 'prog'.  It is optional for filesystem drivers to implement this function.
	return (_syscall(_fnum_filesystemFormat, &name));
}

_X_ int filesystemClobber(const char *name)
{
	// Proto: int kernelFilesystemClobber(const char *);
	// Desc : Clobber all known filesystem types on the logical volume 'theDisk'.  It is optional for filesystem drivers to implement this function.
	return (_syscall(_fnum_filesystemClobber, &name));
}

_X_ int filesystemCheck(const char *name, int force _U_, int repair _U_, progress *prog _U_)
{
	// Proto: int kernelFilesystemCheck(const char *, int, int, progress *)
	// Desc : Check the filesystem on disk 'name'.  If 'force' is non-zero, the filesystem will be checked regardless of whether the filesystem driver thinks it needs to be.  If 'repair' is non-zero, the filesystem driver will attempt to repair any errors found.  If 'repair' is zero, a non-zero return value may indicate that errors were found.  If 'repair' is non-zero, a non-zero return value may indicate that errors were found but could not be fixed.  Progress can optionally be monitored by passing a non-NULL progress structure pointer 'prog'.  It is optional for filesystem drivers to implement this function.
	return (_syscall(_fnum_filesystemCheck, &name));
}

_X_ int filesystemDefragment(const char *name, progress *prog _U_)
{
	// Proto: int kernelFilesystemDefragment(const char *, progress *)
	// Desc : Defragment the filesystem on disk 'name'.  Progress can optionally be monitored by passing a non-NULL progress structure pointer 'prog'.  It is optional for filesystem drivers to implement this function.
	return (_syscall(_fnum_filesystemDefragment, &name));
}

_X_ int filesystemResizeConstraints(const char *name, uquad_t *minBlocks _U_, uquad_t *maxBlocks _U_, progress *prog _U_)
{
	// Proto: int kernelFilesystemResizeConstraints(const char *, uquad_t *, uquad_t *, progress *);
	// Desc : Get the minimum ('minBlocks') and maximum ('maxBlocks') number of blocks for a filesystem resize on disk 'name'.  Progress can optionally be monitored by passing a non-NULL progress structure pointer 'prog'.  It is optional for filesystem drivers to implement this function.
	return (_syscall(_fnum_filesystemResizeConstraints, &name));
}

_X_ int filesystemResize(const char *name, uquad_t blocks _U_, progress *prog _U_)
{
	// Proto: int kernelFilesystemResize(const char *, uquad_t, progress *);
	// Desc : Resize the filesystem on disk 'name' to the given number of blocks 'blocks'.  Progress can optionally be monitored by passing a non-NULL progress structure pointer 'prog'.  It is optional for filesystem drivers to implement this function.
	return (_syscall(_fnum_filesystemResize, &name));
}

_X_ int filesystemMount(const char *name, const char *mp _U_)
{
	// Proto: int kernelFilesystemMount(const char *, const char *)
	// Desc : Mount the filesystem on disk 'name', using the mount point specified by the absolute pathname 'mp'.  Note that no file or directory called 'mp' should exist, as the mount function will expect to be able to create it.
	return (_syscall(_fnum_filesystemMount, &name));
}

_X_ int filesystemUnmount(const char *mp)
{
	// Proto: int kernelFilesystemUnmount(const char *);
	// Desc : Unmount the filesystem mounted represented by the mount point 'fs'.
	return (_syscall(_fnum_filesystemUnmount, &mp));
}

_X_ uquad_t filesystemGetFreeBytes(const char *fs)
{
	// Proto: uquad_t kernelFilesystemGetFreeBytes(const char *);
	// Desc : Returns the amount of free space, in bytes, on the filesystem represented by the mount point 'fs'.
	return (_syscall(_fnum_filesystemGetFreeBytes, &fs));
}

_X_ unsigned filesystemGetBlockSize(const char *fs)
{
	// Proto: unsigned kernelFilesystemGetBlockSize(const char *);
	// Desc : Returns the block size (for example, 512 or 1024) of the filesystem represented by the mount point 'fs'.
	return (_syscall(_fnum_filesystemGetBlockSize, &fs));
}


//
// File functions
//

_X_ int fileFixupPath(const char *origPath, char *newPath _U_)
{
	// Proto: int kernelFileFixupPath(const char *, char *);
	// Desc : Take the absolute pathname in 'origPath' and fix it up.  This means eliminating extra file separator characters (for example) and resolving links or '.' or '..' components in the pathname.
	return (_syscall(_fnum_fileFixupPath, &origPath));
}

_X_ int fileGetDisk(const char *path, disk *d _U_)
{
	// Proto: int kernelFileGetDisk(const char *, disk *);
	// Desc : Given the file name 'path', return the user space structure for the logical disk that the file resides on.
	return (_syscall(_fnum_fileGetDisk, &path));
}

_X_ int fileCount(const char *path)
{
	// Proto: int kernelFileCount(const char *);
	// Desc : Get the count of file entries from the directory referenced by 'path'.
	return (_syscall(_fnum_fileCount, &path));
}

_X_ int fileFirst(const char *path, file *f _U_)
{
	// Proto: int kernelFileFirst(const char *, file *);
	// Desc : Get the first file from the directory referenced by 'path'.  Put the information in the file structure 'f'.
	return (_syscall(_fnum_fileFirst, &path));
}

_X_ int fileNext(const char *path, file *f _U_)
{
	// Proto: int kernelFileNext(const char *, file *);
	// Desc : Get the next file from the directory referenced by 'path'.  'f' should be a file structure previously filled by a call to either fileFirst() or fileNext().
	return (_syscall(_fnum_fileNext, &path));
}

_X_ int fileFind(const char *name, file *f _U_)
{
	// Proto: int kernelFileFind(const char *, kernelFile *);
	// Desc : Find the file referenced by 'name', and fill the file data structure 'f' with the results if successful.
	return (_syscall(_fnum_fileFind, &name));
}

_X_ int fileOpen(const char *name, int mode _U_, file *f _U_)
{
	// Proto: int kernelFileOpen(const char *, int, file *);
	// Desc : Open the file referenced by 'name' using the file open mode 'mode' (defined in <sys/file.h>).  Update the file data structure 'f' if successful.
	return (_syscall(_fnum_fileOpen, &name));
}

_X_ int fileClose(file *f)
{
	// Proto: int kernelFileClose(const char *, file *);
	// Desc : Close the previously opened file 'f'.
	return (_syscall(_fnum_fileClose, &f));
}

_X_ int fileRead(file *f, unsigned blocknum _U_, unsigned blocks _U_, void *buff _U_)
{
	// Proto: int kernelFileRead(file *, unsigned, unsigned, void *);
	// Desc : Read data from the previously opened file 'f'.  'f' should have been opened in a read or read/write mode.  Read 'blocks' blocks (see the filesystem functions for information about getting the block size of a given filesystem) and put them in buffer 'buff'.
	return (_syscall(_fnum_fileRead, &f));
}

_X_ int fileWrite(file *f, unsigned blocknum _U_, unsigned blocks _U_, void *buff _U_)
{
	// Proto: int kernelFileWrite(file *, unsigned, unsigned, void *);
	// Desc : Write data to the previously opened file 'f'.  'f' should have been opened in a write or read/write mode.  Write 'blocks' blocks (see the filesystem functions for information about getting the block size of a given filesystem) from the buffer 'buff'.
	return (_syscall(_fnum_fileWrite, &f));
}

_X_ int fileDelete(const char *name)
{
	// Proto: int kernelFileDelete(const char *);
	// Desc : Delete the file referenced by the pathname 'name'.
	return (_syscall(_fnum_fileDelete, &name));
}

_X_ int fileDeleteRecursive(const char *name)
{
	// Proto: int kernelFileDeleteRecursive(const char *);
	// Desc : Recursively delete filesystem items, starting with the one referenced by the pathname 'name'.
	return (_syscall(_fnum_fileDeleteRecursive, &name));
}

_X_ int fileDeleteSecure(const char *name, int passes _U_)
{
	// Proto: int kernelFileDeleteSecure(const char *);
	// Desc : Securely delete the file referenced by the pathname 'name'.  'passes' indicates the number of times to overwrite the file.  The file is overwritten (number - 1) times with random data, and then NULLs.  A larger number of passes is more secure but takes longer.
	return (_syscall(_fnum_fileDeleteSecure, &name));
}

_X_ int fileMakeDir(const char *name)
{
	// Proto: int kernelFileMakeDir(const char *);
	// Desc : Create a directory to be referenced by the pathname 'name'.
	return (_syscall(_fnum_fileMakeDir, &name));
}

_X_ int fileRemoveDir(const char *name)
{
	// Proto: int kernelFileRemoveDir(const char *);
	// Desc : Remove the directory referenced by the pathname 'name'.
	return (_syscall(_fnum_fileRemoveDir, &name));
}

_X_ int fileCopy(const char *src, const char *dest _U_)
{
	// Proto: int kernelFileCopy(const char *, const char *);
	// Desc : Copy the file referenced by the pathname 'src' to the pathname 'dest'.  This will overwrite 'dest' if it already exists.
	return (_syscall(_fnum_fileCopy, &src));
}

_X_ int fileCopyRecursive(const char *src, const char *dest _U_)
{
	// Proto: int kernelFileCopyRecursive(const char *, const char *);
	// Desc : Recursively copy the file referenced by the pathname 'src' to the pathname 'dest'.  If 'src' is a regular file, the result will be the same as using the non-recursive call.  However if it is a directory, all contents of the directory and its subdirectories will be copied.  This will overwrite any files in the 'dest' tree if they already exist.
	return (_syscall(_fnum_fileCopyRecursive, &src));
}

_X_ int fileMove(const char *src, const char *dest _U_)
{
	// Proto: int kernelFileMove(const char *, const char *);
	// Desc : Move (rename) a file referenced by the pathname 'src' to the pathname 'dest'.
	return (_syscall(_fnum_fileMove, &src));
}

_X_ int fileTimestamp(const char *name)
{
	// Proto: int kernelFileTimestamp(const char *);
	// Desc : Update the time stamp on the file referenced by the pathname 'name'
	return (_syscall(_fnum_fileTimestamp, &name));
}

_X_ int fileSetSize(file *f, unsigned size _U_)
{
	// Proto: int kernelFileSetSize(file *, unsigned);
	// Desc : Change the length of the file 'file' to the new length 'length'
	return (_syscall(_fnum_fileSetSize, &f));
}

_X_ int fileGetTempName(char *buff, unsigned len _U_)
{
	// Proto: int kernelFileGetTempName(char *, unsigned);
	// Desc : Given a buffer 'buff' and a buffer size 'len', get a file name to use as a temporary file or directory.  Doesn't create anything, only computes a suitable name.
	return (_syscall(_fnum_fileGetTempName, &buff));
}

_X_ int fileGetTemp(file *f)
{
	// Proto: int kernelFileGetTemp(void);
	// Desc : Create and open a temporary file in write mode.
	return (_syscall(_fnum_fileGetTemp, &f));
}

_X_ int fileGetFullPath(file *f, char *buff _U_, int len _U_)
{
	// Proto: int kernelFileGetFullPath(file *, char *, int);
	// Desc : Given a file structure, return up to 'len' bytes of the fully-qualified file name in the buffer 'buff'.
	return (_syscall(_fnum_fileGetFullPath, &f));
}

_X_ int fileStreamOpen(const char *name, int mode _U_, fileStream *f _U_)
{
	// Proto: int kernelFileStreamOpen(const char *, int, fileStream *);
	// Desc : Open the file referenced by the pathname 'name' for streaming operations, using the open mode 'mode' (defined in <sys/file.h>).  Fills the fileStream data structure 'f' with information needed for subsequent filestream operations.
	return (_syscall(_fnum_fileStreamOpen, &name));
}

_X_ int fileStreamSeek(fileStream *f, unsigned offset _U_)
{
	// Proto: int kernelFileStreamSeek(fileStream *, unsigned);
	// Desc : Seek the filestream 'f' to the absolute position 'offset'
	return (_syscall(_fnum_fileStreamSeek, &f));
}

_X_ int fileStreamRead(fileStream *f, unsigned bytes _U_, char *buff _U_)
{
	// Proto: int kernelFileStreamRead(fileStream *, unsigned, char *);
	// Desc : Read 'bytes' bytes from the filestream 'f' and put them into 'buff'.
	return (_syscall(_fnum_fileStreamRead, &f));
}

_X_ int fileStreamReadLine(fileStream *f, unsigned bytes _U_, char *buff _U_)
{
	// Proto: int kernelFileStreamReadLine(fileStream *, unsigned, char *);
	// Desc : Read a complete line of text from the filestream 'f', and put up to 'bytes' characters into 'buff'
	return (_syscall(_fnum_fileStreamReadLine, &f));
}

_X_ int fileStreamWrite(fileStream *f, unsigned bytes _U_, const char *buff _U_)
{
	// Proto: int kernelFileStreamWrite(fileStream *, unsigned, char *);
	// Desc : Write 'bytes' bytes from the buffer 'buff' to the filestream 'f'.
	return (_syscall(_fnum_fileStreamWrite, &f));
}

_X_ int fileStreamWriteStr(fileStream *f, const char *buff _U_)
{
	// Proto: int kernelFileStreamWriteStr(fileStream *, char *);
	// Desc : Write the string in 'buff' to the filestream 'f'
	return (_syscall(_fnum_fileStreamWriteStr, &f));
}

_X_ int fileStreamWriteLine(fileStream *f, const char *buff _U_)
{
	// Proto: int kernelFileStreamWriteLine(fileStream *, char *);
	// Desc : Write the string in 'buff' to the filestream 'f', and add a newline at the end
	return (_syscall(_fnum_fileStreamWriteLine, &f));
}

_X_ int fileStreamFlush(fileStream *f)
{
	// Proto: int kernelFileStreamFlush(fileStream *);
	// Desc : Flush filestream 'f'.
	return (_syscall(_fnum_fileStreamFlush, &f));
}

_X_ int fileStreamClose(fileStream *f)
{
	// Proto: int kernelFileStreamClose(fileStream *);
	// Desc : [Flush and] close the filestream 'f'.
	return (_syscall(_fnum_fileStreamClose, &f));
}

_X_ int fileStreamGetTemp(fileStream *f)
{
	// Proto: int kernelFileStreamGetTemp(fileStream *);
	// Desc : Open a temporary filestream 'f'.
	return (_syscall(_fnum_fileStreamGetTemp, &f));
}


//
// Memory functions
//

_X_ void *memoryGet(unsigned size, const char *desc _U_)
{
	// Proto: void *kernelMemoryGet(unsigned, const char *);
	// Desc : Return a pointer to a new block of memory of size 'size' and (optional) physical alignment 'align', adding the (optional) description 'desc'.  If no specific alignment is required, use '0'.  Memory allocated using this function is automatically cleared (like 'calloc').
		return ((void *)(long) _syscall(_fnum_memoryGet, &size));
}

_X_ int memoryRelease(void *p)
{
	// Proto: int kernelMemoryRelease(void *);
	// Desc : Release the memory block starting at the address 'p'.  Must have been previously allocated using the memoryRequestBlock() function.
	return (_syscall(_fnum_memoryRelease, &p));
}

_X_ int memoryReleaseAllByProcId(int pid)
{
	// Proto: int kernelMemoryReleaseAllByProcId(int);
	// Desc : Release all memory allocated to/by the process referenced by process ID 'pid'.  Only privileged functions can release memory owned by other processes.
	return (_syscall(_fnum_memoryReleaseAllByProcId, &pid));
}

_X_ int memoryGetStats(memoryStats *stats, int kernel _U_)
{
	// Proto: int kernelMemoryGetStats(memoryStats *, int);
	// Desc : Returns the current memory totals and usage values to the current output stream.  If non-zero, the flag 'kernel' will return kernel heap statistics instead of overall system statistics.
	return (_syscall(_fnum_memoryGetStats, &stats));
}

_X_ int memoryGetBlocks(memoryBlock *blocksArray, unsigned buffSize _U_, int kernel _U_)
{
	// Proto: int kernelMemoryGetBlocks(memoryBlock *, unsigned, int);
	// Desc : Returns a copy of the array of used memory blocks in 'blocksArray', up to 'buffSize' bytes.  If non-zero, the flag 'kernel' will return kernel heap blocks instead of overall heap allocations.
	return (_syscall(_fnum_memoryGetBlocks, &blocksArray));
}

_X_ int memoryBlockInfo(void *p, memoryBlock *block _U_)
{
	// Proto: int kernelMemoryBlockInfo(void *, memoryBlock *);
	// Desc : Fills in the structure 'block' with information about the allocated memory block starting at virtual address 'p'
	return (_syscall(_fnum_memoryBlockInfo, &p));
}


//
// Multitasker functions
//

_X_ int multitaskerCreateProcess(const char *name, int privilege _U_, processImage *execImage _U_)
{
	// Proto: int kernelMultitaskerCreateProcess(const char *, int, processImage *);
	// Desc : Create a new process.  'name' will be the new process' name.  'privilege' is the privilege level.  'execImage' is a processImage structure that describes the loaded location of the file, the program's desired virtual address, entry point, size, etc.  If the value returned by the call is a positive integer, the call was successful and the value is the new process' process ID.  New processes are created and left in a stopped state, so if you want it to run you will need to set it to a running state ('ready', actually) using the function call multitaskerSetProcessState().
	return (_syscall(_fnum_multitaskerCreateProcess, &name));
}

_X_ int multitaskerSpawn(void *addr, const char *name _U_, int numargs _U_, void *args[] _U_)
{
	// Proto: int kernelMultitaskerSpawn(void *, const char *, int, void *[]);
	// Desc : Spawn a thread from the current process.  The starting point of the code (for example, a function address) should be specified as 'addr'.  'name' will be the new thread's name.  'numargs' and 'args' will be passed as the "int argc, char *argv[]) parameters of the new thread.  If there are no arguments, these should be 0 and NULL, respectively.  If the value returned by the call is a positive integer, the call was successful and the value is the new thread's process ID.  New threads are created and made runnable, so there is no need to change its state to activate it.
	return (_syscall(_fnum_multitaskerSpawn, &addr));
}

_X_ int multitaskerGetCurrentProcessId(void)
{
	// Proto: int kernelMultitaskerGetCurrentProcessId(void);
	// Desc : Returns the process ID of the calling program.
	return (_syscall(_fnum_multitaskerGetCurrentProcessId, NULL));
}

_X_ int multitaskerGetProcess(int pid, process *proc _U_)
{
	// Proto: int kernelMultitaskerGetProcess(int, process *);
	// Desc : Returns the process structure for the supplied process ID.
	return (_syscall(_fnum_multitaskerGetProcess, &pid));
}

_X_ int multitaskerGetProcessByName(const char *name, process *proc _U_)
{
	// Proto: int kernelMultitaskerGetProcessByName(const char *, process *);
	// Desc : Returns the process structure for the supplied process name
	return (_syscall(_fnum_multitaskerGetProcessByName, &name));
}

_X_ int multitaskerGetProcesses(void *buffer, unsigned buffSize _U_)
{
	// Proto: int kernelMultitaskerGetProcesses(void *, unsigned);
	// Desc : Fills 'buffer' with up to 'buffSize' bytes' worth of process structures, and returns the number of structures copied.
	return (_syscall(_fnum_multitaskerGetProcesses, &buffer));
}

_X_ int multitaskerSetProcessState(int pid, int state _U_)
{
	// Proto: int kernelMultitaskerSetProcessState(int, kernelProcessState);
	// Desc : Sets the state of the process referenced by process ID 'pid' to the new state 'state'.
	return (_syscall(_fnum_multitaskerSetProcessState, &pid));
}

_X_ int multitaskerProcessIsAlive(int pid)
{
	// Proto: int kernelMultitaskerProcessIsAlive(int);
	// Desc : Returns 1 if the process with the id 'pid' still exists and is in a 'runnable' (viable) state.  Returns 0 if the process does not exist or is in a 'finished' state.
	return (_syscall(_fnum_multitaskerProcessIsAlive, &pid));
}

_X_ int multitaskerSetProcessPriority(int pid, int priority _U_)
{
	// Proto: int kernelMultitaskerSetProcessPriority(int, int);
	// Desc : Sets the priority of the process referenced by process ID 'pid' to 'priority'.
	return (_syscall(_fnum_multitaskerSetProcessPriority, &pid));
}

_X_ int multitaskerGetProcessPrivilege(int pid)
{
	// Proto: kernelMultitaskerGetProcessPrivilege(int);
	// Desc : Gets the privilege level of the process referenced by process ID 'pid'.
	return (_syscall(_fnum_multitaskerGetProcessPrivilege, &pid));
}

_X_ int multitaskerGetCurrentDirectory(char *buff, int buffsz _U_)
{
	// Proto: int kernelMultitaskerGetCurrentDirectory(char *, int);
	// Desc : Returns the absolute pathname of the calling process' current directory.  Puts the value in the buffer 'buff' which is of size 'buffsz'.
	return (_syscall(_fnum_multitaskerGetCurrentDirectory, &buff));
}

_X_ int multitaskerSetCurrentDirectory(const char *buff)
{
	// Proto: int kernelMultitaskerSetCurrentDirectory(const char *);
	// Desc : Sets the current directory of the calling process to the absolute pathname 'buff'.
	return (_syscall(_fnum_multitaskerSetCurrentDirectory, &buff));
}

_X_ objectKey multitaskerGetTextInput(void)
{
	// Proto: kernelTextInputStream *kernelMultitaskerGetTextInput(void);
	// Desc : Get an object key to refer to the current text input stream of the calling process.
	return ((objectKey)(long) _syscall(_fnum_multitaskerGetTextInput, NULL));
}

_X_ int multitaskerSetTextInput(int processId, objectKey key _U_)
{
	// Proto: int kernelMultitaskerSetTextInput(int, kernelTextInputStream *);
	// Desc : Set the text input stream of the process referenced by process ID 'processId' to a text stream referenced by the object key 'key'.
	return (_syscall(_fnum_multitaskerSetTextInput, &processId));
}

_X_ objectKey multitaskerGetTextOutput(void)
{
	// Proto: kernelTextOutputStream *kernelMultitaskerGetTextOutput(void);
	// Desc : Get an object key to refer to the current text output stream of the calling process.
	return ((objectKey)(long) _syscall(_fnum_multitaskerGetTextOutput, NULL));
}

_X_ int multitaskerSetTextOutput(int processId, objectKey key _U_)
{
	// Proto: int kernelMultitaskerSetTextOutput(int, kernelTextOutputStream *);
	// Desc : Set the text output stream of the process referenced by process ID 'processId' to a text stream referenced by the object key 'key'.
	return (_syscall(_fnum_multitaskerSetTextOutput, &processId));
}

_X_ int multitaskerDuplicateIO(int pid1, int pid2 _U_, int clear _U_)
{
	// Proto: int kernelMultitaskerDuplicateIO(int, int, int);
	// Desc : Set 'pid2' to use the same input and output streams as 'pid1', and if 'clear' is non-zero, clear any pending input or output.
	return (_syscall(_fnum_multitaskerDuplicateIO, &pid1));
}

_X_ int multitaskerGetProcessorTime(clock_t *clk)
{
	// Proto: int kernelMultitaskerGetProcessorTime(clock_t *);
	// Desc : Fill the clock_t structure with the amount of processor time consumed by the calling process.
	return (_syscall(_fnum_multitaskerGetProcessorTime, &clk));
}

_X_ void multitaskerYield(void)
{
	// Proto: void kernelMultitaskerYield(void);
	// Desc : Yield the remainder of the current processor timeslice back to the multitasker's scheduler.  It's nice to do this when you are waiting for some event, so that your process is not 'hungry' (i.e. hogging processor cycles unnecessarily).
	_syscall(_fnum_multitaskerYield, NULL);
}

_X_ void multitaskerWait(unsigned milliseconds)
{
	// Proto: void kernelMultitaskerWait(unsigned);
	// Desc : Yield the remainder of the current processor timeslice back to the multitasker's scheduler, and wait at least 'milliseconds' before running the calling process again.
	_syscall(_fnum_multitaskerWait, &milliseconds);
}

_X_ int multitaskerBlock(int pid)
{
	// Proto: int kernelMultitaskerBlock(int);
	// Desc : Yield the remainder of the current processor timeslice back to the multitasker's scheduler, and block on the process referenced by process ID 'pid'.  This means that the calling process will not run again until process 'pid' has terminated.  The return value of this function is the return value of process 'pid'.
	return (_syscall(_fnum_multitaskerBlock, &pid));
}

_X_ int multitaskerDetach(void)
{
	// Proto: int kernelMultitaskerDetach(void);
	// Desc : This allows a program to 'daemonize', detaching from the IO streams of its parent and, if applicable, the parent stops blocking.  Useful for a process that want to operate in the background, or that doesn't want to be killed if its parent is killed.
	return (_syscall(_fnum_multitaskerDetach, NULL));
}

_X_ int multitaskerKillProcess(int pid, int force _U_)
{
	// Proto: int kernelMultitaskerKillProcess(int);
	// Desc : Terminate the process referenced by process ID 'pid'.  If 'force' is non-zero, the multitasker will attempt to ignore any errors and dismantle the process with extreme prejudice.  The 'force' flag also has the necessary side effect of killing any child threads spawned by process 'pid'.  (Otherwise, 'pid' is left in a stopped state until its threads have terminated normally).
	return (_syscall(_fnum_multitaskerKillProcess, &pid));
}

_X_ int multitaskerKillByName(const char *name, int force _U_)
{
	// Proto: int kernelMultitaskerKillByName(const char *name, int force)
	// Desc : Like multitaskerKillProcess, except that it attempts to kill all instances of processes whose names match 'name'
	return (_syscall(_fnum_multitaskerKillByName, &name));
}

_X_ int multitaskerTerminate(int code)
{
	// Proto: int kernelMultitaskerTerminate(int);
	// Desc : Terminate the calling process, returning the exit code 'code'
	return (_syscall(_fnum_multitaskerTerminate, &code));
}

_X_ int multitaskerSignalSet(int processId, int sig _U_, int on _U_)
{
	// Proto: int kernelMultitaskerSignalSet(int, int, int);
	// Desc : Set the current process' signal handling enabled (on) or disabled for the specified signal number 'sig'
	return (_syscall(_fnum_multitaskerSignalSet, &processId));
}

_X_ int multitaskerSignal(int processId, int sig _U_)
{
	// Proto: int kernelMultitaskerSignal(int, int);
	// Desc : Send the requested signal 'sig' to the process 'processId'
	return (_syscall(_fnum_multitaskerSignal, &processId));
}

_X_ int multitaskerSignalRead(int processId)
{
	// Proto: int kernelMultitaskerSignalRead(int);
	// Desc : Returns the number code of the next pending signal for the current process, or 0 if no signals are pending.
	return (_syscall(_fnum_multitaskerSignalRead, &processId));
}

_X_ int multitaskerGetIOPerm(int processId, int portNum _U_)
{
	// Proto: int kernelMultitaskerGetIOPerm(int, int);
	// Desc : Returns 1 if the process with process ID 'processId' can do I/O on port 'portNum'
	return (_syscall(_fnum_multitaskerGetIOPerm, &processId));
}

_X_ int multitaskerSetIOPerm(int processId, int portNum _U_, int yesNo _U_)
{
	// Proto: int kernelMultitaskerSetIOPerm(int, int, int);
	// Desc : Set I/O permission port 'portNum' for the process with process ID 'processId'.  If 'yesNo' is non-zero, permission will be granted.
	return (_syscall(_fnum_multitaskerSetIOPerm, &processId));
}

_X_ int multitaskerStackTrace(int processId)
{
	// Proto: int kernelMultitaskerStackTrace(int);
	// Desc : Print a stack trace for the process with process ID 'processId'.
	return (_syscall(_fnum_multitaskerStackTrace, &processId));
}


//
// Loader functions
//

_X_ void *loaderLoad(const char *filename, file *theFile _U_)
{
	// Proto: void *kernelLoaderLoad(const char *, file *);
	// Desc : Load a file referenced by the pathname 'filename', and fill the file data structure 'theFile' with the details.  The pointer returned points to the resulting file data.
	return ((void *)(long) _syscall(_fnum_loaderLoad, &filename));
}

_X_ objectKey loaderClassify(const char *fileName, void *fileData _U_, unsigned size _U_, loaderFileClass *fileClass _U_)
{
	// Proto: kernelFileClass *kernelLoaderClassify(const char *, void *, unsigned, loaderFileClass *);
	// Desc : Given a file by the name 'fileName', the contents 'fileData', of size 'size', get the kernel loader's idea of the file type.  If successful, the return  value is non-NULL and the loaderFileClass structure 'fileClass' is filled out with the known information.
	return ((objectKey)(long) _syscall(_fnum_loaderClassify, &fileName));
}

_X_ objectKey loaderClassifyFile(const char *fileName, loaderFileClass *fileClass _U_)
{
	// Proto: kernelFileClass *kernelLoaderClassifyFile(const char *, loaderFileClass *);
	// Desc : Like loaderClassify(), except the first argument 'fileName' is a file name to classify.  Returns the kernel loader's idea of the file type.  If successful, the return value is non-NULL and the loaderFileClass structure 'fileClass' is filled out with the known information.
	return ((objectKey)(long) _syscall(_fnum_loaderClassifyFile, &fileName));
}

_X_ loaderSymbolTable *loaderGetSymbols(const char *fileName)
{
	// Proto: loaderSymbolTable *kernelLoaderGetSymbols(const char *);
	// Desc : Given a binary executable, library, or object file 'fileName', return a loaderSymbolTable structure filled out with the loader symbols from the file.
	return ((loaderSymbolTable *)(long) _syscall(_fnum_loaderGetSymbols,
		&fileName));
}

_X_ int loaderCheckCommand(const char *command)
{
	// Proto: int kernelLoaderCheckCommand(const char *);
	// Desc : Takes a command line string 'command' and ensures that the program (the first part of the string) exists.
	return (_syscall(_fnum_loaderCheckCommand, &command));
}

_X_ int loaderLoadProgram(const char *command, int privilege _U_)
{
	// Proto: int kernelLoaderLoadProgram(const char *, int);
	// Desc : Run 'command' as a process with the privilege level 'privilege'.  If successful, the call's return value is the process ID of the new process.  The process is left in a stopped state and must be set to a running state explicitly using the multitasker function multitaskerSetProcessState() or the loader function loaderExecProgram().
	return (_syscall(_fnum_loaderLoadProgram, &command));
}

_X_ int loaderLoadLibrary(const char *libraryName)
{
	// Proto: int kernelLoaderLoadLibrary(const char *);
	// Desc : This takes the name of a library file 'libraryName' to load and creates a shared library in the kernel.  This function is not especially useful to user programs, since normal shared library loading happens automatically as part of the 'loaderLoadProgram' process.
	return (_syscall(_fnum_loaderLoadLibrary, &libraryName));
}

_X_ void *loaderGetLibrary(const char *libraryName)
{
	// Proto: kernelDynamicLibrary *kernelLoaderGetLibrary(const char *);
	// Desc : Takes the name of a library file 'libraryName' and if necessary, loads the shared library into the kernel.  Returns an (kernel-only) reference to the library.  This function is not especially useful to user programs, since normal shared library loading happens automatically as part of the 'loaderLoadProgram' process.
	return ((void *)(long) _syscall(_fnum_loaderGetLibrary, &libraryName));
}

_X_ void *loaderLinkLibrary(const char *libraryName)
{
	// Proto: kernelDynamicLibrary *kernelLoaderLinkLibrary(const char *);
	// Desc : Takes the name of a library file 'libraryName' and if necessary, loads the shared library into the kernel.  Next, the library is linked into the current process.  Returns an (kernel-only) reference to the library.  This function is not especially useful to user programs, since normal shared library loading happens automatically as part of the 'loaderLoadProgram' process.  Used by the dlopen() and friends library functions.
	return ((void *)(long) _syscall(_fnum_loaderLinkLibrary, &libraryName));
}

_X_ void *loaderGetSymbol(const char *symbolName)
{
	// Proto: void *kernelLoaderGetSymbol(const char *);
	// Desc : Takes a symbol name, and returns the address of the symbol in the current process.  This function is not especially useful to user programs, since normal shared library loading happens automatically as part of the 'loaderLoadProgram' process.  Used by the dlopen() and friends library functions.
	return ((void *)(long) _syscall(_fnum_loaderGetSymbol, &symbolName));
}

_X_ int loaderExecProgram(int processId, int block _U_)
{
	// Proto: int kernelLoaderExecProgram(int, int);
	// Desc : Execute the process referenced by process ID 'processId'.  If 'block' is non-zero, the calling process will block until process 'pid' has terminated, and the return value of the call is the return value of process 'pid'.
	return (_syscall(_fnum_loaderExecProgram, &processId));
}

_X_ int loaderLoadAndExec(const char *command, int privilege _U_, int block _U_)
{
	// Proto: int kernelLoaderLoadAndExec(const char *, int, int);
	// Desc : This function is just for convenience, and is an amalgamation of the loader functions loaderLoadProgram() and  loaderExecProgram().
	return (_syscall(_fnum_loaderLoadAndExec, &command));
}


//
// Real-time clock functions
//

_X_ int rtcReadSeconds(void)
{
	// Proto: int kernelRtcReadSeconds(void);
	// Desc : Get the current seconds value.
	return (_syscall(_fnum_rtcReadSeconds, NULL));
}

_X_ int rtcReadMinutes(void)
{
	// Proto: int kernelRtcReadMinutes(void);
	// Desc : Get the current minutes value.
	return (_syscall(_fnum_rtcReadMinutes, NULL));
}

_X_ int rtcReadHours(void)
{
	// Proto: int kernelRtcReadHours(void);
	// Desc : Get the current hours value.
	return (_syscall(_fnum_rtcReadHours, NULL));
}

_X_ int rtcDayOfWeek(unsigned day, unsigned month _U_, unsigned year _U_)
{
	// Proto: int kernelRtcDayOfWeek(unsigned, unsigned, unsigned);
	// Desc : Get the current day of the week value.
	return (_syscall(_fnum_rtcDayOfWeek, &day));
}

_X_ int rtcReadDayOfMonth(void)
{
	// Proto: int kernelRtcReadDayOfMonth(void);
	// Desc : Get the current day of the month value.
	return (_syscall(_fnum_rtcReadDayOfMonth, NULL));
}

_X_ int rtcReadMonth(void)
{
	// Proto: int kernelRtcReadMonth(void);
	// Desc : Get the current month value.
	return (_syscall(_fnum_rtcReadMonth, NULL));
}

_X_ int rtcReadYear(void)
{
	// Proto: int kernelRtcReadYear(void);
	// Desc : Get the current year value.
	return (_syscall(_fnum_rtcReadYear, NULL));
}

_X_ unsigned rtcUptimeSeconds(void)
{
	// Proto: unsigned kernelRtcUptimeSeconds(void);
	// Desc : Get the number of seconds the system has been running.
	return (_syscall(_fnum_rtcUptimeSeconds, NULL));
}

_X_ int rtcDateTime(struct tm *theTime)
{
	// Proto: int kernelRtcDateTime(struct tm *);
	// Desc : Get the current data and time as a tm data structure in 'theTime'.
	return (_syscall(_fnum_rtcDateTime, &theTime));
}


//
// Random number functions
//

_X_ unsigned randomUnformatted(void)
{
	// Proto: unsigned kernelRandomUnformatted(void);
	// Desc : Get an unformatted random unsigned number.  Just any unsigned number.
	return (_syscall(_fnum_randomUnformatted, NULL));
}

_X_ unsigned randomFormatted(unsigned start, unsigned end _U_)
{
	// Proto: unsigned kernelRandomFormatted(unsigned, unsigned);
	// Desc : Get a random unsigned number between the start value 'start' and the end value 'end', inclusive.
	return (_syscall(_fnum_randomFormatted, &start));
}

_X_ unsigned randomSeededUnformatted(unsigned seed)
{
	// Proto: unsigned kernelRandomSeededUnformatted(unsigned);
	// Desc : Get an unformatted random unsigned number, using the random seed 'seed' instead of the kernel's default random seed.
	return (_syscall(_fnum_randomSeededUnformatted, &seed));
}

_X_ unsigned randomSeededFormatted(unsigned seed, unsigned start _U_, unsigned end _U_)
{
	// Proto: unsigned kernelRandomSeededFormatted(unsigned, unsigned, unsigned);
	// Desc : Get a random unsigned number between the start value 'start' and the end value 'end', inclusive, using the random seed 'seed' instead of the kernel's default random seed.
	return (_syscall(_fnum_randomSeededFormatted, &seed));
}

_X_ void randomBytes(unsigned char *buffer, unsigned size _U_)
{
	// Proto: void kernelRandomBytes(unsigned char *, unsigned);
	// Desc : Given the supplied buffer and size, fill the buffer with random bytes.
	_syscall(_fnum_randomBytes, &buffer);
}


//
// Variable list functions
//

_X_ int variableListCreate(variableList *list)
{
	// Proto: int kernelVariableListCreate(variableList *);
	// Desc : Set up a new variable list structure.
	return (_syscall(_fnum_variableListCreate, &list));
}

_X_ int variableListDestroy(variableList *list)
{
	// Proto: int kernelVariableListDestroy(variableList *);
	// Desc : Deallocate a variable list structure previously allocated by a call to variableListCreate() or configurationReader()
	return (_syscall(_fnum_variableListDestroy, &list));
}

_X_ const char *variableListGetVariable(variableList *list, int num _U_)
{
	// Proto: const char *kernelVariableListGetVariable(variableList *, int);
	// Desc : Return a pointer to the name of the 'num'th variable from the variable list 'list'.
	return ((const char *)(long) _syscall(_fnum_variableListGetVariable, &list));
}

_X_ const char *variableListGet(variableList *list, const char *var _U_)
{
	// Proto: const char *kernelVariableListGet(variableList *, const char *);
	// Desc : Return a pointer to the value of the variable 'var' from the variable list 'list'.
	return ((const char *)(long) _syscall(_fnum_variableListGet, &list));
}

_X_ int variableListSet(variableList *list, const char *var _U_, const char *value _U_)
{
	// Proto: int kernelVariableListSet(variableList *, const char *, const char *);
	// Desc : Set the value of the variable 'var' to the value 'value'.
	return (_syscall(_fnum_variableListSet, &list));
}

_X_ int variableListUnset(variableList *list, const char *var _U_)
{
	// Proto: int kernelVariableListUnset(variableList *, const char *);
	// Desc : Remove the variable 'var' from the variable list 'list'.
	return (_syscall(_fnum_variableListUnset, &list));
}


//
// Environment functions
//

_X_ int environmentGet(const char *var, char *buf _U_, unsigned bufsz _U_)
{
	// Proto: int kernelEnvironmentGet(const char *, char *, unsigned);
	// Desc : Get the value of the environment variable named 'var', and put it into the buffer 'buf' of size 'bufsz' if successful.
	return (_syscall(_fnum_environmentGet, &var));
}

_X_ int environmentSet(const char *var, const char *val _U_)
{
	// Proto: int kernelEnvironmentSet(const char *, const char *);
	// Desc : Set the environment variable 'var' to the value 'val', overwriting any old value that might have been previously set.
	return (_syscall(_fnum_environmentSet, &var));
}

_X_ int environmentUnset(const char *var)
{
	// Proto: int kernelEnvironmentUnset(const char *);
	// Desc : Delete the environment variable 'var'.
	return (_syscall(_fnum_environmentUnset, &var));
}

_X_ void environmentDump(void)
{
	// Proto: void kernelEnvironmentDump(void);
	// Desc : Print a listing of all the currently set environment variables in the calling process' environment space to the current text output stream.
	_syscall(_fnum_environmentDump, NULL);
}


//
// Raw graphics functions
//

_X_ int graphicsAreEnabled(void)
{
	// Proto: int kernelGraphicsAreEnabled(void);
	// Desc : Returns 1 if the kernel is operating in graphics mode.
	return (_syscall(_fnum_graphicsAreEnabled, NULL));
}

_X_ int graphicGetModes(videoMode *buffer, unsigned size _U_)
{
	// Proto: int kernelGraphicGetModes(videoMode *, unsigned);
	// Desc : Get up to 'size' bytes worth of videoMode structures in 'buffer' for the supported video modes of the current hardware.
	return (_syscall(_fnum_graphicGetModes, &buffer));
}

_X_ int graphicGetMode(videoMode *mode)
{
	// Proto: int kernelGraphicGetMode(videoMode *);
	// Desc : Get the current video mode in 'mode'
	return (_syscall(_fnum_graphicGetMode, &mode));
}

_X_ int graphicSetMode(videoMode *mode)
{
	// Proto: int kernelGraphicSetMode(videoMode *);
	// Desc : Set the video mode in 'mode'.  Generally this will require a reboot in order to take effect.
	return (_syscall(_fnum_graphicSetMode, &mode));
}

_X_ int graphicGetScreenWidth(void)
{
	// Proto: int kernelGraphicGetScreenWidth(void);
	// Desc : Returns the width of the graphics screen.
	return (_syscall(_fnum_graphicGetScreenWidth, NULL));
}

_X_ int graphicGetScreenHeight(void)
{
	// Proto: int kernelGraphicGetScreenHeight(void);
	// Desc : Returns the height of the graphics screen.
	return (_syscall(_fnum_graphicGetScreenHeight, NULL));
}

_X_ int graphicCalculateAreaBytes(int width, int height _U_)
{
	// Proto: int kernelGraphicCalculateAreaBytes(int, int);
	// Desc : Returns the number of bytes required to allocate a graphic buffer of width 'width' and height 'height'.  This is a function of the screen resolution, etc.
	return (_syscall(_fnum_graphicCalculateAreaBytes, &width));
}

_X_ int graphicClearScreen(color *background)
{
	// Proto: int kernelGraphicClearScreen(color *);
	// Desc : Clear the screen to the background color 'background'.
	return (_syscall(_fnum_graphicClearScreen, &background));
}

_X_ int graphicDrawPixel(graphicBuffer *buffer, color *foreground _U_, drawMode mode _U_, int xCoord _U_, int yCoord _U_)
{
	// Proto: int kernelGraphicDrawPixel(graphicBuffer *, color *, drawMode, int, int);
	// Desc : Draw a single pixel into the graphic buffer 'buffer', using the color 'foreground', the drawing mode 'drawMode' (for example, 'draw_normal' or 'draw_xor'), the X coordinate 'xCoord' and the Y coordinate 'yCoord'.  If 'buffer' is NULL, draw directly onto the screen.
	return (_syscall(_fnum_graphicDrawPixel, &buffer));
}

_X_ int graphicDrawLine(graphicBuffer *buffer, color *foreground _U_, drawMode mode _U_, int startX _U_, int startY _U_, int endX _U_, int endY _U_)
{
	// Proto: int kernelGraphicDrawLine(graphicBuffer *, color *, drawMode, int, int, int, int);
	// Desc : Draw a line into the graphic buffer 'buffer', using the color 'foreground', the drawing mode 'drawMode' (for example, 'draw_normal' or 'draw_xor'), the starting X coordinate 'startX', the starting Y coordinate 'startY', the ending X coordinate 'endX' and the ending Y coordinate 'endY'.  At the time of writing, only horizontal and vertical lines are supported by the linear framebuffer graphic driver.  If 'buffer' is NULL, draw directly onto the screen.
	return (_syscall(_fnum_graphicDrawLine, &buffer));
}

_X_ int graphicDrawRect(graphicBuffer *buffer, color *foreground _U_, drawMode mode _U_, int xCoord _U_, int yCoord _U_, int width _U_, int height _U_, int thickness _U_, int fill _U_)
{
	// Proto: int kernelGraphicDrawRect(graphicBuffer *, color *, drawMode, int, int, int, int, int, int);
	// Desc : Draw a rectangle into the graphic buffer 'buffer', using the color 'foreground', the drawing mode 'drawMode' (for example, 'draw_normal' or 'draw_xor'), the starting X coordinate 'xCoord', the starting Y coordinate 'yCoord', the width 'width', the height 'height', the line thickness 'thickness' and the fill value 'fill'.  Non-zero fill value means fill the rectangle.   If 'buffer' is NULL, draw directly onto the screen.
	return (_syscall(_fnum_graphicDrawRect, &buffer));
}

_X_ int graphicDrawOval(graphicBuffer *buffer, color *foreground _U_, drawMode mode _U_, int xCoord _U_, int yCoord _U_, int width _U_, int height _U_, int thickness _U_, int fill _U_)
{
	// Proto: int kernelGraphicDrawOval(graphicBuffer *, color *, drawMode, int, int, int, int, int, int);
	// Desc : Draw an oval (circle, whatever) into the graphic buffer 'buffer', using the color 'foreground', the drawing mode 'drawMode' (for example, 'draw_normal' or 'draw_xor'), the starting X coordinate 'xCoord', the starting Y coordinate 'yCoord', the width 'width', the height 'height', the line thickness 'thickness' and the fill value 'fill'.  Non-zero fill value means fill the oval.   If 'buffer' is NULL, draw directly onto the screen.  Currently not supported by the linear framebuffer graphic driver.
	return (_syscall(_fnum_graphicDrawOval, &buffer));
}

_X_ int graphicGetImage(graphicBuffer *buffer, image *getImage _U_, int xCoord _U_, int yCoord _U_, int width _U_, int height _U_)
{
	// Proto: int kernelGraphicGetImage(graphicBuffer *, image *, int, int, int, int);
	// Desc : Grab a new image 'getImage' from the graphic buffer 'buffer', using the starting X coordinate 'xCoord', the starting Y coordinate 'yCoord', the width 'width' and the height 'height'.   If 'buffer' is NULL, grab the image directly from the screen.
	return (_syscall(_fnum_graphicGetImage, &buffer));
}

_X_ int graphicDrawImage(graphicBuffer *buffer, image *drawImage _U_, drawMode mode _U_, int xCoord _U_, int yCoord _U_, int xOffset _U_, int yOffset _U_, int width _U_, int height _U_)
{
	// Proto: int kernelGraphicDrawImage(graphicBuffer *, image *, drawMode, int, int, int, int, int, int);
	// Desc : Draw the image 'drawImage' into the graphic buffer 'buffer', using the drawing mode 'mode' (for example, 'draw_normal' or 'draw_xor'), the starting X coordinate 'xCoord' and the starting Y coordinate 'yCoord'.   The 'xOffset' and 'yOffset' parameters specify an offset into the image to start the drawing (0, 0 to draw the whole image).  Similarly the 'width' and 'height' parameters allow you to specify a portion of the image (0, 0 to draw the whole image -- minus any X or Y offsets from the previous parameters).  So, for example, to draw only the middle pixel of a 3x3 image, you would specify xOffset=1, yOffset=1, width=1, height=1.  If 'buffer' is NULL, draw directly onto the screen.
	return (_syscall(_fnum_graphicDrawImage, &buffer));
}

_X_ int graphicDrawText(graphicBuffer *buffer, color *foreground _U_, color *background _U_, objectKey font _U_, const char *charSet _U_, const char *text _U_, drawMode mode _U_, int xCoord _U_, int yCoord _U_)
{
	// Proto: int kernelGraphicDrawText(graphicBuffer *, color *, color *, kernelFont *, const char *, const char *, drawMode, int, int);
	// Desc : Draw the text string 'text' into the graphic buffer 'buffer', using the colors 'foreground' and 'background', the font 'font', the character set 'charSet', the drawing mode 'drawMode' (for example, 'draw_normal' or 'draw_xor'), the starting X coordinate 'xCoord', the starting Y coordinate 'yCoord'.   If 'buffer' is NULL, draw directly onto the screen.  If 'font' is NULL, use the default font.  If 'charSet' is NULL, use the default character set.
	return (_syscall(_fnum_graphicDrawText, &buffer));
}

_X_ int graphicCopyArea(graphicBuffer *buffer, int xCoord1 _U_, int yCoord1 _U_, int width _U_, int height _U_, int xCoord2 _U_, int yCoord2 _U_)
{
	// Proto: int kernelGraphicCopyArea(graphicBuffer *, int, int, int, int, int, int);
	// Desc : Within the graphic buffer 'buffer', copy the area bounded by ('xCoord1', 'yCoord1'), width 'width' and height 'height' to the starting X coordinate 'xCoord2' and the starting Y coordinate 'yCoord2'.  If 'buffer' is NULL, copy directly to and from the screen.
	return (_syscall(_fnum_graphicCopyArea, &buffer));
}

_X_ int graphicClearArea(graphicBuffer *buffer, color *background _U_, int xCoord _U_, int yCoord _U_, int width _U_, int height _U_)
{
	// Proto: int kernelGraphicClearArea(graphicBuffer *, color *, int, int, int, int);
	// Desc : Clear the area of the graphic buffer 'buffer' using the background color 'background', using the starting X coordinate 'xCoord', the starting Y coordinate 'yCoord', the width 'width' and the height 'height'.  If 'buffer' is NULL, clear the area directly on the screen.
	return (_syscall(_fnum_graphicClearArea, &buffer));
}

_X_ int graphicRenderBuffer(graphicBuffer *buffer, int drawX _U_, int drawY _U_, int clipX _U_, int clipY _U_, int clipWidth _U_, int clipHeight _U_)
{
	// Proto: int kernelGraphicRenderBuffer(graphicBuffer *, int, int, int, int, int, int);
	// Desc : Draw the clip of the buffer 'buffer' onto the screen.  Draw it on the screen at starting X coordinate 'drawX' and starting Y coordinate 'drawY'.  The buffer clip is bounded by the starting X coordinate 'clipX', the starting Y coordinate 'clipY', the width 'clipWidth' and the height 'clipHeight'.  It is not legal for 'buffer' to be NULL in this case.
	return (_syscall(_fnum_graphicRenderBuffer, &buffer));
}


//
// Image functions
//

_X_ int imageNew(image *blankImage, unsigned width _U_, unsigned height _U_)
{
	// Proto: int kernelImageNew(image *, unsigned, unsigned);
	// Desc : Using the (possibly uninitialized) image data structure 'blankImage', allocate memory for a new image with the specified 'width' and 'height'.
	return (_syscall(_fnum_imageNew, &blankImage));
}

_X_ int imageFree(image *freeImage)
{
	// Proto: int kernelImageFree(image *);
	// Desc : Frees memory allocated for image data (but does not deallocate the image structure itself).
	return (_syscall(_fnum_imageFree, &freeImage));
}

_X_ int imageLoad(const char *filename, unsigned width _U_, unsigned height _U_, image *loadImage _U_)
{
	// Proto: int imageLoad(const char *, unsigned, unsigned, image *);
	// Desc : Try to load the image file 'filename' (with the specified 'width' and 'height' if possible -- zeros indicate no preference), and if successful, save the data in the image data structure 'loadImage'.
		return (_syscall(_fnum_imageLoad, &filename));
}

_X_ int imageSave(const char *filename, int format _U_, image *saveImage _U_)
{
	// Proto: int imageSave(const char *, int, image *);
	// Desc : Save the image data structure 'saveImage' using the image format 'format' to the file 'fileName'.  Image format codes are found in the file <sys/image.h>
	return (_syscall(_fnum_imageSave, &filename));
}

_X_ int imageResize(image *resizeImage, unsigned width _U_, unsigned height _U_)
{
	// Proto: int imageResize(image *, unsigned, unsigned);
	// Desc : Resize the image represented in the image data structure 'resizeImage' to the new 'width' and 'height' values.
	return (_syscall(_fnum_imageResize, &resizeImage));
}

_X_ int imageCopy(image *srcImage, image *destImage _U_)
{
	// Proto: int kernelImageCopy(image *, image *);
	// Desc : Make a copy of the image 'srcImage' to 'destImage', including all of its data, alpha channel information (if applicable), etc.
	return (_syscall(_fnum_imageCopy, &srcImage));
}

_X_ int imageFill(image *fillImage, color *fillColor _U_)
{
	// Proto: int kernelImageFill(image *, color *);
	// Desc : Fill the image 'fillImage' with the color 'fillColor'
	return (_syscall(_fnum_imageFill, &fillImage));
}

_X_ int imagePaste(image *srcImage, image *destImage _U_, int xCoord _U_, int yCoord _U_)
{
	// Proto: int kernelImagePaste(image *, image *, int, int);
	// Desc : Paste the image 'srcImage' into the image 'destImage' at the requested coordinates.
	return (_syscall(_fnum_imagePaste, &srcImage));
}


//
// Font functions
//

_X_ objectKey fontGet(const char *family, unsigned flags _U_, int points _U_, const char *charSet _U_)
{
	// Proto: kernelFont *kernelFontGet(const char *, unsigned, int, const char *);
	// Desc : Load the font with the desired family, flags, points, and optional character set.  Returns an object key for the font in 'pointer' if successful.  See <sys/font.h> for flags.  The fixed width flag should be non-zero if you want each character of the font to have uniform width (i.e. an 'i' character will be padded with empty space so that it takes up the same width as, for example, a 'W' character).
	return ((objectKey)(long) _syscall(_fnum_fontGet, &family));
}

_X_ int fontGetPrintedWidth(objectKey font, const char *charSet _U_, const char *string _U_)
{
	// Proto: int kernelFontGetPrintedWidth(kernelFont *, const char *, const char *);
	// Desc : Given the supplied string and character set (may be NULL), return the screen width that the text will consume given the font 'font'.  Useful for placing text when using a variable-width font, but not very useful otherwise.
	return (_syscall(_fnum_fontGetPrintedWidth, &font));
}

_X_ int fontGetWidth(objectKey font)
{
	// Proto: int kernelFontGetWidth(kernelFont *);
	// Desc : Returns the character width of the supplied font.  Only useful when the font is fixed-width.
	return (_syscall(_fnum_fontGetWidth, &font));
}

_X_ int fontGetHeight(objectKey font)
{
	// Proto: int kernelFontGetHeight(kernelFont *);
	// Desc : Returns the character height of the supplied font.
	return (_syscall(_fnum_fontGetHeight, &font));
}


//
// Windowing system functions
//

_X_ int windowLogin(const char *userName)
{
	// Proto: kernelWindowLogin(const char *, const char *);
	// Desc : Log the user into the window environment with 'userName'.  The return value is the PID of the window shell for this session.  The calling program must have supervisor privilege in order to use this function.
	return (_syscall(_fnum_windowLogin, &userName));
}

_X_ int windowLogout(void)
{
	// Proto: kernelWindowLogout(void);
	// Desc : Log the current user out of the windowing system.  This kills the window shell process returned by windowLogin() call.
	return (_syscall(_fnum_windowLogout, NULL));
}

_X_ objectKey windowNew(int processId, const char *title _U_)
{
	// Proto: kernelWindow *kernelWindowNew(int, const char *);
	// Desc : Create a new window, owned by the process 'processId', and with the title 'title'.  Returns an object key to reference the window, needed by most other window functions below (such as adding components to the window)
	return ((objectKey)(long) _syscall(_fnum_windowNew, &processId));
}

_X_ objectKey windowNewDialog(objectKey parent, const char *title _U_)
{
	// Proto: kernelWindow *kernelWindowNewDialog(kernelWindow *, const char *);
	// Desc : Create a dialog window to associate with the parent window 'parent', using the supplied title.  In the current implementation, dialog windows are modal, which is the main characteristic distinguishing them from regular windows.
	return ((objectKey)(long) _syscall(_fnum_windowNewDialog, &parent));
}

_X_ int windowDestroy(objectKey window)
{
	// Proto: int kernelWindowDestroy(kernelWindow *);
	// Desc : Destroy the window referenced by the object key 'window'
	return (_syscall(_fnum_windowDestroy, &window));
}

_X_ int windowUpdateBuffer(void *buffer, int xCoord _U_, int yCoord _U_, int width _U_, int height _U_)
{
	// Proto: kernelWindowUpdateBuffer(graphicBuffer *, int, int, int, int);
	// Desc : Tells the windowing system to redraw the visible portions of the graphic buffer 'buffer', using the given clip coordinates/size.
	return (_syscall(_fnum_windowUpdateBuffer, &buffer));
}

_X_ int windowSetCharSet(objectKey window, const char *charSet _U_)
{
	// Proto: int kernelWindowSetCharSet(kernelWindow *, const char *);
	// Desc : Set the character set of window 'window' to be 'charSet'.
	return (_syscall(_fnum_windowSetCharSet, &window));
}

_X_ int windowSetTitle(objectKey window, const char *title _U_)
{
	// Proto: int kernelWindowSetTitle(kernelWindow *, const char *);
	// Desc : Set the new title of window 'window' to be 'title'.
	return (_syscall(_fnum_windowSetTitle, &window));
}

_X_ int windowGetSize(objectKey window, int *width _U_, int *height _U_)
{
	// Proto: int kernelWindowGetSize(kernelWindow *, int *, int *);
	// Desc : Get the size of the window 'window', and put the results in 'width' and 'height'.
	return (_syscall(_fnum_windowGetSize, &window));
}

_X_ int windowSetSize(objectKey window, int width _U_, int height _U_)
{
	// Proto: int kernelWindowSetSize(kernelWindow *, int, int);
	// Desc : Resize the window 'window' to the width 'width' and the height 'height'.
	return (_syscall(_fnum_windowSetSize, &window));
}

_X_ int windowGetLocation(objectKey window, int *xCoord _U_, int *yCoord _U_)
{
	// Proto: int kernelWindowGetLocation(kernelWindow *, int *, int *);
	// Desc : Get the current screen location of the window 'window' and put it into the coordinate variables 'xCoord' and 'yCoord'.
	return (_syscall(_fnum_windowGetLocation, &window));
}

_X_ int windowSetLocation(objectKey window, int xCoord _U_, int yCoord _U_)
{
	// Proto: int kernelWindowSetLocation(kernelWindow *, int, int);
	// Desc : Set the screen location of the window 'window' using the coordinate variables 'xCoord' and 'yCoord'.
	return (_syscall(_fnum_windowSetLocation, &window));
}

_X_ int windowCenter(objectKey window)
{
	// Proto: int kernelWindowCenter(kernelWindow *);
	// Desc : Center 'window' on the screen.
	return (_syscall(_fnum_windowCenter, &window));
}

_X_ int windowSnapIcons(objectKey parent)
{
	// Proto: void kernelWindowSnapIcons(void *);
	// Desc : If 'parent' (either a window or a windowContainer) has icon components inside it, this will snap them to a grid.
	return (_syscall(_fnum_windowSnapIcons, &parent));
}

_X_ int windowSetHasBorder(objectKey window, int trueFalse _U_)
{
	// Proto: int kernelWindowSetHasBorder(kernelWindow *, int);
	// Desc : Tells the windowing system whether to draw a border around the window 'window'.  'trueFalse' being non-zero means draw a border.  Windows have borders by default.
	return (_syscall(_fnum_windowSetHasBorder, &window));
}

_X_ int windowSetHasTitleBar(objectKey window, int trueFalse _U_)
{
	// Proto: int kernelWindowSetHasTitleBar(kernelWindow *, int);
	// Desc : Tells the windowing system whether to draw a title bar on the window 'window'.  'trueFalse' being non-zero means draw a title bar.  Windows have title bars by default.
	return (_syscall(_fnum_windowSetHasTitleBar, &window));
}

_X_ int windowSetMovable(objectKey window, int trueFalse _U_)
{
	// Proto: int kernelWindowSetMovable(kernelWindow *, int);
	// Desc : Tells the windowing system whether the window 'window' should be movable by the user (i.e. clicking and dragging it).  'trueFalse' being non-zero means the window is movable.  Windows are movable by default.
	return (_syscall(_fnum_windowSetMovable, &window));
}

_X_ int windowSetResizable(objectKey window, int trueFalse _U_)
{
	// Proto: int kernelWindowSetResizable(kernelWindow *, int);
	// Desc : Tells the windowing system whether to allow 'window' to be resized by the user.  'trueFalse' being non-zero means the window is resizable.  Windows are resizable by default.
	return (_syscall(_fnum_windowSetResizable, &window));
}

_X_ int windowSetFocusable(objectKey window, int trueFalse _U_)
{
	// Proto: int kernelWindowSetFocusable(kernelWindow *, int);
	// Desc : Tells the windowing system whether to allow 'window' to be focused.  'trueFalse' being non-zero means the window can focus.  Windows can focus by default.
	return (_syscall(_fnum_windowSetFocusable, &window));
}

_X_ int windowRemoveMinimizeButton(objectKey window)
{
	// Proto: int kernelWindowRemoveMinimizeButton(kernelWindow *);
	// Desc : Tells the windowing system not to draw a minimize button on the title bar of the window 'window'.  Windows have minimize buttons by default, as long as they have a title bar.  If there is no title bar, then this function has no effect.
	return (_syscall(_fnum_windowRemoveMinimizeButton, &window));
}

_X_ int windowRemoveCloseButton(objectKey window)
{
	// Proto: int kernelWindowRemoveCloseButton(kernelWindow *);
	// Desc : Tells the windowing system not to draw a close button on the title bar of the window 'window'.  Windows have close buttons by default, as long as they have a title bar.  If there is no title bar, then this function has no effect.
	return (_syscall(_fnum_windowRemoveCloseButton, &window));
}

_X_ int windowSetVisible(objectKey window, int visible _U_)
{
	// Proto: int kernelWindowSetVisible(kernelWindow *, int);
	// Desc : Tell the windowing system whether to make 'window' visible or not.  Non-zero 'visible' means make the window visible.  When windows are created, they are not visible by default so you can add components, do layout, set the size, etc.
	return (_syscall(_fnum_windowSetVisible, &window));
}

_X_ void windowSetMinimized(objectKey window, int minimized _U_)
{
	// Proto: void kernelWindowSetMinimized(kernelWindow *, int);
	// Desc : Tell the windowing system whether to make 'window' minimized or not.  Non-zero 'minimized' means make the window non-visible, but accessible via the task bar.  Zero 'minimized' means restore a minimized window to its normal, visible size.
	_syscall(_fnum_windowSetMinimized, &window);
}

_X_ int windowAddConsoleTextArea(objectKey window)
{
	// Proto: int kernelWindowAddConsoleTextArea(kernelWindow *);
	// Desc : Add a console text area component to 'window'.  The console text area is where most kernel logging and error messages are sent, particularly at boot time.  Note that there is only one instance of the console text area, and thus it can only exist in one window at a time.  Destroying the window is one way to free the console area to be used in another window.
	return (_syscall(_fnum_windowAddConsoleTextArea, &window));
}

_X_ void windowRedrawArea(int xCoord, int yCoord _U_, int width _U_, int height _U_)
{
	// Proto: void kernelWindowRedrawArea(int, int, int, int);
	// Desc : Tells the windowing system to redraw whatever is supposed to be in the screen area bounded by the supplied coordinates.  This might be useful if you were drawing non-window-based things (i.e., things without their own independent graphics buffer) directly onto the screen and you wanted to restore an area to its original contents.  For example, the mouse driver uses this method to erase the pointer from its previous position.
	_syscall(_fnum_windowRedrawArea, &xCoord);
}

_X_ void windowDrawAll(void)
{
	// Proto: void kernelWindowDrawAll(void);
	// Desc : Tells the windowing system to (re)draw all the windows.
	_syscall(_fnum_windowDrawAll, NULL);
}

_X_ int windowGetColor(const char *colorName, color *getColor _U_)
{
	// Proto: int kernelWindowGetColor(const char *, color *);
	// Desc : Get the window system color 'colorName' and place its values in the color structure 'getColor'.  Examples of system color names include 'foreground', 'background', and 'desktop'.
	return (_syscall(_fnum_windowGetColor, &colorName));
}

_X_ int windowSetColor(const char *colorName, color *setColor _U_)
{
	// Proto: int kernelWindowSetColor(const char *, color *);
	// Desc : Set the window system color 'colorName' to the values in the color structure 'getColor'.  Examples of system color names include 'foreground', 'background', and 'desktop'.
	return (_syscall(_fnum_windowSetColor, &colorName));
}

_X_ void windowResetColors(void)
{
	// Proto: void kernelWindowResetColors(void);
	// Desc : Tells the windowing system to reset the colors of all the windows and their components, and then re-draw all the windows.  Useful for example when the user has changed the color scheme.
	_syscall(_fnum_windowResetColors, NULL);
}

_X_ void windowProcessEvent(objectKey event)
{
	// Proto: void kernelWindowProcessEvent(windowEvent *);
	// Desc : Creates a window event using the supplied event structure.  This function is most often used within the kernel, particularly in the mouse and keyboard functions, to signify clicks or key presses.  It can, however, be used by external programs to create 'artificial' events.  The windowEvent structure specifies the target component and event type.
	_syscall(_fnum_windowProcessEvent, &event);
}

_X_ int windowComponentEventGet(objectKey key, windowEvent *event _U_)
{
	// Proto: int kernelWindowComponentEventGet(objectKey, windowEvent *);
	// Desc : Gets a pending window event, if any, applicable to component 'key', and puts the data into the windowEvent structure 'event'.  If an event was received, the function returns a positive, non-zero value (the actual value reflects the amount of raw data read from the component's event stream -- not particularly useful to an application).  If the return value is zero, no event was pending.
	return (_syscall(_fnum_windowComponentEventGet, &key));
}

_X_ int windowSetBackgroundColor(objectKey window, color *background _U_)
{
	// Proto: int kernelWindowSetBackgroundColor(kernelWindow *, color *);
	// Desc : Set the background color of 'window'.  If 'color' is NULL, use the default.
	return (_syscall(_fnum_windowSetBackgroundColor, &window));
}

_X_ int windowShellTileBackground(const char *theFile)
{
	// Proto: int kernelWindowShellTileBackground(const char *);
	// Desc : Load the image file specified by the pathname 'theFile', and if successful, tile the image on the background root window.
	return (_syscall(_fnum_windowShellTileBackground, &theFile));
}

_X_ int windowShellCenterBackground(const char *theFile)
{
	// Proto: int kernelWindowShellCenterBackground(const char *);
	// Desc : Load the image file specified by the pathname 'theFile', and if successful, center the image on the background root window.
	return (_syscall(_fnum_windowShellCenterBackground, &theFile));
}

_X_ objectKey windowShellNewTaskbarIcon(image *img)
{
	// Proto: kernelWindowComponent *kernelWindowShellNewTaskbarIcon(image *);
	// Desc : Create an icon in the window shell's taskbar menu, using the supplied image 'img'
	return ((objectKey)(long) _syscall(_fnum_windowShellNewTaskbarIcon, &img));
}

_X_ objectKey windowShellNewTaskbarTextLabel(const char *text)
{
	// Proto: kernelWindowComponent *kernelWindowShellNewTaskbarTextLabel(const char *);
	// Desc : Create a text label in the window shell's taskbar menu, using the supplied text 'text'
	return ((objectKey)(long) _syscall(_fnum_windowShellNewTaskbarTextLabel,
		&text));
}

_X_ void windowShellDestroyTaskbarComp(objectKey component)
{
	// Proto: void kernelWindowShellDestroyTaskbarComp(kernelWindowComponent *);
	// Desc : Destroy a component in the window shell's taskbar menu
	_syscall(_fnum_windowShellDestroyTaskbarComp, &component);
}

_X_ objectKey windowShellIconify(objectKey window, int iconify _U_, image *img _U_)
{
	// Proto: kernelWindowComponent *kernelWindowShellIconify(kernelWindow *, int, image *);
	// Desc : Iconify or restore 'window' in the window shell's taskbar menu, depending on the parameter 'iconify'.  If an image 'img' is supplied, then that will be used to create the icon component, which is returned.
	return ((objectKey)(long) _syscall(_fnum_windowShellIconify, &window));
}

_X_ int windowScreenShot(image *saveImage)
{
	// Proto: int kernelWindowScreenShot(image *);
	// Desc : Get an image representation of the entire screen in the image data structure 'saveImage'.  Note that it is not necessary to allocate memory for the data pointer of the image structure beforehand, as this is done automatically.  You should, however, deallocate the data field of the image structure when you are finished with it.
	return (_syscall(_fnum_windowScreenShot, &saveImage));
}

_X_ int windowSaveScreenShot(const char *filename)
{
	// Proto: int kernelWindowSaveScreenShot(const char *);
	// Desc : Save a screenshot of the entire screen to the file specified by the pathname 'filename'.
	return (_syscall(_fnum_windowSaveScreenShot, &filename));
}

_X_ int windowSetTextOutput(objectKey key)
{
	// Proto: int kernelWindowSetTextOutput(kernelWindowComponent *);
	// Desc : Set the text output (and input) of the calling process to the object key of some window component, such as a TextArea or TextField component.  You'll need to use this if you want to output text to one of these components or receive input from one.
	return (_syscall(_fnum_windowSetTextOutput, &key));
}

_X_ int windowLayout(objectKey window)
{
	// Proto: int kernelWindowLayout(kernelWindow *);
	// Desc : Layout, or re-layout, the requested window 'window'.  This function can be used when components are added to or removed from and already laid-out window.
	return (_syscall(_fnum_windowLayout, &window));
}

_X_ void windowDebugLayout(objectKey window)
{
	// Proto: void kernelWindowDebugLayout(kernelWindow *);
	// Desc : This function draws grid boxes around all the grid cells containing components (or parts thereof).
	_syscall(_fnum_windowDebugLayout, &window);
}

_X_ int windowContextAdd(objectKey parent, windowMenuContents *contents _U_)
{
	// Proto: int kernelWindowContextAdd(objectKey, windowMenuContents *);
	// Desc : This function allows the caller to add context menu items in the 'content' structure to the supplied parent object 'parent' (can be a window or a component).  The function supplies the pointers to the new menu items in the caller's structure, which can then be manipulated to some extent (enable/disable, destroy, etc) using regular component functions.
	return (_syscall(_fnum_windowContextAdd, &parent));
}

_X_ int windowContextSet(objectKey parent, objectKey menu _U_)
{
	// Proto: int kernelWindowContextSet(objectKey, kernelWindowComponent *);
	// Desc : This function allows the caller to set the context menu of the the supplied parent object 'parent' (can be a window or a component).
	return (_syscall(_fnum_windowContextSet, &parent));
}

_X_ int windowSwitchPointer(objectKey parent, const char *pointerName _U_)
{
	// Proto: int kernelWinowSwitchPointer(objectKey, const char *);
	// Desc : Switch the mouse pointer for the parent window or component object 'parent' to the pointer represented by the name 'pointerName'.  Pointer names are defined in the header file <sys/mouse.h>.  Examples are "default" and "busy".
	return (_syscall(_fnum_windowSwitchPointer, &parent));
}

_X_ int windowRefresh(void)
{
	// Proto: int kernelWindowRefresh(void);
	// Desk : Tell the window system to do a global refresh, sending 'refresh' events to all the windows.
	return (_syscall(_fnum_windowRefresh, NULL));
}

_X_ void windowComponentDestroy(objectKey component)
{
	// Proto: void kernelWindowComponentDestroy(kernelWindowComponent *);
	// Desc : Deallocate and destroy a window component.
	_syscall(_fnum_windowComponentDestroy, &component);
}

_X_ int windowComponentSetCharSet(objectKey component, const char *charSet _U_)
{
	// Proto: int kernelWindowComponentSetCharSet(kernelWindowComponent *, const char *);
	// Desc : Set the character set as 'charSet' for 'component'.
	return (_syscall(_fnum_windowComponentSetCharSet, &component));
}

_X_ int windowComponentSetVisible(objectKey component, int visible _U_)
{
	// Proto: int kernelWindowComponentSetVisible(kernelWindowComponent *, int);
	// Desc : Set 'component' visible or non-visible.
	return (_syscall(_fnum_windowComponentSetVisible, &component));
}

_X_ int windowComponentSetEnabled(objectKey component, int enabled _U_)
{
	// Proto: int kernelWindowComponentSetEnabled(kernelWindowComponent *, int);
	// Desc : Set 'component' enabled or non-enabled; non-enabled components appear greyed-out.
	return (_syscall(_fnum_windowComponentSetEnabled, &component));
}

_X_ int windowComponentGetWidth(objectKey component)
{
	// Proto: int kernelWindowComponentGetWidth(kernelWindowComponent *);
	// Desc : Get the pixel width of the window component 'component'.
	return (_syscall(_fnum_windowComponentGetWidth, &component));
}

_X_ int windowComponentSetWidth(objectKey component, int width _U_)
{
	// Proto: int kernelWindowComponentSetWidth(kernelWindowComponent *, int);
	// Desc : Set the pixel width of the window component 'component'
	return (_syscall(_fnum_windowComponentSetWidth, &component));
}

_X_ int windowComponentGetHeight(objectKey component)
{
	// Proto: int kernelWindowComponentGetHeight(kernelWindowComponent *);
	// Desc : Get the pixel height of the window component 'component'.
	return (_syscall(_fnum_windowComponentGetHeight, &component));
}

_X_ int windowComponentSetHeight(objectKey component, int height _U_)
{
	// Proto: int kernelWindowComponentSetHeight(kernelWindowComponent *, int);
	// Desc : Set the pixel height of the window component 'component'.
	return (_syscall(_fnum_windowComponentSetHeight, &component));
}

_X_ int windowComponentFocus(objectKey component)
{
	// Proto: int kernelWindowComponentFocus(kernelWindowComponent *);
	// Desc : Give window component 'component' the focus of its window.
	return (_syscall(_fnum_windowComponentFocus, &component));
}

_X_ int windowComponentUnfocus(objectKey component)
{
	// Proto: int kernelWindowComponentUnfocus(kernelWindowComponent *component);
	// Desc : Removes the focus from window component 'component' in its window.
	return (_syscall(_fnum_windowComponentUnfocus, &component));
}

_X_ int windowComponentDraw(objectKey component)
{
	// Proto: int kernelWindowComponentDraw(kernelWindowComponent *)
	// Desc : Calls the window component 'component' to redraw itself.
	return (_syscall(_fnum_windowComponentDraw, &component));
}

_X_ int windowComponentGetData(objectKey component, void *buffer _U_, int size _U_)
{
	// Proto: int kernelWindowComponentGetData(kernelWindowComponent *, void *, int);
	// Desc : This is a generic call to get data from the window component 'component', up to 'size' bytes, in the buffer 'buffer'.  The size and type of data that a given component will return is totally dependent upon the type and implementation of the component.
	return (_syscall(_fnum_windowComponentGetData, &component));
}

_X_ int windowComponentSetData(objectKey component, void *buffer _U_, int size _U_, int render _U_)
{
	// Proto: int kernelWindowComponentSetData(kernelWindowComponent *, void *, int, int);
	// Desc : This is a generic call to set data in the window component 'component', up to 'size' bytes, in the buffer 'buffer'.  Non-zero 'render' flag causes the component to be redrawn on the screen.  The size and type of data that a given component will use or accept is totally dependent upon the type and implementation of the component.
	return (_syscall(_fnum_windowComponentSetData, &component));
}

_X_ int windowComponentGetSelected(objectKey component, int *selection _U_)
{
	// Proto: int kernelWindowComponentGetSelected(kernelWindowComponent *);
	// Desc : This is a call to get the 'selected' value of the window component 'component'.  The type of value returned depends upon the type of component; a list component, for example, will return the 0-based number(s) of its selected item(s).  On the other hand, a boolean component such as a checkbox will return 1 if it is currently selected.
	return (_syscall(_fnum_windowComponentGetSelected, &component));
}

_X_ int windowComponentSetSelected(objectKey component, int selected _U_)
{
	// Proto: int kernelWindowComponentSetSelected(kernelWindowComponent *, int);
	// Desc : This is a call to set the 'selected' value of the window component 'component'.  The type of value accepted depends upon the type of component; a list component, for example, will use the 0-based number to select one of its items.  On the other hand, a boolean component such as a checkbox will clear itself if 'selected' is 0, and set itself otherwise.
	return (_syscall(_fnum_windowComponentSetSelected, &component));
}

_X_ objectKey windowNewButton(objectKey parent, const char *label _U_, image *buttonImage _U_, componentParameters *params _U_)
{
	// Proto: kernelWindowComponent *kernelWindowNewButton(objectKey, const char *, image *, componentParameters *);
	// Desc : Get a new button component to be placed inside the parent object 'parent', with the given component parameters, and with the (optional) label 'label', or the (optional) image 'buttonImage'.  Either 'label' or 'buttonImage' can be used, but not both.
	return ((objectKey)(long) _syscall(_fnum_windowNewButton, &parent));
}

_X_ objectKey windowNewCanvas(objectKey parent, int width _U_, int height _U_, componentParameters *params _U_)
{
	// Proto: kernelWindowComponent *kernelWindowNewCanvas(objectKey, int, int, componentParameters *);
	// Desc : Get a new canvas component, to be placed inside the parent object 'parent', using the supplied width and height, with the given component parameters.  Canvas components are areas which will allow drawing operations, for example to show line drawings or unique graphical elements.
	return ((objectKey)(long) _syscall(_fnum_windowNewCanvas, &parent));
}

_X_ objectKey windowNewCheckbox(objectKey parent, const char *text _U_, componentParameters *params _U_)
{
	// Proto: kernelWindowComponent *kernelWindowNewCheckbox(objectKey, const char *, componentParameters *);
	// Desc : Get a new checkbox component, to be placed inside the parent object 'parent', using the accompanying text 'text', and with the given component parameters.
	return ((objectKey)(long) _syscall(_fnum_windowNewCheckbox, &parent));
}

_X_ objectKey windowNewContainer(objectKey parent, const char *name _U_, componentParameters *params _U_)
{
	// Proto: kernelWindowComponent *kernelWindowNewContainer(objectKey, const char *, componentParameters *);
	// Desc : Get a new container component, to be placed inside the parent object 'parent', using the name 'name', and with the given component parameters.  Containers are useful for layout when a simple grid is not sufficient.  Each container has its own internal grid layout (for components it contains) and external grid parameters for placing it inside a window or another container.  Containers can be nested arbitrarily.  This allows limitless control over a complex window layout.
	return ((objectKey)(long) _syscall(_fnum_windowNewContainer, &parent));
}

_X_ objectKey windowNewDivider(objectKey parent, dividerType type _U_, componentParameters *params _U_)
{
	// Proto: kernelWindowComponent *kernelWindowNewDivider(objectKey, dividerType, componentParameters *);
	// Desc : Get a new divider component, to be placed inside the parent object 'parent', using the type 'type' (divider_vertical or divider_horizontal), and with the given component parameters.  These are just horizontal or vertical lines that can be used to visually separate sections of a window.
	return ((objectKey)(long) _syscall(_fnum_windowNewDivider, &parent));
}

_X_ objectKey windowNewIcon(objectKey parent, image *iconImage _U_, const char *label _U_, componentParameters *params _U_)
{
	// Proto: kernelWindowComponent *kernelWindowNewIcon(objectKey, image *, const char *, const char *, componentParameters *);
	// Desc : Get a new icon component to be placed inside the parent object 'parent', using the image data structure 'iconImage' and the label 'label', and with the given component parameters 'params'.
	return ((objectKey)(long) _syscall(_fnum_windowNewIcon, &parent));
}

_X_ objectKey windowNewImage(objectKey parent, image *baseImage _U_, drawMode mode _U_, componentParameters *params _U_)
{
	// Proto: kernelWindowComponent *kernelWindowNewImage(objectKey, image *, drawMode, componentParameters *);
	// Desc : Get a new image component to be placed inside the parent object 'parent', using the image data structure 'baseImage', and with the given component parameters 'params'.
	return ((objectKey)(long) _syscall(_fnum_windowNewImage, &parent));
}

_X_ objectKey windowNewList(objectKey parent, windowListType type _U_, int rows _U_, int columns _U_, int multiple _U_, listItemParameters *items _U_, int numItems _U_, componentParameters *params _U_)
{
	// Proto: kernelWindowComponent *kernelWindowNewList(objectKey, windowListType, int, int, int, listItemParameters *, int, componentParameters *);
	// Desc : Get a new window list component to be placed inside the parent object 'parent', using the component parameters 'params'.  'type' specifies the type of list (see <sys/window.h> for possibilities), 'rows' and 'columns' specify the size of the list and layout of the list items, 'multiple' allows multiple selections if non-zero, and 'items' is an array of 'numItems' list item parameters.
	return ((objectKey)(long) _syscall(_fnum_windowNewList, &parent));
}

_X_ objectKey windowNewListItem(objectKey parent, windowListType type _U_, listItemParameters *item _U_, componentParameters *params _U_)
{
	// Proto: kernelWindowComponent *kernelWindowNewListItem(objectKey, windowListType, listItemParameters *, componentParameters *);
	// Desc : Get a new list item component to be placed inside the parent object 'parent', using the list item parameters 'item', and the component parameters 'params'.
	return ((objectKey)(long) _syscall(_fnum_windowNewListItem, &parent));
}

_X_ objectKey windowNewMenu(objectKey parent, objectKey menuBar _U_, const char *name _U_, windowMenuContents *contents _U_, componentParameters *params _U_)
{
	// Proto: kernelWindow *kernelWindowNewMenu(kernelWindow *, kernelWindowComponent *, const char *, windowMenuContents *, componentParameters *);
	// Desc : Get a new menu to be associated with the parent object 'parent', in the (optional) menu bar 'menuBar', using the name 'name' (which will be the header of the menu in any menu bar, for example), the menu contents structure 'contents', and the component parameters 'params'.  A menu is a type of window, typically contains menu item components, and is often added to a menu bar component.
	return ((objectKey)(long) _syscall(_fnum_windowNewMenu, &parent));
}

_X_ objectKey windowNewMenuBar(objectKey window, componentParameters *params _U_)
{
	// Proto: kernelWindowComponent *kernelWindowNewMenuBar(kernelWindow *, componentParameters *);
	// Desc : Get a new menu bar component to be placed inside the window 'window', using the component parameters 'params'.  A menu bar component is an instance of a container, and typically contains menu components.
	return ((objectKey)(long) _syscall(_fnum_windowNewMenuBar, &window));
}

_X_ objectKey windowNewMenuItem(objectKey parent, const char *text _U_, componentParameters *params _U_)
{
	// Proto: kernelWindowComponent *kernelWindowNewMenuItem(objectKey, const char *, componentParameters *);
	// Desc : Get a new menu item component to be placed inside the parent object 'parent', using the string 'text' and the component parameters 'params'.  A menu item  component is typically added to menu components, which are in turn added to menu bar components.
	return ((objectKey)(long) _syscall(_fnum_windowNewMenuItem, &parent));
}

_X_ objectKey windowNewPasswordField(objectKey parent, int columns _U_, componentParameters *params _U_)
{
	// Proto: kernelWindowComponent *kernelWindowNewPasswordField(objectKey, int, componentParameters *);
	// Desc : Get a new password field component to be placed inside the parent object 'parent', using 'columns' columns and the component parameters 'params'.  A password field component is a special case of a text field component, and behaves the same way except that typed characters are shown as asterisks (*).
	return ((objectKey)(long) _syscall(_fnum_windowNewPasswordField, &parent));
}

_X_ objectKey windowNewProgressBar(objectKey parent, componentParameters *params _U_)
{
	// Proto: kernelWindowComponent *kernelWindowNewProgressBar(objectKey, componentParameters *);
	// Desc : Get a new progress bar component to be placed inside the parent object 'parent', using the component parameters 'params'.  Use the windowComponentSetData() function to set the percentage of progress.
	return ((objectKey)(long) _syscall(_fnum_windowNewProgressBar, &parent));
}

_X_ objectKey windowNewRadioButton(objectKey parent, int rows _U_, int columns _U_, char *items[] _U_, int numItems _U_, componentParameters *params _U_)
{
	// Proto: kernelWindowComponent *kernelWindowNewRadioButton(objectKey, int, int, const char **, int, componentParameters *);
	// Desc : Get a new radio button component to be placed inside the parent object 'parent', using the component parameters 'params'.  'rows' and 'columns' specify the size and layout of the items, and 'numItems' specifies the number of strings in the array 'items', which specifies the different radio button choices.  The windowComponentSetSelected() and windowComponentGetSelected() functions can be used to get and set the selected item (numbered from zero, in the order they were supplied in 'items').
	return ((objectKey)(long) _syscall(_fnum_windowNewRadioButton, &parent));
}

_X_ objectKey windowNewScrollBar(objectKey parent, scrollBarType type _U_, int width _U_, int height _U_, componentParameters *params _U_)
{
	// Proto: kernelWindowComponent *kernelWindowNewScrollBar(objectKey, scrollBarType, int, int, componentParameters *);
	// Desc : Get a new scroll bar component to be placed inside the parent object 'parent', with the scroll bar type 'type', and the given component parameters 'params'.
	return ((objectKey)(long) _syscall(_fnum_windowNewScrollBar, &parent));
}

_X_ objectKey windowNewSlider(objectKey parent, scrollBarType type _U_, int width _U_, int height _U_, componentParameters *params _U_)
{
	// Proto: kernelWindowComponent *kernelWindowNewSlider(objectKey, scrollBarType, int, int, componentParameters *);
	// Desc : Get a new slider component to be placed inside the parent object 'parent', with the scroll bar type 'type', and the given component parameters 'params'.
	return ((objectKey)(long) _syscall(_fnum_windowNewSlider, &parent));
}

_X_ objectKey windowNewTextArea(objectKey parent, int columns _U_, int rows _U_, int bufferLines _U_, componentParameters *params _U_)
{
	// Proto: kernelWindowComponent *kernelWindowNewTextArea(objectKey, int, int, int, componentParameters *);
	// Desc : Get a new text area component to be placed inside the parent object 'parent', with the given component parameters 'params'.  The 'columns' and 'rows' are the visible portion, and 'bufferLines' is the number of extra lines of scrollback memory.  If 'font' is NULL, the default font will be used.
	return ((objectKey)(long) _syscall(_fnum_windowNewTextArea, &parent));
}

_X_ objectKey windowNewTextField(objectKey parent, int columns _U_, componentParameters *params _U_)
{
	// Proto: kernelWindowComponent *kernelWindowNewTextField(objectKey, int, componentParameters *);
	// Desc : Get a new text field component to be placed inside the parent object 'parent', using the number of columns 'columns' and with the given component parameters 'params'.  Text field components are essentially 1-line 'text area' components.  If the params 'font' is NULL, the default font will be used.
	return ((objectKey)(long) _syscall(_fnum_windowNewTextField, &parent));
}

_X_ objectKey windowNewTextLabel(objectKey parent, const char *text _U_, componentParameters *params _U_)
{
	// Proto: kernelWindowComponent *kernelWindowNewTextLabel(objectKey, const char *, componentParameters *);
	// Desc : Get a new text label component to be placed inside the parent object 'parent', with the given component parameters 'params', and using the text string 'text'.  If the params 'font' is NULL, the default font will be used.
	return ((objectKey)(long) _syscall(_fnum_windowNewTextLabel, &parent));
}

_X_ objectKey windowNewTree(objectKey parent, windowTreeItem *rootItem _U_, int width _U_, int height _U_, componentParameters *params _U_)
{
	// Proto: kernelWindowComponent *kernelWindowNewTree(objectKey, windowTreeItem *, int, int, componentParameters *);
	// Desc : Get a new tree component to be placed inside the parent object 'parent', with the given width and height, component parameters 'params', and using the tree of windowTreeItems rooted at 'rootItem'.  If the params 'font' is NULL, the default font will be used.
	return ((objectKey)(long) _syscall(_fnum_windowNewTree, &parent));
}


//
// User functions
//

_X_ int userAuthenticate(const char *name, const char *password _U_)
{
	// Proto: int kernelUserAuthenticate(const char *, const char *);
	// Desc : Given the user 'name', return 0 if 'password' is the correct password.
	return (_syscall(_fnum_userAuthenticate, &name));
}

_X_ int userLogin(const char *name, const char *password _U_)
{
	// Proto: int kernelUserLogin(const char *, const char *);
	// Desc : Log the user 'name' into the system, using the password 'password'.  Calling this function requires supervisor privilege level.
	return (_syscall(_fnum_userLogin, &name));
}

_X_ int userLogout(const char *name)
{
	// Proto: int kernelUserLogout(const char *);
	// Desc : Log the user 'name' out of the system.  This can only be called by a process with supervisor privilege, or one running as the same user being logged out.
	return (_syscall(_fnum_userLogout, &name));
}

_X_ int userExists(const char *name)
{
	// Proto: int kernelUserExists(const char *);
	// Desc : Returns 1 if the user 'name' exists in the sytem, 0 otherwise.
	return (_syscall(_fnum_userExists, &name));
}

_X_ int userGetNames(char *buffer, unsigned bufferSize _U_)
{
	// Proto: int kernelUserGetNames(char *, unsigned);
	// Desc : Fill the buffer 'buffer' with the names of all users, up to 'bufferSize' bytes.
	return (_syscall(_fnum_userGetNames, &buffer));
}

_X_ int userAdd(const char *name, const char *password _U_)
{
	// Proto: int kernelUserAdd(const char *, const char *);
	// Desc : Add the user 'name' with the password 'password'
	return (_syscall(_fnum_userAdd, &name));
}

_X_ int userDelete(const char *name)
{
	// Proto: int kernelUserDelete(const char *);
	// Desc : Delete the user 'name'
	return (_syscall(_fnum_userDelete, &name));
}

_X_ int userSetPassword(const char *name, const char *oldPass _U_, const char *newPass _U_)
{
	// Proto: int kernelUserSetPassword(const char *, const char *, const char *);
	// Desc : Set the password of user 'name'.  If the calling program is not supervisor privilege, the correct old password must be supplied in 'oldPass'.  The new password is supplied in 'newPass'.
	return (_syscall(_fnum_userSetPassword, &name));
}

_X_ int userGetCurrent(char *buffer, unsigned bufferSize _U_)
{
	// Proto: int kernelUserGetCurrent(char *, unsigned);
	// Desc : Returns the name of the currently logged-in (if any) user in 'buffer', up to 'bufferSize' bytes.
	return (_syscall(_fnum_userGetCurrent, &buffer));
}

_X_ int userGetPrivilege(const char *name)
{
	// Proto: int kernelUserGetPrivilege(const char *);
	// Desc : Get the privilege level of the user represented by 'name'.
	return (_syscall(_fnum_userGetPrivilege, &name));
}

_X_ int userGetPid(void)
{
	// Proto: int kernelUserGetPid(void);
	// Desc : Get the process ID of the current user's 'login process'.
	return (_syscall(_fnum_userGetPid, NULL));
}

_X_ int userSetPid(const char *name, int pid _U_)
{
	// Proto: int kernelUserSetPid(const char *, int);
	// Desc : Set the login PID of user 'name' to 'pid'.  This is the process that gets killed when the user indicates that they want to logout.  In graphical mode this will typically be the PID of the window shell pid, and in text mode it will be the PID of the login VSH shell.
	return (_syscall(_fnum_userSetPid, &name));
}

_X_ int userFileAdd(const char *passFile, const char *userName _U_, const char *password _U_)
{
	// Proto: int kernelUserFileAdd(const char *, const char *, const char *);
	// Desc : Add a user to the designated password file, with the given name and password.  This can only be done by a privileged user.
	return (_syscall(_fnum_userFileAdd, &passFile));
}

_X_ int userFileDelete(const char *passFile, const char *userName _U_)
{
	// Proto: int kernelUserFileDelete(const char *, const char *);
	// Desc : Remove a user from the designated password file.  This can only be done by a privileged user
	return (_syscall(_fnum_userFileDelete, &passFile));
}

_X_ int userFileSetPassword(const char *passFile, const char *userName _U_, const char *oldPass _U_, const char *newPass _U_)
{
	// Proto: int kernelUserFileSetPassword(const char *, const char *, const char *, const char *);
	// Desc : Set the password of user 'userName' in the designated password file.  If the calling program is not supervisor privilege, the correct old password must be supplied in 'oldPass'.  The new password is supplied in 'newPass'.
	return (_syscall(_fnum_userFileSetPassword, &passFile));
}


//
// Network functions
//

_X_ int networkDeviceGetCount(void)
{
	// Proto: int kernelNetworkDeviceGetCount(void);
	// Desc: Returns the count of network devices
	return (_syscall(_fnum_networkDeviceGetCount, NULL));
}

_X_ int networkDeviceGet(const char *name, networkDevice *dev _U_)
{
	// Proto: int kernelNetworkDeviceGet(int, networkDevice *);
	// Desc: Returns the user-space portion of the requested (by 'name') network device in 'dev'.
	return (_syscall(_fnum_networkDeviceGet, &name));
}

_X_ int networkInitialized(void)
{
	// Proto: int kernelNetworkInitialized(void);
	// Desc: Returns 1 if networking is currently enabled.
	return (_syscall(_fnum_networkInitialized, NULL));
}

_X_ int networkInitialize(void)
{
	// Proto: int kernelNetworkInitialize(void);
	// Desc: Initialize and start networking.
	return (_syscall(_fnum_networkInitialize, NULL));
}

_X_ int networkShutdown(void)
{
	// Proto: int kernelNetworkShutdown(void);
	// Desc: Shut down networking.
	return (_syscall(_fnum_networkShutdown, NULL));
}

_X_ objectKey networkOpen(int mode, networkAddress *address _U_, networkFilter *filter _U_)
{
	// Proto: kernelNetworkConnection *kernelNetworkOpen(int, networkAddress *, networkFilter *);
	// Desc: Opens a connection for network communication.  The 'type' and 'mode' arguments describe the kind of connection to make (see possiblilities in the file <sys/network.h>.  If applicable, 'address' specifies the network address of the remote host to connect to.  If applicable, the 'localPort' and 'remotePort' arguments specify the TCP/UDP ports to use.
	return ((objectKey)(long) _syscall(_fnum_networkOpen, &mode));
}

_X_ int networkClose(objectKey connection)
{
	// Proto: int kernelNetworkClose(kernelNetworkConnection *);
	// Desc: Close the specified, previously-opened network connection.
	return (_syscall(_fnum_networkClose, &connection));
}

_X_ int networkCount(objectKey connection)
{
	// Proto: int kernelNetworkCount(kernelNetworkConnection *connection);
	// Desc: Given a network connection, return the number of bytes currently pending in the input stream
	return (_syscall(_fnum_networkCount, &connection));
}

_X_ int networkRead(objectKey connection, unsigned char *buffer _U_, unsigned bufferSize _U_)
{
	// Proto: int kernelNetworkRead(kernelNetworkConnection *, unsigned char *, unsigned);
	// Desc: Given a network connection, a buffer, and a buffer size, read up to 'bufferSize' bytes (or the number of bytes available in the connection's input stream) and return the number read.  The connection must be initiated using the networkConnectionOpen() function.
	return (_syscall(_fnum_networkRead, &connection));
}

_X_ int networkWrite(objectKey connection, unsigned char *buffer _U_, unsigned bufferSize _U_)
{
	// Proto: int kernelNetworkWrite(kernelNetworkConnection *, unsigned char *, unsigned);
	// Desc: Given a network connection, a buffer, and a buffer size, write up to 'bufferSize' bytes from 'buffer' to the connection's output.  The connection must be initiated using the networkConnectionOpen() function.
	return (_syscall(_fnum_networkWrite, &connection));
}

_X_ int networkPing(objectKey connection, int seqNum _U_, unsigned char *buffer _U_, unsigned bufferSize _U_)
{
	// Proto: int kernelNetworkPing(kernelNetworkConnection *, int, unsigned char *, unsigned);
	// Desc: Send an ICMP "echo request" packet to the host at the network address 'destAddress', with the (optional) sequence number 'seqNum'.  The 'buffer' and 'bufferSize' arguments point to the location of data to send in the ping packet.  The content of the data is mostly irrelevant, except that it can be checked to ensure the same data is returned by the ping reply from the remote host.
	return (_syscall(_fnum_networkPing, &connection));
}

_X_ int networkGetHostName(char *buffer, int bufferSize _U_)
{
	// Proto: int kernelNetworkGetHostName(char *, int);
	// Desc: Returns up to 'bufferSize' bytes of the system's network hostname in 'buffer'
	return (_syscall(_fnum_networkGetHostName, &buffer));
}

_X_ int networkSetHostName(const char *buffer, int bufferSize _U_)
{
	// Proto: int kernelNetworkSetHostName(const char *, int);
	// Desc: Sets the system's network hostname using up to 'bufferSize' bytes from 'buffer'
	return (_syscall(_fnum_networkSetHostName, &buffer));
}

_X_ int networkGetDomainName(char *buffer, int bufferSize _U_)
{
	// Proto: int kernelNetworkGetDomainName(char *, int);
	// Desc: Returns up to 'bufferSize' bytes of the system's network domain name in 'buffer'
	return (_syscall(_fnum_networkGetDomainName, &buffer));
}

_X_ int networkSetDomainName(const char *buffer, int bufferSize _U_)
{
	// Proto: int kernelNetworkSetDomainName(const char *, int);
	// Desc: Sets the system's network domain name using up to 'bufferSize' bytes from 'buffer'
	return (_syscall(_fnum_networkSetDomainName, &buffer));
}


//
// Miscellaneous functions
//

_X_ int shutdown(int reboot, int nice _U_)
{
	// Proto: int kernelShutdown(int, int);
	// Desc : Shut down the system.  If 'reboot' is non-zero, the system will reboot.  If 'nice' is zero, the shutdown will be orderly and will abort if serious errors are detected.  If 'nice' is non-zero, the system will go down like a kamikaze regardless of errors.
	return (_syscall(_fnum_shutdown, &reboot));
}

_X_ void getVersion(char *buff, int buffSize _U_)
{
	// Proto: void kernelGetVersion(char *, int);
	// Desc : Get the kernel's version string int the buffer 'buff', up to 'buffSize' bytes
	_syscall(_fnum_getVersion, &buff);
}

_X_ int systemInfo(struct utsname *uname)
{
	// Proto: int kernelSystemInfo(void *);
	// Desc : Gathers some info about the system and puts it into the utsname structure 'uname', just like the one returned by the system call 'uname' in Unix.
	return (_syscall(_fnum_systemInfo, &uname));
}

_X_ int encryptMD5(const char *in, char *out _U_)
{
	// Proto: int kernelEncryptMD5(const char *, char *);
	// Desc : Given the input string 'in', return the encrypted numerical message digest in the buffer 'out'.
	return (_syscall(_fnum_encryptMD5, &in));
}

_X_ int lockGet(lock *getLock)
{
	// Proto: int kernelLockGet(lock *);
	// Desc : Get an exclusive lock based on the lock structure 'getLock'.
	return (_syscall(_fnum_lockGet, &getLock));
}

_X_ int lockRelease(lock *relLock)
{
	// Proto: int kernelLockRelease(lock *);
	// Desc : Release a lock on the lock structure 'lock' previously obtained with a call to the lockGet() function.
	return (_syscall(_fnum_lockRelease, &relLock));
}

_X_ int lockVerify(lock *verLock)
{
	// Proto: int kernelLockVerify(lock *);
	// Desc : Verify that a lock on the lock structure 'verLock' is still valid.  This can be useful for retrying a lock attempt if a previous one failed; if the process that was previously holding the lock has failed, this will release the lock.
	return (_syscall(_fnum_lockVerify, &verLock));
}

_X_ int configRead(const char *fileName, variableList *list _U_)
{
	// Proto: int kernelConfigRead(const char *, variableList *);
	// Desc : Read the contents of the configuration file 'fileName', and return the data in the variable list structure 'list'.  Configuration files are simple properties files, consisting of lines of the format "variable=value"
	return (_syscall(_fnum_configRead, &fileName));
}

_X_ int configWrite(const char *fileName, variableList *list _U_)
{
	// Proto: int kernelConfigWrite(const char *, variableList *);
	// Desc : Write the contents of the variable list 'list' to the configuration file 'fileName'.  Configuration files are simple properties files, consisting of lines of the format "variable=value".  If the configuration file already exists, the configuration writer will attempt to preserve comment lines (beginning with '#') and formatting whitespace.
	return (_syscall(_fnum_configWrite, &fileName));
}

_X_ int configGet(const char *fileName, const char *variable _U_, char *buffer _U_, unsigned buffSize _U_)
{
	// Proto: int kernelConfigGet(const char *, const char *, char *, unsigned);
	// Desc : This is a convenience function giving the ability to quickly get a single variable value from a config file.  Uses the configRead function, above, to read the config file 'fileName', and attempt to read the variable 'variable' into the buffer 'buffer' with size 'buffSize'.
	return (_syscall(_fnum_configGet, &fileName));
}

_X_ int configSet(const char *fileName, const char *variable _U_, const char *value _U_)
{
	// Proto: int kernelConfigSet(const char *, const char *, const char *);
	// Desc : This is a convenience function giving the ability to quickly set a single variable value in a config file.  Uses the configRead and configWrite functions, above, to change the variable 'variable' to the value 'value'.
	return (_syscall(_fnum_configSet, &fileName));
}

_X_ int configUnset(const char *fileName, const char *variable _U_)
{
	// Proto: int kernelConfigUnset(const char *, const char *);
	// Desc : This is a convenience function giving the ability to quickly unset a single variable value in a config file.  Uses the configRead and configWrite functions, above, to delete the variable 'variable'.
	return (_syscall(_fnum_configUnset, &fileName));
}

_X_ int guidGenerate(guid *g)
{
	// Proto: int kernelGuidGenerate(guid *);
	// Desc : Generates a GUID in the guid structure 'g'.
	return (_syscall(_fnum_guidGenerate, &g));
}

_X_ unsigned crc32(void *buff, unsigned len _U_, unsigned *lastCrc _U_)
{
	// Proto: unsigned kernelCrc32(void *, unsigned, unsigned *);
	// Desc : Generate a CRC32 from 'len' bytes of the buffer 'buff', using an optional previous CRC32 value (otherwise lastCrc should be NULL).
	return (_syscall(_fnum_crc32, &buff));
}

_X_ int keyboardGetMap(keyMap *map)
{
	// Proto: int kernelKeyboardGetMap(keyMap *map);
	// Desc : Returns a copy of the current keyboard map in 'map'.
	return (_syscall(_fnum_keyboardGetMap, &map));
}

_X_ int keyboardSetMap(const char *name)
{
	// Proto: int kernelKeyboardSetMap(const char *);
	// Desc : Load the keyboard map from the file 'name' and set it as the system's current mapping.  If the filename is NULL, then the default (English US) mapping will be used.
	return (_syscall(_fnum_keyboardSetMap, &name));
}

_X_ int keyboardVirtualInput(int eventType, keyScan scanCode _U_)
{
	// Proto: int kernelKeyboardVirtualInput(int, keyScan);
	// Desc : Supply input to the kernel's virtual keyboard.  'eventType' should be either EVENT_KEY_DOWN or EVENT_KEY_UP, and 'scanCode' specifies which 'physical' key was pressed or unpressed (the actual character this might produce depends on the active keyboard map).
	return (_syscall(_fnum_keyboardVirtualInput, &eventType));
}

_X_ int deviceTreeGetRoot(device *rootDev)
{
	// Proto: int kernelDeviceTreeGetRoot(device *);
	// Desc : Returns the user-space portion of the device tree root device in the structure 'rootDev'.
	return (_syscall(_fnum_deviceTreeGetRoot, &rootDev));
}

_X_ int deviceTreeGetChild(device *parentDev, device *childDev _U_)
{
	// Proto: int kernelDeviceTreeGetChild(device *, device *);
	// Desc : Returns the user-space portion of the first child device of 'parentDev' in the structure 'childDev'.
	return (_syscall(_fnum_deviceTreeGetChild, &parentDev));
}

_X_ int deviceTreeGetNext(device *siblingDev)
{
	// Proto: int kernelDeviceTreeGetNext(device *);
	// Desc : Returns the user-space portion of the next sibling device of the supplied device 'siblingDev' in the same data structure.
	return (_syscall(_fnum_deviceTreeGetNext, &siblingDev));
}

_X_ int mouseLoadPointer(const char *pointerName, const char *fileName _U_)
{
	// Proto: int kernelMouseLoadPointer(const char *, const char *)
	// Desc : Tells the mouse driver code to load the mouse pointer 'pointerName' from the file 'fileName'.
	return (_syscall(_fnum_mouseLoadPointer, &pointerName));
}

_X_ void *pageGetPhysical(int processId, void *pointer _U_)
{
	// Proto: void *kernelPageGetPhysical(int, void *);
	// Desc : Returns the physical address corresponding pointed to by the virtual address 'pointer' for the process 'processId'
	return ((void *)(long) _syscall(_fnum_pageGetPhysical, &processId));
}

_X_ unsigned charsetToUnicode(const char *set, unsigned value _U_)
{
	// Proto: unsigned kernelCharsetToUnicode(const char *, unsigned);
	// Desc : Given a character set name and a character code from that set, return the equivalent unicode value
	return (_syscall(_fnum_charsetToUnicode, &set));
}

_X_ unsigned charsetFromUnicode(const char *set, unsigned value _U_)
{
	// Proto: unsigned kernelCharsetFromUnicode(const char *, unsigned);
	// Desc : Given a character set name and a unicode value, return the equivalent character set value
	return (_syscall(_fnum_charsetFromUnicode, &set));
}

