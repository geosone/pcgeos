/*
 * graphicsArc.c -- arcs (after graphicsArc.asm: GrDrawArc, GrFillArc,
 * GrDrawArc3Point, GrFillArc3Point, GrDrawArc3PointTo,
 * GrFillArc3PointTo, GrDrawRelArc3PointTo, ArcSetCurPos,
 * Arc3PointSetCurPos; graphicsArcLow.asm: SetupArc3PointToLow,
 * SetupRelArc3PointToLow).
 *
 * The pen: a bounded arc leaves it at its end if open, else at its
 * start; a 3-point arc at point 1 if open, else at point 3; the "to"
 * arcs start at the pen and leave it at point 3.  Graphics strings and
 * paths store GrDrawArc and GrFillArc (graphicsString*.c,
 * graphicsPath.c); not yet the 3-point arcs.
 */
#include "graphicsInt.h"

#define AREA	((word) offsetof(GState, GS_areaAttr))

static sdword WWF(const WWFixed *w)
{
    return (sdword) (((dword) (word) w->WWF_int << 16) | w->WWF_frac);
}

static void SetWWF(WWFixed *w, sdword v)
{
    w->WWF_int = (sword) (v >> 16);
    w->WWF_frac = (word) v;
}

static void Bounded(GStateHandle gstate, sword l, sword t, sword r, sword b,
		    word a1, word a2, ArcCloseType close, Boolean fill)
{
    GrDraw d;
    GrArcParams ap;
    sword x, y;

    GrEnterGraphics(gstate, &d);
    GrArcAnglePos(l, t, r, b, (sword) (close == ACT_OPEN ? a2 : a1), &x,
		  &y);					/* ArcSetCurPos */
    GrSetDocPenPos(d.GD_gs, x, y);
    if (!GrTrivialReject(&d)) {
	memset(&ap, 0, sizeof(ap));
	ap.GA_close = (word) close;
	GrSetupArcLow(&d, l, t, r, b, (sword) a1, (sword) a2, &ap);
	if (fill)
	    GrFillArcEllipseLow(&d, l, t, r, b, GR_CET_BOUNDED_ARC, &ap,
				AREA);
	else
	    GrDrawArcEllipseLow(&d, l, t, r, b, GR_CET_BOUNDED_ARC, &ap);
    }
    GrExitGraphics(&d);
}

void GrDrawArc(GStateHandle gstate, sword left, sword top, sword right,
	       sword bottom, word startAngle, word endAngle,
	       ArcCloseType closeType)
{
    (void) GrGSRecord(gstate, GR_DRAW_ARC, "wwwwwww", closeType, left, top,
		      right, bottom, startAngle, endAngle);
    Bounded(gstate, left, top, right, bottom, startAngle, endAngle,
	    closeType, FALSE);
}

void GrFillArc(GStateHandle gstate, sword left, sword top, sword right,
	       sword bottom, word startAngle, word endAngle,
	       ArcCloseType closeType)
{
    (void) GrGSRecord(gstate, GR_FILL_ARC, "wwwwwww", closeType, left, top,
		      right, bottom, startAngle, endAngle);
    Bounded(gstate, left, top, right, bottom, startAngle, endAngle,
	    closeType, TRUE);
}

/* the arc through ap's three points; pen: set the pen to point 3 once
 * not rejected (the "to" arcs) */
static void ThreePoint(GrDraw *d, GrArcParams *ap, Boolean pen,
		       Boolean fill)
{
    sword l, t, r, b;
    word flags;

    if (GrTrivialReject(d))
	return;
    if (pen)
	GrSetDocWWFPenPos(d->GD_gs, WWF(&ap->GA_point3.PF_x),
			  WWF(&ap->GA_point3.PF_y));
    flags = GrSetupArc3PointLow(d, ap, &l, &t, &r, &b);
    if (fill)
	GrFillArcEllipseLow(d, l, t, r, b, flags, ap, AREA);
    else
	GrDrawArcEllipseLow(d, l, t, r, b, flags, ap);
}

static void Arc3Point(GStateHandle gstate, const ThreePointArcParams *p,
		      Boolean fill)
{
    GrDraw d;
    GrArcParams ap;
    const PointWWFixed *pp;

    GrEnterGraphics(gstate, &d);
    pp = p->TPAP_close == ACT_OPEN ? &p->TPAP_point1 : &p->TPAP_point3;
    GrSetDocWWFPenPos(d.GD_gs, WWF(&pp->PF_x), WWF(&pp->PF_y));
    memset(&ap, 0, sizeof(ap));
    ap.GA_close = (word) p->TPAP_close;
    ap.GA_point1 = p->TPAP_point1;
    ap.GA_point2 = p->TPAP_point2;
    ap.GA_point3 = p->TPAP_point3;
    ThreePoint(&d, &ap, FALSE, fill);
    GrExitGraphics(&d);
}

void GrDrawArc3Point(GStateHandle gstate, const ThreePointArcParams *params)
{
    Arc3Point(gstate, params, FALSE);
}

void GrFillArc3Point(GStateHandle gstate, const ThreePointArcParams *params)
{
    Arc3Point(gstate, params, TRUE);
}

static void Arc3PointTo(GStateHandle gstate, const ThreePointArcToParams *p,
			Boolean fill)
{
    GrDraw d;
    GrArcParams ap;

    memset(&ap, 0, sizeof(ap));
    GrGetCurPosWWFixed(gstate, &ap.GA_point1);
    ap.GA_close = (word) p->TPATP_close;
    ap.GA_point2 = p->TPATP_point2;
    ap.GA_point3 = p->TPATP_point3;
    GrEnterGraphics(gstate, &d);
    ThreePoint(&d, &ap, TRUE, fill);
    GrExitGraphics(&d);
}

void GrDrawArc3PointTo(GStateHandle gstate,
		       const ThreePointArcToParams *params)
{
    Arc3PointTo(gstate, params, FALSE);
}

void GrFillArc3PointTo(GStateHandle gstate,
		       const ThreePointArcToParams *params)
{
    Arc3PointTo(gstate, params, TRUE);
}

void GrDrawRelArc3PointTo(GStateHandle gstate,
			  const ThreePointRelArcToParams *params)
{
    GrDraw d;
    GrArcParams ap;
    sdword x, y;

    memset(&ap, 0, sizeof(ap));
    GrGetCurPosWWFixed(gstate, &ap.GA_point1);
    x = WWF(&ap.GA_point1.PF_x);
    y = WWF(&ap.GA_point1.PF_y);
    ap.GA_close = (word) params->TPRATP_close;
    SetWWF(&ap.GA_point2.PF_x, x + WWF(&params->TPRATP_delta2.PF_x));
    SetWWF(&ap.GA_point2.PF_y, y + WWF(&params->TPRATP_delta2.PF_y));
    SetWWF(&ap.GA_point3.PF_x, x + WWF(&params->TPRATP_delta3.PF_x));
    SetWWF(&ap.GA_point3.PF_y, y + WWF(&params->TPRATP_delta3.PF_y));
    GrEnterGraphics(gstate, &d);
    ThreePoint(&d, &ap, TRUE, FALSE);
    GrExitGraphics(&d);
}
