/*
 * printcomPCL4Dialog.c -- the UI routines of the PCL 4 drivers (the C
 * port of printcomPCL4Dialog.asm: UI/uiGetMain.asm, uiGetOptions.asm,
 * uiEval.asm, uiEvalPCL4Simplex.asm, uiEvalPCL4Duplex.asm).
 *
 * The print UI is not ported (Port32/PLAN.md): the device info has no
 * UI trees and no eval routine (dmdevinfo2c.py), so there is no tree to
 * return, evaluate or stuff; the paper paths, the printer's memory and
 * what is deleted from it (PrintDriverUIData) are what the job's
 * JP_printerData says.
 */
#include "printcom.h"

Boolean PrintGetMainUI(PState *ps, DriverRegs *r)
{
    r->DR_cx = OptrToHandle(ps->PS_deviceInfo->PI_mainUI);
    r->DR_dx = OptrToChunk(ps->PS_deviceInfo->PI_mainUI);
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
