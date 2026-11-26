/*
 * graphicsRasterRotate.c -- after graphicsRasterRotate.asm: This file
 * contains the rotation parts of the kernel raster graphics routines.
 * These will probably eventually be moved to a library.
 */
#include "graphicsInt.h"
#include "graphicsConstant.h"
#include <stdlib.h>

/* CalcBitmapDimensions: the size at 72 dpi, WWFixed */
static WWFixedAsDWord Dimension(word size, word res, Boolean complexBM)
{
    if (!complexBM || res == DEF_BITMAP_RES)
	return (WWFixedAsDWord) size << 16;
    return GrMulWWFixed(GrUDivWWFixed((WWFixedAsDWord) DEF_BITMAP_RES << 16,
				      (WWFixedAsDWord) res << 16),
			(WWFixedAsDWord) size << 16);
}

static Boolean RotCorner(BMFrame *f, int64_t x, int64_t y, sword *dx,
			 sword *dy)
{
    GrMatTransDWF(&f->d->GD_w->W_curTMatrix, &x, &y);
    return (Boolean) (GrRoundToWord(x, dx) && GrRoundToWord(y, dy));
}

static void PutLine(BMFrame *f, sword ax, sword bx, sword cx, sword dx)
{
    DriverRegs r;

    memset(&r, 0, sizeof(r));
    r.DR_ax = (word) ax;
    r.DR_bx = (word) bx;
    r.DR_cx = (word) cx;
    r.DR_dx = (word) dx;
    r.DR_bpPtr = &f->args;
    GrBMCallConverted(f, DR_VID_PUTLINE, &r, 1);
}

static sword Abs(sword v)
{
    return (sword) (v < 0 ? -v : v);
}

/* RotateBitmap, CalcRotatedScaleFactor, BumpCoords: the bitmap scaled
 * to the length of its rotated top edge and the number of steps down
 * its left edge, each scan line put along a line parallel to the top
 * edge (DR_VID_PUTLINE), the lines stepping down the left edge.  Where
 * the left edge steps sideways too, the scan line is put once more
 * before the sideways step, so that no holes are left (this happens
 * after the last line too, as in the asm). */
void RotateBitmap(BMFrame *f, sword x, sword y)
{
    const Bitmap *bm = f->bm;
    Boolean complexBM = (Boolean) ((bm->B_type & BMT_COMPLEX) != 0);
    WWFixedAsDWord w = Dimension(bm->B_width,
				 complexBM ? GrBMComplex(bm)->CB_xres : 0,
				 complexBM);
    WWFixedAsDWord h = Dimension(bm->B_height,
				 complexBM ? GrBMComplex(bm)->CB_yres : 0,
				 complexBM);
    int64_t ox = (int64_t) x << 16, oy = (int64_t) y << 16;
    sword ulx, uly, urx, ury, llx, lly, xbump = 0, ybump = 0;
    sword ax, bx, cx, dx;
    sword adx, ady;
    Boolean flip;
    word fw, fh, j, line;
    sdword slope, cur;
    dword ystep, acc;
    word *xs;
    byte *out;
    BMSource src;

    if (!RotCorner(f, ox + (sdword) w, oy, &urx, &ury) ||
	!RotCorner(f, ox, oy + (sdword) h, &llx, &lly) ||
	!RotCorner(f, ox, oy, &ulx, &uly))
	return;
    adx = Abs((sword) (ulx - urx));
    ady = Abs((sword) (uly - ury));
    flip = (Boolean) (adx >= ady ? ulx > urx : uly >= ury);
    fw = (word) ((adx > ady ? adx : ady) + 1);
    adx = Abs((sword) (ulx - llx));
    ady = Abs((sword) (uly - lly));
    if (adx < ady) {				/* down the left edge in y */
	fh = (word) ady;
	ybump = (sword) (uly < lly ? 1 : -1);
	cur = (sdword) ulx << 16;
	slope = (sdword) GrSDivWWFixed((WWFixedAsDWord) ((sdword) (llx - ulx)
							 << 16),
				       (WWFixedAsDWord) ((sdword) fh << 16));
    } else {					/* in x */
	fh = (word) adx;
	xbump = (sword) (ulx < llx ? 1 : -1);
	cur = (sdword) uly << 16;
	slope = fh ? (sdword) GrSDivWWFixed(
	    (WWFixedAsDWord) ((sdword) (lly - uly) << 16),
	    (WWFixedAsDWord) ((sdword) fh << 16)) : 0;
    }
    if (!fh || (bm->B_type & BMT_FORMAT) > BMF_24BIT)
	return;
    ystep = GrUDivWWFixed((WWFixedAsDWord) bm->B_height << 16,
			  (WWFixedAsDWord) fh << 16);
    line = GrBitmapLineSize(fw, bm->B_type);
    xs = GrBMXSteps(bm, fw, GrUDivWWFixed((WWFixedAsDWord) bm->B_width << 16,
				      (WWFixedAsDWord) fw << 16));
    out = malloc(line);
    memset(&src, 0, sizeof(src));
    src.f = f;
    src.line = GrBitmapLineSize(bm->B_width, bm->B_type);
    if (!xs || !out || !GrBMSourceSlice(&src, bm)) {
	free(xs);
	free(out);
	free(src.buf);
	return;
    }
    f->args.PBA_bm.B_width = fw;
    f->args.PBA_bm.B_height = 1;
    f->args.PBA_bm.B_compact = BMC_UNCOMPACTED;
    f->args.PBA_size = line;
    f->args.PBA_data = out;
    ax = ulx;
    bx = uly;
    cx = urx;
    dx = ury;
    for (j = 0, acc = 0; j < fh; j++, acc += ystep) {
	const byte *s;
	sword r, step;

	if ((acc >> 16) >= bm->B_height ||
	    !(s = GrBMSourceScan(&src, (word) (acc >> 16))))
	    break;
	ScaleOneScan(bm, s, out, xs, fw, flip);
	PutLine(f, ax, bx, cx, dx);
	ax = (sword) (ax + xbump);		/* BumpCoords */
	cx = (sword) (cx + xbump);
	bx = (sword) (bx + ybump);
	dx = (sword) (dx + ybump);
	cur += slope;
	r = (sword) ((cur + ((cur & 0xffff) == 0x8000 && cur < 0 ? 0 :
			     0x8000)) >> 16);
	step = (sword) (r - (ybump ? ax : bx));
	if (step) {
	    PutLine(f, ax, bx, cx, dx);
	    if (ybump) {
		ax = (sword) (ax + step);
		cx = (sword) (cx + step);
	    } else {
		bx = (sword) (bx + step);
		dx = (sword) (dx + step);
	    }
	}
    }
    free(xs);
    free(out);
    free(src.buf);
}
