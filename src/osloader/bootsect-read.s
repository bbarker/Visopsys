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
;;  bootsect-read.s
;;

;; This code is a common disk reading routine for boot sector code.  It's just
;; meant to be %included, not compiled separately.


read:
	;; Proto: int read(dword logical, word seg, word offset, word count);

	pusha

	;; Save the stack pointer
	mov BP, SP

	;; Determine whether int13 extensions are available
	cmp byte [DISK], 80h
	jb .noExtended

	;; We have a nice extended read function which will allow us to
	;; just use the logical sector number for the read

	mov word [DISKPACKET], 0010h		; Packet size
	mov AX, word [SS:(BP + 26)]
	mov word [DISKPACKET + 2], AX		; Sector count
	mov AX, word [SS:(BP + 24)]
	mov word [DISKPACKET + 4], AX		; Offset
	mov AX, word [SS:(BP + 22)]
	mov word [DISKPACKET + 6], AX		; Segment
	mov EAX, dword [SS:(BP + 18)]
	mov dword [DISKPACKET + 8], EAX		; > Logical sector
	mov dword [DISKPACKET + 12], 0		; >
	mov AX, 4200h
	mov DL, byte [DISK]
	mov SI, DISKPACKET
	int 13h
	jc IOError

	;; Done
	jmp .done

	.noExtended:
	;; No extended functionality.  Read the sectors one at a time.

	.readSector:
	;; Calculate the CHS.  First the sector
	mov EAX, dword [SS:(BP + 18)]	; Logical sector
	xor EBX, EBX
	xor EDX, EDX
	mov BX, word [NUMSECTS]
	div EBX
	mov byte [SECTOR], DL			; The remainder
	add byte [SECTOR], 1			; Sectors start at 1

	;; Now the head and track
	xor EDX, EDX					; Don't need the remainder anymore
	mov BX, word [NUMHEADS]
	div EBX
	mov byte [HEAD], DL				; The remainder
	mov word [CYLINDER], AX

	mov AX, 0201h					; Number to read, subfunction 2
	mov CX, word [CYLINDER]			; >
	rol CX, 8						; > Cylinder
	shl CL, 6						; >
	or CL, byte [SECTOR]			; Sector
	mov DH, byte [HEAD]				; Head
	mov DL, byte [DISK]				; Disk
	mov BX, word [SS:(BP + 24)]		; Offset
	push ES							; Save ES
	mov ES, word [SS:(BP + 22)]		; Use user-supplied segment
	int 13h
	pop ES							; Restore ES
	jc IOError

	add word [SS:(BP + 24)], 512	; Increment the memory pointer
	inc dword [SS:(BP + 18)]		; Increment the sector
	dec word [SS:(BP + 26)]			; Decrement the count
	jnz .readSector					; Check whether we're finished

	.done:
	popa
	ret

