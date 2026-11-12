/*
 * mainManager.c -- the TrueType font driver's own part (after
 * mainManager.asm: TrueTypeStrategy, DriverTable; truetypeInit.asm:
 * TrueTypeInit, TrueTypeExit, TrueTypeInitFonts; truetypeWidths.asm,
 * truetypeChars.asm, truetypeMetrics.asm, truetypePath.asm: the
 * wrappers of the adapter's C functions; ansic_runtime.asm:
 * TrueType_GrMulWWFixed; truetypeVariable.def: the driver's variables).
 * The FreeType engine and the GEOS adapter were C already.
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
#include "../Adapter/ttinit.h"
#include "../Adapter/ttwidths.h"
#include "../Adapter/ttchars.h"
#include "../Adapter/ttmetrics.h"
#include "../Adapter/ttpath.h"
#include <heap.h>
#include <geode.h>
#include <Internal/window.h>

/* ttinit.c */
TT_Error _pascal Init_FreeType(void);
void _pascal Exit_FreeType(MemHandle varBlock);

#define TRUETYPE_BLOCK_SIZE	1024	/* truetypeConstant.def */

static Boolean TrueTypeStrategy(word function, DriverRegs *r);

FontDriverInfoStruct DriverTable = {
    { DRIVER_STRATEGY(TrueTypeStrategy), 0, DRIVER_TYPE_FONT },
    FM_TRUETYPE
};

TEngine_Instance engineInstance;
static MemHandle bitmapHandle;		/* the picture of a character */
static MemHandle variableHandle;	/* TrueTypeVars */

/* the adapter's matrix is a TMatrix */
typedef char MatrixIsTMatrix[sizeof(TT_FontMatrix) == sizeof(TMatrix) &&
			     offsetof(TT_FontMatrix, FM_flags) ==
			     offsetof(TMatrix, TM_flags) ? 1 : -1];

WWFixedAsDWord _pascal TrueType_GrMulWWFixed(WWFixedAsDWord i,
					     WWFixedAsDWord j)
{
    /* the middle 32 bits of the product (TrueType_GrRegMul32ToDDF) */
    return (WWFixedAsDWord) (sdword) (((int64_t) (sdword) i *
				       (sdword) j) >> 16);
}

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

static WWFixedAsDWord PointSize(const GState *gs)
{
    return ((WWFixedAsDWord) (word) gs->GS_fontAttr.FCA_pointsize.WBF_int
	    << 16) |
	   ((WWFixedAsDWord) gs->GS_fontAttr.FCA_pointsize.WBF_frac << 8);
}

/* TrueTypeInit */
static Boolean Init(void)
{
    GeodeHandle gh = GeodeGetCodeProcessHandle();

    bitmapHandle = MemAllocSetOwner(gh, TRUETYPE_BLOCK_SIZE,
				    HF_DISCARDABLE | HF_SWAPABLE |
				    HF_SHARABLE | HF_DISCARDED, HAF_NO_ERR);
    variableHandle = MemAllocSetOwner(gh, TRUETYPE_BLOCK_SIZE,
				      HF_SWAPABLE | HF_SHARABLE |
				      HF_DISCARDED, HAF_NO_ERR);
    return (Boolean) (Init_FreeType() != TT_Err_Ok);
}

/* TrueTypeExit */
static Boolean Exit(void)
{
    Exit_FreeType(variableHandle);
    MemFree(bitmapHandle);
    MemFree(variableHandle);
    bitmapHandle = variableHandle = 0;
    return FALSE;
}

/* TrueTypeGenWidths */
static Boolean GenWidths(DriverRegs *r)
{
    GState *gs = r->DR_esDi;
    FontInfo *fi = FontDrFindFontInfo(NULL, gs->GS_fontAttr.FCA_fontID);
    TextStyle toDo;
    const OutlineEntry *header, *first;
    MemHandle h;

    if (!fi)
	return TRUE;
    header = FontDrFindOutlineData(fi, gs->GS_fontAttr.FCA_textStyle,
				   ODF_HEADER, NULL);
    first = FontDrFindOutlineData(fi, gs->GS_fontAttr.FCA_textStyle,
				  ODF_PART1, &toDo);
    h = TrueType_Gen_Widths((MemHandle) r->DR_bx,
			    (TT_FontMatrix *) r->DR_bpPtr, PointSize(gs),
			    gs->GS_fontAttr.FCA_width,
			    gs->GS_fontAttr.FCA_weight, fi, header, first,
			    toDo, (TT_FontMatrix *) r->DR_dsSi,
			    (byte) (gs->GS_fontFlags & FBF_IS_GREY),
			    variableHandle);
    r->DR_bx = h;
    return (Boolean) !h;
}

/* TrueTypeGenChar */
static Boolean GenChar(DriverRegs *r)
{
    GState *gs = r->DR_bpPtr;
    FontBuf *fb = r->DR_esDi;
    MemHandle fh = MemPtrToHandle(fb);
    FontInfo *fi = FontDrFindFontInfo(NULL, gs->GS_fontAttr.FCA_fontID);
    TextStyle toDo;
    const OutlineEntry *header;

    if (!fi || !fh)
	return TRUE;
    header = FontDrFindOutlineData(fi, gs->GS_fontAttr.FCA_textStyle,
				   ODF_HEADER, &toDo);
    TrueType_Gen_Chars((word) r->DR_dx, fb, PointSize(gs),
		       gs->GS_fontAttr.FCA_width, gs->GS_fontAttr.FCA_weight,
		       fi, header, toDo, bitmapHandle, variableHandle);
    r->DR_esDi = MemDeref(fh);
    return FALSE;
}

/* TrueTypeCharMetrics */
static Boolean CharMetrics(DriverRegs *r)
{
    GState *gs = r->DR_esDi;
    FontInfo *fi = FontDrFindFontInfo(NULL, gs->GS_fontAttr.FCA_fontID);
    TextStyle toDo;
    const OutlineEntry *header;
    WWFixedAsDWord v;
    word i, f;

    if (!fi)
	return TRUE;
    header = FontDrFindOutlineData(fi, gs->GS_fontAttr.FCA_textStyle,
				   ODF_HEADER, &toDo);
    v = TrueType_Char_Metrics((word) r->DR_dx, (GCM_info) r->DR_cx, fi,
			      header, toDo, PointSize(gs),
			      gs->GS_fontAttr.FCA_width,
			      gs->GS_fontAttr.FCA_weight, variableHandle);
    i = (word) (v >> 16);
    f = (word) v;
    if (r->DR_cx & 1) {				/* GCMI_ROUNDED: rndwwf */
	if (f > 0x8000 || (f == 0x8000 && !(i & 0x8000)))
	    i++;
    } else if ((f & 0xff) > 0x80 ||		/* rndwwbf */
	       ((f & 0xff) == 0x80 && !(i & 0x8000))) {
	if ((word) (f + 0x100) < f)
	    i++;
	f = (word) (f + 0x100);
    }
    r->DR_dx = i;
    r->DR_ax = f;
    return FALSE;
}

/* TrueTypeGenPath */
static Boolean GenPath(DriverRegs *r)
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

static Boolean TrueTypeStrategy(word function, DriverRegs *r)
{
    switch (function) {
    case DR_INIT:
	return Init();
    case DR_EXIT:
	return Exit();
    case DR_FONT_INIT_FONTS:
	TrueType_InitFonts((MemHandle) r->DR_bx, variableHandle);
	return FALSE;
    case DR_FONT_GEN_WIDTHS:
	return GenWidths(r);
    case DR_FONT_GEN_CHAR:
	return GenChar(r);
    case DR_FONT_CHAR_METRICS:
	return CharMetrics(r);
    case DR_FONT_GEN_PATH:
	return GenPath(r);
    default:
	return TRUE;
    }
}
