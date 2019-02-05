//
//  Visopsys
//  Copyright (C) 1998-2006 J. Andrew McLaughlin
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
//  59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
//
//  kernelApi.c
//
	
// This is the part of the kernel's API that sorts out which functions
// get called from external locations.

#include "kernelApi.h"
#include "kernelDebug.h"
#include "kernelDisk.h"
#include "kernelEncrypt.h"
#include "kernelEnvironment.h"
#include "kernelError.h"
#include "kernelFile.h"
#include "kernelFileStream.h"
#include "kernelFilesystem.h"
#include "kernelKeyboard.h"
#include "kernelLoader.h"
#include "kernelMemory.h"
#include "kernelMisc.h"
#include "kernelMultitasker.h"
#include "kernelNetwork.h"
#include "kernelNetworkDevice.h"
#include "kernelParameters.h"
#include "kernelProcessorX86.h"
#include "kernelRandom.h"
#include "kernelRtc.h"
#include "kernelShutdown.h"
#include "kernelText.h"
#include "kernelUser.h"
#include "kernelWindow.h"
// next one added by Davide Airaghi
#include "kernelRamDiskDriver.h"

// We do this so that <sys/api.h> won't complain about being included
// in a kernel file
#undef KERNEL
#include <sys/api.h>
#define KERNEL

static kernelFunctionIndex textFunctionIndex[] = {

  // Text input/output functions (1000-1999 range)

  { _fnum_textGetConsoleInput, kernelTextGetConsoleInput, 0, PRIVILEGE_USER },
  { _fnum_textSetConsoleInput, kernelTextSetConsoleInput,
    1, PRIVILEGE_SUPERVISOR },
  { _fnum_textGetConsoleOutput, kernelTextGetConsoleOutput,
    0, PRIVILEGE_USER },
  { _fnum_textSetConsoleOutput, kernelTextSetConsoleOutput,
    1, PRIVILEGE_SUPERVISOR },
  { _fnum_textGetCurrentInput, kernelTextGetCurrentInput, 0, PRIVILEGE_USER },
  { _fnum_textSetCurrentInput, kernelTextSetCurrentInput, 1, PRIVILEGE_USER },
  { _fnum_textGetCurrentOutput, kernelTextGetCurrentOutput,
    0, PRIVILEGE_USER },
  { _fnum_textSetCurrentOutput, kernelTextSetCurrentOutput,
    1, PRIVILEGE_USER },
  { _fnum_textGetForeground, kernelTextGetForeground, 0, PRIVILEGE_USER },
  { _fnum_textSetForeground, kernelTextSetForeground, 1, PRIVILEGE_USER },
  { _fnum_textGetBackground, kernelTextGetBackground, 0, PRIVILEGE_USER },
  { _fnum_textSetBackground, kernelTextSetBackground, 1, PRIVILEGE_USER },
  { _fnum_textPutc, kernelTextPutc, 1, PRIVILEGE_USER },
  { _fnum_textPrint, kernelTextPrint, 1, PRIVILEGE_USER },
  { _fnum_textPrintLine, kernelTextPrintLine, 1, PRIVILEGE_USER },
  { _fnum_textNewline, kernelTextNewline, 0, PRIVILEGE_USER },
  { _fnum_textBackSpace, kernelTextBackSpace, 0, PRIVILEGE_USER },
  { _fnum_textTab, kernelTextTab, 0, PRIVILEGE_USER },
  { _fnum_textCursorUp, kernelTextCursorUp, 0, PRIVILEGE_USER },
  { _fnum_textCursorDown, kernelTextCursorDown, 0, PRIVILEGE_USER },
  { _fnum_ternelTextCursorLeft, kernelTextCursorLeft, 0, PRIVILEGE_USER },
  { _fnum_textCursorRight, kernelTextCursorRight, 0, PRIVILEGE_USER },
  { _fnum_textEnableScroll, kernelTextEnableScroll, 1, PRIVILEGE_USER },
  { _fnum_textScroll, kernelTextScroll, 1, PRIVILEGE_USER },
  { _fnum_textGetNumColumns, kernelTextGetNumColumns, 0, PRIVILEGE_USER },
  { _fnum_textGetNumRows, kernelTextGetNumRows, 0, PRIVILEGE_USER },
  { _fnum_textGetColumn, kernelTextGetColumn, 0, PRIVILEGE_USER },
  { _fnum_textSetColumn, kernelTextSetColumn, 1, PRIVILEGE_USER },
  { _fnum_textGetRow, kernelTextGetRow, 0, PRIVILEGE_USER },
  { _fnum_textSetRow, kernelTextSetRow, 1, PRIVILEGE_USER },
  { _fnum_textSetCursor, kernelTextSetCursor, 1, PRIVILEGE_USER },
  { _fnum_textScreenClear, kernelTextScreenClear, 0, PRIVILEGE_USER },
  { _fnum_textScreenSave, kernelTextScreenSave, 1, PRIVILEGE_USER },
  { _fnum_textScreenRestore, kernelTextScreenRestore, 1, PRIVILEGE_USER },
  { _fnum_textInputStreamCount, kernelTextInputStreamCount,
    1, PRIVILEGE_USER },
  { _fnum_textInputCount, kernelTextInputCount, 0, PRIVILEGE_USER },
  { _fnum_textInputStreamGetc, kernelTextInputStreamGetc, 2, PRIVILEGE_USER },
  { _fnum_textInputGetc, kernelTextInputGetc, 1, PRIVILEGE_USER },
  { _fnum_textInputStreamReadN, kernelTextInputStreamReadN,
    3, PRIVILEGE_USER },
  { _fnum_textInputReadN, kernelTextInputReadN, 2, PRIVILEGE_USER },
  { _fnum_textInputStreamReadAll, kernelTextInputStreamReadAll,
    2, PRIVILEGE_USER },
  { _fnum_textInputReadAll, kernelTextInputReadAll, 1, PRIVILEGE_USER },
  { _fnum_textInputStreamAppend, kernelTextInputStreamAppend,
    2, PRIVILEGE_USER },
  { _fnum_textInputAppend, kernelTextInputAppend, 1, PRIVILEGE_USER },
  { _fnum_textInputStreamAppendN, kernelTextInputStreamAppendN,
    3, PRIVILEGE_USER },
  { _fnum_textInputAppendN, kernelTextInputAppendN, 2, PRIVILEGE_USER },
  { _fnum_textInputStreamRemove, kernelTextInputStreamRemove,
    1, PRIVILEGE_USER },
  { _fnum_textInputRemove, kernelTextInputRemove, 0, PRIVILEGE_USER },
  { _fnum_textInputStreamRemoveN, kernelTextInputStreamRemoveN,
    2, PRIVILEGE_USER },
  { _fnum_textInputRemoveN, kernelTextInputRemoveN, 1, PRIVILEGE_USER },
  { _fnum_textInputStreamRemoveAll, kernelTextInputStreamRemoveAll,
    1, PRIVILEGE_USER },
  { _fnum_textInputRemoveAll, kernelTextInputRemoveAll, 0, PRIVILEGE_USER },
  { _fnum_textInputStreamSetEcho, kernelTextInputStreamSetEcho,
    2, PRIVILEGE_USER },
  { _fnum_textInputSetEcho, kernelTextInputSetEcho, 1,  PRIVILEGE_USER }
};

static kernelFunctionIndex diskFunctionIndex[] = {

  // Disk functions (2000-2999 range)

  { _fnum_diskReadPartitions, kernelDiskReadPartitions,
    1, PRIVILEGE_SUPERVISOR },
  { _fnum_diskReadPartitionsAll, kernelDiskReadPartitionsAll,
    0, PRIVILEGE_SUPERVISOR },
  { _fnum_diskSync, kernelDiskSync, 0, PRIVILEGE_USER },
  { _fnum_diskGetBoot, kernelDiskGetBoot, 1, PRIVILEGE_USER },
  { _fnum_diskGetCount, kernelDiskGetCount, 0, PRIVILEGE_USER },
  { _fnum_diskGetPhysicalCount, kernelDiskGetPhysicalCount,
    0, PRIVILEGE_USER },
  { _fnum_diskGet, kernelDiskGet, 2, PRIVILEGE_USER },
  { _fnum_diskGetAll, kernelDiskGetAll, 2, PRIVILEGE_USER },
  { _fnum_diskGetAllPhysical, kernelDiskGetAllPhysical, 2, PRIVILEGE_USER },
  { _fnum_diskGetPartType, kernelDiskGetPartType, 2, PRIVILEGE_USER },
  { _fnum_diskGetPartTypes, kernelDiskGetPartTypes, 0, PRIVILEGE_USER },
  { _fnum_diskSetLockState, kernelDiskSetLockState, 2, PRIVILEGE_USER },
  { _fnum_diskSetDoorState, kernelDiskSetDoorState, 2, PRIVILEGE_USER },
  { _fnum_diskGetMediaState, kernelDiskGetMediaState, 1, PRIVILEGE_USER },
  { _fnum_diskReadSectors, kernelDiskReadSectors, 4, PRIVILEGE_SUPERVISOR },
  { _fnum_diskWriteSectors, kernelDiskWriteSectors, 4, PRIVILEGE_SUPERVISOR },
  { _fnum_diskGetFilesystemType, kernelDiskGetFilesystemType, 3, PRIVILEGE_USER },
  // next three added by Davide Airaghi
  { _fnum_diskRamDiskCreate, kernelRamDiskCreate,  2, PRIVILEGE_SUPERVISOR },
  { _fnum_diskRamDiskDestroy, kernelRamDiskDestroy,  1, PRIVILEGE_SUPERVISOR },
  { _fnum_diskRamDiskInfo, kernelRamDiskInfo,  2, PRIVILEGE_USER },
};

static kernelFunctionIndex filesystemFunctionIndex[] = {

  // Filesystem functions (3000-3999 range)

  { _fnum_filesystemFormat, kernelFilesystemFormat, 5, PRIVILEGE_SUPERVISOR },
  { _fnum_filesystemClobber, kernelFilesystemClobber,
    1, PRIVILEGE_SUPERVISOR },
  { _fnum_filesystemCheck, kernelFilesystemCheck, 4, PRIVILEGE_USER },
  { _fnum_filesystemDefragment, kernelFilesystemDefragment,
    2, PRIVILEGE_SUPERVISOR },
  { _fnum_filesystemResizeConstraints, kernelFilesystemResizeConstraints,
    3, PRIVILEGE_USER },
  { _fnum_filesystemResize, kernelFilesystemResize, 3, PRIVILEGE_SUPERVISOR },
  { _fnum_filesystemMount, kernelFilesystemMount, 3, PRIVILEGE_USER },
  { _fnum_filesystemUnmount, kernelFilesystemUnmount, 1, PRIVILEGE_USER },
  { _fnum_filesystemGetFree, kernelFilesystemGetFree, 1, PRIVILEGE_USER },
  { _fnum_filesystemGetBlockSize, kernelFilesystemGetBlockSize,
    1, PRIVILEGE_USER }
};

static kernelFunctionIndex fileFunctionIndex[] = {

  // File functions (4000-4999 range)

  { _fnum_fileFixupPath, kernelFileFixupPath, 2, PRIVILEGE_USER },
  { _fnum_fileSeparateLast, kernelFileSeparateLast, 3, PRIVILEGE_USER },
  { _fnum_fileGetDisk, kernelFileGetDisk, 2, PRIVILEGE_USER },
  { _fnum_fileCount, kernelFileCount, 1, PRIVILEGE_USER },
  { _fnum_fileFirst, kernelFileFirst, 2, PRIVILEGE_USER },
  { _fnum_fileNext, kernelFileNext, 2, PRIVILEGE_USER },
  { _fnum_fileFind, kernelFileFind, 2, PRIVILEGE_USER },
  { _fnum_fileOpen, kernelFileOpen, 3, PRIVILEGE_USER },
  { _fnum_fileClose, kernelFileClose, 1, PRIVILEGE_USER },
  { _fnum_fileRead, kernelFileRead, 4, PRIVILEGE_USER },
  { _fnum_fileWrite, kernelFileWrite, 4, PRIVILEGE_USER },
  { _fnum_fileDelete, kernelFileDelete, 1, PRIVILEGE_USER },
  { _fnum_fileDeleteRecursive, kernelFileDeleteRecursive, 1, PRIVILEGE_USER },
  { _fnum_fileDeleteSecure, kernelFileDeleteSecure, 1, PRIVILEGE_USER },
  { _fnum_fileMakeDir, kernelFileMakeDir, 1, PRIVILEGE_USER },
  { _fnum_fileRemoveDir, kernelFileRemoveDir, 1, PRIVILEGE_USER },
  { _fnum_fileCopy, kernelFileCopy, 2, PRIVILEGE_USER },
  { _fnum_fileCopyRecursive, kernelFileCopyRecursive, 2, PRIVILEGE_USER },
  { _fnum_fileMove, kernelFileMove, 2, PRIVILEGE_USER },
  { _fnum_fileTimestamp, kernelFileTimestamp, 1, PRIVILEGE_USER },
  { _fnum_fileGetTemp, kernelFileGetTemp, 1, PRIVILEGE_USER },
  { _fnum_fileStreamOpen, kernelFileStreamOpen, 3, PRIVILEGE_USER },
  { _fnum_fileStreamSeek, kernelFileStreamSeek, 2, PRIVILEGE_USER },
  { _fnum_fileStreamRead, kernelFileStreamRead, 3, PRIVILEGE_USER },
  { _fnum_fileStreamReadLine, kernelFileStreamReadLine, 3, PRIVILEGE_USER },
  { _fnum_fileStreamWrite, kernelFileStreamWrite, 3, PRIVILEGE_USER },
  { _fnum_fileStreamWriteStr, kernelFileStreamWriteStr, 2, PRIVILEGE_USER },
  { _fnum_fileStreamWriteLine, kernelFileStreamWriteLine, 2, PRIVILEGE_USER },
  { _fnum_fileStreamFlush, kernelFileStreamFlush, 1, PRIVILEGE_USER },
  { _fnum_fileStreamClose, kernelFileStreamClose, 1, PRIVILEGE_USER }
};

static kernelFunctionIndex memoryFunctionIndex[] = {

  // Memory manager functions (5000-5999 range)

  { _fnum_memoryGet, kernelMemoryGet, 2, PRIVILEGE_USER },
  { _fnum_memoryGetPhysical, kernelMemoryGetPhysical,
    3, PRIVILEGE_SUPERVISOR },
  { _fnum_memoryRelease, kernelMemoryRelease, 1, PRIVILEGE_USER },
  { _fnum_memoryReleaseAllByProcId, kernelMemoryReleaseAllByProcId,
    1, PRIVILEGE_USER },
  { _fnum_memoryChangeOwner, kernelMemoryChangeOwner,
    4, PRIVILEGE_SUPERVISOR },
  { _fnum_memoryGetStats, kernelMemoryGetStats, 2, PRIVILEGE_USER },
  { _fnum_memoryGetBlocks, kernelMemoryGetBlocks, 3, PRIVILEGE_USER },
  { _fnum_memoryBlockInfo, kernelMemoryBlockInfo, 2, PRIVILEGE_USER }
};

static kernelFunctionIndex multitaskerFunctionIndex[] = {

  // Multitasker functions (6000-6999 range)

  { _fnum_multitaskerCreateProcess, kernelMultitaskerCreateProcess,
    3, PRIVILEGE_USER },
  { _fnum_multitaskerSpawn, kernelMultitaskerSpawn, 4, PRIVILEGE_USER },
  { _fnum_multitaskerGetCurrentProcessId, kernelMultitaskerGetCurrentProcessId,
    0, PRIVILEGE_USER },
  { _fnum_multitaskerGetProcess, kernelMultitaskerGetProcess,
    2, PRIVILEGE_USER },
  { _fnum_multitaskerGetProcessByName, kernelMultitaskerGetProcessByName,
    2, PRIVILEGE_USER },
  { _fnum_multitaskerGetProcesses, kernelMultitaskerGetProcesses,
    2, PRIVILEGE_USER },
  { _fnum_multitaskerSetProcessState, kernelMultitaskerSetProcessState,
    2, PRIVILEGE_USER },
  { _fnum_multitaskerProcessIsAlive, kernelMultitaskerProcessIsAlive,
    1, PRIVILEGE_USER },
  { _fnum_multitaskerSetProcessPriority, kernelMultitaskerSetProcessPriority,
    2, PRIVILEGE_USER },
  { _fnum_multitaskerGetProcessPrivilege, kernelMultitaskerGetProcessPrivilege,
    1, PRIVILEGE_USER },
  { _fnum_multitaskerGetCurrentDirectory, kernelMultitaskerGetCurrentDirectory,
    2, PRIVILEGE_USER },
  { _fnum_multitaskerSetCurrentDirectory, kernelMultitaskerSetCurrentDirectory,
    1, PRIVILEGE_USER },
  { _fnum_multitaskerGetTextInput, kernelMultitaskerGetTextInput,
    0, PRIVILEGE_USER },
  { _fnum_multitaskerSetTextInput, kernelMultitaskerSetTextInput,
    2, PRIVILEGE_USER },
  { _fnum_multitaskerGetTextOutput, kernelMultitaskerGetTextOutput,
    0, PRIVILEGE_USER },
  { _fnum_multitaskerSetTextOutput, kernelMultitaskerSetTextOutput,
    2, PRIVILEGE_USER },
  { _fnum_multitaskerDuplicateIO, kernelMultitaskerDuplicateIO,
    3, PRIVILEGE_USER },
  { _fnum_multitaskerGetProcessorTime, kernelMultitaskerGetProcessorTime, 
    1, PRIVILEGE_USER },
  { _fnum_multitaskerYield, kernelMultitaskerYield, 0, PRIVILEGE_USER },
  { _fnum_multitaskerWait, kernelMultitaskerWait, 1, PRIVILEGE_USER },
  { _fnum_multitaskerBlock, kernelMultitaskerBlock, 1, PRIVILEGE_USER },
  { _fnum_multitaskerDetach, kernelMultitaskerDetach, 0, PRIVILEGE_USER },
  { _fnum_multitaskerKillProcess, kernelMultitaskerKillProcess,
    2, PRIVILEGE_USER },
  { _fnum_multitaskerKillByName, kernelMultitaskerKillByName, 2,
    PRIVILEGE_USER },
  { _fnum_multitaskerTerminate, kernelMultitaskerTerminate,
    1, PRIVILEGE_USER },
  { _fnum_multitaskerSignalSet, kernelMultitaskerSignalSet,
    3, PRIVILEGE_USER },
  { _fnum_multitaskerSignal, kernelMultitaskerSignal, 2, PRIVILEGE_USER },
  { _fnum_multitaskerSignalRead, kernelMultitaskerSignalRead,
    1, PRIVILEGE_USER },
  { _fnum_multitaskerGetIOPerm, kernelMultitaskerGetIOPerm, 2, PRIVILEGE_USER},
  { _fnum_multitaskerSetIOPerm, kernelMultitaskerSetIOPerm,
    3, PRIVILEGE_SUPERVISOR}
};

static kernelFunctionIndex loaderFunctionIndex[] = {

  // Loader functions (7000-7999 range)

  { _fnum_loaderLoad, kernelLoaderLoad, 2, PRIVILEGE_USER },
  { _fnum_loaderClassify, kernelLoaderClassify, 4, PRIVILEGE_USER },
  { _fnum_loaderClassifyFile, kernelLoaderClassifyFile, 2, PRIVILEGE_USER },
  { _fnum_loaderGetSymbols, kernelLoaderGetSymbols, 2, PRIVILEGE_USER },
  { _fnum_loaderLoadProgram, kernelLoaderLoadProgram, 2, PRIVILEGE_USER },
  { _fnum_loaderLoadLibrary, kernelLoaderLoadLibrary, 1, PRIVILEGE_USER },
  { _fnum_loaderExecProgram, kernelLoaderExecProgram, 2, PRIVILEGE_USER },
  { _fnum_loaderLoadAndExec, kernelLoaderLoadAndExec,  3, PRIVILEGE_USER }
};

static kernelFunctionIndex rtcFunctionIndex[] = {

  // Real-time clock functions (8000-8999 range)

  { _fnum_rtcReadSeconds, kernelRtcReadSeconds, 0, PRIVILEGE_USER },
  { _fnum_rtcReadMinutes, kernelRtcReadMinutes, 0, PRIVILEGE_USER },
  { _fnum_rtcReadHours, kernelRtcReadHours, 0, PRIVILEGE_USER },
  { _fnum_rtcDayOfWeek, kernelRtcDayOfWeek, 3, PRIVILEGE_USER },
  { _fnum_rtcReadDayOfMonth, kernelRtcReadDayOfMonth, 0, PRIVILEGE_USER },
  { _fnum_rtcReadMonth, kernelRtcReadMonth, 0, PRIVILEGE_USER },
  { _fnum_rtcReadYear, kernelRtcReadYear, 0, PRIVILEGE_USER },
  { _fnum_rtcUptimeSeconds, kernelRtcUptimeSeconds, 0, PRIVILEGE_USER },
  { _fnum_rtcDateTime, kernelRtcDateTime, 1, PRIVILEGE_USER }
};

static kernelFunctionIndex randomFunctionIndex[] = {

  // Random number functions (9000-9999 range)

  { _fnum_randomUnformatted, kernelRandomUnformatted, 0, PRIVILEGE_USER },
  { _fnum_randomFormatted, kernelRandomFormatted, 2, PRIVILEGE_USER },
  { _fnum_randomSeededUnformatted, kernelRandomSeededUnformatted,
    1, PRIVILEGE_USER },
  { _fnum_randomSeededFormatted, kernelRandomSeededFormatted,
    3, PRIVILEGE_USER }
};

static kernelFunctionIndex environmentFunctionIndex[] = {
  
  // Environment functions (10000-10999 range)

  { _fnum_environmentGet, kernelEnvironmentGet, 3, PRIVILEGE_USER },
  { _fnum_environmentSet, kernelEnvironmentSet, 2, PRIVILEGE_USER },
  { _fnum_environmentUnset, kernelEnvironmentUnset, 1, PRIVILEGE_USER },
  { _fnum_environmentDump, kernelEnvironmentDump, 0, PRIVILEGE_USER }
};

static kernelFunctionIndex graphicFunctionIndex[] = {
  
  // Raw graphics functions (11000-11999 range)

  { _fnum_graphicsAreEnabled, kernelGraphicsAreEnabled, 0, PRIVILEGE_USER },
  { _fnum_graphicGetModes, kernelGraphicGetModes, 2, PRIVILEGE_USER },
  { _fnum_graphicGetMode, kernelGraphicGetMode, 1, PRIVILEGE_USER },
  { _fnum_graphicSetMode, kernelGraphicSetMode, 1, PRIVILEGE_SUPERVISOR },
  { _fnum_graphicGetScreenWidth, kernelGraphicGetScreenWidth,
    0, PRIVILEGE_USER },
  { _fnum_graphicGetScreenHeight, kernelGraphicGetScreenHeight,
    0, PRIVILEGE_USER },
  { _fnum_graphicCalculateAreaBytes, kernelGraphicCalculateAreaBytes,
    2, PRIVILEGE_USER },
  { _fnum_graphicClearScreen, kernelGraphicClearScreen, 1, PRIVILEGE_USER },
  { _fnum_graphicGetColor, kernelGraphicGetColor, 2, PRIVILEGE_USER },
  { _fnum_graphicSetColor, kernelGraphicSetColor, 2, PRIVILEGE_USER },
  { _fnum_graphicDrawPixel, kernelGraphicDrawPixel, 5, PRIVILEGE_USER },
  { _fnum_graphicDrawLine, kernelGraphicDrawLine, 7, PRIVILEGE_USER },
  { _fnum_graphicDrawRect, kernelGraphicDrawRect, 9, PRIVILEGE_USER },
  { _fnum_graphicDrawOval, kernelGraphicDrawOval, 9, PRIVILEGE_USER },
  { _fnum_graphicDrawImage, kernelGraphicDrawImage, 9, PRIVILEGE_USER },
  { _fnum_graphicGetImage, kernelGraphicGetImage, 6, PRIVILEGE_USER },
  { _fnum_graphicDrawText, kernelGraphicDrawText, 8, PRIVILEGE_USER },
  { _fnum_graphicCopyArea, kernelGraphicCopyArea, 7, PRIVILEGE_USER },
  { _fnum_graphicClearArea, kernelGraphicClearArea, 6, PRIVILEGE_USER },
  { _fnum_graphicRenderBuffer, kernelGraphicRenderBuffer, 7, PRIVILEGE_USER }
};

static kernelFunctionIndex windowFunctionIndex[] = {
  
  // Windowing system functions (12000-12999 range)

  { _fnum_windowLogin, kernelWindowLogin, 1, PRIVILEGE_SUPERVISOR },
  { _fnum_windowLogout, kernelWindowLogout, 0, PRIVILEGE_USER },
  { _fnum_windowNew, kernelWindowNew, 2, PRIVILEGE_USER },
  { _fnum_windowNewDialog, kernelWindowNewDialog, 2, PRIVILEGE_USER },
  { _fnum_windowDestroy, kernelWindowDestroy, 1, PRIVILEGE_USER },
  { _fnum_windowUpdateBuffer, kernelWindowUpdateBuffer, 5, PRIVILEGE_USER },
  { _fnum_windowSetTitle, kernelWindowSetTitle, 2, PRIVILEGE_USER },
  { _fnum_windowGetSize, kernelWindowGetSize, 3, PRIVILEGE_USER },
  { _fnum_windowSetSize, kernelWindowSetSize, 3, PRIVILEGE_USER },
  { _fnum_windowGetLocation, kernelWindowGetLocation, 3, PRIVILEGE_USER },
  { _fnum_windowSetLocation, kernelWindowSetLocation, 3, PRIVILEGE_USER },
  { _fnum_windowCenter, kernelWindowCenter, 1, PRIVILEGE_USER },
  { _fnum_windowSnapIcons, kernelWindowSnapIcons, 1, PRIVILEGE_USER },
  { _fnum_windowSetHasBorder, kernelWindowSetHasBorder, 2, PRIVILEGE_USER },
  { _fnum_windowSetHasTitleBar, kernelWindowSetHasTitleBar,
    2, PRIVILEGE_USER },
  { _fnum_windowSetMovable, kernelWindowSetMovable, 2, PRIVILEGE_USER },
  { _fnum_windowSetResizable, kernelWindowSetResizable, 2, PRIVILEGE_USER },
  { _fnum_windowRemoveMinimizeButton, kernelWindowRemoveMinimizeButton,
    1, PRIVILEGE_USER },
  { _fnum_windowRemoveCloseButton, kernelWindowRemoveCloseButton,
    1, PRIVILEGE_USER },
  { _fnum_windowSetColors, kernelWindowSetColors, 2, PRIVILEGE_USER },
  { _fnum_windowSetVisible, kernelWindowSetVisible, 2, PRIVILEGE_USER },
  { _fnum_windowSetMinimized, kernelWindowSetMinimized, 2, PRIVILEGE_USER },
  { _fnum_windowAddConsoleTextArea, kernelWindowAddConsoleTextArea,
    1, PRIVILEGE_USER },
  { _fnum_windowRedrawArea, kernelWindowRedrawArea, 4, PRIVILEGE_USER },
  { _fnum_windowDrawAll, kernelWindowDrawAll, 0, PRIVILEGE_USER },
  { _fnum_windowResetColors, kernelWindowResetColors, 0, PRIVILEGE_USER },
  { _fnum_windowProcessEvent, kernelWindowProcessEvent, 1, PRIVILEGE_USER },
  { _fnum_windowComponentEventGet, kernelWindowComponentEventGet,
    2, PRIVILEGE_USER },
  { _fnum_windowTileBackground, kernelWindowTileBackground,
    1, PRIVILEGE_USER },
  { _fnum_windowCenterBackground, kernelWindowCenterBackground,
    1, PRIVILEGE_USER },
  { _fnum_windowScreenShot, kernelWindowScreenShot, 1, PRIVILEGE_USER },
  { _fnum_windowSaveScreenShot, kernelWindowSaveScreenShot, 
    1, PRIVILEGE_USER },
  { _fnum_windowSetTextOutput, kernelWindowSetTextOutput, 1, PRIVILEGE_USER },
  { _fnum_windowLayout, kernelWindowLayout, 1, PRIVILEGE_USER },
  { _fnum_windowDebugLayout, kernelWindowDebugLayout, 1, PRIVILEGE_USER },
  { _fnum_windowContextAdd, kernelWindowContextAdd, 2, PRIVILEGE_USER },
  { _fnum_windowContextSet, kernelWindowContextSet, 2, PRIVILEGE_USER },
  { _fnum_windowSwitchPointer, kernelWindowSwitchPointer, 2, PRIVILEGE_USER },
  { _fnum_windowComponentDestroy, kernelWindowComponentDestroy,
    1, PRIVILEGE_USER },
  { _fnum_windowComponentSetVisible, kernelWindowComponentSetVisible,
    2, PRIVILEGE_USER },
  { _fnum_windowComponentSetEnabled, kernelWindowComponentSetEnabled,
    2, PRIVILEGE_USER },
  { _fnum_windowComponentGetWidth, kernelWindowComponentGetWidth,
    1, PRIVILEGE_USER },
  { _fnum_windowComponentSetWidth, kernelWindowComponentSetWidth,
    2, PRIVILEGE_USER },
  { _fnum_windowComponentGetHeight, kernelWindowComponentGetHeight,
    1, PRIVILEGE_USER },
  { _fnum_windowComponentSetHeight, kernelWindowComponentSetHeight,
    2, PRIVILEGE_USER },
  { _fnum_windowComponentFocus, kernelWindowComponentFocus,
    1, PRIVILEGE_USER },
  { _fnum_windowComponentDraw, kernelWindowComponentDraw, 1, PRIVILEGE_USER },
  { _fnum_windowComponentGetData, kernelWindowComponentGetData,
    3, PRIVILEGE_USER },
  { _fnum_windowComponentSetData, kernelWindowComponentSetData,
    3, PRIVILEGE_USER },
  { _fnum_windowComponentGetSelected, kernelWindowComponentGetSelected,
    2, PRIVILEGE_USER },
  { _fnum_windowComponentSetSelected, kernelWindowComponentSetSelected,
    2, PRIVILEGE_USER },
  { _fnum_windowNewButton, kernelWindowNewButton, 4, PRIVILEGE_USER },
  { _fnum_windowNewCanvas, kernelWindowNewCanvas, 4, PRIVILEGE_USER },
  { _fnum_windowNewCheckbox, kernelWindowNewCheckbox, 3, PRIVILEGE_USER },
  { _fnum_windowNewContainer, kernelWindowNewContainer, 3, PRIVILEGE_USER },
  { _fnum_windowNewIcon, kernelWindowNewIcon, 4, PRIVILEGE_USER },
  { _fnum_windowNewImage, kernelWindowNewImage, 4, PRIVILEGE_USER },
  { _fnum_windowNewList, kernelWindowNewList, 8, PRIVILEGE_USER },
  { _fnum_windowNewListItem, kernelWindowNewListItem, 3, PRIVILEGE_USER },
  { _fnum_windowNewMenu, kernelWindowNewMenu, 4, PRIVILEGE_USER },
  { _fnum_windowNewMenuBar, kernelWindowNewMenuBar, 2, PRIVILEGE_USER },
  { _fnum_windowNewMenuItem, kernelWindowNewMenuItem, 3, PRIVILEGE_USER },
  { _fnum_windowNewPasswordField, kernelWindowNewPasswordField,
    3, PRIVILEGE_USER },
  { _fnum_windowNewProgressBar, kernelWindowNewProgressBar,
    2, PRIVILEGE_USER },
  { _fnum_windowNewRadioButton, kernelWindowNewRadioButton,
    6, PRIVILEGE_USER },
  { _fnum_windowNewScrollBar, kernelWindowNewScrollBar, 5, PRIVILEGE_USER },
  { _fnum_windowNewSlider, kernelWindowNewSlider, 5, PRIVILEGE_USER },
  { _fnum_windowNewTextArea, kernelWindowNewTextArea, 5, PRIVILEGE_USER },
  { _fnum_windowNewTextField, kernelWindowNewTextField, 3, PRIVILEGE_USER },
  { _fnum_windowNewTextLabel, kernelWindowNewTextLabel, 3, PRIVILEGE_USER }
};

static kernelFunctionIndex userFunctionIndex[] = {

  // User functions (13000-13999 range)

  { _fnum_userAuthenticate, kernelUserAuthenticate, 2, PRIVILEGE_USER },
  { _fnum_userLogin, kernelUserLogin, 2, PRIVILEGE_SUPERVISOR },
  { _fnum_userLogout, kernelUserLogout, 1, PRIVILEGE_USER },
  { _fnum_userGetNames, kernelUserGetNames, 2, PRIVILEGE_USER },
  { _fnum_userAdd, kernelUserAdd, 2, PRIVILEGE_SUPERVISOR },
  { _fnum_userDelete, kernelUserDelete, 1, PRIVILEGE_SUPERVISOR },
  { _fnum_userSetPassword, kernelUserSetPassword, 3, PRIVILEGE_USER },
  { _fnum_userGetPrivilege, kernelUserGetPrivilege, 1, PRIVILEGE_USER },
  { _fnum_userGetPid, kernelUserGetPid, 0, PRIVILEGE_USER },
  { _fnum_userSetPid, kernelUserSetPid, 2, PRIVILEGE_SUPERVISOR },
  { _fnum_userFileAdd, kernelUserFileAdd, 3, PRIVILEGE_SUPERVISOR },
  { _fnum_userFileDelete, kernelUserFileDelete, 2, PRIVILEGE_SUPERVISOR },
  { _fnum_userFileSetPassword, kernelUserFileSetPassword, 4, PRIVILEGE_USER }
};

static kernelFunctionIndex networkFunctionIndex[] = {

  // Network functions (14000-14999 range)

  { _fnum_networkDeviceGetCount, kernelNetworkDeviceGetCount,
    0, PRIVILEGE_USER },
  { _fnum_networkDeviceGet, kernelNetworkDeviceGet, 2, PRIVILEGE_USER },
  { _fnum_networkInitialized, kernelNetworkInitialized, 0, PRIVILEGE_USER },
  { _fnum_networkInitialize, kernelNetworkInitialize,
    0, PRIVILEGE_SUPERVISOR },
  { _fnum_networkShutdown, kernelNetworkShutdown, 0, PRIVILEGE_SUPERVISOR },
  { _fnum_networkOpen, kernelNetworkOpen, 3, PRIVILEGE_USER },
  { _fnum_networkClose, kernelNetworkClose, 1, PRIVILEGE_USER },
  { _fnum_networkCount, kernelNetworkCount, 1, PRIVILEGE_USER },
  { _fnum_networkRead, kernelNetworkRead, 3, PRIVILEGE_USER },
  { _fnum_networkWrite, kernelNetworkWrite, 3, PRIVILEGE_USER },
  { _fnum_networkPing, kernelNetworkPing, 4, PRIVILEGE_USER },
  { _fnum_networkGetHostName, kernelNetworkGetHostName, 2, PRIVILEGE_USER },
  { _fnum_networkSetHostName, kernelNetworkSetHostName,
    2, PRIVILEGE_SUPERVISOR },
  { _fnum_networkGetDomainName, kernelNetworkGetDomainName,
    2, PRIVILEGE_USER },
  { _fnum_networkSetDomainName, kernelNetworkSetDomainName,
    2, PRIVILEGE_SUPERVISOR },
};

static kernelFunctionIndex miscFunctionIndex[] = {

  // Miscellaneous functions (99000-99999 range)
  
  { _fnum_fontGetDefault, kernelFontGetDefault, 1, PRIVILEGE_USER },
  { _fnum_fontSetDefault, kernelFontSetDefault, 1, PRIVILEGE_USER },
  { _fnum_fontLoad, kernelFontLoad, 4, PRIVILEGE_USER },
  { _fnum_fontGetPrintedWidth, kernelFontGetPrintedWidth, 2, PRIVILEGE_USER },
  { _fnum_fontGetWidth, kernelFontGetWidth, 1, PRIVILEGE_USER },
  { _fnum_fontGetHeight, kernelFontGetHeight, 1, PRIVILEGE_USER },
  { _fnum_imageLoad, kernelImageLoad, 4, PRIVILEGE_USER },
  { _fnum_imageSave, kernelImageSave, 3, PRIVILEGE_USER },
  { _fnum_shutdown, kernelShutdown, 2, PRIVILEGE_USER },
  { _fnum_getVersion, kernelGetVersion, 2, PRIVILEGE_USER },
  { _fnum_systemInfo, kernelSystemInfo, 1, PRIVILEGE_USER },
  { _fnum_encryptMD5, kernelEncryptMD5, 2, PRIVILEGE_USER },
  { _fnum_lockGet, kernelLockGet, 1, PRIVILEGE_USER },
  { _fnum_lockRelease, kernelLockRelease, 1, PRIVILEGE_USER },
  { _fnum_lockVerify, kernelLockVerify, 1, PRIVILEGE_USER },
  { _fnum_variableListCreate, kernelVariableListCreate, 1, PRIVILEGE_USER },
  { _fnum_variableListDestroy, kernelVariableListDestroy, 1, PRIVILEGE_USER },
  { _fnum_variableListGet, kernelVariableListGet, 4, PRIVILEGE_USER },
  { _fnum_variableListSet, kernelVariableListSet, 3, PRIVILEGE_USER },
  { _fnum_variableListUnset, kernelVariableListUnset, 2, PRIVILEGE_USER },
  { _fnum_configurationReader, kernelConfigurationReader, 2, PRIVILEGE_USER },
  { _fnum_configurationWriter, kernelConfigurationWriter, 2, PRIVILEGE_USER },
  { _fnum_keyboardGetMaps, kernelKeyboardGetMaps, 2, PRIVILEGE_USER },
  { _fnum_keyboardSetMap, kernelKeyboardSetMap, 1, PRIVILEGE_USER },
  { _fnum_deviceTreeGetCount, kernelDeviceTreeGetCount, 0, PRIVILEGE_USER },
  { _fnum_deviceTreeGetRoot, kernelDeviceTreeGetRoot, 1, PRIVILEGE_USER },
  { _fnum_deviceTreeGetChild, kernelDeviceTreeGetChild, 2, PRIVILEGE_USER },
  { _fnum_deviceTreeGetNext, kernelDeviceTreeGetNext, 1, PRIVILEGE_USER },
  { _fnum_mouseLoadPointer, kernelMouseLoadPointer, 2, PRIVILEGE_USER }
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
  environmentFunctionIndex,
  graphicFunctionIndex,
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


void kernelApi(unsigned CS __attribute__((unused)), unsigned *argList)
{
  // This is the initial entry point for the kernel's API.  This
  // function will be first the recipient of all calls to the global
  // call gate.  This function will pass a pointer to the rest of the
  // arguments to the processCall function that does all the real work.
  // This funcion does the far return.

  int status = 0;
  unsigned stackAddress = 0;
  int argCount = 0;
  int functionNumber = 0;
  kernelFunctionIndex *functionEntry = NULL;
  int currentProc = 0;
  int currentPriv = 0;
  int (*functionPointer)() = NULL;
  int count;
#if defined(DEBUG)
  extern kernelSymbol *kernelSymbols;
  extern int kernelNumberSymbols;
  char *symbol = "unknown";
#endif // defined(DEBUG)

  kernelProcessorApiEnter(stackAddress);

  // Check arg
  if (argList == NULL)
    {
      kernelError(kernel_error, "No args supplied to API call");
      status = ERR_NULLPARAMETER;
      goto out;
    }

  // How many parameters are there?
  argCount = argList[0];
  argCount--;

  // Which function number are we being asked to call?
  functionNumber = argList[1];

  if ((functionNumber < 1000) || (functionNumber > 99999))
    {
      kernelError(kernel_error, "Illegal function number (%d) in API call",
		  functionNumber);
      status = ERR_NOSUCHENTRY;
      goto out;
    }
  if ((argCount < 0) || (argCount > API_MAX_ARGS))
    {
      kernelError(kernel_error, "Illegal number of arguments (%d) to API "
		  "call %d", argCount, argList[1]);
      status = ERR_ARGUMENTCOUNT;
      goto out;
    }

  if ((functionNumber / 1000) == 99)
    // 'misc' functions are in spot 0
    functionEntry = &functionIndex[0][functionNumber % 1000];
  else
    functionEntry =
      &functionIndex[functionNumber / 1000][functionNumber % 1000];

  // Is there such a function?
  if ((functionEntry == NULL) ||
      (functionEntry->functionNumber != functionNumber))
    {
      kernelError(kernel_error, "No such API function %d in API call",
		  functionNumber);
      status = ERR_NOSUCHFUNCTION;
      goto out;
    }

  // Do the number of args match the number expected?
  if (argCount != functionEntry->argCount)
    {
      kernelError(kernel_error, "Incorrect number of arguments (%d) to API "
		  "call %d (%d)", argCount, functionEntry->functionNumber,
		  functionEntry->argCount);
      status = ERR_ARGUMENTCOUNT;
      goto out;
    }

  // Does the caller have the adequate privilege level to call this
  // function?
  currentProc = kernelMultitaskerGetCurrentProcessId();
  currentPriv = kernelMultitaskerGetProcessPrivilege(currentProc);
  if (currentPriv < 0)
    {
      kernelError(kernel_error, "Couldn't determine current privilege level "
		  "in call to API function %d", functionEntry->functionNumber);
      status = currentPriv;
      goto out;
    }
  else if (currentPriv > functionEntry->privilege)
    {
      kernelError(kernel_error, "Insufficient privilege to invoke API "
		  "function %d", functionEntry->functionNumber);
      status = ERR_PERMISSION;
      goto out;
    }

  // Make 'functionPointer' equal the address of the requested kernel
  // function.
  functionPointer = functionEntry->functionPointer;

#if defined(DEBUG)
  if (kernelSymbols)
    {
      for (count = 0; count < kernelNumberSymbols; count ++)
	if (kernelSymbols[count].address == (unsigned) functionPointer)
	  {
	    symbol = kernelSymbols[count].symbol;
	    break;
	  }
    }
  kernelDebug(debug_api, "Kernel API function %d (%s), %d args ",
  	      functionNumber, symbol, argCount);
  for (count = 0; count < argCount; count ++)
    kernelDebug(debug_api, "arg %d=%p", count, (void *) argList[count + 2]);
#endif // defined(DEBUG)

  for (count = (argCount + 1); count > 1; count --)
    kernelProcessorPush(argList[count]);

  status = functionPointer();

  for (count = (argCount + 1); count > 1; count --)
    kernelProcessorPop(argList[count]);

#if defined(DEBUG)
  kernelDebug(debug_api, "ret=%d", status);
#endif

 out:
  kernelProcessorApiExit(stackAddress, status);
}
