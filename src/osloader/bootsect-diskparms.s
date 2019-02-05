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
;;  bootsect-diskparms.s
;;

;; This code is a common disk parameter gathering for boot sector code.  It's
;; just meant to be %included, not compiled separately.


	;; Get the drive parameters

	;; ES:DI = 0000h:0000h to guard against BIOS bugs
	push ES
	xor DI, DI
	mov AX, 0800h
	mov DL, byte [DISK]
	int 13h
	jc near IOError
	pop ES

	;; Save info
	shr DX, 8				; Number of heads, 0-based
	inc DX
	mov word [NUMHEADS], DX
	and CX, 003Fh			; Sectors per track/cylinder
	mov word [NUMSECTS], CX

