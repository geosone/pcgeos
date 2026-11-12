/*
 * graphicsRaster.c -- drawing bitmaps and GrBitBlt (after
 * graphicsRaster.asm: GrBitBlt, BltCommon, CalcDestRegion,
 * CalcInvalRegion, GrDrawBitmap, GrDrawBitmapAtCP, GrFillBitmap,
 * GrFillBitmapAtCP, BitmapSetDrawPoint, BMCheckAllocation, InitBitmapPalette, BMCallBack,
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
 * Not yet: graphics strings.
 *
 * Huge bitmaps (graphicsBitmapHuge.asm) are drawn like complex ones,
 * each block of the huge array a slice (DrawSlice's note): the
 * directory block stays locked, its CBitmap (EB_bm) is the bitmap, and
 * the slice is the scan lines of one data block, locked in turn.
 */
#include "graphicsInt.h"
#include "Win/winInt.h"
#include <stdlib.h>
#include <hugearr.h>

#define AREA	((word) offsetof(GState, GS_areaAttr))
#define LINE_ATTR	((word) offsetof(GState, GS_lineAttr))
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
    VMFileHandle	hfile;		/* a huge bitmap: its array */
    VMBlockHandle	hblock;
    const byte		*hdata;		/* the locked slice */
    word		hfirst, hn;	/* its scan lines */
    /* format conversion (BMOT_FORMAT) */
    Boolean		conv;
    byte		srcType;	/* BF_origBMtype, simple */
    byte		convTab[256];	/* BF_formatPtr: index to SDM or
					 * index */
    const byte		*cur;		/* the window's palette (RGB to
					 * index) */
    Handle		curWin;
    word		curLast;
    word		fscan;		/* BF_formatScan */
    byte		*cbuf;
    dword		csize;
    /* GrDrawImage */
    ImageFlags		image;		/* BF_imageFlags; 0: a bitmap */
    word		bump;		/* BF_ybump: a pixel's size */
    const byte		*imgPal;	/* the bitmap's palette (RGB) */
    word		imgPalN;
} BMFrame;

/* the huge bitmap's slice from scan line first on; FALSE past the end */
static Boolean HugeSlice(BMFrame *f, word first)
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
static const byte *SliceData(const BMFrame *f, const Bitmap *bm, word *n)
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
static void InitConvert(BMFrame *f, const Bitmap *bm)
{
    byte src = (byte) (bm->B_type & BMT_FORMAT);
    const byte *pal = grDefaultPalette;
    word i, n = src == BMF_4BIT ? 16 : 256;

    f->conv = TRUE;
    f->srcType = (byte) (bm->B_type & ~(BMT_COMPLEX | BMT_HUGE));
    f->cur = GrLockPalette(f->d->GD_gs, &f->curLast, &f->curWin);
    if (src == BMF_24BIT)
	return;					/* InitRGBtoAll */
    if ((bm->B_type & BMT_PALETTE) && Complex(bm)->CB_palette) {
	const byte *p = (const byte *) bm + Complex(bm)->CB_palette;
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
static void CallConverted(BMFrame *f, word function, DriverRegs *r,
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

static void ImageSection(BMFrame *f, sword y);
static void DoneConvert(BMFrame *f);

static void PutBits(BMFrame *f, sword y)
{
    DriverRegs r;

    if (f->bump > 1) {
	ImageSection(f, y);
	return;
    }

    memset(&r, 0, sizeof(r));
    r.DR_ax = (word) f->x;
    r.DR_bx = (word) y;
    r.DR_bpPtr = &f->args;
    CallConverted(f, DR_VID_PUTBITS, &r, f->args.PBA_bm.B_height);
}

/* the callback's next slice; NULL when there is none */
static const Bitmap *NextSlice(BMFrame *f, const Bitmap *bm)
{
    const CBitmap *c = Complex(bm);

    if (f->hfile)
	return HugeSlice(f, (word) (f->hfirst + f->hn)) ? bm : NULL;
    if ((word) (c->CB_startScan + c->CB_numScans) >= bm->B_height ||
	!f->cb)
	return NULL;
    return f->cb((Bitmap *) bm);
}

/* n bytes from *src to dst, PackBits or LZG (a stream after another,
 * as LZGUncompressSource in the asm loop) */
static void Unpack(byte compact, const byte **src, byte *dst, dword n)
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

/* SliceToString: the bytes of a bitmap (its first slice) as a graphics
 * string keeps it, the header, a complex bitmap's palette and the data
 * (packed: as far as its scan lines go); 0 if too large */
word GrBitmapStringSize(const Bitmap *bm)
{
    word lines, line = GrBitmapLineSize(bm->B_width, bm->B_type);
    const byte *data;
    dword size;

    if (bm->B_type & BMT_COMPLEX) {
	lines = Complex(bm)->CB_numScans;
	data = (const byte *) bm + Complex(bm)->CB_data;
    } else {
	lines = bm->B_height;
	data = (const byte *) (bm + 1);
    }
    if (bm->B_compact == BMC_PACKBITS || bm->B_compact == BMC_LZG) {
	byte *scratch = malloc((size_t) line * lines + 1);
	const byte *src = data;
	if (!scratch)
	    return 0;
	Unpack(bm->B_compact, &src, scratch, (dword) line * lines);
	free(scratch);
	size = (dword) (src - (const byte *) bm);
    } else
	size = (dword) (data - (const byte *) bm) + (dword) line * lines;
    return (word) (size <= 0xfff0 ? size : 0);
}

/* the element of a bitmap: at x, y or (atCP) at the pen */
static void RecordBitmap(GStateHandle gstate, byte op, Boolean atCP, sword x,
			 sword y, const Bitmap *bm)
{
    word n;

    if (!GrGStringOf(gstate) || !(n = GrBitmapStringSize(bm)))
	return;
    if (atCP)
	(void) GrGSRecord(gstate, op, "wr", n, bm, n);
    else
	(void) GrGSRecord(gstate, op, "wwwr", x, y, n, bm, n);
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

	src = SliceData(f, bm, &n);
	need = (dword) n * line;
	if (need > size) {
	    byte *nb = realloc(buf, need);
	    if (!nb)
		break;
	    buf = nb;
	    size = need;
	}
	if (n) {
	    Unpack(bm->B_compact, &src, buf, need);
	    f->args.PBA_data = buf;
	    f->args.PBA_bm.B_height = n;
	    PutBits(f, y);
	    y = (sword) (y + n);
	}
	if (!(bm->B_type & BMT_COMPLEX) && !f->hfile)
	    break;
	if (y > f->d->GD_w->W_maskRect.R_bottom)
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
    src = SliceData(s->f, bm, &s->n);
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
    Unpack(bm->B_compact, &src, s->buf, need);
    s->data = s->buf;
    return TRUE;
}

/* scan line k (not before the one asked last); NULL past the end */
static const byte *SourceScan(BMSource *s, word k)
{
    while ((dword) k >= (dword) s->first + s->n) {
	const Bitmap *next;
	if (!(s->bm->B_type & BMT_COMPLEX) && !s->f->hfile)
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

/* BuildMonoMasks and the others: the source pixel of each of the fw
 * pixels of a scaled line */
static word *XSteps(const Bitmap *bm, word fw, dword xstep)
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
static void ScaleLine(const Bitmap *bm, const byte *s, byte *d,
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
static void DrawScaled(BMFrame *f)
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
		complexBM ? Complex(bm)->CB_xres : DEF_BITMAP_RES, complexBM,
		bm->B_width, &f->errX, &negX, &fw, &xstep);
    ScaleFactor(*(const WWFixedAsDWord *) &tm->TM_22,
		complexBM ? Complex(bm)->CB_yres : DEF_BITMAP_RES, complexBM,
		bm->B_height, &f->errY, &negY, &fh, &ystep);
    if (!fw || !fh || format > BMF_24BIT)
	return;
    line = GrBitmapLineSize(fw, bm->B_type);
    xs = XSteps(bm, fw, xstep);
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
	    ScaleLine(bm, s, d, xs, fw, negX);
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
    CallConverted(f, DR_VID_PUTLINE, &r, 1);
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
static void DrawRotated(BMFrame *f, sword x, sword y)
{
    const Bitmap *bm = f->bm;
    Boolean complexBM = (Boolean) ((bm->B_type & BMT_COMPLEX) != 0);
    WWFixedAsDWord w = Dimension(bm->B_width,
				 complexBM ? Complex(bm)->CB_xres : 0,
				 complexBM);
    WWFixedAsDWord h = Dimension(bm->B_height,
				 complexBM ? Complex(bm)->CB_yres : 0,
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
    xs = XSteps(bm, fw, GrUDivWWFixed((WWFixedAsDWord) bm->B_width << 16,
				      (WWFixedAsDWord) fw << 16));
    out = malloc(line);
    memset(&src, 0, sizeof(src));
    src.f = f;
    src.line = GrBitmapLineSize(bm->B_width, bm->B_type);
    if (!xs || !out || !SourceSlice(&src, bm)) {
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
	    !(s = SourceScan(&src, (word) (acc >> 16))))
	    break;
	ScaleLine(bm, s, out, xs, fw, flip);
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

static void Draw(BMFrame *f, sword x, sword y)
{
    GrDraw *d = f->d;
    const Bitmap *bm = f->bm;
    DriverRegs r;
    const CBitmap *c = Complex(bm);
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
    } else if ((scaled ? ComplexReject(f, x, y) : Reject(f, x, y)) ||
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
	InitConvert(f, bm);			/* checkFormatChange */
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
    if (f->hfile && !HugeSlice(f, 0))
	return;
    if (rotated) {
	DrawRotated(f, x, y);
	return;
    }
    if (scaled) {
	DrawScaled(f);
	return;
    }
    if (bm->B_compact != BMC_UNCOMPACTED) {
	DrawUnpacked(f);
	return;
    }
    if (!complexBM) {
	f->args.PBA_data = (const byte *) (bm + 1);
	f->args.PBA_bm.B_height = bm->B_height;
	PutBits(f, f->y);
	return;
    }
    for (;;) {					/* handleComplex */
	sword sy = f->y;
	word n;
	if (f->hfile)
	    sy = (sword) (f->y + f->hfirst);
	else if (bm != f->bm)
	    sy = (sword) (f->y + Complex(bm)->CB_startScan);
	f->args.PBA_data = SliceData(f, bm, &n);
	if (n) {
	    f->args.PBA_bm.B_height = n;
	    PutBits(f, sy);
	}
	bm = NextSlice(f, bm);			/* BMCallBack */
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
#define IF_HUGE_C	0x40

static const byte imageSizes[] = { 1, 2, 4, 8, 16, 1, 1, 1 };

/* the color of pixel x of a scan line (after the mask) into the area
 * attributes; a key to compare runs with */
static dword ImageColor(const BMFrame *f, const byte *pix, word x,
			Boolean set)
{
    GState *gs = f->d->GD_gs;
    byte fmt = (byte) (f->args.PBA_bm.B_type & BMT_FORMAT), i;
    const byte *e;

    switch (fmt) {
    case BMF_MONO:
	i = (byte) ((pix[x >> 3] & (0x80 >> (x & 7))) ? C_BLACK : C_WHITE);
	break;
    case BMF_4BIT:
	i = (byte) ((x & 1) ? pix[x >> 1] & 0x0f : pix[x >> 1] >> 4);
	break;
    case BMF_8BIT:
	i = pix[x];
	break;
    default:
	e = pix + 3 * x;
	if (set)
	    GrSetColorIn(gs, &gs->GS_areaAttr, CF_RGB, e[0], e[1], e[2]);
	return 0x1000000UL | ((dword) e[0] << 16) | ((dword) e[1] << 8) |
	       e[2];
    }
    if (f->imgPal && fmt != BMF_MONO) {
	e = f->imgPal + 3 * (i < f->imgPalN ? i : 0);
	if (set)
	    GrSetColorIn(gs, &gs->GS_areaAttr, CF_RGB, e[0], e[1], e[2]);
    } else if (set)
	GrSetColorIn(gs, &gs->GS_areaAttr, CF_INDEX, i, 0, 0);
    return i;
}

/* DrawImageScan: one scan line at x0, y */
static void ImageScan(BMFrame *f, const byte *line, sword x0, sword y)
{
    const GrDraw *d = f->d;
    word w = f->args.PBA_bm.B_width, n = f->bump, count = 0;
    Boolean masked = (Boolean) ((f->args.PBA_bm.B_type & BMT_MASK) != 0);
    const byte *pix = line + (masked ? (w + 7) >> 3 : 0);
    Boolean border = (Boolean) ((f->image & IF_BORDER) != 0);
    sword x = x0;

    if (f->image & IF_IGNORE_MASK)
	masked = FALSE;
    while (count < w) {
	word run = 1;
	if (masked && !(line[count >> 3] & (0x80 >> (count & 7)))) {
	    while (count + run < w &&
		   !(line[(count + run) >> 3] & (0x80 >> ((count + run) & 7))))
		run++;				/* CheckMaskedPixels: skip */
	} else {
	    dword key = ImageColor(f, pix, count, TRUE);
	    while (count + run < w &&
		   (!masked || (line[(count + run) >> 3] &
				(0x80 >> ((count + run) & 7)))) &&
		   ImageColor(f, pix, (word) (count + run), FALSE) == key)
		run++;
	    GrFillRectLow(d, x, y, (sword) (x + run * n - border),
			  (sword) (y + n - border), AREA);
	}
	count = (word) (count + run);
	x = (sword) (x + run * n);
	if (x > d->GD_w->W_maskRect.R_right)
	    break;
    }
    if (border) {				/* handleBorder */
	word k;
	GrFillRectLow(d, x0, (sword) (y + n - 1), (sword) (x0 + count * n),
		      (sword) (y + n - 1), LINE_ATTR);
	for (k = 0; k < count; k++) {
	    sword bx = (sword) (x0 + n - 1 + k * n);
	    GrFillRectLow(d, bx, y, bx, (sword) (y + n - 1), LINE_ATTR);
	}
    }
}

/* ImageHugeSection: the scan lines of the slice at device line sy (the
 * bitmap's line sy - f->y) */
static void ImageSection(BMFrame *f, sword sy)
{
    const Window *w = f->d->GD_w;
    const byte *line = f->args.PBA_data;
    sword y = (sword) (f->y + (sy - f->y) * (sword) f->bump);
    word k;

    for (k = 0; k < f->args.PBA_bm.B_height; k++) {
	if (y > w->W_maskRect.R_bottom)
	    break;
	if (y + (sword) f->bump > w->W_maskRect.R_top)
	    ImageScan(f, line, f->x, y);
	y = (sword) (y + f->bump);
	line += f->args.PBA_size;
    }
}

static void DrawImage(GrDraw *d, sword x, sword y, ImageFlags flags,
		      const Bitmap *bm, VMFileHandle file, VMBlockHandle block)
{
    GState *gs = d->GD_gs;
    CommonAttr area = gs->GS_areaAttr;
    BMFrame f;
    void *dir = NULL;

    if (GrTrivialReject(d))
	return;
    memset(&f, 0, sizeof(f));
    f.d = d;
    f.image = (ImageFlags) (flags | 0x80);	/* IF_DRAW_IMAGE */
    f.bump = imageSizes[flags & IF_BITSIZE];
    gs->GS_areaAttr.CA_mapMode |= CMM_MAP_TYPE;
    if (file) {
	HugeArrayLockDir(file, block, &dir);
	bm = (const Bitmap *) ((byte *) dir + sizeof(HugeArrayDirectory));
	f.hfile = file;
	f.hblock = block;
    }
    f.bm = bm;
    Draw(&f, x, y);
    DoneConvert(&f);
    if (f.hdata)
	HugeArrayUnlock((void *) f.hdata);
    if (dir)
	HugeArrayUnlockDir(dir);
    gs->GS_areaAttr = area;
}

void GrDrawImage(GStateHandle gstate, sword x, sword y, ImageFlags flags,
		 const Bitmap *bm)
{
    GrDraw d;

    GrEnterGraphics(gstate, &d);
    DrawImage(&d, x, y, (ImageFlags) (flags & ~IF_HUGE_C), bm, 0, 0);
    GrExitGraphics(&d);
}

void GrDrawHugeImage(GStateHandle gstate, sword x, sword y, ImageFlags flags,
		     VMFileHandle vmFile, VMBlockHandle vmBlk)
{
    GrDraw d;

    GrEnterGraphics(gstate, &d);
    DrawImage(&d, x, y, (ImageFlags) (flags | IF_HUGE_C), NULL, vmFile,
	      vmBlk);
    GrExitGraphics(&d);
}

static void DoneConvert(BMFrame *f)
{
    if (f->conv)
	GrUnlockPalette(f->curWin);
    free(f->cbuf);
}

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
    Draw(&f, x, y);
    DoneConvert(&f);
}

/* graphicsBitmapHuge.asm: the bitmap in the huge array's directory
 * block (EditableBitmap: EB_bm after the HugeArrayDirectory), one scan
 * line per element */
static void DrawHuge(GrDraw *d, sword x, sword y, VMFileHandle file,
		     VMBlockHandle block, Boolean fill)
{
    BMFrame f;
    void *dir;

    if (GrTrivialReject(d))
	return;
    memset(&f, 0, sizeof(f));
    HugeArrayLockDir(file, block, &dir);
    f.d = d;
    f.bm = (const Bitmap *) ((byte *) dir + sizeof(HugeArrayDirectory));
    f.fill = fill;
    f.hfile = file;
    f.hblock = block;
    Draw(&f, x, y);
    DoneConvert(&f);
    if (f.hdata)
	HugeArrayUnlock((void *) f.hdata);
    HugeArrayUnlockDir(dir);
}

void GrDrawHugeBitmap(GStateHandle gstate, sword x, sword y,
		      VMFileHandle vmFile, VMBlockHandle vmBlk)
{
    GrDraw d;

    GrEnterGraphics(gstate, &d);
    GrSetDocPenPos(d.GD_gs, x, y);
    DrawHuge(&d, x, y, vmFile, vmBlk, FALSE);
    GrExitGraphics(&d);
}

void GrFillHugeBitmap(GStateHandle gstate, sword x, sword y,
		      VMFileHandle vmFile, VMBlockHandle vmBlk)
{
    GrDraw d;

    GrEnterGraphics(gstate, &d);
    GrSetDocPenPos(d.GD_gs, x, y);
    DrawHuge(&d, x, y, vmFile, vmBlk, TRUE);
    GrExitGraphics(&d);
}

static void HugeAtCP(GStateHandle gstate, VMFileHandle vmFile,
		     VMBlockHandle vmBlk, Boolean fill)
{
    XYValueAsDWord pen = GrGetCurPos(gstate);
    GrDraw d;

    GrEnterGraphics(gstate, &d);
    DrawHuge(&d, (sword) (word) pen, (sword) (word) (pen >> 16), vmFile,
	     vmBlk, fill);
    GrExitGraphics(&d);
}

void GrDrawHugeBitmapAtCP(GStateHandle gstate, VMFileHandle vmFile,
			  VMBlockHandle vmBlk)
{
    HugeAtCP(gstate, vmFile, vmBlk, FALSE);
}

void GrFillHugeBitmapAtCP(GStateHandle gstate, VMFileHandle vmFile,
			  VMBlockHandle vmBlk)
{
    HugeAtCP(gstate, vmFile, vmBlk, TRUE);
}

void GrDrawBitmap(GStateHandle gstate, sword x, sword y, const Bitmap *bm,
		  Bitmap *(*callback)(Bitmap *bm))
{
    GrDraw d;

    RecordBitmap(gstate, GR_DRAW_BITMAP, FALSE, x, y, bm);
    GrEnterGraphics(gstate, &d);
    GrSetDocPenPos(d.GD_gs, x, y);
    DrawBitmap(&d, x, y, bm, callback, FALSE);
    GrExitGraphics(&d);
}

void GrFillBitmap(GStateHandle gstate, sword x, sword y, const Bitmap *bm,
		  Bitmap *(*callback)(Bitmap *bm))
{
    GrDraw d;

    RecordBitmap(gstate, GR_FILL_BITMAP, FALSE, x, y, bm);
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
    RecordBitmap(gstate, GR_DRAW_BITMAP_CP, TRUE, 0, 0, bm);
    AtCP(gstate, bm, callback, FALSE);
}

void GrFillBitmapAtCP(GStateHandle gstate, const Bitmap *bm,
		      Bitmap *(*callback)(Bitmap *bm))
{
    RecordBitmap(gstate, GR_FILL_BITMAP_CP, TRUE, 0, 0, bm);
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
