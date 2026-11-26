/*
 * graphicsFontDriver.c -- the kernel's side of the font drivers (after
 * graphicsFontDriver.asm: FontDrGetMaxBufSize, FontDrCharDataSize,
 * FontDrFindOutlineData, GrCallFontDriver; LoadFontDriver of
 * bootInitfile.asm).
 *
 * A font driver is a geode of type DRIVER_TYPE_FONT whose
 * FontDriverInfoStruct names its maker.  The kernel loads the drivers of
 * "[system] font" (a list, from SP_FONT_DRIVERS), enters each in the
 * font info block and has it add its fonts (DR_FONT_INIT_FONTS).  It
 * calls a driver with the info block locked, as the asm did.
 *
 * The functions (fontDr.h), registers as DriverRegs:
 *   DR_FONT_INIT_FONTS   DR_bx the font info block
 *   DR_FONT_GEN_WIDTHS   DR_bx the FontBuf block to fill (discarded),
 *                        DR_esDi the GState, DR_bpPtr the matrix
 *                        (TMatrix), DR_dsSi the window's W_TMatrix
 *                        (NULL without a window); returns DR_bx, locked
 *   DR_FONT_GEN_CHAR     DR_dx the character, DR_esDi the locked
 *                        FontBuf (returned, it may move), DR_bpPtr the
 *                        GState
 *   DR_FONT_CHAR_METRICS DR_dx the character, DR_cx GCM_info, DR_esDi
 *                        the GState; returns DR_dx.DR_ax
 */
#include "graphicsInt.h"
#include <initfile.h>
#include <geode.h>
#include <ctype.h>

#define DEFAULT_FONT_DRIVERS	"truetype.geo"

word FontDrGetMaxBufSize(const FontBuf *fb)
{
    return (word) ((fb->FB_flags & FBF_IS_GREY) ? MAX_FONT_SIZE_GREY :
		   MAX_FONT_SIZE);
}

word FontDrCharDataSize(const FontBuf *fb, word dataOffset)
{
    const byte *p = (const byte *) fb + dataOffset;
    word n;

    if (fb->FB_flags & FBF_IS_REGION)
	return ((const RegionCharData *) p)->RCD_size;
    if (fb->FB_flags & FBF_IS_GREY) {
	n = (word) (((p[0] + 1) >> 1) * p[1]);
	if (fb->FB_flags & FBF_GREY_PHASES)
	    n = (word) (n * GREY_PHASES);
	return (word) (n + SIZE_CHAR_HEADER);
    }
    return (word) (((p[0] + 7) >> 3) * p[1] + SIZE_CHAR_HEADER);
}

OutlineEntry *FontDrFindOutlineData(FontInfo *fi, TextStyle style,
				    OutlineDataFlag part, TextStyle *toDo)
{
    word n, i;
    OutlineDataEntry *ode = GrOutlineTab(fi, &n), *best = ode;
    TextStyle bestSub = 0, left;

    left = style;
    for (i = 0; i < n; i++) {
	TextStyle s = ode[i].ODE_style;
	if (s == style) {
	    best = &ode[i];
	    left = 0;
	    break;
	}
	if (!((style & s) ^ s) && (TextStyle) (style & s) >= bestSub) {
	    best = &ode[i];
	    bestSub = (TextStyle) (style & s);
	}
    }
    if (i == n)
	left = (TextStyle) (style ^ bestSub);
    if (toDo)
	*toDo = left;
    return part == ODF_HEADER ? &best->ODE_header :
	   part == ODF_PART1 ? &best->ODE_first : &best->ODE_second;
}

/* GrCallFontDriver: the driver of the GState's font */
Boolean GrCallFontDriver(GStateHandle gstate, word function, DriverRegs *r)
{
    FontBuf *fb = FontDrLockFont(gstate);
    FontMaker maker;

    if (!fb)
	return TRUE;
    maker = fb->FB_maker;
    FontDrUnlockFont(gstate);
    return GrCallFontDriverID(maker, function, r);
}

/* LoadFontDriver: the drivers of [system] font, each name in turn */
void GrLoadFontDrivers(void)
{
    char list[256], *p, *name;
    word n;
    GeodeLoadError e;
    GeodeHandle gh;

    if (InitFileReadStringBuffer("system", "font", list, sizeof(list), &n))
	strcpy(list, DEFAULT_FONT_DRIVERS);
    FilePushDir();
    FileSetStandardPath(SP_FONT_DRIVERS);
    for (p = list; *p;) {
	while (*p && isspace((byte) *p))
	    p++;
	if (!*p)
	    break;
	name = p;
	while (*p && !isspace((byte) *p))
	    p++;
	if (*p)
	    *p++ = 0;
	gh = GeodeUseDriver(name, FONT_PROTO_MAJOR, FONT_PROTO_MINOR, &e);
	if (gh)
	    GrInitFontDriver(gh);
    }
    FilePopDir();
}

FontInfo *FontDrFindFontInfo(void *infoBlock, FontID id)
{
    FontsAvailEntry *fa;

    (void) infoBlock;
    fa = GrFontAvailEntry(id);
    return fa ? GrFontChunk(fa->FAE_infoHandle, NULL) : NULL;
}
