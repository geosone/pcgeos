/*
 * printcomPaperOnlyDialog.c -- the UI routines of the dot matrix drivers
 * with the paper path choice only (the C port of
 * printcomPaperOnlyDialog.asm: UI/uiGetNoMain.asm, uiGetOptions.asm,
 * uiEval.asm, uiEval1ASF.asm), and of printcomDotMatrixDialog.asm
 * (uiEvalASF.asm).
 *
 * The print UI is not ported (Port32/PLAN.md): the device info has no
 * UI trees (PI_mainUI, PI_optionsUI 0) and no eval routine, so there is
 * no tree to return, evaluate or stuff; the paper path
 * (PrintDriverUIData) is what the job's JP_printerData says.
 */
#include "printcom.h"
#include "UI/uiGetNoMain.c"
#include "UI/uiGetOptions.c"
#include "UI/uiEval.c"
