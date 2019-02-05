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
;;  loaderLoadKernel.s
;;

	GLOBAL loaderLoadKernel
	GLOBAL KERNELENTRY

	EXTERN loaderLoadFile
	EXTERN loaderMemCopy
	EXTERN loaderMemSet
	EXTERN loaderPrint
	EXTERN loaderPrintNewline
	EXTERN loaderPrintNumber

	EXTERN KERNELSIZE
	EXTERN FILEDATABUFFER

	SEGMENT .text
	BITS 16
	ALIGN 4

	%include "loader.h"


getElfHeaderInfo:
	;; This function checks the ELF header of the kernel file and
	;; saves the relevant information about the file.

	;; Save a word for our return code
	sub SP, 2

	;; Save regs
	pusha

	;; Save the stack register
	mov BP, SP

	;; Copy the first sector of the ELF header into the file data buffer
	push dword 512
	push dword [FILEDATABUFFER]
	push dword KERNELLOADADDRESS
	call loaderMemCopy
	add SP, 12

	push ES
	mov EAX, dword [FILEDATABUFFER]
	shr EAX, 4
	mov ES, EAX

	;; Make sure it's an ELF file (check the magic number)
	mov ESI, 0
	mov EAX, dword [ES:ESI]
	cmp EAX, 464C457Fh	; ELF magic number (7Fh, 'ELF')
	je .isElf

	;; The kernel was not an ELF binary
	call loaderPrintNewline
	call loaderPrintNewline
	call loaderPrintNewline
	mov DL, BADCOLOR
	mov SI, THEFILE
	call loaderPrint
	mov SI, NOTELF
	call loaderPrint
	call loaderPrintNewline
	mov SI, REINSTALL
	call loaderPrint
	call loaderPrintNewline
	mov word [SS:(BP + 16)], -1
	jmp .done

	.isElf:

	;; It's an ELF binary.  We will skip doing exhaustive checks, as we
	;; would do in the case of loading some user binary.  We will,
	;; however, make sure that it's an executable ELF binary
	mov ESI, 16
	mov AX, word [ES:ESI]
	cmp AX, 2		; ELF executable file type
	je .isExec

	;; The kernel was not executable
	call loaderPrintNewline
	call loaderPrintNewline
	call loaderPrintNewline
	mov DL, BADCOLOR
	mov SI, THEFILE
	call loaderPrint
	mov SI, NOTEXEC
	call loaderPrint
	call loaderPrintNewline
	mov SI, REINSTALL
	call loaderPrint
	call loaderPrintNewline
	mov word [SS:(BP + 16)], -2
	jmp .done

	.isExec:
	;; Cool.  Now we start parsing the header, collecting any info
	;; that we care about.

	;; First the kernel entry point.
	mov ESI, 24
	mov EAX, dword [ES:ESI]
	mov dword [KERNELENTRY], EAX

	;; Now the offset of the program headers
	mov ESI, 28
	mov EAX, dword [ES:ESI]
	mov dword [PROGHEADERS], EAX

	;; That is all the information we get directly from the ELF header.
	;; Now, we need to get information from the program headers.

	;; Find the code.
	mov ESI, dword [PROGHEADERS]

	;; Loop until we find a header with a segment type PT_LOAD (1) and
	;; flags indicating that the segment is read-only and executable
	.codeFind:
	mov EAX, dword [ES:ESI]
	cmp EAX, 1 		; PT_LOAD
	je .codeCheckFlags
	add ESI, 32
	jmp .codeFind

	.codeCheckFlags:
	mov EAX, [ES:ESI + 24]
	cmp EAX, 05h		; 04h=read 01h=execute
	je .codeFound
	add ESI, 32
	jmp .codeFind

	.codeFound:
	;; Get the code offset
	mov EAX, dword [ES:ESI + 4]
	mov dword [CODE_OFFSET], EAX

	;; Get the code virtual address
	mov EAX, dword [ES:ESI + 8]
	mov dword [CODE_VIRTADDR], EAX

	;; Get the code size in the file
	mov EAX, dword [ES:ESI + 16]
	mov dword [CODE_SIZEINFILE], EAX

	;; Make sure the size in memory is the same in the file
	mov EAX, dword [ES:ESI + 20]
	cmp EAX, dword [CODE_SIZEINFILE]
	je .codeCheckAlign

	;; The kernel image doesn't look the way we expected.  This program
	;; isn't versatile enough to handle that yet.
	call loaderPrintNewline
	call loaderPrintNewline
	call loaderPrintNewline
	mov DL, BADCOLOR
	mov SI, THEFILE
	call loaderPrint
	mov SI, SEGLAYOUT
	call loaderPrint
	call loaderPrintNewline
	mov SI, REINSTALL
	call loaderPrint
	call loaderPrintNewline
	mov word [SS:(BP + 16)], -3
	jmp .done

	.codeCheckAlign:
	;; Check the alignment.  Must be 4096 (page size)
	mov EAX, dword [ES:ESI + 28]
	cmp EAX, 4096
	je .dataFind

	;; The kernel image doesn't look the way we expected.  This program
	;; isn't versatile enough to handle that yet.
	call loaderPrintNewline
	call loaderPrintNewline
	call loaderPrintNewline
	mov DL, BADCOLOR
	mov SI, THEFILE
	call loaderPrint
	mov SI, SEGALIGN
	call loaderPrint
	call loaderPrintNewline
	mov SI, REINSTALL
	call loaderPrint
	call loaderPrintNewline
	mov word [SS:(BP + 16)], -4
	jmp .done

	;; Now the data segment.
	mov ESI, dword [PROGHEADERS]

	;; Loop until we find a header with a segment type PT_LOAD (1) and
	;; flags indicating that the segment is read-write
	.dataFind:
	mov EAX, dword [ES:ESI]
	cmp EAX, 1 		; PT_LOAD
	je .dataCheckFlags
	add ESI, 32
	jmp .dataFind

	.dataCheckFlags:
	mov EAX, [ES:ESI + 24]
	cmp EAX, 06h		; 04h=read 02h=write
	je .dataFound
	add ESI, 32
	jmp .dataFind

	.dataFound:
	;; Get the data offset
	mov EAX, dword [ES:ESI + 4]
	mov dword [DATA_OFFSET], EAX

	;; Get the data virtual address
	mov EAX, dword [ES:ESI + 8]
	mov dword [DATA_VIRTADDR], EAX

	;; Get the data size in the file
	mov EAX, dword [ES:ESI + 16]
	mov dword [DATA_SIZEINFILE], EAX

	;; Get the data size in memory
	mov EAX, dword [ES:ESI + 20]
	mov dword [DATA_SIZEINMEM], EAX

	;; Check the alignment.  Must be 4096 (page size)
	mov EAX, dword [ES:ESI + 28]
	cmp EAX, 4096
	je .success

	;; The kernel image doesn't look the way we expected.  This program
	;; isn't versatile enough to handle that yet.
	call loaderPrintNewline
	call loaderPrintNewline
	call loaderPrintNewline
	mov DL, BADCOLOR
	mov SI, THEFILE
	call loaderPrint
	mov SI, SEGALIGN
	call loaderPrint
	call loaderPrintNewline
	mov SI, REINSTALL
	call loaderPrint
	call loaderPrintNewline
	mov word [SS:(BP + 16)], -5
	jmp .done

	.success:
	;; Make 0 be our return code
	mov word [SS:(BP + 16)], 0

	.done:
	pop ES
	popa
	;; Pop our return code.
	xor EAX, EAX
	pop AX
	ret


layoutKernel:
	;; This function takes information about the kernel ELF file
	;; sections and modifies the kernel image appropriately in memory.

	;; Save regs
	pusha

	;; Save the stack register
	mov BP, SP

	;; We will do layout for two segments; the code and data segments
	;; (the getElfHeaderInfo() function should have caught any deviation
	;; from that state of affairs).

	;; For the code segment, we simply place it at the hard-coded load
	;; location (which may be different from the entry point).  Then,
	;; all we do is move all code forward by CODE_OFFSET bytes.
	;; This will have the side effect of deleting the ELF header and
	;; program header from memory.

	mov ECX, dword [CODE_SIZEINFILE]
	mov ESI, KERNELLOADADDRESS
	add ESI, dword [CODE_OFFSET]
	mov EDI, KERNELLOADADDRESS

	push ECX
	push EDI
	push ESI
	call loaderMemCopy
	add SP, 12

	;; We do the same operation for the data segment, except we have to
	;; first make sure that the difference between the code and data's
	;; virtual address is the same as the difference between the offsets
	;; in the file.
	mov EAX, dword [DATA_VIRTADDR]
	sub EAX, dword [CODE_VIRTADDR]
	cmp EAX, dword [DATA_OFFSET]
	je .okDataOffset

	;; The kernel image doesn't look exactly the way we expected, but
	;; that can happen depending on which linker is used.  We can adjust
	;; it.  Move the initialized data forward from the original offset
	;; so that it matches the difference between the code and data's
	;; virtual addresses.
	mov ECX, dword [DATA_SIZEINFILE]
	mov ESI, KERNELLOADADDRESS
	add ESI, dword [DATA_OFFSET]
	mov EDI, dword [DATA_VIRTADDR]
	sub EDI, dword [CODE_VIRTADDR]
	mov dword [DATA_OFFSET], EDI	;; This will be different now
	add EDI, KERNELLOADADDRESS

	push ECX
	push EDI
	push ESI
	call loaderMemCopy
	add SP, 12

	.okDataOffset:
	;; We need to zero out the memory that makes up the difference
	;; between the data's file size and its size in memory.
	mov ECX, dword [DATA_SIZEINMEM]
	sub ECX, dword [DATA_SIZEINFILE]
	mov EDI, KERNELLOADADDRESS
	add EDI, dword [DATA_OFFSET]
	add EDI, dword [DATA_SIZEINFILE]

	push ECX
	push EDI
	push word 0
	call loaderMemSet
	add SP, 10

	popa
	ret


evaluateLoadError:
	;; This function takes an error code as its only parameter, and
	;; prints the appropriate error message

	;; Save regs
	pusha

	;; Save the stack register
	mov BP, SP

	;; Use the error color
	call loaderPrintNewline
	mov DL, BADCOLOR
	mov SI, LOADFAIL
	call loaderPrint

	;; Get the error code
	mov AX, word [SS:(BP + 18)]

	;; Was there an error loading the directory?
	cmp AX, -1
	jne .errorFIND

	;; There was an error loading the directory.
	mov SI, NODIRECTORY
	call loaderPrint
	call loaderPrintNewline
	mov SI, CORRUPTFS1
	call loaderPrint
	call loaderPrintNewline
	mov SI, CORRUPTFS2
	call loaderPrint
	call loaderPrintNewline
	jmp .done

	.errorFIND:
	;; Was there an error finding the kernel file in the directory?
	cmp AX, -2
	jne .errorFAT

	;; The kernel file could not be found.
	mov SI, THEFILE
	call loaderPrint
	call loaderPrintNewline
	mov SI, NOFILE1
	call loaderPrint
	call loaderPrintNewline
	mov SI, NOFILE2
	call loaderPrint
	call loaderPrintNewline
	jmp .done

	.errorFAT:
	;; Was there an error loading the FAT table?
	cmp AX, -3
	jne .errorFILE

	;; The FAT table could not be read
	mov SI, NOFAT
	call loaderPrint
	call loaderPrintNewline
	mov SI, CORRUPTFS1
	call loaderPrint
	call loaderPrintNewline
	mov SI, CORRUPTFS2
	call loaderPrint
	call loaderPrintNewline
	jmp .done

	.errorFILE:
	;; Was there an error loading the kernel file itself?
	cmp AX, -4
	jne .errorUNKNOWN

	mov SI, THEFILE
	call loaderPrint
	call loaderPrintNewline
	mov SI, BADFILE
	call loaderPrint
	call loaderPrintNewline
	mov SI, CORRUPTFS1
	call loaderPrint
	call loaderPrintNewline
	mov SI, CORRUPTFS2
	call loaderPrint
	call loaderPrintNewline
	jmp .done

	.errorUNKNOWN:
	;; We should really have a proper error message for this.
	mov SI, UNKNOWN
	call loaderPrint
	call loaderPrintNewline

	.done:
	popa
	ret


loaderLoadKernel:
	;; This function is in charge of loading the kernel file and
	;; setting it up for execution.  This is designed to load the
	;; kernel as an ELF binary.  First, it sets up to call the
	;; loaderLoadFile routine with the correct parameters.  If there
	;; is an error, it can print an informative error message about
	;; the problem that was encountered (based on the error code from
	;; the loaderLoadFile function).  Next, it performs functions
	;; like that of any other 'loader': it examines the ELF header
	;; of the file, does any needed memory spacing as specified therein
	;; (such as moving segments around and creating data segments)

	;; Save a word on the stack for our return value
	push word 0

	;; Save regs
	pusha

	;; Save the stack register
	mov BP, SP

	;; Load the kernel file
	push word 1		; Show progress indicator
	push dword KERNELLOADADDRESS
	push word KERNELNAME
	call loaderLoadFile
	add SP, 8

	;; Make sure the load was successful
	cmp EAX, 0
	jge near .okLoad

	;; We failed to load the kernel.  The following call will determine
	;; the type of error encountered while loading the kernel, and print
	;; a helpful error message about the reason.
	push AX
	call evaluateLoadError
	add SP, 2

	;; Quit
	mov word [SS:(BP + 16)], -1
	jmp .done

	.okLoad:
	;; We were successful.  The kernel's size is in AX.  Ignore it.

	;; Now we need to examine the elf header.
	call getElfHeaderInfo

	;; Make sure the evaluation was successful
	cmp AX, 0
	jge near .okEval

	;; The kernel image is not what we expected.  Return the error
	;; code from the call
	mov word [SS:(BP + 16)], AX
	jmp .done

	.okEval:
	;; OK, call the routine to create the proper layout for the kernel
	;; based on the ELF information we gathered
	call layoutKernel

	;; Set the size of the kernel image, which is the combined memory
	;; size of the code and data segments.  Return 0
	mov EAX, dword [CODE_SIZEINFILE]
	add EAX, dword [DATA_SIZEINMEM]
	;; Make it the next multiple of 4K
	add EAX, 00001000h
	and EAX, 0FFFFF000h
	mov dword [KERNELSIZE], EAX
	mov word [SS:(BP + 16)], 0

	.done:
	;; Restore regs
	popa
	;; Pop our return code
	pop AX
	ret


;;
;; The data segment
;;

	SEGMENT .data
	ALIGN 4

KERNELENTRY		dd 0
PROGHEADERS		dd 0
CODE_OFFSET		dd 0
CODE_VIRTADDR	dd 0
CODE_SIZEINFILE	dd 0
DATA_OFFSET		dd 0
DATA_VIRTADDR	dd 0
DATA_SIZEINFILE	dd 0
DATA_SIZEINMEM	dd 0

KERNELNAME	db 'VISOPSYS   ', 0

;;
;; The error messages
;;

LOADFAIL	db 'Loading the Visopsys kernel failed.  ', 0
NODIRECTORY	db 'The root directory could not be read.', 0
NOFAT		db 'The FAT table could not be read.', 0
THEFILE		db 'The kernel file ', 27h, 'visopsys', 27h, 0
NOFILE1		db 'could not be found.', 0
NOFILE2		db 'Please make sure that this file exists in the root directory.', 0
BADFILE		db 'could not be read.', 0
CORRUPTFS1	db 'The filesystem on the boot device may be corrupt:  you should use a disk', 0
CORRUPTFS2	db 'utility to check the integrity of the filesystem and the Visopsys files.', 0
NOTELF		db ' is not an ELF binary.', 0
NOTEXEC		db ' is not executable.', 0
SEGALIGN	db ' has incorrectly aligned ELF segments.', 0
SEGLAYOUT	db ' has an incorrect ELF segment layout.', 0
REINSTALL	db 'You will probably need to reinstall Visopsys on this boot media.', 0
UNKNOWN		db 'The error code is unknown.', 0

