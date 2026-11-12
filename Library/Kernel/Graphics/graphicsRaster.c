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
 * Scaled (a scaled matrix, or a complex bitmap of another resolution),
 * the scan lines and pixels are picked from the source in steps of the
 * inverse factor, nearest neighbours, as InitBitmapScale and
 * ScaleSlice do; the scaled size takes the fraction of the device
 * position into account.  A negative factor mirrors: to the left of
 * the position in x, upwards from it in y.
 *
 * Not yet: rotation (nothing is drawn), LZG compaction,
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
    sword		errX, errY;	/* BF_drawPointErrorX/Y */
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
    f->errX = (sword) (word) ((vx & 0xffff) + 0x8000);
    f->errY = (sword) (word) ((vy & 0xffff) + 0x8000);
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

/* the source scan lines one after another, through the slices and
 * unpacked (GetSliceComplex) */
typedef struct {
    BMFrame		*f;
    const Bitmap	*bm;		/* the slice */
    const byte		*data;		/* its first scan line */
    word		first, n;	/* its scan lines */
    word		line;		/* BF_origBMscanSize */
    byte		*buf;		/* unpacked */
    dword		size;
} BMSource;

static Boolean SourceSlice(BMSource *s, const Bitmap *bm)
{
    const byte *src;
    dword need;

    s->bm = bm;
    if (bm->B_type & BMT_COMPLEX) {
	src = (const byte *) bm + Complex(bm)->CB_data;
	s->n = Complex(bm)->CB_numScans;
    } else {
	src = (const byte *) (bm + 1);
	s->n = bm->B_height;
    }
    if (bm->B_compact != BMC_PACKBITS) {
	s->data = src;
	return TRUE;
    }
    need = (dword) s->n * s->line;
    if (need > s->size) {
	byte *nb = realloc(s->buf, need);
	if (!nb)
	    return FALSE;
	s->buf = nb;
	s->size = need;
    }
    UnpackBits(&src, s->buf, need);
    s->data = s->buf;
    return TRUE;
}

/* scan line k (not before the one asked last); NULL past the end */
static const byte *SourceScan(BMSource *s, word k)
{
    while ((dword) k >= (dword) s->first + s->n) {
	const Bitmap *next;
	if (!(s->bm->B_type & BMT_COMPLEX))
	    return NULL;
	next = NextSlice(s->f, s->bm);
	if (!next)
	    return NULL;
	s->first = (word) (s->first + s->n);
	if (!SourceSlice(s, next))
	    return NULL;
    }
    return s->data + (dword) (k - s->first) * s->line;
}

/* ScaleMonoScan and the others: pixel i of the scaled line is pixel
 * xs[i] of the source */
static void ScaleBits(const byte *src, byte *dst, const word *xs, word w,
		      byte format)
{
    word i;

    switch (format) {
    case BMF_MONO:
	memset(dst, 0, (word) ((w + 7) >> 3));
	for (i = 0; i < w; i++)
	    if (src[xs[i] >> 3] & (0x80 >> (xs[i] & 7)))
		dst[i >> 3] |= (byte) (0x80 >> (i & 7));
	break;
    case BMF_4BIT:
	memset(dst, 0, (word) ((w + 1) >> 1));
	for (i = 0; i < w; i++) {
	    byte v = (byte) ((xs[i] & 1) ? src[xs[i] >> 1] & 0x0f :
			     src[xs[i] >> 1] >> 4);
	    dst[i >> 1] |= (byte) ((i & 1) ? v : v << 4);
	}
	break;
    case BMF_8BIT:
	for (i = 0; i < w; i++)
	    dst[i] = src[xs[i]];
	break;
    case BMF_24BIT:
	for (i = 0; i < w; i++)
	    memcpy(dst + 3 * i, src + 3 * xs[i], 3);
	break;
    }
}

/* MirrorBitmapBits: one scan line right to left */
static void MirrorBits(byte *p, word w, byte format)
{
    word i, j;

    for (i = 0, j = (word) (w - 1); i < j; i++, j--) {
	switch (format) {
	case BMF_MONO: {
	    byte bi = (byte) ((p[i >> 3] >> (7 - (i & 7))) & 1);
	    byte bj = (byte) ((p[j >> 3] >> (7 - (j & 7))) & 1);
	    p[i >> 3] = (byte) ((p[i >> 3] & ~(0x80 >> (i & 7))) |
				(bj << (7 - (i & 7))));
	    p[j >> 3] = (byte) ((p[j >> 3] & ~(0x80 >> (j & 7))) |
				(bi << (7 - (j & 7))));
	    break;
	}
	case BMF_4BIT: {
	    byte vi = (byte) ((i & 1) ? p[i >> 1] & 0x0f : p[i >> 1] >> 4);
	    byte vj = (byte) ((j & 1) ? p[j >> 1] & 0x0f : p[j >> 1] >> 4);
	    p[i >> 1] = (byte) ((i & 1) ? (p[i >> 1] & 0xf0) | vj :
				(p[i >> 1] & 0x0f) | (vj << 4));
	    p[j >> 1] = (byte) ((j & 1) ? (p[j >> 1] & 0xf0) | vi :
				(p[j >> 1] & 0x0f) | (vi << 4));
	    break;
	}
	case BMF_8BIT: {
	    byte t = p[i];
	    p[i] = p[j];
	    p[j] = t;
	    break;
	}
	case BMF_24BIT: {
	    byte t[3];
	    memcpy(t, p + 3 * i, 3);
	    memcpy(p + 3 * i, p + 3 * j, 3);
	    memcpy(p + 3 * j, t, 3);
	    break;
	}
	}
    }
}

/* InitBitmapScale: the factor of one direction (the matrix and the
 * bitmap's resolution), the scaled size and the step in the source */
static void ScaleFactor(WWFixedAsDWord m, word res, Boolean complexBM,
			word size, sword *err, Boolean *neg, word *final,
			dword *step)
{
    WWFixedAsDWord f = m;

    *neg = (Boolean) ((sdword) f < 0);
    if (*neg) {
	f = (WWFixedAsDWord) -(sdword) f;
	*err = (sword) -*err;
    }
    if (complexBM)
	f = GrMulWWFixed(GrUDivWWFixed((WWFixedAsDWord) DEF_BITMAP_RES << 16,
				       (WWFixedAsDWord) res << 16), f);
    *final = (word) ((GrMulWWFixed((WWFixedAsDWord) size << 16, f) +
		      (word) *err) >> 16);
    *step = f ? GrUDivWWFixed((WWFixedAsDWord) 1 << 16, f) : 0;
}

#define SCALE_LINES	32

/* DrawSlice with BMOT_SCALE: nearest neighbours, the source pixels
 * taken in steps of the inverse factor */
static void DrawScaled(BMFrame *f)
{
    const Bitmap *bm = f->bm;
    const TMatrix *tm = &f->d->GD_w->W_curTMatrix;
    const Rectangle *mask = &f->d->GD_w->W_maskRect;
    Boolean complexBM = (Boolean) ((bm->B_type & BMT_COMPLEX) != 0);
    byte format = (byte) (bm->B_type & BMT_FORMAT);
    word srcMask = (bm->B_type & BMT_MASK) ? (word) ((bm->B_width + 7) >> 3)
					    : 0;
    Boolean negX, negY;
    word fw, fh, line, dstMask, i, j, done;
    dword xstep, ystep, acc;
    word *xs;
    byte *out;
    BMSource src;

    ScaleFactor(*(const WWFixedAsDWord *) &tm->TM_11,
		complexBM ? Complex(bm)->CB_xres : DEF_BITMAP_RES, complexBM,
		bm->B_width, &f->errX, &negX, &fw, &xstep);
    ScaleFactor(*(const WWFixedAsDWord *) &tm->TM_22,
		complexBM ? Complex(bm)->CB_yres : DEF_BITMAP_RES, complexBM,
		bm->B_height, &f->errY, &negY, &fh, &ystep);
    if (!fw || !fh || format > BMF_24BIT)
	return;
    line = GrBitmapLineSize(fw, bm->B_type);
    dstMask = srcMask ? (word) ((fw + 7) >> 3) : 0;
    xs = malloc(fw * sizeof(word));
    out = malloc((dword) line * SCALE_LINES);
    memset(&src, 0, sizeof(src));
    src.f = f;
    src.line = GrBitmapLineSize(bm->B_width, bm->B_type);
    if (!xs || !out || !SourceSlice(&src, bm)) {
	free(xs);
	free(out);
	free(src.buf);
	return;
    }
    for (i = 0, acc = 0, xs[0] = 0; i + 1 < fw; i++) {	/* BuildMasks */
	word si;
	acc += xstep;
	si = (word) (acc >> 16);
	xs[i + 1] = (acc >> 16) < bm->B_width ? si : xs[i];
    }
    if (negX)					/* InitBitmapMasksJumps */
	f->x = (sword) (f->x - fw);
    f->args.PBA_bm.B_width = fw;
    f->args.PBA_bm.B_compact = BMC_UNCOMPACTED;
    f->args.PBA_size = line;
    f->args.PBA_data = out;

    for (j = 0, acc = 0, done = 0; j < fh && !done; ) {
	word n = 0;
	sword y0 = (sword) (negY ? f->y - j : f->y + j);

	if (negY ? y0 < mask->R_top : y0 > mask->R_bottom)
	    break;
	while (n < SCALE_LINES && j < fh) {	/* ScaleOneScan, BumpCurScan */
	    const byte *s;
	    byte *d;
	    if ((acc >> 16) >= bm->B_height ||
		!(s = SourceScan(&src, (word) (acc >> 16)))) {
		done = 1;
		break;
	    }
	    d = out + (dword) (negY ? SCALE_LINES - 1 - n : n) * line;
	    if (srcMask) {
		ScaleBits(s, d, xs, fw, BMF_MONO);
		if (negX)
		    MirrorBits(d, fw, BMF_MONO);
	    }
	    ScaleBits(s + srcMask, d + dstMask, xs, fw, format);
	    if (negX)
		MirrorBits(d + dstMask, fw, format);
	    acc += ystep;
	    n++;
	    j++;
	}
	if (!n)
	    break;
	f->args.PBA_bm.B_height = n;
	if (negY) {				/* upwards from y0 */
	    f->args.PBA_data = out + (dword) (SCALE_LINES - n) * line;
	    PutBits(f, (sword) (y0 - n + 1));
	} else {
	    f->args.PBA_data = out;
	    PutBits(f, y0);
	}
    }
    free(xs);
    free(out);
    free(src.buf);
}

/* BMComplexReject: the four corners of the bitmap (its size at 72 dpi)
 * through the matrix, TRUE when outside the mask */
static Boolean ComplexReject(BMFrame *f, sword x, sword y)
{
    const Bitmap *bm = f->bm;
    const Rectangle *m = &f->d->GD_w->W_maskRect;
    word w = bm->B_width, h = bm->B_height;
    sword cx[2], cy[2], l = 0x7fff, t = 0x7fff, r = -0x8000, b = -0x8000;
    int i;

    if (bm->B_type & BMT_COMPLEX) {
	const CBitmap *c = Complex(bm);
	if (c->CB_xres != DEF_BITMAP_RES)
	    w = (word) ((GrMulWWFixed(GrUDivWWFixed(
		(WWFixedAsDWord) DEF_BITMAP_RES << 16,
		(WWFixedAsDWord) c->CB_xres << 16), (WWFixedAsDWord) w << 16) +
			 0x8000) >> 16);
	if (c->CB_yres != DEF_BITMAP_RES)
	    h = (word) ((GrMulWWFixed(GrUDivWWFixed(
		(WWFixedAsDWord) DEF_BITMAP_RES << 16,
		(WWFixedAsDWord) c->CB_yres << 16), (WWFixedAsDWord) h << 16) +
			 0x8000) >> 16);
    }
    cx[0] = x;
    cx[1] = (sword) (x + w);
    cy[0] = y;
    cy[1] = (sword) (y + h);
    for (i = 0; i < 4; i++) {
	sword dx, dy;
	if (!GrTransDev(f->d, cx[i & 1], cy[i >> 1], &dx, &dy))
	    return TRUE;
	if (dx < l) l = dx;
	if (dx > r) r = dx;
	if (dy < t) t = dy;
	if (dy > b) b = dy;
    }
    return (Boolean) (l > m->R_right || r < m->R_left || t > m->R_bottom ||
		      b < m->R_top);
}

static void DrawBitmap(GrDraw *d, sword x, sword y, const Bitmap *bm,
		       Bitmap *(*cb)(Bitmap *), Boolean fill)
{
    BMFrame f;
    DriverRegs r;
    const CBitmap *c = Complex(bm);
    Boolean complexBM = (Boolean) ((bm->B_type & BMT_COMPLEX) != 0);
    Boolean scaled;

    if (GrTrivialReject(d))
	return;
    memset(&f, 0, sizeof(f));
    f.d = d;
    f.bm = bm;
    f.cb = cb;
    f.fill = fill;
    scaled = (Boolean) ((d->GD_w->W_curTMatrix.TM_flags & TM_SCALED) ||
			(complexBM && (c->CB_xres != DEF_BITMAP_RES ||
				       c->CB_yres != DEF_BITMAP_RES)));
    if (d->GD_w->W_curTMatrix.TM_flags & TM_ROTATED)
	return;					/* rotated: not yet */
    if ((scaled ? ComplexReject(&f, x, y) : Reject(&f, x, y)) ||
	!SetDrawPoint(&f, x, y))
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

    if (bm->B_compact != BMC_UNCOMPACTED && bm->B_compact != BMC_PACKBITS)
	return;					/* LZG: not yet */
    if (scaled) {
	DrawScaled(&f);
	return;
    }
    if (bm->B_compact == BMC_PACKBITS) {
	DrawUnpacked(&f);
	return;
    }
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
