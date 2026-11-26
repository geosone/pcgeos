/*
 * uiEval.c -- after uiEval.asm: PrintEvalUI, PrintStuffUI.
 * Included by the printcom*.c files whose asm includes it.
 */
#include "printcom.h"

Boolean PrintEvalUI(PState *ps, DriverRegs *r)
{
    (void) ps;
    (void) r;
    return FALSE;
}

Boolean PrintStuffUI(PState *ps, DriverRegs *r)
{
    (void) ps;
    (void) r;
    return FALSE;
}
