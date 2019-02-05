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
;;  loaderA20.s
;;

	GLOBAL loaderEnableA20

	EXTERN loaderPrint
	EXTERN loaderPrintNewline
	EXTERN FATALERROR

	SEGMENT .text
	BITS 16
	ALIGN 4

	%include "loader.h"


biosQuery:
	;; Ask the BIOS which A20 methods are supported

	push word -1	; return code
	pusha			; save regs
	mov BP, SP		; save stack ptr

	;; SYSTEM - later PS/2s - QUERY A20 GATE SUPPORT
	mov AX, 2403h
	int 15h
	jc .done

	mov word [SS:(BP + 16)], BX

	.done:
	popa
	pop AX
	ret


biosMethod:
	;; This method attempts the easiest thing, which is to try to let the
	;; BIOS set A20 for us.

	push word -1	; return code
	pusha			; save regs
	mov BP, SP		; save stack ptr

	;; Check

	;; SYSTEM - later PS/2s - GET A20 GATE STATUS
	mov AX, 2402h
	int 15h
	jc .done

	;; Already set?
	cmp AL, 01h
	jne .set

	;; Already set
	mov word [SS:(BP + 16)], 0
	jmp .done

	.set:
	;; Do the 'enable' call

	;; SYSTEM - later PS/2s - ENABLE A20 GATE
	mov AX, 2401h
	int 15h
	jc .done

	;; Check

	;; SYSTEM - later PS/2s - GET A20 GATE STATUS
	mov AX, 2402h
	int 15h
	jc .done

	;; Set?
	cmp AL, 01h
	jne .done

	;; The BIOS did it for us.
	mov word [SS:(BP + 16)], 0

	.done:
	popa
	pop AX
	ret


port92Method:
	;; This method attempts the second easiest thing, which is to try
	;; writing a bit to port 92h

	push word -1	; return code
	pusha			; save regs
	mov BP, SP		; save stack ptr

	;; Read port 92h
	xor AX, AX
	in AL, 92h

	;; Already set?
	bt AX, 1
	jnc .continue

	;; A20 is on
	mov word [SS:(BP + 16)], 0
	jmp .done

	.continue:
	;; Try to write it
	or AL, 2
	out 92h, AL

	;; Re-read and check
	in AL, 92h
	bt AX, 1
	jnc .done

	;; A20 is on
	mov word [SS:(BP + 16)], 0

	.done:
	popa
	pop AX
	ret


keyboardCommandWait:
	;; Wait for the keyboard controller to be ready for a command
	xor AX, AX
	in AL, 64h
	bt AX, 1
	jc keyboardCommandWait
	ret


keyboardDataWait:
	;; Wait for the controller to be ready with a byte of data
	xor AX, AX
	in AL, 64h
	bt AX, 0
	jnc keyboardDataWait
	ret


delay:
	;; Delay
	jcxz $+2
	jcxz $+2
	ret


keyboardRead60:
	;; Wait for the controller to be ready for a command
	call keyboardCommandWait

	;; Tell the controller we want to read the current status.
	;; Send the command D0h: read output port.
	mov AL, 0D0h
	out 64h, AL

	;; Delay
	call delay

	;; Wait for the controller to be ready with a byte of data
	call keyboardDataWait

	;; Read the current port status from port 60h
	xor AX, AX
	in AL, 60h
	ret


keyboardWrite60:
	;; Save AX on the stack for the moment
	push AX

	;; Wait for the controller to be ready for a command
	call keyboardCommandWait

	;; Tell the controller we want to write the status byte
	mov AL, 0D1h
	out 64h, AL

	;; Delay
	call delay

	;; Wait for the controller to be ready for a command
	call keyboardCommandWait

	;; Write the new value to port 60h.  Remember we saved the old value
	;; on the stack
	pop AX
	out 60h, AL
	ret


keyboardMethod:
	push word -1	; return code
	pusha			; save regs
	mov BP, SP		; save stack ptr

	;; Make sure interrupts are disabled
	cli

	;; Read the current port 60h status
	call keyboardRead60

	;; Turn on the A20 enable bit and write it.
	or AL, 2
	call keyboardWrite60

	;; Read back the A20 status to ensure it was enabled.
	call keyboardRead60

	;; Check the result
	bt AX, 1
	jnc .done

	;; A20 is on
	mov word [SS:(BP + 16)], 0

	.done:
	sti
	popa
	pop AX
	ret


altKeyboardMethod:
	;; This is alternate way to set A20 using the keyboard (which is
	;; supposedly not supported on many chipsets).

	push word -1	; return code
	pusha			; save regs
	mov BP, SP		; save stack ptr

	;; Make sure interrupts are disabled
	cli

	;; Wait for the controller to be ready for a command
	call keyboardCommandWait

	;; Tell the controller we want to turn on A20
	mov AL, 0DFh
	out 64h, AL

	;; Delay
	call delay

	;; Attempt to read back the A20 status to ensure it was enabled.
	call keyboardRead60

	;; Check the result
	bt AX, 1
	jnc .done

	;; A20 is on
	mov word [SS:(BP + 16)], 0

	.done:
	sti
	popa
	pop AX
	ret


loaderEnableA20:
	;; This routine will attempt to enable the A20 address line using
	;; various methods, if necessary.

	pusha

	;; Supported methods: all available by default
	mov BL, 03h

	;; Try asking the BIOS what methods are supported
	call biosQuery
	cmp AX, 0
	jl .try		; unknown - just try everything

	;; Bitmask of supported methods.  Specifically, don't try something if
	;; the BIOS told us it's unsupported
	and BL, AL

	.try:
	;; Try to let the BIOS do it for us
	call biosMethod
	cmp AX, 0
	jz .done

	;; Didn't work.  Is the 'port 92h' method supported?
	test BL, 02h
	jz .noPort92

	;; Try the 'port 92h' method
	call port92Method
	cmp AX, 0
	jz .done

	.noPort92:
	;; Didn't work, or not supported.  Is the standard keyboard method
	;; supported?
	test BL, 01h
	jz .noKeyboard

	;; Try the standard keyboard method
	call keyboardMethod
	cmp AX, 0
	jz .done

	.noKeyboard:
	;; Didn't work, or not supported.

	;; Try an alternate keyboard method
	call altKeyboardMethod
	cmp AX, 0
	jz .done

	;; OK, we weren't able to set the A20 address line, so we *may* not be
	;; able to access much memory.  We can give a fairly helpful error
	;; message, but don't fail the whole boot because of this.  Some modern
	;; machines don't let us set A20 manually, but work anyway.

	call loaderPrintNewline
	mov DL, BADCOLOR
	mov SI, A20BAD1
	call loaderPrint
	call loaderPrintNewline
	mov SI, A20BAD2
	call loaderPrint
	call loaderPrintNewline

	.done:
	popa
	ret


;;
;; The data segment
;;

	SEGMENT .data
	ALIGN 4

A20BAD1		db 'Could not enable the A20 address line, which could cause '
			db 'serious memory', 0
A20BAD2		db 'problems for the kernel.  This is often associated with '
			db 'keyboard errors.', 0

