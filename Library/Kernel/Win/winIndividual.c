/*
 * winIndividual.c -- routines of the window system that work on one
 * window (after winIndividual.asm).  So far WinValClipLine, which the
 * video drivers call; the rest comes with the window system.
 */
#include "kernelC.h"
#include <Internal/window.h>

#define CLIP_TOP_START	((sword) 0xc000)
#define CLIP_PAST_END	0x3fff

/*
 * WinValClipLine: make the clip line the band of W_maskReg that holds
 * line y.  W_clipRect top/bottom are the lines of the band, left/right
 * the first on and the last off point (EOREGREC when the band is
 * empty), W_clipPtr the offset of its pairs in the chunk;
 * WGF_CLIP_SIMPLE when it has one pair, WGF_CLIP_NULL when none.
 *
 * The asm searched from the current band, back or on; the region is
 * walked from its start here, which gives the same band.
 */
void WinValClipLine(Window *w, sword y)
{
    sword *reg = WinRegionOf(w, w->W_maskReg), *p = reg, *line;
    sword top = CLIP_TOP_START, bottom, right;

    w->W_grFlags &= (WinGrFlags) ~(WGF_CLIP_SIMPLE | WGF_CLIP_NULL);
    for (;;) {
	if ((word) *p == EOREGREC) {	/* past the end: empty to the end */
	    bottom = CLIP_PAST_END;
	    line = p;
	    break;
	}
	if (*p >= y) {
	    bottom = *p;
	    line = p + 1;
	    break;
	}
	top = (sword) (*p + 1);
	for (p++; (word) *p != EOREGREC; p++)
	    ;
	p++;
    }
    w->W_clipRect.R_top = top;
    w->W_clipRect.R_bottom = bottom;
    w->W_clipPtr = (word) ((byte *) line - (byte *) reg);
    if ((word) line[0] == EOREGREC) {
	w->W_clipRect.R_left = w->W_clipRect.R_right = (sword) EOREGREC;
	w->W_grFlags |= WGF_CLIP_NULL;
	return;
    }
    w->W_clipRect.R_left = line[0];
    right = line[1];
    if ((word) line[2] == EOREGREC)
	w->W_grFlags |= WGF_CLIP_SIMPLE;
    else
	for (p = line + 2; (word) *p != EOREGREC; p += 2)
	    right = p[1];
    w->W_clipRect.R_right = right;
}
