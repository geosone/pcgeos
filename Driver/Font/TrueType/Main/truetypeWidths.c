/*
 * truetypeWidths.c -- after truetypeWidths.asm: Implements a font driver
 * for: The TrueType outline fonts
 */
#include "../Adapter/ttwidths.h"

Boolean TrueTypeGenWidths(DriverRegs *r)
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
			    (TT_FontMatrix *) r->DR_bpPtr, TrueTypePointSize(gs),
			    gs->GS_fontAttr.FCA_width,
			    gs->GS_fontAttr.FCA_weight, fi, header, first,
			    toDo, (TT_FontMatrix *) r->DR_dsSi,
			    (byte) (gs->GS_fontFlags & FBF_IS_GREY),
			    variableHandle);
    r->DR_bx = h;
    return (Boolean) !h;
}
