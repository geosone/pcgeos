/*
 * printcomPCL4Dialog.c -- the UI routines of the PCL 4 drivers (the C
 * port of printcomPCL4Dialog.asm: UI/uiGetMain.asm, uiGetOptions.asm,
 * uiEval.asm, uiEvalPCL4Simplex.asm, uiEvalPCL4Duplex.asm).
 *
 * The print UI is not ported (Port32/PLAN.md): the device info has no
 * UI trees and no eval routine (dmdevinfo2c), so there is no tree to
 * return, evaluate or stuff; the paper paths, the printer's memory and
 * what is deleted from it (PrintDriverUIData) are what the job's
 * JP_printerData says.
 */
#include "printcom.h"
#include "UI/uiGetMain.c"
#include "UI/uiGetOptions.c"
#include "UI/uiEval.c"
