/*
 * graphicsSpline.c -- Bezier curves (after graphicsSpline.asm:
 * GrDrawCurve, GrDrawCurveTo, GrDrawRelCurveTo, GrDrawSpline,
 * GrDrawSplineTo, DrawSplineLow, CurveToPolyline; RasterBezier of
 * graphicsRegionRaster.asm).
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

#define BEZIER_POINT_TOLERANCE	2	/* grWinInt.def, pixels */
#define BEZIER_MAX_DEPTH	10

typedef struct {
    sword	*SP_pts;	/* device x, y pairs */
    word	SP_count;
    word	SP_size;
} SplinePoly;

static Boolean AddPoint(SplinePoly *p, sdword x, sdword y)
{
    if (p->SP_count + 2 > p->SP_size) {	/* and room for SEPARATOR */
	word n = (word) (p->SP_size ? p->SP_size * 2 : 64);
	sword *np = realloc(p->SP_pts, (size_t) n * 2 * sizeof(sword));
	if (!np)
	    return FALSE;
	p->SP_pts = np;
	p->SP_size = n;
    }
    p->SP_pts[2 * p->SP_count] = (sword) ((x + 128) >> 8);
    p->SP_pts[2 * p->SP_count + 1] = (sword) ((y + 128) >> 8);
    p->SP_count++;
    return TRUE;
}

static sdword Abs(sdword v)
{
    return v < 0 ? -v : v;
}

/* the distance of c from the line a-b, squared, is within half a pixel */
static Boolean NearLine(const sdword *a, const sdword *b, const sdword *c)
{
    int64_t dx = b[0] - a[0], dy = b[1] - a[1];
    int64_t cross = dx * (c[1] - a[1]) - dy * (c[0] - a[0]);
    int64_t len2 = dx * dx + dy * dy;

    if (!len2)
	return FALSE;
    /* (cross / len)^2 <= (128)^2 */
    return (Boolean) ((double) cross * (double) cross <=
		      16384.0 * (double) len2);
}

/* RasterBezier: p[0..7] the four points (x, y), 1/256 pixel; adds the
 * end of each piece */
static Boolean Bezier(SplinePoly *poly, const sdword *p, int depth)
{
    sdword l[8], r[8], m[2];
    int i;
    Boolean flat;

    flat = (Boolean) (Abs(p[2] - p[0]) <= BEZIER_POINT_TOLERANCE * 256 &&
		      Abs(p[3] - p[1]) <= BEZIER_POINT_TOLERANCE * 256 &&
		      Abs(p[6] - p[4]) <= BEZIER_POINT_TOLERANCE * 256 &&
		      Abs(p[7] - p[5]) <= BEZIER_POINT_TOLERANCE * 256);
    if (!flat)
	flat = (Boolean) (NearLine(&p[0], &p[6], &p[2]) &&
			  NearLine(&p[0], &p[6], &p[4]));
    if (flat || depth >= BEZIER_MAX_DEPTH)
	return AddPoint(poly, p[6], p[7]);
    for (i = 0; i < 2; i++) {			/* DivideCurve */
	sdword p01 = (p[i] + p[2 + i]) / 2, p12 = (p[2 + i] + p[4 + i]) / 2;
	sdword p23 = (p[4 + i] + p[6 + i]) / 2;
	sdword a = (p01 + p12) / 2, b = (p12 + p23) / 2;
	m[i] = (a + b) / 2;
	l[i] = p[i];
	l[2 + i] = p01;
	l[4 + i] = a;
	l[6 + i] = m[i];
	r[i] = m[i];
	r[2 + i] = b;
	r[4 + i] = p23;
	r[6 + i] = p[6 + i];
    }
    return (Boolean) (Bezier(poly, l, depth + 1) &&
		      Bezier(poly, r, depth + 1));
}

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
    if (AddPoint(&poly, (sdword) dev[0] << 8, (sdword) dev[1] << 8)) {
	for (i = 0; i + 3 < n; i += 3) {
	    sdword p[8];
	    int k;
	    for (k = 0; k < 8; k++)
		p[k] = (sdword) dev[2 * i + k] << 8;
	    if (!Bezier(&poly, p, 0))
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
