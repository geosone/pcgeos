/*
 * graphicsPolyline.c -- polylines (after graphicsPolyline.asm:
 * GrDrawPolyline, GrDrawPolygon, GrBrushPolyline, SetPolyPenPos,
 * DrawPolyline, DrawPolylineMed, GetPolylineBounds, DrawPolylineLow,
 * CreateSeparatorFormat, DrawSeparatorFormat, ScaleLineWidth,
 * ScaleScalar, DistanceApprox; graphicsLine.asm: CheckThinLine).
 *
 * The points are transformed into the separator format: x, y pairs in
 * device coordinates, SEPARATOR after each polyline and once more at the
 * end; a closed polyline gets its first point again at the end.  A
 * point past MAX_COORD (or an overflow) draws nothing.  The format is
 * what DR_VID_POLYLINE takes; the asm kept it in a block, here it is in
 * the C heap.
 *
 * The line width is scaled by the window's matrix (ScaleScalar: the
 * mean of the approximate lengths of the width along x and along y).
 * A polyline wider than a pixel is drawn by the fat line code
 * (graphicsFatLine.c), a styled one is cut into dashes first
 * (graphicsLineStyles.c).
 */
#include "graphicsInt.h"
#include <stdlib.h>

#define LINE	((word) offsetof(GState, GS_lineAttr))

/* CreateSeparatorFormat; NULL when a point does not fit */
sword *GrSeparatorFormat(const GrDraw *d, const Point *points, word n,
			 Boolean close, word *count)
{
    sword *sep, *o;
    word i;

    if (n == 1)
	close = TRUE;
    sep = malloc(((size_t) n + 3) * 2 * sizeof(sword));
    if (!sep)
	return NULL;
    for (i = 0, o = sep; i < n; i++, o += 2)
	if (!GrTransDev(d, points[i].P_x, points[i].P_y, &o[0], &o[1]) ||
	    o[0] > MAX_COORD || o[1] > MAX_COORD) {
	    free(sep);
	    return NULL;
	}
    if (close) {
	o[0] = sep[0];
	o[1] = sep[1];
	o += 2;
	n++;
    }
    o[0] = o[1] = SEPARATOR;
    *count = n;
    return sep;
}

/* GetPolylineBounds */
void GrPolylineBounds(const sword *p, Rectangle *b)
{
    b->R_left = b->R_right = p[0];
    b->R_top = b->R_bottom = p[1];
    for (p += 2;; p += 2) {
	if (p[0] == SEPARATOR) {
	    if (p[1] == SEPARATOR)
		break;
	    p++;
	}
	if (p[0] < b->R_left)
	    b->R_left = p[0];
	else if (p[0] > b->R_right)
	    b->R_right = p[0];
	if (p[1] < b->R_top)
	    b->R_top = p[1];
	else if (p[1] > b->R_bottom)
	    b->R_bottom = p[1];
    }
}

/* DistanceApprox: the larger difference plus half the smaller */
static sdword Distance(int64_t x0, int64_t y0, int64_t x1, int64_t y1)
{
    sdword dx = (sdword) (x0 - x1), dy = (sdword) (y0 - y1);

    if (dx < 0)
	dx = -dx;
    if (dy < 0)
	dy = -dy;
    if (dx < dy) {
	sdword s = dx; dx = dy; dy = s;
    }
    return (sdword) (((dword) dx * 2 + (dword) dy)) >> 1;
}

/* ScaleScalar: a length (WWFixed) through the window's matrix, rounded */
word GrScaleScalar(const GrDraw *d, sdword len)
{
    if (d->GD_w) {
	const TMatrix *m = &d->GD_w->W_TMatrix;
	int64_t x0 = 0, y0 = 0, x1 = len, y1 = 0, x2 = 0, y2 = len;

	GrMatTransDWF(m, &x0, &y0);
	GrMatTransDWF(m, &x1, &y1);
	GrMatTransDWF(m, &x2, &y2);
	len = (sdword) (((dword) Distance(x0, y0, x1, y1) +
			 (dword) Distance(x0, y0, x2, y2))) >> 1;
    }
    return (word) ((dword) (len + 0x8000) >> 16);
}

static sdword LineWidth(const GState *gs)
{
    return (sdword) (((dword) (word) gs->GS_lineWidth.WWF_int << 16) |
		     gs->GS_lineWidth.WWF_frac);
}

/* ScaleLineWidth: GS_scaledLineWid, at least 1 */
void GrScaleLineWidth(const GrDraw *d)
{
    word w = GrScaleScalar(d, LineWidth(d->GD_gs));

    d->GD_gs->GS_scaledLineWid = w ? w : 1;
}

/* CheckThinLine: TRUE when lines are one pixel wide */
Boolean GrCheckThinLine(const GrDraw *d)
{
    sdword w = LineWidth(d->GD_gs);

    if (d->GD_w->W_TMatrix.TM_flags & TM_COMPLEX)
	return (Boolean) (GrScaleScalar(d, w) <= 1);
    return (Boolean) (!(w & 0xffff) && (dword) w >> 16 <= 1);
}

/* DrawSeparatorFormat */
void GrDrawSeparatorFormat(const GrDraw *d, const sword *sep, word brush)
{
    DriverRegs r;

    memset(&r, 0, sizeof(r));
    r.DR_ax = brush;
    r.DR_farPtr = (void *) sep;
    GrCallDriver(d, DR_VID_POLYLINE, &r, LINE);
}

/* DrawPolylineLow: device points, the line attributes */
void GrDrawPolylineLow(const GrDraw *d, const sword *sep, word count,
		       Boolean connected)
{
    sword *dashes = NULL;

    GrScaleLineWidth(d);
    if (d->GD_gs->GS_lineStyle != LS_SOLID) {
	dashes = GrDashPolyline(d, sep, count);
	if (!dashes)
	    return;
	sep = dashes;
    }
    if (d->GD_gs->GS_scaledLineWid > 1)
	GrDrawFatLines(d, sep, connected);
    else
	GrDrawSeparatorFormat(d, sep, 0);
    free(dashes);
}

/* DrawPolylineMed: rejected on its bounds (widened by half the line
 * width), unless a mitered polyline, whose joins may reach further */
void GrDrawPolylineMed(const GrDraw *d, const sword *sep, word count,
		       Boolean connected)
{
    if (count <= 2 || d->GD_gs->GS_lineJoin != LJ_MITERED) {
	Rectangle b;
	word w = d->GD_gs->GS_scaledLineWid;

	GrPolylineBounds(sep, &b);
	if (w > 1) {
	    w = (word) ((w >> 1) + 1);
	    b.R_left = (sword) (b.R_left - w);
	    b.R_top = (sword) (b.R_top - w);
	    b.R_right = (sword) (b.R_right + w);
	    b.R_bottom = (sword) (b.R_bottom + w);
	}
	if (GrRejectRect(d, b.R_left, b.R_top, b.R_right, b.R_bottom))
	    return;
    }
    GrDrawPolylineLow(d, sep, count, connected);
}

/* DrawPolyline: document points */
void GrDrawPolylineDoc(const GrDraw *d, const Point *points, word n,
		       Boolean connected)
{
    sword *sep;
    word count;

    GrScaleLineWidth(d);
    if (!n)
	return;
    sep = GrSeparatorFormat(d, points, n, connected, &count);
    if (!sep)
	return;
    GrDrawPolylineMed(d, sep, count, connected);
    free(sep);
}

void GrDrawPolyline(GStateHandle gstate, const Point *points, word numPoints)
{
    GrDraw d;

    GrEnterGraphics(gstate, &d);
    if (numPoints)			/* SetPolyPenPos: the last point */
	GrSetDocPenPos(d.GD_gs, points[numPoints - 1].P_x,
		       points[numPoints - 1].P_y);
    if (!GrTrivialReject(&d))
	GrDrawPolylineDoc(&d, points, numPoints, FALSE);
    GrExitGraphics(&d);
}

void GrDrawPolygon(GStateHandle gstate, const Point *points, word numPoints)
{
    GrDraw d;

    GrEnterGraphics(gstate, &d);
    if (numPoints)
	GrSetDocPenPos(d.GD_gs, points[0].P_x, points[0].P_y);
    if (!GrTrivialReject(&d))
	GrDrawPolylineDoc(&d, points, numPoints, TRUE);
    GrExitGraphics(&d);
}

/*
 * A brush of brushW x brushH pixels, in the line color, drawn along the
 * points (for ink).  The C stub read its fourth argument as the width,
 * so GrBrushPolyline(gs, pts, n, a, b) brushes a wide and b high, as it
 * did in GEOS.
 */
void GrBrushPolyline(GStateHandle gstate, const Point *points,
		     word numPoints, word brushH, word brushW)
{
    GrDraw d;
    word brush = (word) ((brushH & 0xff) | ((brushW & 0xff) << 8));
    sword *sep, pts[6];
    word count;
    Rectangle b;

    GrEnterGraphics(gstate, &d);
    if (numPoints)
	GrSetDocPenPos(d.GD_gs, points[numPoints - 1].P_x,
		       points[numPoints - 1].P_y);
    if (!GrTrivialReject(&d) && numPoints) {
	if (numPoints == 2) {		/* the usual ink stroke */
	    GrTransDev(&d, points[0].P_x, points[0].P_y, &pts[0], &pts[1]);
	    GrTransDev(&d, points[1].P_x, points[1].P_y, &pts[2], &pts[3]);
	    pts[4] = pts[5] = SEPARATOR;
	    GrDrawSeparatorFormat(&d, pts, brush);
	} else if ((sep = GrSeparatorFormat(&d, points, numPoints, FALSE,
					    &count)) != NULL) {
	    GrPolylineBounds(sep, &b);
	    if (!GrRejectRect(&d, b.R_left, b.R_top,
			      (sword) (b.R_right + (brush & 0xff)),
			      (sword) (b.R_bottom + (brush >> 8))))
		GrDrawSeparatorFormat(&d, sep, brush);
	    free(sep);
	}
    }
    GrExitGraphics(&d);
}
