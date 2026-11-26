/*
 * printcomIBMStyles.c -- the style routines of the IBM style drivers
 * (after printcomIBMStyles.asm, with the Styles/stylesSR*.c parts it
 * includes, as the asm includes Styles/stylesSR*.asm): those of the
 * Epson drivers and overline; none for strike-thru, shadow, outline,
 * reverse, quad height and the future bit.
 */
#include "printcom.h"
#include "printcomDriver.h"
#include "Styles/stylesSRCondensed.c"
#include "Styles/stylesSRBold.c"
#include "Styles/stylesSRItalic.c"
#include "Styles/stylesSROverline.c"
#include "Styles/stylesSRDblWidth.c"
#include "Styles/stylesSRDblHeight.c"
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
    None, SetOverline, None, SetDblHeight, SetDblWidth, None, None, None,
    None, SetUnderline, SetItalic, SetBold, SetNLQ, SetSuperscript,
    SetSubscript, SetCondensed
};
PrintStyleRoutine *const printResetStyle[16] = {
    None, ResetOverline, None, ResetDblHeight, ResetDblWidth, None,
    None, None, None, ResetUnderline, ResetItalic, ResetBold, ResetNLQ,
    ResetSuperscript, ResetSubscript, ResetCondensed
};
