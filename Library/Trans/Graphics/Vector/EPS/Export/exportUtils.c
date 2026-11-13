/*
 * exportUtils.c -- PostScript code that many objects need (the C port of
 * exportUtils.asm): the object brackets, the transform, line and area
 * attributes, colors, numbers as ASCII, the element at hand, and the
 * write to the stream.
 */
#include "exportInt.h"

#include <system.h>
#include <string.h>

#define FRAC_MINIMUM    3       /* less than this is .0 */
#define FRAC_MAXIMUM    65530   /* this or more rounds to 1.0 */

/* ---------------------------------------------------------------------
 * Writing
 * ------------------------------------------------------------------- */

/* SendToStream: DR_STREAM_WRITE with STREAM_BLOCK; TRUE (carry) on an
 * error, its StreamError in *error */
Boolean SendToStream(MemHandle stream, const void *data, word size,
		     word *error)
{
    EPSExportLowStreamStruct *s;
    DriverRegs r;
    Boolean err;

    if (!size)
	return FALSE;
    s = MemLock(stream);
    memset(&r, 0, sizeof(r));
    r.DR_ax = STREAM_BLOCK;
    r.DR_bx = s->ESPELSS_token;
    r.DR_cx = size;
    r.DR_dsSi = (void *) data;
    err = s->ESPELSS_strategy(DR_STREAM_WRITE, &r);
    MemUnlock(stream);
    if (err && error)
	*error = (word) r.DR_ax;
    return err;
}

/* EmitPSCode: a piece of PSCode; an error is kept in TGS_writeErr */
Boolean EmitPSCode(TGSLocals *tgs, const char *s, word len)
{
    word e = 0;

    if (SendToStream(tgs->TGS_stream, s, len, &e)) {
	tgs->TGS_writeErr = e ? e : STREAM_SHORT_READ_WRITE;
	return TRUE;
    }
    return FALSE;
}

word EpsStrLen(const char *s)
{
    word n = 0;

    while (s[n])
	n++;
    return n;
}

void ResetBuffer(TGSLocals *tgs)
{
    tgs->p = tgs->TGS_buffer;
}

/* EmitBuffer: TGS_buffer up to tgs->p, which then starts over */
Boolean EmitBuffer(TGSLocals *tgs)
{
    Boolean err = EmitPSCode(tgs, tgs->TGS_buffer,
			     (word) (tgs->p - tgs->TGS_buffer));
    ResetBuffer(tgs);
    return err;
}

void Put(TGSLocals *tgs, const char *s, word len)
{
    if (tgs->p + len > tgs->TGS_buffer + EPS_BUFFER_SIZE) {
	EmitBuffer(tgs);			/* never in the asm: 300 bytes */
	if (len > EPS_BUFFER_SIZE) {
	    EmitPSCode(tgs, s, len);
	    return;
	}
    }
    memcpy(tgs->p, s, len);
    tgs->p += len;
}

void PutChar(TGSLocals *tgs, char c)
{
    Put(tgs, &c, 1);
}

/* ---------------------------------------------------------------------
 * Numbers
 * ------------------------------------------------------------------- */

/* UWordToAscii: no leading zeroes */
char *UWordToAscii(char *p, word v)
{
    char d[5];
    int n = 0;

    do {
	d[n++] = (char) ('0' + v % 10);
	v /= 10;
    } while (v);
    while (n)
	*p++ = d[--n];
    return p;
}

char *SWordToAscii(char *p, sword v)
{
    if (v < 0) {
	*p++ = '-';
	return UWordToAscii(p, (word) -v);
    }
    return UWordToAscii(p, (word) v);
}

char *SDWordToAscii(char *p, sdword v)
{
    dword u = (dword) v;

    if (v < 0) {
	*p++ = '-';
	u = (dword) 0 - u;
    }
    return p + UtilHex32ToAscii(p, u, 0);
}

/* the fraction tables: tenths .. ten-thousandths, ten entries each */
static const word fracTable[4][10] = {
    { 6553, 13107, 19660, 26214, 32768, 39321, 45872, 52428, 58982, 65535 },
    {  655,  1310,  1966,  2621,  3276,  3932,  4587,  5242,  5898,  6554 },
    {   65,   131,   196,   262,   327,   393,   458,   524,   589,   656 },
    {    6,    13,    19,    26,    32,    39,    45,    52,    58,    66 }
};

/* ConvertFraction: at most four digits, as the asm worked them out */
static char *ConvertFraction(char *p, word ax)
{
    int row, i;

    if (ax < FRAC_MINIMUM)
	return p;
    *p = '.';
    for (row = 0; row < 4; row++) {
	p++;
	if (ax < FRAC_MINIMUM)
	    return p;
	*p = '0';
	if (ax >= fracTable[row][0]) {
	    for (i = 0; ; ) {
		(*p)++;
		i++;
		if (i >= 10 || ax < fracTable[row][i]) {
		    ax -= fracTable[row][i - 1];
		    break;
		}
		if (ax == fracTable[row][i]) {	/* doneEarly */
		    (*p)++;
		    if (*p > '9')
			*p = '9';
		    return p + 1;
		}
	    }
	}
    }
    p++;
    if (p[-1] <= '9') {
	if (ax < FRAC_MINIMUM)
	    return p;
	p[-1]++;
	if (p[-1] <= '9')
	    return p;
    }
    /* roundIt */
    p[-1] = '0';
    if (++p[-2] <= '9')
	return p;
    p[-2] = '0';
    if (++p[-3] <= '9')
	return p;
    p[-3] = '0';
    ++p[-4];
    return p;
}

char *WWFixedToAscii(char *p, WWFixedAsDWord v)
{
    word i, f;

    if ((sdword) v < 0) {
	*p++ = '-';
	v = (WWFixedAsDWord) ((dword) 0 - (dword) v);
    }
    i = (word) (v >> 16);
    f = (word) v;
    if (f >= FRAC_MAXIMUM) {
	f = 0;
	i++;
    }
    p = UWordToAscii(p, i);
    return ConvertFraction(p, f);
}

char *DWFixedToAscii(char *p, const DWFixed *v)
{
    /* the 48-bit value as int.frac */
    int64_t x = ((int64_t) v->DWF_int << 16) | v->DWF_frac;
    dword i;
    word f;

    if (x < 0) {
	*p++ = '-';
	x = -x;
    }
    i = (dword) (x >> 16);
    f = (word) x;
    if (f >= FRAC_MAXIMUM) {
	f = 0;
	i++;
    }
    p += UtilHex32ToAscii(p, i, 0);
    return ConvertFraction(p, f);
}

/* ---------------------------------------------------------------------
 * Objects and the transform
 * ------------------------------------------------------------------- */

void EmitStartObject(TGSLocals *tgs, word op)
{
    if (tgs->TGS_pathgs)		/* inside a path: no brackets */
	return;
    if (EmitPSCode(tgs, PS(emitSO)))
	return;
    if (!(tgs->TGS_options->PSEB_status & PSES_EXPORTING_BITMAP))
	EmitClipPath(tgs, op);
}

void EmitEndObject(TGSLocals *tgs)
{
    if (!tgs->TGS_pathgs)
	EmitPSCode(tgs, PS(emitEO));
}

static Boolean WWZero(const WWFixed *w)
{
    return !w->WWF_int && !w->WWF_frac;
}

static Boolean SameMatrix(const TransMatrix *a, const TransMatrix *b)
{
    return a->TM_e11.WWF_int == b->TM_e11.WWF_int &&
	a->TM_e11.WWF_frac == b->TM_e11.WWF_frac &&
	a->TM_e12.WWF_int == b->TM_e12.WWF_int &&
	a->TM_e12.WWF_frac == b->TM_e12.WWF_frac &&
	a->TM_e21.WWF_int == b->TM_e21.WWF_int &&
	a->TM_e21.WWF_frac == b->TM_e21.WWF_frac &&
	a->TM_e22.WWF_int == b->TM_e22.WWF_int &&
	a->TM_e22.WWF_frac == b->TM_e22.WWF_frac &&
	a->TM_e31.DWF_int == b->TM_e31.DWF_int &&
	a->TM_e31.DWF_frac == b->TM_e31.DWF_frac &&
	a->TM_e32.DWF_int == b->TM_e32.DWF_int &&
	a->TM_e32.DWF_frac == b->TM_e32.DWF_frac;
}

static WWFixedAsDWord WW(const WWFixed *w)
{
    return ((dword) (word) w->WWF_int << 16) | w->WWF_frac;
}

/* EmitTransform: "[matrix] SOT", or "[matrix] concat" inside a path,
 * where it is left out while it has not changed (and for identity) */
void EmitTransform(TGSLocals *tgs)
{
    static const TransMatrix ident = {
	{ 0, 1 }, { 0, 0 }, { 0, 0 }, { 0, 1 }, { 0, 0 }, { 0, 0 }
    };
    TransMatrix *m = &tgs->TGS_matrix;

    GrGetTransform(tgs->TGS_pathGState ? tgs->TGS_pathGState
				       : tgs->TGS_gstate, m);
    if (tgs->TGS_pathGState) {
	if (SameMatrix(m, &ident) || SameMatrix(m, &tgs->TGS_pathmatrix))
	    return;
	tgs->TGS_pathmatrix = *m;
	EmitPSCode(tgs, PS(emitRPM));
    }

    /* PostScript inverts it: keep it invertible */
    tgs->TGS_xfactor = 1;
    tgs->TGS_yfactor = 1;
    if (WWZero(&m->TM_e11) && WWZero(&m->TM_e12)) {
	tgs->TGS_xfactor = 0;
	m->TM_e11.WWF_int = 1;
	if (WWZero(&m->TM_e22)) {
	    m->TM_e11.WWF_int = 0;
	    m->TM_e12.WWF_int = 1;
	}
    }
    if (WWZero(&m->TM_e22) && WWZero(&m->TM_e21)) {
	tgs->TGS_yfactor = 0;
	m->TM_e22.WWF_int = 1;
	if (WWZero(&m->TM_e11)) {
	    m->TM_e22.WWF_int = 0;
	    m->TM_e21.WWF_int = 1;
	}
    }

    /* MatrixToAscii */
    ResetBuffer(tgs);
    *tgs->p++ = '[';
    tgs->p = WWFixedToAscii(tgs->p, WW(&m->TM_e11));
    *tgs->p++ = ' ';
    tgs->p = WWFixedToAscii(tgs->p, WW(&m->TM_e12));
    *tgs->p++ = ' ';
    tgs->p = WWFixedToAscii(tgs->p, WW(&m->TM_e21));
    *tgs->p++ = ' ';
    tgs->p = WWFixedToAscii(tgs->p, WW(&m->TM_e22));
    *tgs->p++ = ' ';
    tgs->p = DWFixedToAscii(tgs->p, &m->TM_e31);
    *tgs->p++ = ' ';
    tgs->p = DWFixedToAscii(tgs->p, &m->TM_e32);
    *tgs->p++ = ']';
    if (EmitBuffer(tgs))
	return;
    if (tgs->TGS_pathGState)
	EmitPSCode(tgs, PS(emitSPT));
    else
	EmitPSCode(tgs, PS(emitSOT));
}

/* ---------------------------------------------------------------------
 * Attributes
 * ------------------------------------------------------------------- */

/* line style patterns: the count, then the lengths (per unit of width) */
static const byte styleValues[4][8] = {
    { 1, 4, 0, 0, 0, 0, 0, 0 },		/* LS_DASHED */
    { 2, 1, 2, 0, 0, 0, 0, 0 },		/* LS_DOTTED */
    { 4, 4, 4, 1, 4, 0, 0, 0 },		/* LS_DASHDOT */
    { 6, 4, 4, 1, 4, 1, 4, 0 }		/* LS_DASHDDOT */
};

/* EmitLineAttributes: what differs from the defaults (width 1, mitered
 * joins with limit 10, butt ends, black, solid); multi: a polyline type
 * object, which joins.  A zero width line with a draw mask is drawn
 * dotted and without the mask (TGS_bmType). */
void EmitLineAttributes(TGSLocals *tgs, Boolean multi)
{
    GStateHandle gs = tgs->TGS_gstate;
    WWFixedAsDWord width = GrGetLineWidth(gs);
    LineStyle style;

    tgs->TGS_bmType = 0;
    if (!width && !CheckFullLineMask(tgs))
	tgs->TGS_bmType = 1;
    ResetBuffer(tgs);

    if (width != 0x10000) {
	tgs->p = WWFixedToAscii(tgs->p, width);
	Put(tgs, PS(emitLW));
    }
    if (multi) {
	LineJoin join = GrGetLineJoin(gs);
	if (join == LJ_MITERED) {
	    tgs->p = WWFixedToAscii(tgs->p, GrGetMiterLimit(gs));
	    Put(tgs, PS(emitLM));
	} else {
	    tgs->p = UWordToAscii(tgs->p, (word) join);
	    Put(tgs, PS(emitLJ));
	}
    }
    {
	LineEnd end = GrGetLineEnd(gs);
	if (end != LE_BUTTCAP) {
	    tgs->p = UWordToAscii(tgs->p, (word) end);
	    Put(tgs, PS(emitLC));
	}
    }
    if (tgs->TGS_bmType)
	style = LS_DOTTED;
    else {
	style = GrGetLineStyle(gs);
	if (style == LS_CUSTOM)
	    style = LS_DASHED;
    }
    if (style != LS_SOLID && style <= LS_DASHDDOT) {
	const byte *sv = styleValues[style - 1];
	byte unit = (byte) (width >> 16);	/* at least 1 */
	int i;
	if (!(word) (width >> 16))
	    unit = 1;
	Put(tgs, PS(emitOpenBracket));
	for (i = 1; i <= sv[0]; i++) {
	    tgs->p = UWordToAscii(tgs->p, (word) (sv[i] * unit));
	    *tgs->p++ = ' ';
	}
	Put(tgs, PS(emitLD));
    }
    EmitColor(tgs, GrGetLineColor(gs));
    Put(tgs, PS(emitCRLF));
    EmitBuffer(tgs);
}

void EmitAreaAttributes(TGSLocals *tgs)
{
    ResetBuffer(tgs);
    EmitColor(tgs, GrGetAreaColor(tgs->TGS_gstate));
}

/* MapRGBForPrinter: the 16 default colors as they look better printed */
static const byte defaultB[16] = {
    0x00,0xaa,0x00,0xaa,0x00,0xaa,0x00,0xaa,
    0x55,0xff,0x55,0xff,0x55,0xff,0x55,0xff };
static const byte defaultG[16] = {
    0x00,0x00,0xaa,0xaa,0x00,0x00,0x55,0xaa,
    0x55,0x55,0xff,0xff,0x55,0x55,0xff,0xff };
static const byte defaultR[16] = {
    0x00,0x00,0x00,0x00,0xaa,0xaa,0xaa,0xaa,
    0x55,0x55,0x55,0x55,0xff,0xff,0xff,0xff };
static const byte desiredB[16] = {
    0x00,0xff,0x22,0xbc,0x00,0xff,0x00,0x9a,
    0x44,0xff,0x00,0xff,0x9a,0xff,0x00,0xff };
static const byte desiredG[16] = {
    0x00,0x9a,0xff,0xff,0x00,0x00,0x66,0x9a,
    0x44,0xff,0xff,0xff,0x44,0x66,0xff,0xff };
static const byte desiredR[16] = {
    0x00,0x00,0x55,0x33,0xcd,0x44,0xcd,0x9a,
    0x44,0x00,0x77,0x66,0xff,0x9a,0xff,0xff };

static void MapRGBForPrinter(byte *r, byte *g, byte *b)
{
    int i;

    for (i = 0; i < 16; i++)
	if (*b == defaultB[i] && *g == defaultG[i] && *r == defaultR[i]) {
	    *b = desiredB[i];
	    *g = desiredG[i];
	    *r = desiredR[i];
	    return;
	}
}

/* EmitColor: "R G B SGC" and a line end, written at once; nothing for
 * black */
void EmitColor(TGSLocals *tgs, RGBColorAsDWord c)
{
    byte r = (byte) RGB_RED(c), g = (byte) RGB_GREEN(c),
	b = (byte) RGB_BLUE(c);

    if (!(r | g | b))
	return;
    MapRGBForPrinter(&r, &g, &b);
    tgs->p = UWordToAscii(tgs->p, r);
    *tgs->p++ = ' ';
    tgs->p = UWordToAscii(tgs->p, g);
    *tgs->p++ = ' ';
    tgs->p = UWordToAscii(tgs->p, b);
    Put(tgs, PS(emitSC));
    Put(tgs, PS(emitCRLF));
    EmitBuffer(tgs);
}

/* ---------------------------------------------------------------------
 * The element at hand
 * ------------------------------------------------------------------- */

/* ExtractElement: the next element of TGS_src, past which the position
 * moves (GrCopyGString with GSC_ONE in the asm); NULL if none */
byte *ExtractElement(TGSLocals *tgs)
{
    word size = 0;

    (void) GrGetGStringElement(tgs->TGS_gstate, tgs->TGS_src, 0, NULL,
			       &size);
    if (!size) {
	tgs->TGS_emitRetType = GSRT_COMPLETE;
	return NULL;
    }
    if (size > tgs->TGS_elemSize) {
	MemUnlock(tgs->TGS_elemBlock);
	if (!MemReAlloc(tgs->TGS_elemBlock, size, 0)) {
	    tgs->TGS_elem = MemLock(tgs->TGS_elemBlock);
	    tgs->TGS_emitRetType = GSRT_FAULT;
	    return NULL;
	}
	tgs->TGS_elem = MemLock(tgs->TGS_elemBlock);
	tgs->TGS_elemSize = size;
    }
    (void) GrGetGStringElement(tgs->TGS_gstate, tgs->TGS_src, size,
			       tgs->TGS_elem, &size);
    GrSetGStringPos(tgs->TGS_src, GSSPT_SKIP_1, 0);
    tgs->TGS_emitRetType = GSRT_ONE;
    return tgs->TGS_elem;
}
