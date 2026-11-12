/*
 * graphicsRaster.c -- drawing bitmaps (after graphicsRaster.asm:
 * GrDrawBitmap, GrDrawBitmapAtCP, GrFillBitmap, GrFillBitmapAtCP,
 * BitmapSetDrawPoint, BMCheckAllocation, InitBitmapPalette, BMCallBack,
 * BMTrivialReject, CalcLineSize; graphicsRasterDraw.asm: DrawSlice,
 * InitBitmapOperations, BMGetSlice, GetSliceComplex, UncompactPackBits).
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
 * Not yet: scaling and rotation (nothing is drawn), LZG compaction,
 * format conversion for devices of fewer colors than the bitmap, huge
 * bitmaps, graphics strings.
 */
#include "graphicsInt.h"
#include "Win/winInt.h"
#include <stdlib.h>

#define AREA	((word) offsetof(GState, GS_areaAttr))
#define DEF_BITMAP_RES	72

typedef struct {
    GrDraw		*d;
    const Bitmap	*bm;		/* BF_origBM */
    Bitmap *(*cb)(Bitmap *bm);	/* BF_cbFunc */
    Boolean		fill;		/* BMOT_FILL_MASK */
    byte		devType;	/* BF_deviceType */
    sword		x, y;		/* BF_drawPoint */
    PutBitsArgs		args;		/* BF_args */
    byte		pal[256 * 3];
} BMFrame;

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

static const CBitmap *Complex(const Bitmap *bm)
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
    return (Boolean) (GrRoundToWord(vx, &f->x) && GrRoundToWord(vy, &f->y));
}

/* InitBitmapPalette: from the bitmap's palette (a count, then RGB) */
static void InitPalette(BMFrame *f, const Bitmap *bm)
{
    const byte *p = (const byte *) bm + Complex(bm)->CB_palette;
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

static void PutBits(BMFrame *f, sword y)
{
    DriverRegs r;

    memset(&r, 0, sizeof(r));
    r.DR_ax = (word) f->x;
    r.DR_bx = (word) y;
    r.DR_bpPtr = &f->args;
    GrCallDriver(f->d, DR_VID_PUTBITS, &r, AREA);
}

/* the callback's next slice; NULL when there is none */
static const Bitmap *NextSlice(BMFrame *f, const Bitmap *bm)
{
    const CBitmap *c = Complex(bm);

    if ((word) (c->CB_startScan + c->CB_numScans) >= bm->B_height ||
	!f->cb)
	return NULL;
    return f->cb((Bitmap *) bm);
}

/* UncompactPackBits: n bytes from *src to dst */
static void UnpackBits(const byte **src, byte *dst, dword n)
{
    const byte *s = *src;

    while (n) {
	sbyte c = (sbyte) *s++;
	word k;
	if (c < 0) {			/* a byte -c + 1 times */
	    k = (word) (-c + 1);
	    if (k > n)
		k = (word) n;
	    memset(dst, *s++, k);
	} else {			/* c + 1 bytes as they are */
	    k = (word) (c + 1);
	    if (k > n)
		k = (word) n;
	    memcpy(dst, s, k);
	    s += c + 1;
	}
	dst += k;
	n -= k;
    }
    *src = s;
}

/* DrawSlice, for unpacking: each slice of the original unpacked and
 * drawn below the one before */
static void DrawUnpacked(BMFrame *f)
{
    const Bitmap *bm = f->bm;
    word line = GrBitmapLineSize(bm->B_width, bm->B_type);
    sword y = f->y;
    byte *buf = NULL;
    dword size = 0;

    f->args.PBA_bm.B_compact = BMC_UNCOMPACTED;
    while (bm) {
	const byte *src;
	word n;
	dword need;

	if (bm->B_type & BMT_COMPLEX) {
	    src = (const byte *) bm + Complex(bm)->CB_data;
	    n = Complex(bm)->CB_numScans;
	} else {
	    src = (const byte *) (bm + 1);
	    n = bm->B_height;
	}
	need = (dword) n * line;
	if (need > size) {
	    byte *nb = realloc(buf, need);
	    if (!nb)
		break;
	    buf = nb;
	    size = need;
	}
	if (n) {
	    UnpackBits(&src, buf, need);
	    f->args.PBA_data = buf;
	    f->args.PBA_bm.B_height = n;
	    PutBits(f, y);
	    y = (sword) (y + n);
	}
	if (!(bm->B_type & BMT_COMPLEX) || y > f->d->GD_w->W_maskRect.R_bottom)
	    break;
	bm = NextSlice(f, bm);
    }
    free(buf);
}

static void DrawBitmap(GrDraw *d, sword x, sword y, const Bitmap *bm,
		       Bitmap *(*cb)(Bitmap *), Boolean fill)
{
    BMFrame f;
    DriverRegs r;
    const CBitmap *c = Complex(bm);
    Boolean complexBM = (Boolean) ((bm->B_type & BMT_COMPLEX) != 0);

    if (GrTrivialReject(d))
	return;
    memset(&f, 0, sizeof(f));
    f.d = d;
    f.bm = bm;
    f.cb = cb;
    f.fill = fill;
    if ((d->GD_w->W_curTMatrix.TM_flags & TM_COMPLEX) ||
	(complexBM && (c->CB_xres != DEF_BITMAP_RES ||
		       c->CB_yres != DEF_BITMAP_RES)))
	return;					/* scaled: not yet */
    if (Reject(&f, x, y) || !SetDrawPoint(&f, x, y))
	return;
    memset(&r, 0, sizeof(r));
    WinCallVidDriver(d->GD_w, DR_VID_INFO, &r);
    f.devType = r.DR_dsSi ?
	(byte) (((const VideoDriverInfo *) r.DR_dsSi)->VDI_bmFormat &
		BMT_FORMAT) : BMF_24BIT;
    if ((bm->B_type & BMT_FORMAT) != BMF_MONO &&
	(bm->B_type & BMT_FORMAT) < BMF_4CMYK &&
	(bm->B_type & BMT_FORMAT) > f.devType && !fill)
	return;					/* format change: not yet */

    f.args.PBA_bm.B_width = bm->B_width;
    f.args.PBA_bm.B_compact = bm->B_compact;
    f.args.PBA_bm.B_type = (byte) (bm->B_type & ~BMT_COMPLEX);
    f.args.PBA_size = GrBitmapLineSize(bm->B_width, bm->B_type);
    if (bm->B_type & BMT_PALETTE) {
	if (fill)
	    f.args.PBA_flags |= PBF_FILL_MASK;
	else
	    InitPalette(&f, bm);
    } else if (fill)
	f.args.PBA_flags |= PBF_FILL_MASK;

    if (bm->B_compact == BMC_PACKBITS) {
	DrawUnpacked(&f);
	return;
    }
    if (bm->B_compact != BMC_UNCOMPACTED)
	return;					/* LZG: not yet */
    if (!complexBM) {
	f.args.PBA_data = (const byte *) (bm + 1);
	f.args.PBA_bm.B_height = bm->B_height;
	PutBits(&f, f.y);
	return;
    }
    for (;;) {					/* handleComplex */
	sword sy = f.y;
	if (bm != f.bm)
	    sy = (sword) (f.y + Complex(bm)->CB_startScan);
	if (Complex(bm)->CB_numScans) {
	    f.args.PBA_data = (const byte *) bm + Complex(bm)->CB_data;
	    f.args.PBA_bm.B_height = Complex(bm)->CB_numScans;
	    PutBits(&f, sy);
	}
	bm = NextSlice(&f, bm);			/* BMCallBack */
	if (!bm)
	    break;
    }
}

void GrDrawBitmap(GStateHandle gstate, sword x, sword y, const Bitmap *bm,
		  Bitmap *(*callback)(Bitmap *bm))
{
    GrDraw d;

    GrEnterGraphics(gstate, &d);
    GrSetDocPenPos(d.GD_gs, x, y);
    DrawBitmap(&d, x, y, bm, callback, FALSE);
    GrExitGraphics(&d);
}

void GrFillBitmap(GStateHandle gstate, sword x, sword y, const Bitmap *bm,
		  Bitmap *(*callback)(Bitmap *bm))
{
    GrDraw d;

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
    AtCP(gstate, bm, callback, FALSE);
}

void GrFillBitmapAtCP(GStateHandle gstate, const Bitmap *bm,
		      Bitmap *(*callback)(Bitmap *bm))
{
    AtCP(gstate, bm, callback, TRUE);
}
