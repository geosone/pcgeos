/*
 * uiGetMain.c -- after uiGetMain.asm: PrintGetMainUI.
 * Included by the printcom*.c files whose asm includes it.
 */
#include "printcom.h"

Boolean PrintGetMainUI(PState *ps, DriverRegs *r)
{
    r->DR_cx = OptrToHandle(ps->PS_deviceInfo->PI_mainUI);
    r->DR_dx = OptrToChunk(ps->PS_deviceInfo->PI_mainUI);
    return FALSE;
}
