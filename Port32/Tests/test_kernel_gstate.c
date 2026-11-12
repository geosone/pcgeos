/*
 * test_kernel_gstate.c -- graphics states of the C kernel
 * (Library/Kernel/Graphics/graphicsState.c, graphicsGetState.c,
 * graphicsColor.c).
 *
 *   - the defaults of a new GState;
 *   - colors by index, RGB (exact, closest, color cube), grey and CMY,
 *     GrMapColorIndex/RGB, a window's palette;
 *   - mix mode and draw masks with their AttrOpts, inverse and custom
 *     masks, a mask lined up with the window;
 *   - line, area and text attributes, the miter limit;
 *   - GrSaveState/GrRestoreState nested, GrDestroyState with saved
 *     states and the window's W_curState.
 */
#include "kernelC.h"
#include <graphics.h>
#include <color.h>
#include <lmem.h>
#include <Internal/gstate.h>
#include <Internal/window.h>

#include <stdio.h>
#include <string.h>

static int fails;
#define CHECK(c) \
    do { if (!(c)) { fails++; printf("FAIL line %d: %s\n", __LINE__, #c); } \
    } while (0)

#define RGB(r, g, b)	((dword) (r) | ((dword) (g) << 8) | ((dword) (b) << 16))

static GState *Peek(GStateHandle h)
{
    GState *gs = MemLock(h);
    MemUnlock(h);			/* fixed for the test's purpose */
    return gs;
}

int main(void)
{
    GStateHandle g;
    DrawMask dm, ones;
    GState *gs;
    Color idx;
    LineAttr la;
    AreaAttr aa;
    MemHandle win;
    Window *w;
    ChunkHandle pal;
    byte *p;
    int i;
    static const DashPairArray dashes = { 0x0302, 0x0504 };

    kernelCurrentGeode = 0x2340;
    memset(ones, 0xff, sizeof(ones));

    /* defaults */
    g = GrCreateState(0);
    CHECK(g != 0);
    CHECK(GrGetMixMode(g) == MM_COPY);
    CHECK(GrGetLineWidth(g) == 0x10000UL);
    CHECK(GrGetLineEnd(g) == LE_BUTTCAP && GrGetLineJoin(g) == LJ_MITERED);
    CHECK(GrGetLineStyle(g) == LS_SOLID);
    CHECK(GrGetMiterLimit(g) >> 16 == 9 || GrGetMiterLimit(g) >> 16 == 10);
    CHECK(GrGetAreaColor(g) == RGB(0, 0, 0));
    CHECK(GrGetAreaColorMap(g) == CMT_DITHER &&
	  GrGetLineColorMap(g) == CMT_CLOSEST);
    CHECK(GrGetAreaMask(g, &dm) == SDM_100 && !memcmp(dm, ones, 8));
    gs = Peek(g);
    CHECK(gs->GS_header.LMBH_lmemType == LMEM_TYPE_GSTATE &&
	  gs->GS_header.LMBH_handle == g);
    CHECK(gs->GS_areaAttr.CA_flags == (AO_MASK_1 | AO_MASK_1_COPY));
    CHECK(gs->GS_TMatrix.TM_11.WWF_int == 1 && gs->GS_pixelHeightM1 == 15);

    /* colors */
    GrSetAreaColor(g, CF_INDEX, C_RED, 0, 0);
    CHECK(GrGetAreaColor(g) == RGB(0xaa, 0, 0));
    CHECK(gs->GS_areaAttr.CA_colorIndex == C_RED &&
	  (gs->GS_areaAttr.CA_flags & AO_USE_INDEX));
    GrSetLineColor(g, CF_RGB, 0x55, 0xff, 0x55);
    CHECK(gs->GS_lineAttr.CA_colorIndex == C_LIGHT_GREEN &&
	  (gs->GS_lineAttr.CA_flags & AO_USE_INDEX));
    GrSetLineColor(g, CF_RGB, 0xcc, 0, 0);	/* in the color cube */
    CHECK(gs->GS_lineAttr.CA_colorIndex == 40 + 4 * 36 &&
	  (gs->GS_lineAttr.CA_flags & AO_USE_INDEX));
    GrSetTextColor(g, CF_RGB, 0xa0, 0x08, 0x08);	/* closest: dark red */
    CHECK(gs->GS_textAttr.CA_colorIndex != 0 &&
	  !(gs->GS_textAttr.CA_flags & AO_USE_INDEX) &&
	  GrGetTextColor(g) == RGB(0xa0, 0x08, 0x08));
    CHECK(GrMapColorRGB(g, 0xa0, 0x08, 0x08, &idx) >> 24 == idx &&
	  idx == gs->GS_textAttr.CA_colorIndex);
    CHECK(GrMapColorRGB(g, 0xa0, 0x08, 0x08, NULL) ==
	  GrMapColorIndex(g, idx));
    CHECK(GrMapColorIndex(g, C_WHITE) ==
	  (RGB(0xff, 0xff, 0xff) | ((dword) C_WHITE << 24)));
    GrSetAreaColor(g, CF_GRAY, 0x88, 0, 0);
    CHECK(GrGetAreaColor(g) == RGB(0x88, 0x88, 0x88) &&
	  (gs->GS_areaAttr.CA_flags & AO_USE_INDEX));	/* a grey level */
    GrSetAreaColor(g, CF_CMY, 0xff, 0, 0xff);
    CHECK(GrGetAreaColor(g) == RGB(0, 0xff, 0));

    /* mix mode and masks */
    GrSetMixMode(g, MM_INVERT);
    CHECK(GrGetMixMode(g) == MM_INVERT &&
	  (gs->GS_areaAttr.CA_flags & ~AO_USE_INDEX) ==
	  (AO_MASK_1 | AO_MASK_1_INVERT));
    GrSetMixMode(g, MM_XOR);
    CHECK((gs->GS_areaAttr.CA_flags & ~AO_USE_INDEX) == AO_MASK_1);
    GrSetMixMode(g, MM_COPY);
    GrSetAreaMaskSys(g, SDM_50);
    CHECK(GrGetAreaMask(g, &dm) == SDM_50 && dm[0] != dm[1] &&
	  !(gs->GS_areaAttr.CA_flags & AO_MASK_1));
    GrSetAreaMaskSys(g, SDM_0 | SDM_INVERSE);
    CHECK(GrGetAreaMask(g, &dm) == (SDM_0 | SDM_INVERSE) &&
	  !memcmp(dm, ones, 8) &&
	  (gs->GS_areaAttr.CA_flags & (AO_MASK_1 | AO_MASK_1_COPY)) ==
	  (AO_MASK_1 | AO_MASK_1_COPY));
    GrSetLineMaskSys(g, SDM_HORIZONTAL | SDM_INVERSE);
    CHECK(GrGetLineMask(g, &dm) == (SDM_HORIZONTAL | SDM_INVERSE) &&
	  dm[0] == 0x00 && dm[1] == 0xff);
    memset(dm, 0, 8);
    dm[0] = 0x80;
    gs->GS_xShift = 2;			/* the window at 2, 1 */
    gs->GS_yShift = 1;
    GrSetTextMaskCustom(g, (const DrawMask *) &dm);
    CHECK(GrGetTextMask(g, &dm) == SDM_CUSTOM && dm[1] == 0x20 &&
	  dm[0] == 0 && gs->GS_optDrawMask != 0);
    gs->GS_xShift = gs->GS_yShift = 0;
    GrSetTextMaskSys(g, SDM_100);
    CHECK(gs->GS_optDrawMask == 0);

    /* lines */
    GrSetLineWidth(g, 0x28000UL);
    CHECK(GrGetLineWidth(g) == 0x28000UL);
    GrSetLineStyle(g, LS_DASHDOT, 1, NULL, 0);
    CHECK(GrGetLineStyle(g) == LS_DASHDOT && gs->GS_numOfDashPairs == 2 &&
	  ((byte *) gs->GS_dashPairArray)[2] == 1 &&
	  gs->GS_dashSkipDistance == 1);
    GrSetLineStyle(g, LS_CUSTOM, 0, &dashes, 2);
    CHECK(gs->GS_numOfDashPairs == 2 && gs->GS_dashPairArray[1] == 0x0504);
    GrSetMiterLimit(g, 0x10000UL);
    CHECK(gs->GS_inverseMiterLimit == 0xffff &&
	  GrGetMiterLimit(g) == 0x10000UL);
    GrSetMiterLimit(g, 0x40000UL);
    CHECK(gs->GS_inverseMiterLimit == 0x4000 &&
	  GrGetMiterLimit(g) == 0x40000UL);
    memset(&la, 0, sizeof(la));
    la.LA_colorFlag = CF_INDEX;
    la.LA_color.RGB_red = C_BLUE;
    la.LA_mask = SDM_25;
    la.LA_mapMode = CMT_DITHER;
    la.LA_end = LE_ROUNDCAP;
    la.LA_join = LJ_BEVELED;
    la.LA_style = LS_DOTTED;
    la.LA_width.WWF_int = 3;
    GrSetLineAttr(g, &la);
    CHECK(GrGetLineColor(g) == RGB(0, 0, 0xaa) && GrGetLineMask(g, NULL) ==
	  SDM_25 && GrGetLineColorMap(g) == CMT_DITHER &&
	  GrGetLineEnd(g) == LE_ROUNDCAP && GrGetLineJoin(g) == LJ_BEVELED &&
	  GrGetLineStyle(g) == LS_DOTTED && GrGetLineWidth(g) == 0x30000UL);
    memset(&aa, 0, sizeof(aa));
    aa.AA_colorFlag = CF_RGB;
    aa.AA_color.RGB_red = 0xff;
    aa.AA_mask = SDM_100;
    aa.AA_mapMode = CMT_CLOSEST;
    GrSetAreaAttr(g, &aa);
    CHECK(GrGetAreaColor(g) == RGB(0xff, 0, 0) &&
	  GrGetAreaColorMap(g) == CMT_CLOSEST);

    /* text */
    GrSetTextStyle(g, TS_BOLD | TS_UNDERLINE, 0);
    GrSetTextStyle(g, 0, TS_UNDERLINE);
    CHECK(GrGetTextStyle(g) == TS_BOLD);
    GrSetTextMode(g, TM_DRAW_OPTIONAL_HYPHENS | TM_PAD_SPACES, 0);
    CHECK(GrGetTextMode(g) == TM_DRAW_OPTIONAL_HYPHENS &&
	  gs->GS_hyphenOpcode == GO_SPECIAL_CASE);
    GrSetTextSpacePad(g, 0x28000UL);
    CHECK(GrGetTextSpacePad(g) == 0x28000UL &&
	  (GrGetTextMode(g) & TM_PAD_SPACES) &&
	  gs->GS_optSpacePad == GO_SPECIAL_CASE);
    GrSetTextDrawOffset(g, 3);
    CHECK(GrGetTextDrawOffset(g) == 3 &&
	  gs->GS_drawOffsetOpcode == GO_SPECIAL_CASE);

    /* save and restore, nested */
    GrSetAreaColor(g, CF_INDEX, C_GREEN, 0, 0);
    GrSaveState(g);
    GrSetAreaColor(g, CF_INDEX, C_YELLOW, 0, 0);
    GrSetMixMode(g, MM_XOR);
    GrSaveState(g);
    GrSetLineWidth(g, 0x50000UL);
    GrRestoreState(g);
    gs = Peek(g);
    CHECK(GrGetLineWidth(g) == 0x30000UL && GrGetMixMode(g) == MM_XOR &&
	  GrGetAreaColor(g) == RGB(0xff, 0xff, 0x55));
    GrRestoreState(g);
    gs = Peek(g);
    CHECK(GrGetMixMode(g) == MM_COPY &&
	  GrGetAreaColor(g) == RGB(0, 0xaa, 0) &&
	  gs->GS_saveStateLink == 0 && gs->GS_header.LMBH_handle == g);
    GrRestoreState(g);			/* nothing saved: nothing */
    GrSaveState(g);
    GrSaveState(g);
    GrDestroyState(g);			/* with the saved ones */

    /* a window with a palette of its own */
    win = MemAllocLMem(LMEM_TYPE_WINDOW, sizeof(Window));
    w = MemLock(win);
    pal = LMemAlloc(win, 256 * 3);
    w = MemDeref(win);
    w->W_palette = pal;
    p = LMemDeref(ConstructOptr(win, pal));
    for (i = 0; i < 256 * 3; i++)
	p[i] = (byte) (i / 3);
    MemUnlock(win);
    g = GrCreateState(win);
    GrSetAreaColor(g, CF_INDEX, 7, 0, 0);
    CHECK(GrGetAreaColor(g) == RGB(7, 7, 7));
    GrSetAreaColor(g, CF_RGB, 9, 9, 10);
    gs = Peek(g);
    CHECK(gs->GS_areaAttr.CA_colorIndex == 9 &&
	  !(gs->GS_areaAttr.CA_flags & AO_USE_INDEX));
    w = MemLock(win);
    w->W_curState = g;
    w->W_grFlags = 0xff;
    MemUnlock(win);
    GrSaveState(g);
    GrRestoreState(g);
    w = MemLock(win);
    CHECK(w->W_grFlags == (WinGrFlags) ~(WGF_MASK_VALID | WGF_XFORM_VALID));
    MemUnlock(win);
    GrDestroyState(g);
    w = MemLock(win);
    CHECK(w->W_curState == 0 && w->W_grFlags == 0);
    MemUnlock(win);
    MemFree(win);

    printf("%s (%d failed)\n", fails ? "FAIL" : "ok", fails);
    return fails != 0;
}
