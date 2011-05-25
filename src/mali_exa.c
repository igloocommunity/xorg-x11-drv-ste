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

/*
 * Copyright (C) ST-Ericsson SA 2011
 *
 * Hardware blitting functionality which uses libblt_hw added and
 * ump has been replaced by hwmem for buffer handling.
 *
 * Author: John Frediksson, <john.xj.fredriksson@stericsson.com> for
 * ST-Ericsson.
 */ 

#include <stdio.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include "mali_exa.h"
#include "mali_fbdev.h"
#include <blt_api.h>
#include <errno.h>

static struct mali_info mi;

#ifdef USE_TRACING
#define TRACE_ENTER()  xf86DrvMsg(mi.pScrn->scrnIndex, X_INFO, "%s: ENTER\n", __FUNCTION__)
#define TRACE_EXIT()   xf86DrvMsg(mi.pScrn->scrnIndex, X_INFO, "%s: EXIT\n", __FUNCTION__)
#else
#define TRACE_ENTER()
#define TRACE_EXIT()
#endif

#define MALI_EXA_FUNC(s) exa->s = mali ## s
#define IGNORE( a ) ( a = a );
#define MALI_ALIGN( value, base ) (((value) + ((base) - 1)) & ~((base) - 1))
static Bool maliPrepareAccess(PixmapPtr pPix, int index);
static void maliFinishAccess(PixmapPtr pPix, int index);

static int fd_fbdev = -1;

static int maliGetColorFormat(int bitsPerPixel)
{
        switch(bitsPerPixel) {
                case 15:
                        return BLT_FMT_16_BIT_ARGB1555;
                case 16:
                        return BLT_FMT_16_BIT_RGB565;
                case 24:
                        return BLT_FMT_24_BIT_RGB888;
                case 32:
                        return BLT_FMT_32_BIT_ARGB8888;
                default:
                        xf86DrvMsg(mi.pScrn->scrnIndex, X_INFO, "Unknown bit depth, %d", bitsPerPixel);
                        return 0;
        }
}

static Bool maliPrepareSolid( PixmapPtr pPixmap, int alu, Pixel planemask, Pixel fg )
{
	int ret = 0;

	TRACE_ENTER();
	IGNORE( alu );
	IGNORE( planemask );

	if (pPixmap->drawable.bitsPerPixel <= 8)
		ret = FALSE;
	else {
	        mi.fillColor = fg;
		ret = TRUE;
	}

	TRACE_EXIT();

 	return ret;
}

static void maliSolid( PixmapPtr pPixmap, int x1, int y1, int x2, int y2 )
{
	struct blt_req bltreq = {0};
	int status = 0;
	MaliPtr fPtr;
	PrivPixmap *privPixmap = (PrivPixmap *)exaGetPixmapDriverPrivate(pPixmap);
	fPtr = MALIPTR(xf86Screens[pPixmap->drawable.pScreen->myNum]);

	TRACE_ENTER();

	bltreq.size = sizeof(struct blt_req);
	bltreq.flags = BLT_FLAG_ASYNCH | BLT_FLAG_SOURCE_FILL_RAW;
	bltreq.transform = BLT_TRANSFORM_NONE;
	bltreq.src_color = mi.fillColor;
	bltreq.dst_img.buf.type = BLT_PTR_HWMEM_BUF_NAME_OFFSET;
	bltreq.dst_img.buf.hwmem_buf_name = privPixmap->mem_info->hwmem_global_name;
	bltreq.dst_img.width = pPixmap->drawable.width;
	bltreq.dst_img.height = pPixmap->drawable.height;
	bltreq.dst_img.fmt = maliGetColorFormat(pPixmap->drawable.bitsPerPixel);
	bltreq.dst_img.pitch = exaGetPixmapPitch(pPixmap);
	bltreq.dst_rect.x = x1;
	bltreq.dst_rect.y = y1 + fPtr->fb_lcd_var.yoffset;
	bltreq.dst_rect.width = x2 - x1;
	bltreq.dst_rect.height = y2 - y1;
	bltreq.dst_clip_rect.x = x1;
	bltreq.dst_clip_rect.y = y1 + fPtr->fb_lcd_var.yoffset;
	bltreq.dst_clip_rect.width = x2 - x1;
	bltreq.dst_clip_rect.height = y2 - y1;

	do {
		status = blt_request(mi.blt_handle, &bltreq);
		if (status < 0)
			xf86DrvMsg(mi.pScrn->scrnIndex, X_INFO, "maliSolid blt_request failed, errno %d\n", errno);
	} while (status < 0 && errno == EAGAIN);

	TRACE_EXIT();
}

static void maliDoneSolid( PixmapPtr pPixmap )
{
	TRACE_ENTER();

	mi.fillColor = 0;	
	(void)blt_synch(mi.blt_handle, 0);

	IGNORE( pPixmap );

	TRACE_EXIT();
}

static Bool maliPrepareCopy( PixmapPtr pSrcPixmap, PixmapPtr pDstPixmap, int xdir, int ydir, int alu, Pixel planemask )
{
	int ret = 0;
	
	TRACE_ENTER();

        if (pSrcPixmap->drawable.bitsPerPixel <= 8 || pDstPixmap->drawable.bitsPerPixel <= 8)
                ret = FALSE;
        else {
                mi.pSourcePixmap = pSrcPixmap;
                ret = TRUE;
        }

	IGNORE( xdir );
	IGNORE( ydir );
	IGNORE( alu );
	IGNORE( planemask );
	TRACE_EXIT();

	return ret;
}

static void maliCopy( PixmapPtr pDstPixmap, int srcX, int srcY, int dstX, int dstY, int width, int height )
{
        PrivPixmap *privPixmapSrc;
        PrivPixmap *privPixmapDst;
        MaliPtr fPtr;

        TRACE_ENTER();

        privPixmapSrc = (PrivPixmap *)exaGetPixmapDriverPrivate(mi.pSourcePixmap);
        privPixmapDst = (PrivPixmap *)exaGetPixmapDriverPrivate(pDstPixmap);
        fPtr = MALIPTR(xf86Screens[pDstPixmap->drawable.pScreen->myNum]);

        if ((mi.pSourcePixmap == pDstPixmap) && 
		!(((srcY + height < dstY) || (dstY + height < srcY)) &&
		  ((srcX + width < dstX) || (dstX + width < srcX)))) {
                /* SW blitting if areas are overlapping on the same surface */
                RegionPtr pReg;

                if (!mi.pGC) {
                        mi.pGC = GetScratchGC(pDstPixmap->drawable.depth, pDstPixmap->drawable.pScreen);
                        ValidateGC(&pDstPixmap->drawable, mi.pGC);
                }

                maliPrepareAccess(mi.pSourcePixmap, EXA_PREPARE_SRC);
                maliPrepareAccess(pDstPixmap, EXA_PREPARE_DEST);
                pReg = fbCopyArea(&mi.pSourcePixmap->drawable, &pDstPixmap->drawable,
                                  mi.pGC, srcX, srcY, width, height, dstX, dstY);
                if (pReg) {
			REGION_DESTROY(pDstPixmap->drawable.pScreen, pReg);
                }

                maliFinishAccess(mi.pSourcePixmap, EXA_PREPARE_SRC);
                maliFinishAccess(pDstPixmap, EXA_PREPARE_DEST);

        } else {
                /* HW blitting */
                struct blt_req bltreq = {0};
                int status = 0;

                bltreq.size = sizeof(struct blt_req);
                bltreq.flags = BLT_FLAG_ASYNCH;
                bltreq.transform = BLT_TRANSFORM_NONE;
                bltreq.src_img.fmt = maliGetColorFormat(mi.pSourcePixmap->drawable.bitsPerPixel);
                bltreq.src_img.buf.type = BLT_PTR_HWMEM_BUF_NAME_OFFSET;
                bltreq.src_img.buf.hwmem_buf_name = privPixmapSrc->mem_info->hwmem_global_name;
                bltreq.src_img.width = mi.pSourcePixmap->drawable.width;
                bltreq.src_img.height = mi.pSourcePixmap->drawable.height;
                bltreq.src_img.pitch = exaGetPixmapPitch(mi.pSourcePixmap);
                bltreq.dst_img.fmt = maliGetColorFormat(pDstPixmap->drawable.bitsPerPixel);
                bltreq.dst_img.buf.type = BLT_PTR_HWMEM_BUF_NAME_OFFSET;
                bltreq.dst_img.buf.hwmem_buf_name = privPixmapDst->mem_info->hwmem_global_name;
                bltreq.dst_img.width = pDstPixmap->drawable.width;
                bltreq.dst_img.height = pDstPixmap->drawable.height;
                bltreq.dst_img.pitch = exaGetPixmapPitch(pDstPixmap);
                bltreq.src_rect.x = srcX;
                bltreq.src_rect.y = srcY;
                bltreq.src_rect.width = width;
                bltreq.src_rect.height = height;
                bltreq.dst_rect.x = dstX;
                bltreq.dst_rect.y = dstY + fPtr->fb_lcd_var.yoffset;
                bltreq.dst_rect.width = width;
                bltreq.dst_rect.height = height;
                bltreq.dst_clip_rect.x = dstX;
                bltreq.dst_clip_rect.y = dstY + fPtr->fb_lcd_var.yoffset;
                bltreq.dst_clip_rect.width = width;
                bltreq.dst_clip_rect.height = height;

                do {
                        status = blt_request(mi.blt_handle, &bltreq);
                        if (status < 0)
                                xf86DrvMsg(mi.pScrn->scrnIndex, X_INFO, "maliCopy blt_request failed, errno %d\n", errno);
                } while (status < 0 && errno == EAGAIN);

        }


	TRACE_EXIT();
}

static void maliDoneCopy( PixmapPtr pDstPixmap )
{
	TRACE_ENTER();

        if (mi.pGC) {
                FreeScratchGC(mi.pGC);
                mi.pGC = NULL;
        }
        (void)blt_synch(mi.blt_handle, 0);

	IGNORE( pDstPixmap );
	TRACE_EXIT();
}

static void maliWaitMarker( ScreenPtr pScreen, int marker )
{
	TRACE_ENTER();
	IGNORE( pScreen );
	IGNORE( marker );
	TRACE_EXIT();
}

static void* maliCreatePixmap(ScreenPtr pScreen, int size, int align )
{
	PrivPixmap *privPixmap = calloc(1, sizeof(PrivPixmap));

	TRACE_ENTER();
	IGNORE( pScreen );
	IGNORE( size );
	IGNORE( align );
	privPixmap->bits_per_pixel = 0;
	TRACE_EXIT();

	return privPixmap;
}

static void maliDestroyPixmap(ScreenPtr pScreen, void *driverPriv )
{
	PrivPixmap *privPixmap = (PrivPixmap *)driverPriv;

	TRACE_ENTER();
	IGNORE( pScreen );
	if ( NULL != privPixmap->mem_info )
	{
		if (privPixmap->addr != NULL)
		{
			munmap(privPixmap->addr, privPixmap->mem_info->usize);
		}
		ioctl( (MALIPTR(xf86Screens[pScreen->myNum]))->hwmem_fd,
			HWMEM_RELEASE_IOC, 
			privPixmap->mem_info->hwmem_alloc);
		free( privPixmap->mem_info );
		privPixmap->mem_info = NULL;
		free( privPixmap );
	}
	TRACE_EXIT();
}

static Bool maliModifyPixmapHeader(PixmapPtr pPixmap, int width, int height, int depth, int bitsPerPixel, int devKind, pointer pPixData)
{
	unsigned int size;
	PrivPixmap *privPixmap = (PrivPixmap *)exaGetPixmapDriverPrivate(pPixmap);
	mali_mem_info *mem_info;

	TRACE_ENTER();

	if (!pPixmap) 
	{
		TRACE_EXIT();
		return FALSE;
	}

	miModifyPixmapHeader(pPixmap, width, height, depth, bitsPerPixel, devKind, pPixData);

	if (pPixData == mi.fb_virt) 
	{
		/* initialize it to -1 since this denotes an error */
		unsigned int secure_id = -1;

		privPixmap->isFrameBuffer = TRUE;

		mem_info = privPixmap->mem_info;
		if ( mem_info ) 
		{
			TRACE_EXIT();
			return TRUE;
		}

		/* create new mem_info for the on-screen buffer */
		mem_info = calloc(1, sizeof(*mem_info));
		if (!mem_info) 
		{
			xf86DrvMsg(mi.pScrn->scrnIndex, X_ERROR, "[%s:%d] failed to allocate for memory metadata\n", __FUNCTION__, __LINE__);
			TRACE_EXIT();
			return FALSE;
		}

		/* get the secure ID for the framebuffer */
		secure_id = ioctl( fd_fbdev, MCDE_GET_BUFFER_NAME_IOC, NULL );

		if ( -1 == secure_id)
		{
			free( mem_info );
			privPixmap->mem_info = NULL;
			xf86DrvMsg( mi.pScrn->scrnIndex, X_ERROR, "[%s:%d] hwmem failed to retrieve secure id\n", __FUNCTION__, __LINE__);
			TRACE_EXIT();
			return FALSE;
		}

		mem_info->hwmem_alloc = ioctl(	MALIPTR(xf86Screens[pPixmap->drawable.pScreen->myNum])->hwmem_fd,
						HWMEM_IMPORT_IOC,
						secure_id);

		mem_info->hwmem_global_name = secure_id;

		if ( -1 == mem_info->hwmem_alloc )
		{
			xf86DrvMsg( mi.pScrn->scrnIndex, X_ERROR, "[%s:%d] hwmem failed to create handle from secure id\n", __FUNCTION__, __LINE__);
			free( mem_info );
			privPixmap->mem_info = NULL;
			TRACE_EXIT();
			return FALSE;
		}

		size = exaGetPixmapPitch(pPixmap) * pPixmap->drawable.height;
		mem_info->usize = size;

		privPixmap->mem_info = mem_info;
		if( bitsPerPixel != 0 ) privPixmap->bits_per_pixel = bitsPerPixel;

		TRACE_EXIT();
		return TRUE;
	}
	else
	{
		privPixmap->isFrameBuffer = FALSE;
	}

	if ( pPixData ) 
	{
		if ( privPixmap->mem_info != NULL ) 
		{
			TRACE_EXIT();
			return TRUE;
		}

		TRACE_EXIT();
		return FALSE;
	}

	pPixmap->devKind = ( (pPixmap->drawable.width*pPixmap->drawable.bitsPerPixel) + 7 ) / 8;
	pPixmap->devKind = MALI_ALIGN( pPixmap->devKind, 8 );

	size = exaGetPixmapPitch(pPixmap) * pPixmap->drawable.height;

	/* allocate pixmap data */
	mem_info = privPixmap->mem_info;

	if ( mem_info && mem_info->usize == size ) 
	{
		TRACE_EXIT();
		return TRUE;
	}

	if ( mem_info && mem_info->usize != 0 )
	{
		ioctl(	MALIPTR(xf86Screens[pPixmap->drawable.pScreen->myNum])->hwmem_fd,
			HWMEM_RELEASE_IOC,
			mem_info->hwmem_alloc);

		mem_info->hwmem_alloc = 0;
		mem_info->hwmem_global_name = 0;
		memset(privPixmap, 0, sizeof(*privPixmap));

		TRACE_EXIT();
		return TRUE;
	}

	if (!size) 
	{
		TRACE_EXIT();
		return TRUE;
	}

	if ( NULL == mem_info )
	{
		mem_info = calloc(1, sizeof(*mem_info));
		if (!mem_info) 
		{
			xf86DrvMsg(mi.pScrn->scrnIndex, X_ERROR, "[%s:%d] failed to allocate memory metadata\n", __FUNCTION__, __LINE__);
			TRACE_EXIT();
			return FALSE;
		}
	}

	{
		struct hwmem_alloc_request args;
		args.size = size;
		args.flags = HWMEM_ALLOC_CACHED;
		args.default_access = HWMEM_ACCESS_READ | HWMEM_ACCESS_WRITE | HWMEM_ACCESS_IMPORT;
		args.mem_type = HWMEM_MEM_CONTIGUOUS_SYS;
		mem_info->hwmem_alloc = ioctl(	MALIPTR(xf86Screens[pPixmap->drawable.pScreen->myNum])->hwmem_fd,
						HWMEM_ALLOC_IOC,
						&args);

		if ( 0 == mem_info->hwmem_alloc )
		{
			xf86DrvMsg(mi.pScrn->scrnIndex, X_ERROR, "[%s:%d] failed to allocate hwmem memory (%i bytes)\n", __FUNCTION__, __LINE__, size);
			TRACE_EXIT();
			return FALSE;
		}

	}

	mem_info->hwmem_global_name = ioctl(  	MALIPTR(xf86Screens[pPixmap->drawable.pScreen->myNum])->hwmem_fd,
						HWMEM_EXPORT_IOC,
						mem_info->hwmem_alloc);

	if ( 0 == mem_info->hwmem_global_name )
	{
		xf86DrvMsg(mi.pScrn->scrnIndex, X_ERROR, "[%s:%d] failed to export global hwmem name\n", __FUNCTION__, __LINE__);
		TRACE_EXIT();		
		return FALSE;
	}

	mem_info->usize = size;
	privPixmap->mem_info = mem_info;
	privPixmap->mem_info->usize = size;
	privPixmap->addr = NULL;
	privPixmap->bits_per_pixel = 16;

	TRACE_EXIT();

	return TRUE;
}

static Bool maliPixmapIsOffscreen( PixmapPtr pPix )
{
	ScreenPtr pScreen = pPix->drawable.pScreen;
	PrivPixmap *privPixmap = (PrivPixmap *)exaGetPixmapDriverPrivate(pPix);

	TRACE_ENTER();

	if (pScreen->GetScreenPixmap(pScreen) == pPix ) 
	{
		TRACE_EXIT();
		return TRUE;
	}

	if ( privPixmap )
	{
		TRACE_EXIT();
		return pPix->devPrivate.ptr ? FALSE : TRUE;
	}

	TRACE_EXIT();

	return FALSE;
}

static Bool maliPrepareAccess(PixmapPtr pPix, int index)
{
	PrivPixmap *privPixmap = (PrivPixmap *)exaGetPixmapDriverPrivate(pPix);
	mali_mem_info *mem_info;

	TRACE_ENTER();
	IGNORE( index );

	if ( !privPixmap ) 
	{
		xf86DrvMsg(mi.pScrn->scrnIndex, X_ERROR, "[%s:%d] Failed to get private pixmap data\n", __FUNCTION__, __LINE__);
		TRACE_EXIT();
		return FALSE;
	}


	mem_info = privPixmap->mem_info;
	if ( NULL != mem_info )
	{
		struct hwmem_set_domain_request args;
		args.id = mem_info->hwmem_alloc;
		args.domain = HWMEM_DOMAIN_CPU;
		args.access = HWMEM_ACCESS_READ | HWMEM_ACCESS_WRITE | HWMEM_ACCESS_IMPORT;

		/* using memset avoids API compatibility problems with the skip/offset field. */
		/* skip/offset = 0; start = 0; */
		memset(&args.region, 0, sizeof(args.region));
		args.region.count = 1;
		args.region.end = mem_info->usize;
		args.region.size = mem_info->usize;
		ioctl(	MALIPTR(xf86Screens[pPix->drawable.pScreen->myNum])->hwmem_fd,
			HWMEM_SET_DOMAIN_IOC,
			&args);

		if (!privPixmap->addr)
		{
			privPixmap->addr = mmap(NULL, mem_info->usize, PROT_READ | PROT_WRITE,
						MAP_SHARED, MALIPTR(xf86Screens[pPix->drawable.pScreen->myNum])->hwmem_fd,
						(off_t)mem_info->hwmem_alloc);
		}
	}
	else
	{
		xf86DrvMsg(mi.pScrn->scrnIndex, X_ERROR, "[%s:%d] No mem_info on pixmap\n", __FUNCTION__, __LINE__);
		TRACE_EXIT();
		return FALSE;
	}

	pPix->devPrivate.ptr = privPixmap->addr;
	if ( NULL == pPix->devPrivate.ptr ) 
	{
		xf86DrvMsg(mi.pScrn->scrnIndex, X_ERROR, "[%s:%d] cpu address not set\n", __FUNCTION__, __LINE__);
		TRACE_EXIT();
		return FALSE;
	}

	TRACE_EXIT();

	return TRUE;
}

static void maliFinishAccess(PixmapPtr pPix, int index)
{
	PrivPixmap *privPixmap = (PrivPixmap *)exaGetPixmapDriverPrivate(pPix);

	TRACE_ENTER();
	IGNORE( index );

	if ( !privPixmap ) 
	{
		TRACE_EXIT();
		return;
	}

	if ( !pPix ) 
	{
		TRACE_EXIT();
		return;
	}

	pPix->devPrivate.ptr = NULL;

	TRACE_EXIT();
}

static Bool maliCheckComposite( int op, PicturePtr pSrcPicture, PicturePtr pMaskPicture, PicturePtr pDstPicture )
{
	TRACE_ENTER();
	IGNORE( op );
	IGNORE( pSrcPicture );
	IGNORE( pMaskPicture );
	IGNORE( pDstPicture );
	TRACE_EXIT();

	return FALSE;
}

static Bool maliPrepareComposite( int op, PicturePtr pSrcPicture, PicturePtr pMaskPicture, PicturePtr pDstPicture, PixmapPtr pSrcPixmap, PixmapPtr pMask, PixmapPtr pDstPixmap )
{
	TRACE_ENTER();
	IGNORE( op );
	IGNORE( pSrcPicture );
	IGNORE( pMaskPicture );
	IGNORE( pDstPicture );
	IGNORE( pSrcPixmap );
	IGNORE( pMask );
	IGNORE( pDstPixmap );
	TRACE_EXIT();

	return FALSE;
}

static void maliComposite( PixmapPtr pDstPixmap, int srcX, int srcY, int maskX, int maskY, int dstX, int dstY, int width, int height)
{
	TRACE_ENTER();
	IGNORE( pDstPixmap );
	IGNORE( srcX );
	IGNORE( srcY );
	IGNORE( maskX );
	IGNORE( maskY );
	IGNORE( dstX );
	IGNORE( dstY );
	IGNORE( width );
	IGNORE( height );
	TRACE_EXIT();
}

static void maliDoneComposite( PixmapPtr pDst )
{
	TRACE_ENTER();
	IGNORE( pDst );
	TRACE_EXIT();
}


static void maliDumpInfo()
{
	xf86DrvMsg(mi.pScrn->scrnIndex, X_INFO, "XRES: %i YRES: %i PHYS: 0x%x VIRT: 0x%x\n", mi.fb_xres, mi.fb_yres, (int)mi.fb_phys, (int)mi.fb_virt);
}

Bool maliSetupExa( ScreenPtr pScreen, ExaDriverPtr exa, int xres, int yres, unsigned char *virt )
{
	ScrnInfoPtr pScrn = xf86Screens[pScreen->myNum];
	MaliPtr fPtr = MALIPTR(pScrn);

	if ( NULL == exa ) return FALSE;

	memset(&mi, 0, sizeof(mi));
	mi.pScrn = pScrn;
	mi.fb_xres = xres;
	mi.fb_yres = yres;
	mi.fb_phys = pScrn->memPhysBase;
	mi.fb_virt = virt;

	TRACE_ENTER();

	maliDumpInfo();

	exa->exa_major = 2;
	exa->exa_minor = 0;
	exa->memoryBase = fPtr->fbmem;
	exa->maxX = fPtr->fb_lcd_var.xres_virtual;
	exa->maxY = fPtr->fb_lcd_var.yres_virtual;
	exa->flags = EXA_OFFSCREEN_PIXMAPS | EXA_HANDLES_PIXMAPS | EXA_SUPPORTS_PREPARE_AUX;
	exa->offScreenBase = (fPtr->fb_lcd_fix.line_length*fPtr->fb_lcd_var.yres);
	exa->memorySize = fPtr->fb_lcd_fix.smem_len;
	exa->pixmapOffsetAlign = 4096;
	exa->pixmapPitchAlign = 8;

	fd_fbdev = fPtr->fb_lcd_fd;

	maliDumpInfo();

	MALI_EXA_FUNC(PrepareSolid);
	MALI_EXA_FUNC(Solid);
	MALI_EXA_FUNC(DoneSolid);

	MALI_EXA_FUNC(PrepareCopy);
	MALI_EXA_FUNC(Copy);
	MALI_EXA_FUNC(DoneCopy);

	MALI_EXA_FUNC(CheckComposite);
	MALI_EXA_FUNC(PrepareComposite);
	MALI_EXA_FUNC(Composite);
	MALI_EXA_FUNC(DoneComposite);

	MALI_EXA_FUNC(WaitMarker);

	MALI_EXA_FUNC(CreatePixmap);
	MALI_EXA_FUNC(DestroyPixmap);
	MALI_EXA_FUNC(ModifyPixmapHeader);
	MALI_EXA_FUNC(PixmapIsOffscreen);

	MALI_EXA_FUNC(PrepareAccess);
	MALI_EXA_FUNC(FinishAccess);

	mi.blt_handle = blt_open();
	if (mi.blt_handle < 0)
		return FALSE;

	xf86DrvMsg(mi.pScrn->scrnIndex, X_INFO, "Mali EXA driver is loaded successfully\n");
	TRACE_EXIT();

	return TRUE;
}
