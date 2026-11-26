/*
 * uiGetOptions.c -- after uiGetOptions.asm: PrintGetOptionsUI.
 * Included by the printcom*.c files whose asm includes it.
 */
#include "printcom.h"

Boolean PrintGetOptionsUI(PState *ps, DriverRegs *r)
{
    r->DR_cx = OptrToHandle(ps->PS_deviceInfo->PI_optionsUI);
    r->DR_dx = OptrToChunk(ps->PS_deviceInfo->PI_optionsUI);
    return FALSE;
}
