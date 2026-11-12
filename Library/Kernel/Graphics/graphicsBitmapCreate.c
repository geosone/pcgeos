/*
 * graphicsBitmapCreate.c -- bitmaps to draw into (after
 * graphicsBitmapCreate.asm: GrCreateBitmap, GrCreateBitmapRaw,
 * IsFormatSupported, GrEditBitmap, GrDestroyBitmap, GrSetBitmapMode,
 * GrGetBitmapMode, GrClearBitmap, LockHugeBitmap, GrGetPoint,
 * GrGetBitmap, GetBitmap, GetBitSizeBlock, GrCompactBitmap,
 * CompactPackBits, GrUncompactBitmap,
 * AllocBitmapInitDeviceInfo; graphicsRasterUtils.asm: GrSetBitmapRes,
 * GrGetBitmapRes, GrGetHugeBitmapSize, GrGetBitmapSize).
 *
 * A bitmap is a huge array, a scan line an element, whose directory
 * block is an EditableBitmap (CInclude/Internal/videoDr.h): the
 * CBitmap, the mode, the DOS16 device information, a scratch buffer as
 * the asm reserved it, and the palette of a bitmap with BMT_PALETTE.
 * GrCreateBitmap opens a root window on the memory video driver
 * (GDDT_MEMORY_VIDEO, Driver/Video/Dumb/VidMem) with the array in
 * W_bitmap; the driver finds the bitmap there.  The kernel does not
 * lock the directory block for the driver (EnterGraphics did, into
 * W_bmSegment): the driver locks it itself while W_bmSegment is NULL.
 *
 * Formats: monochrome, 4, 8 and 24 bits, with or without a mask (the
 * DOS16 driver had CMYK too).  GrGetBitmap returns what DR_VID_GETBITS
 * gives, 24 bits on the C drivers (DOS16: the device's format).
 * GrCompactBitmap packs with the rules of CompactPackBits (runs of three
 * and more repeated, at most 128 a run); where a run meets the limits
 * the bytes may differ from the asm's.  Not yet: color transfer
 * (EB_color).
 */
#include "graphicsInt.h"
#include "Win/winInt.h"
#include <hugearr.h>
#include <Internal/videoDr.h>

#define DEF_BITMAP_RES	72
#define VIDMEM_VERSION_MAJOR	1
#define VIDMEM_VERSION_MINOR	0

static const word bitsAndPlanes[] = {	/* nPlanes, nBits */
    0x0101, 0x0401, 0x0801, 0x0803, 0x0104, 0x0104
};
static const byte nColorTable[] = { 1, 4, 8, 24, 4, 4 };

static void Put16(byte *p, word v)
{
    p[0] = (byte) v;
    p[1] = (byte) (v >> 8);
}

/* IsFormatSupported: what the memory video driver draws */
static Boolean FormatSupported(byte format)
{
    return (Boolean) (GeodeGetDefaultDriver(GDDT_MEMORY_VIDEO) != 0 &&
		      format <= BMF_24BIT);
}

/* AllocBitmapInitDeviceInfo: EB_device, the DOS16 VideoDriverInfo
 * (videoDr.def, 32 bytes; the strategy routine is not a 16-bit far
 * pointer here: 0) */
static void InitDeviceInfo(EditableBitmap *eb, word bpScan)
{
    byte *d = eb->EB_device;
    byte f = (byte) (eb->EB_bm.CB_simple.B_type & BMT_FORMAT);

    memset(d, 0, EB_DEVICE_SIZE);
    Put16(d + 4, DA_CHARACTER);			/* DIS_driverAttributes */
    Put16(d + 6, DRIVER_TYPE_VIDEO);		/* DIS_driverType */
    Put16(d + 10, DT_MEMORY);			/* VDI_tech */
    d[12] = VIDMEM_VERSION_MAJOR;
    d[13] = VIDMEM_VERSION_MINOR;
    Put16(d + 14, eb->EB_bm.CB_simple.B_height);	/* VDI_pageH */
    Put16(d + 16, eb->EB_bm.CB_simple.B_width);	/* VDI_pageW */
    Put16(d + 18, DEF_BITMAP_RES);		/* VDI_vRes */
    Put16(d + 20, DEF_BITMAP_RES);		/* VDI_hRes */
    Put16(d + 22, bpScan);			/* VDI_bpScan */
    d[24] = nColorTable[f];			/* VDI_nColors */
    d[25] = (byte) bitsAndPlanes[f];		/* VDI_nPlanes */
    d[26] = (byte) (bitsAndPlanes[f] >> 8);	/* VDI_nBits */
    d[27] = 24;					/* VDI_wColTab */
    d[28] = f;					/* VDI_bmFormat */
}

VMBlockHandle GrCreateBitmapRaw(BMFormat initFormat, word initWidth,
				word initHeight, VMFileHandle vmFile)
{
    byte format = (byte) initFormat;
    word bwidth, sBuffer, header, n, f;
    VMBlockHandle ha;
    EditableBitmap *eb;
    void *dir;

    if (!FormatSupported((byte) (format & BMT_FORMAT)))
	KernelFatal(GRAPHICS_BAD_BITMAP_FORMAT);
    bwidth = GrBitmapLineSize(initWidth, format);
    sBuffer = (word) (2 * initHeight > bwidth ? 2 * initHeight : bwidth);
    header = (word) (sizeof(EditableBitmap) + sBuffer);
    f = (word) (format & BMT_FORMAT);
    n = 0;
    if (format & BMT_PALETTE) {
	n = f == BMF_4BIT ? 16 : f == BMF_8BIT ? 256 : 0;
	if (n)
	    header = (word) (header + 2 + 3 * n);
	else
	    format = (byte) (format & ~BMT_PALETTE);
    }
    ha = HugeArrayCreate(vmFile, bwidth, header);
    HugeArrayAppend(vmFile, ha, initHeight, NULL);
    HugeArrayCompressBlocks(vmFile, ha);

    HugeArrayLockDir(vmFile, ha, &dir);
    eb = dir;
    eb->EB_flags = 0;
    eb->EB_color = 0;
    memset(&eb->EB_bm, 0, sizeof(eb->EB_bm));
    eb->EB_bm.CB_simple.B_width = initWidth;
    eb->EB_bm.CB_simple.B_height = initHeight;
    eb->EB_bm.CB_simple.B_type = (byte) (format | BMT_HUGE | BMT_COMPLEX);
    eb->EB_bm.CB_simple.B_compact = BMC_UNCOMPACTED;
    eb->EB_bm.CB_devInfo = (word) (offsetof(EditableBitmap, EB_device) -
				   offsetof(EditableBitmap, EB_bm));
    if (n) {
	/* after the scratch buffer: the count, then the default colors */
	byte *p = (byte *) dir + sizeof(EditableBitmap) + sBuffer;
	eb->EB_bm.CB_palette = (word) (p - (byte *) &eb->EB_bm);
	Put16(p, n);
	memcpy(p + 2, grDefaultPalette, 3u * n);
    }
    eb->EB_bm.CB_xres = eb->EB_bm.CB_yres = DEF_BITMAP_RES;
    InitDeviceInfo(eb, bwidth);
    HugeArrayDirty(dir);
    HugeArrayUnlockDir(dir);
    return ha;
}

/* the root window of a bitmap on the memory video driver, and its
 * GState */
static GStateHandle OpenBitmapWindow(VMFileHandle vmFile, VMBlockHandle ha,
				     word width, word height, optr exposureOD)
{
    WindowHandle win;
    GStateHandle gs;
    Window *w;

    win = WinOpen(GeodeGetDefaultDriver(GDDT_MEMORY_VIDEO), 0, exposureOD,
		  WCF_TRANSPARENT, 0, 0, 0, WPF_ROOT | WPF_INIT_EXCLUDED, 0,
		  0, NULL, 0, 0, (word) (width - 1), (word) (height - 1));
    w = MemLock(win);
    w->W_bitmap = ConstructOptr(vmFile, ha);
    MemUnlock(win);
    gs = GrCreateState(win);
    GrBeginUpdate(gs);			/* nothing invalid */
    GrEndUpdate(gs);
    return gs;
}

VMBlockHandle GrCreateBitmap(BMFormat initFormat, word initWidth,
			     word initHeight, VMFileHandle vmFile,
			     optr exposureOD, GStateHandle *bmgs)
{
    VMBlockHandle ha = GrCreateBitmapRaw(initFormat, initWidth, initHeight,
					 vmFile);
    GStateHandle gs = OpenBitmapWindow(vmFile, ha, initWidth, initHeight,
				       exposureOD);

    GrClearBitmap(gs);
    if (initFormat & BMT_MASK) {
	GrSetBitmapMode(gs, BM_EDIT_MASK, 0);
	GrClearBitmap(gs);
	GrSetBitmapMode(gs, 0, 0);
    }
    if (bmgs)
	*bmgs = gs;
    return ha;
}

/* the scale of a bitmap's resolution against 72 dpi, or new against
 * old */
static WWFixedAsDWord Ratio(word a, word b)
{
    return GrSDivWWFixed((WWFixedAsDWord) a << 16, (WWFixedAsDWord) b << 16);
}

GStateHandle GrEditBitmap(VMFileHandle vmFile, VMBlockHandle vmBlock,
			  optr exposureOD)
{
    void *dir;
    const CBitmap *c;
    word w, h, xres, yres;
    GStateHandle gs;

    HugeArrayLockDir(vmFile, vmBlock, &dir);
    c = EB_BM(dir);
    w = c->CB_simple.B_width;
    h = c->CB_simple.B_height;
    xres = c->CB_xres;
    yres = c->CB_yres;
    HugeArrayUnlockDir(dir);
    gs = OpenBitmapWindow(vmFile, vmBlock, w, h, exposureOD);
    if (xres != DEF_BITMAP_RES || yres != DEF_BITMAP_RES)
	WinApplyScale(GrGetWinHandle(gs), Ratio(xres, DEF_BITMAP_RES),
		      Ratio(yres, DEF_BITMAP_RES), WIF_DONT_INVALIDATE);
    return gs;
}

/* LockHugeBitmap: the directory block of the GState's bitmap; NULL if
 * it has none */
static void *LockBitmap(GStateHandle gstate, VMFileHandle *file,
			VMBlockHandle *block)
{
    GState *gs = MemLock(gstate);
    WindowHandle win = gs ? gs->GS_window : 0;
    Window *w;
    optr bm;
    void *dir;

    if (gs)
	MemUnlock(gstate);
    if (!win)
	return NULL;
    w = MemLock(win);
    bm = w->W_bitmap;
    MemUnlock(win);
    if (!bm)
	return NULL;
    if (file)
	*file = (VMFileHandle) OptrToHandle(bm);
    if (block)
	*block = (VMBlockHandle) OptrToChunk(bm);
    HugeArrayLockDir((VMFileHandle) OptrToHandle(bm),
		     (VMBlockHandle) OptrToChunk(bm), &dir);
    return dir;
}

void GrDestroyBitmap(GStateHandle gstate, BMDestroy flags)
{
    GState *gs = MemLock(gstate);
    WindowHandle win = gs->GS_window;
    optr bm = 0;

    MemUnlock(gstate);
    if (win) {
	Window *w = MemLock(win);
	bm = w->W_bitmap;
	MemUnlock(win);
    }
    WinClose(gstate);
    if (flags != BMD_LEAVE_DATA && bm)
	HugeArrayDestroy((VMFileHandle) OptrToHandle(bm),
			 (VMBlockHandle) OptrToChunk(bm));
}

void GrSetBitmapMode(GStateHandle gstate, word flags, MemHandle colorCorr)
{
    EditableBitmap *eb = LockBitmap(gstate, NULL, NULL);

    if (!eb)
	return;
    if (!(eb->EB_bm.CB_simple.B_type & BMT_MASK))
	flags &= (word) ~BM_EDIT_MASK;
    eb->EB_flags = flags;
    eb->EB_color = 0;		/* no color transfer yet */
    (void) colorCorr;
    HugeArrayDirty(eb);
    HugeArrayUnlockDir(eb);
}

word GrGetBitmapMode(GStateHandle gstate)
{
    EditableBitmap *eb = LockBitmap(gstate, NULL, NULL);
    word flags;

    if (!eb)
	return 0;
    flags = eb->EB_flags;
    HugeArrayUnlockDir(eb);
    return flags;
}

/* white: the data (0 on monochrome, 0xff on the others) or, editing
 * the mask, the mask (0) */
void GrClearBitmap(GStateHandle gstate)
{
    VMFileHandle file;
    VMBlockHandle block;
    EditableBitmap *eb = LockBitmap(gstate, &file, &block);
    word height, width, lineSize, off, n;
    byte type, value;
    void *p;
    word size;
    dword count;

    if (!eb)
	return;
    height = eb->EB_bm.CB_simple.B_height;
    width = eb->EB_bm.CB_simple.B_width;
    type = eb->EB_bm.CB_simple.B_type;
    lineSize = eb->EB_header.HAD_size;
    off = 0;
    n = lineSize;
    value = (byte) ((type & BMT_FORMAT) == BMF_MONO ||
		    (type & BMT_FORMAT) >= BMF_4CMYK ? 0 : 0xff);
    if (type & BMT_MASK) {
	word maskBytes = (word) ((width + 7) >> 3);
	if (eb->EB_flags & BM_EDIT_MASK) {
	    n = maskBytes;
	    value = 0;
	} else {
	    off = maskBytes;
	    n = (word) (lineSize - maskBytes);
	}
    }
    HugeArrayUnlockDir(eb);
    if (!height)
	return;
    count = HAL_COUNT(HugeArrayLock(file, block, 0, &p, &size));
    while (count) {
	memset((byte *) p + off, value, n);
	HugeArrayDirty(p);
	if (!--height)
	    break;
	count = HugeArrayNext(&p, &size);
    }
    HugeArrayUnlock(p);
}

RGBColorAsDWord GrGetPoint(GStateHandle gstate, sword x, sword y)
{
    GrDraw d;
    DriverRegs r;
    sword dx, dy;
    RGBColorAsDWord c = 0;

    GrEnterGraphics(gstate, &d);
    if (d.GD_w && !(d.GD_w->W_grFlags & WGF_MASK_NULL) &&
	GrTransDev(&d, x, y, &dx, &dy) &&
	dx >= d.GD_w->W_winRect.R_left && dx <= d.GD_w->W_winRect.R_right &&
	dy >= d.GD_w->W_winRect.R_top && dy <= d.GD_w->W_winRect.R_bottom) {
	memset(&r, 0, sizeof(r));
	r.DR_ax = (word) dx;
	r.DR_bx = (word) dy;
	if (!WinCallVidDriver(d.GD_w, DR_VID_GETPIXEL, &r))
	    /* al red, ah raw, bl green, bh blue */
	    c = (byte) r.DR_ax | ((dword) (word) r.DR_bx << 8) |
		((dword) (byte) (r.DR_ax >> 8) << 24);
    }
    GrExitGraphics(&d);
    return c;
}

void GrSetBitmapRes(GStateHandle gstate, word xRes, word yRes)
{
    XYValueAsDWord old = GrGetBitmapRes(gstate);
    EditableBitmap *eb;

    if (!old)
	return;
    WinApplyScale(GrGetWinHandle(gstate), Ratio(xRes, (word) old),
		  Ratio(yRes, (word) (old >> 16)), WIF_DONT_INVALIDATE);
    eb = LockBitmap(gstate, NULL, NULL);
    if (!eb)
	return;
    eb->EB_bm.CB_xres = xRes;
    eb->EB_bm.CB_yres = yRes;
    HugeArrayDirty(eb);
    HugeArrayUnlockDir(eb);
}

/* x resolution low, y high; 0 if the GState has no bitmap */
XYValueAsDWord GrGetBitmapRes(GStateHandle gstate)
{
    EditableBitmap *eb = LockBitmap(gstate, NULL, NULL);
    XYValueAsDWord res;

    if (!eb)
	return 0;
    res = ((dword) eb->EB_bm.CB_yres << 16) | eb->EB_bm.CB_xres;
    HugeArrayUnlockDir(eb);
    return res;
}

/* the size in points: the pixels at 72 dpi, others scaled */
static word Points(word pixels, word res)
{
    WWFixedAsDWord v = GrMulWWFixed(GrUDivWWFixed(
				    (WWFixedAsDWord) DEF_BITMAP_RES << 16,
				    (WWFixedAsDWord) res << 16),
				    (WWFixedAsDWord) pixels << 16);

    return (word) ((v + 0x8000) >> 16);
}

XYValueAsDWord GrGetBitmapSize(const Bitmap *bm)
{
    const CBitmap *c = (const CBitmap *) bm;
    word w = bm->B_width, h = bm->B_height;

    if ((bm->B_type & BMT_COMPLEX) &&
	(c->CB_xres != DEF_BITMAP_RES || c->CB_yres != DEF_BITMAP_RES)) {
	w = Points(w, c->CB_xres);
	h = Points(h, c->CB_yres);
    }
    return ((dword) h << 16) | w;
}

XYValueAsDWord GrGetHugeBitmapSize(VMFileHandle vmFile, VMBlockHandle vmBlk)
{
    void *dir;
    XYValueAsDWord s;

    HugeArrayLockDir(vmFile, vmBlk, &dir);
    s = GrGetBitmapSize(&EB_BM(dir)->CB_simple);
    HugeArrayUnlockDir(dir);
    return s;
}

/* ---------------------------------------------------------------------
 * GrGetBitmap
 * ------------------------------------------------------------------- */

MemHandle GrGetBitmap(GStateHandle gstate, sword x, sword y, word width,
		      word height, XYSize *sizeCopied)
{
    GrDraw d;
    DriverRegs r;
    sword dx, dy;
    sdword w = width, h = height;
    dword size;
    MemHandle mh = 0;

    GrEnterGraphics(gstate, &d);
    if (d.GD_w && GrTransDev(&d, x, y, &dx, &dy)) {
	const Window *win = d.GD_w;
	if (win->W_curTMatrix.TM_flags & TM_COMPLEX) {
	    /* GGB_complex: the size as a point, from the window's
	     * origin */
	    sword tw, th;
	    GrTransDev(&d, (sword) width, (sword) height, &tw, &th);
	    w = tw - win->W_winRect.R_left;
	    h = th - win->W_winRect.R_top;
	}
	/* GetBitSizeBlock: within the window */
	if (w > win->W_winRect.R_right - dx + 1)
	    w = win->W_winRect.R_right - dx + 1;
	if (h > win->W_winRect.R_bottom - dy + 1)
	    h = win->W_winRect.R_bottom - dy + 1;
	size = sizeof(Bitmap) + (dword) (w > 0 ? w : 0) * 3 *
	       (dword) (h > 0 ? h : 0);
	if (w > 0 && h > 0 && size <= 0xffff) {
	    mh = MemAlloc((word) size, HF_SHARABLE | HF_SWAPABLE,
			  HAF_ZERO_INIT | HAF_LOCK);
	    if (mh) {
		memset(&r, 0, sizeof(r));
		r.DR_ax = (word) dx;
		r.DR_bx = (word) dy;
		r.DR_cx = (word) w;
		r.DR_dx = (word) h;
		r.DR_si = (word) size;
		r.DR_bpPtr = MemDeref(mh);
		WinCallVidDriver(d.GD_w, DR_VID_GETBITS, &r);
		MemUnlock(mh);
	    }
	}
    }
    GrExitGraphics(&d);
    if (sizeCopied) {
	sizeCopied->XYS_width = (word) (w > 0 ? w : 0);
	sizeCopied->XYS_height = (word) (h > 0 ? h : 0);
    }
    return mh;
}

/* ---------------------------------------------------------------------
 * Compacting
 * ------------------------------------------------------------------- */

/* CompactPackBits: n bytes of src to dst; the size of the result */
static word PackBits(const byte *src, word n, byte *dst)
{
    word i = 0, o = 0;

    while (i < n) {
	word run = 1, j;
	while (i + run < n && run < 128 && src[i + run] == src[i])
	    run++;
	if (run >= 3) {				/* repeated */
	    dst[o++] = (byte) (1 - run);
	    dst[o++] = src[i];
	    i = (word) (i + run);
	    continue;
	}
	for (j = i; j < n && j - i < 128; j++)	/* unique */
	    if (j + 2u < n && src[j] == src[j + 1] && src[j] == src[j + 2])
		break;
	dst[o++] = (byte) (j - i - 1);
	memcpy(dst + o, src + i, (word) (j - i));
	o = (word) (o + (j - i));
	i = j;
    }
    return o;
}

/* the new bitmap's directory: the old one's header (from EB_bm on),
 * compaction and device information as asked */
static VMBlockHandle CopyHeader(VMFileHandle srcFile, VMBlockHandle srcBlock,
				VMFileHandle destFile, word elemSize,
				void **dirOut)
{
    void *src, *dst;
    word end;
    VMBlockHandle nb;

    HugeArrayLockDir(srcFile, srcBlock, &src);
    end = ((LMemBlockHeader *) src)->LMBH_offset;
    nb = HugeArrayCreate(destFile, elemSize, end);
    HugeArrayLockDir(destFile, nb, &dst);
    memcpy((byte *) dst + offsetof(EditableBitmap, EB_bm),
	   (byte *) src + offsetof(EditableBitmap, EB_bm),
	   end - offsetof(EditableBitmap, EB_bm));
    HugeArrayUnlockDir(src);
    *dirOut = dst;
    return nb;
}

VMBlockHandle GrCompactBitmap(VMFileHandle srcFile, VMBlockHandle srcBlock,
			      VMFileHandle destFile)
{
    void *dir, *p;
    CBitmap *c;
    word scan, height, size, n;
    byte *buf;
    VMBlockHandle nb = CopyHeader(srcFile, srcBlock, destFile, 0, &dir);
    MemHandle mh;

    c = EB_BM(dir);
    c->CB_simple.B_compact = BMC_PACKBITS;
    c->CB_devInfo = 0;
    scan = GrBitmapLineSize(c->CB_simple.B_width, c->CB_simple.B_type);
    height = c->CB_simple.B_height;
    HugeArrayDirty(dir);
    HugeArrayUnlockDir(dir);
    mh = MemAlloc((word) (scan + scan / 64 + 4), HF_SWAPABLE, HAF_LOCK);
    if (!mh)
	return nb;
    buf = MemDeref(mh);
    if (height && HAL_COUNT(HugeArrayLock(srcFile, srcBlock, 0, &p, &size))) {
	for (;;) {
	    n = PackBits(p, scan, buf);
	    HugeArrayAppend(destFile, nb, n, buf);
	    if (!--height || !HugeArrayNext(&p, &size))
		break;
	}
	HugeArrayUnlock(p);
    }
    MemFree(mh);
    return nb;
}

VMBlockHandle GrUncompactBitmap(VMFileHandle srcFile, VMBlockHandle srcBlock,
				VMFileHandle destFile)
{
    void *dir, *src, *p;
    word scan, height, size;
    byte *buf;
    MemHandle mh;
    VMBlockHandle nb;
    EditableBitmap *eb;

    HugeArrayLockDir(srcFile, srcBlock, &src);
    scan = GrBitmapLineSize(EB_BM(src)->CB_simple.B_width,
			    EB_BM(src)->CB_simple.B_type);
    HugeArrayUnlockDir(src);
    nb = CopyHeader(srcFile, srcBlock, destFile, scan, &dir);
    eb = dir;
    eb->EB_bm.CB_simple.B_compact = BMC_UNCOMPACTED;
    eb->EB_bm.CB_devInfo = (word) (offsetof(EditableBitmap, EB_device) -
				   offsetof(EditableBitmap, EB_bm));
    InitDeviceInfo(eb, scan);
    height = eb->EB_bm.CB_simple.B_height;
    HugeArrayDirty(dir);
    HugeArrayUnlockDir(dir);
    mh = MemAlloc(scan ? scan : 1, HF_SWAPABLE, HAF_LOCK);
    if (!mh)
	return nb;
    buf = MemDeref(mh);
    if (height && HAL_COUNT(HugeArrayLock(srcFile, srcBlock, 0, &p, &size))) {
	for (;;) {
	    const byte *q = p;
	    GrUnpackBits(&q, buf, scan);
	    HugeArrayAppend(destFile, nb, 1, buf);
	    if (!--height || !HugeArrayNext(&p, &size))
		break;
	}
	HugeArrayUnlock(p);
    }
    MemFree(mh);
    return nb;
}
