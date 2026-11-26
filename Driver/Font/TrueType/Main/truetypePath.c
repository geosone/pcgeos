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
#include "truetypeConstant.h"

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

Boolean TrueTypeGenPath(DriverRegs *r)
{
    GStateHandle gstate = (GStateHandle) r->DR_bx;
    GState *gs = MemLock(gstate);
    FontInfo *fi = FontDrFindFontInfo(NULL, gs->GS_fontAttr.FCA_fontID);
    TextStyle style = gs->GS_fontAttr.FCA_textStyle, toDo;
    const OutlineEntry *header, *first;

    MemUnlock(gstate);
    if (!fi)
	return TRUE;
    header = FontDrFindOutlineData(fi, style, ODF_HEADER, NULL);
    first = FontDrFindOutlineData(fi, style, ODF_PART1, &toDo);
    TrueType_Gen_Path(gstate, (FontGenPathFlags) r->DR_cx, (word) r->DR_dx,
		      fi, header, first, toDo, variableHandle);
    return FALSE;
}
