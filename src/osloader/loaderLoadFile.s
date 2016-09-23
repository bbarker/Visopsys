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
;;  loaderLoadFile.s
;;

	GLOBAL loaderCalcVolInfo
	GLOBAL loaderFindFile
	GLOBAL loaderLoadFile
	GLOBAL PARTENTRY
	GLOBAL FILEDATABUFFER

	EXTERN loaderMemCopy
	EXTERN loaderPrint
	EXTERN loaderPrintNewline
	EXTERN loaderDiskError
	EXTERN loaderGetCursorAddress
	EXTERN loaderSetCursorAddress

	EXTERN BYTESPERSECT
	EXTERN ROOTDIRCLUST
	EXTERN ROOTDIRENTS
	EXTERN RESSECS
	EXTERN FATSECS
	EXTERN SECPERTRACK
	EXTERN HEADS
	EXTERN SECPERCLUST
	EXTERN FATS
	EXTERN DRIVENUMBER
	EXTERN FSTYPE

	SEGMENT .text
	BITS 16
	ALIGN 4

	%include "loader.h"


headTrackSector:
	;; This routine takes the logical sector number in EAX.  From this it
	;; calculates the head, track and sector number on disk.

	;; We destroy a bunch of registers, so save them
	pusha

	;; First the sector
	xor EDX, EDX
	xor EBX, EBX
	mov BX, word [SECPERTRACK]
	div EBX
	mov byte [SECTOR], DL			; The remainder
	add byte [SECTOR], 1			; Sectors start at 1

	;; Now the head and track
	xor EDX, EDX					; Don't need the remainder anymore
	xor EBX, EBX
	mov BX, word [HEADS]
	div EBX
	mov byte [HEAD], DL				; The remainder
	mov word [CYLINDER], AX

	popa
	ret


read:
	;; Proto: int read(dword logical, word seg, word offset, dword count);

	;; Save a word on the stack for our return value
	push word 0

	;; Save regs
	pusha

	;; Save the stack pointer
	mov BP, SP

	push word 0						; To keep track of read attempts

	.readAttempt:
	;; Determine whether int13 extensions are available
	cmp word [DRIVENUMBER], 80h
	jb .noExtended

	mov AX, 4100h
	mov BX, 55AAh
	mov DX, word [DRIVENUMBER]
	int 13h
	jc .noExtended

	;; We have a nice extended read function which will allow us to
	;; just use the logical sector number for the read

	mov word [DISKPACKET], 0010h	; Packet size
 	mov EAX, dword [SS:(BP + 28)]
	mov word [DISKPACKET + 2], AX	; Sector count
	mov AX, word [SS:(BP + 26)]
	mov word [DISKPACKET + 4], AX	; Offset
	mov AX, word [SS:(BP + 24)]
	mov word [DISKPACKET + 6], AX	; Segment
	mov EAX, dword [SS:(BP + 20)]
	mov dword [DISKPACKET + 8], EAX	; Logical sector
	mov dword [DISKPACKET + 12], 0	;
	mov AX, 4200h
	mov DX, word [DRIVENUMBER]
	mov SI, DISKPACKET
	int 13h
	jc .IOError
	jmp .done

	.noExtended:
	;; Calculate the CHS
	mov EAX, dword [SS:(BP + 20)]
	call headTrackSector

	mov EAX, dword [SS:(BP + 28)]	; Number to read
	mov AH, 02h						; Subfunction 2
	mov CX, word [CYLINDER]			; >
	rol CX, 8						; > Cylinder
	shl CL, 6						; >
	or CL, byte [SECTOR]			; Sector
	mov DX, word [DRIVENUMBER]		; Drive
	mov DH, byte [HEAD]				; Head
	mov BX, word [SS:(BP + 26)]		; Offset
	push ES							; Save ES
	push word [SS:(BP + 24)]		; Use user-supplied segment
	pop ES
	int 13h
	pop ES							; Restore ES
	jc .IOError
	jmp .done

	.IOError:
	;; We'll reset the disk and retry up to 4 more times

	;; Reset the disk controller
	xor AX, AX
	mov DX, word [DRIVENUMBER]
	int 13h

	;; Increment the counter
	pop AX
	inc AX
	push AX
	cmp AX, 05h
	jnae .readAttempt

	mov word [SS:(BP + 16)], -1

	.done:
	pop AX							; Counter
	popa
	xor EAX, EAX
	pop AX							; Status
	ret


loadFAT:
	;; This routine will load the entire FAT table into memory at
	;; location [FATSEGMENT:0000]

	;; Save 1 word for our return code
	push word 0

	pusha

	;; Save the stack pointer
	mov BP, SP

	;; Don't want to read more than 128 FAT sectors (one segment's worth)
	mov EBX, dword [FATSECS]
	cmp EBX, 64
	jb .noShrink
	mov EBX, 64
	.noShrink:

	xor EAX, EAX
	mov AX, word [RESSECS]			; FAT starts after reserved sectors

	cmp word [DRIVENUMBER], 80h
	jb .noOffset
	add EAX, dword [PARTENTRY + 8]
	.noOffset:

	push dword EBX					; Number of FAT sectors
	push word 0						; Offset (beginning of buffer)
	push word [FATSEGMENT]			; Segment of data buffer
	push dword EAX
	call read
	add SP, 12

	;; Check status
	cmp AX, 0
	je .done

	;; Call the 'disk error' routine
	call loaderDiskError
	;; Put a -1 as our return code
	mov word [SS:(BP + 16)], -1

	.done:
	popa
	;; Pop the return code
	xor EAX, EAX
	pop AX
	ret


loadDirectory:
	;; This subroutine finds the root directory of the boot volume
	;; and loads it into memory at location [FATSEGMENT:0000]

	;; Save 1 word for our return code
	push word 0

	;; Save regs
	pusha

	;; Save the stack pointer
	mov BP, SP

	;; Get the logical sector number of the root directory
	xor EAX, EAX
	mov AX, word [RESSECS]			; The reserved sectors
	mov EBX, dword [FATSECS]		; Sectors per FAT
	add EAX, EBX					; Sectors for 1st FAT
	add EAX, EBX					; Sectors for 2nd FAT

	cmp word [FSTYPE], FS_FAT32
	jne .notFat32

	;; Obviously I was intending to put something here for finding the
	;; root directory sector here in FAT32?

	.notFat32:
	;; Add the partition starting sector if applicable
	cmp word [DRIVENUMBER], 80h
	jb .noOffset
	add EAX, dword [PARTENTRY + 8]
	.noOffset:

	;; This is normally where we will keep the FAT data, but we will
	;; put the directory here temporarily
	xor EBX, EBX
	mov BX, word [DIRSECTORS]		; Number of sectors to read
	push dword EBX
	push word 0						; Load at offset 0 of the data buffer
	push word [FATSEGMENT]			; Segment of the data buffer
	push dword EAX
	call read
	add SP, 12

	;; Check status
	cmp AX, 0
	je .done

	;; Call the 'disk error' routine
	call loaderDiskError

	;; Put a -1 as our return code
	mov word [SS:(BP + 16)], -1

	.done:
	popa
	;; Pop the return code
	xor EAX, EAX
	pop AX
	ret


searchFile:
	;; This routine will search the pre-loaded root directory of the
	;; boot volume at LDRDATABUFFER and return the starting cluster of
	;; the requested file.
	;; Proto:
	;;   int searchFile(char *filename);

	;; Save a word for our return code (the starting cluster of the
	;; file)
	push dword 0

	;; Save regs
	pusha

	;; Save the stack pointer
	mov BP, SP

	;; The root directory should be loaded into memory at the location
	;; LDRDATABUFFER.  We will walk through looking for the name
	;; we were passed as a parameter.
	mov word [ENTRYSTART], 0

	;; Get the pointer to the requested name from the stack
	mov SI, word [SS:(BP + 22)]

	;; We need to make ES point to the data buffer
	push ES
	mov ES, word [FATSEGMENT]

	.entryLoop:
	;; Determine whether this is a valid, undeleted file.
	;; E5 means this is a deleted entry
	mov DI, word [ENTRYSTART]
	mov AL, byte [ES:DI]
	cmp AL, 0E5h
	je .notThisEntry	; Deleted
	;; 00 means that there are no more entries
	cmp AL, 0
	je .noFile

	xor CX, CX

	.nextLetter:
	mov DI, word [ENTRYSTART]
	add DI, CX
	mov AL, byte [ES:DI]
	mov BX, SI
	add BX, CX
	mov DL, byte [BX]

	cmp AL, DL
	jne .notThisEntry

	inc CX
	cmp CX, 11
	jb .nextLetter
	jmp .foundFile

	.notThisEntry:
	;; Move to the next directory entry
	add word [ENTRYSTART], FAT_BYTESPERDIRENTRY

	;; Make sure we're not at the end of the directory
	mov AX, word [BYTESPERSECT]
	mul word [DIRSECTORS]
	cmp word [ENTRYSTART], AX
	jae .noFile

	jmp .entryLoop

	.noFile:
	;; Restore ES
	pop ES
	;; The file is not there.  Return -1 as our error code
	mov dword [SS:(BP + 16)], -1
	;; Jump to the end.  We're finished
	jmp .done

	.foundFile:
	;; Return the starting cluster number of the file
	xor EAX, EAX
	mov BX, word [ENTRYSTART]
	add BX, 0014h					;; Offset in directory entry of high word
	mov AX, word [ES:BX]
	shl EAX, 16
	add BX, 6						;; Offset (1Ah) in dir entry of low word
	mov AX, word [ES:BX]
	mov dword [SS:(BP + 16)], EAX

	;; Record the size of the file
	mov BX, word [ENTRYSTART]
	add BX, 001Ch					;; Offset in directory entry
	mov EAX, dword [ES:BX]
	mov dword [FILESIZE], EAX

	;; Restore ES
	pop ES

	.done:
	popa
	;; Pop our return value
	xor EAX, EAX
	pop EAX
	ret


makeProgress:
	;; This routine sets up a little progress indicator.

	pusha

	cmp byte [SHOWPROGRESS], 0
	je .done

	;; Disable the cursor
	mov CX, 2000h
	mov AH, 01h
	int 10h

	mov DL, FOREGROUNDCOLOR
	mov SI, PROGRESSTOP
	call loaderPrint
	call loaderPrintNewline
	call loaderGetCursorAddress
	add AX, 1
	push AX
	mov SI, PROGRESSMIDDLE
	call loaderPrint
	call loaderPrintNewline
	mov SI, PROGRESSBOTTOM
	call loaderPrint
	pop AX
	call loaderSetCursorAddress

	;; To keep track of how many characters we've printed in the
	;; progress indicator
	mov word [PROGRESSCHARS], 0

	.done:
	popa


updateProgress:
	pusha

	cmp byte [SHOWPROGRESS], 0
	je .done

	;; Make sure we're not already at the end
	mov AX, word [PROGRESSCHARS]
	cmp AX, PROGRESSLENGTH
	jae .done
	inc AX
	mov word [PROGRESSCHARS], AX

	;; Print the character on the screen
	mov DL, GOODCOLOR
	mov CX, 1
	mov SI, PROGRESSCHAR
	call loaderPrint

	.done:
	popa
	ret


killProgress:
	;; Get rid of the progress indicator

	pusha

	cmp byte [SHOWPROGRESS], 0
	je .done

	call loaderPrintNewline
	call loaderPrintNewline

	.done:
	popa
	ret


clusterToLogical:
	;; This takes the cluster number in EAX and returns the logical
	;; sector number in EAX

	;; Save a word for our return code
	sub SP, 4

	;; Save regs
	pusha

	;; Save the stack pointer
	mov BP, SP

	sub EAX, 2						;  Subtract 2 (because they start at 2)
	xor EBX, EBX
	mov BX, word [SECPERCLUST]		; How many sectors per cluster?
	mul EBX

	;; This little sequence figures out where the data clusters
	;; start on this volume

	xor EBX, EBX
	mov BX, word [RESSECS]			; The reserved sectors
	add EAX, EBX
	mov EBX, dword [FATSECS]
	shl EBX, 1						; Add sectors for both FATs
	add EAX, EBX

	cmp word [FSTYPE], FS_FAT32
	je .noAddDir
	xor EBX, EBX
	mov BX, word [DIRSECTORS]		; Root dir sectors
	add EAX, EBX
	.noAddDir:

	cmp word [DRIVENUMBER], 80h
	jb .noOffset
	add EAX, dword [PARTENTRY + 8]
	.noOffset:

	mov dword [SS:(BP + 16)], EAX

	popa
	pop EAX
	ret


loadFile:
	;; This routine is responsible for loading the requested file into
	;; the requested memory location.  The FAT table must have previously
	;; been loaded at memory location LDRDATABUFFER
	;; Proto:
	;;   word loadFile(dword cluster, dword memory_address);

	;; Save a word for our return code
	push word 0

	;; Save regs
	pusha

	;; Save the stack pointer
	mov BP, SP

	;; The first parameter is the starting cluster of the file we're
	;; supposed to load.

	;; The second parameter is a DWORD pointer to the absolute memory
	;; location where we should load the file.

	mov dword [BYTESREAD], 0
	mov dword [OLDPROGRESS], 0

	;; Make a little progress indicator so the user gets transfixed,
	;; and suddenly doesn't mind the time it takes to load the file.
	call makeProgress

	;; Put the starting cluster number in NEXTCLUSTER
	mov EAX, dword [SS:(BP + 20)]
	mov dword [NEXTCLUSTER], EAX

	;; Put the starting memory offset in MEMORYMARKER
	mov EAX, dword [SS:(BP + 24)]
	mov dword [MEMORYMARKER], EAX

	;; Save ES, because we're going to dick with it throughout.
	push ES

	.FATLoop:
	;; Get the logical sector for this cluster number
	mov EAX, dword [NEXTCLUSTER]
	call clusterToLogical

	;; Use the portion of loader's data buffer that comes AFTER the
	;; FAT data.  This is where we will initially load each cluster's
	;; contents.
	xor EBX, EBX
	mov BX, word [SECPERCLUST]		; Read 1 cluster's worth of sectors
	push dword EBX
	push word 0						; >
	push word [CLUSTERSEGMENT]		; > Real-mode buffer for data
	push dword EAX
	call read
	add SP, 12

	cmp AX, 0
	je .gotCluster

	;; Make an error message
	call killProgress
	call loaderDiskError

	;; Return -1 as our error code
	mov word [SS:(BP + 16)], -1
	jmp .done

	.gotCluster:
	;; Update the number of bytes read
	mov EAX, dword [BYTESREAD]
	add EAX, dword [BYTESPERCLUST]
	mov dword [BYTESREAD], EAX

	;; Determine whether we should update the progress indicator
	mov EAX, dword [BYTESREAD]
	mov EBX, 100
	mul EBX
	xor EDX, EDX
	div dword [FILESIZE]
	mov EBX, EAX
	sub EBX, dword [OLDPROGRESS]
	cmp EBX, (100 / PROGRESSLENGTH)
	jb .noProgress
	mov dword [OLDPROGRESS], EAX
	call updateProgress
	.noProgress:

	mov EAX, dword [BYTESPERCLUST]
	push dword EAX
	push dword [MEMORYMARKER]
	push dword [CLUSTERBUFFER]		; 32-bit source address
	call loaderMemCopy
	add SP, 12

	cmp AX, 0
	je .copiedData

	;; Couldn't copy the data into high memory
	call killProgress
	call loaderPrintNewline
	mov DL, BADCOLOR
	mov SI, INT15ERR
	call loaderPrint
	call loaderPrintNewline

	;; Return -2 as our error code
	mov word [SS:(BP + 16)], -2
	jmp .done

	.copiedData:
	;; Increment the buffer pointer
	mov EAX, dword [BYTESPERCLUST]
	add dword [MEMORYMARKER], EAX

	;; Now make ES point to the beginning of loader's data buffer,
	;; which contains the FAT data
	mov ES, word [FATSEGMENT]

	;; Get the next cluster in the chain

	cmp word [FSTYPE], FS_FAT32
	je .fat32

	cmp word [FSTYPE], FS_FAT16
	je .fat16

	.fat12:
	mov EAX, dword [NEXTCLUSTER]
	mov BX, FAT12_NYBBLESPERCLUST
	mul BX   						; We can ignore DX because it shouldn't
									; be bigger than a word
	mov BX, AX
	;; There are 2 nybbles per byte.  We will shift the register
	;; right by 1, and the remainder (1 or 0) will be in the
	;; carry flag.
	shr BX, 1	; Divide by 2
	;; Now we have to shift or mask the value in AX depending
	;; on whether CF is 1 or 0
	jnc .mask
	;; Get the value at ES:BX
	mov AX, word [ES:BX]
	;; Shift the value we got
	shr AX, 4
	jmp .doneConvert
	.mask:
	;; Get the value at ES:BX
	mov AX, word [ES:BX]
	;; Mask the value we got
	and AX, 0FFFh
	.doneConvert:
	cmp AX, 0FF8h
	jae .success
	jmp .next

	.fat16:
	mov EBX, dword [NEXTCLUSTER]
	;; FAT16_NYBBLESPERCLUST is 4, so we can just shift
	shl EBX, 1
	mov AX, word [ES:BX]
	cmp AX, 0FFF8h
	jae .success
	jmp .next

	.fat32:
	mov EBX, dword [NEXTCLUSTER]
	;; FAT32_NYBBLESPERCLUST is 8, so we can just shift
	shl EBX, 2
	mov EAX, dword [ES:EBX]
	cmp EAX, 0FFFFFF8h
	jae .success

	.next:
	mov dword [NEXTCLUSTER], EAX
	jmp .FATLoop

	.success:
	;; Return 0 for success
	mov word [SS:(BP + 16)], 0
	call killProgress

	.done:
	;; Restore ES
	pop ES
	popa
	;; Pop our return code
	xor EAX, EAX
	pop AX
	ret


loaderCalcVolInfo:
	;; This routine will calculate some constant things that are dependent
	;;  upon the type of the current volume.  It stores the results in
	;; the static data area for the use of the other routines

	pusha

	;; Calculate the number of bytes per cluster in this volume
	xor EAX, EAX
	mov AX, word [BYTESPERSECT]
	mul word [SECPERCLUST]
	mov dword [BYTESPERCLUST], EAX

	mov AX, word [FSTYPE]
	cmp AX, FS_FAT32
	je .fat32

	;; How many root directory sectors are there?
	mov AX, FAT_BYTESPERDIRENTRY
	mul word [ROOTDIRENTS]
	xor DX, DX
	div word [BYTESPERSECT]
	mov word [DIRSECTORS], AX
	jmp .doneRoot

	.fat32:
	;; Just do one cluster of the root dir
	mov AX, word [SECPERCLUST]
	mov word [DIRSECTORS], AX

	.doneRoot:
	;; Calculate the segment where we will keep FAT (and directory)
	;; data after loading them.  It comes at the beginning of the
	;; LDRDATABUFFER
	mov AX, (LDRDATABUFFER / 16)
	mov word [FATSEGMENT], AX

	;; Calculate a buffer where we will load cluster data.  It comes
	;; after the FAT data in the LDRDATABUFFER
	mov EAX, dword [FATSECS]
	mul word [BYTESPERSECT]
	add EAX, LDRDATABUFFER
	mov dword [CLUSTERBUFFER], EAX
	shr EAX, 4
	mov word [CLUSTERSEGMENT], AX

	;; Calculate a buffer for general file data.  It comes after the
	;; buffer for cluster data
	mov EAX, dword [BYTESPERCLUST]
	add EAX, dword [CLUSTERBUFFER]
	mov dword [FILEDATABUFFER], EAX

	popa
	ret


loaderFindFile:
	;; This routine is will simply search for the requested file, and
	;; return the starting cluster number if it is present.  Returns
	;; negative otherwise.
	;; Proto:
	;;   int loaderFindFile(char *filename);

	;; Save a word for our return code
	push word 0

	;; Save registers
	pusha

	;; Save the stack pointer
	mov BP, SP

	;; The parameter is a pointer to an 11-character string
	;; (FAT 8.3 format) containing the name of the file to find.

	;; We need to locate the file.  Read the root directory from
	;; the disk
	call loadDirectory

	;; Was that successful?  Do a signed comparison.  Less than 0
	;; means error.
	cmp AX, 0
	jge .search

	;; Failed to load the directory.  Put a 0 as our return code
	mov word [SS:(BP + 16)], 0
	jmp .done

	.search:
	;; Now we need to search for the requested file in the root
	;; directory.
	push word [SS:(BP + 20)]
	call searchFile
	add SP, 2

	;; If the file was found successfully, put a 1 as our return
	;; code.  Otherwise, put 0
	cmp EAX, 0
	jge .success

	mov word [SS:(BP + 16)], 0
	jmp .done

	.success:
	mov word [SS:(BP + 16)], 1

	.done:
	popa
	pop AX							; return code
	ret


loaderLoadFile:
	;; This routine is responsible for loading the requested file into
	;; the requested memory location.  It is specific to the FAT-12
	;; filesystem.
	;; Proto:
	;;   dword loaderLoadFile(char *filename, dword loadOffset,
	;;			  word showProgress)

	;; Save a dword for our return code
	push dword 0

	;; Save registers
	pusha

	;; Save the stack pointer
	mov BP, SP

	;; The parameter is a pointer to an 11-character string
	;; (FAT 8.3 format) containing the name of the file to load.

	;; The second parameter is a DWORD value representing the absolute
	;; memory location at which we should load the file.

	;; We need to locate the file.  Read the root directory from
	;; the disk
	call loadDirectory

	;; Was that successful?  Do a signed comparison.  Less than 0
	;; means error.
	cmp AX, 0
	jge .search

	;; Failed to load the directory.  Put a -1 as our return code
	mov dword [SS:(BP + 16)], -1
	jmp .done

	.search:
	;; Now we need to search for the requested file in the root
	;; directory.
	push word [SS:(BP + 22)]
	call searchFile
	add SP, 2

	;; Was that successful?  Do a signed comparison.  Less than 0
	;; means error.
	cmp EAX, 0
	jge .loadFAT

	;; Failed to find the file.  Put a -2 as our return code
	mov dword [SS:(BP + 16)], -2
	jmp .done

	.loadFAT:
	;; Save the starting cluster of the file (it's in EAX)
	mov dword [NEXTCLUSTER], EAX

	;; Now we load the FAT table into memory
	call loadFAT

	;; Was that successful?  Do a signed comparison.  Less than 0
	;; means error.
	cmp AX, 0
	jge .loadFile

	;; Failed to load the FAT.  Put -3 as our return code
	mov dword [SS:(BP + 16)], -3
	jmp .done

	.loadFile:
	;; Now we can actually load the file.  The starting cluster
	;; that we saved earlier on the stack will be the first parameter
	;; to the loadFile function.  The second parameter is the
	;; load location of the file (which was THIS function's second
	;; parameter, also)

	mov AX, word [SS:(BP + 28)]
	mov byte [SHOWPROGRESS], AL

	push dword [SS:(BP + 24)]
	push dword [NEXTCLUSTER]
	call loadFile
	add SP, 8

	;; Was that successful?  Do a signed comparison.  Less than 0
	;; means error.
	cmp AX, 0
	jge .success

	;; Failed to load the file.  Put -4 as our return code
	mov dword [SS:(BP + 16)], -4
	jmp .done

	.success:
	;; Success.  Put the file size as our return code.
	mov EAX, dword [FILESIZE]
	mov dword [SS:(BP + 16)], EAX

	.done:
	popa
	;; Pop the return code
	pop EAX
	ret


	SEGMENT .data
	ALIGN 4

MEMORYMARKER	dd 0		;; Offset to load next data cluster
FATSEGMENT		dw 0		;; The segment for FAT and directory data
CLUSTERBUFFER	dd 0		;; The buffer for cluster data
CLUSTERSEGMENT	dw 0		;; The segment of the buffer for cluster data
FILEDATABUFFER	dd 0		;; The buffer for general file data
DIRSECTORS		dw 0		;; The size of the root directory, in sectors
BYTESPERCLUST	dd 0		;; Bytes per cluster
ENTRYSTART		dw 0		;; Directory entry start
FILESIZE		dd 0		;; Size of the file we're loading
BYTESREAD		dd 0		;; Number of bytes read so far
NEXTCLUSTER		dd 0		;; Next cluster to load

;; For int13 disk ops
CYLINDER		dw 0
HEAD			db 0
SECTOR			db 0

;; Disk cmd packet for ext. int13
DISKPACKET		dd 0, 0, 0, 0

PARTENTRY		times 16 db 0 ;; Partition table entry of bootable partition

;; Stuff for the progress indicator
PROGRESSCHARS	dw 0		;; Number of progress indicator chars showing
OLDPROGRESS		dd 0		;; Percentage of file load completed
SHOWPROGRESS	db 0		;; Whether or not to show a progress bar
PROGRESSTOP		db 218
				times PROGRESSLENGTH db 196
				db 191, 0
PROGRESSMIDDLE	db 179
				times PROGRESSLENGTH db ' '
				db 179, 0
PROGRESSBOTTOM	db 192
				times PROGRESSLENGTH db 196
				db 217, 0
PROGRESSCHAR	db 177, 0

INT15ERR		db 'The computer', 27h, 's BIOS was unable to move data into '
				db 'high memory.', 0

