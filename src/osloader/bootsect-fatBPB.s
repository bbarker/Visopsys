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
;;  bootsect-fatBPB.s
;;

;; This code is the BIOS Parameter Block (BPB) for the FAT boot sectors.
;; it's not meant to be used on its own, merely %included.


OEMName			times 8 db ' '	; 03 - 0A OEM Name
BytesPerSect	dw 0			; 0B - 0C Bytes per sector
SecPerClust		db 0			; 0D - 0D Sectors per cluster
ResSectors		dw 0			; 0E - 0F Reserved sectors
FATs			db 0			; 10 - 10 Copies of FAT
RootDirEnts		dw 0			; 11 - 12 Max root directory entries
Sectors			dw 0			; 13 - 14 Number of sectors
Media			db 0			; 15 - 15 Media descriptor byte
FATSecs			dw 0			; 16 - 17 Sectors per FAT
SecPerTrack		dw 0			; 18 - 19 Sectors per track
Heads			dw 0			; 1A - 1B Number of heads
Hidden			dd 0			; 1C - 1F Hidden sectors
HugeSecs		dd 0			; 20 - 23 Number of sectors (32)

%ifdef FAT32
FATSize			dd 0			; 24 - 27 Sectors per FAT (32)
ExtFlags		dw 0			; 28 - 29 Flags
FSVersion		dw 0			; 2A - 2B FS version number
RootClust		dd 0			; 2C - 2F Root directory cluster
FsInfo			dw 0			; 30 - 31 FSInfo struct sector
BackupBoot		dw 0			; 32 - 33 Backup boot sector
Reserved2		times 12 db 0	; 34 - 3F ?
DriveNumber		db 0			; 40 - 40 BIOS drive number
Reserved1		db 0 			; 41 - 41 ?
BootSignature	db 0			; 42 - 42 Signature
VolumeID		dd 0			; 43 - 46 Volume ID
VolumeName		times 11 db ' '	; 47 - 51 Volume name
FSType			times 8 db ' '	; 52 - 59 Filesystem type

%else	;; FAT12, FAT16
DriveNumber		db 0			; 24 - 24 BIOS drive number
Reserved1		db 0 			; 25 - 25 ?
BootSignature	db 0			; 26 - 26 Signature
VolumeID		dd 0			; 27 - 2A Volume ID
VolumeName		times 11 db ' '	; 2B - 35 Volume name
FSType			times 8 db ' '	; 36 - 3D Filesystem type
%endif

