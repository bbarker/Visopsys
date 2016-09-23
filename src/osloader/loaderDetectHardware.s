;;
;;  Visopsys
;;  Copyright (C) 1998-2016 J. Andrew McLaughlin
;;
;;  This program is free software; you can redistribute it and/or modify it
;;  under the terms of the GNU General Public License as published by the Free
;;  Software Foundation; either version 2 of the License, or (at your option)
;;  any later version.
;;
;;  This program is distributed in the hope that it will be useful, but
;;  WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
;;  or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License
;;  for more details.
;;
;;  You should have received a copy of the GNU General Public License along
;;  with this program; if not, write to the Free Software Foundation, Inc.,
;;  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
;;
;;  loaderDetectHardware.s
;;

	GLOBAL loaderDetectHardware
	GLOBAL HARDWAREINFO
	GLOBAL BOOTSECTSIG
	GLOBAL HDDINFO

	EXTERN loaderFindFile
	EXTERN loaderDetectVideo
	EXTERN loaderPrint
	EXTERN loaderPrintNewline
	EXTERN loaderPrintNumber
	EXTERN FATALERROR
	EXTERN PRINTINFO
	EXTERN DRIVENUMBER
	EXTERN CYLINDERS

	SEGMENT .text
	BITS 16
	ALIGN 4

	%include "loader.h"


loaderDetectHardware:
	;; This routine is called by the main program.  It is the master
	;; routine for detecting hardware, and is responsible for filling
	;; out the data structure which contains all of the information
	;; about the hardware detected, which in turn gets passed to
	;; the kernel.

	;; The routine is also responsible for stopping the boot process
	;; in the event that the system does not meet hardware requirements.
	;; The function returns a single value representing the number of
	;; fatal errors encountered during this process.

	;; Save regs
	pusha

	;; Detect the processor
	call detectProcessor

	;; Detect the memory
	call detectMemory

	;; Before we check video, make sure that the user hasn't specified
	;; text-only mode
	push word NOGRAPHICS
	call loaderFindFile
	add SP, 2

	;; Does the file exist?
	cmp AX, 1
	je .skipVideo	; The user doesn't want graphics

	;; Detect video.  Push a pointer to the start of the video
	;; information in the hardware structure
	push word VIDEO
	call loaderDetectVideo
	add SP, 2

	.skipVideo:
	;; Check for CD-ROM emulation stuffs
	call detectCdEmul

	;; Detect floppy drives, if any
	call detectFloppies

	cmp word [PRINTINFO], 1
	jne .noEmul
	;; If we're booting from CD-ROM in emulation mode, print a message
	cmp dword [BOOTCD], 1
	jne .noEmul
	mov DL, GOODCOLOR		; Use good color
	mov SI, HAPPY
	call loaderPrint
	mov SI, CDCHECK
	call loaderPrint
	mov DL, FOREGROUNDCOLOR
	mov SI, CDEMUL
	call loaderPrint
	call loaderPrintNewline
	.noEmul:

	;; Detect Fixed disk drives, if any
	call detectHardDisks

	;; Serial ports
	call detectSerial

	;; Restore flags
	popa

	;; Return whether we detected any fatal errors
	xor AX, AX
	mov AL, byte [FATALERROR]

	ret


detectProcessor:
	;; We're going to attempt to get a basic idea of the CPU
	;; type we're running on.  (We don't need to be really, really
	;; particular about the details)

	;; Save regs
	pusha

	;; Hook the 'invalid opcode' interrupt
	call int6_hook

	;; Bochs hack
	;; mov byte [INVALIDOPCODE], 1
	;; jmp .goodCPU

	;; Try an opcode which is only good on 486+
	mov word [ISRRETURNADDR], .return1
	xadd DX, DX

	;; Now try an opcode which is only good on a pentium+
	.return1:
	mov word [ISRRETURNADDR], .return2
	mov EAX, dword [0000h]
	not EAX
	cmpxchg8b [0000h]

	;; We know we're OK, but let's check for Pentium Pro+
	.return2:
	mov word [ISRRETURNADDR], .return3
	cmovne AX, BX

	;; Now we have to compare the number of 'invalid opcodes'
	;; generated
	.return3:
	mov AL, byte [INVALIDOPCODE]

	cmp AL, 3
	jae .badCPU

	;; If we fall through, we have an acceptible CPU

	;; We make the distinction between different processors
	;; based on how many invalid opcodes we got
	mov AL, byte [INVALIDOPCODE]

	cmp AL, 2
	jb .checkPentium
	;; It's a 486
	mov dword [CPUTYPE], i486
	jmp .detectMMX

	.checkPentium:
	cmp AL, 1
	jb .pentiumPro
	;; Pentium CPU
	mov dword [CPUTYPE], PENTIUM
	jmp .detectMMX

	.pentiumPro:
	;; Pentium pro CPU
	mov dword [CPUTYPE], PENTIUMPRO

	.detectMMX:
	;; Are MMX or 3DNow! extensions supported by the processor?

	mov dword [MMXEXT], 0
	mov byte [INVALIDOPCODE], 0

	;; Try an MMX opcode
	mov word [ISRRETURNADDR], .return
	movd MM0, EAX

	.return:
	;; If it was an invalid opcode, the processor does not support
	;; MMX extensions
	cmp byte [INVALIDOPCODE], 0
	jnz .print

	mov dword [MMXEXT], 1

	.print:
	;; Done.  Print information about what we found
	cmp word [PRINTINFO], 1
	jne .unhook6
	call printCpuInfo
	jmp .unhook6

	.badCPU:
	;; Print out the fatal message that we're not running an
	;; adequate processor
	mov DL, BADCOLOR	; Use error color
	mov SI, SAD
	call loaderPrint
	mov SI, PROCESSOR
	call loaderPrint

	mov SI, CPU386
	call loaderPrint

	mov SI, CPUCHECKBAD
	call loaderPrint
	call loaderPrintNewline
	mov SI, BLANK
	call loaderPrint
	mov SI, CPUCHECKBAD2
	call loaderPrint
	call loaderPrintNewline
	mov SI, BLANK
	call loaderPrint
	mov SI, CPUCHECKBAD3
	call loaderPrint
	call loaderPrintNewline

	;; Register the fatal error
	add byte [FATALERROR], 01h

	;; We're finished

	.unhook6:
	;; Unhook the 'invalid opcode' interrupt
	call int6_restore

	.done:
	;; Restore regs
	popa
	ret


detectMemory:
	;; Determine the amount of extended memory

	;; Save regs
	pusha

	mov dword [EXTENDEDMEMORY], 0

	;; This BIOS function will give us the amount of extended memory
	;; (even greater than 64M), but it is not found in old BIOSes.  We'll
	;; have to assume that if the function is not available, the
	;; extended memory is less than 64M.

	mov EAX, 0000E801h	; Subfunction
	int 15h
	jc .noE801

	and EAX, 0000FFFFh	; 16-bit value, memory under 16M in 1K blocks
	and EBX, 0000FFFFh	; 16-bit, memory over 16M in 64K blocks
	shl EBX, 6		; Multiply by 64 to get 1K blocks
	add EAX, EBX
	mov dword [EXTENDEDMEMORY], EAX

	jmp .printMemory

	.noE801:
	;; We will use this as a last-resort method for getting memory
	;; size.  We just grab the 16-bit value from CMOS

	mov AL, 17h	;; Select the address we need to get the data
	out 70h, AL	;; from

	in AL, 71h
	mov byte [EXTENDEDMEMORY], AL

	mov AL, 18h	;; Select the address we need to get the data
	out 70h, AL	;; from

	in AL, 71h
	mov byte [(EXTENDEDMEMORY + 1)], AL

	.printMemory:
	cmp word [PRINTINFO], 1
	jne .noPrint
	call printMemoryInfo
	.noPrint:

	;; Now, can the system supply us with a memory map?  If it can,
	;; this will allow us to supply a list of unusable memory to the
	;; kernel (which will improve reliability, we hope).  Try to call
	;; the appropriate BIOS function.

	;; This function might dink with ES
	push ES

	xor EBX, EBX		; Continuation counter
	mov DI, MEMORYMAP	; The buffer

	.smapLoop:
	mov EAX, 0000E820h		; Function number
	mov EDX, 534D4150h		; ('SMAP')
	mov ECX, memoryInfoBlock_size	; Size of buffer
	int 15h

	;; Call successful?
	jc .doneSmap

	;; Function supported?
	cmp EAX, 534D4150h	; ('SMAP')
	jne .doneSmap

	;; All done?
	cmp EBX, 0
	je .doneSmap

	;; Call the BIOS for the next bit
	add DI, 20
	cmp DI, (MEMORYMAP + (MEMORYMAPSIZE * memoryInfoBlock_size))
	jl .smapLoop

	.doneSmap:
	;; Restore ES
	pop ES

	;; Restore regs
	popa
	ret


detectCdEmul:
	;; This routine will detect CD-ROM emulation stuffs

	;; Save regs
	pusha

	cmp word [DRIVENUMBER], 80h
	jae .notEmul

	mov AX, 4B01h
	mov DX, word [DRIVENUMBER]
	mov SI, EMUL_SAVE
	int 13h
	jnc .emulCheck

	;; Couldn't check for CD-ROM emulation.  This might cause CD-ROM
	;; booting to fail on this system.  *However*, it appers that in
	;; some of these cases, the number of cylinders will be
	;; unrealistically large, so we will assume in that case.

	cmp dword [CYLINDERS], 256
	ja .isEmul

	;; Else, print a warning.
	mov DL, BADCOLOR	; Use error color
	mov SI, SAD
	call loaderPrint
	mov SI, EMULCHECKBAD
	call loaderPrint
	call loaderPrintNewline
	jmp .notEmul

	.emulCheck:
	mov AL, byte [EMUL_SAVE + 1]
	and AL, 07h
	cmp AL, 0
	je .notEmul
	cmp AL, 04h
	ja .notEmul

	.isEmul:
	mov dword [BOOTCD], 1

	.notEmul:
	popa
	ret


detectFloppies:
	;; This routine will detect the number and types of floppy
	;; disk drives on board

	;; Save regs
	pusha

	;; Initialize 'number of floppies' value
	mov dword [FLOPPYDISKS], 0

	;; This is a buggy, overloaded, screwy interrupt call.  We need to
	;; do this carefully to make sure we get good/real info.  It will
	;; destroy ES, so save it
	push ES

	mov SI, FD0
	push word 0		; Disk number counter

	.floppyLoop:

	;; Pre-increment the disk counter so we can 'continue' if we get
	;; any funny things, without missing any following devices
	pop DX
	mov AX, DX
	inc AX
	push AX

	;; My Toshiba laptop reports the 'fake' floppy from bootable
	;; CD-ROM emulations as a real one, indistinguishable from real
	;; ones.  So, if the emulation disk number is the same as this one,
	;; skip it.
	cmp dword [BOOTCD], 1
	jne .noEmul
	cmp DL, byte [EMUL_SAVE + 2]
	je .floppyLoop
	.noEmul:

	;; Any more to do?
	cmp DX, 2
	jae .print

	;; Guards againt BIOS bugs, apparently
	push word 0
	pop ES
	xor DI, DI

	;; Now the screwy interrupt
	mov AX, 0800h
	xor BX, BX
	xor CX, CX
	int 13h

	;; If there was an error, continue
	jc .print

	;; If ES:DI is NULL, continue
	xor EAX, EAX
	mov AX, ES
	shl EAX, 16
	mov AX, DI
	cmp EAX, 0
	je .floppyLoop

	;; If any of these registers are empty, continue
	cmp BX, 0
	je .floppyLoop
	cmp CX, 0
	je .floppyLoop
	cmp DX, 0
	je .floppyLoop

	;; Count it
	add dword [FLOPPYDISKS], 1

	;; Put the type/head/track/sector values into the data structures
	xor EAX, EAX
	mov AL, BL
	mov dword [SI + fddInfoBlock.type], EAX
	inc DH			; Number is 0-based
	mov AL, DH
	mov dword [SI + fddInfoBlock.heads], EAX
	inc CH			; Number is 0-based
	mov AL, CH
	mov dword [SI + fddInfoBlock.tracks], EAX
	mov AL, CL
	mov dword [SI + fddInfoBlock.sectors], EAX

	;; Move our pointer to the next disk
	add SI, fddInfoBlock_size

	jmp .floppyLoop

	.print:
	sti	; Buggy BIOSes can apparently leave ints disabled
	pop AX	; Loop control
	pop ES
	cmp word [PRINTINFO], 1
	jne .done

	;; Print message about the disk scan

	mov DL, GOODCOLOR		; Use good color
	mov SI, HAPPY
	call loaderPrint
	mov SI, FDDCHECK
	call loaderPrint

	mov DL, FOREGROUNDCOLOR	; Switch to foreground color
	mov EAX, dword [FLOPPYDISKS]
	call loaderPrintNumber
	mov SI, DISKCHECK
	call loaderPrint
	call loaderPrintNewline

	cmp dword [FLOPPYDISKS], 1
	jb .done
	;; Print information about the disk.  EBX contains the pointer...
	mov EBX, FD0
	call printFddInfo

	cmp dword [FLOPPYDISKS], 2
	jb .done
	;; Print information about the disk.  EBX contains the pointer...
	mov EBX, FD1
	call printFddInfo

	.done:
	;; Restore regs
	popa

	ret


detectHardDisks:
	;; This routine will detect the number, types, and sizes of
	;; hard disk drives on board

	;; Save regs
	pusha

	;; Initialize
	mov dword [HARDDISKS], 0

	;; Call the BIOS int13h function with the number of the first
	;; disk drive.  Doesn't matter if it's actually present -- all
	;; we want to do is find out how many drives there are

	;; This interrupt call will destroy ES, so save it
	push ES

	;; Guards againt BIOS bugs, apparently
	push word 0
	pop ES
	xor DI, DI

	mov AX, 0800h
	xor BX, BX
	xor CX, CX
	mov DX, 0080h
	int 13h

	;; Restore ES
	pop ES

	;; Now, if the carry bit is set, we assume no hard disks
	;; and we're finished
	jc near .done

	;; Save the number
	xor EAX, EAX
	mov AL, DL
	mov dword [HARDDISKS], EAX

	.printDisks:
	cmp word [PRINTINFO], 1
	jne .noPrint1
	;; Print messages about the disk scan
	mov DL, GOODCOLOR		; Use good color
	mov SI, HAPPY
	call loaderPrint
	mov SI, HDDCHECK
	call loaderPrint

	mov EAX, dword [HARDDISKS]
	call loaderPrintNumber
	mov DL, FOREGROUNDCOLOR
	mov SI, DISKCHECK
	call loaderPrint
	call loaderPrintNewline
	.noPrint1:

	;; Attempt to determine information about the drives

	;; Start with drive 0
	mov ECX, 0
	mov EDI, HDDINFOBLOCK

	.driveLoop:

	;; Test here instead of at the end, since some BIOSes can return
	;; success from the 'get drive parameters' call, above, but with
	;; the number of disks reported as zero
	cmp ECX, dword [HARDDISKS]
	jae near .done

	;; This interrupt call will destroy ES, so save it
	push ECX		; Save this first
	push EDI
	push ES

	;; Guards againt BIOS bugs, apparently
	push word 0
	pop ES
	xor DI, DI

	mov AX, 0800h		; Read disk drive parameters
	xor BX, BX
	xor DX, DX
	mov DL, 80h
	add DL, CL
	xor CX, CX
	int 13h

	;; Restore
	pop ES
	pop EDI

	;; If carry set, the call was unsuccessful (for whatever reason)
	;; and we will move to the next disk
	jnc .okOldCall

	;; Error
	pop ECX
	jmp .nextDisk

	.okOldCall:
	;; Save the numbers of heads, cylinders, and sectors, and the
	;; sector size (usually 512)

	;; heads
	xor EAX, EAX
	mov AL, DH
	inc AX			; Number is 0-based
	mov dword [EDI + hddInfoBlock.heads], EAX

	;; cylinders
	xor EAX, EAX
	mov AL, CL		; Two bits of cylinder number in bits 6&7
	and AL, 11000000b	; Mask it
	shl AX, 2		; Move them to bits 8&9
	mov AL, CH		; Rest of the cylinder bits
	inc AX			; Number is 0-based
	mov dword [EDI + hddInfoBlock.cylinders], EAX

	;; sectors
	xor EAX, EAX
	mov AL, CL		; Bits 0-5
	and AL, 00111111b	; Mask it
	mov dword [EDI + hddInfoBlock.sectors], EAX
	mov dword [EDI + hddInfoBlock.bytesPerSector], 512 ; Assume 512 BPS

	;; Restore ECX
	pop ECX

	;; Calculate the disk size.  EDI contains the pointer...
	call diskSize

	cmp word [PRINTINFO], 1
	jne .noPrint2
	;; Print information about the disk.  EBX contains the pointer...
	mov EBX, EDI
	call printHddInfo
	.noPrint2:

	;; Reset/specify/recalibrate the disk and controller
	push ECX
	mov AX, 0D00h
	mov DL, 80h
	add DL, CL
	int 13h
	pop ECX

	.nextDisk:
	;; Prepare for the next disk.  Counter is checked at the beginning
	;; of the loop.
	inc ECX
	jmp .driveLoop

	.done:
	popa
	ret


detectSerial:
	;; Detects the serial ports

	pusha
	push ES

	xor EAX, EAX

	push 0040h
	pop ES

	mov AX, word [ES:00h]
	mov dword [SERIAL + serialInfoBlock.port1], EAX
	mov AX, word [ES:02h]
	mov dword [SERIAL + serialInfoBlock.port2], EAX
	mov AX, word [ES:04h]
	mov dword [SERIAL + serialInfoBlock.port3], EAX
	mov AX, word [ES:06h]
	mov dword [SERIAL + serialInfoBlock.port4], EAX

	pop ES
	popa
	ret


diskSize:
	;; This calculates the total number of sectors on the disk.  Takes
	;; a pointer to the disk data in EDI, and puts the number in the
	;; "totalsectors" slot of the data block

	;; Save regs
	pusha
	push EDI

	;; Determine whether we can use an extended BIOS function to give
	;; us the number of sectors

	mov word [HDDINFO], 42h  ; Size of the info buffer we provide
	mov AH, 48h
	mov DL, CL
	add DL, 80h
	mov SI, HDDINFO
	int 13h

	;; Function call successful?
	jc .noEBIOS

	pop EDI
	;; Save the number of sectors
	mov EAX, dword [HDDINFO + 10h]
	mov dword [EDI + hddInfoBlock.totalSectors], EAX

	;; Recalculate the number of cylinders
	mov EAX, dword [EDI + hddInfoBlock.heads]	; heads
	mul dword [EDI + hddInfoBlock.sectors]		; sectors per cyl
	mov ECX, EAX					; total secs per cyl
	test dword ECX, 0
	jz .done
	xor EDX, EDX
	mov EAX, dword [EDI + hddInfoBlock.totalSectors]
	div ECX
	mov dword [EDI + hddInfoBlock.cylinders], EAX	; new cyls value

	jmp .done

	.noEBIOS:
	pop EDI
	mov EAX, dword [EDI]				; heads
	mul dword [EDI + hddInfoBlock.cylinders]	; cylinders
	mul dword [EDI + hddInfoBlock.sectors]		; sectors per cyl

	mov dword [EDI + hddInfoBlock.totalSectors], EAX

	.done:
	popa
	ret


printCpuInfo:
	;; Takes no parameter, and prints info about the CPU that was
	;; detected

	pusha

	mov DL, GOODCOLOR		; Use good color
	mov SI, HAPPY
	call loaderPrint
	mov SI, PROCESSOR
	call loaderPrint

	;; Switch to foreground color
	mov DL, FOREGROUNDCOLOR

	;; What type of CPU was it?
	mov EAX, dword [CPUTYPE]

	cmp EAX, PENTIUMPRO
	jne .notPPro
	;; Say we found a pentium pro CPU
	mov SI, CPUPPRO
	jmp .printType

	.notPPro:
	cmp EAX, PENTIUM
	jne .notPentium
	;; Say we found a Pentium CPU
	mov SI, CPUPENTIUM
	jmp .printType

	.notPentium:
	;; Say we found a 486 CPU
	mov SI, CPU486

	.printType:
	call loaderPrint

	;; If we have a pentium or better, we can find out some more
	;; information using the cpuid instruction
	cmp dword [CPUTYPE], i486
	je .noCPUID

	mov EAX, 0
	cpuid

	;; Now, EBX:EDX:ECX should contain the vendor "string".  This might
	;; be, for example "AuthenticAMD" or "GenuineIntel"
	mov dword [CPUVEND], EBX
	mov dword [(CPUVEND + 4)], EDX
	mov dword [(CPUVEND + 8)], ECX
	;; There are already 4 bytes of NULLs at the end of this

	;; Print the CPU vendor string
	mov SI, CPUVEND
	mov DL, FOREGROUNDCOLOR
	call loaderPrint
	mov SI, CLOSEBRACKETS
	call loaderPrint
	.noCPUID:

	;; Do we have MMX?
	cmp dword [MMXEXT], 1
	jne .noMMX
	mov SI, MMX			;; Say we found MMX
	mov DL, FOREGROUNDCOLOR
	call loaderPrint
	.noMMX:

	call loaderPrintNewline

	popa
	ret


printMemoryInfo:
	;;  Takes no parameters and prints out the amount of memory detected

	pusha

	mov DL, GOODCOLOR		; Use good color
	mov SI, HAPPY
	call loaderPrint
	mov SI, MEMDETECT1
	call loaderPrint

	mov EAX, dword [EXTENDEDMEMORY]
	call loaderPrintNumber
	mov SI, KREPORTED
	mov DL, FOREGROUNDCOLOR
	call loaderPrint
	call loaderPrintNewline

	popa
	ret


printFddInfo:
	;; This takes a pointer to the disk data in EBX, and prints
	;; disk info to the console

	;; Save regs
	pusha

	;; Print a message about what we found
	mov DL, GOODCOLOR
	mov SI, BLANK
	call loaderPrint

	mov EAX, dword [EBX + fddInfoBlock.heads]
	call loaderPrintNumber

	mov DL, FOREGROUNDCOLOR
	mov SI, HEADS
	call loaderPrint

	mov EAX, dword [EBX + fddInfoBlock.tracks]
	call loaderPrintNumber

	mov DL, FOREGROUNDCOLOR
	mov SI, TRACKS
	call loaderPrint

	mov EAX, dword [EBX + fddInfoBlock.sectors]
	call loaderPrintNumber

	mov DL, FOREGROUNDCOLOR
	mov SI, SECTS
	call loaderPrint
	call loaderPrintNewline

	;; Restore regs
	popa

	ret


printHddInfo:
	;; This takes a pointer to the disk data in EBX, and prints
	;; disk info to the console

	;; Save regs
	pusha

	;; Print a message about what we found
	mov DL, GOODCOLOR
	mov SI, BLANK
	call loaderPrint

	mov EAX, dword [EBX + hddInfoBlock.heads]
	call loaderPrintNumber

	mov DL, FOREGROUNDCOLOR
	mov SI, HEADS
	call loaderPrint

	mov EAX, dword [EBX + hddInfoBlock.cylinders]
	call loaderPrintNumber

	mov DL, FOREGROUNDCOLOR
	mov SI, CYLS
	call loaderPrint

	mov EAX, dword [EBX + hddInfoBlock.sectors]
	call loaderPrintNumber

	mov DL, FOREGROUNDCOLOR
	mov SI, SECTS
	call loaderPrint

	mov EAX, dword [EBX + hddInfoBlock.bytesPerSector]
	call loaderPrintNumber

	mov DL, FOREGROUNDCOLOR
	mov SI, BPSECT
	call loaderPrint

	mov EAX, dword [EBX + hddInfoBlock.totalSectors]
	shr EAX, 11		; Turn (assumed) 512-byte sectors to MB
	call loaderPrintNumber

	mov DL, FOREGROUNDCOLOR
	mov SI, MEGA
	call loaderPrint
	call loaderPrintNewline

	;; Restore regs
	popa
	ret


int6_hook:
	;; This sets up our hook for interrupt 6, in order to catch
	;; invalid opcodes for CPU determination

	;; Save regs
	pusha

	;; Get the address of the current interrupt 6 handler
	;; and save it

	;; Set ES so that it points to the beginning of memory
	push ES
	xor AX, AX
	mov ES, AX

	mov AX, word [ES:0018h]	;; The offset of the routine
	mov word [OLDINT6], AX
	mov AX, word [ES:001Ah]	;; The segment of the routine
	mov word [(OLDINT6 + 2)], AX

	cli

	;; Move the address of our new handler into the interrupt
	;; table
	mov word [ES:0018h], int6_handler	;; The offset
	mov word [ES:001Ah], CS		;; The segment

	sti

	;; Restore ES
	pop ES

	;; Initialize the value that keeps track of invalid opcodes
	mov byte [INVALIDOPCODE], 00h

	popa
	ret


int6_handler:
	;; This is our int 6 interrupt handler, to determine
	;; when an invalid opcode has been generated

	;; If we got here, then we know we have an invalid opcode,
	;; so we have to change the value in the INVALIDOPCODE
	;; memory location

	push AX
	push BX

	cli

	add byte [INVALIDOPCODE], 1

	;; Better change the instruction pointer to point to the
	;; next instruction to execute
	mov AX, word [ISRRETURNADDR]
	mov BX, SP
	mov word [SS:(BX + 4)], AX

	sti

	pop BX
	pop AX

	iret


int6_restore:
	;; This unhooks interrupt 6 (we'll let the kernel handle
	;; interrupts in its own way later)

	pusha

	;; Set ES so that it points to the beginning of memory
	push ES
	xor AX, AX
	mov ES, AX

	cli

	mov AX, word [OLDINT6]
	mov word [ES:0018h], AX	;; The offset of the routine
	mov AX, word [OLDINT6 + 2]
	mov word [ES:001Ah], AX	;; The segment of the routine

	sti

	;; Restore ES
	pop ES

	popa
	ret


;;
;; The data segment
;;

	SEGMENT .data
	ALIGN 4

OLDINT6		dd 0		;; Address of the interrupt 6 handler
ISRRETURNADDR	dw 0	;; The offset of the return address for int6
INVALIDOPCODE	db 0	;; To pass data from our interrupt handler
HARDDISKS	dd 0		;; Number present
HDDINFO		times 42h  db 0	;; Space for info ret by EBIOS
HDDINFOBLOCK: ISTRUC hddInfoBlock
	times hddInfoBlock_size db 0
IEND

;; This is the data structure that these routines will fill.  The
;; (flat-mode) address of this structure is eventually passed to
;; the kernel at invocation

	ALIGN 4

HARDWAREINFO:
	CPUTYPE			dd 0			;; See %defines at top
	CPUVEND			dd 0, 0, 0, 0	;; CPU vendor string, if supported
	MMXEXT			dd 0			;; Boolean; 1 or zero
	EXTENDEDMEMORY	dd 0			;; In Kbytes

	;; Info returned by int 15h function E820h
	MEMORYMAP:
	times (MEMORYMAPSIZE * memoryInfoBlock_size) db 0

	;; This is all the information about the video capabilities
	VIDEO: ISTRUC graphicsInfoBlock
		times graphicsInfoBlock_size db 0
	IEND

	;; This is the info about the boot device and booted sector
	BOOTSECTSIG		dd 0			;; Boot sector signature
	BOOTCD			dd 0			;; Booting from a CD

	;; This is an array of info about up to 2 floppy disks in the system
	FLOPPYDISKS		dd 0			;; Number present
	;; Floppy 0
	FD0: ISTRUC fddInfoBlock
		times fddInfoBlock_size db 0
	IEND
	;; Floppy 1
	FD1: ISTRUC fddInfoBlock
		times fddInfoBlock_size db 0
	IEND

	;; Info about the serial ports
	SERIAL: ISTRUC serialInfoBlock
		times serialInfoBlock_size db 0
	IEND

;;
;; These are general messages related to hardware detection
;;

HAPPY			db 01h, ' ', 0
BLANK			db '               ', 10h, ' ', 0
PROCESSOR		db 'Processor    ', 10h, ' ', 0
CPUPPRO			db 'Pentium Pro or better ("', 0
CPUPENTIUM		db 'Pentium ("', 0
CPU486			db 'i486', 0
CPU386			db 'i386 (or lower)', 0
CLOSEBRACKETS	db '") ', 0
MMX				db 'with MMX', 0
MEMDETECT1		db 'Extended RAM ', 10h, ' ', 0
KREPORTED		db 'K reported', 0
FDDCHECK		db 'Floppy disks ', 10h, ' ', 0
CDCHECK			db 'CD/DVD       ', 10h, ' ', 0
CDEMUL			db 'Booting in emulation mode', 0
HDDCHECK		db 'Hard disks   ', 10h, ' ', 0
DISKCHECK		db ' disk(s)', 0
HEADS			db ' heads, ', 0
TRACKS			db ' tracks, ', 0
CYLS			db ' cyls, ', 0
SECTS			db ' sects, ', 0
BPSECT			db ' bps  ', 0
MEGA			db ' MBytes', 0
NOGRAPHICS		db 'NOGRAPH    ', 0

EMUL_SAVE	times 20 db 0

;;
;; These are error messages related to hardware detection
;;

SAD				db 'x ', 0
EMULCHECKBAD	db 'CD-ROM emulation check failed.  CD booting might not be '
				db 'successful.', 0
CPUCHECKBAD		db ': This processor is not adequate to run Visopsys.  '
				db 'Visopsys', 0
CPUCHECKBAD2	db 'requires an i486 or better processor in order to '
				db 'function', 0
CPUCHECKBAD3	db 'properly.  Please see your computer dealer.', 0

