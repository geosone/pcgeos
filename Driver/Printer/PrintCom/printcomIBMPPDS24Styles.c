/*
 * printcomIBMPPDS24Styles.c -- the style routines of the IBM PPDS 24-pin
 * and the Canon 48 drivers (after printcomIBMPPDS24Styles.asm, with the
 * Styles/stylesSR*.c parts it includes, as the asm includes
 * Styles/stylesSR*.asm): a control code each for bold, double height and
 * width, italic, shadow, outline, NLQ, the scripts and underline; none
 * for condensed, overline, strike-thru, reverse, quad height and the
 * future bit.
 */
#include "printcom.h"
#include "printcomDriver.h"
#include "Styles/stylesSRBold.c"
#include "Styles/stylesSRDblHeight.c"
#include "Styles/stylesSRDblWidth.c"
#include "Styles/stylesSRItalic.c"
#include "Styles/stylesSRShadow.c"
#include "Styles/stylesSROutline.c"
#include "Styles/stylesSRNLQ.c"
#include "Styles/stylesSRSubSuperScript.c"
#include "Styles/stylesSRUnderline.c"

static Boolean None(PState *ps)
{
    (void) ps;
    return FALSE;
}

/* by bit: PTS_FUTURE (0) ... PTS_CONDENSED (15) */
PrintStyleRoutine *const printSetStyle[16] = {
    None, None, None, SetDblHeight, SetDblWidth, None, SetOutline,
    SetShadow, None, SetUnderline, SetItalic, SetBold, SetNLQ,
    SetSuperscript, SetSubscript, None
};
PrintStyleRoutine *const printResetStyle[16] = {
    None, None, None, ResetDblHeight, ResetDblWidth, None, ResetOutline,
    ResetShadow, None, ResetUnderline, ResetItalic, ResetBold, ResetNLQ,
    ResetSuperscript, ResetSubscript, None
};
