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
//  processor.h
//

#if !defined(_PROCESSOR_H)

// This file contains macros for processor-specific operations.  At the moment
// it's only for X86 processors.

#define PROCESSOR_LITTLE_ENDIAN			1

// Model-specific registers that we use
#define X86_MSR_APICBASE				0x1B

// Bitfields for the APICBASE MSR
#define X86_MSR_APICBASE_BASEADDR		0xFFFFF000
#define X86_MSR_APICBASE_APICENABLE		0x00000800
#define X86_MSR_APICBASE_BSP			0x00000100

//
// Processor registers
//

#define processorId(arg, rega, regb, regc, regd) \
	__asm__ __volatile__ ("cpuid" : "=a" (rega), "=b" (regb), "=c" (regc), \
		"=d" (regd) : "a" (arg))

#define processorReadMsr(msr, rega, regd) \
	__asm__ __volatile__ ("rdmsr" : "=a" (rega), "=d" (regd) : "c" (msr));

#define processorWriteMsr(msr, rega, regd) \
	__asm__ __volatile__ ("wrmsr" : : "a" (rega), "d" (regd), "c" (msr));

#define processorGetCR0(variable) \
	__asm__ __volatile__ ("movl %%cr0, %0" : "=r" (variable))

#define processorSetCR0(variable) \
	__asm__ __volatile__ ("movl %0, %%cr0" : : "r" (variable))

#define processorGetCR3(variable) \
	__asm__ __volatile__ ("movl %%cr3, %0" : "=r" (variable))

#define processorSetCR3(variable) \
	__asm__ __volatile__ ("movl %0, %%cr3" : : "r" (variable))

#define processorClearAddressCache(addr) \
	__asm__ __volatile__ ("invlpg %0" : : "m" (*((char *)(addr))))

#define processorTimestamp(hi, lo) do { \
	processorId(0, hi, hi, hi, hi); /* serialize */ \
	__asm__ __volatile__ ("rdtsc" : "=a" (lo), "=d" (hi)); \
} while (0)

//
// Stack operations
//

#define processorPush(value) \
	__asm__ __volatile__ ("pushl %0" : : "r" (value) : "%esp")

#define processorPop(variable) \
	__asm__ __volatile__ ("popl %0" : "=r" (variable) : : "%esp")

#define processorPushRegs() __asm__ __volatile__ ("pushal" : : : "%esp")

#define processorPopRegs() __asm__ __volatile__ ("popal" : : : "%esp")

#define processorPushFlags() __asm__ __volatile__ ("pushfl" : : : "%esp")

#define processorPopFlags() __asm__ __volatile__ ("popfl" : : : "%esp")

#define processorGetStackPointer(addr) \
	__asm__ __volatile__ ("movl %%esp, %0" : "=r" (addr))

#define processorSetStackPointer(addr) \
	__asm__ __volatile__ ("movl %0, %%esp" : : "r" (addr) : "%esp")

//
// Interrupts
//

#define processorIntStatus(variable) do { \
	processorPushFlags(); \
	processorPop(variable); \
	variable = ((variable >> 9) & 1); \
} while (0)

#define processorEnableInts() __asm__ __volatile__ ("sti")

#define processorDisableInts() __asm__ __volatile__ ("cli")

#define processorSuspendInts(variable) do { \
	processorIntStatus(variable); \
	processorDisableInts(); \
} while (0)

#define processorRestoreInts(variable) do { \
	if (variable) \
		processorEnableInts(); \
} while (0)

//
// Memory copying
//

#define processorSetDirection() __asm__ __volatile__ ("std")

#define processorClearDirection() __asm__ __volatile__ ("cld")

#define processorCopyBytes(src, dest, count) do { \
	processorPushRegs(); \
	processorPushFlags(); \
	processorClearDirection(); \
	__asm__ __volatile__ ( \
		"rep movsb" \
		: : "S" (src), "D" (dest), "c" (count)); \
	processorPopFlags(); \
	processorPopRegs(); \
} while (0)

#define processorCopyBytesBackwards(src, dest, count) do { \
	processorPushRegs(); \
	processorPushFlags(); \
	processorSetDirection(); \
	__asm__ __volatile__ ( \
		"rep movsb" \
		: : "S" (src), "D" (dest), "c" (count)); \
	processorPopFlags(); \
	processorPopRegs(); \
} while (0)

#define processorCopyDwords(src, dest, count) do { \
	processorPushRegs(); \
	processorPushFlags(); \
	processorClearDirection(); \
	__asm__ __volatile__ ( \
		"rep movsl" \
		: : "S" (src), "D" (dest), "c" (count)); \
	processorPopFlags(); \
	processorPopRegs(); \
} while (0)

#define processorCopyDwordsBackwards(src, dest, count) do { \
	processorPushRegs(); \
	processorPushFlags(); \
	processorSetDirection(); \
	__asm__ __volatile__ ( \
		"rep movsl" \
		: : "S" (src), "D" (dest), "c" (count)); \
	processorPopFlags(); \
	processorPopRegs(); \
} while (0)

#define processorWriteBytes(value, dest, count) do { \
	processorPushRegs(); \
	processorPushFlags(); \
	processorClearDirection(); \
	__asm__ __volatile__ ( \
		"rep stosb" \
		: : "a" (value), "D" (dest), "c" (count)); \
	processorPopFlags(); \
	processorPopRegs(); \
} while (0)

#define processorWriteWords(value, dest, count) do { \
	processorPushRegs(); \
	processorPushFlags(); \
	processorClearDirection(); \
	__asm__ __volatile__ ( \
		"rep stosw" \
		: : "a" (value), "D" (dest), "c" (count)); \
	processorPopFlags(); \
	processorPopRegs(); \
} while (0)

#define processorWriteDwords(value, dest, count) do { \
	processorPushRegs(); \
	processorPushFlags(); \
	processorClearDirection(); \
	__asm__ __volatile__ ( \
		"rep stosl" \
		: : "a" (value), "D" (dest), "c" (count)); \
	processorPopFlags(); \
	processorPopRegs(); \
} while (0)

//
// Port I/O
//

#define processorInPort8(port, data) \
	__asm__ __volatile__ ("inb %%dx, %%al" : "=a" (data) : "d" (port))

#define processorOutPort8(port, data) \
	__asm__ __volatile__ ("outb %%al, %%dx" : : "a" (data), "d" (port))

#define processorInPort16(port, data) \
	__asm__ __volatile__ ("inw %%dx, %%ax" : "=a" (data) : "d" (port))

#define processorOutPort16(port, data) \
	__asm__ __volatile__ ("outw %%ax, %%dx" : : "a" (data), "d" (port))

#define processorInPort32(port, data) \
	__asm__ __volatile__ ("inl %%dx, %%eax" : "=a" (data) : "d" (port))

#define processorOutPort32(port, data) \
	__asm__ __volatile__ ("outl %%eax, %%dx" : : "a" (data), "d" (port))

//
// Task-related (multiasking, interrupt/exception handling, API)
//

#define processorSetGDT(ptr, size) do { \
	processorPushFlags(); \
	processorDisableInts(); \
	processorPush(ptr); \
	__asm__ __volatile__ ( \
		"pushw %%ax \n\t" \
		"lgdt (%%esp) \n\t" \
		"addl $6, %%esp" \
		: : "a" (size) : "%esp"); \
	processorPopFlags(); \
} while (0)

#define processorSetIDT(ptr, size) do { \
	processorPushFlags(); \
	processorDisableInts(); \
	processorPush(ptr); \
	__asm__ __volatile__ ( \
		"pushw %%ax \n\t" \
		"lidt (%%esp) \n\t" \
		"addl $6, %%esp" \
		: : "a" (size) : "%esp"); \
	processorPopFlags(); \
} while (0)

#define processorLoadTaskReg(selector) do { \
	processorPushFlags(); \
	processorDisableInts(); \
	__asm__ __volatile__ ("ltr %%ax" : : "a" (selector)); \
	processorPopFlags(); \
} while (0)

#define processorClearTaskSwitched() __asm__ __volatile__ ("clts")

#define processorGetInstructionPointer(addr) \
	__asm__ __volatile__ ( \
		"call 1f \n\t" \
		"1: pop %0" : "=r" (addr))

#define processorFarJump(selector) do { \
	processorPushFlags(); \
	processorPush(selector); \
	processorPush(0); \
	__asm__ __volatile__ ( \
		"ljmp *(%%esp) \n\t" \
		"addl $8, %%esp" \
		: : : "%esp"); \
	processorPopFlags(); \
} while (0)

#define processorIsrCall(addr) do { \
	processorPush(PRIV_CODE); \
	processorPush(addr); \
	__asm__ __volatile__ ("movl %%esp, %%eax" : : : "%eax"); \
	processorPushFlags(); \
	__asm__ __volatile__ ( \
		"lcall *(%%eax) \n\t" \
		"add $8, %%esp" \
		: : : "%eax"); \
} while (0)

#define processorIntReturn() __asm__ __volatile__ ("iret")

#define processorFarReturn() __asm__ __volatile__ ("lret")

#define processorGetFramePointer(addr) \
	__asm__ __volatile__ ("movl %%ebp, %0" : "=r" (addr))

#define processorPopFrame() \
	__asm__ __volatile__ ( \
		"movl %%ebp, %%esp \n\t" \
		"popl %%ebp" \
		: : : "%esp" )

#define processorExceptionEnter(exAddr, ints) do { \
	processorPushRegs(); \
	processorSuspendInts(ints); \
	__asm__ __volatile__ ("movl 4(%%ebp), %0" : "=r" (exAddr)); \
} while (0)

#define processorExceptionExit(ints) do { \
	processorRestoreInts(ints); \
	processorPopRegs(); \
	processorPopFrame(); \
	processorIntReturn(); \
} while (0)

#define processorIsrEnter(stAddr) do { \
	processorDisableInts(); \
	processorPushRegs(); \
	processorGetStackPointer(stAddr); \
} while (0)

#define processorIsrExit(stAddr) do { \
	processorSetStackPointer(stAddr); \
	processorPopRegs(); \
	processorPopFrame(); \
	processorEnableInts(); \
	processorIntReturn(); \
} while (0)

#define processorApiExit(stAddr, codeLo, codeHi) do { \
	__asm__ __volatile__ ( \
		"movl %0, %%eax \n\t" \
		"movl %1, %%edx" \
		: : "r" (codeLo), "r" (codeHi) : "%eax", "%edx" ); \
	processorPopFrame(); \
	processorFarReturn(); \
} while (0)

//
// Floating point ops
//

#define processorGetFpuStatus(code) \
	__asm__ __volatile__ ("fstsw %0" : "=a" (code))

#define processorFpuStateSave(addr) \
	__asm__ __volatile__ ( \
		"fnsave %0 \n\t" \
		"fwait" \
		: : "m" (addr) : "memory")

#define processorFpuStateRestore(addr) \
	__asm__ __volatile__ ("frstor %0" : : "m" (addr))

#define processorFpuInit() __asm__ __volatile__ ("fninit")

#define processorGetFpuControl(code) \
	__asm__ __volatile__ ("fstcw %0" : "=m" (code))

#define processorSetFpuControl(code) \
	__asm__ __volatile__ ("fldcw %0" : : "m" (code))

#define processorFpuClearEx() \
	__asm__ __volatile__ ("fnclex");

//
// Misc
//

#define processorLock(lck, proc) \
	__asm__ __volatile__ ("lock cmpxchgl %1, %2" \
		: : "a" (0), "r" (proc), "m" (lck) : "memory")

static inline unsigned short processorSwap16(unsigned short variable)
{
	volatile unsigned short tmp = (variable);
	__asm__ __volatile__ ("rolw $8, %0" : "=r" (tmp) : "r" (tmp));
	return (tmp);
}

static inline unsigned processorSwap32(unsigned variable)
{
	volatile unsigned tmp = (variable);
	__asm__ __volatile__ ("bswap %0" : "=r" (tmp) : "r" (tmp));
	return (tmp);
}

#define processorDelay() do { \
	unsigned char d; \
	processorPushRegs(); \
	processorInPort8(0x3F6, d); \
	processorInPort8(0x3F6, d); \
	processorInPort8(0x3F6, d); \
	processorInPort8(0x3F6, d); \
	processorPopRegs(); \
} while (0)

#define processorHalt() __asm__ __volatile__ ("hlt")

#define processorIdle() do { \
	processorEnableInts(); \
	processorHalt(); \
} while (0)

#define processorStop() do { \
	processorDisableInts(); \
	processorHalt(); \
} while (0)

#define processorReboot() do { \
	processorDisableInts(); \
	__asm__ __volatile__ ( \
		"movl $0xFE, %%eax \n\t" \
		"outb %%al, $0x64" \
		: : : "%eax"); \
	processorHalt(); \
} while (0)

#define _PROCESSOR_H
#endif

