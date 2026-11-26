/*
 * printcomDeskjetDialog.c -- the UI routines of the DeskJet drivers (the
 * C port of printcomDeskjetDialog.asm: UI/uiGetNoMain.asm,
 * uiGetOptions.asm, uiEval.asm, uiEvalDeskjet.asm).
 *
 * The print UI is not ported (Port32/PLAN.md): the device info has no
 * UI trees and no eval routine (dmdevinfo2c), so there is no tree to
 * return, evaluate or stuff; the compression, the paper input and the
 * symbol set (PrintDriverUIData) are what the job's JP_printerData says.
 */
#include "printcom.h"
#include "UI/uiGetNoMain.c"
#include "UI/uiGetOptions.c"
#include "UI/uiEval.c"
