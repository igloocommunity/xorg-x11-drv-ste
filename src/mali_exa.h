/*
 * Copyright (C) 2010 ARM Limited. All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#ifndef _MALI_EXA_H_
#define _MALI_EXA_H_

#include "xf86.h"
#include "exa.h"

struct mali_info 
{
	ScrnInfoPtr pScrn;
	unsigned long fb_phys;
	unsigned char *fb_virt;
	int fb_xres;
	int fb_yres;
	int fd;
	int blt_handle;
	int fillColor;
	GCPtr pGC;
	PixmapPtr pSourcePixmap;
};

typedef struct
{
	int hwmem_alloc;
	int hwmem_global_name;
	int bits_per_pixel;
	unsigned long usize;
} mali_mem_info;

typedef struct
{
	Bool isFrameBuffer;
	void *addr;
	mali_mem_info *mem_info;
	int bits_per_pixel;
} PrivPixmap;

#endif /* _MALI_EXA_H_ */
