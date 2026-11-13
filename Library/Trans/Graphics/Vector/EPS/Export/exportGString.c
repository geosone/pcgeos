/*
 * exportGString.c -- the translation of a graphics string into
 * PostScript (the C port of exportGString.asm and the emitRouts table of
 * exportTables.asm).
 *
 * The string is played into a GState of our own, which so keeps the
 * attributes, the transform and the pen; it stops before each output
 * element (GSC_OUTPUT), whose routine writes the PostScript for it.
 * Paths are skipped here (GR_BEGIN_PATH to GR_END_PATH): what they do
 * reaches the output through the clip path (exportPath.c).
 *
 * Text goes through exportText.c, bitmaps through exportBitmap.c.  Not
 * ported yet: the download of fonts the printer lacks
 * (DownloadPageFonts); the pointer forms of text and bitmaps are skipped
 * (EmitNothing), as in the asm.
 */
#include "exportInt.h"

#include <string.h>

typedef void EmitRout(TGSLocals *tgs, word op);

/* ---------------------------------------------------------------------
 * Coordinates
 * ------------------------------------------------------------------- */

/* CoordToAscii: "x y " (a zero factor of the matrix zeroes the value) */
void CoordToAscii(TGSLocals *tgs, sword x, sword y)
{
    tgs->p = SWordToAscii(tgs->p, tgs->TGS_xfactor ? x : 0);
    *tgs->p++ = ' ';
    tgs->p = SWordToAscii(tgs->p, tgs->TGS_yfactor ? y : 0);
    *tgs->p++ = ' ';
}

void XCoordPlusSpace(TGSLocals *tgs, sword x)
{
    tgs->p = SWordToAscii(tgs->p, tgs->TGS_xfactor ? x : 0);
    *tgs->p++ = ' ';
}

void YCoordPlusSpace(TGSLocals *tgs, sword y)
{
    tgs->p = SWordToAscii(tgs->p, tgs->TGS_yfactor ? y : 0);
    *tgs->p++ = ' ';
}

static void SWordPlusSpace(TGSLocals *tgs, sword v)
{
    tgs->p = SWordToAscii(tgs->p, v);
    *tgs->p++ = ' ';
}

void WWFCoordToAscii(TGSLocals *tgs, WWFixedAsDWord x, WWFixedAsDWord y)
{
    tgs->p = WWFixedToAscii(tgs->p, tgs->TGS_xfactor ? x : 0);
    *tgs->p++ = ' ';
    tgs->p = WWFixedToAscii(tgs->p, tgs->TGS_yfactor ? y : 0);
    *tgs->p++ = ' ';
}

/* EmitPolyCoords: the points last to first (so the pen ends right), five
 * pairs to a line; the pen of gs moves to the last point */
void EmitPolyCoords(TGSLocals *tgs, const byte *pts, word count,
		    GStateHandle gs)
{
    const byte *q = pts + 4 * (count - 1);
    word n = count;

    GrMoveTo(gs, EL_W(q, 0), EL_W(q, 2));
    ResetBuffer(tgs);
    while (n) {
	int k;
	for (k = 0; k < 5 && n; k++, n--, q -= 4)
	    CoordToAscii(tgs, EL_W(q, 0), EL_W(q, 2));
	if (k == 5) {
	    Put(tgs, PS(emitCRLF));
	    EmitBuffer(tgs);
	}
    }
}

/* ---------------------------------------------------------------------
 * Masks
 * ------------------------------------------------------------------- */

static Boolean CheckZeroAreaMask(TGSLocals *tgs)
{
    if (GrGetAreaMask(tgs->TGS_gstate, NULL) == SDM_0) {
	EmitNothing(tgs);
	return TRUE;
    }
    return FALSE;
}

/* the area is solid: a full mask and no pattern */
Boolean CheckFullAreaMask(TGSLocals *tgs)
{
    GraphicPattern gp;

    if (GrGetAreaMask(tgs->TGS_gstate, NULL) != SDM_100)
	return FALSE;
    gp = GrGetAreaPattern(tgs->TGS_gstate, NULL, NULL);
    return gp.HP_type == PT_SOLID;
}

static Boolean CheckZeroLineMask(TGSLocals *tgs)
{
    if (GrGetLineMask(tgs->TGS_gstate, NULL) == SDM_0) {
	EmitNothing(tgs);
	return TRUE;
    }
    return FALSE;
}

/* the line is solid (or is drawn so: TGS_bmType) */
Boolean CheckFullLineMask(TGSLocals *tgs)
{
    if (tgs->TGS_bmType)
	return TRUE;
    return GrGetLineMask(tgs->TGS_gstate, NULL) == SDM_100;
}

/* EmitMaskByte: one byte as a string character, twice (16 bits) */
static void EmitMaskByte(TGSLocals *tgs, byte b)
{
    static const char oct[] = "01234567";
    char *p = tgs->p;

    if (b >= 0x1b && b <= '~') {	/* C_ESCAPE .. C_ASCII_TILDE */
	*p++ = (char) b;
	*p++ = (char) b;
    } else {
	p[0] = '\\';
	p[1] = oct[(b >> 6) & 3];
	p[2] = oct[(b >> 3) & 7];
	p[3] = oct[b & 7];
	memcpy(p + 4, p, 4);
	p += 8;
    }
    tgs->p = p;
}

/* EmitDrawMask: " [(..)(..)(..)(..)(..)(..)(..)(..)]" */
static void EmitDrawMask(TGSLocals *tgs, const byte *m)
{
    int i;

    *tgs->p++ = ' ';
    *tgs->p++ = '[';
    for (i = 0; i < 8; i++) {
	*tgs->p++ = '(';
	EmitMaskByte(tgs, m[i]);
	*tgs->p++ = ')';
    }
    *tgs->p++ = ']';
}

static void EmitLineDrawMask(TGSLocals *tgs)
{
    DrawMask m;

    GrGetLineMask(tgs->TGS_gstate, &m);
    EmitDrawMask(tgs, m);
}

/* the hatches of PT_SYSTEM_HATCH (SH_*), ANDed into the mask */
static const byte hatches[6][8] = {
    { 0x88,0x88,0x88,0x88,0x88,0x88,0x88,0x88 },	/* SH_VERTICAL */
    { 0xff,0x00,0x00,0x00,0xff,0x00,0x00,0x00 },	/* SH_HORIZONTAL */
    { 0x80,0x40,0x20,0x10,0x08,0x04,0x02,0x01 },	/* SH_45_DEGREE */
    { 0x08,0x10,0x20,0x40,0x80,0x01,0x02,0x04 },	/* SH_135_DEGREE */
    { 0x80,0x80,0x80,0xff,0x08,0x08,0x08,0xff },	/* SH_BRICK */
    { 0x80,0x40,0x20,0x50,0x88,0x05,0x02,0x01 }	/* SH_SLANTED_BRICK */
};

static void EmitAreaDrawMask(TGSLocals *tgs)
{
    DrawMask m;
    GraphicPattern gp;
    int i;

    GrGetAreaMask(tgs->TGS_gstate, &m);
    gp = GrGetAreaPattern(tgs->TGS_gstate, NULL, NULL);
    if (gp.HP_type == PT_SYSTEM_HATCH && gp.HP_data < 6)
	for (i = 0; i < 8; i++)
	    m[i] &= hatches[gp.HP_data][i];
    EmitDrawMask(tgs, m);
}

/* ---------------------------------------------------------------------
 * The objects
 * ------------------------------------------------------------------- */

void EmitNothing(TGSLocals *tgs)
{
    (void) ExtractElement(tgs);
}

static void EmitNothingOp(TGSLocals *tgs, word op)
{
    (void) op;
    EmitNothing(tgs);
}

static void EmitStartStrokeStuff(TGSLocals *tgs, word op, Boolean multi)
{
    EmitStartObject(tgs, op);
    EmitTransform(tgs);
    EmitLineAttributes(tgs, multi);
}

static void EmitStartAreaStuff(TGSLocals *tgs, word op)
{
    EmitStartObject(tgs, op);
    EmitTransform(tgs);
    EmitAreaAttributes(tgs);
}

/* the opcode string, the mask form if the line or area is not solid */
static void EmitOp(TGSLocals *tgs, Boolean full, const char *s, word n,
		   const char *ms, word mn)
{
    if (full)
	Put(tgs, s, n);
    else
	Put(tgs, ms, mn);
    EmitBuffer(tgs);
}

/* GR_DRAW_LINE, GR_DRAW_HLINE, GR_DRAW_VLINE: "x2 y2 x1 y1 DL" */
static void EmitDrawLine(TGSLocals *tgs, word op)
{
    byte *e;
    sword x1, y1, x2, y2;
    Boolean full;

    if (CheckZeroLineMask(tgs))
	return;
    EmitStartStrokeStuff(tgs, op, FALSE);
    if (!(e = ExtractElement(tgs)))
	return;
    switch (op) {
    case GR_DRAW_HLINE:
	x1 = EL_W(e, 1); y1 = EL_W(e, 3); x2 = EL_W(e, 5); y2 = y1;
	ResetBuffer(tgs);
	CoordToAscii(tgs, x1, y1);
	CoordToAscii(tgs, x2, y2);
	break;
    case GR_DRAW_VLINE:
	x1 = EL_W(e, 1); y1 = EL_W(e, 3); x2 = x1; y2 = EL_W(e, 5);
	ResetBuffer(tgs);
	CoordToAscii(tgs, x1, y1);
	CoordToAscii(tgs, x2, y2);
	break;
    default:
	x1 = EL_W(e, 1); y1 = EL_W(e, 3); x2 = EL_W(e, 5); y2 = EL_W(e, 7);
	ResetBuffer(tgs);
	CoordToAscii(tgs, x2, y2);
	CoordToAscii(tgs, x1, y1);
	break;
    }
    GrMoveTo(tgs->TGS_gstate, x2, y2);
    full = CheckFullLineMask(tgs);
    if (!full)
	EmitLineDrawMask(tgs);
    EmitOp(tgs, full, PS(emitDL), PS(emitDML));
    EmitEndObject(tgs);
}

/* GR_DRAW_LINE_TO, GR_DRAW_HLINE_TO, GR_DRAW_VLINE_TO: "x y DLT";
 * GR_DRAW_REL_LINE_TO: "dx dy DRLT" */
static void EmitDrawLineTo(TGSLocals *tgs, word op)
{
    GStateHandle gs = tgs->TGS_gstate;
    byte *e;
    XYValueAsDWord cp;
    sword x, y;

    EmitStartStrokeStuff(tgs, op, FALSE);
    if (!(e = ExtractElement(tgs)))
	return;
    ResetBuffer(tgs);
    if (op == GR_DRAW_REL_LINE_TO) {
	WWFCoordToAscii(tgs, EL_D(e, 1), EL_D(e, 5));
	GrDrawRelLineTo(gs, EL_D(e, 1), EL_D(e, 5));
	Put(tgs, PS(emitDRLT));
    } else {
	cp = GrGetCurPos(gs);
	x = (sword) DWORD_X(cp);
	y = (sword) DWORD_Y(cp);
	if (op == GR_DRAW_HLINE_TO)
	    x += EL_W(e, 1);
	else if (op == GR_DRAW_VLINE_TO)
	    y += EL_W(e, 1);
	else {
	    x = EL_W(e, 1);
	    y = EL_W(e, 3);
	}
	GrMoveTo(gs, x, y);
	CoordToAscii(tgs, x, y);
	Put(tgs, PS(emitDLT));
    }
    EmitBuffer(tgs);
    EmitEndObject(tgs);
}

/* GR_DRAW_CURVE, GR_DRAW_CURVE_TO: "x4 y4 x2 y2 x3 y3 x1 y1 DC";
 * GR_DRAW_REL_CURVE_TO: "dx2 dy2 dx3 dy3 dx4 dy4 DRC" */
static void EmitCurve(TGSLocals *tgs, word op)
{
    GStateHandle gs = tgs->TGS_gstate;
    byte *e, *q;
    XYValueAsDWord cp;
    sword x1, y1;
    Boolean full;

    if (CheckZeroLineMask(tgs))
	return;
    EmitStartStrokeStuff(tgs, op, FALSE);
    if (!(e = ExtractElement(tgs)))
	return;
    cp = GrGetCurPos(gs);
    ResetBuffer(tgs);
    if (op == GR_DRAW_REL_CURVE_TO) {
	/* the pen moves by the last point (GrDrawRelCurveTo) */
	GrMoveTo(gs, (sword) (DWORD_X(cp) + EL_W(e, 9)),
		 (sword) (DWORD_Y(cp) + EL_W(e, 11)));
	CoordToAscii(tgs, EL_W(e, 1), EL_W(e, 3));
	CoordToAscii(tgs, EL_W(e, 5), EL_W(e, 7));
	CoordToAscii(tgs, EL_W(e, 9), EL_W(e, 11));
	full = CheckFullLineMask(tgs);
	EmitOp(tgs, full, PS(emitDRC), PS(emitDRC));
	EmitEndObject(tgs);
	return;
    }
    x1 = (sword) DWORD_X(cp);
    y1 = (sword) DWORD_Y(cp);
    q = e + 1;				/* the points of a curve-to */
    if (op == GR_DRAW_CURVE) {
	x1 = EL_W(e, 1);
	y1 = EL_W(e, 3);
	q += 4;
    }
    GrMoveTo(gs, EL_W(q, 8), EL_W(q, 10));
    CoordToAscii(tgs, EL_W(q, 8), EL_W(q, 10));	/* point 4 */
    CoordToAscii(tgs, EL_W(q, 0), EL_W(q, 2));	/* point 2 */
    CoordToAscii(tgs, EL_W(q, 4), EL_W(q, 6));	/* point 3 */
    CoordToAscii(tgs, x1, y1);
    full = CheckFullLineMask(tgs);
    if (!full)
	EmitLineDrawMask(tgs);
    EmitOp(tgs, full, PS(emitDC), PS(emitDMC));
    EmitEndObject(tgs);
}

/* an arc's numbers: "ang1 ang2 x2 x1 y2 y1 " */
static void ArcNumbers(TGSLocals *tgs, const byte *e)
{
    ResetBuffer(tgs);
    SWordPlusSpace(tgs, EL_W(e, 11));
    SWordPlusSpace(tgs, EL_W(e, 13));
    XCoordPlusSpace(tgs, EL_W(e, 7));
    XCoordPlusSpace(tgs, EL_W(e, 3));
    YCoordPlusSpace(tgs, EL_W(e, 9));
    YCoordPlusSpace(tgs, EL_W(e, 5));
}

/* the pen after an arc: as GrDrawArc leaves it */
static void ArcPen(GStateHandle gs, const byte *e, Boolean fill)
{
    if (fill)
	GrFillArc(gs, EL_W(e, 3), EL_W(e, 5), EL_W(e, 7), EL_W(e, 9),
		  (word) EL_W(e, 11), (word) EL_W(e, 13),
		  (ArcCloseType) EL_W(e, 1));
    else
	GrDrawArc(gs, EL_W(e, 3), EL_W(e, 5), EL_W(e, 7), EL_W(e, 9),
		  (word) EL_W(e, 11), (word) EL_W(e, 13),
		  (ArcCloseType) EL_W(e, 1));
}

/* the arc closes (ArcCloseType) open, chord, pie */
static const char *const arcString[6] = {
    emitDAO, emitDAC, emitDAP, emitDMAO, emitDMAC, emitDMAP
};
static const char *const farcString[6] = {
    emitFAC, emitFAC, emitFAP, emitFMAC, emitFMAC, emitFMAP
};

static void PutCode(TGSLocals *tgs, const char *s)
{
    Put(tgs, s, EpsStrLen(s));
}

/* GR_DRAW_ARC and the three point arcs: "... DAO" (DAC, DAP) */
static void EmitDrawArc(TGSLocals *tgs, word op)
{
    byte *e;
    word close;

    if (CheckZeroLineMask(tgs))
	return;
    EmitStartStrokeStuff(tgs, op, FALSE);
    if (!(e = ExtractElement(tgs)))
	return;
    if (tgs->TGS_xfactor || tgs->TGS_yfactor) {
	if (e[0] == GR_DRAW_ARC) {
	    ArcPen(tgs->TGS_gstate, e, FALSE);
	    ArcNumbers(tgs, e);
	} else
	    Convert3PointToNormalArc(tgs, e, tgs->TGS_gstate);
	close = (word) EL_W(e, 1);
	if (close > 2)
	    close = 0;
	if (!CheckFullLineMask(tgs)) {
	    EmitLineDrawMask(tgs);
	    close += 3;
	}
	PutCode(tgs, arcString[close]);
	EmitBuffer(tgs);
    }
    EmitEndObject(tgs);
}

/* GR_FILL_ARC and the three point fills: "... FAC" (FAP) */
static void EmitFillArc(TGSLocals *tgs, word op)
{
    byte *e;
    word close;

    if (CheckZeroAreaMask(tgs))
	return;
    EmitStartAreaStuff(tgs, op);
    if (!(e = ExtractElement(tgs)))
	return;
    if (tgs->TGS_xfactor || tgs->TGS_yfactor) {
	if (e[0] == GR_FILL_ARC) {
	    ArcPen(tgs->TGS_gstate, e, TRUE);
	    ArcNumbers(tgs, e);
	} else
	    Convert3PointToNormalArc(tgs, e, tgs->TGS_gstate);
	close = (word) EL_W(e, 1);
	if (close > 2)
	    close = 0;
	if (!CheckFullAreaMask(tgs)) {
	    EmitAreaDrawMask(tgs);
	    close += 3;
	}
	PutCode(tgs, farcString[close]);
	EmitBuffer(tgs);
    }
    EmitEndObject(tgs);
}

/* GR_DRAW_POLYLINE, GR_DRAW_POLYGON, GR_DRAW_SPLINE(_TO),
 * GR_BRUSH_POLYLINE: "xn yn .. x1 y1 n-1 DPL" (DPG, DS, DST) */
static void EmitPolyline(TGSLocals *tgs, word op)
{
    byte *e;
    word count;
    Boolean full;

    if (CheckZeroLineMask(tgs))
	return;
    EmitStartStrokeStuff(tgs, op, TRUE);
    if (!(e = ExtractElement(tgs)))
	return;
    count = (word) EL_W(e, 1);
    if (!count) {
	EmitEndObject(tgs);
	return;
    }
    EmitPolyCoords(tgs, e + (op == GR_BRUSH_POLYLINE ? 5 : 3), count,
		   tgs->TGS_gstate);
    tgs->p = UWordToAscii(tgs->p, (word) (count - 1));
    full = CheckFullLineMask(tgs);
    if (!full)
	EmitLineDrawMask(tgs);
    switch (op) {
    case GR_DRAW_POLYGON:
	EmitOp(tgs, full, PS(emitDPG), PS(emitDMPG));
	break;
    case GR_DRAW_SPLINE:
	EmitOp(tgs, full, PS(emitDS), PS(emitDMS));
	break;
    case GR_DRAW_SPLINE_TO:
	EmitOp(tgs, full, PS(emitDST), PS(emitDMST));
	break;
    default:
	EmitOp(tgs, full, PS(emitDPL), PS(emitDMPL));
	break;
    }
    EmitEndObject(tgs);
}

/* GR_FILL_POLYGON: "xn yn .. x1 y1 n-1 FPW" (FPO odd-even) */
static void EmitPolygon(TGSLocals *tgs, word op)
{
    byte *e;
    word count;
    byte rule;
    Boolean full;

    if (CheckZeroAreaMask(tgs))
	return;
    EmitStartAreaStuff(tgs, op);
    if (!(e = ExtractElement(tgs)))
	return;
    count = (word) EL_W(e, 1);
    rule = e[3];
    if (!count) {
	EmitEndObject(tgs);
	return;
    }
    EmitPolyCoords(tgs, e + 4, count, tgs->TGS_gstate);
    tgs->p = UWordToAscii(tgs->p, (word) (count - 1));
    full = CheckFullAreaMask(tgs);
    if (!full)
	EmitAreaDrawMask(tgs);
    if (rule == WINDING)
	EmitOp(tgs, full, PS(emitFPW), PS(emitFPMW));
    else
	EmitOp(tgs, full, PS(emitFPO), PS(emitFPMO));
    EmitEndObject(tgs);
}

/* GR_DRAW_PATH: the current path, stroked */
static void EmitDrawPath(TGSLocals *tgs, word op)
{
    if (CheckZeroLineMask(tgs))
	return;
    EmitStartObject(tgs, op);
    EmitLineAttributes(tgs, TRUE);
    if (!ExtractElement(tgs))
	return;
    EmitPath(tgs, GPT_CURRENT);
    ResetBuffer(tgs);
    Put(tgs, PS(emitDP));
    EmitBuffer(tgs);
    EmitEndObject(tgs);
}

/* GR_FILL_PATH: the current path, filled by its rule */
static void EmitFillPath(TGSLocals *tgs, word op)
{
    byte *e;
    byte rule;
    Boolean full;

    if (CheckZeroAreaMask(tgs))
	return;
    EmitStartObject(tgs, op);
    EmitAreaAttributes(tgs);
    if (!(e = ExtractElement(tgs)))
	return;
    rule = e[1];
    EmitPath(tgs, GPT_CURRENT);
    ResetBuffer(tgs);
    full = CheckFullAreaMask(tgs);
    if (!full) {
	EmitAreaDrawMask(tgs);
	if (rule == WINDING)
	    Put(tgs, PS(emitFPMWind));
	else
	    Put(tgs, PS(emitFPMOdd));
    } else if (rule == ODD_EVEN)
	Put(tgs, PS(emitFPOdd));
    else
	Put(tgs, PS(emitFPWind));
    EmitBuffer(tgs);
    EmitEndObject(tgs);
}

/* GR_DRAW_RECT, GR_DRAW_RECT_TO: "x2 y2 x1 y1 DR" */
static void EmitDrawRect(TGSLocals *tgs, word op)
{
    byte *e;
    Boolean full;

    if (CheckZeroLineMask(tgs))
	return;
    EmitStartStrokeStuff(tgs, op, TRUE);
    if (!(e = ExtractElement(tgs)))
	return;
    ResetBuffer(tgs);
    if (op == GR_DRAW_RECT_TO) {
	XYValueAsDWord cp = GrGetCurPos(tgs->TGS_gstate);
	CoordToAscii(tgs, EL_W(e, 1), EL_W(e, 3));
	CoordToAscii(tgs, (sword) DWORD_X(cp), (sword) DWORD_Y(cp));
    } else {
	CoordToAscii(tgs, EL_W(e, 5), EL_W(e, 7));
	CoordToAscii(tgs, EL_W(e, 1), EL_W(e, 3));
    }
    full = CheckFullLineMask(tgs);
    if (!full)
	EmitLineDrawMask(tgs);
    EmitOp(tgs, full, PS(emitDR), PS(emitDMR));
    EmitEndObject(tgs);
}

/* GR_FILL_RECT(_TO), GR_DRAW_POINT(_CP) (a one pixel rectangle):
 * "x2 y2 x1 y1 FR" */
static void EmitFillRect(TGSLocals *tgs, word op)
{
    byte *e;
    sword x1, y1, x2, y2;
    XYValueAsDWord cp;
    Boolean full;

    if (CheckZeroAreaMask(tgs))
	return;
    EmitStartAreaStuff(tgs, op);
    if (!(e = ExtractElement(tgs)))
	return;
    cp = GrGetCurPos(tgs->TGS_gstate);
    switch (op) {
    case GR_FILL_RECT:
	x1 = EL_W(e, 1); y1 = EL_W(e, 3); x2 = EL_W(e, 5); y2 = EL_W(e, 7);
	break;
    case GR_DRAW_POINT_CP:
	x1 = (sword) DWORD_X(cp); y1 = (sword) DWORD_Y(cp);
	x2 = x1 + 1; y2 = y1 + 1;
	break;
    case GR_DRAW_POINT:
	x1 = EL_W(e, 1); y1 = EL_W(e, 3);
	x2 = x1 + 1; y2 = y1 + 1;
	break;
    default:				/* GR_FILL_RECT_TO */
	x1 = (sword) DWORD_X(cp); y1 = (sword) DWORD_Y(cp);
	x2 = EL_W(e, 1); y2 = EL_W(e, 3);
	break;
    }
    ResetBuffer(tgs);
    CoordToAscii(tgs, x2, y2);
    CoordToAscii(tgs, x1, y1);
    full = CheckFullAreaMask(tgs);
    if (!full)
	EmitAreaDrawMask(tgs);
    EmitOp(tgs, full, PS(emitFR), PS(emitFMR));
    EmitEndObject(tgs);
}

/* OrderRoundRectCoords: x1,y1 become the center, x2,y2 the offsets of
 * the corner centers from it, the radius at most half of each side */
void OrderRoundRectCoords(const byte *e, sword *x1, sword *y1,
			  sword *x2, sword *y2, sword *radius)
{
    sword r = EL_W(e, 1);		/* the same place in all of them */
    sword dx = (sword) ((*x2 - *x1) >> 1);
    sword dy = (sword) ((*y2 - *y1) >> 1);

    *x1 += dx;
    *y1 += dy;
    if (dx < 0)
	dx = -dx;
    if (dy < 0)
	dy = -dy;
    if (dx < r)
	r = dx;
    if (dy < r)
	r = dy;
    *x2 = dx - r;
    *y2 = dy - r;
    *radius = r;
}

/* round rectangles: "dx dy r cx cy DRR" (FRR) */
static void RoundRect(TGSLocals *tgs, word op, Boolean fill)
{
    byte *e;
    sword x1, y1, x2, y2, r;
    Boolean full;

    if (fill ? CheckZeroAreaMask(tgs) : CheckZeroLineMask(tgs))
	return;
    if (fill)
	EmitStartAreaStuff(tgs, op);
    else
	EmitStartStrokeStuff(tgs, op, TRUE);
    if (!(e = ExtractElement(tgs)))
	return;
    if (op == GR_DRAW_ROUND_RECT_TO || op == GR_FILL_ROUND_RECT_TO) {
	XYValueAsDWord cp = GrGetCurPos(tgs->TGS_gstate);
	x1 = (sword) DWORD_X(cp);
	y1 = (sword) DWORD_Y(cp);
	x2 = EL_W(e, 3);
	y2 = EL_W(e, 5);
    } else {
	x1 = EL_W(e, 3); y1 = EL_W(e, 5); x2 = EL_W(e, 7); y2 = EL_W(e, 9);
    }
    OrderRoundRectCoords(e, &x1, &y1, &x2, &y2, &r);
    ResetBuffer(tgs);
    CoordToAscii(tgs, x2, y2);
    tgs->p = UWordToAscii(tgs->p, (word) r);
    *tgs->p++ = ' ';
    CoordToAscii(tgs, x1, y1);
    if (fill) {
	full = CheckFullAreaMask(tgs);
	if (!full)
	    EmitAreaDrawMask(tgs);
	EmitOp(tgs, full, PS(emitFRR), PS(emitFMRR));
    } else {
	full = CheckFullLineMask(tgs);
	if (!full)
	    EmitLineDrawMask(tgs);
	EmitOp(tgs, full, PS(emitDRR), PS(emitDMRR));
    }
    EmitEndObject(tgs);
}

static void EmitDrawRoundRect(TGSLocals *tgs, word op)
{
    RoundRect(tgs, op, FALSE);
}

static void EmitFillRoundRect(TGSLocals *tgs, word op)
{
    RoundRect(tgs, op, TRUE);
}

/* ellipses: "x2 x1 y2 y1 DE" (FE) */
static void Ellipse(TGSLocals *tgs, word op, Boolean fill)
{
    byte *e;
    Boolean full;

    if (fill ? CheckZeroAreaMask(tgs) : CheckZeroLineMask(tgs))
	return;
    if (fill)
	EmitStartAreaStuff(tgs, op);
    else
	EmitStartStrokeStuff(tgs, op, TRUE);
    if (!(e = ExtractElement(tgs)))
	return;
    ResetBuffer(tgs);
    CoordToAscii(tgs, EL_W(e, 5), EL_W(e, 1));
    CoordToAscii(tgs, EL_W(e, 7), EL_W(e, 3));
    if (fill) {
	full = CheckFullAreaMask(tgs);
	if (!full)
	    EmitAreaDrawMask(tgs);
	EmitOp(tgs, full, PS(emitFE), PS(emitFME));
    } else {
	full = CheckFullLineMask(tgs);
	if (!full)
	    EmitLineDrawMask(tgs);
	EmitOp(tgs, full, PS(emitDE), PS(emitDME));
    }
    EmitEndObject(tgs);
}

static void EmitDrawEllipse(TGSLocals *tgs, word op)
{
    Ellipse(tgs, op, FALSE);
}

static void EmitFillEllipse(TGSLocals *tgs, word op)
{
    Ellipse(tgs, op, TRUE);
}

/* emitRouts: GR_DRAW_LINE .. GR_DRAW_BITMAP_PTR */
static EmitRout *const emitRouts[] = {
    EmitDrawLine,	/* DrawLine */
    EmitDrawLineTo,	/* DrawLineTo */
    EmitDrawLineTo,	/* DrawRelLineTo */
    EmitDrawLine,	/* DrawHLine */
    EmitDrawLineTo,	/* DrawHLineTo */
    EmitDrawLine,	/* DrawVLine */
    EmitDrawLineTo,	/* DrawVLineTo */
    EmitPolyline,	/* DrawPolyline */
    EmitDrawArc,	/* Arc */
    EmitDrawArc,	/* Arc3Point */
    EmitDrawArc,	/* Arc3PtTo */
    EmitDrawArc,	/* RelArc3PtTo */
    EmitDrawRect,	/* DrawRect */
    EmitDrawRect,	/* DrawRectTo */
    EmitDrawRoundRect,	/* DrawRRect */
    EmitDrawRoundRect,	/* DrawRRectTo */
    EmitPolyline,	/* Spline */
    EmitPolyline,	/* SplineTo */
    EmitCurve,		/* Curve */
    EmitCurve,		/* CurveTo */
    EmitCurve,		/* RelCurveTo */
    EmitDrawEllipse,	/* DrawEllipse */
    EmitPolyline,	/* DrawPolygon */
    EmitFillRect,	/* DrawPoint */
    EmitFillRect,	/* DrawPointCP */
    EmitPolyline,	/* BrushPolyline */
    EmitText,		/* DrawChar */
    EmitText,		/* DrawCharCP */
    EmitText,		/* DrawText */
    EmitText,		/* DrawTextCP */
    EmitTextField,	/* DrawTextField */
    EmitNothingOp,	/* DrawTextPtr */
    EmitNothingOp,	/* DrawTextOPtr */
    EmitDrawPath,	/* DrawPath */
    EmitFillRect,	/* FillRect */
    EmitFillRect,	/* FillRectTo */
    EmitFillRoundRect,	/* FillRRect */
    EmitFillRoundRect,	/* FillRRectTo */
    EmitFillArc,	/* FillArc */
    EmitPolygon,	/* FillPolygon */
    EmitFillEllipse,	/* FillEllipse */
    EmitFillPath,	/* FillPath */
    EmitFillArc,	/* FillArc3Pt */
    EmitFillArc,	/* FillArc3PtTo */
    EmitBitmap,		/* FillBitmap */
    EmitBitmap,		/* FillBitmapCP */
    EmitNothingOp,	/* FillBitmapOPtr */
    EmitNothingOp,	/* FillBitmapPtr */
    EmitBitmap,		/* DrawBitmap */
    EmitBitmap,		/* DrawBitmapCP */
    EmitNothingOp,	/* DrawBitmapOPtr */
    EmitNothingOp	/* DrawBitmapPtr */
};
#define NUM_EMIT_ROUTS	(sizeof(emitRouts) / sizeof(emitRouts[0]))

static void EmitElement(TGSLocals *tgs, word op)
{
    if (op >= GSE_FIRST_OUTPUT_OPCODE &&
	op < GSE_FIRST_OUTPUT_OPCODE + NUM_EMIT_ROUTS)
	emitRouts[op - GSE_FIRST_OUTPUT_OPCODE](tgs, op);
    else
	EmitNothing(tgs);		/* GR_OUTPUT_54 .. 5F */
}

/* ---------------------------------------------------------------------
 * The loop
 * ------------------------------------------------------------------- */

/* TranslateGString (TranslateGStringCommon): the string up to its end,
 * a new page or what flags stop at; what GrDrawGString returned */
GSRetType TranslateGString(TGSLocals *tgs, GStateHandle gstring,
			   MemHandle stream, PSExportBlock *opts,
			   GSControl flags)
{
    GSRetType ret;
    word op;

    memset(tgs, 0, sizeof(*tgs));
    tgs->TGS_gstate = GrCreateState(0);
    tgs->TGS_options = opts;
    tgs->TGS_stream = stream;
    tgs->TGS_gstring = gstring;
    tgs->TGS_src = gstring;
    tgs->TGS_flags = flags | GSC_OUTPUT | GSC_PATH;
    tgs->TGS_xfactor = 1;
    tgs->TGS_yfactor = 1;
    tgs->TGS_elemBlock = MemAlloc(256, HF_DYNAMIC, HAF_STANDARD_NO_ERR);
    tgs->TGS_elemSize = 256;
    tgs->TGS_elem = MemLock(tgs->TGS_elemBlock);
    ResetBuffer(tgs);
    InitPageFonts(tgs);

    GrMoveTo(tgs->TGS_gstate, 0, 0);
    for (;;) {
	ret = GrDrawGStringAtCP(tgs->TGS_gstate, gstring, tgs->TGS_flags,
				&op);
    again:
	if (ret == GSRT_PATH) {
	    if (op != GR_BEGIN_PATH)
		continue;
	    /* skip the path's elements up to its end */
	    for (;;) {
		ret = GrDrawGString(tgs->TGS_gstate, gstring, 0, 0,
				    GSC_PATH | GSC_NEW_PAGE, &op);
		if (ret == GSRT_FAULT)
		    ret = GSRT_COMPLETE;
		if (ret == GSRT_NEW_PAGE || ret == GSRT_COMPLETE)
		    goto done;
		if (op == GR_END_PATH)
		    break;
	    }
	    continue;
	}
	if (ret == GSRT_FAULT)		/* the last element was output */
	    ret = GSRT_COMPLETE;
	if (ret == GSRT_NEW_PAGE || ret == GSRT_COMPLETE)
	    break;

	/* an output element, and those after it */
	for (;;) {
	    EmitElement(tgs, op);
	    if (tgs->TGS_emitRetType == GSRT_COMPLETE) {
		ret = GSRT_COMPLETE;
		goto done;
	    }
	    op = GrGetGStringElement(tgs->TGS_gstate, gstring, 0, NULL, NULL);
	    if (op == GR_NEW_PAGE) {
		GrSetGStringPos(gstring, GSSPT_SKIP_1, 0);
		ret = GSRT_NEW_PAGE;
		goto done;
	    }
	    if (op >= GSE_FIRST_OUTPUT_OPCODE && op <= GSE_LAST_OUTPUT_OPCODE)
		continue;
	    if (op >= GSE_FIRST_PATH_OPCODE && op <= GSE_LAST_PATH_OPCODE) {
		ret = GSRT_PATH;
		goto again;
	    }
	    break;
	}
    }
done:
    FreePageFonts(tgs);
    MemFree(tgs->TGS_elemBlock);
    GrDestroyState(tgs->TGS_gstate);
    if (ret != GSRT_FAULT && tgs->TGS_writeErr)
	ret = GSRT_FAULT;
    return ret;
}
