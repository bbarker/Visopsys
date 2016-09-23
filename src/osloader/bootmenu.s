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
;;  bootmenu.s
;;

;; This code is a boot menu for chain-loading different partitions.

	ORG 0000h
	SEGMENT .text
	BITS 16
	ALIGN 4

	;; Some handy definitions we need
	%include "loader.h"

	%define STRINGLENGTH 60

;; To describe each of our possibilities
STRUC bootTarget
 .string:		resb STRINGLENGTH	; Descriptive string
 .startSector		resd 1			; Sector to boot
ENDSTRUC


main:
	jmp bootCode				; 00 - 02 Jump instruction
	nop					; 03 - 03 No op

	TARGETS: ISTRUC bootTarget
	times bootTarget_size	db 0
	IEND
	ISTRUC bootTarget
	times bootTarget_size	db 0
	IEND
	ISTRUC bootTarget
	times bootTarget_size	db 0
	IEND
	ISTRUC bootTarget
	times bootTarget_size	db 0
	IEND
	NUM_TARGETS		dd 0
	DEFAULT_ENTRY		dd 0
	TIMEOUT_SECONDS		dd 0
	MAGIC			db 'VBM2'


bootCode:
	cli

	;; Make the data segment registers be the same as the code segment
	mov AX, CS
	mov DS, AX
	mov ES, AX

	;; Set the stack to be at the top of the code
	mov SS, AX
	mov SP, main

	sti

	pusha

	;; The MBR sector will pass the boot device number to us in the DL
	;; register.
	mov byte [DISK], DL

	;; The MBR will pass a pointer to the start of the partition table
	;; in SI
	mov word [PART_TABLE], SI

	;; Set the text display to a good mode, clearing the screen
	call setTextMode
	call clearScreen

	push word LOADMSG
	call print
	add SP, 2

	;; Get disk parameters
	%include "bootsect-diskparms.s"

	;; Make sure there's at least one boot target
	cmp word [NUM_TARGETS], 0
	jne .okChoices

	push word NOTARGETS
	call print
	add SP, 2
	.stop1: jmp .stop1

	.okChoices:

	;; Get the default
	mov EAX, dword [DEFAULT_ENTRY]
	mov word [SELECTEDTARGET], AX

	;; Read the current RTC seconds value
	call readTimer
	mov word [RTCSECONDS], AX
	mov EAX, dword [TIMEOUT_SECONDS]
	mov word [TIMEOUTSECS], AX

	;; Disable the cursor
	mov CX, 2000h
	mov AH, 01h
	int 10h

	.showChoices:
	call display

	.waitKey:

	;; Idle the processor until something happens
	sti
	hlt

	cmp dword [TIMEOUT_SECONDS], 0
	je .noSecond
	;; Check for timeout
	call readTimer
	cmp word [RTCSECONDS], AX
	je .noSecond
	mov word [RTCSECONDS], AX
	mov AH, 02
	mov BH, VIDEOPAGE
	mov DX, word [TIMEOUTSECPOS]
	int 10h
	push word [TIMEOUTSECS]
	call printNumber
	push word TIMEOUT2
	call print
	add SP, 4
	cmp word [TIMEOUTSECS], 0
	je .bootSelected	; Timer reached zero
	sub word [TIMEOUTSECS], 1
	.noSecond:

	;; Check for a key press
	mov AX, 0100h
	int 16h
	jz .waitKey

	;; Cancel the countdown timer
	mov dword [TIMEOUT_SECONDS], 0

	;; Read the key press
	mov AX, 0000h
	int 16h

	;; If up or down cursor, change selected one appropriately

	cmp AH, 48h		; Up
	jne .notUp
	cmp word [SELECTEDTARGET], 0
	jle .notUp
	sub word [SELECTEDTARGET], 1

	.notUp:
	cmp AH, 50h		; Down
	jne .notDown
	mov CX, word [SELECTEDTARGET]
	inc CX
	cmp CX, word [NUM_TARGETS]
	jae .notDown
	add word [SELECTEDTARGET], 1
	.notDown:

	cmp AH, 1Ch		; Enter
	je .bootSelected

	jmp .showChoices

	.bootSelected:
	;; The user has chosen.

	;; Get the pointer to the correct partition table entry
	mov CX, 4
	mov SI, word [PART_TABLE]
	.findEntry:
	push DS
	push 0
	pop DS
	mov EAX, dword [DS:SI + 8]
	pop DS
	cmp EAX, dword [STARTSECTOR]
	je .foundEntry
	add SI, 16
	loop .findEntry

	;; Eeek!  Didn't find the entry
	push word NOSUCHENTRY
	call print
	add SP, 2
	.stop2: jmp .stop2

	.foundEntry:
	mov word [PART_TABLE], SI

	push word BOOTING
	call print
	add SP, 2

	;; Re-enable the cursor
	mov CX, 0607h
	mov AH, 01h
	int 10h

	;; Get the selected entry start sector

	;; Load the target bootsector
	push word 1				; Read 1 sector
	push word 7C00h				; Offset where we'll move it
	push word 0				; Segment where we'll move it
	push dword [STARTSECTOR]
	call read
	add SP, 10

	popa

	mov SI, word [PART_TABLE]

	;; Go
	jmp 0000:7C00h


display:
	;; Refresh the screen contents

	pusha

	call clearScreen

	push word LOADMSG
	call print
	add SP, 2

	push word CHOOSE
	call print
	add SP, 2

	call indent
	push word STRAIGHTLINE
	call print
	add SP, 2

	;; Loop through the targets and print them
	mov SI, TARGETS
	xor CX, CX

	.targetLoop:
	cmp CX, word [SELECTEDTARGET]
	jne .noReverse

	;; Reverse foreground and background colors
	mov byte [FGCOLOR], BACKGROUNDCOLOR
	mov byte [BGCOLOR], FOREGROUNDCOLOR

	;; Grab the start sector from it
	mov EAX, dword [SI + bootTarget.startSector]
	mov dword [STARTSECTOR], EAX

	.noReverse:
	call indent
	push SI
	call print
	add SP, 2

	push word NEWLINE
	call print
	add SP, 2

	add SI, bootTarget_size

	;; Normal colors
	mov byte [FGCOLOR], FOREGROUNDCOLOR
	mov byte [BGCOLOR], BACKGROUNDCOLOR

	inc CX
	cmp CX, word [NUM_TARGETS]
	jb .targetLoop

	call indent
	push word STRAIGHTLINE
	call print
	add SP, 2

	cmp dword [TIMEOUT_SECONDS], 0
	je .noTimeout
	;; Print a message about the timeout
	push word TIMEOUT1
	call print
	;; Get the cursor position
	mov AH, 03
	mov BH, VIDEOPAGE
	int 10h
	mov word [TIMEOUTSECPOS], DX
	push word [TIMEOUTSECS]
	call printNumber
	push word TIMEOUT2
	call print
	add SP, 6
	.noTimeout:

	popa
	ret


setTextMode:
	;; This function will set the text display mode to a known state.

	pusha

	;; Set the active display page to VIDEOPAGE
	mov AH, 05h
	mov AL, VIDEOPAGE
	int 10h

	;; Set the overscan color.  That's the color that forms the
	;; default backdrop, and sometimes appears as a border around
	;; the printable screen
	mov AX, 0B00h
	mov BH, 0
	mov BL, BACKGROUNDCOLOR
	int 10h

	;; We will try to change text modes to a more attractive 80x50
	;; mode.  This takes a couple of steps

	;; Change the number of scan lines in the text display
	mov AX, 1202h		; 400 scan lines
	mov BX, 0030h		; Change scan lines command
	int 10h

	;; Set the text video mode to make the change take effect
	mov AX, 0003h
	int 10h

	;; The following call messes with ES, so save it
	push ES

	;; Change the VGA font to match a 80x50 configuration
	mov AX, 1112h		; 8x8 character set
	mov BL, 0
	int 10h

	;; Restore ES
	pop ES

	popa
	ret


clearScreen:
	pusha

	;; Blank the screen
	mov AX, 0700h
	mov BH, byte [BGCOLOR]
	and BH, 00000111b
	shl BH, 4
	or BH, byte [FGCOLOR]
	mov CX, 0000h
	mov DH, ROWS
	mov DL, COLUMNS
	int 10h

	mov AX, 0200h
	mov BH, VIDEOPAGE
	mov DH, 0
	mov DL, 0
	int 10h

	popa
	ret


readTimer:
	;; Save a word on the stack for our return value
	push word 0

	;; Save regs
	pusha

	;; Save the stack pointer
	mov BP, SP

	;; Wait for the RTC controller to be ready
	.waitRtc:
	mov AX, 000Ah
	out 70h, AL
	in AL, 71h
	bt AX, 7
	jc .waitRtc

	;; Read the seconds register, disabling NMI
	mov AX, 0080h
	out 70h, AL
	in AL, 71h
	mov word [SS:(BP + 16)], AX
	;; Re-enable NMI
	mov AL, 0
	out 70h, AL

	popa
	xor EAX, EAX
	pop AX			; Result
	ret


indent:
	pusha

	;; Get the cursor position
	mov AH, 03
	mov BH, VIDEOPAGE
	int 10h

	;; Adjust column
	add DL, 1

	mov AH, 02h
	mov BH, VIDEOPAGE
	int 10h

	popa
	ret


print:
	pusha

	;; Save SP
	mov BP, SP

	;; Get cursor position in DX
	mov AH, 03h
	mov BH, VIDEOPAGE
	int 10h
	push DX

	;; Count the number of chars we're writing
	xor CX, CX
	mov SI, word [SS:(BP + 18)]
	.characterLoop:
	cmp byte [SI], 0
	je .doneCharLoop
	inc CX
	inc SI
	jmp .characterLoop
	.doneCharLoop:
	push CX

	;; Call the BIOS' 'write string' interrupt.
	mov AX, 1301h
	mov BH, VIDEOPAGE
	mov BL, byte [BGCOLOR]
	shl BL, 4
	or BL, byte [FGCOLOR]
	pop CX
	pop DX
	mov BP, word [SS:(BP + 18)]
	int 10h

	popa
	ret


printNumber:
	pusha

	;; Save SP
	mov BP, SP

	xor EAX, EAX
	mov AX, word [SS:(BP + 18)]

	mov dword [REMAINDER], EAX
	mov ECX, 1000000000
	mov byte [LEADZERO], 01h

	.nextChar:

	xor EDX, EDX
	mov EAX, dword [REMAINDER]
	div ECX
	mov dword [REMAINDER], EDX

	cmp EAX, 0
	jne .notZero
	cmp ECX, 1
	je .notZero ;; If the value is 0 we still want it to print

	cmp byte [LEADZERO], 01h
	je .afterPrint

	.notZero:
	mov byte [LEADZERO], 00h
	mov SI, TALLY
	shl AX, 1
	add SI, AX

	;; Print a digit on the screen
	push word SI
	call print
	add SP, 2

	.afterPrint:
	;; Decrease ECX
	xor EDX, EDX
	mov EAX, ECX
	mov ECX, 10
	div ECX
	mov ECX, EAX

	cmp ECX, 0
	ja .nextChar

	popa
	ret


IOError:
	;; If we got a fatal IO error or something, we just have to print
	;; an error and try to let the BIOS select another device to boot.
	;; This isn't very helpful, but unfortunately this piece of code
	;; is too small to do very much else.

	push word IOERR
	call print
	add SP, 2

	int 18h

	;; Stop, just in case
	.fatalErrorLoop:
	jmp .fatalErrorLoop


	%include "bootsect-read.s"


;;
;; The data segment
;;

	SEGMENT .data
	ALIGN 4

LOADMSG			db 0Dh, 0Ah, ' Visopsys Boot Menu' , 0Dh, 0Ah
				db ' Copyright (C) 1998-2016 J. Andrew McLaughlin', 0Dh, 0Ah
				db 0Dh, 0Ah, 0
NOTARGETS		db ' No targets to boot!  Did you run the installer program?'
				db 0Dh, 0Ah, 0
TIMEOUT1		db 0Dh, 0Ah, ' Default selection will boot in ', 0
TIMEOUT2		db ' seconds.   ', 0Dh, 0Ah, 0
CHOOSE			db 0Dh, 0Ah, ' Please choose the partition to boot:', 0Dh, 0Ah
				db 0Dh, 0Ah, 0
NOSUCHENTRY		db ' No such partition table entry to boot!', 0Dh, 0Ah, 0
BOOTING			db 0Dh, 0Ah, ' Booting...', 0Dh, 0Ah, 0
NEWLINE			db 0Dh, 0Ah, 0
IOERR			db ' I/O Error reading boot sector', 0Dh, 0Ah, 0
STRAIGHTLINE	times (STRINGLENGTH - 1) db 196
				db 0Dh, 0Ah, 0
FGCOLOR			db FOREGROUNDCOLOR
BGCOLOR			db BACKGROUNDCOLOR
SELECTEDTARGET	dw 0
STARTSECTOR		dd 0
PART_TABLE		dw 0

;; For the timer
RTCSECONDS		dw 0
TIMEOUTSECPOS	dw 0
TIMEOUTSECS		dw 0

;; For printing numbers
LEADZERO		db 0
REMAINDER		dd 0
TALLY			db '0', 0, '1', 0, '2', 0, '3', 0, '4', 0, '5', 0, '6', 0,
				db '7', 0, '8', 0, '9', 0

;; For loading
DISK			db 0
NUMHEADS		dw 0
NUMCYLS			dw 0
NUMSECTS		db 0
HEAD			db 0
SECTOR			db 0
CYLINDER		dw 0

;; Disk cmd packet for extended int13 disk ops
DISKPACKET	times 16 db 0

