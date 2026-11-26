/*
 * truetypePath.c -- after truetypePath.asm: Routines for generating
 * graphics string of a character.
 */
#include "../Adapter/ttadapter.h"
#include "../Adapter/ttinit.h"
#include "../Adapter/ttwidths.h"
#include "../Adapter/ttchars.h"
#include "../Adapter/ttmetrics.h"
#include "../Adapter/ttpath.h"
#include <heap.h>
#include <geode.h>
#include <Internal/window.h>

/* the region path wrappers of truetypePath.asm: not reached, see above */
void _pascal GrRegionPathMovePen(Handle regionPath, sword x, sword y)
{
    (void) regionPath; (void) x; (void) y;
}

void _pascal GrRegionPathDrawLineTo(Handle regionPath, sword x, sword y)
{
    (void) regionPath; (void) x; (void) y;
}

void _pascal GrRegionPathDrawCurveTo(Handle regionPath, const Point *points)
{
    (void) regionPath; (void) points;
}
