//
//	Visopsys
//	Copyright (C) 1998-2018 J. Andrew McLaughlin
//
//	This library is free software; you can redistribute it and/or modify it
//	under the terms of the GNU Lesser General Public License as published by
//	the Free Software Foundation; either version 2.1 of the License, or (at
//	your option) any later version.
//
//	This library is distributed in the hope that it will be useful, but
//	WITHOUT ANY WARRANTY; without even the implied warranty of
//	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	See the GNU Lesser
//	General Public License for more details.
//
//	You should have received a copy of the GNU Lesser General Public License
//	along with this library; if not, write to the Free Software Foundation,
//	Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
//
//	api.h
//

// This file describes all of the functions that are directly exported by
// the Visopsys kernel to the outside world.	All functions and their
// numbers are listed here, as well as macros needed to perform call-gate
// calls into the kernel.	Also, each exported kernel function is represented
// here in the form of a little inline function.

#if !defined(_API_H)

// This file should mostly never be included when we're compiling a kernel
// file (kernelApi.c is an exception)
#if defined(KERNEL)
#error "You cannot call the kernel API from within a kernel function"
#endif

#ifndef _X_
#define _X_
#endif

#include <time.h>
#include <sys/cdefs.h>
#include <sys/device.h>
#include <sys/disk.h>
#include <sys/file.h>
#include <sys/font.h>
#include <sys/graphic.h>
#include <sys/guid.h>
#include <sys/image.h>
#include <sys/keyboard.h>
#include <sys/loader.h>
#include <sys/lock.h>
#include <sys/memory.h>
#include <sys/network.h>
#include <sys/process.h>
#include <sys/progress.h>
#include <sys/stream.h>
#include <sys/text.h>
#include <sys/utsname.h>
#include <sys/variable.h>
#include <sys/window.h>

// Included in the Visopsys standard library to prevent API calls from
// within kernel code.
extern int visopsys_in_kernel;

// This is the big list of kernel function codes.

// Text input/output functions.  All are in the 0x1000-0x1FFF range.
#define _fnum_textGetConsoleInput				0x1000
#define _fnum_textSetConsoleInput				0x1001
#define _fnum_textGetConsoleOutput				0x1002
#define _fnum_textSetConsoleOutput				0x1003
#define _fnum_textGetCurrentInput				0x1004
#define _fnum_textSetCurrentInput				0x1005
#define _fnum_textGetCurrentOutput				0x1006
#define _fnum_textSetCurrentOutput				0x1007
#define _fnum_textGetForeground					0x1008
#define _fnum_textSetForeground					0x1009
#define _fnum_textGetBackground					0x100A
#define _fnum_textSetBackground					0x100B
#define _fnum_textPutc							0x100C
#define _fnum_textPrint							0x100D
#define _fnum_textPrintAttrs					0x100E
#define _fnum_textPrintLine						0x100F
#define _fnum_textNewline						0x1010
#define _fnum_textBackSpace						0x1011
#define _fnum_textTab							0x1012
#define _fnum_textCursorUp						0x1013
#define _fnum_textCursorDown					0x1014
#define _fnum_ternelTextCursorLeft				0x1015
#define _fnum_textCursorRight					0x1016
#define _fnum_textEnableScroll					0x1017
#define _fnum_textScroll						0x1018
#define _fnum_textGetNumColumns					0x1019
#define _fnum_textGetNumRows					0x101A
#define _fnum_textGetColumn						0x101B
#define _fnum_textSetColumn						0x101C
#define _fnum_textGetRow						0x101D
#define _fnum_textSetRow						0x101E
#define _fnum_textSetCursor						0x101F
#define _fnum_textScreenClear					0x1020
#define _fnum_textScreenSave					0x1021
#define _fnum_textScreenRestore					0x1022
#define _fnum_textInputStreamCount				0x1023
#define _fnum_textInputCount					0x1024
#define _fnum_textInputStreamGetc				0x1025
#define _fnum_textInputGetc						0x1026
#define _fnum_textInputStreamReadN				0x1027
#define _fnum_textInputReadN					0x1028
#define _fnum_textInputStreamReadAll			0x1029
#define _fnum_textInputReadAll					0x102A
#define _fnum_textInputStreamAppend				0x102B
#define _fnum_textInputAppend					0x102C
#define _fnum_textInputStreamAppendN			0x102D
#define _fnum_textInputAppendN					0x102E
#define _fnum_textInputStreamRemove				0x102F
#define _fnum_textInputRemove					0x1030
#define _fnum_textInputStreamRemoveN			0x1031
#define _fnum_textInputRemoveN					0x1032
#define _fnum_textInputStreamRemoveAll			0x1033
#define _fnum_textInputRemoveAll				0x1034
#define _fnum_textInputStreamSetEcho			0x1035
#define _fnum_textInputSetEcho					0x1036

// Disk functions.  All are in the 0x2000-0x2FFF range.
#define _fnum_diskReadPartitions				0x2000
#define _fnum_diskReadPartitionsAll				0x2001
#define _fnum_diskSync							0x2002
#define _fnum_diskSyncAll						0x2003
#define _fnum_diskGetBoot						0x2004
#define _fnum_diskGetCount						0x2005
#define _fnum_diskGetPhysicalCount				0x2006
#define _fnum_diskGet							0x2007
#define _fnum_diskGetAll						0x2008
#define _fnum_diskGetAllPhysical				0x2009
#define _fnum_diskGetFilesystemType				0x200A
#define _fnum_diskGetMsdosPartType				0x200B
#define _fnum_diskGetMsdosPartTypes				0x200C
#define _fnum_diskGetGptPartType				0x200D
#define _fnum_diskGetGptPartTypes				0x200E
#define _fnum_diskSetFlags						0x200F
#define _fnum_diskSetLockState					0x2010
#define _fnum_diskSetDoorState					0x2011
#define _fnum_diskMediaPresent					0x2012
#define _fnum_diskReadSectors					0x2013
#define _fnum_diskWriteSectors					0x2014
#define _fnum_diskEraseSectors					0x2015
#define _fnum_diskGetStats						0x2016
#define _fnum_diskRamDiskCreate					0x2017
#define _fnum_diskRamDiskDestroy				0x2018

// Filesystem functions.  All are in the 0x3000-0x3FFF range.
#define _fnum_filesystemScan					0x3000
#define _fnum_filesystemFormat					0x3001
#define _fnum_filesystemClobber					0x3002
#define _fnum_filesystemCheck					0x3003
#define _fnum_filesystemDefragment				0x3004
#define _fnum_filesystemResizeConstraints		0x3005
#define _fnum_filesystemResize					0x3006
#define _fnum_filesystemMount					0x3007
#define _fnum_filesystemUnmount					0x3008
#define _fnum_filesystemGetFreeBytes			0x3009
#define _fnum_filesystemGetBlockSize			0x300A

// File functions.  All are in the 0x4000-0x4FFF range.
#define _fnum_fileFixupPath						0x4000
#define _fnum_fileGetDisk						0x4001
#define _fnum_fileCount							0x4002
#define _fnum_fileFirst							0x4003
#define _fnum_fileNext							0x4004
#define _fnum_fileFind							0x4005
#define _fnum_fileOpen							0x4006
#define _fnum_fileClose							0x4007
#define _fnum_fileRead							0x4008
#define _fnum_fileWrite							0x4009
#define _fnum_fileDelete						0x400A
#define _fnum_fileDeleteRecursive				0x400B
#define _fnum_fileDeleteSecure					0x400C
#define _fnum_fileMakeDir						0x400D
#define _fnum_fileRemoveDir						0x400E
#define _fnum_fileCopy							0x400F
#define _fnum_fileCopyRecursive					0x4010
#define _fnum_fileMove							0x4011
#define _fnum_fileTimestamp						0x4012
#define _fnum_fileSetSize						0x4013
#define _fnum_fileGetTempName					0x4014
#define _fnum_fileGetTemp						0x4015
#define _fnum_fileGetFullPath					0x4016
#define _fnum_fileStreamOpen					0x4017
#define _fnum_fileStreamSeek					0x4018
#define _fnum_fileStreamRead					0x4019
#define _fnum_fileStreamReadLine				0x401A
#define _fnum_fileStreamWrite					0x401B
#define _fnum_fileStreamWriteStr				0x401C
#define _fnum_fileStreamWriteLine				0x401D
#define _fnum_fileStreamFlush					0x401E
#define _fnum_fileStreamClose					0x401F
#define _fnum_fileStreamGetTemp					0x4020

// Memory manager functions. All are in the 0x5000-0x5FFF range.
#define _fnum_memoryGet							0x5000
#define _fnum_memoryRelease						0x5001
#define _fnum_memoryReleaseAllByProcId			0x5002
#define _fnum_memoryGetStats					0x5003
#define _fnum_memoryGetBlocks					0x5004

// Multitasker functions.  All are in the 0x6000-0x6FFF range.
#define _fnum_multitaskerCreateProcess			0x6000
#define _fnum_multitaskerSpawn					0x6001
#define _fnum_multitaskerGetCurrentProcessId	0x6002
#define _fnum_multitaskerGetProcess				0x6003
#define _fnum_multitaskerGetProcessByName		0x6004
#define _fnum_multitaskerGetProcesses			0x6005
#define _fnum_multitaskerSetProcessState		0x6006
#define _fnum_multitaskerProcessIsAlive			0x6007
#define _fnum_multitaskerSetProcessPriority		0x6008
#define _fnum_multitaskerGetProcessPrivilege	0x6009
#define _fnum_multitaskerGetCurrentDirectory	0x600A
#define _fnum_multitaskerSetCurrentDirectory	0x600B
#define _fnum_multitaskerGetTextInput			0x600C
#define _fnum_multitaskerSetTextInput			0x600D
#define _fnum_multitaskerGetTextOutput			0x600E
#define _fnum_multitaskerSetTextOutput			0x600F
#define _fnum_multitaskerDuplicateIo			0x6010
#define _fnum_multitaskerGetProcessorTime		0x6011
#define _fnum_multitaskerYield					0x6012
#define _fnum_multitaskerWait					0x6013
#define _fnum_multitaskerBlock					0x6014
#define _fnum_multitaskerDetach					0x6015
#define _fnum_multitaskerKillProcess			0x6016
#define _fnum_multitaskerKillByName				0x6017
#define _fnum_multitaskerTerminate				0x6018
#define _fnum_multitaskerSignalSet				0x6019
#define _fnum_multitaskerSignal					0x601A
#define _fnum_multitaskerSignalRead				0x601B
#define _fnum_multitaskerGetIoPerm				0x601C
#define _fnum_multitaskerSetIoPerm				0x601D
#define _fnum_multitaskerStackTrace				0x601E

// Loader functions.  All are in the 0x7000-0x7FFF range.
#define _fnum_loaderLoad						0x7000
#define _fnum_loaderClassify					0x7001
#define _fnum_loaderClassifyFile				0x7002
#define _fnum_loaderGetSymbols					0x7003
#define _fnum_loaderCheckCommand				0x7004
#define _fnum_loaderLoadProgram					0x7005
#define _fnum_loaderLoadLibrary					0x7006
#define _fnum_loaderGetLibrary					0x7007
#define _fnum_loaderLinkLibrary					0x7008
#define _fnum_loaderGetSymbol					0x7009
#define _fnum_loaderExecProgram					0x700A
#define _fnum_loaderLoadAndExec					0x700B

// Real-time clock functions.  All are in the 0x8000-0x8FFF range.
#define _fnum_rtcReadSeconds					0x8000
#define _fnum_rtcReadMinutes					0x8001
#define _fnum_rtcReadHours						0x8002
#define _fnum_rtcDayOfWeek						0x8003
#define _fnum_rtcReadDayOfMonth					0x8004
#define _fnum_rtcReadMonth						0x8005
#define _fnum_rtcReadYear						0x8006
#define _fnum_rtcUptimeSeconds					0x8007
#define _fnum_rtcDateTime						0x8008

// Random number functions.	All are in the 9000-9999 range.
#define _fnum_randomUnformatted					0x9000
#define _fnum_randomFormatted					0x9001
#define _fnum_randomSeededUnformatted			0x9002
#define _fnum_randomSeededFormatted				0x9003
#define _fnum_randomBytes						0x9004

// Variable list functions.  All are in the 0xA000-0xAFFF range.
#define _fnum_variableListCreate				0xA000
#define _fnum_variableListDestroy				0xA001
#define _fnum_variableListGetVariable			0xA002
#define _fnum_variableListGet					0xA003
#define _fnum_variableListSet					0xA004
#define _fnum_variableListUnset					0xA005

// Environment functions.  All are in the 0xB000-0xBFFF range.
#define _fnum_environmentGet					0xB000
#define _fnum_environmentSet					0xB001
#define _fnum_environmentUnset					0xB002
#define _fnum_environmentDump					0xB003

// Raw graphics drawing functions.  All are in the 0xC000-0xCFFF range.
#define _fnum_graphicsAreEnabled				0xC000
#define _fnum_graphicGetModes					0xC001
#define _fnum_graphicGetMode					0xC002
#define _fnum_graphicSetMode					0xC003
#define _fnum_graphicGetScreenWidth				0xC004
#define _fnum_graphicGetScreenHeight			0xC005
#define _fnum_graphicCalculateAreaBytes			0xC006
#define _fnum_graphicClearScreen				0xC007
#define _fnum_graphicDrawPixel					0xC008
#define _fnum_graphicDrawLine					0xC009
#define _fnum_graphicDrawRect					0xC00A
#define _fnum_graphicDrawOval					0xC00B
#define _fnum_graphicGetImage					0xC00C
#define _fnum_graphicDrawImage					0xC00D
#define _fnum_graphicDrawText					0xC00E
#define _fnum_graphicCopyArea					0xC00F
#define _fnum_graphicClearArea					0xC010
#define _fnum_graphicRenderBuffer				0xC011

// Image functions  All are in the 0xD000-0xDFFF range.
#define _fnum_imageNew							0xD000
#define _fnum_imageFree							0xD001
#define _fnum_imageLoad							0xD002
#define _fnum_imageSave							0xD003
#define _fnum_imageResize						0xD004
#define _fnum_imageCopy							0xD005
#define _fnum_imageFill							0xD006
#define _fnum_imagePaste						0xD007

// Font functions  All are in the 0xE000-0xEFFF range.
#define _fnum_fontGet							0xE000
#define _fnum_fontGetPrintedWidth				0xE001
#define _fnum_fontGetWidth						0xE002
#define _fnum_fontGetHeight						0xE003

// Windowing system functions.  All are in the 0xF000-0xFFFF range.
#define _fnum_windowLogin						0xF000
#define _fnum_windowLogout						0xF001
#define _fnum_windowNew							0xF002
#define _fnum_windowNewDialog					0xF003
#define _fnum_windowDestroy						0xF004
#define _fnum_windowUpdateBuffer				0xF005
#define _fnum_windowSetCharSet					0xF006
#define _fnum_windowSetTitle					0xF007
#define _fnum_windowGetSize						0xF008
#define _fnum_windowSetSize						0xF009
#define _fnum_windowGetLocation					0xF00A
#define _fnum_windowSetLocation					0xF00B
#define _fnum_windowCenter						0xF00C
#define _fnum_windowSnapIcons					0xF00D
#define _fnum_windowSetHasBorder				0xF00E
#define _fnum_windowSetHasTitleBar				0xF00F
#define _fnum_windowSetMovable					0xF010
#define _fnum_windowSetResizable				0xF011
#define _fnum_windowSetFocusable				0xF012
#define _fnum_windowRemoveMinimizeButton		0xF013
#define _fnum_windowRemoveCloseButton			0xF014
#define _fnum_windowSetVisible					0xF015
#define _fnum_windowSetMinimized				0xF016
#define _fnum_windowAddConsoleTextArea			0xF017
#define _fnum_windowRedrawArea					0xF018
#define _fnum_windowDrawAll						0xF019
#define _fnum_windowGetColor					0xF01A
#define _fnum_windowSetColor					0xF01B
#define _fnum_windowResetColors					0xF01C
#define _fnum_windowProcessEvent				0xF01D
#define _fnum_windowComponentEventGet			0xF01E
#define _fnum_windowSetBackgroundColor			0xF01F
#define _fnum_windowShellTileBackground			0xF020
#define _fnum_windowShellCenterBackground		0xF021
#define _fnum_windowShellNewTaskbarIcon			0xF022
#define _fnum_windowShellNewTaskbarTextLabel	0xF023
#define _fnum_windowShellDestroyTaskbarComp		0xF024
#define _fnum_windowShellIconify				0xF025
#define _fnum_windowScreenShot					0xF026
#define _fnum_windowSaveScreenShot				0xF027
#define _fnum_windowSetTextOutput				0xF028
#define _fnum_windowLayout						0xF029
#define _fnum_windowDebugLayout					0xF02A
#define _fnum_windowContextAdd					0xF02B
#define _fnum_windowContextSet					0xF02C
#define _fnum_windowSwitchPointer				0xF02D
#define _fnum_windowRefresh						0xF02E
#define _fnum_windowComponentDestroy			0xF02F
#define _fnum_windowComponentSetCharSet			0xF030
#define _fnum_windowComponentSetVisible			0xF031
#define _fnum_windowComponentSetEnabled			0xF032
#define _fnum_windowComponentGetWidth			0xF033
#define _fnum_windowComponentSetWidth			0xF034
#define _fnum_windowComponentGetHeight			0xF035
#define _fnum_windowComponentSetHeight			0xF036
#define _fnum_windowComponentFocus				0xF037
#define _fnum_windowComponentUnfocus			0xF038
#define _fnum_windowComponentDraw				0xF039
#define _fnum_windowComponentGetData			0xF03A
#define _fnum_windowComponentSetData			0xF03B
#define _fnum_windowComponentGetSelected		0xF03C
#define _fnum_windowComponentSetSelected		0xF03D
#define _fnum_windowNewButton					0xF03E
#define _fnum_windowNewCanvas					0xF03F
#define _fnum_windowNewCheckbox					0xF040
#define _fnum_windowNewContainer				0xF041
#define _fnum_windowNewDivider					0xF042
#define _fnum_windowNewIcon						0xF043
#define _fnum_windowNewImage					0xF044
#define _fnum_windowNewList						0xF045
#define _fnum_windowNewListItem					0xF046
#define _fnum_windowNewMenu						0xF047
#define _fnum_windowNewMenuBar					0xF048
#define _fnum_windowNewMenuBarIcon				0xF049
#define _fnum_windowNewMenuItem					0xF04A
#define _fnum_windowNewPasswordField			0xF04B
#define _fnum_windowNewProgressBar				0xF04C
#define _fnum_windowNewRadioButton				0xF04D
#define _fnum_windowNewScrollBar				0xF04E
#define _fnum_windowNewSlider					0xF04F
#define _fnum_windowNewTextArea					0xF050
#define _fnum_windowNewTextField				0xF051
#define _fnum_windowNewTextLabel				0xF052
#define _fnum_windowNewTree						0xF053

// User functions.  All are in the 0x10000-0x10FFF range.
#define _fnum_userAuthenticate					0x10000
#define _fnum_userLogin							0x10001
#define _fnum_userLogout						0x10002
#define _fnum_userExists						0x10003
#define _fnum_userGetNames						0x10004
#define _fnum_userAdd							0x10005
#define _fnum_userDelete						0x10006
#define _fnum_userSetPassword					0x10007
#define _fnum_userGetCurrent					0x10008
#define _fnum_userGetPrivilege					0x10009
#define _fnum_userGetPid						0x1000A
#define _fnum_userSetPid						0x1000B
#define _fnum_userFileAdd						0x1000C
#define _fnum_userFileDelete					0x1000D
#define _fnum_userFileSetPassword				0x1000E

// Network functions.  All are in the 0x11000-0x11FFF range.
#define _fnum_networkEnabled					0x11000
#define _fnum_networkEnable						0x11001
#define _fnum_networkDisable					0x11002
#define _fnum_networkOpen						0x11003
#define _fnum_networkClose						0x11004
#define _fnum_networkCount						0x11005
#define _fnum_networkRead						0x11006
#define _fnum_networkWrite						0x11007
#define _fnum_networkPing						0x11008
#define _fnum_networkGetHostName				0x11009
#define _fnum_networkSetHostName				0x1100A
#define _fnum_networkGetDomainName				0x1100B
#define _fnum_networkSetDomainName				0x1100C
#define _fnum_networkDeviceEnable				0x1100D
#define _fnum_networkDeviceDisable				0x1100E
#define _fnum_networkDeviceGetCount				0x1100F
#define _fnum_networkDeviceGet					0x11010
#define _fnum_networkDeviceHook					0x11011
#define _fnum_networkDeviceUnhook				0x11012
#define _fnum_networkDeviceSniff				0x11013

// Miscellaneous functions.  All are in the 0xFF000-0xFFFFF range.
#define _fnum_systemShutdown					0xFF000
#define _fnum_getVersion						0xFF001
#define _fnum_systemInfo						0xFF002
#define _fnum_cryptHashMd5						0xFF003
#define _fnum_lockGet							0xFF004
#define _fnum_lockRelease						0xFF005
#define _fnum_lockVerify						0xFF006
#define _fnum_configRead						0xFF007
#define _fnum_configWrite						0xFF008
#define _fnum_configGet							0xFF009
#define _fnum_configSet							0xFF00A
#define _fnum_configUnset						0xFF00B
#define _fnum_guidGenerate						0xFF00C
#define _fnum_crc32								0xFF00D
#define _fnum_keyboardGetMap					0xFF00E
#define _fnum_keyboardSetMap					0xFF00F
#define _fnum_keyboardVirtualInput				0xFF010
#define _fnum_deviceTreeGetRoot					0xFF011
#define _fnum_deviceTreeGetChild				0xFF012
#define _fnum_deviceTreeGetNext					0xFF013
#define _fnum_mouseLoadPointer					0xFF014
#define _fnum_pageGetPhysical					0xFF015
#define _fnum_charsetToUnicode					0xFF016
#define _fnum_charsetFromUnicode				0xFF017
#define _fnum_cpuGetMs							0xFF018
#define _fnum_cpuSpinMs							0xFF019


//
// Text input/output functions
//
objectKey textGetConsoleInput(void);
int textSetConsoleInput(objectKey);
objectKey textGetConsoleOutput(void);
int textSetConsoleOutput(objectKey);
objectKey textGetCurrentInput(void);
int textSetCurrentInput(objectKey);
objectKey textGetCurrentOutput(void);
int textSetCurrentOutput(objectKey);
int textGetForeground(color *);
int textSetForeground(color *);
int textGetBackground(color *);
int textSetBackground(color *);
int textPutc(int);
int textPrint(const char *);
int textPrintAttrs(textAttrs *, const char *);
int textPrintLine(const char *);
void textNewline(void);
int textBackSpace(void);
int textTab(void);
int textCursorUp(void);
int textCursorDown(void);
int textCursorLeft(void);
int textCursorRight(void);
int textEnableScroll(int);
void textScroll(int);
int textGetNumColumns(void);
int textGetNumRows(void);
int textGetColumn(void);
void textSetColumn(int);
int textGetRow(void);
void textSetRow(int);
void textSetCursor(int);
int textScreenClear(void);
int textScreenSave(textScreen *);
int textScreenRestore(textScreen *);
int textInputStreamCount(objectKey);
int textInputCount(void);
int textInputStreamGetc(objectKey, char *);
int textInputGetc(char *);
int textInputStreamReadN(objectKey, int, char *);
int textInputReadN(int, char *);
int textInputStreamReadAll(objectKey, char *);
int textInputReadAll(char *);
int textInputStreamAppend(objectKey, int);
int textInputAppend(int);
int textInputStreamAppendN(objectKey, int, char *);
int textInputAppendN(int, char *);
int textInputStreamRemove(objectKey);
int textInputRemove(void);
int textInputStreamRemoveN(objectKey, int);
int textInputRemoveN(int);
int textInputStreamRemoveAll(objectKey);
int textInputRemoveAll(void);
void textInputStreamSetEcho(objectKey, int);
void textInputSetEcho(int);

//
// Disk functions
//
int diskReadPartitions(const char *);
int diskReadPartitionsAll(void);
int diskSync(const char *);
int diskSyncAll(void);
int diskGetBoot(char *);
int diskGetCount(void);
int diskGetPhysicalCount(void);
int diskGet(const char *, disk *);
int diskGetAll(disk *, unsigned);
int diskGetAllPhysical(disk *, unsigned);
int diskGetFilesystemType(const char *, char *, unsigned);
int diskGetMsdosPartType(int, msdosPartType *);
msdosPartType *diskGetMsdosPartTypes(void);
int diskGetGptPartType(guid *, gptPartType *);
gptPartType *diskGetGptPartTypes(void);
int diskSetFlags(const char *, unsigned, int);
int diskSetLockState(const char *, int);
int diskSetDoorState(const char *, int);
int diskMediaPresent(const char *);
int diskReadSectors(const char *, uquad_t, uquad_t, void *);
int diskWriteSectors(const char *, uquad_t, uquad_t, const void *);
int diskEraseSectors(const char *, uquad_t, uquad_t, int);
int diskGetStats(const char *, diskStats *);
int diskRamDiskCreate(unsigned, char *);
int diskRamDiskDestroy(const char *);

//
// Filesystem functions
//
int filesystemScan(const char *);
int filesystemFormat(const char *, const char *, const char *, int,
	progress *);
int filesystemClobber(const char *);
int filesystemCheck(const char *, int, int, progress *);
int filesystemDefragment(const char *, progress *);
int filesystemResizeConstraints(const char *, uquad_t *, uquad_t *,
	progress *);
int filesystemResize(const char *, uquad_t, progress *);
int filesystemMount(const char *, const char *);
int filesystemUnmount(const char *);
uquad_t filesystemGetFreeBytes(const char *);
unsigned filesystemGetBlockSize(const char *);

//
// File functions
//
int fileFixupPath(const char *, char *);
int fileGetDisk(const char *, disk *);
int fileCount(const char *);
int fileFirst(const char *, file *);
int fileNext(const char *, file *);
int fileFind(const char *, file *);
int fileOpen(const char *, int, file *);
int fileClose(file *);
int fileRead(file *, unsigned, unsigned, void *);
int fileWrite(file *, unsigned, unsigned, void *);
int fileDelete(const char *);
int fileDeleteRecursive(const char *);
int fileDeleteSecure(const char *, int);
int fileMakeDir(const char *);
int fileRemoveDir(const char *);
int fileCopy(const char *, const char *);
int fileCopyRecursive(const char *, const char *);
int fileMove(const char *, const char *);
int fileTimestamp(const char *);
int fileSetSize(file *, unsigned);
int fileGetTempName(char *, unsigned);
int fileGetTemp(file *);
int fileGetFullPath(file *, char *, int);
int fileStreamOpen(const char *, int, fileStream *);
int fileStreamSeek(fileStream *, unsigned);
int fileStreamRead(fileStream *, unsigned, char *);
int fileStreamReadLine(fileStream *, unsigned, char *);
int fileStreamWrite(fileStream *, unsigned, const char *);
int fileStreamWriteStr(fileStream *, const char *);
int fileStreamWriteLine(fileStream *, const char *);
int fileStreamFlush(fileStream *);
int fileStreamClose(fileStream *);
int fileStreamGetTemp(fileStream *);

//
// Memory functions
//
void *memoryGet(unsigned, const char *);
int memoryRelease(void *);
int memoryReleaseAllByProcId(int);
int memoryGetStats(memoryStats *, int);
int memoryGetBlocks(memoryBlock *, unsigned, int);

//
// Multitasker functions
//
int multitaskerCreateProcess(const char *, int, processImage *);
int multitaskerSpawn(void *, const char *, int, void *[]);
int multitaskerGetCurrentProcessId(void);
int multitaskerGetProcess(int, process *);
int multitaskerGetProcessByName(const char *, process *);
int multitaskerGetProcesses(void *, unsigned);
int multitaskerSetProcessState(int, int);
int multitaskerProcessIsAlive(int);
int multitaskerSetProcessPriority(int, int);
int multitaskerGetProcessPrivilege(int);
int multitaskerGetCurrentDirectory(char *, int);
int multitaskerSetCurrentDirectory(const char *);
objectKey multitaskerGetTextInput(void);
int multitaskerSetTextInput(int, objectKey);
objectKey multitaskerGetTextOutput(void);
int multitaskerSetTextOutput(int, objectKey);
int multitaskerDuplicateIo(int, int, int);
int multitaskerGetProcessorTime(clock_t *);
void multitaskerYield(void);
void multitaskerWait(unsigned);
int multitaskerBlock(int);
int multitaskerDetach(void);
int multitaskerKillProcess(int, int);
int multitaskerKillByName(const char *, int);
int multitaskerTerminate(int);
int multitaskerSignalSet(int, int, int);
int multitaskerSignal(int, int);
int multitaskerSignalRead(int);
int multitaskerGetIoPerm(int, int);
int multitaskerSetIoPerm(int, int, int);
int multitaskerStackTrace(int);

//
// Loader functions
//
void *loaderLoad(const char *, file *);
objectKey loaderClassify(const char *, void *, unsigned, loaderFileClass *);
objectKey loaderClassifyFile(const char *, loaderFileClass *);
loaderSymbolTable *loaderGetSymbols(const char *);
int loaderCheckCommand(const char *);
int loaderLoadProgram(const char *, int);
int loaderLoadLibrary(const char *);
void *loaderGetLibrary(const char *);
void *loaderLinkLibrary(const char *);
void *loaderGetSymbol(const char *);
int loaderExecProgram(int, int);
int loaderLoadAndExec(const char *, int, int);

//
// Real-time clock functions
//
int rtcReadSeconds(void);
int rtcReadMinutes(void);
int rtcReadHours(void);
int rtcDayOfWeek(unsigned, unsigned, unsigned);
int rtcReadDayOfMonth(void);
int rtcReadMonth(void);
int rtcReadYear(void);
unsigned rtcUptimeSeconds(void);
int rtcDateTime(struct tm *);

//
// Random number functions
//
unsigned randomUnformatted(void);
unsigned randomFormatted(unsigned, unsigned);
unsigned randomSeededUnformatted(unsigned);
unsigned randomSeededFormatted(unsigned, unsigned, unsigned);
void randomBytes(unsigned char *, unsigned);

//
// Variable list functions
//
int variableListCreate(variableList *);
int variableListDestroy(variableList *);
const char *variableListGetVariable(variableList *, int);
const char *variableListGet(variableList *, const char *);
int variableListSet(variableList *, const char *, const char *);
int variableListUnset(variableList *, const char *);

//
// Environment functions
//
int environmentGet(const char *, char *, unsigned);
int environmentSet(const char *, const char *);
int environmentUnset(const char *);
void environmentDump(void);

//
// Raw graphics functions
//
int graphicsAreEnabled(void);
int graphicGetModes(videoMode *, unsigned);
int graphicGetMode(videoMode *);
int graphicSetMode(videoMode *);
int graphicGetScreenWidth(void);
int graphicGetScreenHeight(void);
int graphicCalculateAreaBytes(int, int);
int graphicClearScreen(color *);
int graphicDrawPixel(graphicBuffer *, color *, drawMode, int, int);
int graphicDrawLine(graphicBuffer *, color *, drawMode, int, int, int, int);
int graphicDrawRect(graphicBuffer *, color *, drawMode, int, int, int, int,
	int, int);
int graphicDrawOval(graphicBuffer *, color *, drawMode, int, int, int, int,
	int, int);
int graphicGetImage(graphicBuffer *, image *, int, int, int, int);
int graphicDrawImage(graphicBuffer *, image *, drawMode, int, int, int, int,
	int, int);
int graphicDrawText(graphicBuffer *, color *, color *, objectKey,
	const char *, const char *, drawMode, int, int);
int graphicCopyArea(graphicBuffer *, int, int, int, int, int, int);
int graphicClearArea(graphicBuffer *, color *, int, int, int, int);
int graphicRenderBuffer(graphicBuffer *, int, int, int, int, int, int);

//
// Image functions
//
int imageNew(image *, unsigned, unsigned);
int imageFree(image *);
int imageLoad(const char *, unsigned, unsigned, image *);
int imageSave(const char *, int, image *);
int imageResize(image *, unsigned, unsigned);
int imageCopy(image *, image *);
int imageFill(image *, color *);
int imagePaste(image *, image *, int, int);

//
// Font functions
//
objectKey fontGet(const char *, unsigned, int, const char *);
int fontGetPrintedWidth(objectKey, const char *, const char *);
int fontGetWidth(objectKey);
int fontGetHeight(objectKey);

//
// Windowing system functions
//
int windowLogin(const char *);
int windowLogout(void);
objectKey windowNew(int, const char *);
objectKey windowNewDialog(objectKey, const char *);
int windowDestroy(objectKey);
int windowUpdateBuffer(void *, int, int, int, int);
int windowSetCharSet(objectKey, const char *);
int windowSetTitle(objectKey, const char *);
int windowGetSize(objectKey, int *, int *);
int windowSetSize(objectKey, int, int);
int windowGetLocation(objectKey, int *, int *);
int windowSetLocation(objectKey, int, int);
int windowCenter(objectKey);
int windowSnapIcons(objectKey);
int windowSetHasBorder(objectKey, int);
int windowSetHasTitleBar(objectKey, int);
int windowSetMovable(objectKey, int);
int windowSetResizable(objectKey, int);
int windowSetFocusable(objectKey, int);
int windowRemoveMinimizeButton(objectKey);
int windowRemoveCloseButton(objectKey);
int windowSetVisible(objectKey, int);
void windowSetMinimized(objectKey, int);
int windowAddConsoleTextArea(objectKey);
void windowRedrawArea(int, int, int, int);
void windowDrawAll(void);
int windowGetColor(const char *, color *);
int windowSetColor(const char *, color *);
void windowResetColors(void);
void windowProcessEvent(objectKey);
int windowComponentEventGet(objectKey, windowEvent *);
int windowSetBackgroundColor(objectKey, color *);
int windowShellTileBackground(const char *);
int windowShellCenterBackground(const char *);
objectKey windowShellNewTaskbarIcon(image *);
objectKey windowShellNewTaskbarTextLabel(const char *);
void windowShellDestroyTaskbarComp(objectKey);
objectKey windowShellIconify(objectKey, int, image *);
int windowScreenShot(image *);
int windowSaveScreenShot(const char *);
int windowSetTextOutput(objectKey);
int windowLayout(objectKey);
void windowDebugLayout(objectKey);
int windowContextAdd(objectKey, windowMenuContents *);
int windowContextSet(objectKey, objectKey);
int windowSwitchPointer(objectKey, const char *);
int windowRefresh(void);
void windowComponentDestroy(objectKey);
int windowComponentSetCharSet(objectKey, const char *);
int windowComponentSetVisible(objectKey, int);
int windowComponentSetEnabled(objectKey, int);
int windowComponentGetWidth(objectKey);
int windowComponentSetWidth(objectKey, int);
int windowComponentGetHeight(objectKey);
int windowComponentSetHeight(objectKey, int);
int windowComponentFocus(objectKey);
int windowComponentUnfocus(objectKey);
int windowComponentDraw(objectKey);
int windowComponentGetData(objectKey, void *, int);
int windowComponentSetData(objectKey, void *, int, int);
int windowComponentGetSelected(objectKey, int *);
int windowComponentSetSelected(objectKey, int );
objectKey windowNewButton(objectKey, const char *, image *,
	componentParameters *);
objectKey windowNewCanvas(objectKey, int, int, componentParameters *);
objectKey windowNewCheckbox(objectKey, const char *, componentParameters *);
objectKey windowNewContainer(objectKey, const char *, componentParameters *);
objectKey windowNewDivider(objectKey, dividerType, componentParameters *);
objectKey windowNewIcon(objectKey, image *, const char *,
	componentParameters *);
objectKey windowNewImage(objectKey, image *, drawMode, componentParameters *);
objectKey windowNewList(objectKey, windowListType, int, int, int,
	listItemParameters *, int, componentParameters *);
objectKey windowNewListItem(objectKey, windowListType, listItemParameters *,
	componentParameters *);
objectKey windowNewMenu(objectKey, objectKey,
	const char *, windowMenuContents *, componentParameters *);
objectKey windowNewMenuBar(objectKey, componentParameters *);
objectKey windowNewMenuBarIcon(objectKey, image *, componentParameters *);
objectKey windowNewMenuItem(objectKey, const char *, componentParameters *);
objectKey windowNewPasswordField(objectKey, int, componentParameters *);
objectKey windowNewProgressBar(objectKey, componentParameters *);
objectKey windowNewRadioButton(objectKey, int, int, char *[], int,
	componentParameters *);
objectKey windowNewScrollBar(objectKey, scrollBarType, int, int,
	componentParameters *);
objectKey windowNewSlider(objectKey, scrollBarType, int, int,
	componentParameters *);
objectKey windowNewTextArea(objectKey, int, int, int, componentParameters *);
objectKey windowNewTextField(objectKey, int, componentParameters *);
objectKey windowNewTextLabel(objectKey, const char *, componentParameters *);
objectKey windowNewTree(objectKey, windowTreeItem *, int, int,
	componentParameters *);

//
// User functions
//
int userAuthenticate(const char *, const char *);
int userLogin(const char *, const char *);
int userLogout(const char *);
int userExists(const char *);
int userGetNames(char *, unsigned);
int userAdd(const char *, const char *);
int userDelete(const char *);
int userSetPassword(const char *, const char *, const char *);
int userGetCurrent(char *, unsigned);
int userGetPrivilege(const char *);
int userGetPid(void);
int userSetPid(const char *, int);
int userFileAdd(const char *, const char *, const char *);
int userFileDelete(const char *, const char *);
int userFileSetPassword(const char *, const char *, const char *, const char *);

//
// Network functions
//
int networkEnabled(void);
int networkEnable(void);
int networkDisable(void);
objectKey networkOpen(int, networkAddress *, networkFilter *);
int networkClose(objectKey);
int networkCount(objectKey);
int networkRead(objectKey, unsigned char *, unsigned);
int networkWrite(objectKey, unsigned char *, unsigned);
int networkPing(objectKey, int, unsigned char *, unsigned);
int networkGetHostName(char *, int);
int networkSetHostName(const char *, int);
int networkGetDomainName(char *, int);
int networkSetDomainName(const char *, int);
int networkDeviceEnable(const char *);
int networkDeviceDisable(const char *);
int networkDeviceGetCount(void);
int networkDeviceGet(const char *, networkDevice *);
int networkDeviceHook(const char *, objectKey *, int);
int networkDeviceUnhook(const char *, objectKey, int);
unsigned networkDeviceSniff(objectKey, unsigned char *, unsigned);

//
// Miscellaneous functions
//
int systemShutdown(int, int);
void getVersion(char *, int);
int systemInfo(struct utsname *);
int cryptHashMd5(const unsigned char *, unsigned, unsigned char *);
int lockGet(lock *);
int lockRelease(lock *);
int lockVerify(lock *);
int configRead(const char *, variableList *);
int configWrite(const char *, variableList *);
int configGet(const char *, const char *, char *, unsigned);
int configSet(const char *, const char *, const char *);
int configUnset(const char *, const char *);
int guidGenerate(guid *);
unsigned crc32(void *, unsigned, unsigned *);
int keyboardGetMap(keyMap *);
int keyboardSetMap(const char *);
int keyboardVirtualInput(int, keyScan);
int deviceTreeGetRoot(device *);
int deviceTreeGetChild(device *, device *);
int deviceTreeGetNext(device *);
int mouseLoadPointer(const char *, const char *);
void *pageGetPhysical(int, void *);
unsigned charsetToUnicode(const char *, unsigned);
unsigned charsetFromUnicode(const char *, unsigned);
uquad_t cpuGetMs(void);
void cpuSpinMs(unsigned);

#define _API_H
#endif

