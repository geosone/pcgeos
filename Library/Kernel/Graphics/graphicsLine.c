/*
 * graphicsLine.c -- lines (after graphicsLine.asm: GrDrawLine,
 * GrDrawLineTo, GrDrawRelLineTo, GrDrawHLine, GrDrawHLineTo,
 * GrDrawVLine, GrDrawVLineTo, DrawLine, DrawLineLow, AdjustLineEnds,
 * TrivialRejectRect).
 *
 * A line is drawn in the device between its transformed endpoints.
 * With LE_BUTTCAP the last pixel is left out (AdjustLineEnds): on the
 * right of a more horizontal line, at the bottom of a more vertical
 * one, both at 45 degrees.  A horizontal or vertical line goes to the
 * driver as a rectangle, others as DR_VID_LINE.  The pen moves to the
 * second endpoint.
 *
 * A line wider than a pixel is drawn as a polyline
 * (graphicsPolyline.c), a styled one in dashes (graphicsLineStyles.c).
 */
#include "graphicsInt.h"

#define LINE	((word) offsetof(GState, GS_lineAttr))

/* AdjustLineEnds */
void GrAdjustLineEnds(const GState *gs, sword *x1, sword *y1, sword *x2,
		      sword *y2)
{
    sword ddx, ddy;

    if (gs->GS_lineEnd != LE_BUTTCAP)
	return;
    ddx = (sword) (*x2 - *x1 < 0 ? *x1 - *x2 : *x2 - *x1);
    ddy = (sword) (*y2 - *y1 < 0 ? *y1 - *y2 : *y2 - *y1);
    if ((word) ddy >= (word) ddx && *y1 != *y2) {	/* vertical, 45 */
	if (*y1 < *y2)
	    (*y2)--;
	else
	    (*y1)--;
    }
    if ((word) ddy <= (word) ddx && *x1 != *x2) {	/* horizontal, 45 */
	if (*x1 < *x2)
	    (*x2)--;
	else
	    (*x1)--;
    }
}

/* TrivialRejectRect: TRUE when the bounds (not sorted) are outside the
 * mask's */
Boolean GrRejectRect(const GrDraw *d, sword l, sword t, sword r, sword b)
{
    const Window *w = d->GD_w;

    return (Boolean) ((l < r ? r : l) < w->W_maskRect.R_left ||
		      (l < r ? l : r) > w->W_maskRect.R_right ||
		      (t < b ? b : t) < w->W_maskRect.R_top ||
		      (t < b ? t : b) > w->W_maskRect.R_bottom);
}

/* DrawLineLow: device coordinates */
void GrDrawLineLow(const GrDraw *d, sword x1, sword y1, sword x2, sword y2,
		   word attr)
{
    DriverRegs r;
    sword s;
    word fn = DR_VID_LINE;

    if (d->GD_gs->GS_lineStyle != LS_SOLID) {
	GrDrawLineLowStyled(d, x1, y1, x2, y2, attr);
	return;
    }
    GrAdjustLineEnds(d->GD_gs, &x1, &y1, &x2, &y2);
    if (GrRejectRect(d, x1, y1, x2, y2))
	return;
    if (x1 == x2 || y1 == y2) {
	fn = DR_VID_RECT;
	if (x1 > x2) {
	    s = x1; x1 = x2; x2 = s;
	}
	if (y1 > y2) {
	    s = y1; y1 = y2; y2 = s;
	}
    }
    memset(&r, 0, sizeof(r));
    r.DR_ax = (word) x1;
    r.DR_bx = (word) y1;
    r.DR_cx = (word) x2;
    r.DR_dx = (word) y2;
    GrCallDriver(d, fn, &r, attr);
}

/* DrawFatOrThinLine: document coordinates; a fat line as a polyline */
static void DrawLine(const GrDraw *d, sword x1, sword y1, sword x2, sword y2)
{
    sword a, b, c, e;
    Point p[2];

    if (!GrCheckThinLine(d)) {
	p[0].P_x = x1;
	p[0].P_y = y1;
	p[1].P_x = x2;
	p[1].P_y = y2;
	GrDrawPolylineDoc(d, p, 2, FALSE);
    } else if (GrTransDev(d, x1, y1, &a, &b) && GrTransDev(d, x2, y2, &c, &e))
	GrDrawLineLow(d, a, b, c, e, LINE);
}

static void Line(GStateHandle gstate, sword x1, sword y1, sword x2, sword y2)
{
    GrDraw d;

    GrEnterGraphics(gstate, &d);
    GrSetDocPenPos(d.GD_gs, x2, y2);
    if (!GrTrivialReject(&d))
	DrawLine(&d, x1, y1, x2, y2);
    GrExitGraphics(&d);
}

void GrDrawLine(GStateHandle gstate, sword x1, sword y1, sword x2, sword y2)
{
    (void) GrGSRecord(gstate, GR_DRAW_LINE, "wwww", x1, y1, x2, y2);
    Line(gstate, x1, y1, x2, y2);
}

void GrDrawHLine(GStateHandle gstate, sword x1, sword y, sword x2)
{
    (void) GrGSRecord(gstate, GR_DRAW_HLINE, "www", x1, y, x2);
    Line(gstate, x1, y, x2, y);
}

void GrDrawVLine(GStateHandle gstate, sword x, sword y1, sword y2)
{
    (void) GrGSRecord(gstate, GR_DRAW_VLINE, "www", x, y1, y2);
    Line(gstate, x, y1, x, y2);
}

/* from the pen (in the device) to x, y; a fat line from the pen in
 * document coordinates */
static void LineTo(GStateHandle gstate, sword x, sword y)
{
    GrDraw d;
    sword px, py, dx, dy;
    Boolean ok;

    GrEnterGraphics(gstate, &d);
    if (!GrTrivialReject(&d) && !GrCheckThinLine(&d)) {
	XYValueAsDWord pen;
	MemUnlock(gstate);
	pen = GrGetCurPos(gstate);
	MemLock(gstate);
	GrSetDocPenPos(d.GD_gs, x, y);
	DrawLine(&d, DWORD_X(pen), DWORD_Y(pen), x, y);
	GrExitGraphics(&d);
	return;
    }
    ok = (Boolean) (!GrTrivialReject(&d) && GrGetDevPenPos(&d, &px, &py));
    GrSetDocPenPos(d.GD_gs, x, y);
    if (ok && GrTransDev(&d, x, y, &dx, &dy))
	GrDrawLineLow(&d, px, py, dx, dy, LINE);
    GrExitGraphics(&d);
}

void GrDrawLineTo(GStateHandle gstate, sword x, sword y)
{
    (void) GrGSRecord(gstate, GR_DRAW_LINE_TO, "ww", x, y);
    LineTo(gstate, x, y);
}

void GrDrawHLineTo(GStateHandle gstate, sword x)
{
    XYValueAsDWord p = GrGetCurPos(gstate);

    (void) GrGSRecord(gstate, GR_DRAW_HLINE_TO, "w", x);
    LineTo(gstate, x, DWORD_Y(p));
}

void GrDrawVLineTo(GStateHandle gstate, sword y)
{
    XYValueAsDWord p = GrGetCurPos(gstate);

    (void) GrGSRecord(gstate, GR_DRAW_VLINE_TO, "w", y);
    LineTo(gstate, DWORD_X(p), y);
}

/* the pen moves by x, y (through the GState's 2x2 part) */
void GrDrawRelLineTo(GStateHandle gstate, WWFixedAsDWord x, WWFixedAsDWord y)
{
    GrDraw d;
    sword px, py, nx, ny;
    Boolean ok;

    (void) GrGSRecord(gstate, GR_DRAW_REL_LINE_TO, "dd", x, y);
    GrEnterGraphics(gstate, &d);
    ok = (Boolean) (!GrTrivialReject(&d) && GrGetDevPenPos(&d, &px, &py));
    MemUnlock(gstate);			/* GrRelMovePen locks it itself */
    GrRelMovePen(gstate, x, y);
    MemLock(gstate);
    if (ok && GrGetDevPenPos(&d, &nx, &ny))
	GrDrawLineLow(&d, px, py, nx, ny, LINE);
    GrExitGraphics(&d);
}
