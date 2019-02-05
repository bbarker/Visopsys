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
//  kernelMultitasker.h
//

#if !defined(_KERNELMULTITASKER_H)

#include "kernelDescriptor.h"
#include "kernelPage.h"
#include "kernelSysTimer.h"
#include "kernelText.h"
#include <time.h>
#include <sys/file.h>
#include <sys/loader.h>
#include <sys/process.h>
#include <sys/variable.h>

// Definitions
#define MAX_PROCESSES				((GDT_SIZE - RES_GLOBAL_DESCRIPTORS))
#define PRIORITY_LEVELS				8
#define DEFAULT_STACK_SIZE			(32 * 1024)
#define DEFAULT_SUPER_STACK_SIZE	(32 * 1024)
#define TIME_SLICES_PER_SEC			64 // ~15ms per slice
#define TIME_SLICE_LENGTH			(SYSTIMER_FREQ_HZ / TIME_SLICES_PER_SEC)
#define CPU_PERCENT_TIMESLICES		(TIME_SLICES_PER_SEC / 2) // every 1/2 sec
#define PRIORITY_RATIO				3
#define PRIORITY_DEFAULT			((PRIORITY_LEVELS / 2) - 1)
#define FPU_STATE_LEN				108
#define IO_PORTS					65536
#define PORTS_BYTES					(IO_PORTS / 8)
#define IOBITMAP_OFFSET				0x68

// Exception vector numbers
#define EXCEPTION_DIVBYZERO			0
#define EXCEPTION_DEBUG				1
#define EXCEPTION_NMI				2
#define EXCEPTION_BREAK				3
#define EXCEPTION_OVERFLOW			4
#define EXCEPTION_BOUNDS			5
#define EXCEPTION_OPCODE			6
#define EXCEPTION_DEVNOTAVAIL		7
#define EXCEPTION_DOUBLEFAULT		8
#define EXCEPTION_COPROCOVER		9
#define EXCEPTION_INVALIDTSS		10
#define EXCEPTION_SEGNOTPRES		11
#define EXCEPTION_STACK				12
#define EXCEPTION_GENPROTECT		13
#define EXCEPTION_PAGE				14
#define EXCEPTION_RESERVED			15
#define EXCEPTION_FLOAT				16
#define EXCEPTION_ALIGNCHECK		17
#define EXCEPTION_MACHCHECK			18

// A structure representing x86 TSSes (Task State Sements)
typedef volatile struct {
	unsigned oldTSS;
	unsigned ESP0;
	unsigned SS0;
	unsigned ESP1;
	unsigned SS1;
	unsigned ESP2;
	unsigned SS2;
	unsigned CR3;
	unsigned EIP;
	unsigned EFLAGS;
	unsigned EAX;
	unsigned ECX;
	unsigned EDX;
	unsigned EBX;
	unsigned ESP;
	unsigned EBP;
	unsigned ESI;
	unsigned EDI;
	unsigned ES;
	unsigned CS;
	unsigned SS;
	unsigned DS;
	unsigned FS;
	unsigned GS;
	unsigned LDTSelector;
	unsigned short pad;
	unsigned short IOMapBase;
	unsigned char IOMap[PORTS_BYTES];

} __attribute__((packed)) kernelTSS;

// A structure for processes
typedef volatile struct {
	char name[MAX_PROCNAME_LENGTH];
	processImage execImage;
	int userId;
	int processId;
	processType type;
	int priority;
	int privilege;
	int processorPrivilege;
	int parentProcessId;
	int descendentThreads;
	unsigned cpuTime;
	int cpuPercent;
	unsigned lastSlice;
	unsigned waitTime;
	unsigned long long waitUntil;
	int waitForProcess;
	int blockingExitCode;
	processState state;
	void *userStack;
	unsigned userStackSize;
	void *superStack;
	unsigned superStackSize;
	kernelPageDirectory *pageDirectory;
	kernelSelector tssSelector;
	kernelTSS taskStateSegment;
	char currentDirectory[MAX_PATH_LENGTH];
	variableList *environment;
	kernelTextInputStream *textInputStream;
	kernelTextInputStreamAttrs oldInputAttrs;
	kernelTextOutputStream *textOutputStream;
	unsigned signalMask;
	stream signalStream;
	unsigned char fpuState[FPU_STATE_LEN];
	int fpuStateSaved;
	loaderSymbolTable *symbols;

} kernelProcess;

// When in system calls, processes will be allowed to access information
// about themselves
extern kernelProcess *kernelCurrentProcess;

// Functions exported by kernelMultitasker.c
int kernelMultitaskerInitialize(void *, unsigned);
int kernelMultitaskerShutdown(int);
void kernelException(int, unsigned);
void kernelMultitaskerDumpProcessList(void);
int kernelMultitaskerGetCurrentProcessId(void);
int kernelMultitaskerGetProcess(int, process *);
int kernelMultitaskerGetProcessByName(const char *, process *);
int kernelMultitaskerGetProcesses(void *, unsigned);
int kernelMultitaskerCreateProcess(const char *, int, processImage *);
int kernelMultitaskerSpawn(void *, const char *, int, void *[]);
int kernelMultitaskerSpawnKernelThread(void *, const char *, int, void *[]);
//int kernelMultitaskerFork(void);
int kernelMultitaskerGetProcessState(int, processState *);
int kernelMultitaskerSetProcessState(int, processState);
int kernelMultitaskerProcessIsAlive(int);
int kernelMultitaskerGetProcessPriority(int);
int kernelMultitaskerSetProcessPriority(int, int);
int kernelMultitaskerGetProcessPrivilege(int);
int kernelMultitaskerGetProcessParent(int);
int kernelMultitaskerSetProcessParent(int, int);
int kernelMultitaskerGetCurrentDirectory(char *, int);
int kernelMultitaskerSetCurrentDirectory(const char *);
kernelTextInputStream *kernelMultitaskerGetTextInput(void);
int kernelMultitaskerSetTextInput(int, kernelTextInputStream *);
kernelTextOutputStream *kernelMultitaskerGetTextOutput(void);
int kernelMultitaskerSetTextOutput(int, kernelTextOutputStream *);
int kernelMultitaskerDuplicateIo(int, int, int);
int kernelMultitaskerGetProcessorTime(clock_t *);
void kernelMultitaskerYield(void);
void kernelMultitaskerWait(unsigned);
int kernelMultitaskerBlock(int);
int kernelMultitaskerDetach(void);
int kernelMultitaskerKillProcess(int, int);
int kernelMultitaskerKillByName(const char *, int);
int kernelMultitaskerKillAll(void);
int kernelMultitaskerTerminate(int);
int kernelMultitaskerSignalSet(int, int, int);
int kernelMultitaskerSignal(int, int);
int kernelMultitaskerSignalRead(int);
int kernelMultitaskerGetIoPerm(int, int);
int kernelMultitaskerSetIoPerm(int, int, int);
kernelPageDirectory *kernelMultitaskerGetPageDir(int);
loaderSymbolTable *kernelMultitaskerGetSymbols(int);
int kernelMultitaskerSetSymbols(int, loaderSymbolTable *);
int kernelMultitaskerStackTrace(int);
int kernelMultitaskerPropagateEnvironment(const char *);

#define _KERNELMULTITASKER_H
#endif

