//
//  Visopsys
//  Copyright (C) 1998-2005 J. Andrew McLaughlin
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
//  kernelMultitasker.h
//

#if !defined(_KERNELMULTITASKER_H)

#include "kernelDescriptor.h"
#include "kernelText.h"
#include <time.h>
#include <sys/process.h>
#include <sys/variable.h>

// Definitions
#define MAX_PROCESSES             ((GDT_SIZE - RES_GLOBAL_DESCRIPTORS))
#define PRIORITY_LEVELS           8
#define DEFAULT_STACK_SIZE        (32 * 1024)
#define DEFAULT_SUPER_STACK_SIZE  (32 * 1024)
#define TIME_SLICE_LENGTH         0x00002000
#define CPU_PERCENT_TIMESLICES    300
#define PRIORITY_RATIO            3
#define PRIORITY_DEFAULT          ((PRIORITY_LEVELS / 2) - 1)
#define FPU_STATE_LEN             108
// added by Davide Airaghi
#define KERNEL_PRIORITY		  1

// next added for I/O port protection by Davide Airaghi
#define IO_PORTS 65536
#define PORTS_BYTES  ( IO_PORTS / 8 )
#define IOBITMAP_OFFSET (26*4)
#define PORT_VAL_BIT_TRUE 0x00
#define PORT_VAL_BIT_FALSE 0x01
#define PORT_VAL_BYTE_TRUE 0x00
#define PORT_VAL_BYTE_FALSE 0xff


// Exception vector numbers
#define EXCEPTION_DIVBYZERO       0
#define EXCEPTION_DEBUG           1
#define EXCEPTION_NMI             2
#define EXCEPTION_BREAK           3
#define EXCEPTION_OVERFLOW        4
#define EXCEPTION_BOUNDS          5
#define EXCEPTION_OPCODE          6
#define EXCEPTION_DEVNOTAVAIL     7
#define EXCEPTION_DOUBLEFAULT     8
#define EXCEPTION_COPROCOVER      9
#define EXCEPTION_INVALIDTSS      10
#define EXCEPTION_SEGNOTPRES      11
#define EXCEPTION_STACK           12
#define EXCEPTION_GENPROTECT      13
#define EXCEPTION_PAGE            14
#define EXCEPTION_RESERVED        15
#define EXCEPTION_FLOAT           16
#define EXCEPTION_ALIGNCHECK      17
#define EXCEPTION_MACHCHECK       18

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
  // next fields required for I/O port protection, Davide Airaghi
  short unsigned dummy;
  short unsigned IOMapBase;
  // unsigned char IOMap[PORTS_BYTES];  
  unsigned char IOMap;
} __attribute__((packed)) kernelTSS;

// A structure for processes
typedef volatile struct {
  char processName[MAX_PROCNAME_LENGTH];
  int userId;
  int processId;
  processType type;
  int priority;
  int privilege;
  int parentProcessId;
  int descendentThreads;
  unsigned startTime;
  unsigned cpuTime;
  int cpuPercent;
  unsigned yieldSlice;
  unsigned waitTime;
  unsigned waitUntil;
  int waitForProcess;
  int blockingExitCode;
  processState state;
  void *userStack;
  unsigned userStackSize;
  void *superStack;
  unsigned superStackSize;
  kernelSelector tssSelector;
  void * taskStateSegment; // from kernelTSS to void *, by Davide Airaghi
  // next 4 added by Davide Airaghi for dynamic TSS.IOMap handling
  unsigned int max_io_port;
  unsigned char * IOMap;
  unsigned int TSSsize;
  int ring0;
  
  char currentDirectory[MAX_PATH_LENGTH];
  variableList environment;
  kernelTextInputStream *textInputStream;
  kernelTextOutputStream *textOutputStream;
  unsigned signalMask;
  stream signalStream;
  int fpuInUse;
  unsigned char fpuState[FPU_STATE_LEN];
  int fpuStateValid;
  
} kernelProcess;

// When in system calls, processes will be allowed to access information
// about themselves
extern kernelProcess *kernelCurrentProcess;

// Functions exported by kernelMultitasker.c
int kernelMultitaskerInitialize(void);
int kernelMultitaskerShutdown(int);
void kernelExceptionHandler(int, unsigned);
void kernelMultitaskerDumpProcessList(void);
int kernelMultitaskerGetCurrentProcessId(void);
// next added by Davide Airaghi
int kernelMultitaskerIsLowLevelProcess(int);
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
int kernelMultitaskerGetCurrentDirectory(char *, int);
int kernelMultitaskerSetCurrentDirectory(const char *);
kernelTextInputStream *kernelMultitaskerGetTextInput(void);
int kernelMultitaskerSetTextInput(int, kernelTextInputStream *);
kernelTextOutputStream *kernelMultitaskerGetTextOutput(void);
int kernelMultitaskerSetTextOutput(int, kernelTextOutputStream *);
int kernelMultitaskerDuplicateIO(int, int, int);
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

// next 3 added by Davide Airaghi for "dynamic" IO perms
int kernelMultitaskerAllowIO(int, unsigned int );
int kernelMultitaskerNotAllowIO(int, unsigned int );
int kernelMultitaskerGetIOperm(int, unsigned int);


#define _KERNELMULTITASKER_H
#endif
