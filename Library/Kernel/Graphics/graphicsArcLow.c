/*
 * graphicsArcLow.c -- drawing and filling ellipses and arcs (after
 * graphicsArcLow.asm: DrawArcEllipseLow, FillArcEllipseLow,
 * SetupEllipseLow, SetupEllipseLineLow, TempFillPolygonLow).
 *
 * The points come from CalcEllipse (graphicsCalcEllipse.c).  An outline
 * is a closed polyline in the line attributes (thin, fat or dashed like
 * any polyline); a filled ellipse is convex and goes to the driver as
 * DR_VID_POLYGON.
 *
 * Not yet: arcs.
 */
#include "graphicsInt.h"
#include <stdlib.h>

#define AREA	((word) offsetof(GState, GS_areaAttr))

void GrDrawArcEllipseLow(const GrDraw *d, sword l, sword t, sword r,
			 sword b, word flags, GrArcParams *ap)
{
    sword *pts, top, bot;
    word count;

    if (d->GD_w->W_grFlags & WGF_MASK_NULL)
	return;
    pts = GrCalcEllipse(d, l, t, r, b, flags, ap, &count, &top, &bot);
    if (!pts)
	return;
    if (count >= 2)
	GrDrawPolylineLow(d, pts, count, TRUE);
    free(pts);
}

/* TempFillPolygonLow, for a convex polygon */
static void FillConvex(const GrDraw *d, sword *pts, word count, word attr)
{
    DriverRegs regs;

    memset(&regs, 0, sizeof(regs));
    regs.DR_cx = count;
    regs.DR_farPtr = pts;
    GrCallDriver(d, DR_VID_POLYGON, &regs, attr);
}

void GrFillArcEllipseLow(const GrDraw *d, sword l, sword t, sword r,
			 sword b, word flags, GrArcParams *ap, word attr)
{
    sword *pts, top, bot;
    word count;

    if (d->GD_w->W_grFlags & WGF_MASK_NULL)
	return;
    pts = GrCalcEllipse(d, l, t, r, b, flags, ap, &count, &top, &bot);
    if (!pts)
	return;
    if (count > 2)
	FillConvex(d, pts, count, attr);
    free(pts);
}

/* FillArcEllipseLow with SetupEllipseLineLow: device coordinates, the
 * line attributes (round line ends and joins) */
void GrFillEllipseDev(const GrDraw *d, sword l, sword t, sword r, sword b,
		      word attr)
{
    GrFillArcEllipseLow(d, l, t, r, b,
			GR_CET_ELLIPSE | GR_CEF_DONT_TRANSFORM, NULL, attr);
}
