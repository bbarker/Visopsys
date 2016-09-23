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
;;  loaderVideo.s
;;

	EXTERN loaderPrint
	EXTERN loaderPrintNumber
	EXTERN loaderPrintNewline
	EXTERN loaderFindFile
	EXTERN loaderLoadFile
	EXTERN PRINTINFO
	EXTERN FILEDATABUFFER

	GLOBAL loaderSetTextDisplay
	GLOBAL loaderDetectVideo
	GLOBAL loaderQueryGraphicMode
	GLOBAL loaderSetGraphicDisplay
	GLOBAL KERNELGMODE
	GLOBAL CURRENTGMODE

	SEGMENT .text
	BITS 16
	ALIGN 4

	%include "loader.h"


enumerateGraphicsModes:
	;; This function will enumerate graphics modes based on which ones are
	;; available with this video card, and are suitable for our use.

	pusha

	;; Per VESA specs we make no assumptions about mode numbers; we ask for
	;; resolutions and bit depths, and the card tells us the mode number if
	;; it's supported.

	xor CX, CX	; Saves number of available modes
	push CX

	;; We need to get a pointer to a list of graphics mode numbers from the
	;; VBE.  We can gather this from the VESA info block retrieved earlier by
	;; the video hardware detection code.

	;; Get the offset now, and the segment inside the loop.
	mov SI, [VESAINFO + 0Eh]

	;; Do a loop through the supported modes
	.modeLoop:

	;; Save ES
	push ES

	;; Now get the segment of the pointer, as mentioned above
	mov AX, [VESAINFO + 10h]
	mov ES, AX

	;; ES:SI is now a pointer to a word list of supported modes,
	;; terminated with the value FFFFh

	;; Get the first/next mode number
	mov DX, word [ES:SI]

	;; Restore ES
	pop ES

	;; Is it the end of the mode number list?
	cmp DX, 0FFFFh
	je .done

	;; Increment the pointer for the next loop
	add SI, 2

	;; We have a mode number.  Now we need to do a VBE call to determine
	;; whether this mode number suits our needs.  This call will put a bunch
	;; of info in the buffer pointed to by ES:DI
	mov CX, DX
	mov AX, 4F01h
	mov DI, MODEINFO
	int 10h

	;; Make sure the function call is supported
	cmp AL, 4Fh
	jne .done
	;; Is the mode supported by this call?
	cmp AH, 00h
	jne .modeLoop

	;; We need to look for a few features to determine whether this is a mode
	;; we want.  It needs to be supported, and it needs to be a graphics mode
	;; with linear framebuffer support.

	;; Get the first word of the buffer
	mov AX, word [MODEINFO]

	;; Is the mode supported?
	bt AX, 0
	jnc .modeLoop

	;; Is this mode a graphics mode?
	bt AX, 4
	jnc .modeLoop

	;; Does this mode support a linear frame buffer?
	bt AX, 7
	jnc .modeLoop

	;; Is this mode at least 640x480?
	mov AX, word [MODEINFO + 12h]
	cmp AX, 640
	jl .modeLoop
	mov AX, word [MODEINFO + 14h]
	cmp AX, 480
	jl .modeLoop

	;; Is this mode more than 8 bits per pixel?
	mov AL, byte [MODEINFO + 19h]
	cmp AL, 8
	jle .modeLoop

	;; Get the correct list address in DI
	pop CX
	push CX
	mov DI, CX
	shl DI, 4
	add DI, word [VIDEODATA_P]
	add DI, graphicsInfoBlock.supportedModes

	xor EAX, EAX
	mov AX, DX
	mov dword [DI], EAX			; Mode number
	mov AX, word [MODEINFO + 12h]
	mov dword [DI + 4], EAX		; X resolution
	mov AX, word [MODEINFO + 14h]
	mov dword [DI + 8], EAX		; Y resolution
	xor AX, AX
	mov AL, byte [MODEINFO + 19h]
	mov dword [DI + 12], EAX	; Bits per pixel

	;; Increment the number of acceptable modes
	pop CX
	inc CX
	push CX

	cmp CX, MAXVIDEOMODES
	jl .modeLoop

	.done:
	;; Save the number of acceptable modes
	xor ECX, ECX
	pop CX
	mov DI, word [VIDEODATA_P]
	mov dword [DI + graphicsInfoBlock.numberModes], ECX

	popa
	ret


selectGraphicMode:
	;; This function will select a graphics mode for the kernel.  The algorithm
	;; is to first search for a fallback mode; the biggest screen area less
	;; than 1500 in width if possible.  Then, we look for the highest aspect
	;; ratio of all modes greater than 800x600, *hopefully* identifying the
	;; 'native' aspect ratio.  Finally, we try to find the biggest screen area
	;; with this aspect ratio that's less than 1500 in width (but we'll accept
	;; bigger).  If we don't choose one with the ideal aspect ratio, we use the
	;; fallback.

	;; The original C version of this code is preserved in
	;; src/programs/disprops.c if we ever need to debug the algorithm

	pusha

	;; Save the stack pointer
	mov BP, SP

	;; Save some space for local variables
	push dword 0	; [SS:(BP - 4)]		bestScreenArea
	push word 0		; [SS:(BP - 6)]		fallbackMode
	push dword 0	; [SS:(BP - 10)]	bestAspect

	;; Loop through the enumerated modes, and try to find a fallback mode

	xor ECX, ECX					; counter
	mov DI, 0						; fallback mode

	.fallbackLoop:
	;; Get the correct list address in SI
	mov SI, CX
	shl SI, 4
	add SI, word [VIDEODATA_P]
	add SI, graphicsInfoBlock.supportedModes

	;; Less than 640x480?
	cmp dword [SI + 4], 640			; X res
	jb .nextFallback
	cmp dword [SI + 8], 480			; Y res
	jb .nextFallback

	;; If we have some fallback, and X res > 1500, skip it
	cmp DI, 0
	je .noFallback
	cmp dword [SI + 4], 1500
	ja .nextFallback
	.noFallback:

	;; Calculate the screen area of this mode
	xor EDX, EDX
	mov EAX, dword [SI + 4]			; X res
	mov EBX, dword [SI + 8]			; Y res
	mul EBX

	;; If we have no fallback, or the fallback X res > 1500, or the screen
	;; area is the highest one (or higher BPP), remember this one
	cmp DI, 0
	je .saveFallback
	cmp dword [DI + 4], 1500		; X res
	je .saveFallback
	cmp EAX, dword [SS:(BP - 4)]	; screen area vs. best screen area
	ja .saveFallback
	cmp EAX, dword [SS:(BP - 4)]	; screen area vs. best screen area
	jae .maybeSaveFallback
	jmp .nextFallback
	.maybeSaveFallback:
	mov EBX, dword [SI + 12]		; bits per pixel
	cmp EBX, dword [DI + 12]		; bits per pixel
	jbe .nextFallback

	.saveFallback:
	;; Save this one
	mov dword [SS:(BP - 4)], EAX	; best screen area
	mov DI, SI

	.nextFallback:
	inc CX
	mov SI, word [VIDEODATA_P]
	cmp ECX, dword [SI + graphicsInfoBlock.numberModes]
	jb .fallbackLoop

	;; Did we find any reasonable fallback mode?
	cmp DI, 0
	je near .fail
	mov word [SS:(BP - 6)], DI		; fallback mode

	;; Loop through the enumerated modes, and try to find the best aspect ratio

	xor ECX, ECX					; counter

	.aspectLoop:
	;; Get the correct list address in SI
	mov SI, CX
	shl SI, 4
	add SI, word [VIDEODATA_P]
	add SI, graphicsInfoBlock.supportedModes

	;; Less than 800x600?
	cmp dword [SI + 4], 800			; X res
	jb .nextAspect
	cmp dword [SI + 8], 600			; Y res
	jb .nextAspect

	;; Calculate this mode's aspect ratio
	xor EDX, EDX
	mov EAX, dword [SI + 4]			; X res
	shl EAX, 8
	mov EBX, dword [SI + 8]			; Y res
	div EBX

	;; Biggest so far?
	cmp EAX, dword [SS:(BP - 10)]	; best aspect
	jb .nextAspect

	;; Save this one
	mov dword [SS:(BP - 10)], EAX	; best aspect

	.nextAspect:
	inc CX
	mov SI, word [VIDEODATA_P]
	cmp ECX, dword [SI + graphicsInfoBlock.numberModes]
	jb .aspectLoop

	;; Loop again, looking for the mode with this aspect ratio that has the
	;; biggest screen area

	xor ECX, ECX					; counter
	mov DI, 0						; best mode
	mov dword [SS:(BP - 4)], 0		; best screen area

	.bestLoop:
	;; Get the correct list address in SI
	mov SI, CX
	shl SI, 4
	add SI, word [VIDEODATA_P]
	add SI, graphicsInfoBlock.supportedModes

	;; Calculate this mode's aspect ratio
	xor EDX, EDX
	mov EAX, dword [SI + 4]			; X res
	shl EAX, 8
	mov EBX, dword [SI + 8]			; Y res
	div EBX

	;; Same as best aspect?
	cmp EAX, dword [SS:(BP - 10)]	; best aspect
	jne .nextBest

	;; If we have some best, and X res > 1500, skip it
	cmp dword [SI + 4], 1500		; X res
	jbe .smallBest
	cmp DI, 0
	jne .nextBest
	.smallBest:

	;; Calculate the screen area of this mode
	xor EDX, EDX
	mov EAX, dword [SI + 4]			; X res
	mov EBX, dword [SI + 8]			; Y res
	mul EBX

	;; If we have no best, or the best X res > 1500, or the screen area is the
	;; highest one (or higher BPP), remember this one
	cmp DI, 0
	je .saveBest
	cmp dword [DI + 4], 1500		; X res
	ja .saveBest
	cmp EAX, dword [SS:(BP - 4)]	; screen area vs. best screen area
	ja .saveBest
	cmp EAX, dword [SS:(BP - 4)]	; screen area vs. best screen area
	jae .maybeSaveBest
	jmp .nextBest
	.maybeSaveBest:
	mov EBX, dword [SI + 12]		; bits per pixel
	cmp EBX, dword [DI + 12]		; bits per pixel
	jbe .nextBest

	.saveBest:
	mov dword [SS:(BP - 4)], EAX	; best screen area
	mov DI, SI

	.nextBest:
	inc CX
	mov SI, word [VIDEODATA_P]
	cmp ECX, dword [SI + graphicsInfoBlock.numberModes]
	jb .bestLoop

	;; Did we find a best mode?
	cmp DI, 0
	jne .haveBest
	;; Use fallback
	mov DI, word [SS:(BP - 6)]		; fallback mode

	.haveBest:
	;; We chose a mode
	mov SI, DI
	mov DI, word [VIDEODATA_P]

	mov EAX, dword [SI + 0]			; mode number
	mov dword [DI + graphicsInfoBlock.mode], EAX

	;; Save it here too
	mov word [KERNELGMODE], AX

	mov EAX, dword [SI + 4]			; X res
	mov dword [DI + graphicsInfoBlock.xRes], EAX

	mov EAX, dword [SI + 8]			; Y res
	mov dword [DI + graphicsInfoBlock.yRes], EAX

	mov EAX, dword [SI + 12]		; bits per pixel
	mov dword [DI + graphicsInfoBlock.bitsPerPixel], EAX

	;; Assume scan line length equals X res times bytes per pixel for now.
	;; We'll check later.
	xor EDX, EDX
	mov EAX, dword [SI + 4]			; X res
	mov EBX, dword [SI + 12]		; bits per pixel
	mul EBX
	add EAX, 7						; round up to a byte
	shr EAX, 3						; bytes per pixel
	mov dword [DI + graphicsInfoBlock.scanLineBytes], EAX

	cmp word [PRINTINFO], 1
	jne .done

	;; Say we found a mode.
	mov DL, GOODCOLOR		; Switch to good color
	mov SI, BLANK
	call loaderPrint
	mov DL, FOREGROUNDCOLOR	; Switch to foreground color
	mov SI, MODESTATS1
	call loaderPrint
	mov SI, word [VIDEODATA_P]
	mov EAX, dword [SI + graphicsInfoBlock.xRes]
	call loaderPrintNumber
	mov SI, MODESTATS2
	call loaderPrint
	mov SI, word [VIDEODATA_P]
	mov EAX, dword [SI + graphicsInfoBlock.yRes]
	call loaderPrintNumber
	mov SI, SPACE
	call loaderPrint
	mov SI, word [VIDEODATA_P]
	mov EAX, dword [SI + graphicsInfoBlock.bitsPerPixel]
	call loaderPrintNumber
	mov SI, MODESTATS3
	call loaderPrint
	call loaderPrintNewline
	jmp .done

	.fail:
	;; If we fall through to here, we didn't find any acceptable video mode.
	;; Make an error message.
	mov DL, BADCOLOR	; Switch to the error color
	mov SI, SAD
	call loaderPrint
	mov SI, VIDMODE
	call loaderPrint
	mov SI, NOMODE
	call loaderPrint
	call loaderPrintNewline
	mov SI, BLANK
	call loaderPrint
	mov SI, TEXTMODE
	call loaderPrint
	call loaderPrintNewline
	mov SI, BLANK
	call loaderPrint
	mov SI, DUBIOUS
	call loaderPrint
	call loaderPrintNewline

	.done:
	mov SP, BP
	popa
	ret


findGraphicMode:
	;; This function takes parameters that describe the desired graphic
	;; mode, and returns the VBE graphic mode number, if found.
	;; The C prototype for the parameters would look like this:
	;; int loaderFindGraphicMode(int xres, int yres, int bpp);
	;; (X resolution, Y resolution, Bits per pixel)
	;; On success it returns the mode number of the applicable graphics
	;; mode.  Otherwise, it returns 0

	;; Save space on the stack for the mode number we're returning
	sub SP, 2

	pusha

	;; Save the stack pointer
	mov BP, SP

	;; By default, return the value 0
	mov word [SS:(BP + 16)], 0

	;; We need to get a pointer to a list of graphics mode numbers
	;; from the VBE.  We can gather this from the VESA info block
	;; retrieved earlier by the video hardware detection routines.
	;; Get the offset now, and the segment inside the loop.
	mov SI, [VESAINFO + 0Eh]

	;; Do a loop through the supported modes
	.modeLoop:

	;; Save ES
	push ES

	;; Now get the segment of the pointer, as mentioned above
	mov AX, [VESAINFO + 10h]
	mov ES, AX

	;; ES:SI is now a pointer to a word list of supported modes,
	;; terminated with the value FFFFh

	;; Get the first/next mode number
	mov DX, word [ES:SI]

	;; Restore ES
	pop ES

	;; Is it the end of the mode number list?
	cmp DX, 0FFFFh
	je near .done

	;; Increment the pointer for the next loop
	add SI, 2

	;; We have a mode number.  Now we need to do a VBE call to
	;; determine whether this mode number suits our needs.
	;; This call will put a bunch of info in the buffer pointed to
	;; by ES:DI

	mov CX, DX
	mov AX, 4F01h
	mov DI, MODEINFO
	int 10h

	;; Make sure the function call is supported
	cmp AL, 4Fh
	jne near .done
	;; Is the mode supported by this call?
	cmp AH, 00h
	jne .modeLoop

	;; We need to look for a few features to determine whether this
	;; is the mode we want.  First, it needs to be supported, and it
	;; needs to be a graphics mode.  Next, it needs to match the
	;; requested attributes of resolution and bpp

	;; Get the first word of the buffer
	mov AX, word [MODEINFO]

	;; Is the mode supported?
	bt AX, 0
	jnc .modeLoop

	;; Is this mode a graphics mode?
	bt AX, 4
	jnc .modeLoop

	;; Does this mode support a linear frame buffer?
	bt AX, 7
	jnc .modeLoop

	;; Does the horizontal resolution of this mode match the requested
	;; number?
	mov AX, word [MODEINFO + 12h]
	cmp AX, word [SS:(BP + 20)]
	jne near .modeLoop

	;; Does the vertical resolution of this mode match the requested
	;; number?
	mov AX, word [MODEINFO + 14h]
	cmp AX, word [SS:(BP + 22)]
	jne near .modeLoop

	;; Do the Bits Per Pixel of this mode match the requested number?
	xor AX, AX
	mov AL, byte [MODEINFO + 19h]
	cmp AX, word [SS:(BP + 24)]
	jne near .modeLoop

	;; If we fall through to here, this is the mode we want.
	mov word [SS:(BP + 16)], DX

	.done:
	popa
	;; Return the mode number
	pop AX
	ret


checkLinearFramebuffer:
	;; Returns 1 if any linear framebuffer modes are supported.  0
	;; otherwise

	;; Save space on the stack our return code
	sub SP, 2

	pusha

	;; Save the stack pointer
	mov BP, SP

	;; By default, return the value 0
	mov word [SS:(BP + 16)], 0

	;; We need to get a pointer to a list of graphics mode numbers
	;; from the VBE.  We can gather this from the VESA info block
	;; retrieved earlier by the video hardware detection routines.
	;; Get the offset now, and the segment inside the loop.
	mov SI, [VESAINFO + 0Eh]

	;; Do a loop through the supported modes
	.modeLoop:

	;; Save ES
	push ES

	;; Now get the segment of the pointer, as mentioned above
	mov AX, [VESAINFO + 10h]
	mov ES, AX

	;; ES:SI is now a pointer to a word list of supported modes,
	;; terminated with the value FFFFh

	;; Get the first/next mode number
	mov DX, word [ES:SI]

	;; Restore ES
	pop ES

	;; Is it the end of the mode number list?
	cmp DX, 0FFFFh
	je near .done

	;; Increment the pointer for the next loop
	add SI, 2

	;; We have a mode number.  Now we need to do a VBE call to
	;; determine whether this mode number suits our needs.
	;; This call will put a bunch of info in the buffer pointed to
	;; by ES:DI

	mov CX, DX
	mov AX, 4F01h
	mov DI, MODEINFO
	int 10h

	;; Make sure the function call is supported
	cmp AL, 4Fh
	jne near .done
	;; Is the mode supported by this call?
	cmp AH, 00h
	jne .modeLoop

	;; We need to look for a few features to determine whether this
	;; is the mode we want.  First, it needs to be supported, and it
	;; needs to be a graphics mode.  Next, it needs to match the
	;; requested attributes of resolution and bpp

	;; Get the first word of the buffer
	mov AX, word [MODEINFO]

	;; Is the mode supported?
	bt AX, 0
	jnc .modeLoop

	;; Is this mode a graphics mode?
	bt AX, 4
	jnc .modeLoop

	;; Does this mode support a linear frame buffer?
	bt AX, 7
	jnc .modeLoop

	;; Framebuffer is supported
	mov word [SS:(BP + 16)], 1

	.done:
	popa
	pop AX
	ret


loaderGetLinearFramebuffer:
	;; This will return the address of the Linear Frame Buffer for
	;; the requested video mode

	pusha

	;; Save stack pointer
	mov BP, SP

	;; Make sure SVGA is available
	cmp byte [SVGAAVAIL], 1
	jne near .error

	mov AX, 4F01h
	mov ECX, dword [SS:(BP + 18)]
	mov DI, MODEINFO
	int 10h

	;; Make sure the function call was successful
	cmp AX, 004Fh
	jne .error

	popa
	mov EAX, dword [MODEINFO + 28h]
	ret

	.error:
	popa
	mov EAX, 0
	ret


loaderSetTextDisplay:
	;; This function will set the text display mode to a known state
	;; for the initial loading messages, etc.

	pusha

	;; Save the stack pointer
	mov BP, SP

	;; Set the active display page to VIDEOPAGE
	mov AH, 05h
	mov AL, VIDEOPAGE
	int 10h

	;; Set the overscan color.  That's the color that forms the
	;; default backdrop, and sometimes appears as a border around
	;; the printable screen
	mov AX, 0B00h
	mov BH, 0
	mov BL, BACKGROUNDCOLOR
	int 10h

	;; We will try to change text modes to a more attractive 80x50
	;; mode.  This takes a couple of steps

	;; Change the number of scan lines in the text display
	mov AX, 1202h		; 400 scan lines
	mov BX, 0030h		; Change scan lines command
	int 10h

	;; Set the text video mode to make the change take effect
	mov AX, 0003h
	;; Should we clear the screen?
	mov BX, word [SS:(BP + 18)]
	shl BX, 7
	or AX, BX
	int 10h

	;; The following call messes with ES, so save it
	push ES

	;; Change the VGA font to match a 80x50 configuration
	mov AX, 1112h		; 8x8 character set
	mov BL, 0
	int 10h

	;; Restore ES
	pop ES

	;; Should we blank the screen?
	cmp word [SS:(BP + 18)], 0
	jne .done
	mov AX, 0700h
	mov BH, BACKGROUNDCOLOR
	and BH, 00000111b
	shl BH, 4
	or BH, FOREGROUNDCOLOR
	mov CX, 0000h
	mov DH, ROWS
	mov DL, COLUMNS
	int 10h

	.done:
	;; We are now in text mode
	mov word [CURRENTGMODE], 0

	popa
	ret


loaderDetectVideo:
	;; Check for the mandatory SVGA video card

	;; Save regs
	pusha

	;; Save the stack pointer
	mov BP, SP

	;; We are passed a pointer to the place we should store video
	;; data (the loader's hardware structure).  Save it.
	mov AX, word [SS:(BP + 18)]
	mov word [VIDEODATA_P], AX

	;; Save ES, since this call could destroy it
	push ES

	mov DI, VESAINFO
	mov AX, 4F00h
	int 10h

	;; Restore ES
	pop ES

	cmp AX, 004Fh
	jne .noSVGA

	;; If the signature 'VESA' appears at the beginning of the info
	;; block, we are OK
	mov EAX, dword [VESAINFO]
	cmp EAX, 41534556h
	je .okSVGA

	.noSVGA:
	;; There is no SVGA video detected
	mov DL, BADCOLOR	; Use the error color
	mov SI, SAD
	call loaderPrint
	mov SI, SVGA
	call loaderPrint
	mov SI, NOSVGA
	call loaderPrint
	call loaderPrintNewline
	mov SI, BLANK
	call loaderPrint
	mov SI, TEXTMODE
	call loaderPrint
	call loaderPrintNewline

	jmp .done

	.okSVGA:
	;; Figure out which VESA version this card supports.  Since we use
	;; the Linear Frame Buffer approach, we will require version 2.0
	;; or greater
	mov AX, word [VESAINFO + 04h]
	cmp AX, 0200h
	jae .okVersion

	;; This video card is too old to support VESA version 2.0.  We won't
	;; be able to use the Linear Frame Buffer
	mov DL, BADCOLOR	; Use the error color
	mov SI, SAD
	call loaderPrint
	mov SI, SVGA
	call loaderPrint
	mov SI, SVGAVER
	call loaderPrint
	call loaderPrintNewline
	mov SI, BLANK
	call loaderPrint
	mov SI, TEXTMODE
	call loaderPrint
	call loaderPrintNewline

	jmp .done

	.okVersion:
	cmp word [PRINTINFO], 1
	jne .noPrint1
	;; Indicate SVGA detected
	mov DL, GOODCOLOR		; Switch to good color
	mov SI, HAPPY
	call loaderPrint
	mov SI, SVGA
	call loaderPrint

	mov DL, FOREGROUNDCOLOR	; Switch to foreground color
	mov SI, VESA20
	call loaderPrint
	.noPrint1:

	;; Get the amount of video memory
	xor EAX, EAX
	mov AX, word [(VESAINFO + 12h)]
	shl EAX, 6	;; (multiply by 64 to get 1K blocks)
	mov SI, word [VIDEODATA_P]
	mov dword [SI + graphicsInfoBlock.videoMemory], EAX

	;; Write out how much video memory detected.  The amount is in EAX.
	cmp word [PRINTINFO], 1
	jne .noPrint2
	call loaderPrintNumber
	mov SI, VIDEORAM
	call loaderPrint
	call loaderPrintNewline
	.noPrint2:

	;; Make note that we can use SVGA.
	mov byte [SVGAAVAIL], 1

	;; Check whether linear framebuffer is available in any mode
	call checkLinearFramebuffer
	cmp AX, 1
	je .okFramebuffer

	mov DL, BADCOLOR	; Use the error color
	mov SI, SAD
	call loaderPrint
	mov SI, SVGA
	call loaderPrint
	mov SI, NOFRAMEBUFF
	call loaderPrint
	call loaderPrintNewline
	mov SI, BLANK
	call loaderPrint
	mov SI, TEXTMODE
	call loaderPrint
	call loaderPrintNewline

	jmp .done

	.okFramebuffer:
	;; Get a list of the preferred graphics modes that this hardware
	;; can display.
	call enumerateGraphicsModes

	;; Find out whether the user prefers a particular video mode
	push word GRAPHICSMODE
	call loaderFindFile
	add SP, 2

	;; Does the file exist?
	cmp AX, 1
	jne near .selectMode

	;; Try to load our 'graphics mode' file
	push word 0		; No progress indicator
	push dword [FILEDATABUFFER]
	push word GRAPHICSMODE
	call loaderLoadFile
	add SP, 8

	cmp EAX, 0
	jl near .selectMode

	;; Get values
	push ES
	mov EAX, dword [FILEDATABUFFER]
	shr EAX, 4
	mov ES, EAX
	xor ESI, ESI
	mov EDX, dword [ES:(ESI + 8)]	; BPP
	mov ECX, dword [ES:(ESI + 4)]	; Y resolution
	mov EBX, dword [ES:ESI]			; X resolution
	pop ES

	;; Check whether the mode is supported
	push DX	; BPP
	push CX ; Y resolution
	push BX	; X resolution
	call findGraphicMode

	;; Need the top halves of these clear
	xor EBX, EBX
	xor ECX, ECX
	xor EDX, EDX

	pop BX	; X resolution
	pop CX	; Y resolution
	pop DX	; BPP

	cmp AX, 0
	je .selectMode		; Not a good mode

	;; Okay, it's a supported mode.  Save the values and skip mode
	;; selection

	;; Clear top half of EAX
	push AX
	xor EAX, EAX
	pop AX

	;; Save mode
	mov DI, word [VIDEODATA_P]
	mov dword [DI + graphicsInfoBlock.mode], EAX

	;; Save it here too
	mov word [KERNELGMODE], AX

	;; Save X res, Y res, and BPP
	mov dword [DI + graphicsInfoBlock.xRes], EBX
	mov dword [DI + graphicsInfoBlock.yRes], ECX
	mov dword [DI + graphicsInfoBlock.bitsPerPixel], EDX

	;; Whew.  This was quite a hack.  Continue, please.
	jmp .noSelectMode

	.selectMode:
	;; See if we can find a good graphics mode
	call selectGraphicMode

	.noSelectMode:
	;; Save the eventual LFB pointer as well
	push word [KERNELGMODE]
	call loaderGetLinearFramebuffer
	add SP, 2
	mov DI, word [VIDEODATA_P]
	mov dword [DI + graphicsInfoBlock.framebuffer], EAX

	.done:
	popa
	ret


loaderQueryGraphicMode:
	;; Presents the user with a list of possible graphics modes.
	pusha

	mov DL, FOREGROUNDCOLOR
	mov SI, CHOOSEMODE
	call loaderPrint
	call loaderPrintNewline
	call loaderPrintNewline

	mov DI, VIDEOMODES
	mov BX, 0			; valid mode not found
	mov CX, 1

	.modeLoop:
	;; See whether the mode is supported
	push word [DI + 4]		; BPP
	push word [DI + 2]		; Y resolution
	push word [DI]			; X resolution
	call findGraphicMode
	add SP, 6
	cmp AX, 0
	je .skip				; Not a good mode

	add BX, 1				; valid mode found

	;; Print selection number
	mov SI, INDENT
	call loaderPrint
	mov AX, CX
	call loaderPrintNumber
	mov SI, COLON
	call loaderPrint

	;; Print mode info
	mov AX, word [DI]		; X resolution
	call loaderPrintNumber
	mov SI, MODESTATS2
	call loaderPrint
	mov AX, word [DI + 2]	; Y resolution
	call loaderPrintNumber
	mov SI, SPACE
	call loaderPrint
	mov AX, word [DI + 4]	; BPP
	call loaderPrintNumber
	mov SI, MODESTATS3
	call loaderPrint
	call loaderPrintNewline

	.skip:
	inc CX
	add DI, 6
	mov AX, word [DI]
	cmp AX, 0
	jnz .modeLoop

	call loaderPrintNewline

	;; Valid mode found?
	cmp BX, 0
	je .out

	.getInput:
	;; Read the key press
	mov AX, 0000h
	int 16h

	;; Valid number key?
	cmp AH, 2
	jb .getInput
	cmp AH, 10
	ja .getInput

	;; Valid mode?
	shr AX, 8
	sub AX, 2			; Index into our mode array
	mov BL, 6
	mul BL
	mov DI, VIDEOMODES
	add DI, AX

	push word [DI + 4]	; BPP
	push word [DI + 2]	; Y resolution
	push word [DI]		; X resolution
	call findGraphicMode
	add SP, 6
	cmp AX, 0
	je .getInput		; Not a good mode

	;; Clear top half of EAX
	push AX
	xor EAX, EAX
	pop AX

	xor EBX, EBX
	mov BX, word [DI]		; X resolution
	xor ECX, ECX
	mov CX, word [DI + 2]	; Y resolution
	xor EDX, EDX
	mov DX, word [DI + 4]	; BPP

	;; Save mode
	mov SI, word [VIDEODATA_P]
	mov dword [SI + graphicsInfoBlock.mode], EAX

	;; Save it here too
	mov word [KERNELGMODE], AX

	;; Save X res, Y res, and BPP
	mov dword [SI + graphicsInfoBlock.xRes], EBX
	mov dword [SI + graphicsInfoBlock.yRes], ECX
	mov dword [SI + graphicsInfoBlock.bitsPerPixel], EDX

	.out:
	popa
	ret


loaderSetGraphicDisplay:
	;; This routine switches the video adapter into the requested
	;; graphics mode.
	pusha

	;; Save the stack pointer
	mov BP, SP

	;; Make sure SVGA is available
	cmp byte [SVGAAVAIL], 1
	jne near .done

	;; Get the requested graphic mode from the stack and save it
	mov AX, word [SS:(BP + 18)]
	mov word [CURRENTGMODE], AX

	;; Get the information about this graphic mode
	mov AX, 4F01h
	mov CX, word [CURRENTGMODE]
	or CX, 4000h			; Use linear frame buffer
	mov DI, MODEINFO
	int 10h

	cmp AX, 004Fh			; Returns this value if successful
	jne near .done

	;; Here's where we actually change to the selected graphics mode
	mov AX, 4F02h			; SVGA function 4F, subfunction 2
	mov BX, word [CURRENTGMODE]	; Mode number
	or BX, 4000h			; Use linear frame buffer
	int 10h

	cmp AX, 004Fh			; Returns this value if successful
	je .switchok

	;; Couldn't switch to graphics mode.  Print an error message.
	mov DL, BADCOLOR	; Switch to the error color
	mov SI, SAD
	call loaderPrint
	mov SI, VIDMODE
	call loaderPrint
	mov SI, NOSWITCH1
	call loaderPrint
	call loaderPrintNewline
	mov SI, BLANK
	call loaderPrint
	mov SI, NOSWITCH2
	call loaderPrint
	call loaderPrintNewline
	mov SI, BLANK
	call loaderPrint
	mov SI, TEXTMODE
	call loaderPrint
	call loaderPrintNewline

	mov word [CURRENTGMODE], 0
	jmp .done

	.switchok:
	;; Scan line lengths should be the same (in pixels) as the
	;; X resolution.  Careful; kills AX, BX, CX, DX
	mov AX, 4F06h
	xor BX, BX
	mov CX, word [MODEINFO + 12h]
	int 10h

	;; Get the scan line length in bytes
	mov AX, 4F06h
	mov BX, 1
	int 10h

	cmp AX, 004Fh			; Returns this value if successful
	jne .displayStart

	;; Save it
	mov DI, word [VIDEODATA_P]
	xor EAX, EAX
	mov AX, BX
	mov dword [DI + graphicsInfoBlock.scanLineBytes], EAX

	.displayStart:
	;; Make sure the display starts at the same place as the
	;; frame buffer (line 0, pixel 0).  Careful; kills AX, BX, CX, DX
	mov AX, 4F07h
	xor BX, BX
	xor CX, CX
	xor DX, DX
	int 10h

	.done:
	popa
	ret

;;
;; The data segment
;;

	SEGMENT .data
	ALIGN 4

CURRENTGMODE	dw 0
KERNELGMODE		dw 0
VIDEODATA_P		dw 0
VESAINFO		db 'VBE2'		;; Space for info ret by vid BIOS
				times 508 db 0
MODEINFO		times 256 db 0
SVGAAVAIL		db 0
GRAPHICSMODE	db 'GRPHMODE   ', 0

;;
;; Predefined video resolutions, for the boot menu
;;

VIDEOMODES:
	dw 1280, 1024, 32	; 1280 x 1024 x 32 bpp
	dw 1280, 800, 32	; 1280 x 800 x 32 bpp
	dw 1152, 864, 32	; 1152 x 864 x 32 bpp
	dw 1024, 768, 32	; 1024 x 768 x 32 bpp
	dw 1024, 768, 24	; 1024 x 768 x 24 bpp
	dw 800, 600, 32		; 800 x 600 x 32 bpp
	dw 800, 600, 24		; 800 x 600 x 24 bpp
	dw 640, 480, 32		; 640 x 480 x 32 bpp
	dw 640, 480, 24		; 640 x 480 x 24 bpp
	dw 0

;;
;; The good/informational messages
;;

SVGA		db 'SVGA video   ', 10h, ' ', 0
BLANK		db '               ', 10h, ' ', 0
HAPPY		db 01h, ' ', 0
VESA20		db 'VESA 2.0 or greater, ', 0
VIDEORAM	db 'K video RAM', 0
VIDMODE		db 'Video mode   ', 10h, ' ', 0
MODESTATS1	db 'Selected mode: ', 0
MODESTATS2	db 'x', 0
MODESTATS3	db ' bits/pixel', 0
CHOOSEMODE	db 'Please choose the video mode:', 0
SPACE		db ' ',0
INDENT		db '  ',0
COLON		db ': ',0

;;
;; The error messages
;;

SAD			db 'x ', 0
NOSVGA		db 'SVGA video not detected.', 0
TEXTMODE	db 'This session will be restricted to text mode operation.', 0
SVGAVER		db 'VESA version 2.0 or greater not available.', 0
NOFRAMEBUFF db 'Linear framebuffer video not available.', 0
NOSWITCH1	db 'Could not switch to graphics mode.  You may wish to', 0
NOSWITCH2	db 'investigate possible problems with your video card.', 0
NOMODE		db 'Could not find a supported graphics mode to use.', 0
DUBIOUS		db 'Note: The VESA compatibility of your video card is dubious.', 0

