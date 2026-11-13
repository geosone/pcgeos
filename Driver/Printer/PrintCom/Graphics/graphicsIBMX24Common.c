/*
 * graphicsIBMX24Common.c -- PrSendGraphicControlCode (the C port of
 * graphicsIBMX24Common.asm): the IBM graphics code ESC [ g, its byte
 * count (the columns, three bytes each from mode 8 on, six from mode 13
 * on, and the mode byte) and the mode.  The driver's graphics codes are
 * the mode alone.
 */
#include "printcom.h"
#include "printcomDriver.h"

Boolean PrSendGraphicControlCode(PState *ps, const byte *code, word columns)
{
    byte mode = code[0];
    word n = columns;

    if (mode >= 8) {
	n = (word) (n * 3);
	if (mode >= 13)
	    n = (word) (n * 2);
    }
    n++;
    return SendCodeOut(ps, pr_codes_SetGraphics) ||
	PrintStreamWriteByte(ps, (byte) n) ||
	PrintStreamWriteByte(ps, (byte) (n >> 8)) ||
	PrintStreamWriteByte(ps, mode);
}
