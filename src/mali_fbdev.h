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

#ifndef _MALI_FBDEV_DRIVER_H_
#define _MALI_FBDEV_DRIVER_H_

#include <linux/videodev2.h>
#include <linux/fb.h>
#include <linux/hwmem.h>
#include <sys/mman.h>
#include "exa.h"
#include <xf86xv.h>
#include <video/mcde_fb.h>

#define DPMSModeOn	0
#define DPMSModeStandby	1
#define DPMSModeSuspend	2
#define DPMSModeOff	3

enum dri_type
{
	DRI_DISABLED,
	DRI_NONE,
	DRI_2,
};

typedef struct {
	unsigned char  *fbstart;
	unsigned char  *fbmem;
	int             fboff;
	int             lineLength;
	CreateScreenResourcesProcPtr CreateScreenResources;
	void (*PointerMoved)(int index, int x, int y);
	CloseScreenProcPtr  CloseScreen;
	EntityInfoPtr       pEnt;
	OptionInfoPtr       Options;
	int    fb_lcd_fd;
	struct fb_fix_screeninfo fb_lcd_fix;
	struct fb_var_screeninfo fb_lcd_var;
	ExaDriverPtr exa;
	int  dri_render;
	Bool dri_open;
	int  drm_fd;
	char deviceName[64];
	Bool use_pageflipping;
	Bool use_pageflipping_vsync;
	int  hwmem_fd;
        /* Video Adaptors */
        XF86VideoAdaptorPtr overlay_adaptor;
        XF86VideoAdaptorPtr textured_adaptor;
} MaliRec, *MaliPtr;

typedef struct {
	char *device;
	int   fd;
	void *fbmem;
	unsigned int   fbmem_len;
	unsigned int   fboff;
	struct fb_fix_screeninfo fix;
	struct fb_var_screeninfo var;
	struct fb_var_screeninfo saved_var;
	DisplayModeRec buildin;
	int xres;
	int yres;
} MaliHWRec, *MaliHWPtr;

#define MALIPTR(p) ((MaliPtr)((p)->driverPrivate))
#define MALIHWPTRLVAL(p) (p)->privates[malihwPrivateIndex].ptr
#define MALIHWPTR(p) ((MaliHWPtr)(MALIHWPTRLVAL(p)))

//#define MALI_DEBUG_MSG_ENABLE

#ifdef MALI_DEBUG_MSG_ENABLE
#define MALIDBGMSG(type, format, args...)       xf86Msg(type, format, args)
#else
#define MALIDBGMSG(type, format, args...)
#endif

Bool FBDEV_lcd_init(ScrnInfoPtr pScrn);

Bool MaliDRI2ScreenInit( ScreenPtr pScreen );
void MaliDRI2CloseScreen( ScreenPtr pScreen );

#define VIDEO_IMAGE_MAX_WIDTH 1920
#define VIDEO_IMAGE_MAX_HEIGHT 1280

#define VIDEO_RESIZE_MAX_WIDTH 1920
#define VIDEO_RESIZE_MAX_HEIGHT 1280

#define FOURCC_YUMB 0x424D5559
#define XVIMAGE_YUMB \
   { \
        FOURCC_YUMB, \
        XvYUV, \
        LSBFirst, \
        {'Y','U','M','B', \
          0x00,0x00,0x00,0x10,0x80,0x00,0x00,0xAA,0x00,0x38,0x9B,0x71}, \
        12, \
        XvPacked, \
        3, \
        0, 0, 0, 0, \
        8, 8, 8, \
        1, 2, 2, \
        1, 2, 2, \
        {'Y','V','U', \
          0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0}, \
        XvTopToBottom \
   }


#define FOURCC_STE0 0x30455453
#define XVIMAGE_STE0 \
   { \
        FOURCC_STE0, \
        XvYUV, \
        LSBFirst, \
        {'S','T','E','0', \
          0x00,0x00,0x00,0x10,0x80,0x00,0x00,0xAA,0x00,0x38,0x9B,0x71}, \
        16, \
        XvPacked, \
        1, \
        0, 0, 0, 0, \
        8, 8, 8, \
        1, 2, 2, \
        1, 1, 1, \
        {'S','T','E','0', \
          0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0}, \
        XvTopToBottom \
   }

typedef struct st_yuvmb_frame_desc {
    unsigned int poolid;
    unsigned int logicaladdress;
    unsigned int physicaladdress;
    unsigned int size;
} st_yuvmb_frame_desc;

#endif /* _MALI_FBDEV_DRIVER_H_ */

