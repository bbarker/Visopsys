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
;;  bootsect-fat.s
;;

;; This code is a boot sector for generic FAT filesystems


	ORG 7C00h
	SEGMENT .text

	;; Some handy definitions we need
	%include "loader.h"

;; Memory for storing things...

;; Loader starting sector
%define LOADERSECTOR	(ENDSECTOR + 2)
%define LOADERSECTOR_SZ	DWORD

;; Device info
%define DISK			(LOADERSECTOR + LOADERSECTOR_SZ)
%define DISK_SZ			WORD
%define NUMHEADS		(DISK + DISK_SZ)
%define NUMHEADS_SZ		WORD
%define NUMSECTS		(NUMHEADS + NUMHEADS_SZ)
%define NUMSECTS_SZ		WORD

;; For int13 disk ops
%define HEAD			(NUMSECTS + NUMSECTS_SZ)
%define HEAD_SZ			WORD
%define SECTOR			(HEAD + HEAD_SZ)
%define SECTOR_SZ		WORD
%define CYLINDER		(SECTOR + SECTOR_SZ)
%define CYLINDER_SZ		WORD

;; Disk cmd packet for ext. int13
%define DISKPACKET		(CYLINDER + CYLINDER_SZ)
%define DISKPACKET_SZ	(BYTE * 16)

;; Space for the partition table entry
%define PARTENTRY		(DISKPACKET + DISKPACKET_SZ)
%define PARTENTRY_SZ	(BYTE * 16)

main:
	jmp short bootCode	; 00 - 01 Jump instruction
	nop					; 02 - 02 No op

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

	mov dword [LOADERSECTOR], 0

	;; If we are not booting from a floppy, the MBR code should have put
	;; a pointer to the MBR record for this partition in SI.  Calculate
	;; the partition starting sector and add it to the LOADERSECTOR value
	cmp byte [DISK], 80h
	jb .noOffset
	mov EAX, dword [SI + 8]
	add dword [LOADERSECTOR], EAX

	;; Make a copy of the partition entry.
	mov DI, PARTENTRY
	mov CX, 16
	cld
	rep movsb

	.noOffset:

	;; Get disk parameters
	%include "bootsect-diskparms.s"

	;; Calculate the starting sector number of the loader

	;; Sectors preceding the loader's first cluster (maybe root dir, for
	;; example, in FAT32)
	mov EAX, dword [LOADERSTARTCLUSTER]
	sub EAX, 2								; 2 unused cluster numbers
	xor EBX, EBX
	mov BL, byte [SecPerClust]
	mul EBX
	add dword [LOADERSECTOR], EAX

	;; Sectors for FATs
%ifdef FAT32
	mov EAX, dword [FATSize]
%else
	xor EAX, EAX
	mov AX, word [FATSecs]
%endif
	mov BL, byte [FATs]
	mul EBX

	;; Reserved sectors
	mov BX, word [ResSectors]
	add EAX, EBX
	add dword [LOADERSECTOR], EAX

%ifndef FAT32
	;; Directory sectors
	xor EAX, EAX
	mov AX, word [RootDirEnts]
	shl EAX, 5								; 32 bytes per entry
	mov BX, word [BytesPerSect]
	div EBX
	add dword [LOADERSECTOR], EAX
%endif

	;; Load the loader
	push word [LOADERNUMSECTORS]
	push word 0								; Offset where we'll move it
	push word (LDRCODESEGMENTLOCATION / 16)	; Segment where we'll move it
	push dword [LOADERSECTOR]
	call read
	add SP, 10

	mov DL, byte [DISK]
	mov EBX, dword [BOOTSECTSIG]
	mov SI, PARTENTRY

	jmp (LDRCODESEGMENTLOCATION / 16):0

	%include "bootsect-print.s"
	%include "bootsect-error.s"
	%include "bootsect-read.s"

IOERR				db 'Error.  Press any key ', 0

times (498-($-$$))	db 0

;; This is the boot sector signature, to help the kernel find the root
;; filesystem.
BOOTSECTSIG			dd 0

;; The installation process will record the starting cluster of the loader,
;; and number of sectors to read, here.
LOADERSTARTCLUSTER	dd 0
LOADERNUMSECTORS	dd 0

;; This puts the value AA55h in the last two bytes of the boot
;; sector.  The BIOS uses this to determine whether this sector was
;; meant to be booted from (and also helps prevent us from making the
;; boot sector code larger than 512 bytes)
ENDSECTOR			dw 0AA55h

