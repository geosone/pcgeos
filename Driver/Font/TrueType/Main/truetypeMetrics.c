/*
 * truetypeMetrics.c -- after truetypeMetrics.asm: Routines for
 * generating character metrics.
 */
#include "../Adapter/ttwidths.h"
#include "../Adapter/ttmetrics.h"

Boolean TrueTypeCharMetrics(DriverRegs *r)
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
			      header, toDo, TrueTypePointSize(gs),
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
