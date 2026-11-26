/*
 * graphicsRaster.c -- drawing bitmaps and GrBitBlt (after
 * graphicsRaster.asm: GrBitBlt, BltCommon, CalcDestRegion,
 * CalcInvalRegion, GrDrawBitmap, GrDrawBitmapAtCP, GrFillBitmap,
 * GrFillBitmapAtCP, BitmapSetDrawPoint, BMCheckAllocation,
 * InitBitmapPalette, BMCallBack, BMTrivialReject, CalcLineSize).  The
 * rest of the bitmap code is in the files of its asm files: slices,
 * unpacking and format conversion in graphicsRasterDraw.c, scaling in
 * graphicsRasterScale.c, rotation in graphicsRasterRotate.c, bitmaps in
 * graphics strings and paths in graphicsBitmapString.c, huge bitmaps in
 * graphicsBitmapHuge.c, images in graphicsImage.c.  How it works, for
 * all of them:
 *
 * A bitmap goes to the video driver as DR_VID_PUTBITS at its device
 * position, as it is when it can: uncompacted, no scaling or rotation,
 * the default resolution (72 dpi) and a format the device takes.  A
 * complex bitmap (BMT_COMPLEX) comes in slices: the callback gives the
 * next one (NULL: no more), each drawn CB_startScan below the first.
 *
 * A compacted one (PackBits) is unpacked slice by slice into a buffer
 * first, the scan lines following one another down from the position,
 * until they pass the bottom of the mask.
 *
 * GrFillBitmap draws the 1s of a monochrome bitmap (or the mask) in the
 * area color (PBF_FILL_MASK).  A palette (BMT_PALETTE) goes to a 24-bit
 * device as it is, else as the device's indices of its colors.
 *
 * Scaled (a scaled matrix, or a complex bitmap of another resolution),
 * the scan lines and pixels are picked from the source in steps of the
 * inverse factor, nearest neighbours, as InitBitmapScale and
 * ScaleSlice do; the scaled size takes the fraction of the device
 * position into account.  A negative factor mirrors: to the left of
 * the position in x, upwards from it in y.
 *
 * Rotated, each scaled scan line goes along a line parallel to the
 * rotated top edge (DR_VID_PUTLINE), the lines stepping down the left
 * edge, as RotateBitmap does.
 *
 * GrBitBlt moves what is on the screen (BltCommon, which WinScroll
 * uses too): only what is visible and valid at the source, the rest
 * of the destination invalid; the source left behind invalid
 * (BLTM_MOVE) or washed out (BLTM_CLEAR).
 *
 * Bitmaps of more colors than the device are converted scan line by
 * scan line on their way to the driver (see "Format conversion").
 *
 * Compacted bitmaps: PackBits or LZG (Heap/heapCompress.c).
 *
 * A graphics string stores a bitmap (GrBitmapStringSize) in its element;
 * a complex one with a callback stores its first slice there and each
 * one after it as a GSE_BITMAP_SLICE element (BitmapToString): the
 * size, the CBitmap with its data right after it (CB_data), the data.
 *
 * Huge bitmaps (graphicsBitmapHuge.asm) are drawn like complex ones,
 * each block of the huge array a slice (DrawSlice's note): the
 * directory block stays locked, its CBitmap (EB_bm) is the bitmap, and
 * the slice is the scan lines of one data block, locked in turn.
 */
#include "graphicsInt.h"
#include "graphicsConstant.h"
#include "graphicsMacro.h"
#include "Win/winInt.h"
#include <stdlib.h>
#include <hugearr.h>

/* CalcLineSize: the bytes of a scan line of width pixels */
word GrBitmapLineSize(word width, byte type)
{
    word bits = (word) ((width + 7) >> 3), n;

    switch (type & BMT_FORMAT) {
    case BMF_MONO:
	n = bits;
	break;
    case BMF_4BIT:
	n = (word) ((width + 1) >> 1);
	break;
    case BMF_8BIT:
	n = width;
	break;
    case BMF_24BIT:
	n = (word) (width * 3);
	break;
    default:					/* CMYK: four planes */
	n = (word) (bits << 2);
	break;
    }
    if (type & BMT_MASK)
	n = (word) (n + bits);
    return n;
}

const CBitmap *GrBMComplex(const Bitmap *bm)
{
    return (const CBitmap *) bm;
}

/* BMTrivialReject, without scaling and rotation: TRUE when outside the
 * mask */
static Boolean Reject(BMFrame *f, sword x, sword y)
{
    sword l, t, r, b;
    const Rectangle *m = &f->d->GD_w->W_maskRect;

    if (!GrTransDev(f->d, x, y, &l, &t) ||
	!GrTransDev(f->d, (sword) (x + f->bm->B_width - 1),
		    (sword) (y + f->bm->B_height - 1), &r, &b))
	return TRUE;
    return (Boolean) (l > m->R_right || r < m->R_left || t > m->R_bottom ||
		      b < m->R_top);
}

/* BitmapSetDrawPoint: the device position, rounded */
static Boolean SetDrawPoint(BMFrame *f, sword x, sword y)
{
    int64_t vx = (int64_t) x * 65536, vy = (int64_t) y * 65536;

    GrMatTransDWF(&f->d->GD_w->W_curTMatrix, &vx, &vy);
    f->errX = (sword) (word) ((vx & 0xffff) + 0x8000);
    f->errY = (sword) (word) ((vy & 0xffff) + 0x8000);
    return (Boolean) (GrRoundToWord(vx, &f->x) && GrRoundToWord(vy, &f->y));
}

/* InitBitmapPalette: from the bitmap's palette (a count, then RGB) */
static void InitPalette(BMFrame *f, const Bitmap *bm)
{
    const byte *p = (const byte *) bm + GrBMComplex(bm)->CB_palette;
    word n = (word) (p[0] | (p[1] << 8)), i, last;
    const byte *cur;
    Handle win;

    p += 2;
    if (!n) {
	f->args.PBA_bm.B_type &= (byte) ~BMT_PALETTE;
	return;
    }
    if (f->devType >= BMF_24BIT) {		/* RGB entries as they are */
	f->args.PBA_pal = p;
	return;
    }
    if (n > 256) {
	f->args.PBA_bm.B_type &= (byte) ~BMT_PALETTE;
	return;
    }
    f->args.PBA_flags |= BMPT_INDEX;
    cur = GrLockPalette(f->d->GD_gs, &last, &win);
    for (i = 0; i < n; i++, p += 3)
	f->pal[i] = GrMapRGBtoIndex(cur, last, p[0], p[1], p[2]);
    GrUnlockPalette(win);
    if (n == 16 && f->devType == BMF_4BIT) {	/* two pixels a byte */
	int hi, lo;
	for (hi = 15; hi >= 0; hi--)
	    for (lo = 15; lo >= 0; lo--)
		f->pal[hi * 16 + lo] = (byte) (((f->pal[hi] << 4) & 0xf0) |
					       (f->pal[lo] & 0x0f));
    }
    f->args.PBA_pal = f->pal;
}

void GrBitmapDraw(BMFrame *f, sword x, sword y)
{
    GrDraw *d = f->d;
    const Bitmap *bm = f->bm;
    DriverRegs r;
    const CBitmap *c = GrBMComplex(bm);
    Boolean complexBM = (Boolean) ((bm->B_type & BMT_COMPLEX) != 0 ||
				   f->hfile);
    Boolean fill = f->fill;
    Boolean scaled, rotated;

    rotated = (Boolean) ((d->GD_w->W_curTMatrix.TM_flags & TM_ROTATED) !=
			 0);
    scaled = (Boolean) (rotated ||
			(d->GD_w->W_curTMatrix.TM_flags & TM_SCALED) ||
			(complexBM && (c->CB_xres != DEF_BITMAP_RES ||
				       c->CB_yres != DEF_BITMAP_RES)));
    if (f->image) {			/* the device's pixels, n by n */
	rotated = scaled = FALSE;
	if (!SetDrawPoint(f, x, y) ||
	    f->x > d->GD_w->W_winRect.R_right ||
	    f->y > d->GD_w->W_winRect.R_bottom)
	    return;
    } else if ((scaled ? BMComplexReject(f, x, y) : Reject(f, x, y)) ||
	       !SetDrawPoint(f, x, y))
	return;
    memset(&r, 0, sizeof(r));
    WinCallVidDriver(d->GD_w, DR_VID_INFO, &r);
    f->devType = r.DR_dsSi ?
	(byte) (((const VideoDriverInfo *) r.DR_dsSi)->VDI_bmFormat &
		BMT_FORMAT) : BMF_24BIT;

    f->args.PBA_bm.B_width = bm->B_width;
    f->args.PBA_bm.B_compact = bm->B_compact;
    f->args.PBA_bm.B_type = (byte) (bm->B_type & ~BMT_COMPLEX);
    f->args.PBA_size = GrBitmapLineSize(bm->B_width, bm->B_type);
    if (f->bump > 1) {			/* the pixels' colors as they are */
	if (complexBM && (bm->B_type & BMT_PALETTE) && c->CB_palette) {
	    const byte *p = (const byte *) bm + c->CB_palette;
	    f->imgPalN = (word) (p[0] | (p[1] << 8));
	    f->imgPal = p + 2;
	}
    } else if (!fill && (bm->B_type & BMT_FORMAT) < BMF_4CMYK &&
	(bm->B_type & BMT_FORMAT) > f->devType) {
	InitFormatConversion(f, bm);			/* checkFormatChange */
	f->args.PBA_bm.B_type &= (byte) ~BMT_PALETTE;
    } else if (bm->B_type & BMT_PALETTE) {
	if (fill)
	    f->args.PBA_flags |= PBF_FILL_MASK;
	else
	    InitPalette(f, bm);
    } else if (fill)
	f->args.PBA_flags |= PBF_FILL_MASK;

    if (bm->B_compact > BMC_LZG)
	return;
    if (f->hfile && !GrBMHugeSlice(f, 0))
	return;
    if (rotated) {
	RotateBitmap(f, x, y);
	return;
    }
    if (scaled) {
	ScaleSlice(f);
	return;
    }
    if (bm->B_compact != BMC_UNCOMPACTED) {
	GrBMDrawUnpacked(f);
	return;
    }
    if (!complexBM) {
	f->args.PBA_data = (const byte *) (bm + 1);
	f->args.PBA_bm.B_height = bm->B_height;
	GrBMPutBits(f, f->y);
	return;
    }
    for (;;) {					/* handleComplex */
	sword sy = f->y;
	word n;
	if (f->hfile)
	    sy = (sword) (f->y + f->hfirst);
	else if (bm != f->bm)
	    sy = (sword) (f->y + GrBMComplex(bm)->CB_startScan);
	f->args.PBA_data = GrBMSliceData(f, bm, &n);
	if (n) {
	    f->args.PBA_bm.B_height = n;
	    GrBMPutBits(f, sy);
	}
	bm = GrBMNextSlice(f, bm);			/* BMCallBack */
	if (!bm)
	    break;
    }
}

/* ---------------------------------------------------------------------
 * GrDrawImage (graphicsImage.asm): a bitmap in the device's pixels,
 * each pixel 1, 2, 4, 8 or 16 of them square, whatever the
 * transformation; only the position is transformed.  At size 1 it is
 * the bitmap unscaled.  Larger, each run of pixels of one color is a
 * rectangle in the area attributes with that color (dithered), and
 * IF_BORDER leaves the last row and column of each pixel to a grid in
 * the line attributes.  Masked pixels are left out, unless
 * IF_IGNORE_MASK.  Monochrome: 1 black, 0 white; 4 and 8 bits: the
 * index, or the bitmap's palette entry; 24 bits: R, G, B a pixel (the
 * asm's Get24BitPixelRun read the three a width apart, which no 24-bit
 * bitmap is).
 * ------------------------------------------------------------------- */

#ifndef IF_IGNORE_MASK
#define IF_IGNORE_MASK	0x10
#endif

static void DrawBitmap(GrDraw *d, sword x, sword y, const Bitmap *bm,
		       Bitmap *(*cb)(Bitmap *), Boolean fill)
{
    BMFrame f;

    if (GrTrivialReject(d))
	return;
    memset(&f, 0, sizeof(f));
    f.d = d;
    f.bm = bm;
    f.cb = cb;
    f.fill = fill;
    GrBitmapDraw(&f, x, y);
    GrBitmapDoneConvert(&f);
}

void GrDrawBitmap(GStateHandle gstate, sword x, sword y, const Bitmap *bm,
		  Bitmap *(*callback)(Bitmap *bm))
{
    GrDraw d;

    if (GrPathRect(gstate, FALSE, x, y, GrGetBitmapSize(bm)))
	return;
    if (BitmapToString(gstate, GR_DRAW_BITMAP, FALSE, x, y, bm, callback))
	callback = NULL;
    GrEnterGraphics(gstate, &d);
    GrSetDocPenPos(d.GD_gs, x, y);
    DrawBitmap(&d, x, y, bm, callback, FALSE);
    GrExitGraphics(&d);
}

void GrFillBitmap(GStateHandle gstate, sword x, sword y, const Bitmap *bm,
		  Bitmap *(*callback)(Bitmap *bm))
{
    GrDraw d;

    if (GrPathRect(gstate, FALSE, x, y, GrGetBitmapSize(bm)))
	return;
    if (BitmapToString(gstate, GR_FILL_BITMAP, FALSE, x, y, bm, callback))
	callback = NULL;
    GrEnterGraphics(gstate, &d);
    GrSetDocPenPos(d.GD_gs, x, y);
    DrawBitmap(&d, x, y, bm, callback, TRUE);
    GrExitGraphics(&d);
}

/* GetDocPenPos: the pen, rounded to document coordinates */
static void AtCP(GStateHandle gstate, const Bitmap *bm,
		 Bitmap *(*callback)(Bitmap *bm), Boolean fill)
{
    XYValueAsDWord pen = GrGetCurPos(gstate);
    GrDraw d;

    GrEnterGraphics(gstate, &d);
    DrawBitmap(&d, (sword) (word) pen, (sword) (word) (pen >> 16), bm,
	       callback, fill);
    GrExitGraphics(&d);
}

void GrDrawBitmapAtCP(GStateHandle gstate, const Bitmap *bm,
		      Bitmap *(*callback)(Bitmap *bm))
{
    if (GrPathRect(gstate, TRUE, 0, 0, GrGetBitmapSize(bm)))
	return;
    if (BitmapToString(gstate, GR_DRAW_BITMAP_CP, TRUE, 0, 0, bm, callback))
	callback = NULL;
    AtCP(gstate, bm, callback, FALSE);
}

void GrFillBitmapAtCP(GStateHandle gstate, const Bitmap *bm,
		      Bitmap *(*callback)(Bitmap *bm))
{
    if (GrPathRect(gstate, TRUE, 0, 0, GrGetBitmapSize(bm)))
	return;
    if (BitmapToString(gstate, GR_FILL_BITMAP_CP, TRUE, 0, 0, bm, callback))
	callback = NULL;
    AtCP(gstate, bm, callback, TRUE);
}

/* ---------------------------------------------------------------------
 * GrBitBlt (graphicsRaster.asm: GrBitBlt, BltCommon, ScaleScalarSimple,
 * CalcDestRegion, CalcInvalRegion)
 * ------------------------------------------------------------------- */

#define REG(wh, ch)	ConstructOptr((wh), (ch))

/* CalcDestRegion: the mask becomes where the copy is good, the source
 * visible and valid moved to the destination, within the mask; the old
 * mask goes to W_temp2Reg */
static void DestRegion(WindowHandle wh, const Rectangle *src, sword sx,
		       sword sy)
{
    Window *w = MemDeref(wh);
    ChunkHandle t;

    WinRegRect(wh, w->W_temp2Reg, src->R_left, src->R_top, src->R_right,
	       src->R_bottom);
    w = MemDeref(wh);
    WinRegAnd(REG(wh, w->W_temp2Reg), REG(wh, w->W_maskReg),
	      REG(wh, w->W_temp1Reg));
    w = MemDeref(wh);
    if (!WinRegIsNull(wh, w->W_invalReg)) {	/* without the invalid */
	WinRegSub(REG(wh, w->W_temp1Reg), REG(wh, w->W_invalReg),
		  REG(wh, w->W_temp1Reg), REG(wh, w->W_temp2Reg));
	w = MemDeref(wh);
    }
    if (WinRegIsNull(wh, w->W_temp1Reg))
	WinRegNull(wh, w->W_temp2Reg);
    else {
	GrMoveReg(WinReg(wh, w->W_temp1Reg), sx, sy);
	WinRegAnd(REG(wh, w->W_maskReg), REG(wh, w->W_temp1Reg),
		  REG(wh, w->W_temp2Reg));
    }
    w = MemDeref(wh);
    t = w->W_temp2Reg;
    w->W_temp2Reg = w->W_maskReg;
    w->W_maskReg = t;
    WinSetupMaskFlags(w);
}

/* CalcInvalRegion: what of the destination was not copied is invalid;
 * the source left behind, unless BLTM_COPY, invalid (BLTM_MOVE) or
 * washed out (BLTM_CLEAR).  The mask is left not valid. */
static void InvalRegion(WindowHandle wh, const Rectangle *src, sword sx,
			sword sy, BLTMode mode)
{
    Window *w = MemDeref(wh);

    w->W_grFlags &= (WinGrFlags) ~WGF_MASK_VALID;
    WinRegNot(REG(wh, w->W_maskReg), REG(wh, w->W_temp1Reg));
    w = MemDeref(wh);
    WinRegAnd(REG(wh, w->W_temp2Reg), REG(wh, w->W_temp1Reg),
	      REG(wh, w->W_maskReg));
    w = MemDeref(wh);
    WinRegRect(wh, w->W_temp2Reg, (sword) (src->R_left + sx),
	       (sword) (src->R_top + sy), (sword) (src->R_right + sx),
	       (sword) (src->R_bottom + sy));
    w = MemDeref(wh);
    WinRegAnd(REG(wh, w->W_maskReg), REG(wh, w->W_temp2Reg),
	      REG(wh, w->W_temp1Reg));
    w = MemDeref(wh);
    if (!WinRegIsNull(wh, w->W_temp1Reg))
	WinAddToInvalReg(wh);
    if (mode == BLTM_COPY)
	return;
    w = MemDeref(wh);
    WinRegRect(wh, w->W_temp2Reg, src->R_left, src->R_top, src->R_right,
	       src->R_bottom);
    w = MemDeref(wh);
    WinRegAnd(REG(wh, w->W_maskReg), REG(wh, w->W_temp2Reg),
	      REG(wh, w->W_temp1Reg));
    w = MemDeref(wh);
    if (WinRegIsNull(wh, w->W_temp1Reg))
	return;
    if (mode == BLTM_MOVE)
	WinAddToInvalReg(wh);
    else
	WinWashOutReg(wh, w->W_temp1Reg);
}

/*
 * BltCommon: width x height pixels from sx, sy to dx, dy (screen
 * coordinates) in window wh (locked, the tree too); also for WinScroll.
 * Only what is visible and valid at the source is copied, by
 * DR_VID_MOVBITS within that; the rest of the destination becomes
 * invalid.
 */
void GrBltCommon(WindowHandle wh, sword sx, sword sy, sword dx, sword dy,
		 word width, word height, BLTMode mode)
{
    Window *w = MemDeref(wh);
    DriverRegs r;
    Rectangle src;

    memset(&r, 0, sizeof(r));
    r.DR_ax = (word) w->W_winRect.R_left;
    r.DR_bx = (word) w->W_winRect.R_top;
    r.DR_cx = (word) w->W_winRect.R_right;
    r.DR_dx = (word) w->W_winRect.R_bottom;
    WinCallVidDriver(w, DR_VID_COLLIDE_UNDER, &r);
    if (!width || !height)
	return;
    src.R_left = sx;
    src.R_top = sy;
    src.R_right = (sword) (sx + width - 1);
    src.R_bottom = (sword) (sy + height - 1);
    DestRegion(wh, &src, (sword) (dx - sx), (sword) (dy - sy));
    w = MemDeref(wh);
    if (!(w->W_grFlags & WGF_MASK_NULL) &&
	dx <= w->W_maskRect.R_right &&
	(sword) (dx + width - 1) >= w->W_maskRect.R_left &&
	dy <= w->W_maskRect.R_bottom &&
	(sword) (dy + height - 1) >= w->W_maskRect.R_top) {
	memset(&r, 0, sizeof(r));
	r.DR_ax = (word) sx;
	r.DR_bx = (word) sy;
	r.DR_cx = (word) dx;
	r.DR_dx = (word) dy;
	r.DR_si = width;
	r.DR_bp = height;
	WinCallVidDriver(w, DR_VID_MOVBITS, &r);
    }
    InvalRegion(wh, &src, (sword) (dx - sx), (sword) (dy - sy), mode);
}

/* ScaleScalarSimple: a size through the scale of the matrix, rounded */
static word ScaleSize(word v, const WWFixed *f)
{
    sdword s = (sdword) (((dword) (word) f->WWF_int << 16) | f->WWF_frac);

    return (word) (((int64_t) v * s + 0x8000) >> 16);
}

void GrBitBlt(GStateHandle gstate, sword sourceX, sword sourceY,
	      sword destX, sword destY, word width, word height,
	      BLTMode mode)
{
    GrDraw d;
    sword sx, sy, dx, dy;

    GrEnterGraphics(gstate, &d);
    GrSetDocPenPos(d.GD_gs, destX, destY);
    if (d.GD_w && !(d.GD_w->W_grFlags & WGF_MASK_NULL) &&
	GrTransDev(&d, sourceX, sourceY, &sx, &sy) &&
	GrTransDev(&d, destX, destY, &dx, &dy)) {
	if (d.GD_w->W_curTMatrix.TM_flags & TM_COMPLEX) {
	    width = ScaleSize(width, &d.GD_w->W_curTMatrix.TM_11);
	    height = ScaleSize(height, &d.GD_w->W_curTMatrix.TM_22);
	}
	GrBltCommon(d.GD_win, sx, sy, dx, dy, width, height, mode);
	d.GD_w = MemDeref(d.GD_win);
    }
    GrExitGraphics(&d);
}
