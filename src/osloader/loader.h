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
;;  loader.h
;;

;; Constants

%define BYTE					1
%define WORD					2
%define DWORD					4

;; Memory locations for loading the kernel

%define LDRCODESEGMENTLOCATION	00008000h
%define LDRCODESEGMENTSIZE		00005000h
%define LDRSTCKSEGMENTLOCATION	(LDRCODESEGMENTLOCATION + LDRCODESEGMENTSIZE)
%define LDRSTCKSEGMENTSIZE		00001000h   ; Only needs a small stack
%define LDRSTCKBASE				(LDRSTCKSEGMENTSIZE - 2)
%define LDRPAGINGDATA			(LDRSTCKSEGMENTLOCATION + LDRSTCKSEGMENTSIZE)
%define LDRPAGINGDATASIZE		00003000h
%define LDRDATABUFFER			(LDRPAGINGDATA + LDRPAGINGDATASIZE)
;; Use all the rest up to the start of video memory for the data buffer
%define DATABUFFERSIZE			(000A0000h - LDRDATABUFFER)

%define KERNELVIRTUALADDRESS	0C0000000h	;; 3 GB mark
%define KERNELLOADADDRESS		00100000h	;; 1 MB mark
%define KERNELSTACKSIZE			00010000h	;; 64 KB

;; The length of the progress indicator during kernel load
%define PROGRESSLENGTH 20

;; Some checks, to make sure the data above is correct

%if (KERNELLOADADDRESS % 4096)
	%error "Kernel code must start on 4Kb boundary"
%endif
%if (KERNELSTACKSIZE % 4096)
	%error "Kernel stack size must be a multiple of 4Kb"
%endif
%if (LDRPAGINGDATA % 4096)
	%error "Loader paging data must be a multiple of 4Kb"
%endif

;; Segment descriptor information for the temporary GDT

%define PRIV_CODEINFO1			10011010b
%define PRIV_CODEINFO2			11001111b
%define PRIV_DATAINFO1			10010010b
%define PRIV_DATAINFO2			11001111b
%define PRIV_STCKINFO1			10010010b
%define PRIV_STCKINFO2			11001111b

%define LDRCODEINFO1			10011010b
%define LDRCODEINFO2			01000000b

%define SCREENSTART				0x000B8000

%define VIDEOPAGE				0
%define ROWS					50
%define COLUMNS					80

%define BIOSCOLOR_BLACK			0
%define BIOSCOLOR_BLUE			1
%define BIOSCOLOR_GREEN			2
%define BIOSCOLOR_CYAN			3
%define BIOSCOLOR_RED			4
%define BIOSCOLOR_MAGENTA		5
%define BIOSCOLOR_BROWN			6
%define BIOSCOLOR_LIGHTGREY		7
%define BIOSCOLOR_DARKGREY		8
%define BIOSCOLOR_LIGHTBLUE		9
%define BIOSCOLOR_LIGHTGREEN	10
%define BIOSCOLOR_LIGHTCYAN		11
%define BIOSCOLOR_LIGHTRED		12
%define BIOSCOLOR_LIGHTMAGENTA	13
%define BIOSCOLOR_YELLOW		14
%define BIOSCOLOR_WHITE			15

%define FOREGROUNDCOLOR			BIOSCOLOR_LIGHTGREY
%define BACKGROUNDCOLOR			BIOSCOLOR_BLUE
%define GOODCOLOR				BIOSCOLOR_GREEN
%define BADCOLOR				BIOSCOLOR_BROWN

;; Selectors in the GDT
%define PRIV_CODESELECTOR		0x0008
%define PRIV_DATASELECTOR		0x0010
%define PRIV_STCKSELECTOR		0x0018
%define LDRCODESELECTOR			0x0020

;; Filesystem types
%define FS_UNKNOWN				0
%define FS_FAT12				1
%define FS_FAT16				2
%define FS_FAT32				3

;; Filesystem values
%define FAT_BYTESPERDIRENTRY	32
%define FAT12_NYBBLESPERCLUST	3
%define FAT16_NYBBLESPERCLUST	4
%define FAT32_NYBBLESPERCLUST	8

;; CPU types
%define i486					0
%define PENTIUM					1
%define PENTIUMPRO				2
%define PENTIUM2				3
%define PENTIUM3				4
%define PENTIUM4				5

;; Number of elements in our memory map
%define MEMORYMAPSIZE			50

;; Maximum number of graphics modes we check
%define MAXVIDEOMODES			100

;; Our data structures that we pass to the kernel, mostly having to do with
;; hardware
STRUC graphicsInfoBlock
	.videoMemory	resd 1 ;; Video memory in Kbytes
	.framebuffer	resd 1 ;; Address of the framebuffer
	.mode			resd 1 ;; Current video mode
	.xRes			resd 1 ;; Current X resolution
	.yRes			resd 1 ;; Current Y resolution
	.bitsPerPixel	resd 1 ;; Bits per pixel
	.scanLineBytes	resd 1 ;; Scan line length in bytes
	.numberModes	resd 1 ;; Number of graphics modes in the following list
	.supportedModes	resd (MAXVIDEOMODES * 4)
ENDSTRUC

STRUC memoryInfoBlock
	.start	resq 1
	.size	resq 1
	.type	resd 1
ENDSTRUC

;; The data structure created by the loader to describe the particulars
;; about a floppy disk drive to the kernel
STRUC fddInfoBlock
	.type		resd 1
	.heads		resd 1
	.tracks		resd 1
	.sectors	resd 1
ENDSTRUC

;; The data structure created by the loader to describe the particulars
;; about a hard disk drive
STRUC hddInfoBlock
	.heads			resd 1
	.cylinders		resd 1
	.sectors		resd 1
	.bytesPerSector	resd 1
	.totalSectors	resd 1
ENDSTRUC

;; The data structure created by the loader to hold info about the serial
;; ports
STRUC serialInfoBlock
	.port1	resd 1
	.port2	resd 1
	.port3	resd 1
	.port4	resd 1
ENDSTRUC

