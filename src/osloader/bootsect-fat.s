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
;;  bootsect-fat.s
;;

;; This code is a boot sector for generic FAT filesystems

	ORG 7C00h
	SEGMENT .text

	;; Some handy definitions we need
	%include "loader.h"

;; Memory for storing things...

;; Device info
%define DISK			(ENDSECTOR + 2)
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

;; Disk cmd packet for ext. int13
%define DISKPACKET		(CYLINDER + CYLINDER_SZ)
%define DISKPACKET_SZ	(BYTE * 16)

;; Space for the partition table entry
%define PARTENTRY		(DISKPACKET + DISKPACKET_SZ)
%define PARTENTRY_SZ	(BYTE * 16)

main:
	jmp short bootCode				; 00 - 01 Jump instruction
	nop								; 02 - 02 No op

	%include "bootsect-fatBPB.s"

bootCode:

	cli

	;; Adjust the data segment registers
	xor AX, AX
	mov DS, AX
	mov ES, AX

	;; Set the stack to be at the top of the code
	mov SS, AX
	mov SP, main

	;; Apparently, some broken BIOSes jump to 07C0h:0000h instead of
	;; 0000h:7C00h.
	jmp (0):.adjCsIp
	.adjCsIp:

	sti

	;; The MBR or bootloader code will pass the boot device number to us
	;; in the DL register.
	mov byte [DISK], DL

	;; If we are not booting from a floppy, the MBR code should have put
	;; a pointer to the MBR record for this partition in SI.  Calculate
	;; the partition starting sector and add it to the LOADERSTARTSECTOR
	;; value
	cmp byte [DISK], 80h
	jb .noOffset
	mov EAX, dword [SI + 8]
	add dword [LOADERSTARTSECTOR], EAX

	mov DI, PARTENTRY
	mov CX, 16
	cld
	rep movsb

	.noOffset:

	;; Get disk parameters
	%include "bootsect-diskparms.s"

	push word [LOADERNUMSECTORS]
	push word 0						; Offset where we'll move it
	push word (LDRCODESEGMENTLOCATION / 16)	; Segment where we'll move it
	push dword [LOADERSTARTSECTOR]
	call read
	add SP, 10

	mov DL, byte [DISK]
	mov EBX, dword [BOOTSECTSIG]
	mov SI, PARTENTRY

	jmp (LDRCODESEGMENTLOCATION / 16):0


	%include "bootsect-print.s"
	%include "bootsect-error.s"
	%include "bootsect-read.s"


IOERR				db 'Error reading OS loader', 0Dh, 0Ah
					db 'Press any key to continue', 0Dh, 0Ah, 0

times (498-($-$$))	db 0

;; This is the boot sector signature, to help the kernel find the root
;; filesystem.
BOOTSECTSIG			dd 0

;; The installation process will record the logical starting sector of
;; the loader, and number of sectors to read, here.
LOADERSTARTSECTOR	dd 0
LOADERNUMSECTORS	dd 0

;; This puts the value AA55h in the last two bytes of the boot
;; sector.  The BIOS uses this to determine whether this sector was
;; meant to be booted from (and also helps prevent us from making the
;; boot sector code larger than 512 bytes)
ENDSECTOR			dw 0AA55h

