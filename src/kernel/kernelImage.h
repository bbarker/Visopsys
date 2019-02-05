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
//  kernelImage.h
//

// This goes with the file kernelImage.c

#if !defined(_KERNELIMAGE_H)

#include <sys/image.h>

// Functions exported by kernelImage.c
int kernelImageNew(image *, unsigned, unsigned);
int kernelImageFree(image *);
int kernelImageLoad(const char *, unsigned, unsigned, image *);
int kernelImageSave(const char *, int, image *);
int kernelImageResize(image *, unsigned, unsigned);
int kernelImageCopy(image *, image *);
int kernelImageCopyToKernel(image *, image *);
int kernelImageFill(image *, color *);
int kernelImagePaste(image *, image *, int, int);
int kernelImageGetAlpha(image *);

#define _KERNELIMAGE_H
#endif

