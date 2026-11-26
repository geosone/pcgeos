/*
 * grpr9Styles.c -- the style routines of the IBM Graphics 9-pin driver
 * (after grpr9Styles.asm, with the Styles/stylesSR*.c parts it includes,
 * as the asm includes Styles/stylesSR*.asm): a control code each for
 * condensed, bold, overline, double width, NLQ, sub- and superscript and
 * underline; none for the others.
 */
#include "printcom.h"
#include "printcomDriver.h"
#include "Styles/stylesSRCondensed.c"
#include "Styles/stylesSRBold.c"
#include "Styles/stylesSROverline.c"
#include "Styles/stylesSRDblWidth.c"
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
    None, SetOverline, None, None, SetDblWidth, None, None, None, None,
    SetUnderline, None, SetBold, SetNLQ, SetSuperscript, SetSubscript,
    SetCondensed
};
PrintStyleRoutine *const printResetStyle[16] = {
    None, ResetOverline, None, None, ResetDblWidth, None, None, None,
    None, ResetUnderline, None, ResetBold, ResetNLQ, ResetSuperscript,
    ResetSubscript, ResetCondensed
};
