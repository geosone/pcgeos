/*
 * graphicsPathLow.c -- after graphicsPathLow.asm: GrGetTransform.
 */
#include "graphicsInt.h"

void GrGetTransform(GStateHandle gstate, TransMatrix *tm)
{
    GState *gs = MemLock(gstate);

    GrMatToTransMatrix(&gs->GS_TMatrix, tm);
    MemUnlock(gstate);
}
