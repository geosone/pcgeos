/*
 * vidcomPolygon.c -- DR_VID_POLYGON (after vidcomPolygon.asm:
 * VidPolygon, ScanPolygonLeft, FillPolygonRight, GetPolygonBounds).
 *
 * A convex polygon (bx:dx the points, cx how many, at least 3), filled
 * with its outline: each edge is stepped as a line (VidLineRuns), each
 * line of the polygon filled from its leftmost to its rightmost edge
 * pixel.  With al 1 it is drawn only if its points go clockwise (on the
 * screen, y down), as the asm decided at the lowest point: by the sign
 * of its area here.
 */
#include "vidcom.h"

#define POLY_ROWS	16384		/* the tallest screen */

static sword rowMin[POLY_ROWS], rowMax[POLY_ROWS];
static sword rowTop, rowBottom;		/* the rows kept */

static void EdgeRun(sword x1, sword y1, sword x2, sword y2, word k,
		    void *ctx)
{
    sword y, l = x1 < x2 ? x1 : x2, r = x1 < x2 ? x2 : x1;
    sword t = y1 < y2 ? y1 : y2, b = y1 < y2 ? y2 : y1;

    (void) k; (void) ctx;
    if (t < rowTop)
	t = rowTop;
    if (b > rowBottom)
	b = rowBottom;
    for (y = t; y <= b; y++) {
	if (l < rowMin[y - rowTop])
	    rowMin[y - rowTop] = l;
	if (r > rowMax[y - rowTop])
	    rowMax[y - rowTop] = r;
    }
}

void VidFillPolygon(Window *w, const VidDrawState *ds, const Point *p,
		    word n)
{
    sword top = p[0].P_y, bottom = p[0].P_y, y;
    word i;

    for (i = 1; i < n; i++) {
	if (p[i].P_y < top)
	    top = p[i].P_y;
	if (p[i].P_y > bottom)
	    bottom = p[i].P_y;
    }
    /* only the rows that can be seen */
    if (top < w->W_maskRect.R_top)
	top = w->W_maskRect.R_top;
    if (bottom > w->W_maskRect.R_bottom)
	bottom = w->W_maskRect.R_bottom;
    if (top < 0)
	top = 0;
    if (bottom >= POLY_ROWS)
	bottom = POLY_ROWS - 1;
    if (top > bottom)
	return;
    rowTop = top;
    rowBottom = bottom;
    for (y = top; y <= bottom; y++) {
	rowMin[y - top] = 0x7fff;
	rowMax[y - top] = (sword) 0x8000;
    }
    for (i = 0; i < n; i++)
	VidLineRuns(p[i].P_x, p[i].P_y, p[(i + 1) % n].P_x,
		    p[(i + 1) % n].P_y, FALSE, EdgeRun, NULL);
    for (y = top; y <= bottom; y++)
	if (rowMin[y - top] <= rowMax[y - top])
	    VidFillClipped(w, rowMin[y - top], y, rowMax[y - top], y, ds);
}

/* the bounds of the points */
void VidPointBounds(const Point *p, word n, Rectangle *b)
{
    word i;

    b->R_left = b->R_right = p[0].P_x;
    b->R_top = b->R_bottom = p[0].P_y;
    for (i = 1; i < n; i++) {
	if (p[i].P_x < b->R_left)
	    b->R_left = p[i].P_x;
	if (p[i].P_x > b->R_right)
	    b->R_right = p[i].P_x;
	if (p[i].P_y < b->R_top)
	    b->R_top = p[i].P_y;
	if (p[i].P_y > b->R_bottom)
	    b->R_bottom = p[i].P_y;
    }
}

void VidPolygon(DriverRegs *r)
{
    Window *w = r->DR_esDi;
    const Point *p = r->DR_farPtr;
    word n = (word) r->DR_cx, i;
    VidDrawState ds;
    Rectangle b;
    long area = 0;

    if (!r->DR_dsSi || !w || !p || n < 3)
	return;
    if ((byte) r->DR_ax == 1) {
	for (i = 0; i < n; i++)
	    area += (long) p[i].P_x * p[(i + 1) % n].P_y -
		    (long) p[(i + 1) % n].P_x * p[i].P_y;
	if (area < 0)
	    return;			/* counterclockwise */
    }
    VidSetupDraw(r->DR_dsSi, (const CommonAttr *) ((byte *) r->DR_dsSi +
						   (word) r->DR_si),
		 w, &ds);
    VidPointBounds(p, n, &b);
    VidDevCondHidePtr(b.R_left, b.R_top, b.R_right, b.R_bottom);
    VidFillPolygon(w, &ds, p, n);
    VidDevCondShowPtr();
}
