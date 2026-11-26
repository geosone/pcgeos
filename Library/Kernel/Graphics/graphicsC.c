/*
 * graphicsC.c -- after graphicsC.asm: GrSetCustomAreaPattern,
 * GrSetCustomTextPattern, GrSetLineMaskSys, GrSetLineMaskCustom,
 * GrSetAreaMaskSys, GrSetAreaMaskCustom, GrSetTextMaskSys,
 * GrSetTextMaskCustom, GrTextWidthWWFixed.
 */
#include "graphicsInt.h"
#include <gstring.h>
#include <stdlib.h>
#include <char.h>

void GrSetCustomAreaPattern(GStateHandle gstate, GraphicPattern pattern,
			    const void *patternData, word patternSize)
{
    GrSetPatternLow(gstate, FALSE, pattern, patternData, patternSize);
}

/* the C binding has no size: that of the HatchPattern or Bitmap */
void GrSetCustomTextPattern(GStateHandle gstate, GraphicPattern pattern,
			    const void *patternData)
{
    word size = 0;

    if (patternData)
	size = pattern.HP_type == PT_CUSTOM_HATCH ?
	    GrHatchSize(patternData) : GrBitmapStringSize(patternData);
    GrSetPatternLow(gstate, TRUE, pattern, patternData, size);
}

static void SetMask(GStateHandle gstate, word attr, SysDrawMask sdm,
		    const DrawMask *custom)
{
    GState *gs = MemLock(gstate);

    GrSetMaskIn(gs, (CommonAttr *) ((byte *) gs + attr), sdm, custom);
    if (attr == offsetof(GState, GS_textAttr))
	GrUpdateOpts(gs);
    MemUnlock(gstate);
}

void GrSetLineMaskSys(GStateHandle gstate, word sysDM)
{
    (void) GrGSRecord(gstate, GR_SET_LINE_MASK, "b", sysDM);
    SetMask(gstate, offsetof(GState, GS_lineAttr), (SysDrawMask) sysDM, NULL);
}

void GrSetLineMaskCustom(GStateHandle gstate, const DrawMask *dm)
{
    (void) GrGSRecord(gstate, GR_SET_CUSTOM_LINE_MASK, "r", dm, 8);
    SetMask(gstate, offsetof(GState, GS_lineAttr), SDM_CUSTOM, dm);
}

void GrSetAreaMaskSys(GStateHandle gstate, word sysDM)
{
    (void) GrGSRecord(gstate, GR_SET_AREA_MASK, "b", sysDM);
    SetMask(gstate, offsetof(GState, GS_areaAttr), (SysDrawMask) sysDM, NULL);
}

void GrSetAreaMaskCustom(GStateHandle gstate, const DrawMask *dm)
{
    (void) GrGSRecord(gstate, GR_SET_CUSTOM_AREA_MASK, "r", dm, 8);
    SetMask(gstate, offsetof(GState, GS_areaAttr), SDM_CUSTOM, dm);
}

void GrSetTextMaskSys(GStateHandle gstate, SysDrawMask sysDM)
{
    (void) GrGSRecord(gstate, GR_SET_TEXT_MASK, "b", sysDM);
    SetMask(gstate, offsetof(GState, GS_textAttr), sysDM, NULL);
}

void GrSetTextMaskCustom(GStateHandle gstate, const DrawMask *dm)
{
    (void) GrGSRecord(gstate, GR_SET_CUSTOM_TEXT_MASK, "r", dm, 8);
    SetMask(gstate, offsetof(GState, GS_textAttr), SDM_CUSTOM, dm);
}

dword GrTextWidthWWFixed(GStateHandle gstate, const char *str, word size)
{
    return (dword) (GrTextWidthStr(gstate, str, size) << 8);
}
