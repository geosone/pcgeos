/*
 * quietjetStyles.c -- the style routines of the QuietJet driver (after
 * quietjetStyles.asm, with the Styles/stylesSR*.c parts it includes, as
 * the asm includes Styles/stylesSR*.asm): a control code each for NLQ,
 * bold and underline; none for the others.
 */
#include "printcom.h"
#include "printcomDriver.h"
#include "Styles/stylesSRNLQ.c"
#include "Styles/stylesSRUnderline.c"
#include "Styles/stylesSRBold.c"

static Boolean None(PState *ps)
{
    (void) ps;
    return FALSE;
}

/* by bit: PTS_FUTURE (0) ... PTS_CONDENSED (15) */
PrintStyleRoutine *const printSetStyle[16] = {
    None, None, None, None, None, None, None, None, None, SetUnderline,
    None, SetBold, SetNLQ, None, None, None
};
PrintStyleRoutine *const printResetStyle[16] = {
    None, None, None, None, None, None, None, None, None,
    ResetUnderline, None, ResetBold, ResetNLQ, None, None, None
};
