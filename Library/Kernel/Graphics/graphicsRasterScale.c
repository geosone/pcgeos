/*
 * graphicsRasterScale.c -- after graphicsRasterScale.asm: Routines to
 * scale bitmaps
 */
#include "graphicsInt.h"
#include "graphicsConstant.h"
#include "graphicsMacro.h"
#include "Win/winInt.h"
#include <stdlib.h>
#include <hugearr.h>

Boolean GrBMSourceSlice(BMSource *s, const Bitmap *bm)
{
    const byte *src;
    dword need;

    s->bm = bm;
    src = GrBMSliceData(s->f, bm, &s->n);
    if (bm->B_compact == BMC_UNCOMPACTED) {
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
    GrBMUnpack(bm->B_compact, &src, s->buf, need);
    s->data = s->buf;
    return TRUE;
}

/* scan line k (not before the one asked last); NULL past the end */
const byte *GrBMSourceScan(BMSource *s, word k)
{
    while ((dword) k >= (dword) s->first + s->n) {
	const Bitmap *next;
	if (!(s->bm->B_type & BMT_COMPLEX) && !s->f->hfile)
	    return NULL;
	next = GrBMNextSlice(s->f, s->bm);
	if (!next)
	    return NULL;
	s->first = (word) (s->first + s->n);
	if (!GrBMSourceSlice(s, next))
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

/* BuildMonoMasks and the others: the source pixel of each of the fw
 * pixels of a scaled line */
word *GrBMXSteps(const Bitmap *bm, word fw, dword xstep)
{
    word *xs = malloc(fw * sizeof(word)), i;
    dword acc = 0;

    if (!xs)
	return NULL;
    xs[0] = 0;
    for (i = 0; i + 1 < fw; i++) {
	acc += xstep;
	xs[i + 1] = (acc >> 16) < bm->B_width ? (word) (acc >> 16) : xs[i];
    }
    return xs;
}

/* ScaleOneScan (and MirrorBitmapBits): a source scan line to fw pixels,
 * the mask first if there is one */
void ScaleOneScan(const Bitmap *bm, const byte *s, byte *d,
		      const word *xs, word fw, Boolean mirror)
{
    byte format = (byte) (bm->B_type & BMT_FORMAT);
    word srcMask = 0, dstMask = 0;

    if (bm->B_type & BMT_MASK) {
	srcMask = (word) ((bm->B_width + 7) >> 3);
	dstMask = (word) ((fw + 7) >> 3);
	ScaleBits(s, d, xs, fw, BMF_MONO);
	if (mirror)
	    MirrorBits(d, fw, BMF_MONO);
    }
    ScaleBits(s + srcMask, d + dstMask, xs, fw, format);
    if (mirror)
	MirrorBits(d + dstMask, fw, format);
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
void ScaleSlice(BMFrame *f)
{
    const Bitmap *bm = f->bm;
    const TMatrix *tm = &f->d->GD_w->W_curTMatrix;
    const Rectangle *mask = &f->d->GD_w->W_maskRect;
    Boolean complexBM = (Boolean) ((bm->B_type & BMT_COMPLEX) != 0);
    byte format = (byte) (bm->B_type & BMT_FORMAT);
    Boolean negX, negY;
    word fw, fh, line, j, done;
    dword xstep, ystep, acc;
    word *xs;
    byte *out;
    BMSource src;

    ScaleFactor(*(const WWFixedAsDWord *) &tm->TM_11,
		complexBM ? GrBMComplex(bm)->CB_xres : DEF_BITMAP_RES, complexBM,
		bm->B_width, &f->errX, &negX, &fw, &xstep);
    ScaleFactor(*(const WWFixedAsDWord *) &tm->TM_22,
		complexBM ? GrBMComplex(bm)->CB_yres : DEF_BITMAP_RES, complexBM,
		bm->B_height, &f->errY, &negY, &fh, &ystep);
    if (!fw || !fh || format > BMF_24BIT)
	return;
    line = GrBitmapLineSize(fw, bm->B_type);
    xs = GrBMXSteps(bm, fw, xstep);
    out = malloc((dword) line * SCALE_LINES);
    memset(&src, 0, sizeof(src));
    src.f = f;
    src.line = GrBitmapLineSize(bm->B_width, bm->B_type);
    if (!xs || !out || !GrBMSourceSlice(&src, bm)) {
	free(xs);
	free(out);
	free(src.buf);
	return;
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
		!(s = GrBMSourceScan(&src, (word) (acc >> 16)))) {
		done = 1;
		break;
	    }
	    d = out + (dword) (negY ? SCALE_LINES - 1 - n : n) * line;
	    ScaleOneScan(bm, s, d, xs, fw, negX);
	    acc += ystep;
	    n++;
	    j++;
	}
	if (!n)
	    break;
	f->args.PBA_bm.B_height = n;
	if (negY) {				/* upwards from y0 */
	    f->args.PBA_data = out + (dword) (SCALE_LINES - n) * line;
	    GrBMPutBits(f, (sword) (y0 - n + 1));
	} else {
	    f->args.PBA_data = out;
	    GrBMPutBits(f, y0);
	}
    }
    free(xs);
    free(out);
    free(src.buf);
}

/* BMComplexReject: the four corners of the bitmap (its size at 72 dpi)
 * through the matrix, TRUE when outside the mask */
Boolean BMComplexReject(BMFrame *f, sword x, sword y)
{
    const Bitmap *bm = f->bm;
    const Rectangle *m = &f->d->GD_w->W_maskRect;
    word w = bm->B_width, h = bm->B_height;
    sword cx[2], cy[2], l = 0x7fff, t = 0x7fff, r = -0x8000, b = -0x8000;
    int i;

    if (bm->B_type & BMT_COMPLEX) {
	const CBitmap *c = GrBMComplex(bm);
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
