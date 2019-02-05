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
;;  loaderDiskError.s
;;

	GLOBAL loaderDiskError

	EXTERN loaderPrint
	EXTERN loaderPrintNewline

	SEGMENT .text
	BITS 16
	ALIGN 4

	%include "loader.h"


loaderDiskError:
	;; This routine is for outputting disk-specific error
	;; messages

	;; Save regs
	pusha

	;; Print a message that we have an error

	call loaderPrintNewline

	;; Use error color
	mov DL, BADCOLOR
	mov SI, IOERR
	call loaderPrint

	;; Get the disk error status from the controller
	mov AH, 01h
	int 13h

	;; The error code returned should be in AL

	cmp AL, 01h
	jne .error02

	mov SI, DSM1
	jmp .finished

	.error02:
	cmp AL, 02h
	jne .error03

	mov SI, DSM2
	jmp .finished

	.error03:
	cmp AL, 03h
	jne .error04

	mov SI, DSM3
	jmp .finished

	.error04:
	cmp AL, 04h
	jne .error05

	mov SI, DSM4
	jmp .finished

	.error05:
	cmp AL, 05h
	jne .error06

	mov SI, DSM5
	jmp .finished

	.error06:
	cmp AL, 06h
	jne .error07

	mov SI, DSM6
	jmp .finished

	.error07:
	cmp AL, 07h
	jne .error08

	mov SI, DSM7
	jmp .finished

	.error08:
	cmp AL, 08h
	jne .error09

	mov SI, DSM8
	jmp .finished

	.error09:
	cmp AL, 09h
	jne .error0A

	mov SI, DSM9
	jmp .finished

	.error0A:
	cmp AL, 0Ah
	jne .error0B

	mov SI, DSM10
	jmp .finished

	.error0B:
	cmp AL, 0Bh
	jne .error0C

	mov SI, DSM11
	jmp .finished

	.error0C:
	cmp AL, 0Ch
	jne .error0D

	mov SI, DSM12
	jmp .finished

	.error0D:
	cmp AL, 0Dh
	jne .error0E

	mov SI, DSM13
	jmp .finished

	.error0E:
	cmp AL, 0Eh
	jne .error0F

	mov SI, DSM14
	jmp .finished

	.error0F:
	cmp AL, 0Fh
	jne .error10

	mov SI, DSM15
	jmp .finished

	.error10:
	cmp AL, 10h
	jne .error11

	mov SI, DSM16
	jmp .finished

	.error11:
	cmp AL, 11h
	jne .error20

	mov SI, DSM17
	jmp .finished

	.error20:
	cmp AL, 20h
	jne .error40

	mov SI, DSM18
	jmp .finished

	.error40:
	cmp AL, 40h
	jne .error80

	mov SI, DSM19
	jmp .finished

	.error80:
	cmp AL, 80h
	jne .errorAA

	mov SI, DSM20
	jmp .finished

	.errorAA:
	cmp AL, 0AAh
	jne .errorBB

	mov SI, DSM21
	jmp .finished

	.errorBB:
	cmp AL, 0BBh
	jne .errorCC

	mov SI, DSM22
	jmp .finished

	.errorCC:
	cmp AL, 0CCh
	jne .errorE0

	mov SI, DSM23
	jmp .finished

	.errorE0:
	cmp AL, 0E0h
	jne .errorFF

	mov SI, DSM24
	jmp .finished

	cmp AL, 0FFh
	jne .errorunknown

	.errorFF:
	mov SI, DSM25
	jmp .finished

	.errorunknown:
	;; Otherwise, it's unknown
	mov SI, DSMHUH

	.finished:
	call loaderPrint
	call loaderPrintNewline
	mov SI, IOERR2
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


;; General message for disk errors
IOERR		db 'Disk IO error:  ', 0
IOERR2		db 'The boot device or media may require service.', 0

;;
;; Disk error status messages:
;;

DSM1		db 'Bad command passed to driver.', 0
DSM2		db 'Address mark not found or bad sector.', 0
DSM3		db 'Diskette write protect error.', 0
DSM4		db 'Sector not found.', 0
DSM5		db 'Disk reset failed.', 0
DSM6		db 'Diskette changed or removed.', 0
DSM7		db 'Bad fixed disk parameter table.', 0
DSM8		db 'DMA overrun.', 0
DSM9		db 'DMA access accross 64K boundary.', 0
DSM10		db 'Bad fixed disk sector flag.', 0
DSM11		db 'Bad fixed disk cylinder.', 0
DSM12		db 'Unsupported track or invalid media.', 0
DSM13		db 'Invalid number of sectors on fixed disk format.', 0
DSM14		db 'Fixed disk controlled data access mark detected.', 0
DSM15		db 'Fixed disk DMA arbitration level out of range.', 0
DSM16		db 'ECC/CRC error on disk read.', 0
DSM17		db 'Recoverable fixed disk data error.', 0
DSM18		db 'Controller error.', 0
DSM19		db 'Seek failure.', 0
DSM20		db 'Time out, drive not ready.', 0
DSM21		db 'Fixed disk drive not ready.', 0
DSM22		db 'Fixed disk undefined error.', 0
DSM23		db 'Fixed disk write fault on selected drive.', 0
DSM24		db 'Fixed disk status error/Error reg = 0.', 0
DSM25		db 'Sense operation failed.', 0
DSMHUH		db 'Unknown disk I/O error.', 0

