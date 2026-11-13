/*
 * graphicsCitohCommon.c -- PrSendGraphicControlCode (the C port of
 * graphicsCitohCommon.asm): a graphics code and its column count as four
 * ASCII digits.
 */
#include "printcom.h"

Boolean PrSendGraphicControlCode(PState *ps, const byte *code, word columns)
{
    return SendCodeOut(ps, code) || HexToAsciiStreamWrite0(ps, columns, 4);
}
