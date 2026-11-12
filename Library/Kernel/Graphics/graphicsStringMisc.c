/*
 * graphicsStringMisc.c -- the graphics string routines of
 * graphicsStringMisc.asm that a GState without a graphics string takes
 * as well (GrComment).
 *
 * A comment is stored only in a graphics string (GS_gstring); drawing to
 * a window or a bitmap ignores it.  Not yet: graphics strings, so a
 * comment is always ignored.
 */
#include "graphicsInt.h"

void GrComment(GStateHandle gstate, const void *data, word size)
{
    (void) gstate; (void) data; (void) size;
}
