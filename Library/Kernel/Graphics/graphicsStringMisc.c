/*
 * graphicsStringMisc.c -- the graphics string elements that only a
 * graphics string keeps (after graphicsStringMisc.asm: GrComment,
 * GrEscape, GrLabel, GrNewPage, GrNullOp, GrSetGStringBounds).
 *
 * Each stores its element if the GState has a graphics string
 * (GS_gstring); drawing to a window or a bitmap ignores them.
 */
#include "graphicsInt.h"
#include <gstring.h>

void GrComment(GStateHandle gstate, const void *data, word size)
{
    (void) GrGSRecord(gstate, GR_COMMENT, "wr", size, data, size);
}

void GrEscape(GStateHandle gstate, word code, const void *data, word size)
{
    (void) GrGSRecord(gstate, GR_ESCAPE, "wwr", code, size, data, size);
}

void GrLabel(GStateHandle gstate, word label)
{
    (void) GrGSRecord(gstate, GR_LABEL, "w", label);
}

void GrNewPage(GStateHandle gstate, PageEndCommand pageEndCommand)
{
    GState *gs = MemLock(gstate);
    Boolean path = (Boolean) ((gs->GS_pathFlags & PF_DEFINING_PATH) != 0);

    MemUnlock(gstate);
    if (!path)
	(void) GrGSRecord(gstate, GR_NEW_PAGE, "b", pageEndCommand);
}

void GrNullOp(GStateHandle gstate)
{
    (void) GrGSRecord(gstate, GR_NULL_OP, "");
}

void GrSetGStringBounds(GStateHandle gstate, sword left, sword top,
			sword right, sword bottom)
{
    (void) GrGSRecord(gstate, GR_SET_GSTRING_BOUNDS, "wwww", left, top,
		      right, bottom);
}
