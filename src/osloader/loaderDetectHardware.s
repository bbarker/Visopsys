;;
;;  Visopsys
;;  Copyright (C) 1998-2018 J. Andrew McLaughlin
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

	;; Restore flags
	popa

	;; Return whether we detected any fatal errors
	xor AX, AX
	mov AL, byte [FATALERROR]

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


;;
;; The data segment
;;

	SEGMENT .data
	ALIGN 4

;; This is the data structure that these routines will fill.  The
;; (flat-mode) address of this structure is eventually passed to
;; the kernel at invocation

	ALIGN 4

HARDWAREINFO:
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

;;
;; These are general messages related to hardware detection
;;

HAPPY			db 01h, ' ', 0
BLANK			db '               ', 10h, ' ', 0
MEMDETECT1		db 'Extended RAM ', 10h, ' ', 0
KREPORTED		db 'K reported', 0
FDDCHECK		db 'Floppy disks ', 10h, ' ', 0
CDCHECK			db 'CD/DVD       ', 10h, ' ', 0
CDEMUL			db 'Booting in emulation mode', 0
DISKCHECK		db ' disk(s)', 0
HEADS			db ' heads, ', 0
TRACKS			db ' tracks, ', 0
SECTS			db ' sects, ', 0
NOGRAPHICS		db 'NOGRAPH    ', 0

EMUL_SAVE	times 20 db 0

;;
;; These are error messages related to hardware detection
;;

SAD				db 'x ', 0
EMULCHECKBAD	db 'CD-ROM emulation check failed.  CD booting might not be '
				db 'successful.', 0

