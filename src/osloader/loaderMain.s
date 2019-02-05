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
;;  loaderMain.s
;;

	EXTERN loaderSetTextDisplay
	EXTERN loaderCalcVolInfo
	EXTERN loaderFindFile
	EXTERN loaderDetectHardware
	EXTERN loaderLoadKernel
	EXTERN loaderEnableA20
	EXTERN loaderQueryGraphicMode
	EXTERN loaderSetGraphicDisplay
	EXTERN loaderPrint
	EXTERN loaderPrintNumber
	EXTERN loaderPrintNewline
	EXTERN BOOTSECTSIG
	EXTERN PARTENTRY
	EXTERN HARDWAREINFO
	EXTERN KERNELGMODE
	EXTERN KERNELENTRY

	GLOBAL loaderMain
	GLOBAL loaderMemCopy
	GLOBAL loaderMemSet
	GLOBAL KERNELSIZE
	GLOBAL BYTESPERSECT
	GLOBAL ROOTDIRENTS
	GLOBAL FSTYPE
	GLOBAL RESSECS
	GLOBAL FATS
	GLOBAL FATSECS
	GLOBAL HEADS
	GLOBAL CYLINDERS
	GLOBAL SECPERTRACK
	GLOBAL SECPERCLUST
	GLOBAL ROOTDIRCLUST
	GLOBAL DRIVENUMBER
	GLOBAL FATALERROR
	GLOBAL PRINTINFO

	SEGMENT .text
	BITS 16
	ALIGN 4

	%include "loader.h"


loaderMain:
	;; This is the main OS loader driver code.  It calls a number
	;; of other routines for the puposes of detecting hardware,
	;; loading the kernel, etc.  After everything else is done, it
	;; switches the processor to protected mode and starts the kernel.

	cli

	;; Make sure all of the data segment registers point to the same
	;; segment as the code segment
	mov EAX, (LDRCODESEGMENTLOCATION / 16)
	mov DS, EAX
	mov ES, EAX
	mov FS, EAX
	mov GS, EAX

	;; Now ensure the stack segment and stack pointer are set to
	;; something more appropriate for the loader
	mov EAX, (LDRSTCKSEGMENTLOCATION / 16)
	mov SS, EAX
	mov SP, LDRSTCKBASE

	sti

	;; The boot sector code should have put the boot drive number in DL
	xor DH, DH
	mov word [DRIVENUMBER], DX

	;; It should also have put the boot sector signature in EBX
	mov dword [BOOTSECTSIG], EBX

	;; If we are not booting from a floppy, then the boot sector code
	;; should have put a pointer to the MBR record for this partition
	;; in SI.  Copy the partition entry.
	cmp word [DRIVENUMBER], 80h
	jb .setTextDisplay
	push DS
	push 0
	pop DS
	mov CX, 16
	mov DI, PARTENTRY
	cld
	rep movsb
	pop DS

	.setTextDisplay:
	;; Set the text display to a good mode, clearing the screen
	push word 0
	call loaderSetTextDisplay
	add SP, 2

	;; Print the 'Visopsys loader' messages
	mov SI, LOADMSG1
	mov DL, FOREGROUNDCOLOR
	call loaderPrint
	call loaderPrintNewline
	mov SI, LOADMSG2
	call loaderPrint
	call loaderPrintNewline
	call loaderPrintNewline

	;; Gather information about the boot device
	call bootDevice

	;; Get FAT filesystem info from the boot sector
	call fatInfo

	;; Calculate values that will help us deal with the filesystem
	;; volume correctly
	call loaderCalcVolInfo

	;; Before we print any other info, determine whether the user wants
	;; to see any hardware info messages.  If the BOOTINFO file exists,
	;; then we print the messages
	push word BOOTINFO
	call loaderFindFile
	add SP, 2
	mov word [PRINTINFO], AX

	;; Print out the boot device information
	cmp word [PRINTINFO], 1
	jne .noPrint1
	call printBootDevice
	.noPrint1:

	;; Call the routine to do the hardware detection
	call loaderDetectHardware

	;; Do a fatal error check before loading
	call fatalErrorCheck

	call loaderPrintNewline
	mov SI, LOADING
	mov DL, FOREGROUNDCOLOR
	call loaderPrint
	call loaderPrintNewline

	;; Load the kernel
	call loaderLoadKernel

	;; Make sure the kernel load was successful
	cmp AX, 0
	jge .okLoad

	add byte [FATALERROR], 1

	.okLoad:
	;; Enable the A20 address line so that we will have access to the
	;; entire extended memory space.
	call loaderEnableA20

	;; Check for fatal errors before attempting to start the kernel
	call fatalErrorCheck

	;; Check for user requesting a boot menu
	call bootMenu

	;; Did we find a good graphics mode?
	cmp word [KERNELGMODE], 0
	je .noGraphics

	;; Get the graphics mode for the kernel and switch to it
	push word [KERNELGMODE]
	call loaderSetGraphicDisplay
	add SP, 2

	.noGraphics:
	;; Disable the cursor
	mov CX, 2000h
	mov AH, 01h
	int 10h

	;; Disable interrupts.  The kernel's initial state will be with
	;; interrupts disabled.  It will have to do the appropriate setup
	;; before re-enabling them.
	cli

	;; Set up a temporary GDT (Global Descriptor Table) for the protected
	;; mode switch.  The kernel will replace it later with a permanent
	;; version
	lgdt [GDTSTART]

	;; Save the kernel's entry point address in EBX
	mov EBX, dword [KERNELENTRY]

	;; Here's the big moment.  Switch permanently to protected mode.
	mov EAX, CR0
	or AL, 01h
	mov CR0, EAX

	BITS 32

	;; Set EIP to protected mode values by spoofing a far jump
	db 0EAh
	dw .returnLabel, LDRCODESELECTOR
	.returnLabel:

	;; Now enable very basic paging
	call pagingSetup

	;; Make the data and stack segment registers contain correct
	;; values for the kernel in protected mode

	;; First the data registers (all point to the whole memory as data)
	mov EAX, PRIV_DATASELECTOR
	mov DS, EAX
	mov ES, EAX
	mov FS, EAX
	mov GS, EAX

	;; Now the stack registers
	mov EAX, PRIV_STCKSELECTOR
	mov SS, EAX
	mov EAX, KERNELVIRTUALADDRESS
	add EAX, dword [LDRCODESEGMENTLOCATION + KERNELSIZE]
	add EAX, (KERNELSTACKSIZE - 4)
	mov ESP, EAX

	;; Pass the kernel arguments.

	;; First the hardware structure
	push dword (LDRCODESEGMENTLOCATION + HARDWAREINFO)

	;; Next the kernel stack size
	push dword KERNELSTACKSIZE

	;; Next the kernel stack location
	mov EAX, KERNELVIRTUALADDRESS
	add EAX, dword [LDRCODESEGMENTLOCATION + KERNELSIZE]
	push EAX

	;; Next the amount of used kernel memory.  We need to add the
	;; size of the stack we allocated to the kernel image size
	mov EAX, dword [LDRCODESEGMENTLOCATION + KERNELSIZE]
	add EAX, KERNELSTACKSIZE
	push EAX

	;; Waste some space on the stack that would normally be used to
	;; store the return address in a call.  This will ensure that the
	;; kernel's arguments are located where gcc thinks they should be
	;; for a "normal" function call.  We will use a NULL return address
	push dword 0

	;; Start the kernel.
	push dword PRIV_CODESELECTOR
	push dword EBX
	retf

	BITS 16

	;;--------------------------------------------------------------


bootDevice:
	;; This function will gather some needed information about the
	;; boot device (and print messages about what it finds)

	pusha

	;; Gather some more disk information directly from the
	;; BIOS using interrupt 13, subfunction 8

	;; This interrupt call will destroy ES, so save it
	push ES

	;; Guards againt BIOS bugs, apparently
	push word 0
	pop ES
	xor DI, DI

	mov AX, 0800h
	xor BX, BX
	xor CX, CX
	mov DX, word [DRIVENUMBER]
	int 13h

	;; Restore ES
	pop ES

	jnc .gotDiskInfo

	;; Ooops, the BIOS isn't helping us...
	;; Print out the fatal error message

	;; Change to the error color
	mov DL, BADCOLOR

	mov SI, SAD
	call loaderPrint
	mov SI, BOOTDEV1
	call loaderPrint
	mov SI, BIOSERR
	call loaderPrint
	call loaderPrintNewline
	mov SI, BLANK
	call loaderPrint
	mov SI, BIOSERR2
	call loaderPrint
	call loaderPrintNewline

	;; We can't continue with any disk-related stuff, so we can't load
	;; the kernel.
	add  byte [FATALERROR], 1
	jmp .done

	.gotDiskInfo:

	;; Heads
	xor EAX, EAX
	mov AL, DH
	inc AX							; Number is 0-based
	mov dword [HEADS], EAX

	;; cylinders
	xor EAX, EAX
	mov AL, CL						; Two bits of cylinder number in bits 6&7
	and AL, 11000000b				; Mask it
	shl AX, 2						; Move them to bits 8&9
	mov AL, CH						; Rest of the cylinder bits
	inc AX							; Number is 0-based
	mov dword [CYLINDERS], EAX

	;; sectors
	xor EAX, EAX
	mov AL, CL		; Bits 0-5
	and AL, 00111111b	; Mask it
	mov dword [SECPERTRACK], EAX

	;; Determine whether we can use an extended BIOS function to give
	;; us the number of sectors

	mov word [HDDINFO], 42h  		; Size of the info buffer we provide
	mov AH, 48h
	mov DX, word [DRIVENUMBER]
	mov SI, HDDINFO
	int 13h

	;; Function call successful?
	jc .done

	;; Save the number of sectors
	mov EAX, dword [HDDINFO + 10h]
	mov dword [TOTALSECS], EAX

	;; Recalculate the number of cylinders
	mov EAX, dword [HEADS]			; heads
	mul dword [SECPERTRACK]			; sectors per cyl
	mov ECX, EAX					; total secs per cyl
	xor EDX, EDX
	mov EAX, dword [TOTALSECS]
	div ECX
	mov dword [CYLINDERS], EAX		; new cyls value

	.done:
	popa
	ret


fatInfo:
	;; Gather essential FAT filesystem info from the boot sector

	pusha

	;; The boot sector is loaded at location 7C00h and starts with
	;; some info about the filesystem.  Grab the info we need and store
	;; it in some more convenient locations

	push FS
	xor AX, AX
	mov FS, AX

	mov AL, byte [FS:7C0Dh]
	mov word [SECPERCLUST], AX
	mov AL, byte [FS:7C10h]
	mov word [FATS], AX
	mov AX, word [FS:7C0Eh]
	mov word [RESSECS], AX
	mov AX, word [FS:7C11h]
	mov word [ROOTDIRENTS], AX
	mov AX, word [FS:7C16h]
	mov word [FATSECS], AX

	;; Determine the type of FAT filesystem just based on the FSType
	;; field.  Not reliable, but it's what we do anyway.
	mov EAX, dword [FS:7C37h]
	cmp EAX, 0x32315441				; ('AT12')
	jne .checkFat16
	mov word [FSTYPE], FS_FAT12
	jmp .done
	.checkFat16:
	cmp EAX, 0x36315441				; ('AT16')
	jne .checkFat32
	mov word [FSTYPE], FS_FAT16
	jmp .done
	.checkFat32:
	mov EAX, dword [FS:7C53h]
	cmp EAX, 0x32335441				; ('AT32')
	jne .unknown
	mov word [FSTYPE], FS_FAT32
	;; With FAT32, some of the values are in different places
	mov EAX, dword [FS:7C24h]
	mov dword [FATSECS], EAX
	mov EAX, dword [FS:7C2Ch]
	mov dword [ROOTDIRCLUST], EAX
	jmp .done
	.unknown:
	mov word [FSTYPE], FS_UNKNOWN

	.done:
	pop FS
	popa
	ret


printBootDevice:
	;; Prints info about the boot device

	pusha

	mov DL, GOODCOLOR				; Use good color
	mov SI, HAPPY
	call loaderPrint
	mov SI, BOOTDEV1
	call loaderPrint
	mov DL, FOREGROUNDCOLOR			; Switch to foreground color

	xor EAX, EAX
	mov AX, word [DRIVENUMBER]
	cmp AX, 80h
	jb .notHdd
	mov SI, BOOTHDD
	call loaderPrint
	sub AX, 80h
	jmp .prtDiskNum
	.notHdd:
	mov SI, BOOTFLOPPY
	call loaderPrint
	.prtDiskNum:
	call loaderPrintNumber
	mov SI, BOOTDEV2
	call loaderPrint

	mov EAX, dword [HEADS]
	call loaderPrintNumber
	mov SI, BOOTHEADS
	call loaderPrint

	mov EAX, dword [CYLINDERS]
	call loaderPrintNumber
	mov SI, BOOTCYLS
	call loaderPrint

	mov EAX, dword [SECPERTRACK]
	call loaderPrintNumber
	mov SI, BOOTSECTS
	call loaderPrint

	;; Print the Filesystem type
	mov AX, word [FSTYPE]
	cmp AX, FS_FAT12
	je .fat12
	cmp AX, FS_FAT16
	je .fat16
	cmp AX, FS_FAT32
	je .fat32

	;; Fall through for UNKNOWN
	mov SI, UNKNOWNFS
	mov DL, FOREGROUNDCOLOR
	call loaderPrint
	call loaderPrintNewline
	jmp .done

	.fat12:
	;; Print FAT12
	mov SI, FAT12MES
	mov DL, FOREGROUNDCOLOR
	call loaderPrint
	call loaderPrintNewline
	jmp .done

	.fat16:
	;; Print FAT16
	mov SI, FAT16MES
	mov DL, FOREGROUNDCOLOR
	call loaderPrint
	call loaderPrintNewline
	jmp .done

	.fat32:
	;; Print FAT32
	mov SI, FAT32MES
	mov DL, FOREGROUNDCOLOR
	call loaderPrint
	call loaderPrintNewline

	.done:
	popa
	ret


bootMenu:
	;; This gets called to check whether the user has pressed the ESC key
	;; during the loading process, to give them a menu of options.
	;;
	;; At the moment, it's only for video mode selection.

	pusha

	;; Check for a key press
	mov AX, 0100h
	int 16h
	jz .out

	;; Read the key press
	mov AX, 0000h
	int 16h

	cmp AH, 01h		; ESC
	jne .out

	call loaderPrintNewline
	mov SI, MENUMSG
	mov DL, FOREGROUNDCOLOR
	call loaderPrint
	call loaderPrintNewline
	call loaderPrintNewline

	call loaderQueryGraphicMode

	.out:
	popa
	ret


fatalErrorCheck:
	xor AX, AX
	mov AL, byte [FATALERROR]
	cmp AX, 0000h

	jne .errors
	ret

	.errors:
	call loaderPrintNewline

	;; Print the fact that fatal errors were detected,
	;; and stop
	xor EAX, EAX
	mov AL, byte [FATALERROR]
	call loaderPrintNumber

	mov SI, FATALERROR1
	mov DL, FOREGROUNDCOLOR

	call loaderPrint
	call loaderPrintNewline

	mov SI, FATALERROR2
	mov DL, FOREGROUNDCOLOR

	call loaderPrint
	call loaderPrintNewline

	mov SI, FATALERROR3
	mov DL, FOREGROUNDCOLOR
	call loaderPrint
	call loaderPrintNewline

	;; Print the message indicating system halt/reboot
	mov SI, PRESSREBOOT
	mov DL, FOREGROUNDCOLOR
	call loaderPrint

	mov AX, 0000h
	int 16h

	mov SI, REBOOTING
	mov DL, FOREGROUNDCOLOR
	call loaderPrint

	;; Write the reset command to the keyboard controller
	mov AL, 0FEh
	out 64h, AL
	jecxz $+2
	jecxz $+2

	;; Done.  The computer is now rebooting.

	;; Just in case.  Should never get here.
	hlt


pagingSetup:
	;; This will setup a simple paging environment for the kernel and
	;; enable it.  This involves making a master page directory plus
	;; one page table, and enabling paging.  The kernel can make its own
	;; tables at startup, so this only needs to be temporary.  This
	;; function takes no arguments and returns nothing.  Called only
	;; after protected mode has been entered.

	BITS 32

	pusha

	;; Interrupts should already be disabled

	;; Make sure ES has the selector that points to the whole memory
	;; space
	mov EAX, PRIV_DATASELECTOR
	mov ES, AX

	;; Create a page table to identity-map the first 4 megabytes of
	;; the system's memory.  This is so that the loader can operate
	;; normally after paging has been enabled.  This is 1024 entries,
	;; each one representing 4Kb of real memory.  We will start the table
	;; at the address (LDRPAGINGDATA + 1000h)

	mov EBX, 0						; Location we're mapping
	mov ECX, 1024					; 1024 entries
	mov EDI, (LDRPAGINGDATA + 1000h)

	.entryLoop1:
	;; Make one page table entry.
	mov EAX, EBX
	and AX, 0F000h					; Clear bits 0-11, just in case
	;; Set the entry's page present bit, the writable bit, and the
	;; write-through bit.
	or AL, 00001011b
	stosd							; Store the page table entry at ES:EDI
	add EBX, 1000h					; Move to next 4Kb
	loop .entryLoop1

	;; Create a page table to represent the virtual address space that
	;; the kernel's code will occupy.  Start this table at address
	;; (PAGINGDATA + 2000h)

	mov EBX, KERNELLOADADDRESS		; Location we're mapping
	mov ECX, 1024					; 1024 entries
	mov EDI, (LDRPAGINGDATA + 2000h) ; location in LDRPAGINGDATA

	.entryLoop2:
	;; Make one page table entry.
	mov EAX, EBX
	and AX, 0F000h					; Clear bits 0-11, just in case
	;; Set the entry's page present bit, the writable bit, and the
	;; write-through bit.
	or AL, 00001011b
	stosd							; Store the page table entry at ES:EDI
	add EBX, 1000h					; Move to next 4Kb
	loop .entryLoop2

	;; We will create a master page directory with two entries
	;; representing the page tables we created.  The master page
	;; directory will start at PAGINGDATA

	;; Make sure there are NULL entries throughout the table
	;; to start
	xor EAX, EAX
	mov ECX, 1024
	mov EDI, LDRPAGINGDATA
	rep stosd

	;; The first entry we need to create in this table represents
	;; the first page table we made, which identity-maps the first
	;; 4 megs of address space.  This will be the first entry in our
	;; new table.
	;; The address of the first table
	mov EAX, (LDRPAGINGDATA + 1000h)
	and AX, 0F000h					; Clear bits 0-11, just in case
	;; Set the entry's page present bit, the writable bit, and the
	;; write-through bit.
	or AL, 00001011b
	;; Put it in the first entry
	mov EDI, LDRPAGINGDATA
	stosd

	;; We make the second entry based on the virtual address of the
	;; kernel.
	;; The address of the second table
	mov EAX, (LDRPAGINGDATA + 2000h)
	and AX, 0F000h					; Clear bits 0-11, just in case
	;; Set the entry's page present bit, the writable bit, and the
	;; write-through bit.
	or AL, 00001011b
	mov EDI, KERNELVIRTUALADDRESS	; Kernel's virtual address
	;; We shift right by 22, to make it a multiple of 4 megs, but then
	;; we shift it right again by 2, since the offsets of entries in the
	;; table are multiples of 4 bytes
	shr EDI, 20
	;; Add the offset of the table
	add EDI, LDRPAGINGDATA
	stosd

	;; Move the base address of the master page directory into CR3
	xor EAX, EAX					; CR3 supposed to be zeroed
	or AL, 00001000b				; Set the page write-through bit
	;; The address of the directory
	mov EBX, LDRPAGINGDATA
	and EBX, 0FFFFF800h				; Clear bits 0-10, just in case
	or EAX, EBX						; Combine them into the new CR3
	mov CR3, EAX

	;; Make sure caching is not globally disabled
	mov EAX, CR0
	and EAX, 9FFFFFFFh				; Clear CD (30) and NW (29)
	mov CR0, EAX

	;; Clear out the page cache before we turn on paging, since if
	;; we don't, rebooting from Windows or other OSes can cause us to
	;; crash
	wbinvd
	invd

	;; Here we go.  Turn on paging in the processor.
	or EAX, 80000000h
	mov CR0, EAX

	;; Supposed to do a near jump after all that
	jmp near .pagingOn
	nop

	.pagingOn:
	;; Enable 'global' pages for processors that support it
	mov EAX, CR4
	or EAX, 00000080h
	mov CR4, EAX

	.done:
	;; Done
	popa
	ret

	BITS 16


loaderMemCopy:
	;; Tries to use real mode interrupt 15h:87h to move data in extended
	;; memory.  If that doesn't work it tries a 'big real mode' method.
	;; Proto:
	;;   word loaderMemCopy(dword *src, dword *dest, dword size);

	;; Save a word for our return code
	push word 0

	pusha

	;; Save the stack pointer
	mov BP, SP

	push DS
	push ES

	mov EAX, CS
	mov DS, EAX
	mov ES, EAX

	;; Using this method we can only transfer 64K at a time.
	.copyLoop:

	mov ECX, dword [SS:(BP + 28)]	; Size in bytes
	cmp ECX, 10000h
	jb .noReduce
	mov ECX, 10000h
	.noReduce:

	;; Set up the temporary GDT

	;; Source descriptor
	mov EBX, dword [SS:(BP + 20)]	; Source address
	mov word [TMPGDT.srclow], BX	; Source address low word
	shr EBX, 16
	mov byte [TMPGDT.srcmid], BL	; Source address 3rd byte
	mov byte [TMPGDT.srchi], BH		; Source address 4th byte

	;; Destination descriptor
	mov EBX, dword [SS:(BP + 24)]	; Dest address
	mov word [TMPGDT.dstlow], BX	; Dest address low word
	shr EBX, 16
	mov byte [TMPGDT.dstmid], BL	; Dest address 3rd byte
	mov byte [TMPGDT.dsthi], BH		; Dest address 4th byte

	push ECX
	mov AX, 8700h
	shr ECX, 1						; Size is in words
	jnc .noAdd
	add ECX, 1						; Don't round down though...
	.noAdd:
	mov SI, TMPGDT
	int 15h
	pop ECX

	jnc .success

	;; Failed
	mov word [SS:(BP + 16)], -1
	jmp .out

	.success:
	;; Success.  Any more to do?
	add dword [SS:(BP + 20)], ECX	; Adjust src address
	add dword [SS:(BP + 24)], ECX	; Adjust dest address
	sub dword [SS:(BP + 28)], ECX	; Adjust count
	jnz .copyLoop

	.out:
	pop ES
	pop DS
	popa
	pop AX
	ret


loaderMemSet:
	;; Uses a 'big real mode' method for initializing a memory region.
	;; Proto:
	;;   void loaderMemSet(word byte_value, dword *dest, dword size);

	pusha

	;; Save the stack pointer
	mov BP, SP

	;; Use our loader data buffer to copy the value.  First we need to
	;; fill it.
	mov EBX, dword [SS:(BP + 24)]	; Size in bytes
	;; Don't overrun the buffer
	cmp EBX, DATABUFFERSIZE
	jb .noReduce1
	mov EBX, DATABUFFERSIZE
	.noReduce1:
	;; Don't overrun the real-mode segment
	cmp EBX, 0FFFFh
	jb .noReduce2
	mov EBX, 0FFFFh
	.noReduce2:
	sub EBX, (LDRDATABUFFER % 16)

	cli								; Disable ints
	push ES							; Preserve ES
	mov EAX, (LDRDATABUFFER / 16)	; Segment
	mov ES, EAX						;
	mov DI, (LDRDATABUFFER % 16)	; Offset
	mov CX, BX						; Count
	mov AX, word [SS:(BP + 18)]		; Value in AL
	rep stosb						; Fill the buffer
	pop ES							; Restore ES
	sti								; Re-enable ints

	;; Use the loaderMemCopy function as many times as necessary to
	;; set the value

	.copyLoop:
	mov ECX, dword [SS:(BP + 24)]	; Size in bytes
	;; Reduce size if necessary
	cmp ECX, EBX
	jb .noReduce3
	mov ECX, EBX
	.noReduce3:
	push dword ECX
	push dword [SS:(BP + 20)]		; Dest address
	push dword LDRDATABUFFER		; Src address
	call loaderMemCopy
	add SP, 12

	add dword [SS:(BP + 20)], ECX	; Adjust dest address
	sub dword [SS:(BP + 24)], ECX	; Adjust count
	jnz .copyLoop

	popa
	ret


;;
;; The data segment
;;

	SEGMENT .data
	ALIGN 4

;;
;; Things generated internally
;;

KERNELSIZE	dd 0
PRINTINFO	dw 0	;; Show hardware information messages?
FATALERROR	db 0 	;; Fatal error encountered?

;;
;; Info about our boot device and filesystem.
;;
	ALIGN 4

BYTESPERSECT	dw 512
ROOTDIRENTS		dw 0
ROOTDIRCLUST	dd 0
FSTYPE			dw 0
RESSECS			dw 0
FATSECS			dd 0
TOTALSECS		dd 0
CYLINDERS		dd 0
HEADS			dd 0
SECPERTRACK		dd 0
SECPERCLUST		dw 0
FATS			dw 0
DRIVENUMBER		dw 0
HDDINFO			times 42h  db 0	;; Space for info ret by EBIOS

;;
;; Tables, desciptors, etc., used for protected mode
;;
	ALIGN 4

GDTSTART	dw GDTLENGTH
		dd (LDRCODESEGMENTLOCATION + dummy_desc)
dummy_desc	times 8 db 0		;; The empty first descriptor
allcode_desc	dw 0FFFFh
		dw 0
		db 0
		db PRIV_CODEINFO1
		db PRIV_CODEINFO2
		db 0
alldata_desc	dw 0FFFFh
		dw 0
		db 0
		db PRIV_DATAINFO1
		db PRIV_DATAINFO2
		db 0
allstck_desc	dw 0FFFFh
		dw 0
		db 0
		db PRIV_STCKINFO1
		db PRIV_STCKINFO2
		db 0
ldrcode_desc	dw LDRCODESEGMENTSIZE
		dw (LDRCODESEGMENTLOCATION & 0FFFFh)
		db ((LDRCODESEGMENTLOCATION & 00FF0000h) >> 16)
		db LDRCODEINFO1
		db LDRCODEINFO2
		db ((LDRCODESEGMENTLOCATION & 0FF000000h) >> 24)
GDTLENGTH	equ $-dummy_desc

	ALIGN 4

;; This temporary GDT is for int 15h calls to move data into high memory
;; in the loaderMemCopy() function.
TMPGDT:
	TMPGDT.empty1	times 16 db 0	; empty (used by BIOS)
	TMPGDT.srclenlo dw 0FFFFh	; source segment length in bytes
	TMPGDT.srclow	dw 0		; low word of linear source address
	TMPGDT.srcmid	db 0		; middle byte of linear source address
					db 93h		; source segment access rights
	TMPGDT.srclenhi	db 0Fh		; source extended access rights
	TMPGDT.srchi	db 0		; high byte of source address
	TMPGDT.dstlenlo dw 0FFFFh	; dest segment length in bytes
	TMPGDT.dstlow	dw 0		; low word of linear dest address
	TMPGDT.dstmid	db 0		; middle byte of linear dest address
					db 93h		; dest segment access rights
	TMPGDT.dstlenhi	db 0Fh		; dest extended access rights
	TMPGDT.dsthi	db 0		; high byte of dest address
	TMPGDT.empty2	times 16 db 0	; empty (used by BIOS)
	TMPGDT.empty3	times 16 db 0	; empty (used by BIOS)

;;
;; The good/informational messages
;;

HAPPY		db 01h, ' ', 0
BLANK		db '               ', 10h, ' ', 0
LOADMSG1	db 'Visopsys BIOS OS Loader v0.83', 0
LOADMSG2	db 'Copyright (C) 1998-2018 J. Andrew McLaughlin', 0
BOOTDEV1	db 'Boot device  ', 10h, ' ', 0
BOOTFLOPPY	db 'fd', 0
BOOTHDD		db 'hd', 0
BOOTDEV2	db ', ', 0
BOOTHEADS	db ' heads, ', 0
BOOTCYLS	db ' cyls, ', 0
BOOTSECTS	db ' sects, type: ', 0
FAT12MES	db 'FAT12', 0
FAT16MES	db 'FAT16', 0
FAT32MES	db 'FAT32', 0
UNKNOWNFS	db 'UNKNOWN', 0
LOADING		db 'Loading Visopsys', 0
PRESSREBOOT	db 'Press any key to reboot.', 0
REBOOTING	db '  ...Rebooting', 0
BOOTINFO	db 'BOOTINFO   ', 0
MENUMSG		db 'Boot Menu', 0

;;
;; The error messages
;;

SAD			db 'x ', 0
BIOSERR		db 'The computer', 27h, 's BIOS was unable to provide information '
			db 'about', 0
BIOSERR2	db 'the boot device.  Please check the BIOS for errors.', 0
FATALERROR1	db ' unrecoverable error(s) were recorded, and the boot process '
			db 'cannot continue.', 0
FATALERROR2	db 'Any applicable error information is noted above.  Please '
			db 'attempt to rectify', 0
FATALERROR3	db 'these problems before retrying.', 0

