/*
 * printcomPScriptDialog.c -- the UI routines of the PostScript driver
 * (the C port of printcomPScriptDialog.asm: UI/uiGetNoMain.asm,
 * uiGetOptions.asm, uiEval.asm, uiEvalDummyASF.asm, uiEvalIBM4019PS.asm).
 *
 * The print UI is not ported (Port32/PLAN.md): the device info has no
 * UI trees (PI_mainUI, PI_optionsUI 0) and no eval routine, so there is
 * no tree to return, evaluate or stuff.
 */
#include "printcom.h"

/* PrintGetMainUI (uiGetNoMain): no main UI, cx:dx 0 */
Boolean PrintGetMainUI(PState *ps, DriverRegs *r)
{
    (void) ps;
    r->DR_cx = 0;
    r->DR_dx = 0;
    return FALSE;
}

/* PrintGetOptionsUI: cx:dx = PI_optionsUI */
Boolean PrintGetOptionsUI(PState *ps, DriverRegs *r)
{
    r->DR_cx = OptrToHandle(ps->PS_deviceInfo->PI_optionsUI);
    r->DR_dx = OptrToChunk(ps->PS_deviceInfo->PI_optionsUI);
    return FALSE;
}

/* PrintEvalUI, PrintStuffUI: through PI_evalRoutine, which is none */
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
