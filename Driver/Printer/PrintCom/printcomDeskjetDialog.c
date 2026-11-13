/*
 * printcomDeskjetDialog.c -- the UI routines of the DeskJet drivers (the
 * C port of printcomDeskjetDialog.asm: UI/uiGetNoMain.asm,
 * uiGetOptions.asm, uiEval.asm, uiEvalDeskjet.asm).
 *
 * The print UI is not ported (Port32/PLAN.md): the device info has no
 * UI trees and no eval routine (dmdevinfo2c.py), so there is no tree to
 * return, evaluate or stuff; the compression, the paper input and the
 * symbol set (PrintDriverUIData) are what the job's JP_printerData says.
 */
#include "printcom.h"

Boolean PrintGetMainUI(PState *ps, DriverRegs *r)
{
    (void) ps;
    r->DR_cx = 0;
    r->DR_dx = 0;
    return FALSE;
}

Boolean PrintGetOptionsUI(PState *ps, DriverRegs *r)
{
    r->DR_cx = OptrToHandle(ps->PS_deviceInfo->PI_optionsUI);
    r->DR_dx = OptrToChunk(ps->PS_deviceInfo->PI_optionsUI);
    return FALSE;
}

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
