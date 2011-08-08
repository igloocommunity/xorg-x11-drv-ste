/*
 * Copyright (c) 2010  Instituto Nokia de Tecnologia
 * Copyright (c) 2010  STE
 *
 * Authors:
 *           Ricardo Salveti de Araujo <ricardo.salveti@openbossa.org>
 *           Aloisio Almeida <aloisio.almeida@openbossa.org>
 *
 * This adaptor code is based on the draft released by STE, using xkdrive.
 *
 * The driver implements the XVideo adaptor using a video overlay.
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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "mali_fbdev.h"
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <fourcc.h>

#include <blt_api.h>
#include <linux/hwmem.h>
#include <X11/extensions/Xv.h>

#include "mali_exa.h"
#include "exa.h"
#include "damage.h"

#define ARRAY_SIZE(a) (sizeof(a) / sizeof(a[0]))
#define NUM_OVERLAY_PORTS 1
#define ALIGN_VALUE(a) ((a + 15) & ~15)

#define ENTER() DebugF("Enter %s\n", __FUNCTION__)
#define LEAVE() DebugF("Leave %s\n", __FUNCTION__)

typedef struct {
	/** Port attributes Global settings */
	int color_key;
	int autopaint_colorkey;

	/** YUV framerate */
	int src_x, src_y,
	    src_w, src_h,
	    dst_x, dst_y,
	    dst_w, dst_h,
	    img_width, img_height;

	/*int fd_fb;*/
	int blt_handle;
	int hwmem_fd;
	int hwmem_size;
	int buffer_handle;
	void* vaddr;

	Bool hwmem_buffer_initialized;
	RegionRec clip;
	DrawablePtr pDraw;
	int color_format;		/* FOURCC */
} U8500PortPrivRec, *U8500PortPrivPtr;

static XF86VideoEncodingRec DummyEncoding = {
	0, "XV_IMAGE", VIDEO_IMAGE_MAX_WIDTH, VIDEO_IMAGE_MAX_HEIGHT, {1, 1},
};

static XF86VideoFormatRec Formats[] =
{
	{ 15, TrueColor },
	{ 16, TrueColor },
	{ 24, TrueColor },
};

static XF86AttributeRec OverlayAttributes[] = {
	{ XvSettable | XvGettable, 0, 65535, "XV_COLORKEY" },
	{ XvSettable | XvGettable, 0, 1, "XV_AUTOPAINT_COLORKEY" },
};

static XF86ImageRec Images[] =
{
	XVIMAGE_YV12,
	XVIMAGE_I420,
	XVIMAGE_UYVY,
	XVIMAGE_YUY2,
	XVIMAGE_YUMB,
	XVIMAGE_STE0,
};

static int
U8500SetupPrivate(ScreenPtr screen, U8500PortPrivPtr pPriv)
{
	pPriv->hwmem_buffer_initialized = FALSE;
	pPriv->color_key = -1;
	pPriv->autopaint_colorkey = 0;
	pPriv->pDraw = NULL;
	pPriv->hwmem_size = 0;
	pPriv->buffer_handle = -1;
	pPriv->vaddr = 0;
	pPriv->hwmem_fd = (MALIPTR(xf86Screens[screen->myNum]))->hwmem_fd;

	REGION_INIT(screen, &pPriv->clip, NullBox, 0);

	pPriv->blt_handle = blt_open();
	if(pPriv->blt_handle < 0) {
		return -1;
	}

	if(pPriv->hwmem_fd < 0) {
		blt_close(pPriv->blt_handle);
		return -1;
	}

	return 0;
}

static void free_hwmem(U8500PortPrivPtr pPriv)
{
	munmap(pPriv->vaddr,pPriv->hwmem_size);
	(void)ioctl(pPriv->hwmem_fd, HWMEM_RELEASE_IOC, pPriv->buffer_handle);
	pPriv->vaddr = 0;
}

static int alloc_hwmem(U8500PortPrivPtr pPriv, int size)
{
	struct hwmem_alloc_request allocReq;
	allocReq.size = size;
	allocReq.flags = HWMEM_ALLOC_HINT_CACHED;
	allocReq.default_access = HWMEM_ACCESS_READ | HWMEM_ACCESS_WRITE | HWMEM_ACCESS_IMPORT;
	allocReq.mem_type = HWMEM_MEM_CONTIGUOUS_SYS;
	pPriv->buffer_handle = ioctl(pPriv->hwmem_fd, HWMEM_ALLOC_IOC, &allocReq);
	pPriv->hwmem_size = allocReq.size;

	if (pPriv->buffer_handle < 0) {
		ErrorF("Failed to allocate buffer, error code: %d\n", pPriv->buffer_handle);
		return -1;
	}

	pPriv->vaddr = mmap(NULL, pPriv->hwmem_size, PROT_READ | PROT_WRITE , MAP_SHARED, pPriv->hwmem_fd, pPriv->buffer_handle);
	if (pPriv->vaddr == MAP_FAILED) {
		ErrorF("Failed to mmap buffer %d\n", pPriv->buffer_handle);
		(void)ioctl(pPriv->hwmem_fd, HWMEM_RELEASE_IOC, pPriv->buffer_handle);
		return -1;
	}

	return 0;
}

static int getColorFormat(int bitsPerPixel)
{
	switch (bitsPerPixel) {
	case 15:
		return BLT_FMT_16_BIT_ARGB1555;
	case 16:
		return BLT_FMT_16_BIT_RGB565;
	case 24:
		return BLT_FMT_24_BIT_RGB888;
	case 32:
		return BLT_FMT_32_BIT_ARGB8888;
	default:
		ErrorF("Undefined bit depth = %d\n", bitsPerPixel);
		break;
	}
	return 0;
}

void
U8500overlayFreeAdaptor(MaliPtr fbdev, XF86VideoAdaptorPtr adapt)
{
	U8500PortPrivPtr pPriv;
	int i;

	if (adapt && adapt->pPortPrivates) {
		for (i = 0; i < adapt->nPorts; i++) {
			pPriv = adapt->pPortPrivates[i].ptr;
			if (pPriv) {
				blt_close(pPriv->blt_handle);
				free_hwmem(pPriv);
				pPriv->hwmem_fd = 0;
			}
			free(pPriv);
		}
		free(adapt->pPortPrivates);
	}
	free(adapt);
}

static int
U8500overlayPutImage(ScrnInfoPtr screen, short src_x, short src_y,
		short dst_x, short dst_y, short src_w, short src_h,
		short dst_w, short dst_h, int id, unsigned char *buf,
		short width, short height, Bool sync,
		RegionPtr clip_boxes, pointer data,
		DrawablePtr drawable)
{
	U8500PortPrivPtr pPriv = data;
	st_yuvmb_frame_desc* Data = (st_yuvmb_frame_desc *) buf;
	struct blt_req bltreq = {0};
	PixmapPtr pPixmap;
	MaliPtr fPtr;
	int copy_size = 0;

	ENTER();

	pPriv->pDraw = drawable;
	if (dst_w > VIDEO_IMAGE_MAX_WIDTH) dst_w = VIDEO_IMAGE_MAX_WIDTH;
	if (dst_h > VIDEO_IMAGE_MAX_HEIGHT) dst_h = VIDEO_IMAGE_MAX_HEIGHT;

	/* in case of full screen mode x and y should be 0 */
	if ((dst_w == VIDEO_IMAGE_MAX_WIDTH) &&
			(dst_h == VIDEO_IMAGE_MAX_HEIGHT)) {
		if (dst_x || dst_y) {
			dst_x = dst_y = 0;
		}
	}

	if (pPriv->pDraw->type == DRAWABLE_PIXMAP) {
		pPixmap = (PixmapPtr)pPriv->pDraw;
	}
	else {
		pPixmap = pPriv->pDraw->pScreen->GetWindowPixmap((WindowPtr) pPriv->pDraw);
	}

	PrivPixmap *privPixmap = (PrivPixmap *)exaGetPixmapDriverPrivate(pPixmap);

	/*Check if resize, cropping and clipping settings changed.*/

	if ((pPriv->src_x != src_x) || (pPriv->src_y != src_y)
			|| (pPriv->src_w != src_w)
			|| (pPriv->src_h != src_h)
			|| (pPriv->dst_x != dst_x)
			|| (pPriv->dst_y != dst_y)
			|| (pPriv->dst_w != dst_w)
			|| (pPriv->dst_h != dst_h)) {

		if(((pPriv->color_format != FOURCC_YUMB) && (pPriv->color_format != FOURCC_STE0))
					 && pPriv->hwmem_buffer_initialized) {
			free_hwmem(pPriv);
			pPriv->hwmem_buffer_initialized = FALSE;
		}
	}

	/*Populate pPriv with information about the current yuv frame.*/
	pPriv->src_x = src_x;
	pPriv->src_y = src_y;
	pPriv->src_w = src_w;
	pPriv->src_h = src_h;
	pPriv->dst_x = dst_x;
	pPriv->dst_y = dst_y;
	pPriv->dst_w = dst_w;
	pPriv->dst_h = dst_h;
	pPriv->img_width = width;
	pPriv->img_height = height;
	pPriv->color_format = id;

	/*Set the source format*/
	bltreq.src_img.fmt = BLT_FMT_YUV420_PACKED_PLANAR;
	switch (pPriv->color_format) {
		case FOURCC_YV12: /* GUID_YV12_PLANAR */
		case FOURCC_I420: /* GUID_I420_PLANAR */
			bltreq.src_img.fmt = BLT_FMT_YUV420_PACKED_PLANAR;
			copy_size = width*height*3/2;
			break;
		case FOURCC_UYVY:
			bltreq.src_img.fmt = BLT_FMT_CB_Y_CR_Y;
			copy_size = width*height*2;
			break;
		case FOURCC_YUY2:
			bltreq.src_img.fmt = BLT_FMT_Y_CB_Y_CR;
			copy_size = width*height*2;
			break;
		case FOURCC_YUMB:
			bltreq.src_img.fmt = BLT_FMT_YUV420_PACKED_SEMIPLANAR_MB_STE;
			/* supports zero-copy */
			break;
		case FOURCC_STE0:
			bltreq.src_img.fmt = BLT_FMT_CB_Y_CR_Y;
			/* supports zero-copy */
			break;
		default:
			ErrorF("Unknown format = %x\n", pPriv->color_format);
			break;
	}

	/*Setup hwmem buffer*/
	if (((pPriv->color_format != FOURCC_YUMB) && (pPriv->color_format != FOURCC_STE0)) && !pPriv->hwmem_buffer_initialized) {
		if(alloc_hwmem(pPriv, copy_size) < 0)
			return 0;

		pPriv->hwmem_buffer_initialized = TRUE;
	}

	bltreq.size = sizeof(struct blt_req);
	bltreq.transform = BLT_TRANSFORM_NONE;

	bltreq.src_img.buf.type = BLT_PTR_PHYSICAL;
	bltreq.src_img.buf.offset = Data->physicaladdress;
	bltreq.src_img.buf.bits = 0;
	bltreq.src_img.width = ALIGN_VALUE(src_w);
	bltreq.src_img.height = ALIGN_VALUE(src_h);
	bltreq.src_img.pitch = 0;

	bltreq.src_mask.fmt = BLT_FMT_UNUSED;
	bltreq.src_mask.buf.type = BLT_PTR_NONE;

	bltreq.dst_img.fmt = getColorFormat(pPriv->pDraw->bitsPerPixel);
	bltreq.dst_img.pitch = pPixmap->devKind;
	bltreq.dst_img.buf.type = BLT_PTR_HWMEM_BUF_NAME_OFFSET;
	bltreq.dst_img.buf.hwmem_buf_name = privPixmap->mem_info->hwmem_global_name;
	bltreq.dst_img.buf.offset = 0;
	bltreq.dst_img.buf.bits = 0;

	bltreq.src_rect.x = src_x;
	bltreq.src_rect.y = src_y;
	bltreq.src_rect.width = src_w;
	bltreq.src_rect.height = src_h;

	bltreq.dst_rect.x = dst_x;
	bltreq.dst_rect.y = dst_y;
	bltreq.dst_rect.width = dst_w;
	bltreq.dst_rect.height = dst_h;

	if(privPixmap->isFrameBuffer)
	{
		bltreq.dst_img.width = screen->pScreen->width;
		bltreq.dst_img.height = screen->pScreen->height;

		bltreq.dst_clip_rect.x = 0;
		bltreq.dst_clip_rect.y = 0;
		bltreq.dst_clip_rect.width = screen->pScreen->width;
		bltreq.dst_clip_rect.height = screen->pScreen->height;
	}
	else
	{
		// Compositioning window manager case.
		bltreq.dst_rect.x = 0;
		bltreq.dst_rect.y = 0;

		bltreq.dst_img.width = pPriv->pDraw->width;
		bltreq.dst_img.height = pPriv->pDraw->height;
	
		bltreq.dst_clip_rect.x = 0;
		bltreq.dst_clip_rect.y = 0;
		bltreq.dst_clip_rect.width = dst_w;
		bltreq.dst_clip_rect.height = dst_h;
	}

	bltreq.global_alpha = 255;
	bltreq.prio = 4;
	bltreq.flags = BLT_FLAG_ASYNCH | BLT_FLAG_DESTINATION_CLIP;

	if(pPriv->color_format != FOURCC_YUMB && pPriv->color_format != FOURCC_STE0)
	{
		bltreq.src_img.buf.type = BLT_PTR_HWMEM_BUF_NAME_OFFSET;
		bltreq.src_img.buf.hwmem_buf_name = ioctl(pPriv->hwmem_fd, HWMEM_EXPORT_IOC, pPriv->buffer_handle);
		bltreq.src_img.buf.offset = 0;
		memcpy((void *) pPriv->vaddr, (void *) buf, copy_size);
	}

	int status = blt_request(pPriv->blt_handle, &bltreq);
	if(status < 0)
	{
		ErrorF("Blit request failed: %d\n", status);
		return 0;
	}

	(void)blt_synch(pPriv->blt_handle, status);

	fPtr = MALIPTR(screen);
	if (privPixmap->isFrameBuffer) {
		fPtr->fb_lcd_var.yoffset = 0;
		fPtr->fb_lcd_var.activate |= FB_ACTIVATE_FORCE;
		if ( ioctl( fPtr->fb_lcd_fd, FBIOPUT_VSCREENINFO, &fPtr->fb_lcd_var ) < 0 )
		{
			xf86DrvMsg(screen->scrnIndex, X_WARNING, "[%s:%d] failed in FBIOPUT_VSCREENINFO (offset: %i)\n", __FUNCTION__, __LINE__, fPtr->fb_lcd_var.yoffset );
		}
	}

	DamageDamageRegion(drawable, clip_boxes);

	LEAVE();
	return Success;
}

void
U8500StopVideo(ScrnInfoPtr screen, pointer data, Bool exit)
{
	ENTER();
	LEAVE();
}

static int
U8500overlayGetPortAttribute(ScrnInfoPtr screen, Atom attribute,
		INT32 * value, pointer data)
{
	ENTER();
	LEAVE();
	return Success;
}

static int
U8500overlaySetPortAttribute(ScrnInfoPtr screen, Atom attribute,
		INT32 value, pointer data)
{
	ENTER();
	LEAVE();
	return Success;
}

static void
U8500QueryBestSize(ScrnInfoPtr screen, Bool motion,
		short vid_w, short vid_h, short dst_w,
		short dst_h, unsigned int *p_w,
		unsigned int *p_h, pointer data)
{
	*p_w = ALIGN_VALUE(dst_w);
	*p_h = ALIGN_VALUE(dst_h);
}

static int
U8500QueryImageAttributes(ScrnInfoPtr screen, int id, unsigned short *w,
			unsigned short *h, int *pitches, int *offsets)
{
	int size = 0, tmp = 0;

	ENTER();

	DebugF("%s: id:%d, w:%#x, h:%#x, pitches:%#x, offsets:%#x\n",
			__func__, id, w, h, pitches, offsets);

	/* Check if width and height are crossing maximum supported
	 * sizes and roll back if required */
	if (w && (*w > VIDEO_IMAGE_MAX_WIDTH))
		*w = VIDEO_IMAGE_MAX_WIDTH;
	if (h && (*h > VIDEO_IMAGE_MAX_HEIGHT))
		*h = VIDEO_IMAGE_MAX_HEIGHT;

	if (w)
		*w = ALIGN_VALUE(*w);
	if (h)
		*h = ALIGN_VALUE(*h);

	if (offsets) offsets[0] = 0;

	DebugF("%s: id:%x, w:%d, h:%d\n", __func__, id, *w, *h);

	switch (id) {
		case FOURCC_YUMB:
			size = (*w * *h * 3)/2;
			break;
		case FOURCC_STE0:
			size = *w * *h * 2;
			break;
		case FOURCC_YV12:
		case FOURCC_I420:
			if (h) {
				*h = (*h + 1) & ~1;
			}
			size = (*w + 3) & ~3;
			if (pitches) pitches[0] = size;
			size *= *h;
			if (offsets) offsets[1] = size;
			tmp = ((*w >> 1) + 3) & ~3;
			if (pitches) pitches[1] = pitches[2] = tmp;
			tmp *= (*h >> 1);
			size += tmp;
			if (offsets) offsets[2] = size;
			size += tmp;
			break;
		case FOURCC_UYVY:
		case FOURCC_YUY2:
		default:
			size = *w << 1;
			if (pitches) pitches[0] = size;
			size *= *h;
			break;
	}

	LEAVE();
	return size;
}

XF86VideoAdaptorPtr
U8500overlaySetupImageVideo(ScreenPtr screen)
{
	ScrnInfoPtr xf86screen = xf86Screens[screen->myNum];
	MaliPtr fbdev = xf86screen->driverPrivate;
	XF86VideoAdaptorPtr adapt;
	U8500PortPrivPtr pPriv;
	int i;

	xf86DrvMsg(xf86screen, X_INFO, "U8500overlaySetupImageVideo\n" );

	if (!(adapt = calloc(1, sizeof(XF86VideoAdaptorRec))))
		return NULL;

	adapt->type = XvWindowMask | XvInputMask | XvImageMask;
	adapt->flags = VIDEO_CLIP_TO_VIEWPORT | VIDEO_OVERLAID_IMAGES;
	adapt->name = "B2R2 Overlay Video Accelerator";
	adapt->nEncodings = 1;
	adapt->pEncodings = &DummyEncoding;

	adapt->nFormats = ARRAY_SIZE(Formats);
	adapt->pFormats = Formats;

	adapt->nAttributes = ARRAY_SIZE(OverlayAttributes);
	adapt->pAttributes = OverlayAttributes;

	adapt->nImages = ARRAY_SIZE(Images);
	adapt->pImages = Images;

	adapt->pPortPrivates = (DevUnion *)
		calloc(NUM_OVERLAY_PORTS, sizeof(DevUnion));
	if (!adapt->pPortPrivates)
		goto unwind;

	for (i = 0; i < NUM_OVERLAY_PORTS; i++) {
		pPriv = calloc(1, sizeof(U8500PortPrivRec));
		if (!pPriv)
			goto unwind;

		adapt->pPortPrivates[i].ptr = (pointer) pPriv;
		adapt->nPorts++;

		if (U8500SetupPrivate(screen, pPriv) < 0)
			goto unwind;
	}

	adapt->PutVideo = NULL;
	adapt->PutStill = NULL;
	adapt->GetVideo = NULL;
	adapt->GetStill = NULL;
	adapt->PutImage = U8500overlayPutImage;
	adapt->ReputImage = NULL;
	adapt->StopVideo = U8500StopVideo;
	adapt->GetPortAttribute = U8500overlayGetPortAttribute;
	adapt->SetPortAttribute = U8500overlaySetPortAttribute;
	adapt->QueryBestSize = U8500QueryBestSize;
	adapt->QueryImageAttributes = U8500QueryImageAttributes;
	adapt->ClipNotify = NULL;

	fbdev->overlay_adaptor = adapt;

	return adapt;

unwind:
	U8500overlayFreeAdaptor(fbdev, adapt);

	return NULL;
}
