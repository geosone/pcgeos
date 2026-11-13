/*
 * exportPath.c -- paths in PostScript (the C port of exportPath.asm and
 * the pathRouts table of exportTables.asm).
 *
 * A path (the current path, the clip path, the window clip path) comes
 * from GrGetPath as a graphics string; it is played into a GState of its
 * own (TGS_pathGState) that keeps the pen and the transform, and each
 * output element becomes path construction code ("L", "CCP", ...).
 */
#include "exportInt.h"

typedef void PathRout(TGSLocals *tgs, word op);

static void PathEmit(TGSLocals *tgs, const char *s, word len)
{
    Put(tgs, s, len);
    EmitBuffer(tgs);
}

/* GR_DRAW_LINE, GR_DRAW_HLINE, GR_DRAW_VLINE: "x2 y2 x1 y1 L M" */
static void PathLine(TGSLocals *tgs, word op)
{
    GStateHandle gs = tgs->TGS_pathGState;
    byte *e = ExtractElement(tgs);
    sword x1, y1, x2, y2;

    if (!e)
	return;
    x1 = EL_W(e, 1);
    y1 = EL_W(e, 3);
    ResetBuffer(tgs);
    if (op == GR_DRAW_HLINE || op == GR_DRAW_VLINE) {
	x2 = op == GR_DRAW_HLINE ? EL_W(e, 5) : x1;
	y2 = op == GR_DRAW_HLINE ? y1 : EL_W(e, 5);
	CoordToAscii(tgs, x1, y1);
	CoordToAscii(tgs, x2, y2);
    } else {
	x2 = EL_W(e, 5);
	y2 = EL_W(e, 7);
	CoordToAscii(tgs, x2, y2);
	CoordToAscii(tgs, x1, y1);
    }
    GrMoveTo(gs, x2, y2);
    PathEmit(tgs, PS(emitPL));
}

/* the line-tos: "x y L", "dx dy RLT" */
static void PathLineTo(TGSLocals *tgs, word op)
{
    GStateHandle gs = tgs->TGS_pathGState;
    byte *e = ExtractElement(tgs);
    XYValueAsDWord cp;
    sword x, y;

    if (!e)
	return;
    ResetBuffer(tgs);
    if (op == GR_DRAW_REL_LINE_TO) {
	WWFCoordToAscii(tgs, EL_D(e, 1), EL_D(e, 5));
	GrDrawRelLineTo(gs, EL_D(e, 1), EL_D(e, 5));
	PathEmit(tgs, PS(emitPRLT));
	return;
    }
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
    PathEmit(tgs, PS(emitPLT));
}

/* curves: "x4 y4 x2 y2 x3 y3 x1 y1 CCP", "x4 y4 x2 y2 x3 y3 CCTP",
 * and relative "rcurveto" */
static void PathCurve(TGSLocals *tgs, word op)
{
    GStateHandle gs = tgs->TGS_pathGState;
    byte *e = ExtractElement(tgs);
    const byte *q;
    XYValueAsDWord cp;

    if (!e)
	return;
    ResetBuffer(tgs);
    if (op == GR_DRAW_CURVE) {
	q = e + 5;			/* points 2 to 4 */
	GrMoveTo(gs, EL_W(q, 8), EL_W(q, 10));
	CoordToAscii(tgs, EL_W(q, 8), EL_W(q, 10));
	CoordToAscii(tgs, EL_W(q, 0), EL_W(q, 2));
	CoordToAscii(tgs, EL_W(q, 4), EL_W(q, 6));
	CoordToAscii(tgs, EL_W(e, 1), EL_W(e, 3));
	PathEmit(tgs, PS(emitPC));
	return;
    }
    q = e + 1;
    if (op == GR_DRAW_REL_CURVE_TO) {
	cp = GrGetCurPos(gs);
	GrMoveTo(gs, (sword) (DWORD_X(cp) + EL_W(q, 8)),
		 (sword) (DWORD_Y(cp) + EL_W(q, 10)));
    } else
	GrMoveTo(gs, EL_W(q, 8), EL_W(q, 10));
    CoordToAscii(tgs, EL_W(q, 8), EL_W(q, 10));
    CoordToAscii(tgs, EL_W(q, 0), EL_W(q, 2));
    CoordToAscii(tgs, EL_W(q, 4), EL_W(q, 6));
    if (op == GR_DRAW_REL_CURVE_TO)
	PathEmit(tgs, PS(emitPRC));
    else
	PathEmit(tgs, PS(emitPCT));
}

/* arcs: "ang1 ang2 x2 x1 y2 y1 CAPO" (CAPC, CAPP) */
static void PathArc(TGSLocals *tgs, word op)
{
    GStateHandle gs = tgs->TGS_pathGState;
    byte *e = ExtractElement(tgs);
    word close;

    (void) op;
    if (!e)
	return;
    if (e[0] == GR_DRAW_ARC) {
	GrDrawArc(gs, EL_W(e, 3), EL_W(e, 5), EL_W(e, 7), EL_W(e, 9),
		  (word) EL_W(e, 11), (word) EL_W(e, 13),
		  (ArcCloseType) EL_W(e, 1));
	ResetBuffer(tgs);
	tgs->p = SWordToAscii(tgs->p, EL_W(e, 11));
	*tgs->p++ = ' ';
	tgs->p = SWordToAscii(tgs->p, EL_W(e, 13));
	*tgs->p++ = ' ';
	XCoordPlusSpace(tgs, EL_W(e, 7));
	XCoordPlusSpace(tgs, EL_W(e, 3));
	YCoordPlusSpace(tgs, EL_W(e, 9));
	YCoordPlusSpace(tgs, EL_W(e, 5));
    } else
	Convert3PointToNormalArc(tgs, e, gs);
    close = (word) EL_W(e, 1);
    if (close == ACT_CHORD)
	PathEmit(tgs, PS(emitCAPC));
    else if (close == ACT_PIE)
	PathEmit(tgs, PS(emitCAPP));
    else
	PathEmit(tgs, PS(emitCAPO));
}

/* polylines, polygons, splines: "xn yn .. x1 y1 n-1 PPL" (PPG, PS, PST) */
static void PathPolyline(TGSLocals *tgs, word op)
{
    byte *e = ExtractElement(tgs);
    word count;

    if (!e)
	return;
    count = (word) EL_W(e, 1);
    if (!count)
	return;
    EmitPolyCoords(tgs, e + (op == GR_BRUSH_POLYLINE ? 5 : 3), count,
		   tgs->TGS_pathGState);
    tgs->p = UWordToAscii(tgs->p, (word) (count - 1));
    switch (op) {
    case GR_DRAW_POLYGON:
	PathEmit(tgs, PS(emitPPG));
	break;
    case GR_DRAW_SPLINE:
	PathEmit(tgs, PS(emitPS));
	break;
    case GR_DRAW_SPLINE_TO:
	PathEmit(tgs, PS(emitPST));
	break;
    default:
	PathEmit(tgs, PS(emitPPL));
	break;
    }
}

/* "x2 x1 y2 y1 PE" */
static void PathEllipse(TGSLocals *tgs, word op)
{
    byte *e = ExtractElement(tgs);

    (void) op;
    if (!e)
	return;
    ResetBuffer(tgs);
    CoordToAscii(tgs, EL_W(e, 5), EL_W(e, 1));
    CoordToAscii(tgs, EL_W(e, 7), EL_W(e, 3));
    PathEmit(tgs, PS(emitPE));
}

/* CheckFullPageHack: a clip rectangle the size of the document is left
 * out (the asm tested the high word of the width twice; Port32 tests
 * the width and the height) */
static Boolean CheckFullPageHack(TGSLocals *tgs, sword x1, sword y1,
				 sword x2, sword y2)
{
    const GExportOpts *o = &tgs->TGS_options->PSEB_public.PSEO_common;

    if (tgs->TGS_pathType == GPT_CURRENT)
	return FALSE;
    if ((o->GEO_docW >> 16) || (o->GEO_docH >> 16))
	return FALSE;
    if ((word) o->GEO_docW != (word) x2 || (word) o->GEO_docH != (word) y2 ||
	x1 || y1)
	return FALSE;
    tgs->TGS_pathCnt--;
    return TRUE;
}

/* rectangles and points: "x2 y2 x1 y1 PR" */
static void PathRect(TGSLocals *tgs, word op)
{
    byte *e = ExtractElement(tgs);
    XYValueAsDWord cp;
    sword x1, y1, x2, y2;

    if (!e)
	return;
    cp = GrGetCurPos(tgs->TGS_pathGState);
    switch (op) {
    case GR_DRAW_RECT:
	x1 = EL_W(e, 1); y1 = EL_W(e, 3); x2 = EL_W(e, 5); y2 = EL_W(e, 7);
	if (CheckFullPageHack(tgs, x1, y1, x2, y2))
	    return;
	break;
    case GR_DRAW_RECT_TO:
	x1 = (sword) DWORD_X(cp); y1 = (sword) DWORD_Y(cp);
	x2 = EL_W(e, 1); y2 = EL_W(e, 3);
	break;
    case GR_DRAW_POINT:
	/* the asm read a point as a rectangle; Port32: one pixel */
	x1 = EL_W(e, 1); y1 = EL_W(e, 3); x2 = x1 + 1; y2 = y1 + 1;
	break;
    default:				/* GR_DRAW_POINT_CP */
	x1 = (sword) DWORD_X(cp); y1 = (sword) DWORD_Y(cp);
	x2 = x1 + 1; y2 = y1 + 1;
	break;
    }
    ResetBuffer(tgs);
    CoordToAscii(tgs, x2, y2);
    CoordToAscii(tgs, x1, y1);
    PathEmit(tgs, PS(emitPR));
}

/* "dx dy r cx cy PRR" */
static void PathRoundRect(TGSLocals *tgs, word op)
{
    byte *e = ExtractElement(tgs);
    sword x1, y1, x2, y2, r;

    if (!e)
	return;
    if (op == GR_DRAW_ROUND_RECT_TO) {
	XYValueAsDWord cp = GrGetCurPos(tgs->TGS_pathGState);
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
    PathEmit(tgs, PS(emitPRR));
}

/* text in a path (PathTextStub): not ported yet, as EmitText */
static void PathNothing(TGSLocals *tgs, word op)
{
    (void) op;
    EmitNothing(tgs);
}

/* pathRouts: GR_DRAW_LINE .. GR_DRAW_TEXT_PTR */
static PathRout *const pathRouts[] = {
    PathLine, PathLineTo, PathLineTo, PathLine, PathLineTo, PathLine,
    PathLineTo, PathPolyline,
    PathArc, PathArc, PathArc, PathArc,
    PathRect, PathRect, PathRoundRect, PathRoundRect,
    PathPolyline, PathPolyline,
    PathCurve, PathCurve, PathCurve,
    PathEllipse, PathPolyline, PathRect, PathRect, PathPolyline,
    PathNothing, PathNothing, PathNothing, PathNothing, PathNothing,
    PathNothing
};
#define NUM_PATH_ROUTS	(sizeof(pathRouts) / sizeof(pathRouts[0]))

/* EmitClipPath: before a line or area object that is not solid, its
 * mask is drawn through the clip paths, so they are set */
void EmitClipPath(TGSLocals *tgs, word op)
{
    if (op >= GR_FILL_RECT) {
	if (CheckFullAreaMask(tgs))
	    return;
    } else if (op < GSE_FIRST_TEXT_OPCODE || op > GR_DRAW_TEXT_OPTR) {
	if (CheckFullLineMask(tgs))
	    return;
    }					/* text: always (not ported yet) */
    if (EmitPath(tgs, GPT_WIN_CLIP))
	EmitPSCode(tgs, PS(emitClip));
    if (EmitPath(tgs, GPT_CLIP))
	EmitPSCode(tgs, PS(emitClip));
}

/* EmitPath: "SPM newpath" and the path's construction; FALSE if there
 * is no path or nothing was in it */
Boolean EmitPath(TGSLocals *tgs, GetPathType type)
{
    GStateHandle gs = tgs->TGS_gstate;
    GStateHandle pathString, saveSrc = tgs->TGS_src;
    MemHandle path;
    GSRetType ret;
    word op;
    Boolean any;

    tgs->TGS_pathType = (byte) type;
    tgs->TGS_pathCnt = 0;
    GrSaveState(gs);
    path = GrGetPath(gs, type);
    if (!path) {
	GrRestoreState(gs);
	return FALSE;
    }
    pathString = GrLoadGStringPtr(MemLock(path));
    EmitPSCode(tgs, PS(emitOpenPath));

    /* CreatePathGString */
    memset(&tgs->TGS_pathmatrix, 0, sizeof(tgs->TGS_pathmatrix));
    tgs->TGS_pathgs = TRUE;
    tgs->TGS_pathGState = GrCreateState(0);
    tgs->TGS_src = pathString;

    for (;;) {
	ret = GrDrawGStringAtCP(tgs->TGS_pathGState, pathString,
				GSC_OUTPUT | GSC_PATH, &op);
	if (ret == GSRT_FAULT || ret == GSRT_COMPLETE)
	    break;
	for (;;) {
	    if (ret == GSRT_PATH ||
		(op >= GSE_FIRST_PATH_OPCODE && op <= GSE_LAST_PATH_OPCODE))
		GrSetGStringPos(pathString, GSSPT_SKIP_1, 0);
	    else if (op >= GSE_FIRST_OUTPUT_OPCODE &&
		     op <= GSE_LAST_OUTPUT_IN_PATH) {
		EmitTransform(tgs);
		if (!++tgs->TGS_pathCnt)
		    tgs->TGS_pathCnt = 2;	/* never back to zero */
		if (op - GSE_FIRST_OUTPUT_OPCODE < (int) NUM_PATH_ROUTS)
		    pathRouts[op - GSE_FIRST_OUTPUT_OPCODE](tgs, op);
		else
		    EmitNothing(tgs);
	    } else if (op >= GSE_FIRST_OUTPUT_OPCODE &&
		       op <= GSE_LAST_OUTPUT_OPCODE)
		EmitNothing(tgs);	/* fills: not in a path */
	    else
		break;			/* attributes: play them */
	    ret = GSRT_ONE;
	    op = GrGetGStringElement(tgs->TGS_pathGState, pathString, 0,
				     NULL, NULL);
	    if (op == GR_END_GSTRING)
		break;
	}
	if (op == GR_END_GSTRING)
	    break;
    }

    /* DestroyPathGString */
    tgs->TGS_pathgs = FALSE;
    GrDestroyState(tgs->TGS_pathGState);
    tgs->TGS_pathGState = 0;
    tgs->TGS_src = saveSrc;
    GrDestroyGString(pathString, 0, GSKT_LEAVE_DATA);
    MemFree(path);

    any = tgs->TGS_pathCnt != 0;
    GrRestoreState(gs);
    return any;
}
