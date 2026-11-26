/*
 * graphicsSpline.c -- Bezier curves (after graphicsSpline.asm:
 * GrDrawCurve, GrDrawCurveTo, GrDrawRelCurveTo, GrDrawSpline,
 * GrDrawSplineTo, DrawSplineLow, CurveToPolyline; the halving is
 * RasterBezier of graphicsRegionRaster.c).
 *
 * A spline is 3n+1 points: a start and, per curve, two control points
 * and an end.  The points go to the device first, then each curve is
 * halved (de Casteljau, in 1/256 pixel) until its control points lie
 * within BEZIER_POINT_TOLERANCE of its ends or within half a pixel of
 * the line between them, or the depth runs out; the ends of the pieces
 * are a polyline, drawn with the line attributes.  The pen ends on the
 * last point.
 *
 * Graphics strings and paths store the element (graphicsString*.c,
 * graphicsPath.c: GrSplineDevPoints).  Not yet: the EC check of the
 * number of points.
 */
#include "graphicsInt.h"
#include <stdlib.h>

/* the device polyline of n document points (3k+1), in the separator
 * format (C heap, *count points); NULL if none */
sword *GrSplineDevPoints(const GrDraw *d, const Point *points, word n,
			 word *count)
{
    SplinePoly poly = {NULL, 0, 0};
    sword *dev;
    word i;

    if (n < 4)
	return NULL;
    dev = GrSeparatorFormat(d, points, n, FALSE, &i);
    if (!dev)
	return NULL;
    if (GrSplineAddPoint(&poly, (sdword) dev[0] << 8, (sdword) dev[1] << 8)) {
	for (i = 0; i + 3 < n; i += 3) {
	    sdword p[8];
	    int k;
	    for (k = 0; k < 8; k++)
		p[k] = (sdword) dev[2 * i + k] << 8;
	    if (!RasterBezier(&poly, p, 0))
		break;
	}
	free(dev);
	if (i + 3 >= n) {
	    poly.SP_pts[2 * poly.SP_count] = SEPARATOR;
	    poly.SP_pts[2 * poly.SP_count + 1] = SEPARATOR;
	    *count = poly.SP_count;
	    return poly.SP_pts;
	}
    } else
	free(dev);
    free(poly.SP_pts);
    return NULL;
}

/* DrawSplineLow */
static void DrawSpline(const GrDraw *d, const Point *points, word n)
{
    word count;
    sword *pts = GrSplineDevPoints(d, points, n, &count);

    if (pts)
	GrDrawPolylineMed(d, pts, count, FALSE);
    free(pts);
}

/* points[0] is the start (the pen for the ...To forms) */
static void Curve(GStateHandle gstate, const Point *points, word n,
		  Boolean fromPen)
{
    GrDraw d;
    Point *all = NULL;
    const Point *p = points;

    if (fromPen) {
	XYValueAsDWord pen = GrGetCurPos(gstate);
	all = malloc(((size_t) n + 1) * sizeof(Point));
	if (!all)
	    return;
	all[0].P_x = DWORD_X(pen);
	all[0].P_y = DWORD_Y(pen);
	memcpy(&all[1], points, (size_t) n * sizeof(Point));
	p = all;
	n++;
    }
    GrEnterGraphics(gstate, &d);
    if (n)				/* SetFinalPenPosition */
	GrSetDocPenPos(d.GD_gs, p[n - 1].P_x, p[n - 1].P_y);
    if (!GrTrivialReject(&d))
	DrawSpline(&d, p, n);
    free(all);
    GrExitGraphics(&d);
}

void GrDrawCurve(GStateHandle gstate, const Point *points)
{
    (void) GrGSRecord(gstate, GR_DRAW_CURVE, "r", points, 16);
    Curve(gstate, points, 4, FALSE);
}

void GrDrawCurveTo(GStateHandle gstate, const Point *points)
{
    (void) GrGSRecord(gstate, GR_DRAW_CURVE_TO, "r", points, 12);
    Curve(gstate, points, 3, TRUE);
}

void GrDrawSpline(GStateHandle gstate, const Point *points, word numPoints)
{
    (void) GrGSRecord(gstate, GR_DRAW_SPLINE, "wr", numPoints, points,
		      4 * numPoints);
    Curve(gstate, points, numPoints, FALSE);
}

void GrDrawSplineTo(GStateHandle gstate, const Point *points,
		    word numPoints)
{
    (void) GrGSRecord(gstate, GR_DRAW_SPLINE_TO, "wr", numPoints, points,
		      4 * numPoints);
    Curve(gstate, points, numPoints, TRUE);
}
