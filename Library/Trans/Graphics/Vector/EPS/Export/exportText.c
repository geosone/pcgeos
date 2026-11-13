/*
 * exportText.c -- text in PostScript (the C port of the text part of
 * exportText.asm and of exportFontMap.asm; the tables of
 * exportFontTables.asm, exportMoreFonts.asm and the face table of
 * fontID.def are made at build time by Port32/tools/epsfonts2c.py).
 *
 * Each font and style the text uses becomes a page font: the PostScript
 * font that comes closest among those of the printer (PSEO_fonts).  A
 * GR_DRAW_TEXT (and _CP, GR_DRAW_CHAR, _CP) becomes one style group of
 * one style run, a GR_DRAW_TEXT_FIELD one of its runs, both drawn by the
 * prolog's DSG:
 *
 *	[ [(string) {attrs} trackkern /Font reencode size [style matrix]] ...]
 *	spacepad width x y DSG
 *
 * A font the printer lacks (no exact match) whose outlines GEOS has is
 * downloaded as a Type 3 font named /X<id><style> (DownloadFont), its
 * characters' paths from the font driver (DR_FONT_GEN_PATH with
 * FGPF_POSTSCRIPT) in the asm's compact hex form, drawn by BuildGlyph.
 *
 * Port32: the page fonts are found as the text comes (EmitFont) instead
 * of before the page (GetPageFonts, which set the string back to its
 * beginning after: the beginning of the page only where each page is a
 * string of its own, as the spooler's).  So a downloaded font is defined
 * before the first text in it, with an empty CharProcs and every code
 * encoded, and the characters each text needs and the page has not yet
 * are added to its CharProcs before that text ("/X.. findfont /CharProcs
 * get begin ... end"), instead of all at the page's start.  Its
 * FontMatrix is the driver's transformation of a 1000 point font divided
 * by 1000 (the asm read it at 1 point, rounded to 1/65536), its bounding
 * box 0 0 0 0 (unknown: the characters come later).  Bold and italic are
 * left to the driver (in the FontMatrix), the other styles to the text
 * as for the printer's fonts.  ConquerTheGreeks was
 * switched off in the asm and is left out.  Text in paths (the path
 * GState) is not drawn: paths are skipped (exportGString.c).  The font
 * and style are taken before the metrics lock the font (and put what
 * stands in for it into the GState, LibFindBestFace), and each text
 * element is emitted between GrSaveState and GrRestoreState: the asm
 * read them after, which gave the substitute's name where the font was
 * not installed.
 */
#include "exportInt.h"
#include <font.h>
#include <fontID.h>
#include <char.h>
#include <Internal/fontDr.h>

/* a PageFont (exportConstant.def) */
#define PFF_DEFINED	0x04		/* Port32: downloaded, defined */
#define PFF_DOWNLOAD	0x02
#define PFF_REENCODE	0x01
#define PF_NAME_SIZE	60

typedef struct {
    FontID	PF_id;
    TextStyle	PF_style;	/* asked for */
    TextStyle	PF_newstyle;	/* left to do after the font is chosen */
    byte	PF_flags;
    byte	PF_nlen;
    byte	PF_map[32];	/* the characters used (for a download) */
    char	PF_name[PF_NAME_SIZE];
} PageFont;

/* the style factors of AddMetricsStyles and EmitLocalMotion */
#define SCRIPT_FACTOR		((WWFixedAsDWord) 0x00008000)
#define SUBSCRIPT_OFFSET	((WWFixedAsDWord) 0x00001a00)
#define SUPERSCRIPT_OFFSET	((WWFixedAsDWord) 0xffff9fff)
#define BOLD_FACTOR		((WWFixedAsDWord) 0x00012000)
#define ITALIC_FACTOR		((WWFixedAsDWord) 0xffffc996)
#define STRIKETHRU_FACTOR	((WWFixedAsDWord) 0xffff3333)

#define FID_MAKER_MASK	0xf000

/* a DOS16 TextAttr in a GR_DRAW_TEXT_FIELD run (aligned to a word) */
#define TA_SIZE		24

/* the element layouts (DOS16) */
#define ODT_X		1	/* GR_DRAW_TEXT: x, y, len, text */
#define ODT_Y		3
#define ODT_LEN		5
#define ODT_TEXT	7
#define ODTCP_LEN	1	/* GR_DRAW_TEXT_CP: len, text */
#define ODTCP_TEXT	3
#define ODC_CHAR	1	/* GR_DRAW_CHAR: char, x, y */
#define ODC_X		2
#define ODC_Y		4
#define ODCCP_CHAR	1	/* GR_DRAW_CHAR_CP: char */
#define ODTF_NCHARS	1	/* GR_DRAW_TEXT_FIELD: GDF_saved */
#define ODTF_X		3	/* WBFixed: frac, int */
#define ODTF_Y		6
#define ODTF_BASELINE	9
#define ODTF_FLAGS	14
#define ODTF_SIZE	15	/* then the runs */
#define TFSR_ATTR	2	/* a run: count, TextAttr, text */
#define TFSR_SIZE	(2 + TA_SIZE)

static GStateHandle TextGS(TGSLocals *tgs)
{
    return tgs->TGS_pathGState ? tgs->TGS_pathGState : tgs->TGS_gstate;
}

static WWFixedAsDWord WBF(const byte *e, word off)
{
    return (WWFixedAsDWord) (((dword) (word) EL_W(e, off + 1) << 16) |
			     ((dword) e[off] << 8));
}

/* a WWFixed cut to a WBFixed, as the asm's 8-bit fractions */
static WWFixedAsDWord ToWB(WWFixedAsDWord v)
{
    return v & 0xffffff00UL;
}

static void Space(TGSLocals *tgs)
{
    *tgs->p++ = ' ';
}

static void PutWW(TGSLocals *tgs, WWFixedAsDWord v)
{
    tgs->p = WWFixedToAscii(tgs->p, v);
}

/* ---------------------------------------------------------------------
 * The fonts of a page
 * ------------------------------------------------------------------- */

/* CalcFontDiff: how far apart two faces are (FontMap: 0 the same, 0xff
 * not to be used for each other) */
static byte CalcFontDiff(FontID want, FontID test)
{
    word a = (word) (want & ~FID_MAKER_MASK), b = (word) (test & ~FID_MAKER_MASK);
    word fa, fb, fam;
    int d;

    if (a == b)
	return FM_EXACT;
    fa = (word) ((a >> 8) & 0x0e);
    fb = (word) ((b >> 8) & 0x0e);
    if (fa != fb) {
	if (fa == (FG_MONO >> 8) || fb == (FG_MONO >> 8))
	    return FM_DONT_USE - 1;
	return FM_DONT_USE;
    }
    fam = (word) (fa >> 1);		/* serif, sans, script, ... */
    if (fam >= 3) {
	/* ornament and symbol faces: none; monospaced: any */
	return fam == (FG_MONO >> 9) ? 1 : FM_DONT_USE;
    }
    a &= 0x1ff;
    b &= 0x1ff;
    if (a > (word) (epsFaceStart[fam + 1] - epsFaceStart[fam]) ||
	b > (word) (epsFaceStart[fam + 1] - epsFaceStart[fam]))
	return FM_DONT_USE;
    d = (int) epsFaces[epsFaceStart[fam] + b] -
	(int) epsFaces[epsFaceStart[fam] + a];
    if (d < 0)
	d = -d;
    return (byte) (d + 1);
}

/* MapFontToPostScript: the printer's font closest to id in style, into
 * *pf; how close (FontMap) */
static byte MapFontToPostScript(TGSLocals *tgs, PageFont *pf, FontID id,
				TextStyle style)
{
    const EPSAdobeFont *const *const *t;
    const EPSAdobeFont *const *s;
    const EPSAdobeFont *best = NULL;
    TextStyle want = (TextStyle) (style & (TS_ITALIC | TS_BOLD));
    byte close = FM_DONT_USE, off = 0xff, d, o;
    word list = (word) (tgs->TGS_options->PSEB_public.PSEO_fonts >> 1);

    pf->PF_style = style;
    if ((id & FID_MAKER_MASK) != FM_ADOBE)
	id = (FontID) ((id & ~FID_MAKER_MASK) | FM_ADOBE);
    if (list >= EPS_NUM_FONT_MAPS)
	list = 0;
    for (t = epsFontMaps[list]; *t; t++)
	for (s = *t; *s; s++) {
	    d = CalcFontDiff(id, (*s)->AFE_id);
	    o = (byte) ((*s)->AFE_style ^ want);
	    if (d > close || (d == close && o >= off))
		continue;
	    close = d;
	    off = o;
	    best = *s;
	    if (!close && !off)
		goto found;
	}
found:
    if (!best)
	best = **epsFontMaps[list];
    pf->PF_nlen = best->AFE_nlen;
    pf->PF_newstyle = (TextStyle) (style & ~best->AFE_style);
    if (best->AFE_encode == AFE_STANDARD)
	pf->PF_flags |= PFF_REENCODE;
    memcpy(pf->PF_name, best->AFE_name, best->AFE_nlen);
    return close;
}

static PageFont *PageFonts(TGSLocals *tgs)
{
    return (PageFont *) MemDeref(tgs->TGS_pageFonts);
}

/* GetPageFontEntry: the page font of id in style, added if new; its
 * index (1 on; 0 is the scratch entry) */
static word GetPageFontEntry(TGSLocals *tgs, FontID id, TextStyle style)
{
    PageFont *pf = PageFonts(tgs);
    FontID map = id;
    word i, n = tgs->TGS_pfCount;

    memset(&pf[0], 0, sizeof(PageFont));
    pf[0].PF_id = id;
    pf[0].PF_style = style;
    /* printer and bitmap fonts: as Times */
    if (map >= FM_PRINTER || map < FM_NIMBUSQ)
	map = FID_PS_TIMES_ROMAN;
    if (MapFontToPostScript(tgs, &pf[0], map, style) != FM_EXACT &&
	map == id && GrCheckFontAvailID(FEF_OUTLINES, 0, id) != FID_INVALID) {
	/* not the printer's own, but GEOS has the outlines: download */
	static const char hex[] = "0123456789ABCDEF";
	pf[0].PF_flags = PFF_DOWNLOAD;
	pf[0].PF_newstyle = (TextStyle) (style & ~(TS_BOLD | TS_ITALIC));
	pf[0].PF_nlen = 8;
	pf[0].PF_name[0] = '/';
	pf[0].PF_name[1] = 'X';
	pf[0].PF_name[2] = hex[(id >> 12) & 15];
	pf[0].PF_name[3] = hex[(id >> 8) & 15];
	pf[0].PF_name[4] = hex[(id >> 4) & 15];
	pf[0].PF_name[5] = hex[id & 15];
	pf[0].PF_name[6] = hex[(style >> 4) & 15];
	pf[0].PF_name[7] = hex[style & 15];
    }
    for (i = 1; i <= n; i++)
	if (pf[i].PF_id == id && pf[i].PF_style == style)
	    return i;
    if (!MemReAlloc(tgs->TGS_pageFonts,
		    (word) ((n + 2) * sizeof(PageFont)), 0))
	return 0;
    pf = PageFonts(tgs);
    pf[n + 1] = pf[0];
    tgs->TGS_pfCount = (word) (n + 1);
    return (word) (n + 1);
}

/* InitPageFonts: the page fonts, Helvetica first (GetPageFonts) */
void InitPageFonts(TGSLocals *tgs)
{
    tgs->TGS_pfCount = 0;
    tgs->TGS_pageFonts = MemAlloc(2 * sizeof(PageFont), HF_DYNAMIC,
				  HAF_STANDARD_NO_ERR | HAF_LOCK);
    (void) GetPageFontEntry(tgs, FID_PS_HELVETICA, 0);
}

void FreePageFonts(TGSLocals *tgs)
{
    if (tgs->TGS_pageFonts)
	MemFree(tgs->TGS_pageFonts);
    tgs->TGS_pageFonts = 0;
}

/* ---------------------------------------------------------------------
 * Type 3 fonts (exportType3Fonts.asm, DownloadFont, EmitCharacter)
 * ------------------------------------------------------------------- */

#define HO_MOVETO	0
#define HO_LINETO	2
#define HO_CURVETO	6
#define HO_FILL		8
#define HO_SETCACHE	9
#define HO_DIGITS_RANGE	110
#define HO_DIGITS_ZERO	0x7c
#define HO_110S_LOW	(-10)
#define HO_110S_HIGH	10
#define HO_110S_ZERO	0xf5
#define T3_SIZE		1000		/* the size the paths are made at */
#define T3_LINE		70		/* hex digits on a line */
/* a string literal and its length */
#define PSLIT(s)	s, (word) (sizeof(s) - 1)

/* a byte of a character's procedure, in hex, lines broken */
static void T3Byte(TGSLocals *tgs, byte b)
{
    static const char hex[] = "0123456789ABCDEF";

    if (tgs->p + 4 > tgs->TGS_buffer + EPS_BUFFER_SIZE ||
	tgs->p - tgs->TGS_buffer >= T3_LINE) {
	PutChar(tgs, '\r');
	PutChar(tgs, '\n');
	EmitBuffer(tgs);
    }
    PutChar(tgs, hex[b >> 4]);
    PutChar(tgs, hex[b & 15]);
}

/* HexEncodeSWord: v as a digit -110..110 and steps of 110 */
static void HexEncodeSWord(TGSLocals *tgs, sword v)
{
    sword q = (sword) (v / HO_DIGITS_RANGE), r = (sword) (v % HO_DIGITS_RANGE);

    T3Byte(tgs, (byte) (r + HO_DIGITS_ZERO));
    if (!q)
	return;
    while (q < HO_110S_LOW) {
	T3Byte(tgs, (byte) (HO_110S_LOW + HO_110S_ZERO));
	q = (sword) (q - HO_110S_LOW);
    }
    while (q > HO_110S_HIGH) {
	T3Byte(tgs, (byte) (HO_110S_HIGH + HO_110S_ZERO));
	q = (sword) (q - HO_110S_HIGH);
    }
    T3Byte(tgs, (byte) (q + HO_110S_ZERO));
}

/* the path of character c in font and style as a graphics string in a
 * chunk of blk (ch); FALSE if none */
static GStateHandle CharPath(MemHandle blk, FontID font, TextStyle style,
			     byte c)
{
    word ch = 0;
    GStateHandle g = GrCreateGString(blk, GST_CHUNK, &ch);
    DriverRegs r;

    if (!g)
	return 0;
    GrSetFont(g, font, (WWFixedAsDWord) T3_SIZE << 16);
    style = (TextStyle) (style & (TS_BOLD | TS_ITALIC));
    GrSetTextStyle(g, style, (TextStyle) (~style & 0xff));
    memset(&r, 0, sizeof(r));
    r.DR_bx = g;
    r.DR_cx = FGPF_POSTSCRIPT;
    r.DR_dx = c;
    (void) GrCallFontDriver(g, DR_FONT_GEN_PATH, &r);
    (void) GrEndGString(g);
    GrDestroyGString(g, 0, GSKT_LEAVE_DATA);
    return GrLoadGString(blk, GST_CHUNK, ch);
}

/* the 1/1000 of a WWFixed, in six decimals */
static char *ThousandthToAscii(char *p, sdword v)
{
    int64_t m = ((int64_t) v * 1000000 / 1000 + (v < 0 ? -32768 : 32768))
		/ 65536;
    char d[8];
    int i;

    if (m < 0) {
	*p++ = '-';
	m = -m;
    }
    p = UWordToAscii(p, (word) (m / 1000000));
    m %= 1000000;
    if (m) {
	*p++ = '.';
	for (i = 5; i >= 0; i--, m /= 10)
	    d[i] = (char) ('0' + m % 10);
	for (i = 6; i > 1 && d[i - 1] == '0'; i--)
	    ;
	memcpy(p, d, (size_t) i);
	p += i;
    }
    return p;
}

/* EmitCharacter: "/cNN <procedure> def"; its path from the driver */
static void EmitCharacter(TGSLocals *tgs, MemHandle blk, FontID font,
			  TextStyle style, byte c)
{
    GStateHandle g = CharPath(blk, font, style, c);
    byte e[32];
    word size, op;
    sword ox = 0, oy = 0, cx = (sword) 0x8000, cy = (sword) 0x8000;
    Boolean any = FALSE;

    ResetBuffer(tgs);
    Put(tgs, PSLIT("/c"));
    tgs->p = UWordToAscii(tgs->p, c);
    Put(tgs, PSLIT(" <"));
    while (g) {
	op = GrGetGStringElement(tgs->TGS_gstate, g, sizeof(e), e, &size);
	if (op == GR_END_GSTRING || !size)
	    break;
	GrSetGStringPos(g, GSSPT_SKIP_1, 0);
	if (size > sizeof(e))
	    continue;
	switch (op) {
	case GR_COMMENT:		/* CharSizeInfo: wx wy llx lly urx ury */
	    if (size >= 3 + 12 && !any) {
		word i;
		for (i = 0; i < 6; i++)
		    HexEncodeSWord(tgs, EL_W(e, 3 + 2 * i));
		T3Byte(tgs, HO_SETCACHE);
		any = TRUE;
	    }
	    break;
	case GR_APPLY_TRANSLATION:
	    ox = (sword) (ox + EL_W(e, 3));
	    oy = (sword) (oy + EL_W(e, 7));
	    break;
	case GR_APPLY_TRANSLATION_DWORD:
	    ox = (sword) (ox + EL_W(e, 1));
	    oy = (sword) (oy + EL_W(e, 5));
	    break;
	case GR_MOVE_TO:
	    if ((sword) (EL_W(e, 1) + ox) == cx &&
		(sword) (EL_W(e, 3) + oy) == cy)
		break;
	    /* fall through */
	case GR_DRAW_LINE_TO:
	    cx = (sword) (EL_W(e, 1) + ox);
	    cy = (sword) (EL_W(e, 3) + oy);
	    HexEncodeSWord(tgs, cx);
	    HexEncodeSWord(tgs, cy);
	    T3Byte(tgs, op == GR_MOVE_TO ? HO_MOVETO : HO_LINETO);
	    break;
	case GR_DRAW_CURVE_TO: {
	    word i;
	    for (i = 0; i < 3; i++) {
		HexEncodeSWord(tgs, (sword) (EL_W(e, 1 + 4 * i) + ox));
		HexEncodeSWord(tgs, (sword) (EL_W(e, 3 + 4 * i) + oy));
	    }
	    cx = (sword) (EL_W(e, 9) + ox);
	    cy = (sword) (EL_W(e, 11) + oy);
	    T3Byte(tgs, HO_CURVETO);
	    break;
	}
	default:			/* the transformation: FontMatrix */
	    break;
	}
    }
    if (any)
	T3Byte(tgs, HO_FILL);
    Put(tgs, PSLIT("> def\r\n"));
    EmitBuffer(tgs);
    if (g)
	GrDestroyGString(g, 0, GSKT_KILL_DATA);
}

/* DownloadFont: the font's dictionary, without characters */
static void DownloadFont(TGSLocals *tgs, MemHandle blk, const PageFont *pf)
{
    static const char define[] = " exch definefont pop\r\n";
    TransMatrix tm;
    GStateHandle g;
    word c;
    byte e[32];
    word size;

    EmitPSCode(tgs, PS(beginType3Header));
    EmitPSCode(tgs, PS(emitCPstart));
    EmitPSCode(tgs, PSLIT("257"));
    EmitPSCode(tgs, PS(beginCPdefine));
    EmitPSCode(tgs, PS(beginEVdefine));
    /* every code but 0 named cNN (EmitEncoding) */
    ResetBuffer(tgs);
    PutChar(tgs, '<');
    for (c = 1; c < 256; c++)
	T3Byte(tgs, (byte) c);
    EmitBuffer(tgs);
    EmitPSCode(tgs, PS(emitEAend));

    /* the FontMatrix: the transformation of 'a', at 1000 points */
    memset(&tm, 0, sizeof(tm));
    tm.TM_e11.WWF_int = tm.TM_e22.WWF_int = T3_SIZE;
    g = CharPath(blk, pf->PF_id, pf->PF_style, 'a');
    while (g) {
	word op = GrGetGStringElement(tgs->TGS_gstate, g, sizeof(e), e, &size);
	if (op == GR_END_GSTRING || !size)
	    break;
	GrSetGStringPos(g, GSSPT_SKIP_1, 0);
	if (op == GR_APPLY_TRANSFORM && size >= 29) {
	    tm.TM_e11.WWF_frac = (word) EL_W(e, 1);
	    tm.TM_e11.WWF_int = EL_W(e, 3);
	    tm.TM_e12.WWF_frac = (word) EL_W(e, 5);
	    tm.TM_e12.WWF_int = EL_W(e, 7);
	    tm.TM_e21.WWF_frac = (word) EL_W(e, 9);
	    tm.TM_e21.WWF_int = EL_W(e, 11);
	    tm.TM_e22.WWF_frac = (word) EL_W(e, 13);
	    tm.TM_e22.WWF_int = EL_W(e, 15);
	    break;
	}
    }
    if (g)
	GrDestroyGString(g, 0, GSKT_KILL_DATA);
    ResetBuffer(tgs);
    Put(tgs, PS(emitFontMatrix));
    PutChar(tgs, '[');
    tgs->p = ThousandthToAscii(tgs->p, (sdword) ((dword) (word) tm.TM_e11.WWF_int << 16 | tm.TM_e11.WWF_frac));
    PutChar(tgs, ' ');
    tgs->p = ThousandthToAscii(tgs->p, (sdword) ((dword) (word) tm.TM_e12.WWF_int << 16 | tm.TM_e12.WWF_frac));
    PutChar(tgs, ' ');
    tgs->p = ThousandthToAscii(tgs->p, (sdword) ((dword) (word) tm.TM_e21.WWF_int << 16 | tm.TM_e21.WWF_frac));
    PutChar(tgs, ' ');
    tgs->p = ThousandthToAscii(tgs->p, (sdword) ((dword) (word) tm.TM_e22.WWF_int << 16 | tm.TM_e22.WWF_frac));
    Put(tgs, PSLIT(" 0 0]"));
    Put(tgs, PS(emitFMDef));
    Put(tgs, PSLIT("/FontBBox [0 0 0 0] def\r\n"));
    EmitBuffer(tgs);
    EmitPSCode(tgs, PS(cpend));
    EmitPSCode(tgs, pf->PF_name, pf->PF_nlen);
    EmitPSCode(tgs, define, sizeof(define) - 1);
}

/* DownloadChars: before a text in font and style, the characters of s
 * (and the hyphen) that its page font, if downloaded, lacks */
static void DownloadChars(TGSLocals *tgs, FontID font, TextStyle style,
			  const byte *s, word n)
{
    PageFont *pf;
    word i, k, need = 0;
    byte want[32];
    MemHandle blk;

    if (tgs->TGS_pathgs)
	return;
    i = GetPageFontEntry(tgs, font, style);
    pf = PageFonts(tgs);
    if (!i || !(pf[i].PF_flags & PFF_DOWNLOAD))
	return;
    memset(want, 0, sizeof(want));
    want['-' >> 3] |= (byte) (0x80 >> ('-' & 7));
    for (k = 0; k < n; k++)
	want[s[k] >> 3] |= (byte) (0x80 >> (s[k] & 7));
    want[0] &= 0x7f;				/* not 0 */
    for (k = 0; k < 32; k++) {
	want[k] &= (byte) ~pf[i].PF_map[k];
	if (want[k])
	    need = 1;
    }
    if (!need)
	return;
    blk = MemAllocLMem(LMEM_TYPE_GENERAL, 0);
    if (!blk)
	return;
    if (!(pf[i].PF_flags & PFF_DEFINED)) {
	PageFont copy = pf[i];
	DownloadFont(tgs, blk, &copy);
	pf = PageFonts(tgs);
	pf[i].PF_flags |= PFF_DEFINED;
    }
    {
	PageFont copy = pf[i];
	ResetBuffer(tgs);
	Put(tgs, copy.PF_name, copy.PF_nlen);
	Put(tgs, PSLIT(" findfont /CharProcs get begin\r\n"));
	EmitBuffer(tgs);
	for (k = 0; k < 256; k++)
	    if (want[k >> 3] & (0x80 >> (k & 7)))
		EmitCharacter(tgs, blk, copy.PF_id, copy.PF_style, (byte) k);
	EmitPSCode(tgs, PSLIT("end\r\n"));
    }
    pf = PageFonts(tgs);
    for (k = 0; k < 32; k++)
	pf[i].PF_map[k] |= want[k];
    MemFree(blk);
}

/* EmitFont: "/Name reencode " of the font full and style; the style
 * left to do in TGS_newstyle */
static void EmitFont(TGSLocals *tgs, FontID full, TextStyle style)
{
    static const EPSAdobeFont symbol = {
	FID_PS_SYMBOL, 0, AFE_SYMBOL, 7, "/Symbol"
    };
    FontID id = (FontID) (full & ~FID_MAKER_MASK);
    PageFont *pf = PageFonts(tgs);
    word i, n = tgs->TGS_pfCount;

    for (i = 1; i <= n; i++)
	if ((pf[i].PF_id & ~FID_MAKER_MASK) == id && pf[i].PF_style == style)
	    break;
    /* Port32: a font not seen yet is added now (see the top) */
    if (i > n && id != (FID_PS_SYMBOL & ~FID_MAKER_MASK)) {
	i = GetPageFontEntry(tgs, full, style);
	n = tgs->TGS_pfCount;
	pf = PageFonts(tgs);
	if (!i)
	    i = n + 1;
    }
    if (i > n && id == (FID_PS_SYMBOL & ~FID_MAKER_MASK)) {
	tgs->TGS_newstyle = 0;
	Put(tgs, symbol.AFE_name, symbol.AFE_nlen);
	Put(tgs, " false ", 7);
	return;
    }
    if (i > n)
	i = 1;				/* Helvetica (see the top) */
    tgs->TGS_newstyle = i <= n && pf[i].PF_style == style ?
	pf[i].PF_newstyle : style;
    Put(tgs, pf[i].PF_name, pf[i].PF_nlen);
    if (pf[i].PF_flags & PFF_REENCODE)
	Put(tgs, " true ", 6);
    else
	Put(tgs, " false ", 7);
}

/* ---------------------------------------------------------------------
 * Strings and attributes
 * ------------------------------------------------------------------- */

/* EmitTextString: "[(string) " and a line end, written; characters above
 * 0x7f as octal, ( ) \ escaped, null widths left out, an optional hyphen
 * only at the end (as a hyphen) */
static void EmitTextString(TGSLocals *tgs, const byte *s, word n)
{
    static const char oct[] = "01234567";
    byte c;

    ResetBuffer(tgs);
    *tgs->p++ = '[';
    *tgs->p++ = '(';
    for (; n; n--) {
	c = *s++;
	if (c & 0x80) {
	    *tgs->p++ = '\\';
	    *tgs->p++ = oct[c >> 6];
	    *tgs->p++ = oct[(c >> 3) & 7];
	    *tgs->p++ = oct[c & 7];
	} else if (c == '(' || c == ')' || c == '\\') {
	    *tgs->p++ = '\\';
	    *tgs->p++ = (char) c;
	} else if (c == C_NULL_WIDTH) {
	    ;
	} else if (c == C_OPTHYPHEN) {
	    if (n == 1)
		*tgs->p++ = C_HYPHEN;
	} else
	    *tgs->p++ = (char) c;
	if (tgs->p > tgs->TGS_buffer + EPS_BUFFER_SIZE - 8)
	    EmitBuffer(tgs);
    }
    *tgs->p++ = ')';
    *tgs->p++ = ' ';
    *tgs->p++ = '\r';
    *tgs->p++ = '\n';
    EmitBuffer(tgs);
}

/* EmitTextColor: "R G B SC" */
static void EmitTextColor(TGSLocals *tgs)
{
    RGBColorAsDWord c = GrGetTextColor(TextGS(tgs));
    byte r = (byte) RGB_RED(c), g = (byte) RGB_GREEN(c),
	b = (byte) RGB_BLUE(c);

    MapRGBForPrinter(&r, &g, &b);
    tgs->p = UWordToAscii(tgs->p, r);
    Space(tgs);
    tgs->p = UWordToAscii(tgs->p, g);
    Space(tgs);
    tgs->p = UWordToAscii(tgs->p, b);
    Put(tgs, PS(emitSC));
}

/* a font metric as the asm used it: dx.ah */
static WWFixedAsDWord Metric(GStateHandle gs, GFM_info i)
{
    return ToWB(GrFontMetrics(gs, i));
}

/* the line's thickness less one, at least 0, with its fraction */
static WWFixedAsDWord ThickLess1(GStateHandle gs)
{
    WWFixedAsDWord t = Metric(gs, GFMI_UNDER_THICKNESS);

    t -= 0x10000UL;
    if ((sdword) t < 0)
	t &= 0xffffUL;			/* the int part only went to 0 */
    return t;
}

/* EmitStyleLine: " runlen offset thickness UL" */
static void EmitStyleLine(TGSLocals *tgs, WWFixedAsDWord offset)
{
    Space(tgs);
    PutWW(tgs, tgs->TGS_runlen);
    Space(tgs);
    PutWW(tgs, offset);
    Space(tgs);
    PutWW(tgs, ThickLess1(tgs->TGS_gstate));
    Put(tgs, PS(emitDUL));
}

/* EmitUnderlineStrikethru: the lines of the run's styles */
static void EmitUnderlineStrikethru(TGSLocals *tgs, const byte *s, word n)
{
    GStateHandle gs = tgs->TGS_gstate;
    TextStyle style;
    WWFixedAsDWord v;

    tgs->TGS_runlen = ToWB(GrTextWidthWWFixed(gs, (const char *) s, n));
    style = GrGetTextStyle(gs);
    if (style & TS_UNDERLINE) {
	if (style & (TS_SUPERSCRIPT | TS_SUBSCRIPT)) {
	    GrSaveState(gs);
	    GrSetTextStyle(gs, (TextStyle) (style &
				~(TS_SUPERSCRIPT | TS_SUBSCRIPT)),
			   (TextStyle) (style &
				(TS_SUPERSCRIPT | TS_SUBSCRIPT)));
	}
	v = Metric(gs, GFMI_UNDER_POS) - Metric(gs, GFMI_BASELINE);
	v += GrSDivWWFixed(ThickLess1(gs), 0x20000L);
	v -= tgs->TGS_locmot;
	EmitStyleLine(tgs, v);
	if (style & (TS_SUPERSCRIPT | TS_SUBSCRIPT))
	    GrRestoreState(gs);
    }
    if (style & TS_STRIKE_THRU) {
	v = GrMulWWFixed(Metric(gs, GFMI_MEAN), STRIKETHRU_FACTOR);
	v += GrSDivWWFixed(ThickLess1(gs), 0x20000L);
	v -= tgs->TGS_locmot;
	EmitStyleLine(tgs, v);
    }
}

/* EmitLocalMotion: " motion SS" to undo the last run's sub- or
 * superscript and do this one's */
static void EmitLocalMotion(TGSLocals *tgs)
{
    GStateHandle gs = TextGS(tgs);
    WWFixedAsDWord m = (WWFixedAsDWord) -(sdword) tgs->TGS_locmot;
    TextStyle style = GrGetTextStyle(gs);

    tgs->TGS_locmot = 0;
    if (style & (TS_SUPERSCRIPT | TS_SUBSCRIPT)) {
	tgs->TGS_locmot = GrMulWWFixed(Metric(gs, GFMI_HEIGHT),
				       (style & TS_SUPERSCRIPT) ?
				       SUPERSCRIPT_OFFSET : SUBSCRIPT_OFFSET);
	m += tgs->TGS_locmot;
	m = GrSDivWWFixed(m, SCRIPT_FACTOR);
    }
    if (!m)
	return;
    Space(tgs);
    PutWW(tgs, m);
    Put(tgs, PS(emitSSM));
}

/* EmitTextAttr: "{color lines motion}" and a line end, written */
static void EmitTextAttr(TGSLocals *tgs, const byte *s, word n)
{
    ResetBuffer(tgs);
    *tgs->p++ = '{';
    EmitTextColor(tgs);
    EmitUnderlineStrikethru(tgs, s, n);
    EmitLocalMotion(tgs);
    *tgs->p++ = '}';
    *tgs->p++ = '\r';
    *tgs->p++ = '\n';
    EmitBuffer(tgs);
}

/* AddMetricsStyles: the scales and the slant of the styles PostScript
 * does not do itself (and of the font's weight and width) */
static void AddMetricsStyles(GStateHandle gs, TextStyle style)
{
    byte w = GrGetFontWidth(gs);
    TransMatrix tm;

    if (w != FWI_MEDIUM)
	GrApplyScale(gs, GrUDivWWFixed((dword) w << 16,
				       (dword) FWI_MEDIUM << 16), 0x10000L);
    w = GrGetFontWeight(gs);
    if (w != FW_NORMAL)
	GrApplyScale(gs, GrUDivWWFixed((dword) w << 16,
				       (dword) FW_NORMAL << 16), 0x10000L);
    if (style & TS_BOLD)
	GrApplyScale(gs, BOLD_FACTOR, 0x10000L);
    if (style & (TS_SUBSCRIPT | TS_SUPERSCRIPT))
	GrApplyScale(gs, SCRIPT_FACTOR, SCRIPT_FACTOR);
    if (style & TS_ITALIC) {
	WWFixedAsDWord v;

	GrGetTransform(gs, &tm);
	v = GrMulWWFixed(((dword) (word) tm.TM_e22.WWF_int << 16) |
			 tm.TM_e22.WWF_frac, ITALIC_FACTOR);
	tm.TM_e21.WWF_int = (sword) (v >> 16);
	tm.TM_e21.WWF_frac = (word) v;
	GrSetTransform(gs, &tm);
    }
}

/* EmitStyleMatrix: " [matrix] " of the styles in style */
static void EmitStyleMatrix(TGSLocals *tgs, TextStyle style)
{
    GStateHandle gs = TextGS(tgs);

    GrSaveState(gs);
    GrSetNullTransform(gs);
    AddMetricsStyles(gs, style);
    GrGetTransform(gs, &tgs->TGS_matrix);
    Space(tgs);
    MatrixToAscii(tgs, &tgs->TGS_matrix);
    Space(tgs);
    GrRestoreState(gs);
}

/* ---------------------------------------------------------------------
 * GR_DRAW_TEXT, GR_DRAW_TEXT_CP, GR_DRAW_CHAR, GR_DRAW_CHAR_CP
 * ------------------------------------------------------------------- */

/* EmitDrawText: the string as a style group of one run at x, y */
static void EmitDrawText(TGSLocals *tgs, const byte *s, word n,
			 sword x, sword y)
{
    GStateHandle gs = TextGS(tgs);
    WWFixedAsDWord ps, w, yv;
    sword kern;
    sdword k;
    word size;
    TextMode mode;
    FontID font;
    TextStyle style;

    /* the font asked for: the metrics below lock the font, which puts
     * what stands in for it into the GState (LibFindBestFace) */
    font = GrGetFont(gs, &ps);
    style = GrGetTextStyle(gs);
    DownloadChars(tgs, font, style, s, n);
    GrSaveState(gs);
    EmitStartObject(tgs, GR_DRAW_TEXT);
    EmitTransform(tgs);
    EmitPSCode(tgs, PS(emitOpenBracket));
    EmitTextString(tgs, s, n);
    EmitTextAttr(tgs, s, n);

    /* the track kerning times the point size */
    size = (word) ((ToWB(ps) + 0x8000UL) >> 16);
    kern = (sword) GrGetTrackKern(gs);
    if (kern > MAX_TRACK_KERNING)
	kern = MAX_TRACK_KERNING;
    if (kern < MIN_TRACK_KERNING)
	kern = MIN_TRACK_KERNING;
    k = (sdword) kern * (sword) size;
    if (k > 32767 || k < -32768)
	k = kern < 0 ? (sword) MIN_KERN_VALUE : MAX_KERN_VALUE;
    ResetBuffer(tgs);
    PutWW(tgs, (WWFixedAsDWord) ((dword) (word) k << 16));
    Space(tgs);

    EmitFont(tgs, font, style);
    PutWW(tgs, ToWB(ps));
    EmitStyleMatrix(tgs, tgs->TGS_newstyle);
    Put(tgs, PS(emitCloseBrackets));

    PutWW(tgs, ToWB(GrGetTextSpacePad(gs)));
    Space(tgs);
    w = ToWB(GrTextWidthWWFixed(gs, (const char *) s, n));
    PutWW(tgs, w);
    Space(tgs);
    PutWW(tgs, tgs->TGS_xfactor ? (WWFixedAsDWord) ((dword) (word) x << 16)
	  : 0);
    Space(tgs);

    /* the baseline, from where the text mode puts the text */
    mode = GrGetTextMode(gs);
    yv = (WWFixedAsDWord) ((dword) (word) y << 16);
    if (!(mode & TM_DRAW_BASE)) {
	yv += Metric(gs, GFMI_BASELINE);
	if (mode & TM_DRAW_BOTTOM)
	    yv -= Metric(gs, GFMI_HEIGHT);
	else if (mode & TM_DRAW_ACCENT)
	    yv -= Metric(gs, GFMI_ACCENT);
    }
    PutWW(tgs, tgs->TGS_yfactor ? yv : 0);
    Put(tgs, PS(emitDSG));
    EmitBuffer(tgs);
    EmitEndObject(tgs);
    GrRestoreState(gs);
    GrRelMoveTo(gs, w, 0);
}

void EmitText(TGSLocals *tgs, word op)
{
    const byte *e = ExtractElement(tgs);
    GStateHandle gs = TextGS(tgs);
    sword x, y;

    if (!e)
	return;
    tgs->TGS_locmot = 0;
    switch (op) {
    case GR_DRAW_TEXT:
	EmitDrawText(tgs, e + ODT_TEXT, (word) EL_W(e, ODT_LEN),
		     EL_W(e, ODT_X), EL_W(e, ODT_Y));
	return;
    case GR_DRAW_CHAR:
	/* Port32: y from ODC_y1 (the asm took ODC_x1 for both) */
	EmitDrawText(tgs, e + ODC_CHAR, 1, EL_W(e, ODC_X), EL_W(e, ODC_Y));
	return;
    default:
	break;
    }
    {
	PointWWFixed cp;
	GrGetCurPosWWFixed(gs, &cp);
	x = (sword) (((((dword) (word) cp.PF_x.WWF_int << 16) |
		       cp.PF_x.WWF_frac) + 0x8000UL) >> 16);
	y = (sword) (((((dword) (word) cp.PF_y.WWF_int << 16) |
		       cp.PF_y.WWF_frac) + 0x8000UL) >> 16);
    }
    if (op == GR_DRAW_TEXT_CP)
	EmitDrawText(tgs, e + ODTCP_TEXT, (word) EL_W(e, ODTCP_LEN), x, y);
    else
	EmitDrawText(tgs, e + ODCCP_CHAR, 1, x, y);
}

/* ---------------------------------------------------------------------
 * GR_DRAW_TEXT_FIELD
 * ------------------------------------------------------------------- */

static void DecodeTextAttr(const byte *a, TextAttr *ta)
{
    ta->TA_color.CQ_redOrIndex = a[0];
    ta->TA_color.CQ_info = a[1];
    ta->TA_color.CQ_green = a[2];
    ta->TA_color.CQ_blue = a[3];
    ta->TA_mask = a[4];
    ta->TA_pattern.HP_type = a[5];
    ta->TA_pattern.HP_data = a[6];
    ta->TA_styleSet = a[7];
    ta->TA_styleClear = a[8];
    ta->TA_modeSet = a[9];
    ta->TA_modeClear = a[10];
    ta->TA_spacePad.WBF_frac = a[11];
    ta->TA_spacePad.WBF_int = EL_W(a, 12);
    ta->TA_font = (FontID) EL_W(a, 14);
    ta->TA_size.WBF_frac = a[16];
    ta->TA_size.WBF_int = EL_W(a, 17);
    ta->TA_trackKern = EL_W(a, 19);
    ta->TA_fontWeight = a[21];
    ta->TA_fontWidth = a[22];
}

/* HandleStyleRun: the run at e + off of a field, left characters still
 * to draw before it; how many it had */
static word HandleStyleRun(TGSLocals *tgs, const byte *e, word off,
			   word left)
{
    GStateHandle gs = TextGS(tgs);
    const byte *a = e + off + TFSR_ATTR;
    const byte *s = e + off + TFSR_SIZE;
    word count = (word) EL_W(e, off), n;
    byte hyph[EPS_BUFFER_SIZE];
    TextAttr ta;
    TextStyle style;
    WWFixedAsDWord w, spad;

    if (count > left)
	count = left;
    n = count;
    /* CheckAutoHyphen: the last run of a hyphenated line gets a hyphen */
    if (left == count && (e[ODTF_FLAGS] & HF_AUTO_HYPHEN) &&
	n < sizeof(hyph)) {
	memcpy(hyph, s, n);
	hyph[n++] = '-';
	s = hyph;
    }
    if (n == 1 && s[0] == C_TAB)
	return count;

    DecodeTextAttr(a, &ta);
    spad = GrGetTextSpacePad(gs);
    ta.TA_spacePad.WBF_int = (sword) (spad >> 16);
    ta.TA_spacePad.WBF_frac = (byte) (spad >> 8);
    /* the run's attributes for this run only: the font asked for before
     * the metrics put what stands in for it into the GState */
    GrSaveState(gs);
    GrSetTextAttr(gs, &ta);
    style = GrGetTextStyle(gs);
    w = ToWB(GrTextWidthWWFixed(gs, (const char *) s, n));
    tgs->TGS_gwidth += w;
    tgs->TGS_width += w;
    if (s[0] == C_TAB) {
	s++;
	n--;
    }
    EmitTextString(tgs, s, n);
    EmitTextAttr(tgs, s, n);

    /* the track kerning (BBFixed) times the size */
    ResetBuffer(tgs);
    PutWW(tgs, GrMulWWFixed((WWFixedAsDWord) ((sdword) (sbyte) a[20] << 16 |
					      (dword) a[19] << 8),
			    WBF(a, 16)));
    Space(tgs);
    EmitFont(tgs, ta.TA_font, style);
    PutWW(tgs, WBF(a, 16));
    EmitStyleMatrix(tgs, tgs->TGS_newstyle);
    Put(tgs, PS(emitCloseBracket));
    Put(tgs, PS(emitCRLF));
    EmitBuffer(tgs);
    GrRestoreState(gs);
    return count;
}

/* HandleStyleGroup: the runs of a field as one group */
static void HandleStyleGroup(TGSLocals *tgs, const byte *e, word size)
{
    GStateHandle gs = TextGS(tgs);
    word left = (word) EL_W(e, ODTF_NCHARS), off = ODTF_SIZE, n;
    WWFixedAsDWord x, y;

    EmitPSCode(tgs, PS(emitOpenBracket));
    tgs->TGS_gwidth = 0;
    while (left && off + TFSR_SIZE <= size) {
	n = HandleStyleRun(tgs, e, off, left);
	left = (word) (left - n);
	off = (word) (off + TFSR_SIZE + n);
    }
    /* y: the position and the baseline, rounded */
    y = (WBF(e, ODTF_Y) + WBF(e, ODTF_BASELINE) + 0x8000UL) & 0xffff0000UL;
    x = tgs->TGS_gpos;
    tgs->TGS_gpos += tgs->TGS_gwidth;

    ResetBuffer(tgs);
    Put(tgs, PS(emitCloseBracket));
    PutWW(tgs, ToWB(GrGetTextSpacePad(gs)));
    Space(tgs);
    PutWW(tgs, tgs->TGS_gwidth);
    Space(tgs);
    PutWW(tgs, tgs->TGS_xfactor ? x : 0);
    Space(tgs);
    PutWW(tgs, tgs->TGS_yfactor ? y : 0);
    Space(tgs);
    if (tgs->TGS_pathGState)
	Put(tgs, PS(emitPSG));
    else
	Put(tgs, PS(emitDSG));
    EmitBuffer(tgs);
    EmitEndObject(tgs);
}

/* the characters of a field's runs to download, before it */
static void DownloadFieldChars(TGSLocals *tgs, const byte *e, word size)
{
    GStateHandle gs = TextGS(tgs);
    word left = (word) EL_W(e, ODTF_NCHARS), off = ODTF_SIZE, n;
    TextAttr ta;

    while (left && off + TFSR_SIZE <= size) {
	n = (word) EL_W(e, off);
	if (n > left)
	    n = left;
	if (off + TFSR_SIZE + n > size)
	    break;
	DecodeTextAttr(e + off + TFSR_ATTR, &ta);
	GrSaveState(gs);
	GrSetTextAttr(gs, &ta);
	DownloadChars(tgs, ta.TA_font, GrGetTextStyle(gs),
		      e + off + TFSR_SIZE, n);
	GrRestoreState(gs);
	left = (word) (left - n);
	off = (word) (off + TFSR_SIZE + n);
    }
}

void EmitTextField(TGSLocals *tgs, word op)
{
    const byte *e;

    /* Port32: the element first, for the characters to download before
     * the object (the asm took it after EmitStartObject) */
    e = ExtractElement(tgs);
    if (!e)
	return;
    DownloadFieldChars(tgs, e, tgs->TGS_elemLen);
    EmitStartObject(tgs, op);
    EmitTransform(tgs);
    tgs->TGS_locmot = 0;
    tgs->TGS_width = 0;
    tgs->TGS_gpos = WBF(e, ODTF_X);
    HandleStyleGroup(tgs, e, tgs->TGS_elemLen);
}
