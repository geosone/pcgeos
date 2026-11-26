/*
 * truetypeChars.c -- after truetypeChars.asm: This file contains
 * routines for generating individual characters.
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

Boolean TrueTypeGenChar(DriverRegs *r)
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
    TrueType_Gen_Chars((word) r->DR_dx, fb, TrueTypePointSize(gs),
		       gs->GS_fontAttr.FCA_width, gs->GS_fontAttr.FCA_weight,
		       fi, header, toDo, bitmapHandle, variableHandle);
    r->DR_esDi = MemDeref(fh);
    return FALSE;
}
