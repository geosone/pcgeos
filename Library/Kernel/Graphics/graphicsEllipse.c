/*
 * graphicsEllipse.c -- ellipses (after graphicsEllipse.asm: GrDrawEllipse,
 * GrFillEllipse).
 *
 * The bounds are document coordinates; the pen goes to the first
 * corner.  Graphics strings and paths store the element
 * (graphicsString*.c, graphicsPath.c).
 */
#include "graphicsInt.h"

#define AREA	((word) offsetof(GState, GS_areaAttr))

void GrDrawEllipse(GStateHandle gstate, sword left, sword top, sword right,
		   sword bottom)
{
    GrDraw d;

    (void) GrGSRecord(gstate, GR_DRAW_ELLIPSE, "wwww", left, top, right, bottom);
    GrEnterGraphics(gstate, &d);
    GrSetDocPenPos(d.GD_gs, left, top);
    if (!GrTrivialReject(&d))
	GrDrawArcEllipseLow(&d, left, top, right, bottom, GR_CET_ELLIPSE,
			    NULL);
    GrExitGraphics(&d);
}

void GrFillEllipse(GStateHandle gstate, sword left, sword top, sword right,
		   sword bottom)
{
    GrDraw d;

    if (GrPatternStart(gstate, FALSE)) {
	GrFillEllipse(gstate, left, top, right, bottom);
	GrPatternFinish(gstate, ODD_EVEN, FALSE);
	return;
    }
    (void) GrGSRecord(gstate, GR_FILL_ELLIPSE, "wwww", left, top, right, bottom);
    GrEnterGraphics(gstate, &d);
    GrSetDocPenPos(d.GD_gs, left, top);
    if (!GrTrivialReject(&d))
	GrFillArcEllipseLow(&d, left, top, right, bottom, GR_CET_ELLIPSE,
			    NULL, AREA);
    GrExitGraphics(&d);
}
