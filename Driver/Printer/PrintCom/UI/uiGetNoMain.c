/*
 * uiGetNoMain.c -- after uiGetNoMain.asm: PrintGetMainUI.
 * Included by the printcom*.c files whose asm includes it.
 */
#include "printcom.h"

Boolean PrintGetMainUI(PState *ps, DriverRegs *r)
{
    (void) ps;
    r->DR_cx = 0;
    r->DR_dx = 0;
    return FALSE;
}
