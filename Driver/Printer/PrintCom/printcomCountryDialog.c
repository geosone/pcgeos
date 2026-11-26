/*
 * printcomCountryDialog.c -- the UI routines of the dot matrix drivers
 * with the country choice (the C port of printcomCountryDialog.asm:
 * UI/uiGetNoMain.asm, uiGetOptions.asm, uiEval.asm,
 * uiEval012ASFCountry.asm).
 *
 * The print UI is not ported (Port32/PLAN.md): the device info has no
 * UI trees (PI_mainUI, PI_optionsUI 0) and no eval routine, so there is
 * no tree to return, evaluate or stuff; the paper path and the country
 * (PrintDriverUIData) are what the job's JP_printerData says.
 */
#include "printcom.h"
#include "UI/uiGetNoMain.c"
#include "UI/uiGetOptions.c"
#include "UI/uiEval.c"
