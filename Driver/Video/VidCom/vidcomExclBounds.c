/*
 * vidcomExclBounds.c -- after vidcomExclBounds.asm: These functions
 * calculate the bounds of the operation and stuff the result in some
 * variables for later use.
 */
#include "vidcom.h"
#include <ghal.h>
#include <thread.h>
#include <sem.h>

/* ExclRectBounds: the bounds of an operation not done */
void ExclRectBounds(sword l, sword t, sword r, sword b,
			   const Window *w)
{
    if (l > r) { sword x = l; l = r; r = x; }
    if (t > b) { sword y = t; t = b; b = y; }
    if (w) {
	if (l < w->W_maskRect.R_left)
	    l = w->W_maskRect.R_left;
	if (t < w->W_maskRect.R_top)
	    t = w->W_maskRect.R_top;
	if (r > w->W_maskRect.R_right)
	    r = w->W_maskRect.R_right;
	if (b > w->W_maskRect.R_bottom)
	    b = w->W_maskRect.R_bottom;
    }
    if (l > r || t > b)
	return;
    if (l < exclBound.R_left)
	exclBound.R_left = l;
    if (t < exclBound.R_top)
	exclBound.R_top = t;
    if (r > exclBound.R_right)
	exclBound.R_right = r;
    if (b > exclBound.R_bottom)
	exclBound.R_bottom = b;
}
