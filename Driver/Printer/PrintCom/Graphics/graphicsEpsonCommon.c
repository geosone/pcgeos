/*
 * graphicsEpsonCommon.c -- PrSendGraphicControlCode (the C port of
 * graphicsEpsonCommon.asm): a graphics code and its column count.
 */
#include "printcom.h"

Boolean PrSendGraphicControlCode(PState *ps, const byte *code, word columns)
{
    if (SendCodeOut(ps, code) || PrintStreamWriteByte(ps, (byte) columns))
	return TRUE;
    return PrintStreamWriteByte(ps, (byte) (columns >> 8));
}
