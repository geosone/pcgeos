/*
 * fontDownloadPCL4.c -- soft fonts built by GEOS (the C port of
 * fontDownloadPCL4.asm): the font header from the font's metrics at the
 * size in 300 dpi dots, each character drawn into a monochrome bitmap
 * and sent as an HP character (a header of its offsets and width, then
 * its raster) in bands of at most MAX_BITMAP_SIZE bytes.
 *
 * Port32 differences, each for a header the printer reads right:
 * - the symbol set of the header is big-endian (the asm stored PC-8, 341,
 *   little-endian), its font ID 0 (the asm's local was never set);
 * - a character's bands are of the height rounded up, the last one
 *   lower (the asm divided the height by the bands rounding down and
 *   so sent fewer lines than the header says);
 * - a character without ink (no width or height) is an empty 8 by 1
 *   (the asm divided by its 0 bands).
 */
#include "fontPCL4.h"
#include "printcomDriver.h"
#include <hugearr.h>
#include <win.h>
#include <string.h>

static void Put16(byte *p, word v)
{
    p[0] = (byte) (v >> 8);
    p[1] = (byte) v;
}

/* GrFontMetrics or GrCharMetrics rounded: the value */
static sword Rounded(dword v)
{
    return (sword) (word) v;
}

/* GetFontInfoPCL4: a metric (not rounded, WBFixed << 8) in quarter dots */
static word QuarterDots(dword v)
{
    sdword v256 = (sdword) v >> 8;

    return (word) ((v256 * 4 + 0x80) >> 8);
}

/* SetTextAttrs: the soft font's face in a GState, at its size in dots */
void SetTextAttrs(GStateHandle gs, const SoftFontEntry *sfe)
{
    const TestFontEntry *t = &sfe->SFE_test;
    byte hs;
    sbyte hw;

    GrSetFont(gs, t->TFE_fontID,
	      (WWFixedAsDWord) PointsToPCL((sdword) t->TFE_pointsize << 8)
	      << 16);
    GrSetTextStyle(gs, ConvertTextStyles(t->TFE_style, &hs, &hw), 0xff);
    GrSetFontWidth(gs, t->TFE_optFontEntry.OFE_fontWidth);
    GrSetFontWeight(gs, t->TFE_optFontEntry.OFE_fontWeight);
    GrSetTextColor(gs, CF_RGB, t->TFE_optFontEntry.OFE_color.RGB_red,
		   t->TFE_optFontEntry.OFE_color.RGB_green,
		   t->TFE_optFontEntry.OFE_color.RGB_blue);
}

/* GetFontBounds: the cell of the characters 33..255 into the header (the
 * widest and highest of them, rounded to bytes across) */
static void GetFontBounds(PState *ps, GStateHandle gs, byte *h)
{
    sword width = 0, top = 0, des = -1, x0, v;
    word c;

    if ((ps->PS_asciiStyle & (PTS_SUBSCRIPT | PTS_STRIKETHRU)) ==
	(PTS_SUBSCRIPT | PTS_STRIKETHRU))
	top = Rounded(GrFontMetrics(gs, GFMI_MEAN_ROUNDED));
    for (c = 255; c > C_SPACE; c--) {
	x0 = Rounded(GrCharMetrics(gs, GCMI_MIN_X_ROUNDED, c));
	v = (sword) ((Rounded(GrCharMetrics(gs, GCMI_MAX_X_ROUNDED, c)) - x0 +
		      7) & ~7);
	if (v > width)
	    width = v;
	v = Rounded(GrCharMetrics(gs, GCMI_MAX_Y_ROUNDED, c));
	if (v > top)
	    top = v;
	v = Rounded(GrCharMetrics(gs, GCMI_MIN_Y_ROUNDED, c));
	if (v < des)
	    des = v;
    }
    Put16(h + 10, (word) (top - des + 1));		/* HPFH_cheight */
    Put16(h + 18, (word) ((top - des + 1) * 4));	/* HPFH_height */
    Put16(h + 8, (word) width);				/* HPFH_cwidth */
    Put16(h + 6, (word) (top + 1));			/* HPFH_baseline */
}

void CreateFontHeader(PState *ps, HPFontInfo *fi, const SoftFontEntry *sfe)
{
    byte *h = fi->HPFI_header;
    GStateHandle gs = GrCreateState(0);
    sbyte weight;
    sword base;

    memset(h, 0, HP_FONT_HEADER_SIZE);
    SetTextAttrs(gs, sfe);
    ConvertTextStyles(sfe->SFE_test.TFE_style, &h[23], &weight);
    h[24] = (byte) weight;				/* HPFH_weight */
    Put16(h, HP_FONT_HEADER_SIZE);			/* HPFH_size */
    h[25] = 0;						/* HPFH_fontID */
    h[3] = 2;				/* HPFH_fontType: 8 bit, 256 chars */
    Put16(h + 14, 341);			/* HPFH_symbolSet: PC-8 */
    h[13] = 1;				/* HPFH_spacing: proportional */
    Put16(h + 16, 120);			/* HPFH_pitch: 10 an inch */
    h[12] = sfe->SFE_orientation;
    Put16(h + 32, (word) PointsToPCL4((sdword)	/* HPFH_textHeight */
				      sfe->SFE_test.TFE_pointsize << 8));
    h[31] = (byte) Rounded(GrFontMetrics(gs,		/* HPFH_underThick */
					 GFMI_UNDER_THICKNESS_ROUNDED));
    base = Rounded(GrFontMetrics(gs, GFMI_BASELINE_ROUNDED));
    h[30] = (byte) (base - Rounded(GrFontMetrics(gs,	/* HPFH_underDist */
						  GFMI_UNDER_POS_ROUNDED)));
    Put16(h + 20, QuarterDots(GrFontMetrics(gs, GFMI_MEAN)));
    Put16(h + 34, QuarterDots(GrFontMetrics(gs, GFMI_AVERAGE_WIDTH)));
    GetFontBounds(ps, gs, h);
    GrDestroyState(gs);
}

/* SendFontHeader: the header under the font's ID */
Boolean SendFontHeader(PState *ps, HPFontInfo *fi, const SoftFontEntry *sfe)
{
    return WriteNumCommand(ps, pr_codes_SetFontID, sfe->SFE_fontTag) ||
	WriteNumCommand(ps, pr_codes_SendFont, HP_FONT_HEADER_SIZE) ||
	PrintStreamWrite(ps, fi->HPFI_header, HP_FONT_HEADER_SIZE);
}

/* a character's place in its bitmap */
typedef struct {
    sword	left, top, width, height, deltaX;	/* in dots, deltaX
							 * quarter dots */
    sword	trackKern;
} CharBounds;

/* GetCharBounds: the character's box (with the track kerning, none if
 * that makes its width negative) */
static void GetCharBounds(PState *ps, const SoftFontEntry *sfe, byte c,
			  CharBounds *cb)
{
    GStateHandle gs = GrCreateState(0);
    dword w;
    sword wi, right, bottom, top, mean;
    word frac;

    SetTextAttrs(gs, sfe);
    cb->trackKern = sfe->SFE_test.TFE_optFontEntry.OFE_trackKern;
    w = GrCharWidth(gs, c);
    wi = (sword) (w >> 16);
    frac = (word) ((w >> 8) & 0xff);
    if (wi + cb->trackKern < 0)
	cb->trackKern = 0;
    wi = (sword) (wi + cb->trackKern);
    cb->deltaX = (sword) ((((sdword) wi << 8 | frac) * 4) >> 8);
    cb->left = (sword) (Rounded(GrCharMetrics(gs, GCMI_MIN_X_ROUNDED, c)) +
			cb->trackKern);
    right = (sword) (Rounded(GrCharMetrics(gs, GCMI_MAX_X_ROUNDED, c)) +
		     cb->trackKern);
    bottom = Rounded(GrCharMetrics(gs, GCMI_MIN_Y_ROUNDED, c));
    if ((ps->PS_asciiStyle & (PTS_SUPERSCRIPT | PTS_STRIKETHRU)) ==
	(PTS_SUPERSCRIPT | PTS_STRIKETHRU))
	bottom = 0;
    top = Rounded(GrCharMetrics(gs, GCMI_MAX_Y_ROUNDED, c));
    if ((ps->PS_asciiStyle & (PTS_SUBSCRIPT | PTS_STRIKETHRU)) ==
	(PTS_SUBSCRIPT | PTS_STRIKETHRU)) {
	mean = Rounded(GrFontMetrics(gs, GFMI_MEAN_ROUNDED));
	if (mean > top)
	    top = mean;
    }
    cb->top = (sword) (top + 1);
    cb->height = (sword) (top + 1 - bottom);
    cb->width = (sword) ((right - cb->left + 7) & ~7);
    GrDestroyState(gs);
}

/* the bands' lines to the stream: the bitmap's rows, bytes each */
static Boolean SendBand(PState *ps, VMFileHandle file, VMBlockHandle bm,
			word rows, word bytes)
{
    void *p;
    word size, n, k;
    dword r;

    for (n = 0; n < rows; n++) {
	p = NULL;
	r = HugeArrayLock(file, bm, n, &p, &size);
	if (!p || !HAL_COUNT(r)) {
	    for (k = 0; k < bytes; k++)
		if (PrintStreamWriteByte(ps, 0))
		    return TRUE;
	    continue;
	}
	k = PrintStreamWrite(ps, p, bytes);
	HugeArrayUnlock(p);
	if (k)
	    return TRUE;
    }
    return FALSE;
}

/* DownloadCharData: character c of the soft font */
Boolean DownloadCharData(PState *ps, HPFontInfo *fi,
			 const SoftFontEntry *sfe, byte c)
{
    CharBounds cb;
    byte h[HP_CHAR_HEADER_SIZE];
    word bands, bandH, bytes, rows, done;
    VMBlockHandle bm;
    GStateHandle gs;
    sword y;
    Boolean err = FALSE;

    if (WriteNumCommand(ps, pr_codes_SetFontID, sfe->SFE_fontTag) ||
	WriteNumCommand(ps, pr_codes_SelectChar, c))
	return TRUE;
    GetCharBounds(ps, sfe, c, &cb);
    if (cb.width <= 0 || cb.height <= 0) {
	cb.width = 8;
	cb.height = 1;
    }
    bytes = (word) (cb.width / 8);
    bands = (word) (((dword) bytes * (word) cb.height + MAX_BITMAP_SIZE - 1) /
		    MAX_BITMAP_SIZE);
    bandH = (word) (((word) cb.height + bands - 1) / bands);
    bm = GrCreateBitmap(BMF_MONO, (word) cb.width, bandH,
			fi->HPFI_bmFileHandle,
			ConstructOptr(GeodeGetProcessHandle(), 0), &gs);
    if (!bm)
	return TRUE;
    GrSetBitmapMode(gs, BM_CLUSTERED_DITHER, 0);
    GrBeginUpdate(gs);
    GrEndUpdate(gs);
    SetTextAttrs(gs, sfe);
    GrSetTextMode(gs, TM_DRAW_BASE,
		  TM_DRAW_BOTTOM | TM_DRAW_ACCENT | TM_DRAW_OPTIONAL_HYPHENS);

    /* HPCharHeader: format 4, start, size 14, class 1, portrait */
    memset(h, 0, sizeof(h));
    h[0] = 4;
    h[2] = 14;
    h[3] = 1;
    Put16(h + 6, (word) cb.left);
    Put16(h + 8, (word) cb.top);
    Put16(h + 10, (word) cb.width);
    Put16(h + 12, (word) cb.height);
    Put16(h + 14, (word) cb.deltaX);

    y = (sword) (cb.top - 1);		/* the baseline in the first band */
    for (done = 0; done < (word) cb.height && !err; done = (word) (done +
								   rows)) {
	rows = (word) cb.height - done;
	if (rows > bandH)
	    rows = bandH;
	GrClearBitmap(gs);
	GrDrawChar(gs, (sword) (cb.trackKern - cb.left), y, c);
	if (!done)
	    err = WriteNumCommand(ps, pr_codes_SendChar,
				  (word) (bytes * rows +
					  HP_CHAR_HEADER_SIZE)) ||
		PrintStreamWrite(ps, h, HP_CHAR_HEADER_SIZE);
	else
	    err = WriteNumCommand(ps, pr_codes_ContinueChar,
				  (word) (bytes * rows + 2));
	if (!err)
	    err = SendBand(ps, fi->HPFI_bmFileHandle, bm, rows, bytes);
	y = (sword) (y - (sword) bandH);
    }
    GrDestroyBitmap(gs, BMD_KILL_DATA);
    return err;
}
