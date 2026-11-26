/*
 * graphicsRasterDraw.c -- after graphicsRasterDraw.asm: Routines to
 * draw/scale bitmaps
 */
#include "graphicsInt.h"
#include "graphicsConstant.h"
#include "graphicsMacro.h"
#include "Win/winInt.h"
#include <stdlib.h>
#include <hugearr.h>

/* the huge bitmap's slice from scan line first on; FALSE past the end */
Boolean GrBMHugeSlice(BMFrame *f, word first)
{
    void *p;
    word size;
    dword r;

    if (f->hdata)
	HugeArrayUnlock((void *) f->hdata);
    f->hdata = NULL;
    f->hfirst = first;
    f->hn = 0;
    if (first >= f->bm->B_height)
	return FALSE;
    r = HugeArrayLock(f->hfile, f->hblock, first, &p, &size);
    if (!HAL_COUNT(r))
	return FALSE;
    f->hdata = p;
    f->hn = (word) HAL_COUNT(r);
    if ((dword) first + f->hn > f->bm->B_height)
	f->hn = (word) (f->bm->B_height - first);
    return TRUE;
}

/* a slice's scan lines (n) and their data */
const byte *GrBMSliceData(const BMFrame *f, const Bitmap *bm, word *n)
{
    if (f->hfile) {
	*n = f->hn;
	return f->hdata;
    }
    if (bm->B_type & BMT_COMPLEX) {
	*n = ((const CBitmap *) bm)->CB_numScans;
	return (const byte *) bm + ((const CBitmap *) bm)->CB_data;
    }
    *n = bm->B_height;
    return (const byte *) (bm + 1);
}

/* ---------------------------------------------------------------------
 * Format conversion for devices of fewer colors (graphicsRasterDraw.asm:
 * InitFormatConversion, ChangeBitmapFormat and their routines VGAtoMono,
 * SVGAtoMono, SVGAtoVGA, RGBtoVGA, RGBtoSVGA, RGBtoMono)
 *
 * Each scan line goes to the device's format on its way to the driver,
 * the mask as it is.  To monochrome: a color's luminance picks one of
 * 17 grey draw masks (MapRGBTo16GreyDither; MM_SET, MM_CLEAR and
 * MM_INVERT take black), whose bit at the pixel's x and the scan line's
 * number (from the first converted line, BF_formatScan) makes the pixel
 * white.  To 4 or 8 bits: the nearest of the first 16 or 256 entries of
 * the window's palette.  The colors of an index are the bitmap's palette
 * or the default one.
 * ------------------------------------------------------------------- */

/* CalcLuminance, bit for bit */
static byte Luminance(byte r, byte g, byte b)
{
    byte l = (byte) ((r >> 2) + (r >> 4) + (g >> 1) + (g >> 4) + (b >> 3));
    byte c = (byte) (((r >> 3) & 1) + ((g >> 3) & 1) + ((b >> 2) & 1));

    return (byte) (l + (c >> 1) + (c & 1));
}

/* MapRGBTo16GreyDither: a SystemDrawMask */
static byte GreyDither(const BMFrame *f, byte r, byte g, byte b)
{
    MixMode m = f->d->GD_gs->GS_mixMode;
    byte l;

    if (m == MM_SET || m == MM_CLEAR || m == MM_INVERT)
	return SDM_0;
    l = Luminance(r, g, b);
    return (byte) (SDM_0 - 4 * ((l >> 4) + ((l >> 3) & 1)));
}

/* InitFormatConversion: the table for indexed sources */
void InitFormatConversion(BMFrame *f, const Bitmap *bm)
{
    byte src = (byte) (bm->B_type & BMT_FORMAT);
    const byte *pal = grDefaultPalette;
    word i, n = src == BMF_4BIT ? 16 : 256;

    f->conv = TRUE;
    f->srcType = (byte) (bm->B_type & ~(BMT_COMPLEX | BMT_HUGE));
    f->cur = GrLockPalette(f->d->GD_gs, &f->curLast, &f->curWin);
    if (src == BMF_24BIT)
	return;					/* InitRGBtoAll */
    if ((bm->B_type & BMT_PALETTE) && GrBMComplex(bm)->CB_palette) {
	const byte *p = (const byte *) bm + GrBMComplex(bm)->CB_palette;
	word k = (word) (p[0] | (p[1] << 8));
	if (k)
	    pal = p + 2;
	if (k && k < n)
	    n = k;
    }
    for (i = 0; i < n; i++) {
	const byte *e = pal + 3 * i;
	f->convTab[i] = f->devType == BMF_MONO ?
			GreyDither(f, e[0], e[1], e[2]) :
			GrMapRGBtoIndex(f->cur, 15, e[0], e[1], e[2]);
    }
}

/* a pixel of a source line as RGB (24 bits) or its table entry */
static byte ConvPixel(const BMFrame *f, const byte *s, word x, byte *rgb)
{
    switch (f->srcType & BMT_FORMAT) {
    case BMF_4BIT:
	return f->convTab[(x & 1) ? s[x >> 1] & 0x0f : s[x >> 1] >> 4];
    case BMF_8BIT:
	return f->convTab[s[x]];
    default:
	memcpy(rgb, s + 3 * x, 3);
	return 0;
    }
}

/* ChangeBitmapFormat: n lines of width w from src (stride sstride) to
 * the device's format; the size of a converted line */
static word ConvertLines(BMFrame *f, const byte *src, word sstride, word w,
			 word n)
{
    byte dtype = (byte) ((f->srcType & BMT_MASK) | f->devType);
    word dline = GrBitmapLineSize(w, dtype);
    word mask = (f->srcType & BMT_MASK) ? (word) ((w + 7) >> 3) : 0;
    Boolean rgbSrc = (Boolean) ((f->srcType & BMT_FORMAT) == BMF_24BIT);
    dword need = (dword) dline * n;
    word j, x;

    if (need > f->csize) {
	byte *nb = realloc(f->cbuf, need);
	if (!nb)
	    return 0;
	f->cbuf = nb;
	f->csize = need;
    }
    for (j = 0; j < n; j++, f->fscan++) {
	const byte *s = src + (dword) j * sstride;
	byte *d = f->cbuf + (dword) j * dline;
	memcpy(d, s, mask);
	s += mask;
	d += mask;
	memset(d, 0, dline - mask);
	for (x = 0; x < w; x++) {
	    byte rgb[3], v = ConvPixel(f, s, x, rgb);
	    switch (f->devType) {
	    case BMF_MONO:
		if (rgbSrc)
		    v = GreyDither(f, rgb[0], rgb[1], rgb[2]);
		if (!(grSysDrawMasks[v][f->fscan & 7] & (0x80 >> (x & 7))))
		    d[x >> 3] |= (byte) (0x80 >> (x & 7));
		break;
	    case BMF_4BIT:
		if (rgbSrc)
		    v = GrMapRGBtoIndex(f->cur, 15, rgb[0], rgb[1], rgb[2]);
		d[x >> 1] |= (byte) ((x & 1) ? v & 0x0f : v << 4);
		break;
	    default:				/* RGBtoSVGA */
		d[x] = GrMapRGBtoIndex(f->cur, f->curLast < 255 ?
				       f->curLast : 255, rgb[0], rgb[1],
				       rgb[2]);
		break;
	    }
	}
    }
    return dline;
}

/* the driver gets the lines in the device's format */
void GrBMCallConverted(BMFrame *f, word function, DriverRegs *r,
			  word lines)
{
    const byte *data = f->args.PBA_data;
    word size = f->args.PBA_size;
    byte type = f->args.PBA_bm.B_type;

    if (f->conv) {
	word dl = ConvertLines(f, data, size, f->args.PBA_bm.B_width, lines);
	if (!dl)
	    return;
	f->args.PBA_data = f->cbuf;
	f->args.PBA_size = dl;
	f->args.PBA_bm.B_type = (byte) ((f->srcType & BMT_MASK) |
					f->devType);
    }
    GrCallDriver(f->d, function, r, AREA);
    f->args.PBA_data = data;
    f->args.PBA_size = size;
    f->args.PBA_bm.B_type = type;
}

void GrBMPutBits(BMFrame *f, sword y)
{
    DriverRegs r;

    if (f->bump > 1) {
	ImageHugeSection(f, y);
	return;
    }

    memset(&r, 0, sizeof(r));
    r.DR_ax = (word) f->x;
    r.DR_bx = (word) y;
    r.DR_bpPtr = &f->args;
    GrBMCallConverted(f, DR_VID_PUTBITS, &r, f->args.PBA_bm.B_height);
}

/* the callback's next slice; NULL when there is none */
const Bitmap *GrBMNextSlice(BMFrame *f, const Bitmap *bm)
{
    const CBitmap *c = GrBMComplex(bm);

    if (f->hfile)
	return GrBMHugeSlice(f, (word) (f->hfirst + f->hn)) ? bm : NULL;
    if ((word) (c->CB_startScan + c->CB_numScans) >= bm->B_height ||
	!f->cb)
	return NULL;
    return f->cb((Bitmap *) bm);
}

/* n bytes from *src to dst, PackBits or LZG (a stream after another,
 * as LZGUncompressSource in the asm loop) */
void GrBMUnpack(byte compact, const byte **src, byte *dst, dword n)
{
    if (compact != BMC_LZG) {
	GrUnpackBits(src, dst, n);
	return;
    }
    while (n) {
	dword used, got = LZGUncompressBounded(dst, n, *src, &used);
	*src += used;
	if (!got)
	    break;
	if (got > n)
	    got = n;
	dst += got;
	n -= got;
    }
}

/* UncompactPackBits: n bytes from *src to dst */
void GrUnpackBits(const byte **src, byte *dst, dword n)
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
void GrBMDrawUnpacked(BMFrame *f)
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

	src = GrBMSliceData(f, bm, &n);
	need = (dword) n * line;
	if (need > size) {
	    byte *nb = realloc(buf, need);
	    if (!nb)
		break;
	    buf = nb;
	    size = need;
	}
	if (n) {
	    GrBMUnpack(bm->B_compact, &src, buf, need);
	    f->args.PBA_data = buf;
	    f->args.PBA_bm.B_height = n;
	    GrBMPutBits(f, y);
	    y = (sword) (y + n);
	}
	if (!(bm->B_type & BMT_COMPLEX) && !f->hfile)
	    break;
	if (y > f->d->GD_w->W_maskRect.R_bottom)
	    break;
	bm = GrBMNextSlice(f, bm);
    }
    free(buf);
}

void GrBitmapDoneConvert(BMFrame *f)
{
    if (f->conv)
	GrUnlockPalette(f->curWin);
    free(f->cbuf);
}
