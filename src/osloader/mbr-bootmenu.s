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
;;  mbr-bootmenu.s
;;

;; This code is the boot menu MBR code, which loads the boot menu program
;; from the rest of the track and runs it.


	ORG 7C00h
	SEGMENT .text
	BITS 16

	;; Some handy definitions we need
	%include "loader.h"

;; Heap memory for storing things...

;; The place where we relocate our code
%define NEWCODELOCATION	0600h
%define CODE_SIZE		512

;; Device info
%define DISK			(NEWCODELOCATION + CODE_SIZE)
%define DISK_SZ			BYTE
%define NUMHEADS		(DISK + DISK_SZ)
%define NUMHEADS_SZ		WORD
%define NUMSECTS		(NUMHEADS + NUMHEADS_SZ)
%define NUMSECTS_SZ		WORD

;; For int13 disk ops
%define HEAD			(NUMSECTS + NUMSECTS_SZ)
%define HEAD_SZ			BYTE
%define SECTOR			(HEAD + HEAD_SZ)
%define SECTOR_SZ		BYTE
%define CYLINDER		(SECTOR + SECTOR_SZ)
%define CYLINDER_SZ		WORD

;; Disk cmd packet for extended int13 disk ops
%define DISKPACKET		(CYLINDER + CYLINDER_SZ)
%define DISKPACKET_SZ	(BYTE * 16)

%define	IOERR			(NEWCODELOCATION + (DATA_IOERR - main))
%define	PART_TABLE		(NEWCODELOCATION + (DATA_PART_TABLE - main))


main:
	;; A jump is expected at the start of a boot sector, sometimes.
	jmp short .bootCode		; 00 - 01 Jump instruction
	nop						; 02 - 02 No op

	.bootCode:
	cli

	;; Adjust the data segment registers
	xor AX, AX
	mov DS, AX
	mov ES, AX

	;; Set the stack to be at the top of the code
	mov SS, AX
	mov SP, NEWCODELOCATION

	;; Apparently, some broken BIOSes jump to 07C0h:0000h instead of
	;; 0000h:7C00h.
	jmp (0):.adjCsIp
	.adjCsIp:

	sti

	pusha

	;; Relocate our code, so we can copy the chosen boot sector over
	;; top of ourselves
	mov SI, main
	mov DI, NEWCODELOCATION
	mov CX, CODE_SIZE
	cld
	rep movsb

	;; Jump to it
	jmp (NEWCODELOCATION + (jmpTarget - main))

jmpTarget:

	;; The BIOS will pass the boot device number to us in the DL
	;; register.
	mov byte [DISK], DL

	;; Get disk parameters
	%include "bootsect-diskparms.s"

	;; Load the boot menu at logical sector 1
	xor EAX, EAX
	mov AX, word [NUMSECTS]
	sub AX, 1
	push word AX							; Read (NUMSECTS - 1) sectors
	push word 0								; Offset where we'll move it
	push word (LDRCODESEGMENTLOCATION / 16)	; Segment where we'll move it
	push dword 1							; Start at logical sector 1
	call read
	add SP, 10

	popa

	;; Move the pointer to the start of the partiton table into SI
	mov SI, PART_TABLE

	;; Move the boot disk device number into DL
	mov DL, byte [DISK]

	;; Go
	jmp (LDRCODESEGMENTLOCATION / 16):0


	%include "bootsect-print.s"
	%include "bootsect-error.s"
	%include "bootsect-read.s"


;; Static data follows.  We don't refer to it by any of these symbol names;
;; after relocation these are not so meaningful

;; Messages
DATA_IOERR		db 'I/O Error reading boot sector', 0Dh, 0Ah, 0

;; Move to the end of the sector
times (440-($-$$)) db 0

;; Disk signature
DATA_DISKSIG	dd 0

;; NULLs
DATA_NULLS		dw 0

;; Here's where the partition table goes
DATA_PART_TABLE:
	times 16	db 0
	times 16	db 0
	times 16	db 0
	times 16	db 0

;; This puts the value AA55h in the last two bytes of the boot
;; sector.  The BIOS uses this to determine whether this sector was
;; meant to be booted from (and also helps prevent us from making the
;; boot sector code larger than 512 bytes)
DATA_BOOTSIG	dw 0AA55h

