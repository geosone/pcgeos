/*
 * toshiba24Styles.c -- the style routines of the Toshiba 24-pin driver
 * (after toshiba24Styles.asm, with the Styles/stylesSR*.c parts it
 * includes, as the asm includes Styles/stylesSR*.asm): a control code
 * each for condensed, NLQ, bold, italic, underline, shadow and double
 * width; none for the others.
 */
#include "printcom.h"
#include "printcomDriver.h"
#include "Styles/stylesSRCondensed.c"
#include "Styles/stylesSRNLQ.c"
#include "Styles/stylesSRBold.c"
#include "Styles/stylesSRItalic.c"
#include "Styles/stylesSRUnderline.c"
#include "Styles/stylesSRShadow.c"
#include "Styles/stylesSRDblWidth.c"

static Boolean None(PState *ps)
{
    (void) ps;
    return FALSE;
}

/* by bit: PTS_FUTURE (0) ... PTS_CONDENSED (15) */
PrintStyleRoutine *const printSetStyle[16] = {
    None, None, None, None, SetDblWidth, None, None, SetShadow, None,
    SetUnderline, SetItalic, SetBold, SetNLQ, None, None, SetCondensed
};
PrintStyleRoutine *const printResetStyle[16] = {
    None, None, None, None, ResetDblWidth, None, None, ResetShadow,
    None, ResetUnderline, ResetItalic, ResetBold, ResetNLQ, None, None,
    ResetCondensed
};
