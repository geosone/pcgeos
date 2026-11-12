/*
 * graphicsOutput.c -- rectangles and points (after graphicsOutput.asm:
 * GrFillRect, GrFillRectTo, GrDrawRect, GrDrawRectTo, GrDrawPoint,
 * GrDrawPointAtCP, FillRect, FillRectLow, DrawRect, DrawFillRotRect,
 * CalcRectCorners).
 *
 * Imaging conventions (2.0): a filled rectangle covers its corners'
 * pixels but not the right and bottom ones, unless the two coordinates
 * are the same.  Without rotation it goes to the driver as DR_VID_RECT
 * clipped to the mask's bounds; rotated, as a polygon of its corners
 * (DR_VID_POLYGON).  A frame is four lines with square ends.
 *
 * Not yet: graphics strings and paths (nothing is drawn), area
 * patterns.
 */
#include "graphicsInt.h"

#define AREA	((word) offsetof(GState, GS_areaAttr))
#define LINE	((word) offsetof(GState, GS_lineAttr))

/* FillRectLow: device coordinates, the right and bottom ones not
 * filled unless equal to the left and top */
void GrFillRectLow(const GrDraw *d, sword l, sword t, sword r, sword b,
		   word attr)
{
    const Window *w = d->GD_w;
    DriverRegs regs;
    sword s;

    if (l > r) {
	s = l; l = r; r = s;
    }
    if (l != r)
	r--;
    if (t > b) {
	s = t; t = b; b = s;
    }
    if (t != b)
	b--;
    if (r < w->W_maskRect.R_left || l > w->W_maskRect.R_right ||
	b < w->W_maskRect.R_top || t > w->W_maskRect.R_bottom)
	return;
    if (l < w->W_maskRect.R_left)
	l = w->W_maskRect.R_left;
    if (r > w->W_maskRect.R_right)
	r = w->W_maskRect.R_right;
    if (t < w->W_maskRect.R_top)
	t = w->W_maskRect.R_top;
    if (b > w->W_maskRect.R_bottom)
	b = w->W_maskRect.R_bottom;
    memset(&regs, 0, sizeof(regs));
    regs.DR_ax = (word) l;
    regs.DR_bx = (word) t;
    regs.DR_cx = (word) r;
    regs.DR_dx = (word) b;
    GrCallDriver(d, DR_VID_RECT, &regs, attr);
}

/* CalcRectCorners: the four corners in the device, clockwise from the
 * top left */
static Boolean Corners(const GrDraw *d, sword l, sword t, sword r, sword b,
		       Point *p)
{
    return (Boolean) (GrTransDev(d, l, t, &p[0].P_x, &p[0].P_y) &&
		      GrTransDev(d, r, t, &p[1].P_x, &p[1].P_y) &&
		      GrTransDev(d, r, b, &p[2].P_x, &p[2].P_y) &&
		      GrTransDev(d, l, b, &p[3].P_x, &p[3].P_y));
}

/* FillRect: document coordinates */
static void FillRect(const GrDraw *d, sword l, sword t, sword r, sword b)
{
    sword dl, dt, dr, db;
    Point p[4];
    DriverRegs regs;

    if (d->GD_w->W_curTMatrix.TM_flags & TM_ROTATED) {
	if (!Corners(d, l, t, r, b, p))	/* DrawFillRotRect */
	    return;
	memset(&regs, 0, sizeof(regs));
	regs.DR_cx = 4;
	regs.DR_farPtr = p;
	GrCallDriver(d, DR_VID_POLYGON, &regs, AREA);
	return;
    }
    if (GrTransDev(d, l, t, &dl, &dt) && GrTransDev(d, r, b, &dr, &db))
	GrFillRectLow(d, dl, dt, dr, db, AREA);
}

/* DrawRect: four lines with square ends, in the device; a fat frame as
 * a closed polyline (DrawFillRotRect) */
static void DrawRect(GrDraw *d, sword l, sword t, sword r, sword b)
{
    byte end = d->GD_gs->GS_lineEnd;
    Point p[4];
    int i;

    if (!GrCheckThinLine(d)) {
	p[0].P_x = l; p[0].P_y = t;
	p[1].P_x = r; p[1].P_y = t;
	p[2].P_x = r; p[2].P_y = b;
	p[3].P_x = l; p[3].P_y = b;
	GrDrawPolylineDoc(d, p, 4, TRUE);
	return;
    }
    if (!Corners(d, l, t, r, b, p))
	return;
    d->GD_gs->GS_lineEnd = LE_SQUARECAP;
    if (d->GD_w->W_curTMatrix.TM_flags & TM_ROTATED)
	for (i = 0; i < 4; i++)
	    GrDrawLineLow(d, p[i].P_x, p[i].P_y, p[(i + 1) & 3].P_x,
			  p[(i + 1) & 3].P_y, LINE);
    else {
	GrDrawLineLow(d, p[0].P_x, p[0].P_y, p[0].P_x, p[2].P_y, LINE);
	GrDrawLineLow(d, p[2].P_x, p[0].P_y, p[2].P_x, p[2].P_y, LINE);
	GrDrawLineLow(d, p[0].P_x, p[0].P_y, p[2].P_x, p[0].P_y, LINE);
	GrDrawLineLow(d, p[0].P_x, p[2].P_y, p[2].P_x, p[2].P_y, LINE);
    }
    d->GD_gs->GS_lineEnd = end;
}

void GrFillRect(GStateHandle gstate, sword left, sword top, sword right,
		sword bottom)
{
    GrDraw d;

    GrEnterGraphics(gstate, &d);
    GrSetDocPenPos(d.GD_gs, left, top);
    if (!GrTrivialReject(&d)) {
	sword s;
	if (left > right) {
	    s = left; left = right; right = s;
	}
	if (top > bottom) {
	    s = top; top = bottom; bottom = s;
	}
	FillRect(&d, left, top, right, bottom);
    }
    GrExitGraphics(&d);
}

/* the document position of the pen (GetDocPenPos), rounded */
static void DocPen(const GrDraw *d, sword *x, sword *y)
{
    XYValueAsDWord p;

    MemUnlock(d->GD_gstate);		/* GrGetCurPos locks it itself */
    p = GrGetCurPos(d->GD_gstate);
    MemLock(d->GD_gstate);
    *x = DWORD_X(p);
    *y = DWORD_Y(p);
}

void GrFillRectTo(GStateHandle gstate, sword x, sword y)
{
    GrDraw d;
    sword px, py;

    GrEnterGraphics(gstate, &d);
    if (!GrTrivialReject(&d)) {
	DocPen(&d, &px, &py);
	FillRect(&d, px < x ? px : x, py < y ? py : y, px < x ? x : px,
		 py < y ? y : py);
    }
    GrExitGraphics(&d);
}

void GrDrawRect(GStateHandle gstate, sword left, sword top, sword right,
		sword bottom)
{
    GrDraw d;

    GrEnterGraphics(gstate, &d);
    GrSetDocPenPos(d.GD_gs, left, top);
    if (!GrTrivialReject(&d))
	DrawRect(&d, left, top, right, bottom);
    GrExitGraphics(&d);
}

void GrDrawRectTo(GStateHandle gstate, sword x, sword y)
{
    GrDraw d;
    sword px, py;

    GrEnterGraphics(gstate, &d);
    if (!GrTrivialReject(&d)) {
	DocPen(&d, &px, &py);
	DrawRect(&d, px, py, x, y);
    }
    GrExitGraphics(&d);
}

void GrDrawPoint(GStateHandle gstate, sword x, sword y)
{
    GrDraw d;
    sword dx, dy;

    GrEnterGraphics(gstate, &d);
    GrSetDocPenPos(d.GD_gs, x, y);
    if (!GrTrivialReject(&d) && GrTransDev(&d, x, y, &dx, &dy))
	GrFillRectLow(&d, dx, dy, dx, dy, AREA);
    GrExitGraphics(&d);
}

void GrDrawPointAtCP(GStateHandle gstate)
{
    GrDraw d;
    sword dx, dy;

    GrEnterGraphics(gstate, &d);
    if (!GrTrivialReject(&d) && GrGetDevPenPos(&d, &dx, &dy))
	GrFillRectLow(&d, dx, dy, dx, dy, AREA);
    GrExitGraphics(&d);
}
