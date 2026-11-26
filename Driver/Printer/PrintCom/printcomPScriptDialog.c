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
#include "UI/uiGetNoMain.c"
#include "UI/uiGetOptions.c"
#include "UI/uiEval.c"
