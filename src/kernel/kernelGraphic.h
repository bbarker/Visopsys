//
//  Visopsys
//  Copyright (C) 1998-2018 J. Andrew McLaughlin
//
//  This program is free software; you can redistribute it and/or modify it
//  under the terms of the GNU General Public License as published by the Free
//  Software Foundation; either version 2 of the License, or (at your option)
//  any later version.
//
//  This program is distributed in the hope that it will be useful, but
//  WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
//  or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License
//  for more details.
//
//  You should have received a copy of the GNU General Public License along
//  with this program; if not, write to the Free Software Foundation, Inc.,
//  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
//
//  kernelGraphic.h
//

// This goes with the file kernelGraphic.c

#if !defined(_KERNELGRAPHIC_H)

#include "kernelDevice.h"
#include "kernelFont.h"
#include <sys/graphic.h>
#include <sys/image.h>

// Types of borders
typedef enum {
	border_top = 1, border_left = 2, border_bottom = 4, border_right = 8,
	border_all = (border_top | border_left | border_bottom | border_right)

} borderType;

// Structures for the graphic adapter device

typedef struct {
	int (*driverClearScreen)(color *);
	int (*driverDrawPixel)(graphicBuffer *, color *, drawMode, int, int);
	int (*driverDrawLine)(graphicBuffer *, color *, drawMode, int, int, int,
		int);
	int (*driverDrawRect)(graphicBuffer *, color *, drawMode, int, int, int,
		int, int, int);
	int (*driverDrawOval)(graphicBuffer *, color *, drawMode, int, int, int,
		int, int, int);
	int (*driverDrawMonoImage)(graphicBuffer *, image *, drawMode, color *,
		color *, int, int);
	int (*driverDrawImage)(graphicBuffer *, image *, drawMode, int, int, int,
		int, int, int);
	int (*driverGetImage)(graphicBuffer *, image *, int, int, int, int);
	int (*driverCopyArea)(graphicBuffer *, int, int, int, int, int, int);
	int (*driverRenderBuffer)(graphicBuffer *, int, int, int, int, int, int);
	int (*driverFilter)(graphicBuffer *, color *, int, int, int, int);

} kernelGraphicOps;

typedef struct {
	unsigned videoMemory;
	void *framebuffer;
	int mode;
	int xRes;
	int yRes;
	int bitsPerPixel;
	int bytesPerPixel;
	int scanLineBytes;
	int numberModes;
	videoMode supportedModes[MAXVIDEOMODES];
	unsigned char *lineBuffer;
	kernelDriver *driver;

} kernelGraphicAdapter;

// Functions exported by kernelGraphic.c
int kernelGraphicInitialize(kernelDevice *);
int kernelGraphicsAreEnabled(void);
int kernelGraphicGetModes(videoMode *, unsigned);
int kernelGraphicGetMode(videoMode *);
int kernelGraphicSetMode(videoMode *);
int kernelGraphicGetScreenWidth(void);
int kernelGraphicGetScreenHeight(void);
int kernelGraphicCalculateAreaBytes(int, int);
int kernelGraphicClearScreen(color *);
int kernelGraphicDrawPixel(graphicBuffer *, color *, drawMode, int, int);
int kernelGraphicDrawLine(graphicBuffer *, color *, drawMode, int, int, int,
	int);
int kernelGraphicDrawRect(graphicBuffer *, color *, drawMode, int, int, int,
	int, int, int);
int kernelGraphicDrawOval(graphicBuffer *, color *, drawMode, int, int, int,
	int, int, int);
int kernelGraphicGetImage(graphicBuffer *, image *, int, int, int, int);
int kernelGraphicDrawImage(graphicBuffer *, image *, drawMode, int, int, int,
	int, int, int);
int kernelGraphicDrawText(graphicBuffer *, color *, color *, kernelFont *,
	const char *, const char *, drawMode, int, int);
int kernelGraphicCopyArea(graphicBuffer *, int, int, int, int, int, int);
int kernelGraphicClearArea(graphicBuffer *, color *, int, int, int, int);
int kernelGraphicCopyBuffer(graphicBuffer *, graphicBuffer *, int, int);
int kernelGraphicRenderBuffer(graphicBuffer *, int, int, int, int, int,	int);
int kernelGraphicFilter(graphicBuffer *, color *, int, int, int, int);
void kernelGraphicDrawGradientBorder(graphicBuffer *, int, int, int, int, int,
	color *, int, drawMode, borderType);
void kernelGraphicConvexShade(graphicBuffer *, color *, int, int, int, int,
	shadeType);

#define _KERNELGRAPHIC_H
#endif

