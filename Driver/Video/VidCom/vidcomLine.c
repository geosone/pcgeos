/*
 * vidcomLine.c -- DR_VID_LINE (after vidcomLine.asm: VidDrawLine).
 *
 * A line one pixel wide from (ax,bx) to (cx,dx), both ends drawn, with
 * the CommonAttr at si (the line attributes, as the kernel passes
 * them).  Bresenham's line, in runs: a horizontal or vertical run is a
 * rectangle through the window's mask (VidFillClipped), so a line
 * costs a rectangle a run.  The asm stepped its own DDA through the
 * clip region; the pixels may differ by one where the error ties.
 */
#include "vidcom.h"

void VidDrawLine(DriverRegs *r)
{
    GState *gs = r->DR_dsSi;
    Window *w = r->DR_esDi;
    sword x1 = (sword) (word) r->DR_ax, y1 = (sword) (word) r->DR_bx;
    sword x2 = (sword) (word) r->DR_cx, y2 = (sword) (word) r->DR_dx;
    sword dx, dy, sx, sy, runStart;
    long err;
    VidDrawState ds;

    if (!gs || !w)
	return;
    VidSetupDraw(gs, (const CommonAttr *) ((byte *) gs + (word) r->DR_si),
		 w, &ds);
    VidDevCondHidePtr(x1 < x2 ? x1 : x2, y1 < y2 ? y1 : y2,
		      x1 < x2 ? x2 : x1, y1 < y2 ? y2 : y1);
    if (y1 == y2 || x1 == x2) {
	VidFillClipped(w, x1 < x2 ? x1 : x2, y1 < y2 ? y1 : y2,
		       x1 < x2 ? x2 : x1, y1 < y2 ? y2 : y1, &ds);
	VidDevCondShowPtr();
	return;
    }
    dx = (sword) (x2 > x1 ? x2 - x1 : x1 - x2);
    dy = (sword) (y2 > y1 ? y2 - y1 : y1 - y2);
    sx = (sword) (x2 > x1 ? 1 : -1);
    sy = (sword) (y2 > y1 ? 1 : -1);
    if (dx >= dy) {			/* x major: horizontal runs */
	err = 2L * dy - dx;
	runStart = x1;
	for (;;) {
	    if (x1 == x2 || err > 0) {
		VidFillClipped(w, runStart < x1 ? runStart : x1, y1,
			       runStart < x1 ? x1 : runStart, y1, &ds);
		if (x1 == x2)
		    break;
		y1 = (sword) (y1 + sy);
		err -= 2L * dx;
		runStart = (sword) (x1 + sx);
	    }
	    err += 2L * dy;
	    x1 = (sword) (x1 + sx);
	}
    } else {				/* y major: vertical runs */
	err = 2L * dx - dy;
	runStart = y1;
	for (;;) {
	    if (y1 == y2 || err > 0) {
		VidFillClipped(w, x1, runStart < y1 ? runStart : y1,
			       x1, runStart < y1 ? y1 : runStart, &ds);
		if (y1 == y2)
		    break;
		x1 = (sword) (x1 + sx);
		err -= 2L * dy;
		runStart = (sword) (y1 + sy);
	    }
	    err += 2L * dx;
	    y1 = (sword) (y1 + sy);
	}
    }
    VidDevCondShowPtr();
}
