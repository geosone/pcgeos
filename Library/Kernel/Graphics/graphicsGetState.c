/*
 * graphicsGetState.c -- reading the attributes of a GState (after
 * graphicsGetState.asm, with the C conventions of graphicsC.asm).
 *
 * Not yet: GrGetCurPos and the transformation (with graphicsTransform),
 * GrGetInfo, GrGetFont and the other font attributes (with the fonts).
 */
#include "graphicsInt.h"

#define ATTR(gs, off)	((const CommonAttr *) ((const byte *) (gs) + (off)))

MixMode GrGetMixMode(GStateHandle gstate)
{
    GState *gs = MemLock(gstate);
    MixMode m = gs->GS_mixMode;

    MemUnlock(gstate);
    return m;
}

/* red, green and blue in the low three bytes (CReturnRGBCommon) */
static RGBColorAsDWord GetColor(GStateHandle gstate, word attr)
{
    GState *gs = MemLock(gstate);
    const RGBValue *c = &ATTR(gs, attr)->CA_colorRGB;
    RGBColorAsDWord v = ((dword) c->RGB_blue << 16) |
			((dword) c->RGB_green << 8) | c->RGB_red;

    MemUnlock(gstate);
    return v;
}

RGBColorAsDWord GrGetLineColor(GStateHandle gstate)
{
    return GetColor(gstate, offsetof(GState, GS_lineAttr));
}

RGBColorAsDWord GrGetAreaColor(GStateHandle gstate)
{
    return GetColor(gstate, offsetof(GState, GS_areaAttr));
}

RGBColorAsDWord GrGetTextColor(GStateHandle gstate)
{
    return GetColor(gstate, offsetof(GState, GS_textAttr));
}

/* GetMask: the mask (as lined up with the window) into dm */
static word GetMask(GStateHandle gstate, word attr, DrawMask *dm)
{
    GState *gs = MemLock(gstate);
    const CommonAttr *ca = ATTR(gs, attr);
    word type = ca->CA_maskType;

    if (dm)
	memcpy(*dm, ca->CA_mask, sizeof(DrawMask));
    MemUnlock(gstate);
    return type;
}

word GrGetLineMask(GStateHandle gstate, DrawMask *dm)
{
    return GetMask(gstate, offsetof(GState, GS_lineAttr), dm);
}

word GrGetAreaMask(GStateHandle gstate, DrawMask *dm)
{
    return GetMask(gstate, offsetof(GState, GS_areaAttr), dm);
}

word GrGetTextMask(GStateHandle gstate, DrawMask *dm)
{
    return GetMask(gstate, offsetof(GState, GS_textAttr), dm);
}

static word GetMap(GStateHandle gstate, word attr)
{
    GState *gs = MemLock(gstate);
    word map = ATTR(gs, attr)->CA_mapMode;

    MemUnlock(gstate);
    return map;
}

word GrGetLineColorMap(GStateHandle gstate)
{
    return GetMap(gstate, offsetof(GState, GS_lineAttr));
}

word GrGetAreaColorMap(GStateHandle gstate)
{
    return GetMap(gstate, offsetof(GState, GS_areaAttr));
}

word GrGetTextColorMap(GStateHandle gstate)
{
    return GetMap(gstate, offsetof(GState, GS_textAttr));
}

WWFixedAsDWord GrGetTextSpacePad(GStateHandle gstate)
{
    GState *gs = MemLock(gstate);
    WWFixedAsDWord v = ((dword) (word) gs->GS_textSpacePad.WBF_int << 16) |
		       ((dword) gs->GS_textSpacePad.WBF_frac << 8);

    MemUnlock(gstate);
    return v;
}

word GrGetTextStyle(GStateHandle gstate)
{
    GState *gs = MemLock(gstate);
    word v = gs->GS_fontAttr.FCA_textStyle;

    MemUnlock(gstate);
    return v;
}

word GrGetTextDrawOffset(GStateHandle gstate)
{
    GState *gs = MemLock(gstate);
    word v = gs->GS_textDrawOffset;

    MemUnlock(gstate);
    return v;
}

word GrGetTextMode(GStateHandle gstate)
{
    GState *gs = MemLock(gstate);
    word v = gs->GS_textMode;

    MemUnlock(gstate);
    return v;
}

WWFixedAsDWord GrGetLineWidth(GStateHandle gstate)
{
    GState *gs = MemLock(gstate);
    WWFixedAsDWord v = ((dword) (word) gs->GS_lineWidth.WWF_int << 16) |
		       gs->GS_lineWidth.WWF_frac;

    MemUnlock(gstate);
    return v;
}

LineEnd GrGetLineEnd(GStateHandle gstate)
{
    GState *gs = MemLock(gstate);
    LineEnd v = gs->GS_lineEnd;

    MemUnlock(gstate);
    return v;
}

LineJoin GrGetLineJoin(GStateHandle gstate)
{
    GState *gs = MemLock(gstate);
    LineJoin v = gs->GS_lineJoin;

    MemUnlock(gstate);
    return v;
}

LineStyle GrGetLineStyle(GStateHandle gstate)
{
    GState *gs = MemLock(gstate);
    LineStyle v = gs->GS_lineStyle;

    MemUnlock(gstate);
    return v;
}

/* 1 / the inverse kept; 0xffff stands for 1 */
WWFixedAsDWord GrGetMiterLimit(GStateHandle gstate)
{
    GState *gs = MemLock(gstate);
    word inv = gs->GS_inverseMiterLimit;

    MemUnlock(gstate);
    return inv == 0xffff ? 0x10000UL : GrReciprocal32(inv);
}
