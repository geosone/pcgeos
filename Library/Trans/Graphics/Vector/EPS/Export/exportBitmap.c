/*
 * exportBitmap.c -- bitmaps in PostScript (the C port of exportBitmap.asm
 * of the PostScript translation library, Port32).
 *
 * GR_DRAW_BITMAP, GR_FILL_BITMAP and their _CP forms become a line of
 * parameters, "width height bytes scaledWidth scaledHeight x y", one of
 * the prolog's procedures and the scan lines:
 *   DMB  monochrome bitmaps through imagemask in the area color, each
 *        line in packets that are binary where no byte of BadByteTable
 *        is in them, hex else, tagged 'B' or 'H' (readmixedstring);
 *   DGB  4-bit grey, DXB 8-bit grey: one or two hex digits a pixel;
 *   DCB  color through colorimage: the red, green and blue of a line
 *        each compacted as PackBits runs (UCB), in hex.
 * Color bitmaps become grey on level 1 printers without CMYK, and where
 * every entry of their palette is a grey.  The slices of a complex
 * bitmap (GSE_BITMAP_SLICE elements after it) are taken in turn.
 *
 * Port32 notes:
 * - The palette is a table of 256 colors: the default palette with the
 *   bitmap's entries over it.  MapBitmapColorIndex took the bitmap's
 *   entry for an index equal to P_entries too (ja), one past its end.
 * - The bits of a monochrome line past its width are cleared: the asm
 *   meant to (rightByte) but had rounded the width up to the byte first,
 *   so its edge mask was always 0xff.  Wide lines with a mask were cut
 *   wrongly (withMask took the mask's size off the packet's count).
 * - The PackBits runs of DCB are made by a plain encoder instead of
 *   CompactPixel's state machine; the runs decode the same.
 * - Where a slice is missing, the lines left are sent blank, so that the
 *   PostScript image does not read the rest of the file as its data.
 * - LZG compacted bitmaps and the CMYK formats (which the asm misread
 *   as 24-bit) are skipped, as the pointer forms are.
 * - EPSExportBitmap (TranslateBitmap, huge bitmaps) is not ported yet.
 */
#include "exportInt.h"

#define EPS_BIN_TAG                 'B'
#define EPS_HEX_TAG                 'H'
#define EPS_WIDE_LINE_LENGTH        100
#define EPS_WIDE_LINE_BUFFS_PER_LINE 4
#define EPS_HEX_LINE                240     /* hex digits on a line */

/* the bitmap element's header in the DOS16 layout */
#define BM_WIDTH        0
#define BM_HEIGHT       2
#define BM_COMPACT      4
#define BM_TYPE         5
#define BM_SIZE         6               /* Bitmap */
#define CB_NUMSCANS     8
#define CB_DATA         12
#define CB_PALETTE      14
#define CB_XRES         16
#define CB_YRES         18
#define CB_SIZE         20              /* CBitmap */
#define SLICE_HDR       3               /* OpBitmapSlice */

typedef enum { BK_MONO, BK_GREY, BK_COLOR } BitmapKind;

typedef struct {
    byte        type;                   /* B_type */
    Boolean     packed;                 /* BMC_PACKBITS */
    word        width;                  /* pixels */
    word        left;                   /* lines to send */
    word        bpl;                    /* bytes a line, the mask too */
    word        mbytes;                 /* bytes of the mask, 0 if none */
    word        scans;                  /* lines left in this slice */
    const byte *data, *end;             /* of this slice */
    word        nchars;                 /* hex digits on this line */
    /* monochrome packets */
    word        perBuff, nBuffs;
    byte       *line;                   /* bpl bytes: the line unpacked */
    byte       *out;                    /* perBuff * nBuffs */
    byte       *runs;                   /* PackBits of one component */
    byte       *comp;                   /* one component of a line */
    byte        pal[256 * 3];
} BMState;

/* ---------------------------------------------------------------------
 * Output
 * ------------------------------------------------------------------- */

static void Byte(TGSLocals *tgs, byte c)
{
    if (tgs->p >= tgs->TGS_buffer + EPS_BUFFER_SIZE - 2)
	EmitBuffer(tgs);
    *tgs->p++ = (char) c;
}

static void Hex(TGSLocals *tgs, byte c)
{
    static const char digits[] = "0123456789ABCDEF";

    Byte(tgs, (byte) digits[c >> 4]);
    Byte(tgs, (byte) digits[c & 15]);
}

static void EndLine(TGSLocals *tgs)
{
    Byte(tgs, '\r');
    Byte(tgs, '\n');
    EmitBuffer(tgs);
}

/* a hex digit or two of a grey or color line, broken every 240 */
static void Digits(TGSLocals *tgs, BMState *b, byte c, word n)
{
    static const char digits[] = "0123456789ABCDEF";

    Byte(tgs, (byte) digits[c >> 4]);
    if (n > 1)
	Byte(tgs, (byte) digits[c & 15]);
    b->nchars += n;
    if (b->nchars >= EPS_HEX_LINE) {
	EndLine(tgs);
	b->nchars = 0;
    }
}

/* ---------------------------------------------------------------------
 * The bitmap's lines
 * ------------------------------------------------------------------- */

/* CalcLineSize: the bytes of a line, the mask's too */
static word CalcLineSize(byte type, word width)
{
    word mono = (word) ((width + 7) >> 3);
    word n;

    switch (type & BMT_FORMAT) {
    case BMF_MONO:  n = mono; break;
    case BMF_4BIT:  n = (word) ((width + 1) >> 1); break;
    case BMF_8BIT:  n = width; break;
    default:        n = (word) (width * 3); break;	/* BMF_24BIT */
    }
    return (word) (n + ((type & BMT_MASK) ? mono : 0));
}

/* UncompactPackBits: n bytes into to; the data past it */
static const byte *UncompactPackBits(const byte *s, const byte *end,
				     byte *to, word n)
{
    while (n && s < end) {
	sword c = (sbyte) *s++;
	word k;

	if (c < 0) {
	    if (c == -128)
		continue;
	    k = (word) (1 - c);
	    if (k > n)
		k = n;
	    memset(to, s < end ? *s : 0, k);
	    s++;
	} else {
	    k = (word) (c + 1);
	    if (k > n)
		k = n;
	    if (k > end - s)
		k = (word) (end - s);
	    memcpy(to, s, k);
	    s += c + 1;
	}
	to += k;
	n -= k;
    }
    memset(to, 0, n);
    return s;
}

/* the start of a bitmap's data: the slice's data and lines */
static void SliceData(BMState *b, const byte *bm, const byte *end)
{
    if (bm[BM_TYPE] & BMT_COMPLEX) {
	word d = (word) EL_W(bm, CB_DATA);
	b->scans = (word) EL_W(bm, CB_NUMSCANS);
	b->data = bm + d;
    } else {
	b->scans = (word) EL_W(bm, BM_HEIGHT);
	b->data = bm + BM_SIZE;
    }
    b->end = end;
    if (b->data > end)
	b->data = end;
}

/* the next slice of the bitmap: FALSE if none follows */
static Boolean NextSlice(TGSLocals *tgs, BMState *b)
{
    word size = 0;
    const byte *e;

    if (GrGetGStringElement(tgs->TGS_gstate, tgs->TGS_src, 0, NULL, &size)
	!= GSE_BITMAP_SLICE)
	return FALSE;
    e = ExtractElement(tgs);
    if (!e || tgs->TGS_elemLen < SLICE_HDR + CB_SIZE)
	return FALSE;
    SliceData(b, e + SLICE_HDR, e + tgs->TGS_elemLen);
    return TRUE;
}

/* the next line, unpacked into b->line; FALSE if none is left */
static Boolean NextLine(TGSLocals *tgs, BMState *b)
{
    if (!b->left)
	return FALSE;
    while (!b->scans) {
	if (!NextSlice(tgs, b)) {
	    /* Port32: blank lines for a missing slice (see above) */
	    b->scans = b->left;
	    b->data = b->end = NULL;
	}
    }
    if (!b->data) {
	/* nothing drawn: clear bits, white or the last index */
	memset(b->line, 0, b->bpl);
	if ((b->type & BMT_FORMAT) != BMF_MONO)
	    memset(b->line + b->mbytes, 0xff, b->bpl - b->mbytes);
    } else if (b->packed) {
	b->data = UncompactPackBits(b->data, b->end, b->line, b->bpl);
    } else {
	word n = b->bpl;
	if (n > b->end - b->data)
	    n = (word) (b->end - b->data);
	memcpy(b->line, b->data, n);
	memset(b->line + n, 0, b->bpl - n);
	b->data += n;
    }
    b->scans--;
    b->left--;
    return TRUE;
}

/* MapBitmapColorIndex */
static void IndexRGB(const BMState *b, word i, byte *r, byte *g, byte *bl)
{
    *r = b->pal[3 * i];
    *g = b->pal[3 * i + 1];
    *bl = b->pal[3 * i + 2];
}

/* the color of pixel i of the line (after the mask); masked off pixels
 * as the asm's MaskOffPixels: the last index, or white */
static void PixelRGB(const BMState *b, word i, byte *r, byte *g, byte *bl)
{
    const byte *d = b->line + b->mbytes;
    Boolean off = (Boolean) (b->mbytes &&
			     !(b->line[i >> 3] & (0x80 >> (i & 7))));

    switch (b->type & BMT_FORMAT) {
    case BMF_4BIT:
	IndexRGB(b, off ? 15 : (i & 1) ? d[i >> 1] & 15 : d[i >> 1] >> 4,
		 r, g, bl);
	break;
    case BMF_8BIT:
	IndexRGB(b, off ? 255 : d[i], r, g, bl);
	break;
    default:
	if (off) {
	    *r = *g = *bl = 0xff;
	} else {
	    *r = d[3 * i];
	    *g = d[3 * i + 1];
	    *bl = d[3 * i + 2];
	}
	break;
    }
}

/* ---------------------------------------------------------------------
 * The parameters
 * ------------------------------------------------------------------- */

/* "bmWidth*72/res" as the asm: divided first, then multiplied */
static WWFixedAsDWord Scaled(word n, word res)
{
    if (res == 72 || !res)
	return (WWFixedAsDWord) n << 16;
    return GrMulWWFixed(GrUDivWWFixed((WWFixedAsDWord) n << 16,
				      (WWFixedAsDWord) res << 16),
			(WWFixedAsDWord) 72 << 16);
}

/* EmitBitmapParams */
static void EmitBitmapParams(TGSLocals *tgs, word width, word height,
			     word bytes, word xres, word yres, sword x,
			     sword y, const char *proc, word plen)
{
    ResetBuffer(tgs);
    tgs->p = UWordToAscii(tgs->p, width);
    *tgs->p++ = ' ';
    tgs->p = UWordToAscii(tgs->p, height);
    *tgs->p++ = ' ';
    tgs->p = UWordToAscii(tgs->p, bytes);
    *tgs->p++ = ' ';
    tgs->p = WWFixedToAscii(tgs->p, Scaled(width, xres));
    *tgs->p++ = ' ';
    tgs->p = WWFixedToAscii(tgs->p, Scaled(height, yres));
    *tgs->p++ = ' ';
    tgs->p = SWordToAscii(tgs->p, tgs->TGS_xfactor ? x : 0);
    *tgs->p++ = ' ';
    tgs->p = SWordToAscii(tgs->p, tgs->TGS_yfactor ? y : 0);
    Put(tgs, proc, plen);
    EmitBuffer(tgs);
}

/* ---------------------------------------------------------------------
 * The three kinds
 * ------------------------------------------------------------------- */

/* the bytes the printer may take as control codes (BadByteTable) */
static Boolean BadByte(byte c)
{
    switch (c) {
    case 0x01: case 0x03: case 0x04: case 0x11:
    case 0x12: case 0x13: case 0x14: case 0x1b:
	return TRUE;
    default:
	return FALSE;
    }
}

/* EmitMonoBitmap, EmitMonoScan, WriteBinByte, WriteHexByte */
static void EmitMonoBitmap(TGSLocals *tgs, BMState *b, word xres,
			   word yres, sword x, sword y)
{
    word bytes = (word) ((b->width + 7) >> 3);
    word total, i, k;
    byte edge = (byte) (0xff << ((8 - (b->width & 7)) & 7));

    b->perBuff = bytes;
    b->nBuffs = 1;
    if (bytes >= EPS_WIDE_LINE_LENGTH) {
	b->perBuff = (word) (((bytes + 7) & ~7) >> 2);
	b->nBuffs = EPS_WIDE_LINE_BUFFS_PER_LINE;
    }
    total = (word) (b->perBuff * b->nBuffs);
    EmitBitmapParams(tgs, (word) (total << 3), b->left, b->perBuff,
		     xres, yres, x, y, PS(emitDMB));
    while (NextLine(tgs, b)) {
	const byte *d = b->line + b->mbytes;

	for (i = 0; i < bytes; i++)
	    b->out[i] = (byte) (d[i] & (b->mbytes ? b->line[i] : 0xff));
	b->out[bytes - 1] &= edge;
	memset(b->out + bytes, 0, total - bytes);
	for (k = 0; k < b->nBuffs; k++) {
	    const byte *o = b->out + k * b->perBuff;
	    Boolean bin = TRUE;

	    for (i = 0; i < b->perBuff && bin; i++)
		if (BadByte(o[i]))
		    bin = FALSE;
	    Byte(tgs, bin ? EPS_BIN_TAG : EPS_HEX_TAG);
	    for (i = 0; i < b->perBuff; i++) {
		if (bin)
		    Byte(tgs, o[i]);
		else
		    Hex(tgs, o[i]);
	    }
	    EndLine(tgs);
	}
    }
}

/* EmitGreyBitmap, EmitGreyScan, WriteGreyPixel */
static void EmitGreyBitmap(TGSLocals *tgs, BMState *b, word xres,
			   word yres, sword x, sword y)
{
    Boolean four = (Boolean) ((b->type & BMT_FORMAT) == BMF_4BIT);
    word w = four ? (word) ((b->width + 1) & ~1) : b->width;
    word i;

    if (four)
	EmitBitmapParams(tgs, w, b->left, (word) (w >> 1), xres, yres,
			 x, y, PS(emitDGB));
    else
	EmitBitmapParams(tgs, w, b->left, w, xres, yres, x, y,
			 PS(emitDXB));
    while (NextLine(tgs, b)) {
	b->nchars = 0;
	for (i = 0; i < w; i++) {
	    byte r, g, bl, grey;

	    if (i < b->width)
		PixelRGB(b, i, &r, &g, &bl);
	    else
		IndexRGB(b, 15, &r, &g, &bl);	/* makeRightWhite */
	    /* 3/8 red, 1/2 green, 1/8 blue, rounded as the asm */
	    grey = (byte) ((r >> 3) + (r >> 2) + ((r >> 1) & 1) +
			   (g >> 1) + (g & 1) + (bl >> 3) + ((bl >> 2) & 1));
	    Digits(tgs, b, grey, (word) (four ? 1 : 2));
	}
	if (b->nchars)
	    EndLine(tgs);
    }
}

/* the PackBits runs of n bytes of s (as UCB reads them) in hex */
static void EmitRuns(TGSLocals *tgs, BMState *b, const byte *s, word n)
{
    word i = 0;

    while (i < n) {
	word j = i + 1;

	while (j < n && s[j] == s[i] && j - i < 128)
	    j++;
	if (j - i >= 2) {			/* a repeat run */
	    Digits(tgs, b, (byte) (257 - (j - i)), 2);
	    Digits(tgs, b, s[i], 2);
	    i = j;
	    continue;
	}
	/* a literal run up to the next pair or 128 bytes */
	j = i + 1;
	while (j < n && j - i < 128 && !(j + 1 < n && s[j] == s[j + 1]))
	    j++;
	Digits(tgs, b, (byte) (j - i - 1), 2);
	for (; i < j; i++)
	    Digits(tgs, b, s[i], 2);
    }
}

/* EmitColorBitmap, EmitColorScan, CompactRGBPixel, WriteCompactedScan */
static void EmitColorBitmap(TGSLocals *tgs, BMState *b, word xres,
			    word yres, sword x, sword y)
{
    word i, c;

    EmitBitmapParams(tgs, b->width, b->left, b->width, xres, yres, x, y,
		     PS(emitDCB));
    while (NextLine(tgs, b)) {
	b->nchars = 0;
	for (i = 0; i < b->width; i++) {
	    byte r, g, bl;

	    PixelRGB(b, i, &r, &g, &bl);
	    MapRGBForPrinter(&r, &g, &bl);
	    b->comp[i] = r;
	    b->runs[i] = g;
	    b->out[i] = bl;
	}
	for (c = 0; c < 3; c++)
	    EmitRuns(tgs, b, c == 0 ? b->comp : c == 1 ? b->runs : b->out,
		     b->width);
	if (b->nchars)
	    EndLine(tgs);
    }
}

/* ---------------------------------------------------------------------
 * EmitBitmap
 * ------------------------------------------------------------------- */

/* SetupBitmapPalette: the default palette, the bitmap's over it */
static void SetupBitmapPalette(BMState *b, const byte *bm, const byte *end)
{
    MemHandle mh = GrGetPalette(0, GPT_DEFAULT);

    if (mh) {
	const byte *p = MemLock(mh);
	memcpy(b->pal, p + 2, sizeof(b->pal));
	MemFree(mh);
    } else {
	memset(b->pal, 0, sizeof(b->pal));
    }
    if ((bm[BM_TYPE] & (BMT_COMPLEX | BMT_PALETTE)) ==
	(BMT_COMPLEX | BMT_PALETTE) && EL_W(bm, CB_PALETTE)) {
	const byte *p = bm + (word) EL_W(bm, CB_PALETTE);
	word n;

	if (p + 2 <= end) {
	    n = (word) EL_W(p, 0);
	    if (n > 256)
		n = 256;
	    if (n > (end - p - 2) / 3)
		n = (word) ((end - p - 2) / 3);
	    memcpy(b->pal, p + 2, (word) (3 * n));
	}
    }
}

/* the kind: grey on level 1 without CMYK, and where the palette has only
 * greys (the asm's testloop: indexes 1 up to 15 or 255) */
static BitmapKind Kind(TGSLocals *tgs, const BMState *b)
{
    word level = tgs->TGS_options->PSEB_public.PSEO_level;
    word f = b->type & BMT_FORMAT, i, last;

    if (f == BMF_MONO)
	return BK_MONO;
    if ((level & 0xff) < 2 && !(level & PSL_CMYK))
	return BK_GREY;
    if (f == BMF_24BIT)
	return BK_COLOR;
    last = f == BMF_8BIT ? 255 : 15;
    for (i = 1; i <= last; i++) {
	byte r, g, bl;
	IndexRGB(b, i, &r, &g, &bl);
	if (r != g || r != bl)
	    return BK_COLOR;
    }
    return BK_GREY;
}

/* EmitBitmap: GR_DRAW_BITMAP, GR_FILL_BITMAP and their _CP forms */
void EmitBitmap(TGSLocals *tgs, word op)
{
    const byte *e, *bm, *end;
    Boolean cp = (Boolean) (op == GR_DRAW_BITMAP_CP ||
			    op == GR_FILL_BITMAP_CP);
    sword x, y;
    word xres = 72, yres = 72, mono, f, buf;
    MemHandle mh;
    BMState *b;

    EmitStartObject(tgs, op);
    e = ExtractElement(tgs);
    if (!e)
	goto done;
    bm = e + (cp ? 3 : 7);
    end = e + tgs->TGS_elemLen;
    if (bm + BM_SIZE > end)
	goto done;
    f = bm[BM_TYPE] & BMT_FORMAT;
    if (f > BMF_24BIT || (bm[BM_COMPACT] != BMC_UNCOMPACTED &&
			  bm[BM_COMPACT] != BMC_PACKBITS) ||
	((bm[BM_TYPE] & BMT_COMPLEX) && bm + CB_SIZE > end)) {
	/* skipped, its slices too */
	while (GrGetGStringElement(tgs->TGS_gstate, tgs->TGS_src, 0, NULL,
				   &buf) == GSE_BITMAP_SLICE)
	    (void) ExtractElement(tgs);
	goto done;
    }
    if (cp) {
	XYValueAsDWord pos = GrGetCurPos(tgs->TGS_gstate);
	x = DWORD_X(pos);
	y = DWORD_Y(pos);
    } else {
	x = EL_W(e, 1);
	y = EL_W(e, 3);
    }
    EmitTransform(tgs);
    EmitAreaAttributes(tgs);

    {
	word w = (word) EL_W(bm, BM_WIDTH);
	word bpl = CalcLineSize(bm[BM_TYPE], w);
	word outn = (word) ((((w + 7) >> 3) + 7) & ~7);

	if (outn < w)
	    outn = w;
	mh = MemAlloc((word) (sizeof(BMState) + bpl + 3 * outn + 8),
		      HF_DYNAMIC, HAF_ZERO_INIT | HAF_LOCK);
	if (!mh)
	    goto done;
	b = MemDeref(mh);
	b->line = (byte *) (b + 1);
	b->out = b->line + bpl;
	b->comp = b->out + outn;
	b->runs = b->comp + outn;
	b->type = bm[BM_TYPE];
	b->packed = (Boolean) (bm[BM_COMPACT] == BMC_PACKBITS);
	b->width = w;
	b->left = (word) EL_W(bm, BM_HEIGHT);
	b->bpl = bpl;
	mono = (word) ((w + 7) >> 3);
	b->mbytes = (b->type & BMT_MASK) ? mono : 0;
    }
    if (bm[BM_TYPE] & BMT_COMPLEX) {
	xres = (word) EL_W(bm, CB_XRES);
	yres = (word) EL_W(bm, CB_YRES);
    }
    SetupBitmapPalette(b, bm, end);
    SliceData(b, bm, end);
    if (b->width && b->left) {
	switch (Kind(tgs, b)) {
	case BK_MONO:
	    EmitMonoBitmap(tgs, b, xres, yres, x, y);
	    break;
	case BK_GREY:
	    EmitGreyBitmap(tgs, b, xres, yres, x, y);
	    break;
	default:
	    EmitColorBitmap(tgs, b, xres, yres, x, y);
	    break;
	}
    }
    /* slices left over (more than the height asked for) */
    while (GrGetGStringElement(tgs->TGS_gstate, tgs->TGS_src, 0, NULL,
			       &buf) == GSE_BITMAP_SLICE)
	(void) ExtractElement(tgs);
    MemFree(mh);
done:
    EmitEndObject(tgs);
}
