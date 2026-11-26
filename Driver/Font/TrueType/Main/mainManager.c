/*
 * mainManager.c -- the TrueType font driver's entry (after
 * mainManager.asm: TrueTypeStrategy, DriverTable).  The functions it
 * calls are in the files of their asm files: truetypeInit.c,
 * truetypeWidths.c, truetypeChars.c, truetypeMetrics.c, truetypePath.c
 * (wrappers of the adapter's C functions); TrueType_GrMulWWFixed is in
 * ansic_runtime.c.  The FreeType engine and the GEOS adapter were C
 * already.
 *
 * The functions, registers as the kernel passes them
 * (Library/Kernel/Graphics/graphicsFontDriver.c):
 *   DR_INIT                the bitmap and the variable block, FreeType
 *   DR_EXIT                FreeType and the blocks
 *   DR_FONT_INIT_FONTS     DR_bx the font info block: the TTF fonts of
 *                          SP_FONT/TTF into it
 *   DR_FONT_GEN_WIDTHS     DR_bx the FontBuf block, DR_esDi the GState,
 *                          DR_bpPtr the matrix (the GState's or the
 *                          window's TMatrix), DR_dsSi the window's
 *                          W_TMatrix or NULL; returns DR_bx locked
 *   DR_FONT_GEN_CHAR       DR_dx the character, DR_esDi the locked
 *                          FontBuf, DR_bpPtr the GState; returns
 *                          DR_esDi (the block may have grown)
 *   DR_FONT_CHAR_METRICS   DR_dx the character, DR_cx the GCM_info,
 *                          DR_esDi the GState; returns DR_dx.DR_ax
 *   DR_FONT_GEN_PATH       DR_bx the GState handle, DR_cx the
 *                          FontGenPathFlags, DR_dx the character
 * The kernel holds the font info block locked during each call (the asm
 * driver used ds for it); the outline entries come from it
 * (FontDrFindOutlineData).
 *
 * DR_FONT_GEN_IN_REGION fails, and the region path wrappers of
 * truetypePath.asm do nothing: the asm kernel used it only to put text
 * into region paths; the C kernel fills text in paths from the outlines
 * (DR_FONT_GEN_PATH), so nothing calls it.
 *
 * Not yet: the escape functions (TrueTypeFontEscape: they fail).
 */
#include "../Adapter/ttadapter.h"
#include "truetypeConstant.h"
#include "../Adapter/ttinit.h"
#include "../Adapter/ttwidths.h"
#include "../Adapter/ttchars.h"
#include "../Adapter/ttmetrics.h"
#include "../Adapter/ttpath.h"
#include <heap.h>
#include <geode.h>
#include <Internal/window.h>


static Boolean TrueTypeStrategy(word function, DriverRegs *r);

FontDriverInfoStruct DriverTable = {
    { DRIVER_STRATEGY(TrueTypeStrategy), 0, DRIVER_TYPE_FONT },
    FM_TRUETYPE
};

/* the adapter's matrix is a TMatrix */
typedef char MatrixIsTMatrix[sizeof(TT_FontMatrix) == sizeof(TMatrix) &&
			     offsetof(TT_FontMatrix, FM_flags) ==
			     offsetof(TMatrix, TM_flags) ? 1 : -1];

WWFixedAsDWord TrueTypePointSize(const GState *gs)
{
    return ((WWFixedAsDWord) (word) gs->GS_fontAttr.FCA_pointsize.WBF_int
	    << 16) |
	   ((WWFixedAsDWord) gs->GS_fontAttr.FCA_pointsize.WBF_frac << 8);
}

static Boolean TrueTypeStrategy(word function, DriverRegs *r)
{
    switch (function) {
    case DR_INIT:
	return TrueTypeInit();
    case DR_EXIT:
	return TrueTypeExit();
    case DR_FONT_INIT_FONTS:
	return TrueTypeInitFonts(r);
    case DR_FONT_GEN_WIDTHS:
	return TrueTypeGenWidths(r);
    case DR_FONT_GEN_CHAR:
	return TrueTypeGenChar(r);
    case DR_FONT_CHAR_METRICS:
	return TrueTypeCharMetrics(r);
    case DR_FONT_GEN_PATH:
	return TrueTypeGenPath(r);
    default:
	return TRUE;
    }
}
