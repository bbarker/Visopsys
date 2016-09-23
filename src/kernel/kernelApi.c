//
//  Visopsys
//  Copyright (C) 1998-2016 J. Andrew McLaughlin
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
//  kernelApi.c
//

// This is the part of the kernel's API that sorts out which functions
// get called from external locations.

#include "kernelApi.h"
#include "kernelCharset.h"
#include "kernelDebug.h"
#include "kernelDisk.h"
#include "kernelEncrypt.h"
#include "kernelEnvironment.h"
#include "kernelError.h"
#include "kernelFile.h"
#include "kernelFileStream.h"
#include "kernelFilesystem.h"
#include "kernelFont.h"
#include "kernelImage.h"
#include "kernelKeyboard.h"
#include "kernelLoader.h"
#include "kernelMemory.h"
#include "kernelMisc.h"
#include "kernelMultitasker.h"
#include "kernelNetwork.h"
#include "kernelNetworkDevice.h"
#include "kernelPage.h"
#include "kernelParameters.h"
#include "kernelRandom.h"
#include "kernelRtc.h"
#include "kernelShutdown.h"
#include "kernelText.h"
#include "kernelUser.h"
#include "kernelWindow.h"
#include <sys/processor.h>

// We do this so that <sys/api.h> won't complain about being included
// in a kernel file
#undef KERNEL
	#include <sys/api.h>
#define KERNEL

// Function index arrays.  Grouped by category.

// Text input/output functions (0x1000-0x1FFF range)

static kernelArgInfo args_textSetConsoleInput[] =
	{ { 1, type_ptr, API_ARG_KERNPTR } };
static kernelArgInfo args_textSetConsoleOutput[] =
	{ { 1, type_ptr, API_ARG_KERNPTR } };
static kernelArgInfo args_textSetCurrentInput[] =
	{ { 1, type_ptr, API_ARG_KERNPTR } };
static kernelArgInfo args_textSetCurrentOutput[] =
	{ { 1, type_ptr, API_ARG_KERNPTR } };
static kernelArgInfo args_textGetForeground[] =
	{ { 1, type_ptr, API_ARG_NONNULLPTR | API_ARG_USERPTR } };
static kernelArgInfo args_textSetForeground[] =
	{ { 1, type_ptr, API_ARG_NONNULLPTR | API_ARG_USERPTR } };
static kernelArgInfo args_textGetBackground[] =
	{ { 1, type_ptr, API_ARG_NONNULLPTR | API_ARG_USERPTR } };
static kernelArgInfo args_textSetBackground[] =
	{ { 1, type_ptr, API_ARG_NONNULLPTR | API_ARG_USERPTR } };
static kernelArgInfo args_textPutc[] =
	{ { 1, type_val, API_ARG_ANYVAL } };
static kernelArgInfo args_textPrint[] =
	{ { 1, type_ptr, API_ARG_NONNULLPTR | API_ARG_USERPTR } };
static kernelArgInfo args_textPrintAttrs[] =
	{ { 1, type_ptr, API_ARG_USERPTR },
		{ 1, type_ptr, API_ARG_NONNULLPTR | API_ARG_USERPTR } };
static kernelArgInfo args_textPrintLine[] =
	{ { 1, type_ptr, API_ARG_NONNULLPTR | API_ARG_USERPTR } };
static kernelArgInfo args_textEnableScroll[] =
	{ { 1, type_val, API_ARG_ANYVAL } };
static kernelArgInfo args_textScroll[] =
	{ { 1, type_val, API_ARG_ANYVAL } };
static kernelArgInfo args_textSetColumn[] =
	{ { 1, type_val, API_ARG_ANYVAL } };
static kernelArgInfo args_textSetRow[] =
	{ { 1, type_val, API_ARG_ANYVAL } };
static kernelArgInfo args_textSetCursor[] =
	{ { 1, type_val, API_ARG_ANYVAL } };
static kernelArgInfo args_textScreenSave[] =
	{ { 1, type_ptr, API_ARG_NONNULLPTR | API_ARG_USERPTR } };
static kernelArgInfo args_textScreenRestore[] =
	{ { 1, type_ptr, API_ARG_NONNULLPTR | API_ARG_USERPTR } };
static kernelArgInfo args_textInputStreamCount[] =
	{ { 1, type_ptr, API_ARG_KERNPTR } };
static kernelArgInfo args_textInputStreamGetc[] =
	{ { 1, type_ptr, API_ARG_KERNPTR },
		{ 1, type_ptr, API_ARG_NONNULLPTR | API_ARG_USERPTR } };
static kernelArgInfo args_textInputGetc[] =
	{ { 1, type_ptr, API_ARG_NONNULLPTR | API_ARG_USERPTR } };
static kernelArgInfo args_textInputStreamReadN[] =
	{ { 1, type_ptr, API_ARG_KERNPTR },
		{ 1, type_val, API_ARG_ANYVAL },
		{ 1, type_ptr, API_ARG_NONNULLPTR | API_ARG_USERPTR } };
static kernelArgInfo args_textInputReadN[] =
	{ { 1, type_val, API_ARG_ANYVAL },
		{ 1, type_ptr, API_ARG_NONNULLPTR | API_ARG_USERPTR } };
static kernelArgInfo args_textInputStreamReadAll[] =
	{ { 1, type_ptr, API_ARG_KERNPTR },
		{ 1, type_ptr, API_ARG_NONNULLPTR | API_ARG_USERPTR } };
static kernelArgInfo args_textInputReadAll[] =
	{ { 1, type_ptr, API_ARG_NONNULLPTR | API_ARG_USERPTR } };
static kernelArgInfo args_textInputStreamAppend[] =
	{ { 1, type_ptr, API_ARG_KERNPTR },
		{ 1, type_val, API_ARG_ANYVAL } };
static kernelArgInfo args_textInputAppend[] =
	{ { 1, type_val, API_ARG_ANYVAL } };
static kernelArgInfo args_textInputStreamAppendN[] =
	{ { 1, type_ptr, API_ARG_KERNPTR },
		{ 1, type_val, API_ARG_ANYVAL },
		{ 1, type_ptr, API_ARG_NONNULLPTR | API_ARG_USERPTR } };
static kernelArgInfo args_textInputAppendN[] =
	{ { 1, type_val, API_ARG_ANYVAL },
		{ 1, type_ptr, API_ARG_NONNULLPTR | API_ARG_USERPTR } };
static kernelArgInfo args_textInputStreamRemove[] =
	{ { 1, type_ptr, API_ARG_KERNPTR } };
static kernelArgInfo args_textInputStreamRemoveN[] =
	{ { 1, type_ptr, API_ARG_KERNPTR },
		{ 1, type_val, API_ARG_ANYVAL } };
static kernelArgInfo args_textInputRemoveN[] =
	{ { 1, type_val, API_ARG_ANYVAL } };
static kernelArgInfo args_textInputStreamRemoveAll[] =
	{ { 1, type_ptr, API_ARG_KERNPTR } };
static kernelArgInfo args_textInputStreamSetEcho[] =
	{ { 1, type_ptr, API_ARG_KERNPTR },
		{ 1, type_val, API_ARG_ANYVAL } };
static kernelArgInfo args_textInputSetEcho[] =
	{ { 1, type_val, API_ARG_ANYVAL } };

static kernelFunctionIndex textFunctionIndex[] = {
	{ _fnum_textGetConsoleInput, kernelTextGetConsoleInput,
		PRIVILEGE_USER, 0, NULL, type_ptr },
	{ _fnum_textSetConsoleInput, kernelTextSetConsoleInput,
		PRIVILEGE_SUPERVISOR, 1, args_textSetConsoleInput, type_val },
	{ _fnum_textGetConsoleOutput, kernelTextGetConsoleOutput,
		PRIVILEGE_USER, 0, NULL, type_ptr },
	{ _fnum_textSetConsoleOutput, kernelTextSetConsoleOutput,
		PRIVILEGE_SUPERVISOR, 1, args_textSetConsoleOutput, type_val },
	{ _fnum_textGetCurrentInput, kernelTextGetCurrentInput,
		PRIVILEGE_USER, 0, NULL, type_ptr },
	{ _fnum_textSetCurrentInput, kernelTextSetCurrentInput,
		PRIVILEGE_USER, 1, args_textSetCurrentInput, type_val },
	{ _fnum_textGetCurrentOutput, kernelTextGetCurrentOutput,
		PRIVILEGE_USER, 0, NULL, type_ptr },
	{ _fnum_textSetCurrentOutput, kernelTextSetCurrentOutput,
		PRIVILEGE_USER, 1, args_textSetCurrentOutput, type_val },
	{ _fnum_textGetForeground, kernelTextGetForeground,
		PRIVILEGE_USER, 1, args_textGetForeground, type_val },
	{ _fnum_textSetForeground, kernelTextSetForeground,
		PRIVILEGE_USER, 1, args_textSetForeground, type_val },
	{ _fnum_textGetBackground, kernelTextGetBackground,
		PRIVILEGE_USER, 1, args_textGetBackground, type_val },
	{ _fnum_textSetBackground, kernelTextSetBackground,
		PRIVILEGE_USER, 1, args_textSetBackground, type_val },
	{ _fnum_textPutc, kernelTextPutc,
		PRIVILEGE_USER, 1, args_textPutc, type_val },
	{ _fnum_textPrint, (void *) kernelTextPrint,
		PRIVILEGE_USER, 1, args_textPrint, type_val },
	{ _fnum_textPrintAttrs, (void *) kernelTextPrintAttrs,
		PRIVILEGE_USER, 2, args_textPrintAttrs, type_val },
	{ _fnum_textPrintLine, (void *) kernelTextPrintLine,
		PRIVILEGE_USER, 1, args_textPrintLine, type_val },
	{ _fnum_textNewline, kernelTextNewline,
		PRIVILEGE_USER, 0, NULL, type_void },
	{ _fnum_textBackSpace, kernelTextBackSpace,
		PRIVILEGE_USER, 0, NULL, type_void },
	{ _fnum_textTab, kernelTextTab,
		PRIVILEGE_USER, 0, NULL, type_void },
	{ _fnum_textCursorUp, kernelTextCursorUp,
		PRIVILEGE_USER, 0, NULL, type_void },
	{ _fnum_textCursorDown, kernelTextCursorDown,
		PRIVILEGE_USER, 0, NULL, type_void },
	{ _fnum_ternelTextCursorLeft, kernelTextCursorLeft,
		PRIVILEGE_USER, 0, NULL, type_void },
	{ _fnum_textCursorRight, kernelTextCursorRight,
		PRIVILEGE_USER, 0, NULL, type_void },
	{ _fnum_textEnableScroll, kernelTextEnableScroll,
		PRIVILEGE_USER, 1, args_textEnableScroll, type_val },
	{ _fnum_textScroll, kernelTextScroll,
		PRIVILEGE_USER, 1, args_textScroll, type_void },
	{ _fnum_textGetNumColumns, kernelTextGetNumColumns,
		PRIVILEGE_USER, 0, NULL, type_val },
	{ _fnum_textGetNumRows, kernelTextGetNumRows,
		PRIVILEGE_USER, 0, NULL, type_val },
	{ _fnum_textGetColumn, kernelTextGetColumn,
		PRIVILEGE_USER, 0, NULL, type_val },
	{ _fnum_textSetColumn, kernelTextSetColumn,
		PRIVILEGE_USER, 1, args_textSetColumn, type_void },
	{ _fnum_textGetRow, kernelTextGetRow,
		PRIVILEGE_USER, 0, NULL, type_val },
	{ _fnum_textSetRow, kernelTextSetRow,
		PRIVILEGE_USER, 1, args_textSetRow, type_void },
	{ _fnum_textSetCursor, kernelTextSetCursor,
		PRIVILEGE_USER, 1, args_textSetCursor, type_void },
	{ _fnum_textScreenClear, kernelTextScreenClear,
		PRIVILEGE_USER, 0, NULL, type_void },
	{ _fnum_textScreenSave, kernelTextScreenSave,
		PRIVILEGE_USER, 1, args_textScreenSave, type_val },
	{ _fnum_textScreenRestore, kernelTextScreenRestore,
		PRIVILEGE_USER, 1, args_textScreenRestore, type_val },
	{ _fnum_textInputStreamCount, kernelTextInputStreamCount,
		PRIVILEGE_USER, 1, args_textInputStreamCount, type_val },
	{ _fnum_textInputCount, kernelTextInputCount,
		PRIVILEGE_USER, 0, NULL, type_val },
	{ _fnum_textInputStreamGetc, kernelTextInputStreamGetc,
		PRIVILEGE_USER, 2, args_textInputStreamGetc, type_val },
	{ _fnum_textInputGetc, kernelTextInputGetc,
		PRIVILEGE_USER, 1, args_textInputGetc, type_val },
	{ _fnum_textInputStreamReadN, kernelTextInputStreamReadN,
		PRIVILEGE_USER, 3, args_textInputStreamReadN, type_val },
	{ _fnum_textInputReadN, kernelTextInputReadN,
		PRIVILEGE_USER, 2, args_textInputReadN, type_val },
	{ _fnum_textInputStreamReadAll, kernelTextInputStreamReadAll,
		PRIVILEGE_USER, 2, args_textInputStreamReadAll, type_val },
	{ _fnum_textInputReadAll, kernelTextInputReadAll,
		PRIVILEGE_USER, 1, args_textInputReadAll, type_val },
	{ _fnum_textInputStreamAppend, kernelTextInputStreamAppend,
		PRIVILEGE_USER, 2, args_textInputStreamAppend, type_val },
	{ _fnum_textInputAppend, kernelTextInputAppend,
		PRIVILEGE_USER, 1, args_textInputAppend, type_val },
	{ _fnum_textInputStreamAppendN, kernelTextInputStreamAppendN,
		PRIVILEGE_USER, 3, args_textInputStreamAppendN, type_val },
	{ _fnum_textInputAppendN, kernelTextInputAppendN,
		PRIVILEGE_USER, 2, args_textInputAppendN, type_val },
	{ _fnum_textInputStreamRemove, kernelTextInputStreamRemove,
		PRIVILEGE_USER, 1, args_textInputStreamRemove, type_val },
	{ _fnum_textInputRemove, kernelTextInputRemove,
		PRIVILEGE_USER, 0, NULL, type_val },
	{ _fnum_textInputStreamRemoveN, kernelTextInputStreamRemoveN,
		PRIVILEGE_USER, 2, args_textInputStreamRemoveN, type_val },
	{ _fnum_textInputRemoveN, kernelTextInputRemoveN,
		PRIVILEGE_USER, 1, args_textInputRemoveN, type_val },
	{ _fnum_textInputStreamRemoveAll, kernelTextInputStreamRemoveAll,
		PRIVILEGE_USER, 1, args_textInputStreamRemoveAll, type_val },
	{ _fnum_textInputRemoveAll, kernelTextInputRemoveAll,
		PRIVILEGE_USER, 0, NULL, type_val },
	{ _fnum_textInputStreamSetEcho, kernelTextInputStreamSetEcho,
		PRIVILEGE_USER, 2, args_textInputStreamSetEcho, type_void },
	{ _fnum_textInputSetEcho, kernelTextInputSetEcho,
		PRIVILEGE_USER, 1, args_textInputSetEcho, type_void }
};

// Disk functions (0x2000-0x2FFF range)

static kernelArgInfo args_diskReadPartitions[] =
	{ { 1, type_ptr, API_ARG_NONNULLPTR | API_ARG_USERPTR } };
static kernelArgInfo args_diskSync[] =
	{ { 1, type_ptr, API_ARG_NONNULLPTR | API_ARG_USERPTR } };
static kernelArgInfo args_diskGetBoot[] =
	{ { 1, type_ptr, API_ARG_NONNULLPTR | API_ARG_USERPTR } };
static kernelArgInfo args_diskGet[] =
	{ { 1, type_ptr, API_ARG_NONNULLPTR | API_ARG_USERPTR },
		{ 1, type_ptr, API_ARG_NONNULLPTR | API_ARG_USERPTR } };
static kernelArgInfo args_diskGetAll[] =
	{ { 1, type_ptr, API_ARG_NONNULLPTR | API_ARG_USERPTR },
		{ 1, type_val, API_ARG_ANYVAL } };
static kernelArgInfo args_diskGetAllPhysical[] =
	{ { 1, type_ptr, API_ARG_NONNULLPTR | API_ARG_USERPTR },
		{ 1, type_val, API_ARG_ANYVAL } };
static kernelArgInfo args_diskGetFilesystemType[] =
	{ { 1, type_ptr, API_ARG_NONNULLPTR | API_ARG_USERPTR },
		{ 1, type_ptr, API_ARG_NONNULLPTR | API_ARG_USERPTR },
		{ 1, type_val, API_ARG_ANYVAL } };
static kernelArgInfo args_diskGetMsdosPartType[] =
	{ { 1, type_val, API_ARG_ANYVAL },
		{ 1, type_ptr, API_ARG_NONNULLPTR | API_ARG_USERPTR } };
static kernelArgInfo args_diskGetGptPartType[] =
	{ { 1, type_ptr, API_ARG_NONNULLPTR | API_ARG_USERPTR },
		{ 1, type_ptr, API_ARG_NONNULLPTR | API_ARG_USERPTR } };
static kernelArgInfo args_diskSetFlags[] =
	{ { 1, type_ptr, API_ARG_NONNULLPTR | API_ARG_USERPTR },
		{ 1, type_val, API_ARG_ANYVAL },
		{ 1, type_val, API_ARG_ANYVAL } };
static kernelArgInfo args_diskSetLockState[] =
	{ { 1, type_ptr, API_ARG_NONNULLPTR | API_ARG_USERPTR },
		{ 1, type_val, API_ARG_ANYVAL } };
static kernelArgInfo args_diskSetDoorState[] =
	{ { 1, type_ptr, API_ARG_NONNULLPTR | API_ARG_USERPTR },
		{ 1, type_val, API_ARG_ANYVAL } };
static kernelArgInfo args_diskMediaPresent[] =
	{ { 1, type_ptr, API_ARG_NONNULLPTR | API_ARG_USERPTR } };
static kernelArgInfo args_diskReadSectors[] =
	{ { 1, type_ptr, API_ARG_NONNULLPTR | API_ARG_USERPTR },
		{ 2, type_val, API_ARG_ANYVAL },
		{ 2, type_val, API_ARG_ANYVAL },
		{ 1, type_ptr, API_ARG_NONNULLPTR | API_ARG_USERPTR } };
static kernelArgInfo args_diskWriteSectors[] =
	{ { 1, type_ptr, API_ARG_NONNULLPTR | API_ARG_USERPTR },
		{ 2, type_val, API_ARG_ANYVAL },
		{ 2, type_val, API_ARG_ANYVAL },
		{ 1, type_ptr, API_ARG_NONNULLPTR | API_ARG_USERPTR } };
static kernelArgInfo args_diskEraseSectors[] =
	{ { 1, type_ptr, API_ARG_NONNULLPTR | API_ARG_USERPTR },
		{ 2, type_val, API_ARG_ANYVAL },
		{ 2, type_val, API_ARG_ANYVAL },
		{ 1, type_val, API_ARG_ANYVAL } };
static kernelArgInfo args_diskGetStats[] =
	{ { 1, type_ptr, API_ARG_USERPTR },
		{ 1, type_ptr, API_ARG_NONNULLPTR | API_ARG_USERPTR } };
static kernelArgInfo args_diskRamDiskCreate[] =
	{ { 1, type_val, API_ARG_ANYVAL },
		{ 1, type_ptr, API_ARG_NONNULLPTR | API_ARG_USERPTR } };
static kernelArgInfo args_diskRamDiskDestroy[] =
	{ { 1, type_ptr, API_ARG_NONNULLPTR | API_ARG_USERPTR } };

static kernelFunctionIndex diskFunctionIndex[] = {
	{ _fnum_diskReadPartitions, kernelDiskReadPartitions,
		PRIVILEGE_SUPERVISOR, 1, args_diskReadPartitions, type_val },
	{ _fnum_diskReadPartitionsAll, kernelDiskReadPartitionsAll,
		PRIVILEGE_SUPERVISOR, 0, NULL, type_val },
	{ _fnum_diskSync, kernelDiskSync,
		PRIVILEGE_USER, 1, args_diskSync, type_val },
	{ _fnum_diskSyncAll, kernelDiskSyncAll,
		PRIVILEGE_USER, 0, NULL, type_val },
	{ _fnum_diskGetBoot, kernelDiskGetBoot,
		PRIVILEGE_USER, 1, args_diskGetBoot, type_val },
	{ _fnum_diskGetCount, kernelDiskGetCount,
		PRIVILEGE_USER, 0, NULL, type_val },
	{ _fnum_diskGetPhysicalCount, kernelDiskGetPhysicalCount,
		PRIVILEGE_USER, 0, NULL, type_val },
	{ _fnum_diskGet, kernelDiskGet,
		PRIVILEGE_USER, 2, args_diskGet, type_val },
	{ _fnum_diskGetAll, kernelDiskGetAll,
		PRIVILEGE_USER, 2, args_diskGetAll, type_val },
	{ _fnum_diskGetAllPhysical, kernelDiskGetAllPhysical,
		PRIVILEGE_USER, 2, args_diskGetAllPhysical, type_val },
	{ _fnum_diskGetFilesystemType, kernelDiskGetFilesystemType,
		PRIVILEGE_USER, 3, args_diskGetFilesystemType, type_val },
	{ _fnum_diskGetMsdosPartType, kernelDiskGetMsdosPartType,
		PRIVILEGE_USER, 2, args_diskGetMsdosPartType, type_val },
	{ _fnum_diskGetMsdosPartTypes, kernelDiskGetMsdosPartTypes,
		PRIVILEGE_USER, 0, NULL, type_ptr },
	{ _fnum_diskGetGptPartType, kernelDiskGetGptPartType,
		PRIVILEGE_USER, 2, args_diskGetGptPartType, type_val },
	{ _fnum_diskGetGptPartTypes, kernelDiskGetGptPartTypes,
		PRIVILEGE_USER, 0, NULL, type_ptr },
	{ _fnum_diskSetFlags, kernelDiskSetFlags,
		PRIVILEGE_SUPERVISOR, 3, args_diskSetFlags, type_val },
	{ _fnum_diskSetLockState, kernelDiskSetLockState,
		PRIVILEGE_USER, 2, args_diskSetLockState, type_val },
	{ _fnum_diskSetDoorState, kernelDiskSetDoorState,
		PRIVILEGE_USER, 2, args_diskSetDoorState, type_val },
	{ _fnum_diskMediaPresent, kernelDiskMediaPresent,
		PRIVILEGE_USER, 1, args_diskMediaPresent, type_val },
	{ _fnum_diskReadSectors, kernelDiskReadSectors,
		PRIVILEGE_SUPERVISOR, 4, args_diskReadSectors, type_val },
	{ _fnum_diskWriteSectors, kernelDiskWriteSectors,
		PRIVILEGE_SUPERVISOR, 4, args_diskWriteSectors, type_val },
	{ _fnum_diskEraseSectors, kernelDiskEraseSectors,
		PRIVILEGE_SUPERVISOR, 4, args_diskEraseSectors, type_val },
	{ _fnum_diskGetStats, kernelDiskGetStats,
		PRIVILEGE_USER, 2, args_diskGetStats, type_val },
	{ _fnum_diskRamDiskCreate, kernelDiskRamDiskCreate,
		PRIVILEGE_SUPERVISOR, 2, args_diskRamDiskCreate, type_val },
	{ _fnum_diskRamDiskDestroy, kernelDiskRamDiskDestroy,
		PRIVILEGE_SUPERVISOR, 1, args_diskRamDiskDestroy, type_val }
};

// Filesystem functions (0x3000-0x3FFF range)

static kernelArgInfo args_filesystemScan[] =
	{ { 1, type_ptr, API_ARG_NONNULLPTR | API_ARG_USERPTR } };
static kernelArgInfo args_filesystemFormat[] =
	{ { 1, type_ptr, API_ARG_NONNULLPTR | API_ARG_USERPTR },
		{ 1, type_ptr, API_ARG_NONNULLPTR | API_ARG_USERPTR },
		{ 1, type_ptr, API_ARG_NONNULLPTR | API_ARG_USERPTR },
		{ 1, type_val, API_ARG_ANYVAL },
		{ 1, type_ptr, API_ARG_USERPTR } };
static kernelArgInfo args_filesystemClobber[] =
	{ { 1, type_ptr, API_ARG_NONNULLPTR | API_ARG_USERPTR } };
static kernelArgInfo args_filesystemCheck[] =
	{ { 1, type_ptr, API_ARG_NONNULLPTR | API_ARG_USERPTR },
		{ 1, type_val, API_ARG_ANYVAL },
		{ 1, type_val, API_ARG_ANYVAL },
		{ 1, type_ptr, API_ARG_USERPTR } };
static kernelArgInfo args_filesystemDefragment[] =
	{ { 1, type_ptr, API_ARG_NONNULLPTR | API_ARG_USERPTR },
		{ 1, type_ptr, API_ARG_NONNULLPTR | API_ARG_USERPTR } };
static kernelArgInfo args_filesystemResizeConstraints[] =
	{ { 1, type_ptr, API_ARG_NONNULLPTR | API_ARG_USERPTR },
		{ 1, type_ptr, API_ARG_NONNULLPTR | API_ARG_USERPTR },
		{ 1, type_ptr, API_ARG_NONNULLPTR | API_ARG_USERPTR },
		{ 1, type_ptr, API_ARG_USERPTR } };
static kernelArgInfo args_filesystemResize[] =
	{ { 1, type_ptr, API_ARG_NONNULLPTR | API_ARG_USERPTR },
		{ 2, type_val, API_ARG_ANYVAL },
		{ 1, type_ptr, API_ARG_USERPTR } };
static kernelArgInfo args_filesystemMount[] =
	{ { 1, type_ptr, API_ARG_NONNULLPTR | API_ARG_USERPTR },
		{ 1, type_ptr, API_ARG_NONNULLPTR | API_ARG_USERPTR } };
static kernelArgInfo args_filesystemUnmount[] =
	{ { 1, type_ptr, API_ARG_NONNULLPTR | API_ARG_USERPTR } };
static kernelArgInfo args_filesystemGetFreeBytes[] =
	{ { 1, type_ptr, API_ARG_NONNULLPTR | API_ARG_USERPTR } };
static kernelArgInfo args_filesystemGetBlockSize[] =
	{ { 1, type_ptr, API_ARG_NONNULLPTR | API_ARG_USERPTR } };

static kernelFunctionIndex filesystemFunctionIndex[] = {
	{ _fnum_filesystemScan, kernelFilesystemScan,
		PRIVILEGE_SUPERVISOR, 1, args_filesystemScan, type_val },
	{ _fnum_filesystemFormat, kernelFilesystemFormat,
		PRIVILEGE_SUPERVISOR, 5, args_filesystemFormat, type_val },
	{ _fnum_filesystemClobber, kernelFilesystemClobber,
		PRIVILEGE_SUPERVISOR, 1, args_filesystemClobber, type_val },
	{ _fnum_filesystemCheck, kernelFilesystemCheck,
		PRIVILEGE_USER, 4, args_filesystemCheck, type_val },
	{ _fnum_filesystemDefragment, kernelFilesystemDefragment,
		PRIVILEGE_SUPERVISOR, 2, args_filesystemDefragment, type_val },
	{ _fnum_filesystemResizeConstraints, kernelFilesystemResizeConstraints,
		PRIVILEGE_USER, 4, args_filesystemResizeConstraints, type_val },
	{ _fnum_filesystemResize, kernelFilesystemResize,
		PRIVILEGE_SUPERVISOR, 3, args_filesystemResize, type_val },
	{ _fnum_filesystemMount, kernelFilesystemMount,
		PRIVILEGE_USER, 2, args_filesystemMount, type_val },
	{ _fnum_filesystemUnmount, kernelFilesystemUnmount,
		PRIVILEGE_USER, 1, args_filesystemUnmount, type_val },
	{ _fnum_filesystemGetFreeBytes, kernelFilesystemGetFreeBytes,
		PRIVILEGE_USER, 1, args_filesystemGetFreeBytes, type_val },
	{ _fnum_filesystemGetBlockSize, kernelFilesystemGetBlockSize,
		PRIVILEGE_USER, 1, args_filesystemGetBlockSize, type_val }
};

// File functions (0x4000-0x4FFF range)

static kernelArgInfo args_fileFixupPath[] =
	{ { 1, type_ptr, API_ARG_NONNULLPTR | API_ARG_USERPTR },
		{ 1, type_ptr, API_ARG_NONNULLPTR | API_ARG_USERPTR } };
static kernelArgInfo args_fileGetDisk[] =
	{ { 1, type_ptr, API_ARG_NONNULLPTR | API_ARG_USERPTR },
		{ 1, type_ptr, API_ARG_NONNULLPTR | API_ARG_USERPTR } };
static kernelArgInfo args_fileCount[] =
	{ { 1, type_ptr, API_ARG_NONNULLPTR | API_ARG_USERPTR } };
static kernelArgInfo args_fileFirst[] =
	{ { 1, type_ptr, API_ARG_NONNULLPTR | API_ARG_USERPTR },
		{ 1, type_ptr, API_ARG_NONNULLPTR | API_ARG_USERPTR } };
static kernelArgInfo args_fileNext[] =
	{ { 1, type_ptr, API_ARG_NONNULLPTR | API_ARG_USERPTR },
		{ 1, type_ptr, API_ARG_NONNULLPTR | API_ARG_USERPTR } };
static kernelArgInfo args_fileFind[] =
	{ { 1, type_ptr, API_ARG_NONNULLPTR | API_ARG_USERPTR },
		{ 1, type_ptr, API_ARG_USERPTR } };
static kernelArgInfo args_fileOpen[] =
	{ { 1, type_ptr, API_ARG_NONNULLPTR | API_ARG_USERPTR },
		{ 1, type_val, API_ARG_ANYVAL },
		{ 1, type_ptr, API_ARG_NONNULLPTR | API_ARG_USERPTR } };
static kernelArgInfo args_fileClose[] =
	{ { 1, type_ptr, API_ARG_NONNULLPTR | API_ARG_USERPTR } };
static kernelArgInfo args_fileRead[] =
	{ { 1, type_ptr, API_ARG_NONNULLPTR | API_ARG_USERPTR },
		{ 1, type_val, API_ARG_ANYVAL },
		{ 1, type_val, API_ARG_ANYVAL },
		{ 1, type_ptr, API_ARG_NONNULLPTR | API_ARG_USERPTR } };
static kernelArgInfo args_fileWrite[] =
	{ { 1, type_ptr, API_ARG_NONNULLPTR | API_ARG_USERPTR },
		{ 1, type_val, API_ARG_ANYVAL },
		{ 1, type_val, API_ARG_ANYVAL },
		{ 1, type_ptr, API_ARG_NONNULLPTR | API_ARG_USERPTR } };
static kernelArgInfo args_fileDelete[] =
	{ { 1, type_ptr, API_ARG_NONNULLPTR | API_ARG_USERPTR } };
static kernelArgInfo args_fileDeleteRecursive[] =
	{ { 1, type_ptr, API_ARG_NONNULLPTR | API_ARG_USERPTR } };
static kernelArgInfo args_fileDeleteSecure[] =
	{ { 1, type_ptr, API_ARG_NONNULLPTR | API_ARG_USERPTR },
		{ 1, type_val, API_ARG_ANYVAL } };
static kernelArgInfo args_fileMakeDir[] =
	{ { 1, type_ptr, API_ARG_NONNULLPTR | API_ARG_USERPTR } };
static kernelArgInfo args_fileRemoveDir[] =
	{ { 1, type_ptr, API_ARG_NONNULLPTR | API_ARG_USERPTR } };
static kernelArgInfo args_fileCopy[] =
	{ { 1, type_ptr, API_ARG_NONNULLPTR | API_ARG_USERPTR },
		{ 1, type_ptr, API_ARG_NONNULLPTR | API_ARG_USERPTR } };
static kernelArgInfo args_fileCopyRecursive[] =
	{ { 1, type_ptr, API_ARG_NONNULLPTR | API_ARG_USERPTR },
		{ 1, type_ptr, API_ARG_NONNULLPTR | API_ARG_USERPTR } };
static kernelArgInfo args_fileMove[] =
	{ { 1, type_ptr, API_ARG_NONNULLPTR | API_ARG_USERPTR },
		{ 1, type_ptr, API_ARG_NONNULLPTR | API_ARG_USERPTR } };
static kernelArgInfo args_fileTimestamp[] =
	{ { 1, type_ptr, API_ARG_NONNULLPTR | API_ARG_USERPTR } };
static kernelArgInfo args_fileSetSize[] =
	{ { 1, type_ptr, API_ARG_NONNULLPTR | API_ARG_USERPTR },
		{ 1, type_val, API_ARG_ANYVAL } };
static kernelArgInfo args_fileGetTempName[] =
	{ { 1, type_ptr, API_ARG_NONNULLPTR | API_ARG_USERPTR },
		{ 1, type_val, API_ARG_POSINTVAL } };
static kernelArgInfo args_fileGetTemp[] =
	{ { 1, type_ptr, API_ARG_NONNULLPTR | API_ARG_USERPTR } };
static kernelArgInfo args_fileGetFullPath[] =
	{ { 1, type_ptr, API_ARG_NONNULLPTR | API_ARG_USERPTR },
		{ 1, type_val, API_ARG_NONNULLPTR | API_ARG_USERPTR },
		{ 1, type_val, API_ARG_POSINTVAL } };
static kernelArgInfo args_fileStreamOpen[] =
	{ { 1, type_ptr, API_ARG_NONNULLPTR | API_ARG_USERPTR },
		{ 1, type_val, API_ARG_ANYVAL },
		{ 1, type_ptr, API_ARG_NONNULLPTR | API_ARG_USERPTR } };
static kernelArgInfo args_fileStreamSeek[] =
	{ { 1, type_ptr, API_ARG_NONNULLPTR | API_ARG_USERPTR },
		{ 1, type_val, API_ARG_ANYVAL } };
static kernelArgInfo args_fileStreamRead[] =
	{ { 1, type_ptr, API_ARG_NONNULLPTR | API_ARG_USERPTR },
		{ 1, type_val, API_ARG_ANYVAL },
		{ 1, type_ptr, API_ARG_NONNULLPTR | API_ARG_USERPTR } };
static kernelArgInfo args_fileStreamReadLine[] =
	{ { 1, type_ptr, API_ARG_NONNULLPTR | API_ARG_USERPTR },
		{ 1, type_val, API_ARG_ANYVAL },
		{ 1, type_ptr, API_ARG_NONNULLPTR | API_ARG_USERPTR } };
static kernelArgInfo args_fileStreamWrite[] =
	{ { 1, type_ptr, API_ARG_NONNULLPTR | API_ARG_USERPTR },
		{ 1, type_val, API_ARG_ANYVAL },
		{ 1, type_ptr, API_ARG_NONNULLPTR | API_ARG_USERPTR } };
static kernelArgInfo args_fileStreamWriteStr[] =
	{ { 1, type_ptr, API_ARG_NONNULLPTR | API_ARG_USERPTR },
		{ 1, type_ptr, API_ARG_NONNULLPTR | API_ARG_USERPTR } };
static kernelArgInfo args_fileStreamWriteLine[] =
	{ { 1, type_ptr, API_ARG_NONNULLPTR | API_ARG_USERPTR },
		{ 1, type_ptr, API_ARG_NONNULLPTR | API_ARG_USERPTR } };
static kernelArgInfo args_fileStreamFlush[] =
	{ { 1, type_ptr, API_ARG_NONNULLPTR | API_ARG_USERPTR } };
static kernelArgInfo args_fileStreamClose[] =
	{ { 1, type_ptr, API_ARG_NONNULLPTR | API_ARG_USERPTR } };
static kernelArgInfo args_fileStreamGetTemp[] =
	{ { 1, type_ptr, API_ARG_NONNULLPTR | API_ARG_USERPTR } };

static kernelFunctionIndex fileFunctionIndex[] = {
	{ _fnum_fileFixupPath, kernelFileFixupPath,
		PRIVILEGE_USER, 2, args_fileFixupPath, type_val },
	{ _fnum_fileGetDisk, kernelFileGetDisk,
		PRIVILEGE_USER, 2, args_fileGetDisk, type_val },
	{ _fnum_fileCount, kernelFileCount,
		PRIVILEGE_USER, 1, args_fileCount, type_val },
	{ _fnum_fileFirst, kernelFileFirst,
		PRIVILEGE_USER, 2, args_fileFirst, type_val },
	{ _fnum_fileNext, kernelFileNext,
		PRIVILEGE_USER, 2, args_fileNext, type_val },
	{ _fnum_fileFind, kernelFileFind,
		PRIVILEGE_USER, 2, args_fileFind, type_val },
	{ _fnum_fileOpen, kernelFileOpen,
		PRIVILEGE_USER, 3, args_fileOpen, type_val },
	{ _fnum_fileClose, kernelFileClose,
		PRIVILEGE_USER, 1, args_fileClose, type_val },
	{ _fnum_fileRead, kernelFileRead,
		PRIVILEGE_USER, 4, args_fileRead, type_val },
	{ _fnum_fileWrite, kernelFileWrite,
		PRIVILEGE_USER, 4, args_fileWrite, type_val },
	{ _fnum_fileDelete, kernelFileDelete,
		PRIVILEGE_USER, 1, args_fileDelete, type_val },
	{ _fnum_fileDeleteRecursive, kernelFileDeleteRecursive,
		PRIVILEGE_USER, 1, args_fileDeleteRecursive, type_val },
	{ _fnum_fileDeleteSecure, kernelFileDeleteSecure,
		PRIVILEGE_USER, 2, args_fileDeleteSecure, type_val },
	{ _fnum_fileMakeDir, kernelFileMakeDir,
		PRIVILEGE_USER, 1, args_fileMakeDir, type_val },
	{ _fnum_fileRemoveDir, kernelFileRemoveDir,
		PRIVILEGE_USER, 1, args_fileRemoveDir, type_val },
	{ _fnum_fileCopy, kernelFileCopy,
		PRIVILEGE_USER, 2, args_fileCopy, type_val },
	{ _fnum_fileCopyRecursive, kernelFileCopyRecursive,
		PRIVILEGE_USER, 2, args_fileCopyRecursive, type_val },
	{ _fnum_fileMove, kernelFileMove,
		PRIVILEGE_USER, 2, args_fileMove, type_val },
	{ _fnum_fileTimestamp, kernelFileTimestamp,
		PRIVILEGE_USER, 1, args_fileTimestamp, type_val },
	{ _fnum_fileSetSize, kernelFileSetSize,
		PRIVILEGE_USER, 2, args_fileSetSize, type_val },
	{ _fnum_fileGetTempName, kernelFileGetTempName,
		PRIVILEGE_USER, 2, args_fileGetTempName, type_val },
	{ _fnum_fileGetTemp, kernelFileGetTemp,
		PRIVILEGE_USER, 1, args_fileGetTemp, type_val },
	{ _fnum_fileGetFullPath, kernelFileGetFullPath,
		PRIVILEGE_USER, 3, args_fileGetFullPath, type_val },
	{ _fnum_fileStreamOpen, kernelFileStreamOpen,
		PRIVILEGE_USER, 3, args_fileStreamOpen, type_val },
	{ _fnum_fileStreamSeek, kernelFileStreamSeek,
		PRIVILEGE_USER, 2, args_fileStreamSeek, type_val },
	{ _fnum_fileStreamRead, kernelFileStreamRead,
		PRIVILEGE_USER, 3, args_fileStreamRead, type_val },
	{ _fnum_fileStreamReadLine, kernelFileStreamReadLine,
		PRIVILEGE_USER, 3, args_fileStreamReadLine, type_val },
	{ _fnum_fileStreamWrite, kernelFileStreamWrite,
		PRIVILEGE_USER, 3, args_fileStreamWrite, type_val },
	{ _fnum_fileStreamWriteStr, kernelFileStreamWriteStr,
		PRIVILEGE_USER, 2, args_fileStreamWriteStr, type_val },
	{ _fnum_fileStreamWriteLine, kernelFileStreamWriteLine,
		PRIVILEGE_USER, 2, args_fileStreamWriteLine, type_val },
	{ _fnum_fileStreamFlush, kernelFileStreamFlush,
		PRIVILEGE_USER, 1, args_fileStreamFlush, type_val },
	{ _fnum_fileStreamClose, kernelFileStreamClose,
		PRIVILEGE_USER, 1, args_fileStreamClose, type_val },
	{ _fnum_fileStreamGetTemp, kernelFileStreamGetTemp,
		PRIVILEGE_USER, 1, args_fileStreamGetTemp, type_val }
};

// Memory manager functions (0x5000-0x5FFF range)

static kernelArgInfo args_memoryGet[] =
	{ { 1, type_val, API_ARG_ANYVAL },
		{ 1, type_ptr, API_ARG_NONNULLPTR | API_ARG_USERPTR } };
static kernelArgInfo args_memoryRelease[] =
	{ { 1, type_ptr, API_ARG_NONNULLPTR | API_ARG_USERPTR } };
static kernelArgInfo args_memoryReleaseAllByProcId[] =
	{ { 1, type_val, API_ARG_ANYVAL } };
static kernelArgInfo args_memoryGetStats[] =
	{ { 1, type_ptr, API_ARG_NONNULLPTR | API_ARG_USERPTR },
		{ 1, type_val, API_ARG_ANYVAL } };
static kernelArgInfo args_memoryGetBlocks[] =
	{ { 1, type_ptr, API_ARG_NONNULLPTR | API_ARG_USERPTR },
		{ 1, type_val, API_ARG_ANYVAL },
		{ 1, type_val, API_ARG_ANYVAL } };
static kernelArgInfo args_memoryBlockInfo[] =
	{ { 1, type_ptr, API_ARG_NONNULLPTR | API_ARG_USERPTR },
		{ 1, type_ptr, API_ARG_NONNULLPTR | API_ARG_USERPTR } };

static kernelFunctionIndex memoryFunctionIndex[] = {
	{ _fnum_memoryGet, kernelMemoryGet,
		PRIVILEGE_USER, 2, args_memoryGet, type_ptr },
	{ _fnum_memoryRelease, kernelMemoryRelease,
		PRIVILEGE_USER, 1, args_memoryRelease, type_val },
	{ _fnum_memoryReleaseAllByProcId, kernelMemoryReleaseAllByProcId,
		PRIVILEGE_USER, 1, args_memoryReleaseAllByProcId, type_val },
	{ _fnum_memoryGetStats, kernelMemoryGetStats,
		PRIVILEGE_USER, 2, args_memoryGetStats, type_val },
	{ _fnum_memoryGetBlocks, kernelMemoryGetBlocks,
		PRIVILEGE_USER, 3, args_memoryGetBlocks, type_val },
	{ _fnum_memoryBlockInfo, kernelMemoryBlockInfo,
		PRIVILEGE_USER, 2, args_memoryBlockInfo, type_val }
};

// Multitasker functions (0x6000-0x6FFF range)

static kernelArgInfo args_multitaskerCreateProcess[] =
	{ { 1, type_ptr, API_ARG_NONNULLPTR | API_ARG_USERPTR },
		{ 1, type_val, API_ARG_ANYVAL },
		{ 1, type_ptr, API_ARG_NONNULLPTR | API_ARG_USERPTR } };
static kernelArgInfo args_multitaskerSpawn[] =
	{ { 1, type_ptr, API_ARG_NONNULLPTR | API_ARG_USERPTR },
		{ 1, type_ptr, API_ARG_NONNULLPTR | API_ARG_USERPTR },
		{ 1, type_val, API_ARG_ANYVAL },
		{ 1, type_ptr, API_ARG_USERPTR } };
static kernelArgInfo args_multitaskerGetProcess[] =
	{ { 1, type_val, API_ARG_ANYVAL },
		{ 1, type_ptr, API_ARG_NONNULLPTR | API_ARG_USERPTR } };
static kernelArgInfo args_multitaskerGetProcessByName[] =
	{ { 1, type_ptr, API_ARG_NONNULLPTR | API_ARG_USERPTR },
		{ 1, type_ptr, API_ARG_NONNULLPTR | API_ARG_USERPTR } };
static kernelArgInfo args_multitaskerGetProcesses[] =
	{ { 1, type_ptr, API_ARG_NONNULLPTR | API_ARG_USERPTR },
		{ 1, type_val, API_ARG_ANYVAL } };
static kernelArgInfo args_multitaskerSetProcessState[] =
	{ { 1, type_val, API_ARG_ANYVAL },
		{ 1, type_val, API_ARG_ANYVAL } };
static kernelArgInfo args_multitaskerProcessIsAlive[] =
	{ { 1, type_val, API_ARG_ANYVAL } };
static kernelArgInfo args_multitaskerSetProcessPriority[] =
	{ { 1, type_val, API_ARG_ANYVAL },
		{ 1, type_val, API_ARG_ANYVAL } };
static kernelArgInfo args_multitaskerGetProcessPrivilege[] =
	{ { 1, type_val, API_ARG_ANYVAL } };
static kernelArgInfo args_multitaskerGetCurrentDirectory[] =
	{ { 1, type_ptr, API_ARG_NONNULLPTR | API_ARG_USERPTR },
		{ 1, type_val, API_ARG_ANYVAL } };
static kernelArgInfo args_multitaskerSetCurrentDirectory[] =
	{ { 1, type_ptr, API_ARG_NONNULLPTR | API_ARG_USERPTR } };
static kernelArgInfo args_multitaskerSetTextInput[] =
	{ { 1, type_val, API_ARG_ANYVAL },
		{ 1, type_ptr, API_ARG_NONNULLPTR | API_ARG_KERNPTR } };
static kernelArgInfo args_multitaskerSetTextOutput[] =
	{ { 1, type_val, API_ARG_ANYVAL },
		{ 1, type_ptr, API_ARG_ANYPTR | API_ARG_KERNPTR } };
static kernelArgInfo args_multitaskerDuplicateIO[] =
	{ { 1, type_val, API_ARG_ANYVAL },
		{ 1, type_val, API_ARG_ANYVAL },
		{ 1, type_val, API_ARG_ANYVAL } };
static kernelArgInfo args_multitaskerGetProcessorTime[] =
	{ { 1, type_ptr, API_ARG_NONNULLPTR | API_ARG_USERPTR } };
static kernelArgInfo args_multitaskerWait[] =
	{ { 1, type_val, API_ARG_ANYVAL } };
static kernelArgInfo args_multitaskerBlock[] =
	{ { 1, type_val, API_ARG_ANYVAL } };
static kernelArgInfo args_multitaskerKillProcess[] =
	{ { 1, type_val, API_ARG_ANYVAL },
		{ 1, type_val, API_ARG_ANYVAL } };
static kernelArgInfo args_multitaskerKillByName[] =
	{ { 1, type_ptr, API_ARG_NONNULLPTR | API_ARG_USERPTR },
		{ 1, type_val, API_ARG_ANYVAL } };
static kernelArgInfo args_multitaskerTerminate[] =
	{ { 1, type_val, API_ARG_ANYVAL } };
static kernelArgInfo args_multitaskerSignalSet[] =
	{ { 1, type_val, API_ARG_ANYVAL },
		{ 1, type_val, API_ARG_ANYVAL },
		{ 1, type_val, API_ARG_ANYVAL } };
static kernelArgInfo args_multitaskerSignal[] =
	{ { 1, type_val, API_ARG_ANYVAL },
		{ 1, type_val, API_ARG_ANYVAL } };
static kernelArgInfo args_multitaskerSignalRead[] =
	{ { 1, type_val, API_ARG_ANYVAL } };
static kernelArgInfo args_multitaskerGetIOPerm[] =
	{ { 1, type_val, API_ARG_ANYVAL },
		{ 1, type_val, API_ARG_ANYVAL } };
static kernelArgInfo args_multitaskerSetIOPerm[] =
	{ { 1, type_val, API_ARG_ANYVAL },
		{ 1, type_val, API_ARG_ANYVAL },
		{ 1, type_val, API_ARG_ANYVAL } };
static kernelArgInfo args_multitaskerStackTrace[] =
	{ { 1, type_val, API_ARG_ANYVAL } };

static kernelFunctionIndex multitaskerFunctionIndex[] = {
	{ _fnum_multitaskerCreateProcess, kernelMultitaskerCreateProcess,
		PRIVILEGE_USER, 3, args_multitaskerCreateProcess, type_val },
	{ _fnum_multitaskerSpawn, kernelMultitaskerSpawn,
		PRIVILEGE_USER, 4, args_multitaskerSpawn, type_val },
	{ _fnum_multitaskerGetCurrentProcessId, kernelMultitaskerGetCurrentProcessId,
		PRIVILEGE_USER, 0, NULL, type_val },
	{ _fnum_multitaskerGetProcess, kernelMultitaskerGetProcess,
		PRIVILEGE_USER, 2, args_multitaskerGetProcess, type_val },
	{ _fnum_multitaskerGetProcessByName, kernelMultitaskerGetProcessByName,
		PRIVILEGE_USER, 2, args_multitaskerGetProcessByName, type_val },
	{ _fnum_multitaskerGetProcesses, kernelMultitaskerGetProcesses,
		PRIVILEGE_USER, 2, args_multitaskerGetProcesses, type_val },
	{ _fnum_multitaskerSetProcessState, kernelMultitaskerSetProcessState,
		PRIVILEGE_USER, 2, args_multitaskerSetProcessState, type_val },
	{ _fnum_multitaskerProcessIsAlive, kernelMultitaskerProcessIsAlive,
		PRIVILEGE_USER, 1, args_multitaskerProcessIsAlive, type_val },
	{ _fnum_multitaskerSetProcessPriority, kernelMultitaskerSetProcessPriority,
		PRIVILEGE_USER, 2, args_multitaskerSetProcessPriority, type_val },
	{ _fnum_multitaskerGetProcessPrivilege, kernelMultitaskerGetProcessPrivilege,
		PRIVILEGE_USER, 1, args_multitaskerGetProcessPrivilege, type_val },
	{ _fnum_multitaskerGetCurrentDirectory, kernelMultitaskerGetCurrentDirectory,
		PRIVILEGE_USER, 2, args_multitaskerGetCurrentDirectory, type_val },
	{ _fnum_multitaskerSetCurrentDirectory, kernelMultitaskerSetCurrentDirectory,
		PRIVILEGE_USER, 1, args_multitaskerSetCurrentDirectory, type_val },
	{ _fnum_multitaskerGetTextInput, kernelMultitaskerGetTextInput,
		PRIVILEGE_USER, 0, NULL, type_ptr },
	{ _fnum_multitaskerSetTextInput, kernelMultitaskerSetTextInput,
		PRIVILEGE_USER, 2, args_multitaskerSetTextInput, type_val },
	{ _fnum_multitaskerGetTextOutput, kernelMultitaskerGetTextOutput,
		PRIVILEGE_USER, 0, NULL, type_ptr },
	{ _fnum_multitaskerSetTextOutput, kernelMultitaskerSetTextOutput,
		PRIVILEGE_USER, 2, args_multitaskerSetTextOutput, type_val },
	{ _fnum_multitaskerDuplicateIO, kernelMultitaskerDuplicateIO,
		PRIVILEGE_USER, 3, args_multitaskerDuplicateIO, type_val },
	{ _fnum_multitaskerGetProcessorTime, kernelMultitaskerGetProcessorTime,
		PRIVILEGE_USER, 1, args_multitaskerGetProcessorTime, type_val },
	{ _fnum_multitaskerYield, kernelMultitaskerYield,
		PRIVILEGE_USER, 0, NULL, type_void },
	{ _fnum_multitaskerWait, kernelMultitaskerWait,
		PRIVILEGE_USER, 1, args_multitaskerWait, type_void },
	{ _fnum_multitaskerBlock, kernelMultitaskerBlock,
		PRIVILEGE_USER, 1, args_multitaskerBlock, type_val },
	{ _fnum_multitaskerDetach, kernelMultitaskerDetach,
		PRIVILEGE_USER, 0, NULL, type_val },
	{ _fnum_multitaskerKillProcess, kernelMultitaskerKillProcess,
		PRIVILEGE_USER, 2, args_multitaskerKillProcess, type_val },
	{ _fnum_multitaskerKillByName, kernelMultitaskerKillByName,
		PRIVILEGE_USER, 2, args_multitaskerKillByName, type_val },
	{ _fnum_multitaskerTerminate, kernelMultitaskerTerminate,
		PRIVILEGE_USER, 1, args_multitaskerTerminate, type_val },
	{ _fnum_multitaskerSignalSet, kernelMultitaskerSignalSet,
		PRIVILEGE_USER, 3, args_multitaskerSignalSet, type_val },
	{ _fnum_multitaskerSignal, kernelMultitaskerSignal,
		PRIVILEGE_USER, 2, args_multitaskerSignal, type_val },
	{ _fnum_multitaskerSignalRead, kernelMultitaskerSignalRead,
		PRIVILEGE_USER, 1, args_multitaskerSignalRead, type_val },
	{ _fnum_multitaskerGetIOPerm, kernelMultitaskerGetIOPerm,
		PRIVILEGE_USER, 2, args_multitaskerGetIOPerm, type_val },
	{ _fnum_multitaskerSetIOPerm, kernelMultitaskerSetIOPerm,
		PRIVILEGE_SUPERVISOR, 3, args_multitaskerSetIOPerm, type_val },
	{ _fnum_multitaskerStackTrace, kernelMultitaskerStackTrace,
		PRIVILEGE_USER, 1, args_multitaskerStackTrace, type_val }
};

// Loader functions (0x7000-0x7FFF range)

static kernelArgInfo args_loaderLoad[] =
	{ { 1, type_ptr, API_ARG_NONNULLPTR | API_ARG_USERPTR },
		{ 1, type_ptr, API_ARG_NONNULLPTR | API_ARG_USERPTR } };
static kernelArgInfo args_loaderClassify[] =
	{ { 1, type_ptr, API_ARG_NONNULLPTR | API_ARG_USERPTR },
		{ 1, type_ptr, API_ARG_NONNULLPTR | API_ARG_USERPTR },
		{ 1, type_val, API_ARG_POSINTVAL },
		{ 1, type_ptr, API_ARG_NONNULLPTR | API_ARG_USERPTR } };
static kernelArgInfo args_loaderClassifyFile[] =
	{ { 1, type_ptr, API_ARG_NONNULLPTR | API_ARG_USERPTR },
		{ 1, type_ptr, API_ARG_NONNULLPTR | API_ARG_USERPTR } };
static kernelArgInfo args_loaderGetSymbols[] =
	{ { 1, type_ptr, API_ARG_NONNULLPTR | API_ARG_USERPTR } };
static kernelArgInfo args_loaderCheckCommand[] =
	{ { 1, type_ptr, API_ARG_NONNULLPTR | API_ARG_USERPTR } };
static kernelArgInfo args_loaderLoadProgram[] =
	{ { 1, type_ptr, API_ARG_NONNULLPTR | API_ARG_USERPTR },
		{ 1, type_val, API_ARG_ANYVAL } };
static kernelArgInfo args_loaderLoadLibrary[] =
	{ { 1, type_ptr, API_ARG_NONNULLPTR | API_ARG_USERPTR } };
static kernelArgInfo args_loaderGetLibrary[] =
	{ { 1, type_ptr, API_ARG_NONNULLPTR | API_ARG_USERPTR } };
static kernelArgInfo args_loaderLinkLibrary[] =
	{ { 1, type_ptr, API_ARG_NONNULLPTR | API_ARG_USERPTR } };
static kernelArgInfo args_loaderGetSymbol[] =
	{ { 1, type_ptr, API_ARG_NONNULLPTR | API_ARG_USERPTR } };
static kernelArgInfo args_loaderExecProgram[] =
	{ { 1, type_val, API_ARG_ANYVAL },
		{ 1, type_val, API_ARG_ANYVAL } };
static kernelArgInfo args_loaderLoadAndExec[] =
	{ { 1, type_ptr, API_ARG_NONNULLPTR | API_ARG_USERPTR },
		{ 1, type_val, API_ARG_ANYVAL },
		{ 1, type_val, API_ARG_ANYVAL } };

static kernelFunctionIndex loaderFunctionIndex[] = {
	{ _fnum_loaderLoad, kernelLoaderLoad,
		PRIVILEGE_USER, 2, args_loaderLoad, type_void },
	{ _fnum_loaderClassify, kernelLoaderClassify,
		PRIVILEGE_USER, 4, args_loaderClassify, type_ptr },
	{ _fnum_loaderClassifyFile, kernelLoaderClassifyFile,
		PRIVILEGE_USER, 2, args_loaderClassifyFile, type_ptr },
	{ _fnum_loaderGetSymbols, kernelLoaderGetSymbols,
		PRIVILEGE_USER, 1, args_loaderGetSymbols, type_ptr },
	{ _fnum_loaderCheckCommand, kernelLoaderCheckCommand,
		PRIVILEGE_USER, 1, args_loaderCheckCommand, type_val },
	{ _fnum_loaderLoadProgram, kernelLoaderLoadProgram,
		PRIVILEGE_USER, 2, args_loaderLoadProgram, type_val },
	{ _fnum_loaderLoadLibrary, kernelLoaderLoadLibrary,
		PRIVILEGE_USER, 1, args_loaderLoadLibrary, type_val },
	{ _fnum_loaderGetLibrary, kernelLoaderGetLibrary,
		PRIVILEGE_USER, 1, args_loaderGetLibrary, type_ptr },
	{ _fnum_loaderLinkLibrary, kernelLoaderLinkLibrary,
		PRIVILEGE_USER, 1, args_loaderLinkLibrary, type_ptr },
	{ _fnum_loaderGetSymbol, kernelLoaderGetSymbol,
		PRIVILEGE_USER, 1, args_loaderGetSymbol, type_ptr },
	{ _fnum_loaderExecProgram, kernelLoaderExecProgram,
		PRIVILEGE_USER, 2, args_loaderExecProgram, type_val },
	{ _fnum_loaderLoadAndExec, kernelLoaderLoadAndExec,
		PRIVILEGE_USER, 3, args_loaderLoadAndExec, type_val }
};

// Real-time clock functions (0x8000-0x8FFF range)

static kernelArgInfo args_rtcDayOfWeek[] =
	{ { 1, type_val, API_ARG_ANYVAL },
		{ 1, type_val, API_ARG_ANYVAL },
		{ 1, type_val, API_ARG_ANYVAL } };
static kernelArgInfo args_rtcDateTime[] =
	{ { 1, type_ptr, API_ARG_NONNULLPTR | API_ARG_USERPTR } };

static kernelFunctionIndex rtcFunctionIndex[] = {
	{ _fnum_rtcReadSeconds, kernelRtcReadSeconds,
		PRIVILEGE_USER, 0, NULL, type_val },
	{ _fnum_rtcReadMinutes, kernelRtcReadMinutes,
		PRIVILEGE_USER, 0, NULL, type_val },
	{ _fnum_rtcReadHours, kernelRtcReadHours,
		PRIVILEGE_USER, 0, NULL, type_val },
	{ _fnum_rtcDayOfWeek, kernelRtcDayOfWeek,
		PRIVILEGE_USER, 3, args_rtcDayOfWeek, type_val },
	{ _fnum_rtcReadDayOfMonth, kernelRtcReadDayOfMonth,
		PRIVILEGE_USER, 0, NULL, type_val },
	{ _fnum_rtcReadMonth, kernelRtcReadMonth,
		PRIVILEGE_USER, 0, NULL, type_val },
	{ _fnum_rtcReadYear, kernelRtcReadYear,
		PRIVILEGE_USER, 0, NULL, type_val },
	{ _fnum_rtcUptimeSeconds, kernelRtcUptimeSeconds,
		PRIVILEGE_USER, 0, NULL, type_val },
	{ _fnum_rtcDateTime, kernelRtcDateTime,
		PRIVILEGE_USER, 1, args_rtcDateTime, type_val }
};

// Random number functions (0x9000-0x9FFF range)

static kernelArgInfo args_randomFormatted[] =
	{ { 1, type_val, API_ARG_ANYVAL },
		{ 1, type_val, API_ARG_ANYVAL } };
static kernelArgInfo args_randomSeededUnformatted[] =
	{ { 1, type_val, API_ARG_ANYVAL } };
static kernelArgInfo args_randomSeededFormatted[] =
	{ { 1, type_val, API_ARG_ANYVAL },
		{ 1, type_val, API_ARG_ANYVAL },
		{ 1, type_val, API_ARG_ANYVAL } };
static kernelArgInfo args_randomBytes[] =
	{ { 1, type_ptr, API_ARG_NONNULLPTR | API_ARG_USERPTR },
		{ 1, type_val, API_ARG_ANYVAL } };

static kernelFunctionIndex randomFunctionIndex[] = {
	{ _fnum_randomUnformatted, kernelRandomUnformatted,
		PRIVILEGE_USER, 0, NULL, type_val },
	{ _fnum_randomFormatted, kernelRandomFormatted,
		PRIVILEGE_USER, 2, args_randomFormatted, type_val },
	{ _fnum_randomSeededUnformatted, kernelRandomSeededUnformatted,
		PRIVILEGE_USER, 1, args_randomSeededUnformatted, type_val },
	{ _fnum_randomSeededFormatted, kernelRandomSeededFormatted,
		PRIVILEGE_USER, 3, args_randomSeededFormatted, type_val },
	{ _fnum_randomBytes, kernelRandomBytes,
		PRIVILEGE_USER, 2, args_randomBytes, type_void }
};

// Variable list functions (0xA000-0xAFFF range)
static kernelArgInfo args_variableListCreate[] =
	{ { 1, type_ptr, API_ARG_NONNULLPTR | API_ARG_USERPTR } };
static kernelArgInfo args_variableListDestroy[] =
	{ { 1, type_ptr, API_ARG_NONNULLPTR | API_ARG_USERPTR } };
static kernelArgInfo args_variableListGetVariable[] =
	{ { 1, type_ptr, API_ARG_NONNULLPTR | API_ARG_USERPTR },
		{ 1, type_val, API_ARG_POSINTVAL } };
static kernelArgInfo args_variableListGet[] =
	{ { 1, type_ptr, API_ARG_NONNULLPTR | API_ARG_USERPTR },
		{ 1, type_ptr, API_ARG_NONNULLPTR | API_ARG_USERPTR } };
static kernelArgInfo args_variableListSet[] =
	{ { 1, type_ptr, API_ARG_NONNULLPTR | API_ARG_USERPTR },
		{ 1, type_ptr, API_ARG_NONNULLPTR | API_ARG_USERPTR },
		{ 1, type_ptr, API_ARG_NONNULLPTR | API_ARG_USERPTR } };
static kernelArgInfo args_variableListUnset[] =
	{ { 1, type_ptr, API_ARG_NONNULLPTR | API_ARG_USERPTR },
		{ 1, type_ptr, API_ARG_NONNULLPTR | API_ARG_USERPTR } };

static kernelFunctionIndex variableListFunctionIndex[] = {
	{ _fnum_variableListCreate, kernelVariableListCreate,
		PRIVILEGE_USER, 1, args_variableListCreate, type_val },
	{ _fnum_variableListDestroy, kernelVariableListDestroy,
		PRIVILEGE_USER, 1, args_variableListDestroy, type_val },
	{ _fnum_variableListGetVariable, kernelVariableListGetVariable,
		PRIVILEGE_USER, 2, args_variableListGetVariable, type_ptr },
	{ _fnum_variableListGet, kernelVariableListGet,
		PRIVILEGE_USER, 2, args_variableListGet, type_ptr },
	{ _fnum_variableListSet, kernelVariableListSet,
		PRIVILEGE_USER, 3, args_variableListSet, type_val },
	{ _fnum_variableListUnset, kernelVariableListUnset,
		PRIVILEGE_USER, 2, args_variableListUnset, type_val }
};

// Environment functions (0xB000-0xBFFF range)

static kernelArgInfo args_environmentGet[] =
	{ { 1, type_ptr, API_ARG_NONNULLPTR | API_ARG_USERPTR },
		{ 1, type_ptr, API_ARG_NONNULLPTR | API_ARG_USERPTR },
		{ 1, type_val, API_ARG_ANYVAL } };
static kernelArgInfo args_environmentSet[] =
	{ { 1, type_ptr, API_ARG_NONNULLPTR | API_ARG_USERPTR },
		{ 1, type_ptr, API_ARG_NONNULLPTR | API_ARG_USERPTR } };
static kernelArgInfo args_environmentUnset[] =
	{ { 1, type_ptr, API_ARG_NONNULLPTR | API_ARG_USERPTR } };

static kernelFunctionIndex environmentFunctionIndex[] = {
	{ _fnum_environmentGet, kernelEnvironmentGet,
		PRIVILEGE_USER, 3, args_environmentGet, type_val },
	{ _fnum_environmentSet, kernelEnvironmentSet,
		PRIVILEGE_USER, 2, args_environmentSet, type_val },
	{ _fnum_environmentUnset, kernelEnvironmentUnset,
		PRIVILEGE_USER, 1, args_environmentUnset, type_val },
	{ _fnum_environmentDump, kernelEnvironmentDump,
		PRIVILEGE_USER, 0, NULL, type_void }
};

// Raw graphics functions (0xC000-0xCFFF range)

static kernelArgInfo args_graphicGetModes[] =
	{ { 1, type_ptr, API_ARG_NONNULLPTR | API_ARG_USERPTR },
		{ 1, type_val, API_ARG_ANYVAL } };
static kernelArgInfo args_graphicGetMode[] =
	{ { 1, type_ptr, API_ARG_NONNULLPTR | API_ARG_USERPTR } };
static kernelArgInfo args_graphicSetMode[] =
	{ { 1, type_ptr, API_ARG_NONNULLPTR | API_ARG_USERPTR } };
static kernelArgInfo args_graphicCalculateAreaBytes[] =
	{ { 1, type_val, API_ARG_ANYVAL },
		{ 1, type_val, API_ARG_ANYVAL } };
static kernelArgInfo args_graphicClearScreen[] =
	{ { 1, type_ptr, API_ARG_NONNULLPTR | API_ARG_USERPTR } };
static kernelArgInfo args_graphicDrawPixel[] =
	{ { 1, type_ptr, API_ARG_ANYPTR },
		{ 1, type_ptr, API_ARG_NONNULLPTR | API_ARG_USERPTR },
		{ 1, type_val, API_ARG_ANYVAL },
		{ 1, type_val, API_ARG_ANYVAL },
		{ 1, type_val, API_ARG_ANYVAL } };
static kernelArgInfo args_graphicDrawLine[] =
	{ { 1, type_ptr, API_ARG_ANYPTR },
		{ 1, type_ptr, API_ARG_NONNULLPTR | API_ARG_USERPTR },
		{ 1, type_val, API_ARG_ANYVAL },
		{ 1, type_val, API_ARG_ANYVAL },
		{ 1, type_val, API_ARG_ANYVAL },
		{ 1, type_val, API_ARG_ANYVAL },
		{ 1, type_val, API_ARG_ANYVAL } };
static kernelArgInfo args_graphicDrawRect[] =
	{ { 1, type_ptr, API_ARG_ANYPTR },
		{ 1, type_ptr, API_ARG_NONNULLPTR | API_ARG_USERPTR },
		{ 1, type_val, API_ARG_ANYVAL },
		{ 1, type_val, API_ARG_ANYVAL },
		{ 1, type_val, API_ARG_ANYVAL },
		{ 1, type_val, API_ARG_ANYVAL },
		{ 1, type_val, API_ARG_ANYVAL },
		{ 1, type_val, API_ARG_ANYVAL },
		{ 1, type_val, API_ARG_ANYVAL } };
static kernelArgInfo args_graphicDrawOval[] =
	{ { 1, type_ptr, API_ARG_ANYPTR },
		{ 1, type_ptr, API_ARG_NONNULLPTR | API_ARG_USERPTR },
		{ 1, type_val, API_ARG_ANYVAL },
		{ 1, type_val, API_ARG_ANYVAL },
		{ 1, type_val, API_ARG_ANYVAL },
		{ 1, type_val, API_ARG_ANYVAL },
		{ 1, type_val, API_ARG_ANYVAL },
		{ 1, type_val, API_ARG_ANYVAL },
		{ 1, type_val, API_ARG_ANYVAL } };
static kernelArgInfo args_graphicGetImage[] =
	{ { 1, type_ptr, API_ARG_ANYPTR },
		{ 1, type_ptr, API_ARG_NONNULLPTR | API_ARG_USERPTR },
		{ 1, type_val, API_ARG_ANYVAL },
		{ 1, type_val, API_ARG_ANYVAL },
		{ 1, type_val, API_ARG_ANYVAL },
		{ 1, type_val, API_ARG_ANYVAL } };
static kernelArgInfo args_graphicDrawImage[] =
	{ { 1, type_ptr, API_ARG_ANYPTR },
		{ 1, type_ptr, API_ARG_NONNULLPTR | API_ARG_USERPTR },
		{ 1, type_val, API_ARG_ANYVAL },
		{ 1, type_val, API_ARG_ANYVAL },
		{ 1, type_val, API_ARG_ANYVAL },
		{ 1, type_val, API_ARG_ANYVAL },
		{ 1, type_val, API_ARG_ANYVAL },
		{ 1, type_val, API_ARG_ANYVAL },
		{ 1, type_val, API_ARG_ANYVAL } };
static kernelArgInfo args_graphicDrawText[] =
	{ { 1, type_ptr, API_ARG_ANYPTR },
		{ 1, type_ptr, API_ARG_NONNULLPTR | API_ARG_USERPTR },
		{ 1, type_ptr, API_ARG_NONNULLPTR | API_ARG_USERPTR },
		{ 1, type_ptr, API_ARG_NONNULLPTR | API_ARG_USERPTR },
		{ 1, type_ptr, API_ARG_ANYPTR },
		{ 1, type_ptr, API_ARG_NONNULLPTR | API_ARG_USERPTR },
		{ 1, type_val, API_ARG_ANYVAL },
		{ 1, type_val, API_ARG_ANYVAL },
		{ 1, type_val, API_ARG_ANYVAL } };
static kernelArgInfo args_graphicCopyArea[] =
	{ { 1, type_ptr, API_ARG_ANYPTR },
		{ 1, type_val, API_ARG_ANYVAL },
		{ 1, type_val, API_ARG_ANYVAL },
		{ 1, type_val, API_ARG_ANYVAL },
		{ 1, type_val, API_ARG_ANYVAL },
		{ 1, type_val, API_ARG_ANYVAL },
		{ 1, type_val, API_ARG_ANYVAL } };
static kernelArgInfo args_graphicClearArea[] =
	{ { 1, type_ptr, API_ARG_ANYPTR },
		{ 1, type_ptr, API_ARG_NONNULLPTR | API_ARG_USERPTR },
		{ 1, type_val, API_ARG_ANYVAL },
		{ 1, type_val, API_ARG_ANYVAL },
		{ 1, type_val, API_ARG_ANYVAL },
		{ 1, type_val, API_ARG_ANYVAL } };
static kernelArgInfo args_graphicRenderBuffer[] =
	{ { 1, type_ptr, API_ARG_ANYPTR },
		{ 1, type_val, API_ARG_ANYVAL },
		{ 1, type_val, API_ARG_ANYVAL },
		{ 1, type_val, API_ARG_ANYVAL },
		{ 1, type_val, API_ARG_ANYVAL },
		{ 1, type_val, API_ARG_ANYVAL },
		{ 1, type_val, API_ARG_ANYVAL } };

static kernelFunctionIndex graphicFunctionIndex[] = {
	{ _fnum_graphicsAreEnabled, kernelGraphicsAreEnabled,
		PRIVILEGE_USER, 0, NULL, type_val },
	{ _fnum_graphicGetModes, kernelGraphicGetModes,
		PRIVILEGE_USER, 2, args_graphicGetModes, type_val },
	{ _fnum_graphicGetMode, kernelGraphicGetMode,
		PRIVILEGE_USER, 1, args_graphicGetMode, type_val },
	{ _fnum_graphicSetMode, kernelGraphicSetMode,
		PRIVILEGE_SUPERVISOR, 1, args_graphicSetMode, type_val },
	{ _fnum_graphicGetScreenWidth, kernelGraphicGetScreenWidth,
		PRIVILEGE_USER, 0, NULL, type_val },
	{ _fnum_graphicGetScreenHeight, kernelGraphicGetScreenHeight,
		PRIVILEGE_USER, 0, NULL, type_val },
	{ _fnum_graphicCalculateAreaBytes, kernelGraphicCalculateAreaBytes,
		PRIVILEGE_USER, 2, args_graphicCalculateAreaBytes, type_val },
	{ _fnum_graphicClearScreen, kernelGraphicClearScreen,
		PRIVILEGE_USER, 1, args_graphicClearScreen, type_val },
	{ _fnum_graphicDrawPixel, kernelGraphicDrawPixel,
		PRIVILEGE_USER, 5, args_graphicDrawPixel, type_val },
	{ _fnum_graphicDrawLine, kernelGraphicDrawLine,
		PRIVILEGE_USER, 7, args_graphicDrawLine, type_val },
	{ _fnum_graphicDrawRect, kernelGraphicDrawRect,
		PRIVILEGE_USER, 9, args_graphicDrawRect, type_val },
	{ _fnum_graphicDrawOval, kernelGraphicDrawOval,
		PRIVILEGE_USER, 9, args_graphicDrawOval, type_val },
	{ _fnum_graphicGetImage, kernelGraphicGetImage,
		PRIVILEGE_USER, 6, args_graphicGetImage, type_val },
	{ _fnum_graphicDrawImage, kernelGraphicDrawImage,
		PRIVILEGE_USER, 9, args_graphicDrawImage, type_val },
	{ _fnum_graphicDrawText, kernelGraphicDrawText,
		PRIVILEGE_USER, 9, args_graphicDrawText, type_val },
	{ _fnum_graphicCopyArea, kernelGraphicCopyArea,
		PRIVILEGE_USER, 7, args_graphicCopyArea, type_val },
	{ _fnum_graphicClearArea, kernelGraphicClearArea,
		PRIVILEGE_USER, 6, args_graphicClearArea, type_val },
	{ _fnum_graphicRenderBuffer, kernelGraphicRenderBuffer,
		PRIVILEGE_USER, 7, args_graphicRenderBuffer, type_val }
};

// Image functions (0xD000-0xDFFF range)

static kernelArgInfo args_imageNew[] =
	{ { 1, type_ptr, API_ARG_NONNULLPTR | API_ARG_USERPTR },
		{ 1, type_val, API_ARG_ANYVAL },
		{ 1, type_val, API_ARG_ANYVAL } };
static kernelArgInfo args_imageFree[] =
	{ { 1, type_ptr, API_ARG_NONNULLPTR | API_ARG_USERPTR } };
static kernelArgInfo args_imageLoad[] =
	{ { 1, type_ptr, API_ARG_NONNULLPTR | API_ARG_USERPTR },
		{ 1, type_val, API_ARG_ANYVAL },
		{ 1, type_val, API_ARG_ANYVAL },
		{ 1, type_ptr, API_ARG_NONNULLPTR | API_ARG_USERPTR } };
static kernelArgInfo args_imageSave[] =
	{ { 1, type_ptr, API_ARG_NONNULLPTR | API_ARG_USERPTR },
		{ 1, type_val, API_ARG_ANYVAL },
		{ 1, type_ptr, API_ARG_NONNULLPTR | API_ARG_USERPTR } };
static kernelArgInfo args_imageResize[] =
	{ { 1, type_ptr, API_ARG_NONNULLPTR | API_ARG_USERPTR },
		{ 1, type_val, API_ARG_ANYVAL },
		{ 1, type_val, API_ARG_ANYVAL } };
static kernelArgInfo args_imageCopy[] =
	{ { 1, type_ptr, API_ARG_NONNULLPTR | API_ARG_USERPTR },
		{ 1, type_ptr, API_ARG_NONNULLPTR | API_ARG_USERPTR } };
static kernelArgInfo args_imageFill[] =
	{ { 1, type_ptr, API_ARG_NONNULLPTR | API_ARG_USERPTR },
		{ 1, type_ptr, API_ARG_NONNULLPTR | API_ARG_USERPTR } };
static kernelArgInfo args_imagePaste[] =
	{ { 1, type_ptr, API_ARG_NONNULLPTR | API_ARG_USERPTR },
		{ 1, type_ptr, API_ARG_NONNULLPTR | API_ARG_USERPTR },
		{ 1, type_val, API_ARG_ANYVAL },
		{ 1, type_val, API_ARG_ANYVAL } };

static kernelFunctionIndex imageFunctionIndex[] = {
	{ _fnum_imageNew, kernelImageNew,
		PRIVILEGE_USER, 3, args_imageNew, type_val },
	{ _fnum_imageFree, kernelImageFree,
		PRIVILEGE_USER, 1, args_imageFree, type_val },
	{ _fnum_imageLoad, kernelImageLoad,
		PRIVILEGE_USER, 4, args_imageLoad, type_val },
	{ _fnum_imageSave, kernelImageSave,
		PRIVILEGE_USER, 3, args_imageSave, type_val },
	{ _fnum_imageResize, kernelImageResize,
		PRIVILEGE_USER, 3, args_imageResize, type_val },
	{ _fnum_imageCopy, kernelImageCopy,
		PRIVILEGE_USER, 2, args_imageCopy, type_val },
	{ _fnum_imageFill, kernelImageFill,
		PRIVILEGE_USER, 2, args_imageFill, type_val },
	{ _fnum_imagePaste, kernelImagePaste,
		PRIVILEGE_USER, 4, args_imagePaste, type_val }
};

// Font functions (0xE000-0xEFFF range)

static kernelArgInfo args_fontGet[] =
	{ { 1, type_ptr, API_ARG_NONNULLPTR | API_ARG_ANYPTR },
		{ 1, type_val, API_ARG_ANYVAL },
		{ 1, type_val, API_ARG_NONZEROVAL },
		{ 1, type_val, API_ARG_ANYPTR } };
static kernelArgInfo args_fontGetPrintedWidth[] =
	{ { 1, type_ptr, API_ARG_NONNULLPTR | API_ARG_ANYPTR },
		{ 1, type_ptr, API_ARG_ANYPTR | API_ARG_USERPTR },
		{ 1, type_ptr, API_ARG_NONNULLPTR | API_ARG_USERPTR } };
static kernelArgInfo args_fontGetWidth[] =
	{ { 1, type_ptr, API_ARG_NONNULLPTR | API_ARG_ANYPTR } };
static kernelArgInfo args_fontGetHeight[] =
	{ { 1, type_ptr, API_ARG_NONNULLPTR | API_ARG_ANYPTR } };

static kernelFunctionIndex fontFunctionIndex[] = {
	{ _fnum_fontGet, kernelFontGet,
		PRIVILEGE_USER, 4, args_fontGet, type_ptr },
	{ _fnum_fontGetPrintedWidth, kernelFontGetPrintedWidth,
		PRIVILEGE_USER, 3, args_fontGetPrintedWidth, type_val },
	{ _fnum_fontGetWidth, kernelFontGetWidth,
		PRIVILEGE_USER, 1, args_fontGetWidth, type_val },
	{ _fnum_fontGetHeight, kernelFontGetHeight,
		PRIVILEGE_USER, 1, args_fontGetHeight, type_val }
};

// Window system functions (0xF000-0xFFFF range)

static kernelArgInfo args_windowLogin[] =
	{ { 1, type_ptr, API_ARG_NONNULLPTR | API_ARG_USERPTR } };
static kernelArgInfo args_windowNew[] =
	{ { 1, type_val, API_ARG_ANYVAL },
		{ 1, type_ptr, API_ARG_NONNULLPTR | API_ARG_USERPTR } };
static kernelArgInfo args_windowNewDialog[] =
	{ { 1, type_ptr, API_ARG_NONNULLPTR | API_ARG_KERNPTR },
		{ 1, type_ptr, API_ARG_NONNULLPTR | API_ARG_USERPTR } };
static kernelArgInfo args_windowDestroy[] =
	{ { 1, type_ptr, API_ARG_NONNULLPTR | API_ARG_KERNPTR } };
static kernelArgInfo args_windowUpdateBuffer[] =
	{ { 1, type_ptr, API_ARG_NONNULLPTR | API_ARG_KERNPTR },
		{ 1, type_val, API_ARG_ANYVAL },
		{ 1, type_val, API_ARG_ANYVAL },
		{ 1, type_val, API_ARG_ANYVAL },
		{ 1, type_val, API_ARG_ANYVAL } };
static kernelArgInfo args_windowSetCharSet[] =
	{ { 1, type_ptr, API_ARG_NONNULLPTR | API_ARG_KERNPTR },
		{ 1, type_ptr, API_ARG_NONNULLPTR | API_ARG_USERPTR } };
static kernelArgInfo args_windowSetTitle[] =
	{ { 1, type_ptr, API_ARG_NONNULLPTR | API_ARG_KERNPTR },
		{ 1, type_ptr, API_ARG_NONNULLPTR | API_ARG_USERPTR } };
static kernelArgInfo args_windowGetSize[] =
	{ { 1, type_ptr, API_ARG_NONNULLPTR | API_ARG_KERNPTR },
		{ 1, type_ptr, API_ARG_NONNULLPTR | API_ARG_USERPTR },
		{ 1, type_ptr, API_ARG_NONNULLPTR | API_ARG_USERPTR } };
static kernelArgInfo args_windowSetSize[] =
	{ { 1, type_ptr, API_ARG_NONNULLPTR | API_ARG_KERNPTR },
		{ 1, type_val, API_ARG_ANYVAL },
		{ 1, type_val, API_ARG_ANYVAL } };
static kernelArgInfo args_windowGetLocation[] =
	{ { 1, type_ptr, API_ARG_NONNULLPTR | API_ARG_KERNPTR },
		{ 1, type_ptr, API_ARG_NONNULLPTR | API_ARG_USERPTR },
		{ 1, type_ptr, API_ARG_NONNULLPTR | API_ARG_USERPTR } };
static kernelArgInfo args_windowSetLocation[] =
	{ { 1, type_ptr, API_ARG_NONNULLPTR | API_ARG_KERNPTR },
		{ 1, type_val, API_ARG_ANYVAL },
		{ 1, type_val, API_ARG_ANYVAL } };
static kernelArgInfo args_windowCenter[] =
	{ { 1, type_ptr, API_ARG_NONNULLPTR | API_ARG_KERNPTR } };
static kernelArgInfo args_windowSnapIcons[] =
	{ { 1, type_ptr, API_ARG_NONNULLPTR | API_ARG_KERNPTR } };
static kernelArgInfo args_windowSetHasBorder[] =
	{ { 1, type_ptr, API_ARG_NONNULLPTR | API_ARG_KERNPTR },
		{ 1, type_val, API_ARG_ANYVAL } };
static kernelArgInfo args_windowSetHasTitleBar[] =
	{ { 1, type_ptr, API_ARG_NONNULLPTR | API_ARG_KERNPTR },
		{ 1, type_val, API_ARG_ANYVAL } };
static kernelArgInfo args_windowSetMovable[] =
	{ { 1, type_ptr, API_ARG_NONNULLPTR | API_ARG_KERNPTR },
		{ 1, type_val, API_ARG_ANYVAL } };
static kernelArgInfo args_windowSetResizable[] =
	{ { 1, type_ptr, API_ARG_NONNULLPTR | API_ARG_KERNPTR },
		{ 1, type_val, API_ARG_ANYVAL } };
static kernelArgInfo args_windowSetFocusable[] =
	{ { 1, type_ptr, API_ARG_NONNULLPTR | API_ARG_KERNPTR },
		{ 1, type_val, API_ARG_ANYVAL } };
static kernelArgInfo args_windowRemoveMinimizeButton[] =
	{ { 1, type_ptr, API_ARG_NONNULLPTR | API_ARG_KERNPTR } };
static kernelArgInfo args_windowRemoveCloseButton[] =
	{ { 1, type_ptr, API_ARG_NONNULLPTR | API_ARG_KERNPTR } };
static kernelArgInfo args_windowSetVisible[] =
	{ { 1, type_ptr, API_ARG_NONNULLPTR | API_ARG_KERNPTR },
		{ 1, type_val, API_ARG_ANYVAL } };
static kernelArgInfo args_windowSetMinimized[] =
	{ { 1, type_ptr, API_ARG_NONNULLPTR | API_ARG_KERNPTR },
		{ 1, type_val, API_ARG_ANYVAL } };
static kernelArgInfo args_windowAddConsoleTextArea[] =
	{ { 1, type_ptr, API_ARG_NONNULLPTR | API_ARG_KERNPTR } };
static kernelArgInfo args_windowRedrawArea[] =
	{ { 1, type_val, API_ARG_ANYVAL },
		{ 1, type_val, API_ARG_ANYVAL },
		{ 1, type_val, API_ARG_ANYVAL },
		{ 1, type_val, API_ARG_ANYVAL } };
static kernelArgInfo args_windowGetColor[] =
	{ { 1, type_ptr, API_ARG_NONNULLPTR | API_ARG_USERPTR },
		{ 1, type_ptr, API_ARG_NONNULLPTR | API_ARG_USERPTR } };
static kernelArgInfo args_windowSetColor[] =
	{ { 1, type_ptr, API_ARG_NONNULLPTR | API_ARG_USERPTR },
		{ 1, type_ptr, API_ARG_NONNULLPTR | API_ARG_USERPTR } };
static kernelArgInfo args_windowProcessEvent[] =
	{ { 1, type_ptr, API_ARG_NONNULLPTR | API_ARG_USERPTR } };
static kernelArgInfo args_windowComponentEventGet[] =
	{ { 1, type_ptr, API_ARG_NONNULLPTR | API_ARG_KERNPTR },
		{ 1, type_ptr, API_ARG_NONNULLPTR | API_ARG_USERPTR } };
static kernelArgInfo args_windowSetBackgroundColor[] =
	{ { 1, type_ptr, API_ARG_NONNULLPTR | API_ARG_KERNPTR },
		{ 1, type_ptr, API_ARG_USERPTR } };
static kernelArgInfo args_windowShellTileBackground[] =
	{ { 1, type_ptr, API_ARG_USERPTR } };
static kernelArgInfo args_windowShellCenterBackground[] =
	{ { 1, type_ptr, API_ARG_NONNULLPTR | API_ARG_USERPTR } };
static kernelArgInfo args_windowShellNewTaskbarIcon[] =
	{ { 1, type_ptr, API_ARG_NONNULLPTR | API_ARG_USERPTR } };
static kernelArgInfo args_windowShellNewTaskbarTextLabel[] =
	{ { 1, type_ptr, API_ARG_NONNULLPTR | API_ARG_USERPTR } };
static kernelArgInfo args_windowShellDestroyTaskbarComp[] =
	{ { 1, type_ptr, API_ARG_NONNULLPTR | API_ARG_KERNPTR } };
static kernelArgInfo args_windowShellIconify[] =
	{ { 1, type_ptr, API_ARG_NONNULLPTR | API_ARG_KERNPTR },
		{ 1, type_val, API_ARG_ANYVAL },
		{ 1, type_ptr, API_ARG_USERPTR } };
static kernelArgInfo args_windowScreenShot[] =
	{ { 1, type_ptr, API_ARG_NONNULLPTR | API_ARG_USERPTR } };
static kernelArgInfo args_windowSaveScreenShot[] =
	{ { 1, type_ptr, API_ARG_NONNULLPTR | API_ARG_USERPTR } };
static kernelArgInfo args_windowSetTextOutput[] =
	{ { 1, type_ptr, API_ARG_NONNULLPTR | API_ARG_KERNPTR } };
static kernelArgInfo args_windowLayout[] =
	{ { 1, type_ptr, API_ARG_NONNULLPTR | API_ARG_KERNPTR } };
static kernelArgInfo args_windowDebugLayout[] =
	{ { 1, type_ptr, API_ARG_NONNULLPTR | API_ARG_KERNPTR } };
static kernelArgInfo args_windowContextAdd[] =
	{ { 1, type_ptr, API_ARG_NONNULLPTR | API_ARG_KERNPTR },
		{ 1, type_ptr, API_ARG_NONNULLPTR | API_ARG_USERPTR } };
static kernelArgInfo args_windowContextSet[] =
	{ { 1, type_ptr, API_ARG_NONNULLPTR | API_ARG_KERNPTR },
		{ 1, type_ptr, API_ARG_NONNULLPTR | API_ARG_KERNPTR } };
static kernelArgInfo args_windowSwitchPointer[] =
	{ { 1, type_ptr, API_ARG_KERNPTR },
		{ 1, type_ptr, API_ARG_NONNULLPTR | API_ARG_USERPTR } };
static kernelArgInfo args_windowComponentDestroy[] =
	{ { 1, type_ptr, API_ARG_NONNULLPTR | API_ARG_KERNPTR } };
static kernelArgInfo args_windowComponentSetCharSet[] =
	{ { 1, type_ptr, API_ARG_NONNULLPTR | API_ARG_KERNPTR },
		{ 1, type_ptr, API_ARG_NONNULLPTR | API_ARG_USERPTR } };
static kernelArgInfo args_windowComponentSetVisible[] =
	{ { 1, type_ptr, API_ARG_NONNULLPTR | API_ARG_KERNPTR },
		{ 1, type_val, API_ARG_ANYVAL } };
static kernelArgInfo args_windowComponentSetEnabled[] =
	{ { 1, type_ptr, API_ARG_NONNULLPTR | API_ARG_KERNPTR },
		{ 1, type_val, API_ARG_ANYVAL } };
static kernelArgInfo args_windowComponentGetWidth[] =
	{ { 1, type_ptr, API_ARG_NONNULLPTR | API_ARG_KERNPTR } };
static kernelArgInfo args_windowComponentSetWidth[] =
	{ { 1, type_ptr, API_ARG_NONNULLPTR | API_ARG_KERNPTR },
		{ 1, type_val, API_ARG_ANYVAL } };
static kernelArgInfo args_windowComponentGetHeight[] =
	{ { 1, type_ptr, API_ARG_NONNULLPTR | API_ARG_KERNPTR } };
static kernelArgInfo args_windowComponentSetHeight[] =
	{ { 1, type_ptr, API_ARG_NONNULLPTR | API_ARG_KERNPTR },
		{ 1, type_val, API_ARG_ANYVAL } };
static kernelArgInfo args_windowComponentFocus[] =
	{ { 1, type_ptr, API_ARG_NONNULLPTR | API_ARG_KERNPTR } };
static kernelArgInfo args_windowComponentUnfocus[] =
	{ { 1, type_ptr, API_ARG_NONNULLPTR | API_ARG_KERNPTR } };
static kernelArgInfo args_windowComponentDraw[] =
	{ { 1, type_ptr, API_ARG_NONNULLPTR | API_ARG_KERNPTR } };
static kernelArgInfo args_windowComponentGetData[] =
	{ { 1, type_ptr, API_ARG_NONNULLPTR | API_ARG_KERNPTR },
		{ 1, type_ptr, API_ARG_NONNULLPTR | API_ARG_USERPTR },
		{ 1, type_val, API_ARG_ANYVAL } };
static kernelArgInfo args_windowComponentSetData[] =
	{ { 1, type_ptr, API_ARG_NONNULLPTR | API_ARG_KERNPTR },
		{ 1, type_ptr, API_ARG_USERPTR },
		{ 1, type_val, API_ARG_ANYVAL },
		{ 1, type_val, API_ARG_ANYVAL } };
static kernelArgInfo args_windowComponentGetSelected[] =
	{ { 1, type_ptr, API_ARG_NONNULLPTR | API_ARG_KERNPTR },
		{ 1, type_ptr, API_ARG_NONNULLPTR | API_ARG_USERPTR } };
static kernelArgInfo args_windowComponentSetSelected[] =
	{ { 1, type_ptr, API_ARG_NONNULLPTR | API_ARG_KERNPTR },
		{ 1, type_val, API_ARG_ANYVAL } };
static kernelArgInfo args_windowNewButton[] =
	{ { 1, type_ptr, API_ARG_NONNULLPTR | API_ARG_KERNPTR },
		{ 1, type_ptr, API_ARG_USERPTR },
		{ 1, type_ptr, API_ARG_USERPTR },
		{ 1, type_ptr, API_ARG_NONNULLPTR | API_ARG_USERPTR } };
static kernelArgInfo args_windowNewCanvas[] =
	{ { 1, type_ptr, API_ARG_NONNULLPTR | API_ARG_KERNPTR },
		{ 1, type_val, API_ARG_ANYVAL },
		{ 1, type_val, API_ARG_ANYVAL },
		{ 1, type_ptr, API_ARG_NONNULLPTR | API_ARG_USERPTR } };
static kernelArgInfo args_windowNewCheckbox[] =
	{ { 1, type_ptr, API_ARG_NONNULLPTR | API_ARG_KERNPTR },
		{ 1, type_ptr, API_ARG_NONNULLPTR | API_ARG_USERPTR },
		{ 1, type_ptr, API_ARG_NONNULLPTR | API_ARG_USERPTR } };
static kernelArgInfo args_windowNewContainer[] =
	{ { 1, type_ptr, API_ARG_NONNULLPTR | API_ARG_KERNPTR },
		{ 1, type_ptr, API_ARG_NONNULLPTR | API_ARG_USERPTR },
		{ 1, type_ptr, API_ARG_NONNULLPTR | API_ARG_USERPTR } };
static kernelArgInfo args_windowNewDivider[] =
	{ { 1, type_ptr, API_ARG_NONNULLPTR | API_ARG_KERNPTR },
		{ 1, type_val, API_ARG_ANYVAL },
		{ 1, type_ptr, API_ARG_NONNULLPTR | API_ARG_USERPTR } };
static kernelArgInfo args_windowNewIcon[] =
	{ { 1, type_ptr, API_ARG_NONNULLPTR | API_ARG_KERNPTR },
		{ 1, type_ptr, API_ARG_NONNULLPTR | API_ARG_USERPTR },
		{ 1, type_ptr, API_ARG_USERPTR },
		{ 1, type_ptr, API_ARG_NONNULLPTR | API_ARG_USERPTR } };
static kernelArgInfo args_windowNewImage[] =
	{ { 1, type_ptr, API_ARG_NONNULLPTR | API_ARG_KERNPTR },
		{ 1, type_ptr, API_ARG_NONNULLPTR | API_ARG_USERPTR },
		{ 1, type_val, API_ARG_ANYVAL },
		{ 1, type_ptr, API_ARG_NONNULLPTR | API_ARG_USERPTR } };
static kernelArgInfo args_windowNewList[] =
	{ { 1, type_ptr, API_ARG_NONNULLPTR | API_ARG_KERNPTR },
		{ 1, type_val, API_ARG_ANYVAL },
		{ 1, type_val, API_ARG_ANYVAL },
		{ 1, type_val, API_ARG_ANYVAL },
		{ 1, type_val, API_ARG_ANYVAL },
		{ 1, type_ptr, API_ARG_NONNULLPTR | API_ARG_USERPTR },
		{ 1, type_val, API_ARG_ANYVAL },
		{ 1, type_ptr, API_ARG_NONNULLPTR | API_ARG_USERPTR } };
static kernelArgInfo args_windowNewListItem[] =
	{ { 1, type_ptr, API_ARG_NONNULLPTR | API_ARG_KERNPTR },
		{ 1, type_val, API_ARG_ANYVAL },
		{ 1, type_ptr, API_ARG_NONNULLPTR | API_ARG_USERPTR },
		{ 1, type_ptr, API_ARG_NONNULLPTR | API_ARG_USERPTR } };
static kernelArgInfo args_windowNewMenu[] =
	{ { 1, type_ptr, API_ARG_KERNPTR },
		{ 1, type_ptr, API_ARG_KERNPTR },
		{ 1, type_ptr, API_ARG_NONNULLPTR | API_ARG_USERPTR },
		{ 1, type_ptr, API_ARG_USERPTR },
		{ 1, type_ptr, API_ARG_NONNULLPTR | API_ARG_USERPTR } };
static kernelArgInfo args_windowNewMenuBar[] =
	{ { 1, type_ptr, API_ARG_NONNULLPTR | API_ARG_KERNPTR },
		{ 1, type_ptr, API_ARG_NONNULLPTR | API_ARG_USERPTR } };
static kernelArgInfo args_windowNewMenuItem[] =
	{ { 1, type_ptr, API_ARG_NONNULLPTR | API_ARG_KERNPTR },
		{ 1, type_ptr, API_ARG_NONNULLPTR | API_ARG_USERPTR },
		{ 1, type_ptr, API_ARG_NONNULLPTR | API_ARG_USERPTR } };
static kernelArgInfo args_windowNewPasswordField[] =
	{ { 1, type_ptr, API_ARG_NONNULLPTR | API_ARG_KERNPTR },
		{ 1, type_val, API_ARG_ANYVAL },
		{ 1, type_ptr, API_ARG_NONNULLPTR | API_ARG_USERPTR } };
static kernelArgInfo args_windowNewProgressBar[] =
	{ { 1, type_ptr, API_ARG_NONNULLPTR | API_ARG_KERNPTR },
		{ 1, type_ptr, API_ARG_NONNULLPTR | API_ARG_USERPTR } };
static kernelArgInfo args_windowNewRadioButton[] =
	{ { 1, type_ptr, API_ARG_NONNULLPTR | API_ARG_KERNPTR },
		{ 1, type_val, API_ARG_ANYVAL },
		{ 1, type_val, API_ARG_ANYVAL },
		{ 1, type_ptr, API_ARG_NONNULLPTR | API_ARG_USERPTR },
		{ 1, type_val, API_ARG_ANYVAL },
		{ 1, type_ptr, API_ARG_NONNULLPTR | API_ARG_USERPTR } };
static kernelArgInfo args_windowNewScrollBar[] =
	{ { 1, type_ptr, API_ARG_NONNULLPTR | API_ARG_KERNPTR },
		{ 1, type_val, API_ARG_ANYVAL },
		{ 1, type_val, API_ARG_ANYVAL },
		{ 1, type_val, API_ARG_ANYVAL },
		{ 1, type_ptr, API_ARG_NONNULLPTR | API_ARG_USERPTR } };
static kernelArgInfo args_windowNewSlider[] =
	{ { 1, type_ptr, API_ARG_NONNULLPTR | API_ARG_KERNPTR },
		{ 1, type_val, API_ARG_ANYVAL },
		{ 1, type_val, API_ARG_ANYVAL },
		{ 1, type_val, API_ARG_ANYVAL },
		{ 1, type_ptr, API_ARG_NONNULLPTR | API_ARG_USERPTR } };
static kernelArgInfo args_windowNewTextArea[] =
	{ { 1, type_ptr, API_ARG_NONNULLPTR | API_ARG_KERNPTR },
		{ 1, type_val, API_ARG_ANYVAL },
		{ 1, type_val, API_ARG_ANYVAL },
		{ 1, type_val, API_ARG_ANYVAL },
		{ 1, type_ptr, API_ARG_NONNULLPTR | API_ARG_USERPTR } };
static kernelArgInfo args_windowNewTextField[] =
	{ { 1, type_ptr, API_ARG_NONNULLPTR | API_ARG_KERNPTR },
		{ 1, type_val, API_ARG_ANYVAL },
		{ 1, type_ptr, API_ARG_NONNULLPTR | API_ARG_USERPTR } };
static kernelArgInfo args_windowNewTextLabel[] =
	{ { 1, type_ptr, API_ARG_NONNULLPTR | API_ARG_KERNPTR },
		{ 1, type_ptr, API_ARG_NONNULLPTR | API_ARG_USERPTR },
		{ 1, type_ptr, API_ARG_NONNULLPTR | API_ARG_USERPTR } };
static kernelArgInfo args_windowNewTree[] =
	{ { 1, type_ptr, API_ARG_NONNULLPTR | API_ARG_KERNPTR },
		{ 1, type_ptr, API_ARG_USERPTR },
		{ 1, type_val, API_ARG_ANYVAL },
		{ 1, type_val, API_ARG_ANYVAL },
		{ 1, type_ptr, API_ARG_NONNULLPTR | API_ARG_USERPTR } };

static kernelFunctionIndex windowFunctionIndex[] = {
	{ _fnum_windowLogin, kernelWindowLogin,
		PRIVILEGE_SUPERVISOR, 1, args_windowLogin, type_val },
	{ _fnum_windowLogout, kernelWindowLogout,
		PRIVILEGE_USER, 0, NULL, type_val },
	{ _fnum_windowNew, kernelWindowNew,
		PRIVILEGE_USER, 2, args_windowNew, type_ptr },
	{ _fnum_windowNewDialog, kernelWindowNewDialog,
		PRIVILEGE_USER, 2, args_windowNewDialog, type_ptr },
	{ _fnum_windowDestroy, kernelWindowDestroy,
		PRIVILEGE_USER, 1, args_windowDestroy, type_val },
	{ _fnum_windowUpdateBuffer, kernelWindowUpdateBuffer,
		PRIVILEGE_USER, 5, args_windowUpdateBuffer, type_val },
	{ _fnum_windowSetCharSet, kernelWindowSetCharSet,
		PRIVILEGE_USER, 2, args_windowSetCharSet, type_val },
	{ _fnum_windowSetTitle, kernelWindowSetTitle,
		PRIVILEGE_USER, 2, args_windowSetTitle, type_val },
	{ _fnum_windowGetSize, kernelWindowGetSize,
		PRIVILEGE_USER, 3, args_windowGetSize, type_val },
	{ _fnum_windowSetSize, kernelWindowSetSize,
		PRIVILEGE_USER, 3, args_windowSetSize, type_val },
	{ _fnum_windowGetLocation, kernelWindowGetLocation,
		PRIVILEGE_USER, 3, args_windowGetLocation, type_val },
	{ _fnum_windowSetLocation, kernelWindowSetLocation,
		PRIVILEGE_USER, 3, args_windowSetLocation, type_val },
	{ _fnum_windowCenter, kernelWindowCenter,
		PRIVILEGE_USER, 1, args_windowCenter, type_val },
	{ _fnum_windowSnapIcons, kernelWindowSnapIcons,
		PRIVILEGE_USER, 1, args_windowSnapIcons, type_val },
	{ _fnum_windowSetHasBorder, kernelWindowSetHasBorder,
		PRIVILEGE_USER, 2, args_windowSetHasBorder, type_val },
	{ _fnum_windowSetHasTitleBar, kernelWindowSetHasTitleBar,
		PRIVILEGE_USER, 2, args_windowSetHasTitleBar, type_val },
	{ _fnum_windowSetMovable, kernelWindowSetMovable,
		PRIVILEGE_USER, 2, args_windowSetMovable, type_val },
	{ _fnum_windowSetResizable, kernelWindowSetResizable,
		PRIVILEGE_USER, 2, args_windowSetResizable, type_val },
	{ _fnum_windowSetFocusable, kernelWindowSetFocusable,
		PRIVILEGE_USER, 2, args_windowSetFocusable, type_val },
	{ _fnum_windowRemoveMinimizeButton, kernelWindowRemoveMinimizeButton,
		PRIVILEGE_USER, 1, args_windowRemoveMinimizeButton, type_val },
	{ _fnum_windowRemoveCloseButton, kernelWindowRemoveCloseButton,
		PRIVILEGE_USER, 1, args_windowRemoveCloseButton, type_val },
	{ _fnum_windowSetVisible, kernelWindowSetVisible,
		PRIVILEGE_USER, 2, args_windowSetVisible, type_val },
	{ _fnum_windowSetMinimized, kernelWindowSetMinimized,
		PRIVILEGE_USER, 2, args_windowSetMinimized, type_void },
	{ _fnum_windowAddConsoleTextArea, kernelWindowAddConsoleTextArea,
		PRIVILEGE_USER, 1, args_windowAddConsoleTextArea, type_val },
	{ _fnum_windowRedrawArea, kernelWindowRedrawArea,
		PRIVILEGE_USER, 4, args_windowRedrawArea, type_void },
	{ _fnum_windowDrawAll, kernelWindowDrawAll,
		PRIVILEGE_USER, 0, NULL, type_void },
	{ _fnum_windowGetColor, kernelWindowGetColor,
		PRIVILEGE_USER, 2, args_windowGetColor, type_val },
	{ _fnum_windowSetColor, kernelWindowSetColor,
		PRIVILEGE_USER, 2, args_windowSetColor, type_val },
	{ _fnum_windowResetColors, kernelWindowResetColors,
		PRIVILEGE_USER, 0, NULL, type_void },
	{ _fnum_windowProcessEvent, kernelWindowProcessEvent,
		PRIVILEGE_USER, 1, args_windowProcessEvent, type_void },
	{ _fnum_windowComponentEventGet, kernelWindowComponentEventGet,
		PRIVILEGE_USER, 2, args_windowComponentEventGet, type_val },
	{ _fnum_windowSetBackgroundColor, kernelWindowSetBackgroundColor,
		PRIVILEGE_USER, 2, args_windowSetBackgroundColor, type_val },
	{ _fnum_windowShellTileBackground, kernelWindowShellTileBackground,
		PRIVILEGE_USER, 1, args_windowShellTileBackground, type_val },
	{ _fnum_windowShellCenterBackground, kernelWindowShellCenterBackground,
		PRIVILEGE_USER, 1, args_windowShellCenterBackground, type_val },
	{ _fnum_windowShellNewTaskbarIcon, kernelWindowShellNewTaskbarIcon,
		PRIVILEGE_USER, 1, args_windowShellNewTaskbarIcon, type_ptr },
	{ _fnum_windowShellNewTaskbarTextLabel,
		kernelWindowShellNewTaskbarTextLabel,
		PRIVILEGE_USER, 1, args_windowShellNewTaskbarTextLabel, type_ptr },
	{ _fnum_windowShellDestroyTaskbarComp, kernelWindowShellDestroyTaskbarComp,
		PRIVILEGE_USER, 1, args_windowShellDestroyTaskbarComp, type_void },
	{ _fnum_windowShellIconify, kernelWindowShellIconify,
		PRIVILEGE_USER, 3, args_windowShellIconify, type_ptr },
	{ _fnum_windowScreenShot, kernelWindowScreenShot,
		PRIVILEGE_USER, 1, args_windowScreenShot, type_val },
	{ _fnum_windowSaveScreenShot, kernelWindowSaveScreenShot,
		PRIVILEGE_USER, 1, args_windowSaveScreenShot, type_val },
	{ _fnum_windowSetTextOutput, kernelWindowSetTextOutput,
		PRIVILEGE_USER, 1, args_windowSetTextOutput, type_val },
	{ _fnum_windowLayout, kernelWindowLayout,
		PRIVILEGE_USER, 1, args_windowLayout, type_val },
	{ _fnum_windowDebugLayout, kernelWindowDebugLayout,
		PRIVILEGE_USER, 1, args_windowDebugLayout, type_void },
	{ _fnum_windowContextAdd, kernelWindowContextAdd,
		PRIVILEGE_USER, 2, args_windowContextAdd, type_val },
	{ _fnum_windowContextSet, kernelWindowContextSet,
		PRIVILEGE_USER, 2, args_windowContextSet, type_val },
	{ _fnum_windowSwitchPointer, kernelWindowSwitchPointer,
		PRIVILEGE_USER, 2, args_windowSwitchPointer, type_val },
	{ _fnum_windowRefresh, kernelWindowRefresh,
		PRIVILEGE_USER, 0, NULL, type_val },
	{ _fnum_windowComponentDestroy, kernelWindowComponentDestroy,
		PRIVILEGE_USER, 1, args_windowComponentDestroy, type_val },
	{ _fnum_windowComponentSetCharSet, kernelWindowComponentSetCharSet,
		PRIVILEGE_USER, 2, args_windowComponentSetCharSet, type_val },
	{ _fnum_windowComponentSetVisible, kernelWindowComponentSetVisible,
		PRIVILEGE_USER, 2, args_windowComponentSetVisible, type_val },
	{ _fnum_windowComponentSetEnabled, kernelWindowComponentSetEnabled,
		PRIVILEGE_USER, 2, args_windowComponentSetEnabled, type_val },
	{ _fnum_windowComponentGetWidth, kernelWindowComponentGetWidth,
		PRIVILEGE_USER, 1, args_windowComponentGetWidth, type_val },
	{ _fnum_windowComponentSetWidth, kernelWindowComponentSetWidth,
		PRIVILEGE_USER, 2, args_windowComponentSetWidth, type_val },
	{ _fnum_windowComponentGetHeight, kernelWindowComponentGetHeight,
		PRIVILEGE_USER, 1, args_windowComponentGetHeight, type_val },
	{ _fnum_windowComponentSetHeight, kernelWindowComponentSetHeight,
		PRIVILEGE_USER, 2, args_windowComponentSetHeight, type_val },
	{ _fnum_windowComponentFocus, kernelWindowComponentFocus,
		PRIVILEGE_USER, 1, args_windowComponentFocus, type_val },
	{ _fnum_windowComponentUnfocus, kernelWindowComponentUnfocus,
		PRIVILEGE_USER, 1, args_windowComponentUnfocus, type_val },
	{ _fnum_windowComponentDraw, kernelWindowComponentDraw,
		PRIVILEGE_USER, 1, args_windowComponentDraw, type_val },
	{ _fnum_windowComponentGetData, kernelWindowComponentGetData,
		PRIVILEGE_USER, 3, args_windowComponentGetData, type_val },
	{ _fnum_windowComponentSetData, kernelWindowComponentSetData,
		PRIVILEGE_USER, 4, args_windowComponentSetData, type_val },
	{ _fnum_windowComponentGetSelected, kernelWindowComponentGetSelected,
		PRIVILEGE_USER, 2, args_windowComponentGetSelected, type_val },
	{ _fnum_windowComponentSetSelected, kernelWindowComponentSetSelected,
		PRIVILEGE_USER, 2, args_windowComponentSetSelected, type_val },
	{ _fnum_windowNewButton, kernelWindowNewButton,
		PRIVILEGE_USER, 4, args_windowNewButton, type_ptr },
	{ _fnum_windowNewCanvas, kernelWindowNewCanvas,
		PRIVILEGE_USER, 4, args_windowNewCanvas, type_ptr },
	{ _fnum_windowNewCheckbox, kernelWindowNewCheckbox,
		PRIVILEGE_USER, 3, args_windowNewCheckbox, type_ptr },
	{ _fnum_windowNewContainer, kernelWindowNewContainer,
		PRIVILEGE_USER, 3, args_windowNewContainer, type_ptr },
	{ _fnum_windowNewDivider, kernelWindowNewDivider,
		PRIVILEGE_USER, 3, args_windowNewDivider, type_ptr },
	{ _fnum_windowNewIcon, kernelWindowNewIcon,
		PRIVILEGE_USER, 4, args_windowNewIcon, type_ptr },
	{ _fnum_windowNewImage, kernelWindowNewImage,
		PRIVILEGE_USER, 4, args_windowNewImage, type_ptr },
	{ _fnum_windowNewList, kernelWindowNewList,
		PRIVILEGE_USER, 8, args_windowNewList, type_ptr },
	{ _fnum_windowNewListItem, kernelWindowNewListItem,
		PRIVILEGE_USER, 3, args_windowNewListItem, type_ptr },
	{ _fnum_windowNewMenu, kernelWindowNewMenu,
		PRIVILEGE_USER, 5, args_windowNewMenu, type_ptr },
	{ _fnum_windowNewMenuBar, kernelWindowNewMenuBar,
		PRIVILEGE_USER, 2, args_windowNewMenuBar, type_ptr },
	{ _fnum_windowNewMenuItem, kernelWindowNewMenuItem,
		PRIVILEGE_USER, 3, args_windowNewMenuItem, type_ptr },
	{ _fnum_windowNewPasswordField, kernelWindowNewPasswordField,
		PRIVILEGE_USER, 3, args_windowNewPasswordField, type_ptr },
	{ _fnum_windowNewProgressBar, kernelWindowNewProgressBar,
		PRIVILEGE_USER, 2, args_windowNewProgressBar, type_ptr },
	{ _fnum_windowNewRadioButton, kernelWindowNewRadioButton,
		PRIVILEGE_USER, 6, args_windowNewRadioButton, type_ptr },
	{ _fnum_windowNewScrollBar, kernelWindowNewScrollBar,
		PRIVILEGE_USER, 5, args_windowNewScrollBar, type_ptr },
	{ _fnum_windowNewSlider, kernelWindowNewSlider,
		PRIVILEGE_USER, 5, args_windowNewSlider, type_ptr },
	{ _fnum_windowNewTextArea, kernelWindowNewTextArea,
		PRIVILEGE_USER, 5, args_windowNewTextArea, type_ptr },
	{ _fnum_windowNewTextField, kernelWindowNewTextField,
		PRIVILEGE_USER, 3, args_windowNewTextField, type_ptr },
	{ _fnum_windowNewTextLabel, kernelWindowNewTextLabel,
		PRIVILEGE_USER, 3, args_windowNewTextLabel, type_ptr },
	{ _fnum_windowNewTree, kernelWindowNewTree,
		PRIVILEGE_USER, 5, args_windowNewTree, type_ptr }
};

// User functions (0x10000-0x10FFF range)

static kernelArgInfo args_userAuthenticate[] =
	{ { 1, type_ptr, API_ARG_NONNULLPTR | API_ARG_USERPTR },
		{ 1, type_ptr, API_ARG_NONNULLPTR | API_ARG_USERPTR } };
static kernelArgInfo args_userLogin[] =
	{ { 1, type_ptr, API_ARG_NONNULLPTR | API_ARG_USERPTR },
		{ 1, type_ptr, API_ARG_NONNULLPTR | API_ARG_USERPTR } };
static kernelArgInfo args_userLogout[] =
	{ { 1, type_ptr, API_ARG_USERPTR } };
static kernelArgInfo args_userExists[] =
	{ { 1, type_ptr, API_ARG_NONNULLPTR | API_ARG_USERPTR } };
static kernelArgInfo args_userGetNames[] =
	{ { 1, type_ptr, API_ARG_NONNULLPTR | API_ARG_USERPTR },
		{ 1, type_val, API_ARG_ANYVAL } };
static kernelArgInfo args_userAdd[] =
	{ { 1, type_ptr, API_ARG_NONNULLPTR | API_ARG_USERPTR },
		{ 1, type_ptr, API_ARG_NONNULLPTR | API_ARG_USERPTR } };
static kernelArgInfo args_userDelete[] =
	{ { 1, type_ptr, API_ARG_NONNULLPTR | API_ARG_USERPTR } };
static kernelArgInfo args_userSetPassword[] =
	{ { 1, type_ptr, API_ARG_NONNULLPTR | API_ARG_USERPTR },
		{ 1, type_ptr, API_ARG_NONNULLPTR | API_ARG_USERPTR },
		{ 1, type_ptr, API_ARG_NONNULLPTR | API_ARG_USERPTR } };
static kernelArgInfo args_userGetCurrent[] =
	{ { 1, type_ptr, API_ARG_NONNULLPTR | API_ARG_USERPTR },
		{ 1, type_val, API_ARG_POSINTVAL } };
static kernelArgInfo args_userGetPrivilege[] =
	{ { 1, type_ptr, API_ARG_NONNULLPTR | API_ARG_USERPTR } };
static kernelArgInfo args_userSetPid[] =
	{ { 1, type_ptr, API_ARG_NONNULLPTR | API_ARG_USERPTR },
		{ 1, type_val, API_ARG_ANYVAL } };
static kernelArgInfo args_userFileAdd[] =
	{ { 1, type_ptr, API_ARG_NONNULLPTR | API_ARG_USERPTR },
		{ 1, type_ptr, API_ARG_NONNULLPTR | API_ARG_USERPTR },
		{ 1, type_ptr, API_ARG_NONNULLPTR | API_ARG_USERPTR } };
static kernelArgInfo args_userFileDelete[] =
	{ { 1, type_ptr, API_ARG_NONNULLPTR | API_ARG_USERPTR },
		{ 1, type_ptr, API_ARG_NONNULLPTR | API_ARG_USERPTR } };
static kernelArgInfo args_userFileSetPassword[] =
	{ { 1, type_ptr, API_ARG_NONNULLPTR | API_ARG_USERPTR },
		{ 1, type_ptr, API_ARG_NONNULLPTR | API_ARG_USERPTR },
		{ 1, type_ptr, API_ARG_NONNULLPTR | API_ARG_USERPTR },
		{ 1, type_ptr, API_ARG_NONNULLPTR | API_ARG_USERPTR } };

static kernelFunctionIndex userFunctionIndex[] = {
	{ _fnum_userAuthenticate, kernelUserAuthenticate,
		PRIVILEGE_USER, 2, args_userAuthenticate, type_val },
	{ _fnum_userLogin, kernelUserLogin,
		PRIVILEGE_SUPERVISOR, 2, args_userLogin, type_val },
	{ _fnum_userLogout, kernelUserLogout,
		PRIVILEGE_USER, 1, args_userLogout, type_val },
	{ _fnum_userExists, kernelUserExists,
		PRIVILEGE_USER, 1, args_userExists, type_val },
	{ _fnum_userGetNames, kernelUserGetNames,
		PRIVILEGE_USER, 2, args_userGetNames, type_val },
	{ _fnum_userAdd, kernelUserAdd,
		PRIVILEGE_SUPERVISOR, 2, args_userAdd, type_val },
	{ _fnum_userDelete, kernelUserDelete,
		PRIVILEGE_SUPERVISOR, 1, args_userDelete, type_val },
	{ _fnum_userSetPassword, kernelUserSetPassword,
		PRIVILEGE_USER, 3, args_userSetPassword, type_val },
	{ _fnum_userGetCurrent, kernelUserGetCurrent,
		PRIVILEGE_USER, 2, args_userGetCurrent, type_val },
	{ _fnum_userGetPrivilege, kernelUserGetPrivilege,
		PRIVILEGE_USER, 1, args_userGetPrivilege, type_val },
	{ _fnum_userGetPid, kernelUserGetPid,
		PRIVILEGE_USER, 0, NULL, type_val },
	{ _fnum_userSetPid, kernelUserSetPid,
		PRIVILEGE_SUPERVISOR, 2, args_userSetPid, type_val },
	{ _fnum_userFileAdd, kernelUserFileAdd,
		PRIVILEGE_SUPERVISOR, 3, args_userFileAdd, type_val },
	{ _fnum_userFileDelete, kernelUserFileDelete,
		PRIVILEGE_SUPERVISOR, 2, args_userFileDelete, type_val },
	{ _fnum_userFileSetPassword, kernelUserFileSetPassword,
		PRIVILEGE_USER, 4, args_userFileSetPassword, type_val }
};

// Network functions (0x11000-0x11FFF range)

static kernelArgInfo args_networkDeviceGet[] =
	{ { 1, type_ptr, API_ARG_NONNULLPTR | API_ARG_USERPTR },
		{ 1, type_ptr, API_ARG_NONNULLPTR | API_ARG_USERPTR } };
static kernelArgInfo args_networkOpen[] =
	{ { 1, type_val, API_ARG_ANYVAL },
		{ 1, type_ptr, API_ARG_NONNULLPTR | API_ARG_USERPTR },
		{ 1, type_ptr, API_ARG_NONNULLPTR | API_ARG_USERPTR } };
static kernelArgInfo args_networkClose[] =
	{ { 1, type_ptr, API_ARG_NONNULLPTR | API_ARG_KERNPTR } };
static kernelArgInfo args_networkCount[] =
	{ { 1, type_ptr, API_ARG_NONNULLPTR | API_ARG_KERNPTR } };
static kernelArgInfo args_networkRead[] =
	{ { 1, type_ptr, API_ARG_NONNULLPTR | API_ARG_KERNPTR },
		{ 1, type_ptr, API_ARG_NONNULLPTR | API_ARG_USERPTR },
		{ 1, type_val, API_ARG_ANYVAL } };
static kernelArgInfo args_networkWrite[] =
	{ { 1, type_ptr, API_ARG_NONNULLPTR | API_ARG_KERNPTR },
		{ 1, type_ptr, API_ARG_NONNULLPTR | API_ARG_USERPTR },
		{ 1, type_val, API_ARG_ANYVAL } };
static kernelArgInfo args_networkPing[] =
	{ { 1, type_ptr, API_ARG_NONNULLPTR | API_ARG_KERNPTR },
		{ 1, type_val, API_ARG_ANYVAL },
		{ 1, type_ptr, API_ARG_NONNULLPTR | API_ARG_USERPTR },
		{ 1, type_val, API_ARG_ANYVAL } };
static kernelArgInfo args_networkGetHostName[] =
	{ { 1, type_ptr, API_ARG_NONNULLPTR | API_ARG_USERPTR },
		{ 1, type_val, API_ARG_ANYVAL } };
static kernelArgInfo args_networkSetHostName[] =
	{ { 1, type_ptr, API_ARG_NONNULLPTR | API_ARG_USERPTR },
		{ 1, type_val, API_ARG_ANYVAL } };
static kernelArgInfo args_networkGetDomainName[] =
	{ { 1, type_ptr, API_ARG_NONNULLPTR | API_ARG_USERPTR },
		{ 1, type_val, API_ARG_ANYVAL } };
static kernelArgInfo args_networkSetDomainName[] =
	{ { 1, type_ptr, API_ARG_NONNULLPTR | API_ARG_USERPTR },
		{ 1, type_val, API_ARG_ANYVAL } };

static kernelFunctionIndex networkFunctionIndex[] = {
	{ _fnum_networkDeviceGetCount, kernelNetworkDeviceGetCount,
		PRIVILEGE_USER, 0, NULL, type_val },
	{ _fnum_networkDeviceGet, kernelNetworkDeviceGet,
		PRIVILEGE_USER, 2, args_networkDeviceGet, type_val },
	{ _fnum_networkInitialized, kernelNetworkInitialized,
		PRIVILEGE_USER, 0, NULL, type_val },
	{ _fnum_networkInitialize, kernelNetworkInitialize,
		PRIVILEGE_SUPERVISOR, 0, NULL, type_val },
	{ _fnum_networkShutdown, kernelNetworkShutdown,
		PRIVILEGE_SUPERVISOR, 0, NULL, type_val },
	{ _fnum_networkOpen, kernelNetworkOpen,
		PRIVILEGE_USER, 3, args_networkOpen, type_ptr },
	{ _fnum_networkClose, kernelNetworkClose,
		PRIVILEGE_USER, 1, args_networkClose, type_val },
	{ _fnum_networkCount, kernelNetworkCount,
		PRIVILEGE_USER, 1, args_networkCount, type_val },
	{ _fnum_networkRead, kernelNetworkRead,
		PRIVILEGE_USER, 3, args_networkRead, type_val },
	{ _fnum_networkWrite, kernelNetworkWrite,
		PRIVILEGE_USER, 3, args_networkWrite, type_val },
	{ _fnum_networkPing, kernelNetworkPing,
		PRIVILEGE_USER, 4, args_networkPing, type_val },
	{ _fnum_networkGetHostName, kernelNetworkGetHostName,
		PRIVILEGE_USER, 2, args_networkGetHostName, type_val },
	{ _fnum_networkSetHostName, kernelNetworkSetHostName,
		PRIVILEGE_SUPERVISOR, 2, args_networkSetHostName, type_val },
	{ _fnum_networkGetDomainName, kernelNetworkGetDomainName,
		PRIVILEGE_USER, 2, args_networkGetDomainName, type_val },
	{ _fnum_networkSetDomainName, kernelNetworkSetDomainName,
		PRIVILEGE_SUPERVISOR, 2, args_networkSetDomainName, type_val },
};

// Miscellaneous functions (0xFF000-0xFFFFF range)

static kernelArgInfo args_shutdown[] =
	{ { 1, type_val, API_ARG_ANYVAL },
		{ 1, type_val, API_ARG_ANYVAL } };
static kernelArgInfo args_getVersion[] =
	{ { 1, type_ptr, API_ARG_NONNULLPTR | API_ARG_USERPTR },
		{ 1, type_val, API_ARG_ANYVAL } };
static kernelArgInfo args_systemInfo[] =
	{ { 1, type_ptr, API_ARG_NONNULLPTR | API_ARG_USERPTR } };
static kernelArgInfo args_encryptMD5[] =
	{ { 1, type_ptr, API_ARG_NONNULLPTR | API_ARG_USERPTR },
		{ 1, type_ptr, API_ARG_NONNULLPTR | API_ARG_USERPTR } };
static kernelArgInfo args_lockGet[] =
	{ { 1, type_ptr, API_ARG_NONNULLPTR | API_ARG_USERPTR } };
static kernelArgInfo args_lockRelease[] =
	{ { 1, type_ptr, API_ARG_NONNULLPTR | API_ARG_USERPTR } };
static kernelArgInfo args_lockVerify[] =
	{ { 1, type_ptr, API_ARG_NONNULLPTR | API_ARG_USERPTR } };
static kernelArgInfo args_configRead[] =
	{ { 1, type_ptr, API_ARG_NONNULLPTR | API_ARG_USERPTR },
		{ 1, type_ptr, API_ARG_NONNULLPTR | API_ARG_USERPTR } };
static kernelArgInfo args_configWrite[] =
	{ { 1, type_ptr, API_ARG_NONNULLPTR | API_ARG_USERPTR },
		{ 1, type_ptr, API_ARG_NONNULLPTR | API_ARG_USERPTR } };
static kernelArgInfo args_configGet[] =
	{ { 1, type_ptr, API_ARG_NONNULLPTR | API_ARG_USERPTR },
		{ 1, type_ptr, API_ARG_NONNULLPTR | API_ARG_USERPTR },
		{ 1, type_ptr, API_ARG_NONNULLPTR | API_ARG_USERPTR },
		{ 1, type_ptr, API_ARG_NONNULLPTR | API_ARG_USERPTR } };
static kernelArgInfo args_configSet[] =
	{ { 1, type_ptr, API_ARG_NONNULLPTR | API_ARG_USERPTR },
		{ 1, type_ptr, API_ARG_NONNULLPTR | API_ARG_USERPTR },
		{ 1, type_ptr, API_ARG_NONNULLPTR | API_ARG_USERPTR } };
static kernelArgInfo args_configUnset[] =
	{ { 1, type_ptr, API_ARG_NONNULLPTR | API_ARG_USERPTR },
		{ 1, type_ptr, API_ARG_NONNULLPTR | API_ARG_USERPTR } };
static kernelArgInfo args_guidGenerate[] =
	{ { 1, type_ptr, API_ARG_NONNULLPTR | API_ARG_USERPTR } };
static kernelArgInfo args_crc32[] =
	{ { 1, type_ptr, API_ARG_NONNULLPTR | API_ARG_USERPTR },
		{ 1, type_val, API_ARG_ANYVAL },
		{ 1, type_ptr, API_ARG_USERPTR } };
static kernelArgInfo args_keyboardGetMap[] =
	{ { 1, type_ptr, API_ARG_NONNULLPTR | API_ARG_USERPTR } };
static kernelArgInfo args_keyboardSetMap[] =
	{ { 1, type_ptr, API_ARG_NONNULLPTR | API_ARG_USERPTR } };
static kernelArgInfo args_keyboardVirtualInput[] =
	{ { 1, type_val, API_ARG_ANYVAL },
		{ 1, type_val, API_ARG_ANYVAL } };
static kernelArgInfo args_deviceTreeGetRoot[] =
	{ { 1, type_ptr, API_ARG_NONNULLPTR | API_ARG_USERPTR } };
static kernelArgInfo args_deviceTreeGetChild[] =
	{ { 1, type_ptr, API_ARG_NONNULLPTR | API_ARG_USERPTR },
		{ 1, type_ptr, API_ARG_NONNULLPTR | API_ARG_USERPTR } };
static kernelArgInfo args_deviceTreeGetNext[] =
	{ { 1, type_ptr, API_ARG_NONNULLPTR | API_ARG_USERPTR } };
static kernelArgInfo args_mouseLoadPointer[] =
	{ { 1, type_ptr, API_ARG_NONNULLPTR | API_ARG_USERPTR },
		{ 1, type_ptr, API_ARG_NONNULLPTR | API_ARG_USERPTR } };
static kernelArgInfo args_pageGetPhysical[] =
	{ { 1, type_val, API_ARG_ANYVAL },
		{ 1, type_ptr, API_ARG_ANYPTR } };
static kernelArgInfo args_charsetToUnicode[] =
	{ { 1, type_ptr, API_ARG_NONNULLPTR | API_ARG_ANYPTR },
		{ 1, type_val, API_ARG_ANYVAL } };
static kernelArgInfo args_charsetFromUnicode[] =
	{ { 1, type_ptr, API_ARG_NONNULLPTR | API_ARG_ANYPTR },
		{ 1, type_val, API_ARG_ANYVAL } };

static kernelFunctionIndex miscFunctionIndex[] = {
	{ _fnum_shutdown, kernelShutdown,
		PRIVILEGE_USER, 2, args_shutdown, type_val },
	{ _fnum_getVersion, kernelGetVersion,
		PRIVILEGE_USER, 2, args_getVersion, type_void },
	{ _fnum_systemInfo, kernelSystemInfo,
		PRIVILEGE_USER, 1, args_systemInfo, type_val },
	{ _fnum_encryptMD5, kernelEncryptMD5,
		PRIVILEGE_USER, 2, args_encryptMD5, type_val },
	{ _fnum_lockGet, kernelLockGet,
		PRIVILEGE_USER, 1, args_lockGet, type_val },
	{ _fnum_lockRelease, kernelLockRelease,
		PRIVILEGE_USER, 1, args_lockRelease, type_val },
	{ _fnum_lockVerify, kernelLockVerify,
		PRIVILEGE_USER, 1, args_lockVerify, type_val },
	{ _fnum_configRead, kernelConfigRead,
		PRIVILEGE_USER, 2, args_configRead, type_val },
	{ _fnum_configWrite, kernelConfigWrite,
		PRIVILEGE_USER, 2, args_configWrite, type_val },
	{ _fnum_configGet, kernelConfigGet,
		PRIVILEGE_USER, 4, args_configGet, type_val },
	{ _fnum_configSet, kernelConfigSet,
		PRIVILEGE_USER, 3, args_configSet, type_val },
	{ _fnum_configUnset, kernelConfigUnset,
		PRIVILEGE_USER, 2, args_configUnset, type_val },
	{ _fnum_guidGenerate, kernelGuidGenerate,
		PRIVILEGE_USER, 1, args_guidGenerate, type_val },
	{ _fnum_crc32, kernelCrc32,
		PRIVILEGE_USER, 3, args_crc32, type_val },
	{ _fnum_keyboardGetMap, kernelKeyboardGetMap,
		PRIVILEGE_USER, 1, args_keyboardGetMap, type_val },
	{ _fnum_keyboardSetMap, kernelKeyboardSetMap,
		PRIVILEGE_USER, 1, args_keyboardSetMap, type_val },
	{ _fnum_keyboardVirtualInput, kernelKeyboardVirtualInput,
		PRIVILEGE_USER, 2, args_keyboardVirtualInput, type_val },
	{ _fnum_deviceTreeGetRoot, kernelDeviceTreeGetRoot,
		PRIVILEGE_USER, 1, args_deviceTreeGetRoot, type_val },
	{ _fnum_deviceTreeGetChild, kernelDeviceTreeGetChild,
		PRIVILEGE_USER, 2, args_deviceTreeGetChild, type_val },
	{ _fnum_deviceTreeGetNext, kernelDeviceTreeGetNext,
		PRIVILEGE_USER, 1, args_deviceTreeGetNext, type_val },
	{ _fnum_mouseLoadPointer, kernelMouseLoadPointer,
		PRIVILEGE_USER, 2, args_mouseLoadPointer, type_val },
	{ _fnum_pageGetPhysical, kernelPageGetPhysical,
		PRIVILEGE_USER, 2, args_pageGetPhysical, type_ptr },
	{ _fnum_charsetToUnicode, kernelCharsetToUnicode,
		PRIVILEGE_USER, 2, args_charsetToUnicode, type_val },
	{ _fnum_charsetFromUnicode, kernelCharsetFromUnicode,
		PRIVILEGE_USER, 2, args_charsetFromUnicode, type_val }
};

static kernelFunctionIndex *functionIndex[] = {
	miscFunctionIndex,
	textFunctionIndex,
	diskFunctionIndex,
	filesystemFunctionIndex,
	fileFunctionIndex,
	memoryFunctionIndex,
	multitaskerFunctionIndex,
	loaderFunctionIndex,
	rtcFunctionIndex,
	randomFunctionIndex,
	variableListFunctionIndex,
	environmentFunctionIndex,
	graphicFunctionIndex,
	imageFunctionIndex,
	fontFunctionIndex,
	windowFunctionIndex,
	userFunctionIndex,
	networkFunctionIndex
};


/////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////
//
//  Below here, the functions are exported for external use
//
/////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////

void kernelApi(unsigned CS __attribute__((unused)), unsigned *args)
{
	// This is the initial entry point for the kernel's API.  This
	// function will be first the recipient of all calls to the global
	// call gate.  This function will pass a pointer to the rest of the
	// arguments to the processCall function that does all the real work.
	// This funcion does the far return.

	quad_t status = 0;
	int functionNumber = 0;
	unsigned *functionArgs = 0;
	kernelFunctionIndex *functionEntry = NULL;
	int currentProc = 0;
	int currentPriv = 0;
	quad_t (*functionPointer)() = NULL;
	int pushCount = 0;
	int count;
	#if defined(DEBUG)
	const char *symbolName = NULL;
	#endif // defined(DEBUG)

	// Check args
	if (!args)
	{
		kernelError(kernel_error, "No args supplied to API call");
		status = ERR_NULLPARAMETER;
		goto out;
	}

	// Which function number are we being asked to call?
	functionNumber = args[0];
	functionArgs = (unsigned *) args[1];

	if ((functionNumber < 0x1000) || (functionNumber > 0xFFFFF))
	{
		kernelError(kernel_error, "Illegal function number %x in API call",
			functionNumber);
		status = ERR_NOSUCHENTRY;
		goto out;
	}

	if ((functionNumber >> 12) == 0xFF)
	{
		// 'misc' functions are in spot 0
		functionEntry = &functionIndex[0][functionNumber & 0xFFF];
	}
	else
	{
		functionEntry =
			&functionIndex[functionNumber >> 12][functionNumber & 0xFFF];
	}

	// Is there such a function?
	if (!functionEntry || (functionEntry->functionNumber != functionNumber))
	{
		kernelError(kernel_error, "No such API function %x in API call",
			functionNumber);
		status = ERR_NOSUCHFUNCTION;
		goto out;
	}

	// Does the caller have the adequate privilege level to call this
	// function?
	currentProc = kernelMultitaskerGetCurrentProcessId();
	currentPriv = kernelMultitaskerGetProcessPrivilege(currentProc);
	if (currentPriv < 0)
	{
		kernelError(kernel_error, "Couldn't determine current privilege level "
			"in call to API function %x", functionEntry->functionNumber);
		if (functionEntry->returnType == type_ptr)
			status = NULL;
		else
			status = currentPriv;
		goto out;
	}
	else if (currentPriv > functionEntry->privilege)
	{
		kernelError(kernel_error, "Insufficient privilege to invoke API "
			"function %x", functionEntry->functionNumber);
		if (functionEntry->returnType == type_ptr)
			status = NULL;
		else
			status = ERR_PERMISSION;
		goto out;
	}

	// Make 'functionPointer' equal the address of the requested kernel
	// function.
	functionPointer = functionEntry->functionPointer;

	#if defined(DEBUG)
	symbolName = kernelLookupClosestSymbol(NULL, functionPointer);
	kernelDebug(debug_api, "Kernel API function %x (%s), %d args ",
		functionNumber, symbolName, functionEntry->argCount);
	for (count = 0; count < functionEntry->argCount; count ++)
		kernelDebug(debug_api, "arg %d=%u", count, functionArgs[count]);
	#endif // defined(DEBUG)

	// Examine and tally the arguments
	for (count = 0; count < functionEntry->argCount; count ++)
	{
		// Do we have information about the arguments?
		if (functionEntry->args)
		{
			// Check the argument
			switch (functionEntry->args[count].type)
			{
				case type_ptr:
					if (!functionArgs[count])
					{
						if (functionEntry->args[count].content &
							API_ARG_NONNULLPTR)
						{
							kernelError(kernel_error, "API function %x "
								"argument %d: Pointer is not allowed to be "
								"NULL",	functionEntry->functionNumber, count);
							if (functionEntry->returnType == type_ptr)
								status = NULL;
							else
								status = ERR_NULLPARAMETER;
							goto out;
						}
						else
							break;
					}
					if ((functionArgs[count] >= KERNEL_VIRTUAL_ADDRESS) &&
						(functionEntry->args[count].content & API_ARG_USERPTR))
					{
						kernelError(kernel_error, "API function %x argument "
							"%d: Pointer must point to user memory",
							functionEntry->functionNumber, count);
						if (functionEntry->returnType == type_ptr)
							status = NULL;
						else
							status = ERR_PERMISSION;
						goto out;
					}
					if ((functionArgs[count] < KERNEL_VIRTUAL_ADDRESS) &&
						(functionEntry->args[count].content & API_ARG_KERNPTR))
					{
						kernelError(kernel_error, "API function %x argument "
							"%d: Pointer must point to kernel memory",
							functionEntry->functionNumber, count);
						if (functionEntry->returnType == type_ptr)
							status = NULL;
						else
							status = ERR_PERMISSION;
						goto out;
					}
					break;

				case type_val:
					if (!functionArgs[count] &&
						(functionEntry->args[count].content &
							API_ARG_NONZEROVAL))
					{
						kernelError(kernel_error, "API function %x argument "
							"%d: Value must be non-zero",
							functionEntry->functionNumber, count);
						if (functionEntry->returnType == type_ptr)
							status = NULL;
						else
							status = ERR_NULLPARAMETER;
						goto out;
					}
					if (((int) functionArgs[count] < 0) &&
						(functionEntry->args[count].content &
							API_ARG_POSINTVAL))
					{
						kernelError(kernel_error, "API function %x argument "
							"%d: Value must be a positive integer",
							functionEntry->functionNumber, count);
						if (functionEntry->returnType == type_ptr)
							status = NULL;
						else
							status = ERR_RANGE;
						goto out;
					}
					break;

				default:
					break;
			}
		}

		pushCount += functionEntry->args[count].dwords;
	}

	// Push each of the args onto the current stack
	for (count = (pushCount - 1); count >= 0; count --)
		processorPush(functionArgs[count]);

	// Call the function
	status = functionPointer();

out:
	#if defined(DEBUG)
	kernelDebug(debug_api, "ret=%lld", status);
	#endif

	processorApiExit(stackAddress, (int) status, (status >> 32));
}

