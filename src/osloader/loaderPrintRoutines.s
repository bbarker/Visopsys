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
;;  loaderPrintRoutines.s
;;

	EXTERN CURRENTGMODE

	GLOBAL loaderGetCursorAddress
	GLOBAL loaderSetCursorAddress
	GLOBAL loaderPrint
	GLOBAL loaderPrintNewline
	GLOBAL loaderPrintNumber
	GLOBAL loaderSaveTextDisplay
	GLOBAL loaderRestoreTextDisplay

	SEGMENT .text
	BITS 16
	ALIGN 4

	%include "loader.h"


loaderGetCursorAddress:
	;; This routine gathers the current cursor address
	;; and returns it in AX

	push DX

	;; If we are currently in graphics mode, use the saved cursor
	;; address instead of the on the BIOS tells us
	cmp word [CURRENTGMODE], 0
	je .textMode

	mov AX, word [CURSORPOS]
	jmp .done

	.textMode:
	mov DX, 03D4h	;; The port for the selection register
	mov AL, 0Eh		;; MSB of cursor address
	out DX, AL		;; Select the correct register
	mov DX, 03D5h	;; The data port
	in AL, DX

	shl AX, 8

	mov DX, 03D4h	;; The port for the selection register
	mov AL, 0Fh		;; LSB of cursor address
	out DX, AL		;; Select the correct register
	mov DX, 03D5h	;; The data port
	in AL, DX

	.done:
	pop DX
	ret


loaderSetCursorAddress:
	;; This routine takes a new cursor address offset in AX
	pusha

	;; If we are currently in graphics mode, set the saved cursor
	;; address instead of the one the BIOS tells us
	cmp word [CURRENTGMODE], 0
	je .textMode

	mov word [CURSORPOS], AX
	jmp .done

	.textMode:
	mov CX, AX

	mov DX, 03D4h	;; The port for the selection register
	mov AL, 0Fh		;; LSB of cursor address
	out DX, AL		;; Select the correct register
	mov DX, 03D5h	;; The data port
	mov AL, CL
	out DX, AL

	mov DX, 03D4h	;; The port for the selection register
	mov AL, 0Eh		;; MSB of cursor address
	out DX, AL		;; Select the correct register
	mov DX, 03D5h	;; The data port
	mov AL, CH
	out DX, AL

	.done:
	popa
	ret


loaderPrint:
	;; This routine takes a pointer to the chars to print in DS:SI,
	;; and the color in DL.

	pusha

	;; OR the desired foreground color with the desired
	;; background color
	mov DH, BACKGROUNDCOLOR
	and DH, 00000111b
	shl DH, 4
	or DL, DH

	;; push the data we need to keep onto the stack

	push word 0
	push DX

	;; Get the current cursor position
	call loaderGetCursorAddress

	;; Now AX has the offset we write to
	push AX		;; Save it

	mov BP, SP	;; BP will keep a copy of the new stack pointer

	;; Make it a screen address (multiply by 2)
	shl AX, 1

	;; Now we have a destination offset which should go into DI
	mov DI, AX

	push ES
	mov EAX, (SCREENSTART / 16)
	mov ES, AX

	;; Make sure DS points to the loader's segment
	push DS
	mov EAX, (LDRCODESEGMENTLOCATION / 16)
	mov DS, AX

	;; Get the color (DX) from the stack into AL (AX)
	mov AX, word [SS:(BP + 2)]
	xor AH, AH

	.characterLoop:
	movsb
	stosb
	add word [SS:(BP + 4)], 1
	cmp byte [DS:SI], 0
	jne .characterLoop

	;; Restore DS and ES
	pop DS
	pop ES

	;; Lastly, we want to make the cursor position in the BIOS
	;; data area reflect the correct values
	mov AX, word [SS:(BP + 4)]	;; Number of chars written
	;; Add the current cursor offset (get it from the stack)
	add AX, word [SS:BP]

	;; Change to the new values
	call loaderSetCursorAddress

	;; Clean up the stack
	add SP, 6
	popa
	ret


loaderPrintNewline:
	pusha

	;; Get the current cursor position
	call loaderGetCursorAddress

	;; Determine whether the cursor is on the last line of the
	;; screen; If it is, we have to scroll everything up
	cmp AX, (COLUMNS * (ROWS - 1))
	jnae .noScroll

	call scrollLine

	;; Get the current cursor position
	call loaderGetCursorAddress

	.noScroll:
	;; Now we must round the offset up to the next multiple of COLUMNS
	xor BX, BX
	.addMore:
	add BX, COLUMNS
	cmp BX, AX
	jna .addMore

	mov AX, BX

	;; Change to the new values
	call loaderSetCursorAddress

	popa
	ret


loaderPrintNumber:
	pusha

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

	push ECX

	;; Print a digit on the screen

	mov DL, FOREGROUNDCOLOR
	or DL, (BACKGROUNDCOLOR * 16)

	mov CX, 1
	call loaderPrint

	pop ECX

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


scrollLine:
	;; This routine will scroll the contents of the text console
	;; screen
	pusha

	;; Disable interrupts while we do this
	cli

	;; Save DS and ES, since we will modify them
	push DS
	push ES

	mov AX, (SCREENSTART / 16)
	mov DS, AX
	mov ES, AX

	;; Move up all the screen contents
	mov SI, (COLUMNS * 2)
	mov DI, 0
	mov CX, (((COLUMNS * ROWS) - COLUMNS) / 2)
	cld
	rep movsd

	;; Erase the bottom line of the screen
	mov DI, (COLUMNS * (ROWS - 1) * 2)
	mov CX, COLUMNS

	xor AL, AL		; 'null' character;
	mov AH, BACKGROUNDCOLOR
	and AH, 00000111b
	shl AH, 4
	or AH, FOREGROUNDCOLOR
	cld
	rep stosw

	;; Reenable interrupts
	sti

	;; Make the cursor maintain its position in relation to the
	;; most recent text
	call loaderGetCursorAddress
	sub AX, COLUMNS
	call loaderSetCursorAddress

	;; Restore DS and ES
	pop ES
	pop DS

	popa
	ret


;;
;; The data segment
;;

	SEGMENT .data
	ALIGN 4

REMAINDER	dd 0	;; For number printing
CURSORPOS	dw 0
TALLY		db '0', 0, '1', 0, '2', 0, '3', 0, '4', 0, '5', 0, '6', 0,
			db '7', 0, '8', 0, '9', 0
LEADZERO	db 0	;; In number generation

