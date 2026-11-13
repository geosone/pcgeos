/*
 * textInitFontPCL4.c -- the font of a page of the PCL 4 printers (the C
 * port of textInitFontPCL4.asm): URW Mono 12 (Courier 10 pitch), the font
 * manager started.
 */
#include "printcom.h"

void PrintSetURWMono12(PState *ps)
{
    ps->PS_curFont.FE_fontID = FID_DTC_URW_MONO;
    ps->PS_curFont.FE_size = 12;
    ps->PS_curOptFont.OFE_trackKern = 0;
}

Boolean PrInitFont(PState *ps)
{
    PrintSetURWMono12(ps);
    return FontInit(ps);
}
